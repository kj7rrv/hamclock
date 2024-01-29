/* manage PSKReporter, WSPR and RBN records and drawing.
 * ESP does not draw paths and only shows max dist on each band.
 */

#include "HamClock.h"



// global state
uint8_t psk_mask;                               // one of PSKModeBits
uint32_t psk_bands;                             // bitmask of PSKBandSetting
uint16_t psk_maxage_mins;                       // query period, minutes
uint8_t psk_showdist;                           // show max distances, else count

// query urls
static const char psk_page[] PROGMEM = "/fetchPSKReporter.pl";
static const char wspr_page[] PROGMEM = "/fetchWSPR.pl";
static const char rbn_page[] PROGMEM = "/fetchRBN.pl";

// color config
#define LIVE_COLOR      RGB565(80,80,255)       // title color

// we never tag spots with calls because there are usually too many, but it's always worth marking
#define markSpots()     (dotSpots() || labelSpots())

// private state
static int n_reports;                           // count of reports in psk_bands

// band stats
PSKBandStats bstats[PSKBAND_N];
static int findBand (long Hz);

/* band edges, name and color setup reference
 */
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


/* return drawing color for the given frequency, or black if not found.
 */
uint16_t getBandColor (long Hz)
{
    int b = findBand (Hz);
    return (b >= 0 && b < PSKBAND_N ? getMapColor(bands[b].cid) : RA8875_BLACK);
}



// handy test and set whether a band is in use
#define SET_PSKBAND(b)  (psk_bands |= (1 << (b)))               // record that band b is in use
#define TST_PSKBAND(b)  ((psk_bands & (1 << (b))) != 0)         // test whether band b is in use

// handy target radius, match dots, canonical coords
static uint16_t targetSz(void)
{
    static uint16_t r;
    if (r == 0) {
        uint16_t lwRaw;
        getRawSpotSizes (lwRaw, r);
        r = ceilf ((float)r/tft.SCALESZ);       // improves promoting back to raw coords
    }
    return (r);
}


#if defined(_IS_UNIX)

/* only UNIX stores all spots and adds fast lookup. ESP only stores the spot at max dist per band in bstats.
 */

// UNIX private UNIX state
static PSKReport *reports;                      // malloced list of reports
static int n_malloced;                          // n malloced in reports[]
static int spot_maxrpt[PSKBAND_N];              // indices into reports[] for the farthest spot per band
static KD3Node *kd3tree, *kd3root;              // n_reports of nodes that point into reports[] and tree


#else 

/* erase all distance markers
 * ESP only
 */
static void eraseFarthestPSKSpots (void)
{
    // ignore if not in any rotation set or not showing dots
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE || !markSpots())
        return;

    resetWatchdog();

    for (int i = 0; i < PSKBAND_N; i++) {
        PSKBandStats &pbs = bstats[i];
        if (pbs.maxkm > 0) {
            // erase target for sure
            for (int8_t dy = -targetSz(); dy <= targetSz(); dy += 1) {
                for (int8_t dx = -targetSz(); dx <= targetSz(); dx += 1) {
                    drawMapCoord (bstats[i].max_s.x+dx, bstats[i].max_s.y+dy);
                }
            }
            // erase tag if set
            if (labelSpots()) {
                for (uint8_t dy = 0; dy < pbs.maxtag_b.h; dy++) {
                    for (uint8_t dx = 0; dx < pbs.maxtag_b.w; dx++) {
                        drawMapCoord (pbs.maxtag_b.x+dx, pbs.maxtag_b.y+dy);
                    }
                }
            }
        }
    }
}

/* return whether the given screen coord is over any visible psk spot or its tag
 * ESP only
 */
bool overAnyFarthestPSKSpots (const SCoord &s)
{
    // ignore if not in any rotation set or not showing dots
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE || !markSpots())
        return (false);

    for (int i = 0; i < PSKBAND_N; i++) {
        PSKBandStats &pbs = bstats[i];
        if (pbs.maxkm > 0 && TST_PSKBAND(i)) {
            if (inCircle (s, SCircle {pbs.max_s, targetSz()}))
                return (true);
            if (labelSpots() && inBox (s, pbs.maxtag_b))
                return (true);
        }
    }

    // nope
    return (false);
}

#endif  // _IS_UNIX



/* return index of bands[] containing Hz, else -1
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
 */
