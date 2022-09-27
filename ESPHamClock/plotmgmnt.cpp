/* plot management
 * each PlotPane is in one of PlotChoice state at any given time, all must be different.
 * each pane rotates through the set of bits in its rotset.
 */

#include "HamClock.h"


SBox plot_b[PANE_N] = {
    {235, 0, PLOTBOX_W, PLOTBOX_H},
    {405, 0, PLOTBOX_W, PLOTBOX_H},
    {575, 0, PLOTBOX_W, PLOTBOX_H},
};
PlotChoice plot_ch[PANE_N];
time_t plot_rotationT[PANE_N];
uint32_t plot_rotset[PANE_N];
const char *plot_names[PLOT_CH_N] = {

    // N.B. must be in same order as PLOT_CH_* 
    // N.B. take care that names will fit in menu built by askPaneChoice()
    // N.B. names should not include blanks, but _ are changed to blanks for prettier printing

    "VOACAP",           // PLOT_CH_BC,
    "DE_Wx",            // PLOT_CH_DEWX,
    "DX_Cluster",       // PLOT_CH_DXCLUSTER,
    "DX_Wx",            // PLOT_CH_DXWX,
    "Solar_Flux",       // PLOT_CH_FLUX,
    "Planetary_K",      // PLOT_CH_KP,
    "Moon",             // PLOT_CH_MOON,
    "Space_Wx",         // PLOT_CH_NOAASWX,
    "Sunspot_N",        // PLOT_CH_SSN,
    "X-Ray",            // PLOT_CH_XRAY,
    "Rotator",          // PLOT_CH_GIMBAL,
    "ENV_Temp",         // PLOT_CH_TEMPERATURE,
    "ENV_Press",        // PLOT_CH_PRESSURE,
    "ENV_Humid",        // PLOT_CH_HUMIDITY,
    "ENV_DewPt",        // PLOT_CH_DEWPOINT,
    "SDO_Comp",         // PLOT_CH_SDO_1,
    "SDO_6173A",        // PLOT_CH_SDO_2,
    "SDO_Magneto",      // PLOT_CH_SDO_3,
    "SDO_193A",         // PLOT_CH_SDO_4,
    "Solar_Wind",       // PLOT_CH_SOLWIND,
    "DRAP",             // PLOT_CH_DRAP,
    "Countdown",        // PLOT_CH_COUNTDOWN,
    "STEREO_A",         // PLOT_CH_STEREO_A,
    "Live_spots"        // PLOT_CH_PSK,
};

/* retrieve the plot choice for the given pane from NV, if set
 */
static bool getPlotChoiceNV (PlotPane new_pp, PlotChoice *new_ch)
{
    bool ok = false;
    uint8_t ch;

    switch (new_pp) {
    case PANE_1:
        ok = NVReadUInt8 (NV_PLOT_1, &ch);
        break;
    case PANE_2:
        ok = NVReadUInt8 (NV_PLOT_2, &ch);
        break;
    case PANE_3:
        ok = NVReadUInt8 (NV_PLOT_3, &ch);
        break;
    default:
        fatalError (_FX("getPlotChoiceNV() bad plot pane %d"), (int)new_pp);
        return (false);
    }

    // beware just bonkers
    if (ch >= PLOT_CH_N)
        return (false);

    if (ok)
        *new_ch = (PlotChoice)ch;
    return (ok);
}

/* set the current choice for the given pane to any one of rotset, or a default if none.
 */
static void setDefaultPaneChoice (PlotPane pp)
{
    // check rotset first
    if (plot_rotset[pp]) {
        for (int i = 0; i < PLOT_CH_N; i++) {
            if (plot_rotset[pp] & (1 << i)) {
                plot_ch[pp] = (PlotChoice) i;
                break;
            }
        }
    } else {
        const PlotChoice ch_defaults[PANE_N] = {PLOT_CH_SSN, PLOT_CH_XRAY, PLOT_CH_SDO_1};
        plot_ch[pp] = ch_defaults[pp];
        plot_rotset[pp] = (1 << plot_ch[pp]);
        Serial.printf (_FX("PANE: Setting pane %d to default %s\n"), (int)pp+1, plot_names[plot_ch[pp]]);
    }
}

/* qsort-style function to compare pointers to two MenuItems by their string names
 */
static int menuChoiceQS (const void *p1, const void *p2)
{
    return (strcmp (((MenuItem*)p1)->label, ((MenuItem*)p2)->label));
}

