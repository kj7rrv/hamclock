/* handle the DX Cluster display. Only active when visible on a Pane.
 *
 * Clusters:
 *   [ ] support ClusterSpider only
 *   [ ] code for AR-Cluster exists but it is not active -- see comment below.
 *
 * WSJT-X:
 *   [ ] packet definition: https://github.com/roelandjansen/wsjt-x/blob/master/NetworkMessage.hpp
 *   [ ] We don't actually enforce the Status ID to be WSJT-X so this may also work for, say, JTCluster.
 */

#include "HamClock.h"



// uncomment if want to try AR Cluster support
// #define _SUPPORT_ARCLUSTER

// uncomment to prefill list with show/dx but see prefillDXList() for why this is not the default
// #define _USE_SHOWDX

/* AR-Cluster commands are inconsistent but we have attempted to implement version 6. But worse, results
 * from "show heading" are unreliable, often, but not always, due to a sign error in longitude. This is not
 * likely to get fixed because it seems the author is SK : https://www.qrz.com/db/ab5k
 * 
 * Example of poor location:
 *
 * telnet dxc.nc7j.com 7373                                                          // AR-Cluster
 *   set station grid DM42jj
 *   set station latlon 32 0 N -111 0 W
 *   show heading ut7lw
 *   Heading/distance to: UT7LW/Ukraine   48 deg/228 lp   3873 mi/6233 km            // N Atlantic!
 *
 * telnet dxc.ww1r.com 7300                                                          // Spider
 *   set/qra DM42jj
 *   set/location 32 0 N -111 0 W
 *   show/heading ut7lw
 *   UT Ukraine-UR: 23 degs - dist: 6258 mi, 10071 km Reciprocal heading: 329 degs   // reasonable
 *
 *
 * Examples of some command variations:
 *
 * telnet dxc.nc7j.com 7373
 *
 *   NC7J AR-Cluster node version 6.1.5123
 *   *** TAKE NOTE! AR-Cluster 6 Commands ***
 *
 *   show heading LZ7AA
 *   Heading/distance to: LZ7AA/Bulgaria   31 deg/211 lp   6521 mi/10495 km
 *
 *   show/heading LZ7AA
 *   Heading/distance to: LZ7AA/Bulgaria   31 deg/211 lp   6521 mi/10495 km
 *
 *   set/qra DM42jj
 *   Unknown command - (set/qra DM42jj) - Type Help for a list of commands
 *
 *   SEt Station GRid DM42jj
 *   Grid set to: DM42JJ
 *
 *   show heading LZ7AA
 *   Heading/distance to: LZ7AA/Bulgaria   31 deg/211 lp   6521 mi/10495 km
 *
 *
 * telnet w6cua.no-ip.org 7300
 *
 *   Welcome to the W6CUA AR-Cluster Telnet port in Castro Valley, Ca.
 *   
 *   Your name is Elwood.  Is this correct? (Y or N) >
 *   Your QTH is Tucson, AZ.  Is this correct? (Y or N) >
 *   
 *   Please set your latitude/longitude information with SET/LOCATION.  Thank you!
 *   
 *   set/qra DM42jj
 *   QRA set to DM42jj
 *   
 *   show/heading LZ7AA
 *   Country: LZ = Bulgaria  24 deg (LP 204) 6500 mi (10458 km) from W6CUA
 *   
 *   set/location 30 00 N 110 0 W
 *   Lat/Lon set to 30 00 N 110 0 W
 *   
 *   show/heading LZ7AA
 *   Country: LZ = Bulgaria  32 deg (LP 212) 6629 mi (10666 km) from WB0OEW
 *   
 *   logout/back in: no questions, it remembered everything
 *
 *   logout/back in: give fictious call
 *
 *   Welcome to the W6CUA AR-Cluster node Telnet port!
 *   Please enter your call: w0oew
 *
 *   Please enter your name
 *   set/qra DM42jj
 *   Your name is set/qra DM42jj.  Is this correct? (Y or N) >
 *   set/qra DM42jj
 *   Please enter your name
 *    
 *
 * telnet dxc.ai9t.com 7373
 *    
 *   Running AR-Cluster Version 6 Software 
 *    
 *   set/qra DM42jj
 *   Unknown command - (set/qra DM42jj) - Type Help for a list of commands
 *    
 *   SEt Station GRid DM42jj
 *   Grid set to: DM42JJ
 *    
 *   show heading LZ7AA
 *   Heading/distance to: LZ7AA/Bulgaria   31 deg/211 lp   6567 mi/10569 km
 *
 */



