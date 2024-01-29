/* manage the Solar Dynamics Observatory pane
 */

#include "HamClock.h"

#define SDO_IMG_INTERVAL        1800            // image update interval, seconds

typedef enum {
    SDOT_COMP,
    SDOT_HMIB,
    SDOT_HMIIC,
    SDOT_131,
    SDOT_193,
    SDOT_211,
    SDOT_304,
    SDOT_N
} SDOImgType;

typedef struct {
    const char menu_name[12];
    const char file_name[28];
} SDOName;

#define SDO_COLOR       RA8875_MAGENTA          // just for error messages

// N.B. files must match order in SDOImgType
// N.B. file names depend on build size
static const SDOName sdo_names[SDOT_N] PROGMEM = {
    #if defined(_CLOCK_1600x960) 
        {"Composite",   "/SDO/f_211_193_171_340.bmp"},
        {"Magnetogram", "/SDO/latest_340_HMIB.bmp"},
        {"6173A",       "/SDO/latest_340_HMIIC.bmp"},
        {"131A",        "/SDO/f_131_340.bmp"},
        {"193A",        "/SDO/f_193_340.bmp"},
        {"211A",        "/SDO/f_211_340.bmp"},
        {"304A",        "/SDO/f_304_340.bmp"},
    #elif defined(_CLOCK_2400x1440)
        {"Composite",   "/SDO/f_211_193_171_510.bmp"},
        {"Magnetogram", "/SDO/latest_510_HMIB.bmp"},
        {"6173A",       "/SDO/latest_510_HMIIC.bmp"},
        {"131A",        "/SDO/f_131_510.bmp"},
        {"193A",        "/SDO/f_193_510.bmp"},
        {"211A",        "/SDO/f_211_510.bmp"},
        {"304A",        "/SDO/f_304_510.bmp"},
    #elif defined(_CLOCK_3200x1920)
        {"Composite",   "/SDO/f_211_193_171_680.bmp"},
        {"Magnetogram", "/SDO/latest_680_HMIB.bmp"},
        {"6173A",       "/SDO/latest_680_HMIIC.bmp"},
        {"131A",        "/SDO/f_131_680.bmp"},
        {"193A",        "/SDO/f_193_680.bmp"},
        {"211A",        "/SDO/f_211_680.bmp"},
        {"304A",        "/SDO/f_304_680.bmp"},
    #else
        {"Composite",   "/SDO/f_211_193_171_170.bmp"},
        {"Magnetogram", "/SDO/latest_170_HMIB.bmp"},
        {"6173A",       "/SDO/latest_170_HMIIC.bmp"},
        {"131A",        "/SDO/f_131_170.bmp"},
        {"193A",        "/SDO/f_193_170.bmp"},
        {"211A",        "/SDO/f_211_170.bmp"},
        {"304A",        "/SDO/f_304_170.bmp"},
    #endif
};

// state
#define _SDO_ROT_INIT   10
static uint8_t sdo_choice, sdo_rotating = _SDO_ROT_INIT; // rot is always 0 or 1, init with anything else

/* save image choice and whether rotating to nvram
 */
static void saveSDOChoice (void)
{
    NVWriteUInt8 (NV_SDO, sdo_choice);
    NVWriteUInt8 (NV_SDOROT, sdo_rotating);
}

/* insure sdo_choice and sdo_rotating are loaded from nvram
 */
static void loadSDOChoice (void)
{
    if (sdo_rotating == _SDO_ROT_INIT) {
        if (!NVReadUInt8 (NV_SDOROT, &sdo_rotating)) {
            sdo_rotating = 0;
            NVWriteUInt8 (NV_SDOROT, sdo_rotating);
        }
        if (!NVReadUInt8 (NV_SDO, &sdo_choice) || sdo_choice >= SDOT_N) {
            sdo_choice = SDOT_HMIIC;
            NVWriteUInt8 (NV_SDO, sdo_choice);
        }
    }
}

/* download and render sdo_choice.
 * return whether ok.
 */
static bool drawSDOImage (const SBox &box)
{
    // get corresponding file name
#if defined (_IS_ESP8266)
    // must copy to ram
    char fn_ram[sizeof(sdo_names[0].file_name)];
    strcpy_P (fn_ram, sdo_names[sdo_choice].file_name);
    char *fn = fn_ram;
#else
    const char *fn = sdo_names[sdo_choice].file_name;;
#endif

    // show file
    return (drawHTTPBMP (fn, box, SDO_COLOR));
}

/* return whether sdo image is rotating
 */
bool isSDORotating(void)
{
    loadSDOChoice();
    return (sdo_rotating != 0);
}


/* update SDO pane info for sure and possibly image also.
 */
