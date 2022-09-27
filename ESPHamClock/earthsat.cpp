/* manage selection and display of one earth sat.
 *
 * we call "pass" the overhead view shown in dx_info_b, "path" the orbit shown on the map.
 *
 * N.B. our satellite info server changes blanks to underscores in sat names.
 */


#include "HamClock.h"

bool dx_info_for_sat;                   // global to indicate whether dx_info_b is for DX info or sat info

// path drawing
#if defined(_IS_ESP8266)
    // this requires dense collection of individual dots that are plotted as the map is drawn
    #define MAX_PATH    2000            // max number of points in orbit path
    #define FOOT_ALT0   1000            // n dots for 0 deg altitude locus
    #define FOOT_ALT30  300             // n dots for 30 deg altitude locus
    #define FOOT_ALT60  100             // n dots for 60 deg altitude locus
#else
    // much sparser because here draw lines all at once after the map
    #define MAX_PATH    250             // max number of points in orbit path
    #define FOOT_ALT0   100             // n dots for 0 deg altitude locus
    #define FOOT_ALT30  35              // n dots for 30 deg altitude locus
    #define FOOT_ALT60  12              // n dots for 60 deg altitude locus
#endif
#define N_FOOT      3                   // number of footprint altitude loci


// config
#define ALARM_DT        (1.0F/1440.0F)  // flash this many days before an event
#define RISING_RATE     1               // flash at this rate when sat about to rise
#define SETTING_RATE    10              // flash at this rate when sat about to set
#define MAX_TLE_AGE     7.0F            // max age to use a TLE, days (except moon)
#define TLE_REFRESH     (3600*6)        // freshen TLEs this often, seconds
#define SAT_TOUCH_R     20U             // touch radius, pixels
#define SAT_UP_R        2               // dot radius when up
#define PASS_STEP       10.0F           // pass step size, seconds
#define TBORDER         50              // top border
#define FONT_H          (dx_info_b.h/6) // font height
#define FONT_D          5               // font descent
#define SAT_COLOR       RA8875_RED      // overall annotation color
#define SATUP_COLOR     RGB565(0,200,0) // time color when sat is up
#define SOON_COLOR      RGB565(200,0,0) // table text color for pass soon
#define SOON_MINS       10              // "soon", minutes
#define CB_SIZE         20              // size of selection check box
#define CELL_H          32              // display cell height
#define N_COLS          4               // n cols in name table
#define Q_TO            5               // question timeout
#define CELL_W          (tft.width()/N_COLS)                    // display cell width
#define N_ROWS          ((tft.height()-TBORDER)/CELL_H)         // n rows in name table
#define MAX_NSAT        (N_ROWS*N_COLS)                         // max names we can display
#define MAX_PASS_STEPS  30              // max lines to draw for pass map

// used so findNextPass() can be used for contexts other than the current sat now
// TODO: make another for az/el/range/rate and use them with getSatAzElNow()
typedef struct {
    DateTime rise_time, set_time;       // next pass times
    bool rise_ok, set_ok;               // whether rise_time and set_time are valid
    float rise_az, set_az;              // rise and set az, degrees, if valid
    bool ever_up, ever_down;            // whether sat is ever above or below SAT_MIN_EL in next day
} SatRiseSet;


// state
static const char sat_get_all[] PROGMEM = "/esats.pl?getall=";     // command to get all TLE
static const char sat_one_page[] = "/esats.pl?tlename=%s"; // command to get one TLE
static Satellite *sat;                  // satellite definition, if any
static Observer *obs;                   // DE
static SatRiseSet sat_rs;               // event info for current sat
static SCoord *sat_path;                // mallocd screen coords for orbit, first always now, Moon only 1
static uint16_t n_path;                 // actual number in use
static SCoord *sat_foot[3];             // mallocd screen coords for each footprint altitude
static const uint16_t max_foot[N_FOOT] = {FOOT_ALT0, FOOT_ALT30, FOOT_ALT60};   // max dots on each altitude 
static const float foot_alts[N_FOOT] = {0.0F, 30.0F, 60.0F};                    // alt of each segment
static uint16_t n_foot[N_FOOT];         // actual dots along each altitude 
static SBox map_name_b;                 // location of sat name on map
static SBox ok_b = {730,10,55,35};      // Ok button
static char sat_name[NV_SATNAME_LEN];   // NV_SATNAME cache (spaces are underscores)
#define SAT_NAME_IS_SET()               (sat_name[0])           // whether there is a sat name defined
static time_t tle_refresh;              // last TLE update
static bool new_pass;                   // set when new pass is ready


/* copy from_str to to_str up to maxlen, changing all from_char to to_char
 */
void strncpySubChar (char to_str[], const char from_str[], char to_char, char from_char, int maxlen)
{
    int l = 0;
    char c;

    while (l++ < maxlen && (c = *from_str++) != '\0')
        *to_str++ = c == from_char ? to_char : c;
    *to_str = '\0';
}

#if defined(_SUPPORT_GPIO) and defined(_IS_UNIX)

#include <pthread.h>

static volatile int gpio_rate_hz;              // set my main thread, read by gpioThread

/* return all IO pins to quiescent state
 */
void satResetIO()
{
    if (!GPIOOk())
        return;
    GPIO& gpio = GPIO::getGPIO();
    if (!gpio.isReady())
        return;
    gpio.setAsInput (SATALARM_GPIO);
}

/* thread that repeatedly reads gpio_rate as a desired rate in Hz
 * and controls GPIO pin SATALARM_GPIO
 */
static void * gpioThread (void *vp)
{
    (void) vp;

    // attach to GPIO, init output
    GPIO &gpio = GPIO::getGPIO();
    gpio.setAsOutput (SATALARM_GPIO);
    gpio.setLo (SATALARM_GPIO);

    // set our internal polling rate and init counter there of.
    useconds_t delay_us = 100000;               // N.B. sets maximum rate that can be achieved
    unsigned n_delay = 0;

    // forever check and implement what gpio_rate_hz wants
    for(;;) {
        usleep (delay_us);
        if (gpio_rate_hz < 0) {
            gpio.setLo (SATALARM_GPIO);
            n_delay = 0;
        } else if (gpio_rate_hz == 0) {
            gpio.setHi (SATALARM_GPIO);
            n_delay = 0;
        } else {
            unsigned rate_period_us = 1000000U/gpio_rate_hz;
            if (++n_delay*delay_us >= rate_period_us) {
                gpio.setHiLo (SATALARM_GPIO, !gpio.readPin(SATALARM_GPIO));
                n_delay = 0;
            }
        }
    }
}

/* set alarm buzzer SATALARM_GPIO according to rate -- RPi only.
 * rate < 0  : off
 * rate == 0 : one
 * rate > 0  : flash at this Hz
 */
static void risetAlarm (int rate)
{
    // ignore if not supposed to use GPIO
    if (!GPIOOk())
        return;

    // init helper thread if first time.
    static bool gpiot_started;
    if (!gpiot_started) {
        gpiot_started = true;   // dont repeat even if thread err
        pthread_t tid;
        int e = pthread_create (&tid, NULL, gpioThread, NULL);
        if (e != 0)
            Serial.printf (_FX("GPIO thread err: %s\n"), strerror(e));
    }

    // tell helper thread what we want done
    gpio_rate_hz = rate;
}

#else

// dummy
static void risetAlarm (int rate)
{
    // not used
    (void) rate;
}

#endif // _SUPPORT_GPIO



/* completely undefine the current sat
 */
static void unsetSat()
{
    if (sat) {
        delete sat;
        sat = NULL;
    }
    if (sat_path) {
        free (sat_path);
        sat_path = NULL;
    }
    for (int i = 0; i < N_FOOT; i++) {
        if (sat_foot[i]) {
            free (sat_foot[i]);
            sat_foot[i] = NULL;
        }
    }

    sat_name[0] = '\0';
    NVWriteString (NV_SATNAME, sat_name);
    dx_info_for_sat = false;

    risetAlarm (-1);
}

/* fill sat_foot with loci of points that see the sat at various viewing altitudes.
 * N.B. call this before updateSatPath malloc's its memory
 */
