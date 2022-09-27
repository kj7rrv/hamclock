/* Manage display brightness, either automatically via phot sensor or via on/off/idle settings or brightness
 * slider by the user. N.B. pane purpose must coordinate with ncfdx button and key.
 *
 * ESP phot circuit:
 *
 *       +3.3V
 *         |
 *        330K
 *         |
 *         --- A0
 *         |
 *   photoresistor
 *         |
 *        Gnd
 *
 *
 * H/W Capability matrix:
 *   FB0 = _IS_RPI && _USE_FB0
 *   FS  = !IS_ESP && getX11FullScreen()
 *   DSI = _IS_RPI and display is DSI
 *
 *                         On/Off          Bightness     PhotoR
 *  _IS_ESP8266              Y                 Y           Y
 *  (FB0 || FS) && DSI       Y                 Y           N
 *  FB0 || FS                Y                 N           N
 *  else                     N                 N           N
 *    
 */


#include "HamClock.h"

bool found_phot;                                // set if initial read > 1, else manual clock settings

// NCDXF_b or "BRB" public state
uint8_t brb_mode;                               // one of BRB_MODE
uint8_t brb_rotset;                             // mask of current BRB_MODE
time_t brb_rotationT;                           // time of next rotation
const char *brb_names[BRB_N] = {                // N.B. must be in same order as BRB_MODE
     "NCDXF",
     "On/Off",
     "PhotoR",
     "Brite",
     "SpcWx",
     "BME@76",
     "BME@77",
};

// configuration values
#define BPWM_MAX        255                     // PWM for 100% brightness
#define BPWM_CHG        1.259                   // brightness mult per tap: 1 db = 1 JND
#define BPWM_MIN_CHG    4                       // smallest value that increases by >= 1 when * BPWM_CHG
#define PHOT_PIN        A0                      // Arduino name of analog pin with photo detector
#define PHOT_MAX        1024                    // 10 bits including known range bug
#define BPWM_BLEND      0.2F                    // fraction of new brightness to keep
#define PHOT_BLEND      0.5F                    // fraction of new pwm to keep
#define BPWM_COL        RA8875_WHITE            // brightness scale color
#define PHOT_COL        RA8875_CYAN             // phot scale color
#define BRIGHT_COL      RA8875_RED              // dim marker color  
#define DIM_COL         RA8875_BLUE             // dim marker color  
#define N_ROWS          11                      // rows of clock info, including gaps
#define SFONT_H         7                       // small font height
#define MARKER_H        3                       // scaler marker height
#define SCALE_W         5                       // scale width
#define FOLLOW_DT       100                     // read phot this often, ms

static int16_t bpwm;                            // current brightness PWM value 0 .. BPWM_M
static uint16_t phot;                           // current photorestistor value

// fast access to what is in NVRAM
static uint16_t fast_phot_bright, fast_phot_dim;
static uint16_t fast_bpwm_bright, fast_bpwm_dim;

// timers, idle and hw config
static uint16_t mins_on, mins_off;              // user's local on/off times, stored as hr*60 + min
static uint16_t idle_mins;                      // user's idle timeout period, minutes; 0 for none
static uint32_t idle_t0;                        // time of last user action, millis
static bool clock_off;                          // whether clock is now ostensibly off
static uint8_t user_on, user_off;               // user's on and off brightness
static bool support_onoff;                      // whether we support display on/off
static bool support_dim;                        // whether we support display fine brightness control
static bool support_phot;                       // whether we support a photoresistor

#if defined(_IS_LINUX_RPI) || defined(_USE_FB0)
// RPi path to set DSI brightness, write 0 .. 255
static const char dsi_path_buster[] = "/sys/class/backlight/rpi_backlight/brightness";
static const char dsi_path_bullseye[] = "/sys/class/backlight/10-0045/brightness";
#endif
static const char *dsi_path = "x";              // one of above if one works, non-null now just for lint

// forward references
static void engageDisplayBrightness(bool log);



/* set display brightness to bpwm.
 * on ESP we control backlight, RPi control displays, others systems ignored.
 */
