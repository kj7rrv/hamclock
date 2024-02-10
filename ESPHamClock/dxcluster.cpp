/* handle the DX Cluster display. Only active when visible on a Pane.
 *
 * Clusters:
 *   [ ] support Spider and AR
 *   [ ] as of 3.03 replace show/header with cty_wt_mod-ll.txt -- too big for ESP
 *
 * WSJT-X:
 *   [ ] packet definition: https://sourceforge.net/p/wsjt/wsjtx/ci/master/tree/Network/NetworkMessage.hpp
 *   [ ] We don't actually enforce the Status ID to be WSJT-X so this may also work for, say, JTCluster.
 */

#include "HamClock.h"



// setup 
#define DXC_COLOR       RA8875_GREEN
#define CLUSTER_TIMEOUT (10*60*1000)            // send something if idle this long, millis
#define MAX_AGE         300000                  // max age to restore spot in list, millis
#define FONT_W          6                       // listing font width
#define CLR_DX          13                      // dx clear controler box center
#define CLR_DY          15                      // dy   "
#define CLR_R           4                       // " box radius


// connection info
static WiFiClient dx_client;                    // persistent TCP connection while displayed ...
static WiFiUDP wsjtx_server;                    // or persistent UDP "connection" to WSJT-X client program
static uint32_t last_action;                    // time of most recent spot or user activity, millis()
#define MAX_CPHR        10                      // max connection attempts per hour

// spots
#define MAX_SPOTS       (DXMAX_VIS+nMoreScrollRows())
static DXClusterSpot *dx_spots;                 // malloced list, oldest at [0]
static ScrollState dxc_ss = {DXMAX_VIS,0,0};    // scrolling info



// type
typedef enum {
    CT_UNKNOWN,
    CT_ARCLUSTER,
    CT_DXSPIDER,
    CT_WSJTX,
} DXClusterType;
static DXClusterType cl_type;

#if defined(__GNUC__)
static void dxcLog (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));
#else
static void dxcLog (const char *fmt, ...)
#endif


/* draw, else erase, the clear spots control
 */
static void drawClearListBtn (const SBox &box, bool draw)
{
        uint16_t color = draw ? DXC_COLOR : RA8875_BLACK;

        tft.drawRect (box.x + CLR_DX - CLR_R, box.y + CLR_DY - CLR_R, 2*CLR_R+1, 2*CLR_R+1, color);
        tft.drawLine (box.x + CLR_DX - CLR_R, box.y + CLR_DY - CLR_R,
                      box.x + CLR_DX + CLR_R, box.y + CLR_DY + CLR_R, color);
        tft.drawLine (box.x + CLR_DX + CLR_R, box.y + CLR_DY - CLR_R,
                      box.x + CLR_DX - CLR_R, box.y + CLR_DY + CLR_R, color);
}


/* draw all currently visible spots in the pane then update scroll markers if more
 */
static void drawAllVisDXCSpots (const SBox &box)
{
        int min_i, max_i;
        if (dxc_ss.getVisIndices (min_i, max_i) > 0) {
            for (int i = min_i; i <= max_i; i++)
                drawSpotOnList (box, dx_spots[i], dxc_ss.getDisplayRow(i));
        }

        dxc_ss.drawScrollDownControl (box, DXC_COLOR);
        dxc_ss.drawScrollUpControl (box, DXC_COLOR);
        drawClearListBtn (box, dxc_ss.n_data > 0);
}


/* shift the visible list to show newer spots, if appropriate
 */
static void scrollDXCUp (const SBox &box)
{
        if (dxc_ss.okToScrollUp()) {
            dxc_ss.scrollUp();
            drawAllVisDXCSpots(box);
        }
}

/* shift the visible list to show older spots, if appropriate
 */
static void scrollDXCDown (const SBox &box)
{
        if (dxc_ss.okToScrollDown()) {
            dxc_ss.scrollDown ();
            drawAllVisDXCSpots (box);
        }
}

/* log the given message
 * N.B. we assume trailing \n and remove any \a characters
 */
static void dxcLog (const char *fmt, ...)
{
        // format
        char msg[400];
        va_list ap;
        va_start (ap, fmt);
        (void) vsnprintf (msg, sizeof(msg)-1, fmt, ap); // allow for adding \n
        va_end (ap);

        // remove all \a
        char *bell;
        while ((bell = strchr (msg, '\a')) != NULL)
            *bell = ' ';

        // insure trailing \n just to be kind
        if (!strchr (msg, '\n'))
            strcat (msg, "\n");

        // print with prefix
        Serial.printf (_FX("DXC: %s"), msg);
}

/* return whether the given cluster response line looks like their prompt.
 */
static bool isClusterPrompt (const char *line, int ll)
{
        if (cl_type == CT_DXSPIDER)
            return (ll >= 2 && line[ll-1] == '>' && line[ll-2] == ' ');
        if (cl_type == CT_ARCLUSTER)
            return (ll >= 2 && line[ll-1] == '>' && line[ll-2] == '6');
        return (false);
}

/* read cluster until find next line that looks like a prompt.
 * return whether found
 * TODO: this might skip through more spots
 */
static bool lookForDXClusterPrompt()
{
        char line[150];
        uint16_t ll;

        while (getTCPLine (dx_client, line, sizeof(line), &ll)) {
            dxcLog (_FX("%s\n"), line);
            if (isClusterPrompt (line, ll))
                return (true);
        }

        dxcLog (_FX("Can not find prompt\n"));
        return (false);
}

/* read lines from cluster. if find one containing needle return it in buf.
 * intended for seeking command responses.
 * N.B. we assume needle is all lower case
 */
static bool lookForDXClusterString (char *buf, uint16_t bufl, const char *needle)
{
        // expect within next few lines
        // dxcLog (_FX("looking for %s\n"), needle);
        bool found = false;
        for (int i = 0; !found && i < 5; i++) {
            if (getTCPLine (dx_client, buf, bufl, NULL)) {
                dxcLog (_FX("%s\n"), buf);
                strtolower (buf);
                if (strstr (buf, needle))
                    found = true;
            }
        }

        // log if failure
        if (!found)
            dxcLog (_FX("Failed to find cluster response '%s'\n"), needle);

        // return whether successful
        return (found);
}


