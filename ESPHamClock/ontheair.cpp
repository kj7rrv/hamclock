/* manage the On The Air activation Pane, for now POTA and SOTA.
 * provide sorting option by age, band and id code.
 */

#include "HamClock.h"


#define OTA_COLOR       RGB565(100,150,250)     // title color
#define ORG_DY          32                      // dy of organization name row
#define START_DY        47                      // dy of first row
#define OTA_DY          14                      // dy of each successive row
#define OTA_INDENT      1                       // l-r border
#define MAX_LINE        27                      // max line length, including EOS
#define MAX_VIS         ((PLOTBOX_H - START_DY)/OTA_DY)   // max visible rows
#define OK2SCDW         (top_vis < n_otaspots - MAX_VIS)  // whether it is ok to scroll down (fwd in sort)
#define OK2SCUP         (top_vis > 0)                     // whether it is ok to scroll up (backward in sort)


// current collection of ota spots
#if defined(_IS_ESP8266)
#define MAX_SPOTS       MAX_VIS                 // limit ram use on ESP
#else
#define MAX_SPOTS       50                      // even UNIX has limit so scrolling isn't crazy long
#endif
static DXClusterSpot otaspots[MAX_SPOTS];       // smallest sort field first
static int n_otaspots;                          // otaspots in use, newest at n_otaspots - 1
static int top_vis;                             // otaspots[] index showing at top of pane


// menu names and backend queries for each type of On The Air
typedef enum {
    OTAT_POTA,
    OTAT_SOTA,
    OTAT_N,
} OTAType;

#define MAX_MENUNAME_LEN        5
#define MAX_FILENAME_LEN        26
typedef struct {
    const char menu_name[MAX_MENUNAME_LEN];
    const char file_name[MAX_FILENAME_LEN];
} OTATypeInfo;
static const OTATypeInfo ota_names[OTAT_N] PROGMEM = {
    {"POTA", "/POTA/pota-activators.txt"},
    {"SOTA", "/SOTA/sota-activators.txt"},
};

static uint8_t ota_type;                        // one of OTAType
static char ota_name[MAX_MENUNAME_LEN];         // handy text name of ota_type





/* qsort-style function to compare two DXClusterSpot by freq
 */
static int qsDXCFreq (const void *v1, const void *v2)
{
    DXClusterSpot *s1 = (DXClusterSpot *)v1;
    DXClusterSpot *s2 = (DXClusterSpot *)v2;
    return (s1->kHz - s2->kHz);
}

/* qsort-style function to compare two DXClusterSpot by de_call AKA id
 */
static int qsDXCDECall (const void *v1, const void *v2)
{
    DXClusterSpot *s1 = (DXClusterSpot *)v1;
    DXClusterSpot *s2 = (DXClusterSpot *)v2;
    return (strcmp (s1->de_call, s2->de_call));
}

/* qsort-style function to compare two DXClusterSpot by dx_grid
 */
static int qsDXCDXCall (const void *v1, const void *v2)
{
    DXClusterSpot *s1 = (DXClusterSpot *)v1;
    DXClusterSpot *s2 = (DXClusterSpot *)v2;
    return (strcmp (s1->dx_call, s2->dx_call));
}

/* qsort-style function to compare two DXClusterSpot by spotted
 */
static int qsDXCSpotted (const void *v1, const void *v2)
{
    DXClusterSpot *s1 = (DXClusterSpot *)v1;
    DXClusterSpot *s2 = (DXClusterSpot *)v2;
    return (s1->spotted - s2->spotted);
}


// menu names and functions for each sort type
typedef enum {
    OTAS_BAND,
    OTAS_CALL,
    OTAS_ID,
    OTAS_AGE,
    OTAS_N,
} OTASort;

typedef struct {
    const char *menu_name;                      // menu name for this sort
    int (*qsf)(const void *v1, const void *v2); // matching qsort compare func, if any
} OTASortInfo;
static const OTASortInfo ota_sorts[OTAS_N] = {
    {"Band", qsDXCFreq},
    {"Call", qsDXCDXCall},
    {"Id",   qsDXCDECall},
    {"Age",  qsDXCSpotted},
};