static void setDisplayBrightness(bool log)
{
    // lint
    (void) dsi_path;

    #if defined(_IS_ESP8266)

        if (log)
            Serial.printf (_FX("BR: setting bpwm %d\n"), bpwm);

        // ESP: control backlight
        tft.PWM1out(bpwm);

    #else

        if (support_dim) {
            // control DSI backlight
            // could use 'vcgencmd set_backlight x' but still need a way to check whether bl is controllable
            int dsifd = open (dsi_path, O_WRONLY);
            if (dsifd < 0) {
                Serial.printf ("BR: %s: %s\n", dsi_path, strerror(errno));
            } else {
                if (log)
                    Serial.printf (_FX("BR: setting bpwm %d\n"), bpwm);
                FILE *dsifp = fdopen (dsifd, "w");
                fprintf (dsifp, "%d\n", bpwm);
                fclose (dsifp); // also closes dsifd
            }
        } else if (support_onoff) {
            // control HDMI on or off
            // N.B. can't use system() because we need to retain suid root
            const char *argv_vcg[] = {          // try vcgencmd
                "vcgencmd", "display_power", bpwm > BPWM_MAX/2 ? "1" : "0", NULL
            };
            const char *argv_sso[] = {          // try turning off system dimming
                "xset", "dpms", "0", "0", "0", NULL
            };
            const char *argv_ssvr[] = {         // and turning off the screen saver
                "xset", "s", "0", NULL
            };
            const char *argv_dpms[] = {         // now try forcing dpms
                "xset", "dpms", "force", bpwm > BPWM_MAX/2 ? "on" : "off", NULL
            };
            const char **argvs[] = {            // collect for easy use
                argv_vcg, argv_sso, argv_ssvr, argv_dpms, NULL
            };
            for (const char ***av = argvs; *av != NULL; av++) {
                const char **argv = *av;
                if (log) {
                    Serial.printf ("BR:");
                    for (const char **ap = argv; *ap != NULL; ap++)
                        Serial.printf (" %s", *ap);
                    Serial.printf ("\n");
                }
                if (fork() == 0)
                    execvp (argv[0], (char**)argv);
                sleep (1);
            }
        }

    #endif
}

/* return current photo detector value, range [0..PHOT_MAX] increasing with brightness.
 */
static uint16_t readPhot()
{
    #if defined(_SUPPORT_PHOT)

        resetWatchdog();

        uint16_t new_phot = PHOT_MAX - analogRead (PHOT_PIN);           // brighter gives smaller value

        // Serial.println (new_phot);

        resetWatchdog();

        return (PHOT_BLEND*new_phot + (1-PHOT_BLEND)*phot);             // smoothing

    #else

        return (0);

    #endif  // _SUPPORT_PHOT
}



/* get dimensions of the phot slider control
 */
static void getPhotControl (SBox &p)
{
        // N.B. match getBrControl()
        p.w = SCALE_W;
        p.y = NCDXF_b.y + NCDXF_b.h/9;
        p.h = 6*NCDXF_b.h/10;

        // right third
        p.x = NCDXF_b.x + 2*(NCDXF_b.w - SCALE_W)/3;
}



/* draw a symbol for the photresistor in NCDXF_b.
 * skip if stopwatch is up.
 */
static void drawPhotSymbol()
{
        if (getSWDisplayState() != SWD_NONE)
            return;

        uint8_t n = 2;                                                  // number of \/
        uint16_t w = 2*n+8;                                             // n steps across
        uint16_t s = NCDXF_b.w/w;                                  // 1 x step length
        uint16_t x = NCDXF_b.x + (NCDXF_b.w-w*s)/2 + 2*s;     // initial x to center
        uint16_t y = NCDXF_b.y + NCDXF_b.h - 3*s;             // y center-line

        // lead in from left then up
        tft.drawLine (x, y, x+s, y, PHOT_COL);
        x += s;
        tft.drawLine (x, y, x+s, y-s, PHOT_COL);
        x += s;

        // draw n \/
        for (uint8_t i = 0; i < n; i++) {
            tft.drawLine (x, y-s, x+s, y+s, PHOT_COL);
            x += s;
            tft.drawLine (x, y+s, x+s, y-s, PHOT_COL);
            x += s;
        }

        // down then lead out to right
        tft.drawLine (x, y-s, x+s, y, PHOT_COL);
        x += s;
        tft.drawLine (x, y, x+s, y, PHOT_COL);

        // incoming light arrows
        uint16_t ax = NCDXF_b.x + 6*s;                     // arrow head location

        tft.drawLine (ax, y-2*s, ax-1*s,   y-3*s,    PHOT_COL); // main shaft
        tft.drawLine (ax, y-2*s, ax-3*s/4, y-19*s/8, PHOT_COL); // lower shaft
        tft.drawLine (ax, y-2*s, ax-3*s/8, y-11*s/4, PHOT_COL); // upper shaft 

        ax += 2*s;                                              // move to second arrow head and repeat

        tft.drawLine (ax, y-2*s, ax-1*s,   y-3*s,    PHOT_COL);
        tft.drawLine (ax, y-2*s, ax-3*s/4, y-19*s/8, PHOT_COL);
        tft.drawLine (ax, y-2*s, ax-3*s/8, y-11*s/4, PHOT_COL);
}


/* draw phot control.
 * skip if stopwatch is up.
 */
static void drawPhotControl()
{
        resetWatchdog();

        if (getSWDisplayState() != SWD_NONE)
            return;

        SBox p;
        getPhotControl (p);

        // draw phot scale
        int16_t ph = (p.h-2-MARKER_H)*phot/PHOT_MAX + MARKER_H+1;
        tft.fillRect (p.x+1, p.y+1, p.w-1, p.h-1, RA8875_BLACK);    // leave border to avoid flicker
        drawSBox (p, PHOT_COL);
        tft.fillRect (p.x, p.y+p.h-ph, p.w, MARKER_H, PHOT_COL);

        // overlay phot limits, avoid top and bottom
        ph = (p.h-2-1)*fast_phot_bright/PHOT_MAX + 2;
        tft.drawLine (p.x+1, p.y+p.h-ph, p.x+p.w-2, p.y+p.h-ph, BRIGHT_COL);
        ph = (p.h-2-1)*fast_phot_dim/PHOT_MAX + 2;
        tft.drawLine (p.x+1, p.y+p.h-ph, p.x+p.w-2, p.y+p.h-ph, DIM_COL);
}