/* given a call sign find its lat/long by querying the cty table.
 * return whether successful.
 */
static bool getDXClusterSpotLL (const char *call, LatLong &ll)
{
        static char cty_page[] PROGMEM = "/cty/cty_wt_mod-ll.txt";
        typedef struct {
            char call[MAX_SPOTCALL_LEN];                // mostly prefixes, a few calls; sorted ala strcmp
            float lat_d, lng_d;                         // +N +E degrees
            int call_len;                               // handy strlen(call)
        } CtyLoc;
        static CtyLoc *cty_list;                        // malloced list
        static int n_cty;                               // n entries
        #define _N_RADIX ('Z' - '0' + 1)                // radix range, 
        #define _LOOKUP_DT (3600*24*1000L)              // refresh period, millis
        static int radix[_N_RADIX];                     // table of cty_list index from first character
        static uint32_t last_lookup;                    // update occasionally

        // retrieve the file first time or once per _LOOKUP_DT
        if (!cty_list || timesUp (&last_lookup, _LOOKUP_DT)) {

            WiFiClient cty_client;
            bool ok = false;

            Serial.println (cty_page);

            if (wifiOk() && cty_client.connect(backend_host, backend_port)) {

                // look alive
                updateClocks(false);

                // request page and skip response header
                httpHCPGET (cty_client, backend_host, cty_page);
                if (!httpSkipHeader (cty_client)) { 
                    dxcLog (_FX("%s header short\n"), cty_page);
                    goto out;
                }

                // fresh start
                free (cty_list);
                cty_list = NULL;
                n_cty = 0;
                int n_malloc = 0;
                const int n_more = 1000;

                // read lines and build tables
                char line[50];
                char prev_radix = 0;
                uint16_t line_len;
                CtyLoc cl;
                while (getTCPLine (cty_client, line, sizeof(line), &line_len)) {

                    // skip blank and comment lines
                    if (line_len == 0 || line[0] == '#')
                        continue;

                    // crack
                    if (sscanf (line, "%10s %f %f", cl.call, &cl.lat_d, &cl.lng_d) != 3) {
                        dxcLog (_FX("%s bad format: %s\n"), cty_page, line);
                        goto out;
                    }

                    // add to list, expanding as needed
                    if (n_cty + 1 > n_malloc) {
                        cty_list = (CtyLoc *) realloc (cty_list, (n_malloc += n_more) * sizeof(CtyLoc));
                        if (!cty_list)
                            fatalError (_FX("No memory for cluster location list %d\n"), n_malloc);
                    }
                    cl.call_len = strlen(cl.call);
                    cty_list[n_cty++] = cl;

                    // update radix index when first char changes
                    if (cl.call[0] != prev_radix) {
                        int radix_index = cl.call[0] - '0';
                        if (radix_index < 0 || radix_index >= _N_RADIX)
                            fatalError (_FX("cluster radix out of range %d %d"), radix_index, _N_RADIX);
                        radix[radix_index] = n_cty - 1;
                        prev_radix = cl.call[0];
                        // printf ("********* radix %c %5d %5d\n",cl.call[0],radix_index,radix[radix_index]);
                    }
                }

                // sanity check
                if (n_cty > 20000)
                    ok = true;
            }

          out:

            // close connection regardless
            cty_client.stop();

            if (ok) {
                // note success
                last_lookup = millis();
                Serial.printf (_FX("Found %d locations, next refresh in %ld s at %ld\n"), n_cty,
                                        _LOOKUP_DT/1000L, (last_lookup+_LOOKUP_DT)/1000L);
            } else {
                // note failure and reset
                Serial.printf (_FX("%s download failed after %d\n"), cty_page, n_cty);
                free (cty_list);
                cty_list = NULL;
                n_cty = 0;
                return (false);
            }
        }

        // start at radix then find longest cty_list call entry that starts with call.
        const CtyLoc *candidate = NULL;
        int radix_index = call[0] - '0';
        if (radix_index >= 0 && radix_index < _N_RADIX) {
            int start = radix[radix_index];
            int len_match = 0;
            // printf ("********* %c start %d .. ", call[0], start);
            for (int i = start; i < n_cty; i++) {
                const CtyLoc *cp = &cty_list[i];
                if (cp->call[0] != call[0]) {
                    // printf ("%d\n", i);
                    break;
                }
                if (strncmp (cp->call, call, cp->call_len) == 0) {
                    int cc_len = strlen(cp->call);
                    if (cc_len > len_match) {
                        len_match = cc_len;
                        candidate = cp;
                    }
                }
            }
        } else
            fatalError (_FX("cluster radix out of range %d %d"), radix_index, _N_RADIX);

        if (candidate) {
            ll.lat_d = candidate->lat_d;
            ll.lng_d = candidate->lng_d;
            normalizeLL (ll);
            return (true);
        }

        // darn
        dxcLog (_FX("No location for %s\n"), call);
        return (false);
}

/* set radio and DX from given row, known to be defined
 */
static void engageDXCRow (DXClusterSpot &s)
{
        setRadioSpot(s.kHz);

    #if defined (_SUPPORT_DXCPLOT)

        LatLong ll;
        ll.lat_d = rad2deg(s.dx_lat);
        ll.lng_d = rad2deg(s.dx_lng);
        newDX (ll, NULL, s.dx_call);       // normalizes

    #endif // _SUPPORT_DXCPLOT
}



/* add a new spot both on map and in list, scrolling list if already full.
 */
