/* manage most wifi uses, including pane and map updates.
 */

#include "HamClock.h"


// host name and port of backend server
const char *backend_host = "clearskyinstitute.com";
int backend_port = 80;

// IP where server thinks we came from
char remote_addr[16];                           // INET_ADDRSTRLEN

// user's date and time, UNIX only
time_t usr_datetime;

// RSS info
#define RSS_MAXN        15                      // max number RSS entries to cache
static const char rss_page[] PROGMEM = "/RSS/web15rss.pl";
static char *rss_titles[RSS_MAXN];                  // malloced titles
static uint8_t n_rss_titles, rss_title_i;       // n titles and rolling index
static bool rss_local;                          // if set: don't poll server, assume local titles
uint8_t rss_interval = RSS_DEF_INT;             // polling period, secs

// kp historical and predicted info, new data posted every 3 hours
#define KP_INTERVAL     (3500+randIvl(300))     // polling period, secs
#define KP_COLOR        RA8875_YELLOW           // loading message text color
static const char kp_page[] PROGMEM = "/geomag/kindex.txt";
#define KP_VPD           8                      // number of values per day
#define KP_NHD           7                      // N historical days
#define KP_NPD           2                      // N predicted days
#define KP_NV            ((KP_NHD+KP_NPD)*KP_VPD) // N total Kp values

// xray info, new data posted every 10 minutes
#define XRAY_INTERVAL   (610+randIvl(30))       // polling interval, secs
#define XRAY_LCOLOR     RGB565(255,50,50)       // long wavelength plot color, reddish
#define XRAY_SCOLOR     RGB565(50,50,255)       // short wavelength plot color, blueish
static const char xray_page[] PROGMEM = "/xray/xray.txt";
#define XRAY_NV         (6*25)                  // n lines to collect = 25 hours @ 10 mins per line

// contest info, new data posted every Monday
#define CONTESTS_INTERVAL (3600+randIvl(1000))  // polling interval, secs

// OnTheAir posts can be very rapid
#define ONTA_INTERVAL   (60+randIvl(15))        // polling interval, secs

// sunspot info, new data posted daily
#define SSPOT_INTERVAL  (3400+randIvl(300))     // polling interval, secs
static const char ssn_page[] PROGMEM = "/ssn/ssn-31.txt";
#define SSPOT_NV        31                      // n ssn to plot, 1 per day back 30 days, including 0

// solar flux info, new data posted three times a day
#define SFLUX_INTERVAL  (3300+randIvl(300))     // polling interval, secs
static const char sf_page[] PROGMEM = "/solar-flux/solarflux-99.txt";
#define SFLUX_NV        99                      // n solar flux values, three per day for 33 days

// Bz Bt solar magnetic flux info, new data posted every few minutes
#define BZBT_INTERVAL   (120+randIvl(30))       // polling interval, secs
#define BZBT_BZCOLOR    RGB565(230,75,74)       // BZ plot color
#define BZBT_BTCOLOR    RGB565(100,100,200)     // BT plot color
static const char bzbt_page[] PROGMEM = "/Bz/Bz.txt";
#define BZBT_NV         (6*25)                  // n lines to collect = 25 hours @ 10 mins per line

// solar wind info, new data posted every five minutes
#define SWIND_INTERVAL  (340+randIvl(30))       // polling interval, secs
#define SWIND_COLOR     RA8875_MAGENTA          // loading message text color
static const char swind_page[] PROGMEM = "/solar-wind/swind-24hr.txt";

// world wx table update, new data every hour but N.B. make it longer than OTHER_MAPS_INTERVAL
#define WWX_INTERVAL    (2300+randIvl(200))     // polling interval, secs

// ADIF pane
#define ADIF_INTERVAL   30                      // polling interval, secs

// band conditions and voacap map, models change each hour
#define BC_INTERVAL     (2400+randIvl(200))     // polling interval, secs
#define VOACAP_INTERVAL (2500+randIvl(200))     // polling interval, secs
uint16_t bc_powers[] = {1, 5, 10, 50, 100, 500, 1000};
const int n_bc_powers = NARRAY(bc_powers);
static const char bc_page[] = "/fetchBandConditions.pl";
static BandCdtnMatrix bc_matrix;                // percentage reliability for each band
static time_t bc_time;                          // nowWO() when bc_matrix was loaded
uint16_t bc_power;                              // VOACAP power setting
float bc_toa;                                   // VOACAP take off angle
uint8_t bc_utc_tl;                              // label band conditions timeline in utc else DE local
static time_t map_time;                         // nowWO() when map was loaded

uint8_t bc_modevalue;                           // VOACAP sensitivity value
const BCModeSetting bc_modes[N_BCMODES] {
    {"CW",  19},
    {"SSB", 38},
    {"AM",  49},
    {"WSPR", 3},
    {"FT8", 13},
    {"FT4", 17}
};
uint8_t findBCModeValue (const char *name)      // find value give name, else 0
{
    for (int i = 0; i < N_BCMODES; i++)
        if (strcmp (name, bc_modes[i].name) == 0)
            return (bc_modes[i].value);
    return (0);
}
const char *findBCModeName (uint8_t value)      // find name given value, else NULL
{
    for (int i = 0; i < N_BCMODES; i++)
        if (bc_modes[i].value == value)
            return (bc_modes[i].name);
    return (NULL);
}

// core map update intervals
#if defined(_IS_ESP8266)
#define DRAPMAP_INTERVAL    (900+randIvl(60))   // polling interval, secs -- save FLASH writes
#else
#define DRAPMAP_INTERVAL    (300+randIvl(60))   // polling interval, secs
#endif // _IS_ESP8266
#define OTHER_MAPS_INTERVAL (1800+randIvl(200)) // polling interval, secs

// DRAP plot info, new data posted every few minutes
// collect 24 hours of max value found in each 10 minute interval
#define DRAPDATA_INTERVAL       (10*60)                                 // interval, seconds
#define DRAPDATA_PERIOD         (24*3600)                               // total period, seconds
#define DRAPDATA_NPTS           (DRAPDATA_PERIOD/DRAPDATA_INTERVAL)     // number of points to download
#define DRAPPLOT_INTERVAL       (DRAPMAP_INTERVAL+5)    // polling interval, secs. N.B. avoid race with MAP
#define DRAPPLOT_COLOR  RGB565(188,143,143)                             // plotting color
static const char drap_page[] PROGMEM = "/drap/stats.txt";

// NOAA RSG space weather scales
#define NOAASWX_INTERVAL    (3700+randIvl(300)) // polling interval, secs
static const char noaaswx_page[] PROGMEM = "/NOAASpaceWX/noaaswx.txt";

// geolocation web page
static const char locip_page[] = "/fetchIPGeoloc.pl";

// SDO images
#define SDO_INTERVAL     40                     // annotation update interval, secs
#define SDO_ROT_INTERVAL (90+randIvl(20))       // image and annotation interval when rotating, secs

// weather displays
#define DEWX_INTERVAL   (1700+randIvl(200))     // polling interval, secs
#define DXWX_INTERVAL   (1600+randIvl(200))     // polling interval, secs

// moon display
#define MOON_INTERVAL   50                      // annotation update interval, secs

// Live spots
#define PSK_INTERVAL    (90+randIvl(20))        // polling period. secs

// list of default NTP servers unless user has set their own
static NTPServer ntp_list[] = {                 // init times to 0 insures all get tried initially
    {"time.google.com", 0},
    {"time.apple.com", 0},
    {"pool.ntp.org", 0},
    {"europe.pool.ntp.org", 0},
    {"asia.pool.ntp.org", 0},
    {"time.nist.gov", 0},
};
#define N_NTP NARRAY(ntp_list)                  // number of possible servers


// web site retry interval, secs
#define WIFI_RETRY      (15+randIvl(5))

// pane auto rotation period in seconds
#define ROTATION_INTERVAL       (40+randIvl(5))                 // default pane rotation interval, s

/* "reverting" refers to restoring pane1 after it shows DE or DX weather.
 * pane1_reverting tells panes that can do partial updates that a full update is required.
 * pane1_revtime records when the revert will expire and is used to prevent the underlying pane from
 *   reacting to taps.
 */
static bool pane1_reverting;
static time_t pane1_revtime;

/* time of next update attempts for each pane and the maps.
 * 0 will refresh immediately.
 * reset all in initWiFiRetry()
 */
static time_t next_update[PANE_N];
static time_t next_map;
static time_t next_wwx;
static time_t next_rss;

// persisent space weather data and refresh time for use by getSpaceWeather() and drawSpaceStats()
static time_t ssn_update, xray_update, sflux_update, kp_update, noaa_update, swind_update, bzbt_update;
static time_t drap_update, path_update;
static float ssn_spw = SPW_ERR;
static float xray_spw = SPW_ERR;
static float sflux_spw = SPW_ERR;
static float kp_spw = SPW_ERR;
static float swind_spw = SPW_ERR;
static float drap_spw = SPW_ERR;
static float bz_spw = SPW_ERR, bt_spw = SPW_ERR;
static float path_spw[BMTRX_COLS]; 
static NOAASpaceWx noaa_spw;

// fwd local funcs
static bool updateDRAP(SBox &box);
static bool updateKp(SBox &box);
static bool updateXRay(const SBox &box);
static bool updateSunSpots(const SBox &box);
static bool updateSolarFlux(const SBox &box);
static bool updateBzBt(const SBox &box);
static bool updateBandConditions(const SBox &box);
static bool updateNOAASWx(const SBox &box);
static bool updateSolarWind(const SBox &box);
static bool updateRSS (void);
static uint32_t crackBE32 (uint8_t bp[]);

/* return a random number [-n,n] intended for randomizing update intervals
 */
static int randIvl(int n)
{
    return (random(2*n+1) - n);
}

/* return absolute difference in two time_t regardless of time_t implementation is signed or unsigned.
 */
static time_t tdiff (const time_t t1, const time_t t2)
{
    if (t1 > t2)
        return (t1 - t2);
    if (t2 > t1)
        return (t2 - t1);
    return (0);
}

/* return the next retry time_t.
 * retries are spaced out every WIFI_RETRY
 */
static time_t nextWiFiRetry (void)
{
    int interval = WIFI_RETRY;

    // set and save next retry time
    static time_t prev_try;
    time_t next_t0 = myNow() + interval;                        // interval after now
    time_t next_try = prev_try + interval;                      // interval after prev
    prev_try = next_t0 > next_try ? next_t0 : next_try;         // use whichever is later
    return (prev_try);
}

/* calls nextWiFiRetry() and logs the given string
 */
static time_t nextWiFiRetry (const char *str)
{
    time_t next_try = nextWiFiRetry();
    int dt = next_try - myNow();
    Serial.printf (_FX("Next %s retry in %d sec at %d\n"), str, dt, next_try);
    return (next_try);
}

/* calls nextWiFiRetry() and logs the given plot choice.
 */
static time_t nextWiFiRetry (PlotChoice ch)
{
    time_t next_try = nextWiFiRetry();
    int dt = next_try - myNow();
    int nm = millis()/1000+dt;
    Serial.printf (_FX("Next %s retry in %d sec at %d\n"), plot_names[ch], dt, nm);
    return (next_try);
}

/* figure out when to next rotate the given pane.
 * rotations are spaced out to avoid swamping the server or supporting service.
 */
static time_t nextPaneRotationTime (PlotPane pp)
{
    // start with standard rotation interval
    int interval = ROTATION_INTERVAL;
    time_t rot_time = myNow() + interval;

    // then find soonest rot_time that is at least interval away from all other active panes
    for (int i = 0; i < PANE_N*PANE_N; i++) {           // all permutations
        PlotPane ppi = (PlotPane) (i % PANE_N);
        if (ppi != pp && paneIsRotating((PlotPane)ppi)) {
            if ((rot_time >= next_update[ppi] && rot_time - next_update[ppi] < interval)
                              || (rot_time <= next_update[ppi] && next_update[ppi] - rot_time < interval))
                rot_time = next_update[ppi] + interval;
        }
    }

    return (rot_time);
}

/* given a plot pane return time of its next update.
 * if pane is rotating use pane rotation duration else the given interval.
 */
static time_t nextPaneUpdate (PlotPane pp, int interval)
{
    time_t t0 = myNow();
    time_t next = paneIsRotating(pp) ? nextPaneRotationTime (pp) : t0 + interval;
    int dt = next - t0;
    int nm = millis()/1000+dt;
    Serial.printf (_FX("Next %s pane update in %d sec at %d\n"), plot_names[plot_ch[pp]], dt, nm);
    return (next);
}

/* set de_ll.lat_d and de_ll.lng_d from the given ip else our public ip.
 * report status via tftMsg
 */
static void geolocateIP (const char *ip)
{
    WiFiClient iploc_client;                            // wifi client connection
    float lat, lng;
    char llline[80];
    char ipline[80];
    char credline[80];
    int nlines = 0;

    resetWatchdog();
    if (wifiOk() && iploc_client.connect(backend_host, backend_port)) {

        // create proper query
        size_t l = snprintf (llline, sizeof(llline), "%s", locip_page);
        if (ip)
            l += snprintf (llline+l, sizeof(llline)-l, "?IP=%s", ip);
        Serial.println(llline);

        // send
        httpHCGET (iploc_client, backend_host, llline);
        if (!httpSkipHeader (iploc_client)) {
            Serial.println (F("geoIP header short"));
            goto out;
        }

        // expect 4 lines: LAT=, LNG=, IP= and CREDIT=, anything else first line is error message
        if (!getTCPLine (iploc_client, llline, sizeof(llline), NULL))
            goto out;
        nlines++;
        lat = atof (llline+4);
        if (!getTCPLine (iploc_client, llline, sizeof(llline), NULL))
            goto out;
        nlines++;
        lng = atof (llline+4);
        if (!getTCPLine (iploc_client, ipline, sizeof(ipline), NULL))
            goto out;
        nlines++;
        if (!getTCPLine (iploc_client, credline, sizeof(credline), NULL))
            goto out;
        nlines++;
    }

out:

    if (nlines == 4) {
        // ok

        tftMsg (true, 0, _FX("IP %s geolocation"), ipline+3);
        tftMsg (true, 0, _FX("  by %s"), credline+7);
        tftMsg (true, 0, _FX("  %.2f%c %.2f%c"), fabsf(lat), lat < 0 ? 'S' : 'N',
                                fabsf(lng), lng < 0 ? 'W' : 'E');

        de_ll.lat_d = lat;
        de_ll.lng_d = lng;
        normalizeLL (de_ll);
        NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
        NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
        setNVMaidenhead(NV_DE_GRID, de_ll);
        de_tz.tz_secs = getTZ (de_ll);
        NVWriteInt32(NV_DE_TZ, de_tz.tz_secs);


    } else {
        // trouble, error message if 1 line

        if (nlines == 1) {
            tftMsg (true, 0, _FX("IP geolocation err:"));
            tftMsg (true, 1000, _FX("  %s"), llline);
        } else
            tftMsg (true, 1000, _FX("IP geolocation failed"));
    }

    iploc_client.stop();
    resetWatchdog();
    printFreeHeap (F("geolocateIP"));
}

