/* plot management
 * each PlotPane is in one of PlotChoice state at any given time, all must be different.
 * each pane rotates through the set of bits in its rotset.
 */

#include "HamClock.h"


const SBox plot_b[PANE_N] = {
    {0,   148, PLOTBOX0_W,   PLOTBOX0_H},
    {235, 0,   PLOTBOX123_W, PLOTBOX123_H},
    {405, 0,   PLOTBOX123_W, PLOTBOX123_H},
    {575, 0,   PLOTBOX123_W, PLOTBOX123_H},
};
PlotChoice plot_ch[PANE_N];
uint32_t plot_rotset[PANE_N];

#define X(a,b)  b,                      // expands PLOTNAMES to name and comma
const char *plot_names[PLOT_CH_N] = {
    PLOTNAMES
};
#undef X

/* retrieve the plot choice for the given pane from NV, if set
 */
static bool getPlotChoiceNV (PlotPane new_pp, PlotChoice *new_pc)
{
    bool ok = false;
    uint8_t pc;

    switch (new_pp) {
    case PANE_0:
        ok = NVReadUInt8 (NV_PLOT_0, &pc);
        break;
    case PANE_1:
        ok = NVReadUInt8 (NV_PLOT_1, &pc);
        break;
    case PANE_2:
        ok = NVReadUInt8 (NV_PLOT_2, &pc);
        break;
    case PANE_3:
        ok = NVReadUInt8 (NV_PLOT_3, &pc);
        break;
    case PANE_N:
        break;
    // no default in order to check coverage at compile time
    }

    // beware just bonkers
    if (pc >= PLOT_CH_N)
        return (false);

    if (ok)
        *new_pc = (PlotChoice)pc;
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
        // default for PANE_0 is PLOT_CH_NONE, others are from a standard set
        if (pp == PANE_0) {
            plot_ch[pp] = PLOT_CH_NONE;
            Serial.println (F("PANE: Setting pane 0 to default NONE"));
        } else {
            const PlotChoice ch_defaults[PANE_N] = {PLOT_CH_SSN, PLOT_CH_XRAY, PLOT_CH_SDO};
            plot_ch[pp] = ch_defaults[pp];
            plot_rotset[pp] = (1 << plot_ch[pp]);
            Serial.printf (_FX("PANE: Setting pane %d to default %s\n"), (int)pp, plot_names[plot_ch[pp]]);
        }
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
bool plotChoiceIsAvailable (PlotChoice pc)
{
    switch (pc) {

    case PLOT_CH_DXCLUSTER:     return (useDXCluster());
    case PLOT_CH_GIMBAL:        return (haveGimbal());
    case PLOT_CH_TEMPERATURE:   return (getNBMEConnected() > 0);
    case PLOT_CH_PRESSURE:      return (getNBMEConnected() > 0);
    case PLOT_CH_HUMIDITY:      return (getNBMEConnected() > 0);
    case PLOT_CH_DEWPOINT:      return (getNBMEConnected() > 0);
    case PLOT_CH_COUNTDOWN:     return (getSWEngineState(NULL,NULL) == SWE_COUNTDOWN);
    case PLOT_CH_DEWX:          return ((brb_rotset & (1 << BRB_SHOW_DEWX)) == 0);
    case PLOT_CH_DXWX:          return ((brb_rotset & (1 << BRB_SHOW_DXWX)) == 0);
    case PLOT_CH_ADIF:          return (getADIFilename() != NULL);

    // the remaining pane type are always available

    case PLOT_CH_BC:            // fallthru
    case PLOT_CH_FLUX:          // fallthru
    case PLOT_CH_KP:            // fallthru
    case PLOT_CH_MOON:          // fallthru
    case PLOT_CH_NOAASPW:       // fallthru
    case PLOT_CH_SSN:           // fallthru
    case PLOT_CH_XRAY:          // fallthru
    case PLOT_CH_SDO:           // fallthru
    case PLOT_CH_SOLWIND:       // fallthru
    case PLOT_CH_DRAP:          // fallthru
    case PLOT_CH_CONTESTS:      // fallthru
    case PLOT_CH_PSK:           // fallthru
    case PLOT_CH_BZBT:          // fallthru
    case PLOT_CH_POTA:          // fallthru
    case PLOT_CH_SOTA:          // fallthru
    case PLOT_CH_AURORA:        // fallthru
        return (true);

    case PLOT_CH_N:
        break;                  // lint
    }

    return (false);

}

/* log the rotation set for the given pain, tag PlotChoice if in the set.
 */
void logPaneRotSet (PlotPane pp, PlotChoice pc)
{
    Serial.printf (_FX("Pane %d choices:\n"), (int)pp);
    for (int i = 0; i < PLOT_CH_N; i++)
        if (plot_rotset[pp] & (1 << i))
            Serial.printf (_FX("    %c%s\n"), i == pc ? '*' : ' ', plot_names[i]);
}

/* log the BRB rotation set
 */
void logBRBRotSet()
{
    Serial.printf (_FX("BRB: choices:\n"));
    for (int i = 0; i < BRB_N; i++)
        if (brb_rotset & (1 << i))
            Serial.printf (_FX("    %c%s\n"), i == brb_mode ? '*' : ' ', brb_names[i]);
    Serial.printf (_FX("BRB: now mode %d\n"), brb_mode);
}

/* return whether all panes in the given rotation set can be accommodated together.
 */
bool paneComboOk (const uint32_t new_rotsets[PANE_N])
{
#if !defined(_IS_ESP8266)
    // only an issue on ESP
    return (true);
#else
    // count list of panes that use dynmic memory
    static uint8_t himem_panes[] PROGMEM = {
        PLOT_CH_DXCLUSTER, PLOT_CH_TEMPERATURE, PLOT_CH_PRESSURE, PLOT_CH_HUMIDITY, PLOT_CH_DEWPOINT,
        PLOT_CH_CONTESTS, PLOT_CH_PSK, PLOT_CH_POTA, PLOT_CH_SOTA, PLOT_CH_ADIF
    };
    int n_used = 0;
    for (int i = 0; i < PANE_N; i++)
        for (int j = 0; j < NARRAY(himem_panes); j++)
            if ((1 << pgm_read_byte(&himem_panes[j])) & new_rotsets[i])
                n_used++;

    // allow only 1
    return (n_used <= 1);

#endif
}

/* if the given rotset include PLOT_CH_DXCLUSTER and more, show message in box and return true.
 * else return false.
 */
bool enforceDXCAlone (const SBox &box, uint32_t rotset)
{
    if ((rotset & (1<<PLOT_CH_DXCLUSTER)) && (rotset & ~(1<<PLOT_CH_DXCLUSTER))) {
        plotMessage (box, RA8875_RED, _FX("DX Cluster may not be combined with other data panes"));
        wdDelay(5000);
        return (true);
    }
    return (false);
}

/* show a table of suitable plot choices in and for the given pane and allow user to choose one or more.
 * always return a selection even if it's the current selection again, never PLOT_CH_NONE.
 * N.B. do not use this for PANE_0
 */
static PlotChoice askPaneChoice (PlotPane pp)
{
    resetWatchdog();

    // not for use for PANE_0
    if (pp == PANE_0)
        fatalError (_FX("askPaneChoice called with pane 0"));

    // set this temporarily to show all choices, just for testing worst-case layout
    #define ASKP_SHOWALL 0                      // RBF

    // build items from all candidates suitable for this pane
    MenuItem *mitems = NULL;
    int n_mitems = 0;
    for (int i = 0; i < PLOT_CH_N; i++) {
        PlotChoice pc = (PlotChoice) i;
        PlotPane pp_ch = findPaneForChoice (pc);

        // do not allow cluster on pane 1 with DEDX Wx to avoid disconnect each time DX/DE wx
        if (pp == PANE_1 && pc == PLOT_CH_DXCLUSTER && showNewDXDEWx())
            continue;

        // otherwise use if not used elsewhere and available or already assigned to this pane
        if ( (pp_ch == PANE_NONE && plotChoiceIsAvailable(pc)) || pp_ch == pp || ASKP_SHOWALL) {
            // set up next menu item
            mitems = (MenuItem *) realloc (mitems, (n_mitems+1)*sizeof(MenuItem));
            if (!mitems)
                fatalError ("pane alloc: %d", n_mitems); // no _FX if alloc failing
            MenuItem &mi = mitems[n_mitems++];
            mi.type = MENU_AL1OFN;
            mi.set = (plot_rotset[pp] & (1 << pc)) ? true : false;
            mi.label = plot_names[pc];
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

        // find new rotset for this pane
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

        // enforce limit on number of high-memory scrolling panes and DX cluster alone
        uint32_t new_sets[PANE_N];
        memcpy (new_sets, plot_rotset, sizeof(new_sets));
        new_sets[pp] = new_rotset;
        if (isSatDefined() && !paneComboOk(new_sets)) {

            plotMessage (box, RA8875_RED, _FX("Too many high-memory panes with a satellite"));
            wdDelay(5000);

        } else if (!enforceDXCAlone (box, new_rotset)) {

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
PlotPane findPaneChoiceNow (PlotChoice pc)
{
    for (int i = PANE_0; i < PANE_N; i++)
        if (plot_ch[i] == pc)
            return ((PlotPane)i);
    return (PANE_NONE);
}

/* return which pane has the given choice in its rotation set _even if not currently visible_, else PANE_NONE
 */
PlotPane findPaneForChoice (PlotChoice pc)
{
    for (int i = PANE_0; i < PANE_N; i++)
        if ( (plot_rotset[i] & (1<<pc)) )
            return ((PlotPane)i);
    return (PANE_NONE);
}

/* given a current choice, select the next rotation plot choice for the given pane.
 * if not rotating return the same choice.
 */
PlotChoice getNextRotationChoice (PlotPane pp, PlotChoice pc)
{
    if (paneIsRotating (pp)) {
        for (int i = 1; i < PLOT_CH_N; i++) {
            int j = (pc + i) % PLOT_CH_N;
            if (plot_rotset[pp] & (1 << j))
                return ((PlotChoice)j);
        }
    } else
        return (pc);

    fatalError (_FX("getNextRotationChoice() none for pane %d"), (int)pp);
    return (pc); // lint because fatalError never returns
}

/* return any available unassigned plot choice
 */
PlotChoice getAnyAvailableChoice()
{
    int s = random (PLOT_CH_N);
    for (int i = 0; i < PLOT_CH_N; i++) {
        PlotChoice pc = (PlotChoice)((s + i) % PLOT_CH_N);
        if (plotChoiceIsAvailable (pc)) {
            bool inuse = false;
            for (int j = 0; !inuse && j < PANE_N; j++) {
                if (plot_ch[j] == pc || (plot_rotset[j] & (1 << pc))) {
                    inuse = true;
                }
            }
            if (!inuse)
                return (pc);
        }
    }
    fatalError (_FX("no available pane choices"));

    // never get here, just for lint
    return (PLOT_CH_FLUX);
}

/* remove any PLOT_CH_COUNTDOWN from rotset if stopwatch engine not SWE_COUNTDOWN,
 * and if it is currently visible replace with an alternative.
 * N.B. PANE_0 can never be PLOT_CH_COUNTDOWN
 */
void insureCountdownPaneSensible()
{
    if (getSWEngineState(NULL,NULL) != SWE_COUNTDOWN) {
        for (int i = PANE_1; i < PANE_N; i++) {
            if (plot_rotset[i] & (1 << PLOT_CH_COUNTDOWN)) {
                plot_rotset[i] &= ~(1 << PLOT_CH_COUNTDOWN);
                if (plot_ch[i] == PLOT_CH_COUNTDOWN) {
                    setDefaultPaneChoice((PlotPane)i);
                    if (!setPlotChoice ((PlotPane)i, plot_ch[i])) {
                        fatalError (_FX("can not replace Countdown pain %d with %s"),
                                    i, plot_names[plot_ch[i]]);
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
    // ignore pane 1 taps while reverting
    if (pp == PANE_1 && ignorePane1Touch())
        return (false);

    // for sure not ours if not even in this box
    const SBox &box = plot_b[pp];
    if (!inBox (s, box))
        return (false);

    // reserve top 20% for bringing up choice menu
    bool in_top = s.y < box.y + PANETITLE_H;

    // check the choices that have their own active areas
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
    case PLOT_CH_CONTESTS:
        if (checkContestsTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_SDO:
        if (checkSDOTouch (s, box))
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
    case PLOT_CH_SSN:
        if (!in_top) {
            plotMap (_FX("/ssn/ssn-history.txt"), _FX("SIDC Sunspot History"), SSN_COLOR);
            initEarthMap();
            return(true);
        }
        break;
    case PLOT_CH_FLUX:
        if (!in_top) {
            plotMap (_FX("/solar-flux/solarflux-history.txt"), _FX("10.7 cm Solar Flux History"),SFLUX_COLOR);
            initEarthMap();
            return(true);
        }
        break;
    case PLOT_CH_PSK:
        if (checkPSKTouch (s, box))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_POTA:
        if (checkOnTheAirTouch (s, box, ONTA_POTA))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_SOTA:
        if (checkOnTheAirTouch (s, box, ONTA_SOTA))
            return (true);
        in_top = true;
        break;
    case PLOT_CH_ADIF:
        if (checkADIFTouch (s, box))
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
    if (pp == PANE_0) {
        drawDEFormatMenu();
    } else {

        PlotChoice pc = askPaneChoice(pp);

        // always engage even if same to erase menu
        if (!setPlotChoice (pp, pc)) {
            fatalError (_FX("checkPlotTouch bad choice %d pane %d"), (int)pc, (int)pp);
            // never returns
        }
    }

    // it was ours
    return (true);
}

/* called once to init plot info from NV and insure legal and consistent values.
 * N.B. PANE_0 is the only pane allowed to be PLOT_CH_NONE
 */
void initPlotPanes()
{
    // retrieve rotation sets -- ok to leave 0 for now if not yet defined
    memset (plot_rotset, 0, sizeof(plot_rotset));
    NVReadUInt32 (NV_PANE0ROTSET, &plot_rotset[PANE_0]);
    NVReadUInt32 (NV_PANE1ROTSET, &plot_rotset[PANE_1]);
    NVReadUInt32 (NV_PANE2ROTSET, &plot_rotset[PANE_2]);
    NVReadUInt32 (NV_PANE3ROTSET, &plot_rotset[PANE_3]);

    // rm any choice not available, including dx cluster in pane 1
    for (int i = PANE_0; i < PANE_N; i++) {
        plot_rotset[i] &= ((1 << PLOT_CH_N) - 1);        // reset any bits too high
        for (int j = 0; j < PLOT_CH_N; j++) {
            if (plot_rotset[i] & (1 << j)) {
                if (i == PANE_1 && j == PLOT_CH_DXCLUSTER && showNewDXDEWx()) {
                    plot_rotset[i] &= ~(1 << j);
                    Serial.printf (_FX("PANE: Removing %s from pane %d: not allowed\n"), plot_names[j], i);
                } else if (!plotChoiceIsAvailable ((PlotChoice)j)) {
                    plot_rotset[i] &= ~(1 << j);
                    Serial.printf (_FX("PANE: Removing %s from pane %d: not available\n"), plot_names[j],i);
                }
            }
        }
    }

    // if current selection not yet defined or not in rotset pick one from rotset or set a default
    for (int i = PANE_0; i < PANE_N; i++) {
        if (!getPlotChoiceNV ((PlotPane)i, &plot_ch[i]) || plot_ch[i] >= PLOT_CH_N
                                                        || !(plot_rotset[i] & (1 << plot_ch[i])))
            setDefaultPaneChoice ((PlotPane)i);
    }

    // insure same choice not in more than 1 pane
    for (int i = PANE_0; i < PANE_N; i++) {
        for (int j = i+1; j < PANE_N; j++) {
            if (plot_ch[i] == plot_ch[j]) {
                // found dup -- replace with some other unused choice
                for (int k = 0; k < PLOT_CH_N; k++) {
                    PlotChoice new_pc = (PlotChoice)k;
                    if (plotChoiceIsAvailable(new_pc) && findPaneChoiceNow(new_pc) == PANE_NONE) {
                        Serial.printf (_FX("PANE: Reassigning dup pane %d from %s to %s\n"), j,
                                        plot_names[plot_ch[j]], plot_names[new_pc]);
                        // remove dup from rotation set then replace with new choice
                        plot_rotset[j] &= ~(1 << plot_ch[j]);
                        plot_rotset[j] |= (1 << new_pc);
                        plot_ch[j] = new_pc;
                        break;
                    }
                }
            }
        }
    }

    // enforce PLOT_CH_DXCLUSTER is alone, if any
    for (int i = PANE_0; i < PANE_N; i++) {
        if (plot_rotset[i] & (1 << PLOT_CH_DXCLUSTER)) {
            plot_rotset[i] = (1 << PLOT_CH_DXCLUSTER);
            plot_ch[i] = PLOT_CH_DXCLUSTER;
            Serial.printf (_FX("isolating DX Cluster in pane %d\n"), i+i);
            break;
        }
    }

    // one last bit of paranoia: insure each pane choice is in its rotation set unless empty
    for (int i = PANE_0; i < PANE_N; i++)
        if (plot_ch[i] != PLOT_CH_NONE)
            plot_rotset[i] |= (1 << plot_ch[i]);

    // log and save final arrangement
    for (int i = PANE_0; i < PANE_N; i++)
        logPaneRotSet ((PlotPane)i, plot_ch[i]);
    savePlotOps();
}

/* update NV_PANE?CH from plot_rotset[] and NV_PLOT_? from plot_ch[]
 */
void savePlotOps()
{
    NVWriteUInt32 (NV_PANE0ROTSET, plot_rotset[PANE_0]);
    NVWriteUInt32 (NV_PANE1ROTSET, plot_rotset[PANE_1]);
    NVWriteUInt32 (NV_PANE2ROTSET, plot_rotset[PANE_2]);
    NVWriteUInt32 (NV_PANE3ROTSET, plot_rotset[PANE_3]);

    NVWriteUInt8 (NV_PLOT_0, plot_ch[PANE_0]);
    NVWriteUInt8 (NV_PLOT_1, plot_ch[PANE_1]);
    NVWriteUInt8 (NV_PLOT_2, plot_ch[PANE_2]);
    NVWriteUInt8 (NV_PLOT_3, plot_ch[PANE_3]);
}

/* flash plot borders nearly ready to change, and include NCDXF_b also.
 */
void showRotatingBorder ()
{
    time_t t0 = myNow();

    // check plot panes
    for (int i = 0; i < PANE_N; i++) {
        if (paneIsRotating((PlotPane)i) || (isSDORotating() && findPaneChoiceNow(PLOT_CH_SDO) == i)) {
            // this pane is rotating among other pane choices or SDO is rotating its images
            uint16_t c = ((nextPaneRotation((PlotPane)i) > t0 + PLOT_ROT_WARNING) || (t0&1) == 1)
                                ? RA8875_WHITE : GRAY;
            drawSBox (plot_b[i], c);
        }
    }

    // check BRB
    if (BRBIsRotating()) {
        uint16_t c = ((brb_updateT > t0 + PLOT_ROT_WARNING) || (t0&1) == 1) ? RA8875_WHITE : GRAY;
        drawSBox (NCDXF_b, c);
    }

}

/* read a bmp image from the given connection and display in the given box.
 * return true else false with short reason in ynot[].
 * N.B. either way we do NOT close client.
 */
bool installBMP (WiFiClient &client, const SBox &box, char ynot[], size_t ynot_len)
{
    // stay alert
    resetWatchdog();
    updateClocks(false);

    // composite types
    union { char c[4]; uint32_t x; } i32;
    union { char c[2]; uint16_t x; } i16;

    // keep track of our offset in the image file
    uint32_t byte_os = 0;
    char c;

    // read first two bytes to confirm correct format
    if (!getTCPChar(client,&c) || c != 'B' || !getTCPChar(client,&c) || c != 'M') {
        snprintf (ynot, ynot_len, _FX("File not BMP"));
        return (false);
    }
    byte_os += 2;

    // skip down to byte 10 which is the offset to the pixels offset
    while (byte_os++ < 10) {
        if (!getTCPChar(client,&c)) {
            snprintf (ynot, ynot_len, _FX("Header offset error"));
            return (false);
        }
    }
    for (uint8_t i = 0; i < 4; i++, byte_os++) {
        if (!getTCPChar(client,&i32.c[i])) {
            snprintf (ynot, ynot_len, _FX("Pix_start error"));
            return (false);
        }
    }
    uint32_t pix_start = i32.x;
    // Serial.printf (_FX("pixels start at %d\n"), pix_start);

    // next word is subheader size, must be 40 BITMAPINFOHEADER
    for (uint8_t i = 0; i < 4; i++, byte_os++) {
        if (!getTCPChar(client,&i32.c[i])) {
            snprintf (ynot, ynot_len, _FX("Hdr size error"));
            return (false);
        }
    }
    uint32_t subhdr_size = i32.x;
    if (subhdr_size != 40) {
        Serial.printf (_FX("DIB must be 40: %d\n"), subhdr_size);
        snprintf (ynot, ynot_len, _FX("DIB err"));
        return (false);
    }

    // next word is width
    for (uint8_t i = 0; i < 4; i++, byte_os++) {
        if (!getTCPChar(client,&i32.c[i])) {
            snprintf (ynot, ynot_len, _FX("Width error"));
            return (false);
        }
    }
    int32_t img_w = i32.x;

    // next word is height
    for (uint8_t i = 0; i < 4; i++, byte_os++) {
        if (!getTCPChar(client,&i32.c[i])) {
            snprintf (ynot, ynot_len, _FX("Height error"));
            return (false);
        }
    }
    int32_t img_h = i32.x;
    int32_t n_pix = img_w*img_h;
    Serial.printf (_FX("image is %d x %d = %d\n"), img_w, img_h, img_w*img_h);

    // next short is n color planes
    for (uint8_t i = 0; i < 2; i++, byte_os++) {
        if (!getTCPChar(client,&i16.c[i])) {
            snprintf (ynot, ynot_len, _FX("Planes error"));
            return (false);
        }
    }
    uint16_t n_planes = i16.x;
    if (n_planes != 1) {
        Serial.printf (_FX("planes must be 1: %d\n"), n_planes);
        snprintf (ynot, ynot_len, _FX("N Planes error"));
        return (false);
    }

    // next short is bits per pixel
    for (uint8_t i = 0; i < 2; i++, byte_os++) {
        if (!getTCPChar(client,&i16.c[i])) {
            snprintf (ynot, ynot_len, _FX("bits/pix error"));
            return (false);
        }
    }
    uint16_t n_bpp = i16.x;
    if (n_bpp != 24) {
        Serial.printf (_FX("bpp must be 24: %d\n"), n_bpp);
        snprintf (ynot, ynot_len, _FX("BPP error"));
        return (false);
    }

    // next word is compression method
    for (uint8_t i = 0; i < 4; i++, byte_os++) {
        if (!getTCPChar(client,&i32.c[i])) {
            snprintf (ynot, ynot_len, _FX("Compression error"));
            return (false);
        }
    }
    uint32_t comp = i32.x;
    if (comp != 0) {
        Serial.printf (_FX("compression must be 0: %d\n"), comp);
        snprintf (ynot, ynot_len, _FX("Comp error"));
        return (false);
    }

    // skip down to start of pixels
    while (byte_os++ <= pix_start) {
        if (!getTCPChar(client,&c)) {
            snprintf (ynot, ynot_len, _FX("Header 3 error"));
            return (false);
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

    // center the image within v_b
    uint16_t imgx_border = img_w > v_b.w ? (img_w - v_b.w)/2 : 0;
    uint16_t imgy_border = img_h > v_b.h ? (img_h - v_b.h)/2 : 0;
    uint16_t boxx_border = img_w < v_b.w ? (v_b.w - img_w)/2 : 0;
    uint16_t boxy_border = img_h < v_b.h ? (v_b.h - img_h)/2 : 0;

    // scan all pixels ...
    for (uint16_t img_y = 0; img_y < img_h; img_y++) {

        // keep time active
        resetWatchdog();
        updateClocks(false);

        for (uint16_t img_x = 0; img_x < img_w; img_x++) {

            char b, g, r;

            // read next pixel -- note order!
            if (!getTCPChar (client, &b) || !getTCPChar (client, &g) || !getTCPChar (client, &r)) {
                // allow a little loss because ESP TCP stack can fall behind while also drawing
                int32_t n_draw = img_y*img_w + img_x;
                if (n_draw > 9*n_pix/10) {
                    // close enough
                    Serial.printf (_FX("read error after %d pixels but good enough\n"), n_draw);
                    break;
                } else {
                    Serial.printf (_FX("read error after %d pixels\n"), n_draw);
                    snprintf (ynot, ynot_len, _FX("File is short"));
                    return (false);
                }
            }

            // ... but only draw what fits inside box
            if (img_x > imgx_border && img_x < img_w - imgx_border - tft.SCALESZ
                        && img_y > imgy_border && img_y < img_h - imgy_border - tft.SCALESZ) {

                uint8_t ur = r;
                uint8_t ug = g;
                uint8_t ub = b;
                uint16_t color16 = RGB565(ur,ug,ub);
                tft.drawPixelRaw (v_b.x + boxx_border + img_x - imgx_border,
                        v_b.y + v_b.h - (boxy_border + img_y - imgy_border) - 1, color16); // vertical flip
            }
        }

        // skip padding to bring total row length to multiple of 4
        uint8_t extra = img_w % 4;
        if (extra > 0) {
            for (uint8_t i = 0; i < 4 - extra; i++) {
                if (!getTCPChar(client,&c)) {
                    snprintf (ynot, ynot_len, _FX("Row padding error"));
                    return (false);
                }
            }
        }
    }

    // finally!
    return (true);
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
    if (wifiOk() && client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (client, backend_host, hc_url);

        // skip response header
        if (!httpSkipHeader (client)) {
            plotMessage (box, color, _FX("Image header short"));
            goto out;
        }

        // proceed
        char ynot[100];
        size_t prefix_l = snprintf (ynot, sizeof(ynot), _FX("Image error: "));
        if (installBMP (client, box, ynot+prefix_l, sizeof(ynot)-prefix_l)) {
            // Serial.println (F("image complete"));
            ok = true;
        } else {
            plotMessage (box, color, ynot);
        }

    } else {
        plotMessage (box, color, _FX("Connection failed"));
    }

out:
    client.stop();
    return (ok);
}

/* given min and max and an approximate number of divisions desired,
 * fill in ticks[] with nicely spaced values and return how many.
 * N.B. return value, and hence number of entries to ticks[], might be as
 *   much as 2 more than numdiv.
 */
int tickmarks (float min, float max, int numdiv, float ticks[])
{
    static int factor[] = { 1, 2, 5 };
    #define NFACTOR    NARRAY(factor)
    float minscale;
    float delta;
    float lo;
    float v;
    int n;

    minscale = fabsf (max - min);

    if (minscale == 0) {
        /* null range: return ticks in range min-1 .. min+1 */
        for (n = 0; n < numdiv; n++)
            ticks[n] = min - 1.0 + n*2.0/numdiv;
        return (numdiv);
    }

    delta = minscale/numdiv;
    for (n=0; n < (int)NFACTOR; n++) {
        float scale;
        float x = delta/factor[n];
        if ((scale = (powf(10.0F, ceilf(log10f(x)))*factor[n])) < minscale)
            minscale = scale;
    }
    delta = minscale;

    lo = floor(min/delta);
    for (n = 0; (v = delta*(lo+n)) < max+delta; )
        ticks[n++] = v;

    return (n);
}

/* return whether any pane is currently rotating to other panes
 */
bool paneIsRotating (PlotPane pp)
{
    return ((plot_rotset[pp] & ~(1 << plot_ch[pp])) != 0);  // look for any bit on other than plot_ch
}

/* restore normal PANE_0
 */
void restoreNormPANE0(void)
{
    plot_ch[PANE_0] = PLOT_CH_NONE;
    plot_rotset[PANE_0] = 0;

    drawOneTimeDE();
    drawDEInfo();
    drawOneTimeDX();
    drawDXInfo();
}
