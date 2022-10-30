/* HamClock glue
 */



#ifndef _HAMCLOCK_H
#define _HAMCLOCK_H


// handy build categories

#if defined(ESP8266)
  #define _IS_ESP8266
#else
  #define _IS_UNIX
#endif

#if defined(__linux__)
  #define _IS_LINUX
#endif

#if defined(__FreeBSD__)
  #define _IS_FREEBSD
#endif

#if (defined(__arm__) || defined(__aarch64__)) && defined(_IS_LINUX)
  #if defined(__has_include)
      #if __has_include(<bcm_host.h>) || __has_include(<pigpio.h>) || __has_include(<wiringPi.h>) 
        #define _IS_LINUX_RPI
      #endif
  #endif
#endif

#if defined(_IS_ESP8266)
  #define _IIC_ESP
#elif defined(__has_include)
  #if defined(_IS_FREEBSD) && __has_include(<dev/iicbus/iic.h>) && __has_include("/dev/iic0")
    #define _IIC_FREEBSD
  #elif defined(_IS_LINUX) && (__has_include(<linux/i2c-dev.h>) || __has_include("linux/i2c-dev.h"))
    #define _IIC_LINUX
  #endif
#endif

#if defined(_IS_ESP8266)
  #define _GPIO_ESP
#elif defined(__has_include)
  #if defined(_IS_FREEBSD) && __has_include(<libgpio.h>) && __has_include("/dev/gpioc0")
    #define _GPIO_FREEBSD
  #elif defined(_IS_LINUX) && (__has_include(<bcm_host.h>) || __has_include(<pigpio.h>) || __has_include(<wiringPi.h>) )
    #define _GPIO_LINUX
  #endif
#endif

// whether we seem to support discreet IO
#if defined(_GPIO_ESP) || defined(_GPIO_FREEBSD) || defined(_GPIO_LINUX)
  #define _SUPPORT_GPIO
#endif

// whether we can support a temp sensor
#if defined(_IIC_ESP) || defined(_IIC_FREEBSD) || defined(_IIC_LINUX)
  #define _SUPPORT_ENVSENSOR
#endif

// Flip screen only on ESP
#if defined(_IS_ESP8266)
  #define _SUPPORT_FLIP
#endif

// kx3 on any system with GPIO
#if defined(_SUPPORT_GPIO)
  #define _SUPPORT_KX3
#endif

// phot only supported on ESP and then only if real phot is detected
#if defined(_IS_ESP8266)
  #define _SUPPORT_PHOT
#endif

// dx cluster path plotting not on ESP because paths can't be drawn in raster mode
#if !defined(_IS_ESP8266)
    #define _SUPPORT_CLPATH
#endif

// PSKReporter only partially supported on ESP because can't draw raster paths or spare mem for lists
#if defined(_IS_ESP8266)
    #define _SUPPORT_PSKESP
#endif

// roaming cities is not supported on ESP because it is touch only
#if !defined(_IS_ESP8266)
    #define _SUPPORT_CITIES
#endif

// zones not on ESP -- they fit in flash ok but can't be drawn in raster fashion
#if !defined(_IS_ESP8266)
    #define _SUPPORT_ZONES
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
#define EARTH_XH  1
#define EARTH_W   660
#define EARTH_XW  1


// UNIX-like modules
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#if defined(_IS_UNIX)
#include <signal.h>
#endif // _IS_UNIX

// see Adafruit_RA8875.h
#define USE_ADAFRUIT_GFX_FONTS

// community modules
#include <TimeLib.h>
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

// HamClock modules
#include "calibrate.h"
#include "version.h"
#include "P13.h"


// GPIO pins for unix systems
#if defined(_SUPPORT_GPIO) && defined(_IS_UNIX)

#include "GPIO.h"

// Raspberry Pi GPIO definitions, not header pins

#define SW_RED_GPIO             13      // header 33
#define SW_GRN_GPIO             19      // header 35
#define SW_COUNTDOWN_GPIO       26      // header 37
#define SW_ALARMOUT_GPIO        06      // header 31
#define SW_ALARMOFF_GPIO        05      // header 29
#define Elecraft_GPIO           14      // header 8
#define SATALARM_GPIO           20      // header 38
#define ONAIR_GPIO              21      // header 40