/* get dimensions of the brightness slider control, depends on whether we alone or with phot scale.
 */
static void getBrControl (SBox &b)
{
        // N.B. match getPhotControl()
        b.w = SCALE_W;
        b.y = NCDXF_b.y + NCDXF_b.h/9;
        b.h = 6*NCDXF_b.h/10;

        // x depends on mode
        if (brb_mode == BRB_SHOW_PHOT)
            b.x = NCDXF_b.x + (NCDXF_b.w - SCALE_W)/3;
        else
            b.x = NCDXF_b.x + (NCDXF_b.w - SCALE_W)/2;
}


/* draw current brightness control.
 * skip if stopwatch is up.
 */
static void drawBrControl()
{
        resetWatchdog();

        if (getSWDisplayState() != SWD_NONE)
            return;

        SBox b;
        getBrControl (b);

        // draw bpwm scale
        int16_t bh = (b.h-2-MARKER_H)*(bpwm-user_off)/(user_on - user_off) + MARKER_H+1;
        tft.fillRect (b.x+1, b.y+1, b.w-2, b.h-2, RA8875_BLACK);    // leave border to avoid flicker
        drawSBox (b, BPWM_COL);
        tft.fillRect (b.x, b.y+b.h-bh, b.w, MARKER_H, BPWM_COL);

        if (brb_mode == BRB_SHOW_PHOT) {
            // overlay bpwm limits, avoid top and bottom
            bh = (b.h-2-1)*fast_bpwm_bright/BPWM_MAX + 2;
            tft.drawLine (b.x+1, b.y+b.h-bh, b.x+b.w-2, b.y+b.h-bh, BRIGHT_COL);
            bh = (b.h-2-1)*fast_bpwm_dim/BPWM_MAX + 2;
            tft.drawLine (b.x+1, b.y+b.h-bh, b.x+b.w-2, b.y+b.h-bh, DIM_COL);
        }
}

/* draw mins_on/mins_off and idle controls.
 * skip if stopwatch is up or not in proper mode.
 * N.B. coordinate layout with changeOnOffSetting().
 */
static void drawOnOffControls()
{
        resetWatchdog();

        if (getSWDisplayState() != SWD_NONE || brb_mode != BRB_SHOW_ONOFF)
            return;

        tft.fillRect (NCDXF_b.x+1, NCDXF_b.y+1, NCDXF_b.w-2, NCDXF_b.h-2, RA8875_BLACK);
        tft.drawLine (NCDXF_b.x, NCDXF_b.y, NCDXF_b.x+NCDXF_b.w, NCDXF_b.y, GRAY);
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (RA8875_WHITE);

        // left x values
        uint16_t xl = NCDXF_b.x + 7;               // label indent
        uint16_t xn = NCDXF_b.x + 12;              // number indent

        // title
        tft.setCursor (xl, NCDXF_b.y+2);
        tft.print (F("Display"));

        // walk down by dy each time
        uint8_t dy = NCDXF_b.h/N_ROWS;
        uint16_t y = NCDXF_b.y + dy - SFONT_H/2;

        // gap
        y += dy;

        // idle
        tft.setCursor (xl-3, y+=dy);
        tft.print (F("Idle in:"));
        tft.setCursor (xn-3, y+=dy);
        tft.print (idle_mins);
        tft.print (F(" min"));

        // gap
        y += dy;

        // time on
        tft.setCursor (xl, y+=dy);
        tft.print (F("On at:"));
        tft.setCursor (xn, y+=dy);
        int hr_on = mins_on/60;
        int mn_on = mins_on%60;
        if (hr_on < 10)
            tft.print('0');
        tft.print(hr_on);
        tft.print(':');
        if (mn_on < 10)
            tft.print('0');
        tft.print(mn_on);

        // gap
        y += dy;

        // time off
        tft.setCursor (xl, y+=dy);
        if (support_dim)
            tft.print (F("Dim at:"));
        else
            tft.print (F("Off at:"));
        tft.setCursor (xn, y+=dy);
        int hr_off = mins_off/60;
        int mn_off = mins_off%60;
        if (hr_off < 10)
            tft.print('0');
        tft.print(hr_off);
        tft.print(':');
        if (mn_off < 10)
            tft.print('0');
        tft.print(mn_off);
}


/* save on_mins and off_mins for the given dow 1..7 Sun..Sat within persistent storage NV_DAILYONOFF.
 * N.B. we do not validate dow
 */