static void updateFootPrint (float satlat, float satlng)
{
    resetWatchdog();

    // complement of satlat
    float cosc = sinf(satlat);
    float sinc = cosf(satlat);

    // fill each segment along each altitude
    for (uint8_t alt_i = 0; alt_i < N_FOOT; alt_i++) {

        // start with max n points
        int n_malloc = max_foot[alt_i]*sizeof(SCoord);
        SCoord *foot_path = sat_foot[alt_i] = (SCoord *) realloc (sat_foot[alt_i], n_malloc);
        if (!foot_path && n_malloc > 0)
            fatalError (_FX("sat_foot: %d"), n_malloc);

        // satellite viewing altitude
        float valt = deg2rad(foot_alts[alt_i]);

        // great-circle radius from subsat point to viewing circle at altitude valt
        float vrad = sat->viewingRadius(valt);

        // compute each unique point around viewing circle
        uint16_t n = 0;
        uint16_t m = max_foot[alt_i];
        for (uint16_t foot_i = 0; foot_i < m; foot_i++) {

            // compute next point
            float cosa, B;
            float A = foot_i*2*M_PI/m;
            solveSphere (A, vrad, cosc, sinc, &cosa, &B);
            float vlat = M_PIF/2-acosf(cosa);
            float vlng = fmodf(B+satlng+5*M_PIF,2*M_PIF)-M_PIF; // require [-180.180)
            ll2s (vlat, vlng, foot_path[n], 2);

            // skip duplicate points
            if (n == 0 || memcmp (&foot_path[n], &foot_path[n-1], sizeof(SCoord)))
                n++;
        }

        // reduce memory to only points actually used
        n_foot[alt_i] = n;
        sat_foot[alt_i] = (SCoord *) realloc (sat_foot[alt_i], n*sizeof(SCoord));
        // Serial.printf (_FX("alt %g: n_foot %u / %u\n"), foot_alts[alt_i], n, m);
    }
}

/* return a DateTime for the given time
 */
static DateTime userDateTime(time_t t)
{
    int yr = year(t);
    int mo = month(t);
    int dy = day(t);
    int hr = hour(t);
    int mn = minute(t);
    int sc = second(t);

    DateTime dt(yr, mo, dy, hr, mn, sc);

    return (dt);
}

/* find next rise and set times if sat valid starting from the given time_t.
 * always find rise and set in the future, so set_time will be < rise_time iff pass is in progress.
 * also update flags ever_up, set_ok, ever_down and rise_ok.
 * name is only used for local logging, set to NULL to avoid even this.
 */
static void findNextPass(const char *name, time_t t, SatRiseSet &rs)
{
    if (!sat || !obs) {
        rs.set_ok = rs.rise_ok = false;
        return;
    }

    // measure how long this takes
    uint32_t t0 = millis();

    #define COARSE_DT   90L             // seconds/step forward for fast search
    #define FINE_DT     (-2L)           // seconds/step backward for refined search
    float pel;                          // previous elevation
    long dt = COARSE_DT;                // search time step size, seconds
    DateTime t_now = userDateTime(t);         // search starting time
    DateTime t_srch = t_now + -FINE_DT; // search time, start beyond any previous solution
    float tel, taz, trange, trate;      // target el and az, degrees

    // init pel and make first step
    sat->predict (t_srch);
    sat->topo (obs, pel, taz, trange, trate);
    t_srch += dt;

    // search up to a few days ahead for next rise and set times (for example for moon)
    rs.set_ok = rs.rise_ok = false;
    rs.ever_up = rs.ever_down = false;
    while ((!rs.set_ok || !rs.rise_ok) && t_srch < t_now + 2.0F) {
        resetWatchdog();

        // find circumstances at time t_srch
        sat->predict (t_srch);
        sat->topo (obs, tel, taz, trange, trate);

        // check for rising or setting events
        if (tel >= SAT_MIN_EL) {
            rs.ever_up = true;
            if (pel < SAT_MIN_EL) {
                if (dt == FINE_DT) {
                    // found a refined set event (recall we are going backwards),
                    // record and resume forward time.
                    rs.set_time = t_srch;
                    rs.set_az = taz;
                    rs.set_ok = true;
                    dt = COARSE_DT;
                    pel = tel;
                } else if (!rs.rise_ok) {
                    // found a coarse rise event, go back slower looking for better set
                    dt = FINE_DT;
                    pel = tel;
                }
            }
        } else {
            rs.ever_down = true;
            if (pel > SAT_MIN_EL) {
                if (dt == FINE_DT) {
                    // found a refined rise event (recall we are going backwards).
                    // record and resume forward time but skip if set is within COARSE_DT because we
                    // would jump over it and find the NEXT set.
                    float check_tel, check_taz;
                    DateTime check_set = t_srch + COARSE_DT;
                    sat->predict (check_set);
                    sat->topo (obs, check_tel, check_taz, trange, trate);
                    if (check_tel >= SAT_MIN_EL) {
                        rs.rise_time = t_srch;
                        rs.rise_az = taz;
                        rs.rise_ok = true;
                    }
                    // regardless, resume forward search
                    dt = COARSE_DT;
                    pel = tel;
                } else if (!rs.set_ok) {
                    // found a coarse set event, go back slower looking for better rise
                    dt = FINE_DT;
                    pel = tel;
                }
            }
        }

        // Serial.printf (_FX("R %d S %d dt %ld from_now %8.3fs tel %g\n"), rs.rise_ok, rs.set_ok, dt, SECSPERDAY*(t_srch - t_now), tel);

        // advance time and save tel
        t_srch += dt;
        pel = tel;
    }

    // new pass ready
    new_pass = true;

    if (name) {
        Serial.printf (_FX("%s: next rise in %g hrs, set in %g (%ld ms)\n"), name,
                rs.rise_ok ? 24*(rs.rise_time - t_now) : 0.0F, rs.set_ok ? 24*(rs.set_time - t_now) : 0.0F,
                millis() - t0);
        printFreeHeap (F("findNextPass"));
    }

}

/* display next pass on sky dome.
 * N.B. we assume findNextPass has been called to fill sat_rs
 */
