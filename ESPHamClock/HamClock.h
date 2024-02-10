/* HamClock glue
 */



#ifndef _HAMCLOCK_H
#define _HAMCLOCK_H


// POSIX modules
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <math.h>


#include "ArduinoLib.h"


// N.B. keep showDefines() up to date


// whether we have native IO
#if defined(_NATIVE_GPIO_ESP) || defined(_NATIVE_GPIO_FREEBSD) || defined(_NATIVE_GPIO_LINUX)
  #define _SUPPORT_NATIVE_GPIO
#endif

// Flip screen only on ESP
#if defined(_IS_ESP8266)
  #define _SUPPORT_FLIP
#endif

// kx3 on any system with NATIVE_GPIO
#if defined(_SUPPORT_NATIVE_GPIO)
  #define _SUPPORT_KX3
#endif

// phot only supported on ESP and then only if real phot is detected
#if defined(_IS_ESP8266)
  #define _SUPPORT_PHOT
#endif

// spot path plotting of any kind not on ESP because paths can't be drawn in raster mode.
// cluster spots on ESP can't be plotted because no location is available.
#if !defined(_IS_ESP8266)
    #define _SUPPORT_SPOTPATH
    #define _SUPPORT_DXCPLOT
#endif

// no scrolling on ESP
#if !defined(_IS_ESP8266)
    #define _SUPPORT_SCROLLLEN
#endif


// roaming cities is not supported on ESP because it is touch only
#if !defined(_IS_ESP8266)
    #define _SUPPORT_CITIES
#endif

// zones not on ESP -- they fit in flash ok but can't be drawn in raster fashion
#if !defined(_IS_ESP8266)
    #define _SUPPORT_ZONES
#endif

// ESP does not support reading an ADIF file
#if !defined(_IS_ESP8266)
    #define _SUPPORT_ADIFILE
#endif

// full res app, map, moon and running man sizes
#if defined(_CLOCK_1600x960)

#define HC_MAP_W (660*2)
#define HC_MAP_H (330*2)
#define HC_MOON_W (148*2)
#define HC_MOON_H (148*2)
#define HC_RUNNER_W (13*2)
#define HC_RUNNER_H (20*2)
#define BUILD_W 1600
#define BUILD_H 960

#elif defined(_CLOCK_2400x1440)

#define HC_MAP_W (660*3)
#define HC_MAP_H (330*3)
#define HC_MOON_W (148*3)
#define HC_MOON_H (148*3)
#define HC_RUNNER_W (13*3)
#define HC_RUNNER_H (20*3)
#define BUILD_W 2400
#define BUILD_H 1440

#elif defined(_CLOCK_3200x1920)

#define HC_MAP_W (660*4)
#define HC_MAP_H (330*4)
#define HC_MOON_W (148*4)
#define HC_MOON_H (148*4)
#define HC_RUNNER_W (13*4)
#define HC_RUNNER_H (20*4)
#define BUILD_W 3200
#define BUILD_H 1920

#else   // original size

#define HC_MAP_W 660
#define HC_MAP_H 330
#define HC_MOON_W 148
#define HC_MOON_H 148
#define HC_RUNNER_W 13
#define HC_RUNNER_H 20
#define BUILD_W 800
#define BUILD_H 480

#endif

// canonical map size 
#define EARTH_H   330
#define EARTH_W   660

#if defined(_IS_UNIX)
#include <signal.h>
#include <sys/file.h>
#endif // _IS_UNIX

// see Adafruit_RA8875.h
#define USE_ADAFRUIT_GFX_FONTS

// community modules
#include <Arduino.h>
#include <TimeLib.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <IPAddress.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#include <LittleFS.h>

// screen coordinates, upper left at [0,0]
typedef struct {
    uint16_t x, y;
} SCoord;
#include "Adafruit_RA8875_R.h"
#include "Adafruit_MCP23X17.h"

// HamClock modules
#include "calibrate.h"
#include "version.h"
#include "P13.h"




// handy nelements in array
// N.B. call with real array, not a pointer
#define NARRAY(a)       ((int)(sizeof(a)/sizeof(a[0])))

// handy range clamp
#define CLAMPF(v,minv,maxv)      fmaxf(fminf((v),(maxv)),(minv))

// handy microseconds difference in two struct timeval: t1 - t0
#define TVDELUS(t0,t1)    ((t1.tv_sec-t0.tv_sec)*1000000 + (t1.tv_usec-t0.tv_usec))

// float versions
#define M_PIF   3.14159265F
#define M_PI_2F (M_PIF/2)

#define deg2rad(d)      ((M_PIF/180)*(d))
#define rad2deg(d)      ((180/M_PIF)*(d))


// time to leave new DX path up, millis()
#define DXPATH_LINGER   20000   

// path segment length, degrees
#define PATH_SEGLEN     2

// default menu timeout, millis
#define MENU_TO         30000

// maidenhead character arrey length, including EOS
#define MAID_CHARLEN     7


/* handy malloc wrapper that frees automatically when leaves scope
 */
class StackMalloc 
{
    public:

        StackMalloc (size_t nbytes) {
            // printf ("SM: new %lu\n", nbytes);
            mem = (char *) malloc (nbytes);
            siz = nbytes;
        }

        StackMalloc (const char *string) {
            // printf ("SM: new %s\n", string);
            mem = (char *) strdup (string);
            siz = strlen(string) + 1;
        }

        ~StackMalloc (void) {
            // printf ("SM: free(%d)\n", siz);
            free (mem);
        }

        size_t getSize(void) {
            return (siz);
        }

        void *getMem(void) {
            return (mem);
        }

    private:

        char *mem;
        size_t siz;
};


// handy temperature conversions
#define FAH2CEN(f)      ((5.0F/9.0F)*((f) - 32.0F))
#define CEN2FAH(c)      ((9.0F/5.0F)*(c) + 32.0F)

/* time styles in auxtime_b
 */
#define AUXTIMES                        \
    X(AUXT_DATE,        "Date")         \
    X(AUXT_DOY,         "Day of Year")  \
    X(AUXT_JD,          "Julian Date")  \
    X(AUXT_MJD,         "Modified JD")  \
    X(AUXT_SIDEREAL,    "Sidereal")     \
    X(AUXT_SOLAR,       "Solar")        \
    X(AUXT_UNIX,        "UNIX seconds")

#define X(a,b) a,               // expands AUXTIME to each enum and comma
typedef enum {
    AUXTIMES
    AUXT_N
} AuxTimeFormat;
#undef X

extern AuxTimeFormat auxtime;
extern const char *auxtime_names[AUXT_N];


/* plot choices and pane locations
 */

// N.B. take care that names will fit in menu built by askPaneChoice()
// N.B. names should not include blanks, but _ are changed to blanks for prettier printing
#define PLOTNAMES \
    X(PLOT_CH_BC,           "VOACAP")           \
    X(PLOT_CH_DEWX,         "DE_Wx")            \
    X(PLOT_CH_DXCLUSTER,    "DX_Cluster")       \
    X(PLOT_CH_DXWX,         "DX_Wx")            \
    X(PLOT_CH_FLUX,         "Solar_Flux")       \
    X(PLOT_CH_KP,           "Planetary_K")      \
    X(PLOT_CH_MOON,         "Moon")             \
    X(PLOT_CH_NOAASWX,      "Space_Wx")         \
    X(PLOT_CH_SSN,          "Sunspot_N")        \
    X(PLOT_CH_XRAY,         "X-Ray")            \
    X(PLOT_CH_GIMBAL,       "Rotator")          \
    X(PLOT_CH_TEMPERATURE,  "ENV_Temp")         \
    X(PLOT_CH_PRESSURE,     "ENV_Press")        \
    X(PLOT_CH_HUMIDITY,     "ENV_Humid")        \
    X(PLOT_CH_DEWPOINT,     "ENV_DewPt")        \
    X(PLOT_CH_SDO,          "SDO")              \
    X(PLOT_CH_SOLWIND,      "Solar_Wind")       \
    X(PLOT_CH_DRAP,         "DRAP")             \
    X(PLOT_CH_COUNTDOWN,    "Countdown")        \
    X(PLOT_CH_CONTESTS,     "Contests")         \
    X(PLOT_CH_PSK,          "Live_Spots")       \
    X(PLOT_CH_BZBT,         "Bz_Bt")            \
    X(PLOT_CH_POTA,         "POTA")             \
    X(PLOT_CH_SOTA,         "SOTA")             \
    X(PLOT_CH_ADIF,         "ADIF")

#define X(a,b)  a,              // expands PLOTNAMES to each enum and comma
typedef enum {
    PLOTNAMES
    PLOT_CH_N
} PlotChoice;
#undef X

// reuse count also handy flag for not found
#define PLOT_CH_NONE    PLOT_CH_N

typedef enum {
    PANE_1,
    PANE_2,
    PANE_3,
    PANE_N
} PlotPane;

// reuse count also handy flag for not found
#define PANE_NONE       PANE_N



#define N_NOAASW_C      3       // n categories : R, S and G
#define N_NOAASW_V      4       // values per cat : current and 3 days predictions
typedef struct {
    char cat[N_NOAASW_C];
    int val[N_NOAASW_C][N_NOAASW_V];
} NOAASpaceWx;

typedef struct {
    float value;                // from pane update
    time_t age;                 // secs old
} SPWxValue;



// screen coords of box ul and size
typedef struct {
    uint16_t x, y, w, h;
} SBox;

// screen center, radius
typedef struct {
    SCoord s;
    uint16_t r;
} SCircle;

// timezone info
typedef struct {
    SBox box;
    uint16_t color;
    int32_t tz_secs;
} TZInfo;



// callsign info
typedef struct {
    char *call;                         // malloced callsign
    uint16_t fg_color;                  // fg color
    uint16_t bg_color;                  // bg color unless ..
    uint8_t bg_rainbow;                 // .. bg rainbow?
    SBox box;                           // size and location
} CallsignInfo;
extern CallsignInfo cs_info;

// map lat, lng, + radians N and E
typedef struct {
    float lat, lng;                     // radians north, east
    float lat_d, lng_d;                 // degrees +N +E
} LatLong;

#define LIFE_LED        0

#define DE_INFO_ROWS    3               // n text rows in DE pane -- not counting top row
#define DX_INFO_ROWS    5               // n text rows in DX pane


