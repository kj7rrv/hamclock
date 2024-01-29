/* implement a simple stopwatch with lap timer, countdown timer, alarm clock and a pair of Big Clocks.
 *
 * we maintain two separate states:
 *   SWDisplayState: a display state that indicates which page we are showing, if any.
 *   SWEngineState:  an engine state that indicates what is running, if anything.
 */


#include "HamClock.h"


// set to show all boxes for debugging
// #define _SHOW_ALL                    // remove before flight

#define SW_CD_BLINKHZ   2                       // LED warning blink rate


// countdown ranges, including flashing states
typedef enum {
    SWCDS_OFF,                          // idle or dark
    SWCDS_RUNOK,                        // more than SW_CD_WARNDT remaining
    SWCDS_WARN,                         // > 0 but < SW_CD_WARNDT remaining
    SWCDS_TIMEOUT,                      // timed out
} SWCDState;



/* info to manage the stopwatch blinker threads
 */
static volatile ThreadBlinker sw_cd_blinker_red;
static volatile ThreadBlinker sw_cd_blinker_grn;
static volatile MCPPoller sw_cd_reset;
static volatile MCPPoller sw_alarmoff;

/* return all IO lines to benign state
 */
void SWresetIO()
{
    disableBlinker (sw_cd_blinker_red);
    disableBlinker (sw_cd_blinker_grn);
    disableMCPPoller (sw_cd_reset);
    disableMCPPoller (sw_alarmoff);

    mcp.pinMode (SW_CD_RED_PIN, INPUT);
    mcp.pinMode (SW_CD_GRN_PIN, INPUT);
    mcp.pinMode (SW_ALARMOUT_PIN, INPUT);
    mcp.pinMode (SW_ALARMOFF_PIN, INPUT);

}

/* set the LEDs to indicate the given countdown range state
 */
static void setCDLEDState (SWCDState cds)
{
    switch (cds) {
    case SWCDS_OFF:
        // both off
        setBlinkerRate (sw_cd_blinker_grn, BLINKER_OFF_HZ);
        setBlinkerRate (sw_cd_blinker_red, BLINKER_OFF_HZ);
        break;
    case SWCDS_RUNOK:
        // green on
        setBlinkerRate (sw_cd_blinker_grn, BLINKER_ON_HZ);
        setBlinkerRate (sw_cd_blinker_red, BLINKER_OFF_HZ);
        break;
    case SWCDS_WARN: 
        // green blinking, red on
        setBlinkerRate (sw_cd_blinker_grn, SW_CD_BLINKHZ);
        setBlinkerRate (sw_cd_blinker_red, BLINKER_ON_HZ);
        break;
    case SWCDS_TIMEOUT:
        // red blinking
        setBlinkerRate (sw_cd_blinker_grn, BLINKER_OFF_HZ);
        setBlinkerRate (sw_cd_blinker_red, SW_CD_BLINKHZ);
        break;
    }
}

/* return whether the countdown pin has toggled low,
 * ie, this is an edge-triggered state in order that it does not stay active if used with a PTT switch.
 */
static bool countdownSwitchIsTrue()
{
    static bool prev_pin_true;
    static bool prev_pin_known;

    // read pin, active low
    bool pin_true = !readMCPPoller (sw_cd_reset);

    // init history if first time
    if (!prev_pin_known) {
        prev_pin_true = pin_true;
        prev_pin_known = true;
    }

    // return whether went low
    if (pin_true != prev_pin_true) {
        prev_pin_true = pin_true;
        return (pin_true);
    } else
        return (false);
}


/* return state of alarm clock reset switch.
 */
static bool alarmSwitchIsTrue(void)
{
    // pin is active-low
    return (!readMCPPoller (sw_alarmoff));
}

/* control the alarm clock output pin
 */
static void setAlarmPin (bool set)
{
    mcp.digitalWrite (SW_ALARMOUT_PIN, set);
}


// stopwatch params
#define SW_NDIG         9                       // number of display digits
#define SW_BG           RA8875_BLACK            // bg color
#define SW_ND           8                       // number of digits
#define SW_DGAP         40                      // gap between digits
#define SW_Y0           190                     // upper left Y of all time digits
#define SW_DW           45                      // digit width
#define SW_DH           100                     // digit heigth
#define SW_X0           ((800-SW_ND*SW_DW-(SW_ND-1)*SW_DGAP)/2)   // x coord of left-most digit to center
#define SW_LT           1                       // line thickness
#define SW_PUNCR        3                       // punctuation radius
#define SW_BAX          240                     // control button A x
#define SW_BBX          440                     // control button B x
#define SW_EXITX        670                     // exit button x
#define SW_EXITY        420                     // exit button y
#define SW_BCX          10                      // big-clock button x
#define SW_BCY          SW_EXITY                // big-clock button y
#define SW_BY           350                     // control button y
#define SW_BW           120                     // button width
#define SW_BH           40                      // button height
#define SW_CX           SW_BAX                  // color scale x
#define SW_CY           SW_EXITY                // color scale y
#define SW_CW           (SW_BBX+SW_BW-SW_CX)    // color scale width
#define SW_CH           SW_BH                   // color scale height
#define SW_HSV_S        200                     // color scale HSV saturation, 0..255
#define SW_HSV_V        255                     // color scale HSV value, 0..255

// alarm clock params
#define ALM_X0          180                     // alarm control button x
#define ALM_Y0          25                      // alarm control button y
#define ALM_W           200                     // alarm control button width
#define ALM_EX          420                     // alarm time display box x
#define ALM_EY          ALM_Y0                  // alarm time display box y
#define ALM_EW          SW_CDP_W                // alarm time display box w
#define ALM_TOVFLOW     (24U*60U)               // hrmn overflow value
#define ALM_RINGTO      60000                   // alarm clock ringing timeout, millis

// countdown params
#define SW_CD_X         ALM_X0                  // countdown button x
#define SW_CD_Y         (ALM_Y0+2*SW_BH)        // countdown button y
#define SW_CD_W         ALM_W                   // countdown button width
#define SW_CDP_X        ALM_EX                  // countdown period display box x
#define SW_CDP_W        ALM_W                   // countdown period display box width
#define SW_CD_WARNDT    60000                   // countdown warning time, ms
#define SW_CD_AGEDT     30000                   // countdown aged period, ms

// big analog clock params
#define BAC_X0          400                     // x center
#define BAC_Y0          240                     // y center
#define BAC_MNR         210                     // minute hand radius
#define BAC_SCR         180                     // second hand radius
#define BAC_HRR         130                     // hour hand radius
#define BAC_FR          232                     // face radius
#define BAC_BEZR        238                     // bezel radius
#define BAC_HTR         12                      // hour tick radius
#define BAC_MTR         5                       // minute tick radius
#define BACD_Y0         180                     // when add digital y center
#define BACD_MNR        158                     // when add digital minute hand radius
#define BACD_SCR        135                     // when add digital second hand radius
#define BACD_HRR        97                      // when add digital hour hand radius
#define BACD_FR         174                     // when add digital face radius
#define BACD_BEZR       178                     // when add digital bezel radius
#define BACD_HTR        10                      // when add digital hour tick radius
#define BACD_MTR        4                       // when add digital minute tick radius
#define BACD_DY         430                     // when add digital y coord of digits
#define BACD_HMX0       336                     // when add digital H:M starting x
#define BACD_HMSX0      305                     // when add digital H:M:S starting x
#define BAC_DOTR        2                       // center dot radius
#define BAC_HRTH        deg2rad(15.0F)          // hour hand thickness half-angle, rads
#define BAC_MNTH        (BAC_HRTH*BAC_HRR/BAC_MNR) // minute hand thickness half-angle, rads
#define BAC_HTTH        deg2rad(0.6F)           // hour tick half-angle as seen from center, rads
#define BAC_FCOL        sw_col                  // face color
#define BAC_HRCOL       sw_col                  // hour hand color
#define BAC_MNCOL       sw_col                  // minute hand color
#define BAC_SCCOL       GRAY                    // second hand color
#define BAC_BEZCOL      GRAY                    // bezel color
#define BAC_DATEX       2                       // date box X -- just to anchor text
#define BAC_DATEY       2                       // date box Y -- just to anchor text
#define BAC_DATEW       200                     // date box width -- used just for tapping
#define BAC_DATEH       150                     // date box height -- used just for tapping
#define BAC_WXX         (800-PLOTBOX_W-1)       // weather box X
#define BAC_WXY         5                       // weather box Y
#define BAC_WXW         PLOTBOX_W               // weather box width
#define BAC_WXH         PLOTBOX_H               // weather box height
#define BAC_WXGDT       (30L*60*1000)           // weather update period when good, millis
#define BAC_WXFDT       (6*1000)                // weather update period when fail, millis

// big digital clock
#define BDC_HMW         110                     // digit width
#define BDC_HMCW        (2*BDC_HMW/3)           // colon spacing
#define BDC_HMH         (2*BDC_HMW)             // digit height
#define BDC_HMY0        (BAC_WXY+BAC_WXH+20)    // top y
#define BDC_HMLT        (BDC_HMW/6)             // line thickness
#define BDC_HMGAP       (BDC_HMW/3)             // gap between adjacent digits
#define BDC_HMX0        (400-(4*BDC_HMW+2*BDC_HMGAP+BDC_HMCW)/2)     // left x for hh:mm
#define BDC_HMCR        (BDC_HMLT/2)            // colon radius
#define BCD_SSZRED      3                       // reduce seconds size by this factor

// contols common to both big clock styles
#define BC_CDP_X        2                       // countdown period x
#define BC_CDP_Y        (480-SW_BH)             // countdown period y 
#define BC_CDP_W        100                     // countdown period width 
#define BC_CDP_H        SW_BH                   // countdown period height 
#define BC_ALM_X        (BC_CDP_X+BC_CDP_W)     // x coord of alarm time box
#define BC_ALM_Y        BC_CDP_Y                // y coord of alarm time box
#define BC_ALM_W        SW_BW                   // alarm message width
#define BC_ALM_H        SW_BH                   // alarm message height
#define BC_BAD_W        200                     // bad time message width
#define BC_BAD_H        SW_BH                   // bad time message height
#define BC_BAD_X        (800-BC_BAD_W-2)        // x coord of bad time message
#define BC_BAD_Y        BC_CDP_Y                // y coord of bad time message
#define BC_SAT_W        280                     // width of satellite state
#define BC_SAT_H        SW_BH                   // height of satellite state
#define BC_SAT_X        ((800-BC_SAT_W)/2)      // x coord of satellite state
#define BC_SAT_Y        BC_CDP_Y                // y coord of satellite state



// current state
static SWEngineState sws_engine;                // what is _running_
static SWDisplayState sws_display;              // what is _displaying_
static uint32_t countdown_period;               // count down from here, ms
static uint8_t swdigits[SW_NDIG];               // current digits
static uint32_t start_t, stop_dt;               // millis() at start, since stop
static uint16_t alarm_hrmn;                     // alarm time, hr*60 + min
static time_t alarm_ringtime;                   // now() when alarm started ringing
static AlarmState alarm_state;                  // whether off, armed or ringing

