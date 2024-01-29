/* show a WiFi full-screen signal strength meter
 */

#include "HamClock.h"

#define WN_MIN_DB   (-80)               // minimum graphed dBm
#define WN_MAX_DB   (-20)               // max graphed dBm
#define WN_TRIS     10                  // triangle marker half-size

static bool wifi_meter_up;              // whether visible now

/* given a screen coord x, return its wifi power color for drawing in the given box
 */
static uint16_t getWiFiMeterColor (const SBox &box, uint16_t x)
{
    // make color scale from red @ WN_MIN_DB, yellow @ MIN_WIFI_RSSI and green @ WN_MAX_DB.
    // use power scale because we don't want to assume MIN_WIFI_RSSI is midway.
    static float pwr;
    if (pwr == 0) {
        // first call, find power that puts yellow at MIN_WIFI_RSSI, where is halfway from red to green
        pwr = logf (0.5F) / logf ((float)(MIN_WIFI_RSSI-WN_MIN_DB)/(WN_MAX_DB-WN_MIN_DB));
        // printf ("*************** pwr %g\n", pwr);
    }

    if (x <= box.x)
        return (RA8875_RED);
    if (x >= box.x + box.w)
        return (RA8875_GREEN);
    float frac = powf((float)(x - box.x)/box.w, pwr);
    uint8_t h = 85*frac;                // hue 0 is red, 255/3 = 85 is green
    return (HSV565(h,255,255));
}

/* read wifi signal strength.
 * return whether ok
 */
bool readWiFiRSSI(int &rssi)
{
    int r = WiFi.RSSI();
    if (r < 10) {
        rssi = r;
        return (true);
    } else {
        return (false);
    }
}

/* run the wifi meter screen until op taps Dismiss or Ignore.
 * show ignore based on incoming ignore_on and update if user changes.
 * return last known rssi.
 */
