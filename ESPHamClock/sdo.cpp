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
static const SDOName sdo_names[SDOT_N] = {
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
    const char *fn = sdo_names[sdo_choice].file_name;;

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

/* attempt to show the movie for sdo_choice
 */
static void showSDOmovie (void)
{
    const char *url = NULL;

    switch ((SDOImgType)sdo_choice) {
    case SDOT_COMP:
        url = "https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_211193171.mp4";
        break;
    case SDOT_HMIB:
        url = "https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_HMIB.mp4";
        break;
    case SDOT_HMIIC:
        url = "https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_HMIIC.mp4";
        break;
    case SDOT_131:
        url = "https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_0131.mp4";
        break;
    case SDOT_193:
        url = "https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_0193.mp4";
        break;
    case SDOT_211:
        url = "https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_0211.mp4";
        break;
    case SDOT_304:
        url = "https://sdo.gsfc.nasa.gov/assets/img/latest/mpeg/latest_1024_0304.mp4";
        break;
    case SDOT_N:
        break;
    }

    if (url)
        openURL (url);
}

/* check for our touch in the given pane box.
 * return whether we should stay on this pane, else give user choice of new panes.
 * N.B. we assume s is within box
 */
bool checkSDOTouch (const SCoord &s, const SBox &box)
{
    // not ours when in title
    if (s.y < box.y + PANETITLE_H)
        return (false);

    // insure current values
    loadSDOChoice();

    // show menu of SDOT_N images options + rotate option + show grayline + show movie

    #define SM_INDENT 5

    // handy indices to extra menu items
    enum {
        SDOM_ROTATE = SDOT_N,
        SDOM_GAP,
        SDOM_GRAYLINE,
        SDOM_SHOWWEB,
        SDOM_NMENU
    };

    MenuItem mitems[SDOM_NMENU];

    // set first SDOT_N to name collection
    for (int i = 0; i < SDOT_N; i++) {
        mitems[i] = {MENU_1OFN, !sdo_rotating && i == sdo_choice, 1, SM_INDENT, sdo_names[i].menu_name};
    }

    // set whether rotating
    mitems[SDOM_ROTATE] = {MENU_1OFN, (bool)sdo_rotating, 1, SM_INDENT, "Rotate"};

    // nice gap
    mitems[SDOM_GAP] = {MENU_BLANK, false, 0, 0, NULL};

    // set grayline option
    mitems[SDOM_GRAYLINE] = {MENU_TOGGLE, false, 2, SM_INDENT, "Grayline tool"};

    // set show web page option, but not on fb0
#if defined(_USE_FB0)
    mitems[SDOM_SHOWWEB] = {MENU_IGNORE, false, 0, 0, NULL};
#else
    mitems[SDOM_SHOWWEB] = {MENU_TOGGLE, false, 3, SM_INDENT, "Show movie"};
#endif

    SBox menu_b = box;          // copy, not ref
    menu_b.x += box.w/4;
    menu_b.y += 5;
    menu_b.w = 0;               // shrink wrap
    SBox ok_b;

    MenuInfo menu = {menu_b, ok_b, true, false, 1, SDOM_NMENU, mitems};
    bool ok = runMenu (menu);

    // change to new option unless cancelled
    bool refresh_pane = true;
    if (ok) {

        // set new selection unless rotating
        sdo_rotating = mitems[SDOM_ROTATE].set;
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

        // check for movie
        if (mitems[SDOM_SHOWWEB].set)
            showSDOmovie ();

        // gray line must 1) fix hole in pane 2) show grayline then 3) fix map on return
        if (mitems[SDOM_GRAYLINE].set) {
            updateSDOPane (box, true);
            plotGrayline();
            initEarthMap();
            refresh_pane = false;
        }

    } else if (sdo_rotating) {

        // cancelled but this kludge effectively causes updateSDO to show the same image
        sdo_choice = (sdo_choice + SDOT_N - 1) % SDOT_N;
        saveSDOChoice();
    }

    // show image unless already done so, even if cancelled just to erase the menu
    if (refresh_pane)
        scheduleNewPlot(PLOT_CH_SDO);

    // ours
    return (true);
}
