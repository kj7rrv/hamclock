/* implement a live web server connection so browsers can see and control HamClock.
 *
 * Browser always displays entire HamClock frame buffer. Complete frame is sent initially then only
 * changed pixels. Each broweser connection sends a unique session id used to manage its current
 * display contents. Browser requests for frame updates are queued and serviced in fifo order.
 *
 * N.B. this server-side code must work in concert with client-side code in liveweb-html.cpp.
 *
 * Not implemented on ESP because reading pixels is far too slow and because it does not have enough memory
 *   to keep a reference image.
 */


#include "HamClock.h"


// public
time_t last_live;                                       // last live update; public for wifi.cpp


#if defined(_IS_UNIX)



// import png writer -- complete implementation in a header file -- amazing.
// https://github.com/nothings/stb
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// trace level
static int live_verbose = 0;                            // more chatter if > 0

// our public endpoint port, listener and n threads to use
int liveweb_port = LIVEWEB_PORT;                        // server port -- can be changed with -e
static WiFiServer *liveweb_server;

// png format is 3 bytes per pixel
#define LIVE_BYPPIX     3                               // bytes per pixel
#define LIVE_NPIX       (BUILD_H*BUILD_W)               // pixels per complete image
#define LIVE_NBYTES     (LIVE_NPIX*LIVE_BYPPIX)         // bytes per complete image
#define LIVE_RBYTES     (BUILD_W*LIVE_BYPPIX)           // bytes per row
#define COMP_RGB        3                               // composition request code for RGB pixels

// list of last complete scene on browser for each sid.
typedef long sid_t;                                     // sid type -- large enough for liveweb-html.cpp
typedef long ustime_t;                                  // microsecond time
typedef struct {
    sid_t sid;                                          // client's session id
    uint8_t *pixels;                                    // client's current display image
    ustime_t last_used;                                 // used to detect clients that have disappeared
} SessionInfo;
static SessionInfo *si_list;                            // malloced list
static int si_n;                                        // n malloced
static pthread_mutex_t si_lock = PTHREAD_MUTEX_INITIALIZER;     // atomic updates
#define MAX_SI_AGE      30                              // reuse entry if not heard from this long, secs

// queue of pending work for thread pool
typedef struct {
    sid_t sid;                                          // session id wanting service
    WiFiClient client;                                  // connection
    bool wants_png;                                     // whether wants full png or incremental update
    ustime_t time_added;                                // used to implement fifo behavior
    bool pending;                                       // reset when finished for later reuse
} WorkElement;
static WorkElement *wq_list;                            // malloced list
static int wq_n;                                        // n malloced
static pthread_mutex_t wq_lock = PTHREAD_MUTEX_INITIALIZER;     // atomic updates
static pthread_cond_t wq_cond = PTHREAD_COND_INITIALIZER;       // queue extraction


/* we can't use fatalError because the main thread will keep running and quickly obscure it.
 * N.B. we assume final message will include trailing \n
 */
static void bye (const char *fmt, ...)
{
    printf ("LIVE: ");

    va_list ap;
    va_start (ap, fmt);
    vprintf (fmt, ap);
    va_end (ap);

    exit(1);
}

/* stbi_write_png_to_func helper to write the given array to the given WiFiClient
 */
static void wifiSTBWrite_helper (void *context, void *data, int size)
{
    ((WiFiClient*)context)->write ((uint8_t*)data, size);
    if (live_verbose > 1) {
        printf ("LIVE: sent image %d bytes\n", size);
        if (live_verbose > 2) {
            FILE *fp = fopen ("/tmp/live.png", "w");
            fwrite (data, size, 1, fp);
            fclose(fp);
        }
    }
}

/* get time now to microsecond level
 */
static ustime_t getUsNow()
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (tv.tv_sec * 1000000 + tv.tv_usec);
}

/* assign a fresh SessionInfo for keeping track of the pixels for the given sid.
 */