static uint8_t ota_sortby;                      // one of OTASort




/* save OTA choices
 */
static void saveOTAChoices(void)
{
    NVWriteUInt8 (NV_OTALIST, ota_type);
    NVWriteUInt8 (NV_OTASORT, ota_sortby);
}

/* init OTA choices from NV
 */
static void initOTAChoices(void)
{
    if (!NVReadUInt8 (NV_OTALIST, &ota_type) || ota_type >= OTAT_N) {
        ota_type = OTAT_POTA;
        NVWriteUInt8 (NV_OTALIST, ota_type);
    }
    strcpy_P (ota_name, ota_names[ota_type].menu_name);

    if (!NVReadUInt8 (NV_OTASORT, &ota_sortby) || ota_sortby >= OTAS_N) {
        ota_sortby = OTAS_AGE;
        NVWriteUInt8 (NV_OTASORT, ota_sortby);
    }
}


/* create a line of info for the given spot.
 */
static void formatOTASpot (const DXClusterSpot &spot, char line[MAX_LINE])
{
    // n chars in each field, ie, all lengths are sans EOS and intervening gaps
    const unsigned ID_LEN = ota_type == OTAT_POTA ? 7 : 10;
    const unsigned AGE_LEN = ota_type == OTAT_POTA ? 3 : 1;
    #define FREQ_LEN        6
    #define CALL_LEN        (MAX_LINE - FREQ_LEN - AGE_LEN - ID_LEN - 4) // -EOS and -3 spaces

    // printf ("*********************** ID %d AGE %d FREQ %d CALL %d\n", ID_LEN, AGE_LEN, FREQ_LEN, CALL_LEN);

    // pretty freq + trailing space
    int l = snprintf (line, MAX_LINE, _FX("%*.0f "), FREQ_LEN, spot.kHz);

    // add dx call; truncate if too long or look for / and use longest side
    if (strlen (spot.dx_call) > CALL_LEN) {
        const char *slash = strchr (spot.dx_call, '/');
        if (slash) {
            // use longest section
            int left_len = slash - spot.dx_call;
            int rite_len = strlen (slash+1);
            if (left_len > rite_len)
                l += snprintf (line+l, MAX_LINE-l, _FX("%-*.*s"), CALL_LEN, left_len, spot.dx_call);
            else
                l += snprintf (line+l, MAX_LINE-l, _FX("%-*.*s"),CALL_LEN, CALL_LEN, spot.dx_call+left_len+1);
        } else {
            // no choice but to truncate
            l += snprintf (line+l, MAX_LINE-l, _FX("%.*s"), CALL_LEN, spot.dx_call);
        }
    } else {
        // fits ok as-is
        l += snprintf (line+l, MAX_LINE-l, _FX("%-*.*s"), CALL_LEN, CALL_LEN, spot.dx_call);
    }

    // leading space then spot id
    l += snprintf (line+l, MAX_LINE-l, _FX(" %*.*s"), ID_LEN, ID_LEN, spot.de_call);

    // squeeze in age
    int age = spotAgeMinutes(spot);
    if (ota_type == OTAT_SOTA) {
        if (age < 10)
            snprintf (line+l, MAX_LINE-l, _FX(" %d"), age);
        else
            snprintf (line+l, MAX_LINE-l, _FX(" +"));
    } else
        snprintf (line+l, MAX_LINE-l, _FX(" %2dm"), age);
}

/* redraw all visible otaspots in the given box.
 * N.B. this just draws the otaspots, use drawOTA to start from scratch.
 */
