/* implement a live web server connection so browsers can see and control HamClock.
 *
 * we listen to liveweb_port for live.html or web socket upgrades.
 *
 * Browser displays entire HamClock frame buffer. Complete frame is sent initially then only the
 * pixels that change.
 *
 * N.B. this server-side code must work in concert with client-side code in liveweb-html.cpp.
 *
 * Not implemented on ESP because reading pixels is far too slow and because it does not have enough memory
 *   to keep a reference image.
 */


#include "HamClock.h"


// public
time_t last_live;                                       // last live update; public for wifi.cpp
bool no_web_touch;                                      // disable web touch events
bool liveweb_fs_ready;                                  // set when ok to send fullscreen command

#if defined(_IS_UNIX)

#include "ws.h"                                         // web socket library


// import png writer -- complete implementation in a header file -- amazing.
// https://github.com/nothings/stb
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"


// trace level
static int live_verbose = 0;                            // more chatter if > 0

// our public endpoint port
int liveweb_port = LIVEWEB_PORT;                        // server port -- can be changed with -w


// png format is 3 bytes per pixel
#define LIVE_BYPPIX     3                               // bytes per pixel
#define LIVE_NPIX       (BUILD_H*BUILD_W)               // pixels per complete image
#define LIVE_NBYTES     (LIVE_NPIX*LIVE_BYPPIX)         // bytes per complete image
#define LIVE_RBYTES     (BUILD_W*LIVE_BYPPIX)           // bytes per row
#define COMP_RGB        3                               // composition request code for RGB pixels


// list of last complete scene on browser for each web socket
typedef struct {
    ws_cli_conn_t *client;                              // opaque pointer unique to each connection
    uint8_t *pixels;                                    // this client's current display image
} SessionInfo;
static SessionInfo *si_list;                            // malloced list
static int si_n;                                        // n malloced
static pthread_mutex_t si_lock = PTHREAD_MUTEX_INITIALIZER;     // atomic updates

#if defined(__GNUC__)
static void bye (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));
#else
static void bye (const char *fmt, ...);
#endif


/* we can't use fatalError because the main thread will keep running and quickly obscure it.
 * N.B. we assume final message will include trailing \n
 */
static void bye (const char *fmt, ...)
{
    Serial.printf ("LIVE: ");

    va_list ap;
    va_start (ap, fmt);
    vprintf (fmt, ap);
    va_end (ap);

    exit(1);
}


/* stbi_write_png_to_func helper to write the given array to the given ws_cli_conn_t in context.
 */
static void wifiSTBWrite_helper (void *context, void *data, int size)
{
    ws_cli_conn_t *client = (ws_cli_conn_t *)context;
    int n_sent = ws_sendframe_bin (client, (const char *) data, size);
    if (n_sent != size)
        Serial.printf ("LIVE: client %s: wrong png write len: %d != %d\n", ws_getaddress(client), n_sent, size);
    if (live_verbose > 1) {
        Serial.printf ("LIVE: sent image %d bytes\n", size);
        if (live_verbose > 2) {
            FILE *fp = fopen ("/tmp/live.png", "w");
            fwrite (data, size, 1, fp);
            fclose(fp);
        }
    }
}

/* return the pixels pointer for the existing client, else NULL.
 * pixels pointer is safe to use outside si_lock and even if si_list is later realloced (and hence moves).
 */
static uint8_t *getSIPixels (ws_cli_conn_t *client)
{
    // protect list while manipulating -- N.B. unlock before returning!
    pthread_mutex_lock (&si_lock);

    // scan si_list for client
    SessionInfo *found_sip = NULL;
    for (int i = 0; i < si_n; i++) {
        if (si_list[i].client == client) {
            found_sip = &si_list[i];
            break;
        }
    }

    // capture pixels address 
    uint8_t *pixels = NULL;
    if (found_sip)
        pixels = found_sip->pixels;
    else {
        if (ws_close_client (client) < 0)
            Serial.printf ("LIVE: client %s: failed to close after missing pixels\n", ws_getaddress(client));
        else
            Serial.printf ("LIVE: client %s: closed because missing pixels\n", ws_getaddress(client));
    }

    // unlock
    pthread_mutex_unlock (&si_lock);

    // return result
    return (pixels);
}


