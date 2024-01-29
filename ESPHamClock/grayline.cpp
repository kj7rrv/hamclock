/* plot the current year table of DE grayline sun rise and set times in map_b
 */

#include "HamClock.h"

// config
#define GL_TB   55                                      // top plot border
#define GL_BB   20                                      // bottom plot border
#define GL_LB   30                                      // left plot border
#define GL_RB   10                                      // right plot border
#define GL_TL   5                                       // tick length
#define GL_PW   (map_b.w - GL_LB - GL_RB)               // plot width
#define GL_PH   (map_b.h - GL_TB - GL_BB)               // plot height
#define GL_X0   (map_b.x + GL_LB)                       // plot left x coord
#define GL_X1   (map_b.x + GL_LB + GL_PW)               // plot right x coord
#define GL_Y0   (map_b.y + GL_TB)                       // plot top y coord
#define GL_Y1   (map_b.y + GL_TB + GL_PH)               // plot bottom y coord
#define GL_PI   365                                     // total plot interval, days
#define GL_GC   DKGRAY                                  // grid color
#define GL_LC   BRGRAY                                  // scale color
#define GL_TC   RA8875_WHITE                            // text color
#define RISE_R  2                                       // rise line circle radius
#define SPD     (3600L*24L)                             // seconds per day

// handy conversions
#define GL_X2D(x)    (((x)-GL_X0)*GL_PI/GL_PW)          // x to doy
#define GL_D2X(d)    ((d)*GL_PW/GL_PI+GL_X0)            // doy to x
#define GL_H2Y(h)    (GL_Y0 + (h)*GL_PH/24)             // hours to y

/* perform one-time plot setup, ie, given time_t of Jan 1 this year:
 *   erase map_b
 *   draw title
 *   draw axes
 *   define the resume button.
 */
static void drawGLInit (const time_t yr0, SBox &resume_b)
{
    resetWatchdog();

    // fresh
    fillSBox (map_b, RA8875_BLACK);

    // title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setCursor (map_b.x + 70, map_b.y + 34);
    tft.setTextColor (GL_TC);
    tft.print(_FX("UTC "));
    tft.setTextColor (DE_COLOR);
    tft.print(_FX("DE "));
    tft.setTextColor (GL_TC);
    tft.print(_FX("and "));
    tft.setTextColor (DX_COLOR);
    tft.print(_FX("DX "));
    tft.setTextColor (GL_TC);

    // show rise and set key beneath respective word -- x coords are from Touch log
    tft.print(_FX("Grayline "));
    uint16_t rise_x = tft.getCursorX();
    tft.print(_FX("Rise and "));
    uint16_t set_x = tft.getCursorX();
    tft.print(_FX("Set Times"));
    for (uint16_t x = rise_x; x < rise_x + 42; x += 2)
        tft.fillCircle (x, map_b.y + 42, RISE_R, GL_TC);
    for (uint16_t x = set_x; x < set_x + 34; x += 2)
        tft.drawPixel (x, map_b.y + 42, GL_TC);

    // define resume button box
    resume_b.w = 100;
    resume_b.x = map_b.x + map_b.w - resume_b.w - GL_RB;
    resume_b.h = 40;
    resume_b.y = map_b.y + 4;
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    drawStringInBox (_FX("Resume"), resume_b, false, RA8875_GREEN);

    // draw and label the vertical axis
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(GL_LC);
    tft.drawLine (GL_X0, GL_Y0, GL_X0, GL_Y1, GL_LC);                   // vertical axis
    for (int hr = 0; hr <= 24; hr++) {
        uint16_t y = GL_H2Y(hr);
        tft.drawLine (GL_X0-GL_TL, y, GL_X0, y, GL_LC);                 // tick mark
        tft.setCursor (GL_X0-GL_TL-20, y-4);
        tft.print(hr);                                                  // label
    }

    // draw and label the horizontal axis
    tft.drawLine (GL_X0, GL_Y1, GL_X1, GL_Y1, GL_LC);                   // horizontal axis
    for (uint16_t x = GL_X0; x < GL_X1; x++) {
        int doy = GL_X2D(x);
        time_t t = yr0 + doy*SPD;
        if (day(t) == 1) {
            tft.drawLine (x, GL_Y1+GL_TL, x, GL_Y1, GL_LC);             // tick mark
            tft.setCursor (x-10, GL_Y1+GL_TL+4);
            tft.print (monthShortStr(month(t)));                        // label
        }
    }
}

