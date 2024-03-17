/* manage space weather stats.
 */


#include "HamClock.h"


// retrieve pages
static const char bzbt_page[] PROGMEM = "/Bz/Bz.txt";
static const char swind_page[] PROGMEM = "/solar-wind/swind-24hr.txt";
static const char ssn_page[] PROGMEM = "/ssn/ssn-31.txt";
static const char sf_page[] PROGMEM = "/solar-flux/solarflux-99.txt";
static const char drap_page[] PROGMEM = "/drap/stats.txt";
static const char kp_page[] PROGMEM = "/geomag/kindex.txt";
static const char xray_page[] PROGMEM = "/xray/xray.txt";
static const char noaaswx_page[] PROGMEM = "/NOAASpaceWX/noaaswx.txt";
static const char sw_rank_page[] PROGMEM = "/NOAASpaceWX/rank_coeffs.txt";

// space weather stats
NOAASpaceWx noaa_sw = { false, {'R', 'S', 'G'} };

#define X(a,b,c,d,e,f,g) {a,b,c,d,e,f,g},       // expands SPCWX_DATA to each array initialization in {}
SpaceWeather_t space_wx[SPCWX_N] = {
    SPCWX_DATA
};
#undef X


// handy conversion from space_wx value to it ranking contribution
#define SW_RANKV(sp)     ((int)roundf((sp)->m * (sp)->value + (sp)->b))

/* qsort-style function to compare the scaled value of two SpaceWeather_t.
 * N.B. largest first, any SPW_ERR at the end
 */
static int swQSF (const void *v1, const void *v2)
{
    const SpaceWeather_t *s1 = (SpaceWeather_t *)v1;
    const SpaceWeather_t *s2 = (SpaceWeather_t *)v2;

    if (s1->value == SPW_ERR)
        return (s2->value != SPW_ERR);
    else if (s2->value == SPW_ERR)
        return (-1);

    int rank_val1 = SW_RANKV(s1);
    int rank_val2 = SW_RANKV(s2);
    return (rank_val2 - rank_val1);
}

/* init the slope and intercept of each space wx stat from sw_rank_page.
 * return whether successful.
 */
static bool initSWMB(void)
{
    WiFiClient sw_client;
    bool ok = false;

    Serial.println (sw_rank_page);
    resetWatchdog();
    if (wifiOk() && sw_client.connect(backend_host, backend_port)) {

        resetWatchdog();
        updateClocks(false);

        httpHCPGET (sw_client, backend_host, sw_rank_page);

        if (!httpSkipHeader (sw_client)) {
            Serial.println (F("AUTOSW: rank page header short"));
            goto out;
        }

        char line[50];
        float m[SPCWX_N], b[SPCWX_N];       // local until know all are good
        for (int n_c = 0; n_c < SPCWX_N; ) {
            // read line
            if (!getTCPLine (sw_client, line, sizeof(line), NULL)) {
                Serial.printf (_FX("AUTOSW: rank file is short: %d/%d\n"), n_c/SPCWX_N);
                goto out;
            }

            // ignore comments or empty
            if (line[0] == '#' || line[0] == '\0')
                continue;

            // require matching index and m and b coeffs
            int get_i;
            if (sscanf (line, _FX("%d %f %f"), &get_i, &m[n_c], &b[n_c]) != 3 || get_i != n_c) {
                Serial.printf (_FX("AUTOSW: bad rank line: %s\n"), line);
                goto out;
            }

            // count
            n_c++;
        }

        // all good, log and store
        Serial.println (F("AUTOSW:   Coeffs Name       m       b"));
        for (int i = 0; i < SPCWX_N; i++) {
            SpaceWeather_t &swi = space_wx[i];
            swi.m = m[i];
            swi.b = b[i];
            Serial.printf (_FX("AUTOSW: %13s %7g %7g\n"), plot_names[swi.pc], swi.m, swi.b);
        }

        ok = true;
    }

  out:

    // clean up -- aleady logged any errors
    sw_client.stop();
    resetWatchdog();
    return (ok);
}

/* go through space_wx and set the rank according to the importance of each value.
 */