static void addDXClusterSpot (const SBox &box, DXClusterSpot &new_spot)
{
        // skip if looks to be same as any previous
        for (int i = 0; i < dxc_ss.n_data; i++) {
            DXClusterSpot &spot = dx_spots[i];
            if (fabsf(new_spot.kHz-spot.kHz) < 0.1F && strcmp (new_spot.dx_call, spot.dx_call) == 0) {
                dxcLog (_FX("DXC: %s dup\n"), new_spot.dx_call);
                return;
            }
        }

        // insure calls are upper case for getDXClusterSpotLL()
        strtoupper (new_spot.de_call);
        strtoupper (new_spot.dx_call);

        // grow or slide down over oldest if full
        if (dxc_ss.n_data == MAX_SPOTS) {
            memmove (dx_spots, dx_spots+1, (MAX_SPOTS-1) * sizeof(*dx_spots));
            dxc_ss.n_data = MAX_SPOTS - 1;
        } else {
            // grow dx_spots
            dx_spots = (DXClusterSpot *) realloc (dx_spots, (dxc_ss.n_data+1) * sizeof(DXClusterSpot));
            if (!dx_spots)
                fatalError (_FX("No memory for %d spots"), dxc_ss.n_data);
        }

        // append
        DXClusterSpot &list_spot = dx_spots[dxc_ss.n_data++];
        list_spot = new_spot;

        // printf ("***************** new: n_dxspots= %3d top_vis= %3d\n", n_dxspots, top_vis);

        // update list
        dxc_ss.scrollToNewest();
        drawAllVisDXCSpots(box);

    #if defined (_SUPPORT_DXCPLOT)

        // show on map
        setDXCSpotPosition (list_spot);
        drawDXPathOnMap (list_spot);
        drawDXCLabelOnMap (list_spot);

    #endif // _SUPPORT_DXCPLOT
}

/* given address of pointer into a WSJT-X message, extract bool and advance pointer to next field.
 */
static bool wsjtx_bool (uint8_t **bpp)
{
        bool x = **bpp > 0;
        *bpp += 1;
        return (x);
}

/* given address of pointer into a WSJT-X message, extract uint32_t and advance pointer to next field.
 * bytes are big-endian order.
 */
static uint32_t wsjtx_quint32 (uint8_t **bpp)
{
        uint32_t x = ((*bpp)[0] << 24) | ((*bpp)[1] << 16) | ((*bpp)[2] << 8) | (*bpp)[3];
        *bpp += 4;
        return (x);
}

/* given address of pointer into a WSJT-X message, extract utf8 string and advance pointer to next field.
 * N.B. returned string points into message so will only be valid as long as message memory is valid.
 */
static char *wsjtx_utf8 (uint8_t **bpp)
{
        // save begining of this packet entry
        uint8_t *bp0 = *bpp;

        // decode length
        uint32_t len = wsjtx_quint32 (bpp);

        // check for flag meaning null length string same as 0 for our purposes
        if (len == 0xffffffff)
            len = 0;

        // advance packet pointer over contents
        *bpp += len;

        // copy contents to front, overlaying length, to make room to add EOS
        memmove (bp0, bp0+4, len);
        bp0[len] = '\0';

        // dxcLog (_FX("utf8 %d '%s'\n"), len, (char*)bp0);

        // return address of content now within packet
        return ((char *)bp0);
}

/* given address of pointer into a WSJT-X message, extract double and advance pointer to next field.
 */
static uint64_t wsjtx_quint64 (uint8_t **bpp)
{
        uint64_t x;

        x = ((uint64_t)(wsjtx_quint32(bpp))) << 32;
        x |= wsjtx_quint32 (bpp);

        return (x);
}

/* return whether the given packet contains a WSJT-X Status packet.
 * if true, leave *bpp positioned just after ID.
 */
static bool wsjtxIsStatusMsg (uint8_t **bpp)
{
        resetWatchdog();

        // crack magic header
        uint32_t magic = wsjtx_quint32 (bpp);
        // dxcLog (_FX("magic 0x%x\n"), magic);
        if (magic != 0xADBCCBDA) {
            dxcLog (_FX("packet received but wrong magic\n"));
            return (false);
        }

        // crack and ignore the max schema value
        (void) wsjtx_quint32 (bpp);                         // skip past max schema

        // crack message type. we only care about Status messages which are type 1
        uint32_t msgtype = wsjtx_quint32 (bpp);
        // dxcLog (_FX("type %d\n"), msgtype);
        if (msgtype != 1)
            return (false);

        // if we get this far assume packet is what we want.
        // crack ID but ignore to allow compatibility with clones.
        volatile char *id = wsjtx_utf8 (bpp);
        (void)id;           // lint
        // dxcLog (_FX("id '%s'\n"), id);
        // if (strcmp ("WSJT-X", id) != 0)
            // return (false);

        // ok!
        return (true);
}

/* parse and process WSJT-X message known to be Status.
 * *bpp is positioned just after ID field.
 * draw on screen in box.
 * return whether good.
 */
static bool wsjtxParseStatusMsg (const SBox &box, uint8_t **bpp)
{
        resetWatchdog();
        // dxcLog (_FX("Parsing status\n"));

        // crack remaining fields down to grid
        uint32_t hz = wsjtx_quint64 (bpp);                      // capture freq
        (void) wsjtx_utf8 (bpp);                                // skip over mode
        char *dx_call = wsjtx_utf8 (bpp);                       // capture DX call 
        (void) wsjtx_utf8 (bpp);                                // skip over report
        (void) wsjtx_utf8 (bpp);                                // skip over Tx mode
        (void) wsjtx_bool (bpp);                                // skip over Tx enabled flag
        (void) wsjtx_bool (bpp);                                // skip over transmitting flag
        (void) wsjtx_bool (bpp);                                // skip over decoding flag
        (void) wsjtx_quint32 (bpp);                             // skip over Rx DF -- not always correct
        (void) wsjtx_quint32 (bpp);                             // skip over Tx DF
        char *de_call = wsjtx_utf8 (bpp);                       // capture DE call 
        char *de_grid = wsjtx_utf8 (bpp);                       // capture DE grid
        char *dx_grid = wsjtx_utf8 (bpp);                       // capture DX grid

        // dxcLog (_FX("WSJT: %7d %s %s %s %s\n"), hz, de_call, de_grid, dx_call, dx_grid);

        // ignore if frequency is clearly bogus (which I have seen)
        if (hz == 0)
            return (false);

        // get each ll from grids
        LatLong ll_de, ll_dx;
        if (!maidenhead2ll (ll_de, de_grid)) {
            dxcLog (_FX("%s invalid or missing DE grid: %s\n"), de_call, de_grid);
            return (false);
        }
        if (!maidenhead2ll (ll_dx, dx_grid)) {
            dxcLog (_FX("%s invalid or missing DX grid: %s\n"), dx_call, dx_grid);
            return (false);
        }

        // looks good, create new record
        DXClusterSpot new_spot;
        memset (&new_spot, 0, sizeof(new_spot));
        strncpy (new_spot.dx_call, dx_call, sizeof(new_spot.dx_call)-1);        // preserve EOS
        strncpy (new_spot.de_call, de_call, sizeof(new_spot.de_call)-1);        // preserve EOS
        strncpy (new_spot.dx_grid, dx_grid, sizeof(new_spot.dx_grid)-1);        // preserve EOS
        strncpy (new_spot.de_grid, de_grid, sizeof(new_spot.de_grid)-1);        // preserve EOS
        new_spot.kHz = hz*1e-3F;
        new_spot.dx_lat = ll_dx.lat;
        new_spot.dx_lng = ll_dx.lng;
        new_spot.de_lat = ll_de.lat;
        new_spot.de_lng = ll_de.lng;

        // time is now
        new_spot.spotted = myNow();

        // add to list
        addDXClusterSpot (box, new_spot);

        // ok
        return (true);
}