static void drawNextPass()
{
    resetWatchdog();

    // size and center of screen path
    uint16_t r0 = satpass_c.r;
    uint16_t xc = satpass_c.s.x;
    uint16_t yc = satpass_c.s.y;

    // erase
    tft.fillRect (dx_info_b.x+1, dx_info_b.y+2*FONT_H+1,
            dx_info_b.w-2, dx_info_b.h-2*FONT_H+1, RA8875_BLACK);

    // skip if no sat or never up
    if (!sat || !obs || !sat_rs.ever_up)
        return;

    // find n steps, step duration and starting time
    bool full_pass = false;
    int n_steps = 0;
    float step_dt = 0;
    DateTime t;

    if (sat_rs.rise_ok && sat_rs.set_ok) {

        // find start and pass duration in days
        float pass_duration = sat_rs.set_time - sat_rs.rise_time;
        if (pass_duration < 0) {
            // rise after set means pass is underway so start now for remaining duration
            DateTime t_now = userDateTime(nowWO());
            pass_duration = sat_rs.set_time - t_now;
            t = t_now;
        } else {
            // full pass so start at next rise
            t = sat_rs.rise_time;
            full_pass = true;
        }

        // find step size and number of steps
        n_steps = pass_duration/(PASS_STEP/SECSPERDAY) + 1;
        if (n_steps > MAX_PASS_STEPS)
            n_steps = MAX_PASS_STEPS;
        step_dt = pass_duration/n_steps;

    } else {

        // it doesn't actually rise or set within the next 24 hour but it's up some time 
        // so just show it at its current position (if it's up)
        n_steps = 1;
        step_dt = 0;
        t = userDateTime(nowWO());
    }

    // draw horizon and compass points
    #define HGRIDCOL RGB565(50,90,50)
    tft.drawCircle (xc, yc, r0, BRGRAY);
    for (float a = 0; a < 2*M_PIF; a += M_PIF/6) {
        uint16_t xr = lroundf(xc + r0*cosf(a));
        uint16_t yr = lroundf(yc - r0*sinf(a));
        tft.fillCircle (xr, yr, 1, RA8875_WHITE);
        tft.drawLine (xc, yc, xr, yr, HGRIDCOL);
    }

    // draw elevations
    for (uint8_t el = 30; el < 90; el += 30)
        tft.drawCircle (xc, yc, r0*(90-el)/90, HGRIDCOL);

    // label sky directions
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (BRGRAY);
    tft.setCursor (xc - r0, yc - r0 + 2);
    tft.print (F("NW"));
    tft.setCursor (xc + r0 - 12, yc - r0 + 2);
    tft.print (F("NE"));
    tft.setCursor (xc - r0, yc + r0 - 8);
    tft.print (F("SW"));
    tft.setCursor (xc + r0 - 12, yc + r0 - 8);
    tft.print (F("SE"));

    // connect several points from t until sat_rs.set_time, find max elevation for labeling
    float max_el = 0;
    uint16_t max_el_x = 0, max_el_y = 0;
    uint16_t prev_x = 0, prev_y = 0;
    for (uint8_t i = 0; i < n_steps; i++) {
        resetWatchdog();

        // find topocentric position @ t
        float el, az, range, rate;
        sat->predict (t);
        sat->topo (obs, el, az, range, rate);
        if (el < 0 && n_steps == 1)
            break;                                      // only showing pos now but it's down

        // find screen postion
        float r = r0*(90-el)/90;                        // screen radius, zenith at center 
        uint16_t x = xc + r*sinf(deg2rad(az)) + 0.5F;   // want east right
        uint16_t y = yc - r*cosf(deg2rad(az)) + 0.5F;   // want north up

        // find max el
        if (el > max_el) {
            max_el = el;
            max_el_x = x;
            max_el_y = y;
        }

        // connect if have prev or just dot if only one
        if (i > 0 && (prev_x != x || prev_y != y))      // avoid bug with 0-length line
            tft.drawLine (prev_x, prev_y, x, y, SAT_COLOR);
        else if (n_steps == 1)
            tft.fillCircle (x, y, SAT_UP_R, SAT_COLOR);

        // label the set end if last step of several and full pass
        if (full_pass && i == n_steps - 1) {
            // x,y is very near horizon, try to move inside a little for clarity
            x += x > xc ? -12 : 2;
            y += y > yc ? -8 : 4;
            tft.setCursor (x, y);
            tft.print('S');
        }

        // save
        prev_x = x;
        prev_y = y;

        // next t
        t += step_dt;
    }

    // label max elevation and time up iff we have a full pass
    if (max_el > 0 && full_pass) {

        // max el
        uint16_t x = max_el_x, y = max_el_y;
        bool draw_left_of_pass = max_el_x > xc;
        bool draw_below_pass = max_el_y < yc;
        x += draw_left_of_pass ? -30 : 20;
        y += draw_below_pass ? 5 : -18;
        tft.setCursor (x, y); 
        tft.print(max_el, 0);
        tft.drawCircle (tft.getCursorX()+2, tft.getCursorY(), 1, BRGRAY);       // simple degree symbol

        // pass duration
        int s_up = (sat_rs.set_time - sat_rs.rise_time)*SECSPERDAY;
        char tup_str[32];
        if (s_up >= 3600) {
            int h = s_up/3600;
            int m = (s_up - 3600*h)/60;
            snprintf (tup_str, sizeof(tup_str), _FX("%dh%02d"), h, m);
        } else {
            int m = s_up/60;
            int s = s_up - 60*m;
            snprintf (tup_str, sizeof(tup_str), _FX("%d:%02d"), m, s);
        }
        uint16_t bw = getTextWidth (tup_str);
        if (draw_left_of_pass)
            x = tft.getCursorX() - bw + 4;                                  // account for deg
        y += draw_below_pass ? 12 : -11;
        tft.setCursor (x, y);
        tft.print(tup_str);
    }

    printFreeHeap (F("drawNextPass"));
}

/* draw name of current satellite if used in dx_info box
 */
static void drawSatName()
{
    if (!sat || !obs || !SAT_NAME_IS_SET() || !dx_info_for_sat)
        return;

    resetWatchdog();

    // retrieve saved name without '_'
    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, sat_name, ' ', '_', NV_SATNAME_LEN);

    // erase
    tft.fillRect (dx_info_b.x, dx_info_b.y+1, dx_info_b.w, dx_info_b.h-1, RA8875_BLACK);

    // shorten until fits in satname_b
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t bw = maxStringW (user_name, satname_b.w);

    // draw
    tft.setTextColor (SAT_COLOR);
    fillSBox (satname_b, RA8875_BLACK);
    tft.setCursor (satname_b.x + (satname_b.w - bw)/2, satname_b.y+FONT_H - 2);
    tft.print (user_name);
}

/* fill map_name_b with where sat name should go on map
 */
static void setSatMapNameLoc()
{
    // retrieve saved name without '_'
    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, sat_name, ' ', '_', NV_SATNAME_LEN);

    // get size
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t bw, bh;
    getTextBounds (user_name, &bw, &bh);
    map_name_b.w = bw;
    map_name_b.h = bh;

    switch ((MapProjection)map_proj) {

    case MAPP_AZIM1:
        // easy: just print in upper right
        map_name_b.x = map_b.x + map_b.w - map_name_b.w - 1;
        map_name_b.y = map_b.y + 10;
        break;

    case MAPP_AZIMUTHAL:
        // easy: just print on top between hemispheres
        map_name_b.x = map_b.x + (map_b.w - map_name_b.w)/2 ;
        map_name_b.y = map_b.y + 10;
        break;

    case MAPP_MERCATOR: {
        // locate name away from current sat location and misc symbols

        // start in south pacific
        #define _SP_LNG (-160)          // South Pacific longitude
        #define _SP_LAT (-30)           // " latitude above RSS and map scales
        SCoord name_l_s, name_r_s;      // left and right box candidate location
        ll2s (deg2rad(_SP_LAT), deg2rad(_SP_LNG), name_l_s, 0);
        name_r_s.x = name_l_s.x + map_name_b.w;
        name_r_s.y = name_l_s.y;

        // avoid any symbols
        #define _EDGE_GUARD 20
        while (overAnySymbol (name_l_s) || overAnySymbol(name_r_s)) {
            name_l_s.x += _EDGE_GUARD;
            name_r_s.x = name_l_s.x + map_name_b.w;
        }

        // avoid current sat footprint
        #define _SAT_FOOT_R 75          // typical footprint??
        SCoord &sat_s = sat_path[0];
        uint16_t dy = sat_s.y > name_l_s.y ? sat_s.y - name_l_s.y : name_l_s.y - sat_s.y;
        if (dy < _SAT_FOOT_R && name_r_s.x >= sat_s.x - _SAT_FOOT_R && name_l_s.x < sat_s.x + _SAT_FOOT_R) {
            name_l_s.x = sat_s.x + _SAT_FOOT_R + _EDGE_GUARD;
            name_r_s.x = name_l_s.x + map_name_b.w;
        }

        // check for going off the right edge
        if (name_r_s.x > map_b.x + map_b.w - _EDGE_GUARD) {
            name_l_s.x = map_b.x + _EDGE_GUARD;
            name_r_s.x = name_l_s.x + map_name_b.w;
        }

        // ok
        map_name_b.x = name_l_s.x;
        map_name_b.y = name_l_s.y;
        } break;

    default:
        fatalError (_FX("setSatMapNameLoc() bad map_proj %d"), map_proj);

    }
}

/* mark current sat pass location 
 */
static void drawSatNow()
{
    resetWatchdog();

    float az, el, range, rate, raz, saz;
    getSatAzElNow (NULL, &az, &el, &raz, &range, &rate, &saz, NULL, NULL);

    // size and center of screen path
    uint16_t r0 = (dx_info_b.h-2*FONT_H)/2;
    uint16_t x0 = dx_info_b.x + dx_info_b.w/2;
    uint16_t y0 = dx_info_b.y + dx_info_b.h - r0;

    float r = r0*(90-el)/90;                            // screen radius, zenith at center 
    uint16_t x = x0 + r*sinf(deg2rad(az)) + 0.5F;       // want east right
    uint16_t y = y0 - r*cosf(deg2rad(az)) + 0.5F;       // want north up

    tft.fillCircle (x, y, SAT_UP_R, SAT_COLOR);
}

/* draw event label and time t in the dx_info box unless t < 0 then just show title.
 * t is in days: if > 1 hour show HhM else M:S
 */