static void rankSpaceWx()
{
    // try one time to init all space_wx m and b first time, note and always bale if fail.
    static bool set_mb, mb_ok;
    if (!set_mb) {
        set_mb = true;
        mb_ok = initSWMB();
        if (!mb_ok)
            Serial.println (F("AUTOSW: no ranking available -- using default"));
    }
    if (!mb_ok)
        return;                 // use default ranking

    // copy space_wx and sort best first
    SpaceWeather_t sw_sort[SPCWX_N];
    memcpy (sw_sort, space_wx, sizeof(sw_sort));
    qsort (sw_sort, SPCWX_N, sizeof(SpaceWeather_t), swQSF);
    
    // set and record rank of each entry
    Serial.println (F("AUTOSW: rank      name    value score"));
    for (int i = 0; i < SPCWX_N; i++) {
        SPCWX_t spi = sw_sort[i].sp;
        SpaceWeather_t &swi = space_wx[spi];
        swi.rank = i;
        Serial.printf (_FX("AUTOSW: %d %12s %8.2g %3d\n"), i, plot_names[swi.pc], swi.value,
                                SW_RANKV(&swi));
    }
}

/* given touch location s known to be within NCDXF_b, insure that space stat is in a visible Pane.
 * N.B. coordinate with drawSpaceStats()
 */
void doSpaceStatsTouch (const SCoord &s)
{
    // list of plot choices ordered by rank
    PlotChoice pcs[NCDXF_B_NFIELDS];
    for (int i = 0; i < NCDXF_B_NFIELDS; i++) {
        for (int j = 0; j < SPCWX_N; j++) {
            if (space_wx[j].rank == i) {
                pcs[i] = space_wx[j].pc;
                break;
            }
        }
    }

    // do it
    doNCDXFStatsTouch (s, pcs);
}

/* draw the NCDXF_B_NFIELDS highest ranking space_wx in NCDXF_b.
 * use the given color for everything unless black then use the associated pane colors.
 */
void drawSpaceStats(uint16_t color)
{
    // arrays for drawNCDXFStats()
    static const char err[] = "Err";
    char titles[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN];
    char values[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN];
    uint16_t colors[NCDXF_B_NFIELDS];

    // assign by rank, 0 first
    for (int i = 0; i < NCDXF_B_NFIELDS; i++) {
        for (int j = 0; j < SPCWX_N; j++) {
            if (space_wx[j].rank == i) {

                switch ((SPCWX_t)j) {

                case SPCWX_SSN:
                    strcpy (titles[i], _FX("SSN"));
                    if (space_wx[SPCWX_SSN].value == SPW_ERR)
                        strcpy (values[i], err);
                    else
                        snprintf (values[i], sizeof(values[i]), _FX("%.1f"), space_wx[SPCWX_SSN].value);
                    colors[i] = SSN_COLOR;
                    break;

                case SPCWX_XRAY:
                    strcpy (titles[i], _FX("X-Ray"));
                    xrayLevel(space_wx[SPCWX_XRAY].value, values[i]);
                    colors[i] = RGB565(255,134,0);      // XRAY_LCOLOR is too alarming
                    break;

                case SPCWX_FLUX:
                    strcpy (titles[i], _FX("SFI"));
                    if (space_wx[SPCWX_FLUX].value == SPW_ERR)
                        strcpy (values[i], err);
                    else
                        snprintf (values[i], sizeof(values[i]), _FX("%.1f"), space_wx[SPCWX_FLUX].value);
                    colors[i] = SFLUX_COLOR;
                    break;

                case SPCWX_KP:
                    strcpy (titles[i], _FX("Kp"));
                    if (space_wx[SPCWX_KP].value == SPW_ERR)
                        strcpy (values[i], err);
                    else
                        snprintf (values[i], sizeof(values[i]), _FX("%.1f"), space_wx[SPCWX_KP].value);
                    colors[i] = KP_COLOR;
                    break;

                case SPCWX_SOLWIND:
                    strcpy (titles[i], _FX("Sol Wind"));
                    if (space_wx[SPCWX_SOLWIND].value == SPW_ERR)
                        strcpy (values[i], err);
                    else
                        snprintf (values[i], sizeof(values[i]), _FX("%.1f"), space_wx[SPCWX_SOLWIND].value);
                    colors[i] = SWIND_COLOR;
                    break;

                case SPCWX_DRAP:
                    strcpy (titles[i], _FX("DRAP"));
                    if (space_wx[SPCWX_DRAP].value == SPW_ERR)
                        strcpy (values[i], err);
                    else
                        snprintf (values[i], sizeof(values[i]), _FX("%.1f"), space_wx[SPCWX_DRAP].value);
                    colors[i] = DRAPPLOT_COLOR;
                    break;

                case SPCWX_BZ:
                    strcpy (titles[i], _FX("Bz"));
                    if (space_wx[SPCWX_BZ].value == SPW_ERR)
                        strcpy (values[i], err);
                    else {
                        if (fabsf(space_wx[SPCWX_BZ].value) < 100)
                            snprintf (values[i], sizeof(values[i]), _FX("%.1f"), space_wx[SPCWX_BZ].value);
                        else
                            snprintf (values[i], sizeof(values[i]), _FX("%.0f"), space_wx[SPCWX_BZ].value);
                    }
                    colors[i] = BZBT_BZCOLOR;
                    break;

                case SPCWX_NOAASPW:
                    strcpy (titles[i], _FX("NOAA SpWx"));
                    if (noaa_sw.ok)
                        snprintf (values[i], sizeof(values[i]), _FX("%.0f"), space_wx[SPCWX_NOAASPW].value);
                    else
                        strcpy (values[i], err);
                    colors[i] = NOAASPW_COLOR;
                    break;

                case SPCWX_N:
                    break;              // lint

                }
            }
        }
    }

    // do it
    drawNCDXFStats (color, titles, values, colors);
}