/* display the given error message and shut down the connection.
 */
static void showDXClusterErr (const SBox &box, const char *msg)
{
        char buf[300];
        snprintf (buf, sizeof(buf), "DX Cluster error: %s", msg);
        plotMessage (box, RA8875_RED, buf);

        // log
        dxcLog (_FX("%s\n"), msg);

        // shut down connection
        closeDXCluster();
}

/* return whether max connection rate has been reached
 */
static bool maxConnRate()
{
        // get current state
        uint32_t t0 = myNow();                  // time now
        uint32_t t_maxconn;                     // time when the limit was last reached
        uint8_t n_conn;                         // n connections so far since hr_maxconn
        if (!NVReadUInt32 (NV_DXMAX_T, &t_maxconn)) {
            t_maxconn = t0;
            NVWriteUInt32 (NV_DXMAX_T, t_maxconn);
        }
        if (!NVReadUInt8 (NV_DXMAX_N, &n_conn)) {
            n_conn = 0;
            NVWriteUInt8 (NV_DXMAX_N, n_conn);
        }
        dxcLog (_FX("checking connection %u since %u\n"), n_conn, t_maxconn);

        // check if another connection would hit the max
        bool hit_max = false;
        if (++n_conn > MAX_CPHR) {
            if (t0 < t_maxconn + 3600) {
                // hit the max
                hit_max = true;
                n_conn = MAX_CPHR;
            } else {
                // record the time and start a new count
                NVWriteUInt32 (NV_DXMAX_T, t0);
                n_conn = 1;
            }
        }
        NVWriteUInt8 (NV_DXMAX_N, n_conn);

        return (hit_max);
}

/* try to connect to the cluster.
 * if success: dx_client or wsjtx_server is live and return true,
 * else: both are closed, display error msg in box, return false.
 * N.B. inforce MAX_CPHR via NV_MAXDXCONN_HR
 */
static bool connectDXCluster (const SBox &box)
{
        // check max connection rate
        if (maxConnRate()) {
            char buf[100];
            snprintf (buf, sizeof(buf), _FX("Max %d connections/hr limit"), MAX_CPHR);
            showDXClusterErr (box, buf);
            return (false);
        }

        // get cluster connection info
        const char *dxhost = getDXClusterHost();
        int dxport = getDXClusterPort();

        dxcLog (_FX("Connecting to %s:%d\n"), dxhost, dxport);
        resetWatchdog();

        if (useWSJTX()) {

            // create fresh UDP for WSJT-X
            wsjtx_server.stop();

            // open normal or multicast depending on first octet
            bool ok;
            int first_octet = atoi (dxhost);
            if (first_octet >= 224 && first_octet <= 239) {

                // reformat as IPAddress
                unsigned o1, o2, o3, o4;
                if (sscanf (dxhost, "%u.%u.%u.%u", &o1, &o2, &o3, &o4) != 4) {
                    char emsg[100];
                    snprintf (emsg, sizeof(emsg), _FX("Multicast address must be formatted as a.b.c.d: %s"),
                                                    dxhost);
                    showDXClusterErr (box, emsg);
                    return (false);
                }
                IPAddress ifIP(0,0,0,0);                        // ignored
                IPAddress mcIP(o1,o2,o3,o4);

                ok = wsjtx_server.beginMulticast (ifIP, mcIP, dxport);
                if (ok)
                    dxcLog ("multicast %s:%d ok\n", dxhost, dxport);

            } else {

                ok = wsjtx_server.begin(dxport);

            }

            if (ok) {

                // record and claim ok so far
                cl_type = CT_WSJTX;
                return (true);
            }

        } else {

            // open fresh socket
            dx_client.stop();
            if (wifiOk() && dx_client.connect(dxhost, dxport)) {

                // look alive
                resetWatchdog();
                updateClocks(false);
                dxcLog (_FX("connect ok\n"));

                // assume first question is asking for call
                wdDelay(100);
                const char *login = getDXClusterLogin();
                dxcLog (_FX("logging in as %s\n"), login);
                dx_client.println (login);

                // like lookForDXClusterPrompt() but look for clue about type of cluster along the way
                uint16_t bl;
                char buf[200];
                size_t bufl = sizeof(buf);
                cl_type = CT_UNKNOWN;
                while (getTCPLine (dx_client, buf, bufl, &bl)) {
                    dxcLog (_FX("%s\n"), buf);
                    strtolower(buf);
                    if (strstr (buf, _FX("dx")) && strstr (buf, _FX("spider")))
                        cl_type = CT_DXSPIDER;
                    else if (strstr (buf, _FX("ar-cluster")))
                        cl_type = CT_ARCLUSTER;
                    if (isClusterPrompt(buf,bl))
                        break;
                }

                // what is it?
                if (cl_type == CT_UNKNOWN) {
                    showDXClusterErr (box, _FX("Type unknown or Login rejected"));
                    return (false);
                }
                if (cl_type == CT_DXSPIDER)
                    dxcLog (_FX("Cluster is Spider\n"));
                if (cl_type == CT_ARCLUSTER)
                    dxcLog (_FX("Cluster is AR\n"));

                // send our location
                if (!sendDXClusterDELLGrid()) {
                    showDXClusterErr (box, _FX("Error sending DE grid"));
                    return (false);
                }

            #if defined(_SEND_NOHERE)
                if (cl_type == CT_DXSPIDER) {
                    // send not here -- requested by G4UJS
                    snprintf (buf, bufl, _FX("set/nohere\n"));
                    dx_client.print (buf);
                    dxcLog (_FX("> %s"), buf);
                    if (!lookForDXClusterPrompt())
                        return (false);
                }
            #endif // _SEND_NOHERE

                // send user commands
                const char *dx_cmds[N_DXCLCMDS];
                bool dx_on[N_DXCLCMDS];
                getDXClCommands (dx_cmds, dx_on);
                for (int i = 0; i < N_DXCLCMDS; i++) {
                    if (dx_on[i] && strlen(dx_cmds[i]) > 0) {
                        dx_client.println(dx_cmds[i]);
                        dxcLog (_FX("> %s\n"), dx_cmds[i]);
                        if (!lookForDXClusterPrompt()) {
                            snprintf (buf, bufl, _FX("Err from %s\n"), dx_cmds[i]);
                            showDXClusterErr (box, buf);
                            return (false);
                        }
                    }
                }

                // confirm still ok
                if (!dx_client) {
                    showDXClusterErr (box, _FX("Login failed"));
                    return (false);
                }

                // restore known spots if not too old else reset list
                if (millis() - last_action < MAX_AGE) {
                    drawAllVisDXCSpots(box);
                } else {
                    dxc_ss.n_data = 0;
                    dxc_ss.top_vis = 0;
                }

                // all ok so far
                return (true);
            }
        }

        // sorry
        showDXClusterErr (box, _FX("Connection failed"));    // also calls dx_client.stop()
        return (false);
}