static void addNewSI (const sid_t &sid)
{
    // protect list while manipulating -- N.B. unlock before returning!
    pthread_mutex_lock (&si_lock);

    ustime_t now = getUsNow();

    // scan for possible reuse, cleaning up and logging along the way
    printf ("LIVE: %d SessionInfo report:\n", si_n);
    int n_unused = 0;
    SessionInfo *new_sip = NULL;
    for (int i = 0; i < si_n; i++) {
        SessionInfo *sip = &si_list[i];
        int age_s = (now - sip->last_used)/1000000;         // usec to sec
        if (sip->sid == sid) {
            printf ("  %12ld resuming after %d secs idle\n", sip->sid, age_s);
            new_sip = sip;
        } else if (sip->pixels) {
            if (age_s > MAX_SI_AGE) {
                if (!new_sip) {
                    printf ("  %12ld %d secs idle => reassigning to %ld\n", sip->sid, age_s, sid);
                    new_sip = sip;
                } else {
                    printf ("  %12ld idle %d secs => freeing\n", sip->sid, age_s);
                    free (sip->pixels);
                    sip->pixels = NULL;
                    sip->last_used = 0;
                }
            } else {
                if (age_s < 2)
                    printf ("  %12ld active\n", sip->sid);
                else
                    printf ("  %12ld idle for %d secs\n", sip->sid, age_s);
            }
        } else {
            if (!new_sip) {
                printf ("  %12ld unused => reassigning to %ld\n", sip->sid, sid);
                new_sip = sip;
            } else {
                n_unused++;
            }
        }
    }
    printf ("  %d unused\n", n_unused);

    // grow list if can't find one to reuse
    if (!new_sip) {
        si_list = (SessionInfo *) realloc (si_list, (si_n + 1) * sizeof(SessionInfo));
        if (!si_list)
            bye ("No memory for new live session info %d\n", si_n);
        new_sip = &si_list[si_n++];
        memset (new_sip, 0, sizeof (*new_sip));
        printf ("LIVE: new sid %ld\n", sid);
    }

    // init, including memory for pixels
    new_sip->pixels = (uint8_t *) realloc (new_sip->pixels, LIVE_NBYTES);       // noop if already alloced
    if (!new_sip->pixels)
        bye ("No memory for new live session pixels\n");
    new_sip->sid = sid;
    new_sip->last_used = now;

    // ok
    pthread_mutex_unlock (&si_lock);
}

/* return the pixels pointer for the existing sid, else NULL.
 * pointer is safe to use outside si_lock and even if si_list is later realloced (and hence moves).
 * we also guard against mutiple entries for the same sid.
 */
static uint8_t *getSIPixels (const sid_t &sid)
{
    // protect list while manipulating -- N.B. unlock before returning!
    pthread_mutex_lock (&si_lock);

    // scan for sid, look for dups and missing pixels along the way
    SessionInfo *found_sip = NULL;
    for (int i = 0; i < si_n; i++) {
        if (si_list[i].sid == sid) {
            if (found_sip) {
                printf ("LIVE: multiple ambiguous session entries for sid %ld\n", sid);
                found_sip = NULL;
                break;
            }
            if (!si_list[i].pixels) {
                printf ("LIVE: found entry but no pixels for sid %ld\n", sid);
                found_sip = NULL;
                break;
            }
            found_sip = &si_list[i];
        }
    }

    // capture pixels address and update used time if found
    uint8_t *pixels = NULL;
    if (found_sip) {
        found_sip->last_used = getUsNow();
        pixels = found_sip->pixels;
    }

    // unlock
    pthread_mutex_unlock (&si_lock);

    // return result
    return (pixels);
}

/* queue a new work element and notify thread pool of more work available.
 */
static void queueNewWE (const WiFiClient &client, const sid_t &sid, bool wants_png)
{
    // get exclusive work queue control -- N.B. must unlock before returning
    pthread_mutex_lock (&wq_lock);

    // first search for an unused wq_list entry
    WorkElement *new_wep = NULL;
    for (int i = 0; i < wq_n && !new_wep; i++) {
        WorkElement *wep = &wq_list[i];
        if (!wep->pending)
            new_wep = wep;
    }

    // nothing available so grow list
    if (!new_wep) {
        wq_list = (WorkElement *) realloc (wq_list, (wq_n + 1) * sizeof(WorkElement));
        if (!wq_list)
            bye ("LIVE: no memory for more work %d\n", wq_n);
        new_wep = &wq_list[wq_n++];
        printf ("LIVE: wq_list grown to %d\n", wq_n);
    }

    // set up work q entry
    new_wep->sid = sid;
    new_wep->client = client;
    new_wep->wants_png = wants_png;
    new_wep->time_added = getUsNow();
    new_wep->pending = true;

    // inform thread pool of new work -- worker thread will close client
    pthread_cond_signal (&wq_cond);
    pthread_mutex_unlock (&wq_lock);
}

