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

// LIVE
time_t last_live;                                       // last update time
static int live_verbose = 0;                            // greater is more

// persistent server for listening for remote connections
static WiFiServer *remoteServer;

// handy default message strings
static const char garbcmd[] = "Garbled command";
static const char notsupp[] = "Not supported";


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
    DEMO_AUXTIME,
    DEMO_PSKMASK,

    DEMO_N,

} DemoChoice;
static bool runDemoChoice (DemoChoice choice, bool &slow, char msg[]);

// hack around frame buffer readback weirdness that requires ignoring the very first pixel
static bool first_pixel = true;

#define MAX_WEBARGS     10

typedef struct {
    const char *name[MAX_WEBARGS];              // name to look for
    const char *value[MAX_WEBARGS];             // ptr to its value, or NULL
    bool found[MAX_WEBARGS];                    // whether this name was found in the original GET command
    int nargs;
} WebArgs;

/* remove leading and trailing white space IN PLACE, return new start.
 */
char *trim (char *str)
{
    if (!str)
        return (str);
    while (isspace(*str))
        str++;
    size_t sl = strlen (str);
    while (sl > 0 && isspace(str[sl-1]))
        str[--sl] = '\0';
    return (str);
}

static bool rgbOk (int r, int g, int b)
{
    return (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255);
}

/* set name to value in wa.
 * return whether found.
 * N.B. we accommodate name pointing within ynot
 */
static bool setWCValue (WebArgs &wa, const char *name, const char *value, char ynot[])
{
    // name is required
    if (!name || name[0] == '\0') {
        strcpy (ynot, _FX("missing name"));
        return (false);
    }

    // copy name in case is within ynot
    char *name_copy = strdup (name);

    for (int i = 0; i < wa.nargs; i++) {
        if (strcmp (name_copy, wa.name[i]) == 0) {
            if (wa.found[i]) {
                sprintf (ynot, _FX("%s set more than once"), name_copy);
                free (name_copy);
                return (false);
            }
            wa.value[i] = value;
            wa.found[i] = true;
            return (true);
        }
    }

    sprintf (ynot, _FX("unknown arg: %s"), name_copy);
    free (name_copy);
    return (false);
}

/* given wa containing all expected names, parse the given web GET command for each of their values, if any.
 * line is one or more of <empty> A or A=x separated by &.
 * we assume line[] starts just after "GET /xxx?" and has the trailing http/n removed.
 * line will be modified by changing all '&' and '=' to '\0'.
 * all value pointers will point into line[].
 * value will be NULL if no '=' or "" if nothing after '='.
 * return true if ok else false with excuse in ynot.
 * N.B. we accommodate line and ynot being the same memory
 */
static bool parseWebCommand (WebArgs &wa, char line[], char ynot[])
{
    // init all unknown
    memset (wa.value, 0, sizeof (wa.value));
    memset (wa.found, 0, sizeof (wa.found));

    // parser states
    typedef enum {
        PWC_LOOKING_4_NAME,
        PWC_LOOKING_4_VALUE,
    } PWCState;
    PWCState state = PWC_LOOKING_4_NAME;

    // init working name and value pointers
    char *name = line, *value = NULL;

    // scan entire line -- N.B. this loop returns when it finds '\0'
    for (; ; line++) {
        switch (state) {
        case PWC_LOOKING_4_NAME:
            if (*line == '=') {
                if (name[0] == '\0') {
                    strcpy (ynot, _FX("missing name"));
                    return (false);
                }
                *line = '\0';                                   // terminate name
                value = line + 1;                               // start value
                state = PWC_LOOKING_4_VALUE;                    // now look for value
            } else if (*line == '&') {
                *line = '\0';                                   // terminate name
                if (!setWCValue (wa, trim(name), NULL, ynot))
                    return (false);
                name = line + 1;                                // start next name
                value = NULL;                                   // no value yet
            } else if (*line == '\0') {
                if (name[0] == '\0')                            // no name at all is ok
                    return (true);
                return (setWCValue (wa, trim(name), trim(value), ynot));
            }
            break;

        case PWC_LOOKING_4_VALUE:
            if (*line == '&') {
                *line = '\0';                                   // terminate value
                if (!setWCValue (wa, trim(name), trim(value), ynot))
                    return (false);
                name = line + 1;                                // start next name
                value = NULL;                                   // no value yet
                state = PWC_LOOKING_4_NAME;                     // now look for name
            } else if (*line == '\0') {
                return (setWCValue (wa, trim(name), trim(value), ynot));
            }
            break;
        }
    }

    // should never get here
    strcpy (ynot, _FX("Bogus syntax"));
    return (false);
}


/* convert the given string to integer.
 * return whether the string was entirely valid digits.
 */
static bool atoiOnly (const char *str, int *ip)
{
    if (str == NULL)
        return (false);

    char *endp;
    int i = (int) strtol (str, &endp, 10);
    if (*str != '\0' && *endp == '\0') {
        *ip = i;
        return (true);
    }

    return (false);
}


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
static void startPlainText (WiFiClient &client)
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
static void sendHTTPError (WiFiClient &client, const char *errmsg)
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
static void reportPaneChoices (WiFiClient &client, PlotPane pp)
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
        int sleft = plot_rotationT[pp] - t0;
        snprintf (buf, sizeof(buf), _FX(" rotating in %02d:%02d"), sleft/60, sleft%60);
        client.print(buf);
    }
    client.println();
}

/* format the current stopwatch state into buf
 */
static void getStopwatchReport (char buf[], size_t bufl)
{
    // get current state and times
    uint32_t sw, cd;    // millis
    SWEngineState sws = getSWEngineState (&sw, &cd);

    // break out times, sw and cd end up as remainders in ms
    int sw_hr = sw/(1000*3600);
    sw -= sw_hr*(1000*3600);
    int sw_mn = sw/(1000*60);
    sw -= sw_mn*(1000*60);
    int sw_sc = sw/1000;
    sw -= sw_sc*1000;

    int cd_hr = cd/(1000*3600);
    cd -= cd_hr*(1000*3600);
    int cd_mn = cd/(1000*60);
    cd -= cd_mn*(1000*60);
    int cd_sc = cd/1000;
    cd -= cd_sc*1000;

    // format state
    const char *state_name = "?";
    switch (sws) {
    case SWE_RESET:     state_name = "Reset";     break;
    case SWE_RUN:       state_name = "Running";   break;
    case SWE_STOP:      state_name = "Stopped";   break;
    case SWE_LAP:       state_name = "Lap";       break;
    case SWE_COUNTDOWN: state_name = "Countdown"; break;
    }

    // format report
    size_t bl = snprintf (buf, bufl, _FX("%s %02d:%02d:%02d.%02d"),
                                                state_name, sw_hr, sw_mn, sw_sc, (int)(sw/10));
    if (sws == SWE_COUNTDOWN)
        bl += snprintf (buf+bl, bufl-bl, _FX(" / %02d:%02d:%02d"), cd_hr, cd_mn, cd_sc);
    buf[bl++] = '\n';
    buf[bl] = '\0';
}

/* send screen capture as bmp file
 */
