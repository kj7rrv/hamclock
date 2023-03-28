/* handle contest retrieval and display
 */

#include "HamClock.h"



#define CONTEST_COLOR   RGB565(176,48,96)       // title color -- X11Maroon
#define CREDITS_Y0      31                      // dy of credits row
#define START_DY        47                      // dy of first row
#define CONTEST_DY      12                      // dy of each successive row
#define MAX_CTST_LEN    26                      // max contest entry length, not counting EOS
#define MAX_VIS         ((PLOTBOX_H - START_DY)/CONTEST_DY)     // max visible rows
#define OK2SCDW         (top_vis < n_contests - MAX_VIS)        // whether it is ok to scroll down
#define OK2SCUP         (top_vis > 0)                           // whether it is ok to scroll up

static const char contest_page[] PROGMEM = "/contests/contests.txt";

// current collection of ota spots
#if defined(_IS_ESP8266)
#define MAX_CONTESTS    MAX_VIS                 // limit ram use on ESP
#else
#define MAX_CONTESTS    50                      // even UNIX has limit so scrolling isn't crazy long
#endif
static char *contests[MAX_CONTESTS];            // malloced strings
static char *credit;                            // malloced credit line
static int n_contests;                          // n entries in contests[]
static int top_vis;                             // contests[] index currently showing on first row


/* draw contests[] in the given pane box
 */
static void drawContests (const SBox &box)
{
    // skip if no credit yet
    if (!credit)
        return;

    // erase
    prepPlotBox (box);

    // title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(CONTEST_COLOR);
    static const char *title = "Contests";
    uint16_t tw = getTextWidth(title);
    tft.setCursor (box.x + (box.w-tw)/2, box.y + PANETITLE_H);
    tft.print (title);

    // credit
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(CONTEST_COLOR);
    uint16_t cw = getTextWidth(credit);
    tft.setCursor (box.x + (box.w-cw)/2, box.y + CREDITS_Y0);
    tft.print (credit);

    // show each contest starting with top_vis, up to max visible
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(RA8875_WHITE);
    uint16_t y = box.y + START_DY;
    int n_shown = 0;
    for (int i = top_vis; i < n_contests && n_shown < MAX_VIS; i++) {
        uint16_t w = getTextWidth (contests[i]);
        tft.setCursor (box.x + (box.w-w)/2, y);
        tft.print (contests[i]);
        y += CONTEST_DY;
        n_shown++;
    }

    // draw scroll controls, if needed
    drawScrollDown (box, CONTEST_COLOR, n_contests - top_vis - MAX_VIS, OK2SCDW);
    drawScrollUp (box, CONTEST_COLOR, top_vis, OK2SCUP);
}

/* scroll up, if appropriate to do so now.
 */
static void scrollContestUp (const SBox &box)
{
    if (OK2SCUP) {
        top_vis -= (MAX_VIS - 1);               // retain 1 for context
        if (top_vis < 0)
            top_vis = 0;
        drawContests (box);
    }
}

/* scroll down, if appropriate to do so now.
 */
static void scrollContestDown (const SBox &box)
{
    if (OK2SCDW) {
        top_vis += (MAX_VIS - 1);               // retain 1 for context
        if (top_vis > n_contests - MAX_VIS)
            top_vis = n_contests - MAX_VIS;
        drawContests (box);
    }
}

/* scrub the given line IN PLACE to fit within MAX_CTST_LEN chars
 */
static void scrubContest (char *line)
{
    // nothing to do if already fits
    int ll = strlen (line);
    if (ll <= MAX_CTST_LEN)
        return;

    // keep chopping off at right-most space
    char *right_space = NULL;
    do {
        right_space = strrchr (line, ' ');
        if (right_space) {
            *right_space = '\0';
            ll = right_space - line;
        }
    } while (right_space && ll > MAX_CTST_LEN);

    // then also chop off any final punct char
    if (right_space > line && ispunct(right_space[-1])) {
        right_space[-1] = '\0';
        ll -= 1;
    }

    // just chop if still too long
    if (ll > MAX_CTST_LEN)
        line[MAX_CTST_LEN] = '\0';
}

/* collect Contest info into the contests[] array and show in the given pane box
 */
bool updateContests (const SBox &box)
{
    WiFiClient contest_client;
    char line[100];
    bool ok = false;

    // download and load contests[]
    Serial.println(contest_page);
    resetWatchdog();
    if (wifiOk() && contest_client.connect(backend_host, BACKEND_PORT)) {

        // look alive
        resetWatchdog();
        updateClocks(false);

        // fetch page and skip header
        httpHCPGET (contest_client, backend_host, contest_page);
        if (!httpSkipHeader (contest_client)) {
            Serial.print (F("contest download failed\n"));
            goto out;
        }

        // reset contests and credit
        for (int i = 0; i < n_contests; i++)
            free (contests[i]);
        n_contests = 0;
        free (credit);
        credit = NULL;

        // first line is credit
        if (!getTCPLine (contest_client, line, sizeof(line), NULL)) {
            Serial.print (F("no credit lint\n"));
            goto out;
        }
        credit = strdup (line);

        // ok if get at least the credit message
        ok = true;

        // each addition line is a contest
        while (n_contests < MAX_CONTESTS && getTCPLine (contest_client, line, sizeof(line), NULL)) {
            // Serial.printf (_FX("Contest %d: %s\n"), n_contests, line);
            scrubContest (line);
            contests[n_contests++] = strdup (line);
        }
    }

out:

    // jump to bottom. disconcerting perhaps but how to find same position in new list?
    top_vis = n_contests - MAX_VIS;
    if (top_vis < 0)
        top_vis = 0;

    if (ok) {
        Serial.printf (_FX("CON: Found %d\n"), n_contests);
        drawContests (box);
    } else {
        plotMessage (box, CONTEST_COLOR, _FX("Contests error"));
    }

    contest_client.stop();

    return (ok);
}

/* return true if user is interacting with the contest pane, false if wants to change pane.
 * N.B. we assume s is within box
 */
bool checkContestsTouch (const SCoord &s, const SBox &box)
{
    // scroll control?
    if (s.y < box.y + PANETITLE_H) {

        if (checkScrollUpTouch (s, box)) {
            scrollContestUp (box);
            return (true);
        }
        if (checkScrollDownTouch (s, box)) {
            scrollContestDown (box);
            return (true);
        }

    } else {

#if defined(_IS_UNIX)
        // tapping anywhere in contests brings up browser page showing contests
        //   on macos: sudo port install xdg-utils
        //   on ubuntu or RPi: sudo apt install xdg-utils
        //   on redhat: sudo yum install xdg-utils
        static const char cmd[] = "xdg-open https://www.contestcalendar.com/weeklycont.php";
        if (system (cmd))
            Serial.printf (_FX("CON: fail: %s\n"), cmd);
        else
            Serial.printf (_FX("CON: ok: %s\n"), cmd);
#endif
        // stay if touch lower portion regardless
        return (true);
    }


    // not ours
    return (false);
}

/* return contests to caller
 */
int getContests (char **credp, char ***conppp)
{
    *credp = credit;
    *conppp = contests;
    return (n_contests);
}