extern Adafruit_RA8875_R tft;           // compat layer
extern Adafruit_MCP23X17 mcp;           // I2C digital IO device
extern bool found_mcp;                  // whether found
extern TZInfo de_tz, dx_tz;             // time zone info
extern SBox NCDXF_b;                    // NCDXF box, and more

#define PLOTBOX_W 160                   // common plot box width
#define PLOTBOX_H 148                   // common plot box height, ends just above map border
extern SBox sensor_b;

extern SBox clock_b;                    // main time
extern SBox auxtime_b;                  // extra time 
extern SCircle satpass_c;               // satellite pass horizon

extern SBox rss_bnr_b;                  // rss banner button
extern uint8_t rss_on;                  // rss on/off
extern uint8_t night_on;                // show night portion of map on/off
extern uint8_t names_on;                // show place names when roving

extern SBox desrss_b, dxsrss_b;         // sun rise/set display
extern uint8_t desrss, dxsrss;          // sun rise/set chpice
enum {
    DXSRSS_INAGO,                       // display time from now
    DXSRSS_ATAT,                        // display local time
    DXSRSS_PREFIX,                      // must be last
    DXSRSS_N,
};

// show NCDXF beacons or one of other controls
// N.B. names must fit within NCDXF_b
#define BRBMODES                  \
    X(BRB_SHOW_BEACONS, "NCDXF")  \
    X(BRB_SHOW_ONOFF,   "On/Off") \
    X(BRB_SHOW_PHOT,    "PhotoR") \
    X(BRB_SHOW_BR,      "Brite")  \
    X(BRB_SHOW_SWSTATS, "Spc Wx") \
    X(BRB_SHOW_BME76,   "BME@76") \
    X(BRB_SHOW_BME77,   "BME@77") \
    X(BRB_SHOW_DXWX,    "DX Wx")  \
    X(BRB_SHOW_DEWX,    "DE Wx")

#define X(a,b)  a,                      // expands BRBMODES to enum and comma
typedef enum {
    BRBMODES
    BRB_N                               // count
} BRB_MODE;
#undef X

extern uint8_t brb_mode;                // one of BRB_MODE
extern time_t brb_updateT;              // time at which to update
extern uint16_t brb_rotset;             // bitmask of all active BRB_MODE choices
                                        // N.B. brb_rotset must always include brb_mode
#define BRBIsRotating()                 ((brb_rotset & ~(1 << brb_mode)) != 0)  // any bits other than mode
extern const char *brb_names[BRB_N];    // menu names -- must be in same order as BRB_MODE

// map projection styles
extern uint8_t map_proj;

#define MAPPROJS \
    X(MAPP_MERCATOR,  "Mercator")  \
    X(MAPP_AZIMUTHAL, "Azimuthal") \
    X(MAPP_AZIM1,     "Azim One")  \
    X(MAPP_ROB,       "Robinson")

#define X(a,b)  a,                      // expands MAPPROJS to enum plus comma
typedef enum {
    MAPPROJS
    MAPP_N
} MapProjection;
#undef X

extern const char *map_projnames[MAPP_N];   // projection names

#define AZIM1_ZOOM       1.1F           // horizon will be 180/AZIM1_ZOOM degrees from DE
#define AZIM1_FISHEYE    1.15F          // center zoom -- 1 is natural

// map grid options
typedef enum {
    MAPGRID_OFF,
    MAPGRID_TROPICS,
    MAPGRID_LATLNG,
    MAPGRID_MAID,
    MAPGRID_AZIM,
#if defined(_SUPPORT_ZONES)
    MAPGRID_CQZONES,
    MAPGRID_ITUZONES,
#endif
    MAPGRID_N
} MapGridStyle;
extern uint8_t mapgrid_choice;
extern const char *grid_styles[MAPGRID_N];

extern SBox dx_info_b;                  // dx info pane
extern SBox satname_b;                  // satellite name pick
extern SBox de_info_b;                  // de info pane
extern SBox map_b;                      // main map 
extern SBox view_btn_b;                 // map view menu button
extern SBox dx_maid_b;                  // dx maidenhead pick
extern SBox de_maid_b;                  // de maidenhead pick
extern SBox lkscrn_b;                   // screen lock icon button

extern SBox skip_b;                     // common "Skip" button


// size and location of maidenhead labels
#define MH_TR_H  9                      // top row background height
#define MH_TR_DX 2                      // top row char cell x indent
#define MH_TR_DY 1                      // top row char cell y down
#define MH_RC_W  8                      // right columns background width
#define MH_RC_DX 1                      // right column char cell x indent
#define MH_RC_DY 5                      // right column char cell y down


// ESP mechanism to save lots of RAM by storing what appear to be RAM strings in FLASH
#if defined (_IS_ESP8266)
#define _FX(x)  _FX_helper (PSTR(x))
extern const char *_FX_helper(const char *flash_string);
#else
#define _FX(x)          x
#define _FX_helper(x)   x
#endif

#define RSS_BG_COLOR    RGB565(0,40,80) // RSS banner background color
#define RSS_FG_COLOR    RA8875_WHITE    // RSS banner text color
#define RSS_DEF_INT     15              // RSS default interval, secs
#define RSS_MIN_INT     5               // RSS minimum interval, secs

extern char *stack_start;               // used to estimate stack usage

#define MAX_PREF_LEN     4              // maximumm prefix length


// touch screen actions
typedef enum {
    TT_NONE,                            // no touch event
    TT_TAP,                             // brief touch event
    TT_HOLD,                            // at least TOUCH_HOLDT
} TouchType;



typedef struct {
    char city[32];
    float temperature_c;
    float humidity_percent;
    float pressure_hPa;                 // sea level
    float wind_speed_mps;
    char wind_dir_name[4];
    char clouds[32];
    char conditions[32];
    char attribution[32];
    int8_t pressure_chg;                // < = > 0
} WXInfo;
#define N_WXINFO_FIELDS 10


// cursor distance to map point
#define MAX_CSR_DIST    150             // miles


// pane title height
#define PANETITLE_H        27


/*********************************************************************************************
 *
 * ESPHamClock.ino
 *
 */


extern void drawDXTime(void);
extern void drawDXMarker(bool force);
extern void drawAllSymbols(bool beacons_too);
extern void drawTZ(const TZInfo &tzi);
extern bool inBox (const SCoord &s, const SBox &b);
extern bool inCircle (const SCoord &s, const SCircle &c);
extern bool boxesOverlap (const SBox &b1, const SBox &b2);
extern void doReboot(void);
extern void printFreeHeap (const __FlashStringHelper *label);
extern void getWorstMem (int *heap, int *stack);
extern void resetWatchdog(void);
extern void wdDelay(int ms);
extern bool timesUp (uint32_t *prev, uint32_t dt);
extern void setDXPathInvalid(void);
extern const SCoord raw2appSCoord (const SCoord &s_raw);
extern bool overMap (const SCoord &s);
extern bool overMap (const SBox &b);
extern bool overRSS (const SCoord &s);
extern bool overRSS (const SBox &b);
extern void setScreenLock (bool on);
extern bool checkCallsignTouchFG (SCoord &b);
extern bool checkCallsignTouchBG (SCoord &b);
extern void newDE (LatLong &ll, const char grid[MAID_CHARLEN]);
extern void newDX (LatLong &ll, const char grid[MAID_CHARLEN], const char *override_prefix);
extern void drawDXPath(void);
extern void getTextBounds (const char str[], uint16_t *wp, uint16_t *hp);
extern uint16_t getTextWidth (const char str[]);
extern void normalizeLL (LatLong &ll);
extern bool screenIsLocked(void);
extern time_t getUptime (uint16_t *days, uint8_t *hrs, uint8_t *mins, uint8_t *secs);
extern void eraseScreen(void);
extern void setMapTagBox (const char *tag, const SCoord &c, uint16_t r, SBox &box);
extern void drawMapTag (const char *tag, const SBox &box, uint16_t txt_color = RA8875_WHITE,
        uint16_t bg_color = RA8875_BLACK);
extern void setDXPrefixOverride (char p[MAX_PREF_LEN]);
extern bool getDXPrefix (char p[MAX_PREF_LEN+1]);
extern void drawScreenLock(void);
extern void call2Prefix (const char *call, char prefix[MAX_PREF_LEN]);
extern void setOnAir (bool on);
extern void getDefaultCallsign(void);
extern void drawCallsign (bool all);
extern const char *hc_version;
extern void fillSBox (const SBox &box, uint16_t color);
extern void drawSBox (const SBox &box, uint16_t color);
extern void shadowString (const char *str, bool shadow, uint16_t color, uint16_t x0, uint16_t y0);
extern bool overMapScale (const SCoord &s);
extern uint16_t getGoodTextColor (uint16_t bg_c);



#if defined(__GNUC__)
extern void tftMsg (bool verbose, uint32_t dwell_ms, const char *fmt, ...) __attribute__ ((format (__printf__, 3, 4)));
#else
extern void tftMsg (bool verbose, uint32_t dwell_ms, const char *fmt, ...);
#endif

#if defined(__GNUC__)
extern void fatalError (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));
#else
extern void fatalError (const char *fmt, ...);
#endif







/*********************************************************************************************
 *
 * OTAupdate.cpp
 *
 */

extern bool newVersionIsAvailable (char *nv, uint16_t nvl);
extern bool askOTAupdate(char *ver);
extern void doOTAupdate(const char *ver);





/*********************************************************************************************
 *
 * adif.cpp
 *
 */


// DXClusterSpot used in several places
#define MAX_SPOTCALL_LEN                12      // including \0
#define MAX_SPOTGRID_LEN                MAID_CHARLEN
#define MAX_SPOTMODE_LEN                8
typedef struct {
    char de_call[MAX_SPOTCALL_LEN];     // DE call
    char dx_call[MAX_SPOTCALL_LEN];     // DX call
    char de_grid[MAX_SPOTGRID_LEN];     // DE grid
    char dx_grid[MAX_SPOTGRID_LEN];     // DX grid
    float dx_lat, dx_lng;               // dx location, rads +N +E
    float de_lat, de_lng;               // de location, rads +N +E
    char mode[MAX_SPOTMODE_LEN];        // operating mode
    float kHz;                          // freq
    union {
        SBox map_b;                     // DX map text label location, canonical coords like text
        SCircle map_c;                  // DX map dot location, RAW coords
    } dx_map;                           // use map_b iff labelSpot() else map_c
    time_t spotted;                     // UTC when spotted
} DXClusterSpot;