/* retrieve latest sun spot indices and time scale in days from now.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveSunSpots (float x[SSN_NV], float ssn[SSN_NV])
{
    char line[100];
    WiFiClient ss_client;
    bool ok = false;

    // mark value as bad until proven otherwise
    space_wx[SPCWX_SSN].value = SPW_ERR;

    Serial.println(ssn_page);
    resetWatchdog();
    if (wifiOk() && ss_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCPGET (ss_client, backend_host, ssn_page);

        // skip response header
        if (!httpSkipHeader (ss_client)) {
            Serial.print (F("SSN: header fail\n"));
            goto out;
        }

        // transaction successful
        ok = true;

        // read lines into ssn array and build corresponding time value
        int8_t ssn_i;
        for (ssn_i = 0; ssn_i < SSN_NV && getTCPLine (ss_client, line, sizeof(line), NULL); ssn_i++) {
            ssn[ssn_i] = atof(line+11);
            x[ssn_i] = 1-SSN_NV + ssn_i;
        }

        updateClocks(false);
        resetWatchdog();

        // ok if all received
        if (ssn_i == SSN_NV) {

            // capture latest
            space_wx[SPCWX_SSN].value = ssn[SSN_NV-1];

        } else {

            Serial.printf (_FX("SSN: data short %d / %d\n"), ssn_i, SSN_NV);
        }
    }

out:

    // clean up
    ss_client.stop();
    resetWatchdog();
    return (ok);
}


/* update SPCWX_SSN if not recently done so by its pane.
 * return whether transaction was ok (even if data was not)
 */
static bool checkSunSpots (void)
{
    // use our own delay unless being shown in a pane
    PlotPane ssn_pp = findPaneChoiceNow (PLOT_CH_SSN);
    time_t *next_p = ssn_pp == PANE_NONE ? &space_wx[SPCWX_SSN].next_update : &next_update[ssn_pp];

    if (myNow() < *next_p)
        return (false);

    StackMalloc x_ssn((SSN_NV)*sizeof(float));
    StackMalloc x_x((SSN_NV)*sizeof(float));
    float *ssn = (float*)x_ssn.getMem();
    float *x = (float*)x_x.getMem();

    bool ok = retrieveSunSpots (x, ssn);
    if (ok) {

        // schedule next
        *next_p = myNow() + SSN_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry(PLOT_CH_SSN);
    }

    // true, albeit may be SPW_ERR
    return (ok);
}

/* retrieve latest and predicted solar flux indices.
 * return whether transaction was ok (even if data was not)
 */
bool retrievSolarFlux (float x[SFLUX_NV], float sflux[SFLUX_NV])
{
    StackMalloc line_mem(120);
    char *line = (char *) line_mem.getMem();
    WiFiClient sf_client;
    bool ok = false;

    // mark value as bad until proven otherwise
    space_wx[SPCWX_FLUX].value = SPW_ERR;

    Serial.println (sf_page);
    resetWatchdog();
    if (wifiOk() && sf_client.connect(backend_host, backend_port)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCPGET (sf_client, backend_host, sf_page);

        // skip response header
        if (!httpSkipHeader (sf_client)) {
            Serial.print (F("SFlux: header fail\n"));
            goto out;
        }

        // transaction successful
        ok = true;

        // read lines into flux array and build corresponding time value
        int8_t sflux_i;
        for (sflux_i = 0; sflux_i < SFLUX_NV && getTCPLine(sf_client, line, line_mem.getSize(), NULL);
                                                                        sflux_i++) {
            sflux[sflux_i] = atof(line);
            x[sflux_i] = (sflux_i - (SFLUX_NV-9-1))/3.0F;   // 3x(30 days history + 3 days predictions)
        }

        // ok if found all
        updateClocks(false);
        resetWatchdog();
        if (sflux_i == SFLUX_NV) {

            // capture current value (not predictions)
            space_wx[SPCWX_FLUX].value = sflux[SFLUX_NV-10];

        } else {

            Serial.printf (_FX("SFlux: data short: %d / %d\n"), sflux_i, SFLUX_NV);
        }
    }

out:

    // clean up
    sf_client.stop();
    resetWatchdog();
    return (ok);
}

