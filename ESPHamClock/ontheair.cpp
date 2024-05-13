/* manage the On The Air activation Panes for POTA and SOTA.
 */

#include "HamClock.h"


// names for each ONTA program
#define X(a,b)  b,                                      // expands ONTAPrograms to each name plus comma
const char *onta_names[ONTA_N] = {
    ONTAPrograms
};
#undef X


// config
#define POTA_COLOR      RGB565(150,250,255)             // title and spot text color
#define SOTA_COLOR      RGB565(250,0,0)                 // title and spot text color
#define COUNT_DY        32                              // dy of count
#define START_DY        47                              // dy of first row
#define ONTA_ROWDY      14                              // dy of each successive row
#define LL_PANE_0       23                              // line length for PANE_0
#define LL_PANE_123     27                              // line length for others


/* qsort-style function to compare two DXClusterSpot by freq
 */
static int ontaqsDXCFreq (const void *v1, const void *v2)
{
    DXClusterSpot *s1 = (DXClusterSpot *)v1;
    DXClusterSpot *s2 = (DXClusterSpot *)v2;
    return (s1->kHz - s2->kHz);
}

/* qsort-style function to compare two DXClusterSpot by de_call AKA id
 */
static int ontaqsDXCDECall (const void *v1, const void *v2)
{
    DXClusterSpot *s1 = (DXClusterSpot *)v1;
    DXClusterSpot *s2 = (DXClusterSpot *)v2;
    return (strcmp (s1->de_call, s2->de_call));
}

/* qsort-style function to compare two DXClusterSpot by dx_grid
 */
static int ontaqsDXCDXCall (const void *v1, const void *v2)
{
    DXClusterSpot *s1 = (DXClusterSpot *)v1;
    DXClusterSpot *s2 = (DXClusterSpot *)v2;
    return (strcmp (s1->dx_call, s2->dx_call));
}

/* qsort-style function to compare two DXClusterSpot by time spotted
 */
static int ontaqsDXCSpotted (const void *v1, const void *v2)
{
    DXClusterSpot *s1 = (DXClusterSpot *)v1;
    DXClusterSpot *s2 = (DXClusterSpot *)v2;
    return (s1->spotted - s2->spotted);
}


// menu names and functions for each sort type
typedef enum {
    ONTAS_BAND, 
    ONTAS_CALL,
    ONTAS_ID,
    ONTAS_AGE,
    ONTAS_N,
} ONTASort;

typedef struct {
    const char *menu_name;                      // menu name for this sort
    int (*qsf)(const void *v1, const void *v2); // matching qsort compare func
} ONTASortInfo;
static const ONTASortInfo onta_sorts[ONTAS_N] = {
    {"Band", ontaqsDXCFreq},
    {"Call", ontaqsDXCDXCall},
    {"Id",   ontaqsDXCDECall},
    {"Age",  ontaqsDXCSpotted},
};


// one ONTA state info
typedef struct {
    const char *page;                           // query page in PROGMEM
    const char *prog;                           // project name, SOTA POTA etc
    uint16_t color;                             // title color
    uint8_t whoami;                             // one of ONTAProgram
    uint8_t NV_id;                              // non-volatile memory NV_ id
    uint8_t PLOT_CH_id;                         // PLOT_CH_ id
    uint8_t sortby;                             // one of ONTASort
    ScrollState ss;                             // scroll state info
    DXClusterSpot *spots;                       // malloced collection, smallest sort field first
} ONTAState;

static const char pota_page[] PROGMEM = "/POTA/pota-activators.txt";
static const char sota_page[] PROGMEM = "/SOTA/sota-activators.txt";

// current program states
// N.B. must assign in same order as ONTAProgram
// N.B. page is in PROGMEM
static ONTAState onta_state[ONTA_N] = { 
    { pota_page, onta_names[ONTA_POTA], POTA_COLOR, ONTA_POTA, NV_ONTASPOTA, PLOT_CH_POTA, ONTAS_AGE},
    { sota_page, onta_names[ONTA_SOTA], SOTA_COLOR, ONTA_SOTA, NV_ONTASSOTA, PLOT_CH_SOTA, ONTAS_AGE},
};