// button labels
static char cd_lbl[] = "Count down";
static char lap_lbl[] = "Lap";
static char reset_lbl[] = "Reset";
static char resume_lbl[] = "Resume";
static char run_lbl[] = "Run";
static char stop_lbl[] = "Stop";
static char exit_lbl[] = "Exit";
static char bigclock_lbl[] = "Big Clock";

// stopwatch controls
static SBox countdown_lbl_b = {SW_CD_X, SW_CD_Y, SW_CD_W, SW_BH};
static SBox cdtime_dsp_b = {SW_CDP_X, SW_CD_Y, SW_CDP_W, SW_BH};
static SBox cdtime_up_b = {SW_CDP_X, SW_CD_Y-SW_BH/2, SW_CDP_W, SW_BH};
static SBox cdtime_dw_b = {SW_CDP_X, SW_CD_Y+SW_BH/2, SW_CDP_W, SW_BH};
static SBox A_b = {SW_BAX, SW_BY, SW_BW, SW_BH};
static SBox B_b = {SW_BBX, SW_BY, SW_BW, SW_BH};
static SBox exit_b = {SW_EXITX, SW_EXITY, SW_BW, SW_BH};
static SBox bigclock_b = {SW_BCX, SW_BCY, SW_BW, SW_BH};
static SBox color_b = {SW_CX, SW_CY, SW_CW, SW_CH};
static uint8_t sw_hue;                          // hue 0..255
static uint16_t sw_col;                         // color pixel

// big clock info
static SBox bcdate_b = {BAC_DATEX, BAC_DATEY, BAC_DATEW, BAC_DATEH};
static SBox bcwx_b = {BAC_WXX, BAC_WXY, BAC_WXW, BAC_WXH};              // weather
static SBox bccd_b = {BC_CDP_X, BC_CDP_Y, BC_CDP_W, BC_CDP_H};          // countdown remaining and control
static SBox bcalarm_b = {BC_ALM_X, BC_ALM_Y, BC_ALM_W, BC_ALM_H};       // alarm time and control
static uint16_t bc_bits;                        // see SWBCBits
static uint32_t bc_prev_wx;                     // time of prev drawn wx, millis
static uint32_t bc_wxdt = BAC_WXGDT;            // weather update interval, millis

// alarm clock controls on main sw page
static SBox alarm_lbl_b = {ALM_X0, ALM_Y0, ALM_W, SW_BH};
static SBox alarm_hrmn_b = {ALM_EX, ALM_EY, ALM_EW, SW_BH};
static SBox alarm_up_b = {ALM_EX, ALM_EY-SW_BH/2, ALM_EW, SW_BH};
static SBox alarm_dw_b = {ALM_EX, ALM_EY+SW_BH/2, ALM_EW, SW_BH};


/* save persistent state and log
 */
static void saveSWNV()
{
    NVWriteUInt16 (NV_BCFLAGS, bc_bits);
    NVWriteUInt32 (NV_CD_PERIOD, countdown_period);

    uint16_t acode = alarm_hrmn;
    if (alarm_state != ALMS_OFF)
        acode += ALM_TOVFLOW;
    NVWriteUInt16 (NV_ALARMCLOCK, acode);
}

/* return ms countdown time remaining, if any
 */
static uint32_t getCountdownLeft()
{
    if (sws_engine == SWE_COUNTDOWN) {
        uint32_t since_start = millis() - start_t;
        if (since_start < countdown_period)
            return (countdown_period - since_start);
    }
    return (0);
}


/* set sw_col from sw_hue
 */
static void setSWColor()
{
    sw_col = HSV565 (sw_hue, SW_HSV_S, SW_HSV_V);
}

/* draw the current countdown_period if currently on the main SW page
 */
static void drawSWCDPeriod()
{
    if (sws_display == SWD_MAIN) {
        char buf[20];
        int mins = countdown_period/60000;
        snprintf (buf, sizeof(buf), "%d %s", mins, mins > 1 ? "mins" : "min");
        drawStringInBox (buf, cdtime_dsp_b, false, sw_col);
    }
}

/* draw the color control box
 */
static void drawColorScale()
{
    // erase to remove tick marks
    fillSBox (color_b, RA8875_BLACK);

    // rainbow
    for (uint16_t dx = 0; dx < color_b.w; dx++) {
        uint16_t c = HSV565 (255*dx/color_b.w, SW_HSV_S, SW_HSV_V);
        tft.drawPixel (color_b.x + dx, color_b.y + color_b.h/2, c);
    }

    // mark it
    uint16_t hue_x = color_b.x + sw_hue*color_b.w/255;
    tft.drawLine (hue_x, color_b.y+3*color_b.h/8, hue_x, color_b.y+5*color_b.h/8, RA8875_WHITE);
}

/* draw the given digit in the given bounding box with the given line thickness.
 */
static void drawDigit (const SBox &b, int digit, uint16_t lt)
{
    // insure all dimensions are even so two halves make a whole
    uint16_t t = (lt+1) & ~1U;
    uint16_t w = (b.w+1) & ~1U;
    uint16_t h = (b.h+1) & ~1U;
    uint16_t to2 = t/2;
    uint16_t wo2 = w/2;
    uint16_t ho2 = h/2;

    // erase 
    tft.fillRect (b.x, b.y, w, h, SW_BG);

    // draw digit -- replace with drawRect to check boundaries
    switch (digit) {
    case 0:
        tft.fillRect (b.x, b.y, w, t, sw_col);
        tft.fillRect (b.x, b.y+t, t, h-2*t, sw_col);
        tft.fillRect (b.x, b.y+h-t, w, t, sw_col);
        tft.fillRect (b.x+w-t, b.y+t, t, h-2*t, sw_col);
        break;
    case 1:
        tft.fillRect (b.x+wo2-to2, b.y, t, h, sw_col);      // center column? a little right?
        break;
    case 2:
        tft.fillRect (b.x, b.y, w, t, sw_col);
        tft.fillRect (b.x+w-t, b.y+t, t, ho2-t-to2, sw_col);
        tft.fillRect (b.x, b.y+ho2-to2, w, t, sw_col);
        tft.fillRect (b.x, b.y+ho2+to2, t, ho2-t-to2, sw_col);
        tft.fillRect (b.x, b.y+h-t, w, t, sw_col);
        break;
    case 3:
        tft.fillRect (b.x, b.y, w, t, sw_col);
        tft.fillRect (b.x+t, b.y+ho2-to2, w-t, t, sw_col);
        tft.fillRect (b.x, b.y+h-t, w, t, sw_col);
        tft.fillRect (b.x+w-t, b.y+t, t, h-2*t, sw_col);
        break;
    case 4:
        tft.fillRect (b.x, b.y, t, ho2+to2, sw_col);
        tft.fillRect (b.x+t, b.y+ho2-to2, w-2*t, t, sw_col);
        tft.fillRect (b.x+w-t, b.y, t, h, sw_col);
        break;
    case 5:
        tft.fillRect (b.x, b.y, w, t, sw_col);
        tft.fillRect (b.x, b.y+t, t, ho2-t-to2, sw_col);
        tft.fillRect (b.x, b.y+ho2-to2, w, t, sw_col);
        tft.fillRect (b.x+w-t, b.y+ho2+to2, t, ho2-t-to2, sw_col);
        tft.fillRect (b.x, b.y+h-t, w, t, sw_col);
        break;
    case 6:
        tft.fillRect (b.x, b.y, t, h, sw_col);
        tft.fillRect (b.x+t, b.y, w-t, t, sw_col);
        tft.fillRect (b.x+t, b.y+ho2-to2, w-t, t, sw_col);
        tft.fillRect (b.x+w-t, b.y+ho2+to2, t, ho2-to2-t, sw_col);
        tft.fillRect (b.x+t, b.y+h-t, w-t, t, sw_col);
        break;
    case 7:
        tft.fillRect (b.x, b.y, w, t, sw_col);
        tft.fillRect (b.x+w-t, b.y+t, t, h-t, sw_col);
        break;
    case 8:
        tft.fillRect (b.x, b.y, t, h, sw_col);
        tft.fillRect (b.x+w-t, b.y, t, h, sw_col);
        tft.fillRect (b.x+t, b.y, w-2*t, t, sw_col);
        tft.fillRect (b.x+t, b.y+ho2-to2, w-2*t, t, sw_col);
        tft.fillRect (b.x+t, b.y+h-t, w-2*t, t, sw_col);
        break;
    case 9:
        tft.fillRect (b.x, b.y, t, ho2+to2, sw_col);
        tft.fillRect (b.x+w-t, b.y, t, h, sw_col);
        tft.fillRect (b.x+t, b.y, w-2*t, t, sw_col);
        tft.fillRect (b.x+t, b.y+ho2-to2, w-2*t, t, sw_col);
        break;
    default:
        fatalError(_FX("drawDigit %d"), digit);
        break;
    }
}


/* draw the given stopwatch digit in the given position 0 .. SW_NDIG-1.
 * use swdigits[] to avoid erasing/redrawing the same digit again.
 */
static void drawSWDigit (uint8_t position, uint8_t digit)
{
    // check for no change
    if (swdigits[position] == digit)
        return;
    swdigits[position] = digit;

    // bounding box
    SBox b;
    b.x = SW_X0 + (SW_DW+SW_DGAP)*position;
    b.y = SW_Y0;
    b.w = SW_DW;
    b.h = SW_DH;

    // draw
    drawDigit (b, digit, SW_LT);
}

/* display the given time value in millis()
 */
static void drawSWTime(uint32_t t)
{
    int ndig = 0;

    t %= (100UL*60UL*60UL*1000UL);                        // msec in SW_NDIG digits

    uint8_t tenhr = t / (10UL*3600UL*1000UL);
    t -= tenhr * (10UL*3600UL*1000UL);
    drawSWDigit (0, tenhr);
    ndig++;

    uint8_t onehr = t / (3600UL*1000UL);
    t -= onehr * (3600UL*1000UL);
    drawSWDigit (1, onehr);
    ndig++;

    uint8_t tenmn = t / (600UL*1000UL);
    t -= tenmn * (600UL*1000UL);
    drawSWDigit (2, tenmn);
    ndig++;

    uint8_t onemn = t / (60UL*1000UL);
    t -= onemn * (60UL*1000UL);
    drawSWDigit (3, onemn);
    ndig++;

    uint8_t tensec = t / (10UL*1000UL);
    t -= tensec * (10UL*1000UL);
    drawSWDigit (4, tensec);
    ndig++;

    uint8_t onesec = t / (1UL*1000UL);
    t -= onesec * (1UL*1000UL);
    drawSWDigit (5, onesec);
    ndig++;

    uint8_t tenthsec = t / (100UL);
    t -= tenthsec * (100UL);
    drawSWDigit (6, tenthsec);
    ndig++;

    uint8_t hundsec = t / (10UL);
    t -= hundsec * (10UL);
    drawSWDigit (7, hundsec);
    ndig++;

    if (ndig != SW_ND)
        fatalError (_FX("stopwatch %d != %d"), ndig, SW_ND);
}


/* given countdown time remaining, find range and button text color
 */
