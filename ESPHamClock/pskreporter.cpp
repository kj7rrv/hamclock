/* manage RBN, PSKReporter and WSPR records and drawing.
 * ESP does not draw paths nor support cursor search for nearest spot.
 */

#include "HamClock.h"



// common global state
uint8_t psk_mask;                               // one of PSKModeBits
uint32_t psk_bands;                             // bitmask of PSKBandSetting
uint16_t psk_maxage_mins;                       // query period, minutes

// handy
#define SET_PSKBAND(b)  (psk_bands |= (1 << (b)))
#define TST_PSKBAND(b)  ((psk_bands & (1 << (b))) != 0)

// common query urls
static const char psk_page[] PROGMEM = "/fetchPSKReporter.pl";
static const char wspr_page[] PROGMEM = "/fetchWSPR.pl";
static const char rbn_page[] PROGMEM = "/fetchRBN.pl";

// common config
#define LIVE_COLOR      (RGB565(100,100,255))   // title color
#define PSK_MAXDR       5                       // radius of max dist marker

// common private state
static uint8_t show_maxdist;                    // show max distances, else count
static int n_reports;                           // count of reports in psk_bands

// band stats
PSKBandStats bstats[PSKBAND_N];

/* BandEdge differs between ESP/UNIX in the way it defines a color for each band.
 */


#if defined(_SUPPORT_PSKUNIX)


// UNIX private UNIX state
static PSKReport *reports;                      // malloced list of reports
static int n_malloced;                          // n malloced in reports[]
static int spot_maxrpt[PSKBAND_N];              // indices into reports[] for the farthest spot per band
static KD3Node *kd3tree, *kd3root;              // n_reports of nodes that point into reports[] and tree


// UNIX band edges, name and color setup reference
typedef struct {
    int min_kHz, max_kHz;                       // band edges
    const char *name;                           // common name
    ColorSelection cid;                         // get color from setup
} BandEdge;

static BandEdge bands[PSKBAND_N] = {            // order must match PSKBandSetting
    {  1800,   2000,  "160",  BAND160_CSPR},
    {  3500,   4000,  "80",   BAND80_CSPR},
    {  5330,   5410,  "60",   BAND60_CSPR},
    {  7000,   7300,  "40",   BAND40_CSPR},
    { 10100,  10150,  "30",   BAND30_CSPR},
    { 14000,  14350,  "20",   BAND20_CSPR},
    { 18068,  18168,  "17",   BAND17_CSPR},
    { 21000,  21450,  "15",   BAND15_CSPR},
    { 24890,  24990,  "12",   BAND12_CSPR},
    { 28000,  29700,  "10",   BAND10_CSPR},
    { 50000,  54000,  "6",    BAND6_CSPR},
    {144000, 148000,  "2",    BAND2_CSPR},
};

#define BANDCOLOR(i) getMapColor(bands[i].cid)

static int findBand (long Hz);

/* return drawing color for the given frequency, or black if not found.
 * UNIX only
 */
uint16_t getBandColor (long Hz)
{
    int b = findBand (Hz);
    return (b >= 0 && b < PSKBAND_N ? BANDCOLOR(b) : RA8875_BLACK);
}



#else // defined(_SUPPORT_PSKESP)


// ESP band edges, name and color
typedef struct {
    int min_kHz, max_kHz;                       // band edges
    const char *name;                           // common name
    uint16_t color;                             // fixed color here
} BandEdge;

static BandEdge bands[PSKBAND_N] PROGMEM = {    // order must match PSKBandSetting
    {  1800,   2000,  "160", RGB565(128,0,0) },
    {  3500,   4000,  "80",  RGB565(128,128,0)},
    {  5330,   5410,  "60",  RGB565(230,25,75)},
    {  7000,   7300,  "40",  RGB565(245,130,48)},
    { 10100,  10150,  "30",  RGB565(200,176,20)},
    { 14000,  14350,  "20",  RGB565(250,250,0)},
    { 18068,  18168,  "17",  RGB565(60,180,75)},
    { 21000,  21450,  "15",  RGB565(70,240,240)},
    { 24890,  24990,  "12",  RGB565(0,130,200)},
    { 28000,  29700,  "10",  RGB565(250,190,212)},
    { 50000,  54000,  "6",   RGB565(200,150,100)},
    {144000, 148000,  "2",   RGB565(100,100,100)},
};