int runWiFiMeter(bool warn, bool &ignore_on)
{
    // prep
    Serial.printf (_FX("WiFiM @ %lu start Ignore %s\n"), millis()/1000U, ignore_on ? "on" : "off");
    wifi_meter_up = true;
    eraseScreen();
    closeDXCluster();
    closeGimbal();

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t y = 35;

    // title
    if (warn) {
        tft.setCursor (75, y);
        tft.setTextColor(RA8875_WHITE);
        tft.printf (_FX("WiFi signal strength is too low -- recommend at least %d dBm"), MIN_WIFI_RSSI);
    } else {
        tft.setCursor (100, y);
        tft.setTextColor(RA8875_WHITE);
        tft.printf (_FX("WiFi Signal Strength Meter -- to improve signal strength:"));
    }

    y += 25;

    // tips
    static const char msg0[] PROGMEM = "Try different HamClock orientations";
    static const char msg1[] PROGMEM = "Try different WiFi router orientations";
    static const char msg2[] PROGMEM = "Try moving closer to WiFi router";
    static const char msg3[] PROGMEM = "Try increasing WiFi router power";
    static const char msg4[] PROGMEM = "Try adding a WiFi repeater";
    static const char msg5[] PROGMEM = "Try experimenting with a foil reflector";
    static const char msg6[] PROGMEM = "Try moving metal objects out of line-of-sight with router";
    static const char *msg[] PROGMEM = {
        msg0, msg1, msg2, msg3, msg4, msg5, msg6
    };
    for (unsigned i = 0; i < NARRAY(msg); i++) {
        tft.setCursor (50, y += 32);
        tft.print ((const __FlashStringHelper *)pgm_read_ptr(&msg[i]));
    }

    y += 60;

    // dismiss box
    SBox dismiss_b;
    dismiss_b.x = 40;
    dismiss_b.y = y;
    dismiss_b.w = 100;
    dismiss_b.h = 35;

    // ignore box
    SBox ignore_b;
    ignore_b.x = tft.width() - 150;
    ignore_b.y = y;
    ignore_b.w = 100;
    ignore_b.h = 35;

    // signal strength box
    SBox rssi_b;
    rssi_b.x = dismiss_b.x + dismiss_b.w + 50;
    rssi_b.y = y;
    rssi_b.w = ignore_b.x - rssi_b.x - 50;
    rssi_b.h = 17;

    drawStringInBox ("Resume", dismiss_b, false, RA8875_GREEN);
    drawStringInBox ("Ignore", ignore_b, ignore_on, RA8875_GREEN);

    tft.setTextColor(RA8875_WHITE);
    for (uint16_t i = rssi_b.x; i < rssi_b.x + rssi_b.w; i++)
        tft.fillRect (i, rssi_b.y, 1, rssi_b.h, getWiFiMeterColor (rssi_b,i));
    tft.setCursor (rssi_b.x - 20, rssi_b.y + rssi_b.h + 2*WN_TRIS + 25);
    tft.print (WN_MIN_DB); tft.print (" dBm");
    tft.setCursor (rssi_b.x + rssi_b.w - 20, rssi_b.y + rssi_b.h + 2*WN_TRIS + 25);
    tft.print (WN_MAX_DB);

    uint16_t min_x = rssi_b.x + rssi_b.w*(MIN_WIFI_RSSI - WN_MIN_DB)/(WN_MAX_DB-WN_MIN_DB);
    tft.setCursor (min_x - 20, rssi_b.y - 5);
    tft.print (MIN_WIFI_RSSI);
    tft.drawLine (min_x, rssi_b.y, min_x, rssi_b.y + rssi_b.h, RA8875_BLACK);

    bool done = false;
    uint32_t log_t = millis();
    int prev_rssi_x = rssi_b.x + 1;
    int rssi = 0;
    do {

        // erase marker and value
        tft.fillRect (prev_rssi_x-WN_TRIS-1, rssi_b.y+rssi_b.h+1, 2*WN_TRIS+2, 2*WN_TRIS, RA8875_BLACK);
        tft.fillRect (rssi_b.x + rssi_b.w/2 - 20, rssi_b.y + rssi_b.h + 2*WN_TRIS + 1, 40, 30,
                                                                                    RA8875_BLACK);
        // read and update
        if (readWiFiRSSI(rssi)) {
            rssi = CLAMPF (rssi, WN_MIN_DB, WN_MAX_DB);
            uint16_t rssi_x = rssi_b.x+1 + (rssi_b.w-3)*(rssi - WN_MIN_DB)/(WN_MAX_DB-WN_MIN_DB);
            tft.fillTriangle (rssi_x, rssi_b.y+rssi_b.h+1,
                                rssi_x-WN_TRIS, rssi_b.y+rssi_b.h+2*WN_TRIS,
                                rssi_x+WN_TRIS, rssi_b.y+rssi_b.h+2*WN_TRIS,
                                getWiFiMeterColor (rssi_b,rssi_x));
            tft.setCursor (rssi_b.x + rssi_b.w/2 - 20, rssi_b.y + rssi_b.h + 2*WN_TRIS + 25);
            tft.setTextColor(RA8875_WHITE);
            tft.print (rssi);
            prev_rssi_x = rssi_x;

            // log occassionally
            if (timesUp (&log_t, 1000))
                Serial.printf (_FX("WiFiM @ %lu %d\n"), log_t/1000U, rssi);
        }

        // check touch
        SCoord tap;
        TouchType tt = readCalTouchWS (tap);
        if (tt != TT_NONE) {

            // update BME and brightness, that's all if tap restores full brightness
            readBME280();
            followBrightness();
            if (brightnessOn())
                continue;

            // check controls
            if (inBox (tap, dismiss_b)) {
                Serial.printf (_FX("WiFiM @ %lu Resume\n"), millis()/1000U);
                done = true;
            } else if (inBox (tap, ignore_b)) {
                ignore_on = !ignore_on;
                drawStringInBox ("Ignore", ignore_b, ignore_on, RA8875_GREEN);
                if (ignore_on) {
                    Serial.printf (_FX("WiFiM @ %lu Ignore\n"), millis()/1000U);
                    done = true;
                }
            }

        } else

            wdDelay (100);

    } while (!done);

    // new state
    wifi_meter_up = false;
    initScreen();

    return (rssi);

}

bool wifiMeterIsUp()
{
    return (wifi_meter_up);
}