/* send difference between client's last known screen image and the current image,
 * then store current image back in client's SessionInfo.
 */
static void updateExistingClient (ws_cli_conn_t *client)
{
    // curious how long these steps take
    struct timeval tv0;
    gettimeofday (&tv0, NULL);

    // find client's pixels
    uint8_t *pixels = getSIPixels(client);
    if (!pixels)
        return;

    // make a copy of client's last known image
    uint8_t *img_client = (uint8_t *) malloc (LIVE_NBYTES);
    if (!img_client)
        bye ("No memory for LIVE update\n");
    memcpy (img_client, pixels, LIVE_NBYTES);

    // replace clients's pixels with our current screen contents
    if (!tft.getRawPix (pixels, LIVE_NPIX))
        bye ("getRawPix for update failed\n");
    uint8_t *img_now = pixels;                      // better name

    if (live_verbose > 1) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        Serial.printf ("LIVE: client %s: copying img and reading new pixels took %ld usec\n", ws_getaddress(client),
                                TVDELUS (tv0,tv1));
    }

    // we only send small regions that have changed since previous.
    // image is divided into fixed sized blocks and those which have changed are coalesced into regions
    // of height one block but variable length. these are collected and sent as one image of height one
    // block preceded by a header defining the location and size of each region. the coordinates and
    // length of a region are in units of blocks, not pixels, to reduce each value's size to one byte
    // each in the header. smaller regions are more efficient but the coords must fit in 8 bit header value.
    #define BLOK_W      (BUILD_W>1600?16:8)             // pixels wide
    #define BLOK_H      8                               // pixels high
    #define BLOK_NCOLS  (BUILD_W/BLOK_W)                // blocks in each row over entire image
    #define BLOK_NROWS  (BUILD_H/BLOK_H)                // blocks in each col over entire image
    #define BLOK_NPIX   (BLOK_W*BLOK_H)                 // size of 1 block, pixels
    #define BLOK_NBYTES (BLOK_NPIX*LIVE_BYPPIX)         // size of 1 block, bytes
    #define BLOK_WBYTES (BLOK_W*LIVE_BYPPIX)            // width of 1 block, bytes
    #define MAX_REGNS   (BLOK_NCOLS*BLOK_NROWS)         // worse case number of regions
    #if BLOK_NCOLS > 255                                // insure fits into uint8_t
        #error too many block columns
    #endif
    #if BLOK_NROWS > 255                                // insure fits into uint8_t
        #error too many block rows
    #endif
    #if MAX_REGNS > 65535                               // insure fits into uint16_t
        #error too many live regions
    #endif

    // time block creation
    gettimeofday (&tv0, NULL);

    // set header to location and length of each changed region.
    typedef struct {
        uint8_t x, y, l;                                // region location and length in units of blocks
    } RegnLoc;
    RegnLoc locs[MAX_REGNS];                            // room for max number of header region entries
    uint16_t n_regns = 0;                               // n regions defined so far
    int n_bloks = 0;                                    // n blocks within all regions so far

    // build locs by checking each region for change across then down
    for (int ry = 0; ry < BLOK_NROWS; ry++) {

        // pre-check an image band all the way across BLOK_COLS hi, skip entirely if no change anywhere
        int band_start = ry*LIVE_BYPPIX*BLOK_H*BUILD_W;
        if (memcmp (&img_now[band_start], &img_client[band_start], LIVE_BYPPIX*BLOK_H*BUILD_W) == 0)
            continue;

        // something changed, scan across this band checking each block
        locs[n_regns].l = 0;                            // init n contiguous blocks that start here
        for (int rx = 0; rx < BLOK_NCOLS; rx++) {
            int blok_start = band_start + rx*BLOK_WBYTES;
            uint8_t *now0 = &img_now[blok_start];       // first pixel in this block of current image
            uint8_t *pre0 = &img_client[blok_start];    // first pixel in this block of image in client

            // check each row of this block for any change, start or add to region 
            bool blok_changed = false;                  // set if any changed pixels in this block
            for (int rr = 0; rr < BLOK_H; rr++) {
                if (memcmp (now0+rr*LIVE_BYPPIX*BUILD_W, pre0+rr*LIVE_BYPPIX*BUILD_W, BLOK_WBYTES) != 0) {
                    blok_changed = true;
                    break;
                }
            }

            // add to or start new region
            if (blok_changed) {
                if (locs[n_regns].l == 0) {
                    locs[n_regns].x = rx;
                    locs[n_regns].y = ry;
                }
                locs[n_regns].l++;
                n_bloks++;
            } else {
                if (locs[n_regns].l > 0) {
                    n_regns++;
                    locs[n_regns].l = 0;
                }
            }
        }

        // add last region too if started
        if (locs[n_regns].l > 0) {
            n_regns++;
        }
    }

    // now create one wide image containing each region as a separate sprite.
    // remember each region must work as a separate image of size lx1 blocks.
    uint8_t *chg_regns = (uint8_t*) malloc (n_bloks * BLOK_NBYTES);
    uint8_t *chg0 = chg_regns;
    for (int ry = 0; ry < BLOK_H; ry++) {
        for (int i = 0; i < n_regns; i++) {
            RegnLoc *rp = &locs[i];
            uint8_t *now0 = &img_now[BUILD_W*LIVE_BYPPIX*(ry+BLOK_H*rp->y) + BLOK_WBYTES*rp->x];
            memcpy (chg0, now0, BLOK_WBYTES*rp->l);
            chg0 += BLOK_WBYTES*rp->l;
        }
    }
    if (n_bloks != (chg0-chg_regns)/BLOK_NBYTES)        // assert
        bye ("live regions %d != %d\n", n_bloks, (int)((chg0-chg_regns)/BLOK_NBYTES));

    if (live_verbose > 1) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        Serial.printf ("LIVE: client %s: built %d regions from %d blocks in %ld usec\n",
                        ws_getaddress(client), n_regns, n_bloks, TVDELUS (tv0, tv1));
    }

    // time header creation and png write
    gettimeofday (&tv0, NULL);

    // build 4-byte header followed by x,y,l of each of n regions in units of blocks.
    unsigned hdr_l = 4+3*n_regns;
    StackMalloc hdr_mem(hdr_l);
    uint8_t *hdr = (uint8_t *) hdr_mem.getMem();
    hdr[0] = BLOK_W;                            // block width, pixels
    hdr[1] = BLOK_H;                            // block height, pixels
    hdr[2] = n_regns >> 8;                      // n regions, MSB
    hdr[3] = n_regns & 0xff;                    // n regions, LSB
    for (int i = 0; i < n_regns; i++) {
        hdr[4+3*i] = locs[i].x;
        hdr[5+3*i] = locs[i].y;
        hdr[6+3*i] = locs[i].l;
        if (live_verbose > 2)
            Serial.printf ("   %d,%d %dx%d\n", locs[i].x*BLOK_W, locs[i].y*BLOK_H, locs[i].l*BLOK_W, BLOK_H);
    }

    // always send header
    unsigned n_hdrsent = ws_sendframe_bin (client, (const char *) hdr, hdr_l);
    if (n_hdrsent != hdr_l)
        Serial.printf ("LIVE: client %s: wrong header write %u != %d\n", ws_getaddress(client), n_hdrsent, hdr_l);

    // followed by one image containing one column BLOK_W wide of all changed regions
    stbi_write_png_to_func (wifiSTBWrite_helper, client, BLOK_W*n_bloks, BLOK_H,
                            COMP_RGB, chg_regns, BLOK_WBYTES*n_bloks);

    if (live_verbose > 1) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        Serial.printf ("LIVE: client %s: write hdr %d bytes and update took %ld usec\n",
                        ws_getaddress(client), hdr_l, TVDELUS (tv0,tv1));
    }

    if (live_verbose > 1)
        Serial.printf ("LIVE: client %s: sent update with %d regions %d blocks\n",
                        ws_getaddress(client), n_regns, n_bloks);

    // finished with temps
    free (chg_regns);
    free (img_client);
}

