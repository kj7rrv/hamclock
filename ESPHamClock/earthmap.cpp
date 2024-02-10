/* code to manage the earth map
 */

/* main map drawing routines.
 */


#include "HamClock.h"


// DX location and path to DE
SCircle dx_c = {{0,0},DX_R};                    // screen coords of DX symbol
LatLong dx_ll;                                  // geo coords of dx spot

// DE and AntiPodal location
SCircle de_c = {{0,0},DE_R};                    // screen coords of DE symbol
LatLong de_ll;                                  // geo coords of DE
float sdelat, cdelat;                           // handy tri
SCircle deap_c = {{0,0},DEAP_R};                // screen coords of DE antipode symbol
LatLong deap_ll;                                // geo coords of DE antipode

// sun
AstroCir solar_cir;
SCircle sun_c = {{0,0},SUN_R};                  // screen coords of sun symbol
LatLong sun_ss_ll;                              // subsolar location
float csslat, ssslat;                           // handy trig

// moon
AstroCir lunar_cir;
SCircle moon_c = {{0,0},MOON_R};                // screen coords of moon symbol
LatLong moon_ss_ll;                             // sublunar location

// dx options
uint8_t show_lp;                                // display long path, else short part heading

#define GRAYLINE_COS    (-0.208F)               // cos(90 + grayline angle), we use 12 degs
#define GRAYLINE_POW    (0.75F)                 // cos power exponent, sqrt is too severe, 1 is too gradual
static SCoord moremap_s;                        // drawMoreEarth() scanning location 

// cached grid colors
static uint16_t GRIDC, GRIDC00;                 // main and highlighted

// flag to defer drawing over map until opportune time:
// ESP: draw after any line
// UNIX: draw after entire map
bool mapmenu_pending;

// grid spacing, degrees
#define LL_LAT_GRID     15
#define LL_LNG_GRID     15
#define RADIAL_GRID     15
#define THETA_GRID      15
#define FINESTEP_GRID   1

// establish GRIDC and GRIDC00
static void getGridColorCache()
{
    // get base color
    GRIDC = getMapColor(GRID_CSPR);
    GRIDC00 = getGoodTextColor (GRIDC);
}

/* erase the DE symbol by restoring map contents.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void eraseDEMarker()
{
    eraseSCircle (de_c);
}

/* return whether to display DE marker
 */
bool showDEMarker()
{
    return (overMap(de_c.s));
}

/* draw DE marker.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void drawDEMarker(bool force)
{
    if (force || showDEMarker()) {
        tft.fillCircle (de_c.s.x, de_c.s.y, DE_R, RA8875_BLACK);
        tft.drawCircle (de_c.s.x, de_c.s.y, DE_R, DE_COLOR);
        tft.fillCircle (de_c.s.x, de_c.s.y, DE_R/2, DE_COLOR);
    }
}

/* erase the antipode symbol by restoring map contents.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void eraseDEAPMarker()
{
    eraseSCircle (deap_c);
}

/* return whether to display the DE antipode
 */
bool showDEAPMarker()
{
    return (map_proj != MAPP_AZIM1 && !dx_info_for_sat && overMap(deap_c.s));
}

/* return whether to display the DX marker:
 *   over map and either not showing sat or showing either DX weather or VOACAP.
 */
bool showDXMarker()
{
    return ((!dx_info_for_sat
                    || findPaneChoiceNow(PLOT_CH_DXWX) != PANE_NONE
                    || findPaneChoiceNow(PLOT_CH_BC) != PANE_NONE)
            && overMap(dx_c.s));
}

/* draw antipodal marker if applicable.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void drawDEAPMarker()
{
    if (showDEAPMarker()) {
        tft.fillCircle (deap_c.s.x, deap_c.s.y, DEAP_R, DE_COLOR);
        tft.drawCircle (deap_c.s.x, deap_c.s.y, DEAP_R, RA8875_BLACK);
        tft.fillCircle (deap_c.s.x, deap_c.s.y, DEAP_R/2, RA8875_BLACK);
    }
}

/* draw the NVRAM grid square to 4 chars in the given screen location
 */
static void drawMaidenhead(NV_Name nv, SBox &b, uint16_t color)
{
    char maid[MAID_CHARLEN];
    getNVMaidenhead (nv, maid);
    maid[4] = 0;

    fillSBox (b, RA8875_BLACK);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (color);
    tft.setCursor (b.x, b.y+b.h-7);
    tft.print (maid);
}

/* draw de_info_b according to de_time_fmt
 */
void drawDEInfo()
{
    // init info block
    fillSBox (de_info_b, RA8875_BLACK);
    uint16_t vspace = de_info_b.h/DE_INFO_ROWS;

    // draw desired contents
    switch (de_time_fmt) {
    case DETIME_INFO:

        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (DE_COLOR);

        // time
        drawDECalTime(false);

        // lat and lon
        char buf[50];
        snprintf (buf, sizeof(buf), _FX("%.0f%c  %.0f%c"),
                    roundf(fabsf(de_ll.lat_d)), de_ll.lat_d < 0 ? 'S' : 'N',
                    roundf(fabsf(de_ll.lng_d)), de_ll.lng_d < 0 ? 'W' : 'E');
        tft.setCursor (de_info_b.x, de_info_b.y+2*vspace-6);
        tft.print(buf);

        // maidenhead
        drawMaidenhead(NV_DE_GRID, de_maid_b, DE_COLOR);

        // sun rise/set info
        drawDESunRiseSetInfo();

        break;

    case DETIME_ANALOG:         // fallthru
    case DETIME_ANALOG_DTTM:    // fallthru
    case DETIME_DIGITAL_12:     // fallthru
    case DETIME_DIGITAL_24:

        drawTZ (de_tz);
        updateClocks(true);
        break;

    case DETIME_CAL:

        drawDECalTime(true);
        drawCalendar(true);
        break;
    }
}

/* draw the time in de_info_b suitable for DETIME_INFO and DETIME_CALENDAR formats
 */
void drawDECalTime(bool center)
{
    drawTZ (de_tz);

    // get time
    time_t utc = nowWO();
    time_t local = utc + de_tz.tz_secs;
    int hr = hour (local);
    int mn = minute (local);
    int dy = day(local);
    int mo = month(local);

    // generate text
    char buf[32];
    if (getDateFormat() == DF_MDY || getDateFormat() == DF_YMD)
        snprintf (buf, sizeof(buf), _FX("%02d:%02d %s %d"), hr, mn, monthShortStr(mo), dy);
    else
        snprintf (buf, sizeof(buf), _FX("%02d:%02d %d %s"), hr, mn, dy, monthShortStr(mo));

    // set position
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t vspace = de_info_b.h/DE_INFO_ROWS;
    uint16_t x0 = de_info_b.x;
    if (center) {
        uint16_t bw = getTextWidth (buf);
        x0 += (de_info_b.w - bw)/2;
    }

    // draw
    tft.fillRect (de_info_b.x, de_info_b.y, de_info_b.w, vspace, RA8875_BLACK);
    tft.setTextColor (DE_COLOR);
    tft.setCursor (x0, de_info_b.y+vspace-6);
    tft.print(buf);
}

/* draw the Maidenhead grid key around the map if appropriate.
 */
static void drawMaidGridKey()
{
    // only if selected and using mercator projection
    if (mapgrid_choice != MAPGRID_MAID || map_proj != MAPP_MERCATOR)
        return;

    resetWatchdog();

    // keep right stripe above RSS and map scale, if on
    uint16_t right_h = map_b.h;
    if (rss_on)
        right_h = rss_bnr_b.y - map_b.y;
    if (mapScaleIsUp())
        right_h = mapscale_b.y - map_b.y;           // drap_b.y already above rss if on

    // prep background stripes
    tft.fillRect (map_b.x, map_b.y, map_b.w, MH_TR_H, RA8875_BLACK);                            // top
    tft.fillRect (map_b.x+map_b.w-MH_RC_W, map_b.y, MH_RC_W, right_h, RA8875_BLACK);            // right
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);

    // print labels across the top
    uint16_t rowy = map_b.y + MH_TR_DY;
    for (uint8_t i = 0; i < 18; i++) {
        LatLong ll;
        SCoord s;
        ll.lat_d = 0;
        ll.lng_d = -180 + (i+0.45F)*360/18;     // center character within square
        ll2s (ll, s, 10);
        tft.setCursor (s.x, rowy);
        tft.print ((char)('A' + (180+ll.lng_d)/20));
    }

    // print labels down the right
    uint16_t colx = map_b.x + map_b.w - MH_RC_W + MH_RC_DX;
    for (uint8_t i = 0; i < 18; i++) {
        uint16_t y = map_b.y + map_b.h - (i+1)*map_b.h/18 + MH_RC_DY;
        if (y < map_b.y + right_h - 8) {        // - font height
            tft.setCursor (colx, y);
            tft.print ((char)('A' + i));
        }
    }

}

#if defined(_IS_UNIX)

// MouseLoc geometry
#define ML_LINEDY       9                       // line height, pixels
#define ML_NLINES       12                      // allow this many lines in box
#define ML_MAXCHARS     9                       // max chars wide
#define ML_INDENT       2                       // nominal indentation

/* draw lat/long with given step sizes (used for ll and maidenhead).
 * UNIX only
 */