/* display the current cluster host and port in the given color
 */
static void showHostPort (const SBox &box, uint16_t c)
{
        const char *dxhost = getDXClusterHost();
        int dxport = getDXClusterPort();

        StackMalloc name_mem((box.w-2)/FONT_W);
        char *name = (char *) name_mem.getMem();
        size_t name_l = name_mem.getSize();
        snprintf (name, name_l, _FX("%s:%d"), dxhost, dxport);

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(c);
        uint16_t nw = getTextWidth (name);
        tft.setCursor (box.x + (box.w-nw)/2, box.y + DXSUBTITLE_Y0);
        tft.print (name);
}

/* send something passive just to keep the connection alive
 */
static bool sendDXClusterHeartbeat()
{
        if (!useDXCluster() || !dx_client)
            return (true);

        const char *hbcmd = _FX("ping\n");
        dxcLog (_FX("feeding %s"), hbcmd);
        dx_client.print (hbcmd);
        if (!lookForDXClusterPrompt()) {
            dxcLog (_FX("No > after %s"), hbcmd);
            return (false);
        }

        return (true);
}

/* send our lat/long and grid to dx_client, depending on cluster type.
 * return whether successful.
 * N.B. can be called any time so be prepared to do nothing if not appropriate.
 */
bool sendDXClusterDELLGrid()
{
        if (!useDXCluster() || !dx_client)
            return (true);

        char buf[100];

        // handy DE grid as string
        char maid[MAID_CHARLEN];
        getNVMaidenhead (NV_DE_GRID, maid);

        // handy DE lat/lon in common format
        char llstr[30];
        snprintf (llstr, sizeof(llstr), _FX("%.0f %.0f %c %.0f %.0f %c"),
                floorf(fabsf(de_ll.lat_d)), floorf(fmodf(60*fabsf(de_ll.lat_d), 60)), de_ll.lat_d<0?'S':'N',
                floorf(fabsf(de_ll.lng_d)), floorf(fmodf(60*fabsf(de_ll.lng_d), 60)), de_ll.lng_d<0?'W':'E');

        if (cl_type == CT_DXSPIDER) {

            // set grid
            snprintf (buf, sizeof(buf), _FX("set/qra %s\n"), maid);
            dx_client.print(buf);
            dxcLog (_FX("> %s"), buf);
            if (!lookForDXClusterPrompt())
                return (false);

            // set DE ll
            snprintf (buf, sizeof(buf), _FX("set/location %s\n"), llstr);
            dx_client.print(buf);
            dxcLog (_FX("> %s"), buf);
            if (!lookForDXClusterPrompt())
                return (false);

            // ok!
            return (true);

        } else if (cl_type == CT_ARCLUSTER) {

            // friendly turn off skimmer just avoid getting swamped
            strcpy (buf, _FX("set dx filter not skimmer\n"));
            dx_client.print(buf);
            dxcLog (_FX("> %s"), buf);
            if (!lookForDXClusterString (buf, sizeof(buf), _FX("filter")))
                return (false);

            // set grid
            snprintf (buf, sizeof(buf), _FX("set station grid %s\n"), maid);
            dx_client.print(buf);
            dxcLog (_FX("> %s"), buf);
            if (!lookForDXClusterString (buf, sizeof(buf), _FX("set to")))
                return (false);

            // set ll
            snprintf (buf, sizeof(buf), _FX("set station latlon %s\n"), llstr);
            dx_client.print(buf);
            dxcLog (_FX("> %s"), buf);
            if (!lookForDXClusterString (buf, sizeof(buf), _FX("location")))
                return (false);


            // ok!
            return (true);

        }

        // fail
        return (false);
}

static void initDXGUI (const SBox &box)
{
        // prep
        prepPlotBox (box);

        // title
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor(DXC_COLOR);
        tft.setCursor (box.x + 27, box.y + PANETITLE_H);
        tft.print (F("DX Cluster"));
}

/* prep the given box and connect dx_client to a dx cluster or wsjtx_server.
 * return whether successful.
 */