static void determineCDVisuals (uint32_t ms_left, SWCDState &cds, uint16_t &color)
{
    // all good if beyond the warning time
    if (ms_left >= SW_CD_WARNDT) {
        cds = SWCDS_RUNOK;
        color = RA8875_GREEN;
        return;
    }

    // flash the GUI color but just rely on threadBlinker to handle the LED
    bool flash_on = (millis()%500) < 250;               // flip at 2 Hz
    if (ms_left > 0) {
        color = flash_on ? DYELLOW : RA8875_BLACK;
        cds = SWCDS_WARN;
    } else {
        color = flash_on ? RA8875_RED : RA8875_BLACK;
        cds = SWCDS_TIMEOUT;
    }
}

/* draw the satellite indicator, if want
 */
static void drawSatIndicator(bool force)
{
    // unused for now
    (void) force;

    // get sat info if want and defined
    SatNow sn;
    if (!(bc_bits & SW_BCSATBIT) || !getSatNow(sn))
        return;

    // prep for drawStringInBox
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    SBox sat_b;
    sat_b.x = BC_SAT_X;
    sat_b.y = BC_SAT_Y;
    sat_b.w = BC_SAT_W;
    sat_b.h = BC_SAT_H;

    if (sn.raz == SAT_NOAZ)

        drawStringInBox (_FX("No rise"), sat_b, false, sw_col);

    else if (sn.saz == SAT_NOAZ)

        drawStringInBox (_FX("No set"), sat_b, false, sw_col);

    else {

        // draw circumstances

        // decide whether up or down
        float dt;                               // hours to begin with
        const char *prompt;
        if (sn.rdt < 0 || sn.rdt > sn.sdt) {
            // up now, show time to set
            dt = sn.sdt;
            prompt = "sets in";
        } else {
            // down now, show time to rise
            dt = sn.rdt;
            prompt = "rises in";
        }

        // format time
        int a, b;
        char sep;
        formatSexa (dt, a, sep, b);

        // draw
        char buf[50];
        snprintf (buf, sizeof(buf), "%s %s %d%c%02d", sn.name, prompt, a, sep, b);
        drawStringInBox (buf, sat_b, false, sw_col);
    }
}

/* draw alarm_hrmn, pin and label if requested in various ways depending on sws_display
 */
static void drawAlarmIndicator (bool label_too)
{
    // pin
    setAlarmPin (alarm_state == ALMS_RINGING);

    // prep
    char buf[30];
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    uint16_t a_hr = alarm_hrmn/60;
    uint16_t a_mn = alarm_hrmn%60;

    if (sws_display == SWD_MAIN) {
        snprintf (buf, sizeof(buf), "DE  %02d:%02d", a_hr, a_mn);       // coord with up/down touch
        drawStringInBox (buf, alarm_hrmn_b, false, sw_col);
        if (label_too) {
            const char *lbl = "?";
            switch (alarm_state) {
            case ALMS_OFF:     lbl = "Alarm off";   break;
            case ALMS_ARMED:   lbl = "Alarm armed"; break;
            case ALMS_RINGING: lbl = "Alarm!";      break;
            }
            drawStringInBox (lbl, alarm_lbl_b, alarm_state == ALMS_RINGING, sw_col);
        }
    } else if (sws_display == SWD_BCDIGITAL || sws_display == SWD_BCANALOG) {
        if (alarm_state == ALMS_OFF) {
            if (label_too) {
                // this is so web command set_alarm?off can actually erase the alarm box
                fillSBox (bcalarm_b, RA8875_BLACK);
            }
        } else if (alarm_state == ALMS_ARMED) {
            snprintf (buf, sizeof(buf), "A: %02d:%02d", a_hr, a_mn);
            drawStringInBox (buf, bcalarm_b, false, sw_col);
        } else if (alarm_state == ALMS_RINGING) {
            drawStringInBox ("Alarm!", bcalarm_b, true, sw_col);
        }
    }
}

/* return whether alarm has gone off since previous call.
 * N.B. we assume this will be called more than once per minute
 */
static bool checkAlarm()
{
    // get de time hrmn
    time_t de_lt0 = nowWO() + de_tz.tz_secs;
    uint16_t de_hrmn = hour(de_lt0)*60U + minute(de_lt0);

    // wentoff unless still in same minute
    static uint16_t prev_de_hrmn = 24*60;       // init to impossible hrmn
    bool wentoff = de_hrmn == alarm_hrmn && de_hrmn != prev_de_hrmn;
    prev_de_hrmn = de_hrmn;

    return (wentoff);
}

/* draw remaining count down time and manage the state of the count down button and LED.
 * N.B. we handle all display states but assume sws_engine == SWE_COUNTDOWN 
 */
static void drawCDTimeRemaining(bool force)
{
    // sanity check: this function is only for countdown
    if (sws_engine != SWE_COUNTDOWN)
        return;

    // not crazy fast unless force
    static uint32_t gate;
    if (!force && !timesUp (&gate, 31))
        return;

    // get ms remaining 
    uint32_t ms_left = getCountdownLeft();

    // determine range and color
    SWCDState cds;
    uint16_t color;
    determineCDVisuals (ms_left, cds, color);

    // set LEDS
    setCDLEDState (cds);

    if (sws_display == SWD_MAIN) {

        // showing main stopwatch page at full ms resolution

        // show time using the 7-seg displays
        drawSWTime(ms_left);

        // determine whether to display inverted
        static bool prev_inv;
        bool inv = cds == SWCDS_RUNOK || cds == SWCDS_WARN || cds == SWCDS_TIMEOUT;

        // update the countdown button if different or force
        if (force || inv != prev_inv) {
            drawStringInBox (cd_lbl, countdown_lbl_b, inv, sw_col);
            prev_inv = inv;
        }

    } else {

        // the other display states share a common time format so build that first

        // break into H:M:S
        ms_left += 500U;                         // round to nearest second
        uint8_t hr = ms_left / 3600000U;
        ms_left -= hr * 3600000U;
        uint8_t mn = ms_left / 60000U;
        ms_left -= mn * 60000U;
        uint8_t sc = ms_left/1000U;

        // avoid repeating the same time and color
        static uint8_t prev_sc;
        static uint16_t prev_color;
        if (color == prev_color && sc == prev_sc && !force)
            return;

        // format
        char buf[32];
        if (hr == 0)
            snprintf (buf, sizeof(buf), "%d:%02d", mn, sc);
        else
            snprintf (buf, sizeof(buf), "%dh%02d", hr, mn);

        if (sws_display == SWD_NONE) {

            // main Hamclock page

            // overwrite stopwatch icon
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            uint16_t cdw = getTextWidth(buf);
            fillSBox (stopwatch_b, RA8875_BLACK);
            tft.setTextColor (color);
            tft.setCursor (stopwatch_b.x + (stopwatch_b.w-cdw)/2, stopwatch_b.y+stopwatch_b.h/4);
            tft.print (buf);

            // draw pane if showing
            PlotPane cdp = findPaneChoiceNow(PLOT_CH_COUNTDOWN);
            if (cdp != PANE_NONE) {

                // find box
                SBox box = plot_b[cdp];

                // prep if force
                if (force) {
                    prepPlotBox (box);

                    // title
                    static const char title[] = "Countdown timer";
                    selectFontStyle (BOLD_FONT, FAST_FONT);
                    uint16_t w = getTextWidth(title);
                    tft.setCursor (box.x + (box.w - w)/2, box.y + 3);
                    tft.setTextColor (RA8875_GREEN);
                    tft.print (title);
                }

                // time remaining, don't blink
                static uint16_t prev_pane_color;
                uint16_t pane_color = color == RA8875_BLACK ? prev_pane_color : color;
                if (force || sc != prev_sc || pane_color != prev_pane_color) {
                    selectFontStyle (BOLD_FONT, LARGE_FONT);
                    uint16_t w = getTextWidth(buf);
                    tft.fillRect (box.x+10, box.y+box.h/3, box.w-20, box.h/3, RA8875_BLACK);
                    tft.setCursor (box.x + (box.w - w)/2, box.y + 2*box.h/3 - 5);
                    tft.setTextColor (pane_color);
                    tft.print(buf);
                    prev_pane_color = pane_color;
                }
            }

        } else if (sws_display == SWD_BCDIGITAL || sws_display == SWD_BCANALOG) {

            selectFontStyle (BOLD_FONT, SMALL_FONT);
            drawStringInBox (buf, bccd_b, false, color);

        }

        // remember
        prev_sc = sc;
        prev_color = color;
    }
}




/* draw either BigClock state awareness message as needed
 */
static void drawBCAwareness (bool force)
{
    // whether time was ok last iteration
    static bool time_was_ok;

    // get current state
    bool clock_ok = clockTimeOk();
    bool ut_zero = utcOffset() == 0;
    bool time_ok_now = clock_ok && ut_zero;

    // update if force or new state
    if (time_ok_now) {
        if (force || !time_was_ok) {
            // erase
            tft.fillRect (BC_BAD_X, BC_BAD_Y, BC_BAD_W, BC_BAD_H, RA8875_BLACK);
            Serial.print (F("SW: time ok now\n"));
        }
    } else {
        if (force || time_was_ok) {
            selectFontStyle (BOLD_FONT, SMALL_FONT);
            tft.setCursor (BC_BAD_X, BC_BAD_Y+27);
            tft.setTextColor (RA8875_RED);
            if (clock_ok) {
                const char *msg = _FX("Time is offset");
                tft.print (msg);
                Serial.printf (_FX("SW: %s\n"), msg);
            } else {
                const char *msg = _FX("Time is unknown");
                tft.print (msg);
                Serial.printf (_FX("SW: %s\n"), msg);
            }
        }
    }

    // persist
    time_was_ok = time_ok_now;
}


/* refresh detailed date info in bcdate_b.
 * N.B. we never erase because Wednesday overlays clock
 */
static void drawBCDateInfo (int hr, int dy, int wd, int mo)
{
    // day
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    tft.setTextColor (BAC_FCOL);
    tft.setCursor (bcdate_b.x, bcdate_b.y + 50);
    tft.print (dayStr(wd));

    // month
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setCursor (bcdate_b.x, bcdate_b.y + 90);
    if (getDateFormat() == DF_MDY || getDateFormat() == DF_YMD)
        tft.printf (_FX("%s %d"), monthStr(mo), dy);
    else
        tft.printf (_FX("%d %s"), dy, monthStr(mo));

    // AM/PM/UTC
    tft.setCursor (bcdate_b.x, bcdate_b.y + 125);
    if (sws_display == SWD_BCANALOG || (bc_bits & (SW_DB12HBIT|SW_LSTBIT|SW_UTCBIT)) == SW_DB12HBIT) {
        // AM/PM always for analog or 12 hour digital
        tft.print (hr < 12 ? "AM" : "PM");
    } else if (sws_display == SWD_BCDIGITAL && (bc_bits & SW_UTCBIT)) {
        // UTC
        tft.print ("UTC");
    } else if (sws_display == SWD_BCDIGITAL && (bc_bits & SW_LSTBIT)) {
        // LST
        tft.print ("LST");
    } else if (sws_display == SWD_BCDIGITAL && !(bc_bits & SW_UTCBIT)) {
        // UTC + TZ
        tft.printf ("UTC%+g", de_tz.tz_secs/3600.0F);
    }
}

/* refresh DE weather in bcwx_b, return whether successful
 */