static void ditherLL (LatLong &ll)
{
    // tweak some fraction of a 4 char grid: 1 deg in lat, 2 deg in lng
    ll.lat_d += random(1000)/1000.0F - 0.5F;
    ll.lng_d += random(2000)/1000.0F - 1.0F;
    normalizeLL(ll);
}


/* draw a distance target marker at canonical s with the given fill color.
 */
static void drawDistanceTarget (const SCoord &s, uint16_t fill_color)
{
    uint16_t cross_color = getGoodTextColor (fill_color);

    // raw looks better

    uint16_t x = s.x * tft.SCALESZ;
    uint16_t y = s.y * tft.SCALESZ;
    uint16_t szRaw = targetSz() * tft.SCALESZ;

    tft.fillCircleRaw (x, y, szRaw, fill_color);
    tft.drawCircleRaw (x, y, szRaw, cross_color);
    tft.drawLineRaw (x-szRaw, y, x+szRaw, y, 1, cross_color);
    tft.drawLineRaw (x, y-szRaw, x, y+szRaw, 1, cross_color);
}

/* return whether the given age, in minutes, is allowed.
 */
bool maxPSKageOk (int m)
{
    return (m==15 || m==30 || m==60 || m==360 || m==1440);
}

/* get NV settings related to PSK
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
    if (!NVReadUInt16 (NV_PSK_MAXAGE, &psk_maxage_mins) || !maxPSKageOk(psk_maxage_mins)) {
        // default 30 minutes
        psk_maxage_mins = 30;
        NVWriteUInt16 (NV_PSK_MAXAGE, psk_maxage_mins);
    }
    if (!NVReadUInt8 (NV_PSK_SHOWDIST, &psk_showdist)) {
        psk_showdist = 0;
        NVWriteUInt8 (NV_PSK_SHOWDIST, psk_showdist);
    }
}

/* save NV settings related to PSK
 */
void savePSKState()
{
    NVWriteUInt8 (NV_PSK_MODEBITS, psk_mask);
    NVWriteUInt32 (NV_PSK_BANDS, psk_bands);
    NVWriteUInt16 (NV_PSK_MAXAGE, psk_maxage_mins);
    NVWriteUInt8 (NV_PSK_SHOWDIST, psk_showdist);
}

/* draw a target at the farthest spot in each active band if needed.
 * N.B. as a side effect we set bstats[].max_s and maxtag_b in order to react to changes in projection.
 */
void drawFarthestPSKSpots ()
{
    // proceed unless not in use
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE)
        return;

    for (int i = 0; i < PSKBAND_N; i++) {
        PSKBandStats &pbs = bstats[i];
        if (pbs.maxkm > 0 && TST_PSKBAND(i)) {

            // always set max_s and at least init maxtag_b for searching and erasing
            ll2s (pbs.maxlat, pbs.maxlng, pbs.max_s, targetSz());
            memset (&pbs.maxtag_b, 0, sizeof (pbs.maxtag_b));

            // show target and call as desired
            if (markSpots() && overMap(pbs.max_s)) {

                // target for sure
                drawDistanceTarget (pbs.max_s, getMapColor(bands[i].cid));

                // plus label if desired
                if (labelSpots()) {
                    setMapTagBox (pbs.maxcall, pbs.max_s, targetSz()+1, pbs.maxtag_b);
                    uint16_t band_color = getMapColor(bands[i].cid);
                    drawMapTag (pbs.maxcall, pbs.maxtag_b, getGoodTextColor(band_color), band_color);
                }
            }
        }
    }
}

/* draw the PSK pane in the given box
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
                psk_maxage_mins < 60 ? psk_maxage_mins : psk_maxage_mins/60,
                psk_maxage_mins < 60 ? "mins" : (psk_maxage_mins == 60 ? "hour" : "hrs"));
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
        if (psk_showdist) {
            float d = bstats[i].maxkm;
            if (!useMetricUnits())
                d *= MI_PER_KM;
            snprintf (report, sizeof(report), "%3sm %5.0f", bands[i].name, d);
        } else
            snprintf (report, sizeof(report), "%3sm %5d", bands[i].name, bstats[i].count);
        if (TST_PSKBAND(i)) {
            uint16_t map_col = getMapColor(bands[i].cid);
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
    const char *label = psk_showdist ? (useMetricUnits() ? "Max distance (km)" : "Max distance (mi)")
                                     : "Counts";
    uint16_t lw = getTextWidth (label);
    uint16_t x = box.x + (box.w-lw)/2;
    if (psk_showdist)
        x -= 2*targetSz();
    uint16_t y = box.y + box.h - 15;
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (x, y);
    tft.print (label);

    // show a target example if showing distance
    if (psk_showdist) {
        SCoord s;
        s.x = tft.getCursorX() + 3 + targetSz();
        s.y = y + 3;
        drawDistanceTarget (s, RA8875_BLACK);
    }
}

/* query PSK reporter or WSPR for new reports, draw results and return whether all ok
 */