#define BANDCOLOR(i) pgm_read_word (&bands[i].color)

/* erase all distance markers
 * ESP only
 */
static void erasePSKSpots (void)
{
    resetWatchdog();

    for (int i = 0; i < PSKBAND_N; i++) {
        if (bstats[i].maxkm > 0) {
            for (int8_t dy = -PSK_MAXDR; dy <= PSK_MAXDR; dy += 1) {
                for (int8_t dx = -PSK_MAXDR; dx <= PSK_MAXDR; dx += 1) {
                    drawMapCoord (bstats[i].maxs.x+dx, bstats[i].maxs.y+dy);
                }
            }
        }
    }
}

#endif  // _SUPPORT_PSKESP


/* return index of bands[] containing Hz, else -1
 * COMMON
 */
static int findBand (long Hz)
{
    int kHz = (int)(Hz/1000);

    int min_i = 0;
    int max_i = PSKBAND_N-1;
    while (min_i <= max_i) {
        int mid = (min_i + max_i)/2;
        if ((long)pgm_read_dword(&bands[mid].max_kHz) < kHz)
            min_i = mid+1;
        else if ((long)pgm_read_dword(&bands[mid].min_kHz) > kHz)
            max_i = mid-1;
        else
            return (mid);
    }

    // Serial.printf (_FX("%ld Hz unsupported band\n"), Hz);
    return (-1);
}


/* dither ll so multiple spots at same location will be found by kdtree
 * COMMON
 */
static void ditherLL (LatLong &ll)
{
    // tweak some fraction of a 4 char grid: 1 deg in lat, 2 deg in lng
    ll.lat_d += random(1000)/1000.0F - 0.5F;
    ll.lng_d += random(2000)/1000.0F - 1.0F;
    normalizeLL(ll);
}


/* return a high contrast text color to overlay the given background color
 * https://www.w3.org/TR/AERT#color-contrast
 * COMMON
 */
static uint16_t getGoodTextColor (uint16_t bg_col)
{
    uint8_t r = RGB565_R(bg_col);
    uint8_t g = RGB565_G(bg_col);
    uint8_t b = RGB565_B(bg_col);
    int perceived_brightness = 0.299*r + 0.587*g + 0.114*b;
    uint16_t text_col = perceived_brightness > 70 ? RA8875_BLACK : RA8875_WHITE;
    return (text_col);
}

/* draw a distance target marker at s with the given fill color.
 * COMMON
 */
static void drawDistanceTarget (const SCoord &s, uint16_t fill_color)
{
    uint16_t cross_color = getGoodTextColor (fill_color);

    tft.fillCircle (s.x, s.y, PSK_MAXDR, fill_color);
    tft.drawCircle (s.x, s.y, PSK_MAXDR, cross_color);
    tft.drawLine (s.x-PSK_MAXDR, s.y, s.x+PSK_MAXDR, s.y, cross_color);
    tft.drawLine (s.x, s.y-PSK_MAXDR, s.x, s.y+PSK_MAXDR, cross_color);
}

/* return whether the given screen coord is over any active psk spot
 * COMMON
 */
bool overAnyPSKSpots (const SCoord &s)
{
    // ignore if not in any rotation set
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE)
        return (false);

    for (int i = 0; i < PSKBAND_N; i++) {
        if (bstats[i].maxkm > 0 && TST_PSKBAND(i)) {
            SCircle c;
            c.s = bstats[i].maxs;
            c.r = PSK_MAXDR;
            if (inCircle (s, c))
                return (true);
        }
    }
    return (false);
}

/* get NV settings related to PSK
 * COMMON
 */
void initPSKState()
{
    if (!NVReadUInt8 (NV_PSK_MODEBITS, &psk_mask)) {
        // default PSK of grid
        psk_mask = PSKMB_PSK | PSKMB_OFDE;
        NVWriteUInt8 (NV_PSK_MODEBITS, psk_mask);
    }
    if (!NVReadUInt32 (NV_PSK_BANDS, &psk_bands)) {
        // default all bands
        psk_bands = 0;
        for (int i = 0; i < PSKBAND_N; i++)
            SET_PSKBAND(i);
        NVWriteUInt32 (NV_PSK_BANDS, psk_bands);
    }
    if (!NVReadUInt16 (NV_PSK_MAXAGE, &psk_maxage_mins)) {
        // default 30 minutes
        psk_maxage_mins = 30;
        NVWriteUInt16 (NV_PSK_MAXAGE, psk_maxage_mins);
    }
    if (!NVReadUInt8 (NV_PSK_SHOWDIST, &show_maxdist)) {
        show_maxdist = 0;
        NVWriteUInt8 (NV_PSK_SHOWDIST, show_maxdist);
    }
}