static int max_spots;

/* save each ONTA sort choices
 */
static void saveONTASorts(void)
{
    for (int i = 0; i < ONTA_N; i++) {
        ONTAState *osp = &onta_state[i];
        NVWriteUInt8 ((NV_Name)osp->NV_id, osp->sortby);
    }
}

/* init each ONTA sort choice from NV
 */
static void initONTASorts(void)
{
    uint8_t sortby;

    for (int i = 0; i < ONTA_N; i++) {
        ONTAState *osp = &onta_state[i];
        if (!NVReadUInt8 ((NV_Name)osp->NV_id, &sortby) || sortby >= ONTAS_N) {
            sortby = ONTAS_AGE;
            NVWriteUInt8 ((NV_Name)osp->NV_id, sortby);
        }
        osp->sortby = sortby;
    }
}


/* create a line of text that fits within the box used for PANE_0 or PANE-123 for the given spot.
 */
static void formatONTASpot (const DXClusterSpot &spot, const ONTAState *osp, const SBox &box,
    char *line, size_t l_len, int &freq_len)
{
    if (BOX_IS_PANE_0(box)) {

        // n chars in each field; all lengths are sans EOS and intervening gaps
        const unsigned AGE_LEN = 3;
        const unsigned FREQ_LEN = 6;
        const unsigned CALL_LEN = (LL_PANE_0 - FREQ_LEN - AGE_LEN - 3);     // sans EOS and -2 spaces

        // pretty freq + trailing space
        size_t l = snprintf (line, l_len, _FX("%*.0f "), FREQ_LEN, spot.kHz);

        // return n chars in frequency
        freq_len = FREQ_LEN;

        // add dx call
        l += snprintf (line+l, l_len-l, _FX("%-*.*s "), CALL_LEN, CALL_LEN, spot.dx_call);

        // age on right, 3 columns
        int age = myNow() - spot.spotted;
        (void) formatAge (age, line+l, l_len-l, 3);

    } else {

        // n chars in each field; all lengths are sans EOS and intervening gaps
        const unsigned ID_LEN = osp->whoami == ONTA_POTA ? 7 : 10;
        const unsigned AGE_LEN = osp->whoami == ONTA_POTA ? 3 : 1;
        const unsigned FREQ_LEN = 6;
        const unsigned CALL_LEN = (LL_PANE_123 - FREQ_LEN - AGE_LEN - ID_LEN - 4);   // sans EOS and -3 spaces

        // pretty freq + trailing space
        size_t l = snprintf (line, l_len, _FX("%*.0f "), FREQ_LEN, spot.kHz);

        // return n chars in frequency
        freq_len = FREQ_LEN;

        // add dx call
        l += snprintf (line+l, l_len-l, _FX("%-*.*s "), CALL_LEN, CALL_LEN, spot.dx_call);

        // spot id
        l += snprintf (line+l, l_len-l, _FX("%-*.*s "), ID_LEN, ID_LEN, spot.de_call);

        // age on right
        int age = myNow() - spot.spotted;
        (void) formatAge (age, line+l, l_len-l, osp->whoami == ONTA_SOTA ? 1 : 3);
    }
}

/* redraw all visible otaspots in the given pane box.
 * N.B. this just draws the otaspots, use drawONTA to start from scratch.
 */