extern void SWresetIO(void);
extern void satResetIO(void);
extern void radioResetIO(void);

#endif


// GPIO pins for ESP Huzzah
#if defined (_GPIO_ESP)

#define Elecraft_GPIO           15

#endif



// handy nelements in array
// N.B. call with real array, not a pointer
#define NARRAY(a)       (sizeof(a)/sizeof(a[0]))

// float versions
#define M_PIF   3.14159265F
#define M_PI_2F (M_PIF/2)

#define deg2rad(d)      ((M_PIF/180)*(d))
#define rad2deg(d)      ((180/M_PIF)*(d))


// time to leave new DX path up, millis()
#define DXPATH_LINGER   20000   

// tcp ports
#define HTTPPORT        80
#define SERVERPORT      8080

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

        char *getMem(void) {
            return (mem);
        }

    private:

        char *mem;
        size_t siz;
};


/* time styles in auxtime_b
 */
typedef enum {
    AUXT_DATE,                  // as per NV_DATEMDY and NV_DATEDMYYMD
    AUXT_DOY,                   // day of year
    AUXT_JD,                    // Julian date
    AUXT_MJD,                   // modified Julian date
    AUXT_SIDEREAL,              // local sidereal time
    AUXT_SOLAR,                 // local solar time
    AUXT_UNIX,                  // unix seconds
    AUXT_N,                     // number of options
} AuxTimeFormat;
extern AuxTimeFormat auxtime;
extern const char *auxtime_names[AUXT_N];


/* plot choices and pane locations
 */

typedef enum {
    PLOT_CH_BC,
    PLOT_CH_DEWX,
    PLOT_CH_DXCLUSTER,
    PLOT_CH_DXWX,
    PLOT_CH_FLUX,

    PLOT_CH_KP,
    PLOT_CH_MOON,
    PLOT_CH_NOAASWX,
    PLOT_CH_SSN,
    PLOT_CH_XRAY,

    PLOT_CH_GIMBAL,
    PLOT_CH_TEMPERATURE,
    PLOT_CH_PRESSURE,
    PLOT_CH_HUMIDITY,
    PLOT_CH_DEWPOINT,

    PLOT_CH_SDO_1,
    PLOT_CH_SDO_2,
    PLOT_CH_SDO_3,
    PLOT_CH_SDO_4,
    PLOT_CH_SOLWIND,

    PLOT_CH_DRAP,
    PLOT_CH_COUNTDOWN,
    PLOT_CH_STEREO_A,
    PLOT_CH_PSK,

    PLOT_CH_N
} PlotChoice;

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


extern const char *svr_host;    // backend server name
extern int svr_port;            // web server port

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

typedef enum {
    BRB_SHOW_BEACONS,                   // NCDXF beacons
    BRB_SHOW_ONOFF,                     // on/off/idle times
    BRB_SHOW_PHOT,                      // brightness and phot controls
    BRB_SHOW_BR,                        // just brightness control
    BRB_SHOW_SWSTATS,                   // space weather stats
    BRB_SHOW_BME76,                     // sensor I2C 76
    BRB_SHOW_BME77,                     // sensor I2C 77
    BRB_N,                              // count
} BRB_MODE;

extern uint8_t brb_mode;                // one of BRB_MODE
extern time_t brb_rotationT;            // time at which to rotate, if more than 1 bit in rotset
extern uint8_t brb_rotset;              // bitmask of all active BRB_MODE choices
                                        // N.B. brb_rotset must always include brb_mode
#define BRBIsRotating()                 ((brb_rotset & ~(1 << brb_mode)) != 0)  // any bits other than mode
extern const char *brb_names[BRB_N];    // menu names -- must be in same order as BRB_MODE

// map projection styles
extern uint8_t map_proj;
typedef enum {
    MAPP_MERCATOR,                      // 2D
    MAPP_AZIMUTHAL,                     // two hemispheres, left centered on DE
    MAPP_AZIM1,                         // full sphere centered on DE
    MAPP_N
} MapProjection;
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
extern bool skip_skip;                  // whether to skip skipping
extern bool init_iploc;                 // init DE using our IP location
extern const char *init_locip;          // init DE from given IP

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



