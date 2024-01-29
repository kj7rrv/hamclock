/* look up current weather
 */


#include "HamClock.h"

static const char wx_base[] = "/wx.pl";


/* display the given location weather in NCDXF_b or err.
 */
static void drawNCDXFBoxWx (BRB_MODE m, const WXInfo &wi, bool ok)
{
    // init arrays for drawNCDXFStats() then replace values with real if ok
    uint16_t color = m == BRB_SHOW_DEWX ? DE_COLOR : DX_COLOR;
    char values[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN];
    uint16_t colors[NCDXF_B_NFIELDS];
    char titles[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN] = {
        "",                     // set later with DE/DX prefix
        "Humidity",
        "Wind Dir",
        "W Speed",
    };
    snprintf (titles[0], sizeof(titles[0]), _FX("%s Temp"), m == BRB_SHOW_DEWX ? "DE" : "DX");
    for (int i = 0; i < NCDXF_B_NFIELDS; i++) {
        strcpy (values[i], _FX("Err"));
        colors[i] = color;
    }

    if (ok) {
        float v = useMetricUnits() ? wi.temperature_c : CEN2FAH(wi.temperature_c);
        snprintf (values[0], sizeof(values[0]), _FX("%.1f"), v);
        snprintf (values[1], sizeof(values[1]), _FX("%.1f"), wi.humidity_percent);
        snprintf (values[2], sizeof(values[2]), _FX("%s"), wi.wind_dir_name);
        v = (useMetricUnits() ? 3.6 : 2.237) * wi.wind_speed_mps;                       // kph or mph
        snprintf (values[3], sizeof(values[3]), _FX("%.0f"), v);
    }

    // show it
    drawNCDXFStats (RA8875_BLACK, titles, values, colors);
}

/* look up current weather info for the given location.
 * if wip is filled ok return true, else return false with short reason in ynot[] if set
 */
bool getCurrentWX (const LatLong &ll, bool is_de, WXInfo *wip, char ynot[])
{
    // keep one of each de/dx cached
    #define CACHE_DT (10*60)
    static WXInfo wi_cached[2];
    static LatLong ll_cached[2];
    static time_t time_cached[2];
    if (memcmp (&ll_cached[is_de], &ll, sizeof(ll)) == 0 && myNow() - time_cached[is_de] < CACHE_DT) {
        *wip = wi_cached[is_de];
        Serial.printf (_FX("WX: used cached %s value\n"), is_de ? "DE" : "DX");
        return (true);
    }

    WiFiClient wx_client;
    char line[100];

    bool ok = false;

    resetWatchdog();

    // get
    if (wifiOk() && wx_client.connect(backend_host, backend_port)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        snprintf (line, sizeof(line), _FX("%s?is_de=%d&lat=%g&lng=%g"), wx_base, is_de, ll.lat_d, ll.lng_d);
        Serial.println (line);
        httpHCGET (wx_client, backend_host, line);

        // skip response header
        if (!httpSkipHeader (wx_client)) {
            strcpy (ynot, _FX("WX timeout"));
            goto out;
        }

        // init response 
        memset (wip, 0, sizeof(*wip));

        // crack response
        uint8_t n_found = 0;
        while (n_found < N_WXINFO_FIELDS && getTCPLine (wx_client, line, sizeof(line), NULL)) {
            // Serial.printf (_FX("WX: %s\n"), line);
            updateClocks(false);

            // check for error message in which case abandon further search
            if (sscanf (line, "error=%[^\n]", ynot) == 1)
                goto out;

            // find start of data value after =
            char *vstart = strchr (line, '=');
            if (!vstart)
                continue;
            *vstart++ = '\0';   // eos for name and move to value

            // check for content line
            if (strcmp (line, _FX("city")) == 0) {
                strncpy (wip->city, vstart, sizeof(wip->city)-1);
                n_found++;
            } else if (strcmp (line, _FX("temperature_c")) == 0) {
                wip->temperature_c = atof (vstart);
                n_found++;
            } else if (strcmp (line, _FX("pressure_hPa")) == 0) {
                wip->pressure_hPa = atof (vstart);
                n_found++;
            } else if (strcmp (line, _FX("pressure_chg")) == 0) {
                wip->pressure_chg = atof (vstart);
                n_found++;
            } else if (strcmp (line, _FX("humidity_percent")) == 0) {
                wip->humidity_percent = atof (vstart);
                n_found++;
            } else if (strcmp (line, _FX("wind_speed_mps")) == 0) {
                wip->wind_speed_mps = atof (vstart);
                n_found++;
            } else if (strcmp (line, _FX("wind_dir_name")) == 0) {
                strncpy (wip->wind_dir_name, vstart, sizeof(wip->wind_dir_name)-1);
                n_found++;
            } else if (strcmp (line, _FX("clouds")) == 0) {
                strncpy (wip->clouds, vstart, sizeof(wip->clouds)-1);
                n_found++;
            } else if (strcmp (line, _FX("conditions")) == 0) {
                strncpy (wip->conditions, vstart, sizeof(wip->conditions)-1);
                n_found++;
            } else if (strcmp (line, _FX("attribution")) == 0) {
                strncpy (wip->attribution, vstart, sizeof(wip->attribution)-1);
                n_found++;
            }

            // Serial.printf (_FX("WX %d: %s\n"), n_found, line);
        }

        if (n_found < N_WXINFO_FIELDS) {
            strcpy (ynot, _FX("Missing WX data"));
            goto out;
        }

        // ok!
        ok = true;

        // keep for possible reuse
        memcpy (&wi_cached[is_de], wip, sizeof(WXInfo));
        ll_cached[is_de] = ll;
        time_cached[is_de] = myNow();

    } else {

        strcpy (ynot, _FX("WX connection failed"));

    }



    // clean up
out:
    wx_client.stop();
    resetWatchdog();
    return (ok);
}