static void drawONTAVisSpots (const SBox &box, const ONTAState *osp)
{
    tft.fillRect (box.x+1, box.y + START_DY-1, box.w-2, box.h - START_DY - 1, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t x = box.x + 1;
    uint16_t y0 = box.y + START_DY;

    // draw otaspots top_vis on top
    int min_i, max_i;
    if (osp->ss.getVisIndices (min_i, max_i) > 0) {

        for (int i = min_i; i <= max_i; i++) {
            // get info line
            const DXClusterSpot &spot = osp->spots[i];
            char line[50];
            int freq_len;
            formatONTASpot (spot, osp, box, line, sizeof(line), freq_len);

            // set y location
            uint16_t y = y0 + osp->ss.getDisplayRow(i) * ONTA_ROWDY;

            // highlight overall bg if on watch list
            if (onSPOTAWatchList (spot.dx_call))
                tft.fillRect (x, y-1, box.w-2, ONTA_ROWDY-3, RA8875_RED);

            // show freq with proper band map color background
            uint16_t bg_col = getBandColor ((long)(spot.kHz*1000));           // wants Hz
            uint16_t txt_col = getGoodTextColor (bg_col);
            tft.setTextColor(txt_col);
            tft.fillRect (x, y-1, freq_len*6, ONTA_ROWDY-3, bg_col);
            tft.setCursor (x, y);
            tft.printf (_FX("%*.*s"), freq_len, freq_len, line);

            // show remainder of line in white
            tft.setTextColor(RA8875_WHITE);
            tft.printf (line+freq_len);
        }
    }

    // draw scroll controls, as needed
    osp->ss.drawScrollDownControl (box, osp->color);
    osp->ss.drawScrollUpControl (box, osp->color);
}

/* draw spots in the given pane box from scratch.
 * use drawONTAVisSpots() if want to redraw just the spots.
 */
static void drawONTA (const SBox &box, const ONTAState *osp)
{
    // prep
    prepPlotBox (box);

    // title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(osp->color);
    uint16_t pw = getTextWidth(osp->prog);
    tft.setCursor (box.x + (box.w-pw)/2, box.y + PANETITLE_H);
    tft.print (osp->prog);

    // show count
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor (box.x + (box.w-10)/2, box.y + COUNT_DY);
    tft.printf (_FX("%d"), osp->ss.n_data);

    // show each spot
    drawONTAVisSpots (box, osp);
}

/* scroll up, if appropriate to do so now.
 */
static void scrollONTAUp (const SBox &box, ONTAState *osp)
{
    if (osp->ss.okToScrollUp ()) {
        osp->ss.scrollUp ();
        drawONTAVisSpots (box, osp);
    }
}

/* scroll down, if appropriate to do so now.
 */
static void scrollONTADown (const SBox &box, ONTAState *osp)
{
    if (osp->ss.okToScrollDown()) {
        osp->ss.scrollDown ();
        drawONTAVisSpots (box, osp);
    }
}

/* set radio and new DX from given spot
 */
static void engageONTARow (DXClusterSpot &s)
{
    setRadioSpot(s.kHz);

    LatLong ll;
    ll.lat_d = rad2deg(s.dx_lat);
    ll.lng_d = rad2deg(s.dx_lng);
    newDX (ll, NULL, s.dx_call);       // normalizes
}


/* show menu to let op select sort
 * Sort by:
 *   ( ) Age    ( ) Call
 *   ( ) Band   ( ) ID
 */
static void runONTASortMenu (const SBox &box, ONTAState *osp)
{
    #define ONTA_LINDENT 3
    #define ONTA_MINDENT 7
    MenuItem mitems[] = {
        // first column
        {MENU_LABEL, false,                    0, ONTA_LINDENT, "Sort by:"},
        {MENU_1OFN, osp->sortby == ONTAS_AGE,  1, ONTA_MINDENT, onta_sorts[ONTAS_AGE].menu_name},
        {MENU_1OFN, osp->sortby == ONTAS_BAND, 1, ONTA_MINDENT, onta_sorts[ONTAS_BAND].menu_name},

        // second column
        {MENU_BLANK, false,                    0, ONTA_MINDENT, NULL },
        {MENU_1OFN, osp->sortby == ONTAS_CALL, 1, ONTA_MINDENT, onta_sorts[ONTAS_CALL].menu_name},
        {MENU_1OFN, osp->sortby == ONTAS_ID,   1, ONTA_MINDENT, onta_sorts[ONTAS_ID].menu_name},
    };
    #define ONTAMENU_N   NARRAY(mitems)

    SBox menu_b = box;                          // copy, not ref!
    menu_b.x = box.x + box.w/8;
    menu_b.y = box.y + COUNT_DY;
    menu_b.w = 0;       // shrink to fit
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, true, false, 2, ONTAMENU_N, mitems};
    if (runMenu (menu)) {

        // find new sort field
        if (mitems[1].set)
            osp->sortby = ONTAS_AGE;
        else if (mitems[2].set)
            osp->sortby = ONTAS_BAND;
        else if (mitems[4].set)
            osp->sortby = ONTAS_CALL;
        else if (mitems[5].set)
            osp->sortby = ONTAS_ID;
        else
            fatalError (_FX("runONTASortMenu no menu set"));

        // update
        saveONTASorts();
        updateOnTheAir (box, (ONTAProgram)(osp->whoami));

    } else {

        // just simple refresh to erase menu
        drawONTA (box, osp);
    }
}