static bool drawBCDEWxInfo(void)
{
    WXInfo wi;
    char ynot[100];
    bool ok = getCurrentWX (de_ll, true, &wi, ynot);
    if (ok)
        plotWX (bcwx_b, BAC_FCOL, wi);
    else
        plotMessage (bcwx_b, RA8875_RED, ynot);

    // undo border
    drawSBox (bcwx_b, RA8875_BLACK);

    return (ok);
}

/* draw space weather in upper right.
 */
static void drawBCSpaceWxInfo (bool all)
{
    if (checkSpaceStats() || all)
        drawSpaceStats(sw_col);   
}

/* mark each control or indicator box for debugging big clock layout
 */
static void drawBCShowAll()
{
    #if defined(_SHOW_ALL)
        drawSBox (bccd_b, RA8875_RED);
        drawSBox (bcalarm_b, RA8875_RED);
        drawSBox (bcdate_b, RA8875_RED);
        tft.drawRect (BC_BAD_X, BC_BAD_Y, BC_BAD_W, BC_BAD_H, RA8875_RED);
        tft.drawRect (BC_SAT_X, BC_SAT_Y, BC_SAT_W, BC_SAT_H, RA8875_RED);
    #endif
}

/* draw the digital Big Clock 
 */
static void drawDigitalBigClock (bool all)
{
    // debug
    drawBCShowAll();

    // persist to avoid drawing the same digits again
    static time_t prev_am, prev_t0;                             // previous am/pm and report time
    static uint8_t prev_mnten, prev_mnunit;                     // previous mins tens and unit
    static uint8_t prev_scten;                                  // previous seconds tens and unit
    static uint8_t prev_hr, prev_mo, prev_dy;                   // previous drawn date info
    static bool prev_leadhr;                                    // previous whether leading hours tens digit

    // get UTC time now, including any user offset
    time_t t0 = nowWO();

    // done if same second unless all
    if (!all && t0 == prev_t0)
        return;
    prev_t0 = t0;

    // time components
    int hr, mn, sc, mo, dy, wd;

    // break out hr, mn and sec as utc, local or lst
    if (bc_bits & SW_LSTBIT) {
        double lst;                                             // hours
        double astro_mjd = t0/86400.0 + 2440587.5 - 2415020.0;  // just for now_lst()
        now_lst (astro_mjd, de_ll.lng, &lst);
        hr = lst;
        lst = (lst - hr)*60;                                    // now mins
        mn = lst;
        lst = (lst - mn)*60;                                    // now secs
        sc = lst;
        t0 += de_tz.tz_secs;                                    // now local
    } else {
        if (!(bc_bits & SW_UTCBIT))
            t0 += de_tz.tz_secs;                                // now local
        hr = hour(t0);
        mn = minute(t0);
        sc = second(t0);
    }

    // other components always from t0
    mo = month(t0);
    dy = day(t0);
    wd = weekday(t0);
    bool am = hr < 12;

    // decadal ranges to optimize drawing
    int hrten = hr/10;
    int hrunit = hr%10;
    int mnunit = mn%10;

    // handy
    bool want_secs = !(bc_bits & SW_NOSECBIT);

    // initial box for drawDigit, walk right for each position
    SBox b;
    b.x = BDC_HMX0;
    b.y = BDC_HMY0;
    b.w = BDC_HMW;
    b.h = BDC_HMH;

    // adjust x to center if no leading hour digit in 12 hour mode
    bool leadhr = true;
    if ((bc_bits & (SW_DB12HBIT|SW_UTCBIT|SW_LSTBIT)) == SW_DB12HBIT) {
        // 12 hours don't show leading 0
        int hr12 = hr%12;
        if (hr12 == 0)
            hr12 = 12;
        hrunit = hr12%10;
        leadhr = hr12 >= 10;
        if (leadhr) {
            hrten = hr12/10;
        } else {
            hrten = -1;                         // flag to not show
            b.x += BDC_HMW/2;                   // center with 1 fewer digits
        }
    }

    // also adjust x if showing seconds
    if (want_secs)
        b.x -= (BDC_HMGAP + (2*BDC_HMW + BDC_HMGAP)/BCD_SSZRED)/2;     // see below

    // restart if changing whether we have a leading hour digit
    if (leadhr != prev_leadhr)
        all = true;
    prev_leadhr = leadhr;

    // initial erase or showing date and it's a new day
    if (all || ((bc_bits & SW_BCDATEBIT) && (am != prev_am || dy != prev_dy || mo != prev_mo))) {
        eraseScreen();
        all = true;     // insure everything gets redrawn
        if (bc_bits & SW_BCDATEBIT)
            drawBCDateInfo (hr, dy, wd, mo);
        prev_am = am;
        prev_dy = dy;
        prev_mo = mo;
    }

    // update hour every hour
    if (all || hr != prev_hr) {
        if (hrten >= 0) {
            drawDigit (b, hrten, BDC_HMLT);
            b.x += BDC_HMW + BDC_HMGAP;
        }
        drawDigit (b, hrunit, BDC_HMLT);
        b.x += BDC_HMW;
        prev_hr = hr;
    } else {
        if (hrten >= 0)
            b.x += BDC_HMW + BDC_HMGAP;
        b.x += BDC_HMW;
    }


    // blink unless showing seconds
    if (want_secs) {
        if (all) {
            tft.fillCircle (b.x+BDC_HMCW/2, b.y + BDC_HMH/3,   BDC_HMCR, sw_col);
            tft.fillCircle (b.x+BDC_HMCW/2, b.y + 2*BDC_HMH/3, BDC_HMCR, sw_col);
        }
    } else {
        uint16_t colon_color = all || (t0&1) ? sw_col : SW_BG;
        tft.fillCircle (b.x+BDC_HMCW/2, b.y + BDC_HMH/3,   BDC_HMCR, colon_color);
        tft.fillCircle (b.x+BDC_HMCW/2, b.y + 2*BDC_HMH/3, BDC_HMCR, colon_color);
    }
    b.x += BDC_HMCW;


    // update minutes every minute
    if (all || mnunit != prev_mnunit) {

        // draw tens of minutes if changed
        uint8_t mnten = mn/10;
        if (all || mnten != prev_mnten) {
            drawDigit (b, mnten, BDC_HMLT);
            prev_mnten = mnten;
        }
        b.x += BDC_HMW + BDC_HMGAP;

        // unit minute for sure
        drawDigit (b, mnunit, BDC_HMLT);
        b.x += BDC_HMW;
        prev_mnunit = mnunit;
    } else 
        b.x += BDC_HMW + BDC_HMGAP + BDC_HMW;
        
    // add seconds as superscript if wanted
    if (want_secs) {

        // seconds are shown reduced size
        b.x += BDC_HMGAP;
        b.w /= BCD_SSZRED;
        b.h /= BCD_SSZRED;

        // draw tens of seconds if changed
        uint8_t scten = sc/10;
        if (all || scten != prev_scten) {
            drawDigit (b, scten, BDC_HMLT/BCD_SSZRED);
            prev_scten = scten;
        }
        b.x += b.w + BDC_HMGAP/BCD_SSZRED;

        // unit seconds for sure
        drawDigit (b, sc%10, BDC_HMLT/BCD_SSZRED);
    }

    // update awareness
    drawBCAwareness (all);
    drawSatIndicator(all);

    // init countdown if first call
    if (all) {
        drawCDTimeRemaining(true);
        drawAlarmIndicator(false);
    }

    // update DE weather if desired and all or new
    if ((bc_bits & SW_BCWXBIT) && (timesUp(&bc_prev_wx, bc_wxdt) || all))
        bc_wxdt = drawBCDEWxInfo() ? BAC_WXGDT : BAC_WXFDT;

    // update space weather if desired
    if (bc_bits & SW_BCSPWXBIT)
        drawBCSpaceWxInfo(all);
}

/* draw the digital time portion of the analog clock.
 * hr is local 24 hour.
 */
static void drawAnalogDigital (bool all, int hr, int mn, int sc)
{
    // persist state
    static uint8_t prev_hr, prev_mn, prev_sc;           // previous time
    static uint16_t prev_stx, prev_sux;                 // previous seconds tens and unit x

    // options
    bool want_seconds = !(bc_bits & SW_NOSECBIT);
    uint16_t x0 = want_seconds ? BACD_HMSX0 : BACD_HMX0;

    // prep
    selectFontStyle (LIGHT_FONT, LARGE_FONT);
    tft.setTextColor (sw_col);

    // draw hr and mn
    if (all || hr != prev_hr || mn != prev_mn) {

        tft.fillRect (x0, BACD_DY-45, 140, 50, RA8875_BLACK);
        #if defined(_SHOW_ALL)
            tft.drawRect (x0, BACD_DY-45, 140, 50, RA8875_RED);
        #endif

        // convert to 12 hours, don't print leading zero
        int hr12 = hr%12;
        if (hr12 == 0)
            hr12 = 12;
        if (hr12 >= 10)
            tft.setCursor (x0, BACD_DY);
        else
            tft.setCursor (x0+15, BACD_DY);
        tft.printf ("%d:%02d", hr12, mn);

        if (want_seconds)
            tft.print (':');
        prev_stx = tft.getCursorX();
        prev_hr = hr;
        prev_mn = mn;
    }

    // sc if enabled
    if (want_seconds && (all || sc != prev_sc)) {
        bool new_tens = (sc/10) != (prev_sc/10);
        if (all || new_tens) {
            tft.fillRect (prev_stx, BACD_DY-45, 60, 50, RA8875_BLACK);
            #if defined(_SHOW_ALL)
                tft.drawRect (prev_stx, BACD_DY-45, 60, 50, RA8875_RED);
            #endif
            tft.setCursor (prev_stx, BACD_DY);
            tft.print (sc/10);
            prev_sux = tft.getCursorX();
        } else {
            tft.fillRect (prev_sux, BACD_DY-45, 30, 50, RA8875_BLACK);
            #if defined(_SHOW_ALL)
                tft.drawRect (prev_sux, BACD_DY-45, 30, 50, RA8875_RED);
            #endif
        }

        // always draw unit digit
        tft.setCursor (prev_sux, BACD_DY);
        tft.print (sc%10);
        prev_sc = sc;
    }
}


/* draw analog Big Clock
 */