/* save NV settings related to PSK
 * COMMON
 */
void savePSKState()
{
    NVWriteUInt8 (NV_PSK_MODEBITS, psk_mask);
    NVWriteUInt32 (NV_PSK_BANDS, psk_bands);
    NVWriteUInt16 (NV_PSK_MAXAGE, psk_maxage_mins);
    NVWriteUInt8 (NV_PSK_SHOWDIST, show_maxdist);
}

/* draw the desired spot locations for each band, as appropriate.
 * N.B. as a side effect this we set bstats[].maxs in order to react to changes in projection.
 * COMMON
 */
void drawPSKSpots ()
{
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE)
        return;

    for (int i = 0; i < PSKBAND_N; i++) {
        if (bstats[i].maxkm > 0 && TST_PSKBAND(i)) {
            SCoord &s = bstats[i].maxs;
            ll2s (bstats[i].maxlat, bstats[i].maxlng, s, PSK_MAXDR);
            if (overMap(s))
                drawDistanceTarget (s, BANDCOLOR(i));
        }
    }
}

/* draw the PSK pane in the given
 * COMMON
 */
void drawPSKPane (const SBox &box)
{
    // clear
    prepPlotBox (box);

    // title
    static const char *title = "Live Spots";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t tw = getTextWidth(title);
    tft.setTextColor (LIVE_COLOR);
    tft.setCursor (box.x + (box.w - tw)/2, box.y + 27);
    tft.print (title);

    // DE maid for some titles
    char de_maid[MAID_CHARLEN];
    getNVMaidenhead (NV_DE_GRID, de_maid);
    de_maid[4] = '\0';

    // show which list and source
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    char where_how[100];
    bool ispsk = (psk_mask & PSKMB_SRCMASK) == PSKMB_PSK;
    bool iswspr = (psk_mask & PSKMB_SRCMASK) == PSKMB_WSPR;
    snprintf (where_how, sizeof(where_how), "%s %s - %s %d %s",
                psk_mask & PSKMB_OFDE ? "of" : "by",
                psk_mask & PSKMB_CALL ? getCallsign() : de_maid,
                (ispsk ? "PSK" : (iswspr ? "WSPR" : "RBN")),
                psk_maxage_mins == 30 ? psk_maxage_mins : psk_maxage_mins/60,
                psk_maxage_mins == 30 ? "mins" : (psk_maxage_mins == 60 ? "hour" : "hrs"));
    uint16_t whw = getTextWidth(where_how);
    tft.setCursor (box.x + (box.w-whw)/2, box.y + box.h/4);
    tft.print (where_how);

    // table
    #define TBLGAP (box.w/20)
    #define TBCOLW (43*box.w/100)
    for (int i = 0; i < PSKBAND_N; i++) {
        int row = i % (PSKBAND_N/2);
        int col = i / (PSKBAND_N/2);
        uint16_t x = box.x + TBLGAP + col*(TBCOLW+TBLGAP);
        uint16_t y = box.y + 3*box.h/8 + row*(box.h/2)/(PSKBAND_N/2);
        char report[30];
        if (show_maxdist) {
            float d = bstats[i].maxkm;
            if (!useMetricUnits())
                d *= MI_PER_KM;
            snprintf (report, sizeof(report), "%3sm %5.0f", bands[i].name, d);
        } else
            snprintf (report, sizeof(report), "%3sm %5d", bands[i].name, bstats[i].count);
        if (TST_PSKBAND(i)) {
            uint16_t map_col = BANDCOLOR(i);
            uint16_t txt_col = getGoodTextColor(map_col);
            tft.fillRect (x, y-1, TBCOLW, box.h/14, map_col);
            tft.setTextColor (txt_col);
            tft.setCursor (x+2, y);
            tft.print (report);
        } else {
            // disabled, always show but diminished
            tft.fillRect (x, y-1, TBCOLW, box.h/14, RA8875_BLACK);
            tft.setTextColor (GRAY);
            tft.setCursor (x+2, y);
            tft.print (report);
        }
    }

    // label
    const char *label = show_maxdist ? (useMetricUnits() ? "Max distance (km)" : "Max distance (mi)")
                                     : "Counts";
    uint16_t lw = getTextWidth (label);
    uint16_t x = box.x + (box.w-lw)/2;
    if (show_maxdist)
        x -= 2*PSK_MAXDR;
    uint16_t y = box.y + box.h - 15;
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (x, y);
    tft.print (label);

    // target example if showing distance
    if (show_maxdist) {
        SCoord s;
        s.x = box.x + box.w - 5*PSK_MAXDR;
        s.y = y + 3;
        drawDistanceTarget (s, RA8875_BLACK);
    }
}