static void drawSatTime (bool force, const char *label, uint16_t color, float t)
{
    if (!sat)
        return;

    resetWatchdog();

    // previous state
    static char prev_label[3];                  // use just first few chars
    static uint8_t prev_a, prev_b;

    // layout
    const uint16_t font_y = dx_info_b.y+2*FONT_H-FONT_D;
    const uint16_t erase_h = 26;
    const uint16_t erase_y = font_y-erase_h+3;

    // prep font
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (color);
    uint16_t l_w = getTextWidth(label) + 2;     // right end can be slightly chopped

    // note whether label has changed
    bool new_label = force || strncmp (label, prev_label, sizeof(prev_label)) != 0;
    if (new_label)
        memcpy (prev_label, label, sizeof(prev_label));

    // draw
    if (t >= 0) {

        // draw label and time

        // draw label right-justified in left half
        if (new_label) {
            tft.fillRect (dx_info_b.x+1, erase_y, dx_info_b.w/2-1, erase_h, RA8875_BLACK);
            // tft.drawRect (dx_info_b.x+1, erase_y, dx_info_b.w/2-1, erase_h, RA8875_RED);
        }
        tft.setCursor (dx_info_b.x + dx_info_b.w/2 - l_w, font_y);
        tft.print (label);

        // format time as HhM else M:S
        t *= 24;                        // t is now hours
        uint8_t a, b;
        char sep = 'h';
        if (t < 1) {
            t *= 60;                    // t is now minutes
            sep = ':';
            a = (int)t;
            b = (int)((t-(int)t)*60);
        } else {
            a = (int)t;
            b = (int)((t-(int)t)*60);
        }

        // draw time centered in right half
        if (new_label || a != prev_a || b != prev_b) {
            char t_buf[10];
            snprintf (t_buf, sizeof(t_buf), "%d%c%02d", a, sep, b);
            uint16_t t_w = getTextWidth(t_buf);
            tft.fillRect (dx_info_b.x + dx_info_b.w/2, erase_y, dx_info_b.w/2-1, erase_h, RA8875_BLACK);
            // tft.drawRect (dx_info_b.x + dx_info_b.w/2, erase_y, dx_info_b.w/2-1, erase_h, RA8875_RED);
            tft.setCursor (dx_info_b.x + dx_info_b.w/2 + (dx_info_b.w/2-t_w)/2, font_y);
            tft.print(t_buf);
        }

        // remember last time drawn
        prev_a = a;
        prev_b = b;

    } else {

        // just draw label centered across entire box

        if (new_label) {
            tft.fillRect (dx_info_b.x+1, erase_y, dx_info_b.w-2, erase_h, RA8875_BLACK);
            // tft.drawRect (dx_info_b.x+1, erase_y, dx_info_b.w-2, erase_h, RA8875_RED);
        }
        tft.setCursor (dx_info_b.x + (dx_info_b.w-l_w)/2, font_y);
        tft.print(label);
    }
}

/* return whether the given line appears to be a valid TLE
 * only count digits and '-' counts as 1
 */
static bool tleHasValidChecksum (const char *line)
{
    // sum first 68 chars
    int sum = 0;
    for (uint8_t i = 0; i < 68; i++) {
        char c = *line++;
        if (c == '-')
            sum += 1;
        else if (c == '\0')
            return (false);         // too short
        else if (c >= '0' && c <= '9')
            sum += c - '0';
    }

    // last char is sum of previous modulo 10
    return ((*line - '0') == (sum%10));
}

/* clear screen, show the given message then restart operation without a sat
 */
static void fatalSatError (const char *fmt, ...)
{
    char buf[65] = "Sat error: ";       // max on one line
    va_list ap;

    int l = strlen (buf);

    va_start (ap, fmt);
    vsnprintf (buf+l, sizeof(buf)-l, fmt, ap);
    va_end (ap);

    Serial.println (buf);

    selectFontStyle (BOLD_FONT, SMALL_FONT);
    uint16_t bw = getTextWidth (buf);

    eraseScreen();
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor ((tft.width()-bw)/2, tft.height()/2);
    tft.print (buf);

    wdDelay (5000);

    resetWatchdog();
    unsetSat();
    initScreen();
}

static void showSelectionBox (uint16_t x, uint16_t y, bool on)
{
    uint16_t fill_color = on ? SAT_COLOR : RA8875_BLACK;
    tft.fillRect (x, y+(CELL_H-CB_SIZE)/2+3, CB_SIZE, CB_SIZE, fill_color);
    tft.drawRect (x, y+(CELL_H-CB_SIZE)/2+3, CB_SIZE, CB_SIZE, RA8875_WHITE);
}


/* look up sat_name. if found set up sat, else inform user and remove sat altogether.
 * return whether found it.
 */
static bool satLookup ()
{
    Serial.printf (_FX("Looking up %s\n"), sat_name);

    if (!SAT_NAME_IS_SET())
        return (false);

    // delete then restore if found
    if (sat) {
        delete sat;
        sat = NULL;
    }

    WiFiClient tle_client;
    bool ok = false;

    resetWatchdog();
    if (wifiOk() && tle_client.connect (svr_host, HTTPPORT)) {
        resetWatchdog();

        // memory
        StackMalloc t1(TLE_LINEL);
        StackMalloc t2(TLE_LINEL);
        StackMalloc name_mem(100);
        char *name = name_mem.getMem();

        // query
        snprintf (name, name_mem.getSize(), sat_one_page, sat_name);
        httpHCGET (tle_client, svr_host, name);
        if (!httpSkipHeader (tle_client)) {
            fatalSatError (_FX("Bad http header"));
            goto out;
        }

        // first response line is sat name, should match query
        if (!getTCPLine (tle_client, name, name_mem.getSize(), NULL)) {
            fatalSatError (_FX("Satellite %s not found"), sat_name);
            goto out;
        }
        if (strcasecmp (name, sat_name)) {
            fatalSatError (_FX("No match: '%s' '%s'"), sat_name, name);
            goto out;
        }

        // next two lines are TLE
        if (!getTCPLine (tle_client, t1.getMem(), TLE_LINEL, NULL)) {
            fatalSatError (_FX("Error reading line 1"));
            goto out;
        }
        if (!tleHasValidChecksum (t1.getMem())) {
            fatalSatError (_FX("Bad checksum for %s in line 1"), sat_name);
            goto out;
        }
        if (!getTCPLine (tle_client, t2.getMem(), TLE_LINEL, NULL)) {
            fatalSatError (_FX("Error reading line 2"));
            goto out;
        }
        if (!tleHasValidChecksum (t2.getMem())) {
            fatalSatError (_FX("Bad checksum for %s in line 2"), sat_name);
            goto out;
        }

        // TLE looks good, update name so cases match, define new sat
        memcpy (sat_name, name, sizeof(sat_name)-1);    // retain EOS
        sat = new Satellite (t1.getMem(), t2.getMem());
        tle_refresh = nowWO();
        ok = true;

    } else {

        fatalSatError (_FX("network error"));
    }

out:

    tle_client.stop();

    printFreeHeap (F("satLookup"));

    return (ok);
}

/* show all names and allow op to choose one or none.
 * save selection in sat_name, even if empty for no selection.
 * return whether sat was selected.
 */