/* given ONTAProgram return ONTAState*.
 * fatal if not valid or inconsistent.
 */
static ONTAState *getONTAState (ONTAProgram whoami)
{
    // confirm valid whoami
    ONTAState *osp = NULL;
    switch (whoami) {
    case ONTA_POTA: osp = &onta_state[ONTA_POTA]; break;
    case ONTA_SOTA: osp = &onta_state[ONTA_SOTA]; break;
    default: fatalError (_FX("invalid ONTA whoami %d"), whoami); break;
    }

    // confirm consistent
    if (osp->whoami != whoami)
        fatalError (_FX("inconsistent ONTA whoami %d %d"), whoami, osp->whoami);

    // good!
    return (osp);
}

/* reset storage for the given program
 */
static void resetONTAStorage (const SBox &box, ONTAState *osp)
{
    free (osp->spots);
    osp->spots = NULL;

    osp->ss.init ((box.h - START_DY)/ONTA_ROWDY, 0, 0);         // max_vis, top_vis, n_data = 0;
    max_spots = osp->ss.max_vis + nMoreScrollRows();
}

/* read fresh ontheair info and draw pane in box, beware thinner PANE_0
 */
bool updateOnTheAir (const SBox &box, ONTAProgram whoami)
{
    // get proper state
    ONTAState *osp = getONTAState (whoami);

    // reset
    resetONTAStorage (box, osp);

    initONTASorts();
    int n_read = 0;

    WiFiClient onta_client;

    // line will contain error message if reach out label and !ok
    char line[100];
    strcpy (line, _FX("download error"));
    bool ok = false;

    Serial.println (osp->page);
    if (wifiOk() && onta_client.connect(backend_host, backend_port)) {

        // look alive
        resetWatchdog();
        updateClocks(false);

        // fetch page and skip header
        httpHCPGET (onta_client, backend_host, osp->page);
        if (!httpSkipHeader (onta_client)) {
            Serial.print (F("OnTheAir download failed\n"));
            snprintf (line, sizeof(line), _FX("%s header error"), osp->prog);
            goto out;
        }
        // add each spot
        while (getTCPLine (onta_client, line, sizeof(line), NULL)) {

            // skip comments
            if (line[0] == '#')
                continue;

            // parse -- error message if not recognized
            char dxcall[20], iso[20], dxgrid[7], mode[8], id[12];       // N.B. match sscanf field lengths
            float lat, lng;
            unsigned long hz;
            // JI1ORE,430510000,2023-02-19T07:00:14,CW,QM05,35.7566,140.189,JA-1234
            if (sscanf (line, _FX("%19[^,],%lu,%19[^,],%7[^,],%6[^,],%f,%f,%11s"),
                                dxcall, &hz, iso, mode, dxgrid, &lat, &lng, id) != 8) {

                // maybe a blank mode?
                if (sscanf (line, _FX("%19[^,],%lu,%19[^,],,%6[^,],%f,%f,%11s"),
                                dxcall, &hz, iso, dxgrid, &lat, &lng, id) != 7) {
                    Serial.printf (_FX("ONTA: bogus line: %s\n"), line);

                    // leave message in line
                    goto out;
                }
                // .. yup that was it
                mode[0] = '\0';
            }

            // ignore long calls
            if (strlen (dxcall) >= MAX_SPOTCALL_LEN) {
                Serial.printf (_FX("ONTA: ignoring long call: %s\n"), line);
                continue;
            }

            // ignore GHz spots because they are too wide to print
            if (hz >= 1000000000) {
                Serial.printf (_FX("ONTA: ignoring freq >= 1 GHz: %s\n"), line);
                continue;
            }

            // add unless not on exclusive watch list
            if (showOnlyOnSPOTAWatchList() && !onSPOTAWatchList(dxcall)) {
                Serial.printf (_FX("ONTA: %s not on watch list\n"), dxcall);
                continue;
            }

            // append to spots list up to max_spots
            if (osp->ss.n_data == max_spots) {
                // no more so shift out the oldest
                memmove (osp->spots, &osp->spots[1], (max_spots-1) * sizeof(DXClusterSpot));
                osp->ss.n_data = max_spots - 1;
            } else {
                // add 1 more
                osp->spots = (DXClusterSpot*) realloc (osp->spots, (osp->ss.n_data+1)*sizeof(DXClusterSpot));
                if (!osp->spots)
                    fatalError (_FX("No room for %d spots"), osp->ss.n_data+1);
            }
            DXClusterSpot *new_sp = &osp->spots[osp->ss.n_data++];
            memset (new_sp, 0, sizeof(*new_sp));

            // repurpose de_call for id, de_grid for list name
            memcpy (new_sp->de_call, id, sizeof(new_sp->de_call));
            memcpy (new_sp->de_grid, osp->prog, sizeof(new_sp->de_grid)-1);
            memcpy (new_sp->dx_call, dxcall, sizeof(new_sp->dx_call));
            memcpy (new_sp->dx_grid, dxgrid, sizeof(new_sp->dx_grid));
            memcpy (new_sp->mode, mode, sizeof(new_sp->mode));
            new_sp->dx_lat = deg2rad(lat);
            new_sp->dx_lng = deg2rad(lng);
            new_sp->de_lat = de_ll.lat;
            new_sp->de_lng = de_ll.lng;
            new_sp->kHz = hz / 1000.0F;
            new_sp->spotted = crackISO8601 (iso);

            // count
            n_read++;
        }

        // fresh screen coords
        updateOnTheAirSpotMapLocations();

        // ok, even if none found
        ok = true;
    }

out:

    if (ok) {
        Serial.printf (_FX("ONTA: read %d new spots\n"), n_read);
        qsort (osp->spots, osp->ss.n_data, sizeof(DXClusterSpot), onta_sorts[osp->sortby].qsf);
        osp->ss.scrollToNewest();
        drawONTA (box, osp);
    } else {
        resetONTAStorage (box, osp);
        plotMessage (box, RA8875_RED, line);
    }

    onta_client.stop();

    return (ok);
}