/* query PSK reporter or WSPR for new reports, draw results and return whether all ok
 * COMMON
 */
bool updatePSKReporter (const SBox &box)
{
#if defined (_SUPPORT_PSKESP)
    // erase current max distance markers
    erasePSKSpots();
#endif

    WiFiClient psk_client;
    bool ok = false;
    int n_totspots = 0;

    // handy DE maid if needed
    char de_maid[MAID_CHARLEN];
    getNVMaidenhead (NV_DE_GRID, de_maid);
    de_maid[4] = '\0';

    // build query
    char query[100];
    bool ispsk = (psk_mask & PSKMB_SRCMASK) == PSKMB_PSK;
    bool iswspr = (psk_mask & PSKMB_SRCMASK) == PSKMB_WSPR;
    bool isrbn = (psk_mask & PSKMB_SRCMASK) == PSKMB_RBN;
    bool use_call = (psk_mask & PSKMB_CALL) != 0;
    bool of_de = (psk_mask & PSKMB_OFDE) != 0;
    if (ispsk)
        strcpy_P (query, psk_page);
    else if (iswspr)
        strcpy_P (query, wspr_page);
    else
        strcpy_P (query, rbn_page);
    int qlen = strlen (query);
    snprintf (query+qlen, sizeof(query)-qlen, "?%s%s=%s&maxage=%d",
                                        of_de ? "of" : "by",
                                        use_call ? "call" : "grid",
                                        use_call ? getCallsign() : de_maid,
                                        psk_maxage_mins*60 /* wants seconds */);
    Serial.printf (_FX("PSK: query: %s\n"), query);

    // fetch and fill reports[]
    resetWatchdog();
    if (wifiOk() && psk_client.connect(backend_host, BACKEND_PORT)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCGET (psk_client, backend_host, query);

        // skip header
        if (!httpSkipHeader (psk_client)) {
            Serial.print (F("PSK: no header\n"));
            goto out;
        }

        // reset counts and distances
        n_reports = 0;
        for (int i = 0; i < PSKBAND_N; i++) {
            bstats[i].count = 0;
            bstats[i].maxkm = 0;
        }

        // read lines -- anything unexpected is considered an error message
        char line[100];
        while (getTCPLine (psk_client, line, sizeof(line), NULL)) {

            // Serial.printf (_FX("PSK: fetched %s\n"), line);

            // parse. N.B. match sscanf sizes with elements
            PSKReport new_r;
            memset (&new_r, 0, sizeof(new_r));
            if (sscanf (line, "%ld,%9[^,],%19[^,],%9[^,],%19[^,],%19[^,],%ld,%d",
                            &new_r.posting, new_r.txgrid, new_r.txcall, new_r.rxgrid, new_r.rxcall,
                            new_r.mode, &new_r.Hz, &new_r.snr) != 8) {
                Serial.printf (_FX("PSK: %s\n"), line);
                goto out;
            }

            // RBN does not provide txgrid but it must be us -- TODO really?
            if (isrbn)
                strcpy (new_r.txgrid, de_maid);

            // count each line
            n_totspots++;

            // add to reports[] if meets all requirements
            const char *msg_call = of_de ? new_r.txcall : new_r.rxcall;
            const char *msg_grid = of_de ? new_r.txgrid : new_r.rxgrid;
            const char *other_grid = of_de ? new_r.rxgrid : new_r.txgrid;
            const int band = findBand(new_r.Hz);
            const bool band_ok = band >= 0 && band < PSKBAND_N;
            if (band_ok && maidenhead2ll (new_r.ll, other_grid)
                                && ((use_call && strcasecmp (getCallsign(), msg_call) == 0)
                                        || (!use_call && strncasecmp (de_maid, msg_grid, 4) == 0))) {


                // dither ll a little so duplicate locations are unique on map
                ditherLL (new_r.ll);

                // update count of this band and total
                bstats[band].count++;

            #if defined (_SUPPORT_PSKUNIX)

                // add to reports[] if want this band for plotting
                if (TST_PSKBAND(band)) {

                    // grow reports array if out of room
                    if (n_reports + 1 > n_malloced) {
                        reports = (PSKReport *) realloc (reports, (n_malloced += 100) * sizeof(PSKReport));
                        if (!reports)
                            fatalError (_FX("Live Spots: no mem %d"), n_malloced);
                    }

                    // save new spot
                    reports[n_reports++] = new_r;
                }

                // update max distance and its location for this band. need all bands for the pane table.
                float dist, bearing;        
                propDEPath (false, new_r.ll, &dist, &bearing);  // always show short path to match map
                dist *= KM_PER_MI * ERAD_M;                     // angle to km
                if (dist > bstats[band].maxkm) {
                    bstats[band].maxkm = dist;
                    bstats[band].maxlat = new_r.ll.lat;
                    bstats[band].maxlng = new_r.ll.lng;
                    spot_maxrpt[band] = n_reports-1;            // -1 because already incremented
                    // N.B. do not set spot_maxs[] here, rely on drawPSKSpots() as needed
                }

            #else  // _SUPPORT_PSKESP

                // update max distance and its location for this band. need all bands for the pane table.
                float dist, bearing;        
                propDEPath (false, new_r.ll, &dist, &bearing);  // always show short path to match map
                dist *= KM_PER_MI * ERAD_M;                     // angle to km
                if (dist > bstats[band].maxkm) {
                    bstats[band].maxkm = dist;
                    bstats[band].maxlat = new_r.ll.lat;
                    bstats[band].maxlng = new_r.ll.lng;
                    // N.B. do not set spot_maxs[] here, rely on drawPSKSpots() as needed
                }

            #endif // _SUPPORT_PSKESP

            }
        }

    #if defined (_SUPPORT_PSKUNIX)

        // finished collecting reports now make the fast lookup tree.
        // N.B. can't build incrementally because left/right pointers can move with each realloc
        kd3tree = (KD3Node *) realloc (kd3tree, n_reports * sizeof(KD3Node));
        if (!kd3tree && n_reports > 0)
            fatalError (_FX("Live Spots tree: %d"), n_reports);
        memset (kd3tree, 0, n_reports * sizeof(KD3Node));
        for (int i = 0; i < n_reports; i++) {
            KD3Node *kp = &kd3tree[i];
            PSKReport *rp = &reports[i];
            ll2KD3Node (rp->ll, kp);
            kp->data = (void*) rp;
        }
        kd3root = mkKD3NodeTree (kd3tree, n_reports, 0);

    #endif // _SUPPORT_PSKUNIX

        // ok
        ok = true;

    } else
        Serial.print (F("PSK: Spots connection failed\n"));

out:
    // reset counts if trouble
    if (!ok) {
        n_reports = 0;
        for (int i = 0; i < PSKBAND_N; i++) {
            bstats[i].count = -1;
            bstats[i].maxkm = -1;
        }
    }

    drawPSKPane (box);

#if defined (_SUPPORT_PSKESP)
    // draw everyhing in case erase clobbered, in particular the new max distance markers
    drawAllSymbols(true);
#endif // _SUPPORT_PSKESP

    // finish up
    psk_client.stop();
    Serial.printf (_FX("PSK: found %d %s reports %s %s\n"),
                        n_totspots,
                        (ispsk ? "PSK" : (iswspr ? "WSPR" : "RBN")),
                        of_de ? "of" : "by",
                        use_call ? getCallsign() : de_maid);

    // already reported any problems
    return (ok);
}