extern bool from_set_adif;
extern void updateADIF (const SBox &box);
extern bool checkADIFTouch (const SCoord &s, const SBox &box);
extern void drawADIFSpotsOnMap (void);
extern int readADIFWiFiClient (WiFiClient &client, long content_length, char ynot[], int n_ynot);
extern bool getClosestADIFSpot (const LatLong &ll, DXClusterSpot *sp, LatLong *llp);
extern void checkADIF(void);

#if defined(_IS_ESP8266)
extern bool overAnyADIFSpots(const SCoord &s);
#endif





/*********************************************************************************************
 *
 * asknewpos.cpp
 *
 */

extern bool askNewPos (const SBox &b, LatLong &ll, char grid[MAID_CHARLEN]);




/*********************************************************************************************
 *
 * askpasswd.cpp
 *
 */

extern bool askPasswd (const char *category, bool restore);




/*********************************************************************************************
 *
 * astro.cpp
 *
 */

typedef struct {
    float az, el;               // topocentric, rads
    float ra, dec;              // geocentric EOD, rads
    float gha;                  // geocentric rads
    float dist;                 // geocentric km
    float vel;                  // topocentric m/s
    float phase;                // rad angle from new
} AstroCir;

extern AstroCir lunar_cir, solar_cir;

extern void now_lst (double mjd, double lng, double *lst);
extern void getLunarCir (time_t t0, const LatLong &ll, AstroCir &cir);
extern void getSolarCir (time_t t0, const LatLong &ll, AstroCir &cir);
extern void getSolarRS (const time_t t0, const LatLong &ll, time_t *riset, time_t *sett);
extern void getLunarRS (const time_t t0, const LatLong &ll, time_t *riset, time_t *sett);

#define SECSPERDAY              (3600*24L)      // seconds per day
#define MINSPERDAY              (24*60)         // minutes per day
#define DAYSPERWEEK             7               // days per week



/*********************************************************************************************
 *
 * blinker.cpp
 *
 */

#define BLINKER_OFF_HZ  (-1)    // special hz to mean constant off
#define BLINKER_ON_HZ   0       // speacial hz to mean constant on

typedef struct {
    int pin;                    // pin number
    int hz;                     // blink rate or one of BLINKER_*
    bool on_is_low;             // whether "on" means drive LOW
    bool started;               // set when an attempt was made to start the service
    bool disable;               // set to stop the thread
} ThreadBlinker;

extern void startBinkerThread (volatile ThreadBlinker &tb, int pin, bool on_is_low);
extern void setBlinkerRate (volatile ThreadBlinker &tb, int hz);
extern void disableBlinker (volatile ThreadBlinker &tb);

typedef struct {
    int pin;                    // pin number
    int hz;                     // blink rate or one of BLINKER_*
    bool started;               // set when an attempt was made to start the service
    bool disable;               // set to stop the thread
    bool value;                 // latest value
} MCPPoller;

extern void startMCPPoller (volatile MCPPoller &mp, int pin, int hz);
extern void disableMCPPoller (volatile MCPPoller &mp);
extern bool readMCPPoller (volatile const MCPPoller &mp);



/*********************************************************************************************
 *
 * brightness.cpp
 *
 */


extern void drawBrightness (void);
extern void initBrightness (void);
extern void setupBrightness (void);
extern void followBrightness (void);
extern bool brightnessOn(void);
extern void brightnessOff(void);
extern bool setDisplayOnOffTimes (int dow, uint16_t on, uint16_t off, int &idle);
extern bool getDisplayOnOffTimes (int dow, uint16_t &on, uint16_t &off);
extern bool getDisplayInfo (uint16_t &percent, uint16_t &idle_min, uint16_t &idle_left_sec);
extern void setFullBrightness(void);
extern bool brDimmableOk(void);
extern bool brOnOffOk(void);
extern bool found_phot, found_ltr;





/*********************************************************************************************
 *
 * cities.cpp
 *
 */
extern void readCities(void);
extern const char *getNearestCity (const LatLong &ll, LatLong &city_ll, int &max_l);




/*********************************************************************************************
 *
 * clocks.cpp
 *
 */


// DETIME options
#define DETIMES                                   \
    X(DETIME_INFO,         "All info")            \
    X(DETIME_ANALOG,       "Simple analog")       \
    X(DETIME_CAL,          "Calendar")            \
    X(DETIME_ANALOG_DTTM,  "Annotated analog")    \
    X(DETIME_DIGITAL_12,   "Digital 12 hour")     \
    X(DETIME_DIGITAL_24,   "Digital 24 hour")

#define X(a,b)  a,                      // expands DETIMES to each enum followed by comma
enum {
    DETIMES
    DETIME_N
};
#undef X

extern const char *detime_names[DETIME_N];

extern uint8_t de_time_fmt;     // one of DETIME_*
extern void initTime(void);
extern time_t nowWO(void);
extern time_t myNow(void);
extern void updateClocks(bool all);
extern bool clockTimeOk(void);
extern void changeTime (time_t t);
extern bool checkClockTouch (SCoord &s);
extern bool TZMenu (TZInfo &tzi, const LatLong &ll);
extern void drawDESunRiseSetInfo(void);
extern void drawCalendar(bool force);
extern void hideClocks(void);
extern void showClocks(void);
extern void drawDXSunRiseSetInfo(void);
extern int DEWeekday(void);
extern int32_t utcOffset(void);
extern const char *gpsd_server, *ntp_server;
extern void formatSexa (float dt_hrs, int &a, char &sep, int &b);
extern char *formatAge4 (time_t age, char *line, int line_l);
extern bool crackMonth (const char *name, int *monp);





/*********************************************************************************************
 *
 * color.cpp
 *
 */

// convert 8-bit each (R,G,B) to 5R : 6G : 5G
// would expect this to be in graphics lib but can't find it...
#define RGB565(R,G,B)   ((((uint16_t)(R) & 0xF8) << 8) | (((uint16_t)(G) & 0xFC) << 3) | ((uint16_t)(B) >> 3))

// extract 8-bit colors from uint16_t RGB565 color in range 0-255
#define RGB565_R(c)     (255*(((c) & 0xF800) >> 11)/((1<<5)-1))
#define RGB565_G(c)     (255*(((c) & 0x07E0) >> 5)/((1<<6)-1))
#define RGB565_B(c)     (255*((c) & 0x001F)/((1<<5)-1))

#define GRAY    RGB565(140,140,140)
#define BRGRAY  RGB565(200,200,200)
#define DKGRAY  RGB565(50,50,50)
#define DYELLOW RGB565(255,212,112)

extern void hsvtorgb(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t h, uint8_t s, uint8_t v);
extern void rgbtohsv(uint8_t *h, uint8_t *s, uint8_t *v, uint8_t r, uint8_t g, uint8_t b);
extern uint16_t HSV565 (uint8_t h, uint8_t s, uint8_t v);






/*********************************************************************************************
 *
 * contests.cpp
 *
 */

extern bool updateContests (const SBox &box);
extern bool checkContestsTouch (const SCoord &s, const SBox &box);
extern int getContests (char **credp, char ***conppp);






/*********************************************************************************************
 *
 * dxcluster.cpp
 *
 */

#define SPOTMRNOP    (tft.SCALESZ+4)    // raw spot marker radius when no path
#define DXSUBTITLE_Y0     32            // sub title y down from box top
#define DXLISTING_Y0      47            // first spot y down from box top
#define DXLISTING_DY      14            // listing row separation
#define DXMAX_VIS    ((PLOTBOX_H-DXLISTING_Y0)/DXLISTING_DY)   // number of visible spots in pane



extern bool updateDXCluster(const SBox &box);
extern void closeDXCluster(void);
extern bool checkDXClusterTouch (const SCoord &s, const SBox &box);
extern bool getDXClusterSpots (DXClusterSpot **spp, uint8_t *nspotsp);
extern void drawDXClusterSpotsOnMap (void);
extern void updateDXClusterSpotScreenLocations(void);
extern bool isDXClusterConnected(void);
extern bool sendDXClusterDELLGrid(void);
extern bool getClosestDXCluster (const LatLong &ll, DXClusterSpot *sp, LatLong *llp);

extern void drawDXCLabelOnMap (const DXClusterSpot &spot);
extern bool getClosestDXC (const DXClusterSpot *list, int n_list, const LatLong &ll,
    DXClusterSpot *sp, LatLong *llp);
extern void setDXCSpotPosition (DXClusterSpot &s);
extern void getRawSpotSizes (uint16_t &lwRaw, uint16_t &mkRaw);
extern void drawSpotOnList (const SBox &box, const DXClusterSpot &spot, int row);
extern void drawDXPathOnMap (const DXClusterSpot &spot);
extern bool onDXWatchList (const char *call);










/*********************************************************************************************
 *
 * earthmap.cpp
 *
 */



#define DX_R    6                       // dx marker radius (erases better if even)
#define DX_COLOR RA8875_GREEN

extern SCircle dx_c;
extern LatLong dx_ll;

extern uint16_t map_x0, map_y0;
extern uint16_t map_w, map_h;

extern bool mapmenu_pending;            // draw map menu at next opportunity
extern uint8_t show_lp;                 // show prop long path, else short path
#define ERAD_M          3959.0F         // earth radius, miles
#define MI_PER_KM       0.621371F
#define KM_PER_MI       1.609344F

#define DE_R 6                          // radius of DE marker   (erases better if even)
#define DEAP_R 6                        // radius of DE antipodal marker (erases better if even)
#define DE_COLOR  RGB565(255,125,0)     // orange

extern SCircle de_c;
extern LatLong de_ll;
extern float sdelat, cdelat;
extern SCircle deap_c;
extern LatLong deap_ll;
extern LatLong sun_ss_ll;
extern LatLong moon_ss_ll;

#define SUN_R 6                         // radius of sun marker
extern float sslng, sslat, csslat, ssslat;
extern SCircle sun_c;

#define MOON_R 6                        // radius of moon marker
#define MOON_COLOR  RGB565(150,150,150)
extern SCircle moon_c;

extern uint32_t max_wd_dt;
extern uint8_t flash_crc_ok;