/* return next WorkElement in fifo order (ie, oldest first), else NULL.
 * N.B. we assume wq_list is already locked
 */
static WorkElement *getNextWE(void)
{
    WorkElement *found_wep = NULL;
    for (int i = 0; i < wq_n; i++) {
        WorkElement *wep = &wq_list[i];
        if (wep->pending && (!found_wep || wep->time_added < found_wep->time_added))
            found_wep = wep;
    }
    return (found_wep);
}

/* send difference between sid's last known screen image and the current image,
 * then store current image back in sid.
 */
static void updateExistingClient (WiFiClient &client, const sid_t &sid)
{
    // curious how long these steps take
    struct timeval tv0;
    gettimeofday (&tv0, NULL);

    // get sid's pixels
    uint8_t *sid_pixels = getSIPixels(sid);
    if (!sid_pixels) {
        sendHTTPError (client, "LIVE: pixels disappeared for sid %ld\n", sid);
        return;
    }

    // make a copy of sid's last known image
    uint8_t *img_client = (uint8_t *) malloc (LIVE_NBYTES);
    if (!img_client)
        bye ("No memory for LIVE update\n");
    memcpy (img_client, sid_pixels, LIVE_NBYTES);

    // replace sid's pixels with our current screen contents
    if (!tft.getRawPix (sid_pixels, LIVE_NPIX))
        bye ("getRawPix for update failed\n");
    uint8_t *img_now = sid_pixels;                      // better name

    if (live_verbose > 1) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        printf ("LIVE: copy sid %ld img and read new took %ld usec\n", sid, TVDELUS (tv0, tv1));
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


    // send the proper web page header to client
    resetWatchdog();
    client.println ("HTTP/1.0 200 OK");
    sendUserAgent (client);
    client.println ("Content-Type: application/octet-stream");
    client.println ("Cache-Control: no-cache");
    client.println ("Connection: close\r\n");

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
        bye ("live regions %d != %d\n", n_bloks, (chg0-chg_regns)/BLOK_NBYTES);

    if (live_verbose > 1) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        printf ("LIVE: built %d regions from %d blocks for sid %ld in %ld usec\n",
                        n_regns, n_bloks, sid, TVDELUS (tv0, tv1));
    }

    // time header creation and png write
    gettimeofday (&tv0, NULL);

    // send 4-byte header followed by x,y,l of each of n regions in units of blocks.
    // N.B. must match liveweb-html.cpp expectation.
    uint8_t hdr[4+3*n_regns];
    hdr[0] = BLOK_W;                            // block width, pixels
    hdr[1] = BLOK_H;                            // block height, pixels
    hdr[2] = n_regns >> 8;                      // n regions, MSB
    hdr[3] = n_regns & 0xff;                    // n regions, LSB
    for (int i = 0; i < n_regns; i++) {
        hdr[4+3*i] = locs[i].x;
        hdr[5+3*i] = locs[i].y;
        hdr[6+3*i] = locs[i].l;
        if (live_verbose > 2)
            printf ("   %d,%d %dx%d\n", locs[i].x*BLOK_W, locs[i].y*BLOK_H, locs[i].l*BLOK_W, BLOK_H);
    }
    client.write (hdr, sizeof(hdr));

    // followed by one image containing one column BLOK_W wide of all changed regions
    if (n_bloks > 0)
        stbi_write_png_to_func (wifiSTBWrite_helper, &client, BLOK_W*n_bloks, BLOK_H,
                            COMP_RGB, chg_regns, BLOK_WBYTES*n_bloks);

    if (live_verbose > 1) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        printf ("LIVE: write hdr %d bytes and update for sid %ld in %ld usec\n",
                        (int)sizeof(hdr), sid, TVDELUS (tv0,tv1));
    }

    // finished with temps
    free (chg_regns);
    free (img_client);
}

/* save fresh screen image for sid and send to the given client.
 */