/* check for tap at s known to be within a PLOT_CH_PSK box.
 * return whether it was ours.
 * COMMON
 */
bool checkPSKTouch (const SCoord &s, const SBox &box)
{
    // done if tap title
    if (s.y < box.y+box.h/5)
        return (false);

    // handy current state
    bool ispsk = (psk_mask & PSKMB_SRCMASK) == PSKMB_PSK;
    bool iswspr = (psk_mask & PSKMB_SRCMASK) == PSKMB_WSPR;
    bool isrbn = (psk_mask & PSKMB_SRCMASK) == PSKMB_RBN;
    bool use_call = (psk_mask & PSKMB_CALL) != 0;
    bool of_de = (psk_mask & PSKMB_OFDE) != 0;
    bool sh_dist = show_maxdist != 0;

    // menu
    #define PRI_INDENT 2
    #define SEC_INDENT 12
    #define MI_N (PSKBAND_N + 18)                                // bands + controls
    MenuItem mitems[MI_N];

    // runMenu() expects column-major entries

    mitems[0] = {MENU_1OFN,  isrbn, 1, PRI_INDENT, "RBN"};
    mitems[1] = {MENU_LABEL, false, 0, PRI_INDENT, "Spot:"};
    mitems[2] = {MENU_LABEL, false, 0, PRI_INDENT, "What:"};
    mitems[3] = {MENU_LABEL, false, 0, PRI_INDENT, "Show:"};
    mitems[4] = {MENU_LABEL, false, 5, PRI_INDENT, "Age:"};
    mitems[5] = {MENU_1OFN,  false, 6, 5, "2 hrs"};
    mitems[6] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_160M), 4, SEC_INDENT, bands[PSKBAND_160M].name};
    mitems[7] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_80M),  4, SEC_INDENT, bands[PSKBAND_80M].name};
    mitems[8] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_60M),  4, SEC_INDENT, bands[PSKBAND_60M].name};
    mitems[9] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_40M),  4, SEC_INDENT, bands[PSKBAND_40M].name};

    mitems[10] = {MENU_1OFN, ispsk,     1, PRI_INDENT, "PSK"};
    mitems[11] = {MENU_1OFN, of_de,     2, PRI_INDENT, "of DE"};
    mitems[12] = {MENU_1OFN, use_call,  3, PRI_INDENT, "Call"};
    mitems[13] = {MENU_1OFN, sh_dist,   7, PRI_INDENT, "MaxDst"};
    mitems[14] = {MENU_1OFN, false,     6, PRI_INDENT, "30 min"};
    mitems[15] = {MENU_1OFN, false,     6, PRI_INDENT, "6 hrs"};
    mitems[16] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_30M),  4, SEC_INDENT, bands[PSKBAND_30M].name};
    mitems[17] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_20M),  4, SEC_INDENT, bands[PSKBAND_20M].name};
    mitems[18] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_17M),  4, SEC_INDENT, bands[PSKBAND_17M].name};
    mitems[19] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_15M),  4, SEC_INDENT, bands[PSKBAND_15M].name};

    mitems[20] = {MENU_1OFN, iswspr,    1, PRI_INDENT, "WSPR"};
    mitems[21] = {MENU_1OFN, !of_de,    2, PRI_INDENT, "by DE"};
    mitems[22] = {MENU_1OFN, !use_call, 3, PRI_INDENT, "Grid"};
    mitems[23] = {MENU_1OFN, !sh_dist,  7, PRI_INDENT, "Count"};
    mitems[24] = {MENU_1OFN, false,     6, PRI_INDENT, "1 hour"};
    mitems[25] = {MENU_1OFN, false,     6, PRI_INDENT, "24 hrs"};
    mitems[26] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_12M),  4, SEC_INDENT, bands[PSKBAND_12M].name};
    mitems[27] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_10M),  4, SEC_INDENT, bands[PSKBAND_10M].name};
    mitems[28] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_6M),   4, SEC_INDENT, bands[PSKBAND_6M].name};
    mitems[29] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_2M),   4, SEC_INDENT, bands[PSKBAND_2M].name};

    // set age
    switch (psk_maxage_mins) {
    case 30:   mitems[14].set = true; break;
    case 60:   mitems[24].set = true; break;
    case 120:  mitems[5].set  = true; break;
    case 360:  mitems[15].set = true; break;
    case 1440: mitems[25].set = true; break;
    default:   fatalError (_FX("Bad psk_maxage_mins: %d"), psk_maxage_mins);
    }

    // create a box for the menu
    SBox menu_b;
    menu_b.x = box.x+9;
    menu_b.y = box.y;   // tight!
    menu_b.w = 0;       // shrink to fit

    // run
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, true, false, 3, MI_N, mitems};
    if (runMenu (menu)) {

        // handy
        bool psk_set = mitems[10].set;
        bool wspr_set = mitems[20].set;
        bool rbn_set = mitems[0].set;
        bool ofDE_set = mitems[11].set;
        bool call_set = mitems[12].set;

        // RBN only works with ofcall
        if (rbn_set && (!ofDE_set || !call_set)) {

            // show error briefly then restore existing settings
            plotMessage (box, RA8875_RED, "RBN requires of DE Call");
            wdDelay (5000);
            drawPSKPane(box);

        } else {

            // set new mode mask;
            psk_mask = psk_set ? PSKMB_PSK : (wspr_set ? PSKMB_WSPR : PSKMB_RBN);
            if (ofDE_set)
                psk_mask |= PSKMB_OFDE;
            if (call_set)
                psk_mask |= PSKMB_CALL;

            // set new bands
            psk_bands = 0;
            if (mitems[6].set)  SET_PSKBAND(PSKBAND_160M);
            if (mitems[7].set)  SET_PSKBAND(PSKBAND_80M);
            if (mitems[8].set)  SET_PSKBAND(PSKBAND_60M);
            if (mitems[9].set)  SET_PSKBAND(PSKBAND_40M);
            if (mitems[16].set) SET_PSKBAND(PSKBAND_30M);
            if (mitems[17].set) SET_PSKBAND(PSKBAND_20M);
            if (mitems[18].set) SET_PSKBAND(PSKBAND_17M);
            if (mitems[19].set) SET_PSKBAND(PSKBAND_15M);
            if (mitems[26].set) SET_PSKBAND(PSKBAND_12M);
            if (mitems[27].set) SET_PSKBAND(PSKBAND_10M);
            if (mitems[28].set) SET_PSKBAND(PSKBAND_6M);
            if (mitems[29].set) SET_PSKBAND(PSKBAND_2M);
            Serial.printf (_FX("PSK: new bands mask 0x%x\n"), psk_bands);

            // get new age
            if (mitems[14].set)
                psk_maxage_mins = 30;
            else if (mitems[24].set)
                psk_maxage_mins = 60;
            else if (mitems[5].set)
                psk_maxage_mins = 120;
            else if (mitems[15].set)
                psk_maxage_mins = 360;
            else if (mitems[25].set)
                psk_maxage_mins = 1440;
            else
                fatalError (_FX("PSK: No menu age"));

            // get how to show
            show_maxdist = mitems[13].set;

            // persist
            savePSKState();

            // refresh with new criteria
            updatePSKReporter (box);
        }

    } else  {

        // just restore current settings
        drawPSKPane(box);
    }

    // ours alright
    return (true);
}