static bool initDXCluster(const SBox &box)
{
        // skip if not configured
        if (!useDXCluster())
            return (true);              // feign success to avoid retries

        // prep a fresh GUI
        initDXGUI (box);

        // show cluster host busy
        showHostPort (box, RA8875_YELLOW);

        // connect to dx cluster
        if (connectDXCluster(box)) {

            // ok: show host in green
            showHostPort (box, RA8875_GREEN);

            // reinit time
            last_action = millis();

            // ok
            return (true);

        } // else already displayed error message

        printFreeHeap(F("initDXCluster"));

        // sorry
        return (false);
}


/* parse the given line into a new spot record.
 * return whether successful
 */
static bool crackClusterSpot (char line[], DXClusterSpot &news)
{
        // fresh
        memset (&news, 0, sizeof(news));

        if (sscanf (line, _FX("DX de %11[^ :]: %f %11s"), news.de_call, &news.kHz, news.dx_call) != 3) {
            dxcLog (_FX("??? %s\n"), line);
            return (false);
        }

        // looks good so far, reach over and extract time
        tmElements_t tm;
        breakTime (myNow(), tm);
        tm.Hour = 10*(line[70]-'0') + (line[71]-'0');
        tm.Minute = 10*(line[72]-'0') + (line[73]-'0');
        news.spotted = makeTime (tm);

    #if defined (_SUPPORT_DXCPLOT)

        // find locations
        LatLong ll;

        if (!getDXClusterSpotLL (news.de_call, ll))
            return (false);
        news.de_lat = ll.lat;
        news.de_lng = ll.lng;
        ll2maidenhead (news.de_grid, ll);

        if (!getDXClusterSpotLL (news.dx_call, ll))
            return (false);
        news.dx_lat = ll.lat;
        news.dx_lng = ll.lng;
        ll2maidenhead (news.dx_grid, ll);

    #endif // _SUPPORT_DXCPLOT

        // ok!
        return (true);
}

/* called frequently to drain and process cluster connection, open if not already running.
 * return whether connection is ok.
 */
bool updateDXCluster(const SBox &box)
{
        // redraw occasionally if for no other reason than to update ages
        static uint32_t last_draw;
        bool any_new = false;

        // open if not already
        if (!isDXClusterConnected() && !initDXCluster(box)) {
            // error already shown
            return(false);
        }

        if ((cl_type == CT_DXSPIDER || cl_type == CT_ARCLUSTER) && dx_client) {

            // this works for both types of cluster

            // roll all pending new spots into list as fast as possible
            char line[120];
            while (dx_client.available() && getTCPLine (dx_client, line, sizeof(line), NULL)) {
                // DX de KD0AA:     18100.0  JR1FYS       FT8 LOUD in FL!                2156Z EL98
                dxcLog (_FX("%s\n"), line);

                // look alive
                updateClocks(false);
                resetWatchdog();

                // crack
                DXClusterSpot new_spot;
                if (crackClusterSpot (line, new_spot)) {
                    // note and display
                    last_action = millis();
                    addDXClusterSpot (box, new_spot);
                    any_new = true;
                }
            }

            // check for lost connection
            if (!dx_client) {
                showDXClusterErr (box, _FX("Lost connection"));
                return(false);
            }

            // send something if quiet for too long
            if (millis() - last_action > CLUSTER_TIMEOUT) {
                last_action = millis();        // avoid banging
                if (!sendDXClusterHeartbeat()) {
                    showDXClusterErr (box, _FX("Heartbeat lost connection"));
                    return(false);
                }
            }

        } else if (cl_type == CT_WSJTX && wsjtx_server) {

            resetWatchdog();

            // drain ALL pending packets, retain most recent Status message if any

            uint8_t *any_msg = NULL;        // malloced if get a new packet of any type
            uint8_t *sts_msg = NULL;        // malloced if find Status msg

            int packet_size;
            while ((packet_size = wsjtx_server.parsePacket()) > 0) {
                // dxcLog (_FX("WSJT-X size= %d heap= %d\n"), packet_size, ESP.getFreeHeap());
                any_msg = (uint8_t *) realloc (any_msg, packet_size);
                if (!any_msg)
                    fatalError (_FX("wsjt packet alloc %d"), packet_size);
                resetWatchdog();
                if (wsjtx_server.read (any_msg, packet_size) > 0) {
                    uint8_t *bp = any_msg;
                    if (wsjtxIsStatusMsg (&bp)) {
                        // save from bp to the end in prep for wsjtxParseStatusMsg()
                        int n_skip = bp - any_msg;
                        int n_keep = packet_size - n_skip;
                        // dxcLog (_FX("skip= %d packet_size= %d\n"), n_skip, packet_size);
                        if (n_keep > 0) {
                            sts_msg = (uint8_t *) realloc (sts_msg, n_keep);
                            if (!sts_msg)
                                fatalError (_FX("wsjt alloc fail %d"), n_keep);
                            memcpy (sts_msg, any_msg + n_skip, n_keep);
// #define _WSJT_TRACE
#if defined(_WSJT_TRACE)
                            Serial.printf ("*************** %d\n", n_keep);
                            for (int i = 0; i < n_keep; i += 10) {
                                for (int j = 0; j < 10; j++) {
                                    int n = 10*i+j;
                                    if (n == n_keep)
                                        break;
                                    uint8_t c = sts_msg[n];
                                    Serial.printf ("  %02X %c\n", c, isprint(c) ? c : '?');
                                }
                            }
#endif
                        }
                    }
                }
            }

            // process then free newest Status message if received
            if (sts_msg) {
                uint8_t *bp = sts_msg;
                if (wsjtxParseStatusMsg (box, &bp))
                    any_new = true;
                free (sts_msg);
            }

            // clean up
            if (any_msg)
                free (any_msg);
        }

        // just update ages occasionally if nothing new
        if (any_new)
            last_draw = millis();
        else if (timesUp (&last_draw, 60000))
            drawAllVisDXCSpots (box);

        // didn't break
        return (true);
}

/* insure cluster connection is closed
 */
void closeDXCluster()
{
        // make sure either/both connection is/are closed
        if (dx_client) {
            dx_client.stop();
            dxcLog (_FX("disconnect %s\n"), dx_client ? "failed" : "ok");
        }
        if (wsjtx_server) {
            wsjtx_server.stop();
            dxcLog (_FX("WSTJ-X disconnect %s\n"), wsjtx_server ?"failed":"ok");
        }
}

