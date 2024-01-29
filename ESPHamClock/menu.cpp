/* generic modal dialog
 */

#include "HamClock.h"

// basic parameters
// allow setting some/all these in Menu?
#define MENU_TBM        2               // top and bottom margin
#define MENU_RM         2               // right margin
#define MENU_RH         10              // row height
#define MENU_IS         6               // indicator size
#define MENU_BB         4               // ok/cancel button horizontal border
#define MENU_BDX        2               // ok/cancel button text horizontal offset
#define MENU_BDY        1               // ok/cancel button text vertical offset
#define MENU_TIMEOUT    MENU_TO         // timeout, millis
#define MENU_FGC        RA8875_WHITE    // normal foreground color
#define MENU_BGC        RA8875_BLACK    // normal background color
#define MENU_ERRC       RA8875_RED      // error color
#define MENU_BSYC       RA8875_YELLOW   // busy color
#define MENU_FOCC       RA8875_GREEN    // focus color

static const char ok_label[] = "Ok";
static const char cancel_label[] = "Cancel";


/* draw selector symbol and label for the given menu item in the given pick box.
 * optionally indicate this item has the keyboard focus.
 */
static void menuDrawItem (const MenuItem &mi, const SBox &pb, bool draw_label, bool kb_focus)
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
            tft.setCursor (pb.x + mi.indent, pb.y+2);
            tft.print (no__copy);
        }
        drawSBox (pb, kb_focus ? MENU_FOCC : MENU_BGC);
        break;

    case MENU_01OFN:    // fallthru
    case MENU_1OFN:
        if (mi.set)
            tft.fillCircle (pb.x + mi.indent + MENU_IS/2, pb.y + MENU_RH/2, MENU_IS/2, MENU_FGC);
        else {
            tft.fillCircle (pb.x + mi.indent + MENU_IS/2, pb.y + MENU_RH/2, MENU_IS/2, MENU_BGC);
            tft.drawCircle (pb.x + mi.indent + MENU_IS/2, pb.y + MENU_RH/2, MENU_IS/2, MENU_FGC);
        }
        if (draw_label) {
            tft.setCursor (pb.x + mi.indent + MENU_IS + MENU_IS/2, pb.y+2);
            tft.print (no__copy);
        }
        drawSBox (pb, kb_focus ? MENU_FOCC : MENU_BGC);
        break;

    case MENU_AL1OFN:   // fallthru
    case MENU_TOGGLE:
        if (mi.set)
            tft.fillRect (pb.x + mi.indent, pb.y + (MENU_RH-MENU_IS)/2, MENU_IS, MENU_IS, MENU_FGC);
        else {
            tft.fillRect (pb.x + mi.indent, pb.y + (MENU_RH-MENU_IS)/2, MENU_IS, MENU_IS, MENU_BGC);
            tft.drawRect (pb.x + mi.indent, pb.y + (MENU_RH-MENU_IS)/2, MENU_IS, MENU_IS, MENU_FGC);
        }
        if (draw_label) {
            tft.setCursor (pb.x + mi.indent + MENU_IS + MENU_IS/2, pb.y+2);
            tft.print (no__copy);
        }
        drawSBox (pb, kb_focus ? MENU_FOCC : MENU_BGC);
        break;
    }

    // clean up
    free ((void*)no__copy);

    // show bounding box for debug
    // drawSBox (pb, RA8875_RED);

    // draw now if just changing indicator over map_b
    if (!draw_label && boxesOverlap (pb, map_b))
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
static void menuItemsAllOff (MenuInfo &menu, SBox *pick_boxes, int ii)
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
            menuDrawItem (menu.items[i], pick_boxes[i], false, false);
        }
    }
}



/* engage a action at the specified pick index.
 * from_kb indicates whether to highlight the new focus item.
 */
static void updateMenu (MenuInfo &menu, SBox *pick_boxes, int pick_i, bool from_kb)
{
    SBox &pb = pick_boxes[pick_i];
    MenuItem &mi = menu.items[pick_i];

    switch (mi.type) {
    case MENU_LABEL:        // fallthru
    case MENU_BLANK:        // fallthru
    case MENU_IGNORE:
        break;

    case MENU_1OFN:
        // ignore if already set, else turn this one on and all others in this group off
        if (!mi.set) {
            menuItemsAllOff (menu, pick_boxes, pick_i);
            mi.set = true;
            menuDrawItem (mi, pb, false, from_kb);
        }
        break;

    case MENU_01OFN:
        // turn off if set, else turn this one on and all others in this group off
        if (mi.set) {
            mi.set = false;
            menuDrawItem (mi, pb, false, from_kb);
        } else {
            menuItemsAllOff (menu, pick_boxes, pick_i);
            mi.set = true;
            menuDrawItem (mi, pb, false, from_kb);
        }
        break;

    case MENU_AL1OFN:
        // turn on unconditionally, but turn off only if not the last one
        if (!mi.set) {
            mi.set = true;
            menuDrawItem (mi, pb, false, from_kb);
        } else {
            if (menuCountItemsSet (menu, pick_i) > 1) {
                mi.set = false;
                menuDrawItem (mi, pb, false, from_kb);
            }
        }
        break;

    case MENU_TOGGLE:
        // uncondition change
        mi.set = !mi.set;
        menuDrawItem (mi, pb, false, from_kb);
        break;
    }
}