/* given time_t of Jan 1 this year, draw the plot grid from x0 to x1
 */
static void drawGLGrid (const time_t yr0, uint16_t x0, uint16_t x1)
{
    resetWatchdog();

    // draw horizontal grid lines
    for (int hr = 0; hr < 24; hr++) {                                   // skip last ...
        uint16_t y = GL_H2Y(hr);
        tft.drawLine (x0, y, x1, y, GL_GC);                             // horizontal grid
    }

    // draw vetical grid lines
    int prev_doy = 0;
    for (uint16_t x = x0; x < x1; x++) {
        // x resolution is greater than days so avoid dups
        int doy = GL_X2D(x);
        if (doy == prev_doy)
            continue;
        prev_doy = doy;
        time_t t = yr0 + doy*SPD;
        if (day(t) == 1 && month(t) > 1)                                // skip first ... 
            tft.drawLine (x, GL_Y1, x, GL_Y0, GL_GC);                   // vertical grid
    }
}

/* given time_t of Jan 1 this year, plot rise/set from x0 to x1.
 * N.B. this draws _only_ the data, see drawGLInit() and drawGLGrid() for backgrounds.
 */
static void drawGLData (const time_t yr0, uint16_t x0, uint16_t x1)
{
    resetWatchdog();

    // draw today line if within [x0,x1]
    uint16_t today_x = GL_D2X((myNow() - yr0)/SPD);
    if (today_x >= x0 && today_x <= x1)
        tft.drawLine (today_x, GL_Y0, today_x, GL_Y1, DE_COLOR);

    int prev_doy = 0;
    for (uint16_t x = x0; x < x1; x++) {

        resetWatchdog();

        // x resolution is greater than days so avoid dups
        int doy = GL_X2D(x);
        if (doy == prev_doy)
            continue;
        prev_doy = doy;
        time_t t = yr0 + doy*SPD;

        time_t riset, sett;
        getSolarRS (t, de_ll, &riset, &sett);
        if (riset && sett) {
            int r_hr = hour(riset);
            int r_mn = minute(riset);
            float r_hrfrac = r_hr + r_mn/60.0F;
            uint16_t r_y = GL_H2Y(r_hrfrac);
            tft.fillCircle (x, r_y, RISE_R, DE_COLOR);

            int s_hr = hour(sett);
            int s_mn = minute(sett);
            float s_hrfrac = s_hr + s_mn/60.0F;
            uint16_t s_y = GL_H2Y(s_hrfrac);
            tft.drawPixel (x, s_y, DE_COLOR);
        }

        getSolarRS (t, dx_ll, &riset, &sett);
        if (riset && sett) {
            int r_hr = hour(riset);
            int r_mn = minute(riset);
            float r_hrfrac = r_hr + r_mn/60.0F;
            uint16_t r_y = GL_H2Y(r_hrfrac);
            tft.fillCircle (x, r_y, RISE_R, DX_COLOR);

            int s_hr = hour(sett);
            int s_mn = minute(sett);
            float s_hrfrac = s_hr + s_mn/60.0F;
            uint16_t s_y = GL_H2Y(s_hrfrac);
            tft.drawPixel (x, s_y, DX_COLOR);
        }
    }

    tft.drawPR();
}

/* draw a popup in the given box showing rise and set times for the given moment.
 * N.B. coordinate coords with box size set in plotGrayline().
 */