/* update SPCWX_FLUX if not recently done so by its pane.
 * return whether a new value is ready, even if SPW_ERR
 */
static bool checkSolarFlux (void)
{
    // use our own delay unless being shown in a pane
    PlotPane sflux_pp = findPaneForChoice (PLOT_CH_FLUX);
    time_t *next_p = sflux_pp == PANE_NONE ? &space_wx[SPCWX_FLUX].next_update : &next_update[sflux_pp];

    if (myNow() < *next_p)
        return (false);

    StackMalloc x_mem(SFLUX_NV*sizeof(float));
    StackMalloc sflux_mem(SFLUX_NV*sizeof(float));
    float *x = (float *) x_mem.getMem();
    float *sflux = (float *) sflux_mem.getMem();

    bool ok = retrievSolarFlux (x, sflux);
    if (ok) {

        // schedule next
        *next_p = myNow() + SFLUX_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry(PLOT_CH_FLUX);
    }

    // true, albeit may be SPW_ERR
    return (true);
}


/* retrieve latest DRAP frequencies and current space weather value.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveDRAP (float x[DRAPDATA_NPTS], float y[DRAPDATA_NPTS])
{
    #define _DRAPDATA_MAXMI     (DRAPDATA_NPTS/10)                      // max allowed missing intervals
    #define _DRAP_MINGOODI      (DRAPDATA_NPTS-3600/DRAPDATA_INTERVAL)  // min index with good data

    char line[100];                                                     // text line
    WiFiClient drap_client;                                             // wifi client connection
    bool ok = false;                                                    // set iff all ok

    // want to find any holes in data so init x values to all 0
    memset (x, 0, DRAPDATA_NPTS*sizeof(float));

    // want max in each interval so init y values to all 0
    memset (y, 0, DRAPDATA_NPTS*sizeof(float));

    // mark data as bad until proven otherwise
    space_wx[SPCWX_DRAP].value = SPW_ERR;

    Serial.println (drap_page);
    resetWatchdog();
    if (wifiOk() && drap_client.connect(backend_host, backend_port)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCPGET (drap_client, backend_host, drap_page);

        // skip response header
        if (!httpSkipHeader (drap_client)) {
            Serial.print (F("DRAP: header short\n"));
            goto out;
        }

        // transaction itself is successful
        ok = true;

        // init state
        time_t t_now = myNow();

        // read lines, oldest first
        int n_lines = 0;
        while (getTCPLine (drap_client, line, sizeof(line), NULL)) {
            n_lines++;

            // crack
            long utime;
            float min, max, mean;
            if (sscanf (line, _FX("%ld : %f %f %f"), &utime, &min, &max, &mean) != 4) {
                Serial.printf (_FX("DRAP: garbled: %s\n"), line);
                goto out;
            }
            // Serial.printf (_FX("DRAP: %ld %g %g %g\n", utime, min, max, mean);

            // find age for this datum, skip if crazy new or too old
            int age = t_now - utime;
            int xi = DRAPDATA_NPTS*(DRAPDATA_PERIOD - age)/DRAPDATA_PERIOD;
            if (xi < 0 || xi >= DRAPDATA_NPTS) {
                // Serial.printf (_FX("DRAP: skipping age %g hrs\n"), age/3600.0F);
                continue;
            }
            x[xi] = age/(-3600.0F);                             // seconds to hours ago

            // set in array if larger
            if (max > y[xi]) {
                // if (y[xi] > 0)
                    // Serial.printf (_FX("DRAP: saw xi %d utime %ld age %d again\n"), xi, utime, age);
                y[xi] = max;
            }

            // Serial.printf (_FX("DRAP: %3d %6d: %g %g\n"), xi, age, x[xi], y[xi]);
        }
        Serial.printf (_FX("DRAP: read %d lines\n"), n_lines);

        // look alive
        updateClocks(false);
        resetWatchdog();

        // check for missing data
        int n_missing = 0;
        int maxi_good = 0;
        for (int i = 0; i < DRAPDATA_NPTS; i++) {
            if (x[i] == 0) {
                x[i] = (DRAPDATA_PERIOD - i*DRAPDATA_PERIOD/DRAPDATA_NPTS)/-3600.0F;
                if (i > 0)
                    y[i] = y[i-1];                      // fill with previous
                Serial.printf (_FX("DRAP: filling missing interval %d at age %g hrs to %g\n"), i, x[i], y[i]);
                n_missing++;
            } else {
                maxi_good = i;
            }
        }

        // check for too much missing or newest too old
        if (n_missing > _DRAPDATA_MAXMI) {
            Serial.print (F("DRAP: data too sparse\n"));
            goto out;
        }
        if (maxi_good < _DRAP_MINGOODI) {
            Serial.print (F("DRAP: data too old\n"));
            goto out;
        }

        // capture current value
        space_wx[SPCWX_DRAP].value = y[DRAPDATA_NPTS-1];

    } else {

        Serial.print (F("DRAP: connection failed\n"));
    }

out:

    // clean up
    drap_client.stop();
    resetWatchdog();
    return (ok);
}

/* update SPCWX_DRAP if not recently done so by its pane.
 * return whether a new value is ready.
 */