/* implement a tap at s known to be within the given box for our Pane.
 * return if something for us, else false to mean op wants to change the Pane option.
 */
bool checkOnTheAirTouch (const SCoord &s, const SBox &box, ONTAProgram whoami)
{
    // get proper state
    ONTAState *osp = getONTAState (whoami);

    // check for title or scroll
    if (s.y < box.y + PANETITLE_H) {

        if (osp->ss.checkScrollUpTouch (s, box)) {
            scrollONTAUp (box, osp);
            return (true);
        }

        if (osp->ss.checkScrollDownTouch (s, box)) {
            scrollONTADown (box, osp);
            return (true);
        }

        // else tapping title always leaves this pane
        return (false);
    }

    // check for tapping count
    if (s.y < box.y + START_DY) {
        runONTASortMenu (box, osp);
        return (true);
    }

    // tapped a row, engage if defined
    int spot_row;
    int vis_row = (s.y - (box.y + START_DY))/ONTA_ROWDY;
    if (osp->ss.findDataIndex (vis_row, spot_row))
        engageONTARow (osp->spots[spot_row]);

    // ours even if row is empty
    return (true);

}

/* pass back the given ONTA list, and whether there are any at all.
 * ok to pass back if not displayed because spot list is still intact.
 * N.B. caller should not modify the list
 */