extern void drawMoreEarth (void);
extern void eraseDEMarker (void);
extern void eraseDEAPMarker (void);
extern void drawDEMarker (bool force);
extern bool showDEAPMarker(void);
extern bool showDXMarker(void);
extern bool showDEMarker(void);
extern void drawDEAPMarker (void);
extern void drawDEInfo (void);
extern void drawDECalTime (bool center);
extern void drawDXTime (void);
extern void initEarthMap (void);
extern void antipode (LatLong &to, const LatLong &from);
extern void drawMapCoord (const SCoord &s);
extern void drawMapCoord (uint16_t x, uint16_t y);
extern void drawSun (void);
extern void drawMoon (void);
extern void drawDXInfo (void);
extern void ll2s (const LatLong &ll, SCoord &s, uint8_t edge);
extern void ll2s (float lat, float lng, SCoord &s, uint8_t edge);
extern void ll2sRaw (const LatLong &ll, SCoord &s, uint8_t edge);
extern void ll2sRaw (float lat, float lng, SCoord &s, uint8_t edge);
extern bool s2ll (uint16_t x, uint16_t y, LatLong &ll);
extern bool s2ll (const SCoord &s, LatLong &ll);
extern void solveSphere (float A, float b, float cc, float sc, float *cap, float *Bp);
extern bool checkPathDirTouch (const SCoord &s);
extern void propDEPath (bool long_path, const LatLong &to_ll, float *distp, float *bearp);
extern void propPath (bool long_path, const LatLong &from_ll, float sflat, float cflat, const LatLong &to_ll,
        float *distp, float *bearp);
extern bool waiting4DXPath(void);
extern void eraseSCircle (const SCircle &c);
extern void drawRSSBox (void);
extern void eraseRSSBox (void);
extern void drawMapMenu(void);
extern void roundLatLong (LatLong &ll);
extern void initScreen(void);
extern bool checkOnAir(void);
extern float lngDiff (float dlng);
extern bool overViewBtn (const SCoord &s, uint16_t border);
extern bool segmentSpanOk (const SCoord &s0, const SCoord &s1, uint16_t border);
extern bool segmentSpanOkRaw (const SCoord &s0, const SCoord &s1, uint16_t border);
extern bool desiredBearing (const LatLong &ll, float &bear);






/*********************************************************************************************
 *
 * BME280.cpp
 *
 */

// pack into int16_t to save almost 2 kB on ESP

#define BMEPACK_T(t)            (round((t)*50))
#define BMEPACK_P(p)            (useMetricUnits() ? round((p)*10) : round((p)*100))
#define BMEPACK_H(h)            (round((h)*100))

#define BMEUNPACK_T(t)          ((t)/50.0F)
#define BMEUNPACK_P(p)          (useMetricUnits() ? ((p)*0.1F) : ((p)*0.01F))
#define BMEUNPACK_H(h)          ((h)/100.0F)

// measurement queues
#if defined(_IS_ESP)
#define N_BME_READINGS          100     // n measurements stored for each sensor
#else
#define N_BME_READINGS          250     // n measurements stored for each sensor
#endif
typedef struct {
    time_t u[N_BME_READINGS];           // circular queue of UNIX sensor read times, 0 if no data
    int16_t t[N_BME_READINGS];          // circular queue of temperature values as per useMetricUnits()
    int16_t p[N_BME_READINGS];          // circular queue of pressure values as per useMetricUnits()
    int16_t h[N_BME_READINGS];          // circular queue of humidity values
    uint8_t q_head;                     // index of next q entries to use
    uint8_t i2c;                        // i2c addr
} BMEData;

typedef enum {
    BME_76,                             // index for sensor at 0x76
    BME_77,                             // index for sensor at 0x77
    MAX_N_BME                           // max sensors connected
} BMEIndex; 

extern void initBME280 (void);
extern void readBME280 (void);
extern void drawBMEStats (void);
extern void drawBME280Panes(void);
extern void drawOneBME280Pane (const SBox &box, PlotChoice ch);
extern bool newBME280data (void);
extern const BMEData *getBMEData (BMEIndex i, bool fresh_read);
extern int getNBMEConnected (void);
extern float dewPoint (float T, float RH);
extern void doBMETouch (const SCoord &s);
extern bool recalBMETemp (BMEIndex device, float new_corr);
extern bool recalBMEPres (BMEIndex device, float new_corr);




/*********************************************************************************************
 *
 * earthsat.cpp
 *
 */

#define NV_SATNAME_LEN          9

typedef struct _sat_now {
    char name[NV_SATNAME_LEN];          // name
    float az, el;                       // az, el degs
    float range, rate;                  // km, m/s + receding
    float raz, saz;                     // rise and set az, degs; either may be SAT_NOAZ
    float rdt, sdt;                     // next rise and set, hrs from now; rdt < 0 if up now
    _sat_now() { name[0] = '\0'; }      // constructor to insure name properly empty
} SatNow;
#define SAT_NOAZ        (-999)          // error flag for raz or saz
#define SAT_MIN_EL      0.0F            // min elevation
#define TLE_LINEL       70              // TLE line length, including EOS

extern void updateSatPath(void);
extern void drawSatPathAndFoot(void);
extern void updateSatPass(void);
extern bool querySatSelection(void);
extern void strncpySubChar (char to_str[], const char from_str[], char to_char, char from_char, int maxlen);
extern bool checkSatMapTouch (const SCoord &s);
extern bool checkSatNameTouch (const SCoord &s);
extern void drawSatPass(void);
extern bool setNewSatCircumstance (void);
extern void drawSatPointsOnRow (uint16_t r);
extern void drawSatNameOnRow(uint16_t y);
extern void drawOneTimeDX(void);
extern void drawOneTimeDE(void);
extern bool setSatFromName (const char *new_name);
extern bool setSatFromTLE (const char *name, const char *t1, const char *t2);
extern bool initSatSelection(void);
extern bool getSatNow (SatNow &satnow);
extern bool isNewPass(void);
extern bool isSatMoon(void);
extern const char **getAllSatNames(void);
extern int nextSatRSEvents (time_t **rises, float **raz, time_t **sets, float **saz);
extern bool isSatDefined(void);
extern void drawDXSatMenu(const SCoord &s);
extern bool dx_info_for_sat;
extern void satResetIO(void);





/*********************************************************************************************
 *
 * favicon.cpp
 *
 */

#if defined(_IS_UNIX)

extern void writeFavicon (FILE *fp);

#endif // _IS_UNIX





/*********************************************************************************************
 *
 * gimbal.cpp
 *
 */

extern bool haveGimbal(void);
extern void updateGimbal (const SBox &box);
extern bool checkGimbalTouch (const SCoord &s, const SBox &box);
extern void stopGimbalNow(void);
extern void closeGimbal(void);
extern bool getGimbalState (bool &connected, bool &vis_now, bool &has_el, bool &is_stop, bool &is_auto,
    float &az, float &el);
extern bool commandRotator (const char *new_state, const char *new_az, const char *new_el, char ynot[]);







/*********************************************************************************************
 *
 * gpsd.cpp
 *
 */

extern bool getGPSDLatLong(LatLong *llp);
extern time_t getGPSDUTC(const char **server);
extern void updateGPSDLoc(void);
extern time_t crackISO8601 (const char *iso);





/*********************************************************************************************
 *
 * kd3tree.cpp
 *
 */

struct kd_node_t {
    float s[3];                         // xyz coords on unit sphere
    struct kd_node_t *left, *right;     // branches
    void *data;                         // user data
};

typedef struct kd_node_t KD3Node;

extern KD3Node* mkKD3NodeTree (KD3Node *t, int len, int idx);
extern void nearestKD3Node (KD3Node *root, KD3Node *nd, int idx, KD3Node **best, float *best_dist,
    int *n_visited);
extern void ll2KD3Node (const LatLong &ll, KD3Node *kp);
extern void KD3Node2ll (const KD3Node &n, LatLong *llp);
extern float nearestKD3Dist2Miles(float d);





/*********************************************************************************************
 *
 * liveweb-html.cpp
 *
 */

extern char live_html[];



/*********************************************************************************************
 *
 * liveweb.cpp
 *
 */


extern void initLiveWeb(bool verbose);
extern bool liveweb_fs_ready;
extern time_t last_live;



/*********************************************************************************************
 *
 * robinson.cpp
 *
 */

extern void ll2sRobinson (const LatLong &ll, SCoord &s, int edge, int scalesz);
extern bool s2llRobinson (const SCoord &s, LatLong &ll);
extern float RobLat2G (const float lat_d);







/*********************************************************************************************
 *
 * scroll.cpp
 *
 */

/* info and methods to control scrolling
 */
class ScrollState {

    public:

        // this allows initializing using {} set
        ScrollState (int mv, int tv, int nd) {
            max_vis = mv;
            top_vis = tv;
            n_data = nd;
        };

        void drawScrollUpControl (const SBox &box, uint16_t color) const;
        void drawScrollDownControl (const SBox &box, uint16_t color) const;

        bool checkScrollUpTouch (const SCoord &s, const SBox &b) const;
        bool checkScrollDownTouch (const SCoord &s, const SBox &b) const;

        virtual void scrollDown (void);
        virtual void scrollUp (void);
        bool okToScrollDown (void) const;
        bool okToScrollUp (void) const;

        virtual int nMoreAbove (void) const;
        virtual int nMoreBeneath (void) const;
        void scrollToNewest (void);
        bool findDataIndex (int display_row, int &array_index) const;
        int getVisIndices (int &min_i, int &max_i) const;
        int getDisplayRow (int array_index) const;

        int max_vis;        // maximum rows in the displayed list
        int top_vis;        // index into the data array being dislayed at the top of the list
        int n_data;         // the number of entries in the data array

    private:

        void moveTowardsOlder();
        void moveTowardsNewer();
};


extern void strtolower (char *str);
extern void strtoupper (char *str);



/*********************************************************************************************
 *
 * setup.cpp
 *
 */


typedef enum {
    DF_MDY,
    DF_DMY,
    DF_YMD,
    DF_N
} DateFormat;

#define N_DXCLCMDS      4                       // n dx cluster user commands
#define THINPATHSZ      ((tft.SCALESZ+1)/2)     // NV_MAPSPOTS thin raw path size
#define WIDEPATHSZ      (tft.SCALESZ+1)         // NV_MAPSPOTS wide raw path size