// setup 
#define TITLE_COLOR     RA8875_GREEN
#define LISTING_COLOR   RA8875_WHITE
#define CLUSTER_TIMEOUT 30000                   // send line feed if idle this long, millis
#define MAX_AGE         300000                  // max age to restore spot in list, millis
#define TITLE_Y0        27                      // title dy, match VOACAP title position
#define HOSTNM_Y0       32                      // host name y down from box top
#define LISTING_Y0      47                      // first spot y down from box top
#define LISTING_DY      16                      // listing row separation
#define FONT_H          7                       // listing font height
#define FONT_W          6                       // listing font width
#define DWELL_MS        5000                    // period to show non-fatal message, ms
#define N_VISSPOTS      ((PLOTBOX_H - LISTING_Y0)/LISTING_DY)       // number of visible spots
#define SCR_DX          (PLOTBOX_W-15)          // scroll control dx center within box
#define SCRUP_DY        9                       // up " dy down "
#define SCRDW_DY        23                      // down " dy down "
#define SCR_R           5                       // " radius
#define SCR_COLOR       RA8875_GREEN            // " color
#define CLR_DX          15                      // clear control dx center within box
#define CLR_DY          ((SCRUP_DY+SCRDW_DY)/2) // " dy
#define CLR_R           SCR_R                   // " box radius
#define CLR_COLOR       SCR_COLOR               // " color

// connection info
static WiFiClient dx_client;                    // persistent TCP connection while displayed ...
static WiFiUDP wsjtx_server;                    // or persistent UDP "connection" to WSJT-X client program
static uint32_t last_action;                    // time of most recent spot or user activity, millis()

// spots
#if defined(_IS_ESP)
#define N_MORESP          3                     // use less precious ESP mem
#else
#define N_MORESP          10                    // n more than N_VISSPOTS spots
#endif
#define N_SPOTS         (N_VISSPOTS+N_MORESP)   // total number of spots retained
static DXClusterSpot spots[N_SPOTS];            // N_SPOTS spots, oldest first
static uint8_t n_spots;                         // n spots[] in use, newest at n_spots-1
static uint8_t top_vis;                         // spots[] index showing at top of pane



// type
typedef enum {
    CT_UNKNOWN,
    CT_ARCLUSTER,
    CT_DXSPIDER,
    CT_WSJTX,
} DXClusterType;
static DXClusterType cl_type;


/* return the current host name
 */
static const char *getHostName()
{
        if (useWSJTX())
            return ("WSJT-X");
        else
            return (getDXClusterHost());
}

/* given a spot[] index, draw in box if visible depending on top_vis
 */
static void drawSpotOnList (const SBox &box, int spot_i)
{
        int vis_row = spot_i - top_vis;
        if (vis_row < 0 || vis_row >= N_VISSPOTS)
            return;

        DXClusterSpot &spot = spots[spot_i];
        char line[50];

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(LISTING_COLOR);

        uint16_t x = box.x+4;
        uint16_t y = box.y + LISTING_Y0 + vis_row*LISTING_DY;
        tft.fillRect (x, y, box.w-5, LISTING_DY-1, RA8875_BLACK);
        tft.setCursor (x, y);

        // pretty freq, fixed 8 chars
        const char *f_fmt = spot.kHz < 1e6F ? "%8.1f" : "%8.0f";
        (void) sprintf (line, f_fmt, spot.kHz);

        // add remaining fields
        snprintf (line+8, sizeof(line)-8, _FX(" %-*s %04u"), MAX_SPOTCALL_LEN-1, spot.dx_call, spot.utcs);
        tft.print (line);
}


/* draw, else erase, the up scroll control;
 */
static void drawScrollUp (const SBox &box, bool draw)
{
        uint16_t x0 = box.x + SCR_DX;
        uint16_t y0 = box.y + SCRUP_DY - SCR_R;
        uint16_t x1 = box.x + SCR_DX - SCR_R;
        uint16_t y1 = box.y + SCRUP_DY + SCR_R;
        uint16_t x2 = box.x + SCR_DX + SCR_R;
        uint16_t y2 = box.y + SCRUP_DY + SCR_R;

        tft.fillTriangle (x0, y0, x1, y1, x2, y2, draw ? SCR_COLOR : RA8875_BLACK);
}

/* draw, else erase, the down scroll control.
 */
static void drawScrollDown (const SBox &box, bool draw)
{
        uint16_t x0 = box.x + SCR_DX - SCR_R;
        uint16_t y0 = box.y + SCRDW_DY - SCR_R;
        uint16_t x1 = box.x + SCR_DX + SCR_R;
        uint16_t y1 = box.y + SCRDW_DY - SCR_R;
        uint16_t x2 = box.x + SCR_DX;
        uint16_t y2 = box.y + SCRDW_DY + SCR_R;

        tft.fillTriangle (x0, y0, x1, y1, x2, y2, draw ? SCR_COLOR : RA8875_BLACK);
}

/* draw, else erase, the clear spots control
 */