static bool askSat()
{
    #define NO_SAT              (-1)            // cookie when op has chosen not to display a sat

    resetWatchdog();

    // don't inherit anything lingering after the tap that got us here
    drainTouch();

    // if stop while listing record as if it was a tap on that item
    SCoord s_stop;
    bool stop_tap = false;

    // erase screen and set font
    eraseScreen();
    tft.setTextColor (RA8875_WHITE);

    // show title and prompt
    uint16_t title_y = 3*TBORDER/4;
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setCursor (5, title_y);
    tft.print (F("Select satellite, or none"));

    // show rise units
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (tft.width()-450, title_y);
    tft.print (F("Rise in HH:MM"));

    // show what SOON_COLOR means
    tft.setTextColor (SOON_COLOR);
    tft.setCursor (tft.width()-280, title_y);
    tft.printf (_FX("<%d Mins"), SOON_MINS);

    // show what SATUP_COLOR means
    tft.setTextColor (SATUP_COLOR);
    tft.setCursor (tft.width()-170, title_y);
    tft.print (F("Up Now"));

    // show Ok button
    drawStringInBox ("Ok", ok_b, false, RA8875_WHITE);

    /// setup
    StackMalloc t1(TLE_LINEL);
    StackMalloc t2(TLE_LINEL);
    typedef char SatNames[MAX_NSAT][NV_SATNAME_LEN];
    StackMalloc name_mem(sizeof(SatNames));
    SatNames *sat_names = (SatNames *) name_mem.getMem();
    uint16_t prev_sel_x = 0, prev_sel_y = 0;
    int sel_idx = NO_SAT;
    int n_sat = 0;

    // open connection
    WiFiClient sat_client;
    resetWatchdog();
    if (!wifiOk() || !sat_client.connect (svr_host, HTTPPORT))
        goto out;

    // query page and skip header
    resetWatchdog();
    httpHCPGET (sat_client, svr_host, sat_get_all);
    if (!httpSkipHeader (sat_client))
        goto out;

    // read and display each sat, allow tapping part way through to stop
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    for (n_sat = 0; n_sat < MAX_NSAT; n_sat++) {

        // read name and 2 lines, done when eof or tap
        if (!getTCPLine (sat_client, &(*sat_names)[n_sat][0], NV_SATNAME_LEN, NULL)
                         || !getTCPLine (sat_client, t1.getMem(), TLE_LINEL, NULL)
                         || !getTCPLine (sat_client, t2.getMem(), TLE_LINEL, NULL)) {
            break;
        }

        // find row and column, col-major order
        int r = n_sat % N_ROWS;
        int c = n_sat / N_ROWS;

        // ul corner
        uint16_t x = c*CELL_W;
        uint16_t y = TBORDER + r*CELL_H;

        // allow early stop if tap
        if (readCalTouchWS(s_stop) != TT_NONE) {
            stop_tap = true;
            tft.setTextColor (RA8875_WHITE);
            tft.setCursor (x, y + FONT_H);        // match below
            tft.print (F("Listing stopped"));
            break;
        }

        // show tick box, pre-select if saved before
        if (strcmp (sat_name, (*sat_names)[n_sat]) == 0) {
            sel_idx = n_sat;
            showSelectionBox (x, y, true);
            prev_sel_x = x;
            prev_sel_y = y;
        } else {
            showSelectionBox (x, y, false);
        }

        // display next rise time of this sat
        if (sat)
            delete sat;
        sat = new Satellite (t1.getMem(), t2.getMem());
        SatRiseSet rs;
        findNextPass((*sat_names)[n_sat], nowWO(), rs);
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (x + CB_SIZE + 8, y + FONT_H);
        if (rs.rise_ok) {
            DateTime t_now = userDateTime(nowWO());
            if (rs.rise_time < rs.set_time) {
                // pass lies ahead
                float hrs_to_rise = (rs.rise_time - t_now)*24.0;
                if (hrs_to_rise*60 < SOON_MINS)
                    tft.setTextColor (SOON_COLOR);
                int mins_to_rise = (hrs_to_rise - floor(hrs_to_rise))*60;
                if (hrs_to_rise < 1 && mins_to_rise < 1)
                    mins_to_rise = 1;   // 00:00 looks wrong
                if (hrs_to_rise < 10)
                    tft.print ('0');
                tft.print ((uint16_t)hrs_to_rise);
                tft.print (':');
                if (mins_to_rise < 10)
                    tft.print ('0');
                tft.print (mins_to_rise);
                tft.print (' ');
            } else {
                // pass in progress
                tft.setTextColor (SATUP_COLOR);
                tft.print (F("Up "));
            }
        } else if (!rs.ever_up) {
            tft.setTextColor (GRAY);
            tft.print (F("NoR "));
        } else if (!rs.ever_down) {
            tft.setTextColor (SATUP_COLOR);
            tft.print (F("NoS "));
        }

        // followed by scrubbed name
        char user_name[NV_SATNAME_LEN];
        strncpySubChar (user_name, (*sat_names)[n_sat], ' ', '_', NV_SATNAME_LEN);
        tft.print (user_name);
    }

    // close connection
    sat_client.stop();

    // bale if no satellites displayed
    if (n_sat == 0)
        goto out;

    // entire display is one big menu box
    SBox screen_b;
    screen_b.x = 0;
    screen_b.y = 0;
    screen_b.w = tft.width();
    screen_b.h = tft.height();

    // follow touches to make selection, done when tap Ok or any item during drawing
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    SCoord s_tap;
    while (stop_tap || waitForTap (screen_b, NULL, MENU_TO, false, s_tap)) {

        // use stop tap first time if set
        if (stop_tap) {
            s_tap = s_stop;
            stop_tap = false;
        }

        // tap Ok button?
        if (inBox (s_tap, ok_b)) {
            // show Ok button toggle
            drawStringInBox ("Ok", ok_b, true, RA8875_WHITE);
            goto out;
        }

        // else toggle tapped sat, if any
        resetWatchdog();
        int r = (s_tap.y - TBORDER)/CELL_H;
        int c = s_tap.x/CELL_W;
        if (s_tap.y < TBORDER || s_tap.x - c*CELL_W > CELL_W/4)
            continue;                           // require tapping in left quarter of cell
        int tap_idx = c*N_ROWS + r;             // column major order
        if (tap_idx < n_sat) {
            // toggle
            uint16_t x = c * CELL_W;
            uint16_t y = TBORDER + r * CELL_H;
            if (tap_idx == sel_idx) {
                // already on: forget and toggle off
                // Serial.printf (_FX("forget %s\n"), (*sat_names)[sel_idx]);
                showSelectionBox (x, y, false);
                sel_idx = NO_SAT;
            } else {
                // toggle previous selection off (if any) and show selected
                if (prev_sel_y > 0)
                    showSelectionBox (prev_sel_x, prev_sel_y, false);
                sel_idx = tap_idx;
                prev_sel_x = x;
                prev_sel_y = y;
                // Serial.printf (_FX("select %s\n"), (*sat_names)[sel_idx]);
                showSelectionBox (x, y, true);
            }
        }
    }

  out:

    // close connection
    sat_client.stop();

    printFreeHeap (F("askSat"));

    if (n_sat == 0) {
        fatalSatError (_FX("No satellites found"));
        return (false);
    }

    // set sat_name and whether any selected
    if (sel_idx != NO_SAT) {
        strcpy (sat_name, (*sat_names)[sel_idx]);
        return (true);
    } else {
        unsetSat();
        return (false);
    }
}

/* return whether sat epoch is known to be good at the given time
 */
static bool satEpochOk(time_t t)
{
    if (!sat)
        return (false);

    DateTime t_now = userDateTime(t);
    DateTime t_sat = sat->epoch();

    bool ok;
    if (isSatMoon())
        ok = t_sat + 1.5F > t_now && t_now + 1.5F > t_sat;
    else
        ok = t_sat + MAX_TLE_AGE > t_now && t_now + MAX_TLE_AGE > t_sat;

    if (!ok) {
        int year;
        uint8_t mon, day, h, m, s;
        t_now.gettime (year, mon, day, h, m, s);
        Serial.printf (_FX("Ep: now = %d-%02d-%02d  %02d:%02d:%02d\n"), year, mon, day, h, m, s);
        t_sat.gettime (year, mon, day, h, m, s);
        Serial.printf (_FX("    sat = %d-%02d-%02d  %02d:%02d:%02d\n"), year, mon, day, h, m, s);
    }

    return (ok);

}

/* show pass time and process key rise/set alarms.
 * return whether set event just occurred.
 */
static bool drawSatRSEvents(bool force)
{
    if (!sat)
        return (false);

    DateTime t_now = userDateTime(nowWO());
    float days_to_rise = sat_rs.rise_time - t_now;
    float days_to_set = sat_rs.set_time - t_now;

    bool set = false;

    if (sat_rs.rise_time < sat_rs.set_time) {
        if (t_now < sat_rs.rise_time) {
            // pass lies ahead
            drawSatTime (force, "Rise in", SAT_COLOR, days_to_rise);
            risetAlarm(days_to_rise < ALARM_DT ? RISING_RATE : -1);
        } else if (t_now < sat_rs.set_time) {
            // pass in progress
            drawSatTime (force, "Set in", SAT_COLOR, days_to_set);
            drawSatNow();
            risetAlarm(days_to_set < ALARM_DT ? SETTING_RATE : 0);
        } else {
            // just set
            set = true;
            risetAlarm(-1);
        }
    } else {
        if (t_now < sat_rs.set_time) {
            // pass in progress
            drawSatTime (force, "Set in", SAT_COLOR, days_to_set);
            drawSatNow();
            risetAlarm(days_to_set < ALARM_DT ? SETTING_RATE : 0);
        } else {
            // just set
            set = true;
            risetAlarm(-1);
        }
    }

    return (set);
}

/* set the satellite observing location
 */
void setSatObserver (float lat, float lng)
{
    resetWatchdog();

    if (obs)
        delete obs;
    obs = new Observer (lat, lng, 0);
}

/* if a satellite is currently in play, return its name, current az, el, range, rate, az of next rise and set,
 *    and hours until next rise and set. name and time pointers may be NULL if not interested.
 * even if return true, rise and set az may be SAT_NOAZ, for example geostationary, in which case *rdtp
 *    and *sdtp are not set even if not NULL.
 * N.B. if sat is currently up, rdt might be negative to indicate time to preceding rise.
 */
