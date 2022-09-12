/* manage PSKReporter and WSPR records and drawing.
 * once queried we make no distinction.
 */

#include "HamClock.h"

uint8_t psk_mask;                               // one of PSKModeBits
uint32_t psk_bands;                             // bitmask of PSKBandSetting



#if defined(_SUPPORT_PSKREPORTER)


// config
#define PSK_POLL_DT     30                      // polling period, seconds
#define MSG_DWELL       4000                    // status message dwell time, ms

// state
static PSKReport *reports;                      // malloced list of reports
static int n_reports;                           // n used in reports[]
static int n_malloced;                          // n malloced in reports[]
static time_t last_update;                      // time of last update
static KD3Node *kd3tree, *kd3root;              // n_reports of nodes that point into reports[] and tree
static bool psk_trace = false;                  // set for additional logging

// query urls
static const char psk_page[] = "/fetchPSKReporter.pl";
static const char wspr_page[] = "/fetchWSPR.pl";

// band edges and colors
typedef struct {
    int min_kHz, max_kHz;
    const char *name;
    CSIds cid;
    int count;
} BandEdge;
static BandEdge bands[PSKBAND_N] = {            // must match PSKBandSetting
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

/* return index of bands[] containing Hz, else -1
 */
static int searchBands (long Hz)
{
    int kHz = (int)(Hz/1000);

    int min_i = 0;
    int max_i = PSKBAND_N-1;
    while (min_i <= max_i) {
        int mid = (min_i + max_i)/2;
        if (bands[mid].max_kHz < kHz)
            min_i = mid+1;
        else if (bands[mid].min_kHz > kHz)
            max_i = mid-1;
        else
            return (mid);
    }
    return (-1);
}

/* return whether we want to use the given frequency.
 * N.B. as a hack to avoid another search, increment bands[b].count whether or not we want to use it
 */
static bool wantBand (long Hz)
{
    int b = searchBands(Hz);
    if (b >= 0) {
        bands[b].count++;
        return ((psk_bands & (1<<b)) != 0);
    }
    return (false);
}

/* return drawing color for the given frequency, or black if not found.
 */
uint16_t getBandColor (long Hz)
{
    int b = searchBands (Hz);
    return (b >= 0 ? getMapColor(bands[b].cid) : RA8875_BLACK);
}

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
            psk_bands |= 1 << i;
        NVWriteUInt32 (NV_PSK_BANDS, psk_bands);
    }
}

static void savePSKState()
{
    NVWriteUInt8 (NV_PSK_MODEBITS, psk_mask);
    NVWriteUInt32 (NV_PSK_BANDS, psk_bands);
}

/* draw the PSK pane in its current box, if any
 */
void drawPSKPane ()
{
    // ignore if pane currently not to be visible
    PlotPane pp = findPaneChoiceNow(PLOT_CH_PSK);
    if (pp == PANE_NONE)
        return;
    SBox &box = plot_b[pp];

    // clear
    prepPlotBox (box);

    // title
    static const char *title = "Live Spots";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t tw = getTextWidth(title);
    tft.setTextColor (RGB565(100,100,255));
    tft.setCursor (box.x + (box.w - tw)/2, box.y + 27);
    tft.print (title);

    // which list and source
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    char where_how[70];
    snprintf (where_how, sizeof(where_how), "%s DE %s using %s",
                psk_mask & PSKMB_OFDE ? "of" : "by",
                psk_mask & PSKMB_CALL ? "call" : "grid",
                psk_mask & PSKMB_PSK ? "PSK" : "WSPR");
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
        uint16_t y = box.y + 5*box.h/12 + row*(box.h/2)/(PSKBAND_N/2);
        char report[30];
        snprintf (report, sizeof(report), "%3s: %5d", bands[i].name, bands[i].count);
        if (psk_bands & (1<<i)) {
            tft.fillRect (x, y-1, TBCOLW, box.h/14, getMapColor(bands[i].cid));
            tft.setTextColor (RA8875_BLACK);
            tft.setCursor (x+2, y);
            tft.print (report);
        } else {
            tft.setTextColor (DKGRAY);
            tft.setCursor (x+2, y);
            tft.print (report);
        }
    }
}

/* draw the current set of spot paths in reports[] if enabled
 */
void drawPSKPaths ()
{
    // ignore if not in any rotation set
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE)
        return;

    for (PSKReport *rp = reports; rp < reports+n_reports; rp++) {

        float dist, bear;
        propPath (false, de_ll, sdelat, cdelat, rp->ll, &dist, &bear);
        const int n_step = ceilf(dist/deg2rad(4));
        const float step = dist/n_step;
        uint16_t color = getBandColor(rp->Hz);
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
                    tft.drawLine (prev_s.x, prev_s.y, s.x, s.y, 1, color);
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
}

/* query PSK reporter or WSPR for new reports, if sufficient time has ellapsed or forced.
 */