/*********************************************************************************************
 *
 * ESPHamClock.ino
 *
 */


extern bool askOTAupdate(char *ver);
extern void drawDXTime(void);
extern void drawDXMarker(bool force);
extern void drawAllSymbols(bool beacons_too);
extern void drawTZ(const TZInfo &tzi);
extern bool inBox (const SCoord &s, const SBox &b);
extern bool inCircle (const SCoord &s, const SCircle &c);
extern void tftMsg (bool verbose, uint32_t dwell_ms, const char *fmt, ...);
extern void doReboot(void);
extern void doExit(void);
extern void printFreeHeap (const __FlashStringHelper *label);
extern void getWorstMem (int *heap, int *stack);
extern void resetWatchdog(void);
extern void wdDelay(int ms);
extern bool timesUp (uint32_t *prev, uint32_t dt);
extern void setDXPathInvalid(void);
extern bool overMap (const SCoord &s);
extern bool overAnySymbol (const SCoord &s);
extern bool overRSS (const SCoord &s);
extern bool overRSS (const SBox &b);
extern bool checkCallsignTouchFG (SCoord &b);
extern bool checkCallsignTouchBG (SCoord &b);
extern void newDE (LatLong &ll, const char grid[MAID_CHARLEN]);
extern void newDX (LatLong &ll, const char grid[MAID_CHARLEN], const char *override_prefix);
extern void drawDXPath(void);
extern void getTextBounds (const char str[], uint16_t *wp, uint16_t *hp);
extern uint16_t getTextWidth (const char str[]);
extern void normalizeLL (LatLong &ll);
extern bool screenIsLocked(void);
extern void fatalError (const char *fmt, ...);
extern time_t getUptime (uint16_t *days, uint8_t *hrs, uint8_t *mins, uint8_t *secs);
extern void eraseScreen(void);
extern void setMapTagBox (const char *tag, const SCoord &c, uint16_t r, SBox &box);
extern void drawMapTag (const char *tag, SBox &box);
extern void setDXPrefixOverride (char p[MAX_PREF_LEN]);
extern bool getDXPrefix (char p[MAX_PREF_LEN+1]);
extern void drawScreenLock(void);
extern void call2Prefix (const char *call, char prefix[MAX_PREF_LEN]);
extern void setOnAir (bool on);
extern void drawCallsign (bool all);
extern void logState (void);
extern const char *hc_version;
extern void fillSBox (const SBox &box, uint16_t color);
extern void drawSBox (const SBox &box, uint16_t color);







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
 * askNewPos.cpp
 *
 */

extern bool askNewPos (const SBox &b, LatLong &ll, char grid[MAID_CHARLEN]);





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
extern bool brControlOk(void);
extern bool brOnOffOk(void);
extern bool found_phot;





/*********************************************************************************************
 *
 * cities.cpp
 *
 */
extern void readCities(void);
extern const char *getNearestCity (const LatLong &ll, LatLong &city_ll);




/*********************************************************************************************
 *
 * clocks.cpp
 *
 */


enum {
    DETIME_INFO,                // lots of goodies
    DETIME_ANALOG,              // just analog clock
    DETIME_CAL,                 // calendar
    DETIME_ANALOG_DTTM,         // analog with date info
    DETIME_DIGITAL_12,          // 12 hour digital with date info
    DETIME_DIGITAL_24,          // 24 hour digital
    DETIME_N,
};

extern const char *detime_names[DETIME_N];
extern uint8_t de_time_fmt;     // one of DETIME_*
extern void initTime(void);
extern time_t nowWO(void);
extern void updateClocks(bool all);
extern bool clockTimeOk(void);
extern void changeTime (time_t t);
extern bool checkClockTouch (SCoord &s);
extern bool TZMenu (TZInfo &tzi, const LatLong &ll);
extern void enableSyncProvider(void);
extern void drawDESunRiseSetInfo(void);
extern void drawCalendar(bool force);
extern void hideClocks(void);
extern void showClocks(void);
extern void drawDXSunRiseSetInfo(void);
extern int DEWeekday(void);
extern int32_t utcOffset(void);
extern const char *gpsd_server, *ntp_server;




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
 * dxcluster.cpp
 *
 */