/* return current stats, if active
 */
bool getPSKBandStats (PSKBandStats stats[PSKBAND_N], const char *names[PSKBAND_N])
{
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE)
        return (false);

    memcpy (stats, bstats, sizeof(PSKBandStats) * PSKBAND_N);
    for (int i = 0; i < PSKBAND_N; i++)
        names[i] = bands[i].name;
    return (true);
}

#if defined(_SUPPORT_PSKUNIX)


/* return whether the path for the given freq should be drawn dashed
 * UNIX only
 */
static bool getBandDashed (long Hz)
{
    int b = findBand (Hz);
    return (b >= 0 && b < PSKBAND_N ? getColorDashed(bands[b].cid) : RA8875_BLACK);
}

/* draw path for the given report
 */
static void drawPSKPath (const PSKReport &rpt)
{
    float dist, bear;
    propPath (false, de_ll, sdelat, cdelat, rpt.ll, &dist, &bear);
    const int n_step = (int)(ceilf(dist/deg2rad(4))) | 1;   // odd so first and last dashed always drawn
    const float step = dist/n_step;
    bool dashed = getBandDashed (rpt.Hz);
    uint16_t color = getBandColor(rpt.Hz);
    SCoord last_good_s = {0, 0};            // for dot
    SCoord prev_s = {0, 0};                 // .x == 0 means don't show
    for (int i = 0; i <= n_step; i++) {     // fence posts
        float r = i*step;
        float ca, B;
        SCoord s;
        solveSphere (bear, r, sdelat, cdelat, &ca, &B);
        ll2s (asinf(ca), fmodf(de_ll.lng+B+5*M_PIF,2*M_PIF)-M_PIF, s, 1);
        if (prev_s.x > 0) {
            if (segmentSpanOk(prev_s, s, 0)) {
                if (!dashed || n_step < 7 || (i & 1))
                    tft.drawLine (prev_s.x, prev_s.y, s.x, s.y, 1, color);
                if (i == n_step)            // only mark if really at the end of the path
                    last_good_s = s;
            } else
               s.x = 0;
        }
        prev_s = s;
    } 

    if (last_good_s.x > 0) {
        // transmitter end is round -- like an expanding wave??
        if (psk_mask & PSKMB_OFDE)
            tft.fillRect (last_good_s.x-PSK_DOTR, last_good_s.y-PSK_DOTR, 2*PSK_DOTR, 2*PSK_DOTR, color);
        else
            tft.fillCircle (last_good_s.x, last_good_s.y, PSK_DOTR, color);
    }
}