bool getOnTheAirSpots (DXClusterSpot **spp, uint8_t *nspotsp, ONTAProgram whoami)
{
    // get proper state
    ONTAState *osp = getONTAState (whoami);

    // none if no spots or not showing
    if (!osp->spots || findPaneForChoice ((PlotChoice)osp->PLOT_CH_id) == PANE_NONE)
        return (false);

    // pass back
    *spp = osp->spots;
    *nspotsp = osp->ss.n_data;

    // ok
    return (true);
}

#if defined (_IS_ESP8266)

/* return whether the given location is over any OTA spots
 * ESP only
 */
bool overAnyOnTheAirSpots (const SCoord &s)
{
    // check all spots
    for (int i = 0; i < ONTA_N; i++) {
        ONTAState *osp = &onta_state[i];
        if (osp->spots && findPaneForChoice ((PlotChoice)osp->PLOT_CH_id) != PANE_NONE) {
            for (int j = 0; j < osp->ss.n_data; j++) {
                // N.B. inCircle works even though map_c is in Raw coords because on ESP they equal canonical
                if (labelSpots() ? inBox(s, osp->spots[j].dx_map.map_b)
                                 : (dotSpots() ? inCircle(s, osp->spots[j].dx_map.map_c) : false))
                    return (true);
            }
        }
    }

    // none
    return (false);
}

#endif // _IS_ESP8266

/* draw all current OTA spots on the map
 */
void drawOnTheAirSpotsOnMap (void)
{
    // draw all spots
    for (int i = 0; i < ONTA_N; i++) {
        ONTAState *osp = &onta_state[i];
        if (osp->spots && findPaneForChoice ((PlotChoice)osp->PLOT_CH_id) != PANE_NONE) {
            for (int j = 0; j < osp->ss.n_data; j++) {
                drawDXCLabelOnMap (osp->spots[j]);
            }
        }
    }
}

/* update screen coords of each OTA spot, called ostensibly when projection changes.
 */
void updateOnTheAirSpotMapLocations (void)
{
    for (int i = 0; i < ONTA_N; i++) {
        ONTAState *osp = &onta_state[i];
        for (int j = 0; j < osp->ss.n_data; j++) {
            setDXCSpotPosition (osp->spots[j]);
        }
    }
}

/* find closest otaspot and location on either end to given ll, if any.
 */
bool getClosestOnTheAirSpot (const LatLong &ll, DXClusterSpot *dxc_closest, LatLong *ll_closest)
{
    // find closest spot among all lists
    LatLong ll_cl;
    DXClusterSpot dxc_cl;
    bool found_any = false;
    float best_cl = 0;
    for (int i = 0; i < ONTA_N; i++) {
        ONTAState *osp = &onta_state[i];
        if (osp->spots && findPaneForChoice ((PlotChoice)osp->PLOT_CH_id) != PANE_NONE
                                && getClosestDXC (osp->spots, osp->ss.n_data, ll, &dxc_cl, &ll_cl)) {
            if (found_any) {
                // see if this is even closer than one found so far
                float new_cl = simpleSphereDist (ll, ll_cl);
                if (new_cl < best_cl) {
                    *dxc_closest = dxc_cl;
                    *ll_closest = ll_cl;
                    best_cl = new_cl;
                }
            } else {
                // first candidate
                best_cl = simpleSphereDist (ll, ll_cl);
                *dxc_closest = dxc_cl;
                *ll_closest = ll_cl;
                found_any = true;
            }
        }
    }

    return (found_any);
}