static void drawClearListBtn (const SBox &box, bool draw)
{
        uint16_t color = draw ? SCR_COLOR : RA8875_BLACK;

        tft.drawRect (box.x + CLR_DX - CLR_R, box.y + CLR_DY - CLR_R, 2*CLR_R+1, 2*CLR_R+1, color);
        tft.drawLine (box.x + CLR_DX - CLR_R, box.y + CLR_DY - CLR_R,
                      box.x + CLR_DX + CLR_R, box.y + CLR_DY + CLR_R, color);
        tft.drawLine (box.x + CLR_DX + CLR_R, box.y + CLR_DY - CLR_R,
                      box.x + CLR_DX - CLR_R, box.y + CLR_DY + CLR_R, color);
}


/* draw all currently visible spots then update scroll markers
 */
static void drawAllVisSpots (const SBox &box)
{
        for (int i = top_vis; i < n_spots; i++)
            drawSpotOnList (box, i);

        drawScrollUp (box, top_vis > 0);
        drawScrollDown (box, top_vis < n_spots-N_VISSPOTS);
        drawClearListBtn (box, n_spots > 0);
}


/* shift the visible list to show one older spot, if any
 */
static void scrollUp (const SBox &box)
{
        if (top_vis > 0)
            top_vis -= 1;
        drawAllVisSpots (box);
}

/* shift the visible list to show one newer spot, if any
 */
static void scrollDown (const SBox &box)
{
        if (top_vis < n_spots-N_VISSPOTS)
            top_vis += 1;
        drawAllVisSpots (box);
}

/* convert any upper case letter in str to lower case IN PLACE
 */
static void strtolower (char *str)
{
        for (char c = *str; c != '\0'; c = *++str)
            if (isupper(c))
                *str = tolower(c);
}

/* log the given message, adding trailing \n if not already in message.
 * N.B. remove any \a characters
 */
static void dxcLog (const char *fmt, ...)
{
        // format
        char msg[128];
        va_list ap;
        va_start (ap, fmt);
        int ml = vsnprintf (msg, sizeof(msg), fmt, ap);
        va_end (ap);

        // note whether ends with newline
        bool has_nl = msg[ml-1] == '\n';

        // print with boilerplate and w/o bell
        Serial.print (F("DXC: "));
        if (strncmp (msg, _FX("DX de "), 6) != 0)
            Serial.print (F("   "));
        for (char *mp = msg; *mp != '\0'; mp++)
            if (*mp != '\a')
                Serial.print (*mp);

        // insure trailing newline
        if (!has_nl)
            Serial.print ('\n');
}

/* return whether the given spider response line looks like their prompt.
 * N.B. line ending with > is not good enough, e.g., w6kk.zapto.org 7300 says "<your name>" in banner.
 */
static bool isSpiderPrompt (const char *line, int ll)
{
        return (ll >= 2 && line[ll-1] == '>' && line [ll-2] == ' ');
}

/* read cluster until find next line that looks like a prompt.
 * return whether found
 */
static bool lookForDXClusterPrompt()
{
        char line[120];
        uint16_t ll;

        while (getTCPLine (dx_client, line, sizeof(line), &ll)) {
            dxcLog (line);
            if (isSpiderPrompt (line, ll))
                return (true);
        }

        dxcLog (_FX("Can not find prompt"));
        return (false);
}

/* read lines from cluster. if find one containing str return it in buf and skip to next prompt.
 * intended for seeking command responses.
 */
static bool lookForDXClusterString (char *buf, uint16_t bufl, const char *str)
{
        // expect within next few lines
        bool found = false;
        for (int i = 0; !found && i < 5; i++) {
            if (getTCPLine (dx_client, buf, bufl, NULL) && strstr (buf, str) != NULL)
                found = true;
        }

        // log if failure
        if (!found)
            dxcLog (_FX("Failed to find cluster response '%s'"), str);

        // always try to skip to next prompt
        (void) lookForDXClusterPrompt();

        // return whether successful
        return (found);
}

/* search through buf for " <number> str" followed by non-alnum.
 * if found set *valuep to number and return true, else return false.
 */
static bool findLabeledValue (const char *buf, int *valuep, const char *str)
{
        size_t strl = strlen(str);

        for (; *buf; buf++) {
            if (*buf == ' ' && isdigit(buf[1])) {
                // found start of a number: crack then look for str to follow
                char *vend;
                int v = strtol (buf, &vend, 10);
                if (*vend++ == ' ' && strncmp (vend, str, strl) == 0 && !isalnum(vend[strl])) {
                    // found it
                    *valuep = v;
                    return (true);
                }
            }
        }

        return (false);
}

/* given heading from DE in degrees E of N, dist in miles, return lat degs +N and longitude degs +E
 */