/* return whether the given choice is currently physically available on this platform.
 * N.B. does not consider whether in use by panes -- for that use findPaneForChoice()
 */
bool plotChoiceIsAvailable (PlotChoice ch)
{
    switch (ch) {

    case PLOT_CH_DXCLUSTER:     return (useDXCluster());
    case PLOT_CH_GIMBAL:        return (haveGimbal());
    case PLOT_CH_TEMPERATURE:   return (getNBMEConnected() > 0);
    case PLOT_CH_PRESSURE:      return (getNBMEConnected() > 0);
    case PLOT_CH_HUMIDITY:      return (getNBMEConnected() > 0);
    case PLOT_CH_DEWPOINT:      return (getNBMEConnected() > 0);
    case PLOT_CH_COUNTDOWN:     return (getSWEngineState(NULL,NULL) == SWE_COUNTDOWN);

    case PLOT_CH_BC:            // fallthru
    case PLOT_CH_DEWX:          // fallthru
    case PLOT_CH_DXWX:          // fallthru
    case PLOT_CH_FLUX:          // fallthru
    case PLOT_CH_KP:            // fallthru
    case PLOT_CH_MOON:          // fallthru
    case PLOT_CH_NOAASWX:       // fallthru
    case PLOT_CH_SSN:           // fallthru
    case PLOT_CH_XRAY:          // fallthru
    case PLOT_CH_SDO_1:         // fallthru
    case PLOT_CH_SDO_2:         // fallthru
    case PLOT_CH_SDO_3:         // fallthru
    case PLOT_CH_SDO_4:         // fallthru
    case PLOT_CH_SOLWIND:       // fallthru
    case PLOT_CH_DRAP:          // fallthru
    case PLOT_CH_STEREO_A:      // fallthru
    case PLOT_CH_PSK:           // fallthru
        return (true);
        break;

    default:
        fatalError (_FX("plotChoiceIsAvailable() bad choice %d"), (int)ch);
        return (false);
    }

    return (false);

}

/* log the rotation set for the given pain, tag PlotChoice if in the set.
 */
void logPaneRotSet (PlotPane pp, PlotChoice ch)
{
    Serial.printf (_FX("Pane %d choices:\n"), (int)pp+1);
    for (int i = 0; i < PLOT_CH_N; i++)
        if (plot_rotset[pp] & (1 << i))
            Serial.printf (_FX("    %c%s\n"), i == ch ? '*' : ' ', plot_names[i]);
}

/* log the BRB rotation set
 */
void logBRBRotSet()
{
    Serial.printf (_FX("BBB choices:\n"));
    for (int i = 0; i < BRB_N; i++)
        if (brb_rotset & (1 << i))
            Serial.printf (_FX("    %c%s\n"), i == brb_mode ? '*' : ' ', brb_names[i]);
}

/* show a table of suitable plot choices in and for the given pane and allow user to choose one or more.
 * always return a selection even if it's the current selection again, never PLOT_CH_NONE.
 */