void updatePSKReporter (bool force)
{
    // ignore if not in any rotation set
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE)
        return;

    // skip if too fast unless force
    time_t t0 = now();
    if (last_update + PSK_POLL_DT > t0 && !force)
        return;
    last_update = t0;

    WiFiClient psk_client;

    // need DE maid regardless
    char de_maid[MAID_CHARLEN];
    getNVMaidenhead (NV_DE_GRID, de_maid);
    de_maid[4] = '\0';

    // build query
    char query[100];
    bool data_psk = (psk_mask & PSKMB_PSK) != 0;
    bool use_call = (psk_mask & PSKMB_CALL) != 0;
    bool of_de = (psk_mask & PSKMB_OFDE) != 0;
    snprintf (query, sizeof(query), "%s?%s%s=%s",
                                        data_psk ? psk_page : wspr_page,
                                        of_de ? "of" : "by",
                                        use_call ? "call" : "grid",
                                        use_call ? getCallsign() : de_maid);
    printf ("PSK: query: %s\n", query);

    // fetch and fill reports[]
    resetWatchdog();
    if (wifiOk() && psk_client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCGET (psk_client, svr_host, query);

        // skip header
        if (!httpSkipHeader (psk_client)) {
            mapMsg (MSG_DWELL, "PSK error: no header");
            goto out;
        }

        // reset counts
        n_reports = 0;
        for (int i = 0; i < PSKBAND_N; i++)
            bands[i].count = 0;

        // read lines -- anything unexpected is considered an error message
        char line[100];
        while (getTCPLine (psk_client, line, sizeof(line), NULL)) {

            if (psk_trace)
                printf ("PSK: fetched %s\n", line);

            // parse. N.B. match sscanf sizes with elements
            PSKReport new_r;
            memset (&new_r, 0, sizeof(new_r));
            if (sscanf (line, "%ld,%9[^,],%19[^,],%9[^,],%19[^,],%19[^,],%ld,%d",
                            &new_r.posting, new_r.txgrid, new_r.txcall, new_r.rxgrid, new_r.rxcall,
                            new_r.mode, &new_r.Hz, &new_r.snr) != 8) {
                // TODO mapMsg (MSG_DWELL, "PSK error: %s", line);   // anything unexpected is assumed be an err
                goto out;
            }

            // add to reports[] if meets all requirements
            const char *msg_call = of_de ? new_r.txcall : new_r.rxcall;
            const char *msg_grid = of_de ? new_r.txgrid : new_r.rxgrid;
            const char *other_grid = of_de ? new_r.rxgrid : new_r.txgrid;
            if (maidenhead2ll (new_r.ll, other_grid)
                                && ((use_call && strcasecmp (getCallsign(), msg_call) == 0)
                                        || (!use_call && strncasecmp (de_maid, msg_grid, 4) == 0))
                                && wantBand (new_r.Hz)) {

                // match! grow reports list if necessary
                if (n_reports + 1 > n_malloced) {
                    reports = (PSKReport *) realloc (reports, (n_malloced += 100) * sizeof(PSKReport));
                    if (!reports)
                        fatalError ("Live Spots: %d", n_malloced);
                }

                // set next location
                reports[n_reports++] = new_r;

            } else {
                if (psk_trace)
                    printf ("PSK: unmatched info: %s\n", line);
            }
        }

        // finished collecting reports now make the fast lookup tree.
        // N.B. can't build incrementally because left/right pointers can move with each realloc
        kd3tree = (KD3Node *) realloc (kd3tree, n_reports * sizeof(KD3Node));
        if (!kd3tree && n_reports > 0)
            fatalError ("Live Spots tree: %d", n_reports);
        memset (kd3tree, 0, n_reports * sizeof(KD3Node));
        for (int i = 0; i < n_reports; i++) {
            KD3Node &ki = kd3tree[i];
            PSKReport &ri = reports[i];
            ll2KD3Node (ri.ll, ki);
            ki.data = (void*) &ri;
        }
        kd3root = mkKD3NodeTree (kd3tree, n_reports, 0);

        // ok! show new counts immediately, paths will follow shortly via drawMoreEarth()
        drawPSKPane();

    } else
        mapMsg (MSG_DWELL, "Spot connection failed");