static void sendClientPNG (WiFiClient &client, const sid_t &sid)
{
    // send the proper web page header
    client.println ("HTTP/1.0 200 OK");
    sendUserAgent (client);
    client.println ("Content-Type: image/png");
    client.println ("Cache-Control: no-cache");
    client.println ("Connection: close\r\n");

    // get sid's pixels
    uint8_t *sid_pixels = getSIPixels(sid);
    if (!sid_pixels) {
        sendHTTPError (client, "LIVE: pixels disappeared for sid %ld\n", sid);
        return;
    }

    // fresh capture
    if (!tft.getRawPix (sid_pixels, LIVE_NPIX))
        bye ("getRawPix for png failed\n");

    // convert image to and send as png
    stbi_write_png_compression_level = 2;       // faster with hardly any increase in size
    stbi_write_png_to_func (wifiSTBWrite_helper, &client, BUILD_W, BUILD_H, COMP_RGB, sid_pixels,LIVE_RBYTES);

    printf ("LIVE: sent full PNG to sid %ld\n", sid);
}

/* client running liveweb-html.cpp is asking for a complete screen capture as png file.
 * N.B. close client locally if trouble, else rely on thread pool to close.
 */
static void getLivePNG (WiFiClient &client, char args[], size_t args_len)
{
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "sid";

    // parse
    if (!parseWebCommand (wa, args, args_len)) {
        sendHTTPError (client, "LIVE: get_live.png garbled: %s\n", args);
        client.stop();
        return;
    }

    // crack required sid
    if (!wa.found[0]) {
        sendHTTPError (client, "LIVE: get_live.png with no sid\n");
        client.stop();
        return;
    }
    sid_t sid = atol (wa.value[0]);

    // add a fresh SessionInfo for this sid
    addNewSI (sid);

    // enqueue fresh png command for thread pool
    queueNewWE (client, sid, true);
}

/* client running liveweb-html.cpp is asking for incremental screen update.
 * N.B. close client locally if trouble, else rely on thread pool to close.
 */
static void getLiveUpdate (WiFiClient &client, char args[], size_t args_len)
{
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "sid";

    // parse
    if (!parseWebCommand (wa, args, args_len)) {
        sendHTTPError (client, "LIVE: get_live.bin garbled: %s\n", args);
        client.stop();
        return;
    }

    // crack required sid
    if (!wa.found[0]) {
        sendHTTPError (client, "LIVE: get_live.bin with no sid\n");
        client.stop();
        return;
    }
    sid_t sid = atol (wa.value[0]);

    // insure this sid is in SessionInfo list
    if (!getSIPixels (sid)) {
        sendHTTPError (client, "LIVE: get_live.bin disappearing sid %ld\n", sid);
        client.stop();
        return;
    }

    // enqueue update command for thread pool
    queueNewWE (client, sid, false);
}

/* begin a new live session by sending the page in html-live.cpp.
 * N.B. close client before returning.
 */
static void sendLiveHTML (WiFiClient &client, char *unused_args, size_t args_len)
{
    (void)(unused_args);
    (void)(args_len);

    // send proper header
    client.println ("HTTP/1.0 200 OK");
    sendUserAgent (client);
    client.println ("Content-Type: text/html; charset=us-ascii");
    client.println ("Connection: close\r\n");

    // send page
    client.print (live_html);

    printf ("LIVE: sent live.html ok\n");

    // done
    client.stop();
}

/* client running liveweb-html.cpp sending us a character to act on as if typed locally.
 * N.B. close client before returning.
 */
static void setLiveChar (WiFiClient &client, char args[], size_t args_len)
{
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "char";
    wa.name[wa.nargs++] = "sid";        // unused here but required for future multi-instance routing

    // parse
    if (!parseWebCommand (wa, args, args_len)) {

        sendHTTPError (client, "LIVE: set_char garbled: %s\n", args);

    } else if (!wa.found[0]) {

        sendHTTPError (client, "LIVE: set_char missing char\n");

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
                sendHTTPError (client, "LIVE: Unknown char name: %s\n", str);
        } else {
            // literal
            if (strl != 1 || !isprint(str[0]))
                sendHTTPError (client, "LIVE: Unknown char 0x%02X\n", str[0]);
            else
                c = str[0];
        }

        if (c) {
            // insert into getChar queue
            tft.putChar(c);

            // ack
            char buf[100];
            startPlainText (client);
            if (isprint(c))
                snprintf (buf, sizeof(buf), "typed 0x%02X \"%c\"\n", c, c);
            else
                snprintf (buf, sizeof(buf), "typed 0x%02X\n", c);
            client.print (buf);
            if (live_verbose)
                printf ("LIVE: %s", buf);
        }
    }

    client.stop();
}