bool checkDRAP ()
{
    // use our own delay unless being shown in a pane
    PlotPane drap_pp = findPaneForChoice (PLOT_CH_DRAP);
    time_t *next_p = drap_pp == PANE_NONE ? &space_wx[SPCWX_DRAP].next_update : &next_update[drap_pp];

    if (myNow() < *next_p)
        return (false);

    StackMalloc x_mem(DRAPDATA_NPTS*sizeof(float));
    StackMalloc y_mem(DRAPDATA_NPTS*sizeof(float));
    float *x = (float*)x_mem.getMem();
    float *y = (float*)y_mem.getMem();

    bool ok = retrieveDRAP (x, y);
    if (ok) {

        // schedule next
        *next_p = myNow() + DRAPPLOT_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry(PLOT_CH_DRAP);
    }

    // true, albeit may be SPW_ERR
    return (true);
}

/* retrieve latest and predicted kp indices and space_wx info.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveKp (float kpx[KP_NV], float kp[KP_NV])
{
    WiFiClient kp_client;                               // wifi client connection
    int kp_i = 0;                                       // next kp index to use
    char line[100];                                     // text line
    bool ok = false;                                    // set if no network errors

    // mark value as bad until proven otherwise
    space_wx[SPCWX_KP].value = SPW_ERR;

    Serial.println(kp_page);
    resetWatchdog();
    if (wifiOk() && kp_client.connect(backend_host, backend_port)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCPGET (kp_client, backend_host, kp_page);

        // skip response header
        if (!httpSkipHeader (kp_client)) {
            Serial.print (F("Kp: header short\n"));
            goto out;
        }

        // transaction successful even if data is not
        ok = true;

        // read lines into kp array and build x
        const int now_i = KP_NHD*KP_VPD-1;              // last historic is now
        for (kp_i = 0; kp_i < KP_NV && getTCPLine (kp_client, line, sizeof(line), NULL); kp_i++) {
            kp[kp_i] = atof(line);
            kpx[kp_i] = (kp_i-now_i)/(float)KP_VPD;
            // Serial.printf ("%2d%c: kp[%5.3f] = %g from \"%s\"\n", kp_i, kp_i == now_i ? '*' : ' ', kpx[kp_i], kp[kp_i], line);
        }

        // record sw
        if (kp_i == KP_NV) {

            // save current (not last!) value
            space_wx[SPCWX_KP].value = kp[now_i];

        } else {

            Serial.printf (_FX("Kp: data short: %d of %d\n"), kp_i, KP_NV);
        }
    }

out:

    // clean up
    kp_client.stop();
    resetWatchdog();
    return (ok);
}

/* update SPCWX_KP if not recently done so by its pane.
 * return whether a new value is ready, even if SPW_ERR
 */
static bool checkKp (void)
{
    // use our own delay unless being shown in a pane
    PlotPane kp_pp = findPaneForChoice (PLOT_CH_KP);
    time_t *next_p = kp_pp == PANE_NONE ? &space_wx[SPCWX_KP].next_update : &next_update[kp_pp];

    if (myNow() < *next_p)
        return (false);

    StackMalloc kpx_mem(KP_NV*sizeof(float));
    StackMalloc kp_mem(KP_NV*sizeof(float));
    float *kpx = (float*)kpx_mem.getMem();              // days ago
    float *kp = (float*)kp_mem.getMem();                // kp collection

    bool ok = retrieveKp (kpx, kp);
    if (ok) {

        // schedule next
        *next_p = myNow() + KP_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry(PLOT_CH_KP);
    }

    // true, albeit may be SPW_ERR
    return (true);
}