static void drawLLGrid (int lat_step, int lng_step)
{
    if (map_proj != MAPP_MERCATOR) {

        SCoord s0, s1;                                              // end points

        // lines of latitude, exclude the poles
        for (float lat = -90+lat_step; lat < 90; lat += lat_step) {
            ll2sRaw (deg2rad(lat), deg2rad(-180), s0, 0);
            for (float lng = -180+lng_step; lng <= 180; lng += lng_step) {
                ll2sRaw (deg2rad(lat), deg2rad(lng), s1, 0);
                for (float lg = lng-lng_step+FINESTEP_GRID; lg <= lng; lg += FINESTEP_GRID) {
                    ll2sRaw (deg2rad(lat), deg2rad(lg), s1, 0);
                    if (segmentSpanOkRaw (s0, s1, 1))
                        tft.drawLineRaw (s0.x, s0.y, s1.x, s1.y, 1, lat == 0 ? GRIDC00 : GRIDC);
                    s0 = s1;
                }
                s0 = s1;
            }
        }

        // lines of longitude -- pole to pole
        for (float lng = -180; lng < 180; lng += lng_step) {
            ll2sRaw (deg2rad(-90), deg2rad(lng), s0, 0);
            for (float lat = -90+lat_step; lat <= 90; lat += lat_step) {
                ll2sRaw (deg2rad(lat), deg2rad(lng), s1, 0);
                for (float lt = lat-lat_step+FINESTEP_GRID; lt <= lat; lt += FINESTEP_GRID) {
                    ll2sRaw (deg2rad(lt), deg2rad(lng), s1, 0);
                    if (segmentSpanOkRaw (s0, s1, 1))
                        tft.drawLineRaw (s0.x, s0.y, s1.x, s1.y, 1, lng == 0 ? GRIDC00 : GRIDC);
                    s0 = s1;
                }
                s0 = s1;
            }
        }

    } else {

        // easy! just straight lines but beware View menu button

        int n_lngstep = 360/lng_step;
        int n_latstep = 180/lat_step;

        // vertical
        for (int i = 0; i < n_lngstep; i++) {
            LatLong ll;
            SCoord s;
            ll.lat_d = 0;
            ll.lng_d = -180 + i*lng_step;
            ll2s (ll, s, 1);
            uint16_t top_y = s.x < view_btn_b.x + view_btn_b.w ? view_btn_b.y + view_btn_b.h : map_b.y;
            uint16_t bot_y = map_b.y+map_b.h-1;
            if (rss_on)
                bot_y = rss_bnr_b.y - 1;
            if (mapScaleIsUp())
                bot_y = mapscale_b.y - 1;                   // drap_b.y already above rss if on
            tft.drawLine (s.x, top_y, s.x, bot_y, i == n_lngstep/2 ? GRIDC00 : GRIDC);
        }

        // horizontal
        for (int i = 1; i < n_latstep; i++) {
            uint16_t y = map_b.y + i*map_b.h/n_latstep;
            if ((!rss_on || y < rss_bnr_b.y) && (!mapScaleIsUp() || y < mapscale_b.y)) {
                uint16_t left_x = y < view_btn_b.y + view_btn_b.h ? view_btn_b.x + view_btn_b.w : map_b.x;
                tft.drawLine (left_x, y, map_b.x+map_b.w-1, y, i == n_latstep/2 ? GRIDC00 : GRIDC);
            }
        }

    }
}

/* draw azimuthal grid lines from DE
 * UNIX only
 */
static void drawAzimGrid ()
{
    const float min_pole_lat = deg2rad(-89);
    const float max_pole_lat = deg2rad(89);
    const float max_az1_r = deg2rad(RADIAL_GRID*floorf(rad2deg(M_PIF)/AZIM1_ZOOM/RADIAL_GRID));
    const float min_az_gap = deg2rad(90-RADIAL_GRID);
    const float max_az_gap = deg2rad(90+RADIAL_GRID);

    SCoord s0, s1;

    // radial lines
    for (int ti = 0; ti < 360/THETA_GRID; ti++) {
        float t = deg2rad (ti * THETA_GRID);
        s0.x = 0;
        for (int ri = 0; ri <= 180/FINESTEP_GRID; ri++) {
            float r = deg2rad (ri * FINESTEP_GRID);
            // skip near 90 for AZM and everything over the ZOOM horizon for AZIM1
            if (map_proj == MAPP_AZIMUTHAL && r > min_az_gap && r < max_az_gap) {
                s0.x = 0;
                continue;
            }
            if (map_proj == MAPP_AZIM1 && r > max_az1_r)
                break;
            float ca, B;
            solveSphere (t, r, sdelat, cdelat, &ca, &B);
            float lat = M_PI_2F - acosf(ca);
            // avoid poles on mercator plots
            if (map_proj != MAPP_MERCATOR || (lat > min_pole_lat && lat < max_pole_lat)) {
                float lng = de_ll.lng + B;
                ll2sRaw (lat, lng, s1, 0);
                if (segmentSpanOkRaw (s0, s1, 1))
                    tft.drawLineRaw (s0.x, s0.y, s1.x, s1.y, 1, GRIDC);
                s0 = s1;
            } else
                s0.x = 0;
        }
    }

    // theta rings
    for (int ri = 1; ri < 180/RADIAL_GRID; ri++) {
        float r = deg2rad (ri * RADIAL_GRID);
        // skip near 90 for AZM and everything over the ZOOM horizon for AZIM1
        if (map_proj == MAPP_AZIMUTHAL && r > min_az_gap && r < max_az_gap) {
            s0.x = 0;
            continue;
        }
        if (map_proj == MAPP_AZIM1 && r > max_az1_r)
            break;
        s0.x = 0;
        // reduce zaggies on smaller circles
        int fine_step = r < M_PIF/4 || r > 3*M_PIF/4 ? 2*FINESTEP_GRID : FINESTEP_GRID;
        for (int ti = 0; ti <= 360/fine_step; ti++) {
            float t = deg2rad (ti * fine_step);
            float ca, B;
            solveSphere (t, r, sdelat, cdelat, &ca, &B);
            float lat = M_PI_2F - acosf(ca);
            // avoid poles on mercator plots
            if (map_proj != MAPP_MERCATOR || (lat > min_pole_lat && lat < max_pole_lat)) {
                float lng = de_ll.lng + B;
                ll2sRaw (lat, lng, s1, 0);
                if (s0.x > 0 && segmentSpanOkRaw (s0, s1, 1))
                    tft.drawLineRaw (s0.x, s0.y, s1.x, s1.y, 1, GRIDC);
                s0 = s1;
            } else
                s0.x = 0;
        }
    }
}

/* draw tropics grid lines from DE
 * UNIX only
 */
static void drawTropicsGrid()
{
    if (map_proj != MAPP_MERCATOR) {

        // just 2 lines at lat +- 23.5
        SCoord s00, s01, s10, s11;
        ll2sRaw (deg2rad(-23.5F), deg2rad(-180), s00, 0);
        ll2sRaw (deg2rad(23.5F), deg2rad(-180), s10, 0);
        for (float lng = -180; lng <= 180; lng += FINESTEP_GRID) {
            ll2sRaw (deg2rad(-23.5), deg2rad(lng), s01, 0);
            ll2sRaw (deg2rad(23.5), deg2rad(lng), s11, 0);
            if (segmentSpanOkRaw (s00, s01, 0))
                tft.drawLineRaw (s00.x, s00.y, s01.x, s01.y, 1, GRIDC);
            s00 = s01;
            if (segmentSpanOkRaw (s10, s11, 0))
                tft.drawLineRaw (s10.x, s10.y, s11.x, s11.y, 1, GRIDC);
            s10 = s11;
        }

    } else {

        // easy! just 2 straight lines
        uint16_t y = map_b.y + map_b.h/2 - 23.5F*map_b.h/180;
        tft.drawLine (map_b.x, y, map_b.x+map_b.w-1, y, GRIDC);
        y = map_b.y + map_b.h/2 + 23.5F*map_b.h/180;
        tft.drawLine (map_b.x, y, map_b.x+map_b.w-1, y, GRIDC);

    }
}

/* draw the complete proper map grid, ESP draws incrementally as map is drawn.
 * UNIX only
 */
static void drawMapGrid()
{
    resetWatchdog();

    switch ((MapGridStyle)mapgrid_choice) {

    case MAPGRID_OFF:
        break;

    case MAPGRID_MAID:

        drawMaidGridKey();
        drawLLGrid (10, 20);
        break;

    case MAPGRID_LATLNG:

        drawLLGrid (LL_LAT_GRID, LL_LNG_GRID);
        break;

    case MAPGRID_TROPICS:

        drawTropicsGrid();
        break;

    case MAPGRID_AZIM:

        drawAzimGrid();
        break;

    case MAPGRID_CQZONES:
        drawZone (ZONE_CQ, GRIDC, -1);
        break;

    case MAPGRID_ITUZONES:
        drawZone (ZONE_ITU, GRIDC, -1);
        break;

    default:
        fatalError (_FX("drawMapGrid() bad mapgrid_choice: %d"), mapgrid_choice);
        break;
    }
}

/* drawMouseLoc() helper to show age in nice units.
 * update ty by dy for each row used.
 * UNIX only
 */
static void drawMLAge (time_t t, uint16_t tx, int dy, uint16_t &ty)
{
    // get age in seconds but never negative
    time_t n = myNow();
    time_t age_s = n > t ? n - t : 0;

    // show in nice units
    char str[10];
    tft.setCursor (tx, ty += dy);
    tft.printf ("Age  %s", formatAge4 (age_s, str, sizeof(str)));
}

/* drawMouseLoc() helper to show DE distance and bearing to given location.
 * update ty by dy for each row used.
 * UNIX only
 */
static void drawMLDB (const LatLong &ll, uint16_t tx, int dy, uint16_t &ty)
{
    // get distance and bearing to spot location
    float dist, bearing;
    propDEPath (show_lp, ll, &dist, &bearing);
    dist *= ERAD_M;                             // angle to miles
    bearing *= 180/M_PIF;                       // rad -> degrees
    if (useMetricUnits())
        dist *= KM_PER_MI;

    // get bearing from DE in desired units
    bool bearing_ismag = desiredBearing (de_ll, bearing);

    // show direction
    tft.setCursor (tx, ty += dy);
    tft.printf (_FX("%s %5.0f"), show_lp ? "LP" : "SP", bearing);
    if (bearing_ismag) {
        tft.setCursor(tft.getCursorX()+2, ty-2); 
        tft.print ('M');
    } else {
        tft.drawCircle (tft.getCursorX()+2, ty+1, 1, RA8875_WHITE);         // home-made degree
    }

    // show distance
    tft.setCursor (tx, ty += dy);
    tft.printf (_FX("%6.0f %s"), dist, useMetricUnits() ? "km" : "mi");
}

/* drawMouseLoc() helper to show weather at the given location,
 * update ty by dy for each row used.
 * UNIX only
 */
static void drawMLWX (const LatLong &ll, uint16_t tx, int dy, uint16_t &ty)
{
    WXInfo wi;
    if (getWorldWx (ll, wi)) {

        // temperature in desired units
        float tmp = useMetricUnits() ? wi.temperature_c : CEN2FAH(wi.temperature_c);
        tft.setCursor (tx, ty += dy);
        tft.printf ("Temp%4.0f%c", tmp, useMetricUnits() ? 'C' : 'F');

        // conditions else wind
        int clen = strlen(wi.conditions);
        if (clen > 0) {
            tft.setCursor (tx, ty += dy);
            if (clen > ML_MAXCHARS)
                clen = ML_MAXCHARS;
            tft.printf ("%*s%.*s", (ML_MAXCHARS-clen)/2, "", ML_MAXCHARS, wi.conditions);
        } else {
            // width of combination wind direction and speed varies too much for one printf
            float spd = (useMetricUnits() ? 3.6F : 2.237F) * wi.wind_speed_mps; // kph or mph
            char wbuf[30];
            snprintf (wbuf, sizeof(wbuf), "%s@%.0f", wi.wind_dir_name, spd);
            tft.setCursor (tx, ty += dy);
            tft.printf ("Wnd%6s", wbuf);
        }
    }
}