static void persistOnOffTimes (int dow, uint16_t on_mins, uint16_t off_mins)
{
    // internally we want 0-based
    dow -= 1;

    // retrieve current values
    uint16_t ootimes[NV_DAILYONOFF_LEN];
    NVReadString (NV_DAILYONOFF, (char*)ootimes);

    // merge
    ootimes[dow] = on_mins;
    ootimes[dow+DAYSPERWEEK] = off_mins;

    // save
    NVWriteString (NV_DAILYONOFF, (char*)ootimes);
}

/* set on/off from presistent storage NV_DAILYONOFF for the given week day 1..7 Sun..Sat
 * N.B. we do not validate dow
 */
static void getPersistentOnOffTimes (int dow, uint16_t &on, uint16_t &off)
{
    // internally we require 0..6
    dow -= 1;

    uint16_t ootimes[NV_DAILYONOFF_LEN];
    NVReadString (NV_DAILYONOFF, (char*)ootimes);
    on = ootimes[dow];
    off = ootimes[dow+DAYSPERWEEK];
}

/* given screen tap location known to be within NCDXF_b, allow user to change on/off/idle setting
 * N.B. coordinate layout with drawOnOffControls().
 */
static void changeOnOffSetting (const SCoord &s)
{
        // decide which row and which left-right half were tapped
        int dy = NCDXF_b.h/N_ROWS;
        int row = (s.y - NCDXF_b.y + dy/2 - SFONT_H/2)/dy;          // center of rows in drawOnOffControls
        bool left_half = s.x - NCDXF_b.x < NCDXF_b.w/2;

        // minutes deltas, always forward
        uint16_t on_dt = 0, off_dt = 0;

        switch (row) {
        case 3:
            // increase idle time
            idle_mins += 5;
            NVWriteUInt16 (NV_BR_IDLE, idle_mins);
            break;

        case 4:
            // decrease idle time but never below 0
            if (idle_mins > 0) {
                idle_mins = idle_mins < 5 ? 0 : idle_mins - 5;
                NVWriteUInt16 (NV_BR_IDLE, idle_mins);
            }
            break;

        case 6:
            if (left_half) {
                // increase on-time one hour
                on_dt = 60;
            } else {
                // increase on-time 5 minutes
                on_dt = 5;
            }
            break;

        case 7:
            if (left_half) {
                // decrease on-time one hour
                on_dt = MINSPERDAY - 60;
            } else {
                // decrease on-time 5 minutes
                on_dt = MINSPERDAY - 5;
            }
            break;

        case 9:
            if (left_half) {
                // increase off-time one hour
                off_dt = 60;
            } else {
                // increase off-time 5 minutes
                off_dt = 5;
            }
            break;

        case 10:
            if (left_half) {
                // decrease off-time one hour
                off_dt = MINSPERDAY - 60;
            } else {
                // decrease off-time 5 minutes
                off_dt = MINSPERDAY - 5;
            }
            break;

        default:
            return; 
        }

        if (on_dt || off_dt) {
            // update and save new values
            mins_on = (mins_on + on_dt) % MINSPERDAY;
            mins_off = (mins_off + off_dt) % MINSPERDAY;
            persistOnOffTimes (DEWeekday(), mins_on, mins_off);
        }

        // redraw with new settings
        drawOnOffControls();
}



/* check whether it is time to turn display on or off from the timers or idle timeout.
 * check idle timeout first, then honor on/off settings
 * N.B. we maintain sync with the weekly NV_DAILYONOFF and redraw controls if brb_mode == BRB_SHOW_ONOFF
 */
static void checkOnOffTimers()
{
        // check idle timeout first, if enabled
        if (idle_mins > 0) {
            uint16_t ims = (millis() - idle_t0)/60000;   // ms -> mins
            if (ims >= idle_mins && !clock_off) {
                Serial.println (F("BR: Idle timed out"));
                bpwm = user_off;
                engageDisplayBrightness(true);
                clock_off = true;
            }
        }

        // update on off times whenever DE's week day changes
        static int prev_dow = -1;
        int dow = DEWeekday();
        if (dow != prev_dow) {
            getPersistentOnOffTimes (dow, mins_on, mins_off);
            if (brb_mode == BRB_SHOW_ONOFF)
                drawOnOffControls();
            prev_dow = dow;
        }

        // ignore if on/off are the same
        if (mins_on == mins_off)
            return;

        // only check on/off times at top of each minute
        static time_t check_mins;
        time_t utc = nowWO();
        time_t utc_mins = utc/60;
        if (utc_mins == check_mins)
            return;
        check_mins = utc_mins;

        // check for time to turn on or off.
        // get local time
        time_t local = utc + de_tz.tz_secs;
        int hr = hour (local);
        int mn = minute (local);
        uint16_t mins_now = hr*60 + mn;

        // Serial.printf("idle %d now %d on %d off %d,bpwm %d\n",idle_mins,mins_now,mins_on,mins_off,bpwm);

        // engage when its time
        if (mins_now == mins_on) {
            if (bpwm != user_on) {
                Serial.println (F("BR: on"));
                bpwm = user_on;
                engageDisplayBrightness(true);
                clock_off = false;
                idle_t0 = millis();             // consider this a user action else will turn off again
            }
        } else if (mins_now == mins_off) {
            if (bpwm != user_off) {
                Serial.println (F("BR: off"));
                bpwm = user_off;
                engageDisplayBrightness(true);
                clock_off = true;
            }
        }
}



