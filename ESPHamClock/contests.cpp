/* handle contest retrieval and display.
 * 3.00 added option to show second line of dates.
 */

#include "HamClock.h"



#define CONTEST_COLOR   RGB565(205,91,69)       // X11 coral3
#define TO_COLOR        RA8875_BLACK            // titles-only background
#define TD_COLOR        CONTEST_COLOR           // titles-with-dates background
#define CREDITS_Y0      31                      // dy of credits row
#define START_DY        47                      // dy of first row
#define CONTEST_DY      12                      // dy of each successive row
#define MAX_CTST_LEN    26                      // max contest entry length, not counting EOS
#define MAX_VIS         ((PLOTBOX_H - START_DY)/CONTEST_DY)     // max visible rows

// require even rows for title/date combos
#if (MAX_VIS%2) != 0
#error contests MAX_VIS must be even
#endif

static const char contest_page[] PROGMEM = "/contests/contests.txt";    // just titles
static const char contest3_page[] PROGMEM = "/contests/contests3.txt";  // with dates

// current collection
#if defined(_IS_ESP8266)
#define MAX_CONTESTS    (2*MAX_VIS)             // limit ram use on ESP
#else
#define MAX_CONTESTS    50                      // even UNIX doesn't want scrolling crazy long
#endif

static char *contests[MAX_CONTESTS];            // malloced strings
static char *credit;                            // malloced credit line
static uint8_t show_date;                       // whether to show 2nd line with date

// subclass to override some methods that depend on show_date
class ScrollContest : public ScrollState
{
    public:

        using ScrollState:: ScrollState;

        int nMoreAbove(void) const {
            int n = ScrollState::nMoreAbove();
            if (show_date)
                n /= 2;
            return n;
        }

        int nMoreBeneath(void) const {
            int n = ScrollState::nMoreBeneath();
            if (show_date)
                n /= 2;
            return n;
        }

        void scrollUp(void) {
            ScrollState::scrollUp();
            if (show_date && (max_vis & 1) == 0) {
                // scrolled by max_vis-1 so need 1 more to make it even
                if (ScrollState::nMoreAbove() > 0) {
                    if (scrollTopToBottom())
                        top_vis += 1;
                    else
                        top_vis -= 1;
                }

            }
        }

        void scrollDown (void) {
            ScrollState::scrollDown();
            if (show_date && (max_vis & 1) == 0) {
                // scrolled by max_vis-1 so need 1 more to make it even
                if (ScrollState::nMoreBeneath() > 0) {
                    if (scrollTopToBottom())
                        top_vis -= 1;
                    else
                        top_vis += 1;
                }
            }
        }
};

static ScrollContest cts_ss = {MAX_VIS, 0, 0};    // scrolling context

/* draw contests[] in the given pane box
 */
static void drawContestsPane (const SBox &box)
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
    uint16_t y0 = box.y + START_DY;
    int min_i, max_i;
    if (cts_ss.getVisIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            int r = cts_ss.getDisplayRow(i);
            uint16_t y = y0 + r * CONTEST_DY;
            // faint bg
            if (!show_date || (r & 2) == 0)
                tft.fillRect (box.x+1, y-2, box.w-2, CONTEST_DY, TO_COLOR);
            else
                tft.fillRect (box.x+1, y-2, box.w-2, CONTEST_DY, TD_COLOR);
            uint16_t w = getTextWidth (contests[i]);
            tft.setCursor (box.x + (box.w-w)/2, y);
            tft.print (contests[i]);
        }
    }

    // draw scroll controls, if needed
    cts_ss.drawScrollDownControl (box, CONTEST_COLOR);
    cts_ss.drawScrollUpControl (box, CONTEST_COLOR);
}

/* scroll up, if appropriate to do so now.
 */
static void scrollContestUp (const SBox &box)
{
    if (cts_ss.okToScrollUp()) {
        cts_ss.scrollUp();
        drawContestsPane (box);
    }
}

/* scroll down, if appropriate to do so now.
 */
static void scrollContestDown (const SBox &box)
{
    if (cts_ss.okToScrollDown()) {
        cts_ss.scrollDown();
        drawContestsPane (box);
    }
}

/* scrub the given line IN PLACE to fit within MAX_CTST_LEN chars
 */