static bool getWiFiCaptureBMP(WiFiClient &client, char *unused)
{
    (void)(unused);

    #define CORESZ 14                           // always 14 bytes at front
    #define HDRVER 108                          // BITMAPV4HEADER, also n bytes in subheader
    #define BHDRSZ (CORESZ+HDRVER)              // total header size
    uint8_t buf[300];                           // any modest size ge BHDRSZ and mult of 2

    resetWatchdog();

    // build BMP header 
    uint32_t npix = BUILD_W*BUILD_H;            // n pixels
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
    *((uint32_t*)(buf+18)) = BUILD_W;           // width
    *((uint32_t*)(buf+22)) = -BUILD_H;          // height, neg means starting at the top row
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
    FWIFIPRLN (client, F("Cache-Control: no-cache"));
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
    if (first_pixel) {
        // skip first pixel first time
        tft.readData();
        tft.readData();
        first_pixel = false;
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

/* helper to report DE or DX info which are very similar
 */
static bool getWiFiDEDXInfo_helper (WiFiClient &client, char *unused, bool want_de)
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
        propDEPath (false, dx_ll, &dist, &B);
        dist *= ERAD_M;                             // radians to miles
        B *= 180/M_PIF;                             // radians to degrees
        bool B_ismag = desiredBearing (de_ll, B);
        if (show_km)
            dist *= 1.609344F;                      // mi - > km
        FWIFIPR (client, F("DX_path_SP    "));
        snprintf (buf, sizeof(buf), _FX("%.0f %s @ %.0f deg %s\n"), dist, show_km ? "km" : "mi", B,
                B_ismag ? _FX("magnetic") : _FX("true"));
        client.print (buf);

        // show long path info
        propDEPath (true, dx_ll, &dist, &B);
        dist *= ERAD_M;                             // radians to miles
        B *= 180/M_PIF;                             // radians to degrees
        B_ismag = desiredBearing (de_ll, B);
        if (show_km)
            dist *= 1.609344F;                      // mi - > km
        FWIFIPR (client, F("DX_path_LP    "));
        snprintf (buf, sizeof(buf), _FX("%.0f %s @ %.0f deg %s\n"), dist, show_km ? "km" : "mi", B,
                B_ismag ? _FX("magnetic") : _FX("true"));
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
        float x;

        x = useMetricUnits() ? wip->temperature_c : 9*wip->temperature_c/5+32;
        snprintf (buf, sizeof(buf), _FX("%sWxTemp     %.1f %c\n"), prefix, x, useMetricUnits() ? 'C' : 'F');
        client.print(buf);

        x = useMetricUnits() ? wip->pressure_hPa : wip->pressure_hPa/33.8639;
        snprintf (buf, sizeof(buf), _FX("%sWxPressure %.2f %s %s\n"), prefix, x,
            useMetricUnits() ? "hPa" : "inHg",
            wip->pressure_chg < 0 ? _FX("falling") : (wip->pressure_chg > 0 ? _FX("rising") : _FX("steady")));
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
static bool getWiFiDEInfo (WiFiClient &client, char *unused)
{
    return (getWiFiDEDXInfo_helper (client, unused, false));
}

/* remote report DX info
 */
static bool getWiFiDXInfo (WiFiClient &client, char *unused)
{
    return (getWiFiDEDXInfo_helper (client, unused, true));
}

/* remote report current set of DX spots
 */
static bool getWiFiDXSpots (WiFiClient &client, char *line)
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

    // print each row
    FWIFIPR (client, F("#  kHz   Call        UTC  Grid      Lat     Lng       Dist   Bear\n"));
    for (uint8_t i = 0; i < nspots; i++) {

        DXClusterSpot &spot = spots[i];
        char buf[100];

        // start with pretty freq, fixed 8 chars
        const char *f_fmt = spot.kHz < 1e6 ? "%8.1f" : "%8.0f";
        int bufl = snprintf (buf, sizeof(buf), f_fmt, spot.kHz);

        // distance and bearing from DE
        LatLong to_ll;
        to_ll.lat = spot.dx_lat;
        to_ll.lat_d = rad2deg(to_ll.lat);
        to_ll.lng = spot.dx_lng;
        to_ll.lng_d = rad2deg(to_ll.lng);
        float dist, bear;
        propDEPath (show_lp, to_ll, &dist, &bear);
        dist *= ERAD_M;                                 // angle to miles
        bear *= 180/M_PIF;                              // rad -> degrees
        bool bear_ismag = desiredBearing (de_ll, bear);
        if (show_km)                                    // match DX display
            dist *= 1.609344F;                          // miles -> km

        // grid
        char grid[MAID_CHARLEN];
        ll2maidenhead (grid, to_ll);

        // add remaining fields
        snprintf (buf+bufl, sizeof(buf)-bufl, _FX(" %-*s %04u %s   %6.2f %7.2f   %6.0f   %4.0f %s\n"),
                MAX_SPOTCALL_LEN-1, spot.dx_call, spot.utcs, grid, to_ll.lat_d, to_ll.lng_d, dist, bear,
                bear_ismag ? _FX("magnetic") : _FX("true"));

        // print
        client.print(buf);
    }

    // ok
    return (true);
}

/* remote report some basic clock configuration
 */
static bool getWiFiConfig (WiFiClient &client, char *unused)
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
    snprintf (buf, sizeof(buf), _FX("MapProj   %s\n"), map_projnames[map_proj]);

    // report grid overlay
    snprintf (buf, sizeof(buf), _FX("MapGrid   %s\n"), grid_styles[mapgrid_choice]);
    client.print(buf);

    // report merc center longitude
    snprintf (buf, sizeof(buf), _FX("MerCenter %d degs\n"), getCenterLng());
    client.print(buf);

    // report panes
    for (int pp = 0; pp < PANE_N; pp++)
        reportPaneChoices (client, (PlotPane)pp);

    // report psk if active
    FWIFIPR (client, F("LiveSpots "));
    if (findPaneForChoice(PLOT_CH_PSK) != PANE_NONE) {
        snprintf (buf, sizeof(buf), "%s DE %s using %s\n", 
                            psk_mask & PSKMB_OFDE ? "of" : "by",
                            psk_mask & PSKMB_CALL ? "call" : "grid",
                            psk_mask & PSKMB_PSK ? "PSK" : "WSPR");
    } else {
        strcpy (buf, "off\n");
    }
    client.print(buf);

    // report each selected NCDXF beacon box state
    FWIFIPR (client, F("NCDXF     "));
    for (unsigned i = 0; i < 8*sizeof(brb_rotset)-1; i++) {
        int j = (brb_mode + i) % BRB_N; // start list with current
        if (brb_rotset & (1 << j)) {
            if (j != brb_mode)
                FWIFIPR (client, F(","));
            client.print(brb_names[j]);
        }
    }
    if (BRBIsRotating()) {
        int sleft = brb_rotationT - now();
        snprintf (buf, sizeof(buf), _FX(" rotating in %02d:%02d\n"), sleft/60, sleft%60);
    } else
        strcpy (buf, "\n");
    client.print (buf);

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
                snprintf (buf, sizeof(buf), _FX("OnOff_%s %02d:%02d %02d:%02d\n"), dayShortStr(dow),
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
        snprintf (buf, sizeof(buf), _FX("Off (%02d:%02d)\n"), hr, mn);
        break;
    case ALMS_ARMED:
        snprintf (buf, sizeof(buf), _FX("Armed for %02d:%02d\n"), hr, mn);
        break;
    case ALMS_RINGING:
        snprintf (buf, sizeof(buf), _FX("Ringing since %02d:%02d\n"), hr, mn);
        break;
    }
    client.print (buf);


    // report stopwatch
    getStopwatchReport (buf, sizeof(buf));
    FWIFIPR (client, F("Stopwatch "));
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

    // gpsd?
    FWIFIPR (client, F("GPSD      "));
    if (useGPSDTime() || useGPSDLoc()) {
        FWIFIPR (client, F("get"));
        if (useGPSDTime())
            FWIFIPR (client, F(" time"));
        if (useGPSDLoc())
            FWIFIPR (client, F(" location"));
        FWIFIPR (client, F(" from "));
        client.println (getGPSDHost());
    } else {
        FWIFIPRLN (client, F("off"));
    }

    // gimbal activity
    bool vis_now, has_el, tracking;
    float az, el;
    bool at_all = getGimbalState (vis_now, has_el, tracking, az, el);
    if (at_all) {
        size_t bufl = 0;
        bufl += snprintf (buf+bufl, sizeof(buf)-bufl, _FX("Gimbal    %s, %s @ "),
                        vis_now ? "In use" : "Not up",
                        tracking ? "tracking" : "idle");
        if (has_el)
            bufl += snprintf (buf+bufl, sizeof(buf)-bufl, _FX("%.1f %.1f\n"), az, el);
        else
            bufl += snprintf (buf+bufl, sizeof(buf)-bufl, "%.1f\n", az);
        client.print(buf);
    }


    // report what DE pane is being used for
    snprintf (buf, sizeof(buf), _FX("DEPane    %s, TZ=UTC%+g%s\n"),
                detime_names[de_time_fmt],
                de_tz.tz_secs/3600.0,
                de_time_fmt == DETIME_INFO ? (desrss ? _FX(", RSAtAt") : _FX(", RSInAgo")) : "");
    client.print(buf);


    // report what DX pane is being used for
    nbuf = snprintf (buf, sizeof(buf), _FX("DXPane    %s"), dx_info_for_sat ? "sat\n" : "DX ");
    if (!dx_info_for_sat) {
        nbuf += snprintf (buf+nbuf, sizeof(buf)-nbuf, _FX("TZ=UTC%+g %s\n"), dx_tz.tz_secs/3600.0,
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
            const BMEData *dp = getBMEData((BMEIndex)i, false);
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

    // report each map color
    for (int i = 0; i < N_CSPR; i++) {
        uint16_t color = getMapColor((CSIds)i);
        const char *color_name = getMapColorName((CSIds)i);
        char uscore_name[30];
        strncpySubChar (uscore_name, color_name, '_', ' ', sizeof(uscore_name));
        snprintf (buf, sizeof(buf), _FX("%-16s%d,%d,%d\n"), uscore_name,
                        RGB565_R(color), RGB565_G(color), RGB565_B(color)); 
        client.print(buf);
    }

    // done
    return (true);
}

/* report current satellite info to the given WiFi connection, or none.
 * always return true
 */
static bool getWiFiSatellite (WiFiClient &client, char *unused)
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
    float *razs, *sazs;
    int n_times;
    if (raz == SAT_NOAZ || saz == SAT_NOAZ || (n_times = nextSatRSEvents (&rises, &razs, &sets, &sazs))==0) {
        FWIFIPR (client, F("No rise or set\n"));
    } else {
        // next events
        if (rhrs > 0) {
            snprintf (line, sizeof(line), _FX("NextRiseIn    %02dh%02d\n"), (int)rhrs,
                (int)(60*(rhrs-(int)rhrs)+0.5F)); client.print(line);
        }
        snprintf (line, sizeof(line), _FX("NextSetIn     %02dh%02d\n"),(int)shrs,
                (int)(60*(shrs-(int)shrs)+0.5F)); client.print(line);

        // print heading
        FWIFIPR (client, F("       Upcoming DE Passes\n"));
        FWIFIPR (client, F("Day  Rise    Az   Set    Az   Up\n"));
        FWIFIPR (client, F("___  _____  ___  _____  ___  _____\n"));
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

            // start with rise day and time/az for sure
            size_t l = snprintf (line, sizeof(line), _FX("%.3s  %02dh%02d  %3.0f"), dayShortStr(rt_wd),
                                                                    hour(rt), minute(rt), razs[i]);

            // if set time is tomorrow start new line with set day and blank rise
            if (rt_wd != st_wd)
                l += snprintf (line+l, sizeof(line)-l, _FX("\n%s            "), dayShortStr(st_wd));

            // show set time/az
            l += snprintf (line+l, sizeof(line)-l, _FX("  %02dh%02d  %3.0f  "),hour(st), minute(st), sazs[i]);

            // show up time, beware longer than 1 hour (moon!)
            if (up >= 3600)
                l += snprintf (line+l, sizeof(line)-l, _FX("%02dh%02d\n"), up/3600, (up-3600*(up/3600))/60);
            else
                l += snprintf (line+l, sizeof(line)-l, _FX("%02d:%02d\n"), up/60, up-60*(up/60));

            // done!
            client.print (line);
        }

        // clean up
        free ((void*)rises);
        free ((void*)razs);
        free ((void*)sets);
        free ((void*)sazs);
    }

    return (true);
}


/* report all available satellites.
 */