/* set brightness to bpwm and update GUI controls if visible.
 * N.B. beware states that should not be drawn
 */
static void engageDisplayBrightness(bool log)
{
        setDisplayBrightness(log);

        // Serial.printf (_FX("BR: engage mode %d\n"), brb_mode);

        if (getSWDisplayState() == SWD_NONE && !wifiMeterIsUp()) {
            if (brb_mode == BRB_SHOW_BR)
                drawBrControl();
            else if (brb_mode == BRB_SHOW_PHOT) {
                drawBrControl();
                drawPhotControl();
            }
        }
}


#if !defined(_WEB_ONLY) && (defined(_IS_LINUX_RPI) || defined(_USE_FB0))

/* return whether this is a linux RPi connected to a DSI display
 */
static bool isRPiDSI()
{
        static bool know;


        if (!know) {

            resetWatchdog();

            // try both
            dsi_path = NULL;
            int dsifd = open (dsi_path_buster, O_WRONLY);
            if (dsifd >= 0) {
                dsi_path = dsi_path_buster;
                close (dsifd);
            } else {
                dsifd = open (dsi_path_bullseye, O_WRONLY);
                if (dsifd >= 0) {
                    dsi_path = dsi_path_bullseye;
                    close (dsifd);
                }
            }

            // report
            if (dsi_path)
                Serial.printf (_FX("BR: found DSI display at %s\n"), dsi_path);
            else
                Serial.print (_FX("BR: no DSI display\n"));

            // don't have to test again
            know = true;
        }

        return (dsi_path != NULL);
}

#endif


/* return whether the display hardware brightness can be controlled.
 * intended for external use, set flag for internal use.
 */
bool brControlOk()
{
        #if defined(_WEB_ONLY)
            support_dim = false;                                // never via web
        #elif defined(_IS_ESP8266)
            support_dim = true;                                 // always works
        #elif defined(_USE_FB0)
            support_dim = isRPiDSI();                           // only if DSI
        #elif defined(_IS_LINUX_RPI)
            support_dim = getX11FullScreen() && isRPiDSI();     // only if DSI and running full screen
        #else
            support_dim = false;
        #endif

        return (support_dim);
}

/* return whether display hardware support being turned on/off.
 * intended for external use, set flag for internal use.
 */
bool brOnOffOk()
{
        #if defined(_WEB_ONLY)
            support_onoff = false;                              // never via web
        #elif defined(_IS_ESP8266)
            support_onoff = true;                               // always works
        #elif defined(_USE_FB0)
            support_onoff = true;                               // always works
        #elif defined(_IS_LINUX_RPI)
            support_onoff = getX11FullScreen();                 // works if full screen
        #else
            support_onoff = false;
        #endif

        return (support_onoff);
}

/* return whether we support having a photoresistor connected
 */
static bool photOk()
{
        // determine photoresistor support, need ADC only on ESP
        #if defined(_IS_ESP8266)
            support_phot = true;
        #else
            support_phot = false;
        #endif

        return (support_phot);
}

/* call this ONCE before Setup to determine hardware and set full brightness for now,
 * then call setupBrightness() ONCE after Setup to commence with user's brightness settings.
 */
void initBrightness()
{
        // once
        static bool before;
        if (before)
            return;
        before = true;

        resetWatchdog();

        // determine initial hw capabilities, might change depending on Setup
        (void) brControlOk();
        (void) brOnOffOk();
        (void) photOk();

        // log
        Serial.printf (_FX("BR: 0 onoff= %d dim= %d phot= %d\n"), support_onoff, support_dim, support_phot);

        // check whether photo resistor is connected: discard first read then spin up the blend
        (void) readPhot();
        for (uint8_t i = 0; i < 10; i++)
            (void) readPhot();
        phot = readPhot();
        found_phot = phot > 1;  // in case they ever fix the range bug
        Serial.printf (_FX("BR: phot %d %s\n"), phot, found_phot ? "found" : "not found");

        // full on for now
        bpwm = BPWM_MAX;
        setDisplayBrightness(true);
}

/* call this ONCE after Setup to commence with user's brightness controls and on/off times.
 */