static void drawAnalogBigClock (bool all)
{
    // debug
    drawBCShowAll();

    // persistent time measures
    static time_t prev_am, prev_lt0;                    // detect change of am/pm and secs
    static uint8_t prev_mo, prev_dy;                    // previously drawn date info

    // previous hand positions for motion detection and exact erasing
    // hand point 0 is the face center; 1 and 2 are the fat positions part way out; 3 is the far tip.
    static int16_t prev_hrdx1, prev_hrdx2, prev_hrdx3, prev_hrdy1, prev_hrdy2, prev_hrdy3;
    static int16_t prev_mndx1, prev_mndx2, prev_mndx3, prev_mndy1, prev_mndy2, prev_mndy3;
    static int16_t prev_scdx3, prev_scdy3;

    // get local time now, including any user offset
    time_t lt0 = nowWO() + de_tz.tz_secs;

    // wait for second to change unless all
    if (!all && lt0 == prev_lt0)
        return;
    prev_lt0 = lt0;

    // handy
    bool want_numbers = (bc_bits & SW_ANNUMBIT) != 0;
    bool want_seconds = !(bc_bits & SW_NOSECBIT);
    bool color_hands = (bc_bits & SW_ANCOLHBIT) != 0;

    // crack open
    int hr = hour(lt0);
    int mn = minute(lt0);
    int sc = second(lt0);
    int dy = day(lt0);
    int mo = month(lt0);
    bool am = hr < 12;

    // face geometry, smaller if showing sat or digital too
    bool add_digital = (bc_bits & SW_ANWDBIT) != 0;
    bool shrink = (bc_bits & SW_BCSATBIT) != 0 || add_digital;
    int bac_y0    = shrink ? BACD_Y0   : BAC_Y0;
    int bac_mnr   = shrink ? BACD_MNR  : BAC_MNR;
    int bac_scr   = shrink ? BACD_SCR  : BAC_SCR;
    int bac_hrr   = shrink ? BACD_HRR  : BAC_HRR;
    int bac_fr    = shrink ? BACD_FR   : BAC_FR;
    int bac_bezr  = shrink ? BACD_BEZR : BAC_BEZR;
    int bac_htr   = shrink ? BACD_HTR  : BAC_HTR;
    int bac_mtr   = shrink ? BACD_MTR  : BAC_MTR;

    // refresh if desired or new date (since we never erase the date)
    if (all || ((bc_bits & SW_BCDATEBIT) && (am != prev_am || dy != prev_dy || mo != prev_mo))) {

        // fresh face
        eraseScreen();
        all = true;     // insure everything gets redrawn

        // face perimeter
      #if defined (_IS_ESP8266)
        // avoids bright flash of circle filling but doesn't fill at higher display sizes
        for (uint16_t r = bac_fr+1; r <= bac_bezr; r++)
            tft.drawCircle (BAC_X0, bac_y0, r, BAC_BEZCOL);
      #else
        tft.fillCircle (BAC_X0, bac_y0, bac_bezr, BAC_BEZCOL);
        tft.fillCircle (BAC_X0, bac_y0, bac_fr, RA8875_BLACK);
      #endif
        tft.drawCircle (BAC_X0, bac_y0, bac_fr, BAC_FCOL);

        // hour points
        for (int i = 0; i < 12; i++) {
            float a = deg2rad(360.0F*i/12.0F);
            SCoord hpt[3];
            hpt[0].x = roundf(BAC_X0 + bac_fr * cosf(a-BAC_HTTH));
            hpt[0].y = roundf(bac_y0 + bac_fr * sinf(a-BAC_HTTH));
            hpt[1].x = roundf(BAC_X0 + (bac_fr-bac_htr) * cosf(a));
            hpt[1].y = roundf(bac_y0 + (bac_fr-bac_htr) * sinf(a));
            hpt[2].x = roundf(BAC_X0 + bac_fr * cosf(a+BAC_HTTH));
            hpt[2].y = roundf(bac_y0 + bac_fr * sinf(a+BAC_HTTH));
            if (color_hands)
                tft.fillPolygon (hpt, NARRAY(hpt), BAC_FCOL);
            else
                tft.drawPolygon (hpt, NARRAY(hpt), BAC_FCOL);
        }

        // minute ticks
        for (int i = 0; i < 60; i++) {
            if ((i % 5) == 0)
                continue;                               // don't overdraw hour marks
            float a = deg2rad(360.0F*i/60.0F);
            uint16_t x0 = roundf(BAC_X0 + bac_fr * cosf(a));
            uint16_t y0 = roundf(bac_y0 + bac_fr * sinf(a));
            uint16_t x1 = roundf(BAC_X0 + (bac_fr-bac_mtr) * cosf(a));
            uint16_t y1 = roundf(bac_y0 + (bac_fr-bac_mtr) * sinf(a));
            tft.drawLine (x0, y0, x1, y1, 1, BAC_FCOL);
        }

        // init all locations to any bogus shape inside face for the initial erase
        prev_hrdx1 = 20;  prev_hrdy1 = 20;
        prev_hrdx2 = 0;   prev_hrdy2 = 40;
        prev_hrdx3 = -20; prev_hrdy3 = 20;
        prev_mndx1 = 20;  prev_mndy1 = 20;
        prev_mndx2 = 0;   prev_mndy2 = 40;
        prev_mndx3 = -20; prev_mndy3 = 20;
        prev_scdx3 = prev_scdy3 = 30;

        // date
        if ((bc_bits & SW_BCDATEBIT)) {
            drawBCDateInfo (hr, dy, weekday(lt0), mo);
            prev_am = am;
            prev_dy = dy;
            prev_mo = mo;
        }
    }

    // find central angle and far tip location of each hand
    float hr_angle = deg2rad(30*(3-(((hr+24)%12) + mn/60.0F)));
    float mn_angle = deg2rad(6*(15-(mn+sc/60.0F)));
    float sc_angle = deg2rad(6*(15-sc));
    int16_t hrdx3 = roundf(bac_hrr * cosf(hr_angle));
    int16_t hrdy3 = roundf(bac_hrr * sinf(hr_angle));
    int16_t mndx3 = roundf(bac_mnr * cosf(mn_angle));
    int16_t mndy3 = roundf(bac_mnr * sinf(mn_angle));
    int16_t scdx3 = roundf(bac_scr * cosf(sc_angle));
    int16_t scdy3 = roundf(bac_scr * sinf(sc_angle));

    // erase and update hand position if far tip moved
    bool hr_moved = hrdx3 != prev_hrdx3 || hrdy3 != prev_hrdy3;
    bool mn_moved = mndx3 != prev_mndx3 || mndy3 != prev_mndy3;
    bool sc_moved = scdx3 != prev_scdx3 || scdy3 != prev_scdy3;
    if (hr_moved) {
        SCoord hand[4];         // order avoids bug in ESP
        hand[0].x = BAC_X0+prev_hrdx1; hand[0].y = bac_y0-prev_hrdy1;
        hand[1].x = BAC_X0;            hand[1].y = bac_y0;
        hand[2].x = BAC_X0+prev_hrdx2; hand[2].y = bac_y0-prev_hrdy2;
        hand[3].x = BAC_X0+prev_hrdx3; hand[3].y = bac_y0-prev_hrdy3;
        if (color_hands)
            tft.fillPolygon (hand, NARRAY(hand), RA8875_BLACK);
        else
            tft.drawPolygon (hand, NARRAY(hand), RA8875_BLACK);

        prev_hrdx1 = roundf(bac_hrr/3.0F * cosf(hr_angle-BAC_HRTH));
        prev_hrdy1 = roundf(bac_hrr/3.0F * sinf(hr_angle-BAC_HRTH));
        prev_hrdx2 = roundf(bac_hrr/3.0F * cosf(hr_angle+BAC_HRTH));
        prev_hrdy2 = roundf(bac_hrr/3.0F * sinf(hr_angle+BAC_HRTH));
        prev_hrdx3 = hrdx3;
        prev_hrdy3 = hrdy3;
    }
    if (mn_moved) {
        SCoord hand[4];         // order avoids bug in ESP
        hand[0].x = BAC_X0+prev_mndx1; hand[0].y = bac_y0-prev_mndy1;
        hand[1].x = BAC_X0;            hand[1].y = bac_y0;
        hand[2].x = BAC_X0+prev_mndx2; hand[2].y = bac_y0-prev_mndy2;
        hand[3].x = BAC_X0+prev_mndx3; hand[3].y = bac_y0-prev_mndy3;
        if (color_hands)
            tft.fillPolygon (hand, NARRAY(hand), RA8875_BLACK);
        else
            tft.drawPolygon (hand, NARRAY(hand), RA8875_BLACK);

        prev_mndx1 = roundf(bac_mnr/3.0F * cosf(mn_angle-BAC_MNTH));
        prev_mndy1 = roundf(bac_mnr/3.0F * sinf(mn_angle-BAC_MNTH));
        prev_mndx2 = roundf(bac_mnr/3.0F * cosf(mn_angle+BAC_MNTH));
        prev_mndy2 = roundf(bac_mnr/3.0F * sinf(mn_angle+BAC_MNTH));
        prev_mndx3 = mndx3;
        prev_mndy3 = mndy3;
    }
    if (want_seconds && sc_moved) {
        tft.drawLine (BAC_X0, bac_y0, BAC_X0+prev_scdx3, bac_y0-prev_scdy3, 1, RA8875_BLACK);
        prev_scdx3 = scdx3;
        prev_scdy3 = scdy3;
    }

    // draw numbers if desired and all or if likely partially erased by min or sec hand
    if (want_numbers) {
        int16_t mntipx = BAC_X0+prev_mndx3;
        int16_t mntipy = bac_y0-prev_mndy3;
        int16_t sctipx = BAC_X0+prev_scdx3;
        int16_t sctipy = bac_y0-prev_scdy3;
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (sw_col);
        #define TIP_R 50                                                // redraw if this close to tip
        for (int i = 1; i <= 12; i++) {
            float theta = deg2rad(i*360/12);                            // CW from up
            int16_t x = BAC_X0 + bac_scr*sinf(theta) - (i<10?5:12);;    // wrt center of number
            int16_t y = bac_y0 - bac_scr*cosf(theta) + 9;
            if (all || (mn_moved && abs(x-mntipx)<TIP_R && abs(y-mntipy)<TIP_R)
                        || (want_seconds && sc_moved && abs(x-sctipx)<TIP_R && abs(y-sctipy)<TIP_R)) {
                tft.setCursor (x, y);
                tft.print(i);
            }
        }
    }

    // draw hand if moved or likely clobbered by another hand erasure
    float hr_sc_angle = fabsf(hr_angle - sc_angle);
    float hr_mn_angle = fabsf(hr_angle - mn_angle);
    float mn_sc_angle = fabsf(mn_angle - sc_angle);
    bool hrsc_hit = hr_sc_angle < 2*BAC_HRTH || hr_sc_angle > 2*M_PIF - 2*BAC_HRTH;
    bool hrmn_hit = hr_mn_angle < 2*BAC_HRTH || hr_mn_angle > 2*M_PIF - 2*BAC_HRTH;
    bool mnsc_hit = mn_sc_angle < 2*BAC_MNTH || mn_sc_angle > 2*M_PIF - 2*BAC_MNTH;
    if (want_numbers || hr_moved || hrsc_hit || hrmn_hit) {
        SCoord hand[4];         // order avoids bug in ESP
        hand[0].x = BAC_X0+prev_hrdx1; hand[0].y = bac_y0-prev_hrdy1;
        hand[1].x = BAC_X0;            hand[1].y = bac_y0;
        hand[2].x = BAC_X0+prev_hrdx2; hand[2].y = bac_y0-prev_hrdy2;
        hand[3].x = BAC_X0+prev_hrdx3; hand[3].y = bac_y0-prev_hrdy3;
        if (color_hands)
            tft.fillPolygon (hand, NARRAY(hand), BAC_HRCOL);
        else {
            tft.fillPolygon (hand, NARRAY(hand), RA8875_BLACK);
            tft.drawPolygon (hand, NARRAY(hand), BAC_HRCOL);
        }
    }
    if (want_numbers || mn_moved || hrmn_hit || mnsc_hit) {
        SCoord hand[4];         // order avoids bug in ESP
        hand[0].x = BAC_X0+prev_mndx1; hand[0].y = bac_y0-prev_mndy1;
        hand[1].x = BAC_X0;            hand[1].y = bac_y0;
        hand[2].x = BAC_X0+prev_mndx2; hand[2].y = bac_y0-prev_mndy2;
        hand[3].x = BAC_X0+prev_mndx3; hand[3].y = bac_y0-prev_mndy3;
        if (color_hands) {
            tft.fillPolygon (hand, NARRAY(hand), BAC_MNCOL);
            if (hrmn_hit)
                tft.drawPolygon (hand, NARRAY(hand), RA8875_BLACK);        // mn hand border if near hr hand
        } else {
            tft.fillPolygon (hand, NARRAY(hand), RA8875_BLACK);
            tft.drawPolygon (hand, NARRAY(hand), BAC_HRCOL);
        }
    }

    // draw second hand after numbers so it overlays
    if (want_seconds && (sc_moved || hrsc_hit || mnsc_hit))
        tft.drawLine (BAC_X0, bac_y0, BAC_X0+prev_scdx3, bac_y0-prev_scdy3, 1, BAC_SCCOL);

    // center dot
    tft.fillCircle (BAC_X0, bac_y0, BAC_DOTR, BAC_BEZCOL);

    // update awareness
    drawBCAwareness (all);
    drawSatIndicator(all);

    // init countdown if first call
    if (all) {
        drawCDTimeRemaining(true);
        drawAlarmIndicator(false);
    }

    // numeric time too if desired
    if (add_digital)
        drawAnalogDigital (all, hr, mn, sc);

    // update DE or space weather if desired and all or new
    if ((bc_bits & SW_BCWXBIT) && (timesUp(&bc_prev_wx, bc_wxdt) || all))
        bc_wxdt = drawBCDEWxInfo() ? BAC_WXGDT : BAC_WXFDT;

    // update space weather if desired
    if (bc_bits & SW_BCSPWXBIT)
        drawBCSpaceWxInfo(all);
}

