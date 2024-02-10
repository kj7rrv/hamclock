/* HamClock
 */


// glue
#include "HamClock.h"

// current version
const char *hc_version = HC_VERSION;

// clock, aux time and stopwatch boxes
SBox clock_b = { 0, 65, 230, 49};
SBox auxtime_b = { 0, 113, 205, 32};
SBox stopwatch_b = {149, 93, 38, 22};
SBox lkscrn_b = {216, 117, 13, 20};     // size must match HC_RUNNER_W/H base size

// DE and DX map boxes
SBox dx_info_b;                         // dx info location
static SBox askdx_b;                    // dx lat/lng dialog
SCircle dx_marker_c;                    // DX symbol in DX info pane
SBox satname_b;                         // satellite name
SBox de_info_b;                         // de info location
static SBox askde_b;                    // de lat/lng dialog
SBox de_title_b;                        // de title location
SBox map_b;                             // entire map, pixels only, not border
SBox view_btn_b;                        // map View button
SBox dx_maid_b;                         // dx maindenhead 
SBox de_maid_b;                         // de maindenhead 

// time zone boxes
TZInfo de_tz = {{75, 158, 50, 17}, DE_COLOR, 0};
TZInfo dx_tz = {{75, 307, 50, 17}, DX_COLOR, 0};

// NCDFX box, also used for brightness, on/off controls and space wx stats
SBox NCDXF_b = {740, 0, 60, PLOTBOX_H};

// common "skip" button in several places
SBox skip_b    = {730,10,55,35};
bool skip_skip;
bool want_kbcursor;

// special options to force initing DE using our IP or given IP
bool init_iploc;
const char *init_locip;

// maidenhead label boxes
SBox maidlbltop_b;
SBox maidlblright_b;

// satellite pass horizon
SCircle satpass_c;

// RSS banner box and state
SBox rss_bnr_b;
uint8_t rss_on;

// whether to shade night or show place names
uint8_t night_on, names_on;

// grid styles
uint8_t mapgrid_choice;
const char *grid_styles[MAPGRID_N] = {
    "None",
    "Tropics",
    "Lat/Long",
    "Maidenhead",
    "Azimuthal",
#if defined(_SUPPORT_ZONES)
    "CQ Zones",
    "ITU Zones",
#endif
};

// map projections
uint8_t map_proj;                               // one of MapProjection
#define X(a,b)  b,                              // expands MAPPROJS to name plus comma
const char *map_projnames[MAPP_N] = {
    MAPPROJS
};
#undef X

// info to display in the call sign GUI location -- the real call sign is always getCallsign()
CallsignInfo cs_info;
static const char on_air_msg[] = "ON THE AIR";  // used by setOnAir()
#define ONAIR_FG        RA8875_WHITE            // fg color
#define ONAIR_BG        RA8875_RED              // bg color
static SBox version_b;                          // show or check version, just below call
static SBox wifi_b;                             // wifi info
static bool rssi_ignore;                        // allow op to ignore low wifi power
static int rssi_avg;                            // running rssi mean
#define RSSI_ALPHA 0.5F                         // rssi blending coefficient
#define CSINFO_DROP     3                       // gap below cs_info
static MCPPoller onair_poller;                  // pin polling control

// de and dx sun rise/set boxes, dxsrss_b also used for DX prefix depending on dxsrss
SBox desrss_b, dxsrss_b;


// WiFi touch control
TouchType wifi_tt;
SCoord wifi_tt_s;

// set up TFT display controller RA8875 instance on hardware SPI plus reset and chip select
#define RA8875_RESET    16
#define RA8875_CS       2
Adafruit_RA8875_R tft(RA8875_CS, RA8875_RESET);

// MCP23017 driver
Adafruit_MCP23X17 mcp;
bool found_mcp;

// manage the great circle path through DE and DX points
// ESP must use points in order to selectively erase, UNIX never needs to erase

#if defined(_IS_ESP8266)
#define USE_GPATH
#endif

#if defined(USE_GPATH)
#define MAX_GPATH               1500            // max number of points to draw in great circle path
static SCoord *gpath;                           // malloced path points
static uint16_t n_gpath;                        // actual number in use
#endif // USE_GPATH

static uint32_t gpath_time;                     // millis() when great path was drawn, else 0
static SBox prefix_b;                           // where to show DX prefix text

// manage using DX cluster prefix or one from nearestPrefix()
static bool dx_prefix_use_override;             // whether to use dx_override_prefixp[] or nearestPrefix()
static char dx_override_prefix[MAX_PREF_LEN];

// longest interval between calls to resetWatchdog(), ms
uint32_t max_wd_dt;

// whether flash crc is ok
uint8_t flash_crc_ok;

// name of each DETIME setting, for menu and set_defmt
#define X(a,b)  b,                              // expands DETIMES to name plus comma
const char *detime_names[DETIME_N] = {
    DETIMES
};
#undef X

/* set DX-DE path no longer valid
 */
void setDXPathInvalid()
{
#if defined(USE_GPATH)
    if (gpath) {
        free (gpath);
        gpath = NULL;
    }
    n_gpath = 0;
#endif // USE_GPATH
    gpath_time = 0;
}

// these are needed for any normal C++ compiler, not that crazy Arduino IDE
static void drawVersion(bool force);
static void checkTouch(void);
static void drawUptime(bool force);
static void drawWiFiInfo(void);
static void toggleLockScreen(void);
static void drawDEFormatMenu(void);
static void eraseDXPath(void);
static void eraseDXMarker(void);
static void drawRainbow (SBox &box);
static void drawDXCursorPrefix (void);
static void setDXPrefixOverride (const char *ovprefix);
static void unsetDXPrefixOverride (void);
static void shutdown(void);



/* UNIX-only recovery attempts
 */
#if defined(_IS_UNIX)

/* try to restore pi to somewhat normal config
 */
static void defaultState()
{
    // try to insure screen is back on -- har!
    setFullBrightness();

    // return all IO pins to stable defaults
    SWresetIO();
    satResetIO();
    disableMCPPoller (onair_poller);
    radioResetIO();
}


#endif // _IS_UNIX


/* print setting of several compile-time #defines
 */
static void showDefines(void)
{
    #define _PR_MAC(m)   Serial.printf (_FX("#define %s\n"), #m)

    #if defined(_IS_ESP8266)
        _PR_MAC(_IS_ESP8266);
    #endif

    #if defined(_IS_UNIX)
        _PR_MAC(_IS_UNIX);
    #endif

    #if defined(_IS_LINUX)
        _PR_MAC(_IS_LINUX);
    #endif

    #if defined(_IS_FREEBSD)
        _PR_MAC(_IS_FREEBSD);
    #endif

    #if defined(_IS_LINUX_RPI)
        _PR_MAC(_IS_LINUX_RPI);
    #endif

    #if defined(_I2C_ESP)
        _PR_MAC(_I2C_ESP);
    #endif

    #if defined(_NATIVE_I2C_FREEBSD)
        _PR_MAC(_NATIVE_I2C_FREEBSD);
    #endif

    #if defined(_NATIVE_I2C_LINUX)
        _PR_MAC(_NATIVE_I2C_LINUX);
    #endif

    #if defined(_NATIVE_GPIO_ESP)
        _PR_MAC(_NATIVE_GPIO_ESP);
    #endif

    #if defined(_NATIVE_GPIO_FREEBSD)
        _PR_MAC(_NATIVE_GPIO_FREEBSD);
    #endif

    #if defined(_NATIVE_GPIO_LINUX)
        _PR_MAC(_NATIVE_GPIO_LINUX);
    #endif

    #if defined(_NATIVE_GPIOD_LINUX)
        _PR_MAC(_NATIVE_GPIOD_LINUX);
    #endif

    #if defined(_NATIVE_GPIOBC_LINUX)
        _PR_MAC(_NATIVE_GPIOBC_LINUX);
    #endif

    #if defined(_SUPPORT_NATIVE_GPIO)
        _PR_MAC(_SUPPORT_NATIVE_GPIO);
    #endif

    #if defined(_SUPPORT_FLIP)
        _PR_MAC(_SUPPORT_FLIP);
    #endif

    #if defined(_SUPPORT_KX3)
        _PR_MAC(_SUPPORT_KX3);
    #endif

    #if defined(_SUPPORT_PHOT)
        _PR_MAC(_SUPPORT_PHOT);
    #endif

    #if defined(_SUPPORT_SPOTPATH)
        _PR_MAC(_SUPPORT_SPOTPATH);
    #endif

    #if defined(_SUPPORT_DXCPLOT)
        _PR_MAC(_SUPPORT_DXCPLOT);
    #endif

    #if defined(_SUPPORT_CITIES)
        _PR_MAC(_SUPPORT_CITIES);
    #endif

    #if defined(_SUPPORT_ZONES)
        _PR_MAC(_SUPPORT_ZONES);
    #endif

    #if defined(_SUPPORT_ADIFILE)
        _PR_MAC(_SUPPORT_ADIFILE);
    #endif

    #if defined(_SUPPORT_SCROLLLEN)
        _PR_MAC(_SUPPORT_SCROLLLEN);
    #endif
}


// initial stack location
char *stack_start;