/* drawMouseLoc() helper to show local mean time.
 * update ty by dy for each row used.
 * UNIX only
 */
static void drawMLLMT (const LatLong &ll, uint16_t tx, int dy, uint16_t &ty)
{
    time_t t = myNow() + getTZ(ll);
    tft.setCursor (tx, ty += dy);
    tft.printf ("LMT %02d:%02d", hour(t), minute(t));
}

/* drawMouseLoc() helper to show frequency.
 * update ty by dy for each row used.
 * UNIX only
 */
static void drawMLFreq (long hz, uint16_t tx, int dy, uint16_t &ty)
{
    tft.setCursor (tx, ty += dy);
    if (hz < 30000000L)
        tft.printf ("kHz %5ld", hz/1000);
    else
        tft.printf ("MHz %5ld", hz/1000000L);
}

/* draw local information about the current cursor position over the world map.
 * does not work for ESP because there is no way to follow touch without making a tap.
 * called after every map draw so we only have to erase parts of azm outside the hemispheres.
 * UNIX only
 */
static void drawMouseLoc()
{
    resetWatchdog();

    // draw just below map View button
    uint16_t tx = view_btn_b.x;                         // current text x coord
    uint16_t ty = view_btn_b.y + view_btn_b.h;          // current text y coord

    // size and location of names bar
    const uint16_t names_y = view_btn_b.y;
    const uint16_t names_h = 14;

    // get current mouse location and whether over HamClock window at all.
    SCoord ms;
    bool over_window = tft.getMouse(&ms.x, &ms.y);

    // get corresponding map location, if any
    LatLong ll;
    bool overmap = over_window && s2ll (ms, ll);

    // prep for text
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);

    // must draw the current zones before erasing menu in case it falls underneath the menu
    int cqzone_n = 0, ituzone_n = 0;
    if (overmap) {
        // N.B. call find even if not drawing here so we still get number for listing
        if (findZoneNumber (ZONE_CQ, ms, &cqzone_n) && mapgrid_choice == MAPGRID_CQZONES)
            drawZone (ZONE_CQ, GRIDC00, cqzone_n);
        if (findZoneNumber (ZONE_ITU, ms, &ituzone_n) && mapgrid_choice == MAPGRID_ITUZONES)
            drawZone (ZONE_ITU, GRIDC00, ituzone_n);
    }

    // erase any previous city
    static int max_cl;
    if (max_cl) {
        uint16_t max_cw = max_cl * 6 + 20;          // font w + margin
        tft.fillRect (map_b.x + (map_b.w-max_cw)/2, names_y, max_cw, names_h, RA8875_BLACK);
        max_cl = 0;
    }

    // erase menu area if going to show new data or clean up for azm not over hemispheres
    static bool was_overmap;
    if (overmap || (map_proj != MAPP_MERCATOR && was_overmap))
        tft.fillRect (tx, ty, view_btn_b.w-1, ML_LINEDY*ML_NLINES+1, RA8875_BLACK);
    was_overmap = overmap;

    // that's it if mouse is not over map
    if (!overmap)
        return;

    // show city if interested
    const char *city = NULL;
    LatLong city_ll;
    if (names_on) {
        city = getNearestCity (ll, city_ll, max_cl);
        if (city) {
            // background is already erased
            SCoord s;
            ll2s (city_ll, s, 4);
            tft.fillCircle (s.x, s.y, 4, RA8875_RED);
            uint16_t cw = getTextWidth (city);
            tft.setCursor (map_b.x + (map_b.w-cw)/2, names_y + 3);
            tft.print(city);
        }
    }


    // draw menu content PSK else DX Cluster else default, fields shown left and right justified

    const PSKReport *psk_rp;
    DXClusterSpot dxc_s;
    LatLong dxc_ll;
    if (getClosestPSK (ll, &psk_rp)) {

        // PSK, WSPR or RBN spot

        // adjust for text 
        char buf[ML_MAXCHARS+1];
        uint16_t tw;
        ty += 1;

        // show tx info
        snprintf (buf, sizeof(buf), "%.*s", ML_MAXCHARS, psk_rp->txcall);
        tw = getTextWidth(buf);
        tft.setCursor (tx + (view_btn_b.w-tw)/2, ty);
        tft.printf (buf);
        snprintf (buf, sizeof(buf), "%.*s", ML_MAXCHARS, psk_rp->txgrid);
        tw = getTextWidth(buf);
        tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += ML_LINEDY);
        tft.printf (buf);

        // show rx info
        snprintf (buf, sizeof(buf), "%.*s", ML_MAXCHARS, psk_rp->rxcall);
        tw = getTextWidth(buf);
        tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += ML_LINEDY);
        tft.printf (buf);
        snprintf (buf, sizeof(buf), "%.*s", ML_MAXCHARS, psk_rp->rxgrid);
        tw = getTextWidth(buf);
        tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += ML_LINEDY);
        tft.printf (buf);

        // show mode
        snprintf (buf, sizeof(buf), "%.*s", ML_MAXCHARS, psk_rp->mode);
        tw = getTextWidth(buf);
        tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += ML_LINEDY);
        tft.printf (buf);

        // show freq
        drawMLFreq (psk_rp->Hz, tx+ML_INDENT, ML_LINEDY, ty);

        // show age
        drawMLAge (psk_rp->posting, tx+ML_INDENT, ML_LINEDY, ty);

        // show snr
        tft.setCursor (tx+ML_INDENT, ty += ML_LINEDY);
        tft.printf ("SNR %5d", psk_rp->snr);

        // show distance and bearing
        drawMLDB (psk_rp->dx_ll, tx+ML_INDENT, ML_LINEDY, ty);

        // show weather
        drawMLWX (psk_rp->dx_ll, tx+ML_INDENT, ML_LINEDY, ty);

        // border in band color
        tft.drawRect (view_btn_b.x, view_btn_b.y + view_btn_b.h, view_btn_b.w-1, ML_LINEDY*ML_NLINES+1,
                        getBandColor(psk_rp->Hz));

    } else if (getClosestDXCluster (ll, &dxc_s, &dxc_ll) || getClosestOnTheAirSpot (ll, &dxc_s, &dxc_ll)
                        || getClosestADIFSpot (ll, &dxc_s, &dxc_ll)) {

        // DX Cluster or POTA/SOTA or ADIF spot

        // adjust for text 
        char buf[ML_MAXCHARS+1];
        uint16_t tw;
        ty += 1;

        // show tx info
        snprintf (buf, sizeof(buf), "%.*s", ML_MAXCHARS, dxc_s.dx_call);
        tw = getTextWidth(buf);
        tft.setCursor (tx + (view_btn_b.w-tw)/2, ty);
        tft.printf (buf);
        snprintf (buf, sizeof(buf), "%.*s", 4, dxc_s.dx_grid);
        tw = getTextWidth(buf);
        tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += ML_LINEDY);
        tft.printf (buf);

        // show rx info
        snprintf (buf, sizeof(buf), "%.*s", ML_MAXCHARS, dxc_s.de_call);
        tw = getTextWidth(buf);
        tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += ML_LINEDY);
        tft.printf (buf);
        snprintf (buf, sizeof(buf), "%.*s", 4, dxc_s.de_grid);
        tw = getTextWidth(buf);
        tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += ML_LINEDY);
        tft.printf (buf);

        // show mode if known
        if (strlen (dxc_s.mode) > 0) {
            snprintf (buf, sizeof(buf), "%.*s", ML_MAXCHARS, dxc_s.mode);
            tw = getTextWidth(buf);
            tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += ML_LINEDY);
            tft.printf (buf);
        } else
            ty += ML_LINEDY;

        // show freq
        drawMLFreq (dxc_s.kHz*1000, tx+ML_INDENT, ML_LINEDY, ty);

        // show spot age
        drawMLAge (dxc_s.spotted, tx+ML_INDENT, ML_LINEDY, ty);

        // show local time
        drawMLLMT (dxc_ll, tx+ML_INDENT, ML_LINEDY, ty);

        // show distance and bearing
        drawMLDB (dxc_ll, tx+ML_INDENT, ML_LINEDY, ty);

        // show weather
        drawMLWX (dxc_ll, tx+ML_INDENT, ML_LINEDY, ty);

        // border in band color
        tft.drawRect (view_btn_b.x, view_btn_b.y + view_btn_b.h, view_btn_b.w-1, ML_LINEDY*ML_NLINES+1,
                        getBandColor(1000*dxc_s.kHz));

    } else {

        // arbitrary cursor location, not a spot

        // adjust for text 
        ty += 1;

        // show lat/long
        tft.setCursor (tx+ML_INDENT, ty);
        tft.printf ("Lat %4.0f%c", fabsf(ll.lat_d), ll.lat_d < 0 ? 'S' : 'N');
        tft.setCursor (tx+ML_INDENT, ty += ML_LINEDY);
        tft.printf ("Lng %4.0f%c", fabsf(ll.lng_d), ll.lng_d < 0 ? 'W' : 'E');

        // show maid
        char maid[MAID_CHARLEN];
        ll2maidenhead (maid, ll);
        tft.setCursor (tx+ML_INDENT, ty += ML_LINEDY);
        tft.printf ("Grid %4.4s", maid);

        // zones
        if (cqzone_n) {
            tft.setCursor (tx+ML_INDENT, ty += ML_LINEDY);
            tft.printf (_FX("CQ  %5d"), cqzone_n);
        }
        if (ituzone_n) {
            tft.setCursor (tx+ML_INDENT, ty += ML_LINEDY);
            tft.printf (_FX("ITU %5d"), ituzone_n);
        }

        // prefix, else blank
        tft.setCursor (tx+ML_INDENT, ty += ML_LINEDY);
        char prefix[MAX_PREF_LEN+1];
        if (nearestPrefix (city ? city_ll : ll, prefix))
            tft.printf ("Pfx %5s", prefix);

        // blank so wx is on same rows on all formats
        ty += ML_LINEDY;

        // show local time
        drawMLLMT (ll, tx+ML_INDENT, ML_LINEDY, ty);

        // show distance and bearing
        drawMLDB (ll, tx+ML_INDENT, ML_LINEDY, ty);

        // show weather
        drawMLWX (ll, tx+ML_INDENT, ML_LINEDY, ty);

        // border
        tft.drawRect (view_btn_b.x, view_btn_b.y + view_btn_b.h, view_btn_b.w-1, ML_LINEDY*ML_NLINES+1,
                        RA8875_WHITE);
    }
}

