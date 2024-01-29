/* Mollweide projection, pronounced "mole vee dah"
 * formulas at https://en.wikipedia.org/wiki/Mollweide_projection
 *
 * see below for unit test
 */

#include "HamClock.h"

/* convert ll to map_b screen coords at the given canonical scale factor.
 * avoid globe edge by at least the given number of canonical pixels.
 */
void ll2sMollweide (const LatLong &ll, SCoord &s, int edge, int scalesz)
{
    // find theta iteratively -- there is no closed form
    #define MOLL_MAXN 20                // max loop, even 10 should be plenty except very near poles
    #define MOLL_MAXE 0.001             // convergence angle, rads
    float slat = sinf(ll.lat);
    float dtheta, theta = ll.lat;
    int n = 0;
    do {
        float ctheta = cosf(theta);
        float theta2 = theta*2;
        dtheta = (theta2 + sinf(theta2) - M_PIF*slat)/(4*ctheta*ctheta);
        theta -= dtheta;
    } while (++n < MOLL_MAXN && fabsf(dtheta) > MOLL_MAXE);

    // compute x,y if theta converged, else just place exactly at pole
    float x_moll, y_moll;
    if (n < MOLL_MAXN) {
        // find Moll coords with Wikipedia formula R==1 so both are in range [-1,1] as if a circle
        float lng0 = fmodf (ll.lng - deg2rad(getCenterLng()) + 5*M_PIF, 2*M_PIF) - M_PIF; // [-pi,pi]
        x_moll = (1/M_PIF) * lng0 * cosf(theta);
        y_moll = sinf(theta);
    } else {
        // did not converge, presume at a pole
        x_moll = 0;
        y_moll = ll.lat_d > 0 ? 1 : -1;                         // + up
    }

    // handy half-sizes
    float hw = map_b.w/2.0F;
    float hh = map_b.h/2.0F;

    // find canonical pixels from map center
    float dx = hw * x_moll;
    float dy = hh * y_moll;

    // protect edge if care
    if (edge > 0) {
        float dx_edge = sqrtf(1-y_moll*y_moll)*hw;              // dx to edge at this y
        dx = CLAMPF (dx, -dx_edge+edge, dx_edge-edge);
        float dy_edge = sqrtf(1-x_moll*x_moll)*hh;              // dy to edge at this x
        dy = CLAMPF (dy, -dy_edge+edge, dy_edge-edge);
    }

    // convert to scaled coords
    s.x = roundf (scalesz * (map_b.x + hw + dx));               // + dx means right
    s.y = roundf (scalesz * (map_b.y + hh - dy));               // + dy means up
}

/* convert map_b screen coords to ll.
 * return whether coord really is over the globe.
 */
bool s2llMollweide (const SCoord &s, LatLong &ll)
{
    // handy half-sizes
    float hw = map_b.w/2.0F;
    float hh = map_b.h/2.0F;

    // convert to Moll coords with Wikipedia formula R==1 so both are in range [-1,1] as if a circle
    float x_moll = (s.x - (map_b.x + hw))/hw;                   // want + mol right
    float y_moll = ((map_b.y + hh) - s.y)/hh;                   // want + mol up

    // must fit within circle
    if (x_moll*x_moll + y_moll*y_moll >= 1)
        return (false);

    // nice closed form for this direction
    float theta = asinf(y_moll);
    ll.lat_d = rad2deg (asinf ((2*theta + sinf(2*theta))/M_PIF));
    ll.lng_d = getCenterLng() + rad2deg((M_PIF*x_moll)/cosf(theta));
    normalizeLL (ll);

    // ok
    return (true);
}

#if defined(_UNIT_TEST)

/* g++ -Wall -IArduinoLib -D_UNIT_TEST mollweide.cpp && ./a.out
 * no output unless coordinates don't match back.
 */

SBox map_b;

void fatalError (const char *fmt, ...)
{
    char msg[2000];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf (msg, sizeof(msg), fmt, ap);
    va_end(ap);

    printf ("Fatal: %s\n", msg);
    exit(1);
}

int16_t getCenterLng()
{
    return (-90);
}

void normalizeLL (LatLong &ll)
{
    ll.lat_d = CLAMPF(ll.lat_d,-90,90);                 // clamp lat
    ll.lat = deg2rad(ll.lat_d);

    ll.lng_d = fmodf(ll.lng_d+(2*360+180),360)-180;     // wrap lng
    ll.lng = deg2rad(ll.lng_d);
}


int main (int ac, char *av[]) 
{
    map_b.x = 140;
    map_b.y = 150;
    map_b.w = 660;
    map_b.h = 330;

    for (uint16_t y = map_b.y; y < map_b.y + map_b.h; y++) {
        for (uint16_t x = map_b.x; x < map_b.x + map_b.w; x++) {
            SCoord s = {x, y};
            LatLong ll;
            if (s2llMollweide (s, ll)) {
                SCoord s2;
                ll2sMollweide (ll, s2, 0, 1);
                int x_err = (int)s.x - (int)s2.x;
                int y_err = (int)s.y - (int)s2.y;
                if (abs(x_err) > 1 || abs(y_err) > 1)
                    printf ("y= %4d x= %4d y2= %4d x2= %4d dy= %4d dx= %4d\n", y, x, s2.y, s2.x, y_err, x_err);
            }
        }
    }
}

#endif