bool updateSDOPane (const SBox &box, bool image_too)
{
    resetWatchdog();

    // update and force if rotating
    if (isSDORotating()) {
        sdo_choice = (sdo_choice + 1) % SDOT_N;
        saveSDOChoice();
        image_too = true;
    }

    // current user's time
    static time_t prev_img;
    time_t t0 = nowWO();

    // keep the strings so we can erase them exactly next time; using rectangles cuts chits from solar disk
    static char az_str[10];
    static char el_str[10];
    static char rs_str[10];
    static char rt_str[10];

    // start fresh image if requested or old else just erase previous info

    bool ok;
    if (image_too || labs (t0-prev_img) > SDO_IMG_INTERVAL) {

        // full draw
        ok = drawSDOImage(box);

        // record time if ok
        if (ok)
            prev_img = t0;

    } else {

        // pane image not drawn so erase previous individual stats
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (RA8875_BLACK);
        tft.setCursor (box.x+1, box.y+2);
        tft.print (az_str);
        tft.setCursor (box.x+box.w-getTextWidth(el_str)-1, box.y+2);
        tft.print (el_str);
        tft.setCursor (box.x+1, box.y+box.h-10);
        tft.print (rs_str);
        tft.setCursor (box.x+box.w-getTextWidth(rt_str)-1, box.y+box.h-10);
        tft.print (rt_str);

        // always ok
        ok = true;
    }

    // draw info if ok, layout similar to moon
    if (ok) {

        // fresh info at user's effective time
        getSolarCir (t0, de_ll, solar_cir);

        // draw corners, similar to moon

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (DE_COLOR);

        snprintf (az_str, sizeof(az_str), "Az:%.0f", rad2deg(solar_cir.az));
        tft.setCursor (box.x+1, box.y+2);
        tft.print (az_str);

        snprintf (el_str, sizeof(el_str), "El:%.0f", rad2deg(solar_cir.el));
        tft.setCursor (box.x+box.w-getTextWidth(el_str)-1, box.y+2);
        tft.print (el_str);

        // show which ever rise or set event comes next
        time_t rise, set;
        getSolarRS (t0, de_ll, &rise, &set);
        if (rise > t0 && (set < t0 || rise - t0 < set - t0))
            snprintf (rs_str, sizeof(rs_str), "R@%02d:%02d", hour(rise+de_tz.tz_secs),
                                                              minute (rise+de_tz.tz_secs));
        else if (set > t0 && (rise < t0 || set - t0 < rise - t0))
            snprintf (rs_str, sizeof(rs_str), "S@%02d:%02d", hour(set+de_tz.tz_secs),
                                                              minute (set+de_tz.tz_secs));
        else
            strcpy (rs_str, "No R/S");
        tft.setCursor (box.x+1, box.y+box.h-10);
        tft.print (rs_str);

        snprintf (rt_str, sizeof(rt_str), "%.0fm/s", solar_cir.vel);;
        tft.setCursor (box.x+box.w-getTextWidth(rt_str)-1, box.y+box.h-10);
        tft.print (rt_str);
    }

    return (ok);
}

/* check for touch in the given pane box.
 * return whether we should stay on this pane, else give user choice of new panes.
 */
bool checkSDOTouch (const SCoord &s, const SBox &box)
{
    // not ours when in title
    if (s.y < box.y + PANETITLE_H)
        return (false);

    #define SM_INDENT 5
    #define SDOT_ROTATE SDOT_N

    // insure current values
    loadSDOChoice();

#if defined (_IS_UNIX)
    // check for movie easter egg in lower right corner
    if (s.y > box.y + 4*box.h/5 && s.x > box.x + box.w/2) {
        const char *cmd;
        switch (sdo_choice) {
        case SDOT_COMP:
            cmd = "xdg-open https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_211193171.mp4";
            break;
        case SDOT_HMIB:
            cmd = "xdg-open https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_HMIB.mp4";
            break;
        case SDOT_HMIIC:
            cmd = "xdg-open https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_HMIIC.mp4";
            break;
        case SDOT_131:
            cmd = "xdg-open https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_0131.mp4";
            break;
        case SDOT_193:
            cmd = "xdg-open https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_0193.mp4";
            break;
        case SDOT_211:
            cmd = "xdg-open https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_0211.mp4";
            break;
        case SDOT_304:
            cmd = "xdg-open https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_0304.mp4";
            break;
        default:
            fatalError ("Unknown sdo_choice: %d", sdo_choice);
            return (true);              // lint
        }
        if (system (cmd))
            Serial.printf (_FX("SDO: fail: %s\n"), cmd);
        else
            Serial.printf (_FX("SDO: ok: %s\n"), cmd);
        return (true);
    }
#endif // _IS_UNIX

    // check for grayline rise/set in lower left corner
    if (s.y > box.y + 4*box.h/5 && s.x < box.x + box.w/2) {
        plotGrayline();
        initEarthMap();
        return (true);
    }

    // show menu of SDO images + rotate option

    MenuItem mitems[SDOT_N+1];                                  // +1 for rotate option
    char names[SDOT_N][sizeof(sdo_names[0].menu_name)];         // really just for ESP
    for (int i = 0; i < SDOT_N; i++) {
        strcpy_P (names[i], sdo_names[i].menu_name);
        mitems[i] = {MENU_1OFN, !sdo_rotating && i == sdo_choice, 1, SM_INDENT, names[i]};
    }
    mitems[SDOT_ROTATE] = {MENU_1OFN, (bool)sdo_rotating, 1, SM_INDENT, "Rotate"};

    SBox menu_b = box;          // copy, not ref
    menu_b.x += box.w/4;
    menu_b.y += 20;
    menu_b.w = 0;               // shrink wrap
    SBox ok_b;

    MenuInfo menu = {menu_b, ok_b, true, false, 1, SDOT_N+1, mitems};
    bool ok = runMenu (menu);

    // change to new option unless cancelled
    if (ok) {

        // find new selection unless rotating
        sdo_rotating = mitems[SDOT_ROTATE].set;
        if (!sdo_rotating) {
            for (int i = 0; i < SDOT_N; i++) {
                if (mitems[i].set) {
                    sdo_choice = i;
                    break;
                }
            }
        }

        // save
        saveSDOChoice();

    } else if (sdo_rotating) {

        // cancelled: this kludge effectively causes updateSDO to show the same image
        sdo_choice = (sdo_choice + SDOT_N - 1) % SDOT_N;
        saveSDOChoice();
    }

    // always show image, even if cancelled just to erase the menu
    scheduleNewSDO();

    // ours
    return (true);
}
