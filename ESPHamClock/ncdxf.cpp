/* Northen California DX Foundation Beacon Network.
 * http://www.ncdxf.org/beacon/index.html#Schedule
 */

/* manage the Northern California DX Foundation beacons.
 *
 * Each beacon is drawn as a colored triangle symbol with call sign text below. The triangle is drawn
 * to high res so is redrawn after being scanned. But the text is just jumped over and never redrawn.
 */

#include "HamClock.h"


#define NBEACONS        18                      // number of beacons
#define BEACONR         9                       // beacon symbol radius, pixels
#define BLEG            (BEACONR-4)             // beacon symbol leg length
#define BEACONCW        6                       // beacon char width
#define BORDER_COL      RGB565(127,127,127)     // control box border color

typedef struct {
    int16_t lat, lng;                           // location, degs north and east
    char call[7];                               // call sign
    SCoord s;                                   // screen coord of triangle symbol center
    uint16_t c;                                 // color
    SBox call_b;                                // enclosing background box
} NCDXFBeacon;

/* listed in order of 14, 18, 21, 24 and 28 MHz starting at 3N minutes after the hour.
 * 4 of the 18 stations each transmit for 10 seconds then rotate down.
 *
 * given s seconds after the hour, find index for each frequency:
 *   14 MHz i = (s/10+0+NBEACONS)%NBEACONS
 *   18 MHz i = (s/10-1+NBEACONS)%NBEACONS
 *   21 MHz i = (s/10-2+NBEACONS)%NBEACONS
 *   24 MHz i = (s/10-3+NBEACONS)%NBEACONS
 *   28 MHz i = (s/10-4+NBEACONS)%NBEACONS
 */
static NCDXFBeacon blist[NBEACONS] = {
    {  41,  -74, "4U1UN",  {0,0}, 0, {0,0,0,0}},
    {  68, -133, "VE8AT",  {0,0}, 0, {0,0,0,0}},
    {  37, -122, "W6WX",   {0,0}, 0, {0,0,0,0}},
    {  21, -156, "KH6RS",  {0,0}, 0, {0,0,0,0}},
    { -41,  176, "ZL6B",   {0,0}, 0, {0,0,0,0}},
    { -32,  116, "VK6RBP", {0,0}, 0, {0,0,0,0}},
    {  34,  137, "JA2IGY", {0,0}, 0, {0,0,0,0}},
    {  55,   83, "RR9O",   {0,0}, 0, {0,0,0,0}},
    {  22,  114, "VR2B",   {0,0}, 0, {0,0,0,0}},
    {   7,   80, "4S7B",   {0,0}, 0, {0,0,0,0}},
    { -26,   28, "ZS6DN",  {0,0}, 0, {0,0,0,0}},
    {  -1,   37, "5Z4B",   {0,0}, 0, {0,0,0,0}},
    {  32,   35, "4X6TU",  {0,0}, 0, {0,0,0,0}},
    {  60,   25, "OH2B",   {0,0}, 0, {0,0,0,0}},
    {  33,  -17, "CS3B",   {0,0}, 0, {0,0,0,0}},
    { -35,  -58, "LU4AA",  {0,0}, 0, {0,0,0,0}},
    { -12,  -77, "OA4B",   {0,0}, 0, {0,0,0,0}},
    {   9,  -68, "YV5B",   {0,0}, 0, {0,0,0,0}},
};


/* symbol color for each frequency
 */
#if defined(_SUPPORT_PSKESP)
// no getMapColor for band colors
#define BCOL_14 RA8875_RED              // 14.100 MHz
#define BCOL_18 RA8875_GREEN            // 18.110 MHz
#define BCOL_21 RGB565(100,100,255)     // 21.150 MHz
#define BCOL_24 RA8875_YELLOW           // 24.930 MHz
#define BCOL_28 RGB565(255,125,0)       // 28.200 MHz
#else
#define BCOL_14 getMapColor(BAND20_CSPR)
#define BCOL_18 getMapColor(BAND17_CSPR)
#define BCOL_21 getMapColor(BAND15_CSPR)
#define BCOL_24 getMapColor(BAND12_CSPR)
#define BCOL_28 getMapColor(BAND10_CSPR)
#endif
#define BCOL_S  RA8875_BLACK            // silent, not actually drawn
#define BCOL_N  6                       // number of color states