// N.B. must match csel_pr[] order
typedef enum {
    SHORTPATH_CSPR,
    LONGPATH_CSPR,
    SATPATH_CSPR,
    SATFOOT_CSPR,
    GRID_CSPR,
#if defined(_IS_UNIX)
    ROTATOR_CSPR,
#endif
    BAND160_CSPR,
    BAND80_CSPR,
    BAND60_CSPR,
    BAND40_CSPR,
    BAND30_CSPR,
    BAND20_CSPR,
    BAND17_CSPR,
    BAND15_CSPR,
    BAND12_CSPR,
    BAND10_CSPR,
    BAND6_CSPR,
    BAND2_CSPR,
    N_CSPR
} ColorSelection;

#define NV_ROTHOST_LEN          18
#define NV_RIGHOST_LEN          18
#define NV_FLRIGHOST_LEN        18

extern void clockSetup(void);
extern const char *getWiFiSSID(void);
extern const char *getWiFiPW(void);
extern const char *getCallsign(void);
extern bool setCallsign (const char *cs);
extern const char *getDXClusterHost(void);
extern int getDXClusterPort(void);
extern bool setDXCluster (char *host, const char *port_str, char ynot[]);
extern int getDXClusterPort(void);
extern bool useMetricUnits(void);
extern bool useGeoIP(void);
extern bool useGPSDTime(void);
extern bool useGPSDLoc(void);
extern bool labelSpots(void);
extern bool dotSpots(void);
extern bool plotSpotCallsigns(void);
extern bool rotateScreen(void);
extern float getBMETempCorr(int i);
extern float getBMEPresCorr(int i);
extern bool setBMETempCorr(BMEIndex i, float delta);
extern bool setBMEPresCorr(BMEIndex i, float delta);
extern const char *getGPSDHost(void);
extern bool useLocalNTPHost(void);
extern const char *getLocalNTPHost(void);
extern bool useDXCluster(void);
extern uint32_t getKX3Baud(void);
extern void drawStringInBox (const char str[], const SBox &b, bool inverted, uint16_t color);
extern bool logUsageOk(void);
extern uint16_t getMapColor (ColorSelection cid);
extern const char* getMapColorName (ColorSelection cid);
extern uint8_t getBrMax(void);
extern uint8_t getBrMin(void);
extern bool getX11FullScreen(void);
extern bool latSpecIsValid (const char *lng_spec, float &lng);
extern bool lngSpecIsValid (const char *lng_spec, float &lng);
extern bool getDemoMode(void);
extern int16_t getCenterLng(void);
extern DateFormat getDateFormat(void);
extern bool getRigctld (char host[NV_RIGHOST_LEN], int *portp);
extern bool getRotctld (char host[NV_ROTHOST_LEN], int *portp);
extern bool getFlrig (char host[NV_FLRIGHOST_LEN], int *portp);
extern const char *getDXClusterLogin(void);
extern int getSpotPathSize(void);
extern bool setMapColor (const char *name, uint16_t rgb565);
extern void getDXClCommands(const char *cmds[N_DXCLCMDS], bool on[N_DXCLCMDS]);
extern bool getColorDashed(ColorSelection id);
extern bool useMagBearing(void);
extern bool useWSJTX(void);
extern bool weekStartsOnMonday(void);
extern void formatLat (float lat_d, char s[], int s_len);
extern void formatLng (float lng_d, char s[], int s_len);
extern const char *getADIFilename(void);
extern bool scrollTopToBottom(void);
extern int nMoreScrollRows(void);
extern bool useOSTime (void);












/*********************************************************************************************
 *
 * magdecl.cpp
 *
 */

extern bool magdecl (float l, float L, float e, float y, float *mdp);




/*********************************************************************************************
 *
 * mapmanage.cpp
 *
 */

// unique enum for each band in BandCdtnMatrix
typedef enum {
    PROPBAND_80M,
    PROPBAND_40M,
    PROPBAND_30M,
    PROPBAND_20M,
    PROPBAND_17M,
    PROPBAND_15M,
    PROPBAND_12M,
    PROPBAND_10M,
    PROPBAND_N,
} PropMapBand;

typedef enum {
    PROPTYPE_REL,                       // reliability
    PROPTYPE_TOA,                       // take off angle
} PropMapType;

typedef struct {
    bool active;                        // whether currently in play
    PropMapBand band;                   // one of above if in play
    PropMapType type;                   // one of above if in play
} PropMapSetting;
extern PropMapSetting prop_map;


// CoreMaps and coremap_names
#define COREMAPS                 \
    X(CM_COUNTRIES, "Countries") \
    X(CM_TERRAIN,   "Terrain")   \
    X(CM_DRAP,      "DRAP")      \
    X(CM_MUF,       "MUF")       \
    X(CM_AURORA,    "Aurora")    \
    X(CM_WX,        "Weather")

#define X(a,b)  a,                      // expands COREMAPS to each enum followed by comma
typedef enum {
    COREMAPS
    CM_N
} CoreMaps;
#undef X

#define CM_NONE CM_N                    // handy alias meaning none active

extern CoreMaps core_map;               // current map, if any
extern const char *coremap_names[CM_N]; // core map style names

extern SBox mapscale_b;                 // map scale box

extern void initCoreMaps(void);
extern bool installFreshMaps(void);
extern float propMap2MHz (PropMapBand band);
extern int propMap2Band (PropMapBand band);
extern bool getMapDayPixel (uint16_t row, uint16_t col, uint16_t *dayp);
extern bool getMapNightPixel (uint16_t row, uint16_t col, uint16_t *nightp);
extern const char *getMapStyle (char s[]);
extern void drawMapScale(void);
extern void eraseMapScale(void);
extern bool mapScaleIsUp(void);

#if defined(__GNUC__)
extern void mapMsg (bool force, uint32_t dwell_ms, const char *fmt, ...) __attribute__ ((format (__printf__, 3, 4)));
#else
extern void mapMsg (bool force, uint32_t dwell_ms, const char *fmt, ...);
#endif



typedef struct {
    char name[33];      // name with EOS
    char date[21];      // ISO 8601 date with EOS
    time_t t0;          // unix time
    uint32_t len;       // n bytes
} FS_Info;
extern FS_Info *getConfigDirInfo (int *n_info, char **fs_name, uint64_t *fs_size, uint64_t *fs_used);




/*********************************************************************************************
 *
 * menu.cpp
 *
 */


typedef enum {
    MENU_LABEL,                 // insensitive string
    MENU_1OFN,                  // exactly 1 of this set, round selector
    MENU_01OFN,                 // exactly 0 or 1 of this set, round selector
    MENU_AL1OFN,                // at least 1 of this set, square selector
    MENU_TOGGLE,                // simple on/off with no grouping, square selector
    MENU_IGNORE,                // ignore this entry entirely
    MENU_BLANK,                 // empty space
} MenuFieldType;

// return whether the given MenuFieldType involves active user interaction
#define MENU_ACTIVE(i)          ((i)==MENU_1OFN || (i)==MENU_01OFN || (i)==MENU_AL1OFN || (i)==MENU_TOGGLE)

typedef enum {
    MENU_OK_OK,                 // normal ok button appearance
    MENU_OK_BUSY,               // busy ok button appearance
    MENU_OK_ERR,                // error ok button appearance
} MenuOkState;

typedef struct {
    MenuFieldType type;         // appearance and behavior
    bool set;                   // whether selected
    uint8_t group;              // association
    uint8_t indent;             // pixels to indent
    const char *label;          // string -- user must manage memory
} MenuItem;

typedef struct {
    SBox &menu_b;               // initial menu box -- sized automatically and may be moved
    SBox &ok_b;                 // box for Ok button -- user may use later with menuRedrawOk()
    bool update_clocks;         // whether to update clocks while waiting
    bool no_cancel;             // whether to just have Ok button
    int n_cols;                 // number of columns in which to display items
    int n_items;                // number of items[]
    MenuItem *items;            // list -- user must manage memory
} MenuInfo;

extern bool runMenu (MenuInfo &menu);
extern void menuRedrawOk (SBox &ok_b, MenuOkState oks);


typedef struct {
    const SBox &inbox;                          // overall input box bounds
    bool (*fp)(void);                           // user check function, else NULL
    bool fp_true;                               // true if fp returned true
    uint32_t to_ms;                             // timeout, msec, or 0 forever
    bool update_clocks;                         // whether to update clocks while waiting
    SCoord &tap;                                // tap location or ...
    char &kbchar;                               // keyboard char code
} UserInput;

extern bool waitForUser (UserInput &ui);


/*******************************************************************************************n
 *
 * moonpane.cpp and moon_imgs.cpp
 *
 */

extern void updateMoonPane (const SBox &box, bool image_too);
extern void drawMoonElPlot (void);
extern const uint16_t moon_image[HC_MOON_W*HC_MOON_H] PROGMEM;










/*********************************************************************************************
 *
 * ncdxf.cpp
 *
 */

#define NCDXF_B_NFIELDS         4       // n fields in NCDXF_b
#define NCDXF_B_MAXLEN          10      // max field length

extern void updateBeacons (bool immediate, bool erase_too);
extern void updateBeaconScreenLocations(void);
extern void doNCDXFStatsTouch (const SCoord &s, PlotChoice pcs[NCDXF_B_NFIELDS]);
extern void drawBeaconKey(void);
extern void doNCDXFBoxTouch (const SCoord &s);
extern bool drawNCDXFBox(void);
extern void initBRBRotset(void);
extern void checkBRBRotset(void);
extern void drawNCDXFStats (uint16_t color,
                            const char titles[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN],
                            const char values[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN],
                            const uint16_t colors[NCDXF_B_NFIELDS]);

#if defined (_IS_ESP8266)
extern bool overAnyBeacon (const SCoord &s);
#endif





/*********************************************************************************************
 *
 * nvram.cpp
 *
 */


/* names of each entry
 * N.B. the entries here must match those in nv_sizes[]
 */