PlotChoice askPaneChoice (PlotPane pp)
{
    resetWatchdog();

    // set this temporarily to show all choices, just for testing worst-case layout
    #define ASKP_SHOWALL 0

    // build items from all candidates suitable for this pane
    MenuItem *mitems = NULL;
    int n_mitems = 0;
    for (int i = 0; i < PLOT_CH_N; i++) {
        PlotChoice ch = (PlotChoice) i;
        PlotPane pp_ch = findPaneForChoice (ch);

        // do not allow cluster on pane 1 to avoid disconnect each time DX/DE wx
        if (pp == PANE_1 && ch == PLOT_CH_DXCLUSTER)
            continue;

        // otherwise use if not used elsewhere and available or already assigned to this pane
        if ( (pp_ch == PANE_NONE && plotChoiceIsAvailable(ch)) || pp_ch == pp || ASKP_SHOWALL) {
            // set up next menu item
            mitems = (MenuItem *) realloc (mitems, (n_mitems+1)*sizeof(MenuItem));
            if (!mitems)
                fatalError ("pane alloc: %d", n_mitems); // no _FX if alloc failing
            MenuItem &mi = mitems[n_mitems++];
            mi.type = MENU_AL1OFN;
            mi.set = (plot_rotset[pp] & (1 << ch)) ? true : false;
            mi.label = plot_names[ch];
            mi.indent = 4;
            mi.group = 1;
        }
    }

    // nice sort by label
    qsort (mitems, n_mitems, sizeof(MenuItem), menuChoiceQS);

    // run
    SBox box = plot_b[pp];       // copy
    SBox ok_b;
    MenuInfo menu = {box, ok_b, true, false, 2, n_mitems, mitems};
    bool menu_ok = runMenu (menu);

    // return current choice by default
    PlotChoice return_ch = plot_ch[pp];

    if (menu_ok) {

        // show feedback
        menuRedrawOk (ok_b, MENU_OK_BUSY);

        // find new rotset
        uint32_t new_rotset = 0;
        for (int i = 0; i < n_mitems; i++) {
            if (mitems[i].set) {
                // find which choice this refers to by matching labels
                for (int j = 0; j < PLOT_CH_N; j++) {
                    if (strcmp (plot_names[j], mitems[i].label) == 0) {
                        new_rotset |= (1 << j);
                        break;
                    }
                }
            }
        }

        // enforce cluster on its own
        if ((new_rotset & (1<<PLOT_CH_DXCLUSTER)) && (new_rotset & ~(1<<PLOT_CH_DXCLUSTER))) {

            plotMessage (box, RA8875_RED, _FX("DX Cluster may not be combined with other choices"));
            wdDelay(5000);

        } else {

            plot_rotset[pp] = new_rotset;
            savePlotOps();

            // return current choice if still in rotset, else just pick one
            if (!(plot_rotset[pp] & (1 << return_ch))) {
                for (int i = 0; i < PLOT_CH_N; i++) {
                    if (plot_rotset[pp] & (1 << i)) {
                        return_ch = (PlotChoice)i;
                        break;
                    }
                }
            }
        }
    }

    // finished with menu. labels were static.
    free ((void*)mitems);

    // report
    logPaneRotSet(pp, return_ch);

    // done
    return (return_ch);
}

/* return which pane _is currently showing_ the given choice, else PANE_NONE
 */
PlotPane findPaneChoiceNow (PlotChoice ch)
{
    // unroll the loop ourselves to be sure
    // for (int i = 0; i < PANE_N; i++)
        // if (plot_ch[i] == ch)
            // return ((PlotPane)i);
    // return (PANE_NONE);

    if (PANE_N != 3)
        fatalError (_FX("PANE_N != 3"));

    if (plot_ch[PANE_1] == ch)
        return (PANE_1);
    if (plot_ch[PANE_2] == ch)
        return (PANE_2);
    if (plot_ch[PANE_3] == ch)
        return (PANE_3);
    return (PANE_NONE);
}

/* return which pane has the given choice in its rotation set _even if not currently visible_, else PANE_NONE
 */
PlotPane findPaneForChoice (PlotChoice ch)
{
    // unroll the loop ourselves to be sure
    // for (int i = PANE_1; i < PANE_N; i++)
        // if ( (plot_rotset[i] & (1<<ch)) )
            // return ((PlotPane)i);
    // return (PANE_NONE);

    if (PANE_N != 3)
        fatalError (_FX("PANE_N != 3"));

    uint32_t mask = 1 << ch;
    if (plot_rotset[PANE_1] & mask)
        return (PANE_1);
    if (plot_rotset[PANE_2] & mask)
        return (PANE_2);
    if (plot_rotset[PANE_3] & mask)
        return (PANE_3);
    return (PANE_NONE);
}

/* given a current choice, select the next rotation plot choice for the given pane.
 * if not rotating return the same choice.
 */
PlotChoice getNextRotationChoice (PlotPane pp, PlotChoice pc)
{
    // search starting after given selection
    for (unsigned i = 1; i <= 8*sizeof(plot_rotset[pp]); i++) {
        int pc_test = ((int)pc + i) % PLOT_CH_N;
        if (plot_rotset[pp] & (1 << pc_test))
            return ((PlotChoice)pc_test);
    }

    fatalError (_FX("getNextRotationChoice() none for pane %d"), (int)pp+1);
    return (plot_ch[pp]);
}

/* return any available unassigned plot choice
 */