bool getSatAzElNow (char *name, float *azp, float *elp, float *rangep, float *ratep,
float *razp, float *sazp, float *rdtp, float *sdtp)
{
    // get out fast if nothing to do or no info
    if (!obs || !sat || !SAT_NAME_IS_SET())
        return (false);

    // public name, if interested
    if (name)
        strncpySubChar (name, sat_name, ' ', '_', NV_SATNAME_LEN);

    // compute now
    DateTime t_now = userDateTime(nowWO());
    sat->predict (t_now);
    sat->topo (obs, *elp, *azp, *rangep, *ratep);       // expects refs, not pointers

    // horizon info, if available
    *razp = sat_rs.rise_ok ? sat_rs.rise_az : SAT_NOAZ;
    *sazp = sat_rs.set_ok  ? sat_rs.set_az  : SAT_NOAZ;

    // times, if interested and available
    if (rdtp && sat_rs.rise_ok)
        *rdtp = (sat_rs.rise_time - t_now)*24;
    if (sdtp && sat_rs.set_ok)
        *sdtp = (sat_rs.set_time - t_now)*24;

    // ok
    return (true);
}


/* called by main loop() to update pass info.
 * once per second is enough, not needed at all if no sat named or !dx_info_for_sat
 * the _path_ is updated much less often in updateSatPath().
 */
void updateSatPass()
{
    // get out fast if nothing to do or don't care
    if (!obs || !dx_info_for_sat || !SAT_NAME_IS_SET())
        return;

    // run once per second is fine
    static uint32_t last_run;
    if (!timesUp(&last_run, 1000))
        return;

    // look up if first time or time to refresh
    if (!sat) {
        if (!clockTimeOk()) {
            // network error, wait longer next time to give a chance to recover
            last_run += 60000UL;
            return;
        }
        if (!satLookup()) {
            return;
        }
        if (!satEpochOk(nowWO())) {
            // got it but epoch is out of date, give up
            fatalSatError (_FX("Epoch for %s is out of date"), sat_name);
            return;
        }
        // ok, update all info
        displaySatInfo();
    }

    resetWatchdog();

    // check edge cases
    if (!sat_rs.ever_up) {
        drawSatTime (true, _FX("No rise"), SAT_COLOR, -1);
        return;
    }
    if (!sat_rs.ever_down) {
        drawSatTime (true, _FX("No set"), SAT_COLOR, -1);
        return;
    }

    // show rise/set
    if (drawSatRSEvents(false)) {
        // next pass
        displaySatInfo();
    }
}

/* compute satellite geocentric path into sat_path[] and footprint into sat_foot[].
 * called once at the top of each map sweep so we can afford more extenstive checks than updateSatPass().
 * just skip if no named satellite or time is not confirmed.
 * the _pass_ is updated in updateSatPass().
 * we also update map_name_b to avoid the current sat location.
 */
void updateSatPath()
{
    if (!obs || !SAT_NAME_IS_SET() || !clockTimeOk())
        return;

    resetWatchdog();

    // look up if first time
    if (!sat) {
        if (!satLookup())
            return;
        // init pass info for updateSatPass()
        findNextPass(sat_name, nowWO(), sat_rs);
    }

    // confirm epoch is still valid
    if (!satEpochOk(nowWO())) {
        // not valid, maybe a fresh element set will be ok
        Serial.printf (_FX("%s out of date\n"), sat_name);
        if (!satLookup())
            return;
        if (!satEpochOk(nowWO())) {
            // no update or still bad epoch, give up on this sat
            fatalSatError (_FX("Epoch for %s is out of date"), sat_name);
            return;
        }
        // init pass info for updateSatPass()
        findNextPass(sat_name, nowWO(), sat_rs);
    }

    // from here we have a valid sat to report

    // free sat_path first since it was last to be malloced
    if (sat_path) {
        free (sat_path);
        sat_path = NULL;
    }

    // fill sat_foot
    DateTime t = userDateTime(nowWO());
    float satlat, satlng;
    sat->predict (t);
    sat->geo (satlat, satlng);
    updateFootPrint(satlat, satlng);
    updateClocks(false);

    // start sat_path max size, then reduce when know size needed
    sat_path = (SCoord *) malloc (MAX_PATH * sizeof(SCoord));
    if (!sat_path) {
        Serial.println (F("Failed to malloc sat_path"));
        while (1);      // timeout
    }

    // fill sat_path
    float period = sat->period();
    n_path = 0;
    uint16_t max_path = isSatMoon() ? 1 : MAX_PATH;         // N.B. only set the current location if Moon
    for (uint16_t p = 0; p < max_path; p++) {

        // 1/3rd are off screen to make a dashed line effect
        // N.B. hack knows MAX_PATH is 250 for UNIX and blanks 2/3 on ESP and 1/2 on UNIX
        if (getSatPathDashed() && (p % (MAX_PATH*3/250)) < (MAX_PATH*2/250-1)) {
            sat_path[n_path] = {1000, 1000};
        } else {
            // compute next point along path
            ll2s (satlat, satlng, sat_path[n_path], 2);
        }

        // skip duplicate points
        if (n_path == 0 || memcmp (&sat_path[n_path], &sat_path[n_path-1], sizeof(SCoord)))
            n_path++;

        t += period/max_path;   // show 1 rev
        sat->predict (t);
        sat->geo (satlat, satlng);

        // loop takes over a second on ESP so update clock midway
        if (p == max_path/2)
            updateClocks(false);
    }

    updateClocks(false);
    // Serial.printf (_FX("n_path %u / %u\n"), n_path, MAX_PATH);

    // reduce memory to only points actually used
    sat_path = (SCoord *) realloc (sat_path, n_path * sizeof(SCoord));

    // set map name to avoid current location
    setSatMapNameLoc();
}

/* draw the entire sat path and footprint, connecting points with lines.
 * N.B. only used with _IS_UNIX
 */
void drawSatPathAndFoot()
{
    if (!sat)
        return;

    resetWatchdog();

    uint16_t path_color = getMapColor(SATPATH_CSPR);
    for (int i = 1; i < n_path; i++) {
        SCoord &sp0 = sat_path[i-1];
        SCoord &sp1 = sat_path[i];
        if (segmentSpanOk(sp0,sp1,1))
            tft.drawLine (sp0.x, sp0.y, sp1.x, sp1.y, 1, path_color);
    }

    uint16_t foot_color = getMapColor(SATFOOT_CSPR);
    for (int alt_i = 0; alt_i < N_FOOT; alt_i++) {
        for (uint16_t foot_i = 0; foot_i < n_foot[alt_i]; foot_i++) {
            SCoord &sf0 = sat_foot[alt_i][foot_i];
            SCoord &sf1 = sat_foot[alt_i][(foot_i+1)%n_foot[alt_i]];   // closure!
            if (segmentSpanOk(sf0,sf1,1))
                tft.drawLine (sf0.x, sf0.y, sf1.x, sf1.y, 1, foot_color);
        }
    }
}

/* draw all sat path points on the given screen row.
 * N.B. only used with _IS_ESP8266
 */
void drawSatPointsOnRow (uint16_t y0)
{
    if (!sat)
        return;

    resetWatchdog();

    // draw fat pixel above so we don't clobber it on next row down

    // path
    uint16_t path_color = getMapColor(SATPATH_CSPR);
    for (uint16_t p = 0; p < n_path; p++) {
        SCoord s = sat_path[p];
        if (y0 == s.y && overMap(s)) {
            tft.drawPixel (s.x, s.y, path_color);
            s.y -= 1;
            if (overMap(s)) tft.drawPixel (s.x, s.y, path_color);
            s.x += 1;
            if (overMap(s)) tft.drawPixel (s.x, s.y, path_color);
            s.y += 1;
            if (overMap(s)) tft.drawPixel (s.x, s.y, path_color);
        }
    }

    // 3 footprint segments
    uint16_t foot_color = getMapColor(SATFOOT_CSPR);
    for (uint8_t alt_i = 0; alt_i < N_FOOT; alt_i++) {
        for (uint16_t foot_i = 0; foot_i < n_foot[alt_i]; foot_i++) {
            SCoord s = sat_foot[alt_i][foot_i];
            if (y0 == s.y && overMap(s)) {
                tft.drawPixel (s.x, s.y, foot_color);
                s.y -= 1;
                if (overMap(s)) tft.drawPixel (s.x, s.y, foot_color);
                s.x += 1;
                if (overMap(s)) tft.drawPixel (s.x, s.y, foot_color);
                s.y += 1;
                if (overMap(s)) tft.drawPixel (s.x, s.y, foot_color);
            }
        }
    }
}