#else   // _IS_ESP8266

/* given lat/lng and cos of angle from terminator, return earth map pixel.
 * only used by ESP, all others draw at higher resolution.
 * ESP only
 */
static uint16_t getEarthMapPix (LatLong ll, float cos_t)
{
    // indices into pixel array at this location
    uint16_t ex = (uint16_t)((EARTH_W*(ll.lng_d+180)/360)+0.5F) % EARTH_W;
    uint16_t ey = (uint16_t)((EARTH_H*(90-ll.lat_d)/180)+0.5F) % EARTH_H;

    // final color
    uint16_t pix_c;

    // decide color
    if (!night_on || cos_t > 0) {
        // < 90 deg: full sunlit
        getMapDayPixel (ey, ex, &pix_c);
    } else if (cos_t > GRAYLINE_COS) {
        // blend from day to night
        uint16_t day_c, night_c;
        getMapDayPixel (ey, ex, &day_c);
        getMapNightPixel (ey, ex, &night_c);
        uint8_t day_r = RGB565_R(day_c);
        uint8_t day_g = RGB565_G(day_c);
        uint8_t day_b = RGB565_B(day_c);
        uint8_t night_r = RGB565_R(night_c);
        uint8_t night_g = RGB565_G(night_c);
        uint8_t night_b = RGB565_B(night_c);
        float fract_night = powf(cos_t/GRAYLINE_COS, GRAYLINE_POW);
        float fract_day = 1 - fract_night;
        uint8_t twi_r = (fract_day*day_r + fract_night*night_r);
        uint8_t twi_g = (fract_day*day_g + fract_night*night_g);
        uint8_t twi_b = (fract_day*day_b + fract_night*night_b);
        pix_c = RGB565 (twi_r, twi_g, twi_b);
    } else {
        // full night side
        getMapNightPixel (ey, ex, &pix_c);
    }

    return (pix_c);
}

/* return whether coordinate s is over any symbol
 * ESP only
 */
static bool overAnySymbol (const SCoord &s)
{
    return (inCircle(s, de_c)
                || (showDEAPMarker() && inCircle(s, deap_c))
                || (showDEMarker() && inCircle(s, de_c))
                || (showDXMarker() && inCircle(s, dx_c))
                || inCircle (s, sun_c) || inCircle (s, moon_c)
                || overAnyBeacon(s)
                || overAnyFarthestPSKSpots(s)
                || overAnyOnTheAirSpots(s)
                || overAnyADIFSpots(s)
                || inBox(s,santa_b)
                || overMapScale(s));
}

#endif  // _IS_ESP8266

/* draw some fake stars for the azimuthal projection
 */
static void drawAzmStars()
{
    #define N_AZMSTARS 100
    uint8_t n_stars = 0;

    switch ((MapProjection)map_proj) {

    case MAPP_MERCATOR:
        break;

    case MAPP_AZIMUTHAL:
        while (n_stars < N_AZMSTARS) {
            int32_t x = random (map_b.w);
            int32_t y = random (map_b.h);
            int32_t dx = (x > map_b.w/2) ? (x - 3*map_b.w/4) : (x - map_b.w/4);
            int32_t dy = y - map_b.h/2;
            if (dx*dx + dy*dy > map_b.w*map_b.w/16) {
                uint16_t c = random(256);
                tft.drawPixel (map_b.x+x, map_b.y+y, RGB565(c,c,c));
                n_stars++;
            }
        }
        break;

    case MAPP_AZIM1:
        while (n_stars < N_AZMSTARS) {
            int32_t x = random (map_b.w);
            int32_t y = random (map_b.h);
            int32_t dx = x - map_b.w/2;
            int32_t dy = y - map_b.h/2;
            if (dx*dx + dy*dy > map_b.h*map_b.h/4) {
                uint16_t c = random(256);
                tft.drawPixel (map_b.x+x, map_b.y+y, RGB565(c,c,c));
                n_stars++;
            }
        }
        break;

    case MAPP_ROB:
        while (n_stars < N_AZMSTARS) {
            LatLong ll;
            SCoord star;
            star.x = map_b.x + random(map_b.w);
            star.y = map_b.y + random(map_b.h);
            if (!s2llRobinson(star,ll)) {
                uint16_t c = random(256);
                tft.drawPixel (star.x, star.y, RGB565(c,c,c));
                n_stars++;
            }
        }
        break;

    default:
        fatalError (_FX("drawAzmStars() bad map_proj %d"), map_proj);
    }
}

static void updateCircumstances()
{
    time_t utc = nowWO();

    getSolarCir (utc, de_ll, solar_cir);
    sun_ss_ll.lat_d = rad2deg(solar_cir.dec);
    sun_ss_ll.lng_d = -rad2deg(solar_cir.gha);
    normalizeLL (sun_ss_ll);
    csslat = cosf(sun_ss_ll.lat);
    ssslat = sinf(sun_ss_ll.lat);
    ll2s (sun_ss_ll, sun_c.s, SUN_R+1);

    getLunarCir (utc, de_ll, lunar_cir);
    moon_ss_ll.lat_d = rad2deg(lunar_cir.dec);
    moon_ss_ll.lng_d = -rad2deg(lunar_cir.gha);
    normalizeLL (moon_ss_ll);
    ll2s (moon_ss_ll, moon_c.s, MOON_R+1);

    updateSatPath();
}

/* draw the map view menu button.
 * N.B. adjust y position depending on whether we are drawing the maidenhead labels
 */
static void drawMapMenuButton()
{
    resetWatchdog();

    if (mapgrid_choice == MAPGRID_MAID && map_proj == MAPP_MERCATOR)
        view_btn_b.y = map_b.y + MH_TR_H;
    else
        view_btn_b.y = map_b.y;

    // 1 pixel inside so overMap() gives 2-pixel thick sat footprints some room
    tft.fillRect (view_btn_b.x, view_btn_b.y, view_btn_b.w-1, view_btn_b.h-1, RA8875_BLACK);
    tft.drawRect (view_btn_b.x, view_btn_b.y, view_btn_b.w-1, view_btn_b.h-1, RA8875_WHITE);

    char style_mem[NV_COREMAPSTYLE_LEN];
    const char *str = getMapStyle (style_mem);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t str_w = getTextWidth(str);
    tft.setCursor (view_btn_b.x+(view_btn_b.w-str_w)/2, view_btn_b.y+2);
    tft.setTextColor (RA8875_WHITE);
    tft.print (str);
}

/* erase the RSS box
 */
void eraseRSSBox ()
{
    resetWatchdog();

    // drap scale will move if up so erase where it is
    if (mapScaleIsUp())
        eraseMapScale();

    // erase entire banner if azm mode because redrawing the map will miss the corners
    if (map_proj != MAPP_MERCATOR)
        fillSBox (rss_bnr_b, RA8875_BLACK);

    // restore map and sat path
    for (uint16_t y = rss_bnr_b.y; y < rss_bnr_b.y+rss_bnr_b.h; y++) {
        updateClocks(false);
        for (uint16_t x = rss_bnr_b.x; x < rss_bnr_b.x+rss_bnr_b.w; x++)
            drawMapCoord (x, y);
        drawSatPointsOnRow (y);
    }

    // draw drap in new location
    if (mapScaleIsUp())
        drawMapScale();

    // restore maid key
    drawMaidGridKey();
}

/* arrange to draw the RSS box after it has been off a while, including mapscale and Maid key if necessary
 */
void drawRSSBox()
{
    scheduleRSSNow();
    if (mapScaleIsUp()) {
        eraseMapScale();       // erase where it is now
        drawMapScale();        // draw in new location
        drawMaidGridKey();      // tidy up
    }
}

/* draw, perform and engage results of the map View menu
 */
void drawMapMenu()
{

    enum MIName {     // menu items -- N.B. must be in same order as mitems[]
        MI_STY_TTL, MI_STR_CRY, MI_STY_TER, MI_STY_DRA, MI_STY_MUF, MI_STY_AUR, MI_STY_WXX, MI_STY_PRP,
        MI_GRD_TTL, MI_GRD_NON, MI_GRD_TRO, MI_GRD_LLG, MI_GRD_MAI, MI_GRD_AZM,
    #if defined(_SUPPORT_ZONES)
                    MI_GRD_CQZ, MI_GRD_ITU,
    #endif
        MI_PRJ_TTL, MI_PRJ_MER, MI_PRJ_AZM, MI_PRJ_AZ1, MI_PRJ_MOL,
        MI_RSS_YES,
        MI_NON_YES,
    #if defined(_SUPPORT_CITIES)
        MI_CTY_YES,
    #endif
        MI_N
    };
    #define PRI_INDENT 2
    #define SEC_INDENT 8
    MenuItem mitems[MI_N] = {
        {MENU_LABEL, false, 0, PRI_INDENT, "Style:"},
            {MENU_1OFN, false, 1, SEC_INDENT, coremap_names[CM_COUNTRIES]},
            {MENU_1OFN, false, 1, SEC_INDENT, coremap_names[CM_TERRAIN]},
            {MENU_1OFN, false, 1, SEC_INDENT, coremap_names[CM_DRAP]},
            {MENU_1OFN, false, 1, SEC_INDENT, coremap_names[CM_MUF]},
            {MENU_1OFN, false, 1, SEC_INDENT, coremap_names[CM_AURORA]},
            {MENU_1OFN, false, 1, SEC_INDENT, coremap_names[CM_WX]},
            {MENU_IGNORE, false, 1, SEC_INDENT, NULL},     // MI_STY_PRP: see below
        {MENU_LABEL, false, 0, PRI_INDENT, "Grid:"},
            {MENU_1OFN, false, 2, SEC_INDENT, "None"},
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_TROPICS]},
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_LATLNG]},
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_MAID]},
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_AZIM]},
        #if defined(_SUPPORT_ZONES)
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_CQZONES]},
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_ITUZONES]},
        #endif
        {MENU_LABEL, false, 0, PRI_INDENT, "Projection:"},
            {MENU_1OFN, false, 3, SEC_INDENT, map_projnames[MAPP_MERCATOR]},
            {MENU_1OFN, false, 3, SEC_INDENT, map_projnames[MAPP_AZIMUTHAL]},
            {MENU_1OFN, false, 3, SEC_INDENT, map_projnames[MAPP_AZIM1]},
            {MENU_1OFN, false, 3, SEC_INDENT, map_projnames[MAPP_ROB]},
        {MENU_TOGGLE, false, 4, PRI_INDENT, "RSS"},
        {MENU_TOGGLE, false, 5, PRI_INDENT, "Night"},
    #if defined(_SUPPORT_CITIES)
        {MENU_TOGGLE, false, 6, PRI_INDENT, "Cities"},
    #endif
    };

    // init selections with current states

    // if showing a propmap list in menu as selected else core map
    StackMalloc propband_mem (NV_COREMAPSTYLE_LEN);
    char *propband = (char *) propband_mem.getMem();     // N.B. must be persistent for lifetime of runMenu()
    if (prop_map.active) {
        // add propmap item to menu selected, leaving others all unselected
        mitems[MI_STY_PRP].type = MENU_1OFN;
        mitems[MI_STY_PRP].set = true;
        mitems[MI_STY_PRP].label = getMapStyle (propband);
    } else {
        // select current map, leave MI_STY_PRP as ignored
        mitems[MI_STR_CRY].set = core_map == CM_COUNTRIES;
        mitems[MI_STY_TER].set = core_map == CM_TERRAIN;
        mitems[MI_STY_DRA].set = core_map == CM_DRAP;
        mitems[MI_STY_MUF].set = core_map == CM_MUF;
        mitems[MI_STY_AUR].set = core_map == CM_AURORA;
        mitems[MI_STY_WXX].set = core_map == CM_WX;
    }

    mitems[MI_GRD_NON].set = mapgrid_choice == MAPGRID_OFF;
    mitems[MI_GRD_TRO].set = mapgrid_choice == MAPGRID_TROPICS;
    mitems[MI_GRD_LLG].set = mapgrid_choice == MAPGRID_LATLNG;
    mitems[MI_GRD_MAI].set = mapgrid_choice == MAPGRID_MAID;
    mitems[MI_GRD_AZM].set = mapgrid_choice == MAPGRID_AZIM;
