/* manage the On The Air activation Pane, such as OTA and SOTA.
 */

#include "HamClock.h"


#define OTA_COLOR       RGB565(100,150,250)      // title color
#define ORG_DY          32                      // dy of organization name row
#define START_DY        47                      // dy of first row
#define OTA_DY          14                      // dy of each successive row
#define OTA_INDENT      4                       // l-r border
#define MAX_VIS         ((PLOTBOX_H - START_DY)/OTA_DY)   // max visible rows
#define OK2SCDW         (top_vis < n_otaspots - MAX_VIS)  // whether it is ok to scroll down (fwd in time)
#define OK2SCUP         (top_vis > 0)                     // whether it is ok to scroll up (backward in time)
#define SCR_DX          (PLOTBOX_W-15)          // scroll control dx center within box
#define SCRUP_DY        9                       // up " dy down "
#define SCRDW_DY        23                      // down " dy down "
#define SCR_R           5                       // " radius


typedef enum {
    OTAT_POTA,
    OTAT_SOTA,
    OTAT_N,
} OTAType;

typedef struct {
    const char menu_name[5];
    const char file_name[26];
} OTAName;

// N.B. files must match order in OTAType
static const OTAName ota_names[OTAT_N] PROGMEM = {
    {"POTA", "/POTA/pota-activators.txt"},
    {"SOTA", "/SOTA/sota-activators.txt"},
};

// current collection of otaspots
#if defined(_IS_ESP8266)
#define N_MORESP          0                     // use less precious ESP mem
#else
#define N_MORESP          10                    // n more than MAX_VIS spots
#endif
#define MAX_SPOTS         (MAX_VIS+N_MORESP)   // total number of spots retained
static DXClusterSpot otaspots[MAX_SPOTS];
static int n_otaspots;                          // otaspots in use, newest at n_otaspots - 1
static int top_vis;                             // otaspots[] index showing at top of pane


/* save new OTA choice
 */
static void saveOTAChoice (uint8_t c)
{
    NVWriteUInt8 (NV_ONTHEAIR, c);
}

/* return current OTA choice
 */
static uint8_t getOTAChoice (void)
{
    uint8_t c;
    if (!NVReadUInt8 (NV_ONTHEAIR, &c) || c >= OTAT_N) {
        c = OTAT_POTA;
        NVWriteUInt8 (NV_ONTHEAIR, c);
    }
    return (c);
}


/* draw, else erase, the up scroll control;
 */
static void drawOTAScrollUp (const SBox &box, bool draw)
{
        uint16_t x0 = box.x + SCR_DX;
        uint16_t y0 = box.y + SCRUP_DY - SCR_R;
        uint16_t x1 = box.x + SCR_DX - SCR_R;
        uint16_t y1 = box.y + SCRUP_DY + SCR_R;
        uint16_t x2 = box.x + SCR_DX + SCR_R;
        uint16_t y2 = box.y + SCRUP_DY + SCR_R;

        tft.fillTriangle (x0, y0, x1, y1, x2, y2, draw ? OTA_COLOR : RA8875_BLACK);
}

/* draw, else erase, the down scroll control.
 */
static void drawOTAScrollDown (const SBox &box, bool draw)
{
        uint16_t x0 = box.x + SCR_DX - SCR_R;
        uint16_t y0 = box.y + SCRDW_DY - SCR_R;
        uint16_t x1 = box.x + SCR_DX + SCR_R;
        uint16_t y1 = box.y + SCRDW_DY - SCR_R;
        uint16_t x2 = box.x + SCR_DX;
        uint16_t y2 = box.y + SCRDW_DY + SCR_R;

        tft.fillTriangle (x0, y0, x1, y1, x2, y2, draw ? OTA_COLOR : RA8875_BLACK);
}

/* create a line of info for the given spot.
 * best if similar to dxcluster etc
 */
static void formatOTASpot (const DXClusterSpot &spot, char line[], int line_len)
{
    // pretty freq, fixed 8 chars
    const char *f_fmt = spot.kHz < 1e6F ? _FX("%8.1f") : _FX("%8.0f");
    int l = snprintf (line, line_len, f_fmt, spot.kHz);

    // add remaining fields
    snprintf (line+l, line_len-l, _FX(" %-*s %04u"), MAX_SPOTCALL_LEN-1, spot.dx_call, spot.utcs);
}

/* redraw all visible otaspots in the given box.
 * N.B. this just draws the otaspots, use drawOTA to start from scratch.
 */