/* update stopwatch in any possible display state
 */
static void drawSWState()
{
    switch (sws_display) {
    case SWD_MAIN:

        switch (sws_engine) {
        case SWE_RESET:
            drawSWTime(0);
            drawStringInBox (cd_lbl, countdown_lbl_b, false, sw_col);
            drawStringInBox (run_lbl, A_b, false, sw_col);
            drawStringInBox (reset_lbl, B_b, false, sw_col);
            drawSWCDPeriod();
            setCDLEDState (SWCDS_OFF);
            break;
        case SWE_RUN:
            drawStringInBox (cd_lbl, countdown_lbl_b, false, sw_col);
            drawStringInBox (stop_lbl, A_b, false, sw_col);
            drawStringInBox (lap_lbl, B_b, false, sw_col);
            drawSWCDPeriod();
            setCDLEDState (SWCDS_OFF);
            break;
        case SWE_STOP:
            drawSWTime(stop_dt);        // show stopped time
            drawStringInBox (cd_lbl, countdown_lbl_b, false, sw_col);
            drawStringInBox (run_lbl, A_b, false, sw_col);
            drawStringInBox (reset_lbl, B_b, false, sw_col);
            drawSWCDPeriod();
            setCDLEDState (SWCDS_OFF);
            break;
        case SWE_LAP:
            drawSWTime(stop_dt);        // show lap hold time
            drawStringInBox (cd_lbl, countdown_lbl_b, false, sw_col);
            drawStringInBox (reset_lbl, A_b, false, sw_col);
            drawStringInBox (resume_lbl, B_b, false, sw_col);
            drawSWCDPeriod();
            setCDLEDState (SWCDS_OFF);
            break;
        case SWE_COUNTDOWN:
            drawStringInBox (cd_lbl, countdown_lbl_b, true, sw_col);
            drawStringInBox (reset_lbl, A_b, false, sw_col);
            drawStringInBox (reset_lbl, B_b, false, sw_col);
            drawSWCDPeriod();
            drawCDTimeRemaining(true);
            break;
        }

        drawAlarmIndicator  (true);

        break;


    case SWD_BCDIGITAL:
        drawDigitalBigClock (true);
        break;

    case SWD_BCANALOG:
        drawAnalogBigClock (true);
        break;

    case SWD_NONE:
        drawMainPageStopwatch(true);
        break;
    }
}


/* draw the appropriate Big Clock
 */
static void drawBigClock (bool all)
{
    if (sws_display == SWD_BCDIGITAL)
        drawDigitalBigClock (all);
    else if (sws_display == SWD_BCANALOG)
        drawAnalogBigClock (all);
}


/* draw the main stopwatch page controls.
 * N.B. we do not erase screen, leave that to caller
 */
static void drawSWMainPage()
{
    // get last color, else set default
    if (!NVReadUInt8 (NV_SWHUE, &sw_hue)) {
        sw_hue = 85;    // green
        NVWriteUInt8 (NV_SWHUE, sw_hue);
    }
    setSWColor();

    // buttons
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    drawStringInBox (exit_lbl, exit_b, false, sw_col);
    drawStringInBox (bigclock_lbl, bigclock_b, false, sw_col);

    // state
    sws_display = SWD_MAIN;

    // init sw digits all illegal so they all get drawn first time
    memset (swdigits, 255, sizeof(swdigits));

    // draw punctuation
    tft.fillCircle (SW_X0 + 2*SW_DW + SW_DGAP + SW_DGAP/2,   SW_Y0 + SW_DH/3,   SW_PUNCR, sw_col);
    tft.fillCircle (SW_X0 + 2*SW_DW + SW_DGAP + SW_DGAP/2,   SW_Y0 + 2*SW_DH/3, SW_PUNCR, sw_col);
    tft.fillCircle (SW_X0 + 4*SW_DW + 3*SW_DGAP + SW_DGAP/2, SW_Y0 + SW_DH/3,   SW_PUNCR, sw_col);
    tft.fillCircle (SW_X0 + 4*SW_DW + 3*SW_DGAP + SW_DGAP/2, SW_Y0 + 2*SW_DH/3, SW_PUNCR, sw_col);
    tft.fillCircle (SW_X0 + 6*SW_DW + 5*SW_DGAP + SW_DGAP/2, SW_Y0 + SW_DH,     SW_PUNCR, sw_col);

    // draw buttons from state and color scale
    drawSWState();
    drawColorScale();
}

/* function just for waitForTap to detect whether external pin or web server command turned alarm off.
 */
static bool checkExternalTurnOff()
{
    return (alarmSwitchIsTrue() || alarm_state != ALMS_RINGING);
}

/* called to indicate the alarm has gone off.
 *   always set the alarm pin.
 *   if showing the main hamclock map, overwrite a pane with message, wait for dismiss, restore then return;
 *   if showing the main stopwatch page show alarm label active and return immediately;
 *   if showing a bigclock page show alarm time highlighted and return immediately.
 */
static void showAlarmRinging()
{
    setAlarmPin(true);

    if (sws_display == SWD_NONE) {

        // show icon
        drawMainPageStopwatch (true);

        // overwrite pane, wait here until dismiss, refresh pane
        const PlotPane alarm_pane = PANE_2;
        SBox &b = plot_b[alarm_pane];

        // close down other network systems if using this pane
        if (findPaneChoiceNow(PLOT_CH_DXCLUSTER) == alarm_pane)
            closeDXCluster();
        if (findPaneChoiceNow(PLOT_CH_GIMBAL) == alarm_pane)
            closeGimbal();

        // prep
        prepPlotBox (b);

        // alarm!
        const char *astr = "Alarm!";
        selectFontStyle (BOLD_FONT, SMALL_FONT);
        tft.setCursor (b.x + (b.w-getTextWidth(astr))/2, b.y + b.h/3);
        tft.setTextColor (RA8875_RED);
        tft.print (astr);

        // show a dismiss button
        SBox dismiss_b;
        dismiss_b.x = b.x + 30;
        dismiss_b.y = b.y + 2*b.h/3;
        dismiss_b.w = b.w - 60;
        dismiss_b.h = 35;
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        drawStringInBox (" Cancel ", dismiss_b, false, BRGRAY);

        // wait for tap anywhere in pane or time out
        SCoord s;
        char c;
        UserInput ui = {
            b,
            checkExternalTurnOff,
            false,
            ALM_RINGTO,
            true,
            s,
            c,
        };
        (void) waitForUser (ui);

        // off
        alarm_state = ALMS_ARMED;
        drawMainPageStopwatch (true);
        setAlarmPin (false);

        // restart -- init doesn't include our own countdown pane
        if (findPaneChoiceNow(PLOT_CH_COUNTDOWN) == alarm_pane)
            drawCDTimeRemaining(true);
        else
            initWiFiRetry();

    } else {

        drawAlarmIndicator(true);
    }
}

/* display a Big Clock menu anchored at the given screen loc.
 * return with bc_bits and/or sws_display possibly changed.
 * we do NOT redraw the clock.
 */