static void scrubContestLine (char *line)
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
    WiFiClient ctst_scclient;
    char line[100];
    bool ok = false;

    // get date state
    if (!NVReadUInt8 (NV_CONTESTS, &show_date)) {
        show_date = false;
        NVWriteUInt8 (NV_CONTESTS, show_date);
    }

    // query appropriate list
    char page[sizeof(contest_page)+10];
    strcpy_P (page, show_date ? contest3_page : contest_page);

    // download and load contests[]
    Serial.println(page);
    resetWatchdog();
    if (wifiOk() && ctst_scclient.connect(backend_host, backend_port)) {

        // look alive
        resetWatchdog();
        updateClocks(false);

        // fetch page and skip header
        httpHCPGET (ctst_scclient, backend_host, page);
        if (!httpSkipHeader (ctst_scclient)) {
            Serial.print (F("contest download failed\n"));
            goto out;
        }

        // reset contests and credit
        for (int i = 0; i < cts_ss.n_data; i++)
            free (contests[i]);
        cts_ss.n_data = 0;
        cts_ss.top_vis = 0;
        free (credit);
        credit = NULL;

        // first line is credit
        if (!getTCPLine (ctst_scclient, line, sizeof(line), NULL)) {
            Serial.print (F("no credit line\n"));
            goto out;
        }
        credit = strdup (line);

        // ok if get at least the credit message
        ok = true;

        // if show_date each pair of lines is contest then date, else all lines are contests
        while (cts_ss.n_data < MAX_CONTESTS && getTCPLine (ctst_scclient, line, sizeof(line), NULL)) {
            // Serial.printf (_FX("Contest %d: %s\n"), cts_ss.n_data, line);
            scrubContestLine (line);
            contests[cts_ss.n_data++] = strdup (line);
        }

        // file is newest-first but ScrollState expects oldest-first.
        // N.B. this also swaps each name/date pair order if show_date
        for (int i = 0; i < cts_ss.n_data/2; i++) {
            int opposite_i = cts_ss.n_data - i - 1;
            char *tmp = contests[i];
            contests[i] = contests[opposite_i];
            contests[opposite_i] = tmp;
        }

        // above put name into newest position which must be swapped with date if showing bottom-up
        if (show_date && !scrollTopToBottom()) {
            for (int i = 0; i < cts_ss.n_data; i += 2) {
                char *tmp = contests[i];
                contests[i] = contests[i+1];
                contests[i+1] = tmp;
            }
        }
    }

out:

    if (ok) {
        Serial.printf (_FX("CTS: Found %d\n"), cts_ss.n_data);
        cts_ss.scrollToNewest();
        drawContestsPane (box);
    } else {
        plotMessage (box, CONTEST_COLOR, _FX("Contests error"));
    }

    ctst_scclient.stop();

    return (ok);
}

/* return true if user is interacting with the contest pane, false if wants to change pane.
 * N.B. we assume s is within box
 */
bool checkContestsTouch (const SCoord &s, const SBox &box)
{
    // scroll control?
    if (s.y < box.y + PANETITLE_H) {

        if (cts_ss.checkScrollUpTouch (s, box)) {
            scrollContestUp (box);
            return (true);
        }
        if (cts_ss.checkScrollDownTouch (s, box)) {
            scrollContestDown (box);
            return (true);
        }

#if defined(_IS_UNIX)

    } else if (s.y > box.y + 3*box.h/4 && s.x > box.x + 3*box.w/4) {

        // tapping near bottom right in contests brings up browser page showing contests
        //   on macos: sudo port install xdg-utils
        //   on ubuntu or RPi: sudo apt install xdg-utils
        //   on redhat: sudo yum install xdg-utils
        static const char cmd[] = "xdg-open https://www.contestcalendar.com/weeklycont.php";
        if (system (cmd))
            Serial.printf (_FX("CTS: fail: %s\n"), cmd);
        else
            Serial.printf (_FX("CTS: ok: %s\n"), cmd);
        return (true);                                  // retain pane regardless
#endif

    } else if (s.y > box.y + box.h/3) {
        // toggle and save new option
        show_date = !show_date;
        NVWriteUInt8 (NV_CONTESTS, show_date);
        // refresh pane to show new state
        updateContests(box);
        return (true);                                  // retain pane regardless
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
    return (cts_ss.n_data);
}