/* determine and engage a dx cluster pane touch.
 * return true if looks like user is interacting with the cluster pane, false if wants to change pane.
 * N.B. we assume s is within box
 */
bool checkDXClusterTouch (const SCoord &s, const SBox &box)
{
        if (s.y < box.y + PANETITLE_H) {

            // somewhere in the title bar

            // scroll up?
            if (dxc_ss.checkScrollUpTouch (s, box)) {
                scrollDXCUp (box);
                return (true);
            }

            // scroll down?
            if (dxc_ss.checkScrollDownTouch (s, box)) {
                scrollDXCDown (box);
                return (true);
            }

            // clear control?
            if (s.x < box.x + CLR_DX+2*CLR_R) {
                initDXGUI(box);
                showHostPort (box, RA8875_GREEN);
                dxc_ss.n_data = 0;
                dxc_ss.top_vis = 0;
                return (true);
            }

            // none of those, so we shut down and return indicating user can choose another pane
            closeDXCluster();             // insure disconnected
            last_action = millis();       // in case op wants to come back soon
            return (false);

        }

        // not in title so engage a tapped row, if defined
        int vis_row = (s.y - (box.y + DXLISTING_Y0)) / DXLISTING_DY;
        int spot_row;
        if (dxc_ss.findDataIndex (vis_row, spot_row) && dx_spots[spot_row].dx_call[0] != '\0'
                                                                && isDXClusterConnected())
            engageDXCRow (dx_spots[spot_row]);

        // ours 
        return (true);
}

/* pass back current spots list, and return whether enabled at all.
 * ok to pass back if not displayed because spot list is still intact.
 * N.B. caller should not modify the list
 */
bool getDXClusterSpots (DXClusterSpot **spp, uint8_t *nspotsp)
{
        if (useDXCluster()) {
            *spp = dx_spots;
            *nspotsp = dxc_ss.n_data;
            return (true);
        }

        return (false);
}

/* update map positions of all spots, eg, because the projection has changed
 */
void updateDXClusterSpotScreenLocations()
{
    #if defined (_SUPPORT_DXCPLOT)

        for (uint8_t i = 0; i < dxc_ss.n_data; i++)
            setDXCSpotPosition (dx_spots[i]);

    #endif // _SUPPORT_DXCPLOT
}

/* draw all path and spots on map, as desired
 */
void drawDXClusterSpotsOnMap ()
{
        // skip if we are neither configured nor up.
        if (!useDXCluster() || findPaneForChoice(PLOT_CH_DXCLUSTER) == PANE_NONE)
            return;

        // draw all paths and labels
        for (uint8_t i = 0; i < dxc_ss.n_data; i++) {
            DXClusterSpot &si = dx_spots[i];
            drawDXPathOnMap (si);
            drawDXCLabelOnMap (si);
        }
}

/* return whether cluster is currently connected
 */
bool isDXClusterConnected()
{
        return (useDXCluster() && (dx_client || wsjtx_server));
}

/* find closest spot and location on either end to given ll, if any.
 */
bool getClosestDXCluster (const LatLong &ll, DXClusterSpot *sp, LatLong *llp)
{
        return (getClosestDXC (dx_spots, dxc_ss.n_data, ll, sp, llp));
}





/********************************************
 *
 * a few misc tools useful for DXClusterSpot
 *
 ********************************************/


/* draw the proper symbol at the DX end of the give spot, if any
 */
void drawDXCLabelOnMap (const DXClusterSpot &spot)
{
        if (!overMap (spot.dx_map.map_b))
            return;

        uint16_t bg_color = getBandColor ((long)(spot.kHz*1000));          // wants Hz
        uint16_t txt_color = getGoodTextColor (bg_color);

        if (labelSpots()) {
            // text: call or just prefix
            if (plotSpotCallsigns()) {
                drawMapTag (spot.dx_call, spot.dx_map.map_b, txt_color, bg_color);
            } else {
                char prefix[MAX_PREF_LEN];
                call2Prefix (spot.dx_call, prefix);
                drawMapTag (prefix, spot.dx_map.map_b, txt_color, bg_color);
            }
        } else if (dotSpots()) {
            // use a circle because map_c always refers to the DX (transmitting) end
            const SCircle &c = spot.dx_map.map_c;
            tft.fillCircleRaw (c.s.x, c.s.y, c.r, bg_color);
            tft.drawCircleRaw (c.s.x, c.s.y, c.r, RA8875_BLACK);
        }
}



/* find closest location from ll to either end of paths defined in the given list of spots.
 * return whether found one within MAX_CSR_DIST.
 */
bool getClosestDXC (const DXClusterSpot *list, int n_list, const LatLong &from_ll,
    DXClusterSpot *closest_sp, LatLong *closest_llp)
{
        // linear search -- not worth kdtree etc
        float min_d = 1e10;
        const DXClusterSpot *min_sp = NULL;   
        bool min_is_de = false;         
        for (int i = 0; i < n_list; i++) {

            const DXClusterSpot *sp = &list[i];
            LatLong spot_ll;
            float d;                    

            spot_ll.lat = sp->de_lat;
            spot_ll.lng = sp->de_lng;
            d = simpleSphereDist (spot_ll, from_ll);
            if (d < min_d) {
                min_d = d;
                min_sp = sp;
                min_is_de = true;
            }

            spot_ll.lat = sp->dx_lat;
            spot_ll.lng = sp->dx_lng;
            d = simpleSphereDist (spot_ll, from_ll);
            if (d < min_d) {
                min_d = d;
                min_sp = sp;
                min_is_de = false;
            }
        }

        if (min_sp && min_d*ERAD_M < MAX_CSR_DIST) {

            // return fully formed ll depending on end
            if (min_is_de) {
                closest_llp->lat_d = rad2deg(min_sp->de_lat);
                closest_llp->lng_d = rad2deg(min_sp->de_lng);
            } else {
                closest_llp->lat_d = rad2deg(min_sp->dx_lat);
                closest_llp->lng_d = rad2deg(min_sp->dx_lng);
            }
            normalizeLL (*closest_llp);

            // return spot
            *closest_sp = *min_sp;

            // ok
            return (true);
        }

        return (false);
}


