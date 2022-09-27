/* generic modal dialog
 */

#include "HamClock.h"

// basic parameters
// allow setting some/all these in Menu?
#define MENU_TBM        2               // top and bottom margin
#define MENU_RM         2               // right margin
#define MENU_RH         13              // row height
#define MENU_IS         6               // indicator size
#define MENU_BH         14              // button height
#define MENU_BB         4               // ok/cancel button horizontal border
#define MENU_BDX        2               // ok/cancel button text horizontal offset
#define MENU_BDY        3               // ok/cancel button text vertical offset
#define MENU_TIMEOUT    MENU_TO         // timeout, millis
#define MENU_FGC        RA8875_WHITE    // normal foreground color
#define MENU_BGC        RA8875_BLACK    // normal background color
#define MENU_ERRC       RA8875_RED      // error color
#define MENU_BSYC       RA8875_YELLOW   // busy color

static const char ok_label[] = "Ok";
static const char cancel_label[] = "Cancel";

/* draw selector symbol and label for the given menu item in the given pick box
 */
static void menuDrawItem (const MenuItem &mi, const SBox &box, bool draw_label)
{
    // prepare a copy of the label without underscores if drawing
    char *no__copy = NULL;
    if (draw_label && mi.label) {       // label will be NULL for IGNORE and BLANK
        no__copy = strdup (mi.label);
        strncpySubChar (no__copy, mi.label, ' ', '_', strlen(mi.label));
    }

    // draw depending on type

    switch (mi.type) {

    case MENU_BLANK:    // fallthru
    case MENU_IGNORE:
        break;

    case MENU_LABEL:
        if (draw_label) {
            tft.setCursor (box.x + mi.indent, box.y);
            tft.print (no__copy);
        }
        break;

    case MENU_01OFN:    // fallthru
    case MENU_1OFN:
        if (mi.set)
            tft.fillCircle (box.x + mi.indent + MENU_IS/2, box.y + MENU_IS/2, MENU_IS/2, MENU_FGC);
        else {
            tft.fillCircle (box.x + mi.indent + MENU_IS/2, box.y + MENU_IS/2, MENU_IS/2, MENU_BGC);
            tft.drawCircle (box.x + mi.indent + MENU_IS/2, box.y + MENU_IS/2, MENU_IS/2, MENU_FGC);
        }
        if (draw_label) {
            tft.setCursor (box.x + mi.indent + MENU_IS + MENU_IS/2, box.y);
            tft.print (no__copy);
        }
        break;

    case MENU_AL1OFN:   // fallthru
    case MENU_TOGGLE:
        if (mi.set)
            tft.fillRect (box.x + mi.indent, box.y, MENU_IS, MENU_IS, MENU_FGC);
        else {
            tft.fillRect (box.x + mi.indent, box.y, MENU_IS, MENU_IS, MENU_BGC);
            tft.drawRect (box.x + mi.indent, box.y, MENU_IS, MENU_IS, MENU_FGC);
        }
        if (draw_label) {
            tft.setCursor (box.x + mi.indent + MENU_IS + MENU_IS/2, box.y);
            tft.print (no__copy);
        }
        break;
    }

    // clean up
    free ((void*)no__copy);

    // show bounding box for debug
    // drawSBox (box, RA8875_RED);

    // now if just changing indicator
    if (!draw_label)
        tft.drawPR();
}

/* count how many items in the same group and type as ii are set
 */
static int menuCountItemsSet (MenuInfo &menu, int ii)
{
    MenuItem &menu_ii = menu.items[ii];
    int n_set = 0;

    for (int i = 0; i < menu.n_items; i++) {
        if (menu.items[i].type == MENU_IGNORE)
            continue;
        if (menu.items[i].type != menu_ii.type)
            continue;
        if (menu.items[i].group != menu_ii.group)
            continue;
        if (menu.items[i].set)
            n_set++;
    }

    return (n_set);
}

/* turn off all items in same group and type as item ii.
 */
static void menuItemsAllOff (MenuInfo &menu, SBox *boxes, int ii)
{
    MenuItem &menu_ii = menu.items[ii];

    for (int i = 0; i < menu.n_items; i++) {
        if (menu.items[i].type == MENU_IGNORE)
            continue;
        if (menu.items[i].type != menu_ii.type)
            continue;
        if (menu.items[i].group != menu_ii.group)
            continue;
        if (menu.items[i].set) {
            menu.items[i].set = false;
            menuDrawItem (menu.items[i], boxes[i], false);
        }
    }
}

