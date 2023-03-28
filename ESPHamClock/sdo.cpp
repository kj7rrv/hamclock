/* manage the Solar Dynamics Observatory pane
 */

#include "HamClock.h"

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

/* save new image choice and whether rotating
 */
static void saveSDOChoice (uint8_t c, uint8_t r)
{
    NVWriteUInt8 (NV_SDO, c);
    NVWriteUInt8 (NV_SDOROT, r);
}

/* return current image choice and whether rotating
 */
void getSDOChoice (uint8_t &choice, uint8_t &rot)
{
    if (!NVReadUInt8 (NV_SDO, &choice) || choice >= SDOT_N) {
        choice = SDOT_COMP;
        NVWriteUInt8 (NV_SDO, choice);
    }
    if (!NVReadUInt8 (NV_SDOROT, &rot)) {
        rot = 0;
        NVWriteUInt8 (NV_SDOROT, rot);
    }
}

/* read the current SDO image choice and display in the given box
 */
bool updateSDO (const SBox &box)
{
    // get choice, and possible rotate
    uint8_t sdo_choice, sdo_rotating;
    getSDOChoice (sdo_choice, sdo_rotating);
    if (sdo_rotating) {
        sdo_choice = (sdo_choice + 1) % SDOT_N;
        saveSDOChoice (sdo_choice, sdo_rotating);
    }

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
    bool ok = drawHTTPBMP (fn, box, SDO_COLOR);

    printFreeHeap(F("updateSDO"));
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
    uint8_t sdo_choice, sdo_rotating;
    getSDOChoice (sdo_choice, sdo_rotating);

#if defined (_IS_UNIX)
    // check for movie easter egg
    if (s.y > box.y + 4*box.h/5 && s.x > box.x + 4*box.w/5) {
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
        saveSDOChoice (sdo_choice, sdo_rotating);

    } else if (sdo_rotating) {

        // cancelled: this kludge effectively causes updateSDO to show the same image
        sdo_choice = (sdo_choice + SDOT_N - 1) % SDOT_N;
        saveSDOChoice (sdo_choice, sdo_rotating);
    }

    // always show image, even if cancelled just to erase the menu
    scheduleNewSDO();

    // ours
    return (true);
}