// called once
void setup()
{
    // init record of stack
    char stack;
    stack_start = &stack;

    #if defined(_IS_ESP8266)
    // life
    pinMode(LIFE_LED, OUTPUT);
    digitalWrite (LIFE_LED, HIGH);
    #endif // _IS_ESP8266

    // this just reset the soft timeout, the hard timeout is still 6 seconds
    ESP.wdtDisable();

    // start debug trace
    resetWatchdog();
    Serial.begin(115200);
    do {
        wdDelay(500);
    } while (!Serial);
    Serial.printf("\nHamClock version %s platform %s\n", hc_version, platform);

    // show config
    showDefines();

    // record whether our FLASH CRC is correct -- takes about half a second
    flash_crc_ok = ESP.checkFlashCRC();
#ifdef _IS_ESP8266
    Serial.printf(_FX("FLASH crc ok: %d\n"), flash_crc_ok);
#endif

    // random seed, not critical
    randomSeed(micros());

    // Initialise the display -- not worth continuing if not found
    if (!tft.begin(RA8875_800x480)) {
        Serial.println(_FX("RA8875 Not Found!"));
        while (1);
    }
    Serial.println(_FX("RA8875 found"));

    // must set rotation now from nvram setting .. too early to query setup options
    // N.B. don't save in EEPROM yet.
    uint8_t rot;
    if (!NVReadUInt8 (NV_ROTATE_SCRN, &rot))
        rot = 0;
    tft.setRotation(rot ? 2 : 0);

    // Adafruit's GFX defaults to wrap which breaks getTextBounds if string longer than tft.width()
    tft.setTextWrap(false);

    // Adafruit assumed ESP8266 would run at 80 MHz, but we run it at 160
    extern uint32_t spi_speed;
    spi_speed *= 2;

    // turn display full on
    tft.displayOn(true);
    tft.GPIOX(true); 
    tft.PWM1config(true, RA8875_PWM_CLK_DIV1024); // PWM output for backlight
    initBrightness();


    // now can save rot so if commit fails screen is up for fatalError()
    NVWriteUInt8 (NV_ROTATE_SCRN, rot);


// #define _GFX_COORD_TEST
#if defined(_GFX_COORD_TEST)

    // confirm our posix porting layer graphics agree with Adafruit
    tft.fillScreen(RA8875_BLACK);
    tft.fillRect (100, 100, 6, 6, RA8875_RED);
    tft.drawRect (100, 100, 8, 8, RA8875_RED);
    tft.drawPixel (100,108,RA8875_RED);
    tft.drawPixel (102,108,RA8875_RED);
    tft.drawPixel (104,108,RA8875_RED);
    tft.drawPixel (106,108,RA8875_RED);
    tft.drawPixel (108,108,RA8875_RED);
    tft.drawCircle (100, 200, 1, RA8875_RED);
    tft.drawCircle (100, 200, 5, RA8875_RED);
    tft.fillCircle (110, 200, 3, RA8875_RED);
    tft.drawPixel (100,207,RA8875_RED);
    tft.drawPixel (100,208,RA8875_RED);
    tft.drawPixel (102,207,RA8875_RED);
    tft.drawPixel (104,207,RA8875_RED);
    tft.drawPixel (106,207,RA8875_RED);
    tft.drawPixel (108,207,RA8875_RED);
    tft.drawPixel (110,207,RA8875_RED);
    tft.drawPixel (110,208,RA8875_RED);
    tft.drawPixel (112,207,RA8875_RED);
    tft.drawPixel (114,207,RA8875_RED);
    tft.drawPixel (114,200,RA8875_RED);


    // explore thick line artifacts

    for (float a = 0; a < 2*M_PIF; a += M_PIF/47) {
        uint16_t x = 250*tft.SCALESZ;
        uint16_t y = 120*tft.SCALESZ;
        int16_t dx = 100*tft.SCALESZ * cosf(a);
        int16_t dy = -100*tft.SCALESZ * sinf(a);
        tft.drawLineRaw (x, y, x+dx, y+dy, tft.SCALESZ/2, RA8875_RED);
        x += 400;
        tft.drawLineRaw (x, y, x+dx, y+dy, tft.SCALESZ, RA8875_RED);
        x += 400;
        tft.drawLineRaw (x, y, x+dx, y+dy, 2*tft.SCALESZ, RA8875_RED);
    }

    uint16_t x = 250*tft.SCALESZ;
    uint16_t y = 320*tft.SCALESZ;
    tft.drawLineRaw (x, y, x + 50, y, tft.SCALESZ, RA8875_RED);
    tft.drawLineRaw (x + 50, y, x + 75, y + 25, tft.SCALESZ, RA8875_RED);

    while(1)
        wdDelay(100);
#endif // _GFX_COORD_TEST

// #define _GFX_TEXTSZ
#if defined(_GFX_TEXTSZ)
    // just used to compare our text port with Adafruit GFX 
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    char str[] = "scattered clouds";
    int sum = 0;
    for (char *s = str; *s; s++) {
        char s1 = s[1];
        s[1] = '\0';
        int l = getTextWidth(s);
        Serial.printf ("%c %d\n", *s, l);
        s[1] = s1;
        sum += l;
    }
    Serial.printf ("Net %d\n", getTextWidth(str));
    Serial.printf ("Sum %d\n", sum);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (10,100);
    tft.print("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    tft.setCursor (10,120);
    tft.print("abcdefghijklmnopqrstuvwxyz");
    tft.setCursor (10,140);
    tft.print(R"(0123456789 ~!@#$%^&*()_+{}|:"<>? `-=[]\;',./)");

    while(1)
        wdDelay(100);
#endif // _GFX_TEXTSZ

    // enable touch screen system
    tft.touchEnable(true);

#if defined(_IS_UNIX)
    // support live even in setup
    initLiveWeb(false);
#endif

    // set up brb_rotset and brb_mode
    initBRBRotset();

    // run Setup at full brighness
    clockSetup();

    // initialize MCP23017, fails gracefully so just log whether found
    found_mcp = mcp.begin_I2C();
    if (found_mcp)
        Serial.println(_FX("MCP: GPIO mechanism found"));
    else
        Serial.println(_FX("MCP: GPIO mechanism not found"));

    // start onair poller
    startMCPPoller (onair_poller, ONAIR_PIN, 2);

    // continue with user's desired brightness
    setupBrightness();

    // do not display time until all set up
    hideClocks();

    // prep stopwatch
    initStopwatch();

    // here we go
    eraseScreen();

    // draw initial callsign
    cs_info.box.x = (tft.width()-512)/2;
    cs_info.box.y = 10;                 // coordinate with tftMsg()
    cs_info.box.w = 512;
    cs_info.box.h = 50;
    getDefaultCallsign();
    drawCallsign (true);

    // draw version just below
    tft.setTextColor (RA8875_WHITE);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (cs_info.box.x+(cs_info.box.w-getTextWidth(hc_version))/2, cs_info.box.y+cs_info.box.h+10);
    tft.print (hc_version);

    // position the map box in lower right -- border is drawn outside
    map_b.w = EARTH_W;
    map_b.h = EARTH_H;
    map_b.x = tft.width() - map_b.w - 1;        // 1 in from edge for border
    map_b.y = tft.height() - map_b.h - 1;       // 1 in from edge for border

    // position View button in upper left
    view_btn_b.x = map_b.x;
    view_btn_b.y = map_b.y;                     // gets adjusted if showing grid label
    view_btn_b.w = 60;
    view_btn_b.h = 14;

    // redefine callsign for main screen
    cs_info.box.x = 0;
    cs_info.box.y = 0;
    cs_info.box.w = 230;
    cs_info.box.h = 52;

    // wifi box
    wifi_b.x = cs_info.box.x + 68;              // a little past Uptime
    wifi_b.y = cs_info.box.y+cs_info.box.h+CSINFO_DROP;
    wifi_b.w = 114;
    wifi_b.h = 8;

    // version box
    version_b.w = 46;
    version_b.x = cs_info.box.x+cs_info.box.w-version_b.w;
    version_b.y = cs_info.box.y+cs_info.box.h+CSINFO_DROP;
    version_b.h = 8;

    // start WiFi, maps and set de_ll.lat_d/de_ll.lng_d from geolocation or gpsd as desired -- uses tftMsg()
    initSys();

    // get from nvram even if set prior from setup, geolocate or gpsd
    NVReadInt32(NV_DE_TZ, &de_tz.tz_secs);
    NVReadFloat(NV_DE_LAT, &de_ll.lat_d);
    NVReadFloat(NV_DE_LNG, &de_ll.lng_d);
    normalizeLL(de_ll);

    // ask to update if new version available -- never returns if update succeeds
    if (!skip_skip) {
        char nv[50];
        if (newVersionIsAvailable (nv, sizeof(nv)) && askOTAupdate (nv)) {
            if (askPasswd (_FX("upgrade"), false))
                doOTAupdate(nv);
            eraseScreen();
        }
    }

    // init sensors
    initBME280();

    // read plot settings from NVnsure sane defaults 
    initPlotPanes();

    // init rest of de info
    de_info_b.x = 1;
    de_info_b.y = 185;                  // below DE: line
    de_info_b.w = map_b.x - 2;          // just inside border
    de_info_b.h = 110;
    uint16_t devspace = de_info_b.h/DE_INFO_ROWS;
    askde_b.x = de_info_b.x + 1;
    askde_b.y = de_info_b.y;
    askde_b.w = de_info_b.w - 2;
    askde_b.h = 3*devspace;
    desrss_b.x = de_info_b.x + de_info_b.w/2;
    desrss_b.y = de_info_b.y + 2*devspace;
    desrss_b.w = de_info_b.w/2;
    desrss_b.h = devspace;
    de_maid_b.x = de_info_b.x;
    de_maid_b.y = de_info_b.y+(DE_INFO_ROWS-1)*de_info_b.h/DE_INFO_ROWS;
    de_maid_b.w = de_info_b.w/2;
    de_maid_b.h = devspace;
    if (!NVReadUInt8(NV_DE_SRSS, &desrss)) {
        desrss = false;
        NVWriteUInt8(NV_DE_SRSS, desrss);
    }
    if (!NVReadUInt8(NV_DE_TIMEFMT, &de_time_fmt)) {
        de_time_fmt = DETIME_INFO;
        NVWriteUInt8(NV_DE_TIMEFMT, de_time_fmt);
    }
    sdelat = sinf(de_ll.lat);
    cdelat = cosf(de_ll.lat);
    antipode (deap_ll, de_ll);
    ll2s (de_ll, de_c.s, DE_R);
    ll2s (deap_ll, deap_c.s, DEAP_R);
    de_title_b.x = de_info_b.x;
    de_title_b.y = de_tz.box.y-5;
    de_title_b.w = 30;
    de_title_b.h = 30;

    // init dx unit
    if (!NVReadUInt8 (NV_LP, &show_lp)) {
        show_lp = false;
        NVWriteUInt8 (NV_LP, show_lp);
    }
    dx_info_b.x = 1;
    dx_info_b.y = 295;
    dx_info_b.w = de_info_b.w;
    dx_info_b.h = 184;
    uint16_t dxvspace = dx_info_b.h/DX_INFO_ROWS;
    askdx_b.x = dx_info_b.x+1;
    askdx_b.y = dx_info_b.y + dxvspace;
    askdx_b.w = dx_info_b.w-2;
    askdx_b.h = 3*dxvspace;
    dx_marker_c.s.x = dx_info_b.x+62;
    dx_marker_c.s.y = dx_tz.box.y+8;
    dx_marker_c.r = dx_c.r;
    dxsrss_b.x = dx_info_b.x + dx_info_b.w/2;
    dxsrss_b.y = dx_info_b.y + 3*dxvspace;
    dxsrss_b.w = dx_info_b.w/2;
    dxsrss_b.h = dxvspace;
    dx_maid_b.x = dx_info_b.x;
    dx_maid_b.y = dx_info_b.y + 3*dxvspace;
    dx_maid_b.w = dx_info_b.w/2;
    dx_maid_b.h = dxvspace;
    if (!NVReadUInt8(NV_DX_SRSS, &dxsrss)) {
        dxsrss = DXSRSS_INAGO;
        NVWriteUInt8(NV_DX_SRSS, dxsrss);
    }
    if (!NVReadFloat(NV_DX_LAT,&dx_ll.lat_d) || !NVReadFloat(NV_DX_LNG,&dx_ll.lng_d)) {
        // if never set, default to 0/0
        dx_ll.lat_d = 0;
        dx_ll.lng_d = 0;
        NVWriteFloat (NV_DX_LAT, dx_ll.lat_d);
        NVWriteFloat (NV_DX_LNG, dx_ll.lng_d);
        setNVMaidenhead(NV_DX_GRID, dx_ll);
        dx_tz.tz_secs = getTZ (dx_ll);
        NVWriteInt32(NV_DX_TZ, dx_tz.tz_secs);
    }
    dx_ll.lat = deg2rad(dx_ll.lat_d);
    dx_ll.lng = deg2rad(dx_ll.lng_d);
    ll2s (dx_ll, dx_c.s, DX_R);
    if (!NVReadInt32(NV_DX_TZ, &dx_tz.tz_secs)) {
        dx_tz.tz_secs = getTZ (dx_ll);
        NVWriteInt32(NV_DX_TZ, dx_tz.tz_secs);
    }

    // sat pass circle
    satpass_c.r = dx_info_b.h/3 - 3;
    satpass_c.s.x = dx_info_b.x + dx_info_b.w/2;
    satpass_c.s.y = dx_info_b.y + dx_info_b.h - satpass_c.r - 4;


    // init portion of dx info used for satellite name
    satname_b.x = dx_info_b.x;
    satname_b.y = dx_info_b.y+1U;
    satname_b.w = dx_info_b.w;
    satname_b.h = dx_info_b.h/6;        // match FONT_H in earthsat.cpp

    // set up RSS state and banner box
    rss_bnr_b.x = map_b.x;
    rss_bnr_b.y = map_b.y + map_b.h - 68;
    rss_bnr_b.w = map_b.w;
    rss_bnr_b.h = 68;
    NVReadUInt8 (NV_RSS_ON, &rss_on);
    if (!NVReadUInt8 (NV_RSS_INTERVAL, &rss_interval) || rss_interval < RSS_MIN_INT) {
        rss_interval = RSS_DEF_INT;
        NVWriteUInt8 (NV_RSS_INTERVAL, rss_interval);
    }

    // set up map projection
    NVReadUInt8 (NV_MAPPROJ, &map_proj);

    // get grid style state
    NVReadUInt8 (NV_GRIDSTYLE, &mapgrid_choice);

    // init psk reporter
    initPSKState();

    // position the maiden label boxes
    maidlbltop_b.x = map_b.x;
    maidlbltop_b.y = map_b.y;
    maidlbltop_b.w = map_b.w;
    maidlbltop_b.h = MH_TR_H;
    maidlblright_b.x = map_b.x + map_b.w - MH_RC_W;
    maidlblright_b.y = map_b.y;
    maidlblright_b.w = MH_RC_W;
    maidlblright_b.h = map_b.h;

    // position the map scale
    // N.B. mapscale_b.y is set dynamically in drawMapScale() depending on rss_on
    mapscale_b.x = map_b.x;
    mapscale_b.w = map_b.w;
    mapscale_b.h = 10;
    mapscale_b.y = rss_on ? rss_bnr_b.y - mapscale_b.h: map_b.y + map_b.h - mapscale_b.h;

    // check for saved satellite
    dx_info_for_sat = initSatSelection();

    // show locked if web-only and -c
#if defined(_WEB_ONLY)
    if (no_web_touch)
        setScreenLock (true);
#endif

    // perform inital screen layout
    initScreen();

    // now start checking repetative wd
    max_wd_dt = 0;
}

// called repeatedly forever
void loop()
{
    // update stopwatch exclusively, if active
    if (runStopwatch()) {
        updateSatPass();                        // just for the satellite LED
        return;
    }

    // check on wifi including plots and NCDXF_b
    updateWiFi();

    // update clocks
    updateClocks(false);

    // update sat pass (this is just the pass; the path is recomputed before each map sweep)
    updateSatPass();

    // display more of earth map
    drawMoreEarth();

    // other goodies
    drawUptime(false);
    drawWiFiInfo();
    drawVersion(false);
    followBrightness();
    checkOnAir();
    readBME280();
    runNextDemoCommand();
    updateGPSDLoc();

    // check for touch events
    checkTouch();
}


/* draw the one-time portion of de_info either because we just booted or because
 * we are transitioning back from being in sat mode or a menu
 */
void drawOneTimeDE()
{
    tft.drawLine (0, map_b.y-1, map_b.x, map_b.y-1, GRAY);                        // top

    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor(DE_COLOR);
    tft.setCursor(de_info_b.x, de_tz.box.y+18);
    tft.print(F("DE:"));

    // save/restore de_c so it can be used for marker in box
    SCircle de_c_save = de_c;
    de_c.s.x = de_info_b.x+62;
    de_c.s.y = de_tz.box.y+8;
    drawDEMarker(true);
    de_c = de_c_save;
}

/* draw the one-time portion of dx_info either because we just booted or because
 * we are transitioning back from being in sat mode
 */
void drawOneTimeDX()
{
    if (dx_info_for_sat) {
        // sat
        drawSatPass();
    } else {
        // fresh
        tft.fillRect (dx_info_b.x, dx_info_b.y+1, dx_info_b.w, dx_info_b.h-1, RA8875_BLACK); // avoid borders

        // title
        selectFontStyle (BOLD_FONT, SMALL_FONT);
        tft.setTextColor(DX_COLOR);
        tft.setCursor(dx_info_b.x, dx_info_b.y + 30);
        tft.print(F("DX:"));

        // save/restore dx_c so it can be used for marker in box
        SCircle dx_c_save = dx_c;
        dx_c = dx_marker_c;
        drawDXMarker(true);
        dx_c = dx_c_save;
    }
}

/* assuming basic hw init is complete setup everything for the screen.
 * called once at startup and after each time returning from other full-screen options.
 * The overall layout is establihed by setting the various SBox values.
 * Some are initialized statically in setup() some are then set relative to these.
 */
void initScreen()
{
    resetWatchdog();

    // erase entire screen
    eraseScreen();

    // set protected region, which requires explicit call to tft.drawPR() to update
    tft.setPR (map_b.x, map_b.y, map_b.w, map_b.h);

    // us
    drawVersion(true);
    drawCallsign(true);

    // draw section borders
    tft.drawLine (0, map_b.y-1, tft.width()-1, map_b.y-1, GRAY);                        // top
    tft.drawLine (0, tft.height()-1, tft.width()-1, tft.height()-1, GRAY);              // bottom
    tft.drawLine (0, map_b.y-1, 0, tft.height()-1, GRAY);                               // left
    tft.drawLine (tft.width()-1, map_b.y-1, tft.width()-1, tft.height()-1, GRAY);       // right
    tft.drawLine (map_b.x-1, map_b.y-1, map_b.x-1, tft.height()-1, GRAY);               // left of map
    tft.drawLine (0, dx_info_b.y, map_b.x-1, dx_info_b.y, GRAY);                        // de/dx divider

    // one-time info
    setNewSatCircumstance();
    drawOneTimeDE();
    drawOneTimeDX();

    // set up beacon box
    resetWatchdog();
    (void) drawNCDXFBox();

    // enable clocks
    showClocks();
    drawMainPageStopwatch(true);

    // start 
    initEarthMap();
    initWiFiRetry();
    drawBME280Panes();
    drawUptime(true);
    drawScreenLock();

    // always close these so they will restart if open in any pane
    closeDXCluster();
    closeGimbal();

    // flush any stale touchs
    drainTouch();
}

/* round to whole degrees.
 * used to remove spurious fractions
 */
static void roundLL (LatLong &ll)
{
    ll.lat_d = roundf (ll.lat_d);
    ll.lng_d = roundf (ll.lng_d);
    normalizeLL(ll);
}


/* monitor for touch events
 */
static void checkTouch()
{
    resetWatchdog();

    TouchType tt;
    SCoord s;

    // check for remote and local touch
    if (wifi_tt != TT_NONE) {
        // save and reset remote touch.
        // N.B. remote touches never turn on brightness
        s = wifi_tt_s;
        tt = wifi_tt;
        wifi_tt = TT_NONE;
    } else {
        // check tap and kb
        tt = readCalTouch (s);
        if (tt == TT_NONE)
            tt = checkKBWarp (s);
        if (tt == TT_NONE)
            return;
        // don't do anything else if this tap just turned on brightness
        if (brightnessOn()) {
            drainTouch();
            return;
        }
    }

    // if get here TT is either TAP or HOLD

    // check lock first
    if (inBox (s, lkscrn_b)) {
        if (screenIsLocked()) {
            // if locked all you can do is unlock with a tap
            if (tt == TT_TAP && askPasswd(_FX("unlock"), true))
                toggleLockScreen();
        } else {
            // if unlocked HOLD always means shutdown else toggle
            if (tt == TT_HOLD) {
                shutdown();
            } else {
                if (getDemoMode())
                    setDemoMode (false);
                else
                    toggleLockScreen();
            }
        }

        // nothing else
        drawScreenLock();
        drainTouch();
        return;
    }
    if (screenIsLocked())
        return;

    drainTouch();

    // check all touch locations, ones that can be over map checked first
    LatLong ll;
    if (inBox (s, view_btn_b)) {
        // set flag to draw map menu at next opportunity
        mapmenu_pending = true;
    } else if (checkSatMapTouch (s)) {
        dx_info_for_sat = true;
        drawSatPass();
        eraseDXPath();                  // declutter to emphasize sat track
        drawAllSymbols(true);
    } else if (!overViewBtn(s, DX_R) && s2ll (s, ll)) {
        // new DE or DX.
        roundLL (ll);
        int mcl;
        if (tt == TT_HOLD) {
            // map hold: update DE
            // assume op wants city, if showing
            if (names_on)
                (void) getNearestCity (ll, ll, mcl);
            newDE (ll, NULL);
        } else {
            // just an ordinary map location: update DX
            // assume op wants city, if showing
            if (names_on)
                (void) getNearestCity (ll, ll, mcl);
            newDX (ll, NULL, NULL);
        }
    } else if (inBox (s, de_title_b)) {
        drawDEFormatMenu();
    } else if (inBox (s, stopwatch_b)) {
        // check this before checkClockTouch
        checkStopwatchTouch(tt);
    } else if (checkClockTouch(s)) {
        updateClocks(true);
    } else if (inBox (s, de_tz.box)) {
        if (TZMenu (de_tz, de_ll)) {
            NVWriteInt32 (NV_DE_TZ, de_tz.tz_secs);
            drawTZ (de_tz);
            scheduleNewMoon();
            scheduleNewSDO();
            scheduleNewBC();
        }
        drawDEInfo();   // erase regardless
    } else if (!dx_info_for_sat && inBox (s, dx_tz.box)) {
        if (TZMenu (dx_tz, dx_ll)) {
            NVWriteInt32 (NV_DX_TZ, dx_tz.tz_secs);
            drawTZ (dx_tz);
        }
        drawDXInfo();   // erase regardless
    } else if (checkCallsignTouchFG(s)) {
        NVWriteUInt16 (NV_CALL_FG_COLOR, cs_info.fg_color);
        drawCallsign (false);   // just foreground
    } else if (checkCallsignTouchBG(s)) {
        NVWriteUInt16 (NV_CALL_BG_COLOR, cs_info.bg_color);
        NVWriteUInt8 (NV_CALL_BG_RAINBOW, cs_info.bg_rainbow);
        drawCallsign (true);    // fg and bg
    } else if (!dx_info_for_sat && checkPathDirTouch(s)) {
        show_lp = !show_lp;
        NVWriteUInt8 (NV_LP, show_lp);
        drawDXInfo ();
        scheduleNewBC();
        scheduleNewVOACAPMap(prop_map);
    } else if (inBox (s, askde_b)) {
        // N.B. askde overlaps the desrss box
        if (de_time_fmt == DETIME_INFO && inBox (s, desrss_b)) {
            desrss = !desrss;
            NVWriteUInt8 (NV_DE_SRSS, desrss);
            drawDEInfo();
        } else {
            char maid[MAID_CHARLEN];
            getNVMaidenhead (NV_DE_GRID, maid);
            if (askNewPos (askde_b, ll = de_ll, maid))
                newDE (ll, maid);
            else
                drawDEInfo();
        }
    } else if (!dx_info_for_sat && inBox (s, askdx_b)) {
        // N.B. askdx overlaps the dxsrss box
        if (inBox (s, dxsrss_b)) {
            dxsrss = (dxsrss+1)%DXSRSS_N;
            NVWriteUInt8 (NV_DX_SRSS, dxsrss);
            drawDXInfo();
        } else {
            char maid[MAID_CHARLEN];
            getNVMaidenhead (NV_DX_GRID, maid);
            if (askNewPos (askdx_b, ll = dx_ll, maid)) {
                newDX (ll, maid, NULL);
            } else
                drawDXInfo();
        }
    } else if (!dx_info_for_sat && inCircle(s, dx_marker_c)) {
        newDX (dx_ll, NULL, NULL);
    } else if (checkPlotTouch(s, PANE_1, tt)) {
        updateWiFi();
    } else if (checkPlotTouch(s, PANE_2, tt)) {
        updateWiFi();
    } else if (checkPlotTouch(s, PANE_3, tt)) {
        updateWiFi();
    } else if (inBox (s, NCDXF_b)) {
        doNCDXFBoxTouch(s);
    } else if (checkSatNameTouch(s)) {
        dx_info_for_sat = querySatSelection();
        initScreen();
    } else if (dx_info_for_sat && inBox (s, dx_info_b)) {
        drawDXSatMenu(s);
    } else if (inBox (s, version_b)) {
        char nv[50];
        if (newVersionIsAvailable(nv, sizeof(nv))) {
            if (askOTAupdate (nv) && askPasswd (_FX("upgrade"), false))
                doOTAupdate(nv);
        } else {
            eraseScreen();
            tft.setTextColor (RA8875_WHITE);
            tft.setCursor (tft.width()/8, tft.height()/3);
            selectFontStyle (BOLD_FONT, SMALL_FONT);
            tft.print (F("You're up to date!"));        // match webserver response
            wdDelay(3000);
        }
        initScreen();
    } else if (inBox (s, wifi_b)) {
        int rssi;
        if (readWiFiRSSI(rssi))
            rssi_avg = runWiFiMeter (false, rssi_ignore);     // full effect of last reading
    }
}

/* given the degree members:
 *   clamp lat to [-90,90];
 *   modulo lng to [-180,180).
 * then fill in the radian members.
 */
void normalizeLL (LatLong &ll)
{
    ll.lat_d = CLAMPF(ll.lat_d,-90,90);                 // clamp lat
    ll.lat = deg2rad(ll.lat_d);

    ll.lng_d = fmodf(ll.lng_d+(2*360+180),360)-180;     // wrap lng
    ll.lng = deg2rad(ll.lng_d);
}

/* set new DX location from ll in dx_info.
 * use the given grid, else look up from ll.
 * also set override prefix unless NULL
 */
void newDX (LatLong &ll, const char grid[MAID_CHARLEN], const char *ovprefix)
{
    resetWatchdog();

    // require password if set
    if (!askPasswd(_FX("newdx"), true))
        return;

    // disable the sat info 
    if (dx_info_for_sat) {
        dx_info_for_sat = false;
        drawOneTimeDX();
    }

    // set grid and TZ
    normalizeLL (ll);
    if (grid)
        NVWriteString (NV_DX_GRID, grid);
    else
        setNVMaidenhead (NV_DX_GRID, ll);
    dx_tz.tz_secs = getTZ (ll);
    NVWriteInt32(NV_DX_TZ, dx_tz.tz_secs);

    // erase previous DX info
    eraseDXPath ();
    eraseDXMarker ();

    // set new location
    dx_ll = ll;
    ll2s (dx_ll, dx_c.s, DX_R);

    // set DX prefix
    if (ovprefix)
        setDXPrefixOverride (ovprefix);
    else
        unsetDXPrefixOverride();

    // draw in new location and update info
    drawDXPath ();
    gpath_time = millis();
    drawDXInfo ();
    drawAllSymbols(true);

    // don't draw if prefix from cluster because it draws also
    if (!dx_prefix_use_override)
        drawDXCursorPrefix();

    // show DX weather and update band conditions if showing
    scheduleNewBC();
    showDXWX();                 // after schedules to accommodate reverts

    // persist
    NVWriteFloat (NV_DX_LAT, dx_ll.lat_d);
    NVWriteFloat (NV_DX_LNG, dx_ll.lng_d);

    // log
    char dx_grid[MAID_CHARLEN];
    getNVMaidenhead (NV_DX_GRID, dx_grid);
    Serial.printf (_FX("New DX: %g %g %s\n"), dx_ll.lat_d, dx_ll.lng_d, dx_grid);
}

/* set new DE location, and optional grid
 */
void newDE (LatLong &ll, const char grid[MAID_CHARLEN])
{
    resetWatchdog();

    // require password if set
    if (!askPasswd(_FX("newde"), true))
        return;

    // set grid and TZ
    normalizeLL (ll);
    if (grid)
        NVWriteString (NV_DE_GRID, grid);
    else
        setNVMaidenhead (NV_DE_GRID, ll);
    de_tz.tz_secs = getTZ (ll);
    NVWriteInt32(NV_DE_TZ, de_tz.tz_secs);

    // sat path will change, stop gimbal and require op to start
    stopGimbalNow();

    if (map_proj == MAPP_AZIMUTHAL || map_proj == MAPP_AZIM1 ) {

        // must start over because everything moves to keep new DE centered

        drawDEMarker(false);
        de_ll = ll;

        // prep for all new map
        initEarthMap();

    } else {

        // for mercator|moll we try harder to just update the minimum .. lazy way is just call initEarthMap

        // erase current markers
        eraseDXPath();
        eraseDEMarker();
        eraseDEAPMarker();
        eraseDXMarker();

        // set new
        de_ll = ll;
        sdelat = sinf(de_ll.lat);
        cdelat = cosf(de_ll.lat);
        ll2s (de_ll, de_c.s, DE_R);
        antipode (deap_ll, de_ll);
        ll2s (deap_ll, deap_c.s, DEAP_R);

        // draw in new location and update info
        drawDEInfo();
        drawDXInfo();   // heading changes
        drawAllSymbols(true);
        
        // update lunar info
        getLunarCir (nowWO(), de_ll, lunar_cir);
    }

    // persist
    NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
    NVWriteFloat (NV_DE_LNG, de_ll.lng_d);

    // more updates that depend on DE regardless of projection
    scheduleNewMoon();
    scheduleNewSDO();
    scheduleNewBC();
    scheduleNewVOACAPMap(prop_map);
    sendDXClusterDELLGrid();
    scheduleNewPSK();
    showDEWX();                 // after schedules to accommodate reverts
    if (setNewSatCircumstance())
        drawSatPass();

    // log
    char de_grid[MAID_CHARLEN];
    getNVMaidenhead (NV_DE_GRID, de_grid);
    Serial.printf (_FX("New DE: %g %g %s\n"), de_ll.lat_d, de_ll.lng_d, de_grid);
}

/* return next color after current in basic series of primary colors that is nicely different from contrast.
 */
static uint16_t getNextColor (uint16_t current, uint16_t contrast)
{
    static uint16_t colors[] = {
        RA8875_RED, RA8875_GREEN, RA8875_BLUE, RA8875_CYAN,
        RA8875_MAGENTA, RA8875_YELLOW, RA8875_WHITE, RA8875_BLACK,
        DE_COLOR
    };
    #define NCOLORS NARRAY(colors)
    
    // find index of current color, ok if not found
    unsigned current_i;
    for (current_i = 0; current_i < NCOLORS; current_i++)
        if (colors[current_i] == current)
            break;

    // scan forward from current until find one nicely different from contrast
    for (unsigned cdiff_i = 1; cdiff_i < NCOLORS; cdiff_i++) {
        uint16_t next_color = colors[(current_i + cdiff_i) % NCOLORS];

        // certainly doesn't work if same as contrast
        if (next_color == contrast)
            continue;

        // continue scanning if bad combination
        switch (next_color) {
        case RA8875_RED:
            if (contrast == RA8875_MAGENTA || contrast == DE_COLOR)
                continue;
            break;
        case RA8875_GREEN:      // fallthru
        case RA8875_BLUE:
            if (contrast == RA8875_CYAN)
                continue;
            break;
        case RA8875_CYAN:
            if (contrast == RA8875_GREEN || contrast == RA8875_BLUE)
                continue;
            break;
        case RA8875_MAGENTA:
            if (contrast == RA8875_RED || contrast == DE_COLOR)
                continue;
            break;
        case RA8875_YELLOW:
            if (contrast == RA8875_WHITE)
                continue;
            break;
        case RA8875_WHITE:
            if (contrast == RA8875_YELLOW)
                continue;
            break;
        case RA8875_BLACK:
            // black goes with anything
            break;
        case DE_COLOR:
            if (contrast == RA8875_RED || contrast == RA8875_MAGENTA)
                continue;
            break;
        }

        // no complaints
        return (next_color);
    }

    // default 
    return (colors[0]);
}
 
/* given a touch location check if Op wants to change callsign fg.
 * if so then update cs_info and return true else false.
 */
bool checkCallsignTouchFG (SCoord &b)
{
    SBox left_half = cs_info.box;
    left_half.w /=2;

    if (inBox (b, left_half)) {
        // change fg
        uint16_t bg = cs_info.bg_rainbow ? RA8875_BLACK : cs_info.bg_color;
        cs_info.fg_color = getNextColor (cs_info.fg_color, bg);
        return (true);
    }
    return (false);
}


/* given a touch location check if Op wants to change callsign bg.
 * if so then update cs_info and return true else false.
 */
bool checkCallsignTouchBG (SCoord &b)
{
    SBox right_half = cs_info.box;
    right_half.w /=2;
    right_half.x += right_half.w;

    if (inBox (b, right_half)) {
        // change bg, cycling through rainbow when current color is white
        if (cs_info.bg_rainbow) {
            cs_info.bg_rainbow = false;
            cs_info.bg_color = getNextColor (cs_info.bg_color, cs_info.fg_color);
        } else if (cs_info.bg_color == RA8875_WHITE) {
            cs_info.bg_rainbow = true;
            // leave cs_info.bg_color to resume color scan when rainbow turned off
        } else {
            cs_info.bg_color = getNextColor (cs_info.bg_color, cs_info.fg_color);
        }
        return (true);
    }
    return (false);
}

/* erase the DX marker by restoring map
 */
static void eraseDXMarker()
{
    eraseSCircle (dx_c);

    // restore sat name in case hit
    for (int16_t dy = -dx_c.r; dy <= dx_c.r; dy++)
        drawSatNameOnRow (dx_c.s.y+dy);
}

/* erase great circle through DE and DX by restoring map at each entry in gpath[] then forget.
 * we also erase the box used to display the prefix
 */
static void eraseDXPath()
{
#if defined(USE_GPATH)
    // get out fast if nothing to do
    if (!n_gpath)
        return;

    // erase the prefix box
    for (uint16_t dy = 0; dy < prefix_b.h; dy++)
        for (uint16_t dx = 0; dx < prefix_b.w; dx++)
            drawMapCoord (prefix_b.x + dx, prefix_b.y + dy);

    // erase the great path
    for (uint16_t i = 0; i < n_gpath; i++)
        drawMapCoord (gpath[i]);

#endif // USE_GPATH

    // mark no longer active
    setDXPathInvalid();
}

/* find long- or short-path angular distance and east-of-north bearing from_ll to_ll given helper
 * values for sin and cos of from lat. all values in radians in range 0..2pi.
 */
void propPath (bool long_path, const LatLong &from_ll, float sflat, float cflat, const LatLong &to_ll,
float *distp, float *bearp)
{
    // cdist will be cos of short-path anglar separation in radians, so acos is 0..pi
    // *bearp will be short-path from to ll east-to-north in radians, -pi..pi
    float cdist;
    solveSphere (to_ll.lng-from_ll.lng, M_PI_2F-to_ll.lat, sflat, cflat, &cdist, bearp);

    if (long_path) {
        *distp = 2*M_PIF - acosf(cdist);              // long path can be anywhere 0..2pi
        *bearp = fmodf (*bearp + 3*M_PIF, 2*M_PIF);   // +180 then clamp to 0..2pi
    } else {
        *distp = acosf(cdist);                        // short part always 0..pi
        *bearp = fmodf (*bearp + 2*M_PIF, 2*M_PIF);   // shift -pi..pi to 0..2pi
    }
}

/* handy shortcut path for starting from DE
 */
void propDEPath (bool long_path, const LatLong &to_ll, float *distp, float *bearp)
{
    return (propPath (long_path, de_ll, sdelat, cdelat, to_ll, distp, bearp));
}

/* convert the given true bearing in degrees [0..360) at ll to desired units.
 * return whether desired units are magnetic, so no change in bear if return false.
 */
bool desiredBearing (const LatLong &ll, float &bear)
{
    resetWatchdog();

    if (useMagBearing()) {
        float decl;
        time_t t0 = nowWO();
        float yr = year(t0) + ((month(t0)-1) + (day(t0)-1)/30.0F)/12.0F;        // approx
        if (!magdecl (ll.lat_d, ll.lng_d, 200, yr, &decl)) {
            Serial.printf (_FX("Magnetic model only valid %g .. %g\n"), decl, decl+5);
            return (false);
        } else {
            // Serial.printf ("magdecl @ %g = %g\n", yr, decl);
            bear = fmodf (bear - decl + 360, 360);
            return (true);
        }
    }
    return (false);
}

/* draw great circle through DE and DX.
 * ESP uses points and saves screen coords in gpath[] so they can be selectively erase; UNIX draws
 *   with connected line segments because they need never be erased.
 */
void drawDXPath ()
{
    resetWatchdog();

    // find short-path bearing and distance from DE to DX
    float dist, bear;
    propDEPath (false, dx_ll, &dist, &bear);

#if defined(USE_GPATH)

    // start with max nnumber of points, then reduce
    gpath = (SCoord *) realloc (gpath, MAX_GPATH * sizeof(SCoord));
    if (!gpath)
        fatalError ("gpath %d", MAX_GPATH); // no _FX if alloc failing

    // walk great circle path from DE through DX, storing each point allowing for optinally dashed
    float ca, B;
    SCoord s;
    n_gpath = 0;
    uint16_t short_col = getMapColor(SHORTPATH_CSPR);
    uint16_t long_col = getMapColor(LONGPATH_CSPR);
    bool sp_dashed = getColorDashed(SHORTPATH_CSPR);
    bool lp_dashed = getColorDashed(LONGPATH_CSPR);
    int pix_i = 0;
    for (float b = 0; b < 2*M_PIF; b += 2*M_PIF/MAX_GPATH, pix_i++) {
        solveSphere (bear, b, sdelat, cdelat, &ca, &B);
        ll2s (asinf(ca), fmodf(de_ll.lng+B+5*M_PIF,2*M_PIF)-M_PIF, s, 1);
        bool show = b < dist ? (!sp_dashed || (pix_i % 20) < 10) : (!lp_dashed || (pix_i % 20) < 10);
        if (show && (n_gpath == 0 || memcmp (&s, &gpath[n_gpath-1], sizeof(SCoord))) && overMap(s)) {
            gpath[n_gpath++] = s;
            tft.drawPixel (s.x, s.y, b < dist ? short_col : long_col);
        }
    }

    // reduce to actual number of points used
    // Serial.printf (_FX("n_gpath %u -> %u\n"), MAX_GPATH, n_gpath);
    gpath = (SCoord *) realloc (gpath, n_gpath * sizeof(SCoord));
    if (!gpath && n_gpath > 0)
        fatalError("realloc gpath: %d", n_gpath);

#else // UNIX

    // walk great circle path from DE through DX with segment lengths PATH_SEGLEN
    float ca, B;
    SCoord s0 = {0, 0}, s1;
    uint16_t short_col = getMapColor(SHORTPATH_CSPR);
    uint16_t long_col = getMapColor(LONGPATH_CSPR);
    bool sp_dashed = getColorDashed(SHORTPATH_CSPR);
    bool lp_dashed = getColorDashed(LONGPATH_CSPR);
    bool dash_toggle = false;
    for (float b = 0; b < 2*M_PIF; b += deg2rad(PATH_SEGLEN)) {
        solveSphere (bear, b, sdelat, cdelat, &ca, &B);
        ll2sRaw (asinf(ca), fmodf(de_ll.lng+B+5*M_PIF,2*M_PIF)-M_PIF, s1, 1);
        bool show_seg = b < dist ? (!sp_dashed || dash_toggle) : (!lp_dashed || dash_toggle);
        if (s0.x) {
            if (segmentSpanOkRaw (s0, s1, 1)) {
                if (show_seg)
                    tft.drawLineRaw (s0.x, s0.y, s1.x, s1.y, tft.SCALESZ, b < dist ? short_col : long_col);
            } else {
                s1.x = 0;
            }
        }
        s0 = s1;
        dash_toggle = !dash_toggle;
    }

#endif // USE_GPATH
}

/* return whether we are waiting for a DX path to linger.
 * also erase path if its linger time just expired.
 */
bool waiting4DXPath()
{
    if (gpath_time == 0)
        return (false);

    if (timesUp (&gpath_time, DXPATH_LINGER)) {
        eraseDXPath();          // sets no longer valid
        drawAllSymbols(true);
        return (false);
    }

    return (true);
}

void drawDXMarker (bool force)
{
    if (force || showDXMarker()) {
        tft.fillCircle (dx_c.s.x, dx_c.s.y, DX_R, DX_COLOR);
        tft.drawCircle (dx_c.s.x, dx_c.s.y, DX_R, RA8875_BLACK);
        tft.fillCircle (dx_c.s.x, dx_c.s.y, 2, RA8875_BLACK);
    }
}

/* return the bounding box of the given string in the current font.
 */
void getTextBounds (const char str[], uint16_t *wp, uint16_t *hp)
{
    int16_t x, y;
    tft.getTextBounds ((char*)str, 100, 100, &x, &y, wp, hp);
}


/* return width in pixels of the given string in the current font
 */
uint16_t getTextWidth (const char str[])
{
    uint16_t w, h;
    getTextBounds (str, &w, &h);
    return (w);
}


/* set/restore the default callsign and colors.
 * N.B. this one sets up cs_info, we do not draw
 */
void getDefaultCallsign()
{
    free (cs_info.call);
    cs_info.call = strdup(getCallsign());
    if (!NVReadUInt16 (NV_CALL_FG_COLOR, &cs_info.fg_color)) {
        cs_info.fg_color = RA8875_BLACK;
        NVWriteUInt16 (NV_CALL_FG_COLOR, cs_info.fg_color);
    }
    if (!NVReadUInt16 (NV_CALL_BG_COLOR, &cs_info.bg_color)) {
        cs_info.bg_color = RA8875_WHITE;
        NVWriteUInt16 (NV_CALL_BG_COLOR, cs_info.bg_color);
    }
    if (!NVReadUInt8 (NV_CALL_BG_RAINBOW, &cs_info.bg_rainbow)) {
        cs_info.bg_rainbow = 1;
        NVWriteUInt8 (NV_CALL_BG_RAINBOW, cs_info.bg_rainbow);
    }
}

/* set ON AIR message in callsign area else restore normal call sign
 */
void setOnAir (bool on)
{
    if (on) {
        free (cs_info.call);
        cs_info.call = strdup (on_air_msg);
        cs_info.fg_color = ONAIR_FG;
        cs_info.bg_color = ONAIR_BG;
        cs_info.bg_rainbow = 0;
    } else {
        getDefaultCallsign();
    }

    drawCallsign (true);
}

/* change call sign to ON AIR as long as ONAIR_PIN is low
 */
bool checkOnAir()
{
    // only draw when changes
    static bool prev_on;

    // switch is grounded when active
    bool on = !readMCPPoller (onair_poller);

    if (on && !prev_on)
        setOnAir(true);
    else if (!on && prev_on)
        setOnAir(false);

    prev_on = on;

    return (on);
}

// handy
void fillSBox (const SBox &box, uint16_t color)
{
    tft.fillRect (box.x, box.y, box.w, box.h, color);
}
void drawSBox (const SBox &box, uint16_t color)
{
    tft.drawRect (box.x, box.y, box.w, box.h, color);
}

/* draw the given string in the given color anchored at x0,y0 with optional black shadow.
 */
void shadowString (const char *str, bool shadow, uint16_t color, uint16_t x0, uint16_t y0)
{
    if (shadow && color != RA8875_BLACK) {
        tft.setTextColor (RA8875_BLACK);
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx != 0 && dy != 0) {
                    tft.setCursor (x0 + dx, y0 + dy);
                    tft.print (str);
                }
            }
        }
    }

    tft.setTextColor (color);
    tft.setCursor (x0, y0);
    tft.print (str);
}

/* draw the given string centered in the given box using the current font and given color.
 * if it fits in one line, set y to box.y + l1dy.
 * if fits as two lines draw set their y to box.y + l12dy and l22dy.
 * if latter two are 0 then don't even try 2 lines.
 * if it won't fit in 2 lines and anyway is set, draw as much as possible.
 * if shadow then draw a black background shadow.
 * return whether it all fit some way.
 */
static bool drawBoxText (bool anyway, const SBox &box, const char *str, uint16_t color,
uint16_t l1dy, uint16_t l12dy, uint16_t l22dy, bool shadow)
{
    // try as one line
    uint16_t w = getTextWidth (str);
    if (w < box.w) {
        shadowString (str, shadow, color, box.x + (box.w-w)/2, box.y + l1dy);
        return (true);
    }

    // bale if don't even want to try 2 lines
    if (l12dy == 0 || l22dy == 0)
        return (false);

    // try splitting into 2 lines
    StackMalloc str_copy0(str);
    char *s0 = (char *) str_copy0.getMem();
    for (char *space = strrchr (s0,' '); space; space = strrchr (s0,' ')) {
        *space = '\0';
        uint16_t w0 = getTextWidth (s0);
        if (w0 < box.w) {
            char *s1 = space + 1;
            strcpy (s1, str + (s1 - s0));               // restore zerod spaces
            uint16_t w1 = getTextWidth (s1);
            if (w1 < box.w) {
                // 2 lines fit
                shadowString (s0, shadow, color, box.x + (box.w - w0)/2, box.y + l12dy);
                shadowString (s1, shadow, color, box.x + (box.w - w1)/2, box.y + l22dy);
                return (true);
            } else if (anyway) {
                // print 1st line and as AMAP of 2nd
                shadowString (s0, shadow, color, box.x + (box.w - w0)/2, box.y + l12dy);
                w1 = maxStringW (s1, box.w);
                shadowString (s1, shadow, color, box.x + (box.w - w1)/2, box.y + l22dy);
                return (false);
            }
        }
    }

    // no way
    return (false);
}

/* draw callsign using cs_info.
 * draw everything if all, else just fg text.
 */
void drawCallsign (bool all)
{
    tft.graphicsMode();

    if (all) {
        if (cs_info.bg_rainbow)
            drawRainbow (cs_info.box);
        else
            fillSBox (cs_info.box, cs_info.bg_color);
    }

    tft.setTextColor(cs_info.fg_color);

    // make copy in order to replace each 0 with del which is modified to be a slashed-0 in BOLD/LARGE
    StackMalloc call_slash0(cs_info.call);
    char *slash0 = (char *) call_slash0.getMem();
    for (char *z = slash0; *z != '\0' ; z++) {
        if (*z == '0')
            *z = 127;   // del
    }

    // make a copy with all upper case to try two lines using SMALL font (avoids descenders)
    StackMalloc call_uc(cs_info.call);
    char *uc = (char *) call_uc.getMem();
    for (char *z = uc; *z != '\0' ; z++)
        *z = toupper(*z);


    // keep shrinking font and trying 2 lines until fits
    const SBox &box = cs_info.box;              // handier
    uint16_t fg_c = cs_info.fg_color;           // handy
    bool sh = cs_info.bg_rainbow;               // shadow only if bg is rainbow
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    if (!drawBoxText (false, box, slash0, fg_c, box.h/2+20, 0, 0, sh)) {
        // try smaller font
        selectFontStyle (BOLD_FONT, SMALL_FONT);
        if (!drawBoxText (false, box, cs_info.call, fg_c, box.h/2+10, 0, 0, sh)) {
            // try smaller font
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            if (!drawBoxText (false, box, cs_info.call, fg_c, box.h/2+10, 0, 0, sh)) {
                // try all upper case to allow 2 lines with regard to descenders
                if (!drawBoxText (false, box, uc, fg_c, box.h/2+10, box.h/2-2, box.h-3, sh)) {
                    // try smallest font
                    selectFontStyle (LIGHT_FONT, FAST_FONT);
                    (void) drawBoxText (true, box, cs_info.call, fg_c, box.h/2-10, box.h/2-14, box.h/2+4, sh);
                }
            }
        }
    }
}

/* draw full spectrum in the given box.
 */
static void drawRainbow (SBox &box)
{
    uint8_t x0 = random(box.w);

    tft.graphicsMode();
    for (uint16_t x = box.x; x < box.x+box.w; x++) {
        uint8_t h = 255*((x+x0-box.x)%box.w)/box.w;
        tft.fillRect (x, box.y, 1, box.h, HSV565(h,255,255));
    }
}

/* draw version just below cs_info if time to recheck or force
 */
static void drawVersion(bool force)
{
    // how often to check for new version
    #define VER_CHECK_DT    (6*3600*1000UL)         // millis

    // get out fast if nothing to do yet
    static uint32_t check_t;
    if (!timesUp (&check_t, VER_CHECK_DT) && !force)
        return;

    // check for new version and reset timeout
    char line[50];
    bool new_avail = newVersionIsAvailable (line, sizeof(line));
    check_t = millis();

    // show current version, but highlight if new version is available
    uint16_t col = new_avail ? RA8875_RED : GRAY;
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    snprintf (line, sizeof(line), _FX("Ver %s"), hc_version);
    uint16_t vw = getTextWidth (line);
    tft.setTextColor (col);
    tft.setCursor (version_b.x+version_b.w-vw, version_b.y);        // right justify
    tft.print (line);
}

/* draw wifi signal strength or IP occasionally below cs_info
 */
static void drawWiFiInfo()
{
    resetWatchdog();

    // just once every few seconds, wary about overhead calling RSSI()
    static uint32_t prev_ms;
    if (!timesUp(&prev_ms, 5000))
        return;

    // message, if [0] != 0
    char str[40];
    str[0] = '\0';

    // toggle rssi and ip, or just ip if no wifi
    static bool rssi_ip_toggle;
    if (rssi_ip_toggle && millis() > 30000) {   // ignore first 30 seconds, reports of flakeness

        // show RSSI, if working, and show meter if too low and not ignored

        static int prev_logv;           // for only logging large changes

        // read
        int rssi;
        if (readWiFiRSSI(rssi)) {

            // Serial.printf ("**************************** %d\n", rssi);

            // blend, or use as-is if first time
            rssi_avg = prev_logv == 0 ? rssi : roundf(rssi*RSSI_ALPHA + rssi_avg*(1-RSSI_ALPHA));

            // show meter if too low unless ignored
            if (!rssi_ignore && rssi_avg < MIN_WIFI_RSSI)
                rssi_avg = runWiFiMeter (true, rssi_ignore);     // full effect of last reading

            // display value
            snprintf (str, sizeof(str), _FX("  WiFi %4d dBm"), rssi_avg);
            tft.setTextColor (rssi_avg < MIN_WIFI_RSSI ? RA8875_RED : GRAY);

            // log if changed more than a few db
            if (abs(rssi_avg-prev_logv) > 3) {
                Serial.printf (_FX("Up %lu s: RSSI %d\n"), millis()/1000U, rssi_avg);
                prev_logv = rssi_avg;
            }
        }

    } else {

        // show IP
        IPAddress ip = WiFi.localIP();
        bool net_ok = ip[0] != '\0';
        if (net_ok) {
            snprintf (str, sizeof(str), _FX("IP %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
            tft.setTextColor (GRAY);
        } else {
            strcpy (str, "    No Network");
            tft.setTextColor (RA8875_RED);
        }
    }

    if (str[0]) {
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.fillRect (wifi_b.x, wifi_b.y-1, wifi_b.w, wifi_b.h, RA8875_BLACK);
        tft.setCursor (wifi_b.x, wifi_b.y);
        tft.print(str);
    }

    // toggle
    rssi_ip_toggle = !rssi_ip_toggle;

}

static void prepUptime()
{
    resetWatchdog();

    const uint16_t x = cs_info.box.x;
    const uint16_t y = cs_info.box.y+cs_info.box.h+CSINFO_DROP;
    tft.fillRect (x+11, y-1, 50, 11, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (GRAY);
    tft.setCursor (x, y);
    tft.print (F("Up "));
}

/* return uptime in seconds, 0 if not ready yet.
 * already break into components if not NULL
 */
time_t getUptime (uint16_t *days, uint8_t *hrs, uint8_t *mins, uint8_t *secs)
{
    // "up" is elapsed time since first good value
    static time_t start_s;

    // get time now from NTP
    time_t now_s = myNow();
    if (now_s < 1490000000L)            // March 2017
        return (0);                     // not ready yet

    // get secs since starts_s unless first call or time ran backwards?!
    if (start_s == 0 || now_s < start_s)
        start_s = now_s;
    time_t up0 = now_s - start_s;

    // break out if interested
    if (days && hrs && mins && secs) {
        time_t up = up0;
        *days = up/SECSPERDAY;
        up -= *days*SECSPERDAY;
        *hrs = up/3600;
        up -= *hrs*3600;
        *mins = up/60;
        up -= *mins*60;
        *secs = up;
    }

    // return up secs
    return (up0);

}

/* draw time since boot just below cs_info.
 * keep drawing to a minimum and get out fast if no change unless force.
 */
static void drawUptime(bool force)
{
    // only do the real work once per second
    static uint32_t prev_ms;
    if (!timesUp(&prev_ms, 1000))
        return;

    // only redraw if significant chars change
    static uint8_t prev_m = 99, prev_h = 99;

    // get uptime, bail if not ready yet.
    uint16_t days; uint8_t hrs, mins, secs;
    time_t upsecs = getUptime (&days, &hrs, &mins, &secs);
    if (!upsecs)
        return;

    resetWatchdog();

    // draw two most significant units if change
    if (upsecs < 60) {
        prepUptime();
        tft.print(upsecs); tft.print(F("s "));
    } else if (upsecs < 3600) {
        prepUptime();
        tft.print(mins); tft.print(F("m "));
        tft.print(secs); tft.print(F("s "));
    } else if (upsecs < SECSPERDAY) {
        if (mins != prev_m || force) {
            prepUptime();
            tft.print(hrs); tft.print(F("h "));
            tft.print(mins); tft.print(F("m "));
            prev_m = mins;
        }
    } else {
        if (hrs != prev_h || force) {
            prepUptime();
            tft.print(days); tft.print(F("d "));
            tft.print(hrs); tft.print(F("h "));
            prev_h = hrs;
        }
    }
}

/* given an SCoord in raw coords, return one in app coords
 */
const SCoord raw2appSCoord (const SCoord &s_raw)
{
    SCoord s_app;
    s_app.x = s_raw.x/tft.SCALESZ;
    s_app.y = s_raw.y/tft.SCALESZ;
    return (s_app);
}

/* return whether coordinate s is over the maidenhead key around the edges.
 * N.B. assumes key is only shown in mercator projection.
 */
static bool overMaidKey (const SCoord &s)
{
    return (map_proj == MAPP_MERCATOR && mapgrid_choice == MAPGRID_MAID       
                        && (inBox(s,maidlbltop_b) || inBox(s,maidlblright_b)) );
}

/* return whether s is over the map globe, depending on the current projection.
 */
static bool overActiveMap (const SCoord &s)
{
    switch ((MapProjection)map_proj) {

    case MAPP_AZIMUTHAL: {
            // two adjacent hemispheres
            int map_r = map_b.w/4;
            int dy = (int)s.y - (int)(map_b.y + map_b.h/2);
            if (s.x < map_b.x + map_b.w/2) {
                // left globe
                int dx = (int)s.x - (int)(map_b.x + map_b.w/4);
                return (dx*dx + dy*dy <= map_r*map_r);
            } else {
                // right globe
                int dx = (int)s.x - (int)(map_b.x + 3*map_b.w/4);
                return (dx*dx + dy*dy <= map_r*map_r);
            }
        }

    case MAPP_AZIM1: {
            // one centered globe
            int map_r = map_b.w/4;
            int dy = (int)s.y - (int)(map_b.y + map_b.h/2);
            int dx = (int)s.x - (int)(map_b.x + map_b.w/2);
            return (dx*dx + dy*dy <= map_r*map_r);
        }

    case MAPP_MERCATOR:
        // full map_b
        return (inBox(s,map_b));

    case MAPP_ROB: {
            LatLong ll;
            return (s2llRobinson (s, ll));
        }

    default:
        fatalError (_FX("overActiveMap bogus projection %d"), map_proj);
        return (false);         // lint 
    }
}

/* return whether coordinate s is over an active map scale
 */
bool overMapScale (const SCoord &s)
{
    return (mapScaleIsUp() && inBox(s,mapscale_b));
}

/* return whether coordinate s is over a usable map location
 */
bool overMap (const SCoord &s)
{
    return (overActiveMap(s) && !overRSS(s) && !inBox(s,view_btn_b) && !overMaidKey(s) && !overMapScale(s));
}

/* return whether box b is over a usable map location
 */
bool overMap (const SBox &b)
{
    SCoord s00 = {(uint16_t) b.x,         (uint16_t) b.y};
    SCoord s01 = {(uint16_t) (b.x + b.w), (uint16_t) b.y};
    SCoord s10 = {(uint16_t) b.x,         (uint16_t) (b.y + b.h)};
    SCoord s11 = {(uint16_t) (b.x + b.w), (uint16_t) (b.y + b.h)};

    return (overMap(s00) && overMap(s01) && overMap(s10) && overMap(s11));
}


/* draw all symbols, order establishes layering priority
 * N.B. called by updateBeacons(): beware recursion
 */
void drawAllSymbols(bool beacons_too)
{
    updateClocks(false);
    resetWatchdog();

    if (mapScaleIsUp())
        drawMapScale();
    if (overMap(sun_c.s))
        drawSun();
    if (overMap(moon_c.s))
        drawMoon();
    if (beacons_too)
        updateBeacons(true, false);
    drawDEMarker(false);
    drawDXMarker(false);
    if (!overRSS(deap_c.s))
        drawDEAPMarker();
    drawOnTheAirSpotsOnMap();
    drawDXClusterSpotsOnMap();
    drawADIFSpotsOnMap();
    drawFarthestPSKSpots();
    drawSanta ();

    updateClocks(false);
}

/* return whether coordinate s is over an active RSS region.
 */
bool overRSS (const SCoord &s)
{
        return (rss_on && inBox (s, rss_bnr_b));
}

/* return whether box b is over an active RSS banner
 */
bool overRSS (const SBox &b)
{
    if (!rss_on)
        return (false);

    if (b.x >= rss_bnr_b.x+rss_bnr_b.w)
        return (false);
    if (b.y >= rss_bnr_b.y+rss_bnr_b.h)
        return (false);
    if (b.x + b.w <= rss_bnr_b.x)
        return (false);
    if (b.y + b.h <= rss_bnr_b.y)
        return (false);

    return (true);
}

/* log for sure and write another line to the initial screen if verbose.
 * verbose messages are arranged in two columns. screen row is advanced afterwards unless this and
 *    previous line ended with \r, and fmt == NULL forces return to normal row advancing for next call.
 */
void tftMsg (bool verbose, uint32_t dwell_ms, const char *fmt, ...)
{
    // setup
    #define MSG_NROWS   11              // actually 1- because we preincrement row
    #define MSG_ROWH    35
    #define MSG_COL1_X  10
    #define MSG_COL2_X  (tft.width()/2)
    #define MSG_ROW1_Y  100             // coordinate with _initial_ cs_info
    static uint8_t row, col;            // counters, not pixels
    static bool prev_hold;              // whether previous message stayed on row
    StackMalloc msg_buf(300);
    char *buf = (char *) msg_buf.getMem();

    // NULL fmt signals return to normal row advancing
    if (!fmt) {
        prev_hold = false;
        return;
    }

    // format msg
    va_list ap;
    va_start(ap, fmt);
    int l = vsnprintf (buf, msg_buf.getSize(), fmt, ap);
    va_end(ap);

    // note whether \r
    bool hold = buf[l-1] == '\r';
    // Serial.printf ("tftMsg hold= %d %s\n", hold, buf);

    // rm any \n and \r
    if (buf[l-1] == '\n' || buf[l-1] == '\r')
        buf[--l] = '\0';

    // log
    Serial.println(buf);

    // done unless verbose
    if (!verbose)
        return;


    // advance unless this and prev hold
    if (!(hold && prev_hold)) {
        if (++row == MSG_NROWS) {
            row = 1;
            col++;
        }
    }

    // set location
    uint16_t x = col ? MSG_COL2_X : MSG_COL1_X;
    uint16_t y = MSG_ROW1_Y + row*MSG_ROWH;

    // erase if this is another non-advance
    if (hold && prev_hold)
        tft.fillRect (x, y-(MSG_ROWH-9), tft.width()/2, MSG_ROWH, RA8875_BLACK);

    // draw
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor (x, y);
    tft.print(buf);

    if (dwell_ms)
        wdDelay (dwell_ms);

    // record
    prev_hold = hold;
}

/* toggle the NV_LKSCRN_ON value
 */
static void toggleLockScreen()
{
    uint8_t lock_on = !screenIsLocked();
    Serial.printf (_FX("Screen lock is now %s\n"), lock_on ? "On" : "Off");
    NVWriteUInt8 (NV_LKSCRN_ON, lock_on);
}

/* set the lock screen state.
 * then draw iff showing main display.
 */
void setScreenLock (bool on)
{
    if (on != screenIsLocked()) {
        toggleLockScreen();
        // ?? setDemoMode (false);
        if (getSWDisplayState() == SWD_NONE)
            drawScreenLock();
    }
}

/* draw the lock screen symbol according to demo and/or NV_LKSCRN_ON.
 */
void drawScreenLock()
{
    fillSBox (lkscrn_b, RA8875_BLACK);

    if (getDemoMode()) {

        // runner icon
        const uint16_t rx = tft.SCALESZ*lkscrn_b.x;
        const uint16_t ry = tft.SCALESZ*lkscrn_b.y;
        for (uint16_t dy = 0; dy < HC_RUNNER_H; dy++)
            for (uint16_t dx = 0; dx < HC_RUNNER_W; dx++)
                tft.drawPixelRaw (rx+dx, ry+dy, pgm_read_word(&runner[dy*HC_RUNNER_W + dx]));

    } else {

        uint16_t hh = lkscrn_b.h/2;
        uint16_t hw = lkscrn_b.w/2;

        tft.fillRect (lkscrn_b.x, lkscrn_b.y+hh, lkscrn_b.w, hh, RA8875_WHITE);
        tft.drawLine (lkscrn_b.x+hw, lkscrn_b.y+hh+2, lkscrn_b.x+hw, lkscrn_b.y+hh+hh/2, RA8875_BLACK);
        tft.drawCircle (lkscrn_b.x+hw, lkscrn_b.y+hh, hw, RA8875_WHITE);

        if (!screenIsLocked())
            tft.fillRect (lkscrn_b.x+hw, lkscrn_b.y, hw+1, hh, RA8875_BLACK);
    }
}

/* offer menu of DE format options and engage selection
 */
static void drawDEFormatMenu()
{
    // menu of each DETIME_*
    #define _MI_INDENT  5
    MenuItem mitems[DETIME_N] = {
        {MENU_1OFN, de_time_fmt == DETIME_INFO,        1, _MI_INDENT, detime_names[DETIME_INFO]},
        {MENU_1OFN, de_time_fmt == DETIME_ANALOG,      1, _MI_INDENT, detime_names[DETIME_ANALOG]},
        {MENU_1OFN, de_time_fmt == DETIME_CAL,         1, _MI_INDENT, detime_names[DETIME_CAL]},
        {MENU_1OFN, de_time_fmt == DETIME_ANALOG_DTTM, 1, _MI_INDENT, detime_names[DETIME_ANALOG_DTTM]},
        {MENU_1OFN, de_time_fmt == DETIME_DIGITAL_12,  1, _MI_INDENT, detime_names[DETIME_DIGITAL_12]},
        {MENU_1OFN, de_time_fmt == DETIME_DIGITAL_24,  1, _MI_INDENT, detime_names[DETIME_DIGITAL_24]},
    };

    // create a box for the menu
    SBox menu_b;
    menu_b.x = de_info_b.x + 4;
    menu_b.y = de_info_b.y;
    menu_b.w = 0;       // shrink to fit

    // run menu
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, false, false, 1, NARRAY(mitems), mitems};
    if (runMenu (menu)) {
        // capture and save new value
        for (int i = 0; i < DETIME_N; i++) {
            if (mitems[i].set) {
                de_time_fmt = i;
                break;
            }
        }
        NVWriteUInt8(NV_DE_TIMEFMT, de_time_fmt);
    }

    // draw new state, even if no change in order to erase
    drawDEInfo();
}

/* resume using nearestPrefix
 */
static void unsetDXPrefixOverride ()
{
    dx_prefix_use_override = false;
}

/* set an override prefix for getDXPrefix() to use instead of using nearestPrefix()
 */
static void setDXPrefixOverride (const char *ovprefix)
{
    // extract
    call2Prefix (ovprefix, dx_override_prefix);

    // flag ready
    dx_prefix_use_override = true;
}

/* extract the prefix from the given call sign -- this is magic
 */
void call2Prefix (const char *call, char prefix[MAX_PREF_LEN])
{
    // init
    memset (prefix, 0, MAX_PREF_LEN);

    // copy call into prefix; if contains / usually use the shorter side
    const char *slash = strchr (call, '/');
    if (slash) {
        const char *right = slash+1;
        size_t llen = slash - call;
        size_t rlen = strlen (right);
        const char *slash2 = strchr (right, '/');
        if (slash2)
            rlen = slash2 - right;              // don't count past 2nd slash

        if (rlen <= 1 || llen <= rlen || !strcmp(right,"MM") || !strcmp(right,"AM")
                        || !strncmp (right, "QRP", 3) || strspn(right,"0123456789") == rlen)
            memcpy (prefix, call, llen >= MAX_PREF_LEN ? MAX_PREF_LEN-1 : llen); 
        else
            memcpy (prefix, right, rlen >= MAX_PREF_LEN ? MAX_PREF_LEN-1 : rlen); 
    } else
        memcpy (prefix, call, MAX_PREF_LEN-1);

    // truncate after right-most digit
    for (int i = MAX_PREF_LEN-1; --i >= 0; ) {
        if (isdigit(prefix[i])) {
            prefix[i+1] = '\0';
            break;
        }
    }

}

/* return the override prefix else nearest one based on ll, if any
 */
bool getDXPrefix (char p[MAX_PREF_LEN+1])
{
    if (dx_prefix_use_override) {
        memcpy (p, dx_override_prefix, MAX_PREF_LEN);
        p[MAX_PREF_LEN] = '\0';
        return (true);
    } else {
        return (nearestPrefix (dx_ll, p));
    }
}

/* display the DX prefix at dx_c 
 */
static void drawDXCursorPrefix()
{
    char p[MAX_PREF_LEN+1];
    if (getDXPrefix(p)) {
        setMapTagBox (p, dx_c.s, dx_c.r+2, prefix_b);
        drawMapTag (p, prefix_b);
    }
}

/* this function positions a box to contain short text beneath a symbol at location s that has radius r,
 * where s is assumed to be over map. the preferred position is centered below s, but it may be moved
 * to either side to avoid going off the map.
 * N.B. r == 0 is a special case to mean center the text exactly at s, not beneath it.
 * N.B. coordinate with drawMapTag()
 */
void setMapTagBox (const char *tag, const SCoord &s, uint16_t r, SBox &box)
{
    // get text size
    uint16_t cw, ch;
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    getTextBounds (tag, &cw, &ch);

    // set box size to text with a small margin
    box.w = cw+2;
    box.h = ch+2;

    // set at preferred location
    box.x = s.x - box.w/2;                      // center horizontally at s
    box.y = s.y + (r ? r : -box.h/2);           // below but if r is 0 then center vertically at s

    // but beware edges

    // handy
    const uint16_t rx = box.x + box.w;                      // right box x
    const uint16_t by = box.y + box.h;                      // bottom box y
    const uint16_t mc = map_b.x + map_b.w/2;                // map center
    const bool is_nt = box.y < map_b.y + 2*box.h;           // is near top map edge
    const bool is_nb = box.y > map_b.y + map_b.h - 2*box.h; // is near bottom map edge

    if (is_nb && (!overActiveMap(SCoord{box.x,by}) || !overActiveMap(SCoord{rx,by}))) {
        // near the bottom: move box above s
        box.y = s.y - r - box.h;

    } else if (is_nt && !overActiveMap(SCoord{box.x,box.y})) {
        // near the top and off on the left: move box so left edge is under s
        box.x = s.x;

    } else if (is_nt && !overActiveMap(SCoord{rx,box.y})) {
        // near the top and off on the right: move box so right edge is under s
        box.x = s.x - box.w;

    } else if (!overActiveMap(SCoord{box.x,box.y})) {
        // too far left: center box to the right of s
        box.x = s.x + r;
        box.y = s.y - box.h/2;

    } else if (!overActiveMap(SCoord{rx,box.y})) {
        // too far right: center box to the left of s
        box.x = s.x - r - box.w;
        box.y = s.y - box.h/2;
    } else if (map_proj == MAPP_AZIMUTHAL && (box.x < mc) != (rx < mc)) {
        // sides are in opposite hemispheres: shift to same side as s
        if (s.x < mc) {
            // center box to the left of s
            box.x = s.x - r - box.w;
            box.y = s.y - box.h/2;
        } else {
            // center box to the right of s
            box.x = s.x + r;
            box.y = s.y - box.h/2;
        }
    }
}

/* draw a string within the given box set by setMapTagBox, using the optional color (default white).
 * actually this is no longer a box but text with a shifted background -- more moxy!
 * still, knowing the box has been positioned well allows for this masking.
 * N.B. coordinate with setMapTagBox()
 */
void drawMapTag (const char *tag, const SBox &box, uint16_t txt_color, uint16_t bg_color)
{
    // small font
    selectFontStyle (LIGHT_FONT, FAST_FONT);

    // text position
    uint16_t x0 = box.x+1;
    uint16_t y0 = box.y+1;

#if BUILD_W > 800

    // draw background by shifting O's around
    int tag_len = strlen(tag);
    StackMalloc bkg_str(tag_len+1);
    char *bkg = (char*) bkg_str.getMem();
    memset (bkg, 'O', tag_len);
    bkg[tag_len] = '\0';
    tft.setTextColor (bg_color);
    for (int16_t dx = -1; dx <= 1; dx++) {
        for (int16_t dy = -1; dy <= 1; dy++) {
            if (dx || dy) {
                tft.setCursor (x0+dx, y0+dy);
                tft.print(bkg);
            }
        }
    }

    // avoid some bit turds in the center of the Os
    for (unsigned i = 0; i < strlen(tag); i++)
        tft.fillRect (x0+i*6,y0,5,7,bg_color);

#else

    // just too small for finessing
    fillSBox (box, bg_color);
    
#endif

    // draw text
    tft.setTextColor (txt_color);
    tft.setCursor (x0, y0);
    tft.print((char*)tag);
}

/* return whether screen is currently locked
 */
bool screenIsLocked()
{
    uint8_t lock_on;
    if (!NVReadUInt8 (NV_LKSCRN_ON, &lock_on)) {
        lock_on = 0;
        NVWriteUInt8 (NV_LKSCRN_ON, lock_on);
    }
    return (lock_on != 0);
}

/* return whether SCoord is within SBox
 */
bool inBox (const SCoord &s, const SBox &b)
{
    return (s.x >= b.x && s.x < b.x+b.w && s.y >= b.y && s.y < b.y+b.h);
}

/* return whether SCoord is within SCircle.
 * N.B. must match Adafruit_RA8875::fillCircle()
 */
bool inCircle (const SCoord &s, const SCircle &c)
{
    // beware unsigned subtraction
    uint16_t dx = (s.x >= c.s.x) ? s.x - c.s.x : c.s.x - s.x;
    uint16_t dy = (s.y >= c.s.y) ? s.y - c.s.y : c.s.y - s.y;
    return (4*dx*dx + 4*dy*dy <= 4*c.r*(c.r+1)+1);
}

/* return whether any part of b1 and b2 overlap.
 */
bool boxesOverlap (const SBox &b1, const SBox &b2)
{
    return ((b1.x + b1.w >= b2.x && b1.x <= b2.x + b2.w)
         && (b1.y + b1.h >= b2.y && b1.y <= b2.y + b2.h));
}

/* erase the given SCircle
 */
void eraseSCircle (const SCircle &c)
{
    // scan a circle of radius r+1/2 to include whole pixel.
    // radius (r+1/2)^2 = r^2 + r + 1/4 so we use 2x everywhere to avoid floats
    uint16_t radius2 = 4*c.r*(c.r + 1) + 1;
    for (int16_t dy = -2*c.r; dy <= 2*c.r; dy += 2) {
        for (int16_t dx = -2*c.r; dx <= 2*c.r; dx += 2) {
            int16_t xy2 = dx*dx + dy*dy;
            if (xy2 <= radius2)
                drawMapCoord (c.s.x+dx/2, c.s.y+dy/2);
        }
    }
}

/* erase entire screen engage immediate graphical updates
 */
void eraseScreen()
{
    tft.setPR (0, 0, 0, 0);
    tft.fillScreen(RA8875_BLACK);
    tft.drawPR();
}

void resetWatchdog()
{
#if defined(_IS_ESP8266)
    // record longest wd feed interval so far in max_wd_dt
    static uint32_t prev_ms;
    uint32_t ms = millis();
    uint32_t dt = ms - prev_ms;         // works ok if millis rolls over
    if (dt > 1000) { // ignore crazy fast
        if ((dt > max_wd_dt || dt > 5000) && prev_ms > 0) {
            // Serial.printf (_FX("max WD %u\n"), dt);
            max_wd_dt = dt;
        }
        prev_ms = ms;

        ESP.wdtFeed();
        yield();
    }
#endif // _IS_ESP8266
}

/* like delay() but breaks into small chunks so we can call resetWatchdog() and update live web
 */
void wdDelay(int ms)
{
    #define WD_DELAY_DT   50
    uint32_t t0 = millis();
    int dt;
    while ((dt = millis() - t0) < ms) {
        resetWatchdog();
        checkWebServer(true);
        if (dt < WD_DELAY_DT)
            delay (dt);
        else
            delay (WD_DELAY_DT);
    }
}

/* handy utility to return whether now is atleast_dt ms later than prev.
 * if so, update *prev and return true, else return false.
 */
bool timesUp (uint32_t *prev, uint32_t atleast_dt)
{
    uint32_t ms = millis();
    uint32_t dt = ms - *prev;   // works ok if millis rolls over
    if (dt > atleast_dt) {
        *prev = ms;
        return (true);
    }
    return (false);
}



#if defined (_IS_UNIX)


/* called to post our diagnostics files to the server for later analysis.
 * include the ip in the name which identifies us using our public IP address.
 */
void postDiags (void)
{
    WiFiClient pd;

    // build filename relative to hamclock server root dir and id
    char fn[300];
    snprintf (fn, sizeof(fn), "/ham/HamClock/diagnostic-logs/dl-%lld-%s-%u.txt", (long long)myNow(),
                                                remote_addr, ESP.getChipId());

    // get total size of all diag files for content length
    struct stat s;
    int cl = 0;
    for (int i = 0; i < N_DIAG_FILES; i++) {
        std::string dp = our_dir + diag_files[i];
        if (stat (dp.c_str(), &s) == 0)
            cl += s.st_size;
    }

    // add eeprom file
    if (stat (EEPROM.getFilename(), &s) == 0)
        cl += s.st_size;

    Serial.printf ("DP: %d %s\n", cl, fn);

    if (pd.connect (backend_host, backend_port)) {

        char buf[4096];
        int buf_l = 0;

        // hand-crafted POST header, move to its own func if ever used for something else
        buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, "POST %s HTTP\r\n", fn);
        buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, "Content-Length: %d\r\n", cl);
        pd.print (buf);
        sendUserAgent (pd);
        pd.print ("\r\n");

        // just concat each file including eeprom
        for (int i = 0; i <= N_DIAG_FILES; i++) {                       // 1 more for eeprom
            std::string dp = i < N_DIAG_FILES ? our_dir + diag_files[i] : EEPROM.getFilename();
            FILE *fp = fopen (dp.c_str(), "r");
            if (fp) {
                Serial.printf ("DP:   %s\n", dp.c_str());
                int n_r;
                while ((n_r = fread (buf, 1, sizeof(buf), fp)) > 0)
                    pd.write ((uint8_t*)buf, n_r);
                fclose (fp);
            }
        }
        
        pd.stop();

    } else
        Serial.printf ("postDiags() failed to connect to %s:%d\n", backend_host, backend_port);

}
#endif // _IS_UNIX


/* shutdown helper that asks Are You Sure in the given box.
 * hl_color is BLACK unless we know keyboard action got us here.
 * return answer.
 */
static bool shutdownRUS (const SBox &b, uint16_t txt_color, uint16_t hl_color)
{
    // cursor y
    uint16_t cur_y = b.y + 7*b.h/10;

    // erase and print query
    tft.fillRect (b.x+1, b.y+1, b.w-2, b.h-2, RA8875_BLACK);    // erase but avoid boundary
    tft.setCursor (b.x+50, cur_y);
    tft.setTextColor(txt_color);
    tft.print (F("Are you sure? "));

    // define yes/no boxes
    SBox y_b;
    y_b.x = tft.getCursorX();
    y_b.y = b.y;
    y_b.w = 60;
    y_b.h = b.h;
    tft.setCursor (y_b.x+10, cur_y);
    tft.print (F("Yes"));

    SBox n_b = y_b;
    n_b.x += y_b.w+30;
    tft.setCursor (n_b.x+15, cur_y);
    tft.print (F("No"));

    // keyboard code considers Yes to be box 0, No to be box 1
    int kb_sel = hl_color == RA8875_BLACK ? -1 : 0;

    // wait for one to be tapped
    SCoord tap_s;
    char type_c;
    UserInput ui = {
        b,
        NULL,
        false,
        0,
        false,
        tap_s,
        type_c
    };
    for (;;) {

        tft.drawRect (y_b.x+3, y_b.y+3, y_b.w-6, y_b.h-6, kb_sel == 0 ? hl_color : RA8875_BLACK);
        tft.drawRect (n_b.x+3, n_b.y+3, n_b.w-6, n_b.h-6, kb_sel == 1 ? hl_color : RA8875_BLACK);

        (void) waitForUser (ui);

        if (type_c) {
            if (type_c == '\r' || type_c == '\n' || type_c == ' ')
                return (kb_sel == 0);
            if (type_c == 'h' && kb_sel == 1)
                kb_sel = 0;
            if (type_c == 'l' && kb_sel == 0)
                kb_sel = 1;
        } else {
            if (inBox (tap_s, y_b))
                return (true);
            if (inBox (tap_s, n_b))
                return (false);
        }
    }
}

/* offer power down, restart etc depending on platform.
 */
static void shutdown(void)
{
    closeDXCluster();       // prevent inbound msgs from clogging network
    closeGimbal();          // avoid dangling connection

    eraseScreen();

    // prep
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    const uint16_t x0 = tft.width()/4;
    const uint16_t w = tft.width()/2;
    const uint16_t h = 50;
    uint16_t y = tft.height()/6;

    // define each possibility -- set depends on platform
    typedef struct {
        const char *prompt;
        SBox box;
        uint16_t color;
    } ShutDownCtrl;
    enum {
        _SDC_RESUME,
        _SDC_RESTART,
        _SDC_EXIT,
        _SDC_POSTDIAGS,
        _SDC_REBOOT,
        _SDC_SHUTDOWN
    };
    ShutDownCtrl sdctl[] = {            // N.B. must be in order of _SDC_* enum
        {"Disregard -- resume",         {x0, y,                 w, h}, RA8875_GREEN},
        {"Restart HamClock",            {x0, (uint16_t)(y+1*h), w, h}, RA8875_YELLOW},
        #if defined(_IS_UNIX)
            {"Exit HamClock",           {x0, (uint16_t)(y+2*h), w, h}, RA8875_MAGENTA},
            {"Post diagnostics",        {x0, (uint16_t)(y+3*h), w, h}, RGB565(100,100,255)},
            {"Reboot host",             {x0, (uint16_t)(y+4*h), w, h}, RA8875_RED},
            {"Shutdown host",           {x0, (uint16_t)(y+5*h), w, h}, RGB565(255,125,0)},
        #endif // _IS_UNIX
    };

    // kb managements
    #define _SDC_KBC     RA8875_GREEN   // selected color
    int sdc_kbd = -1;                   // index of selected item via keyboard

    // number of options
    #if defined(_IS_UNIX)
        // os control only when full screen
        int n_sdctl = getX11FullScreen() ? NARRAY(sdctl) : NARRAY(sdctl)-2;
    #else
        // only restart makes sense
        int n_sdctl = NARRAY(sdctl);
    #endif

    // main loop that displays choices until one is confirmed
    int selection = -1;
    do {

        // wait for a selection
        for (selection = -1; selection < 0; ) {

            // display all
            for (int i = 0; i < n_sdctl; i++) {
                ShutDownCtrl &sdc = sdctl[i];
                drawStringInBox (sdc.prompt, sdc.box, false, sdc.color);
                if (i == sdc_kbd)
                    tft.drawRect (sdc.box.x+5, sdc.box.y+5, sdc.box.w-10, sdc.box.h-10, _SDC_KBC);
            }

            // wait forever for user
            SBox screen_b;
            screen_b.x = 0;
            screen_b.y = 0;
            screen_b.w = tft.width();
            screen_b.h = tft.height();
            SCoord tap_s;
            char type_c;
            UserInput ui = {
                screen_b,
                NULL,
                false,
                0,
                false,
                tap_s,
                type_c
            };
            (void) waitForUser (ui);

            // check user input
            if (type_c) {

                switch (type_c) {
                case '\r':
                case '\n':
                case ' ':
                    selection = sdc_kbd;
                    break;
                case 'j':
                    sdc_kbd = (sdc_kbd + 1) % n_sdctl;
                    break;
                case 'k':
                    sdc_kbd = (n_sdctl + sdc_kbd - 1) % n_sdctl;
                    break;
                default:
                    break;
                }

            } else {

                // check mouse
                for (int i = 0; i < n_sdctl; i++) {
                    if (inBox (tap_s, sdctl[i].box)) {
                        selection = i;
                        break;
                    }
                }
            }
        }

    } while (selection > 0 && !shutdownRUS (sdctl[selection].box, sdctl[selection].color,
                                            sdc_kbd >= 0 ? _SDC_KBC : RA8875_BLACK));

    // engage selection action
    switch (selection) {
    case _SDC_RESUME:
        initScreen();
        break;
    case _SDC_RESTART:
        if (askPasswd (_FX("restart"), true)) {
            Serial.print (_FX("Restarting\n"));
            eraseScreen();  // fast touch feedback
            doReboot();
        }
        // resume normal ops
        break;
 #if defined(_IS_UNIX)
    case _SDC_EXIT:
        if (askPasswd ("exit", true)) {
            Serial.print (_FX("Exiting\n"));
            doExit();
        }
        // resume normal ops
        break;                  // ;-)
    case _SDC_POSTDIAGS:
        postDiags();
        initScreen();
        break;
    case _SDC_REBOOT:
        if (askPasswd ("reboot", true)) {
            eraseScreen();
            selectFontStyle (BOLD_FONT, SMALL_FONT);
            drawStringInBox ("Rebooting...", sdctl[3].box, true, sdctl[3].color);
            tft.drawPR();            // forces immediate effect
            Serial.print ("Rebooting\n");
            int x = system ("sudo reboot");
            if (WIFEXITED(x) && WEXITSTATUS(x) == 0)
                doExit();
            else
                Serial.printf ("system(reboot) returns %d\n", x);
        }
        // resume normal ops
        break;
    case _SDC_SHUTDOWN:
        if (askPasswd ("shutdown", true)) {
            eraseScreen();
            selectFontStyle (BOLD_FONT, SMALL_FONT);
            drawStringInBox ("Shutting down...", sdctl[4].box, true, sdctl[4].color);
            tft.drawPR();            // forces immediate effect
            Serial.print ("Shutting down\n");
            int x = system ("sudo poweroff || sudo halt");
            if (WIFEXITED(x) && WEXITSTATUS(x) == 0)
                doExit();
            else
                Serial.printf ("system(poweroff) returns %d\n", x);
        }
        // resume normal ops
        break;
 #endif // _IS_UNIX
    default:
        fatalError (_FX("Shutdown choice: %d"), selection);
        return;

    }
}


/* reboot
 */
void doReboot()
{
    #if defined(_IS_UNIX)
        defaultState();
    #endif
    ESP.restart();
    for(;;);
}

/* do exit, as best we can
 */
void doExit()
{
    #if defined(_IS_ESP8266)
        // all we can do
        doReboot();
    #else
        Serial.printf ("doExit()\n");
        defaultState();
        #if defined(_USE_FB0)
            // X11 calls doExit on window close, so drawing would be recursive back to that thread
            eraseScreen();
        #endif
        _exit(0);
    #endif
}

/* call to display one final message, never returns
 */
void fatalError (const char *fmt, ...)
{
    // format message, accommodate really long strings
    const int mem_inc = 500;
    int mem_len = 0;
    char *msg = NULL;
    int msg_len;
    for(bool stop = false; !stop;) {
        msg = (char *) realloc (msg, mem_len += mem_inc);
        if (!msg) {
            // go back to last successful size then stop
            msg = (char *) malloc (mem_len -= mem_inc);
            if (!msg) {
                // no mem at all, at least show fmt
                Serial.printf (fmt);
                doExit();
            }
            stop = true;
        }
        va_list ap;
        va_start(ap, fmt);
        msg_len = vsnprintf (msg, mem_len, fmt, ap);
        va_end(ap);
        // stop when all fits
        if (msg_len < mem_len - 10)
            break;
    }

    // log for sure
    Serial.printf ("Fatal: %s\n", msg);

    // it may still be very early so wait a short while for display
    for (int i = 0; i < 20; i++) {
        if (tft.displayReady())
            break;
        wdDelay(100);
    }

    // nothing more unless display
    if (tft.displayReady()) {

        // fresh screen
        eraseScreen();

        // prep display
        tft.setTextColor (RA8875_WHITE);
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        const uint16_t line_h = 34;                     // line height
        const uint16_t char_w = 12;                     // char width, approx is ok
        uint16_t y = line_h;                            // walking y coord of font baseline
        tft.setCursor (0, y);

        // display msg with both line and page wrap to insure seeing end
        for (char *mp = msg; *mp; mp++) {
            if (tft.getCursorX() > tft.width() - char_w || *mp == '\n') {
                // drop down a line or wrap back to top, then erase
                if ((y += line_h) > tft.height() - 3*line_h)
                    y = line_h;
                tft.setCursor (0, y);
                tft.fillRect (0, y - line_h + 3, tft.width(), line_h, RA8875_BLACK);
            }
            if (*mp != '\n')
                tft.print (*mp);
        }

        // button font
        selectFontStyle (LIGHT_FONT, SMALL_FONT);

        #if defined(_IS_ESP8266)

            // draw box and wait for click
            SBox r_b = {350, 400, 100, 50};
            const char r_msg[] = "Restart";
            drawStringInBox (r_msg, r_b, false, RA8875_WHITE);
            drainTouch();

            for(;;) {

                SCoord s;
                while (readCalTouchWS (s) == TT_NONE)
                    wdDelay(20);

                if (inBox (s, r_b)) {
                    drawStringInBox (r_msg, r_b, true, RA8875_WHITE);
                    Serial.print (_FX("Fatal error rebooting\n"));
                    doReboot();
                }
            }

        #else

            // draw boxes and wait for click in either
            SBox screen_b;
            screen_b.x = 0;
            screen_b.y = 0;
            screen_b.w = tft.width();
            screen_b.h = tft.height();
            SBox r_b = {250, 400, 100, 50};
            SBox x_b = {450, 400, 100, 50};
            const char r_msg[] = "Restart";
            const char x_msg[] = "Exit";
            drawStringInBox (r_msg, r_b, false, RA8875_WHITE);
            drawStringInBox (x_msg, x_b, false, RA8875_WHITE);
            int kb_sel = -1;    // 0 if kb chose Restart, 1 if Exit
            drainTouch();

            SCoord s;
            char kbc;
            UserInput ui = {
                screen_b,
                NULL,
                false,
                0,
                false,
                s,
                kbc
            };

            for(;;) {

                tft.drawRect (r_b.x+3, r_b.y+3, r_b.w-6, r_b.h-6, kb_sel == 0 ? RA8875_GREEN : RA8875_BLACK);
                tft.drawRect (x_b.x+3, x_b.y+3, x_b.w-6, x_b.h-6, kb_sel == 1 ? RA8875_GREEN : RA8875_BLACK);

                (void) waitForUser(ui);

                bool kb_go = false;
                if (kbc) {
                    if (kb_sel < 0)
                        kb_sel = 0;
                    if (kbc == '\r' || kbc == '\n' || kbc == ' ')
                        kb_go = true;
                    else if (kbc == 'h' && kb_sel == 1)
                        kb_sel = 0;
                    else if (kbc == 'l' && kb_sel == 0)
                        kb_sel = 1;
                }

                if ((kb_go && kb_sel == 0) || inBox (s, r_b)) {
                    drawStringInBox (r_msg, r_b, true, RA8875_WHITE);
                    Serial.print (_FX("Fatal error: rebooting\n"));
                    doReboot();
                }

                if ((kb_go && kb_sel == 1) || inBox (s, x_b)) {
                    drawStringInBox (x_msg, x_b, true, RA8875_WHITE);
                    Serial.print (_FX("Fatal error: exiting\n"));
                    doExit();
                }
            }

        #endif

    }

    // bye bye
    free (msg);
    doExit();
}


/* return the worst offending heap and stack
 */
static int worst_heap = 900000000;
static int worst_stack;
void getWorstMem (int *heap, int *stack)
{
    *heap = worst_heap;
    *stack = worst_stack;
}

#if defined (_IS_ESP8266)

/* call with FLASH string, return pointer to RAM heap string.
 * used to save a lot of RAM for calls that only accept RAM strings, eg, printf's format 
 * N.B. we accommodate nesting these pointers only a few deep then they are reused.
 */
const char *_FX_helper(const char *flash_string)
{
    #define N_PTRS 6
    static char *ram_string[N_PTRS];
    static uint8_t nxt_i;

    // get next available pointer
    char **sp = &ram_string[nxt_i];
    nxt_i = (nxt_i + 1) % N_PTRS;

    // convert and copy 
    return strcpy_P (*sp = (char *) realloc (*sp, strlen_P(flash_string)+1),   flash_string);
}

#endif


/* log current heap and stack usage, record worst offenders
 */
void printFreeHeap (const __FlashStringHelper *label)
{
    // compute sizes
    char stack_here;
    int free_heap = ESP.getFreeHeap();
    int stack_used = stack_start - &stack_here;

    // log..
    // getFreeHeap() is close to binary search of max malloc
    // N.B. do not use getUptime here, it loops NTP
    String l(label);
    Serial.printf (_FX("MEM %s(): free heap %d, stack size %d\n"), l.c_str(), free_heap, stack_used);

    // record worst
    if (free_heap < worst_heap)
        worst_heap = free_heap;
    if (stack_used > worst_stack)
        worst_stack = stack_used;
}

/* return a high contrast text color to overlay the given background color
 * https://www.w3.org/TR/AERT#color-contrast
 */
uint16_t getGoodTextColor (uint16_t bg_col)
{
    uint8_t r = RGB565_R(bg_col);
    uint8_t g = RGB565_G(bg_col);
    uint8_t b = RGB565_B(bg_col);
    int perceived_brightness = 0.299*r + 0.587*g + 0.114*b;
    uint16_t text_col = perceived_brightness > 70 ? RA8875_BLACK : RA8875_WHITE;
    return (text_col);
}