#if defined(_SUPPORT_ZONES)
    mitems[MI_GRD_CQZ].set = mapgrid_choice == MAPGRID_CQZONES;
    mitems[MI_GRD_ITU].set = mapgrid_choice == MAPGRID_ITUZONES;
#endif

    mitems[MI_PRJ_MER].set = map_proj == MAPP_MERCATOR;
    mitems[MI_PRJ_AZM].set = map_proj == MAPP_AZIMUTHAL;
    mitems[MI_PRJ_AZ1].set = map_proj == MAPP_AZIM1;
    mitems[MI_PRJ_MOL].set = map_proj == MAPP_ROB;

    mitems[MI_RSS_YES].set = rss_on;
    mitems[MI_NON_YES].set = night_on;
#if defined(_SUPPORT_CITIES)
    mitems[MI_CTY_YES].set = names_on;
#endif

    // create a box for the menu
    SBox menu_b;
    menu_b.x = view_btn_b.x + 1;                // left edge matches view button with slight indent
    menu_b.y = view_btn_b.y+view_btn_b.h;       // top just below view button
    menu_b.w = 0;                               // shrink to fit

    // run menu
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, true, false, 1, MI_N, mitems};
    bool menu_ok = runMenu (menu);

    bool full_redraw = false;
    if (menu_ok) {

        resetWatchdog();

        // set Ok yellow while processing
        menuRedrawOk (ok_b, MENU_OK_BUSY);

        // schedule a new map if style changed
        bool prop_turned_off = prop_map.active && !mitems[MI_STY_PRP].set;
        if (mitems[MI_STR_CRY].set && (prop_turned_off || core_map != CM_COUNTRIES))
            scheduleNewCoreMap (CM_COUNTRIES);
        else if (mitems[MI_STY_TER].set && (prop_turned_off || core_map != CM_TERRAIN))
            scheduleNewCoreMap (CM_TERRAIN);
        else if (mitems[MI_STY_DRA].set && (prop_turned_off || core_map != CM_DRAP))
            scheduleNewCoreMap (CM_DRAP);
        else if (mitems[MI_STY_MUF].set && (prop_turned_off || core_map != CM_MUF))
            scheduleNewCoreMap (CM_MUF);
        else if (mitems[MI_STY_AUR].set && (prop_turned_off || core_map != CM_AURORA))
            scheduleNewCoreMap (CM_AURORA);
        else if (mitems[MI_STY_WXX].set && (prop_turned_off || core_map != CM_WX))
            scheduleNewCoreMap (CM_WX);

        // check for different grid
        if (mitems[MI_GRD_NON].set && mapgrid_choice != MAPGRID_OFF) {
            mapgrid_choice = MAPGRID_OFF;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
            full_redraw = true;
        } else if (mitems[MI_GRD_TRO].set && mapgrid_choice != MAPGRID_TROPICS) {
            mapgrid_choice = MAPGRID_TROPICS;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
            full_redraw = true;
        } else if (mitems[MI_GRD_LLG].set && mapgrid_choice != MAPGRID_LATLNG) {
            mapgrid_choice = MAPGRID_LATLNG;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
            full_redraw = true;
        } else if (mitems[MI_GRD_MAI].set && mapgrid_choice != MAPGRID_MAID) {
            mapgrid_choice = MAPGRID_MAID;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
            full_redraw = true;
        } else if (mitems[MI_GRD_AZM].set && mapgrid_choice != MAPGRID_AZIM) {
            mapgrid_choice = MAPGRID_AZIM;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
            full_redraw = true;
#if defined(_SUPPORT_ZONES)
        } else if (mitems[MI_GRD_CQZ].set && map_proj != MAPGRID_CQZONES) {
            mapgrid_choice = MAPGRID_CQZONES;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
            full_redraw = true;
        } else if (mitems[MI_GRD_ITU].set && map_proj != MAPGRID_ITUZONES) {
            mapgrid_choice = MAPGRID_ITUZONES;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
            full_redraw = true;
#endif
        }

        // check for different map projection
        if (mitems[MI_PRJ_MER].set && map_proj != MAPP_MERCATOR) {
            map_proj = MAPP_MERCATOR;
            NVWriteUInt8 (NV_MAPPROJ, map_proj);
            full_redraw = true;
        } else if (mitems[MI_PRJ_AZM].set && map_proj != MAPP_AZIMUTHAL) {
            map_proj = MAPP_AZIMUTHAL;
            NVWriteUInt8 (NV_MAPPROJ, map_proj);
            full_redraw = true;
        } else if (mitems[MI_PRJ_AZ1].set && map_proj != MAPP_AZIM1) {
            map_proj = MAPP_AZIM1;
            NVWriteUInt8 (NV_MAPPROJ, map_proj);
            full_redraw = true;
        } else if (mitems[MI_PRJ_MOL].set && map_proj != MAPP_ROB) {
            map_proj = MAPP_ROB;
            NVWriteUInt8 (NV_MAPPROJ, map_proj);
            full_redraw = true;
        }

        // check for change night option
        if (mitems[MI_NON_YES].set != night_on) {
            night_on = mitems[MI_NON_YES].set;
            NVWriteUInt8 (NV_NIGHT_ON, night_on);
            full_redraw = true;
        }


    #if defined(_SUPPORT_CITIES)
        // check for change of names option
        if (mitems[MI_CTY_YES].set != names_on) {
            names_on = mitems[MI_CTY_YES].set;
            NVWriteUInt8 (NV_NAMES_ON, names_on);
        }
    #endif

        // check for changed RSS -- N.B. do this last to utilize full_redraw
        if (mitems[MI_RSS_YES].set != rss_on) {
            rss_on = mitems[MI_RSS_YES].set;
            NVWriteUInt8 (NV_RSS_ON, rss_on);

            // do minimal restore if not restart map
            if (!full_redraw) {
                if (rss_on) {
                    drawRSSBox();
                } else {
                    eraseRSSBox();
                }
            }
        }

        // restart map if enough has changed
        if (full_redraw)
            initEarthMap();
    }

    if (!menu_ok || !full_redraw) {
        // restore map
        resetWatchdog();
        for (uint16_t dy = 0; dy < menu_b.h; dy++)
            for (uint16_t dx = 0; dx < menu_b.w; dx++)
                drawMapCoord (menu_b.x+dx, menu_b.y+dy);
        if (rss_on)
            drawRSSBox();
    }

    tft.drawPR();

    // discard any extra taps
    drainTouch();

    printFreeHeap (F("drawMapMenu"));

}

/* restart map for current projection and de_ll and dx_ll
 */
void initEarthMap()
{
    resetWatchdog();

    // completely erase map
    fillSBox (map_b, RA8875_BLACK);

    // add funky star field if azm
    drawAzmStars();

    // get grid colors
    getGridColorCache();

    // freshen RSS and clocks
    scheduleRSSNow();
    updateClocks(true);

    // draw map view button
    drawMapMenuButton();

    // reset any pending great circle path
    setDXPathInvalid();

    // update astro info
    updateCircumstances();

    // update DE and DX info
    sdelat = sinf(de_ll.lat);
    cdelat = cosf(de_ll.lat);
    ll2s (de_ll, de_c.s, DE_R);
    antipode (deap_ll, de_ll);
    ll2s (deap_ll, deap_c.s, DEAP_R);
    ll2s (dx_ll, dx_c.s, DX_R);

    // show updated info
    drawDEInfo();
    drawDXInfo();

    // insure NCDXF and DX spots screen coords match current map type
    updateBeaconScreenLocations();
    updateDXClusterSpotScreenLocations();
    updateOnTheAirSpotScreenLocations();

    #if defined (_SUPPORT_ZONES)
        // update zone screen boundaries
        updateZoneSCoords(ZONE_CQ);
        updateZoneSCoords(ZONE_ITU);
    #endif

    // init scan line in map_b
    moremap_s.x = 0;                    // avoid updateCircumstances() first call to drawMoreEarth()
    moremap_s.y = map_b.y;

    // now main loop can resume with drawMoreEarth()
}

/* display another earth map row at mmoremap_s.
 * ESP draws map one line at a time, others draw all the map then all the symbols to overlay.
 */