static void runBCMenu (const SCoord &s)
{
    // handy enumeration of fields -- N.B. must align with mitems[]
    enum MIName {
        MI_FMT_TTL,
            MI_FMT_ANA,
                MI_ANA_DIG,
                MI_ANA_NUM,
                MI_ANA_FIL,
            MI_FMT_DIG,
                MI_DIG_UTC,
                MI_DIG_LST,
                MI_DIG_12H,
                MI_DIG_24H,
        MI_ALL_TTL,
            MI_ALL_SEC,
            MI_ALL_SDT,
            MI_ALL_CDW,
            MI_ALL_ALM,
            MI_ALL_SAT,
            MI_ALL_SWX,
            MI_ALL_SPW,
        MI_BLK1,
        MI_EXT,
        MI_BLK2,
        MI_N,
    };

    // items -- N.B. must align with MIName
    #define PRI_INDENT 4
    #define SEC_INDENT 10
    #define TER_INDENT 16
    MenuItem mitems[MI_N] = {
        {MENU_LABEL, false, 0, PRI_INDENT, "Format:"},
            {MENU_1OFN, !(bc_bits & SW_BCDIGBIT), 1, SEC_INDENT, "Analog"},
                {MENU_TOGGLE, !!(bc_bits & SW_ANWDBIT), 2, TER_INDENT, "+ Digital"},
                {MENU_TOGGLE, !!(bc_bits & SW_ANNUMBIT), 2, TER_INDENT, "Numbers"},
                {MENU_TOGGLE, !!(bc_bits & SW_ANCOLHBIT), 2, TER_INDENT, "Color hands"},
            {MENU_1OFN, !!(bc_bits & SW_BCDIGBIT), 1, SEC_INDENT, "Digital"},
                {MENU_1OFN, !!(bc_bits & SW_UTCBIT), 3, TER_INDENT, "UTC"},
                {MENU_1OFN, !!(bc_bits & SW_LSTBIT), 3, TER_INDENT, "LST"},
                {MENU_1OFN, (bc_bits & (SW_DB12HBIT|SW_UTCBIT|SW_LSTBIT)) == SW_DB12HBIT,
                                3, TER_INDENT, "DE 12 hour"},
                {MENU_1OFN, (bc_bits & (SW_DB12HBIT|SW_UTCBIT|SW_LSTBIT)) == 0, 3, TER_INDENT, "DE 24 hour"},
        {MENU_LABEL, false, 0, PRI_INDENT, "Also show:"},
            {MENU_TOGGLE, !(bc_bits & SW_NOSECBIT), 4, SEC_INDENT, "Seconds"},
            {MENU_TOGGLE, !!(bc_bits & SW_BCDATEBIT), 5, SEC_INDENT, "Date info"},
            {MENU_TOGGLE, sws_engine == SWE_COUNTDOWN, 6, SEC_INDENT, "Count down"},
            {MENU_TOGGLE, alarm_state != ALMS_OFF, 7, SEC_INDENT, "Alarm"},
            {isSatDefined() ? MENU_TOGGLE : MENU_IGNORE, !!(bc_bits & SW_BCSATBIT),
                                8, SEC_INDENT, "Satellite"},
            {MENU_01OFN, !!(bc_bits & SW_BCWXBIT), 9, SEC_INDENT, "DE WX"},
            {MENU_01OFN, !!(bc_bits & SW_BCSPWXBIT), 9, SEC_INDENT, "Space WX"},
        {MENU_BLANK, false, 9, PRI_INDENT, NULL},
        {MENU_TOGGLE, false, 10, PRI_INDENT, "Exit Big Clock"},
        {MENU_BLANK, false, 11, PRI_INDENT, NULL},
    };

    // box for menu anchored at s
    SBox menu_b;
    menu_b.x = s.x;
    menu_b.y = s.y;
    menu_b.w = 0;               // shrink to fit
    // w/h are set dynamically by runMenu()

    // run, do nothing more if cancelled
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, false, false, 1, MI_N, mitems};
    if (!runMenu (menu))
        return;

    // engage each selection.

    if (menu.items[MI_FMT_ANA].set) {
        bc_bits &= ~SW_BCDIGBIT;
        sws_display = SWD_BCANALOG;
    } else {
        bc_bits |= SW_BCDIGBIT;
        sws_display = SWD_BCDIGITAL;
    }

    // N.B. check exit AFTER other sws_display possibilities
    if (menu.items[MI_EXT].set)
        sws_display = SWD_MAIN;

    if (menu.items[MI_ANA_DIG].set)
        bc_bits |= SW_ANWDBIT;
    else
        bc_bits &= ~SW_ANWDBIT;

    if (menu.items[MI_ANA_NUM].set)
        bc_bits |= SW_ANNUMBIT;
    else
        bc_bits &= ~SW_ANNUMBIT;

    if (menu.items[MI_DIG_UTC].set)
        bc_bits |= SW_UTCBIT;
    else
        bc_bits &= ~SW_UTCBIT;

    if (menu.items[MI_DIG_LST].set)
        bc_bits |= SW_LSTBIT;
    else
        bc_bits &= ~SW_LSTBIT;

    if (menu.items[MI_DIG_12H].set)
        bc_bits |= SW_DB12HBIT;
    else
        bc_bits &= ~SW_DB12HBIT;

    if (menu.items[MI_ALL_SEC].set)             // N.B. inverted from what you might expect
        bc_bits &= ~SW_NOSECBIT;
    else
        bc_bits |= SW_NOSECBIT;

    if (menu.items[MI_ALL_SDT].set)
        bc_bits |= SW_BCDATEBIT;
    else
        bc_bits &= ~SW_BCDATEBIT;

    if (sws_engine == SWE_COUNTDOWN && !menu.items[MI_ALL_CDW].set)
        sws_engine = SWE_RESET;
    if (sws_engine != SWE_COUNTDOWN && menu.items[MI_ALL_CDW].set) {
        sws_engine = SWE_COUNTDOWN;
        start_t = millis();
    }

    if (alarm_state == ALMS_OFF && menu.items[MI_ALL_ALM].set)
        alarm_state = ALMS_ARMED;
    if (alarm_state != ALMS_OFF && !menu.items[MI_ALL_ALM].set)
        alarm_state = ALMS_OFF;

    if (menu.items[MI_ALL_SWX].set)
        bc_bits |= SW_BCWXBIT;
    else
        bc_bits &= ~SW_BCWXBIT;

    if (menu.items[MI_ALL_SPW].set)
        bc_bits |= SW_BCSPWXBIT;
    else
        bc_bits &= ~SW_BCSPWXBIT;

    if (menu.items[MI_ALL_SAT].set)
        bc_bits |= SW_BCSATBIT;
    else
        bc_bits &= ~SW_BCSATBIT;

    if (menu.items[MI_ANA_FIL].set)
        bc_bits |= SW_ANCOLHBIT;
    else
        bc_bits &= ~SW_ANCOLHBIT;

    // save new settings
    saveSWNV();

}


/* check our touch controls, update state.
 * works for all stopwatch pages: main and either big clock
 */
static void checkSWPageTouch()
{
    // out fast if nothing to do
    SCoord s;
    if (screenIsLocked() || (readCalTouchWS(s) == TT_NONE && checkKBWarp(s) == TT_NONE))
        return;

    // update idle timer, ignore if this tap is restoring full brightness
    if (brightnessOn())
        return;

    if (sws_display == SWD_MAIN) {

        // main stopwatch boxes

        if (inBox (s, countdown_lbl_b)) {

            // start countdown timer regardless of current state
            setSWEngineState (SWE_COUNTDOWN, countdown_period);

        } else if (inBox (s, cdtime_up_b)) {

            // increment countdown period
            countdown_period += 60000;
            countdown_period -= (countdown_period % 60000);             // insure whole minute
            saveSWNV();
            if (sws_engine == SWE_COUNTDOWN)
                setSWEngineState (sws_engine, countdown_period);        // engage new value immediately
            else
                drawSWCDPeriod();                                       // just display new value

        } else if (inBox (s, cdtime_dw_b)) {

            // decrement countdown period
            if (countdown_period >= 2*60000) {                          // 1 minute minimum
                countdown_period -= 60000;
                countdown_period -= (countdown_period % 60000);         // insure whole minute
                saveSWNV();
                if (sws_engine == SWE_COUNTDOWN)
                    setSWEngineState (sws_engine, countdown_period);    // engage new value immediately
                else
                    drawSWCDPeriod();                                   // just display new value
            }

        } else if (inBox (s, alarm_up_b)) {

            // increase alarm hour or minute
            uint16_t a_hr = alarm_hrmn/60;
            uint16_t a_mn = alarm_hrmn%60;
            if (s.x < alarm_up_b.x + 7*alarm_up_b.w/12) {                // coordinate with drawAlarmIndicator
                a_hr = (a_hr + 1) % 24;
            } else {
                if (++a_mn == 60) {
                    if (++a_hr == 24)
                        a_hr = 0;
                    a_mn = 0;
                }
            }
            alarm_hrmn = a_hr*60 + a_mn;
            saveSWNV();
            drawAlarmIndicator (false);

        } else if (inBox (s, alarm_dw_b)) {

            // decresae alarm hour or minute
            uint16_t a_hr = alarm_hrmn/60;
            uint16_t a_mn = alarm_hrmn%60;
            if (s.x < alarm_up_b.x + 7*alarm_up_b.w/12) {                // coordinate with drawAlarmIndicator
                a_hr = (a_hr + 23) % 24;
            } else {
                if (a_mn == 0) {
                    a_mn = 59;
                    a_hr = (a_hr + 23) % 24;
                } else {
                    a_mn -= 1;
                }
            }
            alarm_hrmn = a_hr*60 + a_mn;
            saveSWNV();
            drawAlarmIndicator (false);

        } else if (inBox (s, alarm_lbl_b)) {

            // control alarm clock mode
            switch (alarm_state) {
            case ALMS_OFF:
                alarm_state = ALMS_ARMED;
                break;
            case ALMS_ARMED:
                alarm_state = ALMS_OFF;
                break;
            case ALMS_RINGING:
                alarm_state = ALMS_ARMED;
                break;
            }
            drawAlarmIndicator  (true);
            saveSWNV();
        
        } else if (inBox (s, A_b)) {

            // box action depends on current engine state
            SWEngineState new_sws;
            switch (sws_engine) {
            case SWE_RESET:
                // clicked Run
                new_sws = SWE_RUN;
                break;
            case SWE_RUN:
                // clicked Stop
                new_sws = SWE_STOP;
                break;
            case SWE_STOP:
                // clicked Run
                new_sws = SWE_RUN;
                break;
            case SWE_LAP:
                // clicked Reset
                new_sws = SWE_RESET;
                break;
            case SWE_COUNTDOWN:
                // clicked Reset
                new_sws = SWE_RESET;
                break;
            default:
                new_sws = SWE_RESET;
                break;
            }

            // update state and GUI
            setSWEngineState (new_sws, countdown_period);

        } else if (inBox (s, B_b)) {

            // box action depends on current engine state
            SWEngineState new_sws;
            switch (sws_engine) {
            case SWE_RESET:
                // clicked Reset
                new_sws = SWE_RESET;
                break;
            case SWE_RUN:
                // clicked Lap
                new_sws = SWE_LAP;
                break;
            case SWE_STOP:
                // clicked Reset
                new_sws = SWE_RESET;
                break;
            case SWE_LAP:
                // clicked Resume
                new_sws = SWE_RUN;
                break;
            case SWE_COUNTDOWN:
                // clicked Reset
                new_sws = SWE_RESET;
                break;
            default:
                new_sws = SWE_RESET;
                break;
            }

            // update state and GUI
            setSWEngineState (new_sws, countdown_period);

        } else if (inBox (s, exit_b)) {

            // done
            sws_display = SWD_NONE;

        } else if (inBox (s, color_b)) {

            // change color and redraw
            sw_hue = 255*(s.x - color_b.x)/color_b.w;
            NVWriteUInt8 (NV_SWHUE, sw_hue);
            drawSWMainPage();

        } else if (inBox (s, bigclock_b)) {

            // start desired big clock
            Serial.println(F("SW: BigClock enter"));
            sws_display = (bc_bits & SW_BCDIGBIT) ? SWD_BCDIGITAL : SWD_BCANALOG;
            drawBigClock (true);
        }

    } else if (sws_display == SWD_BCDIGITAL || sws_display == SWD_BCANALOG) {

        // first check the optional display controls
        if (sws_engine == SWE_COUNTDOWN && inBox (s, bccd_b)) {
            // reset cd time but stay in SWE_COUNTDOWN state
            start_t = millis();
        } else if (alarm_state != ALMS_OFF && inBox (s, bcalarm_b)) {
            // don't run menu if click alarm even if not ringing
            if (alarm_state == ALMS_RINGING) {
                alarm_state = ALMS_ARMED;
                drawAlarmIndicator(false);
            }
        } else {
            // show menu and redraw to engage any changes even if cancelled just to erase menu
            runBCMenu (s);
            if (sws_display == SWD_MAIN) {
                Serial.println(F("SW: BigClock exit"));
                eraseScreen();
                drawSWMainPage();
            } else {
                drawBigClock (true);
            }
            saveSWNV();
        }
    }
}

/* one-time prep
 */