out:
    // finish up
    psk_client.stop();
    printf ("PSK: found %d %s reports %s %s\n", n_reports,
                        data_psk ? "PSK" : "WSPR",
                        of_de ? "of" : "by",
                        use_call ? getCallsign() : de_maid);

    // already reported any problems
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
    bool data_psk = (psk_mask & PSKMB_PSK) != 0;
    bool use_call = (psk_mask & PSKMB_CALL) != 0;
    bool of_de = (psk_mask & PSKMB_OFDE) != 0;

    // menu
    #define PRI_INDENT 2
    #define SEC_INDENT 0
    #define TRI_INDENT 12
    #define MI_N (PSKBAND_N + 9)                                // bands + controls
    MenuItem mitems[MI_N];
    mitems[0] =     {MENU_LABEL, false, 0, PRI_INDENT, "Data:"};
    mitems[1] =     {MENU_LABEL, false, 0, PRI_INDENT, "Spot:"};
    mitems[2] =     {MENU_LABEL, false, 0, PRI_INDENT, "What:"};
    mitems[3] = {MENU_AL1OFN, (bool)(psk_bands & (1<<PSKBAND_160M)), 4, TRI_INDENT, bands[PSKBAND_160M].name};
    mitems[4] = {MENU_AL1OFN, (bool)(psk_bands & (1<<PSKBAND_80M)),  4, TRI_INDENT, bands[PSKBAND_80M].name};
    mitems[5] = {MENU_AL1OFN, (bool)(psk_bands & (1<<PSKBAND_60M)),  4, TRI_INDENT, bands[PSKBAND_60M].name};
    mitems[6] = {MENU_AL1OFN, (bool)(psk_bands & (1<<PSKBAND_40M)),  4, TRI_INDENT, bands[PSKBAND_40M].name};
    mitems[7] =     {MENU_1OFN, data_psk, 1, SEC_INDENT, "PSK"};
    mitems[8] =     {MENU_1OFN, of_de, 2, SEC_INDENT, "of DE"};
    mitems[9] =     {MENU_1OFN, use_call, 3, SEC_INDENT, "Call"};
    mitems[10] = {MENU_AL1OFN, (bool)(psk_bands & (1<<PSKBAND_30M)),  4, TRI_INDENT, bands[PSKBAND_30M].name};
    mitems[11] = {MENU_AL1OFN, (bool)(psk_bands & (1<<PSKBAND_20M)),  4, TRI_INDENT, bands[PSKBAND_20M].name};
    mitems[12]= {MENU_AL1OFN, (bool)(psk_bands & (1<<PSKBAND_17M)),  4, TRI_INDENT, bands[PSKBAND_17M].name};
    mitems[13]= {MENU_AL1OFN, (bool)(psk_bands & (1<<PSKBAND_15M)),  4, TRI_INDENT, bands[PSKBAND_15M].name};
    mitems[14]=    {MENU_1OFN, !data_psk, 1, SEC_INDENT, "WSPR"};
    mitems[15]=    {MENU_1OFN, !of_de, 2, SEC_INDENT, "by DE"};
    mitems[16]=    {MENU_1OFN, !use_call, 3, SEC_INDENT, "Grid"};
    mitems[17]= {MENU_AL1OFN, (bool)(psk_bands & (1<<PSKBAND_12M)),  4, TRI_INDENT, bands[PSKBAND_12M].name};
    mitems[18]= {MENU_AL1OFN, (bool)(psk_bands & (1<<PSKBAND_10M)),  4, TRI_INDENT, bands[PSKBAND_10M].name};
    mitems[19]= {MENU_AL1OFN, (bool)(psk_bands & (1<<PSKBAND_6M)),   4, TRI_INDENT, bands[PSKBAND_6M].name};
    mitems[20]= {MENU_AL1OFN, (bool)(psk_bands & (1<<PSKBAND_2M)),   4, TRI_INDENT, bands[PSKBAND_2M].name};

    // create a box for the menu
    SBox menu_b;
    menu_b.x = box.x+21;
    menu_b.y = box.y+35;
    menu_b.w = 0;       // shrink to fit

    // run
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, true, false, 3, MI_N, mitems};
    if (runMenu (menu)) {

        // set new mode
        psk_mask = 0;
        if (mitems[7].set)
            psk_mask |= PSKMB_PSK;
        if (mitems[8].set)
            psk_mask |= PSKMB_OFDE;
        if (mitems[9].set)
            psk_mask |= PSKMB_CALL;

        // set new bands
        psk_bands = 0;
        psk_bands |= mitems[3].set  << PSKBAND_160M;
        psk_bands |= mitems[4].set  << PSKBAND_80M;
        psk_bands |= mitems[5].set  << PSKBAND_60M;
        psk_bands |= mitems[6].set  << PSKBAND_40M;
        psk_bands |= mitems[10].set << PSKBAND_30M;
        psk_bands |= mitems[11].set << PSKBAND_20M;
        psk_bands |= mitems[12].set << PSKBAND_17M;
        psk_bands |= mitems[13].set << PSKBAND_15M;
        psk_bands |= mitems[17].set << PSKBAND_12M;
        psk_bands |= mitems[18].set << PSKBAND_10M;
        psk_bands |= mitems[19].set << PSKBAND_6M;
        psk_bands |= mitems[20].set << PSKBAND_2M;

        // persist
        savePSKState();

        // refresh
        updatePSKReporter (true);
    }

    // draw pane even if cancel to restore
    drawPSKPane();

    // ours alright
    return (true);
}

/* return report of spot closest to ll if appropriate.
 */
bool getClosestPSK (const LatLong &ll, const PSKReport **rpp)
{
    // ignore if not in any rotation set or no tree yet
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE || !kd3tree || !kd3root)
        return (false);

    // find node clostest to ll
    KD3Node target_node, *best_node;
    ll2KD3Node (ll, target_node);
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

    return (false);
}



#endif // _SUPPORT_PSKREPORTER)
