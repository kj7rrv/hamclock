/* handy class to manage the scroll controls used in several panes.
 *
 * assumes data array is sorted oldest first. displayed list can either show newest entry
 * on top or bottom row, depending on scrollTopToBottom(). If newest is shown at the bottom, then 
 * scrolling Up means show Older entries; if newest is shown on top, then Up means show Newer entries.
 *
 * state variables:
 *
 *  max_vis: maximum rows in the displayed list
 *  top_vis: index into the data array containing the newest entry to be displayed
 *  n_data:  the number of entries in the data array
 *
 *  example: n_data 7, max_vis 3, scrolled to show newest data
 *
 *    if scrollTopToBottom() is true:
 *  
 *    top_vis  6 |  A  |   ------|        display row 0, ie, top row
 *             5 |  B  |       max_vis    display row 1
 *             4 |  C  |   ------|        display row 2
 *             3 |  D  |   
 *             2 |  E  |   
 *             1 |  F  | 
 *             0 |  G  |   oldest data array entry at index 0
 *               -------
 *  
 *    else scrollTopToBottom() is falae:
 *  
 *    top_vis  6 |  A  |   ------|        display row 2
 *             5 |  B  |       max_vis    display row 1
 *             4 |  C  |   ------|        display row 0, ie, top row
 *             3 |  D  |   
 *             2 |  E  |   
 *             1 |  F  | 
 *             0 |  G  |   oldest data array entry at index 0
 *               -------
 *
 */

#include "HamClock.h"


// control layout geometry
#define SCR_DX          24                      // x offset back from box right to center of scroll arrows
#define SCRUP_DY        9                       // y offset from box top to center of up arrow
#define SCRDW_DY        23                      // y offset from box top to center of down arrow
#define SCR_W           6                       // scroll arrow width
#define SCR_H           10                      // scroll arrow height


/* draw or erase the up scroll control as needed.
 */
void ScrollState::drawScrollUpControl (const SBox &box, uint16_t color) const
{
        bool draw = okToScrollUp();

        // up arrow

        const uint16_t scr_dx = box.w - SCR_DX;                 // center
        const uint16_t x0 = box.x + scr_dx;                     // top point
        const uint16_t y0 = box.y + SCRUP_DY - SCR_H/2;
        const uint16_t x1 = box.x + scr_dx - SCR_W/2;           // LL
        const uint16_t y1 = box.y + SCRUP_DY + SCR_H/2;
        const uint16_t x2 = box.x + scr_dx + SCR_W/2;           // LR
        const uint16_t y2 = box.y + SCRUP_DY + SCR_H/2;

        tft.fillRect (x2+1, y0+1, box.x + box.w - x2 - 2, SCR_H, RA8875_BLACK);
        // tft.drawRect (x2+1, y0+1, box.x + box.w - x2 - 2, SCR_H, RA8875_RED);

        if (draw) {
            tft.setCursor (x2+3, y0+2);
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            tft.setTextColor (color);
            tft.print (nMoreAbove());
        }

        tft.fillTriangle (x0, y0, x1, y1, x2, y2, draw ? color : RA8875_BLACK);
}

/* draw, else erase, the down scroll control and associated count n.
 */
void ScrollState::drawScrollDownControl (const SBox &box, uint16_t color) const
{
        bool draw = okToScrollDown();

        // down arrow 

        const uint16_t scr_dx = box.w - SCR_DX;                 // center
        const uint16_t x0 = box.x + scr_dx - SCR_W/2;           // UL
        const uint16_t y0 = box.y + SCRDW_DY - SCR_H/2;
        const uint16_t x1 = box.x + scr_dx + SCR_W/2;           // UR
        const uint16_t y1 = box.y + SCRDW_DY - SCR_H/2;
        const uint16_t x2 = box.x + scr_dx;                     // bottom point
        const uint16_t y2 = box.y + SCRDW_DY + SCR_H/2;

        tft.fillRect (x1+1, y0+1, box.x + box.w - x1 - 2, SCR_H, RA8875_BLACK);
        // tft.drawRect (x1+1, y0+1, box.x + box.w - x1 - 2, SCR_H, RA8875_RED);

        if (draw) {
            tft.setCursor (x1+3, y0+2);
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            tft.setTextColor (color);
            tft.print (nMoreBeneath());
        }

        tft.fillTriangle (x0, y0, x1, y1, x2, y2, draw ? color : RA8875_BLACK);
}