#define MAX_SPOTCALL_LEN                12      // including \0
#define MAX_SPOTGRID_LEN                MAID_CHARLEN
typedef struct {
    char de_call[MAX_SPOTCALL_LEN];     // DE call
    char dx_call[MAX_SPOTCALL_LEN];     // DX call
    char de_grid[MAX_SPOTGRID_LEN];     // DE grid
    char dx_grid[MAX_SPOTGRID_LEN];     // DX grid
    float dx_lat, dx_lng;               // dx location, rads +N +E
    float de_lat, de_lng;               // de location, rads +N +E
    float kHz;                          // freq
    SBox map_b;                         // map label
    uint16_t utcs;                      // UTC spotted 100*hr+min
} DXClusterSpot;

extern bool updateDXCluster(const SBox &box);
extern void closeDXCluster(void);
extern bool checkDXClusterTouch (const SCoord &s, const SBox &box);
extern bool getDXClusterSpots (DXClusterSpot **spp, uint8_t *nspotsp);
extern bool overAnyDXClusterSpots(const SCoord &s);
extern void drawDXClusterSpotsOnMap (void);
extern void updateDXClusterSpotScreenLocations(void);
extern bool isDXClusterConnected(void);
extern bool sendDXClusterDELLGrid(void);
extern bool getClosestDXCluster (const LatLong &ll, DXClusterSpot *sp, LatLong *llp);







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
extern uint8_t show_km;                 // show prop path distance in km, else miles
extern uint8_t show_lp;                 // show prop long path, else short path
#define ERAD_M  3959.0F                 // earth radius, miles

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
extern bool s2ll (uint16_t x, uint16_t y, LatLong &ll);
extern bool s2ll (const SCoord &s, LatLong &ll);
extern void solveSphere (float A, float b, float cc, float sc, float *cap, float *Bp);
extern bool checkDistTouch (const SCoord &s);
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
extern bool desiredBearing (const LatLong &ll, float &bear);






/*********************************************************************************************
 *
 * BME280.cpp
 *
 */

// pack ESP into words to save almost 2 kB

#define BMEPACK_T(t)            (round((t)*50))
#define BMEPACK_hPa(p)          (round((p)*10))
#define BMEPACK_inHg(p)         (round((p)*100))
#define BMEPACK_H(h)            (round((h)*100))
#define BMEUNPACK_T(t)          ((t)/50.0F)
#define BMEUNPACK_P(p)          (useMetricUnits() ? ((p)/10.0F) : ((p)/100.0F))
#define BMEUNPACK_H(h)          ((h)/100.0F)

// measurement queues
#define N_BME_READINGS          100     // n measurements stored for each sensor
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
extern void updateBMEStats(void);




/*********************************************************************************************
 *
 * earthsat.cpp
 *
 */

extern void updateSatPath(void);
extern void drawSatPathAndFoot(void);
extern void updateSatPass(void);
extern bool querySatSelection(void);
extern void strncpySubChar (char to_str[], const char from_str[], char to_char, char from_char, int maxlen);
extern bool checkSatMapTouch (const SCoord &s);
extern bool checkSatNameTouch (const SCoord &s);
extern void displaySatInfo(void);
extern void setSatObserver (float lat, float lng);
extern void drawSatPointsOnRow (uint16_t r);
extern void drawSatNameOnRow(uint16_t y);
extern void drawOneTimeDX(void);
extern void drawOneTimeDE(void);
extern bool dx_info_for_sat;
extern bool setSatFromName (const char *new_name);
extern bool setSatFromTLE (const char *name, const char *t1, const char *t2);
extern bool initSatSelection(void);
extern bool getSatAzElNow (char *name, float *azp, float *elp, float *rangep, float *ratep,
        float *razp, float *sazp, float *rdtp, float *sdtp);
extern bool isNewPass(void);
extern bool isSatMoon(void);
extern const char **getAllSatNames(void);
extern int nextSatRSEvents (time_t **rises, float **raz, time_t **sets, float **saz);
extern void drawDXSatMenu(const SCoord &s);

#define SAT_NOAZ        (-999)  // error flag
#define SAT_MIN_EL      0.0F    // rise elevation
#define TLE_LINEL       70      // including EOS