typedef enum {
    NV_TOUCH_CAL_A,             // touch calibration coefficient
    NV_TOUCH_CAL_B,             // touch calibration coefficient
    NV_TOUCH_CAL_C,             // touch calibration coefficient
    NV_TOUCH_CAL_D,             // touch calibration coefficient
    NV_TOUCH_CAL_E,             // touch calibration coefficient

    NV_TOUCH_CAL_F,             // touch calibration coefficient
    NV_TOUCH_CAL_DIV,           // touch calibration normalization
    NV_DXMAX_N,                 // n dx connections since NV_DXMAX_T
    NV_DE_TIMEFMT,              // DE: 0=info; 1=analog; 2=cal; 3=analog+day; 4=dig 12hr; 5=dig 24hr
    NV_DE_LAT,                  // DE latitude, degrees N

    NV_DE_LNG,                  // DE longitude, degrees E
    NV_DE_GRID_OLD,             // deprecated
    NV_DX_DST,                  // deprecated
    NV_DX_LAT,                  // DX latitude, degrees N
    NV_DX_LNG,                  // DX longitude, degrees E

    NV_DX_GRID_OLD,             // deprecated
    NV_CALL_FG_COLOR,           // Call foreground color as RGB 565
    NV_CALL_BG_COLOR,           // Call background color as RGB 565 unless...
    NV_CALL_BG_RAINBOW,         // set if Call background to be rainbow
    NV_PSK_SHOWDIST,            // Live spots shows max distance, else counts

    NV_UTC_OFFSET,              // offset from UTC, seconds
    NV_PLOT_1,                  // Pane 1 PlotChoice
    NV_PLOT_2,                  // Pane 2 PlotChoice
    NV_BRB_ROTSET_OLD,          // deprecated after it became too small
    NV_PLOT_3,                  // Pane 3 PlotChoice

    NV_RSS_ON,                  // whether to display RSS
    NV_BPWM_DIM,                // dim PWM, 0..255
    NV_PHOT_DIM,                // photo r dim value, 0 .. 1023
    NV_BPWM_BRIGHT,             // bright PWM, 0..255
    NV_PHOT_BRIGHT,             // photo r bright value, 0 .. 1023

    NV_LP,                      // whether to show DE-DX long or short path info
    NV_METRIC_ON,               // whether to use metric or imperical values
    NV_LKSCRN_ON,               // whether screen lock is on
    NV_MAPPROJ,                 // 0: merc 1: azim 2: azim 1
    NV_ROTATE_SCRN,             // whether to flip screen

    NV_WIFI_SSID,               // WIFI SSID
    NV_WIFI_PASSWD_OLD,         // deprecated
    NV_CALLSIGN,                // call 
    NV_SATNAME,                 // satellite name with underscore for each space
    NV_DE_SRSS,                 // whether DE pane shows sun times 0=until or 1=at

    NV_DX_SRSS,                 // whether DX pane shows sun times 0=until or 1=at or 2=DX prefix
    NV_GRIDSTYLE,               // map grid style 0=off; 1=tropics; 2=lat-lng; 3=maindenhead, 4=radial
    NV_DPYON,                   // deprecated since NV_DAILYONOFF
    NV_DPYOFF,                  // deprecated since NV_DAILYONOFF
    NV_DXHOST,                  // DX cluster host name, unless using WSJT

    NV_DXPORT,                  // DX cluster port number
    NV_SWHUE,                   // stopwatch color RGB 565
    NV_TEMPCORR76,              // BME280 76 temperature correction, NV_METRIC_ON units
    NV_GPSDHOST,                // gpsd daemon host name
    NV_KX3BAUD,                 // KX3 baud rate or 0

    NV_BCPOWER,                 // VOACAP power, watts
    NV_CD_PERIOD,               // stopwatch count down period, seconds
    NV_PRESCORR76,              // BME280 76 pressure correction, NV_METRIC_ON units
    NV_BR_IDLE,                 // idle period, minutes
    NV_BR_MIN,                  // minimum brightness, percent of display range

    NV_BR_MAX,                  // maximum brightness, percent of display range
    NV_DE_TZ,                   // DE offset from UTC, seconds
    NV_DX_TZ,                   // DX offset from UTC, seconds
    NV_COREMAPSTYLE,            // name of core map background images (not voacap propmaps)
    NV_USEDXCLUSTER,            // whether to attempt using a DX cluster

    NV_USEGPSD,                 // bit 1: use gpsd for time, bit 2: use for location
    NV_LOGUSAGE,                // whether to phone home with clock settings
    NV_MAPSPOTS,                // DX spot annotations: 0=none; 1=just prefix; 2=full call; |= width
    NV_WIFI_PASSWD,             // WIFI password
    NV_NTPSET,                  // whether to use NV_NTPHOST

    NV_NTPHOST,                 // user defined NTP host name
    NV_GPIOOK,                  // whether ok to use GPIO pins
    NV_SATPATHCOLOR,            // satellite path color as RGB 565
    NV_SATFOOTCOLOR,            // satellite footprint color as RGB 565
    NV_X11FLAGS,                // set if want full screen

    NV_BCFLAGS,                 // Big Clock bitmask: 1=date;2=wx;4=dig;8=12hr;16=nosec;32=UTC;64=an+dig;128=hrs;256=SpWx;512=hands;1024=sat
    NV_DAILYONOFF,              // 7 2-byte on times then 7 off times, each mins from midnight
    NV_TEMPCORR77,              // BME280 77 temperature correction, NV_METRIC_ON units
    NV_PRESCORR77,              // BME280 77 pressure correction, NV_METRIC_ON units
    NV_SHORTPATHCOLOR,          // prop short path color as RGB 565

    NV_LONGPATHCOLOR,           // prop long path color as RGB 565
    NV_PLOTOPS,                 // deprecated since NV_PANE?CH
    NV_NIGHT_ON,                // whether to show night on map
    NV_DE_GRID,                 // DE 6 char grid
    NV_DX_GRID,                 // DX 6 char grid

    NV_GRIDCOLOR,               // map grid color as RGB 565
    NV_CENTERLNG,               // mercator center longitude
    NV_NAMES_ON,                // whether to show roving place names
    NV_PANE1ROTSET,             // PlotChoice bitmask of pane 1 rotation choices
    NV_PANE2ROTSET,             // PlotChoice bitmask of pane 2 rotation choices

    NV_PANE3ROTSET,             // PlotChoice bitmask of pane 3 rotation choices
    NV_AUX_TIME,                // 0=date, DOY, JD, MJD, LST, UNIX
    NV_ALARMCLOCK,              // DE alarm time 60*hr + min, + 60*24 if off
    NV_BC_UTCTIMELINE,          // band conditions timeline labeled in UTC else DE
    NV_RSS_INTERVAL,            // RSS update interval, seconds

    NV_DATEMDY,                 // 0 = MDY 1 = see NV_DATEDMYYMD
    NV_DATEDMYYMD,              // 0 = DMY 1 = YMD
    NV_ROTUSE,                  // whether to use rotctld
    NV_ROTHOST,                 // rotctld tcp host
    NV_ROTPORT,                 // rotctld tcp port

    NV_RIGUSE,                  // whether to use rigctld
    NV_RIGHOST,                 // rigctld tcp host
    NV_RIGPORT,                 // rigctld tcp port
    NV_DXLOGIN,                 // DX cluster login
    NV_FLRIGUSE,                // whether to use flrig

    NV_FLRIGHOST,               // flrig tcp host
    NV_FLRIGPORT,               // flrig tcp port
    NV_DXCMD0,                  // dx cluster command 0
    NV_DXCMD1,                  // dx cluster command 1
    NV_DXCMD2,                  // dx cluster command 2

    NV_DXCMD3,                  // dx cluster command 3
    NV_DXCMDUSED,               // bitmask of dx cluster commands in use
    NV_PSK_MODEBITS,            // live spots mode: bit 0: on=psk off=wspr bit 1: on=bycall off=bygrid
    NV_PSK_BANDS,               // live spots bands: bit mask 0 .. 11 160 .. 2m
    NV_160M_COLOR,              // 160 m path color as RGB 565

    NV_80M_COLOR,               // 80 m path color as RGB 565
    NV_60M_COLOR,               // 60 m path color as RGB 565
    NV_40M_COLOR,               // 40 m path color as RGB 565
    NV_30M_COLOR,               // 30 m path color as RGB 565
    NV_20M_COLOR,               // 20 m path color as RGB 565

    NV_17M_COLOR,               // 17 m path color as RGB 565
    NV_15M_COLOR,               // 15 m path color as RGB 565
    NV_12M_COLOR,               // 12 m path color as RGB 565
    NV_10M_COLOR,               // 10 m path color as RGB 565
    NV_6M_COLOR,                // 6 m path color as RGB 565

    NV_2M_COLOR,                // 2 m path color as RGB 565
    NV_DASHED,                  // ColorSelection bitmask set for dashed
    NV_BEAR_MAG,                // show magnetic bearings, else true
    NV_WSJT_SETSDX,             // deprecated
    NV_WSJT_DX,                 // whether dx cluster is WSJT-X

    NV_PSK_MAXAGE,              // live spots max age, minutes
    NV_WEEKMON,                 // whether week starts on Monday
    NV_BCMODE,                  // CW=19 SSB=38 AM=49 WSPR=3 FT8=13 FT4=17
    NV_SDO,                     // sdo pane choice 0..6
    NV_SDOROT,                  // whether SDO pane is rotating

    NV_ONTASPOTA,               // POTA sort 0-3: Band Call ID Age
    NV_ONTASSOTA,               // SOTA sort 0-3: Band Call ID Age
    NV_BRB_ROTSET,              // Beacon box mode bit mask
    NV_ROTCOLOR,                // rotator map color
    NV_CONTESTS,                // 1 to show date

    NV_BCTOA,                   // VOACAP take off angle, degs
    NV_ADIFFN,                  // ADIF file name, if any
    NV_I2CFN,                   // I2C device filename
    NV_I2CON,                   // whether to use I2C
    NV_DXMAX_T,                 // time when n dx connections exceeded max

    NV_DXWLIST,                 // DX watch list
    NV_SCROLLDIR,               // 0=bottom 1=top
    NV_SCROLLLEN,               // n more lines to scroll

    NV_N

} NV_Name;

// string valued lengths including trailing EOS
#define NV_WIFI_SSID_LEN        32
#define NV_WIFI_PW_LEN_OLD      32
#define NV_CALLSIGN_LEN         12
// NV_SATNAME_LEN needed above for SatNow
#define NV_DXHOST_LEN           26
#define NV_GPSDHOST_LEN         18
#define NV_NTPHOST_LEN          18
#define NV_COREMAPSTYLE_LEN     10
#define NV_WIFI_PW_LEN          64
#define NV_DAILYONOFF_LEN       28      // (2*DAYSPERWEEK*sizeof(uint16_t))
#define NV_DE_GRID_LEN          MAID_CHARLEN
#define NV_DX_GRID_LEN          MAID_CHARLEN
// NV_ROTHOST_LEN needed above for setup.cpp
// NV_RIGHOST_LEN needed above for setup.cpp
// NV_FLRIGHOST_LEneeded above for setup.cpp
#define NV_DXLOGIN_LEN          12
#define NV_DXWLIST_LEN          26
#define NV_DXCLCMD_LEN          35
#define NV_ADIFFN_LEN           30
#define NV_I2CFN_LEN            30