static void drawOTAVisSpots (const SBox &box)
{
    tft.fillRect (box.x+1, box.y + START_DY-1, box.w-2, box.h - START_DY - 1, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(RA8875_WHITE);
    uint16_t x = box.x + OTA_INDENT;
    uint16_t y = box.y + START_DY;

    // draw otaspots top_vis on top
    int n_shown = 0;
    for (int i = top_vis; i < n_otaspots && n_shown < MAX_VIS; i++) {
        char line[MAX_LINE];
        formatOTASpot (otaspots[i], line);
        tft.setCursor (x, y);
        tft.print (line);
        y += OTA_DY;
        n_shown++;
    }

    // draw scroll controls, as needed
    drawScrollDown (box, OTA_COLOR, n_otaspots - top_vis - MAX_VIS, OK2SCDW);
    drawScrollUp (box, OTA_COLOR, top_vis, OK2SCUP);
}

/* draw otaspots[] in the given pane box from scratch.
 * use drawOTAVisSpots() if want to redraw just the spots.
 */
static void drawOTA (const SBox &box)
{
    // prep
    prepPlotBox (box);

    // title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(OTA_COLOR);
    static const char *title = "On the Air";
    uint16_t tw = getTextWidth(title);
    tft.setCursor (box.x + (box.w-tw)/2, box.y + PANETITLE_H);
    tft.print (title);

    // label count and which organization
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(OTA_COLOR);
    uint16_t ow = getTextWidth(ota_name);
    tft.setCursor (box.x + (box.w-ow)/2, box.y + ORG_DY);
    tft.print (ota_name);

    // show each spot starting with top_vis, up to max
    drawOTAVisSpots (box);
}

/* scroll up, if appropriate to do so now.
 */
static void scrollOTAUp (const SBox &box)
{
    if (OK2SCUP) {
        top_vis -= (MAX_VIS - 1);               // retain 1 for context
        if (top_vis < 0)
            top_vis = 0;
        drawOTAVisSpots (box);
    }
}

/* scroll down, if appropriate to do so now.
 */
static void scrollOTADown (const SBox &box)
{
    if (OK2SCDW) {
        top_vis += (MAX_VIS - 1);               // retain 1 for context
        if (top_vis > n_otaspots - MAX_VIS)
            top_vis = n_otaspots - MAX_VIS;
        drawOTAVisSpots (box);
    }
}

/* set radio and DX from given row, known to be defined
 */
static void engageOTARow (DXClusterSpot &s)
{
    setRadioSpot(s.kHz);

    LatLong ll;
    ll.lat_d = rad2deg(s.dx_lat);
    ll.lng_d = rad2deg(s.dx_lng);
    newDX (ll, NULL, s.dx_call);       // normalizes
}


/* show menu to let op change organization
 * Program:
 *   (*) SOTA   ( ) POTA
 * Sort:
 *   ( ) Age    ( ) Band
 *   (*) Call   ( ) ID
 */
static void runOTAOrgMenu (const SBox &box)
{

    // copy menu names, really only for ESP
    char names[OTAT_N][MAX_MENUNAME_LEN];
    for (int i = 0; i < OTAT_N; i++)
        strcpy_P (names[i], ota_names[i].menu_name);

    #define OTA_LINDENT 3
    #define OTA_MINDENT 7
    bool is_pota = ota_type == OTAT_POTA;
    MenuItem mitems[] = {
        // first column
        {MENU_LABEL, false,                  0, OTA_LINDENT, "Program:"},
        {MENU_1OFN, is_pota,                 1, OTA_MINDENT, names[OTAT_POTA]},
        {MENU_LABEL, false,                  0, OTA_LINDENT, "Sort by:"},
        {MENU_1OFN, ota_sortby == OTAS_AGE,  2, OTA_MINDENT, ota_sorts[OTAS_AGE].menu_name},
        {MENU_1OFN, ota_sortby == OTAS_BAND, 2, OTA_MINDENT, ota_sorts[OTAS_BAND].menu_name},

        // second column
        {MENU_BLANK, false,                  0, OTA_MINDENT, NULL },
        {MENU_1OFN, !is_pota,                1, OTA_MINDENT, names[OTAT_SOTA]},
        {MENU_BLANK, false,                  0, OTA_MINDENT, NULL },
        {MENU_1OFN, ota_sortby == OTAS_CALL, 2, OTA_MINDENT, ota_sorts[OTAS_CALL].menu_name},
        {MENU_1OFN, ota_sortby == OTAS_ID,   2, OTA_MINDENT, ota_sorts[OTAS_ID].menu_name},
    };
    #define OTAMENU_N   NARRAY(mitems)

    SBox menu_b = box;                  // copy, not ref
    menu_b.x = box.x + box.w/8;
    menu_b.y = box.y + ORG_DY;
    menu_b.w = 0;       // shrink to fit
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, true, false, 2, OTAMENU_N, mitems};
    if (runMenu (menu)) {

        // find desired list source and note whether changed for ESP
#if defined(_IS_ESP8266)
        uint8_t old_type = ota_type;
#endif // _IS_ESP8266
        if (mitems[1].set)
            ota_type = OTAT_POTA;
        else
            ota_type = OTAT_SOTA;

        // find new sort field
        if (mitems[3].set)
            ota_sortby = OTAS_AGE;
        else if (mitems[4].set)
            ota_sortby = OTAS_BAND;
        else if (mitems[8].set)
            ota_sortby = OTAS_CALL;
        else
            ota_sortby = OTAS_ID;

        // update
        saveOTAChoices();
        updateOnTheAir(box);
#if defined(_IS_ESP8266)
        if (ota_type != old_type) {
            // too lazy to erase old then redraw fresh map spots
            initEarthMap();
        }
#endif // _IS_ESP8266

    } else {

        // just simple refresh to erase menu
        drawOTA (box);
    }
}