/* retrieve latest xray indices.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveXRay (float lxray[XRAY_NV], float sxray[XRAY_NV], float x[XRAY_NV])
{
    uint8_t xray_i;                                     // next index to use
    WiFiClient xray_client;
    char line[100];
    uint16_t ll;
    bool ok = false;

    // mark value as bad until proven otherwise
    space_wx[SPCWX_XRAY].value = SPW_ERR;

    Serial.println(xray_page);
    resetWatchdog();
    if (wifiOk() && xray_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCPGET (xray_client, backend_host, xray_page);

        // soak up remaining header
        if (!httpSkipHeader (xray_client)) {
            Serial.print (F("XRay: header short\n"));
            goto out;
        }

        // transaction successful
        ok = true;

        // collect content lines and extract both wavelength intensities
        xray_i = 0;
        float raw_lxray = 0;
        while (xray_i < XRAY_NV && getTCPLine (xray_client, line, sizeof(line), &ll)) {
            // Serial.println(line);

            if (line[0] == '2' && ll >= 56) {

                // short
                float s = atof(line+35);
                if (s <= 0)                             // missing values are set to -1.00e+05, also guard 0
                    s = 1e-9;
                sxray[xray_i] = log10f(s);

                // long
                float l = atof(line+47);
                if (l <= 0)                             // missing values are set to -1.00e+05, also guard 0
                    l = 1e-9;
                lxray[xray_i] = log10f(l);
                raw_lxray = l;                          // last one will be current

                // time in hours back from 0
                x[xray_i] = (xray_i-XRAY_NV)/6.0;       // 6 entries per hour

                // good
                xray_i++;
            }
        }

        // capture iff we found all
        if (xray_i == XRAY_NV) {

            // capture
            space_wx[SPCWX_XRAY].value = raw_lxray;

        } else {

            Serial.printf (_FX("XRay: data short %d of %d\n"), xray_i, XRAY_NV);
        }
    }

out:

    // clean up
    xray_client.stop();
    resetWatchdog();
    return (ok);
}

/* update SPCWX_XRAY if not recently done so by its pane.
 * return whether a new value is ready, even if SPW_ERR
 */
static bool checkXRay (void)
{
    // use our own delay unless being shown in a pane
    PlotPane xray_pp = findPaneForChoice (PLOT_CH_XRAY);
    time_t *next_p = xray_pp == PANE_NONE ? &space_wx[SPCWX_XRAY].next_update : &next_update[xray_pp];

    if (myNow() < *next_p)
        return (false);

    StackMalloc lxray_mem(XRAY_NV*sizeof(float));
    StackMalloc sxray_mem(XRAY_NV*sizeof(float));
    StackMalloc x_mem(XRAY_NV*sizeof(float));
    float *lxray = (float *) lxray_mem.getMem();        // long wavelength values
    float *sxray = (float *) sxray_mem.getMem();        // short wavelength values
    float *x = (float *) x_mem.getMem();                // x coords of plot

    bool ok = retrieveXRay (lxray, sxray, x);
    if (ok) {

        // schedule next
        *next_p = myNow() + XRAY_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry(PLOT_CH_XRAY);
    }

    // true, albeit may be SPW_ERR
    return (true);
}

/* retrieve latest bzbt indices.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveBzBt (float bzbt_hrsold[BZBT_NV], float bz[BZBT_NV], float bt[BZBT_NV])
{
    int bzbt_i;                                     // next index to use
    WiFiClient bzbt_client;
    char line[100];
    bool ok = false;
    time_t t0 = myNow();

    // mark value as bad until proven otherwise
    space_wx[SPCWX_BZ].value = SPW_ERR;

    Serial.println(bzbt_page);
    resetWatchdog();
    if (wifiOk() && bzbt_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCPGET (bzbt_client, backend_host, bzbt_page);

        // skip over remaining header
        if (!httpSkipHeader (bzbt_client)) {
            Serial.print (F("BZBT: header short\n"));
            goto out;
        }

        // transaction successful
        ok = true;

        // collect content lines and extract both magnetic values, oldest first (newest last :-)
        // # UNIX        Bx     By     Bz     Bt
        // 1684087500    1.0   -2.7   -3.2    4.3
        bzbt_i = 0;
        while (bzbt_i < BZBT_NV && getTCPLine (bzbt_client, line, sizeof(line), NULL)) {

            // crack
            // Serial.printf("BZBT: %d %s\n", bzbt_i, line);
            long unix;
            float this_bz, this_bt;
            if (sscanf (line, _FX("%ld %*f %*f %f %f"), &unix, &this_bz, &this_bt) != 3) {
                // Serial.printf ("BZBT: rejecting %s\n", line);
                continue;
            }

            // store at bzbt_i
            bz[bzbt_i] = this_bz;
            bt[bzbt_i] = this_bt;

            // time in hours back from now but clamp at 0 in case we are slightly late
            bzbt_hrsold[bzbt_i] = unix < t0 ? (unix - t0)/3600.0 : 0;

            // good
            bzbt_i++;
        }

        // proceed iff we found all and current
        if (bzbt_i == BZBT_NV && bzbt_hrsold[BZBT_NV-1] > -0.25F) {

            // capture latest
            space_wx[SPCWX_BZ].value = bz[BZBT_NV-1];

        } else {

            if (bzbt_i < BZBT_NV)
                Serial.printf (_FX("BZBT: data short %d of %d\n"), bzbt_i, BZBT_NV);
            else
                Serial.printf (_FX("BZBT: data %g hrs old\n"), -bzbt_hrsold[BZBT_NV-1]);
        }
    }

out:

    // clean up
    bzbt_client.stop();
    resetWatchdog();
    return (ok);
}

/* update SPCWX_BZ if not recently done so by its pane.
 * return whether a new value is ready, even if SPW_ERR
 */
