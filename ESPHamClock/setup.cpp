/* handle the initial setup screen.
 */

#include <ctype.h>

#include "HamClock.h"

/* defaults
 */
#define DEF_SSID        "FiOS-9QRT4-Guest"
#define DEF_PASS        "Veritium2017"

// feature tests.

// ESP always needs wifi setup, linux is up to user, others never
#if defined(_IS_ESP8266)
#define _WIFI_ALWAYS
#elif defined(_IS_LINUX)
#include <string.h>
#include <errno.h>
#define _WIFI_ASK
#else
#define _WIFI_NEVER
#endif


// debugs: force all on just for visual testing, and show bounds
// #define _SHOW_ALL                    // RBF
// #define _MARK_BOUNDS                 // RBF
#if defined(_SHOW_ALL) || defined(_MARK_BOUNDS)
#warning _SHOW_ALL and/or _MARK_BOUNDS are set
#endif
#ifdef _SHOW_ALL
    #undef _WIFI_NEVER
    #undef _WIFI_ASK
    #define _WIFI_ALWAYS
    #define _SUPPORT_FLIP
    #define _SUPPORT_KX3 
    #define _SUPPORT_NATIVE_GPIO
    #define _SUPPORT_ADIFILE
    #define _SUPPORT_SPOTPATH
    #define _SUPPORT_SCROLLLEN
#endif // _SHOW_ALL


// static storage for published setup items
static char wifi_ssid[NV_WIFI_SSID_LEN];
static char wifi_pw[NV_WIFI_PW_LEN];
static char call_sign[NV_CALLSIGN_LEN];
static char dx_login[NV_DXLOGIN_LEN];
static char dx_host[NV_DXHOST_LEN];
static char rot_host[NV_ROTHOST_LEN];
static char rig_host[NV_RIGHOST_LEN];
static char flrig_host[NV_FLRIGHOST_LEN];
static char gpsd_host[NV_GPSDHOST_LEN];
static char ntp_host[NV_NTPHOST_LEN];
static uint8_t bright_min, bright_max;
static uint16_t dx_port;
static uint16_t rig_port, rot_port, flrig_port;
static float temp_corr[MAX_N_BME];
static float pres_corr[MAX_N_BME];
static int16_t center_lng;
static int16_t alt_center_lng;
static bool alt_center_lng_set;
static char dxcl_cmds[N_DXCLCMDS][NV_DXCLCMD_LEN];
static char dx_wlist[NV_DXWLIST_LEN];
static char adif_fn[NV_ADIFFN_LEN];
static char i2c_fn[NV_I2CFN_LEN];


// layout constants
#define NQR             4                       // number of virtual keyboard rows
#define NQC             13                      // max number of keyboard columns
#define KB_CHAR_H       56                      // height of box containing 1 keyboard character
#define KB_CHAR_W       59                      // width "
#define KB_SPC_Y        (KB_Y0+NQR*KB_CHAR_H)   // top edge of special keyboard chars
#define KB_SPC_H        35                      // heights of special keyboard chars
#define KB_INDENT       16                      // keyboard indent
#define SBAR_X          (KB_INDENT+3*KB_CHAR_W/2)// space bar x coord
#define SBAR_W          (KB_CHAR_W*10)          // space bar width
#define F_DESCENT       5                       // font descent below baseline
#define TF_INDENT       10                      // top row font indent within square
#define BF_INDENT       30                      // bottom font indent within square
#define KB_Y0           220                     // y coord of keyboard top
#define PR_W            18                      // width of character
#define PR_A            24                      // ascending height above font baseline
#define PR_D            9                       // descending height below font baseline
#define PR_H            (PR_A+PR_D)             // prompt height
#define ASK_TO          10                      // user option timeout, seconds
#define PAGE_W          120                     // page button width
#define PAGE_H          33                      // page button height
#define CURSOR_DROP     2                       // pixels to drop cursor
#define NVMS_MKMSK      0x3                     // NV_MAPSPOTS mark mask
#define NVMS_NONE       0                       // NV_MAPSPOTS & MKMSK value to not mark spots
#define NVMS_PREFIX     1                       // NV_MAPSPOTS & MKMSK value to mark spots with prefix
#define NVMS_CALL       2                       // NV_MAPSPOTS & MKMSK value to mark spots with call_sign
#define NVMS_DOT        3                       // NV_MAPSPOTS & MKMSK value to mark spots with dots
#define NVMS_THIN       0x4                     // NV_MAPSPOTS bit to use THINPATHSZ
#define NVMS_WIDE       0x8                     // NV_MAPSPOTS bit to use WIDEPATHSZ
#define R2Y(r)          ((r)*(PR_H+2))          // macro given row index from 0 return screen y

// color selector constants
#define CSEL_SCX        435                     // all color control scales x coord
#define CSEL_COL1X      2                       // tick boxes in column 1 x
#define CSEL_COL2X      415                     // tick boxes in column 2 x
#define CSEL_SCY        45                      // top scale y coord
#define CSEL_SCW        256                     // scale width -- lt this causes roundoff at end
#define CSEL_SCH        30                      // scale height
#define CSEL_SCYG       15                      // scale y gap
#define CSEL_VDX        20                      // gap dx to value number
#define CSEL_SCM_C      RA8875_WHITE            // scale marker color
#define CSEL_SCB_C      GRAY                    // scale slider border color
#define CSEL_PDX        60                      // prompt dx from tick box x
#define CSEL_PW         140                     // width
#define CSEL_DDX        195                     // demo strip dx from tick box x
#define CSEL_DW         150                     // demo strip width
#define CSEL_DH         6                       // demo strip height
#define CSEL_TBCOL      RA8875_RED              // tick box active color
#define CSEL_TBSZ       20                      // tick box size
#define CSEL_TBDY       5                       // tick box y offset from R2Y
#define CSEL_NDASH      11                      // n segments in dashed color sample
#define CSEL_DDY        12                      // demo strip dy down from rot top
#define CSEL_ADX        32                      // dashed tick box dx from tick box x

// OnOff layout constants
#define OO_Y0           150                     // top y
#define OO_X0           50                      // left x
#define OO_CI           50                      // OnOff label to first column indent
#define OO_CW           90                      // weekday column width
#define OO_RH           30                      // row height -- N.B. must be at least font height
#define OO_TO           20                      // extra title offset on top of table
#define OO_ASZ          10                      // arrow size
#define OO_DHX(d)       (OO_X0+OO_CI+(d)*OO_CW) // day of week to hours x
#define OO_CPLX(d)      (OO_DHX(d)+OO_ASZ)      // day of week to copy left x
#define OO_CPRX(d)      (OO_DHX(d)+OO_CW-OO_ASZ)// day of week to copy right x
#define OO_CHY          (OO_Y0-2)               // column headings y
#define OO_CPLY         (OO_Y0-OO_RH/2)         // copy left y
#define OO_CPRY         (OO_Y0-OO_RH/2)         // copy right y
#define OO_ONY          (OO_Y0+2*OO_RH-4)       // on row y
#define OO_OFFY         (OO_Y0+5*OO_RH-4)       // off row y
#define OO_TW           (OO_CI+OO_CW*DAYSPERWEEK)  // total width


// general colors
#define TX_C            RA8875_WHITE            // text color
#define BG_C            RA8875_BLACK            // overall background color
#define KB_C            RGB565(80,80,255)       // key border color
#define KF_C            RA8875_WHITE            // key face color
#define PR_C            RGB565(255,125,0)       // prompt color
#define DEL_C           RA8875_RED              // Delete color
#define DONE_C          RA8875_GREEN            // Done color
#define BUTTON_C        RA8875_CYAN             // option buttons color
#define CURSOR_C        RA8875_GREEN            // cursor color
#define ERR_C           RA8875_RED              // err msg color

// validation constants
#define MAX_BME_DTEMP   20
#define MAX_BME_DPRES   400                     // eg correction for 10k feet is 316 hPa


// NV_X11FLAGS bit defns
#define X11BIT_FULLSCREEN       0x1

// NV_USEGPD bit defns
#define USEGPSD_FORTIME_BIT     0x1
#define USEGPSD_FORLOC_BIT      0x2


// define a string prompt
typedef struct {
    uint8_t page;                               // page number, 0 .. N_PAGES-1
    SBox p_box;                                 // prompt box
    SBox v_box;                                 // value box
    const char *p_str;                          // prompt string
    char *v_str;                                // value string
    uint8_t v_len;                              // size of v_str including EOS
    uint16_t v_cx;                              // x coord of cursor
} StringPrompt;


// N.B. must match string_pr[] order
typedef enum {
    // page "1"
    CALL_SPR,
    LAT_SPR,
    LNG_SPR,
    GRID_SPR,
    GPSDHOST_SPR,
    WIFISSID_SPR,
    WIFIPASS_SPR,

    // page "2"
    DXWLIST_SPR,
    DXPORT_SPR,
    DXHOST_SPR,
    DXLOGIN_SPR,
    DXCLCMD0_SPR,
    DXCLCMD1_SPR,
    DXCLCMD2_SPR,
    DXCLCMD3_SPR,

    // page "3"
    RIGPORT_SPR,
    RIGHOST_SPR,
    ROTPORT_SPR,
    ROTHOST_SPR,
    FLRIGPORT_SPR,
    FLRIGHOST_SPR,
    NTPHOST_SPR,
    ADIFFN_SPR,

    // page "4"
    CENTERLNG_SPR,
    I2CFN_SPR,
    BME76_DT,
    BME76_DP,
    BME77_DT,
    BME77_DP,
    BRMIN_SPR,
    BRMAX_SPR,

    N_SPR
} SPIds; 


// string prompts for each page. N.B. must match SPIds order
static StringPrompt string_pr[N_SPR] = {

    // "page 1" -- index 0

    {0, { 10, R2Y(0), 70, PR_H}, { 90, R2Y(0), 270, PR_H}, "Call:",   call_sign, NV_CALLSIGN_LEN, 0}, 
    {0, { 90, R2Y(1),180, PR_H}, {270, R2Y(1), 110, PR_H}, "Enter DE Lat:", NULL, 0, 0},       // shadowed
    {0, {380, R2Y(1), 50, PR_H}, {430, R2Y(1), 120, PR_H}, "Lng:", NULL, 0, 0},                // shadowed
    {0, {560, R2Y(1), 60, PR_H}, {620, R2Y(1), 130, PR_H}, "Grid:", NULL, 0, 0},               // shadowed
    {0, {460, R2Y(2), 60, PR_H}, {520, R2Y(2), 280, PR_H}, "host:", gpsd_host, NV_GPSDHOST_LEN, 0},
    {0, { 90, R2Y(4), 60, PR_H}, {160, R2Y(4), 500, PR_H}, "SSID:", wifi_ssid, NV_WIFI_SSID_LEN, 0},
    {0, {670, R2Y(4),110, PR_H}, { 10, R2Y(5), 789, PR_H}, "Password:", wifi_pw, NV_WIFI_PW_LEN, 0},

    // "page 2" -- index 1

    {1, { 20, R2Y(1), 65, PR_H}, { 85, R2Y(1), 260, PR_H}, "watch:", dx_wlist, NV_DXWLIST_LEN, 0},
    {1, { 20, R2Y(2), 65, PR_H}, { 85, R2Y(2),  85, PR_H}, "port:", NULL, 0, 0},               // shadowed
    {1, { 20, R2Y(3), 65, PR_H}, { 85, R2Y(3), 260, PR_H}, "host:", dx_host, NV_DXHOST_LEN, 0},
    {1, { 20, R2Y(4), 65, PR_H}, { 85, R2Y(4), 260, PR_H}, "login:", dx_login, NV_DXLOGIN_LEN, 0},

    // 1 less that full width to avoid erasing border
    {1, {350, R2Y(2), 40, PR_H}, {390, R2Y(2), 409, PR_H}, NULL, dxcl_cmds[0], NV_DXCLCMD_LEN, 0},
    {1, {350, R2Y(3), 40, PR_H}, {390, R2Y(3), 409, PR_H}, NULL, dxcl_cmds[1], NV_DXCLCMD_LEN, 0},
    {1, {350, R2Y(4), 40, PR_H}, {390, R2Y(4), 409, PR_H}, NULL, dxcl_cmds[2], NV_DXCLCMD_LEN, 0},
    {1, {350, R2Y(5), 40, PR_H}, {390, R2Y(5), 409, PR_H}, NULL, dxcl_cmds[3], NV_DXCLCMD_LEN, 0},


    // "page 3" -- index 2

    {2, {150, R2Y(0), 60, PR_H}, {210, R2Y(0),  80, PR_H}, "port:", NULL, 0, 0},               // shadowed
    {2, {290, R2Y(0), 60, PR_H}, {380, R2Y(0), 280, PR_H}, "host:", rig_host, NV_RIGHOST_LEN, 0},
    {2, {150, R2Y(1), 60, PR_H}, {210, R2Y(1),  80, PR_H}, "port:", NULL, 0, 0},               // shadowed
    {2, {290, R2Y(1), 60, PR_H}, {380, R2Y(1), 400, PR_H}, "host:", rot_host, NV_ROTHOST_LEN, 0},
    {2, {150, R2Y(2), 60, PR_H}, {210, R2Y(2),  80, PR_H}, "port:", NULL, 0, 0},               // shadowed
    {2, {290, R2Y(2), 60, PR_H}, {380, R2Y(2), 400, PR_H}, "host:", flrig_host, NV_FLRIGHOST_LEN, 0},

    {2, {100, R2Y(4), 60, PR_H}, {160, R2Y(4), 330, PR_H}, "host:", ntp_host, NV_NTPHOST_LEN, 0},
    {2, {100, R2Y(5), 60, PR_H}, {160, R2Y(5), 330, PR_H}, "file:", adif_fn, NV_ADIFFN_LEN, 0},


    // "page 4" -- index 3

    {3, {10,  R2Y(0), 200, PR_H}, {250, R2Y(0),  70, PR_H}, "Map center lng:", NULL, 0, 0},     // shadowed

    {3, {350, R2Y(1),  70, PR_H}, {440, R2Y(1),  360,PR_H}, "name:", i2c_fn, NV_I2CFN_LEN, 0},

    {3, {100, R2Y(2), 240, PR_H}, {350, R2Y(2),  80, PR_H}, "BME280@76    dTemp:", NULL, 0, 0}, // shadowed
    {3, {440, R2Y(2), 80,  PR_H}, {530, R2Y(2),  80, PR_H}, "dPres:", NULL, 0, 0},              // shadowed
    {3, {100, R2Y(3), 240, PR_H}, {350, R2Y(3),  80, PR_H}, "BME280@77    dTemp:", NULL, 0, 0}, // shadowed
    {3, {440, R2Y(3), 80,  PR_H}, {530, R2Y(3),  80, PR_H}, "dPres:", NULL, 0, 0},              // shadowed

    {3, {10,  R2Y(5), 200, PR_H}, {250, R2Y(5),  80, PR_H}, "Brightness Min%:", NULL, 0, 0},    // shadowed
    {3, {350, R2Y(5),  90, PR_H}, {450, R2Y(5),  80, PR_H}, "Max%:", NULL, 0, 0},               // shadowed



    // "page 5" -- index 4

    // "page 6" -- index 5

    // color scale

    // "page 7" -- index 6

    // on/off table

};




// N.B. must match bool_pr[] order
typedef enum {
    // page "1"
    GPSDON_BPR,
    GPSDFOLLOW_BPR,
    GEOIP_BPR,
    WIFI_BPR,

    // page "2"
    CLUSTER_BPR,
    CLISWSJTX_BPR,
    DXCLCMD0_BPR,
    DXCLCMD1_BPR,
    DXCLCMD2_BPR,
    DXCLCMD3_BPR,

    // page "3"
    RIGUSE_BPR,
    ROTUSE_BPR,
    FLRIGUSE_BPR,
    NTPSET_BPR,
    ADIFSET_BPR,

    // page "4"
    GPIOOK_BPR,
    I2CON_BPR,
    KX3ON_BPR,
    KX3BAUD_BPR,

    // page "5"
    DATEFMT_MDY_BPR,
    DATEFMT_DMYYMD_BPR,
    LOGUSAGE_BPR,
    WEEKDAY1MON_BPR,
    DEMO_BPR,
    UNITS_BPR,
    BEARING_BPR,
    SPOTLBL_BPR,
    SPOTLBLCALL_BPR,
    SPOTPATH_BPR,
    SPOTPATHSZ_BPR,
    SCROLLDIR_BPR,
    SCROLLLEN_BPR,
    SCROLLBIG_BPR,
    X11_FULLSCRN_BPR,
    FLIP_BPR,

    N_BPR,
    NOMATE                                      // flag for ent_mate

} BPIds;

// values for SCROLLLEN_BPR and SCROLLBIG_BPR
#define NSCROLL_A       0
#define NSCROLL_B       10
#define NSCROLL_C       25
#define NSCROLL_D       50

// define a boolean prompt
typedef struct {
    uint8_t page;                               // page number, 0 .. N_PAGES-1
    SBox p_box;                                 // prompt box
    SBox s_box;                                 // state box, if t/f_str
    bool state;                                 // on or off
    const char *p_str;                          // prompt string, or NULL to use just f/t_str
    const char *f_str;                          // "false" string, or NULL
    const char *t_str;                          // "true" string, or NULL
    BPIds ent_mate;                             // entanglement partner, else N_BPR
} BoolPrompt;

/* bool prompts. N.B. must match BPIds order
 * N.B. some fields use two "entangled" bools to create 3 states
 */