bool updatePSKReporter (const SBox &box)
{
#if defined (_IS_ESP8266)
    // erase current max distance markers
    eraseFarthestPSKSpots();
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
    if (wifiOk() && psk_client.connect(backend_host, backend_port)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCGET (psk_client, backend_host, query);

        // skip header
        if (!httpSkipHeader (psk_client)) {
            Serial.print (F("PSK: no header\n"));
            goto out;
        }

        // reset
        n_reports = 0;
        memset (bstats, 0, sizeof(bstats));

        // read lines -- anything unexpected is considered an error message
        char line[100];
        while (getTCPLine (psk_client, line, sizeof(line), NULL)) {

            // Serial.printf (_FX("PSK: fetched %s\n"), line);

            // parse. N.B. match sscanf sizes with elements
            PSKReport new_r;
            memset (&new_r, 0, sizeof(new_r));
            long posting_temp;
            if (sscanf (line, "%ld,%9[^,],%19[^,],%9[^,],%19[^,],%19[^,],%ld,%d",
                            &posting_temp, new_r.txgrid, new_r.txcall, new_r.rxgrid, new_r.rxcall,
                            new_r.mode, &new_r.Hz, &new_r.snr) != 8) {
                Serial.printf (_FX("PSK: %s\n"), line);
                goto out;
            }
            new_r.posting = posting_temp;

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
            if (band_ok && maidenhead2ll (new_r.dx_ll, other_grid)
                                && ((use_call && strcasecmp (getCallsign(), msg_call) == 0)
                                        || (!use_call && strncasecmp (de_maid, msg_grid, 4) == 0))) {


                // dither ll a little so duplicate locations are unique on map
                ditherLL (new_r.dx_ll);

                // update count of this band and total
                bstats[band].count++;

            #if defined (_IS_UNIX)

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

            #endif // _IS_UNIX

                // update max distance and its location for this band. need all bands for the pane table.
                float dist, bearing;        
                propDEPath (false, new_r.dx_ll, &dist, &bearing);  // always show short path to match map
                dist *= KM_PER_MI * ERAD_M;                     // convert core angle to surface km
                PSKBandStats &pbs = bstats[band];
                if (dist > pbs.maxkm) {
                    pbs.maxkm = dist;
                    pbs.maxlat = new_r.dx_ll.lat;
                    pbs.maxlng = new_r.dx_ll.lng;
                    if (labelSpots()) {
                        const char *dx_call = (psk_mask & PSKMB_OFDE) ? new_r.rxcall : new_r.txcall;
                        if (plotSpotCallsigns())
                            strcpy (pbs.maxcall, dx_call);
                        else
                            call2Prefix (dx_call, pbs.maxcall);
                    }
                    // N.B. do not set max_s or maxtag_b here, rely on drawFarthestPSKSpots() as needed

            #if defined (_IS_UNIX)
                    spot_maxrpt[band] = n_reports-1;            // -1 because already incremented
            #endif // _IS_UNIX

                }
            }
        }

    #if defined (_IS_UNIX)

        // finished collecting reports now make the fast lookup tree.
        // N.B. can't build incrementally because left/right pointers can move with each realloc
        kd3tree = (KD3Node *) realloc (kd3tree, n_reports * sizeof(KD3Node));
        if (!kd3tree && n_reports > 0)
            fatalError (_FX("Live Spots tree: %d"), n_reports);
        memset (kd3tree, 0, n_reports * sizeof(KD3Node));
        for (int i = 0; i < n_reports; i++) {
            KD3Node *kp = &kd3tree[i];
            PSKReport *rp = &reports[i];
            ll2KD3Node (rp->dx_ll, kp);
            kp->data = (void*) rp;
        }
        kd3root = mkKD3NodeTree (kd3tree, n_reports, 0);

    #endif

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

#if defined (_IS_ESP8266)
    // draw everyhing in case erase clobbered
    drawAllSymbols(true);
#endif

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
    bool show_dist = psk_showdist != 0;

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
    mitems[5] = {MENU_1OFN,  false, 6, 5, "1 hr"};
    mitems[6] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_160M), 4, SEC_INDENT, bands[PSKBAND_160M].name};
    mitems[7] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_80M),  4, SEC_INDENT, bands[PSKBAND_80M].name};
    mitems[8] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_60M),  4, SEC_INDENT, bands[PSKBAND_60M].name};
    mitems[9] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_40M),  4, SEC_INDENT, bands[PSKBAND_40M].name};

    mitems[10] = {MENU_1OFN, ispsk,     1, PRI_INDENT, "PSK"};
    mitems[11] = {MENU_1OFN, of_de,     2, PRI_INDENT, "of DE"};
    mitems[12] = {MENU_1OFN, use_call,  3, PRI_INDENT, "Call"};
    mitems[13] = {MENU_1OFN, show_dist, 7, PRI_INDENT, "MaxDst"};
    mitems[14] = {MENU_1OFN, false,     6, PRI_INDENT, "15 min"};
    mitems[15] = {MENU_1OFN, false,     6, PRI_INDENT, "6 hrs"};
    mitems[16] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_30M),  4, SEC_INDENT, bands[PSKBAND_30M].name};
    mitems[17] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_20M),  4, SEC_INDENT, bands[PSKBAND_20M].name};
    mitems[18] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_17M),  4, SEC_INDENT, bands[PSKBAND_17M].name};
    mitems[19] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_15M),  4, SEC_INDENT, bands[PSKBAND_15M].name};

    mitems[20] = {MENU_1OFN, iswspr,    1, PRI_INDENT, "WSPR"};
    mitems[21] = {MENU_1OFN, !of_de,    2, PRI_INDENT, "by DE"};
    mitems[22] = {MENU_1OFN, !use_call, 3, PRI_INDENT, "Grid"};
    mitems[23] = {MENU_1OFN, !show_dist,7, PRI_INDENT, "Count"};
    mitems[24] = {MENU_1OFN, false,     6, PRI_INDENT, "30 min"};
    mitems[25] = {MENU_1OFN, false,     6, PRI_INDENT, "24 hrs"};
    mitems[26] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_12M),  4, SEC_INDENT, bands[PSKBAND_12M].name};
    mitems[27] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_10M),  4, SEC_INDENT, bands[PSKBAND_10M].name};
    mitems[28] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_6M),   4, SEC_INDENT, bands[PSKBAND_6M].name};
    mitems[29] = {MENU_AL1OFN, TST_PSKBAND(PSKBAND_2M),   4, SEC_INDENT, bands[PSKBAND_2M].name};

    // set age
    switch (psk_maxage_mins) {
    case 15:   mitems[14].set = true; break;
    case 30:   mitems[24].set = true; break;
    case 60:   mitems[5].set  = true; break;
    case 360:  mitems[15].set = true; break;
    case 1440: mitems[25].set = true; break;
    default:   fatalError (_FX("Bad psk_maxage_mins: %d"), psk_maxage_mins);
    }

    // create a box for the menu
    SBox menu_b;
    menu_b.x = box.x+9;
    menu_b.y = box.y + 5;
    menu_b.w = 0;               // shrink to fit

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
            plotMessage (box, RA8875_RED, _FX("RBN requires \"of DE\" and \"Call\""));
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
                psk_maxage_mins = 15;
            else if (mitems[24].set)
                psk_maxage_mins = 30;
            else if (mitems[5].set)
                psk_maxage_mins = 60;
            else if (mitems[15].set)
                psk_maxage_mins = 360;
            else if (mitems[25].set)
                psk_maxage_mins = 1440;
            else
                fatalError (_FX("PSK: No menu age"));

            // get how to show
            psk_showdist = mitems[13].set;

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

    // copy but zero out entries with 0 count
    memcpy (stats, bstats, sizeof(PSKBandStats) * PSKBAND_N);
    for (int i = 0; i < PSKBAND_N; i++) {
        if (bstats[i].count == 0)
            stats[i].maxkm = stats[i].maxlat = stats[i].maxlng = 0;
        names[i] = bands[i].name;
    }

    return (true);
}