static bool checkBzBt(void)
{
    // use our own delay unless being shown in a pane
    PlotPane bzbt_pp = findPaneForChoice (PLOT_CH_BZBT);
    time_t *next_p = bzbt_pp == PANE_NONE ? &space_wx[SPCWX_BZ].next_update : &next_update[bzbt_pp];

    if (myNow() < *next_p)
        return (false);

    StackMalloc old_mem(BZBT_NV*sizeof(float));
    StackMalloc bz_mem(BZBT_NV*sizeof(float));
    StackMalloc bt_mem(BZBT_NV*sizeof(float));
    float *old = (float *) old_mem.getMem();
    float *bz = (float *) bz_mem.getMem();
    float *bt = (float *) bt_mem.getMem();

    bool ok = retrieveBzBt (old, bz, bt);
    if (ok) {

        // schedule next
        *next_p = myNow() + BZBT_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry(PLOT_CH_BZBT);
    }

    // true, albeit may be SPW_ERR
    return (true);
}

/* retrieve latest and predicted solar wind age and indices, return count.
 * x is hours ago, y is wind index, oldest first.
 */
int retrieveSolarWind(float x[SWIND_MAXN], float y[SWIND_MAXN])
{
    WiFiClient swind_client;
    char line[80];

    // mark value as bad until proven otherwise
    space_wx[SPCWX_SOLWIND].value = SPW_ERR;
    int nsw = 0;

    Serial.println (swind_page);
    resetWatchdog();
    if (wifiOk() && swind_client.connect(backend_host, backend_port)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCPGET (swind_client, backend_host, swind_page);

        // skip response header
        if (!httpSkipHeader (swind_client)) {
            Serial.println (F("SolWind: header short"));
            goto out;
        }

        // read lines into wind array and build corresponding x/y values
        time_t t0 = myNow();
        time_t start_t = t0 - SWIND_PER;
        time_t prev_unixs = 0;
        float max_y = 0;
        for (nsw = 0; nsw < SWIND_MAXN && getTCPLine (swind_client, line, sizeof(line), NULL); ) {
            // Serial.printf (_FX("SolWind: %3d: %s\n"), nsw, line);
            long unixs;         // unix seconds
            float density;      // /cm^2
            float speed;        // km/s
            if (sscanf (line, _FX("%ld %f %f"), &unixs, &density, &speed) != 3) {
                Serial.println (F("SolWind: data garbled"));
                goto out;
            }

            // want y axis to be 10^12 /s /m^2
            float this_y = density * speed * 1e-3;

            // capture largest value in this period
            if (this_y > max_y)
                max_y = this_y;

            // skip until find within period and new interval or always included last
            if ((unixs < start_t || unixs - prev_unixs < SWIND_DT) && nsw != SWIND_MAXN-1)
                continue;
            prev_unixs = unixs;

            // want x axis to be hours back from now
            x[nsw] = (t0 - unixs)/(-3600.0F);
            y[nsw] = max_y;
            // Serial.printf (_FX("SolWind: %3d %5.2f %5.2f\n"), nsw, x[nsw], y[nsw]);

            // good one
            max_y = 0;
            nsw++;
        }

        // proceed iff found enough
        updateClocks(false);
        resetWatchdog();
        if (nsw >= SWIND_MINN) {

            // capture
            space_wx[SPCWX_SOLWIND].value = y[nsw-1];

        } else {
            Serial.println (F("SolWind:: data error"));
        }

    } else {
        Serial.println (F("SolWind: connection failed"));
    }

out:

    // clean up
    swind_client.stop();
    resetWatchdog();
    return (nsw);
}

/* update SPCWX_SOLWIND if not recently done so by its pane.
 * return whether a new value is ready, even if SPW_ERR
 */