/* return whether tap at s within box b means to scroll up
 */
bool ScrollState::checkScrollUpTouch (const SCoord &s, const SBox &b) const
{
    return (s.x > b.x + b.w - SCR_DX - SCR_W && s.y <= b.y + SCRUP_DY + SCR_H/2);
}

/* return whether tap at s within box b means to scroll down
 */
bool ScrollState::checkScrollDownTouch (const SCoord &s, const SBox &b) const
{
    return (s.x > b.x + b.w - SCR_DX - SCR_W
                && s.y > b.y + SCRUP_DY + SCR_H/2 && s.y < b.y + SCRDW_DY + SCR_H/2);
}


/* move top_vis towards older data, ie, towards the beginning of the data array
 */
void ScrollState::moveTowardsOlder()
{
    top_vis -= max_vis - 1;         // leave one for context
    int llimit = max_vis - 1;
    if (top_vis < llimit)
        top_vis = llimit;
}

/* move top_vis towards newer data, ie, towards the end of the data array
 */
void ScrollState::moveTowardsNewer()
{
    top_vis += max_vis - 1;         // leave one for context
    int ulimit = n_data - 1;
    if (top_vis > ulimit)
        top_vis = ulimit;
}



/* modify top_vis to expose data below the visible list
 */
void ScrollState::scrollDown (void)
{
    if (scrollTopToBottom())
        moveTowardsOlder();
    else
        moveTowardsNewer();
}

/* modify top_vis to expose data above the visible list
 */
void ScrollState::scrollUp (void)
{
    if (scrollTopToBottom())
        moveTowardsNewer();
    else
        moveTowardsOlder();
}




/* return whether there is more data beneath the displayed list
 */
bool ScrollState::okToScrollDown (void) const
{
    return (nMoreBeneath() > 0);

}

/* return whether there is more data aboce the displayed list
 */
bool ScrollState::okToScrollUp (void) const
{
    return (nMoreAbove() > 0);
}





/* return the additional number of spots not shown above the current list
 */
int ScrollState::nMoreAbove (void) const
{
    int n;
    if (scrollTopToBottom())
        n = n_data - top_vis - 1;
    else
        n = top_vis - max_vis + 1;
    return (n);
}

/* return the additional number of spots not shown below the current list
 */
int ScrollState::nMoreBeneath (void) const
{
    int n;
    if (scrollTopToBottom())
        n = top_vis - max_vis + 1;
    else
        n = n_data - top_vis - 1;
    return (n);
}

/* scroll to position the newest entry at the head of the list.
 */
void ScrollState::scrollToNewest (void)
{
    top_vis = n_data - 1;
    if (top_vis < 0)
        top_vis = 0;
}

/* given a display row index, which always start with 0 on top, find the corresponding data array index.
 * return whether actually within range.
 */
bool ScrollState::findDataIndex (int display_row, int &array_index) const
{
    int i;

    if (scrollTopToBottom())
        i = top_vis - display_row;
    else
        i = top_vis - max_vis + 1 + display_row;

    bool ok = i >= 0 && i < n_data;
    if (ok)
        array_index = i;

    return (ok);
}

/* pass back the min and max array indices currently visible and return total row count.
 */
int ScrollState::getVisIndices (int &min_i, int &max_i) const
{
    max_i = top_vis;                            // list "head" is always newest being displayed
    min_i = top_vis - max_vis + 1;              // list "tail" is oldest
    if (min_i < 0)                              // but might not be enough to fill the list
        min_i = 0;
    int n = max_i - min_i + 1;                  // inclusive
    if (n > n_data)                             // check for overflow
        n = 0;
    return (n);
}

/* given a data array index ala getVisIndices, return the display row number starting with 0 on top
 */
int ScrollState::getDisplayRow (int array_index) const
{
    int i;

    if (scrollTopToBottom())
        i = top_vis - array_index;
    else
        i = array_index - (top_vis - (max_vis-1));

    return (i);
}