#if defined(_IS_UNIX)


/* return whether the path for the given freq should be drawn dashed
 */
bool getBandDashed (long Hz)
{
    int b = findBand (Hz);
    return (b >= 0 && b < PSKBAND_N ? getColorDashed(bands[b].cid) : RA8875_BLACK);
}

/* draw path for the given report
 * UNIX only
 */
static void drawPSKPath (const PSKReport &rpt)
{
    float dist, bear;
    propPath (false, de_ll, sdelat, cdelat, rpt.dx_ll, &dist, &bear);
    const int n_step = (int)(ceilf(dist/deg2rad(PATH_SEGLEN))) | 1;     // odd so dashed ends always drawn
    const float step = dist/n_step;
    bool dashed = getBandDashed (rpt.Hz);
    uint16_t color = getBandColor(rpt.Hz);
    SCoord dx_s = {0, 0};                                               // last path coord is DX
    SCoord prev_s = {0, 0};                                             // .x == 0 means don't show
    uint16_t lwRaw, mkRaw;                                              // raw path and marker sizes

    getRawSpotSizes (lwRaw, mkRaw);

    // N.B. compute each segment even if not showing paths in order to find dx_s
    for (int i = 0; i <= n_step; i++) {     // fence posts
        float r = i*step;
        float ca, B;
        SCoord s;
        solveSphere (bear, r, sdelat, cdelat, &ca, &B);
        ll2sRaw (asinf(ca), fmodf(de_ll.lng+B+5*M_PIF,2*M_PIF)-M_PIF, s, lwRaw);
        if (prev_s.x > 0) {
            if (segmentSpanOkRaw(prev_s, s, lwRaw)) {
                if (lwRaw && (!dashed || n_step < 7 || (i & 1)))
                    tft.drawLineRaw (prev_s.x, prev_s.y, s.x, s.y, lwRaw, color);
                dx_s = s;
            } else
               s.x = 0;
        }
        prev_s = s;
    } 

    // mark dx end if desired
    if (dx_s.x > 0 && markSpots()) {
        if (psk_mask & PSKMB_OFDE) {
            // DE is tx so DX is rx: draw a square
            tft.fillRectRaw (dx_s.x-mkRaw, dx_s.y-mkRaw, 2*mkRaw, 2*mkRaw, color);
            tft.drawRectRaw (dx_s.x-mkRaw, dx_s.y-mkRaw, 2*mkRaw, 2*mkRaw, RA8875_BLACK);
        } else {
            // DE is rx so DX is tx: draw a cicle (like an expanding wave??)
            tft.fillCircleRaw (dx_s.x, dx_s.y, mkRaw, color);
            tft.drawCircleRaw (dx_s.x, dx_s.y, mkRaw, RA8875_BLACK);
        }
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

    if (psk_showdist) {

        // just show the longest path in each band
        for (int i = 0; i < PSKBAND_N; i++)
            if (bstats[i].maxkm > 0 && TST_PSKBAND(i))
                drawPSKPath (reports[spot_maxrpt[i]]);

    } else {

        // show paths to all spots
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
    ll2s (ll, ll_s, targetSz());
    if (!overMap(ll_s))
        return (false);

    // find closest among bstats[] (which were set when plotted so must be ok)
    int min_d = 10000;
    int min_rpt = 0;
    for (int i = 0; i < PSKBAND_N; i++) {
        if (bstats[i].maxkm > 0 && TST_PSKBAND(i)) {
            SCoord &s = bstats[i].max_s;
            int d = abs((int)s.x - (int)ll_s.x) + abs((int)s.y - (int)ll_s.y);
            if (d < min_d) {
                min_d = d;
                min_rpt = spot_maxrpt[i];
            }
        }
    }

    // good if within symbol
    if (min_d <= targetSz()) {
        *rpp = &reports[min_rpt];
        return (true);
    }

    // ignore others if no tree yet or just showing max
    if (psk_showdist || !kd3tree || !kd3root)
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
void getPSKSpots (const PSKReport* &rp, int &n_rep)
{
    rp = reports;
    n_rep = n_reports;
}


#endif // _IS_UNIX