// accessor functions
extern void NVWriteFloat (NV_Name e, float f);
extern void NVWriteUInt32 (NV_Name e, uint32_t u);
extern void NVWriteInt32 (NV_Name e, int32_t u);
extern void NVWriteUInt16 (NV_Name e, uint16_t u);
extern void NVWriteInt16 (NV_Name e, int16_t u);
extern void NVWriteUInt8 (NV_Name e, uint8_t u);
extern void NVWriteString (NV_Name e, const char *str);
extern bool NVReadFloat (NV_Name e, float *fp);
extern bool NVReadUInt32 (NV_Name e, uint32_t *up);
extern bool NVReadInt32 (NV_Name e, int32_t *up);
extern bool NVReadUInt16 (NV_Name e, uint16_t *up);
extern bool NVReadInt16 (NV_Name e, int16_t *up);
extern bool NVReadUInt8 (NV_Name e, uint8_t *up);
extern bool NVReadString (NV_Name e, char *buf);

extern void reportEESize (uint16_t &ee_used, uint16_t &ee_size);



/*********************************************************************************************
 *
 * maidenhead.cpp
 *
 */


extern void ll2maidenhead (char maid[MAID_CHARLEN], const LatLong &ll);
extern bool maidenhead2ll (LatLong &ll, const char maid[MAID_CHARLEN]);
extern void setNVMaidenhead (NV_Name nv, LatLong &ll);
extern void getNVMaidenhead (NV_Name nv, char maid[MAID_CHARLEN]);




/*********************************************************************************************
 *
 * ontheair.cpp
 *
 */


#define ONTAPrograms             \
    X(ONTA_POTA, "POTA")         \
    X(ONTA_SOTA, "SOTA")

#define X(a,b) a,                       // expands ONTAPrograms to each enum and comma
typedef enum {
    ONTAPrograms
    ONTA_N
} ONTAProgram;
#undef X

extern const char *onta_names[ONTA_N];

extern bool updateOnTheAir (const SBox &box, ONTAProgram onta);
extern bool checkOnTheAirTouch (const SCoord &s, const SBox &box, ONTAProgram onta);
extern bool getOnTheAirSpots (DXClusterSpot **spp, uint8_t *nspotsp, ONTAProgram onta);
extern void drawOnTheAirSpotsOnMap (void);
extern void updateOnTheAirSpotScreenLocations(void);
extern bool getClosestOnTheAirSpot (const LatLong &ll, DXClusterSpot *sp, LatLong *llp);
extern void checkOnTheAirActive(void);

#if defined(_IS_ESP8266)
extern bool overAnyOnTheAirSpots(const SCoord &s);
#endif





/*********************************************************************************************
 *
 * plot.cpp
 *
 */

#define BMTRX_ROWS      24                              // time: UTC 0 .. 23
#define BMTRX_COLS      PROPBAND_N                      // bands: 80-40-30-20-17-15-12-10
typedef uint8_t BandCdtnMatrix[BMTRX_ROWS][BMTRX_COLS]; // percent circuit reliability as matrix of 24 rows
                                                        // UTC 0 .. 23, 8 band cols 80-40-30-20-17-15-12-10.

extern void plotBandConditions (const SBox &box, int busy, const BandCdtnMatrix *bmp, char *config_str);
extern bool plotXY (const SBox &box, float x[], float y[], int nxy, const char *xlabel,
        const char *ylabel, uint16_t color, float y_min, float y_max, float big_value);
extern bool plotXYstr (const SBox &box, float x[], float y[], int nxy, const char *xlabel,
        const char *ylabel, uint16_t color, float y_min, float y_max, char *label_str);
extern void plotWX (const SBox &b, uint16_t color, const WXInfo &wi);
extern void plotMessage (const SBox &b, uint16_t color, const char *message);
extern void plotNOAASWx (const SBox &box, const NOAASpaceWx &noaaspw);
extern uint16_t maxStringW (char *str, uint16_t maxw);
extern void prepPlotBox (const SBox &box);





/*********************************************************************************************
 *
 * plotmap.cpp
 *
 */

extern void plotMap (const char *filename, const char *title, uint16_t color);





/*********************************************************************************************
 *
 * plotmgmnt.cpp
 *
 */


extern SBox plot_b[PANE_N];                     // box for each pane
extern PlotChoice plot_ch[PANE_N];              // current choice in each pane
extern const char *plot_names[PLOT_CH_N];       // must be in same order as PlotChoice
extern uint32_t plot_rotset[PANE_N];            // bitmask of all PlotChoice rotation choices
                                                // N.B. plot_rotset[i] must always include plot_ch[i]

#define PLOT_ROT_WARNING        4               // show rotation about to occur, secs

extern void insureCountdownPaneSensible(void);
extern bool checkPlotTouch (const SCoord &s, PlotPane pp, TouchType tt);
extern PlotPane findPaneForChoice (PlotChoice pc);
extern PlotPane findPaneChoiceNow (PlotChoice pc);
extern PlotChoice getNextRotationChoice (PlotPane pp, PlotChoice pc);
extern PlotChoice getAnyAvailableChoice (void);
extern bool plotChoiceIsAvailable (PlotChoice ch);
extern void logPaneRotSet (PlotPane pp, PlotChoice ch);
extern void logBRBRotSet(void);
extern void showRotatingBorder (void);
extern void initPlotPanes(void);
extern void savePlotOps(void);
extern bool drawHTTPBMP (const char *hc_url, const SBox &box, uint16_t color);
extern int tickmarks (float min, float max, int numdiv, float ticks[]);
extern bool paneIsRotating (PlotPane pp);
extern bool ignorePane1Touch(void);
extern bool paneComboOk (const uint32_t new_rotsets[PANE_N]);








/*********************************************************************************************
 *
 * prefixes.cpp
 *
 */

extern bool nearestPrefix (const LatLong &ll, char prefix[MAX_PREF_LEN+1]);





/*********************************************************************************************
 *
 * pskreporter.cpp
 *
 */


// all implementations share the following:

typedef enum {
    PSKMB_SRC0 = 1,                     // data source, see PSKIS/PSKSET
    PSKMB_CALL = 2,                     // using call, else grid
    PSKMB_OFDE = 4,                     // spot of DE, else by DE
    PSKMB_SRC1 = 8,                     // data source, see PSKIS/PSKSET
} PSKModeBits;

#define PSKMB_SRCMASK   (PSKMB_SRC0|PSKMB_SRC1)
#define PSKMB_PSK       (PSKMB_SRC0)
#define PSKMB_WSPR      (0)
#define PSKMB_RBN       (PSKMB_SRC1)

#define PSK_DOTR       2                // end point marker radius for several paths, not just PSK

typedef enum {
    PSKBAND_160M,
    PSKBAND_80M,
    PSKBAND_60M,
    PSKBAND_40M,
    PSKBAND_30M,
    PSKBAND_20M,
    PSKBAND_17M,
    PSKBAND_15M,
    PSKBAND_12M,
    PSKBAND_10M,
    PSKBAND_6M,
    PSKBAND_2M,
    PSKBAND_N
} PSKBandSetting;

// info known about each report
// N.B. match char sizes with sscanf in pskreporter.cpp
typedef struct {
    time_t posting;
    char txgrid[10];
    char txcall[20];
    char rxgrid[10];
    char rxcall[20];
    char mode[20];
    LatLong dx_ll;                      // location of the "other" guy, regardless of tx/rx
    long Hz;
    int snr;
} PSKReport;

// current stats for each band
typedef struct {
    int count;                          // spots count
    float maxkm;                        // distance to farthest spot, km
    float maxlat;                       // lat of farthest spot, rads +N
    float maxlng;                       // longitude of farthest spot, rads +E
    char maxcall[20];                   // call of farthest station. N.B. match size of PSKReport.txcall
    SCoord max_s;                       // screen coord of farthest spot
    SBox maxtag_b;                      // screen location of tag
} PSKBandStats;

extern uint8_t psk_mask;                // bitmask of PSKModeBits
extern uint32_t psk_bands;              // bitmask of 1 << PSKBandSetting
extern uint16_t psk_maxage_mins;        // max age, minutes
extern uint8_t psk_showdist;

extern bool updatePSKReporter (const SBox &box);
extern bool checkPSKTouch (const SCoord &s, const SBox &box);
extern void drawPSKPane (const SBox &box);
extern void initPSKState(void);
extern void savePSKState(void);
extern void drawFarthestPSKSpots(void);
extern bool getPSKBandStats (PSKBandStats stats[PSKBAND_N], const char *names[PSKBAND_N]);
extern bool maxPSKageOk (int m);
extern uint16_t getBandColor (long Hz);

#if defined (_IS_ESP8266)
extern bool overAnyFarthestPSKSpots (const SCoord &s);
#endif

#if defined(_IS_UNIX)

extern bool getBandDashed (long Hz);
extern void drawPSKPaths (void);
extern bool getClosestPSK (const LatLong &ll, const PSKReport **rpp);
extern void getPSKSpots (const PSKReport* &rp, int &n_rep);

#endif // _IS_UNIX



/*********************************************************************************************
 *
 * radio.cpp
 *
 */

extern void setRadioSpot (float kHz);
extern void radioResetIO(void);




/*********************************************************************************************
 *
 * grayline.cpp
 *
 */

extern void plotGrayline(void);



/*********************************************************************************************
 *
 * runner.cpp
 *
 */
extern const uint16_t runner[HC_RUNNER_W*HC_RUNNER_H] PROGMEM;




/*********************************************************************************************
 *
 * santa.cpp
 *
 */

extern void drawSanta(void);
extern SBox santa_b;



/*********************************************************************************************
 *
 * selectFont.cpp
 *
 */


extern const GFXfont Germano_Regular16pt7b PROGMEM;
extern const GFXfont Germano_Bold16pt7b PROGMEM;
extern const GFXfont Germano_Bold30pt7b PROGMEM;