static void drawGLPopup (time_t t, SBox &box)
{
    // prep
    fillSBox (box, RA8875_BLACK);
    drawSBox (box, RA8875_WHITE);
    selectFontStyle (LIGHT_FONT, FAST_FONT);

    // title
    tft.setCursor (box.x + 55, box.y + 4);
    tft.setTextColor (GL_TC);
    tft.printf (_FX("%s %d"), monthShortStr(month(t)), day(t));

    // draw de rise and set times
    time_t riset, sett;
    getSolarRS (t, de_ll, &riset, &sett);
    tft.setTextColor (DE_COLOR);
    tft.setCursor (box.x + 5, box.y + 14);
    tft.print (_FX("DE  R "));
    if (riset) {
        int r_hr = hour(riset);
        int r_mn = minute(riset);
        tft.printf (_FX("%02d:%02d"), r_hr, r_mn);
    } else
        tft.printf (_FX("--:--"));
    tft.print (_FX("  S "));
    if (sett) {
        int s_hr = hour(sett);
        int s_mn = minute(sett);
        tft.printf (_FX("%02d:%02d"), s_hr, s_mn);
    } else
        tft.printf (_FX("--:--"));

    // draw dx rise and set times
    getSolarRS (t, dx_ll, &riset, &sett);
    tft.setTextColor (DX_COLOR);
    tft.setCursor (box.x + 5, box.y + 24);
    tft.print (_FX("DX  R "));
    if (riset) {
        int r_hr = hour(riset);
        int r_mn = minute(riset);
        tft.printf (_FX("%02d:%02d"), r_hr, r_mn);
    } else
        tft.printf (_FX("--:--"));
    tft.print (_FX("  S "));
    if (sett) {
        int s_hr = hour(sett);
        int s_mn = minute(sett);
        tft.printf (_FX("%02d:%02d"), s_hr, s_mn);
    } else
        tft.printf (_FX("--:--"));
}

/* draw and manage the sun rise/set plot.
 * return ready for fresh call to initEarthMap()
 */
void plotGrayline()
{
    // current UTC with user offset
    time_t t0 = nowWO();

    // start at the beginning of this year
    tmElements_t tm_yr0;
    tm_yr0.Second = 0;
    tm_yr0.Minute = 0;
    tm_yr0.Hour = 0;
    tm_yr0.Day = 1;
    tm_yr0.Month = 1;
    tm_yr0.Year = year(t0) - 1970;
    time_t yr0 = makeTime (tm_yr0);

    // resume button
    SBox resume_b;

    // full setup, grid and data
    drawGLInit (yr0, resume_b);
    drawGLGrid (yr0, GL_X0, GL_X1);
    drawGLData (yr0, GL_X0, GL_X1);

    // prep popup state used for erasing
    struct {
        bool is_up;             // whether in use
        int d0, d1;             // doy extent
        SBox b;                 // box
    } popup;
    memset (&popup, 0, sizeof(popup));
    popup.b.w = 135;                                            // N.B. coordinate size with drawGLPopup()
    popup.b.h = 40;
    // x and y are set from touch location

    // report info for tap times until time out or tap Resume button
    SCoord s;
    char c;
    UserInput ui = {
        map_b,
        NULL,
        false,
        60000,
        true,
        s,
        c,
    };

    while (waitForUser(ui)) {

        // done if return, esc or tap Resume button
        if (c == '\r' || c == '\n' || c == 27 || inBox (s, resume_b))
            break;

        // first erase previous popup, if any
        if (popup.is_up) {
            fillSBox (popup.b, RA8875_BLACK);                   // erase popup box
            uint16_t x1 = popup.b.x + popup.b.w;
            drawGLGrid (yr0, popup.b.x, x1);                    // redraw exposed grids
            drawGLData (yr0, popup.b.x, x1);                    // redraw exposed data
            popup.is_up = false;
        }

        // show new popup if tap is within the plot area
        if (s.x > GL_X0 && s.x < GL_X1 && s.y > GL_Y0 && s.y < GL_Y1) {

            resetWatchdog();

            // popup corner at s
            popup.b.x = s.x;
            popup.b.y = s.y;

            // but insure entirely over plot
            if (popup.b.x + popup.b.w > GL_X1)
                popup.b.x = GL_X1 - popup.b.w;
            if (popup.b.y + popup.b.h > GL_Y1)
                popup.b.y = GL_Y1 - popup.b.h;

            // save occluded period
            popup.d0 = GL_X2D(popup.b.x);
            popup.d1 = GL_X2D(popup.b.x + popup.b.w);

            // draw popup for time corresponding to s.x
            drawGLPopup (yr0 + GL_X2D(s.x)*SPD, popup.b);

            // note popup is now up
            popup.is_up = true;
        }
    }

    // ack
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    drawStringInBox (_FX("Resume"), resume_b, true, RA8875_GREEN);
    tft.drawPR();
}