/* set map.map_b/c for the given spot DX location.
 */
void setDXCSpotPosition (DXClusterSpot &s)
{
        SCoord center;

        LatLong ll;
        ll.lat = s.dx_lat;
        ll.lat_d = rad2deg(ll.lat);
        ll.lng = s.dx_lng;
        ll.lng_d = rad2deg(ll.lng);

        if (labelSpots()) {

            // set map_b from whole or prefix

            char prefix[MAX_PREF_LEN];
            const char *tag;

            if (plotSpotCallsigns())
                tag = s.dx_call;
            else {
                call2Prefix (s.dx_call, prefix);
                tag = prefix;
            }

            ll2s (ll, center, 0);       // setMapTagBox will adjust edge
            setMapTagBox (tag, center, 0, s.dx_map.map_b);

        } else {

            // set map_c using marker radius to insure inside map boundary

            uint16_t lwRaw, mkRaw;
            getRawSpotSizes (lwRaw, mkRaw);
            ll2sRaw (ll, center, mkRaw);
            s.dx_map.map_c.s = center;
            s.dx_map.map_c.r = mkRaw;
        }
}

/* return line width and marker radius, both in raw coords
 */
void getRawSpotSizes (uint16_t &lwRaw, uint16_t &mkRaw)
{
        lwRaw = getSpotPathSize();
        mkRaw = lwRaw ? 3*lwRaw : SPOTMRNOP;
}

/* draw DX path (if enabled) and a square marker at the DE end (if enabled).
 * use drawDXCLabelOnMap() to draw a proper marker/label at the DX end.
 */
void drawDXPathOnMap (const DXClusterSpot &spot)
{
    #if defined(_SUPPORT_SPOTPATH)

        LatLong from_ll, to_ll;
        from_ll.lat = spot.de_lat;
        from_ll.lat_d = rad2deg(from_ll.lat);
        from_ll.lng = spot.de_lng;
        from_ll.lng_d = rad2deg(from_ll.lng);
        to_ll.lat = spot.dx_lat;
        to_ll.lat_d = rad2deg(to_ll.lat);
        to_ll.lng = spot.dx_lng;
        to_ll.lng_d = rad2deg(to_ll.lng);
        float sdelatx = sinf(from_ll.lat);
        float cdelatx = cosf(from_ll.lat);
        float dist, bear;
        propPath (false, from_ll, sdelatx, cdelatx, to_ll, &dist, &bear);
        const int n_step = (int)ceilf(dist/deg2rad(PATH_SEGLEN)) | 1;   // always odd for dashed ends
        const float step = dist/n_step;
        const uint16_t color = getBandColor(spot.kHz * 1000);           // wants Hz
        const bool dashed = getBandDashed (spot.kHz * 1000);
        SCoord prev_s = {0, 0};                                         // .x == 0 means don't show
        SCoord de_s = {0, 0};                                           // first point will be the DE end
        uint16_t lwRaw, mkRaw;                                          // raw path and marker sizes

        getRawSpotSizes (lwRaw, mkRaw);

        for (int i = 0; i <= n_step; i++) {                             // fence posts
            float r = i*step;
            float ca, B;
            SCoord s;
            solveSphere (bear, r, sdelatx, cdelatx, &ca, &B);
            ll2sRaw (asinf(ca), fmodf(from_ll.lng+B+5*M_PIF,2*M_PIF)-M_PIF, s, lwRaw);
            if (prev_s.x > 0) {
                if (segmentSpanOkRaw(prev_s, s, lwRaw)) {
                    if (lwRaw && (!dashed || n_step < 7 || (i & 1)))
                        tft.drawLineRaw (prev_s.x, prev_s.y, s.x, s.y, lwRaw, color);
                    // capture the first visible position for DE
                    if (de_s.x == 0)
                        de_s = prev_s;
                } else
                   s.x = 0;
            }
            prev_s = s;
        }

        // mark de end if want
        if (de_s.x && (dotSpots() || labelSpots()) && overMap(de_s)) {
            // draw square to signify this is the listener end
            tft.fillRectRaw (de_s.x-mkRaw, de_s.y-mkRaw, 2*mkRaw, 2*mkRaw, color);
            tft.drawRectRaw (de_s.x-mkRaw, de_s.y-mkRaw, 2*mkRaw, 2*mkRaw, RA8875_BLACK);
        }

    #else

        (void)spot;     // lint

    #endif // _SUPPORT_SPOTPATH 
}

/* draw the given spot in the given pane row, known to be visible.
 */
void drawSpotOnList (const SBox &box, const DXClusterSpot &spot, int row)
{
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        char line[50];

        // overall background color depends on whether spot is on watch list
        const bool watched = onDXWatchList (spot.dx_call);
        const uint16_t bg_col = watched ? RA8875_RED : RA8875_BLACK;

        // erase entire row
        const uint16_t x = box.x+4;
        const uint16_t y = box.y + DXLISTING_Y0 + row*DXLISTING_DY;
        const uint16_t h = DXLISTING_DY - 2;
        tft.fillRect (x, y-2, box.w-6, h, bg_col);

        // pretty freq, fixed 8 chars, bg matching band color assignment
        const char *f_fmt = spot.kHz < 1e6F ? _FX("%8.1f") : _FX("%8.0f");
        snprintf (line, sizeof(line), f_fmt, spot.kHz);
        const uint16_t fbg_col = getBandColor ((long)(1000*spot.kHz)); // wants Hz
        const uint16_t ffg_col = getGoodTextColor(fbg_col);
        tft.setTextColor(ffg_col);
        tft.fillRect (x, y-2, 50, h, fbg_col);
        tft.setCursor (x, y);
        tft.print (line);

        // add call
        tft.setTextColor(RA8875_WHITE);
        snprintf (line, sizeof(line), _FX(" %-*s "), MAX_SPOTCALL_LEN-1, spot.dx_call);
        tft.print (line);

        // and finally age in 4
        time_t age = myNow() - spot.spotted;
        tft.print (formatAge4 (age, line, sizeof(line)));
}