/* display current DE weather in the given box and in NCDXF_b if up.
 * this is used by updateWiFi() for persistent display, use showDEWX() for transient display
 */
bool updateDEWX (const SBox &box)
{
    char ynot[32];
    WXInfo wi;

    bool ok = getCurrentWX (de_ll, true, &wi, ynot);
    if (ok)
        plotWX (box, DE_COLOR, wi);
    else
        plotMessage (box, DE_COLOR, ynot);

    if (brb_mode == BRB_SHOW_DEWX)
        drawNCDXFBoxWx (BRB_SHOW_DEWX, wi, ok);

    return (ok);
}

/* display current DX weather in the given box and in NCDXF_b if up.
 * this is used by updateWiFi() for persistent display, use showDXWX() for transient display
 */
bool updateDXWX (const SBox &box)
{
    char ynot[32];
    WXInfo wi;

    bool ok = getCurrentWX (dx_ll, false, &wi, ynot);
    if (ok)
        plotWX (box, DX_COLOR, wi);
    else
        plotMessage (box, DX_COLOR, ynot);

    if (brb_mode == BRB_SHOW_DXWX)
        drawNCDXFBoxWx (BRB_SHOW_DXWX, wi, ok);

    return (ok);
}

/* display weather for the given mode in NCDXF_b.
 * return whether all ok.
 */
bool drawNCDXFWx (BRB_MODE m)
{
    // get weather
    char ynot[32];
    WXInfo wi;
    bool ok = false;
    if (m == BRB_SHOW_DEWX)
        ok = getCurrentWX (de_ll, true, &wi, ynot);
    else if (m == BRB_SHOW_DXWX)
        ok = getCurrentWX (dx_ll, false, &wi, ynot);
    else
        fatalError (_FX("Bogus drawNCDXFWx mode: %d"), m);
    if (!ok)
        Serial.printf (_FX("WX: %s\n"), ynot);

    // show it
    drawNCDXFBoxWx (m, wi, ok);

    // done
    return (ok);
}





#if defined(_IS_UNIX)

/* world weather info -- UNIX only
 */


static const char ww_page[] = "/worldwx/wx.txt";        // URL for world weather table


/* convert wind direction in degs to name, return whether in range.
 */
static bool windDeg2Name (float deg, char dirname[4])
{
    const char *name;

    if (deg < 0)          name = _FX("?");
    else if (deg < 22.5)  name = _FX("N");
    else if (deg < 67.5)  name = _FX("NE");
    else if (deg < 112.5) name = _FX("E");
    else if (deg < 157.5) name = _FX("SE");
    else if (deg < 202.5) name = _FX("S");
    else if (deg < 247.5) name = _FX("SW");
    else if (deg < 292.5) name = _FX("W");
    else if (deg < 337.5) name = _FX("NW");
    else if (deg <= 360)  name = _FX("N");
    else                  name = _FX("?");

    strcpy (dirname, name);

    return (dirname[0] != '?');
}

/* wwtable is a 2d table n_wwcols x n_wwrows.
 *   width:  columns are latitude [-90,90] in steps of 180/(n_wwcols-1).
 *   height: rows are longitude [-180..180) in steps of 360/n_wwrows.
 */
static WXInfo *wwtable;                                 // malloced array of info, latitude-major order
static int n_wwrows, n_wwcols;                          // wwtable dimensions

/* find wx conditions for the given location, if pssoible.
 * return whether wi has been filled
 */
bool getWorldWx (const LatLong &ll, WXInfo &wi)
{
    // check whether table is ready and reasonable
    if (!wwtable || n_wwcols < 2 || n_wwrows < 1)
        return (false);

    // offset to match wx map wind vane
    float lat_os = -180.0F/(n_wwcols-1)/2;
    float lng_os = 0; // ?? -360.0F/n_wwrows/2;

    // closest indices
    int row_i = roundf (n_wwrows*(ll.lng_d+lng_os+180)/360);
    if (row_i < 0 || row_i >= n_wwrows) {
        // printf ("WWX: bogus query: n_wwrows %d lng_d %g\n", n_wwrows, ll.lng_d);
        return (false);
    }
    int col_i = roundf (n_wwcols*(ll.lat_d+lat_os+90)/180);
    if (col_i < 0 || col_i >= n_wwcols) {
        // printf ("WWX: bogus query: n_wwcols %d lat_d %g\n", n_wwcols, ll.lat_d);
        return (false);
    }

    // return.
    wi = wwtable[row_i*n_wwcols + col_i];
    return (true);
}