/* read fresh ontheair info and draw pane in box
 */
bool updateOnTheAir (const SBox &box)
{
    initOTAChoices();
    const char *page = ota_names[ota_type].file_name;
    int n_read = 0;

    WiFiClient ota_client;
    char line[100];
    bool ok = false;

    Serial.println (page);
    if (wifiOk() && ota_client.connect(backend_host, BACKEND_PORT)) {

        // look alive
        resetWatchdog();
        updateClocks(false);

        // fetch page and skip header
        httpHCPGET (ota_client, backend_host, page);
        if (!httpSkipHeader (ota_client)) {
            Serial.print (F("OnTheAir download failed\n"));
            goto out;
        }

        // reset otaspots[]
        n_otaspots = 0;

        // add each spot
        while (getTCPLine (ota_client, line, sizeof(line), NULL)) {

            // skip comments
            if (line[0] == '#')
                continue;

            // parse
            char dxcall[12], iso[20], dxgrid[7], mode[8], id[12];       // N.B. match sscanf field lengths
            float lat, lng;
            unsigned long hz;
            // JI1ORE,430510000,2023-02-19T07:00:14,CW,QM05,35.7566,140.189,JA-1234
            if (sscanf (line, _FX("%11[^,],%lu,%19[^,],%7[^,],%6[^,],%f,%f,%11s"),
                                dxcall, &hz, iso, mode, dxgrid, &lat, &lng, id) != 8) {
                // maybe a blank mode?
                if (sscanf (line, _FX("%11[^,],%lu,%19[^,],,%6[^,],%f,%f,%11s"),
                                dxcall, &hz, iso, dxgrid, &lat, &lng, id) != 7) {
                    Serial.printf (_FX("OTA: bogus line: %s\n"), line);
                    continue;
                }
                // .. yup
                mode[0] = '\0';
            }

            // ignore GHz spots because they are too wide to print
            if (hz >= 1000000000) {
                Serial.printf (_FX("OTA: ignoring freq >= 1 GHz: %s\n"), line);
                continue;
            }

            // add to list
            if (n_otaspots == MAX_SPOTS) {
                // shift out the oldest
                memmove (otaspots, &otaspots[1], (MAX_SPOTS-1) * sizeof(DXClusterSpot));
                n_otaspots = MAX_SPOTS - 1;
            }
            DXClusterSpot *sp = &otaspots[n_otaspots++];
            memset (sp, 0, sizeof(*sp));

            // use de_call for id, de_grid for list name
            strncpy (sp->de_call, id, sizeof(sp->de_call));
            strncpy (sp->de_grid, ota_name, sizeof(sp->de_grid));
            strncpy (sp->dx_call, dxcall, sizeof(sp->dx_call));
            strncpy (sp->dx_grid, dxgrid, sizeof(sp->dx_grid));
            strncpy (sp->mode, mode, sizeof(sp->mode));
            sp->dx_lat = deg2rad(lat);
            sp->dx_lng = deg2rad(lng);
            sp->de_lat = de_ll.lat;
            sp->de_lng = de_ll.lng;
            sp->kHz = hz / 1000.0F;
            sp->spotted = crackISO8601 (iso);

            // count
            n_read++;
        }

        // fresh screen coords
        updateOnTheAirSpotScreenLocations();

        // ok, even if none found
        ok = true;
    }

out:

    // restart scrolled all the way down
    top_vis = n_otaspots - MAX_VIS;
    if (top_vis < 0)
        top_vis = 0;

    if (ok) {
        Serial.printf (_FX("OTA: using %d of %d\n"), n_otaspots, n_read);
        qsort (otaspots, n_otaspots, sizeof(DXClusterSpot), ota_sorts[ota_sortby].qsf);
        drawOTA (box);
    } else {
        plotMessage (box, OTA_COLOR, _FX("On The Air error"));
    }

    ota_client.stop();

    return (ok);
}