/* using the current user time set the color state for each beacon.
 */
static void setBeaconStates ()
{
    time_t t = nowWO();
    int mn = minute(t);
    int sc = second(t);
    uint16_t s_10 = (60*mn + sc)/10;

    for (int id = 0; id < NBEACONS; id++)
        blist[id].c = BCOL_S;

    blist[(s_10-0+NBEACONS)%NBEACONS].c = BCOL_14;
    blist[(s_10-1+NBEACONS)%NBEACONS].c = BCOL_18;
    blist[(s_10-2+NBEACONS)%NBEACONS].c = BCOL_21;
    blist[(s_10-3+NBEACONS)%NBEACONS].c = BCOL_24;
    blist[(s_10-4+NBEACONS)%NBEACONS].c = BCOL_28;
}


/* draw beacon symbol centered at given screen location with the given color
 */
static void drawBeaconSymbol (const SCoord &s, uint16_t c)
{
    tft.fillTriangle (s.x, s.y-BEACONR, s.x-9*BEACONR/10, s.y+BEACONR/2,
                s.x+9*BEACONR/10, s.y+BEACONR/2, RA8875_BLACK);
    tft.fillTriangle (s.x, s.y-BLEG, s.x-9*BLEG/10, s.y+BLEG/2, s.x+9*BLEG/10, s.y+BLEG/2, c);
}

/* draw the given beacon, including callsign beneath.
 */
static void drawBeacon (NCDXFBeacon &nb)
{
    // triangle symbol
    drawBeaconSymbol (nb.s, nb.c);

    // draw call sign
    drawMapTag (nb.call, nb.call_b);
}

/* erase beacon
 * only needed on ESP
 */
static void eraseBeacon (NCDXFBeacon &nb)
{
    resetWatchdog();

#if defined (_IS_ESP8266)

    // redraw map under symbol
    for (int8_t dy = -BEACONR; dy <= BEACONR/2; dy += 1) {
        int8_t hw = 3*(dy+BEACONR)/5+1;
        for (int8_t dx = -hw; dx <= hw; dx += 1)
            drawMapCoord (nb.s.x+dx, nb.s.y+dy);
    }

    // redraw map under call
    for (uint16_t y = nb.call_b.y; y < nb.call_b.y + nb.call_b.h; y++) {
        for (uint16_t x = nb.call_b.x; x < nb.call_b.x + nb.call_b.w; x++)
            drawMapCoord (x, y);
    }

#endif
}


/* return whether the given point is anywhere inside a beacon symbol or call
 */
static bool overBeacon (const SCoord &s, const NCDXFBeacon &nb)
{
    // check call
    if (inBox (s, nb.call_b))
        return (true);

    // check above or below symbol
    if (s.y < nb.s.y - BEACONR || s.y > nb.s.y + BEACONR/2)
        return (false);

    // distance below top tip
    uint16_t dy = s.y - (nb.s.y - BEACONR);

    // width at this y (same as eraseBeacon)
    int8_t hw = 3*dy/5+1;

    // left or right
    if (s.x < nb.s.x - hw || s.x > nb.s.x + hw)
        return (false);

    // yup
    return (true);
}


/* update beacon display, typically on each 10 second period unless immediate.
 */
