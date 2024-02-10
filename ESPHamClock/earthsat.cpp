/* manage selection and display of one earth sat.
 *
 * we call "pass" the overhead view shown in dx_info_b, "path" the orbit shown on the map.
 *
 * N.B. our satellite info server changes blanks to underscores in sat names.
 */


#include "HamClock.h"

bool dx_info_for_sat;                   // global to indicate whether dx_info_b is for DX info or sat info

// path drawing
// N.B. MAX_PATH must be a power of 2 for dashed lines
#if defined(_IS_ESP8266)
    // this requires dense collection of individual dots that are plotted as the map is drawn
    #define MAX_PATH    2048            // max number of points in orbit path
    #define FOOT_ALT0   1000            // n dots for 0 deg altitude locus
    #define FOOT_ALT30  300             // n dots for 30 deg altitude locus
    #define FOOT_ALT60  100             // n dots for 60 deg altitude locus
#else
    // can be sparser because we just draw lines
    #define MAX_PATH    512             // max number of points in orbit path
    #define FOOT_ALT0   200             // n dots for 0 deg altitude locus
    #define FOOT_ALT30  100             // n dots for 30 deg altitude locus
    #define FOOT_ALT60  75              // n dots for 60 deg altitude locus
#define SHSATDTMAP      1               // whether to show satellite time on map
#endif
#define N_FOOT      3                   // number of footprint altitude loci


// config
#define ALARM_DT        (1.0F/1440.0F)  // flash this many days before an event
#define SATLED_RISING_HZ  1             // flash at this rate when sat about to rise
#define SATLED_SETTING_HZ 2             // flash at this rate when sat about to set
#define MAX_TLE_AGE     7.0F            // max age to use a TLE in LEO, scaled by period, days (except moon)
#define SAT_TOUCH_R     20U             // touch radius, pixels
#define SAT_UP_R        2               // dot radius when up
#define PASS_STEP       10.0F           // pass step size, seconds
#define TBORDER         50              // top border
#define FONT_H          (dx_info_b.h/6) // height for SMALL_FONT
#define FONT_D          5               // font descent
#define SAT_COLOR       RA8875_RED      // overall annotation color
#define SATUP_COLOR     RGB565(0,200,0) // time color when sat is up
#define SOON_COLOR      RGB565(200,0,0) // table text color for pass soon
#define SOON_MINS       10              // "soon", minutes
#define CB_SIZE         20              // size of selection check box
#define CELL_H          32              // display cell height
#define N_COLS          4               // n cols in name table
#define CELL_W          (800/N_COLS)    // display cell width
#define N_ROWS          ((480-TBORDER)/CELL_H)  // n rows in name table
#define MAX_NSAT        (N_ROWS*N_COLS) // max names we can display
#define MAX_PASS_STEPS  30              // max lines to draw for pass map
#define MAP_DT_W        (6*7)           // map time width assuming X YY:ZZ, pixels

// used so findNextPass() can be used for contexts other than the current sat now
typedef struct {
    DateTime rise_time, set_time;       // next pass times
    bool rise_ok, set_ok;               // whether rise_time and set_time are valid
    float rise_az, set_az;              // rise and set az, degrees, if valid
    bool ever_up, ever_down;            // whether sat is ever above or below SAT_MIN_EL in next day
} SatRiseSet;

// handy pass states from findPassState()
typedef enum {
    PS_NONE,            // no sat rise/set in play
    PS_UPSOON,          // pass lies ahead
    PS_UPNOW,           // pass in progress
    PS_HASSET,          // down after being up
} PassState;

// state
static const char sat_get_all[] PROGMEM = "/esats.pl?getall=";                  // command to get all TLE
static const char sat_one_page[] = "/esats.pl?tlename=%s";                      // command to get one TLE
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
static bool new_pass;                   // set when new pass is ready

#if defined(__GNUC__)
static void fatalSatError (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));
#else
static void fatalSatError (const char *fmt, ...);
#endif


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




/* info to manage the blinker thread
 */
static volatile ThreadBlinker sat_blinker;

/* return all IO pins to quiescent state
 */
void satResetIO()
{
    disableBlinker (sat_blinker);
    mcp.pinMode (SATALARM_PIN, INPUT);
}

/* set alarm SATALARM_PIN flashing with the given frequency or one of BLINKER_*.
 */
static void risetAlarm (int hz)
{
    // tell helper thread what we want done
    setBlinkerRate (sat_blinker, hz);

    // insure helper thread is running
    startBinkerThread (sat_blinker, SATALARM_PIN, false); // on is hi
}



/* completely undefine the current sat
 */