static void drawOTAVisSpots (const SBox &box)
{
    tft.fillRect (box.x+1, box.y + START_DY+1, box.w-2, box.h - START_DY-2, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(RA8875_WHITE);
    uint16_t x = box.x + OTA_INDENT;
    uint16_t y = box.y + START_DY;

    // draw otaspots top_vis on top
    int n_shown = 0;
    for (int i = top_vis; i < n_otaspots && n_shown < MAX_VIS; i++) {
        char line[100];
        formatOTASpot (otaspots[i], line, sizeof(line));
        tft.setCursor (x, y);
        tft.print (line);
        y += OTA_DY;
        n_shown++;
    }

    // draw scroll controls, as needed
    drawOTAScrollDown (box, OK2SCDW);
    drawOTAScrollUp (box, OK2SCUP);
}

/* draw otaspots[] in the given pane box
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
    const char *org = getOTAChoice() == OTAT_POTA ? "POTA" : "SOTA";
    uint16_t ow = getTextWidth(org);
    tft.setCursor (box.x + (box.w-ow)/2, box.y + ORG_DY);
    tft.print (org);

    // start scrolled all the way down
    top_vis = n_otaspots - MAX_VIS;
    if (top_vis < 0)
        top_vis = 0;

    // show each spot starting with top_vis, up to max
    drawOTAVisSpots (box);

}

/* scroll up, if appropriate to do so now.
 */
static void scrollOTAUp (const SBox &box)
{
    if (OK2SCUP) {
        top_vis -= 1;
        drawOTAVisSpots (box);
    }
}

/* scroll down, if appropriate to do so now.
 */
static void scrollOTADown (const SBox &box)
{
    if (OK2SCDW) {
        top_vis += 1;
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
 */
static void runOTAOrgMenu (const SBox &box)
{
    #define OTA_MINDENT 4
    uint8_t ota_choice = getOTAChoice();
    MenuItem mitems[OTAT_N];
    char names[OTAT_N][sizeof(ota_names[0].menu_name)]; // really only for ESP
    for (int i = 0; i < OTAT_N; i++) {
        strcpy_P (names[i], ota_names[i].menu_name);
        mitems[i] = { MENU_1OFN, ota_choice == i, 1, OTA_MINDENT, names[i] };
    }
    SBox menu_b = box;
    menu_b.x = box.x + 3*box.w/8;
    menu_b.y = box.y + ORG_DY;
    menu_b.w = 0;       // shrink to fit
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, true, true, 1, OTAT_N, mitems};
    if (runMenu (menu)) {

        // find new selection
        uint8_t new_choice = 0;
        for (int i = 0; i < OTAT_N; i++) {
            if (mitems[i].set) {
                new_choice = i;
                break;
            }
        }

        // update if changed else just redraw pane to erase menu
        if (new_choice != ota_choice) {
            ota_choice = new_choice;
            saveOTAChoice (ota_choice);
            updateOnTheAir (box);
#if defined(_IS_ESP8266)
            // too lazy to erase old then redraw fresh map spots
            initEarthMap();
#endif
        } else {
            drawOTA (box);
        }

    } else {
        // refresh to erase menu
        drawOTA (box);
    }
}


/* read fresh ontheair info and draw pane in box
 */
bool updateOnTheAir (const SBox &box)
{
    WiFiClient ota_client;
    char line[100];
    bool ok = false;

    uint8_t ota_choice = getOTAChoice();
    const char *page = ota_names[ota_choice].file_name;
    int n_read = 0;

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
            char dxcall[12], iso[20], dxgrid[7], mode[8];               // N.B. match sscanf field lengths
            float lat, lng;
            int hz;
            // JI1ORE,430510000,2023-02-19T07:00:14,CW,QM05,35.7566,140.189
            if (sscanf (line, "%11[^,],%d,%19[^,],%7[^,],%6[^,],%f,%f",
                                dxcall, &hz, iso, mode, dxgrid, &lat, &lng) != 7) {
                // maybe a blank mode?
                if (sscanf (line, "%11[^,],%d,%19[^,],,%6[^,],%f,%f",
                                dxcall, &hz, iso, dxgrid, &lat, &lng) != 6) {
                    Serial.printf (_FX("Bad OTA line: %s\n"), line);
                    goto out;
                }
                // yup
                mode[0] = '\0';
            }

            // save next spot, shift out oldest if already full
            if (n_otaspots == MAX_SPOTS) {
                memmove (otaspots, &otaspots[1], (MAX_SPOTS-1) * sizeof(DXClusterSpot));
                n_otaspots = MAX_SPOTS - 1;
            }
            DXClusterSpot *sp = &otaspots[n_otaspots++];
            memset (sp, 0, sizeof(*sp));
            // leave de_call and de_grid blank; de not really known but use our DE for dist and bearing
            strncpy (sp->dx_call, dxcall, sizeof(sp->dx_call));
            strncpy (sp->dx_grid, dxgrid, sizeof(sp->dx_grid));
            strncpy (sp->mode, mode, sizeof(sp->mode));
            sp->dx_lat = deg2rad(lat);
            sp->dx_lng = deg2rad(lng);
            sp->de_lat = de_ll.lat;
            sp->de_lng = de_ll.lng;
            sp->kHz = hz / 1000.0F;
            sp->utcs = 100*atoi(iso+11) + atoi(iso+14); // 100*hr + min

            // count
            n_read++;
        }

        // fresh screen coords
        updateOnTheAirSpotScreenLocations();

        // ok, even if none found
        ok = true;
    }

out:

    if (ok) {
        Serial.printf (_FX("OTA: saved %d of %d\n"), n_otaspots, n_read);
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
    // check for title tap -- allow a fairly wide region for the scroll controls to reduce false menus
    if (s.y < box.y + PANETITLE_H) {

        if (s.x >= box.x + 3*box.w/4) {
            if (s.y < box.y + PANETITLE_H/2)
                scrollOTAUp (box);
            else
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

/* return a list of the current OTA spots
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
 * N.B. we assume updateOnTheAirlusterSpotScreenLocations() has already been called to set locations.
 */
void drawOnTheAirSpotsOnMap (void)
{
    // skip if we are not up or don't want spots on map
    if (findPaneForChoice(PLOT_CH_OTA) == PANE_NONE || !labelSpots())
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
        return (getClosestDXC (otaspots, n_otaspots, ll, sp, llp));
}