/* capture fresh screen image for client and send.
 */
static void sendClientPNG (ws_cli_conn_t *client)
{
    // get this client's current pixels array
    uint8_t *pixels = getSIPixels(client);
    if (!pixels)
        return;

    // fresh capture
    if (!tft.getRawPix (pixels, LIVE_NPIX))
        bye ("getRawPix for png failed\n");

    // convert image to and send as png
    stbi_write_png_compression_level = 2;       // faster with hardly any increase in size
    stbi_write_png_to_func (wifiSTBWrite_helper, client, BUILD_W, BUILD_H, COMP_RGB, pixels, LIVE_RBYTES);

    if (live_verbose)
        Serial.printf ("LIVE: client %s: sent full PNG\n", ws_getaddress(client));
}

/* send message as to whether or not display in full screen
 */
static void sendFullScreen(ws_cli_conn_t *client)
{
    char fs[3] = {99, 91, getX11FullScreen()};          // see liveweb-html
    ws_sendframe_bin (client, fs, 3);
}

/* client running liveweb-html.cpp is asking for a complete screen capture as png file.
 */
static void getLivePNG (ws_cli_conn_t *client, char args[], size_t args_len)
{
    (void)args;
    (void)args_len;

    sendClientPNG (client);
}

/* client running liveweb-html.cpp is asking for incremental screen update.
 * we also send fullscreen if ready from setup.cpp. must send continuously because it only
 *   works a short while after a GUI interaction and we don't know when those will occur.
 */