static void unsetSat()
{
    // reset sat and its path
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

    // reset name
    sat_name[0] = '\0';
    NVWriteString (NV_SATNAME, sat_name);
    dx_info_for_sat = false;

    risetAlarm (BLINKER_OFF_HZ);
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
            ll2sRaw (vlat, vlng, foot_path[n], 2);

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
    DateTime t_now = userDateTime(t);   // search starting time
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
        int yr;
        uint8_t mo, dy, hr, mn, sc;
        t_now.gettime(yr, mo, dy, hr, mn, sc);
        Serial.printf (
            _FX("SAT: %*s @ %04d-%02d-%02d %02d:%02d:%02d next rise in %6.3f hrs, set in %6.3f (%ld ms)\n"),
            NV_SATNAME_LEN, name, yr, mo, dy, hr, mn,sc,
            rs.rise_ok ? 24*(rs.rise_time - t_now) : 0.0F, rs.set_ok ? 24*(rs.set_time - t_now) : 0.0F,
            millis() - t0);
    }

}

/* display next pass sky dome.
 * N.B. we assume findNextPass has been called to fill sat_rs
 */
static void drawSatSkyDome()
{
    resetWatchdog();

    // size and center of screen path
    uint16_t r0 = satpass_c.r;
    uint16_t xc = satpass_c.s.x;
    uint16_t yc = satpass_c.s.y;

    // erase sky dome
    tft.fillRect (dx_info_b.x+1, dx_info_b.y+2*FONT_H+1, dx_info_b.w-2, dx_info_b.h-2*FONT_H-1, RA8875_BLACK);

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
        tft.drawLine (xc, yc, xr, yr, HGRIDCOL);
        tft.fillCircle (xr, yr, 1, RA8875_WHITE);
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

}

/* erase full dx_info and draw name of current satellite IFF used in dx_info box
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
    tft.fillRect (dx_info_b.x, dx_info_b.y+1, dx_info_b.w, dx_info_b.h-1, RA8875_BLACK);  // avoid separator

    // shorten until fits in satname_b
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t bw = maxStringW (user_name, satname_b.w);

    // draw
    tft.setTextColor (SAT_COLOR);
    fillSBox (satname_b, RA8875_BLACK);
    tft.setCursor (satname_b.x + (satname_b.w - bw)/2, satname_b.y+FONT_H - 2);
    tft.print (user_name);
}

/* set map_name_b with where sat name should go on map
 */
static void setSatMapNameLoc()
{
    // retrieve saved name without '_'
    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, sat_name, ' ', '_', NV_SATNAME_LEN);

    // set size based on longest of sat name or rise/set time
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    map_name_b.w = fmaxf (MAP_DT_W, getTextWidth(user_name));
#if defined(SHSATDTMAP)
    map_name_b.h = FONT_H + 10;
#else
    map_name_b.h = FONT_H + 1;          // reduce flashing by not including height for time not shown anyway
#endif

    switch ((MapProjection)map_proj) {

    case MAPP_AZIM1:            // fallthru
        // easy: just print in upper right
        map_name_b.x = map_b.x + map_b.w - map_name_b.w - 10;
        map_name_b.y = map_b.y + 10;
        break;

    case MAPP_AZIMUTHAL:
        // easy: just print on top between hemispheres
        map_name_b.x = map_b.x + (map_b.w - map_name_b.w)/2 ;
        map_name_b.y = map_b.y + 10;
        break;

    case MAPP_ROB:              // fallthru
    case MAPP_MERCATOR: {
        // try to place somewhere over an ocean and away from sat footprint
        SCoord sat_xy, name_xy;
        LatLong sat_ll;
        sat_xy.x = sat_path[0].x/tft.SCALESZ;
        sat_xy.y = sat_path[0].y/tft.SCALESZ;
        s2ll (sat_xy, sat_ll);
        if (sat_ll.lng_d > -55 && sat_ll.lng_d < 125) {
            // sat in eastern hemi so put name in s pacific
            ll2s (deg2rad(-30), deg2rad(-160), name_xy, 0);
        } else {
            // sat in western hemi so put symbol in s indian
            ll2s (deg2rad(-30), deg2rad(50), name_xy, 0);
        }
        map_name_b.x = CLAMPF (name_xy.x, map_b.x + 10, map_b.x + map_b.w - map_name_b.w - 10);
        map_name_b.y = name_xy.y;

        } break;

    default:
        fatalError (_FX("setSatMapNameLoc() bad map_proj %d"), map_proj);

    }
}

/* mark current sat pass location 
 */
static void drawSatPassMarker()
{
    resetWatchdog();

    SatNow satnow;
    getSatNow (satnow);

    // size and center of screen path
    uint16_t r0 = satpass_c.r;
    uint16_t xc = satpass_c.s.x;
    uint16_t yc = satpass_c.s.y;

    float r = r0*(90-satnow.el)/90;                            // screen radius, zenith at center 
    uint16_t x = xc + r*sinf(deg2rad(satnow.az)) + 0.5F;       // want east right
    uint16_t y = yc - r*cosf(deg2rad(satnow.az)) + 0.5F;       // want north up

    if (y + SAT_UP_R < tft.height() - 1)                       // beware lower edge
        tft.fillCircle (x, y, SAT_UP_R, SAT_COLOR);
}

