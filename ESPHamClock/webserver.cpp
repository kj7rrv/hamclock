/* service the external web server or internal demo.
 *
 */

#include "HamClock.h"



// platform
#if defined (_IS_ESP8266)
const char platform[] = "ESPHamClock";
#elif defined(_IS_LINUX_RPI)
const char platform[] = "HamClock-rpi";
#elif defined(_IS_LINUX)
const char platform[] = "HamClock-linux";
#elif defined (__APPLE__)
const char platform[] = "HamClock-apple";
#elif defined (_IS_FREEBSD)
const char platform[] = "HamClock-FreeBSD";
#else
const char platform[] = "HamClock-UNIX";
#endif


// list of DEMO mode choice codes
typedef enum {
    // 0
    DEMO_PANE1,
    DEMO_PANE2,
    DEMO_PANE3,
    DEMO_RSS,
    DEMO_NEWDX,

    // 5
    DEMO_MAPPROJ,
    DEMO_MAPNIGHT,
    DEMO_MAPGRID,
    DEMO_MAPSTYLE,
    DEMO_NCDXF,

    // 10
    DEMO_VOACAP,
    DEMO_CALLFG,
    DEMO_CALLBG,
    DEMO_DEFMT,
    DEMO_ONAIR,

    // 15
    DEMO_SAT,
    DEMO_EME,

    DEMO_N,

} DemoChoice;
static bool runDemoChoice (DemoChoice choice, bool &slow, char msg[]);

// persistent server for listening for remote connections
static WiFiServer *remoteServer;

// handy default messages
static const char garbcmd[] = "Garbled command";
static const char notsupp[] = "Not supported";

/* convert a hex digit to its numeric value.
 * N.B. assumes ASCII encoding.
 * N.B. we do no range checking.
 */
static int hex2Int (char x)
{
        if (x <= '9')
            return (x - '0');
        else
            return (toupper(x) - 'A' + 10);
}

/* replace all "%XX" or "\xXX" with hex value and + with space, IN PLACE.
 * return whether any such changes were performed.
 */
static bool replaceEncoding (char *from)
{
    bool mod = false;

    char *to = from;
    while (*from) {
        if (from[0] == '+') {
            *to++ = ' ';
            from += 1;
            mod = true;
        } else if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {
            *to++ = 16*hex2Int(from[1]) + hex2Int(from[2]);
            from += 3;
            mod = true;
        } else if (from[0] == '\\' && from[1] == 'x' && isxdigit(from[2]) && isxdigit(from[3])) {
            *to++ = 16*hex2Int(from[2]) + hex2Int(from[3]);
            from += 4;
            mod = true;
        } else {
            *to++ = *from++;
        }
    }
    *to = '\0';

    return (mod);
}

/* send initial response indicating body will be plain text
 */