/* collect world wx data into wwtable
 */
void fetchWorldWx(void)
{
    WiFiClient ww_client;
    bool ok = false;

    // reset table
    free (wwtable);
    wwtable = NULL;
    n_wwrows = n_wwcols = 0;

    // get
    if (wifiOk() && ww_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (ww_client, backend_host, ww_page);

        // prep for scanning (ahead of skipping header to avoid stupid g++ goto errors)
        int line_n = 0;                         // line number
        int n_wwtable = 0;                      // entries defined found so far
        int n_wwmalloc = 0;                     // malloced room so far
        int n_lngcols = 0;                      // build up n cols of constant lng so far this block
        float del_lat = 0, del_lng = 0;         // check constant step sizes
        float prev_lat = 0, prev_lng = 0;       // for checking step sizes

        // skip response header
        if (!httpSkipHeader (ww_client)) {
            Serial.printf ("WWX: header timeout");
            goto out;
        }

        /* read file and build table. confirm regular spacing each dimension.
         * file is one line per datum, increasing lat with same lng, then lng steps at each blank line.
         * file contains lng 180 for plotting but we don't use it.
         */
        char line[100];
        while (getTCPLine (ww_client, line, sizeof(line), NULL)) {

            // another line
            line_n++;

            // skip comment lines
            if (line[0] == '#')
                continue;

            // crack line:   lat     lng  temp,C     %hum    mps     dir    mmHg Wx
            float lat, lng, windir;
            WXInfo wx;
            memset (&wx, 0, sizeof(wx));
            int ns = sscanf (line, "%g %g %g %g %g %g %g %31s", &lat, &lng, &wx.temperature_c,
                        &wx.humidity_percent, &wx.wind_speed_mps, &windir, &wx.pressure_hPa, wx.conditions);

            // skip lng 180
            if (lng == 180)
                break;

            // add and check
            if (ns >= 7) {      // ok if conditions are blank

                // confirm regular spacing
                if (n_lngcols > 0 && lng != prev_lng) {
                    Serial.printf ("WWX: irregular lng: %d x %d  lng %g != %g\n",
                                n_wwrows, n_lngcols, lng, prev_lng);
                    goto out;
                }
                if (n_lngcols > 1 && lat != prev_lat + del_lat) {
                    Serial.printf ("WWX: irregular lat: %d x %d    lat %g != %g + %g\n",
                                n_wwrows, n_lngcols,  lat, prev_lat, del_lat);
                    goto out;
                }

                // convert wind direction to name
                if (!windDeg2Name (windir, wx.wind_dir_name)) {
                    Serial.printf ("WWX: bogus wind direction: %g\n", windir);
                    goto out;
                }

                // add to wwtable
                if (n_wwtable + 1 > n_wwmalloc)
                    wwtable = (WXInfo *) realloc (wwtable, (n_wwmalloc += 100) * sizeof(WXInfo));
                memcpy (&wwtable[n_wwtable++], &wx, sizeof(WXInfo));

                // update walk
                if (n_lngcols == 0)
                    del_lng = lng - prev_lng;
                del_lat = lat - prev_lat;
                prev_lat = lat;
                prev_lng = lng;
                n_lngcols++;

            } else if (ns <= 0) {

                // blank line separates blocks of constant longitude

                // check consistency so far
                if (n_wwrows == 0) {
                    // we know n cols after completing the first lng block, all remaining must equal this 
                    n_wwcols = n_lngcols;
                } else if (n_lngcols != n_wwcols) {
                    Serial.printf ("WWX: inconsistent columns %d != %d after %d rows\n",
                                                n_lngcols, n_wwcols, n_wwrows);
                    goto out;
                }

                // one more wwtable row
                n_wwrows++;

                // reset block stats
                n_lngcols = 0;

            } else {

                Serial.printf ("WWX: bogus line %d: %s\n", line_n, line);
                goto out;
            }
        }

        // final check
        if (n_wwrows != 360/del_lng || n_wwcols != 1 + 180/del_lat) {
            Serial.printf ("WWX: incomplete table: rows %d != 360/%g   cols %d != 1 + 180/%g\n",
                                        n_wwrows, del_lng,  n_wwcols, del_lat);
            goto out;
        }

        // yah!
        ok = true;
        Serial.printf ("WWX: table %d lat x %d lng\n", n_wwcols, n_wwrows);

    out:

        if (!ok) {
            // reset table
            free (wwtable);
            wwtable = NULL;
            n_wwrows = n_wwcols = 0;
        }

        ww_client.stop();
    }
}



#else // !_IS_UNIX

/* dummy that always returns false on ESP systems
 */
bool getWorldWx (const LatLong &ll, WXInfo &wi)
{
    // lint
    (void)ll;
    (void)wi;

    return (false);
}

/* dummy that does notthing on ESP
 */
void fetchWorldWx(void)
{
}

#endif // _IS_UNIX