/* search ntp_list for the fastest so far, or rotate if all bad.
 * N.B. always return one of ntp_list, never NULL
 */
static NTPServer *findBestNTP()
{
    static uint8_t prev_fixed;

    NTPServer *best_ntp = &ntp_list[0];
    int rsp_min = ntp_list[0].rsp_time;

    for (int i = 1; i < N_NTP; i++) {
        NTPServer *np = &ntp_list[i];
        if (np->rsp_time < rsp_min) {
            best_ntp = np;
            rsp_min = np->rsp_time;
        }
    }
    if (rsp_min == NTP_TOO_LONG) {
        prev_fixed = (prev_fixed+1) % N_NTP;
        best_ntp = &ntp_list[prev_fixed];
    }
    return (best_ntp);
}


/* init and connect, inform via tftMsg() if verbose.
 * non-verbose is used for automatic retries that should not clobber the display.
 */
static void initWiFi (bool verbose)
{
    // N.B. look at the usages and make sure this is "big enough"
    static const char dots[] = ".........................................";

    // probable mac when only localhost -- used to detect LAN but no WLAN
    const char *mac_lh = _FX("FF:FF:FF:FF:FF:FF");

    resetWatchdog();

    // begin
    // N.B. ESP seems to reconnect much faster if avoid begin() unless creds change
    // N.B. non-RPi UNIX systems return NULL from getWiFI*()
    WiFi.mode(WIFI_STA);
    const char *myssid = getWiFiSSID();
    const char *mypw = getWiFiPW();
    if (myssid && mypw && (strcmp (WiFi.SSID().c_str(), myssid) || strcmp (WiFi.psk().c_str(), mypw)))
        WiFi.begin ((char*)myssid, (char*)mypw);

    // prep
    resetWatchdog();
    uint32_t t0 = millis();
    uint32_t timeout = verbose ? 30000UL : 3000UL;      // dont wait nearly as long for a retry, millis
    uint16_t ndots = 0;                                 // progress counter
    char mac[30];
    strcpy (mac, WiFi.macAddress().c_str());
    tftMsg (verbose, 0, _FX("MAC addr: %s"), mac);

    // wait for connection
    resetWatchdog();
    if (myssid)
        tftMsg (verbose, 0, "\r");                      // init overwrite
    do {
        if (myssid)
            tftMsg (verbose, 0, _FX("Connecting to %s %.*s\r"), myssid, ndots, dots);
        Serial.printf (_FX("Trying network %d\n"), ndots);
        if (timesUp(&t0,timeout) || ndots == (sizeof(dots)-1)) {
            if (myssid)
                tftMsg (verbose, 1000, _FX("WiFi failed -- signal? credentials?"));
            else
                tftMsg (verbose, 1000, _FX("Network connection attempt failed"));
            return;
        }

        wdDelay(1000);
        ndots++;

        // WiFi.printDiag(Serial);

    } while (strcmp (mac, mac_lh) && (WiFi.status() != WL_CONNECTED));

    // init retry times
    initWiFiRetry();

    // report stats
    resetWatchdog();
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip;
        ip = WiFi.localIP();
        tftMsg (verbose, 0, _FX("IP: %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
        ip = WiFi.subnetMask();
        tftMsg (verbose, 0, _FX("Mask: %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
        ip = WiFi.gatewayIP();
        tftMsg (verbose, 0, _FX("GW: %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
        ip = WiFi.dnsIP();
        tftMsg (verbose, 0, _FX("DNS: %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);

        int rssi;
        if (readWiFiRSSI(rssi)) {
            tftMsg (verbose, 0, _FX("Signal strength: %d dBm"), rssi);
            tftMsg (verbose, 0, _FX("Channel: %d"), WiFi.channel());
        }

        tftMsg (verbose, 0, _FX("S/N: %u"), ESP.getChipId());
    }

    // retrieve cities
    readCities();

    // log server's idea of our IP
    Serial.printf (_FX("Remote_Addr: %s\n"), remote_addr);
}

/* call exactly once to init wifi, maps and maybe time and location.
 * report on initial startup screen with tftMsg.
 */
void initSys()
{
    // start/check WLAN
    initWiFi(true);

    // start web servers
    initWebServer();
#if defined (_IS_UNIX)
    initLiveWeb(true);
#endif

    // init location if desired
    if (useGeoIP() || init_iploc || init_locip) {
        if (WiFi.status() == WL_CONNECTED)
            geolocateIP (init_locip);
        else
            tftMsg (true, 0, _FX("no network for geo IP"));
    } else if (useGPSDLoc()) {
        LatLong ll;
        if (getGPSDLatLong(&ll)) {

            // good -- set de_ll
            de_ll = ll;
            normalizeLL (de_ll);
            NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
            NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
            setNVMaidenhead(NV_DE_GRID, de_ll);
            de_tz.tz_secs = getTZ (de_ll);
            NVWriteInt32(NV_DE_TZ, de_tz.tz_secs);

            tftMsg (true, 0, _FX("GPSD: %.2f%c %.2f%c"),
                                fabsf(de_ll.lat_d), de_ll.lat_d < 0 ? 'S' : 'N',
                                fabsf(de_ll.lng_d), de_ll.lng_d < 0 ? 'W' : 'E');

        } else
            tftMsg (true, 1000, _FX("GPSD: no Lat/Long"));
    }


    // skip box
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    drawStringInBox (_FX("Skip"), skip_b, false, RA8875_WHITE);
    bool skipped_here = false;


    // init time service as desired
    if (useGPSDTime()) {
        if (getGPSDUTC(&gpsd_server)) {
            tftMsg (true, 0, _FX("GPSD: time ok"));
            initTime();
        } else
            tftMsg (true, 1000, _FX("GPSD: no time"));

    } else if (useOSTime()) {
        tftMsg (true, 0, _FX("Time from OS"));

    } else if (WiFi.status() == WL_CONNECTED) {

        if (useLocalNTPHost()) {

            // test user choice
            const char *local_ntp = getLocalNTPHost();
            tftMsg (true, 0, _FX("NTP test %s ...\r"), local_ntp);
            if (getNTPUTC(&ntp_server))
                tftMsg (true, 0, _FX("NTP %s: ok\r"), local_ntp);
            else
                tftMsg (true, 0, _FX("NTP %s: fail\r"), local_ntp);
        } else {

            // try all the NTP servers to find the fastest (with sneaky way out)
            SCoord s;
            drainTouch();
            tftMsg (true, 0, _FX("Finding best NTP ..."));
            NTPServer *best_ntp = NULL;
            for (int i = 0; i < N_NTP; i++) {
                NTPServer *np = &ntp_list[i];

                // measure the next. N.B. assumes we stay in sync
                if (getNTPUTC(&ntp_server) == 0)
                    tftMsg (true, 0, _FX("%s: err\r"), np->server);
                else {
                    tftMsg (true, 0, _FX("%s: %d ms\r"), np->server, np->rsp_time);
                    if (!best_ntp || np->rsp_time < best_ntp->rsp_time)
                        best_ntp = np;
                }

                // cancel scan if found at least one good and tapped or typed
                if (best_ntp && (skip_skip || tft.getChar(NULL,NULL)
                                   || (readCalTouchWS(s) != TT_NONE && inBox (s, skip_b)))) {
                    drawStringInBox (_FX("Skip"), skip_b, true, RA8875_WHITE);
                    Serial.printf (_FX("NTP search cancelled with %s\n"), best_ntp->server);
                    skipped_here = true;
                    break;
                }
            }
            if (!skip_skip)
                wdDelay(800); // linger to show last time
            if (best_ntp)
                tftMsg (true, 0, _FX("Best NTP: %s %d ms\r"), best_ntp->server, best_ntp->rsp_time);
            else
                tftMsg (true, 0, _FX("No NTP\r"));
            drainTouch();
        }
        tftMsg (true, 0, NULL);   // next row

        // go
        initTime();

    } else {

        tftMsg (true, 0, _FX("No time"));
    }

    // track from user's time if set
    if (usr_datetime > 0)
        setTime (usr_datetime);


    // init fs
    LittleFS.begin();
    LittleFS.setTimeCallback(now);

    // init bc_power, bc_toa, bc_utc_tl and bc_modevalue
    if (!NVReadUInt16 (NV_BCPOWER, &bc_power)) {
        bc_power = 100;
        NVWriteUInt16 (NV_BCPOWER, bc_power);
    }
    if (!NVReadFloat (NV_BCTOA, &bc_toa)) {
        bc_toa = 3;
        NVWriteFloat (NV_BCTOA, bc_toa);
    }
    if (!NVReadUInt8 (NV_BC_UTCTIMELINE, &bc_utc_tl)) {
        bc_utc_tl = 0;  // default to local time line
        NVWriteUInt8 (NV_BC_UTCTIMELINE, bc_utc_tl);
    }
    if (!NVReadUInt8 (NV_BCMODE, &bc_modevalue)) {
        bc_modevalue = findBCModeValue("CW");           // default to CW
        NVWriteUInt8 (NV_BCMODE, bc_modevalue);
    }

    // insure core_map is defined
    initCoreMaps();

    // offer time to peruse unless alreay opted to skip
    if (!skipped_here) {
        #define     TO_DS 50                                // timeout delay, decaseconds
        drawStringInBox (_FX("Skip"), skip_b, false, RA8875_WHITE);
        uint8_t s_left = TO_DS/10;                          // seconds remaining
        uint32_t t0 = millis();
        drainTouch();
        for (uint8_t ds_left = TO_DS; !skip_skip && ds_left > 0; --ds_left) {
            SCoord s;
            if (tft.getChar(NULL,NULL) || (readCalTouchWS(s) != TT_NONE && inBox(s, skip_b))) {
                drawStringInBox (_FX("Skip"), skip_b, true, RA8875_WHITE);
                break;
            }
            if ((TO_DS - (millis() - t0)/100)/10 < s_left) {
                // just printing every ds_left/10 is too slow due to overhead
                tftMsg (true, 0, _FX("Ready ... %d\r"), s_left--);
            }
            wdDelay(100);
        }
    }
}

/* update BandConditions pane in box b if needed or requested.
 */
static void checkBandConditions (const SBox &b, bool force)
{
    // skip if not currently showing
    PlotPane bc_pp = findPaneChoiceNow (PLOT_CH_BC);
    if (bc_pp == PANE_NONE)
        return;

    // update if asked to or out of sync with prop map or it's time to refresh or it's just been a while
    bool update_bc = force || (prop_map.active && tdiff(bc_time,map_time)>=3600)
                           || (myNow() > next_update[bc_pp])
                           || (tdiff (nowWO(), bc_time) >= 3600);
    if (!update_bc)
        return;

    if (updateBandConditions(b)) {
        // worked ok so reschedule later
        next_update[bc_pp] = nextPaneUpdate (bc_pp, BC_INTERVAL);
        bc_time = nowWO();
    } else {
        // retry soon
        next_update[bc_pp] = nextWiFiRetry(PLOT_CH_BC);

        // if problem persists more than an hour, this prevents the tdiff's above from being true every time
        map_time = bc_time = nowWO() - 1000;
    }
}

/* check if time to update background map
 */
static void checkMap(void)
{
    // local effective time
    int now_time = nowWO();

    // note whether BC is up
    PlotPane bc_pp = findPaneChoiceNow (PLOT_CH_BC);
    bool bc_up = bc_pp != PANE_NONE;

    // check VOACAP first
    if (prop_map.active) {

        // update if time or to stay in sync with BC if on it's off over an hour
        if (myNow()>next_map || (bc_up && tdiff(map_time,bc_time)>=3600) || tdiff(now_time,map_time)>=3600) {

            // show busy if BC up
            if (bc_up)
                plotBandConditions (plot_b[bc_pp], 1, NULL, NULL);

            // update prop map, schedule next
            bool ok = installFreshMaps();
            if (ok) {
                next_map = myNow() + VOACAP_INTERVAL;           // schedule normal refresh
                map_time = now_time;                            // map is now current
                initEarthMap();                                 // restart fresh

                // sync DRAP plot too if in use
                PlotPane drap_pp = findPaneChoiceNow(PLOT_CH_DRAP);
                if (drap_pp != PANE_NONE)
                    next_update[drap_pp] = myNow();

            } else {
                next_map = nextWiFiRetry("VOACAP");             // schedule retry
                map_time = bc_time;                             // match bc to avoid immediate retry
            }

            // show result of effort if BC up
            if (bc_up)
                plotBandConditions (plot_b[bc_pp], ok ? 0 : -1, NULL, NULL);

            time_t dt = next_map - myNow();
            Serial.printf (_FX("Next VOACAP map check in %ld s at %ld\n"), dt, millis()/1000+dt);
        }

    } else if (core_map != CM_NONE) {

        if (myNow() > next_map || tdiff(now_time,map_time)>=3600) {

            // update map, schedule next
            bool ok = installFreshMaps();
            if (ok) {
                // schedule next refresh
                if (core_map == CM_DRAP)
                    next_map = myNow() + DRAPMAP_INTERVAL;
                else
                    next_map = myNow() + OTHER_MAPS_INTERVAL;

                // update corresponding world wx data
                if (core_map == CM_WX) {
                    fetchWorldWx();
                    next_wwx = myNow() + WWX_INTERVAL;
                }

                // note time of map
                map_time = now_time;                            // map is now current

                // start
                initEarthMap();

            } else
                next_map = nextWiFiRetry(coremap_names[core_map]); // schedule retry

            // insure BC band is off
            if (bc_up)
                plotBandConditions (plot_b[bc_pp], 0, NULL, NULL);

            time_t dt = next_map - myNow();
            Serial.printf (_FX("Next %s map check in %ld s at %ld\n"), coremap_names[core_map],
                                        dt, millis()/1000+dt);
        }

    } else {

        // eh??
        fatalError (_FX("no map"));

    }
}

/* given a GOES XRAY Flux value, return its event level designation in buf.
 */
static char *xrayLevel (float xray, char *buf)
{
    if (xray == SPW_ERR)
        strcpy (buf, "Err");
    else if (xray < 1e-8)
        strcpy (buf, _FX("A0.0"));
    else {
        static const char levels[] = "ABCMX";
        int power = floorf(log10f(xray));
        if (power > -4)
            power = -4;
        float mantissa = xray*powf(10.0F,-power);
        char alevel = levels[8+power];
        snprintf (buf, 10, _FX("%c%.1f"), alevel, mantissa);
    }
    return (buf);
}



/* retrieve latest sun spot indices and time scale in days from now.
 * return whether all ok
 */
static bool retrieveSunSpots (float x[SSPOT_NV], float ssn[SSPOT_NV])
{
    char line[100];
    WiFiClient ss_client;
    bool ok = false;

    // mark value as bad until proven otherwise
    ssn_spw = SPW_ERR;

    Serial.println(ssn_page);
    resetWatchdog();
    if (wifiOk() && ss_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCPGET (ss_client, backend_host, ssn_page);

        // skip response header
        if (!httpSkipHeader (ss_client)) {
            Serial.print (F("SSN header fail\n"));
            goto out;
        }

        // read lines into ssn array and build corresponding time value
        int8_t ssn_i;
        for (ssn_i = 0; ssn_i < SSPOT_NV && getTCPLine (ss_client, line, sizeof(line), NULL); ssn_i++) {
            ssn[ssn_i] = atof(line+11);
            x[ssn_i] = 1-SSPOT_NV + ssn_i;
        }

        updateClocks(false);
        resetWatchdog();

        // ok if all received
        if (ssn_i == SSPOT_NV) {

            ok = true;

            // capture latest for getSpaceWeather() and drawSpaceStats()
            ssn_spw = ssn[SSPOT_NV-1];
            ssn_update = myNow();

        } else {

            Serial.printf (_FX("SSN: data short %d / %d\n"), ssn_i, SSPOT_NV);
        }
    }

    // clean up
out:
    ss_client.stop();
    resetWatchdog();
    return (ok);
}


/* retrieve latest and predicted solar flux indices, return whether all ok.
 */
static bool retrievSolarFlux (float x[SFLUX_NV], float sflux[SFLUX_NV])
{
    StackMalloc line_mem(120);
    char *line = (char *) line_mem.getMem();
    WiFiClient sf_client;
    bool ok = false;

    // mark value as bad until proven otherwise
    sflux_spw = SPW_ERR;

    Serial.println (sf_page);
    resetWatchdog();
    if (wifiOk() && sf_client.connect(backend_host, backend_port)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCPGET (sf_client, backend_host, sf_page);

        // skip response header
        if (!httpSkipHeader (sf_client)) {
            Serial.print (F("SFlux header fail\n"));
            goto out;
        }

        // read lines into flux array and build corresponding time value
        int8_t sflux_i;
        for (sflux_i = 0; sflux_i < SFLUX_NV && getTCPLine(sf_client, line, line_mem.getSize(), NULL);
                                                                        sflux_i++) {
            sflux[sflux_i] = atof(line);
            x[sflux_i] = (sflux_i - (SFLUX_NV-9-1))/3.0F;   // 3x(30 days history + 3 days predictions)
        }

        // ok if found all
        updateClocks(false);
        resetWatchdog();
        if (sflux_i == SFLUX_NV) {

            // capture current value for getSpaceWeather() and drawSpaceStats()
            sflux_spw = sflux[SFLUX_NV-10];         // current value, not predictions
            sflux_update = myNow();
            ok = true;

        } else {

            Serial.printf (_FX("SFlux data short: %d / %d\n"), sflux_i, SFLUX_NV);
        }
    }

    // clean up
out:
    sf_client.stop();
    resetWatchdog();
    return (ok);
}

/* update sflux_spw if not recently done so my pane.
 * return whether a new value is ready.
 */
static bool checkSolarFlux (void)
{
    // use our own delay unless being shown in a pane
    static time_t next_sflux;
    PlotPane sflux_pp = findPaneForChoice (PLOT_CH_FLUX);
    time_t *next_p = sflux_pp == PANE_NONE ? &next_sflux : &next_update[sflux_pp];

    if (myNow() < *next_p)
        return (false);

    StackMalloc x_mem(SFLUX_NV*sizeof(float));
    StackMalloc sflux_mem(SFLUX_NV*sizeof(float));
    float *x = (float *) x_mem.getMem();
    float *sflux = (float *) sflux_mem.getMem();

    bool ok = retrievSolarFlux (x, sflux);
    if (ok) {

        // schedule next
        *next_p = myNow() + SFLUX_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry(PLOT_CH_FLUX);
    }

    // true, albeit may be SPW_ERR
    return (true);
}


/* retrieve and plot latest DRAP frequencies, return whether all ok.
 */
static bool retrieveDRAP (float x[DRAPDATA_NPTS], float y[DRAPDATA_NPTS])
{
    #define _DRAPDATA_MAXMI     (DRAPDATA_NPTS/10)                      // max allowed missing intervals
    #define _DRAP_MINGOODI      (DRAPDATA_NPTS-3600/DRAPDATA_INTERVAL)  // min index with good data

    char line[100];                                                     // text line
    WiFiClient drap_client;                                             // wifi client connection
    bool ok = false;                                                    // set iff all ok

    // want to find any holes in data so init x values to all 0
    memset (x, 0, DRAPDATA_NPTS*sizeof(float));

    // want max in each interval so init y values to all 0
    memset (y, 0, DRAPDATA_NPTS*sizeof(float));

    // mark data as bad until proven otherwise
    drap_spw = SPW_ERR;

    Serial.println (drap_page);
    resetWatchdog();
    if (wifiOk() && drap_client.connect(backend_host, backend_port)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCPGET (drap_client, backend_host, drap_page);

        // skip response header
        if (!httpSkipHeader (drap_client)) {
            Serial.print (F("DRAP header short\n"));
            goto out;
        }

        // init state
        time_t t_now = myNow();

        // read lines, oldest first
        int n_lines = 0;
        while (getTCPLine (drap_client, line, sizeof(line), NULL)) {
            n_lines++;

            // crack
            long utime;
            float min, max, mean;
            if (sscanf (line, _FX("%ld : %f %f %f"), &utime, &min, &max, &mean) != 4) {
                Serial.printf (_FX("DRAP: garbled: %s\n"), line);
                goto out;
            }
            // Serial.printf (_FX("DRAP: %ld %g %g %g\n", utime, min, max, mean);

            // find age for this datum, skip if crazy new or too old
            int age = t_now - utime;
            int xi = DRAPDATA_NPTS*(DRAPDATA_PERIOD - age)/DRAPDATA_PERIOD;
            if (xi < 0 || xi >= DRAPDATA_NPTS) {
                // Serial.printf (_FX("DRAP: skipping age %g hrs\n"), age/3600.0F);
                continue;
            }
            x[xi] = age/(-3600.0F);                             // seconds to hours ago

            // set in array if larger
            if (max > y[xi]) {
                // if (y[xi] > 0)
                    // Serial.printf (_FX("DRAP: saw xi %d utime %ld age %d again\n"), xi, utime, age);
                y[xi] = max;
            }

            // Serial.printf (_FX("DRAP %3d %6d: %g %g\n"), xi, age, x[xi], y[xi]);
        }
        Serial.printf (_FX("DRAP: read %d lines\n"), n_lines);

        // look alive
        updateClocks(false);
        resetWatchdog();

        // check for missing data
        int n_missing = 0;
        int maxi_good = 0;
        for (int i = 0; i < DRAPDATA_NPTS; i++) {
            if (x[i] == 0) {
                x[i] = (DRAPDATA_PERIOD - i*DRAPDATA_PERIOD/DRAPDATA_NPTS)/-3600.0F;
                if (i > 0)
                    y[i] = y[i-1];                      // fill with previous
                Serial.printf (_FX("DRAP: filling missing interval %d at age %g hrs to %g\n"), i, x[i], y[i]);
                n_missing++;
            } else {
                maxi_good = i;
            }
        }

        // check for too much missing or newest too old
        if (n_missing > _DRAPDATA_MAXMI) {
            Serial.print (F("DRAP: data too sparse\n"));
            goto out;
        }
        if (maxi_good < _DRAP_MINGOODI) {
            Serial.print (F("DRAP: data too old\n"));
            goto out;
        }

        // capture for getSpaceWeather()
        drap_spw = y[DRAPDATA_NPTS-1];
        drap_update = t_now;

        // ok!
        ok = true;

    } else {

        Serial.print (F("DRAP: connection failed\n"));
    }

    // clean up
out:
    drap_client.stop();
    resetWatchdog();
    return (ok);
}

/* update drap_spw if not recently done so by its pane.
 * return whether a new value is ready.
 */
static bool checkDRAP ()
{
    // use our own delay unless being shown in a pane
    static time_t next_drap;
    PlotPane drap_pp = findPaneForChoice (PLOT_CH_DRAP);
    time_t *next_p = drap_pp == PANE_NONE ? &next_drap : &next_update[drap_pp];

    if (myNow() < *next_p)
        return (false);

    StackMalloc x_mem(DRAPDATA_NPTS*sizeof(float));
    StackMalloc y_mem(DRAPDATA_NPTS*sizeof(float));
    float *x = (float*)x_mem.getMem();
    float *y = (float*)y_mem.getMem();

    bool ok = retrieveDRAP (x, y);
    if (ok) {

        // schedule next
        *next_p = myNow() + DRAPPLOT_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry(PLOT_CH_DRAP);
    }

    // true, albeit may be SPW_ERR
    return (true);
}

/* retrieve latest and predicted kp indices, return whether all ok
 */
static bool retrieveKp (float kpx[KP_NV], float kp[KP_NV])
{
    int kp_i = 0;                                       // next kp index to use
    char line[100];                                     // text line
    WiFiClient kp_client;                               // wifi client connection
    bool ok = false;                                    // set iff all ok

    // mark value as bad until proven otherwise
    kp_spw = SPW_ERR;

    Serial.println(kp_page);
    resetWatchdog();
    if (wifiOk() && kp_client.connect(backend_host, backend_port)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCPGET (kp_client, backend_host, kp_page);

        // skip response header
        if (!httpSkipHeader (kp_client)) {
            Serial.print (F("Kp header short\n"));
            goto out;
        }

        // read lines into kp array and build x
        const int now_i = KP_NHD*KP_VPD-1;              // last historic is now
        for (kp_i = 0; kp_i < KP_NV && getTCPLine (kp_client, line, sizeof(line), NULL); kp_i++) {
            kp[kp_i] = atof(line);
            kpx[kp_i] = (kp_i-now_i)/(float)KP_VPD;
            // Serial.printf ("%2d%c: kp[%5.3f] = %g from \"%s\"\n", kp_i, kp_i == now_i ? '*' : ' ', kpx[kp_i], kp[kp_i], line);
        }

        // ok if all
        if (kp_i == KP_NV) {

            // save current (not last!) value for getSpaceWeather()
            kp_spw = kp[now_i];
            kp_update = myNow();

            ok = true;

        } else {

            Serial.printf (_FX("Kp data short: %d of %d\n"), kp_i, KP_NV);
        }
    }

    // clean up
out:
    kp_client.stop();
    resetWatchdog();
    return (ok);
}

/* update kp_spw if not recently done so by its pane.
 * return whether a new value is ready.
 */
static bool checkKp (void)
{
    // use our own delay unless being shown in a pane
    static time_t next_kp;
    PlotPane kp_pp = findPaneForChoice (PLOT_CH_KP);
    time_t *next_p = kp_pp == PANE_NONE ? &next_kp : &next_update[kp_pp];

    if (myNow() < *next_p)
        return (false);

    StackMalloc kpx_mem(KP_NV*sizeof(float));
    StackMalloc kp_mem(KP_NV*sizeof(float));
    float *kpx = (float*)kpx_mem.getMem();              // days ago
    float *kp = (float*)kp_mem.getMem();                // kp collection

    bool ok = retrieveKp (kpx, kp);
    if (ok) {

        // schedule next
        *next_p = myNow() + KP_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry(PLOT_CH_KP);
    }

    // true, albeit may be SPW_ERR
    return (true);
}

/* retrieve latest xray indices, return whether all ok
 */
static bool retrieveXRay (float lxray[XRAY_NV], float sxray[XRAY_NV], float x[XRAY_NV])
{
    uint8_t xray_i;                                     // next index to use
    WiFiClient xray_client;
    char line[100];
    uint16_t ll;
    bool ok = false;

    // mark value as bad until proven otherwise
    xray_spw = SPW_ERR;

    Serial.println(xray_page);
    resetWatchdog();
    if (wifiOk() && xray_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCPGET (xray_client, backend_host, xray_page);

        // soak up remaining header
        if (!httpSkipHeader (xray_client)) {
            Serial.print (F("XRay header short\n"));
            goto out;
        }

        // collect content lines and extract both wavelength intensities
        xray_i = 0;
        float raw_lxray = 0;
        while (xray_i < XRAY_NV && getTCPLine (xray_client, line, sizeof(line), &ll)) {
            // Serial.println(line);

            if (line[0] == '2' && ll >= 56) {

                // short
                float s = atof(line+35);
                if (s <= 0)                             // missing values are set to -1.00e+05, also guard 0
                    s = 1e-9;
                sxray[xray_i] = log10f(s);

                // long
                float l = atof(line+47);
                if (l <= 0)                             // missing values are set to -1.00e+05, also guard 0
                    l = 1e-9;
                lxray[xray_i] = log10f(l);
                raw_lxray = l;                          // last one will be current

                // time in hours back from 0
                x[xray_i] = (xray_i-XRAY_NV)/6.0;       // 6 entries per hour

                // good
                xray_i++;
            }
        }

        // proceed iff we found all
        if (xray_i == XRAY_NV) {

            // capture for getSpaceWeather() and drawSpaceStats()
            xray_spw = raw_lxray;
            xray_update = myNow();

            ok = true;

        } else {

            Serial.printf (_FX("XRay data short %d of %d\n"), xray_i, XRAY_NV);
        }
    }

out:

    xray_client.stop();
    resetWatchdog();
    return (ok);
}

/* update xray_spw if not recently done so by its pane.
 * return whether a new value is ready.
 */
static bool checkXRay (void)
{
    // use our own delay unless being shown in a pane
    static time_t next_xray;
    PlotPane xray_pp = findPaneForChoice (PLOT_CH_XRAY);
    time_t *next_p = xray_pp == PANE_NONE ? &next_xray : &next_update[xray_pp];

    if (myNow() < *next_p)
        return (false);

    StackMalloc lxray_mem(XRAY_NV*sizeof(float));
    StackMalloc sxray_mem(XRAY_NV*sizeof(float));
    StackMalloc x_mem(XRAY_NV*sizeof(float));
    float *lxray = (float *) lxray_mem.getMem();        // long wavelength values
    float *sxray = (float *) sxray_mem.getMem();        // short wavelength values
    float *x = (float *) x_mem.getMem();                // x coords of plot

    bool ok = retrieveXRay (lxray, sxray, x);
    if (ok) {

        // schedule next
        *next_p = myNow() + XRAY_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry(PLOT_CH_XRAY);
    }

    // true, albeit may be SPW_ERR
    return (true);
}

/* retrieve latest bzbt indices, return whether all ok
 */
static bool retrieveBzBt (float bzbt_hrsold[BZBT_NV], float bz[BZBT_NV], float bt[BZBT_NV])
{
    int bzbt_i;                                     // next index to use
    WiFiClient bzbt_client;
    char line[100];
    bool ok = false;
    time_t t0 = myNow();

    // mark value as bad until proven otherwise
    bz_spw = bt_spw = SPW_ERR;

    Serial.println(bzbt_page);
    resetWatchdog();
    if (wifiOk() && bzbt_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCPGET (bzbt_client, backend_host, bzbt_page);

        // skip over remaining header
        if (!httpSkipHeader (bzbt_client)) {
            Serial.print (F("BZBT: header short\n"));
            goto out;
        }

        // collect content lines and extract both magnetic values, oldest first (newest last :-)
        // # UNIX        Bx     By     Bz     Bt
        // 1684087500    1.0   -2.7   -3.2    4.3
        bzbt_i = 0;
        while (bzbt_i < BZBT_NV && getTCPLine (bzbt_client, line, sizeof(line), NULL)) {

            // crack
            // Serial.printf("BZBT: %d %s\n", bzbt_i, line);
            long unix;
            float this_bz, this_bt;
            if (sscanf (line, "%ld %*f %*f %f %f", &unix, &this_bz, &this_bt) != 3) {
                // Serial.printf ("BZBT: rejecting %s\n", line);
                continue;
            }

            // store at bzbt_i
            bz[bzbt_i] = this_bz;
            bt[bzbt_i] = this_bt;

            // time in hours back from now but clamp at 0 in case we are slightly late
            bzbt_hrsold[bzbt_i] = unix < t0 ? (unix - t0)/3600.0 : 0;

            // good
            bzbt_i++;
        }

        // proceed iff we found all and current
        if (bzbt_i == BZBT_NV && bzbt_hrsold[BZBT_NV-1] > -0.25F) {

            // capture latest for getSpaceWeather() and drawSpaceStats()
            bz_spw = bz[BZBT_NV-1];
            bt_spw = bt[BZBT_NV-1];
            bzbt_update = t0 + 3600*bzbt_hrsold[BZBT_NV-1];

            // good!
            ok = true;

        } else {

            if (bzbt_i < BZBT_NV)
                Serial.printf (_FX("BZBT: data short %d of %d\n"), bzbt_i, BZBT_NV);
            else
                Serial.printf (_FX("BZBT: data %g hrs old\n"), -bzbt_hrsold[BZBT_NV-1]);
        }
    }

out:

    bzbt_client.stop();
    resetWatchdog();
    return (ok);
}

/* update bz_spw and bt_spw if not recently done so by its pane.
 * return whether a new value is ready.
 */
static bool checkBzBt(void)
{
    // use our own delay unless being shown in a pane
    static time_t next_bzbt;
    PlotPane bzbt_pp = findPaneForChoice (PLOT_CH_BZBT);
    time_t *next_p = bzbt_pp == PANE_NONE ? &next_bzbt : &next_update[bzbt_pp];

    if (myNow() < *next_p)
        return (false);

    StackMalloc old_mem(BZBT_NV*sizeof(float));
    StackMalloc bz_mem(BZBT_NV*sizeof(float));
    StackMalloc bt_mem(BZBT_NV*sizeof(float));
    float *old = (float *) old_mem.getMem();
    float *bz = (float *) bz_mem.getMem();
    float *bt = (float *) bt_mem.getMem();

    bool ok = retrieveBzBt (old, bz, bt);
    if (ok) {

        // schedule next
        *next_p = myNow() + BZBT_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry(PLOT_CH_BZBT);
    }

    // true, albeit may be SPW_ERR
    return (true);
}

/* check for tap at s known to be within BandConditions box b:
 *    tapping left-half band toggles REF map, right-half toggles TOA map
 *    tapping timeline toggles bc_utc_tl;
 *    tapping power offers power menu;
 *    tapping TOA offers take-off menu;
 *    tapping SP/LP toggles.
 * return whether tap was useful for us.
 * N.B. coordinate tap positions with plotBandConditions()
 */
bool checkBCTouch (const SCoord &s, const SBox &b)
{
    // not ours if tap title or not in our box
    if (!inBox (s, b) || s.y < b.y+PANETITLE_H)
        return (false);

    // tap area for power cycle
    SBox power_b;
    power_b.x = b.x + 5;
    power_b.y = b.y + 13*b.h/14;
    power_b.w = b.w/5;
    power_b.h = b.h/12;
    // drawSBox (power_b, RA8875_RED);     // RBF

    // tap area for mode choice
    SBox mode_b;
    mode_b.x = power_b.x + power_b.w + 1;
    mode_b.y = power_b.y;
    mode_b.w = b.w/6;
    mode_b.h = power_b.h;
    // drawSBox (mode_b, RA8875_RED);      // RBF

    // tap area for TOA
    SBox toa_b;
    toa_b.x = mode_b.x + mode_b.w + 1;
    toa_b.y = mode_b.y;
    toa_b.w = b.w/5;
    toa_b.h = mode_b.h;
    // drawSBox (toa_b, RA8875_RED);       // RBF

    // tap area for SP/LP
    SBox splp_b;
    splp_b.x = toa_b.x + toa_b.w + 1;
    splp_b.y = toa_b.y;
    splp_b.w = b.w/6;
    splp_b.h = toa_b.h;
    // drawSBox (splp_b, RA8875_RED);      // RBF

    // tap area for timeline strip
    SBox tl_b;
    tl_b.x = b.x + 1;
    tl_b.y = b.y + 12*b.h/14;
    tl_b.w = b.w - 2;
    tl_b.h = b.h/12;
    // drawSBox (tl_b, RA8875_WHITE);      // RBF

    if (inBox (s, power_b)) {

        // build menu of available power choices
        MenuItem mitems[n_bc_powers];
        char labels[n_bc_powers][20];
        for (int i = 0; i < n_bc_powers; i++) {
            MenuItem &mi = mitems[i];
            mi.type = MENU_1OFN;
            mi.set = bc_power == bc_powers[i];
            mi.group = 1;
            mi.indent = 5;
            mi.label = labels[i];
            snprintf (labels[i], sizeof(labels[i]), "%d watt%s", bc_powers[i], bc_powers[i] > 1 ? "s" : ""); 
        };

        SBox menu_b;
        menu_b.x = power_b.x;
        menu_b.y = b.y + b.h/4;
        menu_b.w = 0;           // shrink to fit

        // run menu, find selection
        SBox ok_b;
        MenuInfo menu = {menu_b, ok_b, true, false, 1, NARRAY(mitems), mitems};
        uint16_t new_power = bc_power;
        if (runMenu (menu)) {
            for (int i = 0; i < n_bc_powers; i++) {
                if (menu.items[i].set) {
                    new_power = bc_powers[i];
                    break;
                }
            }
        }

        // always redo BC if nothing else to erase menu but only update voacap if power changed
        bool power_changed = new_power != bc_power;
        bc_power = new_power;
        NVWriteUInt16 (NV_BCPOWER, bc_power);
        checkBandConditions (b, true);
        if (power_changed)
            scheduleNewVOACAPMap(prop_map);

    } else if (inBox (s, mode_b)) {

        // show menu of available mode choices
        MenuItem mitems[N_BCMODES];
        for (int i = 0; i < N_BCMODES; i++)
            mitems[i] = {MENU_1OFN, bc_modevalue == bc_modes[i].value, 1, 5, bc_modes[i].name};

        SBox menu_b;
        menu_b.x = mode_b.x;
        menu_b.y = b.y + b.h/3;
        menu_b.w = 0;           // shrink to fit

        // run menu, find selection
        SBox ok_b;
        MenuInfo menu = {menu_b, ok_b, true, false, 1, N_BCMODES, mitems};
        uint16_t new_modevalue = bc_modevalue;
        if (runMenu (menu)) {
            for (int i = 0; i < N_BCMODES; i++) {
                if (menu.items[i].set) {
                    new_modevalue = bc_modes[i].value;
                    break;
                }
            }
        }

        // always redo BC if nothing else to erase menu but only update voacap if mode changed
        if (new_modevalue != bc_modevalue) {
            bc_modevalue = new_modevalue;
            NVWriteUInt8 (NV_BCMODE, bc_modevalue);
            scheduleNewVOACAPMap(prop_map);
        }
        checkBandConditions (b, true);

    } else if (inBox (s, toa_b)) {

        // show menu of available TOA choices
        // N.B. line display width can only accommodate 1 character
        MenuItem mitems[3];
        mitems[0] = {MENU_1OFN, bc_toa <= 1,              1, 5, "1 deg"};
        mitems[1] = {MENU_1OFN, bc_toa > 1 && bc_toa < 9, 1, 5, "3 degs"};
        mitems[2] = {MENU_1OFN, bc_toa >= 9,              1, 5, "9 degs"};

        SBox menu_b;
        menu_b.x = toa_b.x;
        menu_b.y = b.y + b.h/2;
        menu_b.w = 0;           // shrink to fit

        // run menu, find selection
        SBox ok_b;
        MenuInfo menu = {menu_b, ok_b, true, false, 1, NARRAY(mitems), mitems};
        float new_toa = bc_toa;
        if (runMenu (menu)) {
            for (int i = 0; i < NARRAY(mitems); i++) {
                if (menu.items[i].set) {
                    new_toa = atof (mitems[i].label);
                    break;
                }
            }
        }

        // always redo BC if nothing else to erase menu but only update voacap if mode changed
        if (new_toa != bc_toa) {
            bc_toa = new_toa;
            NVWriteFloat (NV_BCTOA, bc_toa);
            scheduleNewVOACAPMap(prop_map);
        }
        checkBandConditions (b, true);

    } else if (inBox (s, splp_b)) {

        // toggle short/long path -- update DX info too
        show_lp = !show_lp;
        NVWriteUInt8 (NV_LP, show_lp);
        checkBandConditions (b, true);
        drawDXInfo ();
        scheduleNewVOACAPMap(prop_map);
    
    } else if (inBox (s, tl_b)) {

        // toggle bc_utc_tl and redraw
        bc_utc_tl = !bc_utc_tl;
        NVWriteUInt8 (NV_BC_UTCTIMELINE, bc_utc_tl);
        plotBandConditions (b, 0, NULL, NULL);

    } else {

        // check tapping a row in the table. if so toggle band and type.

        PropMapSetting new_prop_map = prop_map;
        PropMapBand tap_band = (PropMapBand) ((b.y + b.h - 20 - s.y) / ((b.h - 47)/BMTRX_COLS));
        PropMapType tap_type = s.x < b.x + b.w/2 ? PROPTYPE_REL : PROPTYPE_TOA;
        if (prop_map.active && tap_band == prop_map.band && tap_type == prop_map.type) {
            // tapped same prop map, turn off active VOACAP selection
            new_prop_map.active = false;
        } else if (tap_band >= 0 && tap_band < PROPBAND_N) {
            // tapped a different VOACAP selection
            new_prop_map.active = true;
            new_prop_map.band = tap_band;
            new_prop_map.type = tap_type;
        }

        // update
        scheduleNewVOACAPMap(new_prop_map);
        if (!new_prop_map.active) {
            scheduleNewCoreMap (core_map);
            plotBandConditions (b, 0, NULL, NULL);  // indicate no longer active
        }

    }

    // ours just because tap was below title
    return (true);
}

/* keep the NCDXF_b up to date.
 * N.B. this is called often so do minimal work.
 */
static void checkBRB (time_t t)
{
    // routine update of NCFDX map beacons
    updateBeacons (false, false);

    // see if it's time to rotate
    if (t > brb_updateT) {

        // move brb_mode to next rotset if rotating
        if (BRBIsRotating()) {
            for (int i = 1; i < BRB_N; i++) {
                int next_mode = (brb_mode + i) % BRB_N;
                if (brb_rotset & (1 << next_mode)) {
                    brb_mode = next_mode;
                    Serial.printf (_FX("BRB rotating to mode \"%s\"\n"), brb_names[brb_mode]);
                    break;
                }
            }
        }

        // update brb_mode
        if (!drawNCDXFBox()) {

            // trouble: retry
            brb_updateT = nextWiFiRetry("BRB");

        } else {

            // ok: sync rotation with next soonest rotating pane if any, else use standard pane rotation
            time_t next_rotT = 0;
            for (int i = 0; i < PANE_N; i++) {
                if (paneIsRotating((PlotPane)i)) {
                    if (!next_rotT || next_update[i] < next_rotT)
                        next_rotT = next_update[i];
                }
            }
            if (next_rotT)
                brb_updateT = next_rotT;
            else
                brb_updateT = t + ROTATION_INTERVAL;
        }

        int dt = brb_updateT - myNow();
        int nm = millis()/1000+dt;
        Serial.printf (_FX("Next BRB pane update in %d sec at %d\n"), dt, nm);

    } else {

        // check a few that need spontaneous updating

        switch (brb_mode) {

        case BRB_SHOW_BME76:
        case BRB_SHOW_BME77:
            // only if new BME
            if (newBME280data())
                (void) drawNCDXFBox();
            break;

        case BRB_SHOW_SWSTATS:
            // only if new space stats
            if (checkSpaceStats())
                (void) drawNCDXFBox();
            break;

        case BRB_SHOW_DEWX:
        case BRB_SHOW_DXWX:
            // routine drawNCDXFBox() are enough
            break;

        case BRB_SHOW_PHOT:
        case BRB_SHOW_BR:
            // these are updated from followBrightness() in main loop()
            break;
        }

    }
}

/* arrange to resume PANE_1 after dt millis
 */
static void revertPane1 (uint32_t dt)
{
    // set next update time and note a full restore is required.
    pane1_revtime = next_update[PANE_1] = myNow() + dt/1000;
    pane1_reverting = true;

    // a few plot types require extra processing
    switch (plot_ch[PANE_1]) {
    case PLOT_CH_DXCLUSTER:
        closeDXCluster();       // reopen after revert
        break;
    case PLOT_CH_GIMBAL:
        closeGimbal();          // reopen after revert
        break;
    default:
        break;                  // lint
    }
}

/* set the given pane to the given plot choice now.
 * return whether successful.
 * N.B. we might change plot_ch but we NEVER change plot_rotset here
 * N.B. it's harmless to set pane to same choice again.
 */
bool setPlotChoice (PlotPane pp, PlotChoice ch)
{
    // ignore if new choice is already in some other pane
    PlotPane pp_now = findPaneForChoice (ch);
    if (pp_now != PANE_NONE && pp_now != pp)
        return (false);

    // display box
    SBox &box = plot_b[pp];

    // first check a few plot types that require extra tests or processing.
    switch (ch) {
    case PLOT_CH_DXCLUSTER:
        if (!useDXCluster() || pp == PANE_1)    // cluster not allowed on pane 1 to avoid disconnect for wx
            return (false);
        break;
    case PLOT_CH_GIMBAL:
        if (!haveGimbal())
            return (false);
        break;
    case PLOT_CH_TEMPERATURE:
        if (getNBMEConnected() == 0)
            return (false);
        drawOneBME280Pane (box, ch);
        break;
    case PLOT_CH_PRESSURE:
        if (getNBMEConnected() == 0)
            return (false);
        drawOneBME280Pane (box, ch);
        break;
    case PLOT_CH_HUMIDITY:
        if (getNBMEConnected() == 0)
            return (false);
        drawOneBME280Pane (box, ch);
        break;
    case PLOT_CH_DEWPOINT:
        if (getNBMEConnected() == 0)
            return (false);
        drawOneBME280Pane (box, ch);
        break;
    case PLOT_CH_COUNTDOWN:
        if (getSWEngineState(NULL,NULL) != SWE_COUNTDOWN)
            return (false);
        if (getSWDisplayState() == SWD_NONE)
            drawMainPageStopwatch(true);
        break;
    default:
        break;          // lint
    }

    // ok, commit choice to the given pane with immediate refresh
    plot_ch[pp] = ch;
    next_update[pp] = 0;

    // insure DX and gimbal are off if no longer selected for display
    if (findPaneChoiceNow (PLOT_CH_DXCLUSTER) == PANE_NONE)
        closeDXCluster();
    if (findPaneChoiceNow (PLOT_CH_GIMBAL) == PANE_NONE)
        closeGimbal();

    // reset OnTheAir if no longer used
    checkOnTheAirActive();

    // persist
    savePlotOps();

    // ok!
    return (true);
}

/* check if it is time to update any info via wifi.
 * proceed even if no wifi to allow subsystems to update.
 */
void updateWiFi(void)
{
    resetWatchdog();

    // time now
    time_t t0 = myNow();

    // update each pane
    for (int i = PANE_1; i < PANE_N; i++) {

        SBox &box = plot_b[i];
        PlotChoice ch = plot_ch[i];
        PlotPane pp = (PlotPane)i;
        bool new_rot_ch = false;

        // rotate if this pane is rotating and it's time but not if being forced
        if (paneIsRotating(pp) && next_update[i] > 0 && t0 >= next_update[i]) {
            setPlotChoice (pp, getNextRotationChoice(pp, plot_ch[pp]));
            new_rot_ch = true;
            ch = plot_ch[pp];
        }

        switch (ch) {

        case PLOT_CH_BC:
            // does its own timing
            checkBandConditions (box, false);
            break;

        case PLOT_CH_DEWX:
            if (t0 >= next_update[pp]) {
                if (updateDEWX(box))
                    next_update[pp] = nextPaneUpdate (pp, DEWX_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_DXCLUSTER:
            if (t0 >= next_update[pp]) {
                if (updateDXCluster(box))
                    next_update[pp] = 0;   // constant poll
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_DXWX:
            if (t0 >= next_update[pp]) {
                if (updateDXWX(box))
                    next_update[pp] = nextPaneUpdate (pp, DXWX_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_FLUX:
            if (t0 >= next_update[pp]) {
                if (updateSolarFlux(box))
                    next_update[pp] = nextPaneUpdate (pp, SFLUX_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_KP:
            if (t0 >= next_update[pp]) {
                if (updateKp(box))
                    next_update[pp] = nextPaneUpdate (pp, KP_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_MOON:
            if (t0 >= next_update[pp]) {
                updateMoonPane (box, next_update[pp] == 0 || pane1_reverting);
                next_update[pp] = nextPaneUpdate (pp, MOON_INTERVAL);
                pane1_reverting = false;
            }
            break;

        case PLOT_CH_NOAASWX:
            if (t0 >= next_update[pp]) {
                if (updateNOAASWx(box))
                    next_update[pp] = nextPaneUpdate (pp, NOAASWX_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_SSN:
            if (t0 >= next_update[pp]) {
                if (updateSunSpots(box))
                    next_update[pp] = nextPaneUpdate (pp, SSPOT_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_XRAY:
            if (t0 >= next_update[pp]) {
                if (updateXRay(box))
                    next_update[pp] = nextPaneUpdate (pp, XRAY_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_GIMBAL:
            if (t0 >= next_update[pp]) {
                updateGimbal(box);
                next_update[pp] = 0;                // constant poll
            }
            break;

        case PLOT_CH_TEMPERATURE:               // fallthru
        case PLOT_CH_PRESSURE:                  // fallthru
        case PLOT_CH_HUMIDITY:                  // fallthru
        case PLOT_CH_DEWPOINT:
            if (t0 >= next_update[pp]) {
                drawOneBME280Pane (box, ch);
                next_update[pp] = nextPaneUpdate (pp, ROTATION_INTERVAL);
            } else if (newBME280data()) {
                drawOneBME280Pane (box, ch);
            }
            break;

        case PLOT_CH_SDO:
            if (t0 >= next_update[pp]) {
                if (updateSDOPane (box, next_update[pp] == 0 || pane1_reverting)) 
                    next_update[pp] = nextPaneUpdate (pp,
                                    (isSDORotating() ? SDO_ROT_INTERVAL : SDO_INTERVAL));
                else
                    next_update[pp] = nextWiFiRetry(ch);
                pane1_reverting = false;
            }
            break;

        case PLOT_CH_SOLWIND:
            if (t0 >= next_update[pp]) {
                if (updateSolarWind(box))
                    next_update[pp] = nextPaneUpdate (pp, SWIND_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_DRAP:
            if (t0 >= next_update[pp]) {
                if (updateDRAP(box))
                    next_update[pp] = nextPaneUpdate (pp, DRAPPLOT_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_COUNTDOWN:
            // handled by stopwatch system
            break;

        case PLOT_CH_CONTESTS:
            if (t0 >= next_update[pp]) {
                if (updateContests(box))
                    next_update[pp] = nextPaneUpdate (pp, CONTESTS_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_PSK:
            if (t0 >= next_update[pp]) { 
                if (updatePSKReporter(box))
                    next_update[pp] = nextPaneUpdate (pp, PSK_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_BZBT:
            if (t0 >= next_update[pp]) { 
                if (updateBzBt(box))
                    next_update[pp] = nextPaneUpdate (pp, BZBT_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_POTA:
            if (t0 >= next_update[pp]) { 
                if (updateOnTheAir(box, ONTA_POTA))
                    next_update[pp] = nextPaneUpdate (pp, ONTA_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_SOTA:
            if (t0 >= next_update[pp]) { 
                if (updateOnTheAir(box, ONTA_SOTA))
                    next_update[pp] = nextPaneUpdate (pp, ONTA_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(ch);
            }
            break;

        case PLOT_CH_ADIF:
            if (t0 >= next_update[pp]) {
                updateADIF (box);
                next_update[pp] = nextPaneUpdate (pp, ADIF_INTERVAL);
            }
            break;

        case PLOT_CH_N:
            break;              // lint
        }

        // show immediately this is as new rotating pane
        if (new_rot_ch)
            showRotatingBorder ();
    }

    // freshen ADIF memory usage
    checkADIF();

    // freshen NCDXF_b
    checkBRB(t0);

    // freshen world weather table unless wx map is doing it
    if (t0 >= next_wwx) {
        if (!prop_map.active && core_map == CM_WX)
            next_map = 0;                       // this causes checkMap() to call fetchWorldWx()
        else
            fetchWorldWx();
        next_wwx = myNow() + WWX_INTERVAL;
    }

    // check if time to update map
    checkMap();

    // freshen RSS
    if (t0 >= next_rss) {
        if (updateRSS())
            next_rss = myNow() + rss_interval;
        else
            next_rss = nextWiFiRetry();
    }

    // check for server commands
    checkWebServer(false);
}


/* NTP time server query.
 * returns UNIX time and server used if ok, or 0 if trouble.
 * for good NTP packet description try
 *   http://www.cisco.com
 *      /c/en/us/about/press/internet-protocol-journal/back-issues/table-contents-58/154-ntp.html
 */
time_t getNTPUTC(const char **server)
{
    // NTP contents packet
    static const uint8_t timeReqA[] = { 0xE3, 0x00, 0x06, 0xEC };
    static const uint8_t timeReqB[] = { 0x31, 0x4E, 0x31, 0x34 };

    // N.B. do not call wifiOk: now() -> us -> wifiOk -> initWiFi -> initWiFiRetry which forces all 
    // if (!wifiOk())
    //    return (0);

    // create udp endpoint
    WiFiUDP ntp_udp;
    resetWatchdog();
    if (!ntp_udp.begin(1000+random(50000))) {                   // any local port
        Serial.println (F("NTP: UDP startup failed"));
        return (0);
    }

    // decide on server: user's else fastest 
    NTPServer *ntp_use = &ntp_list[0];                          // a place for rsp_time if useLocal
    const char *ntp_server;
    if (useLocalNTPHost()) {
        ntp_server = getLocalNTPHost();
    } else {
        ntp_use = findBestNTP();
        ntp_server = ntp_use->server;
    }

    // NTP buffer and timers
    uint8_t  buf[48];
    uint32_t tx_ms, rx_ms;

    // Assemble request packet
    memset(buf, 0, sizeof(buf));
    memcpy(buf, timeReqA, sizeof(timeReqA));
    memcpy(&buf[12], timeReqB, sizeof(timeReqB));

    // send
    Serial.printf(_FX("NTP: Issuing request to %s\n"), ntp_server);
    resetWatchdog();
    ntp_udp.beginPacket (ntp_server, 123);                      // NTP uses port 123
    ntp_udp.write(buf, sizeof(buf));
    tx_ms = millis();                                           // record when packet sent
    if (!ntp_udp.endPacket()) {
        Serial.println (F("NTP: UDP write failed"));
        ntp_use->rsp_time = NTP_TOO_LONG;                       // force different choice next time
        ntp_udp.stop();
        return (0UL);
    }
    // Serial.print (F("NTP: Sent 48 ... "));
    resetWatchdog();

    // receive response
    // Serial.print(F("NTP: Awaiting response ... "));
    memset(buf, 0, sizeof(buf));
    uint32_t t0 = millis();
    while (!ntp_udp.parsePacket()) {
        if (timesUp (&t0, NTP_TOO_LONG)) {
            Serial.println(F("NTP: UDP timed out"));
            ntp_use->rsp_time = NTP_TOO_LONG;                   // force different choice next time
            ntp_udp.stop();
            return (0UL);
        }
        resetWatchdog();
        wdDelay(10);
    }
    rx_ms = millis();                                           // record when packet arrived
    resetWatchdog();

    // record response time
    ntp_use->rsp_time = rx_ms - tx_ms;
    Serial.printf (_FX("NTP: %s replied after %d ms\n"), ntp_server, ntp_use->rsp_time);

    // read response
    if (ntp_udp.read (buf, sizeof(buf)) != sizeof(buf)) {
        Serial.println (F("NTP: UDP read failed"));
        ntp_use->rsp_time = NTP_TOO_LONG;                       // force different choice next time
        ntp_udp.stop();
        return (0UL);
    }
    Serial.printf (_FX("NTP: received 48 from %s\n"), ntp_udp.remoteIP().toString().c_str());

    // only accept server responses which are mode 4
    uint8_t mode = buf[0] & 0x7;
    if (mode != 4) {                                            // insure server packet
        Serial.printf (_FX("NTP: RX mode must be 4 but it is %d\n"), mode);
        ntp_udp.stop();
        return (0UL);
    }

    // crack and advance to next whole second
    time_t unix_s = crackBE32 (&buf[40]) - 2208988800UL;        // packet transmit time - (1970 - 1900)
    if ((uint32_t)unix_s > 0x7FFFFFFFUL) {                      // sanity check beyond unsigned value
        Serial.printf (_FX("NTP: crazy large UNIX time: %ld\n"), unix_s);
        ntp_udp.stop();
        return (0UL);
    }
    uint32_t fraction_more = crackBE32 (&buf[44]);              // x / 10^32 additional second
    uint16_t ms_more = 1000UL*(fraction_more>>22)/1024UL;       // 10 MSB to ms
    uint16_t transit_time = (rx_ms - tx_ms)/2;                  // transit = half the round-trip time
    ms_more += transit_time;                                    // with transit now = unix_s + ms_more
    uint16_t sec_more = ms_more/1000U+1U;                       // whole seconds behind rounded up
    wdDelay (sec_more*1000U - ms_more);                         // wait to next whole second
    unix_s += sec_more;                                         // account for delay
    // Serial.print (F("NTP: Fraction ")); Serial.print(ms_more);
    // Serial.print (F(", transit ")); Serial.print(transit_time);
    // Serial.print (F(", seconds ")); Serial.print(sec_more);
    // Serial.print (F(", UNIX ")); Serial.print (unix_s); Serial.println();
    resetWatchdog();

    // one more sanity check
    if (unix_s < 1577836800L) {          // Jan 1 2020
        Serial.printf (_FX("NTP: crazy small UNIX time: %ld\n"), unix_s);
        ntp_udp.stop();
        return (0UL);
    }

    ntp_udp.stop();
    *server = ntp_server;
    printFreeHeap (F("NTP"));
    return (unix_s);
}

/* read next char from client.
 * return whether another character was in fact available.
 */
bool getTCPChar (WiFiClient &client, char *cp)
{
    // wait for char, avoid calling millis() if more data are already ready
    if (!client.available()) {
        uint32_t t0 = millis();
        while (!client.available()) {
            if (!client.connected()) {
                // Serial.print (F("getTCPChar disconnect\n"));
                return (false);
            }
            if (timesUp(&t0,10000)) {
                Serial.print (F("getTCPChar timeout\n"));
                return (false);
            }

            // N.B. do not call wdDelay -- it calls checkWebServer() most of whose handlers
            // call back here via getTCPLine()
            delay(2);
            resetWatchdog();
        }
    }

    // read, which offers yet another way to indicate failure
    int c = client.read();
    if (c < 0) {
        Serial.print (F("bad getTCPChar read\n"));
        return (false);
    }

    // got one
    *cp = (char)c;
    return (true);
}

/* send User-Agent to client
 */
void sendUserAgent (WiFiClient &client)
{
    StackMalloc ua_mem(300);
    char *ua = (char *) ua_mem.getMem();
    size_t ual = ua_mem.getSize();

    if (logUsageOk()) {

        // display mode: 0=X11 1=fb0 2=X11full 3=X11+live 4=X11full+live 5=noX
        int dpy_mode = 0;
        #if defined(_USE_FB0)
            dpy_mode = 1;
        #else
            bool fs = getX11FullScreen();
            bool live = last_live + 3600 > time(NULL);
            if (live)
                dpy_mode = fs ? 4 : 3;
            else if (fs)
                dpy_mode = 2;
        #endif
        #if defined(_IS_UNIX)
            #if defined(_WEB_ONLY)
                dpy_mode = 5;
            #endif
        #endif

        // encode stopwatch if on else as per map_proj
        int main_page = 0;
        switch (getSWDisplayState()) {
        default:
        case SWD_NONE:
            // < V2.81: main_page = azm_on ? 1: 0;
            // >= 2.96: add MAPP_MOLL
            // >= 3.05: change MAP_MOLL to MAPP_ROB
            switch ((MapProjection)map_proj) {
            case MAPP_MERCATOR:  main_page = 0; break;
            case MAPP_AZIMUTHAL: main_page = 1; break;
            case MAPP_AZIM1:     main_page = 5; break;
            case MAPP_ROB:       main_page = 6; break;
            default: fatalError(_FX("sendUserAgent() map_proj %d"), map_proj);
            }
            break;
        case SWD_MAIN:
            main_page = 2;
            break;
        case SWD_BCDIGITAL:
            main_page = 4;
            break;
        case SWD_BCANALOG:
            main_page = 3;
            break;
        }

        // alarm clock
        AlarmState as;
        uint16_t hr, mn;
        getAlarmState (as, hr, mn);

        // encode plot options
        // prior to V2.67: value was either plot_ch or 99
        // since V2.67:    value is 100 + plot_ch
        int plotops[PANE_N];
        for (int i = 0; i < PANE_N; i++)
            plotops[i] = paneIsRotating((PlotPane)i) ? 100+(int)plot_ch[i] : (int)plot_ch[i];

        // prefix map style with N if not showing night
        char map_style[NV_COREMAPSTYLE_LEN+1];
        (void) getMapStyle(map_style);
        if (!night_on) {
            memmove (map_style+1, map_style, sizeof(map_style)-1);
            map_style[0] = 'N';
        }

        // kx3 baud else gpio on/off
        int gpio = getKX3Baud();
        if (gpio == 0) {
            if (GPIOOk())
                gpio = 1;
            else if (found_mcp)
                gpio = 2;
        }

        // which phot, if any
        int io = 0;
        if (found_phot) io |= 1;
        if (found_ltr) io |= 2;
        if (found_mcp) io |= 4;
        if (getI2CFilename()) io |= 8;

        // combine rss_on and rss_local
        int rss_code = rss_on + 2*rss_local;

        // gimbal and rig bit mask: 4 = rig, 2 = azel  1 = az only
        bool gconn, vis_now, has_el, gstop, gauto;
        float az, el;
        bool gbl_on = getGimbalState (gconn, vis_now, has_el, gstop, gauto, az, el);
        bool rig_on = getRigctld (NULL, NULL);
        bool flrig = getFlrig (NULL, NULL);
        int rr_score = (gbl_on ? (has_el ? 2 : 1) : 0) | (rig_on ? 4 : 0) | (flrig ? 8 : 0);

        // brb_mode plus 100 to indicate rotation code
        int brb = brb_mode;
        if (BRBIsRotating())
            brb += 100;

        // GPSD
        int gpsd = 0;
        if (useGPSDTime())
            gpsd |= 1;
        if (useGPSDLoc())
            gpsd |= 2;

        // date formatting
        int dayf = (int)getDateFormat();
        if (weekStartsOnMonday())                       // added in 2.86
            dayf |= 4;

        // number of dashed colors                      // added to first LV6 in 2.90
        int n_dashed = 0;
        for (int i = 0; i < N_CSPR; i++)
            if (getColorDashed((ColorSelection)i))
                n_dashed++;

        // path size: 0 none, 1 thin, 2 wide
        int path = getSpotPathSize();                   // returns 0, THINPATHSZ or WIDEPATHSZ
        if (path)
            path = (path == THINPATHSZ ? 1 : 2);

        // label spots: 0 no, 1 prefix, 2 call, 3 dot
        int spots = labelSpots();
        if (spots)
            spots = (plotSpotCallsigns() ? 2 : 1);
        else
            spots = dotSpots() ? 3 : 0;

        // crc code
        int crc = flash_crc_ok;
        if (want_kbcursor)
            crc |= (1<<15);                             // max old _debug_ was 1<<14

        // callsign colors
        uint16_t call_fg, call_bg;
        uint8_t call_rb;
        NVReadUInt16 (NV_CALL_FG_COLOR, &call_fg);
        NVReadUInt8 (NV_CALL_BG_RAINBOW, &call_rb);
        if (call_rb)
            call_bg = 1;                                // unlikely color to mean rainbow
        else
            NVReadUInt16 (NV_CALL_BG_COLOR, &call_bg);

        // ntp source
        int ntp = 0;
        if (useLocalNTPHost())
            ntp = 1;
        else if (useOSTime())
            ntp = 2;                                    // added in 3.05

        snprintf (ua, ual,
            _FX("User-Agent: %s/%s (id %u up %lld) crc %d LV6 %s %d %d %d %d %d %d %d %d %d %d %d %d %d %.2f %.2f %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %u %u %d\r\n"),
            platform, hc_version, ESP.getChipId(), (long long)getUptime(NULL,NULL,NULL,NULL), crc,
            map_style, main_page, mapgrid_choice, plotops[PANE_1], plotops[PANE_2], plotops[PANE_3],
            de_time_fmt, brb, dx_info_for_sat, rss_code, useMetricUnits(),
            getNBMEConnected(), gpio, io, getBMETempCorr(BME_76), getBMEPresCorr(BME_76),
            desrss, dxsrss, BUILD_W, dpy_mode,
            // new for LV5:
            (int)as, getCenterLng(), (int)auxtime /* getDoy() before 2.80 */, names_on, getDemoMode(),
            (int)getSWEngineState(NULL,NULL), (int)getBigClockBits(), utcOffset(), gpsd,
            rss_interval, dayf, rr_score,
            // new for LV6:
            useMagBearing(), n_dashed, ntp,
            path, spots,
            call_fg, call_bg, !clockTimeOk());  // default clock 0 == ok
    } else {
        snprintf (ua, ual, _FX("User-Agent: %s/%s (id %u up %lld) crc %d\r\n"),
            platform, hc_version, ESP.getChipId(), (long long)getUptime(NULL,NULL,NULL,NULL), flash_crc_ok);
    }

    // send
    client.print(ua);

}

/* issue an HTTP Get for an arbitary page
 */
void httpGET (WiFiClient &client, const char *server, const char *page)
{
    resetWatchdog();

    FWIFIPR (client, F("GET ")); client.print(page); FWIFIPRLN (client, F(" HTTP/1.0"));
    FWIFIPR (client, F("Host: ")); client.println (server);
    sendUserAgent (client);
    FWIFIPRLN (client, F("Connection: close\r\n"));

    resetWatchdog();
}

/* issue an HTTP Get to a /ham/HamClock page named in ram
 */
void httpHCGET (WiFiClient &client, const char *server, const char *hc_page)
{
    static const char hc[] PROGMEM = "/ham/HamClock";
    StackMalloc full_mem(strlen(hc_page) + sizeof(hc));         // sizeof includes the EOS
    char *full_hc_page = (char *) full_mem.getMem();
    snprintf (full_hc_page, full_mem.getSize(), "%s%s", _FX_helper(hc), hc_page);
    httpGET (client, server, full_hc_page);
}

/* issue an HTTP Get to a /ham/HamClock page named in PROGMEM
 */
void httpHCPGET (WiFiClient &client, const char *server, const char *hc_page_progmem)
{
    httpHCGET (client, server, _FX_helper(hc_page_progmem));
}

/* skip the given wifi client stream ahead to just after the first blank line, return whether ok.
 * this is often used so subsequent stop() on client doesn't slam door in client's face with RST.
 * Along the way, if find a header field with the given name (unless NULL) return value in the given string.
 * if header is not found, we still return true but value[0] will be '\0'.
 */
bool httpSkipHeader (WiFiClient &client, const char *header, char *value, int value_len)
{
    char line[200];

    // prep
    int hdr_len = header ? strlen(header) : 0;
    if (value)
        value[0] = '\0';
    char *hdr;

    // read until find a blank line
    do {
        if (!getTCPLine (client, line, sizeof(line), NULL))
            return (false);
        // Serial.println (line);

        if (header && value && (hdr = strstr (line, header)) != NULL)
            snprintf (value, value_len, "%s", hdr + hdr_len);

    } while (line[0] != '\0');  // getTCPLine absorbs \r\n so this tests for a blank line

    return (true);
}

/* same but when we don't care about any header field;
 * so we pick up Remote_Addr for postDiags()
 */
bool httpSkipHeader (WiFiClient &client)
{
    return (httpSkipHeader (client, _FX("Remote_Addr: "), remote_addr, sizeof(remote_addr)));
}

/* retrieve and plot latest and predicted DRAP indices, return whether all ok
 */
static bool updateDRAP (SBox &box)
{
    StackMalloc x_mem(DRAPDATA_NPTS*sizeof(float));
    StackMalloc y_mem(DRAPDATA_NPTS*sizeof(float));
    float *x = (float*)x_mem.getMem();
    float *y = (float*)y_mem.getMem();

    bool ok = retrieveDRAP (x, y);
    if (ok) {
        updateClocks(false);
        resetWatchdog();

        // plot
        plotXY (box, x, y, DRAPDATA_NPTS, _FX("Hours"), _FX("DRAP, max MHz"), DRAPPLOT_COLOR,
                                                            0, 0, y[DRAPDATA_NPTS-1]);

        // update NCDXF box if up for this
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawSpaceStats(RA8875_BLACK);

    } else {
        plotMessage (box, DRAPPLOT_COLOR, _FX("DRAP connection failed"));
    }

    // done
    resetWatchdog();
    return (ok);
}

/* retrieve and plot latest and predicted kp indices, return whether all ok
 */
static bool updateKp(SBox &box)
{
    // data are provided every 3 hours == 8/day. collect 7 days of history + 2 days of predictions
    StackMalloc kpx_mem(KP_NV*sizeof(float));
    StackMalloc kp_mem(KP_NV*sizeof(float));
    float *kpx = (float*)kpx_mem.getMem();              // days ago
    float *kp = (float*)kp_mem.getMem();                // kp collection

    bool ok = retrieveKp (kpx, kp);
    if (ok) {
        updateClocks(false);
        resetWatchdog();

        // plot
        plotXY (box, kpx, kp, KP_NV, _FX("Days"), _FX("Planetary Kp"), KP_COLOR, 0, 9, kp_spw);

        // update NCDXF box if up for this
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawSpaceStats(RA8875_BLACK);

    } else {
        plotMessage (box, KP_COLOR, _FX("Kp connection failed"));
    }

    // done
    resetWatchdog();
    return (ok);
}

/* retrieve and plot latest xray indices, return whether all ok
 */
static bool updateXRay(const SBox &box)
{
    StackMalloc lxray_mem(XRAY_NV*sizeof(float));
    StackMalloc sxray_mem(XRAY_NV*sizeof(float));
    StackMalloc x_mem(XRAY_NV*sizeof(float));
    float *lxray = (float *) lxray_mem.getMem();        // long wavelength values
    float *sxray = (float *) sxray_mem.getMem();        // short wavelength values
    float *x = (float *) x_mem.getMem();                // x coords of plot

    bool ok = retrieveXRay (lxray, sxray, x);
    if (ok) {
        updateClocks(false);
        resetWatchdog();

        // overlay short over long with fixed y axis
        char level_str[10];
        plotXYstr (box, x, lxray, XRAY_NV, _FX("Hours"), _FX("GOES 16 X-Ray"), XRAY_LCOLOR,
                                -9, -2, NULL)
                 && plotXYstr (box, x, sxray, XRAY_NV, NULL, NULL, XRAY_SCOLOR, -9, -2,     
                                xrayLevel(xray_spw, level_str));

        // update NCDXF box if up for this
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawSpaceStats(RA8875_BLACK);

    } else {
        plotMessage (box, XRAY_LCOLOR, _FX("X-Ray connection failed"));
    }

    // done
    resetWatchdog();
    return (ok);
}

/* retrieve and plot fresh sun spot indices
 */
static bool updateSunSpots (const SBox &box)
{
    StackMalloc x_ssn((SSPOT_NV)*sizeof(float));
    StackMalloc x_x((SSPOT_NV)*sizeof(float));
    float *ssn = (float*)x_ssn.getMem();
    float *x = (float*)x_x.getMem();

    // get and plot if ok, else show err message
    resetWatchdog();
    bool ok = retrieveSunSpots (x, ssn);
    if (ok) {
        updateClocks(false);
        resetWatchdog();

        // plot, showing value as traditional whole number
        char label[20];
        snprintf (label, sizeof(label), "%.0f", ssn[SSPOT_NV-1]);
        plotXYstr (box, x, ssn, SSPOT_NV, _FX("Days"), _FX("Sunspot Number"), SSPOT_COLOR, 0,0, label);

        // update NCDXF box if up for this
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawSpaceStats(RA8875_BLACK);

    } else {
        plotMessage (box, SSPOT_COLOR, _FX("SSN connection failed"));
    }

    // done
    resetWatchdog();
    return (ok);
}

/* retrieve and plot latest and predicted solar flux indices, return whether all ok.
 */
static bool updateSolarFlux(const SBox &box)
{
    StackMalloc x_mem(SFLUX_NV*sizeof(float));
    StackMalloc sflux_mem(SFLUX_NV*sizeof(float));
    float *x = (float *) x_mem.getMem();
    float *sflux = (float *) sflux_mem.getMem();

    // get flux and plot if ok
    bool ok = retrievSolarFlux(x, sflux);
    if (ok) {
        updateClocks(false);
        resetWatchdog();

        // plot
        plotXY (box, x, sflux, SFLUX_NV, _FX("Days"), _FX("10.7 cm Solar flux"),
                                                SFLUX_COLOR, 0, 0, sflux[SFLUX_NV-10]);

        // update NCDXF box if up for this
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawSpaceStats(RA8875_BLACK);

    } else {
        plotMessage (box, SFLUX_COLOR, _FX("Flux connection failed"));
    }

    // done
    resetWatchdog();
    return (ok);
}

/* retrieve and plot latest and predicted solar wind indices, return whether all ok.
 */
static bool updateSolarWind(const SBox &box)
{
    // max lines to collect, oldest first
    #define     SOLWINDDT       (10*60)         // interval, secs
    #define     SOLWINDP        (24*3600)       // period, secs
    #define     NSOLWIND        (SOLWINDP/SOLWINDDT)
    StackMalloc x_mem(NSOLWIND*sizeof(float));  // hours ago 
    StackMalloc y_mem(NSOLWIND*sizeof(float));  // wind
    float *x = (float *) x_mem.getMem();
    float *y = (float *) y_mem.getMem();
    WiFiClient swind_client;
    char line[80];
    bool ok = false;

    // mark value as bad until proven otherwise
    swind_spw = SPW_ERR;

    Serial.println (swind_page);
    resetWatchdog();
    if (wifiOk() && swind_client.connect(backend_host, backend_port)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCPGET (swind_client, backend_host, swind_page);

        // skip response header
        if (!httpSkipHeader (swind_client)) {
            plotMessage (box, SWIND_COLOR, _FX("Wind header short"));
            goto out;
        }

        // read lines into wind array and build corresponding x/y values
        time_t t0 = myNow();
        time_t start_t = t0 - SOLWINDP;
        time_t prev_unixs = 0;
        float max_y = 0;
        int nsw;
        for (nsw = 0; nsw < NSOLWIND && getTCPLine (swind_client, line, sizeof(line), NULL); ) {
            // Serial.printf (_FX("Swind %3d: %s\n"), nsw, line);
            long unixs;         // unix seconds
            float density;      // /cm^2
            float speed;        // km/s
            if (sscanf (line, "%ld %f %f", &unixs, &density, &speed) != 3) {
                plotMessage (box, SWIND_COLOR, _FX("Wind data garbled"));
                goto out;
            }

            // want y axis to be 10^12 /s /m^2
            float this_y = density * speed * 1e-3;

            // capture largest value in this period for getSpaceWeather()
            if (this_y > max_y)
                max_y = this_y;

            // skip until find within period and new interval or always included last
            if ((unixs < start_t || unixs - prev_unixs < SOLWINDDT) && nsw != NSOLWIND-1)
                continue;
            prev_unixs = unixs;

            // want x axis to be hours back from now
            x[nsw] = (t0 - unixs)/(-3600.0F);
            y[nsw] = max_y;
            // Serial.printf (_FX("Swind %3d %5.2f %5.2f\n"), nsw, x[nsw], y[nsw]);

            // good one
            max_y = 0;
            nsw++;
        }

        // plot if found at least a few
        updateClocks(false);
        resetWatchdog();
        if (nsw >= 10
                && plotXY (box, x, y, nsw, _FX("Hours"), _FX("Solar wind"), SWIND_COLOR, 0, 0, y[nsw-1])) {

            // capture for getSpaceWeather()
            swind_spw = y[nsw-1];
            swind_update = t0;
            ok = true;

        } else {
            plotMessage (box, SWIND_COLOR, _FX("Sol Wind data error"));
        }

    } else {
        plotMessage (box, SWIND_COLOR, _FX("Sol Wind connection failed"));
    }

    // clean up
out:
    swind_client.stop();
    resetWatchdog();
    return (ok);
}

/* retrieve and plot latest BZBT indices, return whether all ok
 */
static bool updateBzBt (const SBox &box)
{
    StackMalloc hrsold_mem(BZBT_NV*sizeof(float));
    StackMalloc bz_mem(BZBT_NV*sizeof(float));
    StackMalloc bt_mem(BZBT_NV*sizeof(float));
    float *hrsold = (float *) hrsold_mem.getMem();      // hours old
    float *bz = (float *) bz_mem.getMem();              // Bz
    float *bt = (float *) bt_mem.getMem();              // Bt

    bool ok = retrieveBzBt (hrsold, bz, bt);
    if (ok) {
        updateClocks(false);
        resetWatchdog();

        // find first within 25 hours thence min/max over both
        float min_bzbt = 1e10, max_bzbt = -1e10;
        int f25 = -1;
        for (int i = 0; i < BZBT_NV; i++) {
            if (f25 < 0 && hrsold[i] >= -25)
                f25 = i;
            if (f25 >= 0) {
                if (bz[i] < min_bzbt) min_bzbt = bz[i];
                else if (bz[i] > max_bzbt) max_bzbt = bz[i];
                if (bt[i] < min_bzbt) min_bzbt = bt[i];
                else if (bt[i] > max_bzbt) max_bzbt = bt[i];
            }
        }

        // plot
        char bz_label[30];
        snprintf (bz_label, sizeof(bz_label), "%.1f", bz[BZBT_NV-1]);         // newest Bz
        plotXYstr (box, hrsold+f25, bz+f25, BZBT_NV-f25, _FX("Hours"), _FX("Solar Bz and Bt, nT"),
                                    BZBT_BZCOLOR, min_bzbt, max_bzbt, NULL)
                 && plotXYstr (box, hrsold+f25, bt+f25, BZBT_NV-f25, NULL, NULL,
                                    BZBT_BTCOLOR, min_bzbt, max_bzbt, bz_label);
        // update NCDXF box if up for this
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawSpaceStats(RA8875_BLACK);

    } else {

        plotMessage (box, BZBT_BZCOLOR, _FX("BzBt update error"));
    }

    // done
    resetWatchdog();
    return (ok);
}

/* retrieve and draw latest band conditions in the given box, return whether all ok.
 */
static bool updateBandConditions(const SBox &box)
{
    StackMalloc response_mem(100);
    StackMalloc config_mem(100);
    char *response = (char *) response_mem.getMem();
    char *config = (char *) config_mem.getMem();
    WiFiClient bc_client;
    bool ok = false;


    // build query
    const size_t qsize = sizeof(bc_page)+200;
    StackMalloc query_mem (qsize);
    char *query = (char *) query_mem.getMem();
    time_t t = nowWO();
    snprintf (query, qsize,
        _FX("%s?YEAR=%d&MONTH=%d&RXLAT=%.3f&RXLNG=%.3f&TXLAT=%.3f&TXLNG=%.3f&UTC=%d&PATH=%d&POW=%d&MODE=%d&TOA=%.1f"),
        bc_page, year(t), month(t), dx_ll.lat_d, dx_ll.lng_d, de_ll.lat_d, de_ll.lng_d,
        hour(t), show_lp, bc_power, bc_modevalue, bc_toa);

    Serial.println (query);
    resetWatchdog();
    if (wifiOk() && bc_client.connect(backend_host, backend_port)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCGET (bc_client, backend_host, query);

        // skip header
        if (!httpSkipHeader (bc_client)) {
            plotMessage (box, RA8875_RED, _FX("BC: no header"));
            goto out;
        }

        // next line is CSV path reliability for the requested time between DX and DE, 9 bands 80-10m
        if (!getTCPLine (bc_client, response, response_mem.getSize(), NULL)) {
            plotMessage (box, RA8875_RED, _FX("BC: No response"));
            goto out;
        }

        // next line is configuration summary
        if (!getTCPLine (bc_client, config, config_mem.getSize(), NULL)) {
            Serial.println(response);
            plotMessage (box, RA8875_RED, _FX("BC: No config"));
            goto out;
        }

        // Serial.printf (_FX("BC response: %s\n"), response);
        // Serial.printf (_FX("BC config: %s\n"), config);

        // keep time fresh
        updateClocks(false);
        resetWatchdog();

        // get current utc hour for path_spw
        int t_hr = hour(t);

        // next 24 lines are reliability matrix.
        // N.B. col 1 is UTC but runs from 1 .. 24, 24 is really 0
        // lines include data for 9 bands, 80-10, but we drop 60 for BandCdtnMatrix
        float rel[BMTRX_COLS];          // values are path reliability 0 .. 1
        memset (&bc_matrix, 0, sizeof(bc_matrix));
        for (int r = 0; r < BMTRX_ROWS; r++) {

            // read next row
            if (!getTCPLine (bc_client, response, response_mem.getSize(), NULL)) {
                Serial.printf (_FX("BC: fail row %d"), r);
                plotMessage (box, RA8875_RED, _FX("No matrix"));
                goto out;
            }

            // crack next row, skipping 60 m
            int utc_hr;
            if (sscanf(response, _FX("%d %f,%*f,%f,%f,%f,%f,%f,%f,%f"), &utc_hr,
                        &rel[0], &rel[1], &rel[2], &rel[3], &rel[4], &rel[5], &rel[6], &rel[7])
                            != BMTRX_COLS + 1) {
                Serial.printf (_FX("BC: bad matrix line: %s\n"), response);
                plotMessage (box, RA8875_RED, _FX("Bad matrix"));
                goto out;
            }

            // correct utc
            utc_hr %= 24;

            // add to bc_matrix as integer percent
            for (int c = 0; c < BMTRX_COLS; c++)
                bc_matrix[utc_hr][c] = (uint8_t)(100*rel[c]);

            // copy to path_spw for getSpaceWeather() if correct time
            if (utc_hr == t_hr) {
                memcpy (path_spw, rel, BMTRX_COLS*sizeof(float));
                path_update = myNow();
            }
        }

        // #define _TEST_BAND_MATRIX
        #if defined(_TEST_BAND_MATRIX)
            for (int r = 0; r < BMTRX_ROWS; r++)                    // time 0 .. 23
                for (int c = 0; c < BMTRX_COLS; c++)                // band 80 .. 10
                    bc_matrix[r][c] = 100*r*c/BMTRX_ROWS/BMTRX_COLS;
                    // (*mp)[r][c] = (float)r/BMTRX_ROWS;
        #endif

        // ok!
        plotBandConditions (box, 0, &bc_matrix, config);
        ok = true;

    } else {
        plotMessage (box, RA8875_RED, _FX("VOACAP connection failed"));
    }

    // clean up
out:
    bc_client.stop();
    resetWatchdog();
    return (ok);
}

/* display the RSG NOAA solar environment scale values.
 */
static bool updateNOAASWx(const SBox &box)
{
    // expecting 3 reply lines of the form, anything else is an error message
    //  R  0 0 0 0
    //  S  0 0 0 0
    //  G  0 0 0 0
    char line[100];
    bool ok = false;

    // TCP client
    WiFiClient noaaswx_client;

    // starting msg
        
    // read scales
    Serial.println(noaaswx_page);
    resetWatchdog();
    if (wifiOk() && noaaswx_client.connect(backend_host, backend_port)) {

        resetWatchdog();
        updateClocks(false);

        // fetch page
        httpHCPGET (noaaswx_client, backend_host, noaaswx_page);

        // skip header then read the data lines
        if (httpSkipHeader (noaaswx_client)) {

            for (int i = 0; i < N_NOAASW_C; i++) {

                // read next line
                if (!getTCPLine (noaaswx_client, line, sizeof(line), NULL)) {
                    plotMessage (box, RA8875_RED, _FX("NOAASW missing data"));
                    goto out;
                }
                // Serial.printf (_FX("NOAA: %d %s\n"), i, line);

                // parse
                // sprintf (line, _FX("%c 1 2 3 4"), 'A'+i);  // test line
                if (sscanf (line, _FX("%c %d %d %d %d"), &noaa_spw.cat[i], &noaa_spw.val[i][0],
                                &noaa_spw.val[i][1], &noaa_spw.val[i][2], &noaa_spw.val[i][3]) != 5) {
                    plotMessage (box, RA8875_RED, line);
                    goto out;
                }
            }

            // all ok: mark for getSpaceWeather() and display
            noaa_update = myNow();
            plotNOAASWx (box, noaa_spw);
            ok = true;

        } else {
            plotMessage (box, RA8875_RED, _FX("NOAASW header short"));
            goto out;
        }
    } else
        plotMessage (box, RA8875_RED, _FX("NOAASW connection failed"));

out:

    // finished with connection
    noaaswx_client.stop();

    return (ok);
}

/* display next RSS feed item if on, return whether ok
 */
static bool updateRSS ()
{
    // skip if not on and clear list if not local
    if (!rss_on) {
        if (!rss_local) {
            while (n_rss_titles > 0) {
                free (rss_titles[--n_rss_titles]);
                rss_titles[n_rss_titles] = NULL;
            }
        }
        return (true);
    }

    // reserve mem
    StackMalloc line_mem(150);
    char *line = (char *) line_mem.getMem();

    // prepare background to show life before possibly lengthy net update
    fillSBox (rss_bnr_b, RSS_BG_COLOR);
    tft.drawLine (rss_bnr_b.x, rss_bnr_b.y, rss_bnr_b.x+rss_bnr_b.w, rss_bnr_b.y, GRAY);

    // fill rss_titles[] from network if empty and wanted
    if (!rss_local && rss_title_i >= n_rss_titles) {

        // reset count and index
        n_rss_titles = rss_title_i = 0;

        // TCP client
        WiFiClient rss_client;
        
        Serial.println(rss_page);
        resetWatchdog();
        if (wifiOk() && rss_client.connect(backend_host, backend_port)) {

            resetWatchdog();
            updateClocks(false);

            // fetch feed page
            httpHCPGET (rss_client, backend_host, rss_page);

            // skip response header
            if (!httpSkipHeader (rss_client)) {
                Serial.println (F("RSS header short"));
                goto out;
            }

            // get up to RSS_MAXN more rss_titles[]
            for (n_rss_titles = 0; n_rss_titles < RSS_MAXN; n_rss_titles++) {
                if (!getTCPLine (rss_client, line, line_mem.getSize(), NULL))
                    goto out;
                if (rss_titles[n_rss_titles])
                    free (rss_titles[n_rss_titles]);
                rss_titles[n_rss_titles] = strdup (line);
                // Serial.printf (_FX("RSS[%d] len= %d\n"), n_rss_titles, strlen(rss_titles[n_rss_titles]));
            }
        }

      out:
        rss_client.stop();

        // real trouble if still no rss_titles
        if (n_rss_titles == 0) {
            // report error 
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            tft.setTextColor (RSS_FG_COLOR);
            tft.setCursor (rss_bnr_b.x + rss_bnr_b.w/2-100, rss_bnr_b.y + 2*rss_bnr_b.h/3-1);
            tft.print (F("RSS network error"));
            Serial.println (F("RSS failed"));
            return (false);
        }
        printFreeHeap (F("updateRSS"));
    }

    // done if no titles
    if (n_rss_titles == 0)
        return (true);

    resetWatchdog();

    // draw next rss_title
    char *title = rss_titles[rss_title_i];

    // usable banner drawing x and width
    uint16_t ubx = rss_bnr_b.x + 5;
    uint16_t ubw = rss_bnr_b.w - 10;

    // get title width in pixels
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t tw = getTextWidth (title);

    // draw as 1 or 2 lines to fit within ubw
    tft.setTextColor (RSS_FG_COLOR);
    if (tw < ubw) {
        // title fits on one row, draw centered horizontally and vertically
        tft.setCursor (ubx + (ubw-tw)/2, rss_bnr_b.y + 2*rss_bnr_b.h/3-1);
        tft.print (title);
    } else {
        // title too long, keep shrinking until it fits
        for (bool fits = false; !fits; ) {

            // split at center blank
            size_t tl = strlen(title);
            char *row2 = strchr (title+tl/2, ' ');
            if (!row2)
                row2 = title+tl/2;          // no blanks! just split in half?
            char sep_char = *row2;          // save to restore
            *row2++ = '\0';                 // replace blank with EOS and move to start of row 2 -- restore!
            uint16_t r1w = getTextWidth (title);
            uint16_t r2w = getTextWidth (row2);

            // draw if fits
            if (r1w <= ubw && r2w <= ubw) {
                tft.setCursor (ubx + (ubw-r1w)/2, rss_bnr_b.y + rss_bnr_b.h/2 - 8);
                tft.print (title);
                tft.setCursor (ubx + (ubw-r2w)/2, rss_bnr_b.y + rss_bnr_b.h - 9);
                tft.print (row2);

                // got it
                fits = true;
            }

            // restore zerod char
            row2[-1] = sep_char;

            if (!fits) {
                Serial.printf (_FX("RSS shrink from %d %d "), tw, tl);
                tw = maxStringW (title, 9*tw/10);       // modifies title
                tl = strlen(title);
                Serial.printf (_FX("to %d %d\n"), tw, tl);
            }
        }
    }

    // if local just cycle to next title, else remove from list and advance
    if (rss_local) {
        rss_title_i = (rss_title_i + 1) % n_rss_titles;
    } else {
        free (rss_titles[rss_title_i]);
        rss_titles[rss_title_i++] = NULL;
    }
 
    resetWatchdog();
    return (true);
}

/* get next line from client in line[] then return true, else nothing and return false.
 * line[] will have \r and \n removed and end with \0, optional line length in *ll will not include \0.
 * if line is longer than line_len it will be silently truncated.
 */
bool getTCPLine (WiFiClient &client, char line[], uint16_t line_len, uint16_t *ll)
{
    // update network stack
    yield();

    // decrement available length so there's always room to add '\0'
    line_len -= 1;

    // read until find \n or time out.
    uint16_t i = 0;
    while (true) {
        char c;
        if (!getTCPChar (client, &c))
            return (false);
        if (c == '\r')
            continue;
        if (c == '\n') {
            line[i] = '\0';
            if (ll)
                *ll = i;
            // Serial.println(line);
            return (true);
        } else if (i < line_len)
            line[i++] = c;
    }
}

/* convert an array of 4 big-endian network-order bytes into a uint32_t
 */
static uint32_t crackBE32 (uint8_t bp[])
{
    union {
        uint32_t be;
        uint8_t ba[4];
    } be4;

    be4.ba[3] = bp[0];
    be4.ba[2] = bp[1];
    be4.ba[1] = bp[2];
    be4.ba[0] = bp[3];

    return (be4.be);
}

/* called when RSS has just been turned on: update now and restart refresh cycle
 */
void scheduleRSSNow()
{
    next_rss = 0;
}

/* it is MUCH faster to print F() strings in a String than using them directly.
 * see esp8266/2.3.0/cores/esp8266/Print.cpp::print(const __FlashStringHelper *ifsh) to see why.
 */
void FWIFIPR (WiFiClient &client, const __FlashStringHelper *str)
{
    String _sp(str);
    client.print(_sp);
}

void FWIFIPRLN (WiFiClient &client, const __FlashStringHelper *str)
{
    String _sp(str);
    client.println(_sp);
}

// handy wifi health check
bool wifiOk()
{
    if (WiFi.status() == WL_CONNECTED)
        return (true);

    // retry occasionally
    static uint32_t last_wifi;
    if (timesUp (&last_wifi, WIFI_RETRY*1000)) {
        initWiFi(false);
        return (WiFi.status() == WL_CONNECTED);
    } else
        return (false);
}

/* arrange for everything to update immediately
 */
void initWiFiRetry()
{
    memset (next_update, 0, sizeof(next_update));
    next_wwx = 0;
    next_rss = 0;
}

/* called to schedule an update to the moon pane if anywhere in rotation.
 * if moon is on PANE_1 defer for a revert in progress otherwise schedule immediately.
 */
void scheduleNewMoon()
{
    PlotPane moon_pp = findPaneForChoice (PLOT_CH_MOON);
    if (moon_pp != PANE_NONE && (moon_pp != PANE_1 || !pane1_reverting))
        next_update[moon_pp] = 0;
}

/* handy way to schedule a fresh pane update for simple cases
 */
static void scheduleNewPane (PlotChoice ch)
{
    PlotPane pp = findPaneForChoice (ch);
    if (pp != PANE_NONE)
        next_update[pp] = 0;
}

/* called to schedule an update to the live spots pane if anywhere in rotation
 */
void scheduleNewPSK()
{
    scheduleNewPane (PLOT_CH_PSK);
}

/* called to schedule an update to the DX Cluster pane if anywhere in rotation
 */
void scheduleNewDXC()
{
    scheduleNewPane (PLOT_CH_DXCLUSTER);
}

/* called to schedule an update to the POTA pane if anywhere in rotation
 */
void scheduleNewPOTA()
{
    scheduleNewPane (PLOT_CH_POTA);
}

/* called to schedule an update to the SOTA pane if anywhere in rotation
 */
void scheduleNewSOTA()
{
    scheduleNewPane (PLOT_CH_SOTA);
}

/* called to schedule an update to the ADIF pane if anywhere in rotation
 */
void scheduleNewADIF()
{
    scheduleNewPane (PLOT_CH_ADIF);
}

/* called to schedule an update to the SDO pane if anywhere in rotation.
 * if SDO is on PANE_1 defer for a revert in progress otherwise schedule immediately.
 */
void scheduleNewSDO()
{
    PlotPane sdo_pp = findPaneForChoice (PLOT_CH_SDO);
    if (sdo_pp != PANE_NONE && (sdo_pp != PANE_1 || !pane1_reverting))
        next_update[sdo_pp] = 0;
}

/* called to schedule an update to the band conditions pane if up now.
 * if BC is on PANE_1 defer for a revert in progress otherwise schedule immediately.
 */
void scheduleNewBC()
{
    PlotPane bc_pp = findPaneChoiceNow (PLOT_CH_BC);
    if (bc_pp != PANE_NONE && (bc_pp != PANE_1 || !pane1_reverting))
        next_update[bc_pp] = 0;
}

/* called to schedule an immediate update of the given VOACAP map.
 * leave core_map unchanged to use later if VOACAP turned off.
 */
void scheduleNewVOACAPMap(PropMapSetting &pm)
{
    bool active_changed = prop_map.active != pm.active;
    prop_map = pm;
    if (prop_map.active || active_changed)
        next_map = 0;
}

/* called to schedule an immediate update of the give core map, unless being turned off
 * turns off any VOACAP map.
 */
void scheduleNewCoreMap(CoreMaps cm)
{
    prop_map.active = false;
    core_map = cm;
    if (cm != CM_NONE)
        next_map = 0;
}

/* display the current DE weather.
 * if already assigned to any pane just update now, else show in PANE_1 then arrange to linger
 */
void showDEWX()
{
    PlotPane dewx_pp = findPaneChoiceNow (PLOT_CH_DEWX);
    if (dewx_pp != PANE_NONE)
        next_update[dewx_pp] = 0;
    else {
        // revert whether worked or not
        (void) updateDEWX (plot_b[PANE_1]);
        revertPane1 (DXPATH_LINGER);
    }
}

/* display the current DX weather.
 * if already assigned to any pane just update now, else show in PANE_1 then arrange to linger
 */
void showDXWX()
{
    PlotPane dxwx_pp = findPaneChoiceNow (PLOT_CH_DXWX);
    if (dxwx_pp != PANE_NONE)
        next_update[dxwx_pp] = 0;
    else {
        // revert whether worked or not
        (void) updateDXWX (plot_b[PANE_1]);
        revertPane1 (DXPATH_LINGER);
    }
}

/* return most recent space weather info and its age. values never read will be ancient.
 * most are value+age but xray is a string and pathrel is an array of BMTRX_COLS.
 * N.B. values will be SPW_ERR if unknown.
 */
void getSpaceWeather (SPWxValue &ssn, SPWxValue &sflux, SPWxValue &kp, SPWxValue &swind, SPWxValue &drap,
SPWxValue &bz, SPWxValue &bt, NOAASpaceWx &noaaspw, time_t &noaaspw_age, char xray[], time_t &xray_age,
float pathrel[BMTRX_COLS], time_t &pathrel_age)
{
    // time now for ages
    time_t t0 = myNow();

    // update
    checkSolarFlux();
    checkKp();
    checkXRay();
    checkBzBt();
    checkDRAP();

    // these are easy scalars
    ssn.value = ssn_spw;
    ssn.age = t0 - ssn_update;
    sflux.value = sflux_spw;
    sflux.age = t0 - sflux_update;
    kp.value = kp_spw;
    kp.age = t0 - kp_update;
    swind.value = swind_spw;
    swind.age = t0 - swind_update;
    drap.value = drap_spw;
    drap.age = t0 - drap_update;
    bz.value = bz_spw;
    bz.age = t0 - bzbt_update;
    bt.value = bt_spw;
    bt.age = t0 - bzbt_update;

    // easy struct but beware never set yet
    if (!noaa_spw.cat[0]) {
        noaa_spw.cat[0] = 'R';
        noaa_spw.cat[1] = 'S';
        noaa_spw.cat[2] = 'G';
    }
    noaaspw = noaa_spw;
    noaaspw_age = t0 - noaa_update;

    // xray is a string
    (void) xrayLevel (xray_spw, xray);  // handles 0 ok
    xray_age = t0 - xray_update;

    // VOACAP path reliability is an array
    for (int i = 0; i < BMTRX_COLS; i++)
        pathrel[i] = path_spw[i];
    pathrel_age = t0 - path_update;
}

/* return current NTP response time list.
 * N.B. this is the real data, caller must not modify.
 */
int getNTPServers (const NTPServer **listp)
{
    *listp = ntp_list;
    return (N_NTP);
}

/* get the current BandCdtnMatrix conditions.
 * return whether values are less than an hour old.
 */
bool getBCMatrix (BandCdtnMatrix &bm)
{
    memcpy (&bm, &bc_matrix, sizeof(bm));
    return (tdiff(bc_time,nowWO()) < 3600);
}

/* used by web server to control local RSS title list.
 * if title == NULL
 *   restore normal network operation
 * else if title[0] == '\0'
 *   turn off network and empty the local title list
 * else
 *   turn off network and add the given title to the local list
 * always report the current number of titles in the list and max number possible.
 * return whether ok
 */
bool setRSSTitle (const char *title, int &n_titles, int &max_titles)

{
    if (!title) {

        // restore network operation
        rss_local = false;
        n_rss_titles = rss_title_i = 0;

    } else {

        // erase list if network on or asked to do so
        if (!rss_local || title[0] == '\0') {
            n_rss_titles = rss_title_i = 0;
            for (int i = 0; i < RSS_MAXN; i++) {
                if (rss_titles[i]) {
                    free (rss_titles[i]);
                    rss_titles[i] = NULL;
                }
            }
        }

        // turn off network
        rss_local = true;

        // add title if room unless blank
        if (title[0] != '\0') {
            if (n_rss_titles < RSS_MAXN) {
                if (rss_titles[n_rss_titles])       // just paranoid
                    free (rss_titles[n_rss_titles]);
                rss_titles[n_rss_titles] = strdup (title);
                rss_title_i = n_rss_titles++;       // show new title
            } else {
                n_titles = RSS_MAXN;
                max_titles = RSS_MAXN;
                return (false);
            }
        }
    }

    // update info and refresh
    n_titles = n_rss_titles;
    max_titles = RSS_MAXN;
    scheduleRSSNow();

    // ok
    return (true);
}

/* return whether any stats shown in the SpcWx pane have changed.
 */
bool checkSpaceStats(void)
{
    return (checkSolarFlux() || checkKp() || checkXRay() || checkBzBt());
}

/* given touch location s known to be within NCDXF_b, insure the given space stat is in a visible Pane.
 * N.B. coordinate with drawSpaceStats()
 */
void doSpaceStatsTouch (const SCoord &s)
{
    // list of pane choices
    PlotChoice pcs[NCDXF_B_NFIELDS];
    pcs[0] = PLOT_CH_SSN;
    pcs[1] = PLOT_CH_FLUX;
    pcs[2] = PLOT_CH_XRAY;
    pcs[3] = PLOT_CH_KP;

    // do it
    doNCDXFStatsTouch (s, pcs);
}

/* draw selected *_spw in NCDXF_b.
 * use the given color for everything unless black then use the associated pane colors.
 */
void drawSpaceStats(uint16_t color)
{
    // arrays for drawNCDXFStats()
    static const char err[] = "Err";
    char titles[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN] = {
        "SFI",
        "X-Ray",
        "Kp",
        "Bz",
    };
    char values[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN];
    uint16_t colors[NCDXF_B_NFIELDS];
    int i = 0;

    // SFI
    if (sflux_spw == SPW_ERR)
        strcpy (values[i], err);
    else
        snprintf (values[i], sizeof(values[i]), "%.1f", sflux_spw);
    colors[i] = SFLUX_COLOR;
    i++;

    // Xray
    xrayLevel(xray_spw, values[i]);
    colors[i] = RGB565(255,134,0);      // XRAY_LCOLOR is too alarming
    i++;

    // Kp
    if (kp_spw == SPW_ERR)
        strcpy (values[i], err);
    else
        snprintf (values[i], sizeof(values[i]), "%.1f", kp_spw);
    colors[i] = KP_COLOR;
    i++;

    // Bz
    if (bz_spw == SPW_ERR)
        strcpy (values[i], err);
    else {
        if (fabsf(bz_spw) < 100)
            snprintf (values[i], sizeof(values[i]), "%.1f", bz_spw);
        else
            snprintf (values[i], sizeof(values[i]), "%.0f", bz_spw);
    }
    colors[i] = BZBT_BZCOLOR;
    i++;

    if (i != NCDXF_B_NFIELDS)
        fatalError (_FX("drawSpaceStats wrong count %d"), i);

    // do it
    drawNCDXFStats (color, titles, values, colors);
}

/* return when the given pane will next update.
 */
time_t nextPaneRotation(PlotPane pp)
{
    return (next_update[pp]);
}

/* return whether pane1 taps are to be ignored because a revert is in progress.
 */
bool ignorePane1Touch()
{
    return (myNow() < pane1_revtime);
}