/*********************************************************************************************
 *
 * favicon.cpp
 *
 */

#if defined(_IS_UNIX)

extern void writeFavicon (WiFiClient &client);

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
extern bool getGimbalState (bool &vis_now, bool &has_el, bool &tracking, float &az, float &el);






/*********************************************************************************************
 *
 * gpsd.cpp
 *
 */

extern bool getGPSDLatLong(LatLong *llp);
extern time_t getGPSDUTC(const char **server);
extern void updateGPSDLoc(void);




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
extern void ll2KD3Node (const LatLong &ll, KD3Node &n);
extern void KD3Node2ll (const KD3Node &n, LatLong &ll);
extern float nearestKD3Dist2Miles(float d);




/*********************************************************************************************
 *
 * setup.cpp
 *
 */


typedef enum {
    DF_MDY,
    DF_DMY,
    DF_YMD
} DateFormat;

#define N_DXCLCMDS              4               // n dx cluster commands


// N.B. must match colsel_pr[] order
typedef enum {
    SATPATH_CSPR,
    SATFOOT_CSPR,
    SHORTPATH_CSPR,
    LONGPATH_CSPR,
    GRID_CSPR,
#if !defined(_SUPPORT_PSKESP)
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
#endif
    N_CSPR
} CSIds;


extern void clockSetup(void);
extern const char *getWiFiSSID(void);
extern const char *getWiFiPW(void);
extern const char *getCallsign(void);
extern const char *getDXClusterHost(void);
extern int getDXClusterPort(void);
extern bool setDXCluster (char *host, const char *port_str, char ynot[]);
extern int getDXClusterPort(void);
extern bool useMetricUnits(void);
extern bool useGeoIP(void);
extern bool useGPSDTime(void);
extern bool useGPSDLoc(void);
extern bool labelDXClusterSpots(void);
extern bool plotSpotCallsigns(void);
extern bool rotateScreen(void);
extern float getBMETempCorr(int i);
extern float getBMEPresCorr(int i);
extern bool setBMETempCorr(BMEIndex i, float delta);
extern bool setBMEPresCorr(BMEIndex i, float delta);
extern const char *getGPSDHost(void);
extern bool useLocalNTPHost(void);
extern bool GPIOOk(void);
extern const char *getLocalNTPHost(void);
extern bool useDXCluster(void);
extern uint32_t getKX3Baud(void);
extern void drawStringInBox (const char str[], const SBox &b, bool inverted, uint16_t color);
extern bool logUsageOk(void);
extern uint16_t getMapColor (CSIds cid);
extern const char* getMapColorName (CSIds cid);
extern uint8_t getBrMax(void);
extern uint8_t getBrMin(void);
extern bool getX11FullScreen(void);
extern bool latSpecIsValid (const char *lng_spec, float &lng);
extern bool lngSpecIsValid (const char *lng_spec, float &lng);
extern bool getDemoMode(void);
extern void setDemoMode(bool on);
extern int16_t getCenterLng(void);
extern void setCenterLng(int16_t);
extern DateFormat getDateFormat(void);
extern bool getRigctld (char host[], int *portp);
extern bool getRotctld (char host[], int *portp);
extern bool getFlrig (char host[], int *portp);
extern const char *getDXClusterLogin(void);
extern bool getDXSpotPaths(void);
extern bool setMapColor (const char *name, uint16_t rgb565);
extern void getDXClCommands(const char *cmds[N_DXCLCMDS], bool on[N_DXCLCMDS]);
extern bool getSatPathDashed(void);
extern bool useMagBearing(void);
extern bool setWSJTDX(void);
extern bool useWSJTX(void);
extern bool weekStartsOnMonday(void);










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


typedef enum {
    PROP_MAP_80M,
    PROP_MAP_40M,
    PROP_MAP_30M,
    PROP_MAP_20M,
    PROP_MAP_17M,
    PROP_MAP_15M,
    PROP_MAP_12M,
    PROP_MAP_10M,
    PROP_MAP_N
} PropMapSetting;
#define PROP_MAP_OFF    PROP_MAP_N      // handy alias meaning none active

extern PropMapSetting prop_map;