static bool getWiFiAllSatellites (WiFiClient &client, char *line)
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
static bool getWiFiSensorData (WiFiClient &client, char *line)
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
        const BMEData *dp = getBMEData((BMEIndex)i, true);
        if (dp) {
            // head points to oldest
            for (int j = 0; j < N_BME_READINGS; j++) {
                uint8_t qj = (dp->q_head+j)%N_BME_READINGS;
                time_t u = dp->u[qj];
                if (u) {
                    char buf[100];
                    snprintf (buf, sizeof(buf),
                                _FX("%4d-%02d-%02dT%02d:%02d:%02dZ %lu  %02x %7.2f %7.2f %7.2f %7.2f\n"),
                                year(u), month(u), day(u), hour(u), minute(u), second(u), u,
                                dp->i2c, BMEUNPACK_T(dp->t[qj]), BMEUNPACK_P(dp->p[qj]),
                                BMEUNPACK_H(dp->h[qj]),
                                dewPoint (BMEUNPACK_T(dp->t[qj]), BMEUNPACK_H(dp->h[qj])));
                    client.print (buf);
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
static bool getWiFiSpaceWx (WiFiClient &client, char *unused)
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
static bool getWiFiSys (WiFiClient &client, char *unused)
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
    snprintf (buf, sizeof(buf), _FX("BuildSz  %d x %d\n"), BUILD_W, BUILD_H); client.print(buf);
    FWIFIPR (client, F("MaxStack ")); client.println (worst_stack);
    FWIFIPR (client, F("MaxWDDT  ")); client.println (max_wd_dt);
    FWIFIPR (client, F("Platform ")); client.println (platform);
    FWIFIPR (client, F("Backend  ")); client.println (svr_host);
    FWIFIPR (client, F("SvrPort  ")); client.println (svr_port);
    FWIFIPR (client, F("S/N      ")); client.println (ESP.getChipId());
#if defined(_IS_ESP8266)
    FWIFIPR (client, F("MinHeap  ")); client.println (worst_heap);
    FWIFIPR (client, F("FreeNow  ")); client.println (ESP.getFreeHeap());
    FWIFIPR (client, F("MaxBlock ")); client.println (ESP.getMaxFreeBlockSize());
    FWIFIPR (client, F("HeapFrag ")); client.println (ESP.getHeapFragmentation());
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

    // show EEPROM used
    uint16_t ee_used, ee_size;
    reportEESize (ee_used, ee_size);
    snprintf (buf, sizeof(buf), _FX("EEPROM   %u used of %u bytes\n"), ee_used, ee_size);
    client.print (buf);

    // show uptime
    uint16_t days; uint8_t hrs, mins, secs;
    if (getUptime (&days, &hrs, &mins, &secs)) {
        snprintf (buf, sizeof(buf), _FX("UpTime   %dd%02d:%02d:%02d\n"), days, hrs, mins, secs);
        client.print (buf);
    }

    // show NTP servers
    const NTPServer *ntp_list;
    int n_ntp = getNTPServers (&ntp_list);
    for (int i = 0; i < n_ntp; i++) {
        int bl = snprintf (buf, sizeof(buf), _FX("NTP      %s "), ntp_list[i].server);
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
        snprintf (buf, sizeof(buf), _FX(" %lu / %lu MiB\n"),
                        (unsigned long)(fs_used/1048576U), (unsigned long)(fs_size/1048576U));
    } else
        snprintf (buf, sizeof(buf), _FX(" %lu / %lu B\n"), (unsigned long)fs_used, (unsigned long)fs_size);
    client.print (buf);
    for (int i = 0; i < n_info; i++) {
        FS_Info *fip = &fip0[i];
        snprintf (buf, sizeof(buf), _FX("  %-32s %20s %7u\n"), fip->name, fip->date, fip->len);
        client.print (buf);
    }
    free (fs_name);
    free (fip0);

    return (true);
}


/* send current clock time
 */
static bool getWiFiTime (WiFiClient &client, char *unused)
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
    int bl = snprintf (buf, sizeof(buf)-10, _FX("Clock_UTC %d-%02d-%02dT%02d:%02d:%02d "),
                        yr, mo, dy, hr, mn, sc);

    // indicate any time offset
    int32_t off = utcOffset();                  // seconds
    if (off == 0) {
        strcat (buf, "Z\n");                    // append Z if above time really is UTC
    } else {
        int off_abs = abs(off);
        int dy_os = off_abs/SECSPERDAY;
        off_abs -= dy_os*SECSPERDAY;
        int hr_os = off_abs/3600;
        off_abs -= hr_os*3600;
        int mn_os = off_abs/60;
        off_abs -= mn_os*60;
        int sc_os = off_abs;
        bl += sprintf (buf+bl, "= Z%c", off < 0 ? '-' : '+');
        if (dy_os != 0)
            bl += sprintf (buf+bl, "%dd", dy_os);
        bl += sprintf (buf+bl, _FX("%02d:%02d:%02d\n"), hr_os, mn_os, sc_os);
    }
    client.print (buf);

    return (true);
}

/* remote command to set call sign to some message and set fg and bg colors.
 * all are optional in any order; restore defaults if no args at all.
 */
static bool setWiFiTitle (WiFiClient &client, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "msg";
    wa.name[wa.nargs++] = "fg";
    wa.name[wa.nargs++] = "bg";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // handy
    const char *msg = wa.value[0];
    const char *fg = wa.value[1];
    const char *bg = wa.value[2];

    // crack fg if found
    uint16_t fg_c = 0;
    if (fg) {
        int r, g, b;
        if (sscanf (fg, _FX("%d,%d,%d"), &r, &g, &b) != 3 || !rgbOk (r, g, b)) {
            strcpy (line, garbcmd);
            return (false);
        }
        fg_c = RGB565(r,g,b);
    }

    // crack bg if found
    uint16_t bg_c = 0;
    bool rainbow = false;
    if (bg) {
        if (strcmp (bg, "rainbow") == 0)
            rainbow = true;
        else {
            int r, g, b;
            if (sscanf (bg, _FX("%d,%d,%d"), &r, &g, &b) != 3 || !rgbOk (r, g, b)) {
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
static bool setWiFiDemo (WiFiClient &client, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "on";
    wa.name[wa.nargs++] = "off";
    wa.name[wa.nargs++] = "n";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    char buf[100];

    // crack
    if (wa.found[0] && wa.value[0] == NULL) {
        // on
        setDemoMode(true);
        drawScreenLock();
        strcpy (buf, _FX("Demo mode on\n"));
        Serial.print (buf);

    } else if (wa.found[1] && wa.value[1] == NULL) {
        // off
        setDemoMode(false);
        drawScreenLock();
        strcpy (buf, _FX("Demo mode off\n"));
        Serial.print (buf);

    } else if (wa.found[2] && wa.value[2] != NULL && wa.value[2][0] != '\0') {
        // n=x
        int choice;
        if (!atoiOnly(wa.value[2], &choice) || choice < 0 || choice >= DEMO_N) {
            sprintf (line, _FX("Demo codes 0 .. %d"), DEMO_N-1);
            return (false);
        }

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
        strcpy (line, garbcmd);
        return (false);
    }

    // ack
    startPlainText (client);
    client.print (buf);

    // good
    return (true);
}


/* remote command to set the DE time format and/or atin
 * set_defmt?fmt=[one from menu]&atin=RSAtAt|RSInAgo"
 */
static bool setWiFiDEformat (WiFiClient &client, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "fmt";
    wa.name[wa.nargs++] = "atin";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // fmt is required
    if (!wa.found[0]) {
        strcpy (line, _FX("fmt is required"));
        return (false);
    }

    // handy
    const char *fmt = wa.value[0];
    const char *atin = wa.value[1];

    // search format options
    int new_fmt = -1;
    for (int i = 0; i < DETIME_N; i++) {
        if (strcmp (fmt, detime_names[i]) == 0) {
            new_fmt = i;
            break;
        }
    }
    if (new_fmt < 0) {
        size_t ll = sprintf (line, _FX("Formats: %s"), detime_names[0]);
        for (int i = 1; i < DETIME_N; i++)
            ll += sprintf (line+ll, ", %s", detime_names[i]);
        return (false);
    }

    // if fmt is DETIME_INFO, also allow setting optional at/in
    uint8_t new_desrss = desrss;
    if (atin) {
        if (new_fmt != DETIME_INFO) {
            sprintf (line, _FX("atin requires %s"), detime_names[DETIME_INFO]);
            return (false);
        }
        if (strcmp (atin, _FX("RSAtAt")) == 0)
            new_desrss = DXSRSS_ATAT;
        else if (strcmp (atin, _FX("RSInAgo")) == 0)
            new_desrss = DXSRSS_INAGO;
        else {
            strcpy (line, _FX("unknown atin"));
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
    sprintf (line, _FX("%s %s\n"), fmt, atin ? atin : "");
    client.println (line);

    // ok
    return (true);
}

/* remote command to set a new dx cluster
 */
static bool setWiFiCluster (WiFiClient &client, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "host";
    wa.name[wa.nargs++] = "port";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // handy
    char *host = (char*) wa.value[0];   // setDXCluster will trim
    const char *port = wa.value[1];

    // try to save
    if (!setDXCluster (host, port, line))
        return (false);

    // close and reopen if active
    closeDXCluster();
    PlotPane pp = findPaneChoiceNow (PLOT_CH_DXCLUSTER);
    bool start_ok = pp == PANE_NONE || updateDXCluster (plot_b[pp]);

    // ack
    startPlainText (client);
    if (start_ok)
        client.print (_FX("ok\n"));
    else
        client.print (_FX("err\n"));

    // we tried
    return (true);
}



/* remote command to set up alarm clock
 *   state=off|armed&time=HR:MN
 */
static bool setWiFiAlarm (WiFiClient &client, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "state";
    wa.name[wa.nargs++] = "time";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // handy
    const char *state = wa.value[0];
    const char *timespec = wa.value[1];

    // get current state
    AlarmState as;
    uint16_t hr16, mn16;
    getAlarmState (as, hr16, mn16);

    // crack new state
    if (state) {
        if (strcmp (state, "off") == 0)
            as = ALMS_OFF;
        else if (strcmp (state, "armed") == 0)
            as = ALMS_ARMED;
        else {
            strcpy (line, _FX("unknown state"));
            return (false);
        }
    } else {
        strcpy (line, _FX("state is required"));
        return (false);
    }

    // crack new time spec if given
    if (timespec) {
        if (as == ALMS_OFF) {
            strcpy (line, _FX("can set time only if armed"));
            return(false);
        }
        int hr = 0, mn = 0;
        if (sscanf (timespec, "%d:%d", &hr, &mn) != 2 || hr < 0 || hr >= 24 || mn < 0 || mn >= 60) {
            strcpy (line, _FX("invalid time spec"));
            return (false);
        }
        hr16 = hr;
        mn16 = mn;
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

/* remote command to set the aux time format
 */
static bool setWiFiAuxTime (WiFiClient &client, char line[])
{
    // parse
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "format";
    if (!parseWebCommand (wa, line, line))
        return (false);

    // look for matching name
    bool found = false;
    if (wa.found[0]) {
        for (int i = 0; i < AUXT_N; i++) {
            if (strcmp (wa.value[0], auxtime_names[i]) == 0) {
                auxtime = (AuxTimeFormat)i;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        strcpy (line, garbcmd);
        return (false);
    }

    // good: engage
    updateClocks(true);

    // ack
    startPlainText (client);
    client.println (wa.value[0]);

    return (true);
}

/* remote command to set display on or off
 */
static bool setWiFiDisplayOnOff (WiFiClient &client, char line[])
{
    if (brOnOffOk()) {

        // define all possible args
        WebArgs wa;
        wa.nargs = 0;
        wa.name[wa.nargs++] = "on";
        wa.name[wa.nargs++] = "off";

        // parse
        if (!parseWebCommand (wa, line, line))
            return (false);

        // engage
        if (wa.found[0] && wa.value[0] == NULL)
            brightnessOn();
        else if (wa.found[1] && wa.value[1] == NULL)
            brightnessOff();
        else {
            strcpy (line, _FX("Specify just on or off"));
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
        if (strcmp (dayShortStr(i), daystr) == 0) {
            dow = i;
            return (true);
        }
    }
    return (false);
}

/* remote command to set display on/off/idle times
 * on=HR:MN&off=HR:MN&idle=mins&day=DOW
 */
static bool setWiFiDisplayTimes (WiFiClient &client, char line[])
{

    if (brOnOffOk()) {

        // define all possible args
        WebArgs wa;
        wa.nargs = 0;
        wa.name[wa.nargs++] = "on";
        wa.name[wa.nargs++] = "off";
        wa.name[wa.nargs++] = "day";
        wa.name[wa.nargs++] = "idle";

        // parse
        if (!parseWebCommand (wa, line, line))
            return (false);

        // handy
        const char *on = wa.value[0];
        const char *off = wa.value[1];
        const char *day = wa.value[2];
        const char *idle = wa.value[3];

        // crack -- on and off are required, day and idle are optional
        int on_hr, on_mn, off_hr, off_mn, idle_mins = -1, dow = -1;
        if ((!wa.found[0] || !on || sscanf (on, _FX("%d:%d"), &on_hr, &on_mn) != 2)
                    || (!wa.found[1] || !off || sscanf (off, _FX("%d:%d"), &off_hr, &off_mn) != 2)
                    || (wa.found[2] && (!day || !crackDOW (day, dow)))
                    || (wa.found[3] && !atoiOnly(idle,&idle_mins))) {
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

        // engage
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
        strcpy (buf, dayShortStr(dow));
        client.println (buf);

        // ok
        return (true);

    } else {

        strcpy (line, notsupp);
        return (false);

    }
}



/* set DE or DX from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewDEDX_helper (WiFiClient &client, bool new_dx, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "lat";
    wa.name[wa.nargs++] = "lng";
    wa.name[wa.nargs++] = "grid";
    wa.name[wa.nargs++] = "TZ";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // handy
    const char *lat_spec = wa.value[0];
    const char *lng_spec = wa.value[1];
    const char *grid_spec = wa.value[2];
    const char *tz = wa.value[3];

    // check and engage depending on args given
    if (lat_spec && lng_spec && !grid_spec) {

        LatLong ll;
        if (!latSpecIsValid (lat_spec, ll.lat_d)) {
            strcpy (line, _FX("bad lat"));
            return (false);
        }
        if (!lngSpecIsValid (lng_spec, ll.lng_d)) {
            strcpy (line, _FX("bad lng"));
            return (false);
        }

        // engage
        if (new_dx)
            newDX (ll, NULL, NULL);
        else
            newDE (ll, NULL);

    } else if (!lat_spec && !lng_spec && grid_spec) {

        size_t gridlen = strlen(grid_spec);
        if (gridlen != 4 && gridlen != 6) {
            strcpy (line, _FX("grid must be 4 or 6 chars"));
            return (false);
        }
        LatLong ll;
        if (!maidenhead2ll (ll, grid_spec)) {
            strcpy (line, _FX("bad grid"));
            return (false);
        }

        // engage
        if (new_dx)
            newDX (ll, grid_spec, NULL);
        else
            newDE (ll, grid_spec);

    } else {

        strcpy (line, _FX("do not mix lat/lng and grid"));
        return (false);
    }

    // update TZ too if set
    if (tz) {
        if (new_dx) {
            dx_tz.tz_secs = atof(tz)*3600;        // hours to seconds
            NVWriteInt32 (NV_DX_TZ, dx_tz.tz_secs);
            drawTZ (dx_tz);
            drawDXInfo();
        } else {
            de_tz.tz_secs = atof(tz)*3600;        // hours to seconds
            NVWriteInt32 (NV_DE_TZ, de_tz.tz_secs);
            drawTZ (de_tz);
            updateMoonPane(true);
            scheduleNewBC();
            drawDEInfo();
        }
    }

    // ack with updated info as if get
    return (getWiFiDEDXInfo_helper (client, line, new_dx));
}

/* set DE from grid or lat/lng
 */
static bool setWiFiNewDE (WiFiClient &client, char line[])
{
    return (setWiFiNewDEDX_helper (client, false, line));
}

/* set DX from grid or lat/lng
 */
static bool setWiFiNewDX (WiFiClient &client, char line[])
{
    return (setWiFiNewDEDX_helper (client, true, line));
}

/* set a map view color, names must match those displayed in Setup after changing all '_' to ' '
 */
static bool setWiFiMapColor (WiFiClient &client, char line[])
{
    // easier not to use WebArgs in this case
    char color[30];
    int r, g, b;
    if (sscanf (line, "%29[^=]=%d,%d,%d", color, &r, &g, &b) != 4 || !rgbOk (r, g, b)) {
        strcpy (line, garbcmd);
        return (false);
    }

    // now try to install
    if (!setMapColor (color, RGB565(r,g,b))) {
        strcpy (line, "bad name");
        return (false);
    }

    // it worked, so restart map
    initEarthMap();

    #if defined(_SUPPORT_PSKREPORTER)
        // update psk pane in case a band color changed; it knows to ignore if not up
        scheduleNewPSK();
    #endif

    // ack
    startPlainText (client);
    client.print("ok\n");

    // good
    return (true);
}

/* set a new mercator center longitude, even if not currently showing mercator
 */
static bool setWiFiMerCenter (WiFiClient &client, char line[])
{
    // crack args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "lng";
    if (!parseWebCommand (wa, line, line))
        return (false);

    // get candidate longitude
    float new_lng;
    if (!lngSpecIsValid (wa.value[0], new_lng)) {
        strcpy (line, "bad longitude");
        return (false);
    }

    // set and restart map if showing mercator
    setCenterLng (new_lng);
    if (map_proj == MAPP_MERCATOR)
        initEarthMap();

    // ack
    startPlainText (client);
    client.print("ok\n");

    // good
    return (true);
}



/* set one or more view features of the map, same as menu.
 * syntax: Style=S&Grid=G&Projection=P&RSS=on|off&Night=on|off
 * all keywords optional but require at least 1.
 */
static bool setWiFiMapView (WiFiClient &client, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "Style";
    wa.name[wa.nargs++] = "Grid";
    wa.name[wa.nargs++] = "Projection";
    wa.name[wa.nargs++] = "RSS";
    wa.name[wa.nargs++] = "Night";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // handy
    const char *S = wa.value[0];
    const char *G = wa.value[1];
    const char *P = wa.value[2];
    const char *R = wa.value[3];
    const char *N = wa.value[4];

    // require at least 1
    if (!S && !G && !P && !R && !N) {
        strcpy (line, _FX("bad args"));
        return (false);
    }

    // check style
    CoreMaps my_cm = CM_NONE;
    if (S) {
        for (int i = 0; i < CM_N; i++) {
            if (strcmp (S, map_styles[i]) == 0) {
                my_cm = (CoreMaps) i;
                break;
            }
        }
        if (my_cm == CM_NONE) {
            size_t ll = sprintf (line, _FX("Styles: %s"), map_styles[0]);
            for (int i = 1; i < CM_N; i++)
                ll += sprintf (line+ll, ", %s", map_styles[i]);
            return (false);
        }
    }

    // check grid
    int my_llg = -1;
    if (G) {
        for (int i = 0; i < MAPGRID_N; i++) {
            if (strcmp (G, grid_styles[i]) == 0) {
                my_llg = i;
                break;
            }
        }
        if (my_llg < 0) {
            size_t ll = sprintf (line, "Grid: %s", grid_styles[0]);
            for (int i = 1; i < MAPGRID_N; i++)
                ll += sprintf (line+ll, ", %s", grid_styles[i]);
            return (false);
        }
    }

    // check projection
    int my_proj = -1;
    if (P) {
        for (int i = 0; i < MAPP_N; i++) {
            if (strcmp (P, map_projnames[i]) == 0) {
                my_proj = i;
                break;
            }
        }
        if (my_proj < 0) {
            size_t ll = sprintf (line, "Projection: %s", map_projnames[0]);
            for (int i = 1; i < MAPP_N; i++)
                ll += sprintf (line+ll, ", %s", map_projnames[i]);
            return (false);
        }
    }

    // check RSS
    int my_rss = -1;
    if (R) {
        if (!strcmp (R, "on"))
            my_rss = 1;
        else if (!strcmp (R, "off"))
            my_rss = 0;
        else {
            strcpy (line, _FX("RSS: on or off"));
            return (false);
        }
    }

    // check Night
    int my_night = -1;
    if (N) {
        if (!strcmp (N, "on"))
            my_night = 1;
        else if (!strcmp (N, "off"))
            my_night = 0;
        else {
            strcpy (line, _FX("Night: on or off"));
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
        NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
        full_redraw = true;
    }
    if (P && my_proj != map_proj) {
        map_proj = my_proj;
        NVWriteUInt8 (NV_MAPPROJ, map_proj);
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
    client.print("ok\n");

    // good
    return (true);
}

/* set a new correction for the given sensor
 *   sensor=76|77&dTemp=X&dPres=Y" },
 */
static bool setWiFiSensorCorr (WiFiClient &client, char line[])
{
    if (getNBMEConnected() == 0) {
        strcpy (line, _FX("No sensors"));
        return (false);
    }

    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "sensor";
    wa.name[wa.nargs++] = "dTemp";
    wa.name[wa.nargs++] = "dPres";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // handy
    const char *S = wa.value[0];
    const char *T = wa.value[1];
    const char *P = wa.value[2];

    // sensor is required
    if (!S) {
        strcpy (line, _FX("missing sensor"));
        return (false);
    }
    int sensor_i2c;
    BMEIndex sensor_idx;
    bool s_ok = atoiOnly (S, &sensor_i2c);
    if (s_ok) {
        if (sensor_i2c == 76)
            sensor_idx = BME_76;
        else if (sensor_i2c == 77)
            sensor_idx = BME_77;
        else
            s_ok = false;
    }
    if (!s_ok) {
        strcpy (line, _FX("sensor must be 76 or 77"));
        return (false);
    }

    // at least one of T and P are required
    if (!T && !P) {
        strcpy (line, _FX("missing value"));
        return (false);
    }

    // try dPres if set
    if (P) {
        if (!setBMEPresCorr(sensor_idx, atof(P))) {
            strcpy (line, _FX("bad dPres sensor"));
            return (false);
        }
    }

    // try dTemp if set
    if (T) {
        if (!setBMETempCorr(sensor_idx, atof(T))) {
            strcpy (line, _FX("bad dTemp sensor"));
            return (false);
        }
    }

    // ack
    startPlainText (client);
    client.print("ok\n");

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
static bool setWiFiRSS (WiFiClient &client, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "reset";
    wa.name[wa.nargs++] = "add";
    wa.name[wa.nargs++] = "file";
    wa.name[wa.nargs++] = "network";
    wa.name[wa.nargs++] = "interval";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    StackMalloc buf_mem(150);
    char *buf = (char *) buf_mem.getMem();
    int n_titles, n_max;

    // check args -- full buf with suitable message
    if (wa.found[3] && wa.value[3] == NULL) {
        // restore normal rss network queries
        (void) setRSSTitle (NULL, n_titles, n_max);
        strcpy (buf, _FX("Restored RSS network feeds\n"));

    } else if (wa.found[0] && wa.value[0] == NULL) {
        // turn off network and empty local list
        (void) setRSSTitle ("", n_titles, n_max);
        buf[0] = '\0';                                  // use default reply

    } else if (wa.found[1] && wa.value[1] != NULL) {
        // turn off network and add title to local list if room
        if (!setRSSTitle (wa.value[1], n_titles, n_max)) {
            sprintf (line, _FX("List is full -- max %d"), n_max);
            return (false);
        }
        buf[0] = '\0';                                  // use default reply

    } else if (wa.found[2] && wa.value[2] == NULL) {
        // titles follow header
        (void) setRSSTitle ("", n_titles, n_max);       // reset list
        while (getTCPLine (client, buf, buf_mem.getSize(), NULL))
            (void) setRSSTitle (buf, n_titles, n_max);
        buf[0] = '\0';                                  // use default reply

    } else if (wa.found[4] && wa.value[4] != NULL) {
        int new_i;
        if (atoiOnly (wa.value[4], &new_i) && new_i >= RSS_MIN_INT) {
            rss_interval = new_i;
            snprintf (buf, buf_mem.getSize(), _FX("RSS interval now %d secs\n"), rss_interval);
            NVWriteUInt8 (NV_RSS_INTERVAL, rss_interval);
        } else {
            sprintf (line, _FX("Min interval %d seconds"), RSS_MIN_INT);
            return (false);
        }

    } else {
        strcpy (line, garbcmd);
        return (false);
    }

    // create default reply if buf empty
    if (buf[0] == '\0')
        snprintf (buf, buf_mem.getSize(), _FX("Now %d of %d local titles are defined\n"), n_titles, n_max);

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
static bool setWiFiPane (WiFiClient &client, char line[])
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
        strcpy (line, _FX("Bad pane num"));
        return (false);
    }
    PlotPane pp = (PlotPane)(pane_1-1);

    // convert remaining args to list of PlotChoices
    PlotChoice pc[PLOT_CH_N];           // max size, only first n_pc in use
    int n_pc = 0;
    char *start = equals + 1;
    for (char *tok = NULL; (tok = strtok (start, ",")) != NULL; start = NULL) {

        // tok is within line, so copy it so we can use line for err msg
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
        
        // dx cluster?
        if (tok_pc == PLOT_CH_DXCLUSTER && pp == PANE_1) {
            sprintf (line, _FX("%s not allowed on pane 1"), tok_copy);
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
static bool setWiFiSatName (WiFiClient &client, char line[])
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
static bool setWiFiSatTLE (WiFiClient &client, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "name";
    wa.name[wa.nargs++] = "t1";
    wa.name[wa.nargs++] = "t2";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // handy
    const char *name = wa.value[0];
    const char *t1 = wa.value[1];
    const char *t2 = wa.value[2];
    if (!name || !t1 || !t2) {
        strcpy (line, garbcmd);
        return (false);
    }

    // enforce known line lengths
    if (strlen(t1) != TLE_LINEL-1) {
        strcpy (line, _FX("bogus t1"));
        return(false);
    }
    if (strlen(t2) != TLE_LINEL-1) {
        strcpy (line, _FX("bogus t2"));
        return(false);
    }

    // try to install
    if (setSatFromTLE (name, t1, t2))
        return (getWiFiSatellite (client, line));

    // nope
    strcpy (line, _FX("Bad spec"));
    return (false);
}

/* remote command to control stopwatch engine state
 */
static bool setWiFiStopwatch (WiFiClient &client, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "reset";
    wa.name[wa.nargs++] = "run";
    wa.name[wa.nargs++] = "stop";
    wa.name[wa.nargs++] = "lap";
    wa.name[wa.nargs++] = "countdown";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // crack
    SWEngineState sws;
    int mins = 0;
    if (wa.found[4] && atoiOnly(wa.value[4], &mins)) {
        sws = SWE_COUNTDOWN;
    } else if (wa.found[0] && wa.value[0] == NULL) {
        sws = SWE_RESET;
    } else if (wa.found[1] && wa.value[1] == NULL) {
        sws = SWE_RUN;
    } else if (wa.found[2] && wa.value[2] == NULL) {
        sws = SWE_STOP;
    } else if (wa.found[3] && wa.value[3] == NULL) {
        sws = SWE_LAP;
    } else {
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
    startPlainText(client);
    char buf[100];
    getStopwatchReport (buf, sizeof(buf));
    client.print(buf);

    // ok
    return (true);
}

/* set clock time from any of three formats:
 *  ISO=YYYY-MM-DDTHH:MM:SS
 *  unix=s
 *  Now
 * return whether command is fully recognized.
 */
static bool setWiFiTime (WiFiClient &client, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "ISO";
    wa.name[wa.nargs++] = "unix";
    wa.name[wa.nargs++] = "Now";
    wa.name[wa.nargs++] = "change";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);


    // crack
    if (wa.found[3] && wa.value[3] != NULL) {

        changeTime (nowWO() + atol(wa.value[3]));

    } else if (wa.found[2] && wa.value[2] == NULL) {

        changeTime (0);

    } else if (wa.found[1] && wa.value[1] != NULL) {

        // crack and engage
        changeTime (atol(wa.value[1]));

    } else if (wa.found[0] && wa.value[0] != NULL) {

        int yr, mo, dy, hr, mn, sc;
        if (sscanf (wa.value[0], _FX("%d-%d-%dT%d:%d:%d"), &yr, &mo, &dy, &hr, &mn, &sc) == 6) {

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

            strcpy (line, _FX("garbled ISO"));
            return (false);

        }

    } else {

        strcpy (line, garbcmd);
        return (false);
    }

    // reply
    startPlainText(client);
    char buf[30];
    snprintf (buf, sizeof(buf), _FX("UNIX_time %ld\n"), (long int) nowWO());
    client.print (buf);

    return (true);
}

/* perform a touch screen action based on coordinates received via wifi GET
 * return whether all ok.
 */
static bool setWiFiTouch (WiFiClient &client, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "x";
    wa.name[wa.nargs++] = "y";
    wa.name[wa.nargs++] = "hold";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // require x and y within screen size
    int x, y;
    if (!atoiOnly(wa.value[0],&x) || x < 0 || x >= tft.width()
                || !atoiOnly(wa.value[1],&y) || y < 0 || y >= tft.height()) {
        sprintf (line, _FX("require 0 .. x .. %d and 0 .. y .. %d"), tft.width()-1, tft.height()-1);
        return (false);
    }

    // hold is optional but must be valid if given
    int h = 0;
    if (wa.found[2] && (!atoiOnly(wa.value[2],&h) || (h != 0 && h != 1))) {
        strcpy (line, _FX("hold must be 0 or 1"));
        return (false);
    }

    // inform checkTouch() to use wifi_tt_s; it will reset
    wifi_tt_s.x = x;
    wifi_tt_s.y = y;
    wifi_tt = h ? TT_HOLD : TT_TAP;

    // ack
    startPlainText (client);
    char buf[50];
    snprintf (buf, sizeof(buf), _FX("Touch %d %d %s\n"), x, y, h ? "hold" : "tap");
    client.print(buf);

    // ok
    return (true);
}

/* set the VOACAP map to the given band and/or power and/or timeline units
 * return whether all ok.
 */
static bool setWiFiVOACAP (WiFiClient &client, char line[])
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "band";
    wa.name[wa.nargs++] = "power";
    wa.name[wa.nargs++] = "tl";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // handy
    const char *B = wa.value[0];
    const char *P = wa.value[1];
    const char *T = wa.value[2];

    // crack band
    PropMapSetting new_pms = prop_map;
    if (B) {
        int band;
        if (!atoiOnly(B,&band)) {
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
        int p;
        if (!atoiOnly(P,&p) || (p != 1 && p != 10 && p != 100 && p != 1000)) {
            strcpy (line, _FX("power must be 1,10,100,1000"));
            return (false);
        }
        new_power = p;
    }

    // crack timeline
    int new_utc = bc_utc_tl;
    if (T) {
        if (strcmp (T, _FX("UTC")) == 0) {
            new_utc = 1;
        } else if (strcmp (T, _FX("DE")) == 0) {
            new_utc = 0;
        } else {
            strcpy (line, _FX("tl must be DE or UTC"));
            return (false);
        }
    }

    // engage or revert
    PlotPane bc_pp = findPaneChoiceNow (PLOT_CH_BC);
    if (B || P) {
        // new band or power so schedule fresh map and/or BC as required
        bc_power = new_power;
        bc_utc_tl = new_utc;
        if (new_pms != PROP_MAP_OFF)
            scheduleNewVOACAPMap (new_pms);
        if (bc_pp != PANE_NONE)
            scheduleNewBC();        // this will also update time line
    } else if (T) {
        // only changing timeline units so just change if different and redraw BC if up
        if (new_utc != bc_utc_tl) {
            bc_utc_tl = new_utc;
            if (bc_pp != PANE_NONE)
                plotBandConditions (plot_b[bc_pp], 0, NULL, NULL);
        }
    } else {
        // none: schedule default core map and update BC pane if up
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
static bool doWiFiReboot (WiFiClient &client, char *unused)
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
    doReboot();

    // never returns but compiler doesn't know that
    return (true);
}

/* update firmware if available
 */
static bool doWiFiUpdate (WiFiClient &client, char *unused)
{
    (void) unused;

    // prep for response but won't be one if we succeed with update
    startPlainText(client);

    // proceed if newer version is available
    char ver[50];
    if (newVersionIsAvailable (ver, sizeof(ver))) {
        char msg[100];
        snprintf (msg, sizeof(msg), _FX("updating from %s to %s ... \n"), hc_version, ver);
        client.print(msg);
        doOTAupdate(ver);                               // never returns if successful
        FWIFIPRLN (client, F("update failed"));
    } else
        FWIFIPRLN (client, F("You're up to date!"));    // match tapping version

    return (true);
}


/* change the live spots configuration
 *   spot=of|by&what=call|grid&data=psk|wspr&bands=160,...
 */
static bool setWiFiLiveSpots (WiFiClient &client, char *line)
{
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "spot";                       // 0
    wa.name[wa.nargs++] = "what";                       // 1
    wa.name[wa.nargs++] = "data";                       // 2
    wa.name[wa.nargs++] = "bands";                      // 3

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // since components are optional we start with the current mask and modify
    uint8_t new_mask = psk_mask;

    if (wa.found[0]) {
        const char *spot = wa.value[0];
        if (strcmp (spot, "of") == 0)
            new_mask |= PSKMB_OFDE;
        else if (strcmp (spot, "by") == 0)
            new_mask &= ~PSKMB_OFDE;
        else {
            strcpy (line, "spot not by or of");
            return (false);
        }
    }

    if (wa.found[1]) {
        const char *what = wa.value[1];
        if (strcmp (what, "call") == 0)
            new_mask |= PSKMB_CALL;
        else if (strcmp (what, "grid") == 0)
            new_mask &= ~PSKMB_CALL;
        else {
            strcpy (line, "what not call or grid");
            return (false);
        }
    }

    if (wa.found[2]) {
        const char *data = wa.value[2];
        if (strcmp (data, "psk") == 0)
            new_mask |= PSKMB_PSK;
        else if (strcmp (data, "wspr") == 0)
            new_mask &= ~PSKMB_PSK;
        else {
            strcpy (line, "data not psk or wspr");
            return (false);
        }
    }

    // new set of bands, if set
#if defined (_SUPPORT_PSKESP)
    // temp fake
    uint32_t psk_bands = 0;
#endif
    uint32_t new_bands = 0;
    if (wa.found[3]) {
        char bands[strlen(wa.value[3])+1];
        strcpy (bands, wa.value[3]);
        if (strcmp (bands, "all") == 0) {
            new_bands = (1 << PSKBAND_N) - 1;
        } else {
            char *token, *string = bands;
            while ((token = strsep (&string, ",")) != NULL) {
                switch (atoi(token)) {
                case 160: new_bands |= (1 << PSKBAND_160M); break;
                case 80:  new_bands |= (1 << PSKBAND_80M);  break;
                case 60:  new_bands |= (1 << PSKBAND_60M);  break;
                case 40:  new_bands |= (1 << PSKBAND_40M);  break;
                case 30:  new_bands |= (1 << PSKBAND_30M);  break;
                case 20:  new_bands |= (1 << PSKBAND_20M);  break;
                case 17:  new_bands |= (1 << PSKBAND_17M);  break;
                case 15:  new_bands |= (1 << PSKBAND_15M);  break;
                case 12:  new_bands |= (1 << PSKBAND_12M);  break;
                case 10:  new_bands |= (1 << PSKBAND_10M);  break;
                case 6:   new_bands |= (1 << PSKBAND_6M);   break;
                case 2:   new_bands |= (1 << PSKBAND_2M);   break;
                default:
                    strcpy (line, "unknown band");
                    return (false);
                }
            }
        }
    } else {
        // no change if not specified
        new_bands = psk_bands;
    }

    // skip if no change
    if (psk_mask == new_mask && psk_bands == new_bands) {
        strcpy (line, "no change");
        return (false);
    }

    // ok - engage
    psk_mask = new_mask;
    psk_bands = new_bands;
    savePSKState();
    scheduleNewPSK();

    // ack
    startPlainText (client);
    client.print ("ok\n");

    // ok
    return (true);
}

#if defined(_IS_UNIX)

/* implement a live web server connection.
 * the idea is to maintain an image of changed regions so browser only gets changes,
 *   saves at least 10x bandwidth. not implemented on ESP because reading pixels is so slow and because
 *   it does not have enough memory to build a png image
 */

// import png writer -- complete implementation in a header file -- amazing.
// https://github.com/nothings/stb
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// png format is 3 bytes per pixel
#define LIVE_BYPPIX     3                               // bytes per pixel
#define LIVE_NPIX       (BUILD_H*BUILD_W)               // pixels per complete image
#define LIVE_NBYTES     (LIVE_NPIX*LIVE_BYPPIX)         // bytes per complete image
#define COMP_RGB        3                               // composition request code for RGB pixels


// persistent copies of client screen shots used to build differences.
typedef struct {
    char IP[16];                                        // client IP xxx.xxx.xxx.xxx with EOS
    uint8_t *pixels;                                    // malloced last known complete image of LIVE_NBYTES
    time_t last_used;                                   // last time() this IP was used
    bool in_use;                                        // whether this record is being used
} LiveConnection;
static LiveConnection *clives;                          // malloced list
static int n_clives;                                    // n malloced (some may be unused)
#define LIVE_EXPIRE     10                              // discard if not used within this many seconds


/* send the favicon.ico icon
 * UNIX only
 */
static bool getWiFifavicon (WiFiClient &client, char *errmsg)
{
    // send the web page header
    resetWatchdog();
    FWIFIPRLN (client, F("HTTP/1.0 200 OK"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: image/x-icon"));
    FWIFIPRLN (client, F("Connection: close\r\n"));

    // send icon
    writeFavicon (client);
    Serial.println(F("favicon sent"));

    // never fails :)
    return (true);
}

/* read out the current image as an array of LIVE_NBYTES RGB pixels
 * UNIX only
 */
static void readLiveImageNow (uint8_t img[LIVE_NBYTES])
{
    if (!tft.getRawPix (img, LIVE_NPIX))
        fatalError ("getRawPix wrong size: %d\n", LIVE_NPIX);
}

/* initialize a LiveConnection for the given IP making no assumptions about its current content.
 * this includes a large malloc so return false of it fails.
 * UNIX only
 */
static bool initLiveConnection (LiveConnection *lp, const String &IP)
{
    printf ("LIVE: init: %s\n", IP.c_str());

    lp->pixels = (uint8_t *) malloc (LIVE_NBYTES);
    if (!lp->pixels)
        return (false);

    strncpy (lp->IP, IP.c_str(), sizeof(lp->IP));
    lp->IP[sizeof(lp->IP)-1] = '\0';
    lp->last_used = time(NULL);
    lp->in_use = true;

    return (true);
}

/* destroy the memory used by the given LiveConnection and mark it as unused.
 * UNIX only
 */
static void destroyLiveConnection (LiveConnection *lp)
{
    if (!lp->in_use)
        fatalError ("destroying unused live connection");
    printf ("LIVE: destroy: %s\n", lp->IP);
    free (lp->pixels);
    lp->in_use = false;
}

/* find an existing connection for this IP, if any
 * UNIX only
 */
static LiveConnection *findLiveConnection (const String &IP)
{
    for (int i = 0; i < n_clives; i++) {
        LiveConnection *lp = &clives[i];
        if (lp->in_use && strcmp (lp->IP, IP.c_str()) == 0) {
            if (live_verbose > 1)
                printf ("LIVE: reusing %s\n", lp->IP);
            return (lp);
        }
    }
    printf ("LIVE: %s not found\n", IP.c_str());
    return (NULL);
}

/* return a connection record for this IP including a fresh copy of the current image.
 * return NULL if trouble.
 * UNIX only
 */
static LiveConnection *startLiveConnection (const String &IP)
{
    // first look for an existing record we can continue to use
    LiveConnection *lp = findLiveConnection (IP);
    if (lp && live_verbose > 1)
        printf ("LIVE: start found %s\n", lp->IP);

    // not found, look for a dormant entry to reuse and clean up other old ones along the way
    if (!lp) {
        for (int i = 0; i < n_clives; i++) {
            LiveConnection *candidate_lp = &clives[i];
            if (candidate_lp->in_use && candidate_lp->last_used < time(NULL) - LIVE_EXPIRE) {
                destroyLiveConnection(candidate_lp);
                if (!lp) {
                    if (!initLiveConnection (candidate_lp, IP))
                        return (NULL);
                    lp = candidate_lp;
                }
            }
        }
        if (lp && live_verbose > 1)
            printf ("LIVE: start repurposed for %s\n", lp->IP);
    }


    // if still none available, grow list
    if (!lp) {
        clives = (LiveConnection *) realloc (clives, (n_clives+1)*sizeof(LiveConnection));
        if (!clives)
            fatalError ("live web connection: %d", n_clives);
        lp = &clives[n_clives++];
        if (!initLiveConnection(lp,IP))
            return (NULL);
        if (lp)
            printf ("LIVE: grow clives to %d for %s\n", n_clives, lp->IP);
    }

    // read current image if found
    if (lp) {
        printf ("LIVE: reading full image for %s\n", lp->IP);
        readLiveImageNow (lp->pixels);
    }

    // return our best shot
    return (lp);
}

/* save the given image for the given connection.
 * N.B. we assume memory is already allocated
 * UNIX only
 */
static void saveLiveConnectionImage (LiveConnection *lp, uint8_t *pixels)
{
    if (!lp->in_use)
        fatalError ("saving to unused live connection");
    if (live_verbose > 1)
        printf ("LIVE:   saving image for %s\n", lp->IP);
    memcpy ((void*)lp->pixels, pixels, LIVE_NBYTES);
    lp->last_used = time(NULL);
}

/* stbi_write_png_to_func helper to write the given array to the given WiFiClient
 * UNIX only
 */
static void wifiSTBWrite_helper (void *context, void *data, int size)
{
    ((WiFiClient*)context)->write ((uint8_t*)data, size);
    if (live_verbose) {
        printf ("LIVE:   image %d bytes\n", size);
        if (live_verbose > 1) {
            FILE *fp = fopen ("/tmp/live.png", "w");
            fwrite (data, size, 1, fp);
            fclose(fp);
        }
    }


}

/* send complete screen capture as png file.
 * works ok stand-alone but typically used by live-html.cpp
 * UNIX only
 */
static bool getWiFiLivePNG (WiFiClient &client, char *errmsg)
{
    // get connection info for this client
    LiveConnection *lp = startLiveConnection (client.remoteIP());
    if (!lp) {
        strcpy (errmsg, "Max live clients");
        return (false);
    }

    // send the web page header
    resetWatchdog();
    FWIFIPRLN (client, F("HTTP/1.0 200 OK"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: image/png"));
    FWIFIPRLN (client, F("Cache-Control: no-cache"));
    FWIFIPRLN (client, F("Connection: close\r\n"));
    if (live_verbose > 1)
        Serial.println(F("web header sent"));

    // convert to and send as png
    stbi_write_png_compression_level = 2;       // faster with hardly any increase in size
    stbi_write_png_to_func (wifiSTBWrite_helper, &client, BUILD_W, BUILD_H, COMP_RGB, lp->pixels,
                                                                                LIVE_BYPPIX*BUILD_W);

    // never fails :)
    return (true);
}

/* send a collection of changed live screen updates.
 * UNIX only
 */
static bool getWiFiLiveUpdate (WiFiClient &client, char *errmsg)
{
    // require an existing live connection
    LiveConnection *lp = findLiveConnection (client.remoteIP());
    if (!lp) {
        strcpy (errmsg, "No previous image");
        return (false);
    }

    // find time correction so next message arrives just after refresh with good margin
    struct timeval arrive_tv;
    gettimeofday (&arrive_tv, NULL);
    int send_corr = (((4*REFRESH_US - arrive_tv.tv_usec)/10000) + 1000) % 100; // csecs 0 .. 99

    // note in use
    last_live = arrive_tv.tv_sec;

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

    // send the web page header
    resetWatchdog();
    FWIFIPRLN (client, F("HTTP/1.0 200 OK"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: application/octet-stream"));
    FWIFIPRLN (client, F("Cache-Control: no-cache"));
    FWIFIPRLN (client, F("Connection: close\r\n"));
    // Serial.println(F("update header sent"));  

    // curious how long these steps take
    struct timeval tv0;
    gettimeofday (&tv0, NULL);

    // collect the current screen image
    uint8_t *img_now = (uint8_t *) malloc (LIVE_NBYTES);
    if (!img_now)
        fatalError ("Can not malloc(%d) for web image", LIVE_NBYTES);
    if (live_verbose > 1)
        printf ("LIVE: reading pixels for %s update\n", lp->IP);
    readLiveImageNow (img_now);

    if (live_verbose) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        printf ("LIVE: get img_now %ld usec\n", (tv1.tv_sec-tv0.tv_sec)*1000000 + (tv1.tv_usec-tv0.tv_usec));
        gettimeofday (&tv0, NULL);
    }


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
        if (memcmp (&img_now[band_start], &(lp->pixels)[band_start], LIVE_BYPPIX*BLOK_H*BUILD_W) == 0)
            continue;

        // something changed, scan across this band checking each block
        locs[n_regns].l = 0;                            // init n contiguous blocks that start here
        for (int rx = 0; rx < BLOK_NCOLS; rx++) {
            int blok_start = band_start + rx*BLOK_WBYTES;
            uint8_t *now0 = &img_now[blok_start];       // first pixel in this block of current image
            uint8_t *pre0 = &(lp->pixels)[blok_start];  // first pixel in this block of previous image

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
            RegnLoc *lp = &locs[i];
            uint8_t *now0 = &img_now[BUILD_W*LIVE_BYPPIX*(ry+BLOK_H*lp->y) + BLOK_WBYTES*lp->x];
            memcpy (chg0, now0, BLOK_WBYTES*lp->l);
            chg0 += BLOK_WBYTES*lp->l;
        }
    }
    if (n_bloks != (chg0-chg_regns)/BLOK_NBYTES)        // assert
        fatalError ("live regions %d != %d", n_bloks, (chg0-chg_regns)/BLOK_NBYTES);

    if (live_verbose) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        printf ("LIVE:   built %d/%d regions in %ld usec\n", n_regns, n_bloks,
                        (tv1.tv_sec-tv0.tv_sec)*1000000 + (tv1.tv_usec-tv0.tv_usec));
        gettimeofday (&tv0, NULL);
    }

    // send header table of regions -- N.B. must match live-html.cpp expectation.
    // following header is x,y,l of each of n regions in units of blocks
    uint8_t hdr[5+3*n_regns];
    hdr[0] = BLOK_W;                            // block width, pixels
    hdr[1] = BLOK_H;                            // block height, pixels
    hdr[2] = send_corr;                         // send time correction, csec
    hdr[3] = n_regns >> 8;                      // n regions, MSB
    hdr[4] = n_regns & 0xff;                    // n regions, LSB
    for (int i = 0; i < n_regns; i++) {
        hdr[5+3*i] = locs[i].x;
        hdr[6+3*i] = locs[i].y;
        hdr[7+3*i] = locs[i].l;
        if (live_verbose > 2)
            printf ("   %d,%d %dx%d\n", locs[i].x*BLOK_W, locs[i].y*BLOK_H, locs[i].l*BLOK_W, BLOK_H);
    }
    client.write (hdr, sizeof(hdr));

    // followed by one image containing one column BLOK_W wide of all changed regions
    if (n_bloks > 0)
        stbi_write_png_to_func (wifiSTBWrite_helper, &client, BLOK_W*n_bloks, BLOK_H,
                            COMP_RGB, chg_regns, BLOK_WBYTES*n_bloks);

    if (live_verbose) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        printf ("LIVE:   write hdr %d and png in %ld usec\n", (int)sizeof(hdr),
                        (tv1.tv_sec-tv0.tv_sec)*1000000 + (tv1.tv_usec-tv0.tv_usec));
        gettimeofday (&tv0, NULL);
        printf ("LIVE:   message arrived %d msecs after %ld\n", (int)(arrive_tv.tv_usec/1000), (long)nowWO());
    }

    // save as most recent complete image for this client
    saveLiveConnectionImage (lp, img_now);

    if (live_verbose) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        printf ("LIVE:   save img_now %ld usec\n",
                    (tv1.tv_sec-tv0.tv_sec)*1000000+(tv1.tv_usec-tv0.tv_usec));
    }

    // done with temps
    free (chg_regns);
    free (img_now);

    // never fails :)
    return (true);
}

/* host a live session
 * UNIX only
 */
static bool doWiFiLive (WiFiClient &client, char *unused)
{
    (void) unused;

    // send header
    FWIFIPRLN (client, F("HTTP/1.0 200 OK"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: text/html; charset=us-ascii"));
    FWIFIPRLN (client, F("Connection: close\r\n"));             // include extra blank line

    // send page
    extern char live_html[];
    client.print (live_html);

    return (true);
}

/* received command from live client containing a character, act as if typed locally
 * UNIX only
 */
static bool setWiFiChar (WiFiClient &client, char line[])
{
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "char";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // figure out the desired char
    const char *str = wa.value[0];
    int strl = strlen(str);
    char c = str[0];
    if (strl > 1) {
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
        else {
            memmove (line, str, strl+1);         // str points within line
            return (false);
        }
    } else if (strl < 1 || !isprint(c)) {
        sprintf (line, "Unknown char 0x%02X", c);
        return (false);
    }

    // insert into getChar queue
    tft.putChar(c);

    // ack
    startPlainText (client);
    char buf[20];
    snprintf (buf, sizeof(buf), "Typed 0x%02X %c\n", c, c);
    client.print (buf);

    return (true);
}


/* received command from live client containing a mouse motion event (no button), act as if happened locally
 * UNIX only
 */
static bool setWiFiMouse (WiFiClient &client, char line[])
{
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "x";
    wa.name[wa.nargs++] = "y";

    // parse
    if (!parseWebCommand (wa, line, line))
        return (false);

    // set
    int x = atoi(wa.value[0]);
    int y = atoi(wa.value[1]);
    tft.setMouse (x, y);

    // ack
    startPlainText (client);
    char buf[20];
    snprintf (buf, sizeof(buf), "Mouse %d %d\n", x, y);
    client.print (buf);

    // ok
    return (true);
}

/* exit -- makes no sense on ESP
 * UNIX only
 */
static bool doWiFiExit (WiFiClient &client, char *unused)
{
    (void) unused;

    // ack then die
    startPlainText(client);
    FWIFIPRLN (client, F("exiting"));

    Serial.print (F("Exiting\n"));
    doExit();

    // lint
    return (true);
}


#endif // defined(_IS_UNIX)



/* table of command strings, its implementing function and additional info for help.
 * functions are called with user input string beginning just after the command and sans HTTP.
 * N.B. set_ functions returning false shall replace the input string with a brief error message.
 *      functions returning true shall send http reply to client.
 *      get_ command[]'s shall include trailing space to detect and prevent trailing garbage.
 *      table is located down here in this file so all handlers are already conveniently defined above.
 *      last N_UNDOCCMD entries are not shown with help
 * the whole table uses arrays so strings are in ESP FLASH too.
 */
#define CT_MAX_CMD      20                              // max command string length
#define CT_MAX_HELP     60                              // max help string length
#define CT_FUNP(ctp) ((PCTF)pgm_read_dword(&ctp->funp)) // handy function pointer
typedef bool (*PCTF)(WiFiClient &client, char *line);   // ptr to command table function
typedef struct {
    const char command[CT_MAX_CMD];                     // command string
    PCTF funp;                                          // handler function
    const char help[CT_MAX_HELP];                       // more info if available
} CmdTble;
static const CmdTble command_table[] PROGMEM = {
    { "get_capture.bmp ",   getWiFiCaptureBMP,     "get live screen shot in bmp format" },
    { "get_config.txt ",    getWiFiConfig,         "get current display options" },
    { "get_de.txt ",        getWiFiDEInfo,         "get DE info" },
    { "get_dx.txt ",        getWiFiDXInfo,         "get DX info" },
    { "get_dxspots.txt ",   getWiFiDXSpots,        "get DX spots" },
    { "get_satellite.txt ", getWiFiSatellite,      "get current sat info" },
    { "get_satellites.txt ",getWiFiAllSatellites,  "get list of all sats" },
    { "get_sensors.txt ",   getWiFiSensorData,     "get sensor data" },
    { "get_spacewx.txt ",   getWiFiSpaceWx,        "get space weather info" },
    { "get_sys.txt ",       getWiFiSys,            "get system stats" },
    { "get_time.txt ",      getWiFiTime,           "get current time" },
    { "set_alarm?",         setWiFiAlarm,          "state=off|armed&time=HR:MN" },
    { "set_auxtime?",       setWiFiAuxTime,        "format=[one_from_menu]" },
    { "set_cluster?",       setWiFiCluster,        "host=xxx&port=yyy" },
    { "set_defmt?",         setWiFiDEformat,       "fmt=[one_from_menu]&atin=RSAtAt|RSInAgo" },
    { "set_displayOnOff?",  setWiFiDisplayOnOff,   "on|off" },
    { "set_displayTimes?",  setWiFiDisplayTimes,   "on=HR:MN&off=HR:MN&day=[Sun..Sat]&idle=mins" },
    { "set_livespots?",     setWiFiLiveSpots,   "spot=of|by&what=call|grid&data=psk|wspr&bands=all|160,..." },
    { "set_mercenter?",     setWiFiMerCenter,      "lng=X" },
    { "set_mapcolor?",      setWiFiMapColor,       "setup_name=R,G,B" },
    { "set_mapview?",       setWiFiMapView,        "Style=S&Grid=G&Projection=P&RSS=on|off&Night=on|off" },
    { "set_newde?",         setWiFiNewDE,          "grid=AB12&lat=X&lng=Y&TZ=local-utc" },
    { "set_newdx?",         setWiFiNewDX,          "grid=AB12&lat=X&lng=Y&TZ=local-utc" },
    { "set_pane?",          setWiFiPane,           "Pane[123]=X,Y,Z... any from:" },
    { "set_rss?",           setWiFiRSS,            "reset|add=X|file|network|interval=secs" },
    { "set_satname?",       setWiFiSatName,        "abc|none" },
    { "set_sattle?",        setWiFiSatTLE,         "name=abc&t1=line1&t2=line2" },
    { "set_senscorr?",      setWiFiSensorCorr,     "sensor=76|77&dTemp=X&dPres=Y" },
    { "set_stopwatch?",     setWiFiStopwatch,      "reset|run|stop|lap|countdown=mins" },
    { "set_time?",          setWiFiTime,           "change=delta_seconds" },
    { "set_time?",          setWiFiTime,           "ISO=YYYY-MM-DDTHH:MM:SS" },
    { "set_time?",          setWiFiTime,           "Now" },
    { "set_time?",          setWiFiTime,           "unix=secs_since_1970" },
    { "set_title?",         setWiFiTitle,          "msg=hello&fg=R,G,B&bg=R,G,B|rainbow" },
    { "set_touch?",         setWiFiTouch,          "x=X&y=Y&hold=0|1" },
    { "set_voacap?",        setWiFiVOACAP,         "band=80-10&power=W&tz=DE|UTC" },
#if defined(_IS_UNIX)
    { "exit ",              doWiFiExit,            "exit HamClock" },
#endif // defined(_IS_UNIX)
    { "restart ",           doWiFiReboot,          "restart HamClock" },
    { "updateVersion ",     doWiFiUpdate,          "update to latest version"},

    // the following entries are not shown with help -- update N_UNDOCCMD if change
    { "set_demo?",          setWiFiDemo,           "on|off|n=N" },
#if defined(_IS_UNIX)
    { "favicon.ico ",       getWiFifavicon,        "return favicon image" },
    { "get_live.png ",      getWiFiLivePNG,        "get initial live session image" },
    { "get_live.bin ",      getWiFiLiveUpdate,     "get updated live session data" },
    { "live.html ",         doWiFiLive,            "start live session" },
    { "set_char?",          setWiFiChar,           "char=x" },
    { "set_mouse?",         setWiFiMouse,          "x=X&y=Y" },
#endif // defined(_IS_UNIX)
};

#define N_CMDTABLE      NARRAY(command_table)           // n entries in command table
#if defined(_IS_UNIX)
#define N_UNDOCCMD      7                               // n undoc commands at end of table for UNIX
#else
#define N_UNDOCCMD      1                               // n undoc commands at end of table for ESP
#endif // defined(_IS_UNIX)

/* return whether the given command is allowed in read-only web service
 */
static bool roCommand (const char *cmd)
{
    return (strncmp (cmd, "get_", 4) == 0
#if defined(_IS_UNIX)
                    || strncmp (cmd, _FX("favicon.ico"), 11) == 0
                    || strncmp (cmd, _FX("live.html"), 9) == 0
                    || strncmp (cmd, _FX("set_char"), 8) == 0
                    || strncmp (cmd, _FX("set_mouse"), 9) == 0
#endif // defined(_IS_UNIX)
                    || strncmp (cmd, _FX("set_alarm"), 9) == 0
                    || strncmp (cmd, _FX("set_stopwatch"), 13) == 0
                    || strncmp (cmd, _FX("set_touch"), 9) == 0);
}

/* return whether to log a web command -- some are just too frequent for routine logging
 */
static bool logCommandOk (const char *line)
{
    return (!strstr (line, _FX("/get_live.bin"))
                && !strstr (line, _FX("/set_mouse"))
                && !strstr (line, _FX("/set_touch")));
}

/* run the given web server command.
 * send ack or error messages to client.
 * return whether command was found, regardless of whether it returned an error.
 */
static bool runWebserverCommand (WiFiClient &client, bool ro, char *command)
{
    // search for command depending on context, execute its implementation function if found
    if (!ro || roCommand (command)) {
        resetWatchdog();
        for (unsigned i = 0; i < N_CMDTABLE; i++) {
            const CmdTble *ctp = &command_table[i];
            int cmd_len = strlen_P (ctp->command);
            if (strncmp_P (command, ctp->command, cmd_len) == 0) {

                // found command, skip to start of params
                char *params = command+cmd_len;

                // replace any %XX encoded values
                if (replaceEncoding (params))
                    Serial.printf (_FX("Decoded: %s\n"), params);      // print decoded version

                // chop off trailing HTTP _after_ looking for commands because get_ commands end with blank.
                char *http = strstr (params, " HTTP");
                if (http)
                    *http = '\0';

                // run handler, passing string starting right after the command, reply with error if trouble.
                resetWatchdog();
                PCTF funp = CT_FUNP(ctp);
                if (!(*funp)(client, params)) {
                    StackMalloc errmsg(cmd_len+strlen(params)+20);
                    snprintf (errmsg.getMem(), errmsg.getSize(), "%.*s error: %s", cmd_len, command, params);
                    sendHTTPError (client, errmsg.getMem());
                }

                // command found, even if it reported an error
                return (true);
            }
        }
    }

    // not found (or allowed)
    Serial.printf (_FX("Unknown web command: %s\n"), command);
    return (false);
}

/* service remote connection.
 * if ro, only accept get commands and set_touch
 * N.B. caller is expected to call client.stop()
 */
static void serveRemote(WiFiClient &client, bool ro)
{
    StackMalloc line_mem(TLE_LINEL*4);          // accommodate longest query, probably set_sattle with %20s
    char *line = (char *) line_mem.getMem();    // handy access to malloced buffer

    // first line must be the GET except set_rss which can be POST
    if (!getTCPLine (client, line, line_mem.getSize(), NULL)) {
        sendHTTPError (client, _FX("empty web query"));
        return;
    }
    if (strncmp (line, _FX("GET /"), 5) && strncmp (line, _FX("POST /set_rss?"), 14)) {
        Serial.println (line);
        sendHTTPError (client, _FX("Method must be GET (or POST with set_rss)"));
        return;
    }
    // Serial.printf ("web: %s\n", line);

    // discard remainder of header
    (void) httpSkipHeader (client);

    // log most commands but a few are just too frequent
    if (logCommandOk (line) || live_verbose > 1) {
        Serial.print (F("Command from "));
            Serial.print(client.remoteIP());
            Serial.print(F(": "));
            Serial.println(line);
    }

    // run command
    if (runWebserverCommand (client, ro, strchr (line,'/')+1))
        return;

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

}

/* check if someone is trying to tell/ask us something
 */
void checkWebServer(bool ro)
{
    if (remoteServer) {
        WiFiClient client = remoteServer->available();
        if (client) {
            serveRemote(client, ro);
            client.stop();
        }
    }
}

bool initWebServer(char ynot[])
{
    resetWatchdog();

    if (remoteServer)
        return (true);

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
    checkWebServer (true);

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
        9,      // DEMO_NEWDX

                // 5
        3,      // DEMO_MAPPROJ
        3,      // DEMO_MAPNIGHT
        3,      // DEMO_MAPGRID
        1,      // DEMO_MAPSTYLE
        9,      // DEMO_NCDXF
                
                // 10
        2,      // DEMO_VOACAP
        5,      // DEMO_CALLFG
        4,      // DEMO_CALLBG
        12,     // DEMO_DEFMT
        4,      // DEMO_ONAIR

                // 15
        5,      // DEMO_SAT
        5,      // DEMO_EME
        6,      // DEMO_AUXTIME
        1,      // DEMO_PSKMASK
    };

    // record previous choice to avoid repeats
    static DemoChoice prev_choice = DEMO_N;             // init to an impossible choice

    // confirm propabilities sum to 100 on first call
    if (prev_choice == DEMO_N) {
        unsigned sum = 0;
        for (unsigned i = 0; i < DEMO_N; i++)
            sum += item_probs[i];
        if (sum != 100)
            fatalError (_FX("demo probs sum %u != 100\n"), sum);
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
        demoMsg (ok, choice, msg, _FX("RSS %s"), rss_on ? "On" : "Off");
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
        map_proj = (map_proj + 1) % MAPP_N;
        NVWriteUInt8 (NV_MAPPROJ, map_proj);
        initEarthMap();
        if (map_proj != MAPP_MERCATOR)
            slow = true;
        ok = true;
        demoMsg (ok, choice, msg, _FX("Proj %s"), map_projnames[map_proj]);
        break;

    case DEMO_MAPNIGHT:
        night_on = !night_on;
        NVWriteUInt8 (NV_NIGHT_ON, night_on);
        initEarthMap();
        if (map_proj != MAPP_MERCATOR)
            slow = true;
        ok = true;
        demoMsg (ok, choice, msg, _FX("Night %s"), night_on ? "On" : "Off");
        break;

    case DEMO_MAPGRID:
        mapgrid_choice = (mapgrid_choice + 1) % MAPGRID_N;
        NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
        initEarthMap();
        if (map_proj != MAPP_MERCATOR)
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
        brb_mode = brb_mode == BRB_SHOW_SWSTATS ? BRB_SHOW_BEACONS : BRB_SHOW_SWSTATS;
        drawNCDXFBox();
        updateBeacons(true);
        ok = true;
        demoMsg (ok, choice, msg, _FX("NCDXF %s"), brb_mode == BRB_SHOW_BEACONS ? "On" : "Off");
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
            demoMsg (ok, choice, msg, _FX("ONAIR %s"), toggle ? "On" : "Off");
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

    case DEMO_AUXTIME:
        auxtime = (AuxTimeFormat)random(AUXT_N);
        updateClocks(true);
        ok = true;
        demoMsg (ok, choice, msg, "Aux time %s", auxtime_names[(int)auxtime]);
        break;

    case DEMO_PSKMASK:
        psk_mask = 0;
        if (random(100) > 50)
            psk_mask |= PSKMB_OFDE;
        if (random(100) > 50)
            psk_mask |= PSKMB_CALL;
        if (random(100) > 50)
            psk_mask |= PSKMB_PSK;
        ok = true;
        demoMsg (ok, choice, msg, "psk_mask 0x%X", psk_mask);
        break;

    case DEMO_N:
        break;
    }

    return (ok);
}