void setupBrightness()
{
        // once
        static bool before;
        if (before)
            return;
        before = true;

        resetWatchdog();

        // final check of hw capabilities after Setup.
        (void) brControlOk();
        (void) brOnOffOk();
        (void) photOk();

        // log
        Serial.printf (_FX("BR: 1 onoff= %d dim= %d phot= %d\n"), support_onoff, support_dim, support_phot);

        // init to user's full brightness
        user_on = (getBrMax()*BPWM_MAX+50)/100;         // round
        user_off = (getBrMin()*BPWM_MAX+50)/100;
        bpwm = user_on;
        setDisplayBrightness(true);
        clock_off = false;

        // init idle time and period
        idle_t0 = millis();
        if (!NVReadUInt16 (NV_BR_IDLE, &idle_mins)) {
            idle_mins = 0;
            NVWriteUInt16 (NV_BR_IDLE, idle_mins);
        }

        // retrieve fast copies, init if first time, honor user settings

        if (!NVReadUInt16 (NV_BPWM_BRIGHT, &fast_bpwm_bright) || fast_bpwm_bright > user_on)
            fast_bpwm_bright = user_on;
        if (!NVReadUInt16 (NV_BPWM_DIM, &fast_bpwm_dim) || fast_bpwm_dim < user_off)
            fast_bpwm_dim = user_off;
        if (fast_bpwm_bright <= fast_bpwm_dim) {
            // new user range is completely outside 
            fast_bpwm_bright = user_on;
            fast_bpwm_dim = user_off;
        }
        NVWriteUInt16 (NV_BPWM_BRIGHT, fast_bpwm_bright);
        NVWriteUInt16 (NV_BPWM_DIM, fast_bpwm_dim);

        if (!NVReadUInt16 (NV_PHOT_BRIGHT, &fast_phot_bright)) {
            fast_phot_bright = PHOT_MAX;
            NVWriteUInt16 (NV_PHOT_BRIGHT, fast_phot_bright);
        }
        if (!NVReadUInt16 (NV_PHOT_DIM, &fast_phot_dim)) {
            fast_phot_dim = 0;
            NVWriteUInt16 (NV_PHOT_DIM, fast_phot_dim);
        }

        // get display mode, reset to something benign if no longer appropriate
        if (!NVReadUInt8 (NV_BRB_ROTSET, &brb_rotset) 
                    || (brb_rotset & (1<<brb_mode)) == 0
                    || ((brb_rotset & (1 << BRB_SHOW_ONOFF)) && !support_onoff)
                    || ((brb_rotset & (1 << BRB_SHOW_PHOT)) && (!support_phot || !found_phot))
                    || ((brb_rotset & (1 << BRB_SHOW_BR)) && (!support_dim || (support_phot && found_phot)))){
            Serial.printf (_FX("BR: Resetting initial brb_reset 0x%x to 0x%x\n"),
                        brb_rotset, 1<<BRB_SHOW_SWSTATS);
            brb_mode = BRB_SHOW_SWSTATS;
            brb_rotset = 1 << BRB_SHOW_SWSTATS;
            NVWriteUInt8 (NV_BRB_ROTSET, brb_rotset);
        }
        logBRBRotSet();
}

/* draw any of the brightness controls in NCDXF_b.
 */
void drawBrightness()
{
        switch (brb_mode) {

        case BRB_SHOW_ONOFF:
            drawOnOffControls();
            break;

        case BRB_SHOW_PHOT:
            drawBrControl();
            drawPhotControl();
            drawPhotSymbol();
            break;

        case BRB_SHOW_BR:
            drawBrControl();
            break;

        default:
            break;
        }
}


/* set display brightness according to current photo detector and check clock settings
 */
void followBrightness()
{
        resetWatchdog();

        if (support_onoff)
            checkOnOffTimers();

        if (support_phot && found_phot && !clock_off) {

            // not too fast (eg, while not updating map after new DE)
            static uint32_t prev_m;
            if (!timesUp (&prev_m, FOLLOW_DT))
                return;

            // save current 
            uint16_t prev_phot = phot;
            int16_t prev_bpwm = bpwm;

            // update mean with new phot reading if connected
            // linear interpolate between dim and bright limits to find new brightness
            phot = readPhot();
            int32_t del_phot = phot - fast_phot_dim;
            int32_t bpwm_range = fast_bpwm_bright - fast_bpwm_dim;
            int32_t phot_range = fast_phot_bright - fast_phot_dim;
            if (phot_range == 0)
                phot_range = 1;         // avoid /0
            int16_t new_bpwm = fast_bpwm_dim + bpwm_range * del_phot / phot_range;
            if (new_bpwm < 0)
                new_bpwm = 0;
            else if (new_bpwm > BPWM_MAX)
                new_bpwm = BPWM_MAX;
            // smooth update
            bpwm = BPWM_BLEND*new_bpwm + (1-BPWM_BLEND)*bpwm + 0.5F;
            if (bpwm < user_off)
                bpwm = user_off;
            if (bpwm > user_on)
                bpwm = user_on;

            // draw even if bpwm doesn't change but phot changed some, such as going above fast_phot_bright
            bool phot_changed = (phot>prev_phot && phot-prev_phot>30) || (phot<prev_phot && prev_phot-phot>30);

            // engage if either changed
            if (bpwm != prev_bpwm || phot_changed)
                engageDisplayBrightness(false);

            // #define _DEBUG_BRIGHTNESS
            #ifdef _DEBUG_BRIGHTNESS

                Serial.print("follow");
                Serial.print ("\tPHOT:\t");
                    Serial.print (phot); Serial.print('\t');
                    Serial.print(fast_phot_dim); Serial.print(" .. "); Serial.print(fast_phot_bright);
                Serial.print ("\tBPWM:\t");
                    Serial.print(bpwm); Serial.print('\t');
                    Serial.print(fast_bpwm_dim); Serial.print(" .. "); Serial.println(fast_bpwm_bright);
            #endif // _DEBUG_BRIGHTNESS
        }

}