/* draw sat name on map if it includes row y0 unless already showing in dx_info.
 * also draw if y0 == 0 as a way to draw regardless.
 */
void drawSatNameOnRow(uint16_t y0)
{
    // done if nothing to do or name is not using row y0
    if (dx_info_for_sat || !sat || !obs || !SAT_NAME_IS_SET())
        return;
    if (y0 != 0 && (y0 < map_name_b.y || y0 >= map_name_b.y + map_name_b.h))
        return;

    resetWatchdog();

    // retrieve saved name without '_'
    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, sat_name, ' ', '_', NV_SATNAME_LEN);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (getMapColor(SATFOOT_CSPR));
    tft.setCursor (map_name_b.x, map_name_b.y + map_name_b.h - 1);
    tft.print (user_name);
}

/* return whether user has tapped near the head of the satellite path or in the map name
 */
bool checkSatMapTouch (const SCoord &s)
{
    if (!sat || !sat_path)
        return (false);

    SBox sat_b;
    sat_b.x = sat_path[0].x-SAT_TOUCH_R;
    sat_b.y = sat_path[0].y-SAT_TOUCH_R;
    sat_b.w = 2*SAT_TOUCH_R;
    sat_b.h = 2*SAT_TOUCH_R;

    return (inBox (s, sat_b) || (!dx_info_for_sat && inBox (s, map_name_b)));
}

/* return whether user has tapped the "DX" label while showing DX info which means op wants
 * to set a new satellite
 */
bool checkSatNameTouch (const SCoord &s)
{
    if (!dx_info_for_sat) {
        // check just the left third so symbol (*) and TZ button are not included
        SBox lt_b = {dx_info_b.x, dx_info_b.y, (uint16_t)(dx_info_b.w/3), 30};
        return (inBox (s, lt_b));
    } else {
        return (false);
    }
}

/* something effecting the satellite has changed such as time or observer so get fresh info then
 * display it in dx_info_b, if care
 */
void displaySatInfo()
{
    if (!obs || !dx_info_for_sat)
        return;

    // freshen elements if new or stale
    if (!sat || nowWO() - tle_refresh > TLE_REFRESH) {
        if (!satLookup())
            return;
    }

    // confirm epoch still valid
    if (!satEpochOk(nowWO())) {
        fatalSatError (_FX("Epoch for %s is out of date"), sat_name);
        return;
    }

    findNextPass(sat_name, nowWO(), sat_rs);
    drawSatName();
    drawNextPass();
    (void) drawSatRSEvents(true);       // already checked rise/set
}

/* present list of satellites and let user select up to one, preselecting last known if any.
 * save name in sat_name and NVRAM, even if empty to signify no satellite.
 * return whether a sat was chosen or not.
 * N.B. caller must call initScreen on return regardless
 */
bool querySatSelection()
{
    resetWatchdog();

    // we need the whole screen
    closeDXCluster();       // prevent inbound msgs from clogging network
    closeGimbal();          // avoid dangling connection
    hideClocks();

    NVReadString (NV_SATNAME, sat_name);
    if (askSat()) {
        Serial.printf (_FX("Selected sat '%s'\n"), sat_name);
        if (!satLookup())
            return (false);
        findNextPass(sat_name, nowWO(), sat_rs);
    } else {
        delete sat;
        sat = NULL;
    }

    NVWriteString (NV_SATNAME, sat_name);       // persist name even if empty

    printFreeHeap (F("querySatSelection"));

    return (SAT_NAME_IS_SET());
}

/* install new satellite, if possible, or remove if "none".
 * N.B. calls initScreen() if changes sat
 */
bool setSatFromName (const char *new_name)
{
    // remove if "none"
    if (strcmp (new_name, "none") == 0) {
        if (SAT_NAME_IS_SET()) {
            unsetSat();
            drawOneTimeDX();
            initEarthMap();
        }
        return (true);
    }

    // build internal name, done if already engaged
    char tmp_name[NV_SATNAME_LEN];
    strncpySubChar (tmp_name, new_name, '_', ' ', NV_SATNAME_LEN);
    if (strcmp (tmp_name, sat_name) == 0)
        return (true);
    strcpy (sat_name, tmp_name);

    // lookup
    if (satLookup()) {
        // found

        // stop any tracking
        stopGimbalNow();

        // make permanent and redraw
        dx_info_for_sat = true;
        NVWriteString (NV_SATNAME, sat_name);
        drawOneTimeDX();
        initEarthMap();
        return (true);

    } else {
        // failed
        return (false);
    }
}

/* install a new satellite from its TLE.
 * return whether all good.
 * N.B. not saved in NV_SATNAME because we won't have the tle
 */
bool setSatFromTLE (const char *name, const char *t1, const char *t2)
{
    if (!tleHasValidChecksum(t1) || !tleHasValidChecksum(t2))
        return(false);

    // stop any tracking
    stopGimbalNow();

    sat = new Satellite (t1, t2);
    if (!satEpochOk(nowWO())) {
        delete sat;
        sat = NULL;
        fatalSatError (_FX("Elements out of date"));
        return (false);
    }
    tle_refresh = nowWO();
    dx_info_for_sat = true;
    strncpySubChar (sat_name, name, '_', ' ', NV_SATNAME_LEN);
    initScreen();
    return (true);
}

/* called once to return whether there is a valid sat in NV.
 * also a good time to insure alarm pin is off.
 */
bool initSatSelection()
{
    NVReadString (NV_SATNAME, sat_name);
    return (SAT_NAME_IS_SET());

    risetAlarm(-1);
}

/* return whether new_pass has been set since last call, and always reset.
 */
bool isNewPass()
{
    bool np = new_pass;
    new_pass = false;
    return (np);
}

/* return whether the current satellite is in fact the moon
 */
bool isSatMoon()
{
    return (sat && !strcmp (sat_name, _FX("Moon")));
}

/* return malloced array of malloced strings containing all available satellite names and their TLE;
 * last name is NULL. return NULL if trouble.
 * N.B. caller must free each name then array.
 */
const char **getAllSatNames()
{
    // malloced list of malloced names
    const char **all_names = NULL;
    int n_names = 0;

    // open connection
    WiFiClient sat_client;
    resetWatchdog();
    if (!wifiOk() || !sat_client.connect (svr_host, HTTPPORT))
        return (NULL);

    // query page and skip header
    resetWatchdog();
    httpHCPGET (sat_client, svr_host, sat_get_all);
    if (!httpSkipHeader (sat_client)) {
        sat_client.stop();
        return (NULL);
    }

    // read and add each name to all_names.
    // read name and 2 lines, done when eof or tap
    char name[NV_SATNAME_LEN];
    char line1[TLE_LINEL];
    char line2[TLE_LINEL];
    while (getTCPLine (sat_client, name, NV_SATNAME_LEN, NULL)
                         && getTCPLine (sat_client, line1, TLE_LINEL, NULL)
                         && getTCPLine (sat_client, line2, TLE_LINEL, NULL)) {
        all_names = (const char **) realloc (all_names, (n_names+1)*sizeof(const char*));
        all_names[n_names++] = strdup (name);
        all_names = (const char **) realloc (all_names, (n_names+1)*sizeof(const char*));
        all_names[n_names++] = strdup (line1);
        all_names = (const char **) realloc (all_names, (n_names+1)*sizeof(const char*));
        all_names[n_names++] = strdup (line2);
    }

    // close
    sat_client.stop();

    // add NULL then done
    all_names = (const char **) realloc (all_names, (n_names+1)*sizeof(char*));
    all_names[n_names++] = NULL;

    return (all_names);
}

/* return count of parallel lists of next several days UTC rise and set events for the current sat.
 * caller can assume each rises[i] < sets[i].
 * N.B. caller must free each list iff return > 0.
 */