static BoolPrompt bool_pr[N_BPR] = {

    // "page 1" -- index 0

    {0, { 90, R2Y(2), 180, PR_H}, {270, R2Y(2), 40,  PR_H}, false, "or use gpsd?", "No", "Yes", NOMATE},
    {0, {330, R2Y(2),  80, PR_H}, {410, R2Y(2), 40,  PR_H}, false, "follow?", "No", "Yes", NOMATE},
    {0, { 90, R2Y(3), 180, PR_H}, {270, R2Y(3), 40,  PR_H}, false, "or IP Geolocate?", "No", "Yes", NOMATE},
    {0, {10,  R2Y(4),  70, PR_H}, {100, R2Y(4), 30,  PR_H}, false, "WiFi?", "No", NULL, NOMATE},

    // "page 2" -- index 1

    {1, {10,  R2Y(0),  90, PR_H},  {100, R2Y(0), 50,  PR_H}, false, "Cluster?", "No", "Yes", NOMATE},
    {1, {200, R2Y(0),  90, PR_H},  {290, R2Y(0), 50,  PR_H}, false, "WSJT-X?", "No", "Yes", NOMATE},

    {1, {350, R2Y(2),   0, PR_H},  {350, R2Y(2), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE},
    {1, {350, R2Y(3),   0, PR_H},  {350, R2Y(3), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE},
    {1, {350, R2Y(4),   0, PR_H},  {350, R2Y(4), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE},
    {1, {350, R2Y(5),   0, PR_H},  {350, R2Y(5), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE},

    // "page 3" -- index 2

    {2, {10,  R2Y(0), 100, PR_H},  {100, R2Y(0),  50, PR_H}, false, "rigctld?", "No", "Yes", NOMATE},
    {2, {10,  R2Y(1), 100, PR_H},  {100, R2Y(1),  50, PR_H}, false, "rotctld?", "No", "Yes", NOMATE},
    {2, {10,  R2Y(2), 100, PR_H},  {100, R2Y(2),  50, PR_H}, false, "flrig?",   "No", "Yes", NOMATE},

    {2, {10,  R2Y(4),  90, PR_H},  {100, R2Y(4), 300, PR_H}, false, "NTP?", "Use default set of servers",
                                                                                                0, NOMATE},
    {2, {10,  R2Y(5),  90, PR_H},  {100, R2Y(5), 300, PR_H}, false, "ADIF?", "No", NULL, NOMATE},


    // "page 4" -- index 3

    {3, {10,  R2Y(1),  80, PR_H},  {100, R2Y(1), 110, PR_H}, false, "GPIO?", "Off", "Active", NOMATE},
    {3, {250, R2Y(1),  80, PR_H},  {350, R2Y(1), 70,  PR_H}, false, "I2C file?", "No", NULL, NOMATE},

    {3, {100, R2Y(4), 120, PR_H},  {250, R2Y(4),  120, PR_H}, false, "KX3?", "No", NULL, KX3BAUD_BPR},
    {3, {250, R2Y(4),   0, PR_H},  {250, R2Y(4),  120, PR_H}, false, NULL, "4800 bps", "38400 bps",KX3ON_BPR},
                                                // 3x entangled: Off: FX   4800: TF   38400: TT

    // "page 5" -- index 4

    {4, {10,  R2Y(0), 140, PR_H},  {150, R2Y(0), 150, PR_H}, false, "Date order?", "Mon Day Year", NULL,
                                                                                        DATEFMT_DMYYMD_BPR},
    {4, {150, R2Y(0), 140, PR_H},  {150, R2Y(0), 150, PR_H}, false, NULL, "Day Mon Year", "Year Mon Day",
                                                                                        DATEFMT_MDY_BPR},
                                                // 3x entangled: MDY: FX   DMY: TF  YMD:  TT


    {4, {400, R2Y(0), 140, PR_H},  {540, R2Y(0),  90, PR_H}, false, "Log usage?", "Opt-Out", "Opt-In",NOMATE},



    {4, {10,  R2Y(1), 140, PR_H},  {150, R2Y(1), 120, PR_H}, false, "Week starts?", "Sunday","Monday",NOMATE},
    {4, {400, R2Y(1), 140, PR_H},  {540, R2Y(1),  40, PR_H}, false, "Demo mode?", "No", "Yes", NOMATE},


    {4, {10,  R2Y(2), 140, PR_H},  {150, R2Y(2), 120, PR_H}, false, "Units?", "Imperial", "Metric", NOMATE},
    {4, {400, R2Y(2), 140, PR_H},  {540, R2Y(2), 120, PR_H}, false, "Bearings?","True N","Magnetic N",NOMATE},


    {4, {10,  R2Y(3), 140, PR_H},  {150, R2Y(3), 120, PR_H}, false,"Spot labels?","No","Dot",SPOTLBLCALL_BPR},
    {4, {150, R2Y(3), 140, PR_H},  {150, R2Y(3), 120, PR_H}, false, NULL, "Prefix", "Call", SPOTLBL_BPR},
                                                // 4x entangled: No: FF Dot: TF  Prefix: FT  Call: TT



    {4, {400, R2Y(3), 140, PR_H},  {540, R2Y(3), 120, PR_H}, false, "Spot paths?", "No", NULL,SPOTPATHSZ_BPR},
    {4, {540, R2Y(3), 140, PR_H},  {540, R2Y(3), 120, PR_H}, false, NULL, "Thin", "Wide", SPOTPATH_BPR},
                                                // 3x entangled: No: FX  Thin: TF  Wide: TT


    {4, {10,  R2Y(4), 140, PR_H},  {150, R2Y(4), 120, PR_H}, false, "Scroll dir?", "Bottom-Up", "Top-Down",
                                                                                                NOMATE},


    {4, {400, R2Y(4), 140, PR_H},  {540, R2Y(4), 120, PR_H}, false, "Scroll length?", "0","10",SCROLLBIG_BPR},
    {4, {540, R2Y(4), 140, PR_H},  {540, R2Y(4), 120, PR_H}, false, NULL, "25", "50", SCROLLLEN_BPR},
                                                // 4x entangled:  0: FF  10: TF   25: FT  50: TT
                                                // FF -> TF -> FT -> TT -> ...
                                                // N.B. match NSCROLL_X


    {4, {10 , R2Y(5), 140, PR_H},  {150, R2Y(5), 120, PR_H}, false, "Full scrn?", "No", "Yes", NOMATE},
                                                // N.B. state box must be wide enough for "Won't fit"

    {4, {400, R2Y(5), 140, PR_H},  {540, R2Y(5),  40, PR_H}, false, "Flip U/D?", "No", "Yes", NOMATE},



    // "page 6" -- index 5

    // color scale

    // "page 7" -- index 6

    // on/off table


};


// store info about a given string or bool focus field
typedef struct {
    // N.B. always one, the other NULL
    StringPrompt *sp;
    BoolPrompt *bp;
} Focus;

// whether to show on/off page
#if defined(_SHOW_ALL)
    #define HAVE_ONOFF()      1
#else
    #define HAVE_ONOFF()      (brDimmableOk() || brOnOffOk())
#endif

// current focus and page names
#define SPIDER_PAGE     1                       // 0-based counting
#define ALLBOOLS_PAGE   4                       // 0-based counting
#define COLOR_PAGE      5                       // 0-based counting
#define ONOFF_PAGE      6                       // 0-based counting
#define N_PAGES         (HAVE_ONOFF() ? 7 : 6)  // last page is on/off
#define SPIDER_TX       480                     // title x
#define SPIDER_TY       (R2Y(2) - PR_D)         // title y
#define SPIDER_BX       345                     // border x
#define SPIDER_BY       (SPIDER_TY - PR_A)      // border y
#define SPIDER_BRX      799                     // border right x
#define SPIDER_BBY      (R2Y(6))                // border bottom y
static Focus cur_focus;
static int cur_page;



/* color selector information.
 * since mouse is required it does not participate in tabbing or Focus.
 */

typedef struct {
    SBox p_box;                                 // prompt box
    SBox t_box;                                 // state tick box
    SBox d_box;                                 // demo patch box
    bool state;                                 // tick box on or off
    uint16_t def_c;                             // default color -- NOT the current color
    NV_Name nv;                                 // nvram location
    const char *p_str;                          // prompt string
    SBox a_box;                                 // dashed control tick box, .x == 0 if not used
    bool a_state;                               // whether dashed is enabled
    uint8_t r, g, b;                            // current color in full precision color
} ColSelPrompt;

#define DASHOK(p)       (p.a_box.x > 0)         // test whether this color has a dash control option
#define NODASH(p)       do { p.a_box.x = 0; } while (0) // disable dash with this color


/* color selector controls and prompts.
 * N.B. must match ColorSelection order
 */
static ColSelPrompt csel_pr[N_CSPR] = {
    {{CSEL_COL1X+CSEL_PDX, R2Y(0), CSEL_PW, PR_H},
            {CSEL_COL1X, R2Y(0)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX, R2Y(0)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, DE_COLOR, NV_SHORTPATHCOLOR, "Short path",
            {CSEL_COL1X+CSEL_ADX, R2Y(0)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(1), CSEL_PW, PR_H},
            {CSEL_COL1X, R2Y(1)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX, R2Y(1)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(229,191,131), NV_LONGPATHCOLOR, "Long path",
            {CSEL_COL1X+CSEL_ADX, R2Y(1)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(2), CSEL_PW, PR_H},
            {CSEL_COL1X, R2Y(2)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX, R2Y(2)+CSEL_DDY, CSEL_DW, CSEL_DH},
            true, RGB565(175,38,127), NV_SATPATHCOLOR, "Sat path",
            {CSEL_COL1X+CSEL_ADX, R2Y(2)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(3), CSEL_PW, PR_H},
            {CSEL_COL1X, R2Y(3)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX, R2Y(3)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(236,193,79), NV_SATFOOTCOLOR, "Sat footprint",
            {0, 0, 0, 0}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(4), CSEL_PW, PR_H},
            {CSEL_COL1X, R2Y(4)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX, R2Y(4)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(44,42,99), NV_GRIDCOLOR, "Map grid",
            {0, 0, 0, 0}, false, 0, 0, 0},

#if defined(_IS_UNIX)

    // only UNIX supports drawing rotator direction on main map
    {{CSEL_COL1X+CSEL_PDX, R2Y(5), CSEL_PW, PR_H},
            {CSEL_COL1X, R2Y(5)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX, R2Y(5)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RA8875_WHITE, NV_ROTCOLOR, "Rotator",
            {0, 0, 0, 0}, false, 0, 0, 0},

#endif // _IS_UNIX

    {{CSEL_COL1X+CSEL_PDX, R2Y(6), CSEL_PW, PR_H},
            {CSEL_COL1X, R2Y(6)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX, R2Y(6)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(128,0,0), NV_160M_COLOR, "160 m",
            {CSEL_COL1X+CSEL_ADX, R2Y(6)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(7), CSEL_PW, PR_H},
            {CSEL_COL1X, R2Y(7)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX, R2Y(7)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(128,128,0), NV_80M_COLOR, "80 m",
            {CSEL_COL1X+CSEL_ADX, R2Y(7)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(8), CSEL_PW, PR_H},
            {CSEL_COL1X, R2Y(8)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX, R2Y(8)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(230,25,75), NV_60M_COLOR, "60 m",
            {CSEL_COL1X+CSEL_ADX, R2Y(8)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(9), CSEL_PW, PR_H},
            {CSEL_COL1X, R2Y(9)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX, R2Y(9)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(245,130,48), NV_40M_COLOR, "40 m",
            {CSEL_COL1X+CSEL_ADX, R2Y(9)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(10), CSEL_PW, PR_H},
            {CSEL_COL1X, R2Y(10)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX, R2Y(10)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(200,176,20), NV_30M_COLOR, "30 m",
            {CSEL_COL1X+CSEL_ADX, R2Y(10)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(11), CSEL_PW, PR_H},
            {CSEL_COL1X, R2Y(11)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX, R2Y(11)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(250,250,0), NV_20M_COLOR, "20 m",
            {CSEL_COL1X+CSEL_ADX, R2Y(11)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL2X+CSEL_PDX, R2Y(6), CSEL_PW, PR_H},
            {CSEL_COL2X, R2Y(6)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_DDX, R2Y(6)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(60,180,75), NV_17M_COLOR, "17 m",
            {CSEL_COL2X+CSEL_ADX, R2Y(6)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL2X+CSEL_PDX, R2Y(7), CSEL_PW, PR_H},
            {CSEL_COL2X, R2Y(7)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_DDX, R2Y(7)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(70,240,240), NV_15M_COLOR, "15 m",
            {CSEL_COL2X+CSEL_ADX, R2Y(7)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL2X+CSEL_PDX, R2Y(8), CSEL_PW, PR_H},
            {CSEL_COL2X, R2Y(8)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_DDX, R2Y(8)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(0,130,200), NV_12M_COLOR, "12 m",
            {CSEL_COL2X+CSEL_ADX, R2Y(8)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL2X+CSEL_PDX, R2Y(9), CSEL_PW, PR_H},
            {CSEL_COL2X, R2Y(9)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_DDX, R2Y(9)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(250,190,212), NV_10M_COLOR, "10 m",
            {CSEL_COL2X+CSEL_ADX, R2Y(9)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL2X+CSEL_PDX, R2Y(10), CSEL_PW, PR_H},
            {CSEL_COL2X, R2Y(10)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_DDX, R2Y(10)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(200,150,100), NV_6M_COLOR, "6 m",
            {CSEL_COL2X+CSEL_ADX, R2Y(10)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL2X+CSEL_PDX, R2Y(11), CSEL_PW, PR_H},
            {CSEL_COL2X, R2Y(11)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_DDX, R2Y(11)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, RGB565(100,100,100), NV_2M_COLOR, "2 m",
            {CSEL_COL2X+CSEL_ADX, R2Y(11)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},
};



// overall color selector box, easier than 3 separate boxes
static SBox csel_ctl_b = {CSEL_SCX, CSEL_SCY, CSEL_SCW, 3*CSEL_SCH + 3*CSEL_SCYG};

// handy conversions between x coord and value 0..255.
// N.B. only valid when x-CSEL_SCX ranges from 0 .. CSEL_SCW-1
#define X2V(x)  (255*((x)-CSEL_SCX)/(CSEL_SCW-1))
#define V2X(v)  (CSEL_SCX+(CSEL_SCW-1)*(v)/255)



// virtual qwerty keyboard
typedef struct {
    char normal, shifted;                               // normal and shifted char
} Key;
static const Key qwerty[NQR][NQC] PROGMEM = {
    { {'`', '~'}, {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'}, {'6', '^'},
      {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'}, {'-', '_'}, {'=', '+'}
    },
    { {'Q', 'q'}, {'W', 'w'}, {'E', 'e'}, {'R', 'r'}, {'T', 't'}, {'Y', 'y'}, {'U', 'u'},
      {'I', 'i'}, {'O', 'o'}, {'P', 'p'}, {'[', '{'}, {']', '}'}, {'\\', '|'},
    },
    { {'A', 'a'}, {'S', 's'}, {'D', 'd'}, {'F', 'f'}, {'G', 'g'}, {'H', 'h'}, {'J', 'j'},
      {'K', 'k'}, {'L', 'l'}, {';', ':'}, {'\'', '"'},
    },
    { {'Z', 'z'}, {'X', 'x'}, {'C', 'c'}, {'V', 'v'}, {'B', 'b'}, {'N', 'n'}, {'M', 'm'},
      {',', '<'}, {'.', '>'}, {'/', '?'},
    }
};


// horizontal pixel offset of each virtual keyboard row then each follows every KB_CHAR_W
static const uint8_t qroff[NQR] = {
    KB_INDENT,
    KB_INDENT,
    KB_INDENT+KB_CHAR_W,
    KB_INDENT+3*KB_CHAR_W/2
};

// special virtual keyboard chars
static const SBox delete_b  = {KB_INDENT, KB_SPC_Y, SBAR_X-KB_INDENT+1, KB_SPC_H};
static const SBox space_b   = {SBAR_X, KB_SPC_Y, SBAR_W, KB_SPC_H};
static const SBox done_b    = {SBAR_X+SBAR_W, KB_SPC_Y, SBAR_X-KB_INDENT+1, KB_SPC_H};
static const SBox page_b    = {800-PAGE_W-KB_INDENT-1, 1, PAGE_W, PAGE_H};

// note whether ll edited
static bool ll_edited;


// a few forward decls topo sort couldn't fix
static void eraseSPValue (const StringPrompt *sp);
static void drawSPValue (StringPrompt *sp);



/* return the value string of the given entangled pair.
 * N.B. B must be the forward ent_mate of A
 * 3x entangled: a: FX  b: TF  c: TT
 * 4x entangled: a: FF  b: TF  c: FT  d: TT
 */
static const char * getEntangledValue (const BoolPrompt *A, const BoolPrompt *B)
{
    if (B != &bool_pr[A->ent_mate])
        fatalError (_FX("bogus entangled pair %s %s\n"), A->p_str, B->f_str);

    const char *s;

    if (A->t_str) {
        // 4 states
        if (B->state)
            s = A->state ? B->t_str : B->f_str;
        else
            s = A->state ? A->t_str : A->f_str;
    } else {
        // 3 states
        if (A->state)
            s = B->state ? B->t_str : B->f_str;
        else
            s = A->f_str;
    }

    return (s);
}

/* log all string and bool settings
 */
static void logAllPrompts(void)
{
    // strings
    for (StringPrompt *sp = string_pr; sp < &string_pr[N_SPR]; sp++)
        if (sp->p_str)
            Serial.printf (_FX("Setup: %s = %s\n"), sp->p_str, sp->v_str ? sp->v_str : _FX("NULL"));

    // bools
    for (int i = 0; i < N_BPR; i++) {
        BoolPrompt *bp = &bool_pr[i];
        if (bp->ent_mate == i+1)                        // only print the forward-reference pair
            Serial.printf (_FX("Setup: %s = %s\n"), bp->p_str, getEntangledValue (bp, &bool_pr[i+1]));
        else if (bp->ent_mate == NOMATE && bp->p_str)
            Serial.printf (_FX("Setup: %s = %s\n"), bp->p_str,
                bp->state ? (bp->t_str ? bp->t_str : _FX("T-NULL")) : (bp->f_str ? bp->f_str :_FX("F-NULL")));
    }

    // on/off times
    uint16_t onoff[NV_DAILYONOFF_LEN];
    NVReadString (NV_DAILYONOFF, (char*)onoff);
    char oostr[40+6*DAYSPERWEEK];
    size_t sl = snprintf (oostr, sizeof(oostr), _FX("Setup: DAILYONOFF =  On"));
    for (int i = 0; i < DAYSPERWEEK; i++) {
        uint16_t on = onoff[i];
        sl += snprintf (oostr+sl, sizeof(oostr)-sl, _FX(" %02u:%02u"), on/60, on%60);
    }
    Serial.println (oostr);
    sl = snprintf (oostr, sizeof(oostr), _FX("Setup: DAILYONOFF = Off"));
    for (int i = 0; i < DAYSPERWEEK; i++) {
        uint16_t off = onoff[i+DAYSPERWEEK];
        sl += snprintf (oostr+sl, sizeof(oostr)-sl, _FX(" %02u:%02u"), off/60, off%60);
    }
    Serial.println (oostr);
}

/* prepare the shadowed prompts
 * N.B. if call this, then always call freeShadowedParams() eventually
 */
static void initShadowedParams()
{

    string_pr[LAT_SPR].v_str = (char*)malloc(string_pr[LAT_SPR].v_len = 9);
                                formatLat (de_ll.lat_d, string_pr[LAT_SPR].v_str, string_pr[LAT_SPR].v_len);
    string_pr[LNG_SPR].v_str = (char*)malloc(string_pr[LNG_SPR].v_len = 9);
                                formatLng (de_ll.lng_d, string_pr[LNG_SPR].v_str, string_pr[LNG_SPR].v_len);
    string_pr[GRID_SPR].v_str = (char*)malloc(string_pr[GRID_SPR].v_len = MAID_CHARLEN);
                                getNVMaidenhead (NV_DE_GRID, string_pr[GRID_SPR].v_str);
    snprintf (string_pr[DXPORT_SPR].v_str = (char*)malloc(8), string_pr[DXPORT_SPR].v_len = 8,
                                "%u", dx_port);
    snprintf (string_pr[RIGPORT_SPR].v_str = (char*)malloc(8), string_pr[RIGPORT_SPR].v_len = 8,
                                "%u", rig_port);

    snprintf (string_pr[ROTPORT_SPR].v_str = (char*)malloc(8), string_pr[ROTPORT_SPR].v_len = 8,
                                "%u", rot_port);
    snprintf (string_pr[FLRIGPORT_SPR].v_str = (char*)malloc(8), string_pr[FLRIGPORT_SPR].v_len = 8,
                                "%u", flrig_port);
    snprintf (string_pr[BME76_DT].v_str = (char*)malloc(8), string_pr[BME76_DT].v_len = 8,
                                "%.2f", temp_corr[BME_76]);
    snprintf (string_pr[BME76_DP].v_str = (char*)malloc(8), string_pr[BME76_DP].v_len = 8,
                                "%.3f", pres_corr[BME_76]);
    snprintf (string_pr[BME77_DT].v_str = (char*)malloc(8), string_pr[BME77_DT].v_len = 8,
                                "%.2f", temp_corr[BME_77]);

    snprintf (string_pr[BME77_DP].v_str = (char*)malloc(8), string_pr[BME77_DP].v_len = 8,
                                "%.3f", pres_corr[BME_77]);
    snprintf (string_pr[BRMIN_SPR].v_str = (char*)malloc(8), string_pr[BRMIN_SPR].v_len = 8,
                                "%u", bright_min);
    snprintf (string_pr[BRMAX_SPR].v_str = (char*)malloc(8), string_pr[BRMAX_SPR].v_len = 8,
                                "%u", bright_max);
    snprintf (string_pr[CENTERLNG_SPR].v_str = (char*)malloc(5), string_pr[CENTERLNG_SPR].v_len = 5,
                                "%.0f%c", fabsf((float)center_lng), center_lng < 0 ? 'W' : 'E');
                                // conversion to float just to avoid g++ snprintf size warning
}

/* free the shadowed parameters
 */
static void freeShadowedParams()
{
    free (string_pr[LAT_SPR].v_str);
    free (string_pr[LNG_SPR].v_str);
    free (string_pr[GRID_SPR].v_str);
    free (string_pr[DXPORT_SPR].v_str);
    free (string_pr[RIGPORT_SPR].v_str);

    free (string_pr[ROTPORT_SPR].v_str);
    free (string_pr[FLRIGPORT_SPR].v_str);
    free (string_pr[BME76_DT].v_str);
    free (string_pr[BME76_DP].v_str);
    free (string_pr[BME77_DT].v_str);

    free (string_pr[BME77_DP].v_str);
    free (string_pr[BRMIN_SPR].v_str);
    free (string_pr[BRMAX_SPR].v_str);
    free (string_pr[CENTERLNG_SPR].v_str);
}

/* set the given StringPrompt to a brief error message
 */
static void flagErrField (const StringPrompt *sp)
{
    eraseSPValue (sp);
    tft.setTextColor (ERR_C);
    tft.setCursor (sp->v_box.x, sp->v_box.y+sp->v_box.h-PR_D);
    tft.print (F("Err"));
}

/* format latitude into s[].
 */
void formatLat (float lat_d, char s[], int s_len)
{
    snprintf (s, s_len, _FX("%.3f%c"), fabsf(lat_d), lat_d < 0 ? 'S' : 'N');
}

/* format longitude into s[].
 */
void formatLng (float lng_d, char s[], int s_len)
{
    snprintf (s, s_len, _FX("%.3f%c"), fabsf(lng_d), lng_d < 0 ? 'W' : 'E');
}

/* update interaction if sp is one of LAT/LNG/GRID_SPR.
 * also set ll_edited.
 */
static void checkLLGEdit(const StringPrompt *sp)
{
    if (sp == &string_pr[LAT_SPR] || sp == &string_pr[LNG_SPR]) {

        // convert to grid if possible
        LatLong ll;
        if (latSpecIsValid (string_pr[LAT_SPR].v_str, ll.lat_d)
                        && lngSpecIsValid (string_pr[LNG_SPR].v_str, ll.lng_d)) {
            normalizeLL (ll);
            ll2maidenhead (string_pr[GRID_SPR].v_str, ll);
            eraseSPValue (&string_pr[GRID_SPR]);
            drawSPValue (&string_pr[GRID_SPR]);
        } else {
            flagErrField (&string_pr[GRID_SPR]);
        }

        ll_edited = true;

    } else if (sp == &string_pr[GRID_SPR]) {

        // convert to ll if possible
        LatLong ll;
        if (maidenhead2ll (ll, sp->v_str)) {
            formatLat (ll.lat_d, string_pr[LAT_SPR].v_str, string_pr[LAT_SPR].v_len);
            eraseSPValue (&string_pr[LAT_SPR]);
            drawSPValue (&string_pr[LAT_SPR]);
            formatLng (ll.lng_d, string_pr[LNG_SPR].v_str, string_pr[LNG_SPR].v_len);
            eraseSPValue (&string_pr[LNG_SPR]);
            drawSPValue (&string_pr[LNG_SPR]);
        } else {
            flagErrField (&string_pr[LAT_SPR]);
            flagErrField (&string_pr[LNG_SPR]);
        }

        ll_edited = true;
    }
}


/* remove blanks from s IN PLACE.
 */
static void noBlanks (char *s)
{
    char c, *s_to = s;
    while ((c = *s++) != '\0')
        if (c != ' ')
            *s_to++ = c;
    *s_to = '\0';
}

/* draw the Spider commands table header
 */
static void drawSpiderCommandsHeader()
{
    // border
    tft.drawLine (SPIDER_BX, SPIDER_BY, SPIDER_BRX, SPIDER_BY, GRAY);
    tft.drawLine (SPIDER_BX, SPIDER_BBY, SPIDER_BRX, SPIDER_BBY, GRAY);
    tft.drawLine (SPIDER_BX, SPIDER_BY, SPIDER_BX, SPIDER_BBY, GRAY);
    tft.drawLine (SPIDER_BRX, SPIDER_BY, SPIDER_BRX, SPIDER_BBY, GRAY);

    // labels
    tft.setTextColor (PR_C);
    tft.setCursor (SPIDER_TX, SPIDER_TY);
    tft.print ("Cluster Commands:");
}

static void drawPageButton()
{
    char buf[32];
    snprintf (buf, sizeof(buf), _FX("< Page %d >"), cur_page+1);      // user sees 1-based
    drawStringInBox (buf, page_b, false, DONE_C);
}

static void drawDoneButton(bool on)
{
    drawStringInBox ("Done", done_b, on, DONE_C);
}


/* return whether the given bool prompt is currently relevant
 */
static bool boolIsRelevant (BoolPrompt *bp)
{
    if (bp->page != cur_page)
        return (false);

#if !defined(_USE_X11) && !defined(_WEB_ONLY)
    if (bp == &bool_pr[X11_FULLSCRN_BPR])
        return (false);
#endif

    if (bp == &bool_pr[WIFI_BPR]) {
        #if defined(_WIFI_ALWAYS) || defined(_WIFI_NEVER)
            return (false);
        #endif
        #if defined(_WIFI_ASK)
            return (true);
        #endif
    }

    if (bp == &bool_pr[CLISWSJTX_BPR]) {
        if (!bool_pr[CLUSTER_BPR].state)
            return (false);
    }

    #if !defined(_SUPPORT_SPOTPATH)
        if (bp == &bool_pr[SPOTPATH_BPR] || bp == &bool_pr[SPOTPATHSZ_BPR])
            return (false);
    #endif

    if (bp == &bool_pr[DXCLCMD0_BPR] || bp == &bool_pr[DXCLCMD1_BPR]
                    || bp == &bool_pr[DXCLCMD2_BPR] || bp == &bool_pr[DXCLCMD3_BPR]) {
        // only show if enabled and host is not WSJT
        if (!bool_pr[CLUSTER_BPR].state || bool_pr[CLISWSJTX_BPR].state)
            return (false);
    }

    if (bp == &bool_pr[FLIP_BPR]) {
        #if !defined(_SUPPORT_FLIP)
            return (false);
        #endif
    }

    if (bp == &bool_pr[KX3ON_BPR]) {
        #if !defined(_SUPPORT_KX3)
            return (false);
        #else
            return (bool_pr[GPIOOK_BPR].state);
        #endif
    }

    if (bp == &bool_pr[KX3BAUD_BPR]) {
        #if !defined(_SUPPORT_KX3)
            return (false);
        #else
            if (!bool_pr[KX3ON_BPR].state || !bool_pr[GPIOOK_BPR].state)
                return (false);
        #endif
    }

    if (bp == &bool_pr[DATEFMT_DMYYMD_BPR]) {
        // this test works correctly for display purposes, but prevents tabbing into DMYYMD if MDY is false
        if (!bool_pr[DATEFMT_MDY_BPR].state)
            return (false);
    }

    if (bp == &bool_pr[I2CON_BPR]) {
        #if defined(_I2C_ESP)
            return (false);
        #endif
    }

    if (bp == &bool_pr[GPIOOK_BPR]) {
        #if !defined(_SUPPORT_NATIVE_GPIO)
            return (false);
        #endif
    }

    if (bp == &bool_pr[GPSDFOLLOW_BPR]) {
        if (!bool_pr[GPSDON_BPR].state)
            return (false);
    }

    #if !defined(_SUPPORT_ADIFILE)
        // always irrelevant if not supporting ADIF file reading
        if (bp == &bool_pr[ADIFSET_BPR])
            return (false);
    #endif


    #if !defined(_SUPPORT_SCROLLLEN)
        // not allowed to change on ESP
        if (bp == &bool_pr[SCROLLLEN_BPR] || bp == &bool_pr[SCROLLBIG_BPR])
            return (false);
    #endif

    // use by default
    return (true);
}

/* return whether the given string prompt is currently relevant
 */
static bool stringIsRelevant (StringPrompt *sp)
{
    if (sp->page != cur_page)
        return (false);

    if (sp == &string_pr[WIFISSID_SPR] || sp == &string_pr[WIFIPASS_SPR]) {
        #if defined(_WIFI_NEVER)
            return (false);
        #endif
        #if defined(_WIFI_ASK)
            if (!bool_pr[WIFI_BPR].state)
                return (false);
        #endif
    }

    if (sp == &string_pr[DXHOST_SPR]) {
        if (!bool_pr[CLUSTER_BPR].state)
            return (false);
    }

    if (sp == &string_pr[DXPORT_SPR]) {
        if (!bool_pr[CLUSTER_BPR].state)
            return (false);
    }

    if (sp == &string_pr[DXCLCMD0_SPR] || sp == &string_pr[DXCLCMD1_SPR]
                    || sp == &string_pr[DXCLCMD2_SPR] || sp == &string_pr[DXCLCMD3_SPR]
                    || sp == &string_pr[DXLOGIN_SPR]) {
        // only show if enabled and not using WSJT
        if (!bool_pr[CLUSTER_BPR].state || bool_pr[CLISWSJTX_BPR].state)
            return (false);
    }

    if (sp == &string_pr[DXWLIST_SPR]) {
        if (!bool_pr[CLUSTER_BPR].state)
            return (false);
    }

    if (sp == &string_pr[RIGHOST_SPR] || sp == &string_pr[RIGPORT_SPR]) {
        if (!bool_pr[RIGUSE_BPR].state)
            return (false);
    }

    if (sp == &string_pr[ROTHOST_SPR] || sp == &string_pr[ROTPORT_SPR]) {
        if (!bool_pr[ROTUSE_BPR].state)
            return (false);
    }

    if (sp == &string_pr[FLRIGHOST_SPR] || sp == &string_pr[FLRIGPORT_SPR]) {
        if (!bool_pr[FLRIGUSE_BPR].state)
            return (false);
    }

    if (sp == &string_pr[NTPHOST_SPR]) {
        if (!bool_pr[NTPSET_BPR].state)
            return (false);
    }

    if (sp == &string_pr[LAT_SPR] || sp == &string_pr[LNG_SPR] || sp == &string_pr[GRID_SPR]) {
        if (bool_pr[GEOIP_BPR].state || bool_pr[GPSDON_BPR].state)
            return (false);
    }

    if (sp == &string_pr[GPSDHOST_SPR]) {
        if (!bool_pr[GPSDON_BPR].state)
            return (false);
    }

    if (sp == &string_pr[I2CFN_SPR]) {
        #if defined(_I2C_ESP)
            return (false);
        #else
            return (bool_pr[I2CON_BPR].state);
        #endif
    }

    if (sp == &string_pr[BME76_DT] || sp == &string_pr[BME77_DT]
                    || sp == &string_pr[BME76_DP] || sp == &string_pr[BME77_DP]) {
        return (bool_pr[GPIOOK_BPR].state || bool_pr[I2CON_BPR].state);
    }

    if (sp == &string_pr[BRMIN_SPR] || sp == &string_pr[BRMAX_SPR])
        return (HAVE_ONOFF());

    if (sp == &string_pr[ADIFFN_SPR]) {
        // always irrelevant if not supporting ADIF file reading
        #if defined(_SUPPORT_ADIFILE)
            if (!bool_pr[ADIFSET_BPR].state)
        #endif
                return (false);
    }

    // no objections
    return (true);
}

/* move cur_focus to the next tab position.
 * ESP does not know about keyboard input
 */
static void nextTabFocus()
{
#if defined(_IS_UNIX)

    /* table of ordered fields for moving to next focus with each tab.
     * N.B. group and order within to their respective pages
     */
    static const Focus tab_fields[] = {
        // page 1

        {       &string_pr[CALL_SPR], NULL},
        {       &string_pr[LAT_SPR], NULL},
        {       &string_pr[LNG_SPR], NULL},
        {       &string_pr[GRID_SPR], NULL},
        { NULL, &bool_pr[GPSDON_BPR] },
        { NULL, &bool_pr[GPSDFOLLOW_BPR] },
        {       &string_pr[GPSDHOST_SPR], NULL},
        { NULL, &bool_pr[GEOIP_BPR] },
        { NULL, &bool_pr[WIFI_BPR] },
        {       &string_pr[WIFISSID_SPR], NULL},
        {       &string_pr[WIFIPASS_SPR], NULL},

        // page 2

        { NULL, &bool_pr[CLUSTER_BPR] },
        { NULL, &bool_pr[CLISWSJTX_BPR] },
        {       &string_pr[DXWLIST_SPR], NULL},
        {       &string_pr[DXPORT_SPR], NULL},
        {       &string_pr[DXHOST_SPR], NULL},
        {       &string_pr[DXLOGIN_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD0_BPR] },
        {       &string_pr[DXCLCMD0_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD1_BPR] },
        {       &string_pr[DXCLCMD1_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD2_BPR] },
        {       &string_pr[DXCLCMD2_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD3_BPR] },
        {       &string_pr[DXCLCMD3_SPR], NULL},

        // page 3

        { NULL, &bool_pr[RIGUSE_BPR] },
        {       &string_pr[RIGPORT_SPR], NULL},
        {       &string_pr[RIGHOST_SPR], NULL},
        { NULL, &bool_pr[ROTUSE_BPR] },
        {       &string_pr[ROTPORT_SPR], NULL},
        {       &string_pr[ROTHOST_SPR], NULL},
        { NULL, &bool_pr[FLRIGUSE_BPR] },
        {       &string_pr[FLRIGPORT_SPR], NULL},
        {       &string_pr[FLRIGHOST_SPR], NULL},
        { NULL, &bool_pr[NTPSET_BPR] },
        {       &string_pr[NTPHOST_SPR], NULL},
        { NULL, &bool_pr[ADIFSET_BPR] },
        {       &string_pr[ADIFFN_SPR], NULL},

        // page 4

        {       &string_pr[CENTERLNG_SPR], NULL},
        { NULL, &bool_pr[GPIOOK_BPR] },
        { NULL, &bool_pr[I2CON_BPR] },
        {       &string_pr[I2CFN_SPR], NULL},
        {       &string_pr[BME76_DT], NULL},
        {       &string_pr[BME76_DP], NULL},
        {       &string_pr[BME77_DT], NULL},
        {       &string_pr[BME77_DP], NULL},
        { NULL, &bool_pr[KX3ON_BPR] },
        {       &string_pr[BRMIN_SPR], NULL},
        {       &string_pr[BRMAX_SPR], NULL},

        // page 5

        { NULL, &bool_pr[DATEFMT_MDY_BPR] },
        { NULL, &bool_pr[LOGUSAGE_BPR] },
        { NULL, &bool_pr[WEEKDAY1MON_BPR] },
        { NULL, &bool_pr[DEMO_BPR] },
        { NULL, &bool_pr[UNITS_BPR] },
        { NULL, &bool_pr[BEARING_BPR] },
        { NULL, &bool_pr[SPOTLBL_BPR] },
        { NULL, &bool_pr[SPOTPATH_BPR] },
        { NULL, &bool_pr[SCROLLDIR_BPR] },
        { NULL, &bool_pr[SCROLLLEN_BPR] },
        { NULL, &bool_pr[X11_FULLSCRN_BPR] },
        { NULL, &bool_pr[FLIP_BPR] },
    };
    #define N_TAB_FIELDS    NARRAY(tab_fields)

    // find current position in table
    unsigned f;
    for (f = 0; f < N_TAB_FIELDS; f++)
        if (memcmp (&cur_focus, &tab_fields[f], sizeof(cur_focus)) == 0)
            break;
    if (f == N_TAB_FIELDS) {
        Serial.printf (_FX("cur_focus not found\n"));
        return;
    }

    // move to next relevant field, wrapping if necessary
    for (unsigned i = 1; i <= N_TAB_FIELDS; i++) {
        const Focus *fp = &tab_fields[(f+i)%N_TAB_FIELDS];
        if (fp->sp) {
            if (stringIsRelevant(fp->sp)) {
                cur_focus = *fp;
                return;
            }
        } else {
            if (boolIsRelevant(fp->bp)) {
                cur_focus = *fp;
                return;
            }
        }
    }
    Serial.printf (_FX("new focus not found\n"));

#endif // _IS_UNIX
}

/* set focus to the given string or bool prompt, opposite assumed to be NULL.
 * N.B. effect of setting both is undefined
 */
static void setFocus (StringPrompt *sp, BoolPrompt *bp)
{
    cur_focus.sp = sp;
    cur_focus.bp = bp;
}

/* set focus to the first relevant prompt in the current page, if any
 */
static void setInitialFocus()
{
    // just stay if still on current page
    if ((cur_focus.sp && cur_focus.sp->page == cur_page) || (cur_focus.bp && cur_focus.bp->page == cur_page))
        return;

    StringPrompt *sp0 = NULL;
    BoolPrompt *bp0 = NULL;

    for (int i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (stringIsRelevant(sp)) {
            sp0 = sp;
            break;
        }
    }

    for (int i = 0; i < N_BPR; i++) {
        BoolPrompt *bp = &bool_pr[i];
        if (boolIsRelevant(bp)) {
            bp0 = bp;
            break;
        }
    }

    // if find both, use the one on the higher row
    if (sp0 && bp0) {
        if (sp0->p_box.y < bp0->p_box.y)
            bp0 = NULL;
        else
            sp0 = NULL;
    }

    setFocus (sp0, bp0);
}

/* draw cursor for cur_focus
 */
static void drawCursor()
{
    uint16_t y, x1, x2;

    if (cur_focus.sp) {
        StringPrompt *sp = cur_focus.sp;
        y = sp->v_box.y+sp->v_box.h-CURSOR_DROP;
        x1 = sp->v_cx;
        x2 = sp->v_cx+PR_W;
    } else if (cur_focus.bp) {
        BoolPrompt *bp = cur_focus.bp;
        y = bp->p_box.y+bp->p_box.h-CURSOR_DROP;
        if (bp->p_str) {
            // cursor in prompt
            x1 = bp->p_box.x;
            x2 = bp->p_box.x+PR_W;
        } else {
            // cursor in state
            x1 = bp->s_box.x;
            x2 = bp->s_box.x+PR_W;
        }
    } else {
        return;
    }

    tft.drawLine (x1, y, x2, y, CURSOR_C);
    tft.drawLine (x1, y+1, x2, y+1, CURSOR_C);
}

/* erase cursor for cur_focus
 */
static void eraseCursor()
{
    uint16_t y, x1, x2;

    if (cur_focus.sp) {
        StringPrompt *sp = cur_focus.sp;
        y = sp->v_box.y+sp->v_box.h-CURSOR_DROP;
        x1 = sp->v_cx;
        x2 = sp->v_cx+PR_W;
    } else if (cur_focus.bp) {
        BoolPrompt *bp = cur_focus.bp;
        y = bp->p_box.y+bp->p_box.h-CURSOR_DROP;
        x1 = bp->p_box.x;
        x2 = bp->p_box.x+PR_W;
    } else {
        return;
    }

    tft.drawLine (x1, y, x2, y, BG_C);
    tft.drawLine (x1, y+1, x2, y+1, BG_C);
}

/* draw the prompt of the given StringPrompt, if any
 */
static void drawSPPrompt (StringPrompt *sp)
{
    if (sp->p_str) {
        tft.setTextColor (PR_C);
        tft.setCursor (sp->p_box.x, sp->p_box.y+sp->p_box.h-PR_D);
        tft.print(sp->p_str);
    }
#ifdef _MARK_BOUNDS
    drawSBox (sp->p_box, GRAY);
#endif // _MARK_BOUNDS
}

/* erase the prompt of the given StringPrompt
 */
static void eraseSPPrompt (const StringPrompt *sp)
{
    fillSBox (sp->p_box, BG_C);
}

/* erase the value of the given StringPrompt
 */
static void eraseSPValue (const StringPrompt *sp)
{
    fillSBox (sp->v_box, BG_C);
}

/* draw the value of the given StringPrompt and set v_cx (but don't draw cursor here)
 * N.B. we will shorten v_str to insure it fits within v_box
 */
static void drawSPValue (StringPrompt *sp)
{
    // prep writing into v_box
    tft.setTextColor (TX_C);
    tft.setCursor (sp->v_box.x, sp->v_box.y+sp->v_box.h-PR_D);

    // insure value string fits within box, shortening if necessary
    size_t vl0 = strlen (sp->v_str);
    (void) maxStringW (sp->v_str, sp->v_box.w);
    size_t vl1 = strlen (sp->v_str);

    if (vl1 < vl0) {
        // string was shortened to fit, show cursor under last character
        eraseSPValue (sp);                              // start over
        tft.printf (_FX("%.*s"), vl1 - 1, sp->v_str);   // show all but last char
        sp->v_cx = tft.getCursorX();                    // cursor goes here
        tft.print(sp->v_str[vl1-1]);                    // draw last char over cursor
    } else {
        // more room available, cursor follows string
        tft.print(sp->v_str);
        sp->v_cx = tft.getCursorX();
    }

    // insure cursor remains within box
    if (sp->v_cx + PR_W > sp->v_box.x + sp->v_box.w)
        sp->v_cx = sp->v_box.x + sp->v_box.w - PR_W;

#ifdef _MARK_BOUNDS
    drawSBox (sp->v_box, GRAY);
#endif // _MARK_BOUNDS
}

/* draw both prompt and value of the given StringPrompt
 */
static void drawSPPromptValue (StringPrompt *sp)
{
    drawSPPrompt (sp);
    drawSPValue (sp);
}

/* erase both prompt and value of the given StringPrompt
 */
static void eraseSPPromptValue (StringPrompt *sp)
{
    eraseSPPrompt (sp);
    eraseSPValue (sp);
}

/* draw the prompt of the given BoolPrompt, if any.
 */
static void drawBPPrompt (BoolPrompt *bp)
{
    if (!bp->p_str)
        return;

    #ifdef _WIFI_ALWAYS
        if (bp == &bool_pr[WIFI_BPR])
            tft.setTextColor (PR_C);            // required wifi is just a passive prompt but ...
        else
    #endif
    tft.setTextColor (BUTTON_C);                // ... others are a question.


    tft.setCursor (bp->p_box.x, bp->p_box.y+bp->p_box.h-PR_D);
    tft.print(bp->p_str);

#ifdef _MARK_BOUNDS
    drawSBox (bp->p_box, GRAY);
#endif // _MARK_BOUNDS
}

/* draw the state of the given BoolPrompt, if any.
 * N.B. beware of a few special cases
 */
static void drawBPState (BoolPrompt *bp)
{
    bool show_t = bp->state && bp->t_str;
    bool show_f = !bp->state && bp->f_str;

    if (show_t || show_f) {

        // dx commands are unusual as they are colored like buttons to appear as active
        if (bp == &bool_pr[DXCLCMD0_BPR] || bp == &bool_pr[DXCLCMD1_BPR]
                    || bp == &bool_pr[DXCLCMD2_BPR] || bp == &bool_pr[DXCLCMD3_BPR])
            tft.setTextColor (BUTTON_C);
        else
            tft.setTextColor (TX_C);

        tft.setCursor (bp->s_box.x, bp->s_box.y+bp->s_box.h-PR_D);
        fillSBox (bp->s_box, BG_C);
        if (show_t)
            tft.print(bp->t_str);
        if (show_f)
            tft.print(bp->f_str);
    #ifdef _MARK_BOUNDS
        drawSBox (bp->s_box, GRAY);
    #endif // _MARK_BOUNDS
    }
}

/* erase state of the given BoolPrompt, if any
 */
static void eraseBPState (BoolPrompt *bp)
{
    fillSBox (bp->s_box, BG_C);
}


/* draw both prompt and state of the given BoolPrompt
 */
static void drawBPPromptState (BoolPrompt *bp)
{
    drawBPPrompt (bp);
    drawBPState (bp);
}


/* erase prompt of the given BoolPrompt 
 */
static void eraseBPPrompt (BoolPrompt *bp)
{
    fillSBox (bp->p_box, BG_C);
}

/* erase both prompt and state of the given BoolPrompt
 */
static void eraseBPPromptState (BoolPrompt *bp)
{
    eraseBPPrompt (bp);
    eraseBPState (bp);
}


/* draw the virtual keyboard
 */
static void drawKeyboard()
{
    tft.fillRect (0, KB_Y0, tft.width(), tft.height()-KB_Y0-1, BG_C);
    tft.setTextColor (KF_C);

    for (int r = 0; r < NQR; r++) {
        resetWatchdog();
        uint16_t y = r * KB_CHAR_H + KB_Y0 + KB_CHAR_H;
        const Key *row = qwerty[r];
        for (int c = 0; c < NQC; c++) {
            const Key *kp = &row[c];
            char n = (char)pgm_read_byte(&kp->normal);
            if (n) {
                uint16_t x = qroff[r] + c * KB_CHAR_W;

                // shifted char above left
                tft.setCursor (x+TF_INDENT, y-KB_CHAR_H/2-F_DESCENT);
                tft.print((char)pgm_read_byte(&kp->shifted));

                // non-shifted below right
                tft.setCursor (x+BF_INDENT, y-F_DESCENT);
                tft.print(n);

                // key border
                tft.drawRect (x, y-KB_CHAR_H, KB_CHAR_W, KB_CHAR_H, KB_C);
            }
        }
    }

    drawStringInBox ("", space_b, false, KF_C);
    drawStringInBox ("Delete", delete_b, false, DEL_C);
}



/* convert a screen coord on the virtual keyboard to its char value, if any.
 * N.B. this does NOT handle Delete or Done.
 */
static bool s2char (SCoord &s, char &kbchar)
{
    // no KB on color page or onoff page
    if (cur_page == COLOR_PAGE || cur_page == ONOFF_PAGE)
        return (false);

    // check main qwerty
    if (s.y >= KB_Y0) {
        uint16_t kb_y = s.y - KB_Y0;
        uint8_t row = kb_y/KB_CHAR_H;
        if (row < NQR) {
            uint8_t col = (s.x-qroff[row])/KB_CHAR_W;
            if (col < NQC) {
                const Key *kp = &qwerty[row][col];
                char n = (char)pgm_read_byte(&kp->normal);
                if (n) {
                    // use shifted char if in top half
                    if (s.y < KB_Y0+row*KB_CHAR_H+KB_CHAR_H/2)
                        kbchar = (char)pgm_read_byte(&kp->shifted);
                    else
                        kbchar = n;
                    return (true);
                }
            }
        }
    }

    // check space bar
    if (inBox (s, space_b)) {
        kbchar = ' ';
        return (true);
    }

    // s is not on the virtual keyboard
    return (false);
}

/* display an entangled pair of bools states:
 *   if A->t_str 4 states: FF A->f_str TF A->t_str FT B->f_str TT B->t_str
 *   else        3 state:s FX A->f_str FT B->f_str TT B->t_str
 * N.B. assumes both A and B's state boxes, ie s_box, are in identical locations.
 * 3x entangled: a: FX  b: TF  c: TT
 * 4x entangled: a: FF  b: TF  c: FT  d: TT
 */
static void drawEntangledBools (BoolPrompt *A, BoolPrompt *B)
{
    if (A->t_str) {
        // 4-states
        if (!B->state)
            drawBPState (A);
        else {
            if (!A->state) {
                // FT: must temporarily turn off B to show its f_str
                B->state = false;
                drawBPState (B);
                B->state = true;
            } else
                drawBPState (B);
        }
    } else {
        // 3 states
        if (A->state)
            drawBPState (B);
        else
            drawBPState (A);
    }
}

/* perform action resulting from tapping the given BoolPrompt.
 * handle both singles and entangled pairs.
 */
static void engageBoolTap (BoolPrompt *bp)
{
    // erase current cursor position
    eraseCursor ();

    // update state
    if (bp->ent_mate == NOMATE) {

        // just a lone bool
        bp->state = !bp->state;
        drawBPState (bp);

        // move cursor to tapped field
        setFocus (NULL, bp);
        drawCursor ();

    } else {

        // this is one of an entangled pair. N.B. primary is always lower in memory.
        // 3x entangled: a: FX  b: TF  c: TT
        // 4x entangled: a: FF  b: TF  c: FT  d: TT
        BoolPrompt *mate = &bool_pr[bp->ent_mate];
        BoolPrompt *A = mate < bp ? mate : bp;
        BoolPrompt *B = mate < bp ? bp : mate;

        // roll choice forward, regardless of which was tapped
        if (A->t_str) {
            // 4-states: FF -> TF -> FT -> TT -> ...
            if (A->state) {
                A->state = false;
                B->state = !B->state;
            } else {
                A->state = true;
            };
                
        } else {
            // 3-states: FX -> TF -> TT -> ...
            if (A->state) {
                if (B->state) {
                    A->state = false;
                    B->state = false;
                } else {
                    B->state = true;
                }
            } else {
                A->state = true;
                B->state = false;
            }
        }

        // draw new state
        drawEntangledBools (A, B);

        // move cursor to Amary field
        setFocus (NULL, A);
        drawCursor ();
    }
}



/* find whether s is in any string_pr.
 * if so return true and set *spp, else return false.
 */
static bool tappedStringPrompt (SCoord &s, StringPrompt **spp)
{
    for (int i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (!stringIsRelevant(sp))
            continue;
        if ((sp->p_str && inBox (s, sp->p_box)) || (sp->v_str && inBox (s, sp->v_box))) {
            *spp = sp;
            return (true);
        }
    }
    return (false);
}

/* find whether s is in any relevant bool object.
 * require s within prompt or state box.
 * if so return true and set *bpp, else return false.
 */
static bool tappedBool (SCoord &s, BoolPrompt **bpp)
{
    for (int i = 0; i < N_BPR; i++) {
        BoolPrompt *bp = &bool_pr[i];
        if (!boolIsRelevant(bp))
            continue;
        if ((bp->p_str && inBox (s, bp->p_box))
               || (((bp->state && bp->t_str) || (!bp->state && bp->f_str)) && inBox (s, bp->s_box))) {
            *bpp = bp;
            return (true);
        }
    }
    return (false);
}




/* update the color component based on s known to be within csel_ctl_b.
 */
static void getCSelBoxColor (const SCoord &s, uint8_t &r, uint8_t &g, uint8_t &b)
{
    // vetical offset into csel_ctl_b
    uint16_t dy = s.y - CSEL_SCY;

    // new color
    uint16_t new_v = X2V(s.x);

    // update one component to new color depending on y, leave the others unchanged
    if (dy < CSEL_SCH+CSEL_SCYG/2) {
        r = new_v;                             // tapped first row
    } else if (dy < 2*CSEL_SCH+3*CSEL_SCYG/2) {
        g = new_v;                             // tapped second row
    } else {
        b = new_v;                             // tapped third row
    }
}

/* draw a color selector demo
 */
static void drawCSelDemoSwatch (const ColSelPrompt &p)
{
    uint16_t c = RGB565(p.r, p.g, p.b);

    // check for dashed, else solid
    if (DASHOK(p) && p.a_state) {
        for (int i = 0; i < CSEL_NDASH; i++) {
            uint16_t dx = i * p.d_box.w / CSEL_NDASH;
            tft.fillRect (p.d_box.x + dx, p.d_box.y, p.d_box.w/CSEL_NDASH, p.d_box.h,
                        (i&1) ? RA8875_BLACK : c);
        }
    } else {
        fillSBox (p.d_box, c);
    }
}

/* draw the dash control tick box, if used
 */
static void drawCSelDashTickBox(const ColSelPrompt &p)
{
    if (!DASHOK(p))
        return;

    uint16_t fg = p.a_state ? CSEL_TBCOL : RA8875_BLACK;
    uint16_t bg = p.a_state ? RA8875_BLACK : CSEL_TBCOL;
    fillSBox (p.a_box, fg);
    tft.fillRect (p.a_box.x, p.d_box.y, p.a_box.w/3, p.d_box.h, bg);
    tft.fillRect (p.a_box.x + 4*p.a_box.w/6, p.d_box.y, p.a_box.w/3, p.d_box.h, bg);
    drawSBox (p.a_box, RA8875_WHITE);
}

/* draw a color selector prompt tick box, on or off depending on state.
 */
static void drawCSelTickBox (const ColSelPrompt &p)
{
    fillSBox (p.t_box, p.state ? CSEL_TBCOL : RA8875_BLACK);
    drawSBox (p.t_box, RA8875_WHITE);
}

/* erase any possible existing then draw a new marker to indicate the current slider position at x,y.
 * beware sides.
 */
static void drawCSelCursor (uint16_t x, int16_t y)
{
    #define _CSEL_CR    2
    #define _CSEL_CH    (2*_CSEL_CR)

    tft.fillRect (CSEL_SCX, y, CSEL_SCW, _CSEL_CH, RA8875_BLACK);

    if (x < CSEL_SCX+_CSEL_CR)
        x = CSEL_SCX+_CSEL_CR;
    else if (x > CSEL_SCX + CSEL_SCW-_CSEL_CR-1)
        x = CSEL_SCX + CSEL_SCW-_CSEL_CR-1;
    tft.fillRect (x-_CSEL_CR, y, 2*_CSEL_CR+1, _CSEL_CH, CSEL_SCM_C);
}

/* given ul corner draw the given color number
 */
static void drawCSelValue (uint16_t x, uint16_t y, uint16_t color)
{
    tft.fillRect (x, y, 50, CSEL_SCH, RA8875_BLACK);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (x, y + CSEL_SCH - 4);
    tft.print(color);
}

/* indicate the color used by the given selector
 */
static void drawCSelPromptColor (const ColSelPrompt &p)
{
    // draw the cursors
    drawCSelCursor (V2X(p.r), CSEL_SCY+CSEL_SCH);
    drawCSelCursor (V2X(p.g), CSEL_SCY+2*CSEL_SCH+CSEL_SCYG);
    drawCSelCursor (V2X(p.b), CSEL_SCY+3*CSEL_SCH+2*CSEL_SCYG);

    // draw the value boxes
    drawCSelValue (CSEL_SCX+CSEL_SCW+CSEL_VDX, CSEL_SCY, p.r);
    drawCSelValue (CSEL_SCX+CSEL_SCW+CSEL_VDX, CSEL_SCY+CSEL_SCH+CSEL_SCYG, p.g);
    drawCSelValue (CSEL_SCX+CSEL_SCW+CSEL_VDX, CSEL_SCY+2*CSEL_SCH+2*CSEL_SCYG, p.b);
}

/* draw the one-time color selector GUI features
 * N.B. we assume screen is already erased.
 */
static void drawCSelInitGUI()
{
    // draw color control sliders
    fillSBox (csel_ctl_b, RA8875_BLACK);
    const uint16_t y0 = CSEL_SCY;
    const uint16_t y1 = CSEL_SCY+CSEL_SCH+CSEL_SCYG;
    const uint16_t y2 = CSEL_SCY+2*CSEL_SCH+2*CSEL_SCYG;
    for (int x = CSEL_SCX; x < CSEL_SCX+CSEL_SCW; x++) {
        uint8_t new_v = X2V(x);
        tft.drawLine (x, y0, x, y0+CSEL_SCH-1, 1, RGB565 (new_v, 0, 0));
        tft.drawLine (x, y1, x, y1+CSEL_SCH-1, 1, RGB565 (0, new_v, 0));
        tft.drawLine (x, y2, x, y2+CSEL_SCH-1, 1, RGB565 (0, 0, new_v));
    }

    // add borders
    tft.drawRect (CSEL_SCX, CSEL_SCY, CSEL_SCW, CSEL_SCH, CSEL_SCB_C);
    tft.drawRect (CSEL_SCX, CSEL_SCY+CSEL_SCH+CSEL_SCYG, CSEL_SCW, CSEL_SCH, CSEL_SCB_C);
    tft.drawRect (CSEL_SCX, CSEL_SCY+2*CSEL_SCH+2*CSEL_SCYG, CSEL_SCW, CSEL_SCH, CSEL_SCB_C);

    // draw prompts and set sliders from one that is set
    resetWatchdog();
    for (int i = 0; i < N_CSPR; i++) {
        ColSelPrompt &p = csel_pr[i];
        tft.setTextColor (TX_C);
        tft.setCursor (p.p_box.x, p.p_box.y+p.p_box.h-PR_D);
        tft.printf ("%s:", p.p_str);
        drawCSelTickBox (p);
        drawCSelDemoSwatch (p);
        drawCSelDashTickBox(p);
        if (p.state)
            drawCSelPromptColor (p);
    }
}

/* handle a possible touch event while on the color selection page.
 * return whether ours
 */
static bool handleCSelTouch (SCoord &s)
{
    bool ours = false;

    // check for setting a new color for the current selection
    if (inBox (s, csel_ctl_b)) {
        for (int i = 0; i < N_CSPR; i++) {
            ColSelPrompt &p = csel_pr[i];
            if (p.state) {
                getCSelBoxColor(s, p.r, p.g, p.b);
                drawCSelPromptColor(p);
                drawCSelDemoSwatch (p);
                break;
            }
        }
        ours = true;
    }

    // else check for changing dashed state
    if (!ours) {
        for (int i = 0; i < N_CSPR; i++) {
            ColSelPrompt &p = csel_pr[i];
            if (DASHOK(p) && inBox (s, p.a_box)) {
                // toggle and redraw
                p.a_state = !p.a_state;
                drawCSelDemoSwatch (p);
                drawCSelDashTickBox(p);
                ours = true;
                break;
            }
        }
    }

    // else check for changing the current selection
    if (!ours) {
        for (int i = 0; i < N_CSPR; i++) {
            ColSelPrompt &pi = csel_pr[i];
            if (inBox (s, pi.t_box) && !pi.state) {
                // clicked an off box, make it the only one on (ignore clicking an on box)
                for (int j = 0; j < N_CSPR; j++) {
                    ColSelPrompt &pj = csel_pr[j];
                    if (pj.state) {
                        pj.state = false;
                        drawCSelTickBox (pj);
                    }
                }
                pi.state = true;
                drawCSelTickBox (pi);
                drawCSelPromptColor (pi);
                ours = true;
                break;
            }
        }
    }

    return (ours);
}


/* draw a V inscribed in a square with size s and given center in one of 4 directions.
 * dir is degs CCW from right
 */
static void drawVee (uint16_t x0, uint16_t y0, uint16_t s, uint16_t dir, uint16_t c16)
{
    uint16_t r = s/2;

    switch (dir) {
    case 0:     // point right
        tft.drawLine (x0+r, y0, x0-r, y0-r, c16);
        tft.drawLine (x0+r, y0, x0-r, y0+r, c16);
        break;
    case 90:    // point up
        tft.drawLine (x0, y0-r, x0-r, y0+r, c16);
        tft.drawLine (x0, y0-r, x0+r, y0+r, c16);
        break;
    case 180:   // point left
        tft.drawLine (x0-r, y0, x0+r, y0-r, c16);
        tft.drawLine (x0-r, y0, x0+r, y0+r, c16);
        break;
    case 270:   // point down
        tft.drawLine (x0, y0+r, x0-r, y0-r, c16);
        tft.drawLine (x0, y0+r, x0+r, y0-r, c16);
        break;
    }
}


/* given dow 0..6, y coord of text and hhmm time print new value
 */
static void drawOnOffTimeCell (int dow, uint16_t y, uint16_t thm)
{
    char buf[20];

    tft.setTextColor(TX_C);
    snprintf (buf, sizeof(buf), "%02d:%02d", thm/60, thm%60);
    tft.setCursor (OO_DHX(dow)+(OO_CW-getTextWidth(buf))/2, y);
    tft.fillRect (OO_DHX(dow)+1, y-OO_RH+1, OO_CW-2, OO_RH, RA8875_BLACK);
    tft.print (buf);
}

/* draw OnOff table from scratch
 */
static void drawOnOffControls()
{
    // title
    const char *title = brDimmableOk()
                        ? _FX("DE Daily Display On/Dim Times")
                        : _FX("DE Daily Display On/Off Times");
    tft.setCursor (OO_X0+(OO_TW-getTextWidth(title))/2, OO_Y0-OO_RH-OO_TO);
    tft.setTextColor (PR_C);
    tft.print (title);


    // DOW column headings and copy marks
    for (int i = 0; i < DAYSPERWEEK; i++) {
        uint16_t l = getTextWidth(dayShortStr(i+1));
        tft.setTextColor (PR_C);
        tft.setCursor (OO_DHX(i)+(OO_CW-l)/2, OO_CHY);
        tft.print (dayShortStr(i+1));
        drawVee (OO_CPLX(i), OO_CPLY, OO_ASZ, 180, BUTTON_C);
        drawVee (OO_CPRX(i), OO_CPRY, OO_ASZ, 0, BUTTON_C);
    }

    // On Off labels
    tft.setTextColor (PR_C);
    tft.setCursor (OO_X0+2, OO_ONY);
    tft.print (F("On"));
    tft.setCursor (OO_X0+2, OO_OFFY);
    if (brDimmableOk())
        tft.print (F("Dim"));
    else
        tft.print (F("Off"));

    // inc/dec hints
    drawVee (OO_X0+(OO_CI-OO_CW/6)/2, OO_Y0+1*OO_RH/2, OO_ASZ, 90, BUTTON_C);
    drawVee (OO_X0+(OO_CI-OO_CW/6)/2, OO_Y0+5*OO_RH/2, OO_ASZ, 270, BUTTON_C);
    drawVee (OO_X0+(OO_CI-OO_CW/6)/2, OO_Y0+7*OO_RH/2, OO_ASZ, 90, BUTTON_C);
    drawVee (OO_X0+(OO_CI-OO_CW/6)/2, OO_Y0+11*OO_RH/2, OO_ASZ, 270, BUTTON_C);

    // graph lines
    tft.drawRect (OO_X0, OO_Y0-OO_RH, OO_CI+OO_CW*DAYSPERWEEK, OO_RH*7, KB_C);
    tft.drawLine (OO_X0, OO_Y0, OO_X0+OO_CI+OO_CW*DAYSPERWEEK, OO_Y0, KB_C);
    for (int i = 0; i < DAYSPERWEEK; i++)
        tft.drawLine (OO_DHX(i), OO_Y0-OO_RH, OO_DHX(i), OO_Y0+6*OO_RH, KB_C);

    // init table
    uint16_t onoff[NV_DAILYONOFF_LEN];
    NVReadString (NV_DAILYONOFF, (char*)onoff);
    for (int i = 0; i < DAYSPERWEEK; i++) {
        drawOnOffTimeCell (i, OO_ONY, onoff[i]);
        drawOnOffTimeCell (i, OO_OFFY, onoff[i+DAYSPERWEEK]);
    }
}

/* handle possible touch on the onoff controls page.
 * return whether really ours
 */
static bool checkOnOffTouch (SCoord &s)
{

    if (!HAVE_ONOFF())
        return (false);

    int dow = ((int)s.x - (OO_X0+OO_CI))/OO_CW;
    int row = ((int)s.y - (OO_Y0-OO_RH))/OO_RH;
    if (dow < 0 || dow >= DAYSPERWEEK || row < 0 || row > 6)
        return (false);

    // read onoff times and make handy shortcuts
    uint16_t onoff[NV_DAILYONOFF_LEN];
    NVReadString (NV_DAILYONOFF, (char*)onoff);
    uint16_t *ontimes = &onoff[0];
    uint16_t *offtimes = &onoff[DAYSPERWEEK];

    int col = ((s.x - (OO_X0+OO_CI))/(OO_CW/2)) % 2;
    bool hour_col   = col == 0; // same as copy left
    bool mins_col   = col == 1; // same as copy right
    bool oncpy_row  = row == 0;
    bool oninc_row  = row == 1;
    bool ondec_row  = row == 3;
    bool offinc_row = row == 4;
    bool offdec_row = row == 6;

    if (oncpy_row) {
        int newdow;
        if (hour_col) {
            // copy left
            newdow = (dow - 1 + DAYSPERWEEK) % DAYSPERWEEK;
        } else if (mins_col) {
            // copy right
            newdow = (dow + 1) % DAYSPERWEEK;
        } else
            return (false);
        ontimes[newdow] = ontimes[dow];
        offtimes[newdow] = offtimes[dow];
        drawOnOffTimeCell (newdow, OO_ONY, ontimes[newdow]);
        drawOnOffTimeCell (newdow, OO_OFFY, offtimes[newdow]);
    } else if (oninc_row) {
        if (hour_col)
            ontimes[dow] = (ontimes[dow] + 60) % MINSPERDAY;
        else if (mins_col)
            ontimes[dow] = (ontimes[dow] + 5)  % MINSPERDAY;
        else
            return (false);
        drawOnOffTimeCell (dow, OO_ONY, ontimes[dow]);
    } else if (ondec_row) {
        if (hour_col)
            ontimes[dow] = (ontimes[dow] + MINSPERDAY-60) % MINSPERDAY;
        else if (mins_col)
            ontimes[dow] = (ontimes[dow] + MINSPERDAY-5)  % MINSPERDAY;
        else
            return (false);
        drawOnOffTimeCell (dow, OO_ONY, ontimes[dow]);
    } else if (offinc_row) {
        if (hour_col)
            offtimes[dow] = (offtimes[dow] + 60) % MINSPERDAY;
        else if (mins_col)
            offtimes[dow] = (offtimes[dow] + 5)  % MINSPERDAY;
        else
            return (false);
        drawOnOffTimeCell (dow, OO_OFFY, offtimes[dow]);
    } else if (offdec_row) {
        if (hour_col)
            offtimes[dow] = (offtimes[dow] + MINSPERDAY-60) % MINSPERDAY;
        else if (mins_col)
            offtimes[dow] = (offtimes[dow] + MINSPERDAY-5)  % MINSPERDAY;
        else
            return (false);
        drawOnOffTimeCell (dow, OO_OFFY, offtimes[dow]);
    }

    // save
    NVWriteString (NV_DAILYONOFF, (char*)onoff);

    // ok
    return (true);
}


/* draw all prompts and values for the current page
 */
static void drawCurrentPageFields()
{
    // draw relevant string prompts on this page
    for (int i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (stringIsRelevant(sp))
            drawSPPromptValue(sp);
    }

    // draw relevant bool prompts on this page
    for (int i = 0; i < N_BPR; i++) {
        BoolPrompt *bp = &bool_pr[i];
        if (boolIsRelevant(bp)) {
            drawBPPrompt (bp);
            if (bp->ent_mate == i+1)
                drawEntangledBools(bp, &bool_pr[i+1]);
            else if (bp->ent_mate == NOMATE)
                drawBPState (bp);
        }
    }

    // draw spider header if appropriate
    if (cur_page == SPIDER_PAGE && bool_pr[CLUSTER_BPR].state && !bool_pr[CLISWSJTX_BPR].state)
        drawSpiderCommandsHeader();

    #if defined(_WIFI_ALWAYS)
        // show prompt but otherwise is not relevant
        if (bool_pr[WIFI_BPR].page == cur_page)
            drawBPPrompt (&bool_pr[WIFI_BPR]);
    #endif

    // set initial focus
    setInitialFocus ();
    drawCursor ();
}


/* change cur_page to the given page
 */
static void changePage (int new_page)
{
    // save current then update
    int prev_page = cur_page;
    cur_page = new_page;

    // draw new page with minimal erasing if possible

    if (new_page == ALLBOOLS_PAGE) {
        // new page is all bools, always a fresh start
        eraseScreen();
        drawPageButton();
        drawCurrentPageFields();
        drawDoneButton(false);

    } else if (new_page == ONOFF_PAGE) {
        // new page is just the on/off table
        eraseScreen();
        drawPageButton();
        drawOnOffControls();
        drawDoneButton(false);

    } else if (new_page == COLOR_PAGE) {
        // new page is color, always a fresh start
        eraseScreen();
        drawPageButton();
        drawCSelInitGUI();
        drawDoneButton(false);
        setInitialFocus ();

    } else {
        // new page is 0-3 which all use a keyboard
        if (prev_page >= 0 && prev_page <= 3) {
            // just refresh top portion, keyboard already ok
            tft.fillRect (0, 0, tft.width(), KB_Y0-1, BG_C);
            drawPageButton();
            drawCurrentPageFields();
        } else {
            // full refresh
            eraseScreen();
            drawPageButton();
            drawCurrentPageFields();
            drawKeyboard();
            drawDoneButton(false);
        }
    }
}

/* check whether the given string is a number between min_port and 65535.
 * if so, set port and return true, else return false.
 */
static bool portOK (const char *port_str, int min_port, uint16_t *portp)
{
    char *first_bad;
    int portn = strtol (port_str, &first_bad, 10);
    if (*first_bad != '\0' || portn < min_port || portn > 65535)
        return (false);
    *portp = portn;
    return (true);
}


/* return whether the given string seems to be a legit host name and fits in the given length.
 * N.B. all blanks are removed IN PLACE
 */
static bool hostOK (char *host_str, int max_len)
{
    noBlanks(host_str);

    // not too long or too short
    int hl = strlen (host_str);
    if (hl > max_len-1 || hl == 0)
        return (false);

    // localhost?
    if (!strcmp (host_str, "localhost"))
        return (true);

    // need at least one dot for TLD or exactly 3 if looks like dotted ip notation
    int n_dots = 0;
    int n_digits = 0;
    int n_other = 0;
    for (int i = 0; i < hl; i++) {
        if (host_str[i] == '.')
            n_dots++;
        else if (isdigit(host_str[i]))
            n_digits++;
        else
            n_other++;
    }
    if (n_dots < 1 || host_str[0] == '.' || host_str[hl-1] == '.')
        return (false);
    if (n_other == 0 && (n_dots != 3 || n_digits != hl-3))
        return (false);

    return (true);
}

/* return whether the i2c_fn looks legit
 */
static bool I2CFnOk(void)
{
    bool ok = strncmp (i2c_fn, _FX("/dev/"), 5) == 0 && strlen (i2c_fn) > 5;

    #if defined(_IS_UNIX)
        // on linux actually try to open and lock the same as Wire will do
        if (ok) {
            int fd = open (i2c_fn, O_RDWR);
            if (fd < 0) {
                Serial.printf (_FX("I2C: %s: %s\n"), i2c_fn, strerror(errno));
                ok = false;
            } else {
                ok = ::flock (fd, LOCK_EX|LOCK_NB) == 0;
                Serial.printf (_FX("I2C: %s: %s\n"), i2c_fn, ok ? "ok" : strerror(errno));
                close (fd);
            }
        }
    #endif

    return (ok);
}

/* return whether dx_login looks ok
 */
static bool clusterLoginOk()
{
    // must be blank or contain DE call
    noBlanks(dx_login);
    return (dx_login[0] == '\0' || strstr (dx_login, call_sign) != NULL);
}

/* return whether string fields are all valid.
 * if show_errors then temporarily indicate ones in error.
 */
static bool validateStringPrompts (bool show_errors)
{
    // collect bad ids to flag
    SPIds badsid[N_SPR];
    uint8_t n_badsid = 0;

    // call must not be blank
    noBlanks(call_sign);
    if (call_sign[0] == '\0')
        badsid[n_badsid++] = CALL_SPR;

    // check lat/long unless using something else
    if (!bool_pr[GEOIP_BPR].state && !bool_pr[GPSDON_BPR].state) {

        if (!latSpecIsValid (string_pr[LAT_SPR].v_str, de_ll.lat_d))
            badsid[n_badsid++] = LAT_SPR;

        if (!lngSpecIsValid (string_pr[LNG_SPR].v_str, de_ll.lng_d))
            badsid[n_badsid++] = LNG_SPR;

        LatLong ll;
        if (!maidenhead2ll (ll, string_pr[GRID_SPR].v_str))
            badsid[n_badsid++] = GRID_SPR;
    }

    // check cluster info if used
    if (bool_pr[CLUSTER_BPR].state) {
        char *clhost = string_pr[DXHOST_SPR].v_str;
        if (!hostOK(clhost,NV_DXHOST_LEN))
            badsid[n_badsid++] = DXHOST_SPR;
        if (!portOK (string_pr[DXPORT_SPR].v_str, 23, &dx_port))        // 23 is telnet
            badsid[n_badsid++] = DXPORT_SPR;
        if (!bool_pr[CLISWSJTX_BPR].state && !clusterLoginOk())         // no used with wsjt
            badsid[n_badsid++] = DXLOGIN_SPR;

        // clean up any extra white space in the commands then check for blank entries that are on
        for (int i = 0; i < N_DXCLCMDS; i++) {
            trim(dxcl_cmds[i]);
            if (strlen(dxcl_cmds[i]) == 0 && bool_pr[DXCLCMD0_BPR+i].state)
                badsid[n_badsid++] = (SPIds)(DXCLCMD0_SPR+i);
        }
    }

    // check rig_host and port if used
    if (bool_pr[RIGUSE_BPR].state) {
        if (!hostOK(string_pr[RIGHOST_SPR].v_str,NV_RIGHOST_LEN))
            badsid[n_badsid++] = RIGHOST_SPR;
        if (!portOK (string_pr[RIGPORT_SPR].v_str, 1000, &rig_port))
            badsid[n_badsid++] = RIGPORT_SPR;
    }

    // check rot_host and port if used
    if (bool_pr[ROTUSE_BPR].state) {
        if (!hostOK(string_pr[ROTHOST_SPR].v_str,NV_ROTHOST_LEN))
            badsid[n_badsid++] = ROTHOST_SPR;
        if (!portOK (string_pr[ROTPORT_SPR].v_str, 1000, &rot_port))
            badsid[n_badsid++] = ROTPORT_SPR;
    }

    // check flrig host and port if used
    if (bool_pr[FLRIGUSE_BPR].state) {
        if (!hostOK(string_pr[FLRIGHOST_SPR].v_str,NV_FLRIGHOST_LEN))
            badsid[n_badsid++] = FLRIGHOST_SPR;
        if (!portOK (string_pr[FLRIGPORT_SPR].v_str, 1000, &flrig_port))
            badsid[n_badsid++] = FLRIGPORT_SPR;
    }

    // check for plausible temperature and pressure corrections and file name if used
    if (bool_pr[GPIOOK_BPR].state || bool_pr[I2CON_BPR].state) {
        char *tc_str = string_pr[BME76_DT].v_str;
        temp_corr[BME_76] = atof (tc_str);
        if (fabsf(temp_corr[BME_76]) > MAX_BME_DTEMP)
            badsid[n_badsid++] = BME76_DT;
        char *tc2_str = string_pr[BME77_DT].v_str;
        temp_corr[BME_77] = atof (tc2_str);
        if (fabsf(temp_corr[BME_77]) > MAX_BME_DTEMP)
            badsid[n_badsid++] = BME77_DT;

        char *pc_str = string_pr[BME76_DP].v_str;
        pres_corr[BME_76] = atof (pc_str);
        if (fabsf(pres_corr[BME_76]) > MAX_BME_DPRES)
            badsid[n_badsid++] = BME76_DP;
        char *pc2_str = string_pr[BME77_DP].v_str;
        pres_corr[BME_77] = atof (pc2_str);
        if (fabsf(pres_corr[BME_77]) > MAX_BME_DPRES)
            badsid[n_badsid++] = BME77_DP;
    }

    // require ssid and pw if wifi
    if (bool_pr[WIFI_BPR].state) {
        if (strlen (string_pr[WIFISSID_SPR].v_str) == 0)
            badsid[n_badsid++] = WIFISSID_SPR;
        if (strlen (string_pr[WIFIPASS_SPR].v_str) == 0)
            badsid[n_badsid++] = WIFIPASS_SPR;
    }

    // allow no spaces in call sign
    if (strchr (string_pr[CALL_SPR].v_str, ' ')) {
        badsid[n_badsid++] = CALL_SPR;
    }

    // require plausible gpsd host name if used
    if (bool_pr[GPSDON_BPR].state) {
        if (!hostOK(string_pr[GPSDHOST_SPR].v_str,NV_GPSDHOST_LEN))
            badsid[n_badsid++] = GPSDHOST_SPR;
    }

    // require plausible ntp host name or a few special cases if used
    if (bool_pr[NTPSET_BPR].state) {
        if (!hostOK(string_pr[NTPHOST_SPR].v_str,NV_NTPHOST_LEN) && !useOSTime())
            badsid[n_badsid++] = NTPHOST_SPR;
    }

    // require both brightness 0..100 and min < max.
    if (brDimmableOk()) {
        // Must use ints to check for < 0
        int brmn = atoi (string_pr[BRMIN_SPR].v_str);
        int brmx = atoi (string_pr[BRMAX_SPR].v_str);
        bool brmn_ok = brmn >= 0 && brmn <= 100;
        bool brmx_ok = brmx >= 0 && brmx <= 100;
        bool order_ok = brmn < brmx;
        if (!brmn_ok || (!order_ok && brmx_ok))
            badsid[n_badsid++] = BRMIN_SPR;
        if (!brmx_ok || (!order_ok && brmn_ok))
            badsid[n_badsid++] = BRMAX_SPR;
        if (brmn_ok && brmx_ok && order_ok) {
            bright_min = brmn;
            bright_max = brmx;
        }
    }

    // require mercator center longitude -180 <= x < 180
    float clng;
    if (lngSpecIsValid (string_pr[CENTERLNG_SPR].v_str, clng))
        center_lng = clng;
    else
        badsid[n_badsid++] = CENTERLNG_SPR;

    // ADIF file name must not be blank if used
    if (bool_pr[ADIFSET_BPR].state) {
        trim (adif_fn);
        if (adif_fn[0] == '\0')
            badsid[n_badsid++] = ADIFFN_SPR;
    }

    // check I2C file name
    if (bool_pr[I2CON_BPR].state) {
        trim (i2c_fn);
        if (!I2CFnOk())
            badsid[n_badsid++] = I2CFN_SPR;
    }

    // if not showing, just return whether all ok
    if (!show_errors)
        return (n_badsid == 0);

    // if any bad indicate values in error, changing pages if necessary
    if (n_badsid > 0) {

        // starting with the current page, search for first page with any bad fields

        int bad_page = -1;
        for (int p = 0; bad_page < 0 && p < N_PAGES; p++) {

            // check this page for any bad values
            int tmp_page = (cur_page + p) % N_PAGES;
            for (int i = 0; i < n_badsid; i++) {
                StringPrompt *sp = &string_pr[badsid[i]];
                if (sp->page == tmp_page) {
                    bad_page = tmp_page;
                    break;
                }
            }
        }

        // if found, change to bad page if not already up and flag all its bad values

        if (bad_page >= 0) {

            // change page, if not already showing
            if (bad_page != cur_page)
                changePage(bad_page);

            // flag each erroneous value on this page
            for (int i = 0; i < n_badsid; i++) {
                StringPrompt *sp = &string_pr[badsid[i]];
                if (sp->page == cur_page) 
                    flagErrField (sp);
            }

            // dwell error flag(s)
            wdDelay(3000);

            // restore values
            for (int i = 0; i < n_badsid; i++) {
                StringPrompt *sp = &string_pr[badsid[i]];
                if (sp->page == cur_page) {
                    eraseSPValue (sp);
                    drawSPValue (sp);
                }
            }

            // redraw cursor in case it's value was flagged
            drawCursor();

        }

        // at least one bad field
        return (false);
    }

    // all good
    return (true);
}


/* if linux try to set NV_WIFI_SSID and NV_WIFI_PASSWD from wpa_supplicant.conf 
 */
static bool getWPA()
{
#if defined(_IS_LINUX)

    // open
    static const char wpa_fn[] = "/etc/wpa_supplicant/wpa_supplicant.conf";
    FILE *fp = fopen (wpa_fn, "r");
    if (!fp) {
        Serial.printf ("%s: %s\n", wpa_fn, strerror(errno));
        return (false);
    }

    // read, looking for ssid and psk
    char buf[100], wpa_ssid[100], wpa_psk[100];
    bool found_ssid = false, found_psk = false;
    while (fgets (buf, sizeof(buf), fp)) {
        if (sscanf (buf, " ssid=\"%100[^\"]\"", wpa_ssid) == 1)
            found_ssid = true;
        if (sscanf (buf, " psk=\"%100[^\"]\"", wpa_psk) == 1)
            found_psk = true;
    }

    // finished with file
    fclose (fp);

    // save if found both
    if (found_ssid && found_psk) {
        wpa_ssid[NV_WIFI_SSID_LEN-1] = '\0';
        strcpy (wifi_ssid, wpa_ssid);
        NVWriteString(NV_WIFI_SSID, wifi_ssid);
        wpa_psk[NV_WIFI_PW_LEN-1] = '\0';
        strcpy (wifi_pw, wpa_psk);
        NVWriteString(NV_WIFI_PASSWD, wifi_pw);
        return (true);
    }

    // nope
    return (false);

#else

    return (false);

#endif // _IS_LINUX
}


/* load all setup values from nvram or set default values:
 */
static void initSetup()
{
    // init wifi, accept OLD PW if valid

    if (!getWPA() && !NVReadString(NV_WIFI_SSID, wifi_ssid)) {
        strncpy (wifi_ssid, DEF_SSID, NV_WIFI_SSID_LEN-1);
        NVWriteString(NV_WIFI_SSID, wifi_ssid);
    }
    if (!NVReadString(NV_WIFI_PASSWD, wifi_pw) && !NVReadString(NV_WIFI_PASSWD_OLD, wifi_pw)) {
        strncpy (wifi_pw, DEF_PASS, NV_WIFI_PW_LEN-1);
        NVWriteString(NV_WIFI_PASSWD, wifi_pw);
    }



    // init call sign, no default

    NVReadString(NV_CALLSIGN, call_sign);


    // init gpsd host and option

    if (!NVReadString (NV_GPSDHOST, gpsd_host)) {
        strcpy (gpsd_host, "localhost");
        NVWriteString (NV_GPSDHOST, gpsd_host);
    }
    uint8_t nv_gpsd;
    if (!NVReadUInt8 (NV_USEGPSD, &nv_gpsd)) {
        bool_pr[GPSDON_BPR].state = false;
        bool_pr[GPSDFOLLOW_BPR].state = false;
        NVWriteUInt8 (NV_USEGPSD, 0);
    } else {
        bool_pr[GPSDON_BPR].state = (nv_gpsd & USEGPSD_FORTIME_BIT) != 0;
        bool_pr[GPSDFOLLOW_BPR].state = bool_pr[GPSDON_BPR].state && (nv_gpsd & USEGPSD_FORLOC_BIT) != 0;
    }



    // init ntp host and option

    if (!NVReadString (NV_NTPHOST, ntp_host)) {
        ntp_host[0] = '\0';
        NVWriteString (NV_NTPHOST, ntp_host);
    }
    uint8_t nv_ntp;
    if (!NVReadUInt8 (NV_NTPSET, &nv_ntp)) {
        nv_ntp = bool_pr[NTPSET_BPR].state = false;
        NVWriteUInt8 (NV_NTPSET, 0);
    } else
        bool_pr[NTPSET_BPR].state = (nv_ntp != 0);

    // init ADIF

    if (!NVReadString (NV_ADIFFN, adif_fn)) {
        adif_fn[0] = '\0';
        NVWriteString (NV_ADIFFN, adif_fn);
    }
    bool_pr[ADIFSET_BPR].state = adif_fn[0] != '\0';

    // init I2C

    if (!NVReadString (NV_I2CFN, i2c_fn)) {
        // supply a reasonable system-dependent default
        #if defined (_I2C_FREEBSD)
            strcpy (i2c_fn, _FX("/dev/iic0"));
        #elif defined (_I2C_LINUX)
            strcpy (i2c_fn, _FX("/dev/i2c-1"));
        #else
            i2c_fn[0] = '\0';
        #endif
        NVWriteString (NV_I2CFN, i2c_fn);
    }
    uint8_t i2c_on;
    if (!NVReadUInt8 (NV_I2CON, &i2c_on)) {
        i2c_on = 0;
        NVWriteUInt8 (NV_I2CON, i2c_on);
    }
    bool_pr[I2CON_BPR].state = (i2c_on != 0);


    // init rigctld host, port and option

    if (!NVReadString (NV_RIGHOST, rig_host)) {
        strcpy (rig_host, "localhost");
        NVWriteString (NV_RIGHOST, rig_host);
    }
    if (!NVReadUInt16(NV_RIGPORT, &rig_port)) {
        rig_port = 4532;
        NVWriteUInt16(NV_RIGPORT, rig_port);
    }
    uint8_t nv_rig;
    if (!NVReadUInt8 (NV_RIGUSE, &nv_rig)) {
        nv_rig = bool_pr[RIGUSE_BPR].state = false;
        NVWriteUInt8 (NV_RIGUSE, 0);
    } else
        bool_pr[RIGUSE_BPR].state = (nv_rig != 0);


    // init rotctld host, port and option

    if (!NVReadString (NV_ROTHOST, rot_host)) {
        strcpy (rot_host, "localhost");
        NVWriteString (NV_ROTHOST, rot_host);
    }
    if (!NVReadUInt16(NV_ROTPORT, &rot_port)) {
        rot_port = 4533;
        NVWriteUInt16(NV_ROTPORT, rot_port);
    }
    uint8_t nv_rot;
    if (!NVReadUInt8 (NV_ROTUSE, &nv_rot)) {
        nv_rot = bool_pr[ROTUSE_BPR].state = false;
        NVWriteUInt8 (NV_ROTUSE, 0);
    } else
        bool_pr[ROTUSE_BPR].state = (nv_rot != 0);


    // init flrig host, port and option

    if (!NVReadString (NV_FLRIGHOST, flrig_host)) {
        strcpy (flrig_host, "localhost");
        NVWriteString (NV_FLRIGHOST, flrig_host);
    }
    if (!NVReadUInt16(NV_FLRIGPORT, &flrig_port)) {
        flrig_port = 12345;
        NVWriteUInt16(NV_FLRIGPORT, flrig_port);
    }
    uint8_t nv_flrig;
    if (!NVReadUInt8 (NV_FLRIGUSE, &nv_flrig)) {
        nv_flrig = bool_pr[FLRIGUSE_BPR].state = false;
        NVWriteUInt8 (NV_FLRIGUSE, 0);
    } else
        bool_pr[FLRIGUSE_BPR].state = (nv_flrig != 0);



    // init dx cluster info

    if (!NVReadString(NV_DXHOST, dx_host)) {
        memset (dx_host, 0, sizeof(dx_host));
        NVWriteString(NV_DXHOST, dx_host);
    }
    if (!NVReadString(NV_DXLOGIN, dx_login) || !clusterLoginOk()) {
        strcpy (dx_login, call_sign);      // default to call
        NVWriteString(NV_DXLOGIN, dx_login);
    }
    if (!NVReadUInt16(NV_DXPORT, &dx_port)) {
        dx_port = 0;
        NVWriteUInt16(NV_DXPORT, dx_port);
    }
    if (!NVReadString(NV_DXCMD0, dxcl_cmds[0])) {
        memset (dxcl_cmds[0], 0, sizeof(dxcl_cmds[0]));
        NVWriteString(NV_DXCMD0, dxcl_cmds[0]);
    }
    if (!NVReadString(NV_DXCMD1, dxcl_cmds[1])) {
        memset (dxcl_cmds[1], 0, sizeof(dxcl_cmds[1]));
        NVWriteString(NV_DXCMD1, dxcl_cmds[1]);
    }
    if (!NVReadString(NV_DXCMD2, dxcl_cmds[2])) {
        memset (dxcl_cmds[2], 0, sizeof(dxcl_cmds[2]));
        NVWriteString(NV_DXCMD2, dxcl_cmds[2]);
    }
    if (!NVReadString(NV_DXCMD3, dxcl_cmds[3])) {
        memset (dxcl_cmds[3], 0, sizeof(dxcl_cmds[3]));
        NVWriteString(NV_DXCMD3, dxcl_cmds[3]);
    }
    if (!NVReadString(NV_DXWLIST, dx_wlist)) {
        memset (dx_wlist, 0, sizeof(dx_wlist));
        NVWriteString(NV_DXWLIST, dx_wlist);
    }

    uint8_t nv_wsjt;
    if (!NVReadUInt8 (NV_WSJT_DX, &nv_wsjt)) {
        // check host for possible backwards compat
        if (strcasecmp(dx_host,"WSJT-X") == 0 || strcasecmp(dx_host,"JTDX") == 0) {
            nv_wsjt = 1;
            memset (dx_host, 0, sizeof(dx_host));
            NVWriteString(NV_DXHOST, dx_host);
        } else
            nv_wsjt = 0;
        NVWriteUInt8 (NV_WSJT_DX, nv_wsjt);
    }
    bool_pr[CLISWSJTX_BPR].state = (nv_wsjt != 0);

    uint8_t nv_dx;
    if (!NVReadUInt8 (NV_USEDXCLUSTER, &nv_dx)) {
        nv_dx = false;
        NVWriteUInt8 (NV_USEDXCLUSTER, nv_dx);
    }
    bool_pr[CLUSTER_BPR].state = (nv_dx != 0);

    uint8_t spotops;
    if (!NVReadUInt8 (NV_MAPSPOTS, &spotops)) {
        spotops = NVMS_PREFIX | NVMS_THIN;
        NVWriteUInt8 (NV_MAPSPOTS, spotops);
    }
    uint8_t spotops_msk = spotops & NVMS_MKMSK;
    bool_pr[SPOTLBL_BPR].state =     spotops_msk == NVMS_DOT || spotops_msk == NVMS_CALL;
    bool_pr[SPOTLBLCALL_BPR].state = spotops_msk == NVMS_PREFIX || spotops_msk == NVMS_CALL;
    bool_pr[SPOTPATH_BPR].state =    (spotops & (NVMS_WIDE|NVMS_THIN)) != 0;
    bool_pr[SPOTPATHSZ_BPR].state =  (spotops & NVMS_WIDE) != 0;

    uint8_t dx_cmdmask;
    if (!NVReadUInt8 (NV_DXCMDUSED, &dx_cmdmask)) {
        dx_cmdmask = 0;
        NVWriteUInt8 (NV_DXCMDUSED, dx_cmdmask);
    }
    bool_pr[DXCLCMD0_BPR].state = (dx_cmdmask & 1) != 0;
    bool_pr[DXCLCMD1_BPR].state = (dx_cmdmask & 2) != 0;
    bool_pr[DXCLCMD2_BPR].state = (dx_cmdmask & 4) != 0;
    bool_pr[DXCLCMD3_BPR].state = (dx_cmdmask & 8) != 0;



    // init de lat/lng

    // if de never set before set to cental US so it differs from default DX which is 0/0.
    if (!NVReadFloat (NV_DE_LAT, &de_ll.lat_d) || !NVReadFloat (NV_DE_LNG, &de_ll.lng_d)) {
        // http://www.kansastravel.org/geographicalcenter.htm
        de_ll.lng_d = -99;
        de_ll.lat_d = 40;
        normalizeLL(de_ll);
        setNVMaidenhead(NV_DE_GRID, de_ll);
        de_tz.tz_secs = getTZ (de_ll);
        NVWriteInt32(NV_DE_TZ, de_tz.tz_secs);
        NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
        NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
    }

    // reset until ll fields are edited this session
    ll_edited = false;


    // init KX3. NV stores actual baud rate, we just toggle between 4800 and 38400, 0 means off

    uint32_t kx3;
    if (!NVReadUInt32 (NV_KX3BAUD, &kx3)) {
        kx3 = 0;                                // default off
        NVWriteUInt32 (NV_KX3BAUD, kx3);
    }
    bool_pr[KX3ON_BPR].state = (kx3 != 0);
    bool_pr[KX3BAUD_BPR].state = (kx3 == 38400);


    // init GPIOOK -- might effect KX3ON

    uint8_t gpiook;
    if (!NVReadUInt8 (NV_GPIOOK, &gpiook)) {
        gpiook = 0;                             // default off
        NVWriteUInt8 (NV_GPIOOK, gpiook);
    }
    bool_pr[GPIOOK_BPR].state = (gpiook != 0);
    if (!gpiook && bool_pr[KX3ON_BPR].state) {
        // no KX3 if no GPIO
        bool_pr[KX3ON_BPR].state = false;
        NVWriteUInt32 (NV_KX3BAUD, 0);
    }


    // init WiFi

#if defined(_WIFI_ALWAYS)
    bool_pr[WIFI_BPR].state = true;             // always on
    bool_pr[WIFI_BPR].p_str = "WiFi:";          // not a question
#elif defined(_WIFI_ASK)
    bool_pr[WIFI_BPR].state = false;            // default off
#endif


    // init colors

    for (int i = 0; i < N_CSPR; i++) {
        ColSelPrompt &p = csel_pr[i];
        uint16_t c;
        if (!NVReadUInt16 (p.nv, &c)) {
            c = p.def_c;
            NVWriteUInt16 (p.nv, c);
        }
        p.r = RGB565_R(c);
        p.g = RGB565_G(c);
        p.b = RGB565_B(c);
    }

    // dashed settings
    uint32_t dashed;
    if (!NVReadUInt32 (NV_DASHED, &dashed)) {
        dashed = 0;
        NVWriteUInt32 (NV_DASHED, dashed);
    }
    for (int i = 0; i < N_CSPR; i++)
        csel_pr[i].a_state = (dashed & (1 << i)) ? true : false;

#if defined (_IS_ESP8266)
    // ESP does not support paths period, let alone dashed paths
    NODASH (csel_pr[BAND160_CSPR]);
    NODASH (csel_pr[BAND80_CSPR]);
    NODASH (csel_pr[BAND60_CSPR]);
    NODASH (csel_pr[BAND40_CSPR]);
    NODASH (csel_pr[BAND30_CSPR]);
    NODASH (csel_pr[BAND20_CSPR]);
    NODASH (csel_pr[BAND17_CSPR]);
    NODASH (csel_pr[BAND15_CSPR]);
    NODASH (csel_pr[BAND12_CSPR]);
    NODASH (csel_pr[BAND10_CSPR]);
    NODASH (csel_pr[BAND6_CSPR]);
    NODASH (csel_pr[BAND2_CSPR]);
#endif


    // X11 flags, engage immediately if defined or sensible thing to do
    uint16_t x11flags;
    int dspw, dsph;
    tft.getScreenSize (&dspw, &dsph);
    Serial.printf (_FX("Display is %d x %d\n"), dspw, dsph);
    Serial.printf (_FX("Built for %d x %d\n"), BUILD_W, BUILD_H);
    if (NVReadUInt16 (NV_X11FLAGS, &x11flags)) {
        Serial.printf (_FX("x11flags found 0x%02X\n"), x11flags);
        bool_pr[X11_FULLSCRN_BPR].state = (x11flags & X11BIT_FULLSCREEN) == X11BIT_FULLSCREEN;
        tft.X11OptionsEngageNow(getX11FullScreen());
    } else {
        // set typical defaults but wait for user choices to save
        bool_pr[X11_FULLSCRN_BPR].state = false;

        // engage full screen now if required to see app
        if (BUILD_W == dspw || BUILD_H == dsph) {
            bool_pr[X11_FULLSCRN_BPR].state = true;
            tft.X11OptionsEngageNow(getX11FullScreen());
        }
    }

    // init and validate daily on-off times

    uint16_t onoff[NV_DAILYONOFF_LEN];
    if (!NVReadString (NV_DAILYONOFF, (char*)onoff)) {
        // try to init from deprecated values
        uint16_t on, off;
        if (!NVReadUInt16 (NV_DPYON, &on))
            on = 0;
        if (!NVReadUInt16 (NV_DPYOFF, &off))
            off = 0;   
        for (int i = 0; i < DAYSPERWEEK; i++) {
            onoff[i] = on;
            onoff[i+DAYSPERWEEK] = off;
        }
        NVWriteString (NV_DAILYONOFF, (char*)onoff);
    } else {
        // reset all if find any bogus from 2.60 bug
        for (int i = 0; i < 2*DAYSPERWEEK; i++) {
            if (onoff[i] >= MINSPERDAY || (onoff[i]%5)) {
                memset (onoff, 0, sizeof(onoff));
                NVWriteString (NV_DAILYONOFF, (char*)onoff);
                break;
            }
        }
    }


    // init several more misc

    uint8_t df_mdy, df_dmyymd;
    if (!NVReadUInt8 (NV_DATEMDY, &df_mdy) || !NVReadUInt8 (NV_DATEDMYYMD, &df_dmyymd)) {
        df_mdy = 0;
        df_dmyymd = 0;
        NVWriteUInt8 (NV_DATEMDY, df_mdy);
        NVWriteUInt8 (NV_DATEDMYYMD, df_dmyymd);
    }
    bool_pr[DATEFMT_MDY_BPR].state = (df_mdy != 0);
    bool_pr[DATEFMT_DMYYMD_BPR].state = (df_dmyymd != 0);

    uint8_t logok;
    if (!NVReadUInt8 (NV_LOGUSAGE, &logok)) {
        logok = 0;
        NVWriteUInt8 (NV_LOGUSAGE, logok);
    }
    bool_pr[LOGUSAGE_BPR].state = (logok != 0);

    uint8_t rot;
    if (!NVReadUInt8 (NV_ROTATE_SCRN, &rot)) {
        rot = 0;
        NVWriteUInt8 (NV_ROTATE_SCRN, rot);
    }
    bool_pr[FLIP_BPR].state = (rot != 0);

    uint8_t met;
    if (!NVReadUInt8 (NV_METRIC_ON, &met)) {
        met = 0;
        NVWriteUInt8 (NV_METRIC_ON, met);
    }
    bool_pr[UNITS_BPR].state = (met != 0);

    uint8_t weekmon;
    if (!NVReadUInt8 (NV_WEEKMON, &weekmon)) {
        weekmon = 0;
        NVWriteUInt8 (NV_WEEKMON, weekmon);
    }
    bool_pr[WEEKDAY1MON_BPR].state = (weekmon != 0);

    uint8_t b_mag;
    if (!NVReadUInt8 (NV_BEAR_MAG, &b_mag)) {
        b_mag = 0;
        NVWriteUInt8 (NV_BEAR_MAG, b_mag);
    }
    bool_pr[BEARING_BPR].state = (b_mag != 0);

    if (!NVReadInt16 (NV_CENTERLNG, &center_lng)) {
        center_lng = 0;
        NVWriteInt16 (NV_CENTERLNG, center_lng);
    }

    // init night option
    if (!NVReadUInt8 (NV_NIGHT_ON, &night_on)) {
        night_on = 1;
        NVWriteUInt8 (NV_NIGHT_ON, night_on);
    }

    // init place names option
    if (!NVReadUInt8 (NV_NAMES_ON, &names_on)) {
        names_on = 0;
        NVWriteUInt8 (NV_NAMES_ON, names_on);
    }

    if (!NVReadFloat (NV_TEMPCORR76, &temp_corr[BME_76])) {
        temp_corr[BME_76] = 0;
        NVWriteFloat (NV_TEMPCORR76, temp_corr[BME_76]);
    }
    if (!NVReadFloat (NV_PRESCORR76, &pres_corr[BME_76])) {
        pres_corr[BME_76] = 0;
        NVWriteFloat (NV_PRESCORR76, pres_corr[BME_76]);
    }
    if (!NVReadFloat (NV_TEMPCORR77, &temp_corr[BME_77])) {
        temp_corr[BME_77] = 0;
        NVWriteFloat (NV_TEMPCORR77, temp_corr[BME_77]);
    }
    if (!NVReadFloat (NV_PRESCORR77, &pres_corr[BME_77])) {
        pres_corr[BME_77] = 0;
        NVWriteFloat (NV_PRESCORR77, pres_corr[BME_77]);
    }

    bool_pr[GEOIP_BPR].state = false;

    if (!NVReadUInt8 (NV_BR_MIN, &bright_min)) {
        bright_min = 0;
        NVWriteUInt8 (NV_BR_MIN, bright_min);
    }
    if (!NVReadUInt8 (NV_BR_MAX, &bright_max)) {
        bright_max = 100;
        NVWriteUInt8 (NV_BR_MAX, bright_max);
    }

    uint8_t scroll_dir;
    if (!NVReadUInt8 (NV_SCROLLDIR, &scroll_dir)) {
        scroll_dir = 0;
        NVWriteUInt8 (NV_SCROLLDIR, scroll_dir);
    }
    bool_pr[SCROLLDIR_BPR].state = (scroll_dir != 0);


    #if defined(_SUPPORT_SCROLLLEN)
        uint8_t scroll_len;
        if (!NVReadUInt8 (NV_SCROLLLEN, &scroll_len)) {
            scroll_len = NSCROLL_C;
            NVWriteUInt8 (NV_SCROLLLEN, scroll_len);
        }

        // entangled: 0: FF  10: TF   25: TT  50: FT
        if (scroll_len < NSCROLL_B) {
            // NSCROLL_A
            bool_pr[SCROLLLEN_BPR].state = false;
            bool_pr[SCROLLBIG_BPR].state = false;
        } else if (scroll_len < NSCROLL_C) {
            // NSCROLL_B
            bool_pr[SCROLLLEN_BPR].state = true;
            bool_pr[SCROLLBIG_BPR].state = false;
        } else if (scroll_len < NSCROLL_D) {
            // NSCROLL_C
            bool_pr[SCROLLLEN_BPR].state = false;
            bool_pr[SCROLLBIG_BPR].state = true;
        } else {
            // NSCROLL_D
            bool_pr[SCROLLLEN_BPR].state = true;
            bool_pr[SCROLLBIG_BPR].state = true;
        }
    #else
        // always force to zero
        NVWriteUInt8 (NV_SCROLLLEN, 0);
        bool_pr[SCROLLLEN_BPR].state = bool_pr[SCROLLBIG_BPR].state = false;
    #endif
}


/* return whether user wants to run setup.
 */ 
static bool askRun()
{
    eraseScreen();

    drawStringInBox ("Skip", skip_b, false, TX_C);

    tft.setTextColor (TX_C);
    tft.setCursor (tft.width()/6, tft.height()/5);

    // appropriate prompt
#if defined(_IS_ESP8266)
    tft.print (F("Tap anywhere to enter Setup ... "));
#else
    tft.print (F("Click anywhere to enter Setup ... "));
#endif // _IS_ESP8266

    int16_t x = tft.getCursorX();
    int16_t y = tft.getCursorY();
    uint16_t to;
    for (to = ASK_TO*10; !skip_skip && to > 0; --to) {
        resetWatchdog();
        if ((to+9)/10 != (to+10)/10) {
            tft.fillRect (x, y-PR_A, 2*PR_W, PR_A+PR_D, BG_C);
            tft.setCursor (x, y);
            tft.print((to+9)/10);
        }

        // check for touch, type ESC or abort box
        SCoord s;
        TouchType tt = readCalTouchWS (s);
        char c = tft.getChar (NULL, NULL);
        if (tt != TT_NONE || c) {
            drainTouch();
            if (c == 27 || (tt != TT_NONE && inBox (s, skip_b))) {
                drawStringInBox ("Skip", skip_b, true, TX_C);
                return (false);
            }
                
            break;
        }
        wdDelay(100);
    }

    return (!skip_skip && to > 0);
}





/* init display and supporting StringPrompt and BoolPrompt data structs
 */
static void initDisplay()
{
    // erase screen
    eraseScreen();

    // set invalid page
    cur_page = -1;


#if defined(_SHOW_ALL) || defined(_MARK_BOUNDS)
    // don't show my creds when testing
    strcpy (wifi_ssid, _FX("mywifissid"));
    strcpy (wifi_pw, _FX("mywifipassword"));
#endif

    // force drawing first page
    cur_page = -1;
    changePage(0);
}

static void drawBMEPrompts (bool on)
{
    if (on) {
        drawSPPromptValue (&string_pr[BME76_DT]);
        drawSPPromptValue (&string_pr[BME76_DP]);
        drawSPPromptValue (&string_pr[BME77_DT]);
        drawSPPromptValue (&string_pr[BME77_DP]);
    } else {
        eraseSPPromptValue (&string_pr[BME76_DT]);
        eraseSPPromptValue (&string_pr[BME76_DP]);
        eraseSPPromptValue (&string_pr[BME77_DT]);
        eraseSPPromptValue (&string_pr[BME77_DP]);
    }
}

/* run the setup screen until all fields check ok and user wants to exit
 */
static void runSetup()
{
    drainTouch();

    SBox screen;
    screen.x = 0;
    screen.y = 0;
    screen.w = tft.width();
    screen.h = tft.height();

    SCoord s;
    char c;
    UserInput ui = {
        screen,
        NULL,
        false,
        0,
        false,
        s,
        c,
    };

    do {
        StringPrompt *sp;
        BoolPrompt *bp = NULL;

        // wait for next tap or character input
        (void) waitForUser(ui);
        if (!ui.kbchar)
            (void) s2char (ui.tap, ui.kbchar);

        // process special cases first

        if (inBox (s, page_b)) {

            // change page back or forward depending on whether tapped in left or right half
            if (s.x > page_b.x + page_b.w/2)
                changePage ((cur_page+1)%N_PAGES);
            else
                changePage ((N_PAGES+cur_page-1)%N_PAGES);
            continue;
        }

        if (c == 27) {              // esc

            // show next page
            changePage ((cur_page+1)%N_PAGES);
            continue;
        }

        if (cur_page == COLOR_PAGE) {

            if (handleCSelTouch(s))
                continue;
        }

        if (cur_page == ONOFF_PAGE) {

            if (checkOnOffTouch(s))
                continue;
        }

        // proceed with normal fields processing

        if (c == '\t') {

            // move focus to next tab position
            eraseCursor();
            nextTabFocus();
            drawCursor();

        } else if (cur_focus.sp && (inBox (s, delete_b) || c == '\b' || c == 127)) {

            // tapped Delete or kb equiv while focus is string: remove one char

            StringPrompt *sp = cur_focus.sp;
            size_t vl = strlen (sp->v_str);
            if (vl > 0) {

                // erase cursor, shorten string, find new width, erase to end, redraw
                eraseCursor ();
                sp->v_str[vl-1] = '\0';
                uint16_t sw = getTextWidth (sp->v_str);
                tft.fillRect (sp->v_box.x+sw, sp->v_box.y, sp->v_box.w-sw, sp->v_box.h, BG_C);
                drawSPValue (sp);
                drawCursor ();

                checkLLGEdit(sp);
            }


        } else if (cur_focus.sp && isprint(c)) {

            // received a new char for string in focus

            StringPrompt *sp = cur_focus.sp;

            // append c if room, else ignore
            size_t vl = strlen (sp->v_str);
            if (vl < sp->v_len-1U) {

                eraseCursor ();

                sp->v_str[vl++] = c;
                sp->v_str[vl] = '\0';

                drawSPValue (sp);
                drawCursor ();

                checkLLGEdit(sp);
            }

        } else if (tappedBool (s, &bp) || (c == ' ' && cur_focus.bp)) {

            // typing space applies to focus bool
            if (c == ' ')
                bp = cur_focus.bp;

            // ignore tapping on bools not being shown
            if (!bp || !boolIsRelevant(bp))
                continue;

            // toggle and redraw with new cursor position
            engageBoolTap (bp);

            // check for possible secondary implications

            if (bp == &bool_pr[X11_FULLSCRN_BPR]) {

                // check for full screen that won't fit
                if (bp->state) {
                    int maxw = 0, maxh = 0;
                    tft.getScreenSize (&maxw, &maxh);
                    if (BUILD_W > maxw || BUILD_H > maxh) {
                        tft.setCursor (bp->s_box.x, bp->s_box.y+PR_H-PR_D);
                        tft.setTextColor (RA8875_RED);
                        eraseBPState (bp);
                        tft.print ("Won't fit");
                        wdDelay (2000);
                        bp->state = false;
                        drawBPState (bp);
                    }
                }
            }

            else if (bp == &bool_pr[GEOIP_BPR]) {
                // show/hide lat/lng/grid/gpsd prompts
                if (bp->state) {
                    // no gpsd
                    eraseSPPromptValue (&string_pr[GPSDHOST_SPR]);
                    eraseBPPromptState (&bool_pr[GPSDFOLLOW_BPR]);
                    bool_pr[GPSDON_BPR].state = false;
                    drawBPState (&bool_pr[GPSDON_BPR]);
                    // no lat/long/grid
                    eraseSPPromptValue (&string_pr[LAT_SPR]);
                    eraseSPPromptValue (&string_pr[LNG_SPR]);
                    eraseSPPromptValue (&string_pr[GRID_SPR]);
                } else {
                    // show lat/long/grid
                    drawSPPromptValue (&string_pr[LAT_SPR]);
                    drawSPPromptValue (&string_pr[LNG_SPR]);
                    drawSPPromptValue (&string_pr[GRID_SPR]);
                }
            }

            else if (bp == &bool_pr[NTPSET_BPR]) {
                // show/hide NTP host
                if (bp->state) {
                    // show host prompt
                    eraseBPState (&bool_pr[NTPSET_BPR]);
                    drawSPPromptValue (&string_pr[NTPHOST_SPR]);
                } else {
                    // show default 
                    eraseSPPromptValue (&string_pr[NTPHOST_SPR]);
                    drawBPState (&bool_pr[NTPSET_BPR]);
                }
            }

            else if (bp == &bool_pr[ADIFSET_BPR]) {
                // show/hide ADIF file name
                if (bp->state) {
                    // show file name
                    eraseBPState (&bool_pr[ADIFSET_BPR]);
                    drawSPPromptValue (&string_pr[ADIFFN_SPR]);
                } else {
                    // show no
                    eraseSPPromptValue (&string_pr[ADIFFN_SPR]);
                    drawBPState (&bool_pr[ADIFSET_BPR]);
                }
            }

            else if (bp == &bool_pr[RIGUSE_BPR]) {
                // show/hide rigctld host and port
                if (bp->state) {
                    // show host and port prompts and say yes
                    drawBPState (&bool_pr[RIGUSE_BPR]);
                    drawSPPromptValue (&string_pr[RIGHOST_SPR]);
                    drawSPPromptValue (&string_pr[RIGPORT_SPR]);
                } else {
                    // hide and say no
                    drawBPState (&bool_pr[RIGUSE_BPR]);
                    eraseSPPromptValue (&string_pr[RIGHOST_SPR]);
                    eraseSPPromptValue (&string_pr[RIGPORT_SPR]);
                }
            }

            else if (bp == &bool_pr[ROTUSE_BPR]) {
                // show/hide rotctld host and port
                if (bp->state) {
                    // show host and port prompts and say yes
                    drawBPState (&bool_pr[ROTUSE_BPR]);
                    drawSPPromptValue (&string_pr[ROTHOST_SPR]);
                    drawSPPromptValue (&string_pr[ROTPORT_SPR]);
                } else {
                    // hide and say no
                    drawBPState (&bool_pr[ROTUSE_BPR]);
                    eraseSPPromptValue (&string_pr[ROTHOST_SPR]);
                    eraseSPPromptValue (&string_pr[ROTPORT_SPR]);
                }
            }

            else if (bp == &bool_pr[FLRIGUSE_BPR]) {
                // show/hide flrig host and port
                if (bp->state) {
                    // show host and port prompts and say yes
                    drawBPState (&bool_pr[FLRIGUSE_BPR]);
                    drawSPPromptValue (&string_pr[FLRIGHOST_SPR]);
                    drawSPPromptValue (&string_pr[FLRIGPORT_SPR]);
                } else {
                    // hide and say no
                    drawBPState (&bool_pr[FLRIGUSE_BPR]);
                    eraseSPPromptValue (&string_pr[FLRIGHOST_SPR]);
                    eraseSPPromptValue (&string_pr[FLRIGPORT_SPR]);
                }
            }

            else if (bp == &bool_pr[ROTUSE_BPR]) {
                // show/hide rotctld host and port
                if (bp->state) {
                    // show host and port prompts and say yes
                    drawBPState (&bool_pr[ROTUSE_BPR]);
                    drawSPPromptValue (&string_pr[ROTHOST_SPR]);
                    drawSPPromptValue (&string_pr[ROTPORT_SPR]);
                } else {
                    // hide and say no
                    drawBPState (&bool_pr[ROTUSE_BPR]);
                    eraseSPPromptValue (&string_pr[ROTHOST_SPR]);
                    eraseSPPromptValue (&string_pr[ROTPORT_SPR]);
                }
            }

            else if (bp == &bool_pr[CLUSTER_BPR] || bp == &bool_pr[CLISWSJTX_BPR]) {
                // so many show/hide dx cluster prompts easier to just redraw the page
                changePage (cur_page);
            }

            else if (bp == &bool_pr[GPSDON_BPR]) {
                // show/hide gpsd host, geolocate, lat/long/grid
                if (bp->state) {
                    // no lat/long/grid
                    eraseSPPromptValue (&string_pr[LAT_SPR]);
                    eraseSPPromptValue (&string_pr[LNG_SPR]);
                    eraseSPPromptValue (&string_pr[GRID_SPR]);
                    // no geolocate
                    bool_pr[GEOIP_BPR].state = false;
                    drawBPState (&bool_pr[GEOIP_BPR]);
                    // show gpsd host and follow
                    drawSPPromptValue (&string_pr[GPSDHOST_SPR]);
                    drawBPPromptState (&bool_pr[GPSDFOLLOW_BPR]);
                } else {
                    // no gpsd host or follow
                    eraseSPPromptValue (&string_pr[GPSDHOST_SPR]);
                    eraseBPPromptState (&bool_pr[GPSDFOLLOW_BPR]);
                    drawBPState (&bool_pr[GPSDON_BPR]);
                    // show lat/long/grid
                    drawSPPromptValue (&string_pr[LAT_SPR]);
                    drawSPPromptValue (&string_pr[LNG_SPR]);
                    drawSPPromptValue (&string_pr[GRID_SPR]);
                }
            }

            else if (bp == &bool_pr[GPIOOK_BPR]) {
                if (bp->state) {
                    drawBPPrompt (&bool_pr[KX3ON_BPR]);
                    drawEntangledBools(&bool_pr[KX3ON_BPR], &bool_pr[KX3BAUD_BPR]);
                } else {
                    bool_pr[KX3ON_BPR].state = false;
                    eraseBPPromptState (&bool_pr[KX3ON_BPR]);
                    eraseBPPromptState (&bool_pr[KX3BAUD_BPR]);
                }
                drawBMEPrompts (bool_pr[GPIOOK_BPR].state || bool_pr[I2CON_BPR].state);
            }

            else if (bp == &bool_pr[I2CON_BPR]) {
                if (bp->state) {
                    // show file name
                    eraseBPState (&bool_pr[I2CON_BPR]);
                    drawSPPromptValue (&string_pr[I2CFN_SPR]);
                } else {
                    // show no
                    eraseSPPromptValue (&string_pr[I2CFN_SPR]);
                    drawBPState (&bool_pr[I2CON_BPR]);
                }
                drawBMEPrompts (bool_pr[GPIOOK_BPR].state || bool_pr[I2CON_BPR].state);
            }

          #if defined(_WIFI_ASK)
            else if (bp == &bool_pr[WIFI_BPR]) {
                // show/hide wifi prompts
                if (bp->state) {
                    eraseBPState (&bool_pr[WIFI_BPR]);
                    drawSPPromptValue (&string_pr[WIFISSID_SPR]);
                    drawSPPromptValue (&string_pr[WIFIPASS_SPR]);
                } else {
                    eraseSPPromptValue (&string_pr[WIFISSID_SPR]);
                    eraseSPPromptValue (&string_pr[WIFIPASS_SPR]);
                    drawBPState (&bool_pr[WIFI_BPR]);
                }
            }
          #endif // _WIFI_ASK

          #if defined(_SUPPORT_KX3)
            else if (bp == &bool_pr[KX3ON_BPR]) {
                // show/hide baud rate but honor GPIOOK
                if (bool_pr[GPIOOK_BPR].state) {
                    drawEntangledBools(&bool_pr[KX3ON_BPR], &bool_pr[KX3BAUD_BPR]);
                } else if (bool_pr[KX3ON_BPR].state) {
                    // maintain off if no GPIO
                    bool_pr[KX3ON_BPR].state = false;
                    drawBPPromptState (&bool_pr[KX3ON_BPR]);
                }
            }
          #endif // _SUPPORT_KX3

        } else if (tappedStringPrompt (s, &sp) && stringIsRelevant (sp)) {

            // move focus here unless already there
            if (cur_focus.sp != sp) {
                eraseCursor ();
                setFocus (sp, NULL);
                drawCursor ();
            }
        }

    } while (!(inBox (s, done_b) || c == '\r' || c == '\n') || !validateStringPrompts(true));

    drawDoneButton(true);

    // all fields are valid

}

/* update the case of each component of the given grid square.
 * N.B. we do NOT validate the grid
 */
static char *scrubGrid (char *g)
{
    g[0] = toupper(g[0]);
    g[1] = toupper(g[1]);
    g[4] = tolower(g[4]);
    g[5] = tolower(g[5]);

    return (g);
}

/* save all parameters to NVRAM
 */
static void saveParams2NV()
{
    // persist results 

#if !defined(_SHOW_ALL) && !defined(_MARK_BOUNDS)
    // only persist creds when not testing
    NVWriteString(NV_WIFI_SSID, wifi_ssid);
    NVWriteString(NV_WIFI_PASSWD, wifi_pw);
#endif

    NVWriteString(NV_CALLSIGN, call_sign);
    NVWriteUInt8 (NV_ROTATE_SCRN, bool_pr[FLIP_BPR].state);
    NVWriteUInt8 (NV_METRIC_ON, bool_pr[UNITS_BPR].state);
    NVWriteUInt8 (NV_WEEKMON, bool_pr[WEEKDAY1MON_BPR].state);
    NVWriteUInt8 (NV_BEAR_MAG, bool_pr[BEARING_BPR].state);
    NVWriteUInt32 (NV_KX3BAUD, bool_pr[KX3ON_BPR].state ? (bool_pr[KX3BAUD_BPR].state ? 38400 : 4800) : 0);
    NVWriteFloat (NV_TEMPCORR76, temp_corr[BME_76]);
    NVWriteFloat (NV_PRESCORR76, pres_corr[BME_76]);
    NVWriteFloat (NV_TEMPCORR77, temp_corr[BME_77]);
    NVWriteFloat (NV_PRESCORR77, pres_corr[BME_77]);
    NVWriteUInt8 (NV_BR_MIN, bright_min);
    NVWriteUInt8 (NV_BR_MAX, bright_max);
    NVWriteUInt8 (NV_USEGPSD, (bool_pr[GPSDON_BPR].state ? USEGPSD_FORTIME_BIT : 0)
                | (bool_pr[GPSDON_BPR].state && bool_pr[GPSDFOLLOW_BPR].state ? USEGPSD_FORLOC_BIT : 0));
    NVWriteString (NV_GPSDHOST, gpsd_host);
    NVWriteUInt8 (NV_USEDXCLUSTER, bool_pr[CLUSTER_BPR].state);
    NVWriteUInt8 (NV_WSJT_DX, bool_pr[CLISWSJTX_BPR].state);
    NVWriteString (NV_DXHOST, dx_host);
    NVWriteString (NV_DXCMD0, dxcl_cmds[0]);
    NVWriteString (NV_DXCMD1, dxcl_cmds[1]);
    NVWriteString (NV_DXCMD2, dxcl_cmds[2]);
    NVWriteString (NV_DXCMD3, dxcl_cmds[3]);
    NVWriteString (NV_DXWLIST, dx_wlist);

    uint8_t dx_cmdmask = 0;
    if (bool_pr[DXCLCMD0_BPR].state)
        dx_cmdmask |= 1;
    if (bool_pr[DXCLCMD1_BPR].state)
        dx_cmdmask |= 2;
    if (bool_pr[DXCLCMD2_BPR].state)
        dx_cmdmask |= 4;
    if (bool_pr[DXCLCMD3_BPR].state)
        dx_cmdmask |= 8;
    NVWriteUInt8 (NV_DXCMDUSED, dx_cmdmask);

    NVWriteUInt16 (NV_DXPORT, dx_port);
    NVWriteString (NV_DXLOGIN, dx_login);
    NVWriteUInt8 (NV_LOGUSAGE, bool_pr[LOGUSAGE_BPR].state);
    NVWriteUInt8 (NV_MAPSPOTS,
              (bool_pr[SPOTLBL_BPR].state ? (bool_pr[SPOTLBLCALL_BPR].state ? NVMS_CALL : NVMS_DOT)
                                          : (bool_pr[SPOTLBLCALL_BPR].state ? NVMS_PREFIX : NVMS_NONE))
            | (bool_pr[SPOTPATH_BPR].state ? (bool_pr[SPOTPATHSZ_BPR].state ? NVMS_WIDE : NVMS_THIN) : 0));
    NVWriteUInt8 (NV_NTPSET, bool_pr[NTPSET_BPR].state);
    NVWriteString (NV_NTPHOST, ntp_host);
    NVWriteString (NV_ADIFFN, adif_fn);
    NVWriteUInt8 (NV_I2CON, bool_pr[I2CON_BPR].state);
    NVWriteString (NV_I2CFN, i2c_fn);
    NVWriteUInt8 (NV_DATEMDY, bool_pr[DATEFMT_MDY_BPR].state);
    NVWriteUInt8 (NV_DATEDMYYMD, bool_pr[DATEFMT_DMYYMD_BPR].state);
    NVWriteUInt8 (NV_GPIOOK, bool_pr[GPIOOK_BPR].state);
    NVWriteInt16 (NV_CENTERLNG, center_lng);
    NVWriteUInt8 (NV_RIGUSE, bool_pr[RIGUSE_BPR].state);
    NVWriteString (NV_RIGHOST, rig_host);
    NVWriteUInt16 (NV_RIGPORT, rig_port);
    NVWriteUInt8 (NV_ROTUSE, bool_pr[ROTUSE_BPR].state);
    NVWriteString (NV_ROTHOST, rot_host);
    NVWriteUInt16 (NV_ROTPORT, rot_port);
    NVWriteUInt8 (NV_FLRIGUSE, bool_pr[FLRIGUSE_BPR].state);
    NVWriteString (NV_FLRIGHOST, flrig_host);
    NVWriteUInt16 (NV_FLRIGPORT, flrig_port);
    NVWriteUInt8 (NV_SCROLLDIR, bool_pr[SCROLLDIR_BPR].state);
    NVWriteUInt8 (NV_SCROLLLEN, nMoreScrollRows());

    // save and engage user's X11 settings
    uint16_t x11flags = 0;
    if (bool_pr[X11_FULLSCRN_BPR].state)
        x11flags |= X11BIT_FULLSCREEN;
    NVWriteUInt16 (NV_X11FLAGS, x11flags);
    tft.X11OptionsEngageNow(getX11FullScreen());

    // save colors
    for (int i = 0; i < N_CSPR; i++) {
        ColSelPrompt &p = csel_pr[i];
        uint16_t c = RGB565(p.r, p.g, p.b);
        NVWriteUInt16 (p.nv, c);
    }

    // save which colors are dashed
    uint32_t dashed = 0;
    for (int i = 0; i < N_CSPR; i++)
        if (csel_pr[i].a_state)
            dashed |= (1 << i);
    NVWriteUInt32 (NV_DASHED, dashed);

    // save DE tz and grid only if ll was edited and op is not using some other method to set location
    if (!bool_pr[GEOIP_BPR].state && !bool_pr[GPSDON_BPR].state && ll_edited) {
        normalizeLL (de_ll);
        NVWriteFloat(NV_DE_LAT, de_ll.lat_d);
        NVWriteFloat(NV_DE_LNG, de_ll.lng_d);
        NVWriteString(NV_DE_GRID, scrubGrid(string_pr[GRID_SPR].v_str));
        de_tz.tz_secs = getTZ (de_ll);
        NVWriteInt32(NV_DE_TZ, de_tz.tz_secs);
    }
}

/* draw the given string with border centered inside the given box using the current font.
 */
void drawStringInBox (const char str[], const SBox &b, bool inverted, uint16_t color)
{
    uint16_t sw = getTextWidth ((char*)str);

    uint16_t fg = inverted ? BG_C : color;
    uint16_t bg = inverted ? color : BG_C;

    tft.setCursor (b.x+(b.w-sw)/2, b.y+3*b.h/4);
    fillSBox (b, bg);
    drawSBox (b, KB_C);
    tft.setTextColor (fg);
    tft.print(str);
}


/* grab everything from NV, setting defaults if first time, then allow user to change,
 * saving to NV if needed.
 */
void clockSetup()
{
    // must start with a calibrated screen
    calibrateTouch(false);

    // set font used throughout, could use BOLD if not for long wifi password
    selectFontStyle (LIGHT_FONT, SMALL_FONT);

    // load values from nvram, else set defaults
    initSetup();

    // prep shadowed params, if nothing else for logging them
    initShadowedParams();

    // ask user whether they want to run setup, display anyway if any strings are invalid
    bool str_ok = validateStringPrompts (false);
    if ((!str_ok || askRun()) && askPasswd (_FX("setup"), false)) {

        // init display prompts and options
        initDisplay();

        // start by indicating any errors
        if (!str_ok)
            validateStringPrompts (true);

        // get current rotation state so we can tell whether it changes
        bool rotated = rotateScreen();

        // main interaction loop
        runSetup();

        // save
        saveParams2NV();

        // must recalibrate if rotating screen
        if (rotated != rotateScreen()) {
            tft.setRotation(rotateScreen() ? 2 : 0);
            calibrateTouch(true);
        }
    }

    // log and clean up shadowed params
    logAllPrompts();
    freeShadowedParams();

    // ok to send liveweb full screen setting
    liveweb_fs_ready = true;
}

/* return whether the given string is a valid latitude specification, if so set lat in degrees
 */
bool latSpecIsValid (const char *lat_spec, float &lat)
{
    float v;
    char ns = ' ', x = ' ';

    int n_lats = sscanf (lat_spec, "%f%c%c", &v, &ns, &x);
    ns = toupper(ns);
    if (n_lats == 1 && v >= -90 && v <= 90)
        lat = v;
    else if ((n_lats == 2 || (n_lats == 3 && isspace(x))) && v >= 0 && v <= 90 && (ns == 'N' || ns == 'S'))
        lat = ns == 'S' ? -v : v;
    else
        return (false);

    return (true);
}

/* return whether the given string is a valid longitude specification, if so set lng in degrees
 * N.B. we allow 180 east in spec but return lng as 180 west.
 */
bool lngSpecIsValid (const char *lng_spec, float &lng)
{
    float v;
    char ew = ' ', x = ' ';

    int n_lngs = sscanf (lng_spec, "%f%c%c", &v, &ew, &x);
    ew = toupper(ew);
    if (n_lngs == 1 && v >= -180 && v <= 180)
        lng = v;
    else if ((n_lngs == 2 || (n_lngs == 3 && isspace(x))) && v >= 0 && v <= 180 && (ew == 'E' || ew == 'W'))
        lng = ew == 'W' ? -v : v;
    else
        return (false);

    if (lng == 180)
        lng = -180;

    return (true);
}



/* only for main() to call once very early to allow setting initial default
 */
void setX11FullScreen (bool on)
{
    uint16_t x11flags = (on ? X11BIT_FULLSCREEN : 0);
    NVWriteUInt16 (NV_X11FLAGS, x11flags);
}


/* return pointer to static storage containing the WiFi SSID, else NULL if not used
 */
const char *getWiFiSSID()
{
    // don't try to set linux wifi while testing
    #ifndef _SHOW_ALL
        if (bool_pr[WIFI_BPR].state)
            return (wifi_ssid);
        else
    #endif // !_SHOW_ALL
            return (NULL);
}


/* return pointer to static storage containing the WiFi password, else NULL if not used
 */
const char *getWiFiPW()
{
    // don't try to set linux wifi while testing
    #ifndef _SHOW_ALL
        if (bool_pr[WIFI_BPR].state)
            return (wifi_pw);
        else
    #endif // !_SHOW_ALL
            return (NULL);
}


/* return pointer to static storage containing the Callsign
 */
const char *getCallsign()
{
    return (call_sign);
}

/* set a new default/persistent DE call sign.
 * also sets dx_login to match.
 * intended for use by set_newde API
 */
bool setCallsign (const char *cs)
{
    int csl = strlen (cs);
    if (cs[0] == '\0' || cs[0] == ' ' || csl >= NV_CALLSIGN_LEN || csl >= NV_DXLOGIN_LEN)
        return (false);
    strncpy (call_sign, cs, NV_CALLSIGN_LEN-1);
    strncpy (dx_login, cs, NV_DXLOGIN_LEN-1);
    NVWriteString (NV_CALLSIGN, call_sign);
    NVWriteString (NV_DXLOGIN, dx_login);
    return (true);
}

/* return pointer to static storage containing the DX cluster host
 * N.B. only sensible if useDXCluster() and !useWSJTX()
 */
const char *getDXClusterHost()
{
    return (dx_host);
}

/* return pointer to static storage containing the GPSD host
 * N.B. only sensible if useGPSDTime() and/or useGPSDLoc() is true
 */
const char *getGPSDHost()
{
    return (gpsd_host);
}

/* return pointer to static storage containing the NTP host defined herein
 * N.B. only sensible if useLocalNTPHost() is true
 */
const char *getLocalNTPHost()
{
    return (ntp_host);
}

/* return dx cluster node port
 * N.B. only sensible if useDXCluster() is true
 */
int getDXClusterPort()
{
    return (dx_port);
}

/* return whether we should be allowing DX cluster
 */
bool useDXCluster()
{
    return (bool_pr[CLUSTER_BPR].state);
}

/* return whether to rotate the screen
 */
bool rotateScreen()
{
    return (bool_pr[FLIP_BPR].state);
}

/* return whether to use metric units
 */
bool useMetricUnits()
{
    return (bool_pr[UNITS_BPR].state);
}

/* return whether week starts on Monday, else Sunday
 */
bool weekStartsOnMonday()
{
    return (bool_pr[WEEKDAY1MON_BPR].state);
}

/* return whether bearings should be magnetic, else true
 */
bool useMagBearing()
{
    return (bool_pr[BEARING_BPR].state);
}

/* return Raw size of spot paths, including zero if not wanted
 */
int getSpotPathSize()
{
#if defined(_SUPPORT_SPOTPATH)
    if (bool_pr[SPOTPATH_BPR].state)
        return (bool_pr[SPOTPATHSZ_BPR].state ? WIDEPATHSZ : THINPATHSZ);
    else
        return (0);
#else
    return (0);
#endif
}

/* return whether to label spots with either call or prefix.
 *   call plotSpotCallsigns() to determine which.
 *   this does NOT include DOT, call dotSpots() to determine that.
 */
bool labelSpots()
{
    return (bool_pr[SPOTLBLCALL_BPR].state);
}

/* return whether to label spots with dots.
 */
bool dotSpots()
{
    return (bool_pr[SPOTLBL_BPR].state && !bool_pr[SPOTLBLCALL_BPR].state);
}

/* return whether to label spots as whole callsigns, else just prefix.
 * N.B. only sensible if labelSpots() is true
 */
bool plotSpotCallsigns()
{
    return (bool_pr[SPOTLBL_BPR].state);
}

/* return whether to use IP geolocation
 */
bool useGeoIP()
{
    return (bool_pr[GEOIP_BPR].state);
}

/* return whether to use GPSD for time
 */
bool useGPSDTime()
{
    return (bool_pr[GPSDON_BPR].state);
}

/* return whether to use GPSD for location
 */
bool useGPSDLoc()
{
    return (bool_pr[GPSDON_BPR].state && bool_pr[GPSDFOLLOW_BPR].state);
}

/* return whether to use NTP host set here
 */
bool useLocalNTPHost()
{
    return (bool_pr[NTPSET_BPR].state);
}

/* return whether to use OS for time, not NTP
 */
bool useOSTime()
{
#if defined(_IS_ESP8266)
    // there is no OS
    return (false);
#else
    return (bool_pr[NTPSET_BPR].state && strcmp (ntp_host, "OS") == 0);
#endif
}

/* return desired date format
 */
DateFormat getDateFormat()
{
    if (bool_pr[DATEFMT_MDY_BPR].state)
        return (bool_pr[DATEFMT_DMYYMD_BPR].state ? DF_YMD : DF_DMY);
    else
        return (DF_MDY);
}

/* return whether user is ok with logging usage
 */
bool logUsageOk()
{
    return (bool_pr[LOGUSAGE_BPR].state);
}

/* return whether ok to use GPIO
 */
bool GPIOOk ()
{
    return (bool_pr[GPIOOK_BPR].state);
}



/* set temp correction, i is BME_76 or BME_77.
 * caller should establish units according to useMetricUnits().
 * save in NV if ok.
 * return whether appropriate.
 */
bool setBMETempCorr(BMEIndex i, float delta)
{
    if ((i != BME_76 && i != BME_77) || !getBMEData(i, false))
        return (false);

    // engage
    temp_corr[(int)i] = delta;

    // persist
    NVWriteFloat (i == BME_76 ? NV_TEMPCORR76 : NV_TEMPCORR77, temp_corr[i]);

    return (true);
}

/* return temperature correction for sensor given BME_76 or BME_77.
 * at this point it's just a number, caller should interpret according to useMetricUnits()
 */
float getBMETempCorr(int i)
{
    return (temp_corr[i % MAX_N_BME]);
}

/* set pressure correction, i is BME_76 or BME_77.
 * caller should establish units according to useMetricUnits().
 * save in NV if ok.
 * return whether appropriate.
 */
bool setBMEPresCorr(BMEIndex i, float delta)
{
    if ((i != BME_76 && i != BME_77) || !getBMEData(i, false))
        return (false);

    // engage
    pres_corr[(int)i] = delta;

    // persist
    NVWriteFloat (i == BME_76 ? NV_PRESCORR76 : NV_PRESCORR77, pres_corr[i]);

    return (true);
}

/* return pressure correction for sensor given BME_76 or BME_77.
 * at this point it's just a number, caller should interpret according to useMetricUnits()
 */
float getBMEPresCorr(int i)
{
    return (pres_corr[i % MAX_N_BME]);
}



/* return KX3 baud rate, 0 if off or no GPIO
 */
uint32_t getKX3Baud()
{
    return (bool_pr[KX3ON_BPR].state && bool_pr[GPIOOK_BPR].state
                ? (bool_pr[KX3BAUD_BPR].state ? 38400 : 4800) : 0);
}

/* return desired maximum brightness, percentage
 */
uint8_t getBrMax()
{
    return (brDimmableOk() ? bright_max : 100);
}

/* return desired minimum brightness, percentage
 */
uint8_t getBrMin()
{
    return (brDimmableOk() ? bright_min : 0);
}

/* whether to engage full screen.
 */
bool getX11FullScreen(void)
{
    return (bool_pr[X11_FULLSCRN_BPR].state);
}

/* whether demo mode is requested
 */
bool getDemoMode(void)
{
    return (bool_pr[DEMO_BPR].state);
}

/* set whether demo mode is active
 */
void setDemoMode(bool on)
{
    bool_pr[DEMO_BPR].state = on;
}

/* return desired mercator map center longitude.
 * caller may assume -180 <= x < 180
 */
int16_t getCenterLng()
{
    return (alt_center_lng_set ? alt_center_lng : center_lng);
}

/* set desired mercator map center longitude.
 * N.B. only works for subsequenct calls to getCenterLng(): ignores initSetup() and not stored to NVRAM
 */
void setCenterLng (int16_t l)
{
    l = ((l + (180+360*10)) % 360) - 180;       // enforce [-180, 180)
    alt_center_lng = l;
    alt_center_lng_set = true;
}

/* get rigctld host and port and return whether it's to be used at all.
 * if just want yes/no either or both pointers can be NULL.
 */
bool getRigctld (char host[NV_RIGHOST_LEN], int *portp)
{
    if (bool_pr[RIGUSE_BPR].state) {
        if (host != NULL)
            strcpy (host, rig_host);
        if (portp != NULL)
            *portp = rig_port;
        return (true);
    }
    return (false);
}

/* get rotctld host and port and return whether it's to be used at all.
 * if just want yes/no either or both pointers can be NULL.
 */
bool getRotctld (char host[NV_ROTHOST_LEN], int *portp)
{
    if (bool_pr[ROTUSE_BPR].state) {
        if (host != NULL)
            strcpy (host, rot_host);
        if (portp != NULL)
            *portp = rot_port;
        return (true);
    }
    return (false);
}

/* get flrig host and port and return whether it's to be used at all.
 * if just want yes/no either or both pointers can be NULL.
 */
bool getFlrig (char host[NV_FLRIGHOST_LEN], int *portp)
{
    if (bool_pr[FLRIGUSE_BPR].state) {
        if (host != NULL)
            strcpy (host, flrig_host);
        if (portp != NULL)
            *portp = flrig_port;
        return (true);
    }
    return (false);
}

/* get name to use for cluster login
 */
const char *getDXClusterLogin()
{
    return (dx_login[0] != '\0' ? dx_login : call_sign);
}

/* return cluster commands and whether each is on or off.
 */
void getDXClCommands(const char *cmds[N_DXCLCMDS], bool on[N_DXCLCMDS])
{
    cmds[0] = dxcl_cmds[0];
    cmds[1] = dxcl_cmds[1];
    cmds[2] = dxcl_cmds[2];
    cmds[3] = dxcl_cmds[3];

    on[0] = bool_pr[DXCLCMD0_BPR].state;
    on[1] = bool_pr[DXCLCMD1_BPR].state;
    on[2] = bool_pr[DXCLCMD2_BPR].state;
    on[3] = bool_pr[DXCLCMD3_BPR].state;
}

/* set a new host and port, return reason if false.
 * N.B. host is trimmed IN PLACE
 */
bool setDXCluster (char *host, const char *port_str, char ynot[])
{
    if (!hostOK(host,NV_DXHOST_LEN)) {
        strcpy (ynot, _FX("Bad host"));
        return (false);
    }
    if (!portOK (port_str, 1000, &dx_port)) {
        strcpy (ynot, _FX("Bad port"));
        return (false);
    }
    strncpy (dx_host, host, NV_DXHOST_LEN-1);
    NVWriteString (NV_DXHOST, dx_host);
    NVWriteUInt16 (NV_DXPORT, dx_port);
    return(true);
}

/* return current rgb565 color for the given ColorSelection
 */
uint16_t getMapColor (ColorSelection id)
{
    uint16_t c;
    if (id >= 0 && id < N_CSPR) {
        ColSelPrompt &p = csel_pr[id];
        c = RGB565(p.r, p.g, p.b);
    } else
        c = RA8875_BLACK;
    return (c);
}

/* return name of the given color
 */
const char* getMapColorName (ColorSelection id)
{
    const char *n;
    if (id >= 0 && id < N_CSPR)
        n = csel_pr[id].p_str;
    else
        n = "???";
    return (n);
}

/* try to set the specified color, name may use '_' or ' '.
 * return whether name is found.
 * N.B. this only saves the new value, other subsystems must do their own query to utilize new values.
 */
bool setMapColor (const char *name, uint16_t rgb565)
{
    // name w/o _
    char scrub_name[50];
    strncpySubChar (scrub_name, name, ' ', '_', sizeof(scrub_name));

    // look for match
    for (int i = 0; i < N_CSPR; i++) {
        ColSelPrompt &p = csel_pr[i];
        if (strcmp (scrub_name, p.p_str) == 0) {
            NVWriteUInt16 (p.nv, rgb565);
            p.r = RGB565_R(rgb565);
            p.g = RGB565_G(rgb565);
            p.b = RGB565_B(rgb565);
            return (true);
        }
    }
    return (false);
}

/* return whether the given color line should be dashed
 */
bool getColorDashed (ColorSelection id)
{
    return (csel_pr[id].a_state);
}

/* return whether dx host is actually WSJT-X, else 
 */
bool useWSJTX(void)
{
    return (bool_pr[CLISWSJTX_BPR].state);
}

/* return name of ADIF, else NULL if not set
 */
const char *getADIFilename(void)
{
    return (bool_pr[ADIFSET_BPR].state ? adif_fn : NULL);
}

/* return name of I2C device to use, else NULL
 */
const char *getI2CFilename(void)
{
    // N.B. do not call I2CFnOk here, just rely on it having been used by setup to determine I2CON_BPR
    return (bool_pr[I2CON_BPR].state ? i2c_fn : NULL);
}

/* return whether the given call is on the dx cluster watch list
 */
bool onDXWatchList (const char *call)
{
    // dx_wlist is a list of calls or prefixes separated by spaces or commas.
    // call is considered to be in the list if its first chars match any of the calls or prefixes.

    // copy for strtok
    StackMalloc watched(sizeof(dx_wlist));
    char *wl = (char *) watched.getMem();
    strcpy (wl, dx_wlist);

    // separators
    const char *sep = ", ";
    
    // scan for match
    for (char *prefix = strtok (wl, sep); prefix; prefix = strtok (NULL, sep))
        if (strncasecmp (call, prefix, strlen(prefix)) == 0)
            return (true);

    // no match
    return (false);
}

/* return whether scolling panes should show the newest entry on top, else newest on bottom
 */
bool scrollTopToBottom(void)
{
    return (bool_pr[SCROLLDIR_BPR].state);
}

/* return number of ADDITIONAL scroll rows
 */
int nMoreScrollRows(void)
{
    return (atoi (getEntangledValue (&bool_pr[SCROLLLEN_BPR], &bool_pr[SCROLLBIG_BPR])));
}