static void getLiveUpdate (ws_cli_conn_t *client, char args[], size_t args_len)
{
    (void)args;
    (void)args_len;

    if (liveweb_fs_ready && getX11FullScreen())
        sendFullScreen (client);

    updateExistingClient (client);
}

/* client running liveweb-html.cpp sending us a character to act on as if typed locally.
 */
static void setLiveChar (ws_cli_conn_t *client, char args[], size_t args_len)
{
    (void)client;

    // ignore if no_web_touch
    if (no_web_touch) {
        Serial.printf ("Ignoring set_char\n");
        return;
    }

    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "char";

    // parse
    if (!parseWebCommand (wa, args, args_len)) {

        Serial.printf ("LIVE: set_char garbled: %s\n", args);

    } else if (!wa.found[0]) {

        Serial.printf ("LIVE: set_char missing char\n");

    } else {

        // engage the given character as if typed

        const char *str = wa.value[0];
        int strl = strlen(str);
        char c = 0;
        if (strl > 1) {
            // one of a named set
            if (strcmp (str, "Escape") == 0)
                c = 27;
            else if (strcmp (str, "Enter") == 0 || strcmp (str, "Return") == 0)
                c = '\n';
            else if (strcmp (str, "Tab") == 0)
                c = '\t';
            else if (strcmp (str, "Backspace") == 0 || strcmp (str, "Delete") == 0)
                c = '\b';
            else if (strcmp (str, "Space") == 0)
                c = ' ';
            else
                Serial.printf ("LIVE: Unknown char name: %s\n", str);
        } else {
            // literal
            if (strl != 1 || !isprint(str[0]))
                Serial.printf ("LIVE: Unknown char 0x%02X\n", str[0]);
            else
                c = str[0];
        }

        if (c) {

            // insert into getChar queue
            tft.putChar(c);
            if (live_verbose)
                Serial.printf ("LIVE: set_char %d %c\n", c, c);
        }
    }
}

/* client running liveweb-html.cpp sending us a touch event.
 */
static void setLiveTouch (ws_cli_conn_t *client, char args[], size_t args_len)
{
    (void)client;

    // ignore if no_web_touch
    if (no_web_touch) {
        Serial.printf ("Ignoring set_touch\n");
        return;
    }

    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "x";
    wa.name[wa.nargs++] = "y";
    wa.name[wa.nargs++] = "hold";

    // parse
    if (!parseWebCommand (wa, args, args_len)) {

        Serial.printf ("LIVE: set_touch garbled: %s\n", args);

    } else if (!wa.found[0] || !wa.found[1]) {

        Serial.printf ("LIVE: set_touch missing x,y\n");

    } else {

        // require x and y within screen size
        int x = atoi(wa.value[0]);
        int y = atoi(wa.value[1]);
        if (x < 0 || x >= tft.width() || y < 0 || y >= tft.height()) {
            Serial.printf ("LIVE: require 0 .. %d .. %d and 0 .. %d .. %d\n", x, tft.width()-1, y, tft.height()-1);

        } else {

            // hold is optional
            int h = wa.found[2] ? atoi(wa.value[2]) : 0;

            // inform checkTouch() to use wifi_tt_s; it will reset
            wifi_tt_s.x = x;
            wifi_tt_s.y = y;
            wifi_tt = h ? TT_HOLD : TT_TAP;

            if (live_verbose)
                Serial.printf ("LIVE: set_touch %d %d %d\n", wifi_tt_s.x, wifi_tt_s.y, wifi_tt);
        }
    }
}