PlotChoice getAnyAvailableChoice()
{
    int s = random (PLOT_CH_N);
    for (int i = 0; i < PLOT_CH_N; i++) {
        PlotChoice ch = (PlotChoice)((s + i) % PLOT_CH_N);
        if (plotChoiceIsAvailable (ch)) {
            bool inuse = false;
            for (int j = 0; !inuse && j < PANE_N; j++) {
                if (plot_ch[j] == ch || (plot_rotset[j] & (1 << ch))) {
                    inuse = true;
                }
            }
            if (!inuse)
                return (ch);
        }
    }
    fatalError (_FX("no available pane choices"));

    // never get here, just for lint
    return (PLOT_CH_FLUX);
}

/* remove any PLOT_CH_COUNTDOWN from rotset if stopwatch engine not SWE_COUNTDOWN,
 * and if it is currently visible replace with an alternative.
 */
void insureCountdownPaneSensible()
{
    if (getSWEngineState(NULL,NULL) != SWE_COUNTDOWN) {
        for (int i = 0; i < PANE_N; i++) {
            if (plot_rotset[i] & (1 << PLOT_CH_COUNTDOWN)) {
                plot_rotset[i] &= ~(1 << PLOT_CH_COUNTDOWN);
                if (plot_ch[i] == PLOT_CH_COUNTDOWN) {
                    setDefaultPaneChoice((PlotPane)i);
                    if (!setPlotChoice ((PlotPane)i, plot_ch[i])) {
                        fatalError (_FX("can not replace Countdown pain %d with %s"),
                                    i+1, plot_names[plot_ch[i]]);
                    }
                }
            }
        }
    }
}

/* check for touch in the given pane, return whether ours.
 * N.B. accommodate a few choices that have their own touch features.
 */