void updateBeacons (bool immediate)
{
    // counts as on as long as in rotation set, need not be in front now
    bool beacons_on = brb_rotset & (1 << BRB_SHOW_BEACONS);

    // process if immediate or (beacons are on and it's a new time period)
    static uint8_t prev_sec10;
    uint8_t sec10 = second(nowWO())/10;
    if (!immediate && (!beacons_on || sec10 == prev_sec10))
        return;
    prev_sec10 = sec10;

    resetWatchdog();

    // now update each beacon as required
    bool erased_any = false;
    setBeaconStates();
    for (NCDXFBeacon *bp = blist; bp < &blist[NBEACONS]; bp++) {
        if (bp->c == BCOL_S || !beacons_on) {
            eraseBeacon (*bp);
            erased_any = true;
        } else if (overMap(bp->s) && !overRSS (bp->call_b)) {
            drawBeacon (*bp);
        }
    }

    // draw other symbols in case erasing a beacon clobbered some -- beware recursion!
    if (erased_any)
        drawAllSymbols(false);

    updateClocks(false);

}

/* update screen location for all beacons.
 */
void updateBeaconScreenLocations()
{
    for (NCDXFBeacon *bp = blist; bp < &blist[NBEACONS]; bp++) {
        ll2s (deg2rad(bp->lat), deg2rad(bp->lng), bp->s, 3*BEACONCW);   // about max
        setMapTagBox (bp->call, bp->s, BEACONR/2+1, bp->call_b);
    }
}

/* return whether the given screen coord is over any visible map symbol or call sign box
 */
bool overAnyBeacon (const SCoord &s)
{
    if (!(brb_rotset & (1 << BRB_SHOW_BEACONS)))
        return (false);

    for (NCDXFBeacon *bp = blist; bp < &blist[NBEACONS]; bp++) {
        if (bp->c == BCOL_S)
            continue;
        if (overBeacon (s, *bp))
            return (true);
    }

    return (false);
}

/* draw the beacon key in NCDXF_b.
 */
void drawBeaconKey()
{
    // tiny font
    selectFontStyle (BOLD_FONT, FAST_FONT);

    // draw title
    tft.setCursor (NCDXF_b.x+13, NCDXF_b.y+2);
    tft.setTextColor (RA8875_WHITE);
    tft.print ("NCDXF");

    // draw each key

    SCoord s;
    s.x = NCDXF_b.x + BEACONR+1;
    s.y = NCDXF_b.y + 16 + BEACONR;
    uint8_t dy = (NCDXF_b.h-16)/(BCOL_N-1);         // silent color not drawn
    uint16_t c;
    
    c = BCOL_14;
    drawBeaconSymbol (s, c);
    tft.setTextColor (c);
    tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
    tft.print (F("14.10"));

    s.y += dy;
    c = BCOL_18;
    drawBeaconSymbol (s, c);
    tft.setTextColor (c);
    tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
    tft.print (F("18.11"));

    s.y += dy;
    c = BCOL_21;
    drawBeaconSymbol (s, c);
    tft.setTextColor (c);
    tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
    tft.print (F("21.15"));

    s.y += dy;
    c = BCOL_24;
    drawBeaconSymbol (s, c);
    tft.setTextColor (c);
    tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
    tft.print (F("24.93"));

    s.y += dy;
    c = BCOL_28;
    drawBeaconSymbol (s, c);
    tft.setTextColor (c);
    tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
    tft.print (F("28.20"));
}

/* draw any of the various contents in NCDXF_b depending on brb_mode.
 */
void drawNCDXFBox()
{
    // erase
    fillSBox (NCDXF_b, RA8875_BLACK);

    // draw appropriate content
    switch ((BRB_MODE)brb_mode) {

    case BRB_SHOW_BEACONS:

        drawBeaconKey();
        break;

    case BRB_SHOW_SWSTATS:

        drawSpaceStats();
        break;

    case BRB_SHOW_BME76:        // fallthru
    case BRB_SHOW_BME77:

        drawBMEStats();
        break;

    case BRB_SHOW_ONOFF:        // fallthru
    case BRB_SHOW_PHOT:         // fallthru
    case BRB_SHOW_BR:

        drawBrightness();
        break;

    case BRB_N:
        
        // lint
        break;
    }

    // border -- avoid base line
    tft.drawLine (NCDXF_b.x, NCDXF_b.y, NCDXF_b.x+NCDXF_b.w-1, NCDXF_b.y, GRAY);
    tft.drawLine (NCDXF_b.x, NCDXF_b.y, NCDXF_b.x, NCDXF_b.y+NCDXF_b.h-1, GRAY);
    tft.drawLine (NCDXF_b.x+NCDXF_b.w-1, NCDXF_b.y, NCDXF_b.x+NCDXF_b.w-1, NCDXF_b.y+NCDXF_b.h-1, GRAY);
}

