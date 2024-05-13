/* handle the touch screen
 */



#include "HamClock.h"



/* read the touch screen and return raw uncalibrated coordinates.
 */
static TouchType readRawTouch (uint16_t &x, uint16_t &y)
{
    // fast return if none
    if (!tft.touched())
        return (TT_NONE);

    // sums for means until released
    uint32_t xsum = 0, ysum = 0;
    uint16_t nsum = 0;

    // collect and determine duration until released
    while (tft.touched()) {
        uint16_t tx, ty;
        tft.touchRead (&tx, &ty);
        xsum += tx;
        ysum += ty;
        nsum++;
        wdDelay(10);
    }

    // set location from means
    x = xsum/nsum;
    y = ysum/nsum;
    
    // return tap
    return (TT_TAP);
}


/* given values return from tft.touchRead(), return screen location.
 * N.B. assumes calibrateTouch() has already been called.
 */
static void touch2Screen (uint16_t tx, uint16_t ty, SCoord &s)
{
    s.x = tx;
    s.y = ty;
}

/* read keyboard char and check for warp cursor if hjkl or engage cr/lf/space
 * N.B. ignore multiple rapid engages
 */
TouchType checkKBWarp (SCoord &s)
{
    TouchType tt = TT_NONE;
    s.x = s.y = 0;

#if !defined(_WEB_ONLY)

    // ignore if don't want warping
    if (!want_kbcursor)
        return (TT_NONE);

    bool control, shift;
    char c = tft.getChar (&control, &shift);
    if (c) {

        switch (c) {

        case CHAR_LEFT: case CHAR_DOWN: case CHAR_UP: case CHAR_RIGHT:
            // warp
            {
                unsigned n = 1;
                if (shift)
                    n *= 2;
                if (control)
                    n *= 4;
                int x, y;
                if (tft.warpCursor (c, n, &x, &y)) {
                    s.x = x;
                    s.y = y;
                }
            }
            break;

        case CHAR_CR: case CHAR_NL: case CHAR_SPACE:
            // engage
            {
                static uint32_t prev_engage_ms;
                static int n_fast_engages;
                uint32_t engage_ms = millis();
                bool engage_rate_ok = engage_ms - prev_engage_ms > 1000;
                bool engage_ok = engage_rate_ok || ++n_fast_engages < 10;

                if (engage_ok && tft.getMouse (&s.x, &s.y))
                    tt = TT_TAP;
                if (engage_rate_ok)
                    n_fast_engages = 0;
                else if (!engage_ok)
                    Serial.printf (F("Keyboard functions are too fast\n"));

                prev_engage_ms = engage_ms;
            }
            break;

        default:
            // ignore all other chars
            break;

        }
    }
#endif // !_WEB_ONLY

    return (tt);
}

/* read the touch screen or mouse.
 * pass back calibrated screen coordinate and return a TouchType.
 */
TouchType readCalTouch (SCoord &s)
{
    // fast return if none
    if (!tft.touched())
        return (TT_NONE);

    // read raw
    uint16_t x = 0, y = 0;
    TouchType tt = readRawTouch (x, y);

    // convert to screen coords via calibration matrix
    touch2Screen (x, y, s);

    Serial.printf(_FX("Touch @ %u s: \t%4d %4d\ttype %d\n"), millis()/1000U, s.x, s.y, (int)tt);

    // return hold or tap
    return (tt);
}


/* wait for no touch events, need time also since the resistance film seems to be sticky
 */
void drainTouch()
{
    resetWatchdog();
    uint32_t t0 = millis();
    bool touched = false;
    while (millis() - t0 < 100 || touched) {
        if ((touched = tft.touched()) == true) {
            uint16_t tx, ty;
            tft.touchRead (&tx, &ty);
        }
    }
    // Serial.println (F("Drain complete"));
    resetWatchdog();
}