// N.B. must be in same order as map_styles[]
typedef enum {
    CM_COUNTRIES,
    CM_TERRAIN,
    CM_DRAP,
    CM_MUF,
    CM_AURORA,
    CM_N
} CoreMaps;
#define CM_NONE CM_N                    // handy alias meaning none active

extern CoreMaps core_map;               // current map, if any
extern const char *map_styles[CM_N];    // core map style names

extern SBox mapscale_b;                 // map scale box

extern void initCoreMaps(void);
extern bool installFreshMaps(void);
extern float propMap2MHz (PropMapSetting pms);
extern int propMap2Band (PropMapSetting pms);
extern bool getMapDayPixel (uint16_t row, uint16_t col, uint16_t *dayp);
extern bool getMapNightPixel (uint16_t row, uint16_t col, uint16_t *nightp);
extern const char *getMapStyle (char s[]);
extern void drawMapScale(void);
extern void eraseMapScale(void);
extern bool mapScaleIsUp(void);
extern void mapMsg (uint32_t dwell_ms, const char *fmt, ...);


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



/*******************************************************************************************n
 *
 * moonpane.cpp and moon_imgs.cpp
 *
 */

extern void updateMoonPane (bool force);
extern void drawMoonElPlot (void);
extern const uint16_t moon_image[HC_MOON_W*HC_MOON_H] PROGMEM;










/*********************************************************************************************
 *
 * ncdxf.cpp
 *
 */

#define NCDXF_B_NFIELDS         4       // n fields in NCDXF_b
#define NCDXF_B_MAXLEN          10      // max field length

extern void updateBeacons (bool immediate);
extern void updateBeaconScreenLocations(void);
extern bool overAnyBeacon (const SCoord &s);
extern void doNCDXFStatsTouch (const SCoord &s, PlotChoice pcs[NCDXF_B_NFIELDS]);
extern void drawBeaconKey(void);
extern void doNCDXFBoxTouch (const SCoord &s);
extern void drawNCDXFBox(void);
extern void initBRBRotset(void);
extern void drawNCDXFStats (const char titles[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN],
                          const char values[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN],
                          const uint16_t colors[NCDXF_B_NFIELDS]);






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
    NV_DE_DST,                  // deprecated
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
    NV_DIST_KM,                 // whether DE-DX distance to be km or miles

    NV_UTC_OFFSET,              // offset from UTC, seconds
    NV_PLOT_1,                  // Pane 1 PlotChoice
    NV_PLOT_2,                  // Pane 2 PlotChoice
    NV_BRB_ROTSET,              // Beacon box mode bit mask
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
    NV_TEMPCORR,                // BME280 76 temperature correction, NV_METRIC_ON units
    NV_GPSDHOST,                // gpsd daemon host name
    NV_KX3BAUD,                 // KX3 baud rate or 0

    NV_BCPOWER,                 // VOACAP power, watts
    NV_CD_PERIOD,               // stopwatch count down period, seconds
    NV_PRESCORR,                // BME280 76 pressure correction, NV_METRIC_ON units
    NV_BR_IDLE,                 // idle period, minutes
    NV_BR_MIN,                  // minimum brightness, percent of display range

    NV_BR_MAX,                  // maximum brightness, percent of display range
    NV_DE_TZ,                   // DE offset from UTC, seconds
    NV_DX_TZ,                   // DX offset from UTC, seconds
    NV_MAPSTYLE,                // base name of map background images
    NV_USEDXCLUSTER,            // whether to attempt using a DX cluster

    NV_USEGPSD,                 // bit 1: use gpsd for time, bit 2: use for location
    NV_LOGUSAGE,                // whether to phone home with clock settings
    NV_MAPSPOTS,                // DX spot annotations: 0=none; 1=just prefix; 2=full call; |= 4 path
    NV_WIFI_PASSWD,             // WIFI password
    NV_NTPSET,                  // whether to use NV_NTPHOST

    NV_NTPHOST,                 // user defined NTP host name
    NV_GPIOOK,                  // whether ok to use GPIO pins
    NV_SATPATHCOLOR,            // satellite path color as RGB 565
    NV_SATFOOTCOLOR,            // satellite footprint color as RGB 565
    NV_X11FLAGS,                // set if want full screen

    NV_BCFLAGS,                 // Big Clock bitmask: 1=date;2=wx;4=dig;8=12hr;16=nosec;32=UTC;64=an+dig;128=hrs;256=SpWx;512=hands
    NV_DAILYONOFF,              // 7 2-byte on times then 7 off times, each mins from midnight
    NV_TEMPCORR2,               // BME280 77 temperature correction, NV_METRIC_ON units
    NV_PRESCORR2,               // BME280 77 pressure correction, NV_METRIC_ON units
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
    NV_DASHED,                  // CSIds bitmask set for dashed
    NV_BEAR_MAG,                // show magnetic bearings, else true
    NV_WSJT_SETSDX,             // whether WSJT-X spots set DX
    NV_WSJT_DX,                 // whether dx cluster is WSJT-X
    NV_PSK_MAXAGE,              // live spots max age, minutes
    NV_WEEKMON,                 // whether week starts on Monday

    NV_N

} NV_Name;