/* called on any tap anywhere to insure screen is on and reset idle_t0.
 * return whether we were off prior to tap.
 */
bool brightnessOn()
{
        idle_t0 = millis();

        if (clock_off) {
            Serial.println (F("display on"));
            bpwm = user_on;
            engageDisplayBrightness(true);
            clock_off = false;
            return (true);
        } else
            return (false);
}

/* turn screen off.
 */
void brightnessOff()
{
        Serial.println (F("display off"));
        bpwm = user_off;
        engageDisplayBrightness(true);
        clock_off = true;
}

/* given a tap within NCDXF_b, change brightness or clock setting
 */
static void changeBrightness (const SCoord &s)
{
        if (brb_mode == BRB_SHOW_PHOT) {

            SBox b;
            getBrControl (b);

            // set brightness directly from tap location within allowed range
            if (s.y < b.y)
                bpwm = user_on;
            else if (s.y > b.y + b.h)
                bpwm = user_off;
            else
                bpwm = user_off + (user_on-user_off)*(b.y + b.h - s.y)/b.h;

            // redefine upper or lower range, whichever is closer
            if (phot > (fast_phot_bright+fast_phot_dim)/2) {
                // change bright end
                fast_bpwm_bright = bpwm;
                fast_phot_bright = phot;

                // persist
                NVWriteUInt16 (NV_BPWM_BRIGHT, fast_bpwm_bright);
                NVWriteUInt16 (NV_PHOT_BRIGHT, fast_phot_bright);
             } else {
                // change dim end
                fast_bpwm_dim = bpwm;
                fast_phot_dim = phot;

                // persist
                NVWriteUInt16 (NV_BPWM_DIM, fast_bpwm_dim);
                NVWriteUInt16 (NV_PHOT_DIM, fast_phot_dim);
            }

            engageDisplayBrightness(true);

            // Serial.printf (_FX("BR: bpwm: %4d < %4d < %4d phot: %4d < %4d < %4d\n"),
                                    // fast_bpwm_dim, bpwm, fast_bpwm_bright,
                                    // fast_phot_dim, phot, fast_phot_bright);

        }

        else if (brb_mode == BRB_SHOW_BR) {

            SBox b;
            getBrControl (b);

            // set brightness directly from tap location within allowed range
            if (s.y < b.y)
                bpwm = user_on;
            else if (s.y > b.y + b.h)
                bpwm = user_off;
            else
                bpwm = user_off + (user_on-user_off)*(b.y + b.h - s.y)/b.h;

            // update scale and engage
            engageDisplayBrightness(true);

        }

        else if (brb_mode == BRB_SHOW_ONOFF) {

            changeOnOffSetting (s);

        }

}

/* perform proper action given s known to be within NCDXF_b.
 */