/* draw event label and time dt in the dx_info box unless dt < 0 then just show title.
 * dt is in days: if > 1 hour show HhM else M:S
 */
static void drawSatTime (bool force, const char *label, uint16_t color, float dt)
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
    if (dt >= 0) {

        // draw label and time

        // draw label right-justified in left half
        if (new_label) {
            tft.fillRect (dx_info_b.x+1, erase_y, dx_info_b.w/2-1, erase_h, RA8875_BLACK);
            // tft.drawRect (dx_info_b.x+1, erase_y, dx_info_b.w/2-1, erase_h, RA8875_RED);
        }
        tft.setCursor (dx_info_b.x + dx_info_b.w/2 - l_w, font_y);
        tft.print (label);

        // format time as HhM else M:S
        dt *= 24;                               // dt is now hours
        int a, b;
        char sep;
        formatSexa (dt, a, sep, b);

        // draw time centered in right half
        if (new_label || a != prev_a || b != prev_b) {
            char t_buf[10];
            snprintf (t_buf, sizeof(t_buf), _FX("%2d%c%02d"), a, sep, b);
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

/* clear screen, show the given message then restart operation after user ack.
 */
static void fatalSatError (const char *fmt, ...)
{
    // common prefix
    char buf[65] = "Sat error: ";               // max on one line
    va_list ap;

    // format message to fit after prefix
    int prefix_l = strlen (buf);
    va_start (ap, fmt);
    vsnprintf (buf+prefix_l, sizeof(buf)-prefix_l, fmt, ap);
    va_end (ap);

    // log 
    Serial.println (buf);

    // clear screen and show message centered
    eraseScreen();
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    uint16_t mw = getTextWidth (buf);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor ((tft.width()-mw)/2, tft.height()/3);
    tft.print (buf);

    // ok button
    SBox ok_b;
    const char button_msg[] = "Continue";
    uint16_t bw = getTextWidth(button_msg);
    ok_b.x = (tft.width() - bw)/2;
    ok_b.y = tft.height() - 40;
    ok_b.w = bw + 30;
    ok_b.h = 35;
    drawStringInBox (button_msg, ok_b, false, RA8875_WHITE);

    // wait forever for user to tap
    SCoord s;
    char c;
    UserInput ui = {
        ok_b,                                   // ok box bounds
        NULL,                                   // user check function, else NULL
        false,                                  // true if fp returned true
        0,                                      // timeout, msec, or 0 forever
        false,                                  // whether to update clocks while waiting
        s,                                      // tap location or ...
        c,                                      // keyboard char code
    };
    (void) waitForUser (ui);

    // restart without sat
    resetWatchdog();
    unsetSat();
    initScreen();
}


/* return whether sat epoch is known to be good at the given time
 */
static bool satEpochOk(const char *name, time_t t)
{
    if (!sat)
        return (false);

    DateTime t_now = userDateTime(t);
    DateTime t_sat = sat->epoch();
    float period = sat->period();       // days

    // N.B. can not use isSatMoon because sat_name is not set
    float max_age = strcmp(name,_FX("Moon")) == 0 ? 1.5F : (MAX_TLE_AGE * period/(1.5F/24.0F));

    bool ok = t_sat + max_age > t_now && t_now + max_age > t_sat;

    if (!ok) {
        int year;
        uint8_t mon, day, h, m, s;
        Serial.printf (_FX("SAT: %s age %g > %g days:\n"), name, t_now - t_sat, max_age);
        t_now.gettime (year, mon, day, h, m, s);
        Serial.printf (_FX("SAT: Ep: now = %d-%02d-%02d  %02d:%02d:%02d\n"), year, mon, day, h, m, s);
        t_sat.gettime (year, mon, day, h, m, s);
        Serial.printf (_FX("SAT:     sat = %d-%02d-%02d  %02d:%02d:%02d\n"), year, mon, day, h, m, s);
    }

    return (ok);

}

/* look up sat_name. if found set up sat, else inform user and remove sat altogether.
 * return whether found it.
 */
static bool satLookup ()
{
    if (!SAT_NAME_IS_SET())
        return (false);

    Serial.printf (_FX("SAT: Looking up %s\n"), sat_name);

    // delete then restore if found
    if (sat) {
        delete sat;
        sat = NULL;
    }

    StackMalloc t1(TLE_LINEL);
    StackMalloc t2(TLE_LINEL);
    char buf[sizeof(sat_one_page) + sizeof(sat_name) + 10];

    WiFiClient tle_client;
    const int MAX_TRIES = 3;
    char err_msg[100];
    bool ok = false;

    for (int try_i = 0; !ok && try_i < MAX_TRIES; try_i++) {

        resetWatchdog();

        // wait a bit before retrying
        if (try_i > 0)
            wdDelay(2000);

        // connect
        if (!wifiOk() || !tle_client.connect (backend_host, backend_port)) {
            strcpy (err_msg, _FX("network error"));
            tle_client.stop();
            continue;
        }

        // query
        snprintf (buf, sizeof(buf), sat_one_page, sat_name);
        httpHCGET (tle_client, backend_host, buf);
        if (!httpSkipHeader (tle_client)) {
            strcpy (err_msg, _FX("Bad http header"));
            tle_client.stop();
            continue;
        }

        // first response line is sat name, should match query
        if (!getTCPLine (tle_client, buf, sizeof(buf), NULL)) {
            snprintf (err_msg, sizeof(err_msg), _FX("Satellite %s not found"), sat_name);
            tle_client.stop();
            continue;
        }
        if (strcasecmp (buf, sat_name)) {
            snprintf (err_msg, sizeof(err_msg), _FX("No match: '%s' '%s'"), sat_name, buf);
            tle_client.stop();
            continue;
        }

        // next two lines are TLE
        if (!getTCPLine (tle_client, (char *) t1.getMem(), TLE_LINEL, NULL)) {
            strcpy (err_msg, _FX("Error reading TLE line 1"));
            tle_client.stop();
            continue;
        }
        if (!tleHasValidChecksum ((char *) t1.getMem())) {
            snprintf (err_msg, sizeof(err_msg), _FX("Bad checksum for %s in line 1"), sat_name);
            tle_client.stop();
            continue;
        }
        if (!getTCPLine (tle_client, (char *) t2.getMem(), TLE_LINEL, NULL)) {
            strcpy (err_msg, _FX("Error reading TLE line 2"));
            tle_client.stop();
            continue;
        }
        if (!tleHasValidChecksum ((char *) t2.getMem())) {
            snprintf (err_msg, sizeof(err_msg), _FX("Bad checksum for %s in line 2"), sat_name);
            tle_client.stop();
            continue;
        }

        // TLE looks good, update name so cases match, define new sat
        memcpy (sat_name, buf, sizeof(sat_name)-1);    // retain EOS
        sat = new Satellite ((char *) t1.getMem(), (char *) t2.getMem());

        // yah!
        ok = true;
    }

    tle_client.stop();

    if (!ok)
        fatalSatError ("%s", err_msg);

    return (ok);
}

static void showSelectionBox (const SCoord &c, bool on)
{
    uint16_t fill_color = on ? SAT_COLOR : RA8875_BLACK;
    tft.fillRect (c.x, c.y+(CELL_H-CB_SIZE)/2+3, CB_SIZE, CB_SIZE, fill_color);
    tft.drawRect (c.x, c.y+(CELL_H-CB_SIZE)/2+3, CB_SIZE, CB_SIZE, RA8875_WHITE);
}

/* show all names and allow op to choose one or none.
 * save selection in sat_name, even if empty for no selection.
 * return whether sat was selected.
 */
static bool askSat()
{
    #define NO_SAT              (-1)            // cookie when op has chosen not to display a sat

    resetWatchdog();

    // entire display is one big menu box
    SBox screen_b;
    screen_b.x = 0;
    screen_b.y = 0;
    screen_b.w = tft.width();
    screen_b.h = tft.height();

    // prep for user input (way up here to avoid goto warnings)
    SCoord tap_s;
    char typed_c;
    UserInput ui = {
        screen_b,
        NULL,
        false,
        MENU_TO,
        false,
        tap_s,
        typed_c,
    };

    // init selected item to none, might be set while drawing the matrix
    SCoord sel_s = {0, 0};
    int sel_idx = NO_SAT;
    int prev_sel_idx = NO_SAT;

    // don't inherit anything lingering after the tap that got us here
    drainTouch();

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

    // prep storage
    StackMalloc t1(TLE_LINEL);
    StackMalloc t2(TLE_LINEL);
    typedef char SatNames[MAX_NSAT][NV_SATNAME_LEN];
    StackMalloc name_mem(sizeof(SatNames));
    SatNames *sat_names = (SatNames *) name_mem.getMem();

    // n sats we display, may be fewer than total possible if tap to stop early
    int n_sat = 0;

    // open connection
    WiFiClient sat_client;
    resetWatchdog();
    if (!wifiOk() || !sat_client.connect (backend_host, backend_port))
        goto out;

    // query page and skip header
    resetWatchdog();
    httpHCPGET (sat_client, backend_host, sat_get_all);
    if (!httpSkipHeader (sat_client))
        goto out;

    // read and display each sat, allow tapping part way through to stop
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    for (n_sat = 0; n_sat < MAX_NSAT; n_sat++) {

        // read name and 2 lines, done when eof or tap
        if (!getTCPLine (sat_client, &(*sat_names)[n_sat][0], NV_SATNAME_LEN, NULL)
                         || !getTCPLine (sat_client, (char *) t1.getMem(), TLE_LINEL, NULL)
                         || !getTCPLine (sat_client, (char *) t2.getMem(), TLE_LINEL, NULL)) {
            break;
        }

        // find row and column, col-major order
        int r = n_sat % N_ROWS;
        int c = n_sat / N_ROWS;

        // ul corner of this cell
        SCoord cell_s;
        cell_s.x = c*CELL_W;
        cell_s.y = TBORDER + r*CELL_H;

        // allow early stop by tapping while drawing matrix
        SCoord tap_s;
        if (readCalTouchWS (tap_s) != TT_NONE || tft.getChar(NULL,NULL) != 0) {
            tft.setTextColor (RA8875_WHITE);
            tft.setCursor (cell_s.x, cell_s.y + FONT_H);
            tft.print (F("Listing stopped"));
            break;
        }

        // draw tick box, pre-selected if it's the current sat
        if (strcmp (sat_name, (*sat_names)[n_sat]) == 0) {
            showSelectionBox (cell_s, true);
            sel_idx = n_sat;
            sel_s = cell_s;
        } else {
            showSelectionBox (cell_s, false);
        }

        // display next rise time of this sat
        if (sat)
            delete sat;
        sat = new Satellite ((char *) t1.getMem(), (char *) t2.getMem());
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (cell_s.x + CB_SIZE + 8, cell_s.y + FONT_H);
        if (satEpochOk((*sat_names)[n_sat], nowWO())) {
            SatRiseSet rs;
            findNextPass((*sat_names)[n_sat], nowWO(), rs);
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
        } else {
            tft.setTextColor (GRAY);
            tft.print (F("Age "));
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

    // follow touches to make selection
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    while (waitForUser (ui)) {

        // tap Ok button or type Enter or ESC?
        if (typed_c == '\r' || typed_c == '\n' || typed_c == 27 || inBox (tap_s, ok_b)) {
            // show Ok button highlighted
            drawStringInBox ("Ok", ok_b, true, RA8875_WHITE);
            goto out;
        }

        // handle a typed direction key
        if (typed_c) {
            if (sel_idx != NO_SAT) {
                // act wrt current selection
                tap_s = sel_s;
                switch (typed_c) {
                case 'h': tap_s.x -= CELL_W; break;
                case 'j': tap_s.y += CELL_H; break;
                case 'k': tap_s.y -= CELL_H; break;
                case 'l': tap_s.x += CELL_W; break;
                case ' ': break;        // toggle this entry
                default:  continue;     // ignore
                }
            } else if (prev_sel_idx != NO_SAT) {
                // nothing selected now but there was a previous selection
                tap_s.x = (prev_sel_idx / N_ROWS) * CELL_W;
                tap_s.y = TBORDER + (prev_sel_idx % N_ROWS) * CELL_H;
            } else {
                // first time and nothing selected, start in first cell
                tap_s.x = 0;
                tap_s.y = TBORDER;
            }
        }

        // ignore if outside or beyond the matrix
        int r = (tap_s.y - TBORDER)/CELL_H;
        int c = tap_s.x/CELL_W;
        int tap_idx = c*N_ROWS + r;             // column major order
        if (r < 0 || r >= N_ROWS || c < 0 || c >= N_COLS || tap_idx < 0 || tap_idx >= n_sat)
            continue;

        // normalize the tap location to upper left of cell
        tap_s.x = c*CELL_W;
        tap_s.y = TBORDER + r*CELL_H;

        // update tapped cell
        if (tap_idx == sel_idx) {
            // already on: toggle off and forget
            showSelectionBox (tap_s, false);
            prev_sel_idx = sel_idx;
            sel_idx = NO_SAT;
        } else {
            // toggle previous selection off (if any) and show new selection
            if (sel_idx != NO_SAT)
                showSelectionBox (sel_s, false);
            prev_sel_idx = sel_idx;
            sel_idx = tap_idx;
            sel_s = tap_s;
            showSelectionBox (sel_s, true);
        }
    }

  out:

    // close connection
    sat_client.stop();

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

/* use sat_rs to catagorize the state of a pass.
 * if optional days also return timing info:
 *   if return PS_NONE then days is not modified
 *   if return PS_UPSOON then days is days until rise;
 *   if return PS_UPNOW then days is days until set
 *   if return PS_HASSET then days is days since set
 * N.B. requires sat_rs already computed
 */
static PassState findPassState (float *days)
{
    PassState ps;

    DateTime t_now = userDateTime(nowWO());

    if (!sat_rs.ever_up || !sat_rs.ever_down) {

        ps = PS_NONE;

    } else if (sat_rs.rise_time < sat_rs.set_time) {

        if (t_now < sat_rs.rise_time) {
            // pass lies ahead
            ps = PS_UPSOON;
            if (days)
                *days = sat_rs.rise_time - t_now;
        } else if (t_now < sat_rs.set_time) {
            // pass in progress
            ps = PS_UPNOW;
            if (days)
                *days = sat_rs.set_time - t_now;
        } else {
            // just set
            ps = PS_HASSET;
            if (days)
                *days = t_now - sat_rs.set_time;
        }

    } else {

        if (t_now < sat_rs.set_time) {
            // pass in progress
            ps = PS_UPNOW;
            if (days)
                *days = sat_rs.set_time - t_now;
        } else {
            // just set
            ps = PS_HASSET;
            if (days)
                *days = t_now - sat_rs.set_time;
        }
    }

    return (ps);
}

/* called often to keep sat and sat_rs updated, including creating sat if a name is known.
 * return whether ok to use and, if so, whether elements or sat_rs were updated if care.
 */
static bool checkSatUpToDate (bool *updated)
{
    // bale fast if no obs or no sat defined at all
    if (!obs || !SAT_NAME_IS_SET())
        return (false);

    // base positions on user's idea of now
    time_t now_wo = nowWO();

    // check if need to refresh
    if (sat && satEpochOk(sat_name, now_wo)) {

        // elements good but still update sat_rs if just set
        if (findPassState(NULL) == PS_HASSET) {
            findNextPass (sat_name, now_wo, sat_rs);
            if (updated)
                *updated = true;
        } else {
            // all still good
            if (updated)
                *updated = false;
        }

    } else {

        // need full refresh
        if (!satLookup())
            return (false);
        if (!satEpochOk(sat_name, now_wo)) {
            fatalSatError (_FX("Epoch for %s is out of date"), sat_name);
            return (false);
        }

        // compute fresh sat_rs for sure
        findNextPass (sat_name, now_wo, sat_rs);
        if (updated)
            *updated = true;
    }

    // ok!
    return (true);
}

/* show pass time of sat_rs
 */
static void drawSatRSEvents(bool force)
{
    float days;

    switch (findPassState(&days)) {

    case PS_NONE:
        // neither
        if (!sat_rs.ever_up)
            drawSatTime (true, _FX("No rise"), SAT_COLOR, -1);
        else if (!sat_rs.ever_down)
            drawSatTime (true, _FX("No set"), SAT_COLOR, -1);
        else
            fatalError (_FX("Bug! no rise/set from PS_NONE"));
        break;

    case PS_UPSOON:
        // pass lies ahead
        drawSatTime (force, "Rise in", SAT_COLOR, days);
        break;

    case PS_UPNOW:
        // pass in progress
        drawSatTime (force, "Set in", SAT_COLOR, days);
        drawSatPassMarker();
        break;

    case PS_HASSET:
        // just set
        break;
    }
}

/* operate the LED alarm GPIO pin depending on current state of pass
 */
static void checkLEDAlarmEvents()
{
    float days;

    switch (findPassState(&days)) {

    case PS_NONE:
        // no pass: turn off
        risetAlarm(BLINKER_OFF_HZ);
        break;

    case PS_UPSOON:
        // pass lies ahead: flash if within ALARM_DT
        risetAlarm(days < ALARM_DT ? SATLED_RISING_HZ : BLINKER_OFF_HZ);
        break;

    case PS_UPNOW:
        // pass in progress: check for soon to set
        risetAlarm(days < ALARM_DT ? SATLED_SETTING_HZ : BLINKER_ON_HZ);
        break;

    case PS_HASSET:
        // set: turn off
        risetAlarm(BLINKER_OFF_HZ);
        break;
    }
}

/* set new satellite observer location to de_ll and time to user's offset
 */
bool setNewSatCircumstance()
{
    if (obs)
        delete obs;
    obs = new Observer (de_ll.lat_d, de_ll.lng_d, 0);
    bool updated = false;
    bool ok = checkSatUpToDate (&updated);
    if (!ok)
        unsetSat();
    else if (!updated)
        findNextPass(sat_name, nowWO(), sat_rs);
    return (ok);
}

/* if a satellite is currently in play, return its name, current az, el, range, rate, az of next rise and set,
 *    and hours until next rise and set.
 * even if return true, rise and set az may be SAT_NOAZ, for example geostationary, in which case rdt
 *    and sdt are undefined.
 * N.B. if sat is currently up, rdt could be either < 0 to indicate time since previous rise or > sdt
 *    to indicate time until rise after set
 */
bool getSatNow (SatNow &satnow)
{
    // get out fast if nothing to do or no info
    if (!obs || !sat || !SAT_NAME_IS_SET())
        return (false);

    // public name
    strncpySubChar (satnow.name, sat_name, ' ', '_', NV_SATNAME_LEN);

    // compute location now
    DateTime t_now = userDateTime(nowWO());
    sat->predict (t_now);
    sat->topo (obs, satnow.el, satnow.az, satnow.range, satnow.rate);       // expects refs, not pointers

    // horizon info, if available
    satnow.raz = sat_rs.rise_ok ? sat_rs.rise_az : SAT_NOAZ;
    satnow.saz = sat_rs.set_ok  ? sat_rs.set_az  : SAT_NOAZ;

    // times
    if (sat_rs.rise_ok)
        satnow.rdt = (sat_rs.rise_time - t_now)*24;
    if (sat_rs.set_ok)
        satnow.sdt = (sat_rs.set_time - t_now)*24;

    // ok
    return (true);
}


/* called by main loop() to update _pass_ info so get out fast if nothing to do.
 * the _path_ is updated much less often in updateSatPath().
 * N.B. beware this is called by loop() while stopwatch is up
 * N.B. update sat_rs if !dx_info_for_sat so drawSatPath can draw rise/set time in map_name_b
 */
void updateSatPass()
{
    // get out fast if nothing for us
    if (!obs || !SAT_NAME_IS_SET())
        return;

    // always operate the LED at full rate
    checkLEDAlarmEvents();

    // other stuff once per second is fine
    static uint32_t last_run;
    if (!timesUp(&last_run, 1000))
        return;

    // done if can't even get the basics
    bool fresh_update;
    if (!checkSatUpToDate(&fresh_update))
        return;

    // do minimal display update if showing
    if (dx_info_for_sat && getSWDisplayState() == SWD_NONE) {
        resetWatchdog();
        if (fresh_update) 
            drawSatPass();              // full display update
        else
            drawSatRSEvents(false);     // just update time
    }
}

/* compute satellite geocentric _path_ into sat_path[] and footprint into sat_foot[].
 * called once at the top of each map sweep.
 * the _pass_ is updated in updateSatPass().
 * we also move map_name_b if necessary to avoid the current sat location.
 */
void updateSatPath()
{
    // N.B. do NOT call checkSatUpToDate() here -- it can cause updateSatPass() to miss PS_HASSET

    // bale fast if no sat defined at all
    if (!sat || !obs || !SAT_NAME_IS_SET())
        return;

    resetWatchdog();

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
    if (!sat_path)
        fatalError (_FX("No memory for satellite path"));

    // fill sat_path
    float period = sat->period();
    n_path = 0;
    uint16_t max_path = isSatMoon() ? 1 : MAX_PATH;             // N.B. only set the current location if Moon
    int dashed = 0;
    uint16_t edge = 2*getSpotPathSize();                        // leave room for dot at start of path
    for (uint16_t p = 0; p < max_path; p++) {

        // place dashed line points off screen courtesy overMap()
        if (getColorDashed(SATPATH_CSPR) && (dashed++ & (MAX_PATH>>7))) {   // first always on for center dot
            sat_path[n_path] = {10000, 10000};
        } else {
            // compute next point along path
            ll2sRaw (satlat, satlng, sat_path[n_path], edge);
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

    // decide line width
    uint16_t lw = getSpotPathSize();
    if (lw == 0)
        lw = tft.SCALESZ;

    // draw path
    uint16_t path_color = getMapColor(SATPATH_CSPR);
    bool draw_start = true;
    for (int i = 1; i < n_path; i++) {
        SCoord &sp0 = sat_path[i-1];
        SCoord &sp1 = sat_path[i];
        if (segmentSpanOkRaw(sp0,sp1,tft.SCALESZ*tft.SCALESZ)) {
            if (draw_start) {
                // N.B. set ll2s edge to accommodate this dot
                tft.fillCircleRaw (sp0.x, sp0.y, 2*lw, path_color);
                tft.drawCircleRaw (sp0.x, sp0.y, 2*lw, RA8875_BLACK);
                draw_start = false;
            }
            tft.drawLineRaw (sp0.x, sp0.y, sp1.x, sp1.y, lw, path_color);
        }
    }

    // draw foots
    uint16_t foot_color = getMapColor(SATFOOT_CSPR);
    for (int alt_i = 0; alt_i < N_FOOT; alt_i++) {
        for (uint16_t foot_i = 0; foot_i < n_foot[alt_i]; foot_i++) {
            SCoord &sf0 = sat_foot[alt_i][foot_i];
            SCoord &sf1 = sat_foot[alt_i][(foot_i+1)%n_foot[alt_i]];   // closure!
            if (segmentSpanOkRaw(sf0,sf1,1))
                tft.drawLineRaw (sf0.x, sf0.y, sf1.x, sf1.y, lw, foot_color);
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

/* draw sat name and next event time in map_name_b if it contains row y0 unless already showing in dx_info.
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

    // draw name
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t un_x = map_name_b.x + (map_name_b.w-getTextWidth(user_name))/2;
    uint16_t un_y = map_name_b.y + FONT_H - FONT_D;
    shadowString (user_name, true, getMapColor(SATFOOT_CSPR), un_x, un_y);

    // draw time to next event, if any, unless ESP which never erases
#if defined(SHSATDTMAP)
    float days;
    PassState ps = findPassState(&days);
    switch (ps) {
    case PS_HASSET:             // fallthru
    case PS_NONE:
        // nothing to show
        break;
    case PS_UPSOON:             // fallthru
    case PS_UPNOW: {
        int a, b;
        char sep;
        char buf[50];
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        formatSexa (days*24, a, sep, b);
        snprintf (buf, sizeof(buf), "%c %d%c%02d", ps == PS_UPSOON ? 'R' : 'S', a, sep, b);
        uint16_t buf_w = getTextWidth(buf);
        uint16_t ut_x = map_name_b.x + (map_name_b.w-buf_w)/2;
        uint16_t ut_y = map_name_b.y + map_name_b.h - 10;
        //drawSBox (map_name_b, RA8875_RED);

        SBox time_b;
        time_b.x = ut_x-2;
        time_b.y = ut_y-2;
        time_b.w = buf_w+4;
        time_b.h = 11;
        drawMapTag (buf, time_b, getMapColor(SATFOOT_CSPR), RA8875_BLACK);

        }
        break;
    }
#endif // SHSATDTMAP
}

/* return whether user has tapped near the head of the satellite path or in the map name
 */
bool checkSatMapTouch (const SCoord &s)
{
    if (!sat || !sat_path)
        return (false);

    SBox sat_b;
    sat_b.x = sat_path[0].x/tft.SCALESZ-SAT_TOUCH_R;
    sat_b.y = sat_path[0].y/tft.SCALESZ-SAT_TOUCH_R;
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

/* display full sat pass unless !dx_info_for_sat
 */
void drawSatPass()
{
    // skip if not showing
    if (!dx_info_for_sat)
        return;

    drawSatName();
    drawSatRSEvents(true);
    drawSatSkyDome();
}

/* present list of satellites and let user select up to one, preselecting last known if any.
 * save name in sat_name and NVRAM, even if empty to signify no satellite.
 * return whether a sat was chosen or not.
 * N.B. caller must call initScreen on return regardless
 */
bool querySatSelection()
{
    resetWatchdog();

    // not allowed to show sat if too many memory intensive panes are up
    if (!paneComboOk(plot_rotset)) {
        fatalSatError (_FX("Too many hi-memory panes to add a satellite also"));
        return (false);
    }

    // we need the whole screen
    closeDXCluster();       // prevent inbound msgs from clogging network
    closeGimbal();          // avoid dangling connection
    hideClocks();

    NVReadString (NV_SATNAME, sat_name);
    if (askSat()) {
        Serial.printf (_FX("SAT: Selected sat '%s'\n"), sat_name);
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

    // stop any tracking
    stopGimbalNow();

    // fresh start
    unsetSat();

    // build internal name
    char tmp_name[NV_SATNAME_LEN];
    strncpySubChar (tmp_name, new_name, '_', ' ', NV_SATNAME_LEN);
    strcpy (sat_name, tmp_name);

    // fresh look up
    if (checkSatUpToDate(NULL)) {

        // ok
        dx_info_for_sat = true;
        NVWriteString (NV_SATNAME, sat_name);
        drawSatPass();
        initEarthMap();
        return (true);

    } else {
        // failed
        unsetSat();
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

    // fresh
    unsetSat();

    // create and check
    sat = new Satellite (t1, t2);
    if (satEpochOk(name, nowWO())) {

        // ok
        dx_info_for_sat = true;
        findNextPass (name, nowWO(), sat_rs);
        strncpySubChar (sat_name, name, '_', ' ', NV_SATNAME_LEN);
        drawSatPass();
        initEarthMap();
        return (true);

    } else {

        delete sat;
        sat = NULL;
        fatalSatError (_FX("Elements for %s are out of data"), name);
        return (false);
    }
}

/* called once to return whether there is a valid sat in NV.
 * also a good time to insure alarm pin is off.
 */
bool initSatSelection()
{
    risetAlarm(BLINKER_OFF_HZ);
    NVReadString (NV_SATNAME, sat_name);
    if (!SAT_NAME_IS_SET())
        return (false);
    return (setNewSatCircumstance());
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
    if (!wifiOk() || !sat_client.connect (backend_host, backend_port))
        return (NULL);

    // query page and skip header
    resetWatchdog();
    httpHCPGET (sat_client, backend_host, sat_get_all);
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

    Serial.printf (_FX("SAT: found %d satellites\n"), n_names);

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
    while (satEpochOk(sat_name, t)) {

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

    // wait for any input
    SCoord s_tap;
    char typed_char;
    UserInput ui = {
        ok_b,
        NULL,
        false,
        _SNS_TIMEOUT,
        false,
        s_tap,
        typed_char,
    };
    waitForUser (ui);

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
        drawSatPass();
    }

}

/* return whether a satellite is currently in play
 */
bool isSatDefined()
{
    return (sat && obs);
}
