/* simple means to ask for a password for a given category, set by -p
 * not ESP
 */

#include "HamClock.h"

// global means to skip password, eg, for restful and demo commands.
bool bypass_pw;

#if defined(_IS_ESP8266)

/* dummy, always true
 * ESP only
 */
bool askPasswd (const char *category, bool restore)
{
    (void) category;
    (void) restore;
    return (true);
}

#else

// layout
#define FW      21                      // widest char width
#define FH      35                      // char height
#define TC      RA8875_WHITE            // title color
#define CC      RA8875_GREEN            // cursor color
#define VC      RA8875_WHITE            // character color
#define CG      4                       // cursor gap
#define HM      50                      // horizontal margin on each end of password
#define UC      GRAY                    // char position indicator color
#define CD      9                       // cursor descent
#define TO      30000                   // timeout, millis
#define NP      ((800 - 2*HM)/FW)       // max pw length, sans EOS
#define TY      (480/4)                 // title baseline y
#define PY      (2*480/3)               // password baseline y
#define NT      3                       // n tries

#define HS_X    600                     // hide/show x
#define HS_Y    350                     // hide/show y
#define HS_W    100                     // hide/show width
#define HS_H    50                      // hide/show height
#define HS_C    RGB565(240,50,50)       // hide/show color
#define HS_V    '*'                     // pw char when hiding

#define MX      200                     // message Y
#define MY      400                     // message Y
#define MW      (800-2*MX)              // message width
#define MC      RA8875_RED              // message color

/* given character baseline position and value: erase cursor, draw c, draw cursor at next location
 */
static void drawChar (uint16_t x, uint16_t y, char c)
{
    // find c width in order to center
    char s[2] = {c, '\0'};
    uint16_t c_w = getTextWidth(s);

    tft.drawLine (x + CG/2, y + CD, x + FW - CG/2, y + CD, UC);
    tft.setCursor (x + (FW - c_w)/2, y);
    tft.setTextColor (VC);
    tft.print (c);
    x += FW;
    tft.drawLine (x + CG/2, y + CD, x + FW - CG/2, y + CD, CC);
}

/* given character baseline position: erase cursor, erase char at prev position, draw cursor at prev position.
 */
static void eraseChar (uint16_t x, uint16_t y)
{
    tft.drawLine (x + CG/2, y + CD, x + FW - CG/2, y + CD, UC);
    x -= FW;
    tft.fillRect (x, y - FH + CD, FW+1, FH, RA8875_BLACK);
    tft.drawLine (x + CG/2, y + CD, x + FW - CG/2, y + CD, CC);
}

static void drawHideShow (const SBox &b, bool hide)
{
    drawStringInBox (hide ? "Show" : "Hide", b, hide, HS_C);
}

/* ask for a password for the given category.
 * return true if no such category, restful or password matches.
 * not ESP
 */
bool askPasswd (const char *category, bool restore)
{
    // free pass if no such category or running a restful command
    if (!testPassword (category, NULL) || bypass_pw)
        return (true);

    // use entire screen. N.B. requires us to call tft.drawPR() to update
    eraseScreen();

    // draw title
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    char title[100];
    snprintf (title, sizeof(title), "Type password for %s:", category);
    uint16_t t_w = getTextWidth(title);
    uint16_t t_x = (tft.width() - t_w)/2;
    tft.setTextColor (TC);
    tft.setCursor (t_x, TY);
    tft.print (title);

    // set font for all subsequent text
    selectFontStyle (LIGHT_FONT, SMALL_FONT);

    // draw instructions
    const char info[] = "Enter to accept, ESC to cancel";
    uint16_t info_w = getTextWidth(info);
    uint16_t info_x = (tft.width() - info_w)/2;
    tft.setCursor (info_x, TY+FH);
    tft.print (info);

    // hide/show control
    SBox hs_b = {HS_X, HS_Y, HS_W, HS_H};
    bool hide = true;
    drawHideShow (hs_b, hide);

    // candidate password and initial cursor position
    char pw_buf[NP+1];
    int cur_pos = 0;

    // draw each char position
    for (int i = 0; i < NP; i++)
        tft.drawLine (HM + i*FW + CG/2, PY + CD, HM + i*FW + FW - CG/2, PY + CD, i == cur_pos ? CC : UC);

    // now user has up to NT tries or until cancels
    bool cancelled = false;
    bool pw_ok = false;
    for (int try_i = 1; !cancelled && !pw_ok && try_i <= NT; try_i++) {

        // show try number
        if (try_i > 1) {
            char try_buf[50];
            if (try_i == NT)
                snprintf (try_buf, sizeof(try_buf), "Incorrect. Last try.");
            else
                snprintf (try_buf, sizeof(try_buf), "Incorrect. Try %d of %d.", try_i, NT);
            tft.fillRect (MX, MY-FH, MW, FH+CD, RA8875_BLACK);
            tft.setCursor (MX, MY);
            tft.setTextColor (MC);
            tft.print (try_buf);
        }

        // configure waitForUser()
        bool enter = false;
        SCoord s;
        char kbc;
        SBox all = {0, 0, tft.width(), tft.height()};   // full screen
        UserInput ui = { all, NULL, false, TO, false, s, kbc };

        // run until enter or cancel
        do {

            // reset
            enter = false;
            cancelled = false;

            // wait for user to do something or time out
            if (!waitForUser(ui)) {
                cancelled = true;
                continue;
            }

            if (kbc) {

                switch (kbc) {
                case '\n':      // fallthru
                case '\r':
                    enter = true;
                    break;
                case 27:        // esc
                    cancelled = true;
                    break;
                case '\b':      // fallthru
                case 127:       // del
                    if (cur_pos > 0) {
                        eraseChar (HM+FW*cur_pos, PY);
                        cur_pos -= 1;
                    }
                    break;
                default:
                    if (isprint (kbc) && cur_pos < NP) {
                        pw_buf[cur_pos] = kbc;
                        drawChar (HM+FW*cur_pos, PY, hide ? HS_V : kbc);
                        cur_pos += 1;
                    }
                    break;
                }

            } if (inBox (s, hs_b)) {

                // toggle and redraw
                hide = !hide;

                drawHideShow (hs_b, hide);
                for (int i = cur_pos; i > 0; --i)
                    eraseChar (HM+FW*i, PY);
                for (int i = 0; i < cur_pos; i++)
                    drawChar (HM+FW*i, PY, hide ? HS_V : pw_buf[i]);

            }


        } while (!cancelled && !enter);

        if (enter) {
            pw_buf[cur_pos] = '\0';
            pw_ok = testPassword (category, pw_buf);
        }
    }

    if (restore)
        initScreen();

    Serial.printf (_FX("password for %s %s\n"), category, pw_ok ? _FX("ok") : _FX("failed"));

    return (pw_ok);
}

#endif // !_IS_ESP8266