/* operate the given menu until ok, cancel or timeout.
 * caller passes a box we use for ok so they can use it later with menuRedrawOk if needed.
 * return true if op clicked ok else false for all other cases.
 * N.B. menu.menu_b.x/y are required but may be adjusted to prevent edge spill.
 * N.B. incomig menu.menu_b.w is only grown to fit; thus calling with 0 will shrink wrap.
 * N.B. incomig menu.menu_b.h is ignored, we always shrink wrap h.
 * N.B. menu box is erased before returning.
 */
bool runMenu (MenuInfo &menu)
{
    // font
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (MENU_FGC);

    // find number of non-ignore items and expand menu_b.w to fit longest label
    int n_activerows = 0;
    int widest = 0;
    for (int i = 0; i < menu.n_items; i++) {
        MenuItem &mi = menu.items[i];
        if (mi.type != MENU_IGNORE) {
            // check extent
            uint16_t iw = mi.label ? getTextWidth(mi.label) + mi.indent + MENU_IS + MENU_IS/2 : 0;
            if (iw > widest)
                widest = iw;
            // found another non-ignore item
            n_activerows++;
        }
    }

    // width is duplicated for each column plus add a bit of right margin
    if (menu.menu_b.w < widest * menu.n_cols + MENU_RM)
        menu.menu_b.w = widest * menu.n_cols + MENU_RM;

    // number of rows in each column
    int n_rowspercol = (n_activerows + menu.n_cols - 1)/menu.n_cols;

    // set menu height, +1 for ok/cancel
    menu.menu_b.h = MENU_TBM + (n_rowspercol+1)*MENU_RH + MENU_TBM;

    // set ok button size, don't know position yet
    menu.ok_b.w = getTextWidth (ok_label) + MENU_BDX*2;
    menu.ok_b.h = MENU_BH;

    // create cancel button, set size but don't know position yet
    SBox cancel_b;
    cancel_b.w = getTextWidth (cancel_label) + MENU_BDX*2;
    cancel_b.h = MENU_BH;

    // insure menu width accommodates ok and/or cancel buttons
    if (menu.no_cancel) {
        if (menu.menu_b.w < MENU_BB + menu.ok_b.w + MENU_BB)
            menu.menu_b.w = MENU_BB + menu.ok_b.w + MENU_BB;
    } else {
        if (menu.menu_b.w < MENU_BB + menu.ok_b.w + MENU_BB + cancel_b.w + MENU_BB)
            menu.menu_b.w = MENU_BB + menu.ok_b.w + MENU_BB + cancel_b.w + MENU_BB;
    }

    // reposition box if needed to avoid spillage
    if (menu.menu_b.x + menu.menu_b.w > tft.width())
        menu.menu_b.x = tft.width() - menu.menu_b.w - 2;
    if (menu.menu_b.y + menu.menu_b.h > tft.height())
        menu.menu_b.y = tft.height() - menu.menu_b.h - 2;

    // now we can set button positions within the menu box
    if (menu.no_cancel) {
        menu.ok_b.x = menu.menu_b.x + (menu.menu_b.w - menu.ok_b.w)/2;
        menu.ok_b.y = menu.menu_b.y + menu.menu_b.h - MENU_TBM - menu.ok_b.h;
        cancel_b.x = 0;
        cancel_b.y = 0;
    } else {
        menu.ok_b.x = menu.menu_b.x + MENU_BB;
        menu.ok_b.y = menu.menu_b.y + menu.menu_b.h - MENU_TBM - menu.ok_b.h;
        cancel_b.x = menu.menu_b.x + menu.menu_b.w - cancel_b.w - MENU_BB;
        cancel_b.y = menu.menu_b.y + menu.menu_b.h - MENU_TBM - cancel_b.h;
    }

    // ready! prepare new menu box
    fillSBox (menu.menu_b, MENU_BGC);
    drawSBox (menu.menu_b, MENU_FGC);

    // display buttons
    fillSBox (menu.ok_b, MENU_BGC);
    drawSBox (menu.ok_b, MENU_FGC);
    tft.setCursor (menu.ok_b.x+MENU_BDX, menu.ok_b.y+MENU_BDY);
    tft.print (ok_label);
    if (!menu.no_cancel) {
        fillSBox (cancel_b, MENU_BGC);
        drawSBox (cancel_b, MENU_FGC);
        tft.setCursor (cancel_b.x+MENU_BDX, cancel_b.y+MENU_BDY);
        tft.print (cancel_label);
    }

    // display each item in its own pick box
    StackMalloc ibox_mem(menu.n_items*sizeof(SBox));
    SBox *items_b = (SBox *) ibox_mem.getMem();
    uint16_t col_w = (menu.menu_b.w - MENU_RM)/menu.n_cols;
    int vrow_i = 0;                          // visual row, only incremented for non-IGNORE items
    for (int i = 0; i < menu.n_items; i++) {

        MenuItem &mi = menu.items[i];

        // assign item next location and draw unless to be ignored
        if (mi.type != MENU_IGNORE) {
            SBox &ib = items_b[i];

            ib.x = menu.menu_b.x + (vrow_i/n_rowspercol)*col_w;
            ib.y = menu.menu_b.y + MENU_TBM + (vrow_i%n_rowspercol)*MENU_RH;
            ib.w = col_w;
            ib.h = MENU_RH;
            menuDrawItem (mi, ib, true);

            vrow_i++;
        }
    }
    if (vrow_i != n_activerows)                      // sanity check
        fatalError (_FX("menu row %d != %d / %d"), vrow_i, n_activerows, menu.n_items);

    // immediate draw if menu is over map
    tft.drawPR();

    // run
    bool ok = false;
    SCoord tap;
    while (waitForTap (menu.menu_b, NULL, MENU_TIMEOUT, menu.update_clocks, tap)) {

        // check for tap in ok or cancel
        if (inBox (tap, menu.ok_b)) {
            ok = true;
            break;
        }
        if (!menu.no_cancel && inBox (tap, cancel_b)) {
            break;
        }

        // check for tap in active menu items
        for (int i = 0; i < menu.n_items; i++) {
            SBox &ib = items_b[i];
            MenuItem &mi = menu.items[i];

            if (mi.type != MENU_IGNORE && inBox (tap, ib)) {

                // implement each type of behavior
                switch (mi.type) {
                case MENU_LABEL:        // fallthru
                case MENU_BLANK:        // fallthru
                case MENU_IGNORE:
                    break;

                case MENU_1OFN:
                    // ignore if already set, else turn this one on and all others in this group off
                    if (!mi.set) {
                        menuItemsAllOff (menu, items_b, i);
                        mi.set = true;
                        menuDrawItem (mi, ib, false);
                    }
                    break;

                case MENU_01OFN:
                    // turn off if set, else turn this one on and all others in this group off
                    if (mi.set) {
                        mi.set = false;
                        menuDrawItem (mi, ib, false);
                    } else {
                        menuItemsAllOff (menu, items_b, i);
                        mi.set = true;
                        menuDrawItem (mi, ib, false);
                    }
                    break;

                case MENU_AL1OFN:
                    // turn on unconditionally, but turn off only if not the last one
                    if (!mi.set) {
                        mi.set = true;
                        menuDrawItem (mi, ib, false);
                    } else {
                        if (menuCountItemsSet (menu, i) > 1) {
                            mi.set = false;
                            menuDrawItem (mi, ib, false);
                        }
                    }
                    break;

                case MENU_TOGGLE:
                    // uncondition change
                    mi.set = !mi.set;
                    menuDrawItem (mi, ib, false);
                    break;
                }

                // tap found
                break;
            }
        }
    }

    drainTouch();

    // erase in prep for caller to restore covered content
    fillSBox (menu.menu_b, RA8875_BLACK);

    // record settings
    Serial.printf (_FX("Menu result after %s:\n"), ok ? "Ok" : "Cancel");
    for (int i = 0; i < menu.n_items; i++) {
        MenuItem &mi = menu.items[i];
        switch (mi.type) {
        case MENU_1OFN:         // fallthru
        case MENU_01OFN:        // fallthru
        case MENU_AL1OFN:       // fallthru
        case MENU_TOGGLE:
            Serial.printf (_FX("  %-15s g%d s%d\n"), mi.label ? mi.label : "", mi.group, mi.set);
            break;
        case MENU_LABEL:        // fallthru
        case MENU_IGNORE:       // fallthru
        case MENU_BLANK:
            break;
        }
    }

    return (ok);
}

/* redraw the given ok box in the given visual state.
 * used to allow caller to provide busy or error feedback.
 * N.B. we assume ok_b is same as passed to runMenu and remains unchanged since its return.
 */
void menuRedrawOk (SBox &ok_b, MenuOkState oks)
{
    switch (oks) {
    case MENU_OK_OK:
        tft.setTextColor (MENU_FGC);
        fillSBox (ok_b, MENU_BGC);
        drawSBox (ok_b, MENU_FGC);
        break;
    case MENU_OK_BUSY:
        tft.setTextColor (MENU_BGC);
        fillSBox (ok_b, MENU_BSYC);
        drawSBox (ok_b, MENU_FGC);
        break;
    case MENU_OK_ERR:
        tft.setTextColor (MENU_BGC);
        fillSBox (ok_b, MENU_ERRC);
        drawSBox (ok_b, MENU_FGC);
        break;
    }

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (ok_b.x+MENU_BDX, ok_b.y+MENU_BDY);
    tft.print (ok_label);
    tft.drawPR();
}
