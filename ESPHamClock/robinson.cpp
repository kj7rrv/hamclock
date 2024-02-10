/* Robinson projection.
 *
 * tried Moyhu but could not get it to work so made my own fits but still using his method of
 *   constraining the end points.
 * https://moyhu.blogspot.com/2019/08/mapping-projections-for-climate-data.html
 * https://en.wikipedia.org/wiki/Robinson_projection
 *
 * see below for unit test
 */

#include "HamClock.h"


#define A_0 (0.1122522F)
#define A_1 (0.1890084F)
#define A_2 (-0.1721879F)
#define A_3 (0.2325704F)

#define B_0 (0.1782038F)
#define B_1 (-0.6687222F)
#define B_2 (1.121952F)
#define B_3 (-0.8132924F)

#define C_0 (-0.08852257F)
#define C_1 (-0.3567884F)
#define C_2 (0.8330805F)
#define C_3 (-1.002338F)

#define D_0 (0.4678F)



/* given lat [-90,90] return plot Y position [-1,1]
 */
static float RobLat2Y (const float lat_d)
{
    const float y = fabsf(lat_d) / 90;
    const float yy = y*y;
    float Y = y + y*(1-yy) * (((A_3*yy + A_2)*yy + A_1)*yy + A_0);
    if (lat_d < 0)
        Y = -Y;
    return (Y);
}

/* given lat [-90,90] return lng scale facter G [0.5322,1]
 * useful elsewhere for finding globe width.
 */
float RobLat2G (const float lat_d)
{
    const float y = fabsf(lat_d) / 90;
    const float yy = y*y;
    return (1 - D_0*yy + yy*(1-yy) * (((B_3*yy + B_2)*yy + B_1)*yy + B_0));
}

/* given plot Y position [-1,1] return lat [-90,90]
 */
static float RobY2Lat (const float Y)
{
    const float YY = Y*Y;
    const float Yabs = fabsf(Y);
    float y = Yabs + Yabs*(1-YY) * (((C_3*YY + C_2)*YY + C_1)*YY + C_0);
    if (Y < 0)
        y = -y;
    return (90*y);
}

/* convert ll to map_b screen coords at the given canonical scale factor.
 * avoid globe edge by at least the given number of canonical pixels.
 */
void ll2sRobinson (const LatLong &ll, SCoord &s, int edge, int scalesz)
{
    // handy half-sizes
    float hw = map_b.w/2.0F;
    float hh = map_b.h/2.0F;

    // find Robinson Y and X scale at this lat
    float Y = RobLat2Y (ll.lat_d);
    float G = RobLat2G (ll.lat_d);

    // pixels from map center
    float lng0_d = fmodf (ll.lng_d - getCenterLng() + 5*180, 2*180) - 180; // [-180,180]
    float dx = hw * G * lng0_d / 180;
    float dy = hh * Y;

    // convert to scaled screen coords, insuring within edge
    float x0 = map_b.x + hw;
    float y0 = map_b.y + hh;
    float dx_edge = hw * G;                                     // full halfwidth at this lat
    s.x = scalesz * CLAMPF (roundf (x0 + dx), x0-dx_edge+edge, x0+dx_edge-edge);
    s.y = scalesz * CLAMPF (roundf (y0 - dy), y0-hh+edge, y0+hh-edge);
}

/* convert map_b screen coords to ll.
 * return whether coord really is over the globe.
 */
bool s2llRobinson (const SCoord &s, LatLong &ll)
{
    // handy half-sizes
    float hw = map_b.w/2.0F;
    float hh = map_b.h/2.0F;

    // pixels from map center
    float dx = s.x - (map_b.x + hw);                            // +right
    float dy = (map_b.y + hh) - s.y;                            // +up

    // find lat from Robinson Y
    ll.lat_d = RobY2Lat (dy/hh);

    // find Robinson X scale at this lat thence lng
    float G = RobLat2G (ll.lat_d);
    ll.lng_d = 180*dx/(hw*G);

    // check bounds before adjustments
    bool ok = fabsf (ll.lat_d) <= 90 && fabsf (ll.lng_d) <= 180;

    // adjust to center lng, rely on normalizeLL to handle wrap
    ll.lng_d += getCenterLng();
    normalizeLL (ll);

    // ok?
    return (ok);
}




#if defined(_UNIT_TEST)

/* g++ -Wall -IArduinoLib -D_UNIT_TEST robinson.cpp && ./a.out
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
            if (s2llRobinson (s, ll)) {
                SCoord s2;
                ll2sRobinson (ll, s2, 0, 1);
                int x_err = (int)s.x - (int)s2.x;
                int y_err = (int)s.y - (int)s2.y;
                if (abs(x_err) > 1 || abs(y_err) > 1)
                    printf ("y= %4d x= %4d y2= %4d x2= %4d dy= %4d dx= %4d\n", y, x, s2.y, s2.x, y_err, x_err);
            }
        }
    }
}

#endif