void initStopwatch()
{
    // read values from NV
    if (!NVReadUInt16 (NV_BCFLAGS, &bc_bits)) {
        bc_bits = SW_BCDATEBIT | SW_BCWXBIT;
        NVWriteUInt16 (NV_BCFLAGS, bc_bits);
    }
    if (!NVReadUInt32 (NV_CD_PERIOD, &countdown_period)) {
        countdown_period = 600000;     // 10 mins default
        NVWriteUInt32 (NV_CD_PERIOD, countdown_period);
    }

    // read and unpack alarm time and whether active
    if (!NVReadUInt16 (NV_ALARMCLOCK, &alarm_hrmn)) {
        alarm_hrmn = 0;
        alarm_state = ALMS_OFF;
        NVWriteUInt16 (NV_ALARMCLOCK, 0);
    }
    if (alarm_hrmn >= ALM_TOVFLOW) {
        alarm_hrmn = alarm_hrmn % ALM_TOVFLOW;
        alarm_state = ALMS_ARMED;
    }

    // init pins
    mcp.pinMode (SW_ALARMOUT_PIN, OUTPUT);
    mcp.digitalWrite (SW_ALARMOUT_PIN, LOW);            // off low

    startBinkerThread (sw_cd_blinker_grn, SW_CD_GRN_PIN, true); // on is low
    startBinkerThread (sw_cd_blinker_red, SW_CD_RED_PIN, true); // on is low
    startMCPPoller (sw_cd_reset, SW_CD_RESET_PIN, 2);
    startMCPPoller (sw_alarmoff, SW_ALARMOFF_PIN, 2);

    setCDLEDState (SWCDS_OFF);
    setAlarmPin (false);
}

/* draw the main HamClock page stopwatch icon or count down time remaining or alarm is set in stopwatch_b
 *   and/or pane if showing, all depending on sws_engine.
 */
void drawMainPageStopwatch (bool force)
{
    if (sws_engine == SWE_COUNTDOWN) {

        drawCDTimeRemaining(force);

    } else if (force) {

        // draw icon

        // erase
        fillSBox (stopwatch_b, RA8875_BLACK);
        #if defined(_SHOW_ALL)
            drawSBox (stopwatch_b, RA8875_WHITE);
        #endif

        // body radius and step for stems
        uint16_t br = 3*stopwatch_b.h/8;
        uint16_t xc = stopwatch_b.x + stopwatch_b.w/2;
        uint16_t yc = stopwatch_b.y + stopwatch_b.h/2;
        uint16_t dx = roundf(br*cosf(deg2rad(45)));

        // body color depends on whether alarm is armed
        uint16_t body_c = alarm_state != ALMS_OFF ? RA8875_GREEN : GRAY;

        // watch
        tft.fillCircle (xc, yc, br, body_c);

        // top stem
        tft.fillRect (xc-1, yc-br-3, 3, 4, body_c);

        // 2 side stems
        tft.fillCircle (xc-dx, yc-dx-1, 1, body_c);
        tft.fillCircle (xc+dx, yc-dx-1, 1, body_c);

        // face
        tft.drawCircle (xc, yc, 3*br/4, RA8875_BLACK);

        // hands
        tft.drawLine (xc, yc, xc, yc-3*br/4, RA8875_WHITE);
        tft.drawLine (xc, yc, xc+3*br/6, yc, RA8875_WHITE);

        // add "vibration" if ringing
        if (alarm_state == ALMS_RINGING) {
            float vr = 1.4F*br;
            uint16_t vdx1 = roundf(vr*cosf(deg2rad(5)));
            uint16_t vdy1 = roundf(vr*sinf(deg2rad(5)));
            uint16_t vdx2 = roundf(vr*cosf(deg2rad(30)));
            uint16_t vdy2 = roundf(vr*sinf(deg2rad(30)));
            tft.drawLine (xc+vdx1, yc-vdy1, xc+vdx2, yc-vdy2, body_c);
            tft.drawLine (xc-vdx1, yc-vdy1, xc-vdx2, yc-vdy2, body_c);
            vr = 1.8F*br;
            vdx1 = roundf(vr*cosf(deg2rad(5)));
            vdy1 = roundf(vr*sinf(deg2rad(5)));
            vdx2 = roundf(vr*cosf(deg2rad(30)));
            vdy2 = roundf(vr*sinf(deg2rad(30)));
            tft.drawLine (xc+vdx1, yc-vdy1, xc+vdx2, yc-vdy2, body_c);
            tft.drawLine (xc-vdx1, yc-vdy1, xc-vdx2, yc-vdy2, body_c);
        }
    }
}


/* stopwatch_b has been touched from HamClock Main page:
 * if tapped while counting down just reset and continue main HamClock page, else start main SW page.
 */
void checkStopwatchTouch(TouchType tt)
{
    // if tapped the stop watch while counting down, just restart
    if (sws_engine == SWE_COUNTDOWN && tt == TT_TAP) {
        setSWEngineState (SWE_COUNTDOWN, countdown_period);
        return;
    }

    Serial.println(F("SW: main enter"));

    // close down other systems
    closeDXCluster();           // prevent inbound msgs from clogging network
    closeGimbal();              // avoid dangling connection
    hideClocks();

    // fresh start
    eraseScreen();
    drawSWMainPage();
}

/* called by main loop to run another iteration of the stop watch.
 * we may or may not be running ("engine" state) and may or may not be visible ("display" state).
 * return whether any stopwatch page is visible now.
 */
bool runStopwatch()
{
    resetWatchdog();

    // always honor countdown switch regardless of display state
    if (countdownSwitchIsTrue())
        setSWEngineState (SWE_COUNTDOWN, countdown_period);

    // check for aged countdown runout
    if (sws_engine == SWE_COUNTDOWN && getCountdownLeft() == 0
                                && millis()-start_t > countdown_period + SW_CD_AGEDT)
        setSWEngineState (SWE_RESET, 0);

    // always check alarm clock regardless of display state
    if (alarm_state == ALMS_ARMED && checkAlarm()) {
        // record time and indicate alarm has just gone off
        alarm_ringtime = myNow();
        alarm_state = ALMS_RINGING;
        showAlarmRinging();
    }
    if (alarm_state == ALMS_RINGING) {
        if (alarmSwitchIsTrue() || myNow() - alarm_ringtime >= ALM_RINGTO/1000) {
            // op hit the cancel pin or timed out
            alarm_state = ALMS_ARMED;
            if (sws_display == SWD_NONE)
                drawMainPageStopwatch (true);
            else
                drawAlarmIndicator (true);
        }
    }

    if (sws_display != SWD_NONE) {

        // one of the stopwatch pages is up

        // check for our button taps.
        // N.B. this may update sws_display so check again afterwards
        checkSWPageTouch();

        switch (sws_display) {

        case SWD_NONE:
            Serial.println(F("SW: main exit"));
            insureCountdownPaneSensible();
            initScreen();
            return (false);

        case SWD_MAIN:
            // show timer if running but not so often as to overload the graphics
            if (sws_engine == SWE_RUN) {
                static uint32_t main_time_gate;
                if (timesUp (&main_time_gate, 41))      // prime number insures all digits change
                    drawSWTime(millis() - start_t);
            }
            break;

        case SWD_BCDIGITAL:
            drawDigitalBigClock (false);
            break;

        case SWD_BCANALOG:
            drawAnalogBigClock (false);
            break;
        }

        // update countdown if running
        if (sws_engine == SWE_COUNTDOWN)
            drawCDTimeRemaining(false);

        // update other systems
        followBrightness();
        readBME280();

        // stopwatch is up
        return (true);

    } else {

        // main hamclock page is up, update count if counting down
        if (sws_engine == SWE_COUNTDOWN)
            drawMainPageStopwatch (false);

        // not up
        return (false);
    }
}

/* change stopwatch engine state and appearance.
 * also set countdown to ms if changing to SWE_COUNTDOWN.
 * return whether requested state is valid now.
 */
bool setSWEngineState (SWEngineState new_sws, uint32_t ms)
{
    switch (new_sws) {
    case SWE_RESET:
        setCDLEDState (SWCDS_OFF);
        if (sws_engine == SWE_RESET)
            return (true);                      // ignore if no change
        sws_engine = SWE_RESET;
        break;
    case SWE_RUN:
        if (sws_engine == SWE_RUN)
            return (true);                      // ignore if no change
        if (sws_engine == SWE_COUNTDOWN)
            break;                              // just continue running countdown
        if (sws_engine == SWE_STOP)
            start_t = millis() - stop_dt;       // resume after stop: reinstate delta
        else if (sws_engine != SWE_LAP)           // resume after lap: just keep going
            start_t = millis();                 // start fresh
        sws_engine = SWE_RUN;
        break;
    case SWE_STOP:
        if (sws_engine == SWE_STOP)
            return (true);                      // ignore if no change
        if (sws_engine == SWE_COUNTDOWN)
            return (false);                     // stop not implemented for countdown
        stop_dt = millis() - start_t;           // capture delta
        sws_engine = SWE_STOP;
        break;
    case SWE_LAP:
        if (sws_engine == SWE_LAP)
            return (true);                      // ignore if no change
        if (sws_engine == SWE_COUNTDOWN || sws_engine == SWE_STOP)
            return (false);                     // lap not implemented for countdown or stop
        stop_dt = millis() - start_t;           // capture delta
        sws_engine = SWE_LAP;
        break;
    case SWE_COUNTDOWN:
        countdown_period = ms;
        saveSWNV();
        start_t = millis();
        sws_engine = SWE_COUNTDOWN;
        break;
    default:
        return (false);
    }

    // draw new state appearance
    drawSWState();

    return (true);
}

/* return current engine state and associated timer value and cd period in ms.
 * both can be NULL if not interested.
 */
SWEngineState getSWEngineState (uint32_t *sw_timer, uint32_t *cd_period)
{
    if (sw_timer) {
        switch (sws_engine) {
        case SWE_RESET:
            *sw_timer = 0;
            break;
        case SWE_RUN:
            *sw_timer = millis() - start_t;
            break;
        case SWE_STOP:
            *sw_timer = stop_dt;
            break;
        case SWE_LAP:
            *sw_timer = stop_dt;
            break;
        case SWE_COUNTDOWN:
            *sw_timer = getCountdownLeft();
            break;
        }
    }

    if (cd_period)
        *cd_period = countdown_period;

    return (sws_engine);
}

/* retrieve current stopwatch display state
 */
SWDisplayState getSWDisplayState()
{
    return (sws_display);
}

/* return alarm state and time 
 */
void getAlarmState (AlarmState &as, uint16_t &hr, uint16_t &mn)
{
    as = alarm_state;
    hr = alarm_hrmn / 60;
    mn = alarm_hrmn % 60;
}

/* set a new alarm state from a web command.
 * N.B. we do no error checking
 */
void setAlarmState (const AlarmState &as, uint16_t hr, uint16_t mn)
{
    if (as == ALMS_OFF) {
        // minimal state downgrade, leave time unchanged
        alarm_state = alarm_state == ALMS_RINGING ? ALMS_ARMED : ALMS_OFF;
    } else {
        // set new state and time
        alarm_state = as;
        alarm_hrmn = hr*60U + mn;
    }
    saveSWNV();

    // update display
    if (sws_display == SWD_NONE)
        drawMainPageStopwatch (true);
    else
        drawAlarmIndicator (true);
}

SWBCBits getBigClockBits(void)
{
    return ((SWBCBits)bc_bits);
}