/* implement a tap at s known to be within the given box for our Pane.
 * return if something for us, else false to mean op wants to change the Pane option.
 */
bool checkOnTheAirTouch (const SCoord &s, const SBox &box)
{
    // check for title or scroll
    if (s.y < box.y + PANETITLE_H) {

        if (checkScrollUpTouch (s, box)) {
            scrollOTAUp (box);
            return (true);
        }

        if (checkScrollDownTouch (s, box)) {
            scrollOTADown (box);
            return (true);
        }

        // else tapping title always leaves this pane
        return (false);
    }

    // check for tapping organization
    if (s.y < box.y + START_DY) {
        runOTAOrgMenu (box);
        return (true);
    }

    // tapped a row, engage if defined
    int vis_row = (s.y - START_DY)/OTA_DY;
    int spot_row = top_vis + vis_row;
    if (spot_row >= 0 && spot_row < n_otaspots)
        engageOTARow (otaspots[spot_row]);

    // ours even if row is empty
    return (true);

}

/* return a list of the current OTA spots, if any
 */
bool getOnTheAirSpots (DXClusterSpot **spp, uint8_t *nspotsp)
{
    *spp = otaspots;
    *nspotsp = n_otaspots;
    return (n_otaspots > 0);
}

/* check whether the given location is over any OTA spots
 */
bool overAnyOnTheAirSpots (const SCoord &s)
{
    // false for sure if not labeling or spots are not on
    if (!labelSpots() || findPaneForChoice(PLOT_CH_OTA) == PANE_NONE)
        return (false);

    for (int i = 0; i < n_otaspots; i++)
        if (inBox (s, otaspots[i].map_b))
            return (true);

    return (false);
}

/* draw all current OTA spots on the map
 * N.B. we assume updateOnTheAirSpotScreenLocations() has already been called to set locations.
 */
void drawOnTheAirSpotsOnMap (void)
{
    // skip if we are not up or don't want spots on map
    if (!labelSpots() || findPaneForChoice(PLOT_CH_OTA) == PANE_NONE)
        return;

    for (int i = 0; i < n_otaspots; i++)
        drawDXCOnMap (otaspots[i]);

}

/* update screen coords of each OTA spot, called ostensibly when projection changes.
 */
void updateOnTheAirSpotScreenLocations (void)
{
    for (int i = 0; i < n_otaspots; i++)
        setDXCMapPosition (otaspots[i]);

}

/* find closest otaspot and location on either end to given ll, if any.
 */
bool getClosestOnTheAirSpot (const LatLong &ll, DXClusterSpot *sp, LatLong *llp)
{
    // false for sure if not labeling or spots are not on
    if (!labelSpots() || findPaneForChoice(PLOT_CH_OTA) == PANE_NONE)
        return (false);

    return (getClosestDXC (otaspots, n_otaspots, ll, sp, llp));
}