/* draw the current set of spot paths in reports[] if enabled
 * UNIX only
 */
void drawPSKPaths ()
{
    // ignore if not in any rotation set
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE)
        return;

    if (show_maxdist) {

        // just show the max path in each band
        for (int i = 0; i < PSKBAND_N; i++)
            if (bstats[i].maxkm > 0 && TST_PSKBAND(i))
                drawPSKPath (reports[spot_maxrpt[i]]);

    } else {

        // show all
        for (int i = 0; i < n_reports; i++)
            drawPSKPath (reports[i]);
    }
}

/* return report of spot closest to ll as appropriate.
 * UNIX only
 */
bool getClosestPSK (const LatLong &ll, const PSKReport **rpp)
{
    // ignore if not in any rotation set
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE)
        return (false);

    // max dist else all

    // get screen loc of ll
    SCoord ll_s;
    ll2s (ll, ll_s, PSK_MAXDR);
    if (!overMap(ll_s))
        return (false);

    // find closest among show_maxs[] (which were set when plotted so must be ok)
    int min_d = 10000;
    int min_rpt = 0;
    for (int i = 0; i < PSKBAND_N; i++) {
        if (bstats[i].maxkm > 0 && TST_PSKBAND(i)) {
            SCoord &s = bstats[i].maxs;
            int d = abs((int)s.x - (int)ll_s.x) + abs((int)s.y - (int)ll_s.y);
            if (d < min_d) {
                min_d = d;
                min_rpt = spot_maxrpt[i];
            }
        }
    }

    // good if within symbol
    if (min_d <= PSK_MAXDR) {
        *rpp = &reports[min_rpt];
        return (true);
    }

    // ignore others if no tree yet or just showing max
    if (show_maxdist || !kd3tree || !kd3root)
        return (false);

    // find node clostest to ll
    KD3Node target_node, *best_node;
    ll2KD3Node (ll, &target_node);
    best_node = NULL;
    float best_dist = 0;
    int n_visited = 0;
    nearestKD3Node (kd3root, &target_node, 0, &best_node, &best_dist, &n_visited);

    float best_miles = nearestKD3Dist2Miles (best_dist);
    // LatLong best_ll;
    // KD3Node2ll (*best_node, best_ll);
    // printf ("*** target (%7.2f,%7.2f) found (%7.2f,%7.2f) dist %7.2f using %d/%d\n",
                // ll.lat_d, ll.lng_d, best_ll.lat_d, best_ll.lng_d, best_miles, n_visited, n_reports);

    if (best_miles < MAX_CSR_DIST) {
        *rpp = (PSKReport *) best_node->data;
        return (true);
    }

    // nope
    return (false);
}

/* return PSKReports list
 */
extern void getPSKSpots (const PSKReport* &rp, int &n_rep)
{
    rp = reports;
    n_rep = n_reports;
}


#endif // _SUPPORT_PSKUNIX
