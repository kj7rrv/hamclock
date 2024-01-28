/* handy routines to draw the scroll controls used in several panes.
 */

#include "HamClock.h"

#define SCR_DX          (PLOTBOX_W-26)          // x offset from box left to center of both scroll arrows
#define SCRUP_DY        9                       // y offset from box top to center of up arrow
#define SCRDW_DY        23                      // y offset from box top to center of down arrow
#define SCR_W           6                       // scrol arrow width
#define SCR_H           10                      // scrol arrow height


/* draw, else erase, the up scroll control;
 */
void drawScrollUp (const SBox &box, uint16_t color, int n, bool draw)
{
        const uint16_t x0 = box.x + SCR_DX;                   // top point
        const uint16_t y0 = box.y + SCRUP_DY - SCR_H/2;
        const uint16_t x1 = box.x + SCR_DX - SCR_W/2;         // LL
        const uint16_t y1 = box.y + SCRUP_DY + SCR_H/2;
        const uint16_t x2 = box.x + SCR_DX + SCR_W/2;         // LR
        const uint16_t y2 = box.y + SCRUP_DY + SCR_H/2;

        tft.fillRect (x2+1, y0+1, box.x + box.w - x2 - 2, SCR_H, RA8875_BLACK);
        // tft.drawRect (x2+1, y0+1, box.x + box.w - x2 - 2, SCR_H, RA8875_RED);

        if (draw) {
            tft.setCursor (x2+3, y0+2);
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            tft.setTextColor (color);
            tft.print (n);
        }

        tft.fillTriangle (x0, y0, x1, y1, x2, y2, draw ? color : RA8875_BLACK);
}

/* draw, else erase, the down scroll control.
 */
void drawScrollDown (const SBox &box, uint16_t color, int n, bool draw)
{
        const uint16_t x0 = box.x + SCR_DX - SCR_W/2;         // UL
        const uint16_t y0 = box.y + SCRDW_DY - SCR_H/2;
        const uint16_t x1 = box.x + SCR_DX + SCR_W/2;         // UR
        const uint16_t y1 = box.y + SCRDW_DY - SCR_H/2;
        const uint16_t x2 = box.x + SCR_DX;                   // bottom point
        const uint16_t y2 = box.y + SCRDW_DY + SCR_H/2;

        tft.fillRect (x1+1, y0+1, box.x + box.w - x1 - 2, SCR_H, RA8875_BLACK);
        // tft.drawRect (x1+1, y0+1, box.x + box.w - x1 - 2, SCR_H, RA8875_RED);

        if (draw) {
            tft.setCursor (x1+3, y0+2);
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            tft.setTextColor (color);
            tft.print (n);
        }

        tft.fillTriangle (x0, y0, x1, y1, x2, y2, draw ? color : RA8875_BLACK);
}

bool checkScrollUpTouch (const SCoord &s, const SBox &b)
{
    return (s.x > b.x + 3*b.w/4 && s.y <= b.y + SCRUP_DY + SCR_H/2);
}

bool checkScrollDownTouch (const SCoord &s, const SBox &b)
{
    return (s.x > b.x + 3*b.w/4 && s.y > b.y + SCRUP_DY + SCR_H/2 && s.y < b.y + SCRDW_DY + SCR_H/2);
}


/* convert any upper case letter in str to lower case IN PLACE
 */
void strtolower (char *str)
{
        for (char c = *str; c != '\0'; c = *++str)
            if (isupper(c))
                *str = tolower(c);
}

/* convert any lower case letter in str to upper case IN PLACE
 */
void strtoupper (char *str)
{
        for (char c = *str; c != '\0'; c = *++str)
            if (islower(c))
                *str = toupper(c);
}

