/* handle contest retrieval and display
 */

#include "HamClock.h"



#define CONTEST_COLOR   RGB565(176,48,96)       // title color -- X11Maroon
#define CREDITS_Y0      31                      // dy of credits row
#define START_DY        47                      // dy of first row
#define CONTEST_DY      12                      // dy of each successive row
#define MAX_ROWS        ((box.h - START_DY)/CONTEST_DY)         // max display rows
#define OK2SCUP         (ctst_topi < n_contests - MAX_ROWS)     // whether it is ok to scroll up
#define OK2SCDW         (ctst_topi > 0)                         // whether it is ok to scroll down
#define CTST_INDENT     5                       // indent
#define MAX_CTST_LEN    26                      // max contest entry length, not counting EOS
#define SCR_DX          (PLOTBOX_W-15)          // scroll control dx center within box
#define SCRUP_DY        9                       // up " dy down "
#define SCRDW_DY        23                      // down " dy down "
#define SCR_R           5                       // " radius

static const char contest_page[] PROGMEM = "/contests/contests.txt";

static char **contests;                         // malloced array of malloced strings
static char *credit;                            // malloced credit line
static int n_contests;                          // n entries in contests[]
static int ctst_topi;                           // contests[] index currently showing on first row


/* draw, else erase, the up scroll control;
 */
static void drawScrollUp (const SBox &box, bool draw)
{
        uint16_t x0 = box.x + SCR_DX;
        uint16_t y0 = box.y + SCRUP_DY - SCR_R;
        uint16_t x1 = box.x + SCR_DX - SCR_R;
        uint16_t y1 = box.y + SCRUP_DY + SCR_R;
        uint16_t x2 = box.x + SCR_DX + SCR_R;
        uint16_t y2 = box.y + SCRUP_DY + SCR_R;

        tft.fillTriangle (x0, y0, x1, y1, x2, y2, draw ? CONTEST_COLOR : RA8875_BLACK);
}

/* draw, else erase, the down scroll control.
 */
static void drawScrollDown (const SBox &box, bool draw)
{
        uint16_t x0 = box.x + SCR_DX - SCR_R;
        uint16_t y0 = box.y + SCRDW_DY - SCR_R;
        uint16_t x1 = box.x + SCR_DX + SCR_R;
        uint16_t y1 = box.y + SCRDW_DY - SCR_R;
        uint16_t x2 = box.x + SCR_DX;
        uint16_t y2 = box.y + SCRDW_DY + SCR_R;

        tft.fillTriangle (x0, y0, x1, y1, x2, y2, draw ? CONTEST_COLOR : RA8875_BLACK);
}


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

    // show each contest starting with ctst_topi, up to max
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(RA8875_WHITE);
    uint16_t y = box.y + START_DY;
    int n_shown = 0;
    for (int i = ctst_topi; i < n_contests && n_shown < MAX_ROWS; i++) {
        uint16_t w = getTextWidth (contests[i]);
        tft.setCursor (box.x + (box.w-w)/2, y);
        tft.print (contests[i]);
        y += CONTEST_DY;
        n_shown++;
    }

    // draw scroll controls, if needed
    if (OK2SCDW)
        drawScrollDown (box, true);
    if (OK2SCUP)
        drawScrollUp (box, true);
}

/* scroll up, if appropriate to do so now.
 */
static void scrollUp (const SBox &box)
{
    if (OK2SCUP) {
        ctst_topi += 1;
        drawContests (box);
    }
}

/* scroll down, if appropriate to do so now.
 */
static void scrollDown (const SBox &box)
{
    if (OK2SCDW) {
        ctst_topi -= 1;
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

        // reset contests[]
        if (contests) {
            for (int i = 0; i < n_contests; i++)
                free (contests[i]);
            free (contests);
            contests = NULL;
            n_contests = 0;
        }

        // first line is credit
        if (!getTCPLine (contest_client, line, sizeof(line), NULL)) {
            Serial.print (F("no credit lint\n"));
            goto out;
        }
        credit = strdup (line);

        // ok if get at least the credit message
        ok = true;

        // each addition line is a contest
        while (getTCPLine (contest_client, line, sizeof(line), NULL)) {
            // Serial.printf (_FX("Contest %d: %s\n"), n_contests, line);
            scrubContest (line);
            contests = (char **) realloc (contests, (n_contests+1) * sizeof(char*));
            contests[n_contests++] = strdup (line);
        }
    }

out:

    // TODO: this will cause confusion if op is using control buttons
    ctst_topi = 0;

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
    // scroll control? N.B. use a fairly wide region to reduce false menus
    if (s.x >= box.x + 3*box.w/4) { 
        if (s.y >= box.y + SCRUP_DY - SCR_R && s.y <= box.y + SCRUP_DY + SCR_R) {
            scrollUp (box);
        } else if (s.y >= box.y + SCRDW_DY - SCR_R && s.y <= box.y + SCRDW_DY + SCR_R) {
            scrollDown (box);
        }
        // always claim responsibility in this area even if no scroll controls are present
        if (s.y < box.y + PANETITLE_H)
            return (true);
    }

    if (s.y > box.y + PANETITLE_H) {
#if defined(_IS_UNIX)
        // tapping anywhere in contests brings up brower page showing contests
        //   on macos: sudo port install xdg-utils
        //   on ubuntu or RPi: sudo apt install xdg-utils
        //   on redhat: sudo yum install xdg-utils
        static const char cmd[] = "xdg-open https://www.contestcalendar.com/weeklycont.php";
        Serial.printf (_FX("CON: running %s\n"), cmd);
        (void) system (cmd);
#endif
        // stay here in any case
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