void doNCDXFBoxTouch (const SCoord &s)
{
    if (s.y < NCDXF_b.y + NCDXF_b.h/10) {

        // tapped near the top so show menu of options, or toggle if only 2

        // this list of each BRB is to avoid knowing their values, nice BUT must be in same order as mitems[]
        static uint8_t mi_brb_order[BRB_N] = {
            BRB_SHOW_BEACONS, BRB_SHOW_SWSTATS, BRB_SHOW_ONOFF, BRB_SHOW_PHOT, BRB_SHOW_BR,
            BRB_SHOW_BME76, BRB_SHOW_BME77
        };


        // build menu, depending on current configuration
        #define _MI_INDENT 2
        MenuItem mitems[BRB_N] = {
             {MENU_AL1OFN, (bool)(brb_rotset & (1 << BRB_SHOW_BEACONS)), 1, _MI_INDENT,
                                brb_names[BRB_SHOW_BEACONS]},
             {MENU_AL1OFN, (bool)(brb_rotset & (1 << BRB_SHOW_SWSTATS)), 1, _MI_INDENT,
                                brb_names[BRB_SHOW_SWSTATS]},  // always
             {support_onoff ? MENU_AL1OFN : MENU_IGNORE,
                        (bool)(brb_rotset & (1 << BRB_SHOW_ONOFF)), 1, _MI_INDENT, brb_names[BRB_SHOW_ONOFF]},
             {support_phot && found_phot ? MENU_AL1OFN : MENU_IGNORE,
                        (bool)(brb_rotset & (1 << BRB_SHOW_PHOT)), 1, _MI_INDENT, brb_names[BRB_SHOW_PHOT]},
             {support_dim && !(support_phot && found_phot) ? MENU_AL1OFN : MENU_IGNORE,
                        (bool)(brb_rotset & (1 << BRB_SHOW_BR)), 1, _MI_INDENT, brb_names[BRB_SHOW_BR]},
             {getBMEData(BME_76,false) != NULL ? MENU_AL1OFN : MENU_IGNORE,
                        (bool)(brb_rotset & (1 << BRB_SHOW_BME76)), 1, _MI_INDENT, brb_names[BRB_SHOW_BME76]},
             {getBMEData(BME_77,false) != NULL ? MENU_AL1OFN : MENU_IGNORE,
                        (bool)(brb_rotset & (1 << BRB_SHOW_BME77)), 1, _MI_INDENT, brb_names[BRB_SHOW_BME77]},
        };

        // boxes
        SBox menu_b = NCDXF_b;                      // copy
        menu_b.y += 20;
        SBox ok_b;

        // run menu
        MenuInfo menu = {menu_b, ok_b, true, true, 1, BRB_N, mitems};   // no room for cancel
        bool ok = runMenu(menu);

        // engage new option unless canceled
        if (ok) {

            // build new rotset
            brb_rotset = 0;
            for (int i = 0; i < BRB_N; i++)
                if (mitems[i].set)
                    brb_rotset |= (1 << mi_brb_order[i]);

            // if brb_mode is not in new set, just pick one
            if ((brb_rotset & (1 << brb_mode)) == 0) {
                for (int i = 0; i < BRB_N; i++) {
                    if (brb_rotset & (1 << i)) {
                        brb_mode = i;
                        break;
                    }
                }
            }

            // make a note
            logBRBRotSet();

            // match beacons to new state
            updateBeacons (true);

            // update on/off times if now used
            if (brb_mode == BRB_SHOW_ONOFF)
                getPersistentOnOffTimes (DEWeekday(), mins_on, mins_off);
        }

        // show new option, even if no change in order to erase menu
        drawNCDXFBox();

        // save
        NVWriteUInt8 (NV_BRB_ROTSET, brb_rotset);
        Serial.printf (_FX("BR: now mode %d\n"), brb_mode);

    } else {

        // tapped below title so pass to appropriate handler

        switch ((BRB_MODE)brb_mode) {

        case BRB_SHOW_BEACONS:

        case BRB_SHOW_ONOFF:    // fallthru
        case BRB_SHOW_PHOT:     // fallthru
        case BRB_SHOW_BR:
            changeBrightness (s);
            break;

        case BRB_SHOW_SWSTATS:
            doSpaceStatsTouch (s);
            break;

        case BRB_SHOW_BME76:
        case BRB_SHOW_BME77:
            doBMETouch (s);
            break;

        case BRB_N:
            // lint
            break;
        }
    }
}


/* set on/off/idle times for the given dow then update display if today is dow.
 * times are minutes since DE midnight; idle is mins or ignored if < 0; dow is 1..7 Sun..Sat else "today".
 * since we only allow changing idle by multiples of 5, we enforce that here and caller can get results.
 * N.B. this does NOT count as a new user interaction for determining idle timeout.
 * N.B. we do NOT validate args
 * return whether even implemented.
 */
bool setDisplayOnOffTimes (int dow, uint16_t new_on, uint16_t new_off, int &idle)
{
    if (support_onoff) {

        // persist new on/off
        persistOnOffTimes (dow, new_on, new_off);

        // engage and persist new idle time if desired
        if (idle >= 0) {
            // enforce multiples of 5
            idle -= idle%5;
            // FYI: set idle_t0 too if you want this to count as a tap
            idle_mins = idle;
            NVWriteUInt16 (NV_BR_IDLE, idle_mins);
        }

        // engage and display to confirm if showing today
        if (brb_mode == BRB_SHOW_ONOFF && (idle >= 0 || dow == DEWeekday())) {
            mins_on = new_on;
            mins_off = new_off;
            drawOnOffControls();
        }

        return (true);

    } else {

        return (false);

    }
}

/* return clock timer settings and whether supported
 */
bool getDisplayInfo (uint16_t &percent, uint16_t &idle_min, uint16_t &idle_left_sec)
{
    if (support_onoff) {
        percent = 100*bpwm/BPWM_MAX;
        idle_min = idle_mins;

        uint16_t idle_dt_s = (millis() - idle_t0)/1000;
        idle_left_sec = idle_mins*60 > idle_dt_s ? idle_mins*60 - idle_dt_s: 0;

        return (true);

    } else {

        return (false);

    }
}

/* return on and off times for the given dow 1..7 Sun..Sat.
 * times are minutes 0 .. 24*60-1
 * N.B. we do NOT validate args
 * return whether even implemented.
 */
bool getDisplayOnOffTimes (int dow, uint16_t &on, uint16_t &off)
{
    if (support_onoff) {
        getPersistentOnOffTimes (dow, on, off);
        return (true);
    } else {
        return (false);
    }
}


/* call to force full brightness, for example just before shutting down.
 */
void setFullBrightness()
{
    bpwm = BPWM_MAX;
    setDisplayBrightness (true);
}