static void findLLFromDEHeadingDist (float heading, float miles, LatLong &ll)
{
        float A = deg2rad(heading);
        float b = miles/ERAD_M;             // 2Pi * miles / (2Pi*ERAD_M)
        float cx = de_ll.lat;               // really (Pi/2 - lat) then exchange sin/cos
        float ca, B;                        // cos polar angle, delta lng
        solveSphere (A, b, sinf(cx), cosf(cx), &ca, &B);
        ll.lat_d = rad2deg(asinf(ca));      // asin(ca) = Pi/2 - acos(ca)
        ll.lng_d = rad2deg(de_ll.lng + B);
        normalizeLL (ll);
}

/* given a call sign find its lat/long by querying dx_client.
 * technique depends on cl_type.
 * return whether successful.
 */
static bool getDXClusterSpotLL (const char *call, LatLong &ll)
{
        char buf[120];

        if (cl_type == CT_DXSPIDER) {

            // ask for heading 
            snprintf (buf, sizeof(buf), _FX("show/heading %s"), call);
            dxcLog (buf);
            dx_client.println (buf);

            // find response
            if (!lookForDXClusterString (buf, sizeof(buf), "degs"))
                return (false);

    #if defined(_SUPPORT_ARCLUSTER)

        } else if (cl_type == CT_ARCLUSTER) {

            // ask for heading 
            snprintf (buf, sizeof(buf), _FX("show heading %s"), call);
            dxcLog (buf);
            dx_client.println (buf);

            // find response
            if (!lookForDXClusterString (buf, sizeof(buf), "distance"))
                return (false);

    #endif // _SUPPORT_ARCLUSTER

        } else {

            fatalError (_FX("no LL from cluster cl_type= %d\n"), cl_type);
            return (false);
        }



        // if get here we should have a line containing <heading> deg .. <miles> mi
        // strcpy(buf,"9miW8WTS Michigan-K: 71 degs - dist: 790 mi, 1272 km Reciprocal heading: 261 degs");
        dxcLog (buf);
        strtolower(buf);
        int heading, miles;
        if (findLabeledValue (buf, &heading, "degs") && findLabeledValue (buf, &miles, "mi")) {
            findLLFromDEHeadingDist (heading, miles, ll);
            dxcLog (_FX("%s heading= %d miles= %d lat= %g lon= %g\n"), call,
                                                                    heading, miles, ll.lat_d, ll.lng_d);
        } else {
            dxcLog (_FX("No heading"));
            return (false);
        }

        // if get here it worked!
        return (true);
}

/* set radio and DX from given row, known to be defined
 */
static void engageRow (DXClusterSpot &s)
{
        setRadioSpot(s.kHz);

        LatLong ll;
        ll.lat_d = rad2deg(s.dx_lat);
        ll.lng_d = rad2deg(s.dx_lng);
        newDX (ll, NULL, s.dx_call);       // normalizes
}

static void setDXClusterSpotMapPosition (DXClusterSpot &s)
{
        char prefix[MAX_PREF_LEN];
        char *tag;

        if (plotSpotCallsigns())
            tag = s.dx_call;
        else {
            call2Prefix (s.dx_call, prefix);
            tag = prefix;
        }

        SCoord center;
        LatLong ll;
        ll.lat = s.dx_lat;
        ll.lat_d = rad2deg(ll.lat);
        ll.lng = s.dx_lng;
        ll.lng_d = rad2deg(ll.lng);
        ll2s (ll, center, 0);
        setMapTagBox (tag, center, 0, s.map_b);
}

static void drawSpotOnMap (DXClusterSpot &spot)
{

      #if defined(_SUPPORT_CLPATH)

        // draw connecting path if desired
        if (getDXSpotPaths()) {
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
            const int n_step = ceilf(dist/deg2rad(3));
            const float step = dist/n_step;
            const uint16_t color = getBandColor(spot.kHz * 1000);   // wants Hz
            SCoord prev_s = {0, 0};                                 // .x == 0 means don't show
            SCoord de_s = {0, 0}, dx_s = {0, 0};                    // first and last points drawn
            for (int i = 0; i <= n_step; i++) {                     // fence posts
                float r = i*step;
                float ca, B;
                SCoord s;
                solveSphere (bear, r, sdelatx, cdelatx, &ca, &B);
                ll2s (asinf(ca), fmodf(from_ll.lng+B+5*M_PIF,2*M_PIF)-M_PIF, s, 1);
                if (prev_s.x > 0) {
                    if (segmentSpanOk(prev_s, s, 0)) {
                        tft.drawLine (prev_s.x, prev_s.y, s.x, s.y, 1, color);
                        // record ends only if really the first and last
                        if (i == 1)
                            de_s = prev_s;
                        if (i == n_step)
                            dx_s = s;
                    } else
                       s.x = 0;
                }
                prev_s = s;
            }

            // always mark the DE end as the listener
            if (overMap (de_s))
                tft.fillRect (de_s.x-PSK_DOTR, de_s.y-PSK_DOTR, 2*PSK_DOTR, 2*PSK_DOTR, color); // not +1

            // mark the DX end as TX only if not also labeling
            if (!labelDXClusterSpots() && overMap(dx_s))
                tft.fillCircle (dx_s.x, dx_s.y, PSK_DOTR, color);
        }

      #endif // _SUPPORT_CLPATH

        // label the DX end, if desired (we don't care who the DE side is :-))
        if (labelDXClusterSpots()) {
            // label, if over map
            SCoord s;
            s.x = spot.map_b.x + spot.map_b.w/2;
            s.y = spot.map_b.y + spot.map_b.h/2;
            if (overMap(s)) {
                if (plotSpotCallsigns()) {
                    drawMapTag (spot.dx_call, spot.map_b);
                } else {
                    char prefix[MAX_PREF_LEN];
                    call2Prefix (spot.dx_call, prefix);
                    drawMapTag (prefix, spot.map_b);
                }
            }
        }
}