/* client running liveweb-html.cpp sending us a mouse motion event (buttons use set_touch()).
 */
static void setLiveMouse (ws_cli_conn_t *client, char args[], size_t args_len)
{
    (void)client;

    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "x";
    wa.name[wa.nargs++] = "y";

    // parse
    if (!parseWebCommand (wa, args, args_len)) {

        Serial.printf ("LIVE: set_mouse garbled: %s\n", args);

    } else if (!wa.found[0] || !wa.found[1]) {

        Serial.printf ("LIVE: set_mouse missing x,y\n");

    } else {

        // set
        int x = atoi(wa.value[0]);
        int y = atoi(wa.value[1]);
        tft.setMouse (x, y);
        if (live_verbose)
            Serial.printf ("LIVE: set_mouse %d %d\n", x, y);
            
    }
}

/* send live.html to browser on the given FILE *
 */
static void sendLiveHTML (FILE *sockfp)
{
    // send proper header
    fprintf (sockfp, "HTTP/1.0 200 OK\r\n");
    fprintf (sockfp, "Content-Type: text/html; charset=us-ascii\r\n");
    fprintf (sockfp, "Connection: close\r\n\r\n");

    // send page
    fprintf (sockfp, "%s\r\n", live_html);

    if (live_verbose)
        Serial.printf ("LIVE: sent live.html\n");
}

/* called when browser wants favicon.ico
 */
static void sendLiveFavicon (FILE *sockfp)
{
    // send the web page header
    fprintf (sockfp, "HTTP/1.0 200 OK\r\n");
    fprintf (sockfp, "Content-Type: image/x-icon\r\n");
    fprintf (sockfp, "Connection: close\r\n\r\n");

    // send icon
    writeFavicon (sockfp);

    if (live_verbose)
        Serial.printf ("LIVE: sent favicon\n");
}

/* callback when browser asks for a new websocket connection.
 * assign a fresh si_list for keeping track of the pixels for the given client.
 */
static void ws_onopen(ws_cli_conn_t *client)
{
    // protect list while manipulating -- N.B. unlock before returning!
    pthread_mutex_lock (&si_lock);

    Serial.printf ("LIVE: client %s: new websocket request\n", ws_getaddress(client));

    // scan for unused entry to reuse
    SessionInfo *new_sip = NULL;
    for (int i = 0; i < si_n; i++) {
        SessionInfo *sip = &si_list[i];
        if (!sip->client) {
            new_sip = sip;
            if (new_sip->pixels) {
                free (new_sip);
                new_sip = NULL;
            }
            break;
        }
    }

    // grow list if can't find one to reuse
    if (!new_sip) {
        si_list = (SessionInfo *) realloc (si_list, (si_n + 1) * sizeof(SessionInfo));
        if (!si_list)
            bye ("No memory for new live session info %d\n", si_n);
        new_sip = &si_list[si_n++];
        memset (new_sip, 0, sizeof (*new_sip));
    }

    // init, including memory for pixels but don't capture until client asks for them
    new_sip->client = client;
    new_sip->pixels = (uint8_t *) malloc (LIVE_NBYTES);
    if (!new_sip->pixels)
        bye ("No memory for new live session pixels\n");

    // ok
    pthread_mutex_unlock (&si_lock);
}

/* callback when browser closes the given websocket connection.
 */