// string valued lengths including trailing EOS
#define NV_WIFI_SSID_LEN        32
#define NV_WIFI_PW_LEN_OLD      32
#define NV_CALLSIGN_LEN         12
#define NV_SATNAME_LEN          9
#define NV_DXHOST_LEN           26
#define NV_GPSDHOST_LEN         18
#define NV_NTPHOST_LEN          18
#define NV_MAPSTYLE_LEN         10
#define NV_WIFI_PW_LEN          64
#define NV_DAILYONOFF_LEN       28      // (2*DAYSPERWEEK*sizeof(uint16_t))
#define NV_DE_GRID_LEN          MAID_CHARLEN
#define NV_DX_GRID_LEN          MAID_CHARLEN
#define NV_ROTHOST_LEN          18
#define NV_RIGHOST_LEN          18
#define NV_FLRIGHOST_LEN        18
#define NV_DXLOGIN_LEN          12
#define NV_DXCLCMD_LEN          35



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
 * plot.cpp
 *
 */

#define BMTRX_ROWS      24                              // time: UTC 0 .. 23
#define BMTRX_COLS      PROP_MAP_N                      // bands: 80-40-30-20-17-15-12-10
typedef uint8_t BandMatrix[BMTRX_ROWS][BMTRX_COLS];     // percent circuit reliability

extern void plotBandConditions (const SBox &box, int busy, const BandMatrix *bmp, char *config_str);
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
 * plotmgmnt.cpp
 *
 */


extern SBox plot_b[PANE_N];                     // box for each pane
extern PlotChoice plot_ch[PANE_N];              // current choice in each pane
extern const char *plot_names[PLOT_CH_N];       // must be in same order as PlotChoice
extern time_t plot_rotationT[PANE_N];           // time of next rotation, iff > 1 bit set in rotset[i]
extern uint32_t plot_rotset[PANE_N];            // bitmask of all PlotChoice rotation choices
                                                // N.B. plot_rotset[i] must always include plot_ch[i]

#define PLOT_ROT_WARNING        5               // show rotation about to occur, secs

#define paneIsRotating(pp)    ((plot_rotset[pp] & ~(1 << plot_ch[pp])) != 0)   // any bit other than plot_ch

extern void insureCountdownPaneSensible(void);
extern bool checkPlotTouch (const SCoord &s, PlotPane pp, TouchType tt);
extern PlotChoice askPaneChoice(PlotPane pp);
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
extern bool waitForTap (const SBox &inbox, bool (*fp)(void), uint32_t to_ms, bool update_clocks, SCoord &tap);





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
    PSKMB_PSK = 1,                      // data is from PSK, else WSPR
    PSKMB_CALL = 2,                     // using call, else grid
    PSKMB_OFDE = 4,                     // spot of DE, else by DE
} PSKModeBits;

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
    LatLong ll;
    long Hz;
    int snr;
} PSKReport;

extern uint8_t psk_mask;                // bitmask of PSKModeBits

extern bool updatePSKReporter (void);
extern bool checkPSKTouch (const SCoord &s, const SBox &box);
extern void drawPSKPane (const SBox &box);
extern void initPSKState(void);
extern void savePSKState(void);

#if !defined(_SUPPORT_PSKESP)

// only UNIX adds the followsing:


#define PSK_DOTR       2                // end point marker radius (also use by dxcluster)