void drawMoreEarth()
{
    resetWatchdog();

    #if defined (_IS_ESP8266)
    // handy health indicator and update timer
    digitalWrite(LIFE_LED, !digitalRead(LIFE_LED));
    #endif // _IS_ESP8266

    // refresh circumstances at start of each map scan but not very first call after initEarthMap()
    if (moremap_s.y == map_b.y && moremap_s.x != 0) {
        updateCircumstances();
        #if defined(DEBUG_ZONES_BB)
            fillSBox (map_b, RA8875_BLACK);
        #endif // DEBUG_ZONES_BB
    }
    
    uint16_t last_x = map_b.x + EARTH_W - 1;

#if defined(_IS_ESP8266)

    // freeze if showing a temporary DX-DE path
    if (waiting4DXPath())
        return;

    // draw all symbols when hit first one after start of sweep, maid key right away
    static bool drew_symbols;
    if (moremap_s.y == map_b.y) {
        drew_symbols = false;
        drawMaidGridKey();
    }

    // draw next row, avoid symbols but note when hit
    resetWatchdog();
    bool hit_symbol = false;
    for (moremap_s.x = map_b.x; moremap_s.x <= last_x; moremap_s.x++) {

        // make symbols appear as overlaied by not drawing map over them.
        if (overAnySymbol (moremap_s))
            hit_symbol = true;
        else
            drawMapCoord (moremap_s);           // also draws grid
    }

    // draw symbols first time hit
    if (!drew_symbols && hit_symbol) {
        drawAllSymbols(true);
        drew_symbols = true;
    }

    // overlay any sat lines on this row except map scale
    // N.B. can't use !inBox(moremap_s, mapscale_b) because .x is off the map now
    if (!mapScaleIsUp() || moremap_s.y < mapscale_b.y || moremap_s.y > mapscale_b.y + mapscale_b.h) {
        drawSatPointsOnRow (moremap_s.y);
        drawSatNameOnRow (moremap_s.y);
    }

    // advance row and wrap and reset at the end
    if ((moremap_s.y += 1) >= map_b.y + EARTH_H)
        moremap_s.y = map_b.y;

    // check for map menu after each row
    if (mapmenu_pending) {
        drawMapMenu();
        mapmenu_pending = false;
    }

#endif  // _IS_ESP8266

#if defined(_IS_UNIX)

    // draw next row
    for (moremap_s.x = map_b.x; moremap_s.x <= last_x; moremap_s.x++)
        drawMapCoord (moremap_s);               // does not draw grid

    // advance row, wrap and reset and finish up at the end
    if ((moremap_s.y += 1) >= map_b.y + EARTH_H) {
        moremap_s.y = map_b.y;

        drawMapGrid();
        drawSatPathAndFoot();
        if (waiting4DXPath())
            drawDXPath();
        drawPSKPaths ();
        drawAllSymbols(true);
        drawSatNameOnRow (0);
        drawMouseLoc();

        // draw now
        tft.drawPR();

        // check for map menu after each full map
        if (mapmenu_pending) {
            drawMapMenu();
            mapmenu_pending = false;
        }

    // define TIME_MAP_DRAW
    #if defined(TIME_MAP_DRAW)
        static struct timeval tv0;
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        if (tv0.tv_sec != 0)
            Serial.printf ("****** map %ld us\n", TVDELUS (tv0, tv1));
        tv0 = tv1;
    #endif // TIME_MAP_DRAW

    }

#endif // _IS_UNIX

}

/* convert lat and long in radians to screen coords.
 * keep result no closer than the given edge distance.
 * the first overload wants rads, the second wants fully populated LatLong
 */
void ll2s (float lat, float lng, SCoord &s, uint8_t edge)
{
    LatLong ll;
    ll.lat = lat;
    ll.lat_d = rad2deg(ll.lat);
    ll.lng = lng;
    ll.lng_d = rad2deg(ll.lng);
    ll2s (ll, s, edge);
}
void ll2s (const LatLong &ll, SCoord &s, uint8_t edge)
{
    resetWatchdog();

    switch ((MapProjection)map_proj) {

    case MAPP_AZIMUTHAL: {

        // sph tri between de, dx and N pole
        float ca, B;
        solveSphere (ll.lng - de_ll.lng, M_PI_2F-ll.lat, sdelat, cdelat, &ca, &B);
        if (ca > 0) {
            // front (left) side, centered at DE
            float a = acosf (ca);
            float R = fminf (a*map_b.w/(2*M_PIF), map_b.w/4 - edge - 1);        // well clear
            float dx = R*sinf(B);
            float dy = R*cosf(B);
            s.x = roundf(map_b.x + map_b.w/4 + dx);
            s.y = roundf(map_b.y + map_b.h/2 - dy);
        } else {
            // back (right) side, centered at DE antipode
            float a = M_PIF - acosf (ca);
            float R = fminf (a*map_b.w/(2*M_PIF), map_b.w/4 - edge - 1);        // well clear
            float dx = -R*sinf(B);
            float dy = R*cosf(B);
            s.x = roundf(map_b.x + 3*map_b.w/4 + dx);
            s.y = roundf(map_b.y + map_b.h/2 - dy);
        }
        } break;

    case MAPP_AZIM1: {

        // sph tri between de, dx and N pole
        float ca, B;
        solveSphere (ll.lng - de_ll.lng, M_PI_2F-ll.lat, sdelat, cdelat, &ca, &B);
        float a = AZIM1_ZOOM*acosf (ca);
        float R = fminf (map_b.h/2*powf(a/M_PIF,1/AZIM1_FISHEYE), map_b.h/2 - edge - 1);
        float dx = R*sinf(B);
        float dy = R*cosf(B);
        s.x = roundf(map_b.x + map_b.w/2 + dx);
        s.y = roundf(map_b.y + map_b.h/2 - dy);
        } break;

    case MAPP_MERCATOR: {

        // straight rectangular Mercator projection
        s.x = roundf(map_b.x + map_b.w*fmodf(ll.lng_d-getCenterLng()+540,360)/360);
        s.y = roundf(map_b.y + map_b.h*(90-ll.lat_d)/180);

        // guard edge
        uint16_t e;
        e = map_b.x + edge;
        if (s.x < e)
            s.x = e;
        e = map_b.x + map_b.w - edge - 1;
        if (s.x > e)
            s.x = e;
        e = map_b.y + edge;
        if (s.y < e)
            s.y = e;
        e = map_b.y + map_b.h - edge - 1;
        if (s.y > e)
            s.y = e;
        } break;

    case MAPP_ROB:
        ll2sRobinson (ll, s, edge, 1);
        break;

    default:
        fatalError (_FX("ll2s() bad map_proj %d"), map_proj);
    }

}


/* same but with explicit lat/lng in rads
 */
void ll2sRaw (float lat, float lng, SCoord &s, uint8_t edge)
{
    LatLong ll;
    ll.lat = lat;
    ll.lat_d = rad2deg(ll.lat);
    ll.lng = lng;
    ll.lng_d = rad2deg(ll.lng);
    ll2sRaw (ll, s, edge);
}
void ll2sRaw (const LatLong &ll, SCoord &s, uint8_t edge)
{
    resetWatchdog();

    uint16_t map_x = tft.SCALESZ*map_b.x;
    uint16_t map_y = tft.SCALESZ*map_b.y;
    uint16_t map_w = tft.SCALESZ*map_b.w;
    uint16_t map_h = tft.SCALESZ*map_b.h;

    switch ((MapProjection)map_proj) {

    case MAPP_AZIMUTHAL: {
        // sph tri between de, dx and N pole
        float ca, B;
        solveSphere (ll.lng - de_ll.lng, M_PI_2F-ll.lat, sdelat, cdelat, &ca, &B);
        if (ca > 0) {
            // front (left) side, centered at DE
            float a = acosf (ca);
            float R = fminf (a*map_w/(2*M_PIF), map_w/4 - edge - 1);        // well clear
            float dx = R*sinf(B);
            float dy = R*cosf(B);
            s.x = roundf(map_x + map_w/4 + dx);
            s.y = roundf(map_y + map_h/2 - dy);
        } else {
            // back (right) side, centered at DE antipode
            float a = M_PIF - acosf (ca);
            float R = fminf (a*map_w/(2*M_PIF), map_w/4 - edge - 1);        // well clear
            float dx = -R*sinf(B);
            float dy = R*cosf(B);
            s.x = roundf(map_x + 3*map_w/4 + dx);
            s.y = roundf(map_y + map_h/2 - dy);
        }
        } break;

    case MAPP_AZIM1: {
        // sph tri between de, dx and N pole
        float ca, B;
        solveSphere (ll.lng - de_ll.lng, M_PI_2F-ll.lat, sdelat, cdelat, &ca, &B);
        float a = AZIM1_ZOOM*acosf (ca);
        float R = fminf (map_h/2*powf(a/M_PIF,1/AZIM1_FISHEYE), map_h/2 - edge - 1);
        float dx = R*sinf(B);
        float dy = R*cosf(B);
        s.x = roundf(map_x + map_w/2 + dx);
        s.y = roundf(map_y + map_h/2 - dy);
        } break;

    case MAPP_MERCATOR: {

        // straight rectangular Mercator projection
        s.x = roundf(map_x + map_w*fmodf(ll.lng_d-getCenterLng()+540,360)/360);
        s.y = roundf(map_y + map_h*(90-ll.lat_d)/180);

        // guard edge
        uint16_t e;
        e = map_x + edge;
        if (s.x < e)
            s.x = e;
        e = map_x + map_w - edge - 1;
        if (s.x > e)
            s.x = e;
        e = map_y + edge;
        if (s.y < e)
            s.y = e;
        e = map_y + map_h - edge - 1;
        if (s.y > e)
            s.y = e;
        } break;

    case MAPP_ROB:
        ll2sRobinson (ll, s, edge, tft.SCALESZ);
        break;

    default:
        fatalError (_FX("ll2sRaw() bad map_proj %d"), map_proj);
    }

}

/* convert a screen coord to lat and long.
 * return whether location is really over valid map.
 */