/* update menu based on the given keyboard char.
 * m_index is index of last updated item.
 */
static int checkKBControl (MenuInfo &menu, SBox *pick_boxes, int m_index, const char kbchar)
{
    // find first active group if nothing prior
    if (m_index < 0) {
        for (int i = 0; i < menu.n_items; i++) {
            MenuItem &mi = menu.items[i];
            if (MENU_ACTIVE(mi.type)) {
                m_index = i;
                break;
            }
        }
    }
    if (m_index < 0)
        return (m_index);                                       // no active types!


    // prep search state
    uint16_t minx = 10000, maxx = 0;
    uint16_t miny = 10000, maxy = 0;
    SBox &pm = pick_boxes[m_index];
    int candidate = -1;

    // search in desired direction
    switch (kbchar) {

    case 'h':
        // next field left, if any
        for (int i = 1; i <= menu.n_items; i++) {
            int ii = (m_index + i) % menu.n_items;
            MenuItem &mii = menu.items[ii];
            SBox &pii = pick_boxes[ii];
            uint16_t rightii_x = pii.x + pii.w;
            if (MENU_ACTIVE(mii.type) && pii.y == pm.y && rightii_x <= pm.x && rightii_x > maxx) {
                candidate = ii;
                maxx = rightii_x;
            }
        }
        if (candidate >= 0) {
            // erase current, draw new
            menuDrawItem (menu.items[m_index], pick_boxes[m_index], false, false);
            menuDrawItem (menu.items[candidate], pick_boxes[candidate], false, true);
            m_index = candidate;
        }
        break;

    case 'j':
        // next field down, if any
        for (int i = 1; i <= menu.n_items; i++) {
            int ii = (m_index + i) % menu.n_items;
            MenuItem &mii = menu.items[ii];
            SBox &pii = pick_boxes[ii];
            uint16_t topii_y = pii.y;
            if (MENU_ACTIVE(mii.type) && pii.x == pm.x && topii_y >= pm.y + pm.h && topii_y < miny) {
                candidate = ii;
                miny = topii_y;
            }
        }
        if (candidate >= 0) {
            // erase current, draw new
            menuDrawItem (menu.items[m_index], pick_boxes[m_index], false, false);
            menuDrawItem (menu.items[candidate], pick_boxes[candidate], false, true);
            m_index = candidate;
        }
        break;

    case 'k':
        // next field up, if any
        for (int i = 1; i <= menu.n_items; i++) {
            int ii = (m_index + i) % menu.n_items;
            MenuItem &mii = menu.items[ii];
            SBox &pii = pick_boxes[ii];
            uint16_t botii_y = pii.y + pii.h;
            if (MENU_ACTIVE(mii.type) && pii.x == pm.x && botii_y <= pm.y && botii_y > maxy) {
                candidate = ii;
                maxy = botii_y;
            }
        }
        if (candidate >= 0) {
            // erase current, draw new
            menuDrawItem (menu.items[m_index], pick_boxes[m_index], false, false);
            menuDrawItem (menu.items[candidate], pick_boxes[candidate], false, true);
            m_index = candidate;
        }
        break;

    case 'l':
        // next field right, if any
        for (int i = 1; i <= menu.n_items; i++) {
            int ii = (m_index + i) % menu.n_items;
            MenuItem &mii = menu.items[ii];
            SBox &pii = pick_boxes[ii];
            uint16_t leftii_x = pii.x;
            if (MENU_ACTIVE(mii.type) && pii.y == pm.y && leftii_x >= pm.x + pm.w && leftii_x < minx) {
                candidate = ii;
                minx = leftii_x;
            }
        }
        if (candidate >= 0) {
            // erase current, draw new
            menuDrawItem (menu.items[m_index], pick_boxes[m_index], false, false);
            menuDrawItem (menu.items[candidate], pick_boxes[candidate], false, true);
            m_index = candidate;
        }
        break;

    case ' ':
        // activate this location
        updateMenu (menu, pick_boxes, m_index, true);
        break;

    default:
        break;
    }

    return (m_index);
}

/* update menu from the given tap.
 */
