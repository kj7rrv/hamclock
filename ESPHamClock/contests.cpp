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

// URL to access info
static const char contest_page[] PROGMEM = "/contests/contests.txt";    // just titles
static const char contest3_page[] PROGMEM = "/contests/contests3.txt";  // with dates

static char **contests;                         // malloced list of malloced strings
static int max_contests;                        // max n contest lines
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

static ScrollContest cts_ss;                    // scrolling context


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

/* scrub the given line IN PLACE to fit within the given box
 */
static void scrubContestLine (char *line, const SBox &box)
{
    // look for a few common phases
    char *phrase;
    if ((phrase = strstr (line, _FX("Parks on the Air"))) != NULL)
        strcpy (phrase, _FX("POTA"));

    // keep chopping off at successive right-most space until fits within box
    uint16_t lw;                                        // line width in pixels
    while ((lw = getTextWidth (line)) >= box.w) {
        char *right_space = strrchr (line, ' ');
        if (right_space)
            *right_space = '\0';                        // EOS now at right-most space
        else
            break;                                      // still too long but no more spaces to chop
    }

    // always chop off any trailing punct char
    size_t ll = strlen (line);                          // line length in chars
    if (ll > 0 && ispunct(line[ll-1]))
        line[--ll] = '\0';

    // well just hack off if still too long
    while (getTextWidth (line) >= box.w)
        line[--ll] = '\0';
}


/* collect Contest info into the contests[] array and show in the given pane box
 */
bool updateContests (const SBox &box)
{
    WiFiClient ctst_scclient;
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
        free (contests);
        contests = NULL;
        free (credit);
        credit = NULL;

        // init scroller and max data size. max_vis must be even if showing date
        cts_ss.init ((box.h - START_DY)/CONTEST_DY, 0, 0);   // max_vis, top_vis, n_data
        if (show_date && (cts_ss.max_vis % 2) != 0)
            cts_ss.max_vis -= 1;
    #if defined(_IS_ESP8266)
        max_contests = cts_ss.max_vis;
    #else
        max_contests = cts_ss.max_vis + nMoreScrollRows();
    #endif

        // first line is credit
        char line[100];
        if (!getTCPLine (ctst_scclient, line, sizeof(line), NULL)) {
            Serial.print (F("no credit line\n"));
            goto out;
        }
        credit = strdup (line);

        // consider transaction is ok if get at least the credit message
        ok = true;

        // set font for scrubContestLine()
        selectFontStyle (LIGHT_FONT, FAST_FONT);

        // if show_date each pair of lines is contest then date, else all lines are contests
        while (cts_ss.n_data < max_contests && getTCPLine (ctst_scclient, line, sizeof(line), NULL)) {
            // Serial.printf (_FX("Contest %d: %s\n"), cts_ss.n_data, line);
            scrubContestLine (line, box);
            contests = (char **) realloc (contests, (cts_ss.n_data+1) * sizeof(char*));
            if (!contests)
                fatalError (_FX("No memory for %d contests"), cts_ss.n_data+1);
            if ((contests[cts_ss.n_data++] = strdup (line)) == NULL)
                fatalError (_FX("No memory for d contest %d"), cts_ss.n_data);
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
        openURL ("https://www.contestcalendar.com/weeklycont.php");

        return (true);                                  // retain pane regardless
#endif

    } else {
        // toggle showing date and save
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