int nextSatRSEvents (time_t **rises, float **raz, time_t **sets, float **saz)
{

    // start now
    time_t t0 = nowWO();
    DateTime t0dt = userDateTime(t0);
    time_t t = t0;

    // make lists for duration of elements
    int n_table = 0;
    while (satEpochOk(t)) {

        SatRiseSet rs;
        findNextPass(sat_name, t, rs);

        // avoid messy edge cases
        if (rs.rise_ok && rs.set_ok) {

            // UTC
            time_t rt = t0 + SECSPERDAY*(rs.rise_time - t0dt);
            time_t st = t0 + SECSPERDAY*(rs.set_time - t0dt);
            int up = SECSPERDAY*(rs.set_time - rs.rise_time);

            // avoid messy edge cases
            if (up > 0) {

                // init tables for realloc
                if (n_table == 0) {
                    *rises = NULL;
                    *raz = NULL;
                    *sets = NULL;
                    *saz = NULL;
                }

                *rises = (time_t *) realloc (*rises, (n_table+1) * sizeof(time_t *));
                *raz = (float *) realloc (*raz, (n_table+1) * sizeof(float *));
                *sets = (time_t *) realloc (*sets, (n_table+1) * sizeof(time_t *));
                *saz = (float *) realloc (*saz, (n_table+1) * sizeof(float *));

                (*rises)[n_table] = rt;
                (*raz)[n_table] = rs.rise_az;
                (*sets)[n_table] = st;
                (*saz)[n_table] = rs.set_az;

                n_table++;
            }

            // start next search half an orbit after set
            t = st + sat->period()*SECSPERDAY/2;

        } else if (!rs.ever_up || !rs.ever_down) {

            break;
        }

        // don't go completely dead
        updateClocks(false);
    }

    // return count
    return (n_table);
}

/* display table of several local DE rise/set events for the current sat using whole screen.
 * return after user has clicked ok or time out.
 * caller should call initScreen() after return.
 */
static void showNextSatEvents ()
{
    // clean
    hideClocks();
    eraseScreen();
    resetWatchdog();

    // setup layout
    #define _SNS_LR_B     10                    // left-right border
    #define _SNS_TOP_B    10                    // top border
    #define _SNS_DAY_W    60                    // width of day column
    #define _SNS_HHMM_W   130                   // width of HH:MM@az columns
    #define _SNS_ROWH     34                    // row height
    #define _SNS_TIMEOUT  30000                 // ms

    // init scan coords
    uint16_t x = _SNS_LR_B;
    uint16_t y = _SNS_ROWH + _SNS_TOP_B;

    // draw header prompt
    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, sat_name, ' ', '_', NV_SATNAME_LEN);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (DE_COLOR);
    tft.setCursor (x, y); tft.print (F("Day"));
    tft.setCursor (x+_SNS_DAY_W, y); tft.print (F("Rise    @Az"));
    tft.setCursor (x+_SNS_DAY_W+_SNS_HHMM_W, y); tft.print (F("Set      @Az"));
    tft.setCursor (x+_SNS_DAY_W+2*_SNS_HHMM_W, y); tft.print (F(" Up"));
    tft.setTextColor (RA8875_RED); tft.print (F(" >10 Mins      "));
    tft.setTextColor (DE_COLOR);
    tft.print (user_name);

    // draw ok button box
    SBox ok_b;
    ok_b.w = 100;
    ok_b.x = tft.width() - ok_b.w - _SNS_LR_B;
    ok_b.y = FONT_D;
    ok_b.h = _SNS_ROWH;
    static const char button_name[] = "Ok";
    drawStringInBox (button_name, ok_b, false, RA8875_GREEN);

    // advance to first data row
    y += _SNS_ROWH;

    // get list of times (ESP takes a while so show signs of life)
    tft.setCursor (x, y);
    tft.print (F("Calculating..."));
    time_t *rises, *sets;
    float *razs, *sazs;
    int n_times = nextSatRSEvents (&rises, &razs, &sets, &sazs);
    tft.fillRect (x, y-24, 250, 100, RA8875_BLACK);     // font y - font height

    // show list, if any
    resetWatchdog();
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    if (n_times == 0) {

        tft.setCursor (x, y);
        tft.print (F("No events"));

    } else {


        // draw table
        for (int i = 0; i < n_times; i++) {

            resetWatchdog();

            // font is variable width so we must space each column separately
            char buf[30];

            // convert to DE local time
            time_t rt = rises[i] + de_tz.tz_secs;
            time_t st = sets[i] + de_tz.tz_secs;
            int up = st - rt;       // nextSatRSEvents assures us this will be > 0

            // detect crossing midnight by comparing weekday
            int rt_wd = weekday(rt);
            int st_wd = weekday(st);

            // show rise day
            snprintf (buf, sizeof(buf), "%.3s", dayShortStr(rt_wd));
            tft.setTextColor (RA8875_WHITE);
            tft.setCursor (x, y);
            tft.print (buf);

            // show rise time/az
            snprintf (buf, sizeof(buf), _FX("%02dh%02d @%.0f"), hour(rt), minute(rt), razs[i]);
            tft.setCursor (x+_SNS_DAY_W, y);
            tft.print (buf);

            // if set time is tomorrow start new line with blank rise time
            if (rt_wd != st_wd) {
                // next row with wrap
                if ((y += _SNS_ROWH) > tft.height()) {
                    if ((x += tft.width()/2) > tft.width())
                        break;                          // no more room
                    y = 2*_SNS_ROWH + _SNS_TOP_B;       // skip ok_b
                }

                snprintf (buf, sizeof(buf), "%.3s", dayShortStr(st_wd));
                tft.setCursor (x, y);
                tft.print (buf);
            }

            // show set time/az
            snprintf (buf, sizeof(buf), _FX("%02dh%02d @%.0f"), hour(st), minute(st), sazs[i]);
            tft.setCursor (x+_SNS_DAY_W+_SNS_HHMM_W, y);
            tft.print (buf);

            // show up time, beware longer than 1 hour (moon!)
            if (up >= 3600)
                snprintf (buf, sizeof(buf), _FX("%02dh%02d"), up/3600, (up-3600*(up/3600))/60);
            else
                snprintf (buf, sizeof(buf), _FX("%02d:%02d"), up/60, up-60*(up/60));
            tft.setCursor (x+_SNS_DAY_W+2*_SNS_HHMM_W, y);
            tft.setTextColor (up >= 600 ? RA8875_RED : RA8875_WHITE);
            tft.print (buf);

            // next row with wrap
            if ((y += _SNS_ROWH) > tft.height()) {
                if ((x += tft.width()/2) > tft.width())
                    break;                              // no more room
                y = 2*_SNS_ROWH + _SNS_TOP_B;           // skip ok_b
            }
        }

        // finished with lists
        free ((void*)rises);
        free ((void*)razs);
        free ((void*)sets);
        free ((void*)sazs);
    }

    // wait for fresh tap or timeout
    SCoord tap;
    (void) waitForTap (ok_b, NULL, _SNS_TIMEOUT, false, tap);

    // ack
    drawStringInBox (button_name, ok_b, true, RA8875_GREEN);
}

/* called when tap within dx_info_b while showing a sat to show menu of choices.
 * s is known to be withing dx_info_b.
 */
void drawDXSatMenu (const SCoord &s)
{
    // list menu items. N.B. enum and mitems[] must be in same order
    enum {
        _DXS_CHGSAT, _DXS_SATRST, _DXS_SATOFF, _DXS_SHOWDX, _DXS_N
    };
    #define _DXS_INDENT 5
    MenuItem mitems[_DXS_N] = {
        {MENU_1OFN, true,  1, _DXS_INDENT, "Change sat"},
        {MENU_1OFN, false, 1, _DXS_INDENT, "Rise/set table"},
        {MENU_1OFN, false, 1, _DXS_INDENT, "Turn off sat"},
        {MENU_1OFN, false, 1, _DXS_INDENT, "Show DX Info"},
    };

    // box for menu
    SBox menu_b;
    menu_b.x = fminf (s.x, dx_info_b.x + dx_info_b.w - 100);
    menu_b.y = fminf (s.y, dx_info_b.y + dx_info_b.h - (_DXS_N+1)*14);
    menu_b.w = 0;                               // shrink to fit

    // run menu
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, false, false, 1, _DXS_N, mitems};
    if (runMenu (menu)) {
        if (mitems[_DXS_SHOWDX].set) {
            // return to normal DX info but leave sat functional
            dx_info_for_sat = false;
            drawOneTimeDX();
            drawDXInfo();

        } else if (mitems[_DXS_CHGSAT].set) {
            // show selection of sats, op may choose one or none
            dx_info_for_sat = querySatSelection();
            initScreen();

        } else if (mitems[_DXS_SATOFF].set) {
            // turn off sat and return to normal DX info
            unsetSat();
            dx_info_for_sat = false;
            drawOneTimeDX();
            initEarthMap();

        } else if (mitems[_DXS_SATRST].set) {
            // uses entire screen
            showNextSatEvents();
            initScreen();

        } else {
            fatalError (_FX("no dx sat menu"));
        }

    } else {
        // cancelled, just restore sat info
        displaySatInfo();
    }

}