/* client running liveweb-html.cpp sending us a touch event.
 * N.B. close client before returning.
 */
static void setLiveTouch (WiFiClient &client, char args[], size_t args_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "x";
    wa.name[wa.nargs++] = "y";
    wa.name[wa.nargs++] = "hold";
    wa.name[wa.nargs++] = "sid";        // unused here but required for future multi-instance routing

    // parse
    if (!parseWebCommand (wa, args, args_len)) {

        sendHTTPError (client, "LIVE: set_touch garbled: %s\n", args);

    } else if (!wa.found[0] || !wa.found[1]) {

        sendHTTPError (client, "LIVE: set_touch missing x,y\n");

    } else {

        // require x and y within screen size
        int x = atoi(wa.value[0]);
        int y = atoi(wa.value[1]);
        if (x < 0 || x >= tft.width() || y < 0 || y >= tft.height()) {

            sendHTTPError (client, "LIVE: require 0 .. %d .. %d and 0 .. %d .. %d\n",
                                x, tft.width()-1, y, tft.height()-1);

        } else {

            // hold is optional
            int h = wa.found[2] ? atoi(wa.value[2]) : 0;

            // inform checkTouch() to use wifi_tt_s; it will reset
            wifi_tt_s.x = x;
            wifi_tt_s.y = y;
            wifi_tt = h ? TT_HOLD : TT_TAP;
            
            // ack
            char buf[100];
            startPlainText (client);
            snprintf (buf, sizeof(buf), "set_touch %d,%d %d\n", x, y, h);
            client.print (buf);
            if (live_verbose)
                printf ("LIVE: %s", buf);
        }
    }

    client.stop();
}

/* client running liveweb-html.cpp sending us a mouse motion even (buttons use set_touch()).
 * N.B. close client before returning.
 */
static void setLiveMouse (WiFiClient &client, char args[], size_t args_len)
{
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "x";
    wa.name[wa.nargs++] = "y";
    wa.name[wa.nargs++] = "sid";        // unused here but required for future multi-instance routing

    // parse
    if (!parseWebCommand (wa, args, args_len)) {

        sendHTTPError (client, "LIVE: set_mouse garbled: %s\n", args);

    } else if (!wa.found[0] || !wa.found[1]) {

        sendHTTPError (client, "LIVE: set_mouse missing x,y\n");

    } else {

        // set
        int x = atoi(wa.value[0]);
        int y = atoi(wa.value[1]);
        tft.setMouse (x, y);
            
        // ack
        char buf[100];
        startPlainText (client);
        snprintf (buf, sizeof(buf), "set_mouse %d,%d\n", x, y);
        client.print (buf);
        if (live_verbose > 1)
            printf ("LIVE: %s", buf);
    }

    client.stop();
}

/* send the favicon.ico icon.
 * N.B. close client before returning.
 */