extern uint32_t psk_bands;              // bitmask of 1 << PSKBandSetting

extern uint16_t getBandColor (long Hz);
extern void drawPSKPaths (void);
extern bool getClosestPSK (const LatLong &ll, const PSKReport **rpp);



#endif // !_SUPPORT_PSKESP



/*********************************************************************************************
 *
 * radio.cpp
 *
 */

void setRadioSpot (float kHz);




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
 * sphere.cpp
 *
 */

extern void solveSphere (float A, float b, float cc, float sc, float *cap, float *Bp);



/*********************************************************************************************
 *
 * touch.cpp
 *
 */

extern void calibrateTouch(bool force);
extern void drainTouch(void);
extern TouchType readCalTouch (SCoord &s);

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






/*********************************************************************************************
 *
 * tz.cpp
 *
 */
extern int32_t getTZ (const LatLong &ll);



/*********************************************************************************************
 *
 * webserver.cpp
 *
 */

extern char *trim (char *str);
extern bool initWebServer(char ynot[]);
extern void checkWebServer(bool ro);
extern TouchType readCalTouchWS (SCoord &s);
extern const char platform[];
extern void runNextDemoCommand(void);
extern time_t last_live;



/*********************************************************************************************
 *
 * wifi.cpp
 *
 */


typedef struct {
    const char *server;                         // name of server
    int rsp_time;                               // last known response time, millis()
} NTPServer;
#define NTP_TOO_LONG 5000U                      // too long response time, millis()

#define SPW_ERR (-9999)                         // bad cookie value for space weather stats

extern void initSys (void);
extern void initWiFiRetry(void);
extern void scheduleNewBC(void);
extern void scheduleNewPSK(void);
extern void scheduleNewVOACAPMap(PropMapSetting pm);
extern void scheduleNewCoreMap(CoreMaps cm);
extern void updateWiFi(void);
extern bool checkBCTouch (const SCoord &s, const SBox &b);
extern bool setPlotChoice (PlotPane new_pp, PlotChoice new_ch);
extern bool getChar (WiFiClient &client, char *cp);
extern time_t getNTPUTC(const char **server);
extern void scheduleRSSNow(void);
extern void checkBandConditions (const SBox &b, bool force);
extern bool getTCPLine (WiFiClient &client, char line[], uint16_t line_len, uint16_t *ll);
extern void sendUserAgent (WiFiClient &client);
extern bool wifiOk(void);
extern void httpGET (WiFiClient &client, const char *server, const char *page);
extern void httpHCGET (WiFiClient &client, const char *server, const char *hc_page);
extern void httpHCPGET (WiFiClient &client, const char *server, const char *hc_page_progmem);
extern bool httpSkipHeader (WiFiClient &client);
extern bool httpSkipHeader (WiFiClient &client, uint32_t *lastmodp);
extern void FWIFIPR (WiFiClient &client, const __FlashStringHelper *str);
extern void FWIFIPRLN (WiFiClient &client, const __FlashStringHelper *str);
extern int getNTPServers (const NTPServer **listp);
extern bool setRSSTitle (const char *title, int &n_titles, int &max_titles);
extern bool checkSpaceStats (time_t t0);
extern void doSpaceStatsTouch (const SCoord &s);
extern void drawSpaceStats(void);

extern uint16_t bc_power;
extern uint8_t bc_utc_tl;
extern uint8_t rss_interval;

extern void getSpaceWeather (SPWxValue &ssn, SPWxValue &sflux, SPWxValue &kp, SPWxValue &swind, 
    SPWxValue &drap, NOAASpaceWx &noaaspw, time_t &noaaspw_age, char xray[], time_t &xray_age,
    float pathrel[PROP_MAP_N], time_t &pathrel_age);




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



/*********************************************************************************************
 *
 * zones.cpp
 *
 */

#if defined(_SUPPORT_ZONES)

typedef enum {
    ZONE_CQ,
    ZONE_ITU
} ZoneID;

extern bool findZoneNumber (ZoneID id, const SCoord &s, int *zone_n);
extern void updateZoneSCoords(ZoneID id);
extern void drawZone (ZoneID id, uint16_t color, int n_only);

#endif // _SUPPORT_ZONES



#endif // _HAMCLOCK_H