bool s2ll (uint16_t x, uint16_t y, LatLong &ll)
{
    SCoord s;
    s.x = x;
    s.y = y;
    return (s2ll (s, ll));
}
bool s2ll (const SCoord &s, LatLong &ll)
{
    // avoid map
    if (!overMap(s))
        return (false);

    switch ((MapProjection)map_proj) {

    case MAPP_AZIMUTHAL: {
        // radius from center of point's hemisphere
        bool on_right = s.x > map_b.x + map_b.w/2;
        float dx = on_right ? s.x - (map_b.x + 3*map_b.w/4) : s.x - (map_b.x + map_b.w/4);
        float dy = (map_b.y + map_b.h/2) - s.y;
        float r2 = dx*dx + dy*dy;

        // see if really on surface
        float w2 = map_b.w*map_b.w/16;
        if (r2 > w2)
            return(false);

        // use screen triangle to find globe
        float b = sqrtf((float)r2/w2)*(M_PI_2F);
        float A = (M_PI_2F) - atan2f (dy, dx);
        float ca, B;
        solveSphere (A, b, (on_right ? -1 : 1) * sdelat, cdelat, &ca, &B);
        ll.lat = M_PI_2F - acosf(ca);
        ll.lat_d = rad2deg(ll.lat);
        ll.lng = fmodf (de_ll.lng + B + (on_right?6:5)*M_PIF, 2*M_PIF) - M_PIF;
        ll.lng_d = rad2deg(ll.lng);

        } break;

    case MAPP_AZIM1: {

        // radius from center
        float dx = s.x - (map_b.x + map_b.w/2);
        float dy = (map_b.y + map_b.h/2) - s.y;
        float r2 = dx*dx + dy*dy;

        // see if really on surface
        float h2 = map_b.h*map_b.h/4;
        if (r2 > h2)
            return(false);

        // use screen triangle to find globe
        float b = powf((float)r2/h2, AZIM1_FISHEYE/2.0F) * M_PIF / AZIM1_ZOOM;     // /2 just for sqrt
        float A = (M_PI_2F) - atan2f (dy, dx);
        float ca, B;
        solveSphere (A, b, sdelat, cdelat, &ca, &B);
        ll.lat = M_PI_2F - acosf(ca);
        ll.lat_d = rad2deg(ll.lat);
        ll.lng = fmodf (de_ll.lng + B + 5*M_PIF, 2*M_PIF) - M_PIF;
        ll.lng_d = rad2deg(ll.lng);

        } break;

    case MAPP_MERCATOR: {

        // straight rectangular mercator projection

        ll.lat_d = 90 - 180.0F*(s.y - map_b.y)/(EARTH_H);
        ll.lng_d = fmodf(360.0F*(s.x - map_b.x)/(EARTH_W)+getCenterLng()+720,360) - 180;
        normalizeLL(ll);

        } break;

    case MAPP_ROB:

        return (s2llRobinson (s, ll));
        break;

    default:
        fatalError (_FX("s2ll() bad map_proj %d"), map_proj);
    }


    return (true);
}

/* given numeric difference between two longitudes in degrees, return shortest diff
 */
float lngDiff (float dlng)
{
    float fdiff = fmodf(fabsf(dlng + 720), 360);
    if (fdiff > 180)
        fdiff = 360 - fdiff;
    return (fdiff);
}


/* draw at the given screen location, if it's over the map.
 * ESP also draws the grid with this one point at a time.
 */
void drawMapCoord (uint16_t x, uint16_t y)
{

    SCoord s;
    s.x = x;
    s.y = y;
    drawMapCoord (s);
}
void drawMapCoord (const SCoord &s)
{

    #if defined(_IS_ESP8266)

        // draw one pixel, which might be an annotation line if over map


        // find lat/lng at this screen location, done if not over map
        LatLong lls;
        if (!s2ll(s, lls))
            return;

        // a latitude cache really helps Mercator performance; anything help others?
        static float slat_c, clat_c;
        static SCoord s_c;
        if (map_proj != MAPP_MERCATOR || s.y != s_c.y) {
            s_c = s;
            slat_c = sinf(lls.lat);
            clat_c = cosf(lls.lat);
        }

        // location tolerance to be considered on a grid line
        #define DLAT        1.0F
        #define DLNG        (1.0F/clat_c)

        switch ((MapGridStyle)mapgrid_choice) {

        case MAPGRID_LATLNG:

            if (map_proj != MAPP_MERCATOR) {

                if (fmodf(lls.lat_d+90, LL_LAT_GRID) < DLAT || fmodf (lls.lng_d+180, LL_LNG_GRID) < DLNG) {
                    uint32_t grid_c = (fabsf (lls.lat_d) < DLAT || fabsf (lls.lng_d) < DLNG) ? GRIDC00:GRIDC;
                    tft.drawPixel (s.x, s.y, grid_c);
                    return;                                         // done
                }

            } else {

                // extra gymnastics are because pixels-per-division is not integral and undo getCenterLng
                #define ALL_PPLG (EARTH_W/(360/LL_LNG_GRID))
                #define ALL_PPLT (EARTH_H/(180/LL_LAT_GRID))
                uint16_t x = map_b.x + ((s.x - map_b.x + map_b.w + map_b.w*getCenterLng()/360) % map_b.w);
                if ( (((x - map_b.x) - (x - map_b.x)/(2*ALL_PPLG)) % ALL_PPLG) == 0
                                    || (((s.y - map_b.y) - (s.y - map_b.y)/(2*ALL_PPLT)) % ALL_PPLT) == 0) {
                    uint32_t grid_c = (fabsf (lls.lat_d) < DLAT || fabsf (lls.lng_d) < DLNG) ? GRIDC00:GRIDC;
                    tft.drawPixel (s.x, s.y, grid_c);
                    return;                                         // done
                }
            }

            break;

        case MAPGRID_TROPICS:

            if (map_proj != MAPP_MERCATOR) {

                if (fabsf (fabsf (lls.lat_d) - 23.5F) < 0.3F) {
                    tft.drawPixel (s.x, s.y, GRIDC);
                    return;                                         // done
                }

            } else {

                // we already know exactly where the grid lines go.
                if (abs(s.y - (map_b.y+EARTH_H/2)) == (uint16_t)((23.5F/180)*(EARTH_H))) {
                    tft.drawPixel (s.x, s.y, GRIDC);
                    return;                                         // done
                }
            }
            break;

        case MAPGRID_MAID:

            if (map_proj != MAPP_MERCATOR) {

                if (fmodf(lls.lat_d+90, 10) < DLAT || fmodf (lls.lng_d+180, 20) < DLNG) {
                    uint32_t grid_c = (fabsf (lls.lat_d) < DLAT || fabsf (lls.lng_d) < DLNG) ? GRIDC00:GRIDC;
                    tft.drawPixel (s.x, s.y, grid_c);
                    return;                                         // done
                }

            } else {

                // extra gymnastics are because pixels-per-division is not integral and undo getCenterLng
                #define MAI_PPLG (EARTH_W/(360/20))
                #define MAI_PPLT (EARTH_H/(180/10))
                uint16_t x = map_b.x + ((s.x - map_b.x + map_b.w + map_b.w*getCenterLng()/360) % map_b.w);
                if ( (((x - map_b.x) - 2*(x - map_b.x)/(3*MAI_PPLG)) % MAI_PPLG) == 0
                                    || (((s.y - map_b.y) - (s.y - map_b.y)/(3*MAI_PPLT)) % MAI_PPLT) == 0) {
                    tft.drawPixel (s.x, s.y, GRIDC);
                    return;                                         // done
                }
            }

            break;

        case MAPGRID_AZIM: {

            // find radial coords and see if this pixel falls on the pattern.
            float ca, B;
            solveSphere (lls.lng - de_ll.lng, M_PIF/2 - lls.lat, sdelat, cdelat, &ca, &B);
            float radius = rad2deg(acosf(ca));                      // radius of ray from DE
            float theta = rad2deg(B);                               // theta angle of ring around DE
            float th_cutoff = 0;                                    // theta thickness

            switch ((MapProjection)map_proj) {
            case MAPP_MERCATOR:
                th_cutoff = 2 * cosf(lls.lat);                      // pole sweeps subtend tiny angles
                if (radius < RADIAL_GRID || radius > 180 - RADIAL_GRID)
                    th_cutoff += 2;                                 // insure thicker at DE and antipode
                break;
            case MAPP_ROB:                                          // fallthru
            case MAPP_AZIMUTHAL:
                th_cutoff = 1+ca*ca;                                // fat @ 0 .. thin at 90 .. fat @ 180
                break;
            case MAPP_AZIM1:
                th_cutoff = ca + 1.5F;                              // thinner all the way to 180
                break;
            default:
                fatalError(_FX("drawMapCoord() bogus map_proj %d"), map_proj);
            }

            if (fmodf (radius+90, RADIAL_GRID) < 1 || fmodf (theta+180, THETA_GRID) < th_cutoff) {
                tft.drawPixel (s.x, s.y, GRIDC);
                return;                                             // done
            }

            }

            break;

        case MAPGRID_OFF:
            break;

        default:
            fatalError (_FX("drawMapCoord() bad mapgrid_choice: %d"), mapgrid_choice);
            break;

        }

        // if get here we did not draw a grid point

        // find angle between subsolar point and this location
        float cos_t = ssslat*slat_c + csslat*clat_c*cosf(sun_ss_ll.lng-lls.lng);

        uint16_t pix_c = getEarthMapPix (lls, cos_t);
        tft.drawPixel (s.x, s.y, pix_c);

        // preserve for next call
        s_c = s;


    #else // !_IS_ESP8266


        // draw one map pixel at full screen resolution. requires lat/lng gradients.

        // find lat/lng at this screen location, bale if not over map
        LatLong lls;
        if (!s2ll(s,lls))
            return; 

        /* even though we only draw one application point, s, plotEarth needs points r and d to
         * interpolate to full map resolution.
         *   s - - - r
         *   |
         *   d
         */
        SCoord sr, sd;
        LatLong llr, lld;
        sr.x = s.x + 1;
        sr.y = s.y;
        if (!s2ll(sr,llr))
            llr = lls;
        sd.x = s.x;
        sd.y = s.y + 1;
        if (!s2ll(sd,lld))
            lld = lls;

        // find angle between subsolar point and any visible near this location
        // TODO: actually different at each subpixel, this causes striping
        float clat = cosf(lls.lat);
        float slat = sinf(lls.lat);
        float cos_t = ssslat*slat + csslat*clat*cosf(sun_ss_ll.lng-lls.lng);

        // decide day, night or twilight
        float fract_day;
        if (!night_on || cos_t > 0) {
            // < 90 deg: sunlit
            fract_day = 1;
        } else if (cos_t > GRAYLINE_COS) {
            // blend from day to night
            fract_day = 1 - powf(cos_t/GRAYLINE_COS, GRAYLINE_POW);
        } else {
            // night side
            fract_day = 0;
        }

        // draw the full res map point
        tft.plotEarth (s.x, s.y, lls.lat_d, lls.lng_d, llr.lat_d - lls.lat_d, llr.lng_d - lls.lng_d,
                    lld.lat_d - lls.lat_d, lld.lng_d - lls.lng_d, fract_day);

    #endif  // _IS_ESP8266

}