/* common template to draw space weather or BME stats in NCDXF_b.
 */
void drawNCDXFStats (const char titles[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN],
                   const char values[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN],
                   const uint16_t colors[NCDXF_B_NFIELDS])
{
    // prep layout
    uint16_t y = NCDXF_b.y + 2;
    const int rect_dy = -23;
    const int rect_h = 26;

    // show each item
    for (int i = 0; i < NCDXF_B_NFIELDS; i++) {

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (NCDXF_b.x + (NCDXF_b.w-getTextWidth(titles[i]))/2, y);
        tft.print (titles[i]);

        y += 31;

        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (colors[i]);
        tft.fillRect (NCDXF_b.x+1, y+rect_dy, NCDXF_b.w-2, rect_h, RA8875_BLACK);
        tft.setCursor (NCDXF_b.x + (NCDXF_b.w-getTextWidth(values[i]))/2, y);
        tft.print (values[i]);

        y += 5;
    }
}

/* common template to respond to a touch in NCDXF_b showing a table of stats.
 */
void doNCDXFStatsTouch (const SCoord &s, PlotChoice pcs[NCDXF_B_NFIELDS])
{
    // decide which row
    int r = NCDXF_B_NFIELDS*(s.y - NCDXF_b.y)/NCDXF_b.h;
    if (r < 0 || r >= NCDXF_B_NFIELDS)
        fatalError(_FX("Bogus doNCDXFStatsTouch r %d"), r);       // never returns
                
    // decide which PLOT_CH 
    PlotChoice pc = pcs[r];
                    
    // done if the chosen pane is already on display
    if (findPaneChoiceNow (pc) != PANE_NONE)
        return; 
            
    // not on display, choose a pane to use
    PlotPane pp = PANE_NONE;
            
    // start by looking for a pane with the new stat already in its rotation set (we know it's not visible)
    for (int i = PANE_1; i < PANE_N; i++) {
        if (plot_rotset[i] & (1<<pc)) {
            pp = (PlotPane)i;
            break;
        } 
    }
            
    // else look for a pane with no related stats anywhere in its rotation set
    if (pp == PANE_NONE) {
        uint32_t pcs_mask = 0;
        for (int i = 0; i < NCDXF_B_NFIELDS; i++)
            pcs_mask |= (1 << pcs[i]);
        for (int i = PANE_1; i < PANE_N; i++) {
            if ((plot_rotset[i] & pcs_mask) == 0) {
                pp = (PlotPane)i;
                break;
            }
        }
    }

    // else just pick the pane next to the stats summary
    if (pp == PANE_NONE)
        pp = PANE_3;

    // install as only choice
    (void) setPlotChoice (pp, pc);
    plot_rotset[pp] = 1 << pc;
    savePlotOps();
}

/* init brb_rotset and brb_mode
 */
void initBRBRotset()
{
    if (!NVReadUInt8 (NV_BRB_ROTSET, &brb_rotset) || brb_rotset == 0) {
        brb_mode = BRB_SHOW_BEACONS;            // just pick one that is always possible
        brb_rotset = 1 << brb_mode;    
        NVWriteUInt8 (NV_BRB_ROTSET, brb_rotset);
    } else {
        // arbitrarily set brb_mode to first bit, will be double-checked later
        for (int i = 0; i < BRB_N; i++) {
            if (brb_rotset & (1 << i)) {
                brb_mode = i;
                break;
            }
        }
    }
}