static void startPlainText (WiFiClient client)
{
    resetWatchdog();

    FWIFIPRLN (client, F("HTTP/1.0 200 OK"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: text/plain; charset=us-ascii"));
    FWIFIPRLN (client, F("Connection: close\r\n"));             // include extra blank line

    resetWatchdog();
}

/* send the given message as HTTP error 400 Bad request.
 */
static void sendHTTPError (WiFiClient client, const char *errmsg)
{
    resetWatchdog();

    // preserve locally
    Serial.println (errmsg);

    // send to client
    FWIFIPRLN (client, F("HTTP/1.0 400 Bad request"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: text/plain; charset=us-ascii"));
    FWIFIPRLN (client, F("Connection: close\r\n"));             // include extra blank line
    client.println (errmsg);

    resetWatchdog();
}

/* report all choices for the given pane to client
 */
static void reportPaneChoices (WiFiClient client, PlotPane pp)
{
    // which pane
    char buf[50];
    snprintf (buf, sizeof(buf), "Pane%d     ", (int)pp+1);
    client.print(buf);

    // always start with current then any others in rotset
    PlotChoice pc_now = plot_ch[pp];
    client.print(plot_names[pc_now]);
    if (pc_now == PLOT_CH_BC) {
        snprintf (buf, sizeof(buf), "/%dW", bc_power);
        client.print(buf);
    }
    if (paneIsRotating(pp)) {
        time_t t0 = now();
        PlotChoice pc_next = pc_now;
        while ((pc_next = getNextRotationChoice (pp, pc_next)) != pc_now) {
            size_t l = snprintf (buf, sizeof(buf), ",%s", plot_names[pc_next]);
            if (pc_next == PLOT_CH_BC)
                snprintf (buf+l, sizeof(buf)-l, "/%dW", bc_power);
            client.print(buf);
        }
        int sleft = next_rotationT[pp] - t0;
        snprintf (buf, sizeof(buf), " rotating in %02d:%02d", sleft/60, sleft%60);
        client.print(buf);
    }
    client.println();
}

/* send screen capture
 */
static bool getWiFiScreenCapture(WiFiClient client, char *line)
{
    (void)(line);

    #define CORESZ 14                           // always 14 bytes at front
    #define HDRVER 108                          // BITMAPV4HEADER, also n bytes in subheader
    #define BHDRSZ (CORESZ+HDRVER)              // total header size
    uint8_t buf[300];                           // any modest size ge BHDRSZ and mult of 2

    uint32_t nrows = tft.SCALESZ*tft.height();
    uint32_t ncols = tft.SCALESZ*tft.width();

    resetWatchdog();

    // build BMP header 
    uint32_t npix = nrows*ncols;                // n pixels
    uint32_t nbytes = npix*2;                   // n bytes of image data

    // 14 byte header common to all formats
    buf[0] = 'B';                               // id
    buf[1] = 'M';                               // id
    *((uint32_t*)(buf+ 2)) = BHDRSZ+nbytes;     // total file size: header + pixels
    *((uint16_t*)(buf+ 6)) = 0;                 // reserved 0
    *((uint16_t*)(buf+ 8)) = 0;                 // reserved 0
    *((uint32_t*)(buf+10)) = BHDRSZ;            // offset to start of pixels

    // we use BITMAPV4INFOHEADER which supports RGB565
    *((uint32_t*)(buf+14)) = HDRVER;            // subheader type
    *((uint32_t*)(buf+18)) = ncols;             // width
    *((uint32_t*)(buf+22)) = -nrows;            // height, neg means starting at the top row
    *((uint16_t*)(buf+26)) = 1;                 // n planes
    *((uint16_t*)(buf+28)) = 16;                // bits per pixel -- 16 RGB565 
    *((uint32_t*)(buf+30)) = 3;                 // BI_BITFIELDS to indicate RGB bitmasks are present
    *((uint32_t*)(buf+34)) = nbytes;            // image size in bytes
    *((uint32_t*)(buf+38)) = 0;                 // X pixels per meter -- 0 is don't care
    *((uint32_t*)(buf+42)) = 0;                 // Y pixels per meter -- 0 is don't care
    *((uint32_t*)(buf+46)) = 0;                 // colors in table
    *((uint32_t*)(buf+50)) = 0;                 // important colors
    *((uint32_t*)(buf+54)) = 0xF800;            // red mask
    *((uint32_t*)(buf+58)) = 0x07E0;            // green mask
    *((uint32_t*)(buf+62)) = 0x001F;            // blue mask
    *((uint32_t*)(buf+66)) = 0;                 // alpha mask
    *((uint32_t*)(buf+70)) = 1;                 // CSType: 1 means ignore all the remaining fields!
    *((uint32_t*)(buf+74)) = 0;                 // RedX
    *((uint32_t*)(buf+78)) = 0;                 // RedY
    *((uint32_t*)(buf+82)) = 0;                 // RedZ
    *((uint32_t*)(buf+86)) = 0;                 // GreenX
    *((uint32_t*)(buf+90)) = 0;                 // GreenY
    *((uint32_t*)(buf+94)) = 0;                 // GreenZ
    *((uint32_t*)(buf+99)) = 0;                 // BlueX
    *((uint32_t*)(buf+102)) = 0;                // BlueY
    *((uint32_t*)(buf+106)) = 0;                // BlueZ
    *((uint32_t*)(buf+110)) = 0;                // GammaRed
    *((uint32_t*)(buf+114)) = 0;                // GammaGreen
    *((uint32_t*)(buf+118)) = 0;                // GammaBlue

    // send the web page header
    resetWatchdog();
    FWIFIPRLN (client, F("HTTP/1.0 200 OK"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: image/bmp"));
    FWIFIPR (client, F("Content-Length: ")); client.println (BHDRSZ+nbytes);
    FWIFIPRLN (client, F("Connection: close\r\n"));
    // Serial.println(F("web header sent"));

    // send the image header
    client.write ((uint8_t*)buf, BHDRSZ);
    // Serial.println(F("img header sent"));

    // send the pixels
    resetWatchdog();
    tft.graphicsMode();
    tft.setXY(0,0);
    tft.writeCommand(RA8875_MRWC);
    static bool first = true;
    if (first) {
        // skip first pixel first time
        tft.readData();
        tft.readData();
        first = false;
    }
    uint16_t bufl = 0;
    for (uint32_t i = 0; i < npix; i++) {
        if ((i % tft.width()) == 0)
            resetWatchdog();

        // swap bytes
        buf[bufl+1] = tft.readData();
        buf[bufl+0] = tft.readData();
        bufl += 2;

        if (bufl == sizeof(buf) || i == npix-1) {

            #if defined (_IS_ESP8266)
                // ESP outgoing data can deadlock if incoming buffer fills, so freshen any pending arrivals.
                if (isDXClusterConnected()) {
                    PlotPane pp = findPaneChoiceNow (PLOT_CH_DXCLUSTER);
                    if (pp != PANE_NONE)
                        updateDXCluster(plot_b[pp]);
                }
            #endif

            client.write ((uint8_t*)buf, bufl);
            bufl = 0;
            resetWatchdog();
        }
    }
    // Serial.println(F("pixels sent"));

    // never fails
    return (true);
}

/* remote command to report the current stopwatch timer value, in seconds
 */
static bool getWiFiStopwatch (WiFiClient client, char *unused)
{
    (void) unused;

    startPlainText(client);

    char buf[50];

    // get current state and time
    uint32_t ms;
    SWEngineState sws = getSWEngineState(ms);

    // format time
    int hr = ms/(1000*3600);
    ms -= hr*(1000*3600);
    int mn = ms/(1000*60);
    ms -= mn*(1000*60);
    int sc = ms/1000;
    ms -= sc*1000;
    char timebuf[30];
    snprintf (timebuf, sizeof(timebuf), _FX("%02d:%02d:%02d.%03d"), hr, mn, sc, (int)ms);

    // report
    switch (sws) {
    case SWE_RESET:
        snprintf (buf, sizeof(buf), _FX("Reset %s\n"), timebuf);
        break;
    case SWE_RUN:
        snprintf (buf, sizeof(buf), _FX("Running %s\n"), timebuf);
        break;
    case SWE_STOP:
        snprintf (buf, sizeof(buf), _FX("Stopped %s\n"), timebuf);
        break;
    case SWE_LAP:
        snprintf (buf, sizeof(buf), _FX("Lap %s\n"), timebuf);
        break;
    case SWE_COUNTDOWN:
        snprintf (buf, sizeof(buf), _FX("Countdown %s\n"), timebuf);
        break;
    }

    client.print (buf);

    return (true);
}

/* helper to report DE or DX info which are very similar
 */
static bool getWiFiDEDXInfo_helper (WiFiClient client, char *unused, bool want_de)
{
    (void) unused;

    char buf[100];

    // handy which
    TZInfo &tz  =        want_de ? dx_tz : de_tz;
    LatLong &ll =        want_de ? dx_ll : de_ll;
    const char *prefix = want_de ? "DX_" : "DE_";
    NV_Name nv_grid =    want_de ? NV_DX_GRID : NV_DE_GRID;

    // start response
    startPlainText(client);


    // report prefix and path if dx else de call
    if (want_de) {
        // show prefix
        char prefix[MAX_PREF_LEN+1];
        if (getDXPrefix(prefix)) {
            snprintf (buf, sizeof(buf), "DX_prefix     %s\n", prefix);
            client.print(buf);
        }

        // show short path info
        float dist, B;
        propDEDXPath (false, dx_ll, &dist, &B);
        dist *= ERAD_M;                             // radians to miles
        B *= 180/M_PIF;                             // radians to degrees
        if (show_km)
            dist *= 1.609344F;                      // mi - > km
        FWIFIPR (client, F("DX_path_SP    "));
        snprintf (buf, sizeof(buf), _FX("%.0f %s @ %.0f deg\n"), dist, show_km ? "km" : "mi", B);
        client.print (buf);

        // show long path info
        propDEDXPath (true, dx_ll, &dist, &B);
        dist *= ERAD_M;                             // radians to miles
        B *= 180/M_PIF;                             // radians to degrees
        if (show_km)
            dist *= 1.609344F;                      // mi - > km
        FWIFIPR (client, F("DX_path_LP    "));
        snprintf (buf, sizeof(buf), _FX("%.0f %s @ %.0f deg\n"), dist, show_km ? "km" : "mi", B);
        client.print (buf);

    } else {
        snprintf (buf, sizeof(buf), _FX("Call          %s\n"), getCallsign());
        client.print (buf);
    }

    // report local time
    time_t t = nowWO();
    time_t local = t + tz.tz_secs;
    int yr = year (local);
    int mo = month(local);
    int dy = day(local);
    int hr = hour (local);
    int mn = minute (local);
    int sc = second (local);
    snprintf (buf, sizeof(buf), _FX("%stime       %d-%02d-%02dT%02d:%02d:%02d\n"), prefix,yr,mo,dy,hr,mn,sc);
    client.print (buf);

    // report timezone
    snprintf (buf, sizeof(buf), _FX("%stz         UTC%+g\n"), prefix, tz.tz_secs/3600.0);
    client.print (buf);

    // report lat
    snprintf (buf, sizeof(buf), _FX("%slat        %.2f deg\n"), prefix, ll.lat_d);
    client.print (buf);

    // report lng
    snprintf (buf, sizeof(buf), _FX("%slng        %.2f deg\n"), prefix, ll.lng_d);
    client.print (buf);

    // report grid
    char maid[MAID_CHARLEN];
    getNVMaidenhead (nv_grid, maid);
    snprintf (buf, sizeof(buf), _FX("%sgrid       %s\n"), prefix, maid);
    client.print (buf);



    // get moon info
    time_t rise, set;
    AstroCir cir;
    getLunarRS (t, ll, &rise, &set);
    getLunarCir (t, ll, cir);

    // report moon rise
    snprintf (buf, sizeof(buf), _FX("%sMoonAz     %.1f deg\n"), prefix, rad2deg(cir.az));
    client.print(buf);
    snprintf (buf, sizeof(buf), _FX("%sMoonEl     %.1f deg\n"), prefix, rad2deg(cir.el));
    client.print(buf);
    if (rise > 0) {
        snprintf (buf, sizeof(buf), _FX("%sMoonRise   %02d:%02d\n"), prefix,
                                hour(rise+tz.tz_secs), minute (rise+tz.tz_secs));
    } else {
        snprintf (buf, sizeof(buf), _FX("%sMoonRise   none\n"), prefix);
    }
    client.print(buf);

    // report moon set
    if (set > 0) {
        snprintf (buf, sizeof(buf), _FX("%sMoonSet    %02d:%02d\n"), prefix,
                                hour(set+tz.tz_secs), minute (set+tz.tz_secs));
    } else {
        snprintf (buf, sizeof(buf), _FX("%sMoonSet    none\n"), prefix);
    }
    client.print(buf);

    // print moon velocity
    snprintf (buf, sizeof(buf), _FX("%sMoonVel    %.1f m/s\n"), prefix, cir.vel);
    client.print(buf);



    // get sun info
    getSolarRS (t, ll, &rise, &set);
    getSolarCir (t, ll, cir);

    // report sun rise
    snprintf (buf, sizeof(buf), _FX("%sSunAz      %.1f deg\n"), prefix, rad2deg(cir.az));
    client.print(buf);
    snprintf (buf, sizeof(buf), _FX("%sSunEl      %.1f deg\n"), prefix, rad2deg(cir.el));
    client.print(buf);
    if (rise > 0) {
        snprintf (buf, sizeof(buf), _FX("%sSunRise    %02d:%02d\n"), prefix,
                                hour(rise+tz.tz_secs), minute (rise+tz.tz_secs));
    } else {
        snprintf (buf, sizeof(buf), _FX("%sSunRise    none\n"), prefix);
    }
    client.print(buf);

    // report sun set
    if (set > 0) {
        snprintf (buf, sizeof(buf), _FX("%sSunSet     %02d:%02d\n"), prefix,
                                hour(set+tz.tz_secs), minute (set+tz.tz_secs));
    } else {
        snprintf (buf, sizeof(buf), _FX("%sSunSet     none\n"), prefix);
    }
    client.print(buf);


    // get weather
    StackMalloc wxi_mem(sizeof(WXInfo));
    WXInfo *wip = (WXInfo *) wxi_mem.getMem();
    if (getCurrentWX (ll, want_de, wip, buf)) {
        float x = useMetricUnits() ? wip->temperature_c : 9*wip->temperature_c/5+32;
        snprintf (buf, sizeof(buf), _FX("%sWxTemp     %.1f %c\n"), prefix, x, useMetricUnits() ? 'C' : 'F');
        client.print(buf);
        snprintf (buf, sizeof(buf), _FX("%sWxHumidity %.1f %%\n"), prefix, wip->humidity_percent);
        client.print(buf);
        x = (useMetricUnits() ? 3.6 : 2.237) * wip->wind_speed_mps; // kph or mph
        snprintf (buf, sizeof(buf), _FX("%sWxWindSpd  %.1f %s\n"), prefix, x, useMetricUnits()?"kph":"mph");
        client.print(buf);
        snprintf (buf, sizeof(buf), _FX("%sWxWindDir  %s\n"), prefix, wip->wind_dir_name);
        client.print(buf);
        snprintf (buf, sizeof(buf), _FX("%sWxClouds   %s\n"), prefix, wip->clouds);
        client.print(buf);
        snprintf (buf, sizeof(buf), _FX("%sWxCondx    %s\n"), prefix, wip->conditions);
        client.print(buf);
        snprintf (buf, sizeof(buf), _FX("%sWxFrom     %s\n"), prefix, wip->attribution);
        client.print(buf);
    } else {
        client.print(prefix);
        client.print(F("WxErr      "));
        client.println(buf);
    }

    printFreeHeap (F("getWiFiDEDXInfo_helper"));

    // ok
    return (true);
}

/* remote report DE info
 */
static bool getWiFiDEInfo (WiFiClient client, char *line)
{
    return (getWiFiDEDXInfo_helper (client, line, false));
}

/* remote report DX info
 */
static bool getWiFiDXInfo (WiFiClient client, char *line)
{
    return (getWiFiDEDXInfo_helper (client, line, true));
}

/* remote report current set of DX spots
 */
static bool getWiFiDXSpots (WiFiClient client, char *line)
{
    // retrieve spots, if available
    DXClusterSpot *spots;
    uint8_t nspots;
    if (!getDXClusterSpots (&spots, &nspots)) {
        strcpy (line, _FX("No cluster"));
        return (false);
    }

    // start reply, even if none
    startPlainText (client);

    // print each row, similar to drawDXSpot()
    FWIFIPR (client, F("#  kHz   Call        UTC  Grid    Lat     Lng       Dist   Bear\n"));
    float sdelat = sinf(de_ll.lat);
    float cdelat = cosf(de_ll.lat);
    for (uint8_t i = 0; i < nspots; i++) {
        DXClusterSpot *sp = &spots[i];
        char line[100];

        // pretty freq, fixed 8 chars
        const char *f_fmt = sp->freq < 1e6 ? "%8.1f" : "%8.0f";
        (void) sprintf (line, f_fmt, sp->freq);

        // cdist will be cos of short-path anglar separation in radians, so acos is 0..pi
        // bear will be bearing from DE to spot east-to-north in radians, -pi..pi
        float cdist, bear;
        solveSphere (sp->ll.lng-de_ll.lng, M_PI_2F-sp->ll.lat, sdelat, cdelat, &cdist, &bear);
        float dist = acosf(cdist);                      // short path angle always 0..pi
        bear = fmodf (bear + 2*M_PIF, 2*M_PIF);         // shift -pi..pi to 0..2pi
        if (show_lp) {                                  // match DX display
            bear = fmodf (bear + 3*M_PIF, 2*M_PIF);     // +180 then 0..2pi
            dist = 2*M_PIF - dist;                      // cocircular angle
        }
        dist *= ERAD_M;                                 // angle to miles
        bear *= 180/M_PIF;                              // rad -> degrees
        if (show_km)                                    // match DX display
            dist *= 1.609344F;                          // miles -> km

        // print together
        snprintf (line+8, sizeof(line)-8, _FX(" %-*s %04u %s   %6.2f %7.2f   %6.0f   %4.0f\n"),
                MAX_SPOTCALL_LEN-1, sp->call, sp->utcs, sp->grid, sp->ll.lat_d, sp->ll.lng_d, dist, bear);
        client.print(line);
    }

    // ok
    return (true);
}

/* remote report some basic clock configuration
 */
static bool getWiFiConfig (WiFiClient client, char *unused)
{
    (void) unused;

    const __FlashStringHelper *not_sup = F("N/A");
    char buf[100];
    size_t nbuf;

    // start reply
    startPlainText (client);

    // report whether screen is locked
    FWIFIPR (client, F("Screen    "));
    if (screenIsLocked())
        FWIFIPRLN (client, F("locked"));
    else
        FWIFIPRLN (client, F("unlocked"));

    // report map style
    FWIFIPR (client, F("MapStyle  "));
    client.print(getMapStyle(buf));
    if (!night_on)
        client.print (F(" without night"));
    client.println();

    // report map projection
    FWIFIPR (client, F("MapProj   "));
    if (azm_on)
        FWIFIPRLN (client, F("Azimuthal"));
    else
        FWIFIPRLN (client, F("Mercator"));

    // report grid overlay
    snprintf (buf, sizeof(buf), "MapGrid   %s\n", grid_styles[mapgrid_choice]);
    client.print(buf);

    // report panes
    for (int pp = 0; pp < PANE_N; pp++)
        reportPaneChoices (client, (PlotPane)pp);

    // report NCDXF beacon box state
    FWIFIPR (client, F("NCDXF     "));
    switch (brb_mode) {
    case BRB_SHOW_BEACONS:  FWIFIPRLN (client, F("Beacons")); break;
    case BRB_SHOW_ONOFF:    FWIFIPRLN (client, F("OnOff_timers")); break;
    case BRB_SHOW_PHOT:     FWIFIPRLN (client, F("Photocell")); break;
    case BRB_SHOW_BR:       FWIFIPRLN (client, F("Brightness")); break;
    default:                FWIFIPRLN (client, F("Off")); break;
    }

    // report display brightness and timers
    uint16_t pcon, t_idle, t_idle_left;
    FWIFIPR (client, F("Bright    "));
    if (getDisplayInfo (pcon, t_idle, t_idle_left)) {

        // display brightness info
        if (brControlOk())
            snprintf (buf, sizeof(buf), _FX("%d <= %d <= %d%%\n"), getBrMin(), pcon, getBrMax());
        else
            snprintf (buf, sizeof(buf), _FX("%d%%\n"), pcon);
        client.print (buf);

        // display dimming info
        FWIFIPR (client, F("AutoDim   "));
        if (t_idle == 0)
            strcpy (buf, _FX("off"));
        else {
            nbuf = snprintf (buf, sizeof(buf), _FX("%d mins, off "), t_idle);
            if (t_idle_left > 0) {
                int mn = t_idle_left/60;
                int sc = t_idle_left - mn*60;
                snprintf (buf+nbuf, sizeof(buf)-nbuf, _FX("in %02d:%02d"), mn, sc);
            } else {
                snprintf (buf+nbuf, sizeof(buf)-nbuf, _FX("now"));
            }
        }
        client.println (buf);

        // display on/off time for each day
        for (int dow = 1; dow <= DAYSPERWEEK; dow++) {
            uint16_t on_mins, off_mins;
            if (getDisplayOnOffTimes (dow, on_mins, off_mins)) {
                snprintf (buf, sizeof(buf), "OnOff_%s %02d:%02d %02d:%02d\n", dayShortStr(dow),
                                    on_mins/60, on_mins%60, off_mins/60, off_mins%60);
                client.print (buf);
            } else
                break;
        }

    } else
        FWIFIPRLN (client, not_sup);


    // report alarm
    FWIFIPR (client, F("Alarm     "));
    AlarmState as;
    uint16_t hr, mn;
    getAlarmState (as, hr, mn);
    switch (as) {
    case ALMS_OFF:
        snprintf (buf, sizeof(buf), "Off (%02d:%02d)\n", hr, mn);
        break;
    case ALMS_ARMED:
        snprintf (buf, sizeof(buf), "Armed for %02d:%02d\n", hr, mn);
        break;
    case ALMS_RINGING:
        snprintf (buf, sizeof(buf), "Ringing since %02d:%02d\n", hr, mn);
        break;
    }
    client.print (buf);


    // time source
    FWIFIPR (client, F("TimeFrom  "));
    if (gpsd_server)
        snprintf (buf, sizeof(buf), _FX("GPSD %s\n"), gpsd_server);
    else if (ntp_server)
        snprintf (buf, sizeof(buf), _FX("NTP %s\n"), ntp_server);
    else
        strcpy (buf, _FX("Error\n"));
    client.print(buf);


    // report what DE pane is being used for
    snprintf (buf, sizeof(buf), _FX("DEPane    %s, TZ=UTC%+g%s\n"),
                detime_names[de_time_fmt],
                de_tz.tz_secs/3600.0,
                de_time_fmt == DETIME_INFO ? (desrss ? _FX(", RSAtAt") : _FX(", RSInAgo")) : "");
    client.print(buf);


    // report what DX pane is being used for
    nbuf = snprintf (buf, sizeof(buf), _FX("DXPane    %s"), dx_info_for_sat ? "sat\n" : "DX ");
    if (!dx_info_for_sat) {
        nbuf += snprintf (buf+nbuf, sizeof(buf)-nbuf, "TZ=UTC%+g %s\n", dx_tz.tz_secs/3600.0,
                            dxsrss == DXSRSS_INAGO ? _FX("RSInAgo")
                            : (dxsrss == DXSRSS_ATAT ? _FX("RSAtAt") : _FX("Prefix")));
    }
    client.print(buf);


    // report rss
    snprintf (buf, sizeof(buf), _FX("RSS       %s, interval %d secs\n"), rss_on ? "On" : "Off", rss_interval);
    client.print (buf);

    // report demo mode
    snprintf (buf, sizeof(buf), _FX("DEMO      %s\n"), getDemoMode() ? "On" : "Off");
    client.print (buf);

    // report dxcluster state
    FWIFIPR (client, F("DXCluster "));
    if (useDXCluster()) {
        snprintf (buf, sizeof(buf), _FX("%s:%d %sconnected\n"), getDXClusterHost(), getDXClusterPort(),
                                        isDXClusterConnected() ? "" : "dis");
        client.print (buf);
    } else
        FWIFIPRLN (client, F("off"));


    // report units
    FWIFIPR (client, F("Units     "));
    if (useMetricUnits())
        FWIFIPRLN (client, F("metric"));
    else
        FWIFIPRLN (client, F("imperial"));


    // report BME info
    FWIFIPR (client, F("BME280    "));
    #if defined(_SUPPORT_ENVSENSOR)
        nbuf = 0;
        for (int i = 0; i < MAX_N_BME; i++) {
            const BMEData *dp = getBMEData(i, false);
            if (dp)
                nbuf += snprintf (buf+nbuf,sizeof(buf)-nbuf, _FX("dTemp@%x= %g dPres@%x= %g "),
                                        dp->i2c, getBMETempCorr(i), dp->i2c, getBMEPresCorr(i));
        }
        if (nbuf > 0)
            client.println (buf);
        else
            FWIFIPRLN (client, F("none"));
    #else
        FWIFIPRLN (client, not_sup);
    #endif // _SUPPORT_ENVSENSOR

    // report KX3 info
    FWIFIPR (client, F("KX3       "));
    #if defined(_SUPPORT_KX3)
        uint32_t baud = getKX3Baud();
        if (baud > 0) {
            snprintf (buf, sizeof(buf), _FX("%d baud\n"), baud);
            client.print (buf);
        } else
            FWIFIPRLN (client, F("off"));
    #else
        FWIFIPRLN (client, not_sup);
    #endif // _SUPPORT_KX3

    // report GPIO info
    FWIFIPR (client, F("GPIO      "));
    #if defined(_SUPPORT_GPIO)
        if (GPIOOk()) {
            FWIFIPRLN (client, F("on"));
            #if defined(_IS_UNIX)
                FWIFIPR (client, F("ONAIR     "));
                if (checkOnAir())
                    FWIFIPRLN (client, F("on"));
                else
                    FWIFIPRLN (client, F("off"));
            #endif
        } else {
            FWIFIPRLN (client, F("off"));
        }
    #else
        FWIFIPRLN (client, not_sup);
    #endif // _SUPPORT_GPIO

    // report photosensor info
    FWIFIPR (client, F("Photocell "));
    #if defined(_SUPPORT_PHOT)
        if (found_phot)
            FWIFIPRLN (client, F("connected"));
        else
            FWIFIPRLN (client, F("disconnected"));
    #else
        FWIFIPRLN (client, not_sup);
    #endif // _SUPPORT_PHOT

    // report call sign colors
    FWIFIPR (client, F("Call_FG   "));
    snprintf (buf, sizeof(buf), _FX("%d,%d,%d\n"), RGB565_R(cs_info.fg_color),
                RGB565_G(cs_info.fg_color), RGB565_B(cs_info.fg_color)); 
    client.print(buf);
    FWIFIPR (client, F("Call_BG   "));
    if (cs_info.bg_rainbow)
        snprintf (buf, sizeof(buf), _FX("Rainbow\n"));
    else
        snprintf (buf, sizeof(buf), _FX("%d,%d,%d\n"), RGB565_R(cs_info.bg_color),
                RGB565_G(cs_info.bg_color), RGB565_B(cs_info.bg_color)); 
    client.print(buf);


    // done
    return (true);
}

/* report current satellite info to the given WiFi connection, or none.
 * always return true
 */
static bool getWiFiSatellite (WiFiClient client, char *unused)
{
    (void) unused;

    // start reply
    startPlainText (client);

    // get name and current position
    float az, el, range, rate, raz, saz, rhrs, shrs;
    char name[NV_SATNAME_LEN];
    if (!getSatAzElNow (name, &az, &el, &range, &rate, &raz, &saz, &rhrs, &shrs)) {
        FWIFIPRLN (client, F("none"));
        return (true);
    }

    // table of info
    char line[60];
    snprintf (line, sizeof(line), _FX("Name          %s\n"), name); client.print(line);
    snprintf (line, sizeof(line), _FX("Alt           %.2f deg\n"), el); client.print(line);
    snprintf (line, sizeof(line), _FX("Az            %.2f deg\n"), az); client.print(line);
    snprintf (line, sizeof(line), _FX("Range         %.2f km\n"), range); client.print(line);
    snprintf (line, sizeof(line), _FX("Rate          %.2f m/s\n"), rate); client.print(line);
    snprintf (line, sizeof(line), _FX("144MHzDoppler %.6f kHz\n"), -rate*144000/3e8); client.print(line);
    snprintf (line, sizeof(line), _FX("440MHzDoppler %.6f kHz\n"), -rate*440000/3e8); client.print(line);
    snprintf (line, sizeof(line), _FX("1.3GHzDoppler %.6f kHz\n"), -rate*1.3e6/3e8); client.print(line);
    snprintf (line, sizeof(line), _FX("10GHzDoppler  %.6f kHz\n"), -rate*1e7/3e8); client.print(line);

    // add table of next several events, if any
    time_t *rises, *sets;
    int n_times;
    if (raz == SAT_NOAZ || saz == SAT_NOAZ || (n_times = nextSatRSEvents (&rises, &sets)) == 0) {
        FWIFIPR (client, F("No rise or set\n"));
    } else {
        // next events
        snprintf (line, sizeof(line), _FX("NextRiseIn    %02dh%02d\n"), (int)rhrs,
                (int)(60*(rhrs-(int)rhrs)+0.5F)); client.print(line);
        snprintf (line, sizeof(line), _FX("NextSetIn     %02dh%02d\n"),(int)shrs,
                (int)(60*(shrs-(int)shrs)+0.5F)); client.print(line);

        // print heading
        FWIFIPR (client, F("  Upcoming DE Passes\n"));
        FWIFIPR (client, F("Day  Rise    Set    Up\n"));
        // snprintf (line, sizeof(line), "%.3s  %02dh%02d  %02dh%02d  %02d:%02d\n"

        // print table
        for (int i = 0; i < n_times; i++) {

            // DE timezone
            time_t rt = rises[i] + de_tz.tz_secs;
            time_t st = sets[i] + de_tz.tz_secs;
            int up = st - rt;

            // detect crossing midnight by comparing weekday
            int rt_wd = weekday(rt);
            int st_wd = weekday(st);

            // start with rise day and time for sure
            size_t l = snprintf (line, sizeof(line), "%.3s  %02dh%02d", dayShortStr(rt_wd),
                                                                                hour(rt), minute(rt));

            // if set time is tomorrow start new line with set day and blank rise
            if (rt_wd != st_wd)
                l += snprintf (line+l, sizeof(line)-l, "\n%s  %s", dayShortStr(st_wd), "     ");

            // show set time
            l += snprintf (line+l, sizeof(line)-l, "  %02dh%02d  ", hour(st), minute(st));

            // show up time, beware longer than 1 hour (moon!)
            if (up >= 3600)
                l += snprintf (line+l, sizeof(line)-l, "%02dh%02d\n", up/3600, (up-3600*(up/3600))/60);
            else
                l += snprintf (line+l, sizeof(line)-l, "%02d:%02d\n", up/60, up-60*(up/60));

            // done!
            client.print (line);
        }

        // clean up
        free ((void*)rises);
        free ((void*)sets);
    }

    return (true);
}


/* report all available satellites.
 */
static bool getWiFiAllSatellites (WiFiClient client, char *line)
{
    // get names and elements
    const char **all_names = getAllSatNames();
    if (all_names == NULL) {
        strcpy (line, _FX("No sats"));
        return (false);
    }

    // list and free
    startPlainText(client);
    const char **all_names_0 = all_names;
    const char *name;
    while ((name = *all_names++) != NULL) {
        client.println (name);
        free ((void*)name);
    }
    free ((void*)all_names_0);

    // ok
    return (true);
}


/* send the current collection of sensor data to client in tabular format.
 */
static bool getWiFiSensorData (WiFiClient client, char *line)
{
    if (getNBMEConnected() == 0) {
        strcpy (line, _FX("No sensors"));
        return (false);
    }

    // send html header
    startPlainText(client);

    // send content header
    if (useMetricUnits())
        FWIFIPR (client, F("#   UTC ISO 8601      UNIX secs I2C  Temp,C   P,hPa   Hum,%  DewP,C\n"));
    else
        FWIFIPR (client, F("#   UTC ISO 8601      UNIX secs I2C  Temp,F  P,inHg   Hum,%  DewP,F\n"));

    // send data for each connected sensor
    resetWatchdog();
    for (int i = 0; i < MAX_N_BME; i++) {
        const BMEData *dp = getBMEData(i, true);
        if (dp) {
            // head points to oldest
            for (int j = 0; j < N_BME_READINGS; j++) {
                uint8_t q = (dp->q_head+j)%N_BME_READINGS;
                time_t u = dp->u[q];
                if (u) {
                    char buf[100];
                    snprintf (buf, sizeof(buf),
                                _FX("%4d-%02d-%02dT%02d:%02d:%02dZ %lu  %02x %7.2f %7.2f %7.2f %7.2f\n"),
                                year(u), month(u), day(u), hour(u), minute(u), second(u), u,
                                dp->i2c, dp->t[q], dp->p[q], dp->h[q], dewPoint (dp->t[q], dp->h[q]));
                    client.print (buf);
                    q = (q+1)%N_BME_READINGS;
                }
            }
            client.print ("\n");
        }
    }

    return (true);
}

/* given age in seconds, set string to short approx description.
 */
static char *ageStr (long secs, char *str)
{
    if (secs < 60)
        sprintf (str, "%2ld secs", secs);
    else if (secs < 3600)
        sprintf (str, "%2ld mins", secs/60);
    else if (secs < 24*3600)
        sprintf (str, "%2ld hrs", secs/3600);
    else 
        sprintf (str, "1+ days");
    return (str);
}

/* send the current space weather stats to client
 */
static bool getWiFiSpaceWx (WiFiClient client, char *unused)
{
    (void) unused;

    // send html header
    startPlainText(client);

    // collect info
    SPWxValue ssn, flux, kp, swind, drap;
    NOAASpaceWx noaaspw;
    float path[PROP_MAP_N];
    char xray[10];
    time_t noaaspw_age, xray_age, path_age;
    getSpaceWeather (ssn, flux, kp, swind, drap, noaaspw, noaaspw_age, xray, xray_age, path, path_age);

    // send values and ages
    char buf[100];
    char age[20];

    client.print (F(" Datum   Value    Age\n"));
    client.print (F("-------- -----  -------\n"));

    snprintf (buf, sizeof(buf), _FX("SSN      %5.1f  %s\n"), ssn.value, ageStr(ssn.age, age));
    client.print (buf);

    snprintf (buf, sizeof(buf), _FX("KP        %4.0f  %s\n"), kp.value, ageStr(kp.age, age));
    client.print (buf);

    snprintf (buf, sizeof(buf), _FX("FLUX     %5.1f  %s\n"), flux.value, ageStr(flux.age, age));
    client.print (buf);

    snprintf (buf, sizeof(buf), _FX("XRAY      %4s  %s\n"), xray, ageStr(xray_age, age));
    client.print (buf);

    snprintf (buf, sizeof(buf), _FX("SOLWIND   %4.1f  %s\n"), swind.value, ageStr(swind.age, age));
    client.print (buf);

    snprintf (buf, sizeof(buf), _FX("DRAP      %4.1f  %s\n"), drap.value, ageStr(drap.age, age));
    client.print (buf);

    for (int i = 0; i < PROP_MAP_N; i++) {
        int band = propMap2Band ((PropMapSetting)i);
        // match format in plotBandConditions()
        snprintf (buf, sizeof(buf), _FX("DEDX_%02dm  %4.0f  %s\n"), band, 99*path[i], ageStr(path_age, age));
        client.print (buf);
    }

    for (int i = 0; i < N_NOAASW_C; i++) {
        for (int j = 0; j < N_NOAASW_V; j++) {
            snprintf (buf, sizeof(buf), _FX("NSPW_%c%d   %4d  %s\n"), noaaspw.cat[i], j, noaaspw.val[i][j],
                    ageStr(noaaspw_age, age));
            client.print (buf);
        }
    }

    // ok
    printFreeHeap (F("getWiFiSpaceWx"));
    return (true);
}



/* send some misc system info
 */
static bool getWiFiSys (WiFiClient client, char *unused)
{
    (void) unused;
    char buf[100];

    // send html header
    startPlainText(client);

    // get latest worst stats
    int worst_heap, worst_stack;
    getWorstMem (&worst_heap, &worst_stack);

    // show basic info
    resetWatchdog();
    FWIFIPR (client, F("Version  ")); client.println (hc_version);
    FWIFIPR (client, F("MaxStack ")); client.println (worst_stack);
    FWIFIPR (client, F("MaxWDDT  ")); client.println (max_wd_dt);
    FWIFIPR (client, F("Platform ")); client.println (platform);
    FWIFIPR (client, F("Backend  ")); client.println (svr_host);
    FWIFIPR (client, F("SvrPort  ")); client.println (svr_port);
#if defined(_IS_ESP8266)
    FWIFIPR (client, F("MinHeap  ")); client.println (worst_heap);
    FWIFIPR (client, F("FreeNow  ")); client.println (ESP.getFreeHeap());
    FWIFIPR (client, F("MaxBlock ")); client.println (ESP.getMaxFreeBlockSize());
    FWIFIPR (client, F("SketchSz ")); client.println (ESP.getSketchSize());
    FWIFIPR (client, F("FreeSkSz ")); client.println (ESP.getFreeSketchSpace());
    FWIFIPR (client, F("FlashSiz ")); client.println (ESP.getFlashChipRealSize());
    FWIFIPR (client, F("CPUMHz   ")); client.println (ESP.getCpuFreqMHz());
    FWIFIPR (client, F("CoreVer  ")); client.println (ESP.getCoreVersion());
    // #if defined __has_include                        should work but doesn't
        // #if __has_include (<lwip-git-hash.h>)        should work but doesn't either
            // #include <lwip-git-hash.h>
            // FWIFIPR (client, F("lwipVer  ")); client.println (LWIP_HASH_STR);
        // #endif
    // #endif
#endif

    // show uptime
    uint16_t days; uint8_t hrs, mins, secs;
    if (getUptime (&days, &hrs, &mins, &secs)) {
        snprintf (buf, sizeof(buf), _FX("%dd%02d:%02d:%02d\n"), days, hrs, mins, secs);
        FWIFIPR (client, F("UpTime   ")); client.print (buf);
    }

    // show NTP servers
    const NTPServer *ntp_list;
    int n_ntp = getNTPServers (&ntp_list);
    for (int i = 0; i < n_ntp; i++) {
        int bl = snprintf (buf, sizeof(buf), "NTP      %s ", ntp_list[i].server);
        int rsp = ntp_list[i].rsp_time;
        if (rsp == 0)
            bl += snprintf (buf+bl, sizeof(buf)-bl, "%s\n", _FX("- Not yet measured"));
        else if (rsp == NTP_TOO_LONG)
            bl += snprintf (buf+bl, sizeof(buf)-bl, "%s\n", _FX("- Timed out"));
        else
            bl += snprintf (buf+bl, sizeof(buf)-bl, "%d ms\n", rsp);
        client.print (buf);
    }

    // show file system info
    int n_info;
    uint64_t fs_size, fs_used;
    char *fs_name;
    FS_Info *fip0 = getConfigDirInfo (&n_info, &fs_name, &fs_size, &fs_used);
    client.print (fs_name);
    if (fs_size > 1000000000U) {
        snprintf (buf, sizeof(buf), " %lu / %lu MiB\n",
                        (unsigned long)(fs_used/1048576U), (unsigned long)(fs_size/1048576U));
    } else
        snprintf (buf, sizeof(buf), " %lu / %lu B\n", (unsigned long)fs_used, (unsigned long)fs_size);
    client.print (buf);
    for (int i = 0; i < n_info; i++) {
        FS_Info *fip = &fip0[i];
        snprintf (buf, sizeof(buf), "  %-32s %20s %7u\n", fip->name, fip->date, fip->len);
        client.print (buf);
    }
    free (fs_name);
    free (fip0);

    return (true);
}


/* send current clock time
 */
static bool getWiFiTime (WiFiClient client, char *unused)
{
    (void) unused;

    // send html header
    startPlainText(client);

    // report user's idea of time
    char buf[100];
    time_t t = nowWO();
    int yr = year (t);
    int mo = month(t);
    int dy = day(t);
    int hr = hour (t);
    int mn = minute (t);
    int sc = second (t);
    int bl = snprintf (buf, sizeof(buf)-10, _FX("Clock_UTC %d-%02d-%02dT%02d:%02d:%02d"),
                        yr, mo, dy, hr, mn, sc);

    // indicate any time offset
    int32_t off = utcOffset();
    if (off == 0) {
        buf[bl++] = 'Z';                        // append Z if time really is UTC
        buf[bl] = 0;
    } else
        sprintf (buf+bl, " %+g", off/3600.0);   // else show offset in hours
    client.println (buf);

    return (true);
}

/* remote command to set call sign to some message and set fg and bg colors.
 * all are optional in any order; restore defaults if no args at all.
 */
static bool setWiFiTitle (WiFiClient client, char line[])
{
    // find each possible keyword -- remember all are optional
    char *msg = strstr (line, "msg=");
    char *fg = strstr (line, "fg=");
    char *bg = strstr (line, "bg=");

    // check for any unexpected & args
    for (char *amp = strchr (line, '&'); amp; amp = strchr (amp+1, '&')) {
        if ((!msg || amp != msg-1) && (!fg || amp != fg-1) && (!bg || amp != bg-1)) {
            strcpy (line, garbcmd);
            return (false);
        }
    }

    // check for any unexpected = args
    for (char *eq = strchr (line, '='); eq; eq = strchr (eq+1, '=')) {
        if ((!msg || eq != msg+3) && (!fg || eq != fg+2) && (!bg || eq != bg+2)) {
            strcpy (line, garbcmd);
            return (false);
        }
    }

    // if msg, terminate at next & if any
    if (msg) {
        msg += 4;                  // skip msg=
        char *amp = strchr (msg, '&');
        if (amp)
            *amp = '\0';
    }

    // crack fg if found
    uint16_t fg_c = 0;
    if (fg) {
        fg += 3;        // skip fg=
        int r, g, b;
        if (sscanf (fg, _FX("%d,%d,%d"), &r, &g, &b) != 3 || r < 0 || r > 255 || g < 0 || g > 255
                        || b < 0 || b > 255) {
            strcpy (line, garbcmd);
            return (false);
        }
        fg_c = RGB565(r,g,b);
    }

    // crack bg if found
    uint16_t bg_c = 0;
    bool rainbow = false;
    if (bg) {
        bg += 3;        // skip bg=
        if (strncmp (bg, "rainbow", 7) == 0)
            rainbow = true;
        else {
            int r, g, b;
            if (sscanf (bg, _FX("%d,%d,%d"), &r, &g, &b) != 3 || r < 0 || r > 255 || g < 0 || g > 255
                            || b < 0 || b > 255) {
                strcpy (line, garbcmd);
                return (false);
            }
            bg_c = RGB565(r,g,b);
        }
    }

    // all good: update definitions
    if (msg) {
        free (cs_info.call);
        cs_info.call = strdup(msg);
    }
    if (fg)
        cs_info.fg_color = fg_c;
    if (bg) {
        if (rainbow)
            cs_info.bg_rainbow = 1;
        else {
            cs_info.bg_color = bg_c;
            cs_info.bg_rainbow = 0;
        }
    }

    // or restore default if no args
    if (!msg && !fg && !bg) {
        free (cs_info.call);
        cs_info.call = strdup(getCallsign());
        NVReadUInt16 (NV_CALL_FG_COLOR, &cs_info.fg_color);
        NVReadUInt16 (NV_CALL_BG_COLOR, &cs_info.bg_color);
        NVReadUInt8 (NV_CALL_BG_RAINBOW, &cs_info.bg_rainbow);
    }


    // engage
    drawCallsign (true);

    // ack
    startPlainText (client);
    client.print (_FX("ok\n"));

    return (true);
}

/* run a DemoChoice or turn off
 */
static bool setWiFiDemo (WiFiClient client, char line[])
{
    char buf[100];
    int choice;

    // crack
    if (strcmp (line, "on") == 0) {
        setDemoMode(true);
        drawScreenLock();
        strcpy (buf, "Demo mode on\n");
        Serial.print (buf);
    } else if (strcmp (line, "off") == 0) {
        setDemoMode(false);
        drawScreenLock();
        strcpy (buf, "Demo mode off\n");
        Serial.print (buf);
    } else if (sscanf (line, "n=%d", &choice) == 1) {
        if (choice >= 0 && choice < DEMO_N) {
            // turn on if not already
            if (!getDemoMode()) {
                setDemoMode(true);
                drawScreenLock();
            }

            // run it
            bool slow;
            if (runDemoChoice ((DemoChoice)choice, slow, buf)) {
                strcat (buf, "\n");
                Serial.print (buf);
            } else {
                strcpy (line, buf);
                return (false);
            }
        } else {
            sprintf (line, _FX("Demo are 0 .. %d"), DEMO_N-1);
            return (false);
        }
    } else {
        strcpy (line, garbcmd);
        return (false);
    }

    // ack
    startPlainText (client);
    client.print (buf);

    // good
    return (true);
}


/* remote command to set the DE time format and atin
 * set_defmt?fmt=[one from menu]&atin=RSAtAt|RSInAgo"
 */
static bool setWiFiDEformat (WiFiClient client, char line[])
{
    char fmt[20], atin[20];
    int ns = sscanf (line, "fmt=%20[^&]&atin=%20s", fmt, atin);
    if (ns < 1 || (ns == 1 && strchr(line,'&')) || ns > 2) {
        strcpy (line, garbcmd);
        return (false);
    }

    // search names
    int new_fmt = -1;
    for (int i = 0; i < DETIME_N; i++) {
        if (strcmp (fmt, detime_names[i]) == 0) {
            new_fmt = i;
            break;
        }
    }
    if (new_fmt < 0) {
        strcpy (line, "unknown format");
        return (false);
    }

    // if info, also allow setting in/at
    uint8_t new_desrss = desrss;
    if (ns == 2) {
        if (new_fmt != DETIME_INFO) {
            sprintf (line, "atin requires %s", detime_names[DETIME_INFO]);
            return (false);
        }
        if (strcmp (atin, _FX("RSAtAt")) == 0)
            new_desrss = DXSRSS_ATAT;
        else if (strcmp (atin, _FX("RSInAgo")) == 0)
            new_desrss = DXSRSS_INAGO;
        else {
            strcpy (line, "unknown atin");
            return (false);
        }
    }

    // set, save and update
    de_time_fmt = new_fmt;
    NVWriteUInt8(NV_DE_TIMEFMT, de_time_fmt);
    desrss = new_desrss;
    NVWriteUInt8 (NV_DE_SRSS, desrss);
    drawDEInfo();

    // ack
    startPlainText (client);
    sprintf (line, "%s %s\n", fmt, ns == 2 ? atin : "");
    client.println (line);

    // ok
    return (true);
}

/* remote command to set up alarm clock
 *   state=off|armed&time=HR:MN
 */
static bool setWiFiAlarm (WiFiClient client, char line[])
{
    // crack
    char state[10];
    int hr = 0, mn = 0;
    int ns = sscanf (line, _FX("state=%10[^&]&time=%d:%d"), state, &hr, &mn);

    // get current state
    AlarmState as;
    uint16_t hr16, mn16;
    getAlarmState (as, hr16, mn16);

    // parse
    if (ns == 1 || ns == 3) {
        if (strcmp (state, "off") == 0)
            as = ALMS_OFF;
        else if (strcmp (state, "armed") == 0)
            as = ALMS_ARMED;
        else {
            strcpy (line, "unknown state");
            return (false);
        }
    } else {
        strcpy (line, garbcmd);
        return (false);
    }
    if (ns == 3) {
        if (hr >= 0 && hr < 24 && mn >= 0 && mn < 60) {
            hr16 = hr;
            mn16 = mn;
        } else {
            strcpy (line, "invalid time");
            return (false);
        }
    }

    // engage
    setAlarmState (as, hr16, mn16);

    // ack
    startPlainText (client);
    if (as == ALMS_OFF)
        client.print (_FX("alarm off\n"));
    else {
        char buf[30];
        snprintf (buf, sizeof(buf), _FX("armed at %02d:%02d\n"), hr16, mn16);
        client.print (buf);
    }

    return (true);
}

/* remote command to set display on or off
 */
static bool setWiFiDisplayOnOff (WiFiClient client, char line[])
{
    if (brOnOffOk()) {

        // parse
        if (strcmp (line, "on") == 0)
            brightnessOn();
        else if (strcmp (line, "off") == 0)
            brightnessOff();
        else {
            strcpy (line, _FX("Specify on or off"));
            return (false);
        }

        // ack with same state
        startPlainText (client);
        FWIFIPR (client, F("display "));
        client.println (line);

        // ok
        return (true);

    } else {

        strcpy (line, notsupp);
        return (false);

    }
}

/* convert 3-letter day-of-week abbreviation to 1..7 (Sun..Sat),
 * return whether successful.
 */
static bool crackDOW (const char *daystr, int &dow)
{
        for (uint8_t i = 1; i <= DAYSPERWEEK; i++) {
            if (strncmp (dayShortStr(i), daystr, 3) == 0) {
                dow = i;
                return (true);
            }
        }
        return (false);
}

/* remote command to set display on/off/idle times
 * on=HR:MN&off=HR:MN&idle=mins&day=DOW
 */
static bool setWiFiDisplayTimes (WiFiClient client, char line[])
{

    if (brOnOffOk()) {

        // parse -- idle and dow are optional
        int on_hr, on_mn, off_hr, off_mn, idle_mins = -1, dow = -1;
        char *on = strstr (line, _FX("on="));
        char *off = strstr (line, _FX("off="));
        char *day = strstr (line, _FX("day="));
        char *idle = strstr (line, _FX("idle="));
        if (!on || sscanf (on+3, _FX("%d:%d"), &on_hr, &on_mn) != 2
                                || !off || sscanf (off+4, _FX("%d:%d"), &off_hr, &off_mn) != 2
                                || (day && !crackDOW (day+4, dow))
                                || (idle && sscanf (idle+5, "%d", &idle_mins) != 1)) {
            strcpy (line, garbcmd);
            return (false);
        }

        // pack times and validate
        uint16_t on_mins = on_hr*60 + on_mn;
        uint16_t off_mins = off_hr*60 + off_mn;
        if (on_mins >= MINSPERDAY || off_mins >= MINSPERDAY) {
            strcpy (line, _FX("Invalid time"));
            return (false);
        }

        // default today if no dow
        if (dow < 0)
            dow = DEWeekday();

        // set
        if (!setDisplayOnOffTimes (dow, on_mins, off_mins, idle_mins)) {
            strcpy (line, notsupp);
            return (false);
        }

        // ack
        startPlainText (client);
        const char hm_fmt[] = "%02d:%02d";
        char buf[50];

        FWIFIPR (client, F("On    "));
        sprintf (buf, hm_fmt, on_hr, on_mn);
        client.println (buf);

        FWIFIPR (client, F("Off   "));
        sprintf (buf, hm_fmt, off_hr, off_mn);
        client.println (buf);

        if (idle_mins >= 0) {
            FWIFIPR (client, F("Idle  "));
            client.println (idle_mins);
        }

        FWIFIPR (client, F("Day   "));
        if (day)
            snprintf (buf, sizeof(buf), "%.3s", day+4);
        else
            strcpy (buf, dayShortStr(DEWeekday()));
        client.println (buf);

        // ok
        return (true);

    } else {

        strcpy (line, notsupp);
        return (false);

    }
}


/* helper to set DE or DX from GET command: lat=XXX&lng=YYY
 * return whether all ok.
 */
static bool setWiFiNewDEDX_helper (WiFiClient client, bool new_dx, char line[])
{
    LatLong ll;

    // crack
    float lat, lng;
    if (sscanf(line, "lat=%f&lng=%f", &lat, &lng) != 2 || lng < -180 || lng >= 180 || lat < -90 || lat > 90) {
        strcpy (line, garbcmd);
        return (false);
    }
    ll.lat_d = lat;
    ll.lng_d = lng;

    // engage -- including normalization
    if (new_dx)
        newDX (ll, NULL, NULL);
    else
        newDE (ll, NULL);

    // ack with updated info as if get
    return (getWiFiDEDXInfo_helper (client, line, new_dx));
}

/* set DE from GET command: lat=XXX&lng=YYY
 * return whether all ok.
 */
static bool setWiFiNewDE (WiFiClient client, char line[])
{
    return (setWiFiNewDEDX_helper (client, false, line));
}

/* set DX from GET command: lat=XXX&lng=YYY
 * return whether all ok.
 */
static bool setWiFiNewDX (WiFiClient client, char line[])
{
    return (setWiFiNewDEDX_helper (client, true, line));
}



/* set DE or DX from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewGrid_helper (WiFiClient client, bool new_dx, char line[])
{
    Serial.println (line);

    // check and convert
    size_t linelen = strlen(line);
    if (linelen < 4 || linelen > MAID_CHARLEN-1) {
        strcpy (line, _FX("Grid must be 4 or 6 chars"));
        return (false);
    }
    LatLong ll;
    if (!maidenhead2ll (ll, line)) {
        strcpy (line, _FX("Invalid grid"));
        return (false);
    }

    // engage
    if (new_dx)
        newDX (ll, line, NULL);
    else
        newDE (ll, line);

    // ack with updated info as if get
    return (getWiFiDEDXInfo_helper (client, line, new_dx));
}

/* set DE from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewDEGrid (WiFiClient client, char line[])
{
    return (setWiFiNewGrid_helper (client, false, line));
}

/* set DX from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewDXGrid (WiFiClient client, char line[])
{
    return (setWiFiNewGrid_helper (client, true, line));
}



/* set one or more view features of the map, same as menu.
 * syntax: Style=S&Grid=G&Projection=P&RSS=on|off&Night=on|off
 * all keywords optional but require at least 1.
 */
static bool setWiFiMapView (WiFiClient client, char line[])
{
    // look for each keyword
    char *S = strstr (line, _FX("Style="));
    char *G = strstr (line, _FX("Grid="));
    char *P = strstr (line, _FX("Projection="));
    char *R = strstr (line, _FX("RSS="));
    char *N = strstr (line, _FX("Night="));

    // require at least 1
    if (!S && !G && !P && !R && !N) {
        strcpy_P (line, PSTR("bad args"));
        return (false);
    }

    // look for unknown keywords
    for (char *sep = line-1, *kw = line; sep != NULL; sep = strchr (kw, '&'), kw = sep + 1) {
        if (S != kw && G != kw && P != kw && R != kw && N != kw) {
            strcpy_P (line, PSTR("unknown keyword"));
            return (false);
        }
    }

    // sscanf buffer and matching safe sscanf format
    char buf[20];
    const char sfmt[] = "%20[^&]";

    // check style
    CoreMaps my_cm = CM_NONE;
    if (S) {
        if (sscanf (S+6, sfmt, buf) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        for (int i = 0; i < CM_N; i++) {
            if (strcmp (buf, map_styles[i]) == 0) {
                my_cm = (CoreMaps) i;
                break;
            }
        }
        if (my_cm == CM_NONE) {
            strcpy_P (line, PSTR("unknown style"));
            return (false);
        }
    }

    // check grid
    int my_llg = -1;
    if (G) {
        if (sscanf (G+5, sfmt, buf) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        for (int i = 0; i < MAPGRID_N; i++) {
            if (strcmp (buf, grid_styles[i]) == 0) {
                my_llg = i;
                break;
            }
        }
        if (my_llg < 0) {
            strcpy_P (line, PSTR("unknown grid"));
            return (false);
        }
    }

    // check projection
    int my_azm = -1;
    if (P) {
        if (sscanf (P+11, sfmt, buf) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        if (!strcmp (buf, _FX("Azimuthal")))
            my_azm = 1;
        else if (!strcmp (buf, _FX("Mercator")))
            my_azm = 0;
        else {
            strcpy_P (line, PSTR("unknown projection"));
            return (false);
        }
    }

    // check RSS
    int my_rss = -1;
    if (R) {
        if (sscanf (R+4, sfmt, buf) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        if (!strcmp (buf, "on"))
            my_rss = 1;
        else if (!strcmp (buf, "off"))
            my_rss = 0;
        else {
            strcpy_P (line, PSTR("unknown RSS"));
            return (false);
        }
    }

    // check Night
    int my_night = -1;
    if (N) {
        if (sscanf (N+6, sfmt, buf) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        if (!strcmp (buf, "on"))
            my_night = 1;
        else if (!strcmp (buf, "off"))
            my_night = 0;
        else {
            strcpy_P (line, PSTR("unknown Night"));
            return (false);
        }
    }

    // all options look good, engage any that have changed.
    // this is rather like drawMapMenu().

    bool full_redraw = false;
    if (S && (my_cm != core_map || prop_map != PROP_MAP_OFF)) {
        // just schedule for updating
        scheduleNewCoreMap (my_cm);
    }
    if (G && my_llg != mapgrid_choice) {
        mapgrid_choice = my_llg;
        NVWriteUInt8 (NV_LLGRID, mapgrid_choice);
        full_redraw = true;
    }
    if (P && my_azm != azm_on) {
        azm_on = my_azm;
        NVWriteUInt8 (NV_AZIMUTHAL_ON, azm_on);
        full_redraw = true;
    }
    if (N && my_night != night_on) {
        night_on = my_night;
        NVWriteUInt8 (NV_NIGHT_ON, night_on);
        full_redraw = true;
    }
    if (R && my_rss != rss_on) {
        rss_on = my_rss;
        NVWriteUInt8 (NV_RSS_ON, rss_on);
        if (!full_redraw) {
            // minimal change if don't need to restart whole map
            if (rss_on)
                drawRSSBox();
            else
                eraseRSSBox();
        }
    }

    // restart map if enough has changed
    if (full_redraw)
        initEarthMap();

    // ack
    startPlainText (client);
    strncpySubChar (line, line, '\n', '&', strlen(line));
    client.println(line);

    // good
    return (true);
}

/* set a new correction for the given sensor
 *   sensor=76|77&dTemp=X&dPres=Y" },
 */
static bool setWiFiSensorCorr (WiFiClient client, char line[])
{
    if (getNBMEConnected() == 0) {
        strcpy (line, _FX("No sensors"));
        return (false);
    }

    // look for each keyword, if any
    char *S = strstr (line, _FX("sensor="));
    char *T = strstr (line, _FX("dTemp="));
    char *P = strstr (line, _FX("dPres="));

    // look for unknown keywords
    for (char *sep = line-1, *kw = line; sep != NULL; sep = strchr (kw, '&'), kw = sep + 1) {
        if (*kw && S != kw && T != kw && P != kw) {
            strcpy (line, garbcmd);
            return (false);
        }
    }

    // sensor is required
    if (!S) {
        strcpy (line, "missing sensor");
        return (false);
    }
    int sensor = atoi (S+7);
    if (sensor == 76)
        sensor = BME_76;
    else if (sensor == 77)
        sensor = BME_77;
    else {
        strcpy (line, "sensor must be 76 or 77");
        return (false);
    }

    // at least one of T and P are required
    if (!T && !P) {
        strcpy (line, "missing delta");
        return (false);
    }

    // try dPres if set
    if (P) {
        if (!setBMEPresCorr(sensor, atof(P+6))) {
            strcpy (line, "bad dPres sensor");
            return (false);
        }
    }

    // try dTemp if set
    if (T) {
        if (!setBMETempCorr(sensor, atof(T+6))) {
            strcpy (line, "bad dTemp sensor");
            return (false);
        }
    }

    // ack
    startPlainText (client);
    client.print("Ok\n");

    // good
    return (true);
}

/* control RSS list:
 *    reset      empty local list
 *    add=X      add 1 to local list
 *    network    resume network connection
 *    file       list of titles follows header in POST format
 *    interval   set refresh interval, secs, min 5
 */
static bool setWiFiRSS (WiFiClient client, char line[])
{
    StackMalloc buf_mem(150);
    char *buf = (char *) buf_mem.getMem();
    int n_titles, n_max;

    // set buf initially empty, fill with default reply if still empty after processing
    buf[0] = '\0';

    // check args -- full buf with suitable message
    if (strcmp (line, "network") == 0) {
        // restore normal rss network queries
        (void) setRSSTitle (NULL, n_titles, n_max);
        strcpy (buf, "Restored RSS network feeds\n");
    } else if (strcmp (line, "reset") == 0) {
        // turn off network and empty local list
        (void) setRSSTitle ("", n_titles, n_max);
    } else if (strncmp (line, "add=", 4) == 0) {
        // turn off network and add title to local list if room
        if (setRSSTitle (line+4, n_titles, n_max)) {
        } else {
            sprintf (line, "List is full -- max %d", n_max);
            return (false);
        }
    } else if (strcmp (line, "file") == 0) {
        // titles follow header
        (void) setRSSTitle ("", n_titles, n_max);       // reset list
        while (getTCPLine (client, buf, buf_mem.getSize(), NULL))
            (void) setRSSTitle (buf, n_titles, n_max);
        buf[0] = '\0';                                  // want default reply
    } else if (strncmp (line, "interval=", 9) == 0) {
        int new_i = atoi(line+9);
        if (new_i >= RSS_MIN_INT) {
            rss_interval = new_i;
            snprintf (buf, buf_mem.getSize(), "RSS interval now %d secs\n", rss_interval);
            NVWriteUInt8 (NV_RSS_INTERVAL, rss_interval);
        } else {
            sprintf (line, "Min interval %d seconds", RSS_MIN_INT);
            return (false);
        }
    } else {
        strcpy (line, garbcmd);
        return (false);
    }

    // create default reply if buf empty
    if (buf[0] == '\0')
        snprintf (buf, buf_mem.getSize(), "Now %d of %d local titles\n", n_titles, n_max);

    // ack
    startPlainText (client);
    client.print (buf);
    Serial.print (buf);

    // ok
    return (true);
}

/* set new collection of plot choices for a given pane.
 * return whether ok
 */
static bool setWiFiPane (WiFiClient client, char line[])
{
    // first arg is 1-based pane number
    int pane_1;
    char *equals;               // 
    if (sscanf (line, "Pane%d", &pane_1) != 1 || (equals = strchr(line,'=')) == NULL) {
        strcpy (line, garbcmd);
        return (false);
    }

    // convert pane_1 to PlotPane
    if (pane_1 < 1 || pane_1 > PANE_N) {
        strcpy_P (line, PSTR("Bad pane num"));
        return (false);
    }
    PlotPane pp = (PlotPane)(pane_1-1);

    // convert remaining args to list of PlotChoices
    PlotChoice pc[PLOT_CH_N];           // max size, only first n_pc in use
    int n_pc = 0;
    char *start = equals + 1;
    for (char *tok = NULL; (tok = strtok (start, ",")) != NULL; start = NULL) {

        // tok is with line, so copy it so we can use line for err msg
        char tok_copy[strlen(tok)+1];
        strcpy (tok_copy, tok);

        // find tok in plot_names
        PlotChoice tok_pc = PLOT_CH_NONE;
        for (int i = 0; i < PLOT_CH_N; i++) {
            if (strcmp (tok_copy, plot_names[i]) == 0) {
                tok_pc = (PlotChoice)i;
                break;
            }
        }

        // found?
        if (tok_pc == PLOT_CH_NONE) {
            sprintf (line, _FX("Unknown choice for pane %d: %s"), pane_1, tok_copy);
            return (false);
        }

        // in use elsewhere?
        PlotPane inuse_pp = findPaneForChoice(tok_pc);
        if (inuse_pp != PANE_NONE && inuse_pp != pp) {
            sprintf (line, _FX("%s already set in pane %d"), tok_copy, (int)inuse_pp+1);
            return (false);
        }

        // available?
        if (!plotChoiceIsAvailable(tok_pc)) {
            sprintf (line, _FX("%s is not available"), tok_copy);
            return (false);
        }

        // room for more?
        if (n_pc == PLOT_CH_N) {
            sprintf (line, _FX("too many choices"));
            return (false);
        }

        // ok!
        pc[n_pc++] = tok_pc;
    }

    // require at least 1
    if (n_pc == 0) {
        sprintf (line, _FX("specify at least one choice for pane %d"), pane_1);
        return (false);
    }

    // show first in list
    if (!setPlotChoice (pp, pc[0])) {
        sprintf (line, _FX("%s failed for pane %d"), plot_names[pc[0]], pane_1);
        return (false);
    }

    // worked so build new rotset
    plot_rotset[pp] = 0;
    for (int i = 0; i < n_pc; i++)
        plot_rotset[pp] |= (1 << pc[i]);

    // persist
    logPaneRotSet(pp, pc[0]);
    savePlotOps();

    // ok!
    startPlainText (client);
    reportPaneChoices (client, pp);

    // good
    return (true);
}



/* try to set the satellite to the given name.
 * return whether command is successful.
 */
static bool setWiFiSatName (WiFiClient client, char line[])
{
    resetWatchdog();

    // do it
    if (setSatFromName (line))
        return (getWiFiSatellite (client, line));

    // nope
    strcpy (line, _FX("Unknown sat"));
    return (false);
}

/* set satellite from given TLE: set_sattle?name=n&t1=line1&t2=line2
 * return whether command is successful.
 */
static bool setWiFiSatTLE (WiFiClient client, char line[])
{
    resetWatchdog();

    // find components
    char *name = strstr (line, "name=");
    char *t1 = strstr (line, "&t1=");
    char *t2 = strstr (line, "&t2=");
    if (!name || !t1 || !t2) {
        strcpy (line, garbcmd);
        return (false);
    }

    // break into proper separate strings
    name += 5; *t1 = '\0';
    t1 += 4; *t2 = '\0';
    t2 += 4;

    // enforce known line lengths
    size_t t1l = strlen(t1);
    if (t1l < TLE_LINEL-1) {
        strcpy (line, _FX("t1 short"));
        return(false);
    }
    t1[TLE_LINEL-1] = '\0';
    size_t t2l = strlen(t2);
    if (t2l < TLE_LINEL-1) {
        strcpy (line, _FX("t2 short"));
        return(false);
    }
    t2[TLE_LINEL-1] = '\0';

    // try to install
    if (setSatFromTLE (name, t1, t2))
        return (getWiFiSatellite (client, line));

    // nope
    strcpy (line, _FX("Bad spec"));
    return (false);
}

/* remote command to control stopwatch engine state
 */
static bool setWiFiStopwatch (WiFiClient client, char line[])
{
    // crack
    SWEngineState sws;
    int mins;
    if (sscanf (line, "countdown=%d", &mins) == 1)
        sws = SWE_COUNTDOWN;
    else if (strcmp (line, "reset") == 0)
        sws = SWE_RESET;
    else if (strcmp (line, "run") == 0)
        sws = SWE_RUN;
    else if (strcmp (line, "stop") == 0)
        sws = SWE_STOP;
    else if (strcmp (line, "lap") == 0)
        sws = SWE_LAP;
    else {
        strcpy (line, garbcmd);
        return (false);
    }

    // engage
    if (!setSWEngineState (sws, mins*60000)) {        // mins -> ms
        strcpy (line, _FX("State is not applicable"));
        return (false);
    }

    // turn off any PLOT_CH_COUNTDOWN if no longer applicable
    insureCountdownPaneSensible();

    // ack
    return (getWiFiStopwatch (client, line));
}

/* set clock time from any of three formats:
 *  ISO=YYYY-MM-DDTHH:MM:SS
 *  unix=s
 *  Now
 * return whether command is fully recognized.
 */
static bool setWiFiTime (WiFiClient client, char line[])
{
    resetWatchdog();

    int yr, mo, dy, hr, mn, sc;

    if (strcmp (line, "Now") == 0) {

        changeTime (0);

    } else if (strncmp (line, "UNIX=", 5) == 0) {

        // crack and engage
        changeTime (atol(line+5));

    } else if (sscanf (line, _FX("ISO=%d-%d-%dT%d:%d:%d"), &yr, &mo, &dy, &hr, &mn, &sc) == 6) {

        // reformat
        tmElements_t tm;
        tm.Year = yr - 1970;
        tm.Month = mo;
        tm.Day = dy;
        tm.Hour = hr;
        tm.Minute = mn;
        tm.Second = sc;

        // convert and engage
        changeTime (makeTime(tm));

    } else {

        strcpy (line, garbcmd);
        return (false);
    }

    // reply
    startPlainText(client);
    char buf[30];
    snprintf (buf, sizeof(buf), "UNIX_time %ld\n", nowWO());
    client.print (buf);

    return (true);
}

/* perform a touch screen action based on coordinates received via wifi GET
 * return whether all ok.
 */
static bool setWiFiTouch (WiFiClient client, char line[])
{
    // crack raw screen x and y and optional hold
    int x, y, h = 0;
    if (sscanf (line, _FX("%*[xX]=%d&%*[yY]=%d&hold=%d"), &x, &y, &h) < 2) {
        strcpy (line, garbcmd);
        return (false);
    }

    // must be over display
    if (x < 0 || x >= tft.width() || y < 0 || y >= tft.height()) {
        strcpy (line, _FX("Invalid range"));
        return (false);
    }

    // inform checkTouch() to use wifi_tt_s; it will reset
    wifi_tt_s.x = x;
    wifi_tt_s.y = y;
    wifi_tt = h ? TT_HOLD : TT_TAP;

    // ack
    startPlainText (client);
    FWIFIPR (client, F("Touch_x ")); client.println (wifi_tt_s.x);
    FWIFIPR (client, F("Touch_y ")); client.println (wifi_tt_s.y);

    // ok
    return (true);
}

/* set the VOACAP map to the given band and/or power and/or timeline units
 * return whether all ok.
 */
static bool setWiFiVOACAP (WiFiClient client, char line[])
{
    // look for each keyword, if any
    char *B = strstr (line, _FX("band="));
    char *P = strstr (line, _FX("power="));
    char *T = strstr (line, _FX("tl="));

    // look for unknown keywords
    for (char *sep = line-1, *kw = line; sep != NULL; sep = strchr (kw, '&'), kw = sep + 1) {
        if (*kw && P != kw && B != kw && T != kw) {
            strcpy (line, garbcmd);
            return (false);
        }
    }

    // crack band
    PropMapSetting new_pms = prop_map;
    if (B) {
        int band = 0;
        if (sscanf (B+5, "%d", &band) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        // find its PROP_MAP
        bool found = false;
        for (int i = 0; i < PROP_MAP_N; i++) {
            if (propMap2Band((PropMapSetting)i) == band) {
                new_pms = (PropMapSetting)i;
                found = true;
                break;
            }
        }
        if (!found) {
            strcpy (line, _FX("Invalid band"));
            return (false);
        }
    }

    // crack power
    int new_power = bc_power;
    if (P) {
        int p = atoi(P+6);
        if (p != 1 && p != 10 && p != 100 && p != 1000) {
            strcpy (line, _FX("Invalid power"));
            return (false);
        }
        new_power = p;
    }

    // crack timeline
    int new_utc = bc_utc_tl;
    if (T) {
        if (strncmp(T+3, _FX("UTC"), 3) == 0) {
            new_utc = 1;
        } else if (strncmp(T+3, _FX("DE"), 2) == 0) {
            new_utc = 0;
        } else {
            strcpy (line, _FX("tl must be DE or UTC"));
            return (false);
        }
    }

    // engage or revert
    PlotPane bc_pp = findPaneChoiceNow (PLOT_CH_BC);
    if (B || P) {
        // new band or power so schedule fresh map and BC as required
        bc_power = new_power;
        scheduleNewVOACAPMap (new_pms);
        scheduleNewBC();        // this will also update time line
    } else if (T) {
        // only changing timeline units so just redraw if different
        if (new_utc != bc_utc_tl) {
            bc_utc_tl = new_utc;
            plotBandConditions (plot_b[bc_pp], 0, NULL, NULL);
        }
    } else {
        // off: schedule default core map and update BC pane if up
        scheduleNewVOACAPMap (PROP_MAP_OFF);
        scheduleNewCoreMap (core_map);
        if (bc_pp != PANE_NONE)
            plotBandConditions (plot_b[bc_pp], 0, NULL, NULL);
    }

    // ack
    startPlainText (client);
    char buf[50];
    size_t l = snprintf (buf, sizeof(buf), _FX("VOACAP "));
    if (new_pms == PROP_MAP_OFF)
        l += snprintf (buf+l, sizeof(buf)-l, _FX("map off"));
    else
        l += snprintf (buf+l, sizeof(buf)-l, _FX("band %d m"), propMap2Band(new_pms));
    l += snprintf (buf+l, sizeof(buf)-l, _FX(", power %d W, timeline %s"), bc_power,bc_utc_tl?"UTC":"DE");
    client.println(buf);

    // ok
    return (true);
}



/* finish the wifi then restart
 */
static bool doWiFiReboot (WiFiClient client, char *unused)
{
    (void) unused;

    // send html header then close
    startPlainText(client);
    FWIFIPRLN (client, F("restarting ... bye for now."));
    wdDelay(100);
    client.flush();
    client.stop();
    wdDelay(1000);

    Serial.println (F("restarting..."));
    reboot();

    // never returns but compiler doesn't know that
    return (true);
}

/* update firmware if available
 */
static bool doWiFiUpdate (WiFiClient client, char *unused)
{
    (void) unused;

    // prep for response but won't be one if we succeed with update
    startPlainText(client);

    // proceed if newer version is available
    char ver[50];
    if (newVersionIsAvailable (ver, sizeof(ver))) {
        char msg[100];
        snprintf (msg, sizeof(msg), "updating from %s to %s ... \n", hc_version, ver);
        client.print(msg);
        doOTAupdate(ver);                               // never returns if successful
        FWIFIPRLN (client, F("update failed"));
    } else
        FWIFIPRLN (client, F("You're up to date!"));    // match tapping version

    return (true);
}


#if defined(_IS_UNIX)
/* exit
 */
static bool doWiFiExit (WiFiClient client, char *unused)
{
    (void) unused;

    // ack then die
    startPlainText(client);
    FWIFIPRLN (client, F("exiting"));

    Serial.print (F("Exiting\n"));
    setFullBrightness();
    eraseScreen();
    wdDelay(500);
    exit(0);

    // lint
    return (true);
}
#endif // defined(_IS_UNIX)



/* table of command strings, its implementing function and additional info for help.
 * functions are called with user input string beginning just after the command and sans HTTP.
 * N.B. functions returning false shall replace the input string with a brief error message.
 *      functions returning true shall send http reply to client.
 *      get_ commands shall include trailing space to detect and prevent trailing garbage.
 *      table is down here so all handlers are already conveniently defined above.
 *      last N_UNDOCCMD entries are not shown with help
 * strings are in arrays so they are in ESP FLASH too.
 */
#define CT_MAX_CMD      30                              // max command string length
#define CT_MAX_HELP     60                              // max help string length
#define CT_FUNP(ctp) ((PCTF)pgm_read_dword(&ctp->funp)) // handy function pointer
typedef bool (*PCTF)(WiFiClient client, char *line);  // ptr to command table function
typedef struct {
    const char command[CT_MAX_CMD];                     // command string
    PCTF funp;                                          // handler function
    const char help[CT_MAX_HELP];                       // more info if available
} CmdTble;
static const CmdTble command_table[] PROGMEM = {
    { "get_capture.bmp ",   getWiFiScreenCapture,  "get live screen shot" },
    { "get_config.txt ",    getWiFiConfig,         "get current display options" },
    { "get_de.txt ",        getWiFiDEInfo,         "get DE info" },
    { "get_dx.txt ",        getWiFiDXInfo,         "get DX info" },
    { "get_dxspots.txt ",   getWiFiDXSpots,        "get DX spots" },
    { "get_satellite.txt ", getWiFiSatellite,      "get current sat info" },
    { "get_satellites.txt ",getWiFiAllSatellites,  "get list of all sats" },
    { "get_sensors.txt ",   getWiFiSensorData,     "get sensor data" },
    { "get_spacewx.txt ",   getWiFiSpaceWx,        "get space weather info" },
    { "get_stopwatch.txt ", getWiFiStopwatch,      "get stopwatch state" },
    { "get_sys.txt ",       getWiFiSys,            "get system stats" },
    { "get_time.txt ",      getWiFiTime,           "get current time" },
    { "set_alarm?",         setWiFiAlarm,          "state=off|armed&time=HR:MN" },
    { "set_defmt?",         setWiFiDEformat,       "fmt=[one from menu]&atin=RSAtAt|RSInAgo" },
    { "set_displayOnOff?",  setWiFiDisplayOnOff,   "on|off" },
    { "set_displayTimes?",  setWiFiDisplayTimes,   "on=HR:MN&off=HR:MN&day=DOW&idle=mins" },
    { "set_mapview?",       setWiFiMapView,        "Style=S&Grid=G&Projection=P&RSS=on|off&Night=on|off" },
    { "set_newde?",         setWiFiNewDE,          "lat=X&lng=Y" },
    { "set_newdegrid?",     setWiFiNewDEGrid,      "AB12" },
    { "set_newdx?",         setWiFiNewDX,          "lat=X&lng=Y" },
    { "set_newdxgrid?",     setWiFiNewDXGrid,      "AB12" },
    { "set_pane?",          setWiFiPane,           "Pane[123]=X,Y,Z... any from:" },
    { "set_rss?",           setWiFiRSS,            "reset|add=X|file|network|interval=secs" },
    { "set_satname?",       setWiFiSatName,        "abc|none" },
    { "set_sattle?",        setWiFiSatTLE,         "name=abc&t1=line1&t2=line2" },
    { "set_senscorr?",      setWiFiSensorCorr,     "sensor=76|77&dTemp=X&dPres=Y" },
    { "set_stopwatch?",     setWiFiStopwatch,      "reset|run|stop|lap|countdown=mins" },
    { "set_time?",          setWiFiTime,           "ISO=YYYY-MM-DDTHH:MM:SS" },
    { "set_time?",          setWiFiTime,           "Now" },
    { "set_time?",          setWiFiTime,           "unix=secs_since_1970" },
    { "set_title?",         setWiFiTitle,          "msg=hello&fg=R,G,B&bg=R,G,B|rainbow" },
    { "set_touch?",         setWiFiTouch,          "x=X&y=Y&hold=0|1" },
    { "set_voacap?",        setWiFiVOACAP,         "band=80-10&power=p&tl=DE/UTC" },
    { "restart ",           doWiFiReboot,          "restart HamClock" },
    { "updateVersion ",     doWiFiUpdate,          "update to latest version"},

#if defined(_IS_UNIX)
    { "exit ",              doWiFiExit,            "exit HamClock" },
#endif // defined(_IS_UNIX)

    // the following entries are not shown with help -- update N_UNDOCCMD if change
    { "set_demo?",          setWiFiDemo,           "on|off|n=N" },
};

#define N_CMDTABLE      NARRAY(command_table)           // n entries in command table
#define N_UNDOCCMD      1                               // n undoc commands at end of table

/* return whether the given command is allowed in read-only web service
 */
static bool roCommand (const char *cmd)
{
    return (strncmp (cmd, "get_", 4) == 0
                    || strncmp (cmd, "set_alarm", 9) == 0
                    || strncmp (cmd, "set_stopwatch", 13) == 0
                    || strncmp (cmd, "set_touch", 9) == 0);
}


/* run the given web server command.
 * send ack or error messages to client.
 * return whether command was found, regardless of whether it returned an error.
 */
static bool runWebserverCommand (WiFiClient client, bool ro, char *command)
{
    // search for command depending on context, execute its implementation function if found
    if (!ro || roCommand (command)) {
        resetWatchdog();
        for (unsigned i = 0; i < N_CMDTABLE; i++) {
            const CmdTble *ctp = &command_table[i];
            size_t cmd_len = strlen_P (ctp->command);
            if (strncmp_P (command, ctp->command, cmd_len) == 0) {

                // found command, skip to start of args
                char *args = command+cmd_len;

                // replace any %XX encoded values
                if (replaceEncoding (args))
                    Serial.printf ("Decoded: %s\n", args);      // print decoded version

                // chop off trailing HTTP _after_ looking for commands because get_ commands end with blank.
                char *http = strstr (args, " HTTP");
                if (http)
                    *http = '\0';

                // run handler, passing string starting right after the command, reply with error if trouble.
                PCTF funp = CT_FUNP(ctp);
                if (!(*funp)(client, args)) {
                    StackMalloc errmsg(strlen(args)+20);
                    snprintf (errmsg.getMem(), errmsg.getSize(), "Error: %s", args);
                    sendHTTPError (client, errmsg.getMem());
                }

                // command found, even if it reported an error
                return (true);
            }
        }
    }

    // not found (or allowed)
    return (false);
}

/* service remote connection.
 * if ro, only accept get commands and set_touch
 */
static void serveRemote(WiFiClient client, bool ro)
{
    StackMalloc line_mem(TLE_LINEL*4);          // accommodate longest query, probably set_sattle with %20s
    char *line = (char *) line_mem.getMem();    // handy access to malloced buffer

    // first line must be the GET except set_rss which can be POST
    if (!getTCPLine (client, line, line_mem.getSize(), NULL)) {
        sendHTTPError (client, "empty web query");
        goto out;
    }
    if (strncmp (line, "GET /", 5) && strncmp (line, "POST /set_rss?", 14)) {
        Serial.println (line);
        sendHTTPError (client, "Method must be GET (or POST with set_rss)");
        goto out;
    }

    // discard remainder of header
    (void) httpSkipHeader (client);

    // log
    Serial.print (F("Command from "));
        Serial.print(client.remoteIP());
        Serial.print(F(": "));
        Serial.println(line);

    // run command
    if (runWebserverCommand (client, ro, strchr (line,'/')+1))
        goto out;

    // if get here, command was not found so list help
    startPlainText(client);
    for (uint8_t i = 0; i < N_CMDTABLE-N_UNDOCCMD; i++) {
        const CmdTble *ctp = &command_table[i];

        // skip if not available for ro
        char ramcmd[CT_MAX_CMD];
        strcpy_P (ramcmd, ctp->command);
        if (ro && !roCommand(ramcmd))
            continue;

        // command followed by help in separate column
        const int indent = 30;
        int cmd_len = strlen (ramcmd);
        client.print (ramcmd);
        sprintf (line, "%*s", indent-cmd_len, "");
        client.print (line);
        client.println (FPSTR(ctp->help));

        // also list pane choices for setWiFiPane
        PCTF funp = CT_FUNP(ctp);
        if (funp == setWiFiPane) {
            const int max_w = 70;
            const char indent[] = "  ";
            int ll = 0;
            for (int i = 0; i < PLOT_CH_N; i++) {
                if (ll == 0)
                    ll = sprintf (line, "%s", indent);
                ll += sprintf (line+ll, " %s", plot_names[i]);
                if (ll > max_w) {
                    client.println (line);
                    ll = 0;
                }
            }
            client.println (line);
        }
    }

  out:

    client.stop();
    printFreeHeap (F("serveRemote"));
}

void checkWebServer()
{
    // check if someone is trying to tell/ask us something
    if (remoteServer) {
        WiFiClient client = remoteServer->available();
        if (client)
            serveRemote(client, false);
    }
}

bool initWebServer(char ynot[])
{
    resetWatchdog();

    if (remoteServer) {
        remoteServer->stop();
        delete remoteServer;
    }

    remoteServer = new WiFiServer(svr_port);

    #if defined(_IS_ESP8266)
        // Arduino version returns void
        (void) ynot;
        remoteServer->begin();
        return (true);
    #else
        return (remoteServer->begin(ynot));
    #endif

}

/* like readCalTouch() but also checks for remote web server touch.
 */
TouchType readCalTouchWS (SCoord &s)
{
    // check for read-only remote commands
    // N.B. might be called before server is set up, eg, a very early fatalError
    if (remoteServer) {
        WiFiClient client = remoteServer->available();
        if (client)
            serveRemote (client, true);
    }

    // return info for remote else local touch
    TouchType tt;
    if (wifi_tt != TT_NONE) {
        s = wifi_tt_s;
        tt = wifi_tt;
        wifi_tt = TT_NONE;
    } else {
        tt = readCalTouch (s);
    }

    // return event type
    return (tt);
}


/* called from main loop() to run another demo command if time.
 */
void runNextDemoCommand()
{
    // out fast if not using this mode
    if (!getDemoMode())
        return;

    // wait for :15 or :45 unless previous was slow on ESP, and beware checking again during same second
    static bool prev_slow;
    static time_t prev_t0;
    time_t t0 = nowWO();
    int t060 = t0 % 60;
    #if defined(_IS_ESP8266)
        if ( t0 == prev_t0 || ! (t060 == 15 || (t060 == 45 && !prev_slow)) )
            return;
    #else
        if ( t0 == prev_t0 || ! (t060 == 15 || t060 == 45) )
            return;
    #endif
    prev_t0 = t0;

    // list of probabilities for each DemoChoice, must sum to 100
    static const uint8_t item_probs[DEMO_N] = {

                // 0
        8,      // DEMO_PANE1
        8,      // DEMO_PANE2
        8,      // DEMO_PANE3
        4,      // DEMO_RSS
        11,     // DEMO_NEWDX

                // 5
        2,      // DEMO_MAPPROJ
        2,      // DEMO_MAPNIGHT
        2,      // DEMO_MAPGRID
        1,      // DEMO_MAPSTYLE
        8,      // DEMO_NCDXF
                
                // 10
        1,      // DEMO_VOACAP
        9,      // DEMO_CALLFG
        9,      // DEMO_CALLBG
        14,     // DEMO_DEFMT
        4,      // DEMO_ONAIR

                // 15
        5,      // DEMO_SAT
        4       // DEMO_EME
    };

    // record previous choice to avoid repeats
    static DemoChoice prev_choice = DEMO_N;             // init to an impossible choice

    // confirm propabilities sum to 100 on first call
    if (prev_choice == DEMO_N) {
        unsigned sum = 0;
        for (unsigned i = 0; i < DEMO_N; i++)
            sum += item_probs[i];
        if (sum != 100)
            fatalError (_FX("Bug! demo probs sum %u != 100\n"), sum);
    }

    // attempt a change until successful.
    bool ok = false;
    do {
        // make next choice, never repeat
        DemoChoice choice;
        do {
            unsigned p = random(100);
            unsigned sum = 0;
            choice = (DemoChoice)(DEMO_N - 1);          // default is end of prob list
            for (unsigned i = 0; i < DEMO_N-1; i++) {
                if (p < (sum += item_probs[i])) {
                    choice = (DemoChoice) i;
                    break;
                }
            }
        } while (choice == prev_choice);
        prev_choice = choice;

        // run choice
        char msg[100];
        ok = runDemoChoice (choice, prev_slow, msg);
        Serial.println (msg);

    } while (!ok);
}

/* handy helper to consistently format a demo command response into buf[]
 */
static void demoMsg (bool ok, int n, char buf[], const char *fmt, ...)
{
    // format the message
    char msg[100];
    va_list ap;
    va_start (ap, fmt);
    vsnprintf (msg, sizeof(msg), fmt, ap);
    va_end (ap);

    // save with boilerplate to buf
    sprintf (buf, _FX("Demo %d @ %ld %s: %s"), n, getUptime(NULL,NULL,NULL,NULL), ok ? "Ok" : "No", msg);
}

/* run the given DemoChoice. 
 * return whether appropriate and successful and, if so, whether it is likely to be slower than usual.
 */
static bool runDemoChoice (DemoChoice choice, bool &slow, char msg[])
{
    // assume not
    slow = false;

    // init bad
    bool ok = false;

    switch (choice) {
    case DEMO_PANE1:
        {
            PlotChoice pc = getAnyAvailableChoice();
            ok = setPlotChoice (PANE_1, pc);
            if (ok) {
                plot_rotset[PANE_1] = (1 << pc);   // no auto rotation
                savePlotOps();
            }
            demoMsg (ok, choice, msg, _FX("Pane 1 %s"), plot_names[pc]);
        }
        break;

    case DEMO_PANE2:
        {
            PlotChoice pc = getAnyAvailableChoice();
            ok = setPlotChoice (PANE_2, pc);
            if (ok) {
                plot_rotset[PANE_2] = (1 << pc);   // no auto rotation
                savePlotOps();
            }
            demoMsg (ok, choice, msg, _FX("Pane 2 %s"), plot_names[pc]);
        }
        break;

    case DEMO_PANE3:
        {
            PlotChoice pc = getAnyAvailableChoice();
            ok = setPlotChoice (PANE_3, pc);
            if (ok) {
                plot_rotset[PANE_3] = (1 << pc);   // no auto rotation
                savePlotOps();
            }
            demoMsg (ok, choice, msg, _FX("Pane 3 %s"), plot_names[pc]);
        }
        break;

    case DEMO_RSS:
        rss_on = !rss_on;
        NVWriteUInt8 (NV_RSS_ON, rss_on);
        if (rss_on)
            drawRSSBox();
        else
            eraseRSSBox();
        ok = true;
        demoMsg (ok, choice, msg, "RSS %s", rss_on ? "On" : "Off");
        break;

    case DEMO_NEWDX:
        {
            // avoid poles just for aesthetics
            LatLong ll;
            ll.lat_d = random(120)-60;
            ll.lng_d = random(359)-180;
            newDX (ll, NULL, NULL);
            ok = true;
            demoMsg (ok, choice, msg, _FX("NewDX %g %g"), ll.lat_d, ll.lng_d);
        }
        break;

    case DEMO_MAPPROJ:
        azm_on = !azm_on;
        NVWriteUInt8 (NV_AZIMUTHAL_ON, azm_on);
        initEarthMap();
        if (azm_on)
            slow = true;
        ok = true;
        demoMsg (ok, choice, msg, "Proj %s", azm_on ? "Azm" : "Mer");
        break;

    case DEMO_MAPNIGHT:
        night_on = !night_on;
        NVWriteUInt8 (NV_NIGHT_ON, night_on);
        initEarthMap();
        if (azm_on)
            slow = true;
        ok = true;
        demoMsg (ok, choice, msg, "Night %s", night_on ? "On" : "Off");
        break;

    case DEMO_MAPGRID:
        mapgrid_choice = (mapgrid_choice + 1) % MAPGRID_N;
        NVWriteUInt8 (NV_LLGRID, mapgrid_choice);
        initEarthMap();
        if (azm_on)
            slow = true;
        ok = true;
        demoMsg (ok, choice, msg, "Grid %s", grid_styles[mapgrid_choice]);
        break;

    case DEMO_MAPSTYLE:
        core_map = (CoreMaps)(((int)core_map + 1) % CM_N);
        scheduleNewCoreMap (core_map);
        slow = true;
        ok = true;
        demoMsg (ok, choice, msg, "Map %s", map_styles[core_map]);
        break;

    case DEMO_NCDXF:
        // TODO: brightness, on/off times?
        brb_mode = brb_mode == BRB_SHOW_NOTHING ? BRB_SHOW_BEACONS : BRB_SHOW_NOTHING;
        drawBeaconBox();
        updateBeacons(true, true, true);
        ok = true;
        demoMsg (ok, choice, msg, "NCDXF %s", brb_mode == BRB_SHOW_BEACONS ? "On" : "Off");
        break;

    case DEMO_VOACAP:
        {
            // find hottest band if recently updated
            SPWxValue ssn, flux, kp, swind, drap;
            NOAASpaceWx noaaspw;
            float path[PROP_MAP_N];
            char xray[10];
            time_t noaaspw_age, xray_age, path_age;
            getSpaceWeather (ssn,flux,kp,swind,drap,noaaspw,noaaspw_age,xray,xray_age,path,path_age);
            if (path_age < 2*3600) {

                // pick band with best propagation
                float best_rel = 0;
                int best_band = 0;
                for (int i = 0; i < PROP_MAP_N; i++) {
                    if (path[i] > best_rel) {
                        best_rel = path[i];
                        best_band = i;
                    }
                }

                // engage
                scheduleNewVOACAPMap ((PropMapSetting)best_band);
                slow = true;
                ok = true;

                char ps[NV_MAPSTYLE_LEN];
                demoMsg (ok, choice, msg, "VOACAP %s", getMapStyle(ps));
            } else {
                // too old to use
                ok = false;
                demoMsg (ok, choice, msg, _FX("VOACAP too old"));
            }
        }
        break;

    case DEMO_CALLFG:
        {
            SCoord s;
            s.x = cs_info.box.x + cs_info.box.w/4;
            s.y = cs_info.box.y + cs_info.box.h/2;
            (void) checkCallsignTouchFG (s);
            NVWriteUInt16 (NV_CALL_FG_COLOR, cs_info.fg_color);
            drawCallsign (false);   // just foreground
            ok = true;
            demoMsg (ok, choice, msg, _FX("call FG 0x%02X %02X %02X"), RGB565_R(cs_info.fg_color),
                                    RGB565_G(cs_info.fg_color), RGB565_B(cs_info.fg_color));
        }
        break;

    case DEMO_CALLBG:
        {
            SCoord s;
            s.x = cs_info.box.x + 3*cs_info.box.w/4;
            s.y = cs_info.box.y + cs_info.box.h/2;
            (void) checkCallsignTouchBG (s);
            NVWriteUInt16 (NV_CALL_BG_COLOR, cs_info.bg_color);
            NVWriteUInt8 (NV_CALL_BG_RAINBOW, cs_info.bg_rainbow);
            drawCallsign (true);    // fg and bg
            ok = true;
            if (cs_info.bg_rainbow)
                demoMsg (ok, choice, msg, _FX("call BG Rainbow"));
            else
                demoMsg (ok, choice, msg, _FX("call BG 0x%02X %02X %02X"), RGB565_R(cs_info.bg_color),
                                    RGB565_G(cs_info.bg_color), RGB565_B(cs_info.bg_color));
        }
        break;

    case DEMO_DEFMT:
        de_time_fmt = (de_time_fmt + 1) % DETIME_N;
        NVWriteUInt8(NV_DE_TIMEFMT, de_time_fmt);
        drawDEInfo();
        ok = true;
        demoMsg (ok, choice, msg, "DE fmt %s", detime_names[de_time_fmt]);
        break;

    case DEMO_ONAIR:
        {
            static bool toggle;
            setOnAir (toggle = !toggle);
            ok = true;
            demoMsg (ok, choice, msg, "ONAIR %s", toggle ? "On" : "Off");
        }
        break;

    case DEMO_SAT:
        {
            float az, el, range, rate, raz, saz, rhrs, shrs;
            char name[NV_SATNAME_LEN];
            if (getSatAzElNow (name, &az, &el, &range, &rate, &raz, &saz, &rhrs, &shrs)) {
                // sat assigned, turn off
                ok = setSatFromName("none");
                demoMsg (ok, choice, msg, "Sat none");
            } else {
                // assign new sat
                static const char *sats[] = {
                    "ISS", "SO-50", "NOAA 19", "FOX-1B"
                };
                static int next_sat;
                next_sat = (next_sat + 1) % NARRAY(sats);
                ok = setSatFromName(sats[next_sat]);
                demoMsg (ok, choice, msg, "Sat %s", sats[next_sat]);
            }
            slow = true;
        }
        break;

    case DEMO_EME:
        ok = findPaneChoiceNow(PLOT_CH_MOON) != PANE_NONE;
        if (ok) {
            drawMoonElPlot();
            initEarthMap();
            slow = true;                // allow for time spent in drawMoonElPlot
        }
        demoMsg (ok, choice, msg, "EME");
        break;

    case DEMO_N:
        break;
    }

    return (ok);
}