typedef enum {
    BOLD_FONT,
    LIGHT_FONT
} FontWeight;

typedef enum {
    FAST_FONT,
    SMALL_FONT,
    LARGE_FONT
} FontSize;

extern void selectFontStyle (FontWeight w, FontSize s);




/*********************************************************************************************
 *
 * sdo.cpp
 *
 */

extern bool checkSDOTouch (const SCoord &s, const SBox &b);
extern bool updateSDOPane (const SBox &box, bool image_too);
extern bool isSDORotating(void);






/*********************************************************************************************
 *
 * sphere.cpp
 *
 */

extern void solveSphere (float A, float b, float cc, float sc, float *cap, float *Bp);
extern float simpleSphereDist (const LatLong &ll1, const LatLong &ll2);




/*********************************************************************************************
 *
 * touch.cpp
 *
 */

extern void calibrateTouch(bool force);
extern void drainTouch(void);
extern TouchType readCalTouch (SCoord &s);
extern TouchType checkKBWarp (SCoord &s);

// for passing web touch command to checkTouch()
extern TouchType wifi_tt;
extern SCoord wifi_tt_s;




/*********************************************************************************************
 *
 * stopwatch.cpp
 *
 */

// bit mask values for NV_BCFLAGS
typedef enum {
    SW_BCDATEBIT =  (1<<0),                     // showing bigclock date
    SW_BCWXBIT   =  (1<<1),                     // showing bigclock weather
    SW_BCDIGBIT  =  (1<<2),                     // big clock is digital else analog
    SW_DB12HBIT  =  (1<<3),                     // digital clock is 12 else 24
    SW_NOSECBIT  =  (1<<4),                     // set if not showing seconds
    SW_UTCBIT    =  (1<<5),                     // set if Big Clock showing 24 hr UTC 
    SW_ANWDBIT   =  (1<<6),                     // set if analog clock also showing digital time
    SW_ANNUMBIT  =  (1<<7),                     // set if analog clock also shows hour numbers on face
    SW_BCSPWXBIT =  (1<<8),                     // showing bigclock space weather
    SW_ANCOLHBIT =  (1<<9),                     // color the analog hands
    SW_LSTBIT    =  (1<<10),                    // set if Big Clock showing 24 hr local sidereal time 
    SW_BCSATBIT  =  (1<<11),                    // set if BC potentially showing satellite up/down
} SWBCBits;

// state of stopwatch engine, _not_ what is being display
typedef enum {
    SWE_RESET,                                  // showing 0, ready to run
    SWE_RUN,                                    // running, can Stop or Lap
    SWE_STOP,                                   // holding time, can run or reset
    SWE_LAP,                                    // hold time, can resume or reset
    SWE_COUNTDOWN,                              // counting down
} SWEngineState;

// what stopwatch is displaying, _not_ the state of the engine
typedef enum {
    SWD_NONE,                                   // not displaying any part of stopwatch
    SWD_MAIN,                                   // basic stopwatch
    SWD_BCDIGITAL,                              // Big Clock, digital
    SWD_BCANALOG,                               // Big Clock, analog
} SWDisplayState;

// alarm state
typedef enum {
    ALMS_OFF,
    ALMS_ARMED,
    ALMS_RINGING
} AlarmState;

extern SBox stopwatch_b;                        // clock icon on main display

extern void initStopwatch(void);
extern void checkStopwatchTouch(TouchType tt);
extern bool runStopwatch(void);
extern void drawMainPageStopwatch (bool force);
extern bool setSWEngineState (SWEngineState nsws, uint32_t ms);
extern SWEngineState getSWEngineState (uint32_t *sw_timer, uint32_t *cd_period);
extern SWDisplayState getSWDisplayState (void);
extern void getAlarmState (AlarmState &as, uint16_t &hr, uint16_t &mn);
extern void setAlarmState (const AlarmState &as, uint16_t hr, uint16_t mn);
extern SWBCBits getBigClockBits(void);
extern void SWresetIO(void);







/*********************************************************************************************
 *
 * tz.cpp
 *
 */
extern int32_t getTZ (const LatLong &ll);
extern int getTZStep (const LatLong &ll);




/*********************************************************************************************
 *
 * webserver.cpp
 *
 */

// handy tool to parse web command arguments
#define MAX_WEBARGS     10
typedef struct {
    const char *name[MAX_WEBARGS];              // name to look for
    const char *value[MAX_WEBARGS];             // ptr to its value, or NULL
    bool found[MAX_WEBARGS];                    // whether this name was found in the original GET command
    int nargs;
} WebArgs;
extern bool parseWebCommand (WebArgs &wa, char line[], size_t line_len);


extern char *trim (char *str);
extern void startPlainText (WiFiClient &client);
extern void initWebServer(void);
extern void checkWebServer(bool ro);
extern TouchType readCalTouchWS (SCoord &s);
extern const char platform[];
extern void runNextDemoCommand(void);
extern bool bypass_pw;

#if defined(__GNUC__)
extern void sendHTTPError (WiFiClient &client, const char *fmt, ...) __attribute__ ((format(__printf__,2,3)));
#else
extern void sendHTTPError (WiFiClient &client, const char *fmt, ...);
#endif



/*********************************************************************************************
 *
 * wifi.cpp
 *
 */

#define SSPOT_COLOR     RA8875_CYAN             // plot and history color
#define SFLUX_COLOR     RA8875_GREEN            // plot and history color

typedef struct {
    const char *server;                         // name of server
    int rsp_time;                               // last known response time, millis()
} NTPServer;
#define NTP_TOO_LONG 5000U                      // too long response time, millis()

#define SPW_ERR (-9999)                         // bad cookie value for space weather stats

extern void initSys (void);
extern void initWiFiRetry(void);
extern void scheduleNewMoon(void);
extern void scheduleNewBC(void);
extern void scheduleNewSDO(void);
extern void scheduleNewPSK(void);
extern void scheduleNewDXC(void);
extern void scheduleNewPOTA(void);
extern void scheduleNewSOTA(void);
extern void scheduleNewADIF(void);
extern void scheduleNewVOACAPMap(PropMapSetting &pm);
extern void scheduleNewCoreMap(CoreMaps cm);
extern void updateWiFi(void);
extern bool checkBCTouch (const SCoord &s, const SBox &b);
extern bool setPlotChoice (PlotPane new_pp, PlotChoice new_ch);
extern bool getTCPChar (WiFiClient &client, char *cp);
extern time_t getNTPUTC(const char **server);
extern void scheduleRSSNow(void);
extern bool getTCPLine (WiFiClient &client, char line[], uint16_t line_len, uint16_t *ll);
extern void sendUserAgent (WiFiClient &client);
extern bool wifiOk(void);
extern void httpGET (WiFiClient &client, const char *server, const char *page);
extern void httpHCGET (WiFiClient &client, const char *server, const char *hc_page);
extern void httpHCPGET (WiFiClient &client, const char *server, const char *hc_page_progmem);
extern bool httpSkipHeader (WiFiClient &client);
extern bool httpSkipHeader (WiFiClient &client, const char *header, char *value, int value_len);
extern void FWIFIPR (WiFiClient &client, const __FlashStringHelper *str);
extern void FWIFIPRLN (WiFiClient &client, const __FlashStringHelper *str);
extern int getNTPServers (const NTPServer **listp);
extern bool setRSSTitle (const char *title, int &n_titles, int &max_titles);
extern bool checkSpaceStats(void);
extern void doSpaceStatsTouch (const SCoord &s);
extern void drawSpaceStats(uint16_t color);
extern bool getBCMatrix (BandCdtnMatrix &bm);
extern time_t nextPaneRotation (PlotPane pp);


extern char remote_addr[16];



extern uint8_t rss_interval;

#define N_BCMODES       6               // n voacap modes
typedef struct {
    const char *name;                   // mode such as CW, SSB, etc
    uint8_t value;                      // voacap sensitivity value
} BCModeSetting;
extern const BCModeSetting bc_modes[N_BCMODES];
extern uint8_t findBCModeValue (const char *name);
extern const char *findBCModeName (uint8_t value);
extern uint8_t bc_modevalue;
extern uint16_t bc_power;
extern float bc_toa;
extern uint8_t bc_utc_tl;
extern const int n_bc_powers;
extern uint16_t bc_powers[];

extern void getSpaceWeather (SPWxValue &ssn, SPWxValue &sflux, SPWxValue &kp, SPWxValue &swind, 
    SPWxValue &drap, SPWxValue &bz, SPWxValue &bt,
    NOAASpaceWx &noaaspw, time_t &noaaspw_age, char xray[], time_t &xray_age,
    float pathrel[BMTRX_COLS], time_t &pathrel_age);




/*********************************************************************************************
 *
 * wifimeter.cpp
 *
 */


#define MIN_WIFI_RSSI (-75)                     // minimum acceptable signal strength, dBm
// https://docs.espressif.com/projects/espressif-esp-faq/en/latest/software-framework/wifi.html#connect-how-do-wi-fi-modules-rank-signal-strength-levels-based-on-rssi-values

extern int runWiFiMeter (bool warn, bool &ignore_on);
extern bool readWiFiRSSI(int &rssi);
extern bool wifiMeterIsUp();




/*********************************************************************************************
 *
 * wx.cpp
 *
 */


extern bool getCurrentWX (const LatLong &ll, bool is_de, WXInfo *wip, char ynot[]);
extern bool updateDEWX (const SBox &box);
extern bool updateDXWX (const SBox &box);
extern void showDXWX(void);
extern void showDEWX(void);
extern bool getWorldWx (const LatLong &ll, WXInfo &wi);
extern void fetchWorldWx(void);
extern bool drawNCDXFWx (BRB_MODE m);





/*********************************************************************************************
 *
 * zones.cpp
 *
 */

#if defined(_SUPPORT_ZONES)

// uncomment this to show the bounding boxes around each zone
// #define DEBUG_ZONES_BB

typedef enum {
    ZONE_CQ,
    ZONE_ITU
} ZoneID;

extern bool findZoneNumber (ZoneID id, const SCoord &s, int *zone_n);
extern void updateZoneSCoords(ZoneID id);
extern void drawZone (ZoneID id, uint16_t color, int n_only);

#endif // _SUPPORT_ZONES



#endif // _HAMCLOCK_H
