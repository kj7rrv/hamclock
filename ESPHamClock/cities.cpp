/* manage list of cities.
 * sparse 2d table contains largest city in each region.
 */

#include "HamClock.h"

// linux only

#if defined(_IS_UNIX)

// one city entry
typedef struct {
    const char *name;           // malloced name
    float lat_d, lng_d;         // degs +N +E
} City;

// one row descriptor
typedef struct {
    City *cities;               // malloc array of City sorted by increasing lng
    int n_cities;               // number of Cities
} LatRow;

// rows
static int LAT_SIZ, LNG_SIZ;    // region size
static LatRow *lat_rows;        // malloced list of 180/LAT_SIZ rows

// name of server file containing cities
static const char cities_fn[] PROGMEM = "/cities.txt";

/* return 2D separation between two locations in degrees squared.
 */
static float separation2D (const LatLong &ll, const float lat_d, const float lng_d)
{
    float dlng = cosf(ll.lat) * (ll.lng_d - lng_d);
    float dlat = ll.lat_d - lat_d;
    return (dlng*dlng + dlat*dlat);
}

/* qsort-style function to compare a pair of pointers to City by lng
 */
static int cityQS (const void *p1, const void *p2)
{
    float lng1 = ((City*)p1)->lng_d;
    float lng2 = ((City*)p2)->lng_d;

    if (lng1 < lng2)
        return (-1);
    if (lng1 > lng2)
        return (1);
    return (0);
}

/* find closest City in the given row within row size, if any.
 * N.B. row assumed to have at least one entry and be sorted by increasing lng
 */
static City *closestCityInRow (const LatRow &lr, const LatLong &ll)
{
        // find closest longitude
        int a = 0, b = lr.n_cities - 1;
        while (b > a + 1) {
            int m = (a + b)/2;
            City &cm = lr.cities[m];
            if (ll.lng_d < cm.lng_d)
                b = m;
            else
                a = m;
        }
        int c_lng = (a != b && fabsf(lr.cities[a].lng_d - ll.lng_d) < fabsf(lr.cities[b].lng_d - ll.lng_d))
                        ? a : b;

        // check a few either side considering latitude
        float c_dist = 1e10;
        int c_i = c_lng;
        for (int d_i = -1; d_i <= 1; d_i++) {
            int c_di = c_lng + d_i;
            if (c_di >= 0 && c_di < lr.n_cities) {
                float m_dist = separation2D (ll, lr.cities[c_di].lat_d, lr.cities[c_di].lng_d); 
                if (m_dist < c_dist) {
                    c_dist = m_dist;
                    c_i = c_di;
                }
            }
        }

        return (c_dist < LAT_SIZ ? &lr.cities[c_i] : NULL);
}

/* query for list of cities, fill regions.
 * harmless if called more than once.
 * N.B. UNIX only.
 */
void readCities()
{

        // ignore if already done
        if (lat_rows)
            return;

        // connection
        WiFiClient cities_client;

        Serial.println (cities_fn);
        resetWatchdog();
        if (wifiOk() && cities_client.connect (svr_host, HTTPPORT)) {

            // stay current
            updateClocks(false);
            resetWatchdog();

            // send query
            httpHCPGET (cities_client, svr_host, cities_fn);

            // skip http header
            if (!httpSkipHeader (cities_client)) {
                Serial.print (F("Cities: bad header\n"));
                goto out;
            }

            // first line is binning sizes
            char line[100];
            if (!getTCPLine (cities_client, line, sizeof(line), NULL)) {
                Serial.print (F("Cities: no bin line\n"));
                goto out;
            }
            if (sscanf (line, "%d %d", &LAT_SIZ, &LNG_SIZ) != 2) {
                Serial.printf (_FX("Cities: bad bin line: %s\n"), line);
                goto out;
            }

            // create row array
            int n_rows = 180/LAT_SIZ;
            lat_rows = (LatRow *) calloc (n_rows, sizeof(LatRow));

            // read each city
            int n_cities = 0;
            while (getTCPLine (cities_client, line, sizeof(line), NULL)) {

                // crack info
                float rlat, rlng;
                if (sscanf (line, "%f, %f", &rlat, &rlng) != 2)
                    continue;
                int lat_row = (rlat+90)/LAT_SIZ;
                if (lat_row < 0 || lat_row >= n_rows)
                    continue;
                char *city_start = strchr (line, '"');
                if (!city_start)
                    continue;
                city_start += 1;
                char *city_end = strchr (city_start, '"');
                if (!city_end)
                    continue;
                *city_end = '\0';

                // append to appropriate latrow, sort later
                LatRow *lrp = &lat_rows[lat_row];
                lrp->cities = (City *) realloc (lrp->cities, (lrp->n_cities+1) * sizeof(City));
                City *cp = &lrp->cities[lrp->n_cities++];
                cp->name = strdup (city_start);
                cp->lat_d = rlat;
                cp->lng_d = rlng;

                // good
                n_cities++;

            }
            Serial.printf (_FX("Cities: found %d %d x %d\n"), n_cities, LAT_SIZ, LNG_SIZ);

            // sort each row by lng
            for (int i = 0; i < n_rows; i++)
                qsort (lat_rows[i].cities, lat_rows[i].n_cities, sizeof(City), cityQS);
        }

    out:
        cities_client.stop();

}

/* return name of city and location nearest the given ll, else NULL.
 */
const char *getNearestCity (const LatLong &ll, LatLong &city_ll)
{

        // ignore if not ready
        if (!lat_rows)
            return (NULL);

        // decide row based on latitude bin
        int lat_row = (ll.lat_d+90)/LAT_SIZ;
        if (lat_row < 0 || lat_row >= 180/LAT_SIZ || lat_rows[lat_row].n_cities < 1)
            return (NULL);

        // check this row for closest city row size, if any
        City *best_cp = closestCityInRow (lat_rows[lat_row], ll);

        // report results if successful
        if (best_cp) {
            city_ll.lat_d = best_cp->lat_d;
            city_ll.lng_d = best_cp->lng_d;
            normalizeLL (city_ll);
            return (best_cp->name);
        } else {
            return (NULL);
        }

}

#else

// dummies

const char *getNearestCity (const LatLong &ll, LatLong &city_ll) {
    (void) ll;
    (void) city_ll;
    return NULL;
}

void readCities() {}

#endif // _IS_UNIX