/* draw sun symbol.
 * N.B. we assume sun_c coords insure marker will be wholy within map boundaries.
 */
void drawSun ()
{
    resetWatchdog();
    
    // draw at full display precision

    #define      N_SUN_RAYS      8

    const uint16_t raw_x = tft.SCALESZ * sun_c.s.x;
    const uint16_t raw_y = tft.SCALESZ * sun_c.s.y;
    const uint16_t sun_r = tft.SCALESZ * SUN_R;
    const uint16_t body_r = sun_r/2;
    tft.fillCircleRaw (raw_x, raw_y, sun_r, RA8875_BLACK);
    tft.fillCircleRaw (raw_x, raw_y, body_r, RA8875_YELLOW);
    for (uint8_t i = 0; i < N_SUN_RAYS; i++) {
        float a = i*2*M_PIF/N_SUN_RAYS;
        float sa = sinf(a);
        float ca = cosf(a);
        uint16_t x0 = raw_x + roundf ((body_r+tft.SCALESZ)*ca);
        uint16_t y0 = raw_y + roundf ((body_r+tft.SCALESZ)*sa);
        uint16_t x1 = raw_x + roundf (sun_r*ca);
        uint16_t y1 = raw_y + roundf (sun_r*sa);
        tft.drawLineRaw (x0, y0, x1, y1, tft.SCALESZ-1, RA8875_YELLOW);
    }

#   undef N_SUN_RAYS
}

/* draw moon symbol.
 * N.B. we assume moon_c coords insure marker will be wholy within map boundaries.
 */
void drawMoon ()
{
    resetWatchdog();

    float phase = lunar_cir.phase;
    
    // draw at full display precision

    const uint16_t raw_r = MOON_R*tft.SCALESZ;
    const uint16_t raw_x = tft.SCALESZ * moon_c.s.x;
    const uint16_t raw_y = tft.SCALESZ * moon_c.s.y;
    for (int16_t dy = -raw_r; dy <= raw_r; dy++) {      // scan top to bottom
        float Ry = sqrtf(raw_r*raw_r-dy*dy);            // half-width at y
        int16_t Ryi = roundf(Ry);                       // " as int
        for (int16_t dx = -Ryi; dx <= Ryi; dx++) {      // scan left to right at y
            float a = acosf(dx/Ry);                     // looking down from NP CW from right limb
            uint16_t color = (isnan(a) || (phase > 0 && a > phase) || (phase < 0 && a < phase+M_PIF))
                                ? RA8875_BLACK : RA8875_WHITE;
            tft.drawPixelRaw (raw_x+dx, raw_y+dy, color);
        }
    }
}

/* display some info about DX location in dx_info_b
 */
void drawDXInfo ()
{
    resetWatchdog();

    // skip if dx_info_b being used for sat info
    if (dx_info_for_sat)
        return;

    // divide into 5 rows
    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    // time
    drawDXTime();

    // erase and init
    tft.graphicsMode();
    tft.fillRect (dx_info_b.x, dx_info_b.y+2*vspace, dx_info_b.w, dx_info_b.h-2*vspace-1, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (DX_COLOR);

    // lat and long
    char buf[50];
    snprintf (buf, sizeof(buf), _FX("%.0f%c  %.0f%c"),
                roundf(fabsf(dx_ll.lat_d)), dx_ll.lat_d < 0 ? 'S' : 'N',
                roundf(fabsf(dx_ll.lng_d)), dx_ll.lng_d < 0 ? 'W' : 'E');
    tft.setCursor (dx_info_b.x, dx_info_b.y+3*vspace-8);
    tft.print(buf);
    uint16_t bw, bh;
    getTextBounds (buf, &bw, &bh);

    // maidenhead
    drawMaidenhead(NV_DX_GRID, dx_maid_b, DX_COLOR);

    // compute dist and bearing in desired units
    float dist, bearing;
    propDEPath (show_lp, dx_ll, &dist, &bearing);
    dist *= ERAD_M;                             // angle to miles
    bearing *= 180/M_PIF;                       // rad -> degrees
    if (useMetricUnits())
        dist *= KM_PER_MI;

    // convert to magnetic if desired
    bool bearing_ismag = desiredBearing (de_ll, bearing);

    // print, capturing where units and deg/path can go
    tft.setCursor (dx_info_b.x, dx_info_b.y+5*vspace-4);
    tft.printf ("%.0f", dist);
    uint16_t units_x = tft.getCursorX()+2;
    tft.setCursor (units_x + 6, tft.getCursorY());
    tft.printf ("@%.0f", bearing);
    uint16_t deg_x = tft.getCursorX() + 3;
    uint16_t deg_y = tft.getCursorY();

    // home-made degree symbol if true, else M for magnetic
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (deg_x, deg_y-bh-bh/6);
    tft.print (bearing_ismag ? 'M' : 'o');

    // rows for small chars
    uint16_t sm_y0 = deg_y - 13*bh/20;
    uint16_t sm_y1 = deg_y - 6*bh/20;

    // path direction
    tft.setCursor (deg_x, sm_y0);
    tft.print (show_lp ? 'L' : 'S');
    tft.setCursor (deg_x, sm_y1);
    tft.print ('P');

    // distance units
    tft.setCursor (units_x, sm_y0);
    tft.print(useMetricUnits() ? 'k' : 'm');
    tft.setCursor (units_x, sm_y1);
    tft.print(useMetricUnits() ? 'm' : 'i');

    // sun rise/set or prefix
    if (dxsrss == DXSRSS_PREFIX) {
        char prefix[MAX_PREF_LEN+1];
        fillSBox (dxsrss_b, RA8875_BLACK);
        if (getDXPrefix (prefix)) {
            tft.setTextColor(DX_COLOR);
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            bw = getTextWidth (prefix);
            tft.setCursor (dxsrss_b.x+(dxsrss_b.w-bw)/2, dxsrss_b.y + 29);
            tft.print (prefix);
        }
    } else {
        drawDXSunRiseSetInfo();
    }
}

/* return whether s is over DX path direction portion of dx_info_b
 */
bool checkPathDirTouch (const SCoord &s)
{
    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    SBox b;
    b.x = dx_info_b.x + dx_info_b.w/2;
    b.w = dx_info_b.w/2;
    b.y = dx_info_b.y + 4*vspace;
    b.h = vspace;

    return (inBox (s, b));
}

/* draw DX time unless in sat mode
 */
void drawDXTime()
{
    // skip if dx_info_b being used for sat info
    if (dx_info_for_sat)
        return;

    drawTZ (dx_tz);

    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    time_t utc = nowWO();
    time_t local = utc + dx_tz.tz_secs;
    int hr = hour (local);
    int mn = minute (local);
    int dy = day(local);
    int mo = month(local);

    tft.graphicsMode();
    tft.fillRect (dx_info_b.x, dx_info_b.y+vspace, dx_info_b.w, vspace, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (DX_COLOR);
    tft.setCursor (dx_info_b.x, dx_info_b.y+2*vspace-8);

    char buf[32];
    if (getDateFormat() == DF_MDY || getDateFormat() == DF_YMD)
        snprintf (buf, sizeof(buf), _FX("%02d:%02d %s %d"), hr, mn, monthShortStr(mo), dy);
    else
        snprintf (buf, sizeof(buf), _FX("%02d:%02d %d %s"), hr, mn, dy, monthShortStr(mo));
    tft.print(buf);
}

/* set `to' to the antipodal location of coords in `from'.
 */
void antipode (LatLong &to, const LatLong &from)
{
    to.lat_d = -from.lat_d;
    to.lng_d = from.lng_d+180;
    normalizeLL(to);
}

/* return whether s is over the view_btn_b, including an extra border for fat lines or DX etc
 */
bool overViewBtn (const SCoord &s, uint16_t border)
{
    border += 1;
    return (s.x < view_btn_b.x + view_btn_b.w + border && s.y < view_btn_b.y + view_btn_b.h + border);
}

/* return whether the given line segment spans a reasonable portion of the map.
 * beware map edge, view button, wrap-around and crossing center of azm map
 */
bool segmentSpanOk (const SCoord &s0, const SCoord &s1, uint16_t border)
{
    if (s0.x > s1.x ? (s0.x - s1.x > map_b.w/4) : (s1.x - s0.x > map_b.w/4))
        return (false);         // too wide
    if (s0.y > s1.y ? (s0.y - s1.y > map_b.h/3) : (s1.y - s0.y > map_b.h/3))
        return (false);         // too hi
    if (map_proj == MAPP_AZIMUTHAL && ((s0.x < map_b.x+map_b.w/2) != (s1.x < map_b.x+map_b.w/2)))
        return (false);         // crosses azimuthal hemispheres
    if (overViewBtn(s0,border) || overViewBtn(s1,border))
        return (false);         // over the view button
    if (!overMap(s0) || !overMap(s1))
        return (false);         // off the map entirely
    return (true);              // ok!
}

/* return whether the given line segment spans a reasonable portion of the map.
 * beware map edge, view button, wrap-around and crossing center of azm map
 * coords are in raw pixels.
 */
bool segmentSpanOkRaw (const SCoord &s0, const SCoord &s1, uint16_t border)
{
    uint16_t map_x = tft.SCALESZ*map_b.x;
    uint16_t map_w = tft.SCALESZ*map_b.w;
    uint16_t map_h = tft.SCALESZ*map_b.h;

    if (s0.x > s1.x ? (s0.x - s1.x > map_w/4) : (s1.x - s0.x > map_w/4))
        return (false);         // too wide
    if (s0.y > s1.y ? (s0.y - s1.y > map_h/3) : (s1.y - s0.y > map_h/3))
        return (false);         // too hi
    if (map_proj == MAPP_AZIMUTHAL && ((s0.x < map_x+map_w/2) != (s1.x < map_x+map_w/2)))
        return (false);         // crosses azimuthal hemisphere
    if (overViewBtn(raw2appSCoord(s0),border/tft.SCALESZ)
                                || overViewBtn(raw2appSCoord(s1),border/tft.SCALESZ))
        return (false);         // over the view button
    if (!overMap(raw2appSCoord(s0)) || !overMap(raw2appSCoord(s1)))
        return (false);         // off the map entirely
    return (true);              // ok!
}