bool checkPlotTouch (const SCoord &s, PlotPane pp, TouchType tt)
{
    // for sure not ours if not even in this box
    SBox &box = plot_b[pp];
    if (!inBox (s, box))
        return (false);

    // reserve top 20% for bringing up choice menu
    bool in_top = s.y < box.y + box.h/5;

    // check a few choices that have their own active areas
    switch (plot_ch[pp]) {
    case PLOT_CH_DXCLUSTER:
        if (checkDXClusterTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_BC:
        if (checkBCTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_GIMBAL:
        if (checkGimbalTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_COUNTDOWN:
        if (!in_top) {
            checkStopwatchTouch(tt);
            return (true);
        }
        break;
    case PLOT_CH_MOON:
        if (!in_top) {
            drawMoonElPlot();
            initEarthMap();
            return(true);
        }
        break;
    case PLOT_CH_PSK:
        if (checkPSKTouch (s, box))
            return (true);
        in_top = true;
        break;

    // tapping a BME below top rotates just among other BME and disables auto rotate.
    // try all possibilities because they might be on other panes.
    case PLOT_CH_TEMPERATURE:
        if (!in_top) {
            if (setPlotChoice (pp, PLOT_CH_HUMIDITY)
                            || setPlotChoice (pp, PLOT_CH_DEWPOINT)
                            || setPlotChoice (pp, PLOT_CH_PRESSURE)) {
                plot_rotset[pp] = (1 << plot_ch[pp]);   // no auto rotation
                savePlotOps();
                return (true);
            }
        }
        break;
    case PLOT_CH_PRESSURE:
        if (!in_top) {
            if (setPlotChoice (pp, PLOT_CH_TEMPERATURE)
                            || setPlotChoice (pp, PLOT_CH_HUMIDITY)
                            || setPlotChoice (pp, PLOT_CH_DEWPOINT)) {
                plot_rotset[pp] = (1 << plot_ch[pp]);   // no auto rotation
                savePlotOps();
                return (true);
            }
        }
        break;
    case PLOT_CH_HUMIDITY:
        if (!in_top) {
            if (setPlotChoice (pp, PLOT_CH_DEWPOINT)
                            || setPlotChoice (pp, PLOT_CH_PRESSURE)
                            || setPlotChoice (pp, PLOT_CH_TEMPERATURE)) {
                plot_rotset[pp] = (1 << plot_ch[pp]);   // no auto rotation
                savePlotOps();
                return (true);
            }
        }
        break;
    case PLOT_CH_DEWPOINT:
        if (!in_top) {
            if (setPlotChoice (pp, PLOT_CH_PRESSURE)
                            || setPlotChoice (pp, PLOT_CH_TEMPERATURE)
                            || setPlotChoice (pp, PLOT_CH_HUMIDITY)) {
                plot_rotset[pp] = (1 << plot_ch[pp]);   // no auto rotation
                savePlotOps();
                return (true);
            }
        }
        break;

    default:
        break;
    }

    if (!in_top)
        return (false);

    // draw menu with choices for this pane
    PlotChoice ch = askPaneChoice(pp);

    // always engage even if same to erase menu
    if (!setPlotChoice (pp, ch)) {
        fatalError (_FX("checkPlotTouch bad choice %d pane %d"), (int)ch, (int)pp+1);
        // never returns
    }

    // it was ours
    return (true);
}

/* called once to init plot info from NV and insure legal and consistent values
 */
void initPlotPanes()
{
    // retrieve rotation sets -- ok to leave 0 for now if not yet defined
    NVReadUInt32 (NV_PANE1ROTSET, &plot_rotset[PANE_1]);
    NVReadUInt32 (NV_PANE2ROTSET, &plot_rotset[PANE_2]);
    NVReadUInt32 (NV_PANE3ROTSET, &plot_rotset[PANE_3]);

    // rm any choice not available, including dx cluster in pane 1
    for (int i = 0; i < PANE_N; i++) {
        for (int j = 0; j < PLOT_CH_N; j++) {
            if (plot_rotset[i] & (1 << j)) {
                if (i == PANE_1 && j == PLOT_CH_DXCLUSTER) {
                    plot_rotset[i] &= ~(1 << j);
                    Serial.printf (_FX("PANE: Removing %s from pane %d: not allowed\n"), plot_names[j], i+1);
                } else if (!plotChoiceIsAvailable ((PlotChoice)j)) {
                    plot_rotset[i] &= ~(1 << j);
                    Serial.printf (_FX("PANE: Removing %s from pane %d: not available\n"), plot_names[j],i+1);
                }
            }
        }
    }

    // if current selection not yet defined or not in rotset pick one from rotset or set a default
    for (int i = 0; i < PANE_N; i++) {
        if (!getPlotChoiceNV ((PlotPane)i, &plot_ch[i]) || !(plot_rotset[i] & (1 << plot_ch[i])))
            setDefaultPaneChoice ((PlotPane)i);
    }

    // insure same choice not in more than 1 pane
    for (int i = 0; i < PANE_N; i++) {
        for (int j = i+1; j < PANE_N; j++) {
            if (plot_ch[i] == plot_ch[j]) {
                // found dup -- replace with some other unused choice
                for (int k = 0; k < PLOT_CH_N; k++) {
                    PlotChoice new_ch = (PlotChoice)k;
                    if (plotChoiceIsAvailable(new_ch) && findPaneChoiceNow(new_ch) == PANE_NONE) {
                        Serial.printf (_FX("PANE: Reassigning dup pane %d from %s to %s\n"), j+1,
                                        plot_names[plot_ch[j]], plot_names[new_ch]);
                        // remove dup from rotation set then replace with new choice
                        plot_rotset[j] &= ~(1 << plot_ch[j]);
                        plot_rotset[j] |= (1 << new_ch);
                        plot_ch[j] = new_ch;
                        break;
                    }
                }
            }
        }
    }

    // enforce cluster is alone, if any
    for (int i = 0; i < PANE_N; i++) {
        if (plot_rotset[i] & (1 << PLOT_CH_DXCLUSTER)) {
            plot_rotset[i] = (1 << PLOT_CH_DXCLUSTER);
            plot_ch[i] = PLOT_CH_DXCLUSTER;
            Serial.printf (_FX("isolating DX Cluster in pane %d\n"), i+i);
            break;
        }
    }

    // one last bit of paranoia: insure each pane choice is in its rotation set
    for (int i = 0; i < PANE_N; i++)
        plot_rotset[i] |= (1 << plot_ch[i]);

    // log and save final arrangement
    for (int i = 0; i < PANE_N; i++)
        logPaneRotSet ((PlotPane)i, plot_ch[i]);
    savePlotOps();
}

/* update NV_PANE?CH from plot_rotset[] and NV_PLOT_? from plot_ch[]
 */
void savePlotOps()
{
    NVWriteUInt32 (NV_PANE1ROTSET, plot_rotset[PANE_1]);
    NVWriteUInt32 (NV_PANE2ROTSET, plot_rotset[PANE_2]);
    NVWriteUInt32 (NV_PANE3ROTSET, plot_rotset[PANE_3]);

    NVWriteUInt8 (NV_PLOT_1, plot_ch[PANE_1]);
    NVWriteUInt8 (NV_PLOT_2, plot_ch[PANE_2]);
    NVWriteUInt8 (NV_PLOT_3, plot_ch[PANE_3]);

    logState();
}

/* flash plot borders nearly ready to change, and include NCDXF_b also.
 */
void showRotatingBorder ()
{
    time_t t0 = now();

    // check plot panes
    for (int i = 0; i < PANE_N; i++) {
        if (paneIsRotating(i)) {
            uint16_t c = ((plot_rotationT[i] > t0 + PLOT_ROT_WARNING) || (t0&1) == 1) ? RA8875_WHITE : GRAY;
            drawSBox (plot_b[i], c);
        }
    }

    // check BRB
    if (BRBIsRotating()) {
        uint16_t c = ((brb_rotationT > t0 + PLOT_ROT_WARNING) || (t0&1) == 1) ? RA8875_WHITE : GRAY;
        drawSBox (NCDXF_b, c);
    }

}

/* download the given hamclock url containing a bmp image and display in the given box.
 * show error messages in the given color.
 * return whether all ok
 */
bool drawHTTPBMP (const char *hc_url, const SBox &box, uint16_t color)
{
    WiFiClient client;
    bool ok = false;

    Serial.println(hc_url);
    resetWatchdog();
    if (wifiOk() && client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);

        // composite types
        union { char c[4]; uint32_t x; } i32;
        union { char c[2]; uint16_t x; } i16;

        // query web page
        httpHCGET (client, svr_host, hc_url);

        // skip response header
        if (!httpSkipHeader (client)) {
            plotMessage (box, color, _FX("Image header short"));
            goto out;
        }

        // keep track of our offset in the image file
        uint32_t byte_os = 0;
        char c;

        // read first two bytes to confirm correct format
        if (!getChar(client,&c) || c != 'B' || !getChar(client,&c) || c != 'M') {
            plotMessage (box, color, _FX("File not BMP"));
            goto out;
        }
        byte_os += 2;

        // skip down to byte 10 which is the offset to the pixels offset
        while (byte_os++ < 10) {
            if (!getChar(client,&c)) {
                plotMessage (box, color, _FX("Header offset error"));
                goto out;
            }
        }
        for (uint8_t i = 0; i < 4; i++, byte_os++) {
            if (!getChar(client,&i32.c[i])) {
                plotMessage (box, color, _FX("Pix_start error"));
                goto out;
            }
        }
        uint32_t pix_start = i32.x;
        // Serial.printf (_FX("pixels start at %d\n"), pix_start);

        // next word is subheader size, must be 40 BITMAPINFOHEADER
        for (uint8_t i = 0; i < 4; i++, byte_os++) {
            if (!getChar(client,&i32.c[i])) {
                plotMessage (box, color, _FX("Hdr size error"));
                goto out;
            }
        }
        uint32_t subhdr_size = i32.x;
        if (subhdr_size != 40) {
            Serial.printf (_FX("DIB must be 40: %d\n"), subhdr_size);
            plotMessage (box, color, _FX("DIB err"));
            goto out;
        }

        // next word is width
        for (uint8_t i = 0; i < 4; i++, byte_os++) {
            if (!getChar(client,&i32.c[i])) {
                plotMessage (box, color, _FX("Width error"));
                goto out;
            }
        }
        int32_t img_w = i32.x;

        // next word is height
        for (uint8_t i = 0; i < 4; i++, byte_os++) {
            if (!getChar(client,&i32.c[i])) {
                plotMessage (box, color, _FX("Height error"));
                goto out;
            }
        }
        int32_t img_h = i32.x;
        int32_t n_pix = img_w*img_h;
        Serial.printf (_FX("image is %d x %d = %d\n"), img_w, img_h, img_w*img_h);

        // next short is n color planes
        for (uint8_t i = 0; i < 2; i++, byte_os++) {
            if (!getChar(client,&i16.c[i])) {
                plotMessage (box, color, _FX("Planes error"));
                goto out;
            }
        }
        uint16_t n_planes = i16.x;
        if (n_planes != 1) {
            Serial.printf (_FX("planes must be 1: %d\n"), n_planes);
            plotMessage (box, color, _FX("N Planes error"));
            goto out;
        }

        // next short is bits per pixel
        for (uint8_t i = 0; i < 2; i++, byte_os++) {
            if (!getChar(client,&i16.c[i])) {
                plotMessage (box, color, _FX("bits/pix error"));
                goto out;
            }
        }
        uint16_t n_bpp = i16.x;
        if (n_bpp != 24) {
            Serial.printf (_FX("bpp must be 24: %d\n"), n_bpp);
            plotMessage (box, color, _FX("BPP error"));
            goto out;
        }

        // next word is compression method
        for (uint8_t i = 0; i < 4; i++, byte_os++) {
            if (!getChar(client,&i32.c[i])) {
                plotMessage (box, color, _FX("Compression error"));
                goto out;
            }
        }
        uint32_t comp = i32.x;
        if (comp != 0) {
            Serial.printf (_FX("compression must be 0: %d\n"), comp);
            plotMessage (box, color, _FX("Comp error"));
            goto out;
        }

        // skip down to start of pixels
        while (byte_os++ <= pix_start) {
            if (!getChar(client,&c)) {
                plotMessage (box, color, _FX("Header 3 error"));
                goto out;
            }
        }

        // prep logical box
        prepPlotBox (box);

        // display box depends on actual output size.
        SBox v_b;
        v_b.x = box.x * tft.SCALESZ;
        v_b.y = box.y * tft.SCALESZ;
        v_b.w = box.w * tft.SCALESZ;
        v_b.h = box.h * tft.SCALESZ;

        // clip and center the image within v_b
        uint16_t xborder = img_w > v_b.w ? (img_w - v_b.w)/2 : 0;
        uint16_t yborder = img_h > v_b.h ? (img_h - v_b.h)/2 : 0;

        // scan all pixels ...
        for (uint16_t img_y = 0; img_y < img_h; img_y++) {

            // keep time active
            resetWatchdog();
            updateClocks(false);

            for (uint16_t img_x = 0; img_x < img_w; img_x++) {

                char b, g, r;

                // read next pixel -- note order!
                if (!getChar (client, &b) || !getChar (client, &g) || !getChar (client, &r)) {
                    // allow a little loss because ESP TCP stack can fall behind while also drawing
                    int32_t n_draw = img_y*img_w + img_x;
                    if (n_draw > 9*n_pix/10) {
                        // close enough
                        Serial.printf (_FX("read error after %d pixels but good enough\n"), n_draw);
                        ok = true;
                        goto out;
                    } else {
                        Serial.printf (_FX("read error after %d pixels\n"), n_draw);
                        plotMessage (box, color, _FX("File is short"));
                        goto out;
                    }
                }

                // ... but only draw if fits inside border
                if (img_x > xborder && img_x < xborder + v_b.w - tft.SCALESZ
                            && img_y > yborder && img_y < yborder + v_b.h - tft.SCALESZ) {

                    uint8_t ur = r;
                    uint8_t ug = g;
                    uint8_t ub = b;
                    uint16_t color16 = RGB565(ur,ug,ub);
                    tft.drawSubPixel (v_b.x + img_x - xborder,
                                v_b.y + v_b.h - (img_y - yborder) - 1, color16); // vertical flip
                }
            }

            // skip padding to bring total row length to multiple of 4
            uint8_t extra = img_w % 4;
            if (extra > 0) {
                for (uint8_t i = 0; i < 4 - extra; i++) {
                    if (!getChar(client,&c)) {
                        plotMessage (box, color, _FX("Row padding error"));
                        goto out;
                    }
                }
            }
        }

        // Serial.println (F("image complete"));
        ok = true;

    } else {
        plotMessage (box, color, _FX("Connection failed"));
    }

out:
    client.stop();
    return (ok);
}

/* wait until:
 *   a tap occurs inside inbox;
 *   (*fp)() (IFF fp != NULL) returns true; or
 *   nothing happens for to_ms millis.
 * if tap inbox return location and true, else false for all other cases.
 * while waiting we optionally update clocks and allow some web server commands.
 */
bool waitForTap (const SBox &inbox, bool (*fp)(void), uint32_t to_ms, bool update_clocks, SCoord &tap)
{
    drainTouch();

    uint32_t t0 = millis();
    for(;;) {

        SCoord s;
        if (readCalTouchWS(s) != TT_NONE) {
            drainTouch();
            if (inBox (s, inbox)) {
                tap = s;
                return(true);
            }
            t0 = millis();
        }

        if (timesUp (&t0, to_ms))
            return (false);

        if (fp && (*fp)())
            return (false);

        if (update_clocks)
            updateClocks(false);

        wdDelay (100);

        // refresh protected region in case X11 window is moved
        tft.drawPR();
    }
}