static void getLiveFavicon (WiFiClient &client, char args[], size_t args_len)
{
    (void)(args);
    (void)(args_len);

    // send the proper web page header
    resetWatchdog();
    FWIFIPRLN (client, F("HTTP/1.0 200 OK"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: image/x-icon"));
    FWIFIPRLN (client, F("Connection: close\r\n"));

    // send icon
    writeFavicon (client);
    if (live_verbose)
        printf ("LIVE: favicon sent\n");

    client.stop();
}


/* one of a pool of identical threads each taking work from the WorkElement list.
 */
static void *liveClientThread (void *unused)
{
    (void) unused;

    for(;;) {

        // wait for work
        WorkElement *wep;
        pthread_mutex_lock (&wq_lock);
        while ((wep = getNextWE()) == NULL)
            pthread_cond_wait (&wq_cond, &wq_lock);

        // copy what we need so we can release wep ASAP.
        sid_t sid = wep->sid;
        WiFiClient client = wep->client;
        bool wants_png = wep->wants_png;

        // recycle
        wep->pending = false;

        // can release list now, we have what we need
        pthread_mutex_unlock (&wq_lock);

        // process depending complete png or update
        if (wants_png)
            sendClientPNG (client, sid);
        else
            updateExistingClient (client, sid);

        // finished with client
        client.stop();

        if (live_verbose > 1)
            printf ("LIVE: thread finished with sid %ld\n", sid);
    }

    // lint
    return (NULL);
}

/* persistent thread dispatching commands arriving from browsers running liveweb-html.
 */
static void *liveServerThread (void *unused)
{
    (void)unused;

    for (;;) {

        // wait for new browser connection
        WiFiClient client = liveweb_server->next();
        if (!client) {
            printf ("LIVE: server accept failed\n");
            return(NULL);
        }
        // printf ("LIVE: new browser connection\n");

        // read first line to capture what it wants
        char line[200];
        if (!getTCPLine (client, line, sizeof(line), NULL)) {
            printf ("LIVE: initial line timed out\n");
            client.stop();
            continue;
        }
        // printf ("LIVE: browser sent: %s\n", line);

        // discard remainder of http header
        httpSkipHeader (client);

        // expect first line of the form "GET /<cmd>"
        if (strncmp (line, "GET /", 5) != 0) {
            printf ("LIVE: expecting GET http header but got: '%s'\n", line);
            client.stop();
            continue;
        }
        char *cmd = line + 5;

        // chop off trailing http version, if present; seems it might no longer be required?
        // https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods/GET
        char *http = strstr (line, " HTTP");
        if (http)
            *http = '\0';

        // dispatch according to GET command
        // N.B. each handling function must (eventually) close client.
        static struct {
            const char *cmd_name;
            void (*cmd_fp) (WiFiClient &, char [], size_t);
        } commands[] = {
            // list in rough order of decreasing usage
            {"set_mouse?", setLiveMouse},
            {"get_live.bin?", getLiveUpdate},
            {"set_touch?", setLiveTouch},
            {"set_char?", setLiveChar},
            {"get_live.png?", getLivePNG},
            {"live.html", sendLiveHTML},
            {"favicon.ico", getLiveFavicon},
        };
        bool found = false;
        for (int i = 0; !found && i < NARRAY(commands); i++) {
            const char *cmd_name = commands[i].cmd_name;
            size_t cmd_name_len = strlen(cmd_name);
            if (strncmp (cmd, cmd_name, cmd_name_len) == 0) {
                if (live_verbose > 1)
                    printf ("LIVE: running %s\n", cmd);
                char *args = cmd + cmd_name_len;
                (*commands[i].cmd_fp) (client, args, http - args);
                // handler will close client
                found = true;
            }
        }

        // close client if no handler found
        if (!found) {
            sendHTTPError (client, "LIVE: unknown cmd: %s\n", cmd);
            client.stop();
        }
    }

    // lint
    return (NULL);
}

/* create a collection of persistent liveClientThread()s then start liveServerThread().
 */
static void createThreadPool(void)
{       
    // prep a detached attr
    pthread_t tid;
    pthread_attr_t sattr;
    pthread_attr_init (&sattr);
    pthread_attr_setdetachstate (&sattr, PTHREAD_CREATE_DETACHED);
    int e;

    // establish number of threads and start pool
    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    if (nproc < 0)
        bye ("can not learn n cores: %s\n", strerror(errno));
    int liveweb_nthreads = (1+nproc) / 2;

    // start client worker thread pool
    for (int i = 0; i < liveweb_nthreads; i++) {
        e = pthread_create (&tid, &sattr, liveClientThread, NULL);
        if (e)
            bye ("failed to create Live thread pool %d: %s\n", i, strerror(e));
    }

    // start server thread
    e = pthread_create (&tid, &sattr, liveServerThread, NULL);
    if (e)
        bye ("failed to create Live server thread: %s\n", strerror(e));

    printf ("LIVE: thread pool ready with %d threads\n", liveweb_nthreads);
}
/* called in two ways:
 *   verbose false: really start, be silent if ok, else call bye if trouble
 *   verbose true:  already running so just call tftMsg with status similar to initWebServer().
 */
void initLiveWeb (bool verbose)
{
    if (verbose) {

        tftMsg (verbose, 0, "Live Web server on port %d", liveweb_port);

    } else {

        // handle socket write errors inline
        signal (SIGPIPE, SIG_IGN);

        // start server
        char ynot[100];
        liveweb_server = new WiFiServer(liveweb_port);
        if (!liveweb_server->begin(ynot))
            bye ("Live web server on port %d failed: %s\n", liveweb_port, ynot);

        // start listener thread
        createThreadPool();
    }
}

#endif // _IS_UNIX
