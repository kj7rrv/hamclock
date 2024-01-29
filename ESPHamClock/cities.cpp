/* manage list of cities.
 */

#include "HamClock.h"


#if defined(_SUPPORT_CITIES)


// name of server file containing cities
static const char cities_fn[] PROGMEM = "/cities2.txt"; // changed in 2.81

// malloced sorted kdtree
static KD3Node *city_root;

// pixel width of longest city
static int max_city_len;


/* query for list of cities, create kdtree.
 * harmless if called more than once.
 * N.B. UNIX only.
 */
void readCities()
{
        // ignore if already done
        if (city_root)
            return;

        // connection
        WiFiClient cities_client;

        Serial.println (cities_fn);
        resetWatchdog();
        if (wifiOk() && cities_client.connect (backend_host, backend_port)) {

            // stay current
            updateClocks(false);
            resetWatchdog();

            // send query
            httpHCPGET (cities_client, backend_host, cities_fn);

            // skip http header
            if (!httpSkipHeader (cities_client)) {
                Serial.print (F("Cities: bad header\n"));
                goto out;
            }

            // read each city and build temp lists of location and name
            char **names = NULL;                // temp malloced list of persistent malloced names
            LatLong *lls = NULL;                // temp malloced list of locations
            int n_cities = 0;                   // number in use in each list
            int n_malloced = 0;                 // number malloced in each list
            char line[200];
            max_city_len = 0;
            while (getTCPLine (cities_client, line, sizeof(line), NULL)) {

                // crack
                char name[101];
                float lat, lng;
                if (sscanf (line, _FX("%f, %f, \"%100[^\"]\""), &lat, &lng, name) != 3)
                    continue;

                // grow lists if full
                if (n_cities + 1 > n_malloced) {
                    n_malloced += 100;
                    names = (char **) realloc (names, n_malloced * sizeof(char *));
                    lls = (LatLong *) realloc (lls, n_malloced * sizeof(LatLong));
                    if (!names || !lls)
                        fatalError (_FX("alloc cities: %d"), n_malloced);
                }

                // add to lists
                names[n_cities] = strdup (name);
                LatLong &new_ll = lls[n_cities];
                new_ll.lat_d = lat;
                new_ll.lng_d = lng;
                normalizeLL (new_ll);

                // capture longest name
                int name_l = strlen (name);
                if (name_l > max_city_len)
                    max_city_len = name_l;

                // good
                n_cities++;

            }
            Serial.printf (_FX("Cities: found %d\n"), n_cities);

            // build tree -- N.B. can not build as we read because realloc could move left/right pointers
            KD3Node *city_tree = (KD3Node *) calloc (n_cities, sizeof(KD3Node));
            if (!city_tree && n_cities > 0)
                fatalError (_FX("alloc cities tree: %d"), n_cities);
            for (int i = 0; i < n_cities; i++) {
                KD3Node *kp = &city_tree[i];
                ll2KD3Node (lls[i], kp);
                kp->data = (void*) names[i];
            }

            // finished with temporary lists -- names themselves live forever
            free (names);
            free (lls);

            // sort
            city_root = mkKD3NodeTree (city_tree, n_cities, 0);
        }

    out:
        cities_client.stop();

}

/* return name of city and location nearest the given ll, else NULL.
 * also report longest city length for drawing purposes.
 */
const char *getNearestCity (const LatLong &ll, LatLong &city_ll, int &max_cl)
{
        // ignore if not ready or failed
        if (!city_root)
            return (NULL);

        // search
        KD3Node seach_city;
        ll2KD3Node (ll, &seach_city);
        KD3Node *best_city = NULL;
        float best_dist = 0;
        int n_visited = 0;
        nearestKD3Node (city_root, &seach_city, 0, &best_city, &best_dist, &n_visited);
        // printf ("**** visted %d\n", n_visited);

        // report results if successful
        best_dist = nearestKD3Dist2Miles (best_dist);   // convert to miles
        if (best_dist < MAX_CSR_DIST) {
            max_cl = max_city_len;
            KD3Node2ll (*best_city, &city_ll);
            return ((char*)(best_city->data));
        } else {
            return (NULL);
        }

}

#else

// dummies

const char *getNearestCity (const LatLong &ll, LatLong &city_ll, int &max_cl) {
    (void) ll;
    (void) city_ll;
    (void) max_cl;
    return NULL;
}

void readCities() {}

#endif // _SUPPORT_CITIES