static void ws_onclose (ws_cli_conn_t *client)
{
    Serial.printf ("LIVE: client %s: disconnected\n", ws_getaddress(client));

    // remove from si_list
    for (int i = 0; i < si_n; i++) {
        SessionInfo *sip = &si_list[i];
        if (sip->client == client) {
            sip->client = NULL;
            if (sip->pixels) {
                free (sip->pixels);
                sip->pixels = NULL;
            }
            return;
        }
    }

    // if get here, client was not found in si_list
    Serial.printf ("LIVE: client %s: disappeared after closing websocket\n", ws_getaddress(client));
}

/* callback when browser sends us a message on a websocket
 */
static void ws_onmessage (ws_cli_conn_t *client, const unsigned char *msg, uint64_t size, int type)
{
    // list of core commands
    static struct {
        const char *cmd_name;
        void (*cmd_fp) (ws_cli_conn_t *, char [], size_t);
    } commands[] = {
        // list in rough order of decreasing usage
        {"get_live.bin?", getLiveUpdate},
        {"set_mouse?",    setLiveMouse},
        {"set_touch?",    setLiveTouch},
        {"set_char?",     setLiveChar},
        {"get_live.png?", getLivePNG},
    };

    // msg as null-terminated string cmd
    StackMalloc cmd_mem(size+1);
    char *cmd = (char *) cmd_mem.getMem();
    memcpy (cmd, msg, size);
    cmd[size] = 0;

    // run the matching command, if any
    bool found = false;
    for (int i = 0; !found && i < NARRAY(commands); i++) {
        const char *cmd_name = commands[i].cmd_name;
        size_t cmd_name_len = strlen(cmd_name);
        if (strncmp (cmd, cmd_name, cmd_name_len) == 0) {
            if (live_verbose > 1)
                Serial.printf ("LIVE: running %s\n", cmd);
            char *args = cmd + cmd_name_len;
            (*commands[i].cmd_fp) (client, args, strlen(args));
            found = true;
        }
    }

    // check
    if (!found)
        Serial.printf ("LIVE: ignoring unknown ws command: %s\n", cmd);
}

/* called when we receive a "normal" client message, ie, one that is not from a web socket.
 * sockfp is prepared for writing and is positioned just after the header. caller will close, not us.
 */
static void ws_not (FILE *sockfp, const char *header)
{
    // first line should be the GET containing the desired page
    char fn[50];
    if (sscanf (header, "GET /%49s", fn) != 1) {
        Serial.printf ("LIVE: expecting GET http header but got: '%s'\n", header);
        return;
    }

    if (live_verbose)
        Serial.printf ("LIVE: ws_not GET %s\n", fn);

    // dispatch according to GET file
    if (strcmp (fn, "live.html") == 0)
        sendLiveHTML (sockfp);
    else if (strcmp (fn, "favicon.ico") == 0)
        sendLiveFavicon (sockfp);
    else {
        Serial.printf ("LIVE: unknown GET %s\n", fn);
        fprintf (sockfp, "HTTP/1.0 400 Bad request\r\n");
        fprintf (sockfp, "Content-Type: text/plain; charset=us-ascii\r\n");
        fprintf (sockfp, "Connection: close\r\n\r\n");
        fprintf (sockfp, "%s: not found\r\n", fn);
    }
}

/* called in two ways:
 *   verbose false: really start, be silent if ok, else call bye if trouble
 *   verbose true:  already running so just call tftMsg with status similar to initWebServer().
 */
void initLiveWeb (bool verbose)
{
    if (verbose) {

        // just report

        if (liveweb_port > 0)
            tftMsg (verbose, 0, "Live Web server on port %d", liveweb_port);
        else
            tftMsg (verbose, 0, "Live Web server is disabled");

    } else {

        // handle all write errors inline
        signal (SIGPIPE, SIG_IGN);

        // actually start stuff unless not wanted
        if (liveweb_port < 0) {
            if (live_verbose)
                Serial.printf ("LIVE: live web is disabled\n");
        } else {
            // start websocket server
            struct ws_events evs;
            evs.onopen    = ws_onopen;
            evs.onclose   = ws_onclose;
            evs.onmessage = ws_onmessage;
            evs.onnonws   = ws_not;
            ws_socket (&evs, liveweb_port, 1, 1000);
            if (live_verbose)
                Serial.printf ("LIVE: started server thread on port %d\n", liveweb_port);
        }
    }
}

#endif // _IS_UNIX