static bool checkSolarWind (void)
{
    // use our own delay unless being shown in a pane
    PlotPane swind_pp = findPaneForChoice (PLOT_CH_SOLWIND);
    time_t *next_p = swind_pp == PANE_NONE ? &space_wx[SPCWX_SOLWIND].next_update : &next_update[swind_pp];

    if (myNow() < *next_p)
        return (false);

    StackMalloc x_mem(SWIND_MAXN*sizeof(float));  // hours ago
    StackMalloc y_mem(SWIND_MAXN*sizeof(float));  // wind
    float *x = (float *) x_mem.getMem();
    float *y = (float *) y_mem.getMem();

    int nsw = retrieveSolarWind (x, y);
    if (nsw >= SWIND_MINN) {

        // schedule next
        *next_p = myNow() + SWIND_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry(PLOT_CH_SOLWIND);
    }

    // true, albeit may be SPW_ERR
    return (nsw >= SWIND_MINN);
}

/* retrieve SPCWX_NOAASPW and noaa_sw.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveNOAASWx(void)
{
    // expecting 3 reply lines of the following form, anything else is an error message
    //  R  0 0 0 0
    //  S  0 0 0 0
    //  G  0 0 0 0

    // init err until known good
    noaa_sw.ok = false;
    space_wx[SPCWX_NOAASPW].value = SPW_ERR;

    // TCP client
    WiFiClient noaaswx_client;
    bool ok = false;

    // read scales
    Serial.println(noaaswx_page);
    resetWatchdog();
    char line[100];
    if (wifiOk() && noaaswx_client.connect(backend_host, backend_port)) {

        resetWatchdog();
        updateClocks(false);

        // fetch page
        httpHCPGET (noaaswx_client, backend_host, noaaswx_page);

        // skip header then read the data lines
        if (httpSkipHeader (noaaswx_client)) {

            // transaction successful
            ok = true;

            // find sum
            int noaasw_max = 0;

            for (int i = 0; i < N_NOAASW_C; i++) {

                // read next line
                if (!getTCPLine (noaaswx_client, line, sizeof(line), NULL)) {
                    Serial.println (F("NOAASW: missing data"));
                    goto out;
                }
                // Serial.printf (_FX("NOAA: %d %s\n"), i, line);

                // category is first char must match
                if (noaa_sw.cat[i] != line[0]) {
                    Serial.printf (_FX("NOAASW: invalid class: %s\n"), line);
                    goto out;
                }

                // then N_NOAASW_V ints
                char *lp = line+1;
                for (int j = 0; j < N_NOAASW_V; j++) {

                    // convert next int
                    char *endptr;
                    noaa_sw.val[i][j] = strtol (lp, &endptr, 10);
                    if (lp == endptr) {
                        Serial.printf (_FX("NOAASW: invalid line: %s\n"), line);
                        goto out;
                    }
                    lp = endptr;

                    // find max
                    if (noaa_sw.val[i][j] > noaasw_max)
                        noaasw_max = noaa_sw.val[i][j];
                }

            }

            // values ok
            noaa_sw.ok = true;
            space_wx[SPCWX_NOAASPW].value = noaasw_max;

        } else {
            Serial.println (F("NOAASW: header short"));
            goto out;
        }
    } else
        Serial.println (F("NOAASW: connection failed"));

out:

    // clean up
    noaaswx_client.stop();
    resetWatchdog();
    return (ok);
}

/* update noaa_sw if not recently done so by its pane.
 * return whether new data are ready, even if not ok.
 */
static bool checkNOAASWx (void)
{
    PlotPane noaasw_pp = findPaneForChoice (PLOT_CH_NOAASPW);
    time_t *next_p = noaasw_pp == PANE_NONE ? &noaa_sw.next_update : &next_update[noaasw_pp];

    if (myNow() < *next_p)
        return (false);

    if (retrieveNOAASWx()) {

        // schedule next
        *next_p = myNow() + NOAASPW_INTERVAL;

    } else {

        // schedule retry
        *next_p = nextWiFiRetry (PLOT_CH_NOAASPW);
    }

    // true, albeit may be !noaa_sp.ok
    return (true);
}

/* update all space_wx stats but no faster than their respective panes would do.
 * return whether any actually updated.
 */
bool checkSpaceWx()
{
    // check each
    bool sf = checkSolarFlux();
    bool kp = checkKp();
    bool xr = checkXRay();
    bool bz = checkBzBt();
    bool dr = checkDRAP();
    bool sw = checkSolarWind();
    bool ss = checkSunSpots();
    bool na = checkNOAASWx();

    // check whether any
    bool any_new = sf || kp || xr || bz || dr || sw || ss || na;

    // if so, redo ranking if desired
    if (any_new && autoSortSpaceWx())
        rankSpaceWx();

    return (any_new);
}