static void checkTapControl (MenuInfo &menu, SBox *pick_boxes, const SCoord &tap)
{
    for (int i = 0; i < menu.n_items; i++) {

        MenuItem &mi = menu.items[i];
        SBox &pb = pick_boxes[i];

        if (mi.type != MENU_IGNORE && inBox (tap, pb)) {

            // implement each type of behavior
            updateMenu (menu, pick_boxes, i, false);

            // tap found
            break;
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
    menu.ok_b.h = MENU_RH;

    // create cancel button, set size but don't know position yet
    SBox cancel_b;
    cancel_b.w = getTextWidth (cancel_label) + MENU_BDX*2;
    cancel_b.h = MENU_RH;

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
    StackMalloc pbox_mem(menu.n_items*sizeof(SBox));
    SBox *pick_boxes = (SBox *) pbox_mem.getMem();
    uint16_t col_w = (menu.menu_b.w - MENU_RM)/menu.n_cols;
    int vrow_i = 0;                          // visual row, only incremented for non-IGNORE items
    for (int i = 0; i < menu.n_items; i++) {

        MenuItem &mi = menu.items[i];

        // assign item next location and draw unless to be ignored
        if (mi.type != MENU_IGNORE) {
            SBox &pb = pick_boxes[i];

            pb.x = menu.menu_b.x + 1 + (vrow_i/n_rowspercol)*col_w;
            pb.y = menu.menu_b.y + MENU_TBM + (vrow_i%n_rowspercol)*MENU_RH;
            pb.w = col_w;
            pb.h = MENU_RH;
            menuDrawItem (mi, pb, true, false);

            vrow_i++;
        }
    }
    if (vrow_i != n_activerows)                      // sanity check
        fatalError (_FX("menu row %d != %d / %d"), vrow_i, n_activerows, menu.n_items);

    // immediate draw if menu is over map
    if (boxesOverlap (menu.menu_b, map_b))
        tft.drawPR();

    SCoord tap;
    char kbchar;
    UserInput ui = {
        menu.menu_b,
        NULL,
        false,
        MENU_TIMEOUT,
        menu.update_clocks,
        tap,
        kbchar,
    };

    // run
    bool ok = false;
    int kb_item = -1;
    while (waitForUser (ui)) {

        // check for Enter or tap in ok
        if (kbchar == '\r' || kbchar == '\n' || inBox (tap, menu.ok_b)) {
            ok = true;
            break;
        }

        // check for ESC tap or tap in optional cancel
        if (kbchar == 27 || (!menu.no_cancel && inBox (tap, cancel_b))) {
            break;
        }

        // check for kb or tap control
        if (kbchar)
            kb_item = checkKBControl (menu, pick_boxes, kb_item, kbchar);
        else
            checkTapControl (menu, pick_boxes, tap);
    }

    // done
    drainTouch();

    // erase in prep for caller to restore covered content
    fillSBox (menu.menu_b, RA8875_BLACK);

    // record settings
    Serial.printf (_FX("Menu result after %s:\n"), ok ? "Ok" : "Cancel");
    for (int i = 0; i < menu.n_items; i++) {
        MenuItem &mi = menu.items[i];
        if (MENU_ACTIVE(mi.type))
            Serial.printf (_FX("  %-15s g%d s%d\n"), mi.label ? mi.label : "", mi.group, mi.set);
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

    // immediate draw if over map
    if (boxesOverlap (ok_b, map_b))
        tft.drawPR();
}

/* wait until:
 *   a tap occurs inside inbox:                      set tap location and type and return true.
 *   a char is typed:                                set kbchar and return true.
 *   (*fp)() (IFF fp != NULL) returns true:          set fp_true to true and return false.
 *   to_ms is > 0 and nothing happens for that long: return false.
 * while waiting we optionally update clocks and allow some web server commands.
 */
bool waitForUser (UserInput &ui)
{
    drainTouch();

    // initial timeout
    uint32_t t0 = millis();

    // reset both actions until they happen here
    ui.kbchar = 0;
    ui.tap = {0, 0};

    // insure screen is on
    setFullBrightness();

    for(;;) {

        if (readCalTouchWS(ui.tap) != TT_NONE) {
            drainTouch();
            if (inBox (ui.tap, ui.inbox))
                return(true);
            // tap restarts base timeout
            t0 = millis();
        }

        ui.kbchar = tft.getChar(NULL,NULL);
        if (ui.kbchar)
            return (true);

        if (ui.to_ms && timesUp (&t0, ui.to_ms))
            return (false);

        if (ui.fp && (*ui.fp)()) {
            ui.fp_true = true;
            return (false);
        }

        if (ui.update_clocks)
            updateClocks(false);

        wdDelay (10);

        // refresh protected region in case X11 window is moved
        if (boxesOverlap (ui.inbox, map_b))
            tft.drawPR();
    }
}