/* add a new spot both on map and in list, scrolling list if already full.
 * return whether ok
 */
static bool addDXClusterSpot (const SBox &box, DXClusterSpot &new_spot)
{
        // skip if looks to be same as any previous
        for (int i = 0; i < n_spots; i++) {
            DXClusterSpot &spot = spots[i];
            if (fabsf(new_spot.kHz-spot.kHz) < 0.1F && strcmp (new_spot.dx_call, spot.dx_call) == 0)
                return (false);
        }

        // printf ("******************** n top %2d %2d   %-15s", n_spots, top_vis, new_spot.dx_call);

        // add to spots[], discarding oldest if full
        if (n_spots == N_SPOTS) {
            // copy up to make room at bottom
            for (int i = 0; i < N_SPOTS-1; i++)
                spots[i] = spots[i+1];
            n_spots = N_SPOTS - 1;

            // move list top up so visible set remains the same unless at the bottom
            if (top_vis > 0 && top_vis != n_spots - N_VISSPOTS)
                top_vis -= 1;
        }

        // if at the bottom, move top down so new spot becomes visible after appending
        if (top_vis == n_spots - N_VISSPOTS)
            top_vis += 1;

        // append
        DXClusterSpot &list_spot = spots[n_spots++];
        list_spot = new_spot;

        // printf (" -> %2d %2d\n", n_spots, top_vis);

        // update visible list
        drawAllVisSpots(box);

        // show on map
        setDXClusterSpotMapPosition (list_spot);
        drawSpotOnMap (list_spot);

        // ok
        return (true);
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
            dxcLog (_FX("packet received but wrong magic"));
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
 */
static void wsjtxParseStatusMsg (const SBox &box, uint8_t **bpp)
{
        resetWatchdog();
        // dxcLog (_FX("Parsing status"));

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

        // dxcLog (_FX("WSJT: %g %s %s %s %s\n"), freq, de_call, de_grid, dx_call, dx_grid);

        // ignore if frequency is clearly bogus (which I have seen)
        if (hz == 0)
            return;

        // get each ll from grids
        LatLong ll_de, ll_dx;
        if (!maidenhead2ll (ll_de, de_grid)) {
            dxcLog (_FX("%s invalid or missing DE grid: %s\n"), de_call, de_grid);
            return;
        }
        if (!maidenhead2ll (ll_dx, dx_grid)) {
            dxcLog (_FX("%s invalid or missing DX grid: %s\n"), dx_call, dx_grid);
            return;
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
        int hr = hour();
        int mn = minute();
        new_spot.utcs = hr*100 + mn;

        // add to list and set dx if desired
        if (addDXClusterSpot (box, new_spot) && setWSJTDX())
            engageRow (spots[n_spots-1]);

        // printFreeHeap(F("wsjtxParseStatusMsg"));
}

/* display the given error message and shut down the connection.
 * draw entire box in case we were not the front pane at time of error.
 */
static void showDXClusterErr (const SBox &box, const char *msg)
{
        // erase box
        fillSBox (box, RA8875_BLACK);

        // show title and message
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(RA8875_RED);
        const char *title = _FX("DX Cluster error:");
        uint16_t tw = getTextWidth (title);
        tft.setCursor (box.x + (box.w-tw)/2, box.y + box.h/3);
        tft.print (title);
        uint16_t mw = getTextWidth (msg);
        tft.setCursor (box.x + (box.w-mw)/2, box.y + box.h/3+2*FONT_H);
        tft.print (msg);

        // log
        dxcLog (msg);

        // shut down connection
        closeDXCluster();
}

#if defined(_USE_SHOWDX)

/* send show/dx to prefill list for a new connection.
 * not an error if none are available.
 * N.B. works fine but HB9CEY says show/dx does not honor filters which will annoy experts
 */
static void prefillDXList(const SBox &box)
{
        char line[150];

        // ask for recent entries
        snprintf (line, sizeof(line), _FX("show/dx %d"), N_VISSPOTS);
        dxcLog (line);
        dx_client.println (line);
        updateClocks(false);

        // collect spots until find next prompt.
        // N.B. response format differs from normal spot format and is sorted most-recent-first
        //  14020.0  OX7AM        4-Nov-2021 1859Z  up 1                         <K2CYS>
        typedef struct {
            float kHz;
            char call[20];
            uint16_t ut;
        } ShowSpot;
        StackMalloc ss_mem(N_VISSPOTS * sizeof(ShowSpot));
        ShowSpot *ss = (ShowSpot *) ss_mem.getMem();
        int n_ss = 0;
        uint16_t ll;
        while (getTCPLine (dx_client, line, sizeof(line), &ll) && !isSpiderPrompt(line, ll)) {
            // dxcLog (line);
            int ut;
            if (sscanf (line, _FX("%f %20s %*s %d"), &ss[n_ss].kHz, ss[n_ss].call, &ut) == 3
                                                                && n_ss < N_VISSPOTS) {
                dxcLog (line);
                ss[n_ss++].ut = ut;
            }
        }
        dxcLog (_FX("found %d prior spots\n"), n_ss);
        updateClocks(false);

        // create list in reverse order
        n_spots = 0;
        while (--n_ss >= 0)
            (void) addDXClusterSpot (box, ss[n_ss].kHz, ss[n_ss].call, NULL, ss[n_ss].ut);
}

#endif // _USE_SHOWDX

/* try to connect to the cluster.
 * if success: dx_client or wsjtx_server is live and return true,
 * else: both are closed, display error msg in box, return false.
 */
static bool connectDXCluster (const SBox &box)
{
        const char *dxhost = getHostName();
        int dxport = getDXClusterPort();

        dxcLog (_FX("Connecting to %s:%d\n"), dxhost, dxport);
        resetWatchdog();

        if (useWSJTX()) {

            // create fresh UDP for WSJT-X
            wsjtx_server.stop();
            if (wsjtx_server.begin(dxport)) {

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
                dxcLog (_FX("connect ok"));

                // assume first question is asking for call
                const char *login = getDXClusterLogin();
                dxcLog (_FX("login as %s\n"), login);
                dx_client.println (login);

                // like lookForDXClusterPrompt() but look for clue about type of cluster along the way
                uint16_t bl;
                StackMalloc buf_mem(200);
                char *buf = buf_mem.getMem();
                cl_type = CT_UNKNOWN;
                while (getTCPLine (dx_client, buf, buf_mem.getSize(), &bl)) {
                    // dxcLog (buf);
                    strtolower(buf);
                    if (strstr (buf, "dx") && strstr (buf, "spider"))
                        cl_type = CT_DXSPIDER;
    #if defined(_SUPPORT_ARCLUSTER)
                    else if (strstr (buf, "ar-cluster") && strstr (buf, "ersion") && strchr (buf, '6'))
                        cl_type = CT_ARCLUSTER;
    #endif // _SUPPORT_ARCLUSTER

                    if (isSpiderPrompt(buf,bl))
                        break;
                }

                if (cl_type == CT_UNKNOWN) {
                    showDXClusterErr (box, _FX("Type unknown"));
                    return (false);
                }

                // send our location
                if (!sendDXClusterDELLGrid()) {
                    showDXClusterErr (box, _FX("Error sending DE grid"));
                    return (false);
                }

                // send not here
                snprintf (buf, buf_mem.getSize(), _FX("set/nohere"));
                dx_client.println(buf);
                dxcLog (buf);
                if (!lookForDXClusterPrompt()) {
                    showDXClusterErr (box, _FX("Error from set/nohere"));
                    return (false);
                }

                // send user commands
                const char *dx_cmds[N_DXCLCMDS];
                bool dx_on[N_DXCLCMDS];
                getDXClCommands (dx_cmds, dx_on);
                for (int i = 0; i < N_DXCLCMDS; i++) {
                    if (dx_on[i] && strlen(dx_cmds[i]) > 0) {
                        dx_client.println(dx_cmds[i]);
                        dxcLog (dx_cmds[i]);
                        if (!lookForDXClusterPrompt()) {
                            snprintf (buf, buf_mem.getSize(), _FX("Error from %s"), dx_cmds[i]);
                            showDXClusterErr (box, buf);
                            return (false);
                        }
                    }
                }

              #if defined(_USE_SHOWDX)

                // prefill list with fresh spots
                prefillDXList (box);

              #else

                // restore known spots if not too old else reset list
                if (millis() - last_action < MAX_AGE) {
                    drawAllVisSpots(box);
                } else {
                    n_spots = 0;
                    top_vis = 0;
                }

              #endif // _USE_SHOWDX



                // confirm still ok
                if (!dx_client) {
                    showDXClusterErr (box, _FX("Login failed"));
                    return (false);
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
        const char *dxhost = getHostName();
        int dxport = getDXClusterPort();

        char name[(box.w-2)/FONT_W];
        snprintf (name, sizeof(name), _FX("%s:%d"), dxhost, dxport);

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(c);
        uint16_t nw = getTextWidth (name);
        tft.setCursor (box.x + (box.w-nw)/2, box.y + HOSTNM_Y0);
        tft.print (name);
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
                    fabsf(de_ll.lat_d), fmodf(60*fabsf(de_ll.lat_d), 60), de_ll.lat_d < 0 ? 'S' : 'N',
                    fabsf(de_ll.lng_d), fmodf(60*fabsf(de_ll.lng_d), 60), de_ll.lng_d < 0 ? 'W' : 'E');

        if (cl_type == CT_DXSPIDER) {

            // set grid
            snprintf (buf, sizeof(buf), _FX("set/qra %s"), maid);
            dx_client.println(buf);
            dxcLog (buf);
            if (!lookForDXClusterPrompt()) {
                dxcLog (_FX("No > after set/qra"));
                return (false);
            }

            // set DE ll
            snprintf (buf, sizeof(buf), _FX("set/location %s"), llstr);
            dx_client.println(buf);
            dxcLog (buf);
            if (!lookForDXClusterPrompt()) {
                dxcLog (_FX("No > after set/loc"));
                return (false);
            }

            // ok!
            return (true);

    #if defined(_SUPPORT_ARCLUSTER)

        } else if (cl_type == CT_ARCLUSTER) {

            // friendly turn off skimmer just avoid getting swamped
            strcpy (buf, _FX("set dx filter not skimmer"));
            dx_client.println(buf);
            dxcLog (buf);
            if (!lookForDXClusterString (buf, sizeof(buf), "filter"))
                return (false);

            // set grid
            snprintf (buf, sizeof(buf), _FX("set station grid %sjj"), maid);    // fake 6-char grid
            dx_client.println(buf);
            dxcLog (buf);
            if (!lookForDXClusterString (buf, sizeof(buf), "set to"))
                return (false);

            // set ll
            snprintf (buf, sizeof(buf), _FX("set station latlon %s"), llstr);
            dx_client.println(buf);
            dxcLog (buf);
            if (!lookForDXClusterString (buf, sizeof(buf), "location"))
                return (false);

            // ok!
            return (true);

    #endif // _SUPPORT_ARCLUSTER

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
        tft.setTextColor(TITLE_COLOR);
        tft.setCursor (box.x + 27, box.y + TITLE_Y0);
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
static bool crackSpiderSpot (char line[], DXClusterSpot &news)
{
        // fresh
        memset (&news, 0, sizeof(news));

        if (sscanf (line, _FX("DX de %11[^ :]: %f %11s"), news.de_call, &news.kHz, news.dx_call) != 3) {
            dxcLog ("unknown format: %s", line);
            return (false);
        }

        // looks good so far, reach over and extract time also
        news.utcs = atoi(&line[70]) % 2400;

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

        // ok!
        return (true);
}

/* called frequently to drain and process cluster connection, open if not already running.
 * return whether connection is ok.
 */
bool updateDXCluster(const SBox &box)
{
        // open if not already
        if (!isDXClusterConnected() && !initDXCluster(box)) {
            // error already shown
            return(false);
        }

        if ((cl_type == CT_DXSPIDER || cl_type == CT_ARCLUSTER) && dx_client) {

            // this works for both types of cluster

            // roll all pending new spots into list
            char line[120];
            while (dx_client.available() && getTCPLine (dx_client, line, sizeof(line), NULL)) {
                // DX de KD0AA:     18100.0  JR1FYS       FT8 LOUD in FL!                2156Z EL98
                dxcLog (line);

                // look alive
                updateClocks(false);
                resetWatchdog();

                // crack
                DXClusterSpot new_spot;
                if (crackSpiderSpot (line, new_spot)) {
                    // note and display
                    last_action = millis();
                    (void) addDXClusterSpot (box, new_spot);
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
                dxcLog (_FX("feeding"));
                if (!sendDXClusterDELLGrid()) {
                    showDXClusterErr (box, _FX("Lost connection"));
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
                        }
                    }
                }
            }

            // process then free newest Status message if received
            if (sts_msg) {
                uint8_t *bp = sts_msg;
                wsjtxParseStatusMsg (box, &bp);
                free (sts_msg);
            }

            // clean up
            if (any_msg)
                free (any_msg);
        }

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
        // scroll control? N.B. use a fairly wide region to reduce false menus
        if (s.x >= box.x + 3*box.w/4) { 
            if (s.y >= box.y + SCRUP_DY - SCR_R && s.y <= box.y + SCRUP_DY + SCR_R) {
                scrollUp (box);
                return (true);
            }
            if (s.y >= box.y + SCRDW_DY - SCR_R && s.y <= box.y + SCRDW_DY + SCR_R) {
                scrollDown (box);
                return (true);
            }
            if (s.y < box.y + TITLE_Y0)
                return (true);
        }

        // clear control?
        if (s.x < box.x + box.w/4 && s.y < box.y + CLR_DY + 3*CLR_R) {
            initDXGUI(box);
            showHostPort (box, RA8875_GREEN);
            n_spots = 0;
            top_vis = 0;
            return (true);
        }

        // tapping title always leaves this pane -- we've already tested for the scroll and clear controls
        if (s.y < box.y + TITLE_Y0) {
            closeDXCluster();             // insure disconnected
            last_action = millis();       // in case op wants to come back soon
            return (false);
        }

        // engage tapped row, if defined
        int vis_row = ((s.y+LISTING_DY/2-FONT_H/2-box.y-LISTING_Y0)/LISTING_DY);
        int spot_row = top_vis + vis_row;
        if (spot_row >= 0 && spot_row < n_spots &&
                                        spots[spot_row].dx_call[0] != '\0' && isDXClusterConnected())
            engageRow (spots[spot_row]);

        // ours
        return (true);
}

/* pass back current spots list, and return whether enabled at all.
 * ok to pass back if not displayed because spot list is still intact.
 */
bool getDXClusterSpots (DXClusterSpot **spp, uint8_t *nspotsp)
{
        if (useDXCluster()) {
            *spp = spots;
            *nspotsp = n_spots;
            return (true);
        }

        return (false);
}

/* update map positions of all spots, eg, because the projection has changed
 */
void updateDXClusterSpotScreenLocations()
{
        for (uint8_t i = 0; i < n_spots; i++)
            setDXClusterSpotMapPosition (spots[i]);
}

/* draw all spots on map, if desired
 */
void drawDXClusterSpotsOnMap ()
{
        // skip if we are not up or don't want spots on map
        if (!useDXCluster() || findPaneForChoice(PLOT_CH_DXCLUSTER) == PANE_NONE
                    || (!labelDXClusterSpots() && !getDXSpotPaths()))
            return;

        for (uint8_t i = 0; i < n_spots; i++)
            drawSpotOnMap (spots[i]);
}

/* return whether the given screen coord lies over any spot label.
 * N.B. we assume map_s are set
 */
bool overAnyDXClusterSpots(const SCoord &s)
{
        // false for sure if spots are not on
        if (!useDXCluster() || findPaneForChoice(PLOT_CH_DXCLUSTER) == PANE_NONE)
            return (false);

        for (uint8_t i = 0; i < n_spots; i++)
            if (inBox (s, spots[i].map_b))
                return (true);

        return (false);
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
        // out fast if not on
        if (!useDXCluster() || findPaneForChoice(PLOT_CH_DXCLUSTER) == PANE_NONE)
            return (false);

        // linear search -- not worth kdtree etc
        float min_d = 1e10;
        DXClusterSpot *min_sp = NULL;
        bool min_is_de = false;
        for (int i = 0; i < n_spots; i++) {
            DXClusterSpot *sp = &spots[i];

            float dlat = fabsf(sp->de_lat - ll.lat);
            float dlng = fabsf(sp->de_lng - ll.lng);
            if (dlng > M_PIF)
                dlng = 2*M_PIF - dlng;
            dlng *= cosf ((sp->de_lat - ll.lat)/2);
            float d = dlat + dlng;
            if (d < min_d) {
                min_d = d;
                min_sp = sp;
                min_is_de = true;
            }

            dlat = fabsf(sp->dx_lat - ll.lat);
            dlng = fabsf(sp->dx_lng - ll.lng);
            if (dlng > M_PIF)
                dlng = 2*M_PIF - dlng;
            dlng *= cosf ((sp->dx_lat - ll.lat)/2);
            d = dlat + dlng;
            if (d < min_d) {
                min_d = d;
                min_sp = sp;
                min_is_de = false;
            }
        }

        if (min_sp && min_d < deg2rad(2*MAX_CSR_DIST/69)) {      // roughly 69 mi/deg, 2* from dlat+dlng

            // reply with fully formed ll depending on end
            if (min_is_de) {
                llp->lat_d = rad2deg(min_sp->de_lat);
                llp->lng_d = rad2deg(min_sp->de_lng);
            } else {
                llp->lat_d = rad2deg(min_sp->dx_lat);
                llp->lng_d = rad2deg(min_sp->dx_lng);
            }
            normalizeLL (*llp);

            // reply spot
            *sp = *min_sp;
            return (true);
        }

        return (false);
}
