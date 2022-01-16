/* handle displaying local and UTC time
 */


#include "HamClock.h"

uint8_t de_time_fmt;                            // one of DETIME_*
uint8_t desrss, dxsrss;                         // show actual de/dx sun rise/set else time to

// time update interval, seconds
#define TIME_INTERVAL   (30*60)                 // normal resync interval when working ok, seconds
#define TIME_RETRY       15000                  // retry interval when not working ok, millis

// handy way for webserver to get last time source
const char *gpsd_server, *ntp_server;           // at most one set to static storage of server name

// run flag and progression
static bool hide_clocks;                        // run but don't display
static int prev_yr, prev_mo, prev_dy, prev_hr, prev_mn, prev_sc, prev_wd;
static bool time_was_bad = true;                // used to erase ? when confirmed ok again
static uint8_t doy_on;                          // show day of year instead of month date.

// TimeLib's now() stays at real UTC, but user can adjust time offset
static int32_t utc_offset;                      // nowWO() offset from UTC, secs

// display 
#define UTC_W           14                      // UTC button width
#define QUESTION_W      28                      // Question mark width
#define hms_h           (5*clock_b.h/8-7)       // HMS height
#define UTC_H           (hms_h-1)               // UTC button height
#define FFONT_W         6                       // fixed font width
#define FFONT_H         8                       // fixed font height
#define HMS_C           RA8875_WHITE            // HMS color
#define MDY_C           RA8875_WHITE            // MDY color

/* the UTC "button" in clock_b depending on whether utc_offset is 0.
 */
static void drawUTCButton()
{
    selectFontStyle (BOLD_FONT, FAST_FONT);
    char msg[4];

    if (utc_offset == 0 && clockTimeOk()) {
        // at UTC for sure
        tft.fillRect (clock_b.x+clock_b.w-UTC_W, clock_b.y, UTC_W, UTC_H, HMS_C);
        tft.setTextColor(RA8875_BLACK);
        strcpy (msg, "UTC");
    } else {
        // unknown or time is other than UTC
        tft.fillRect (clock_b.x+clock_b.w-UTC_W, clock_b.y, UTC_W, UTC_H, RA8875_RED);
        tft.drawRect (clock_b.x+clock_b.w-UTC_W, clock_b.y, UTC_W, UTC_H, RA8875_RED);
        tft.setTextColor(HMS_C);
        strcpy (msg, "OFF");
    }

    uint16_t vgap = (UTC_H - 3*FFONT_H)/4;
    uint16_t x = clock_b.x+clock_b.w-UTC_W+(UTC_W-FFONT_W)/2;
    tft.setCursor(x, clock_b.y+vgap+1);             tft.print(msg[0]);
    tft.setCursor(x, clock_b.y+2*vgap+FFONT_H+1);   tft.print(msg[1]);
    tft.setCursor(x, clock_b.y+3*vgap+2*FFONT_H+1); tft.print(msg[2]);
}

/* called by Time system setSyncProvider (getTime) to resync clock.
 */
static time_t getTime(void)
{
    time_t t = 0;
    gpsd_server = NULL;
    ntp_server = NULL;

    if (useGPSD())
        t = getGPSDUTC(&gpsd_server);
    if (t == 0)
        t = getNTPUTC(&ntp_server);

    if (t) {
        Serial.printf (_FX("getTime from %s: %04d-%02d-%02d %02d:%02d:%02dZ\n"),
                gpsd_server ? gpsd_server : ntp_server,
                year(t), month(t), day(t), hour(t), minute(t), second(t));
    } else
        Serial.print (F("getTime failed\n"));

    return (t);
}

/* given number of seconds, print HH:MM
 */
static void prHM (const uint32_t t)
{
    uint16_t hh = t/SECS_PER_HOUR;
    uint16_t mm = (t - hh*SECS_PER_HOUR)/SECS_PER_MIN;

    char buf[20];
    sprintf (buf, "%d:%02d", hh, mm);
    tft.print(buf);
}

/* common portion for drawing the rise set info in the given box.
 */
static void drawRiseSet(time_t t0, time_t trise, time_t tset, SBox &b, uint8_t srss, int32_t tz_secs)
{
    resetWatchdog();

    tft.fillRect (b.x, b.y, b.w, b.h, RA8875_BLACK);
    //tft.drawRect (b.x, b.y, b.w, b.h, RA8875_WHITE);
    selectFontStyle (LIGHT_FONT, FAST_FONT);

    if (trise == 0) {
        tft.setCursor (b.x, b.y+8);
        tft.print (F("No rise"));
    } else if (tset == 0) {
        tft.setCursor (b.x, b.y+8);
        tft.print (F("No set"));
    } else {

        bool night_now;
        if (trise < tset)
            night_now = t0 < trise || t0 > tset;
        else
            night_now = t0 > tset && t0 < trise;

        if (srss) {

            // draw actual rise set times

            if (night_now) {
                tft.setCursor (b.x+8, b.y+8);
                tft.print (F("R @ "));
                prHM (3600*hour(trise+tz_secs) + 60*minute(trise+tz_secs));
                tft.setCursor (b.x+8, b.y+b.h/2+4);
                tft.print (F("S @ "));
                prHM (3600*hour(tset+tz_secs) + 60*minute(tset+tz_secs));
            } else {
                tft.setCursor (b.x+8, b.y+8);
                tft.print (F("S @ "));
                prHM (3600*hour(tset+tz_secs) + 60*minute(tset+tz_secs));
                tft.setCursor (b.x+8, b.y+b.h/2+4);
                tft.print (F("R @ "));
                prHM (3600*hour(trise+tz_secs) + 60*minute(trise+tz_secs));
            }

        } else {

            // draw until rise and set

            int32_t rdt = t0 - trise;
            int32_t sdt = t0 - tset;

            tft.setCursor (b.x, b.y+8);
            if (night_now) {
                tft.print (F("R in "));
                prHM (rdt > 0 ? SECS_PER_DAY-rdt : -rdt);
                tft.setCursor (b.x, b.y+b.h/2+4);
                tft.print (F("S "));
                prHM (sdt >= 0 ? sdt : SECS_PER_DAY+sdt);
                tft.print (F(" ago"));
            } else {
                tft.print (F("S in "));
                prHM (sdt > 0 ? SECS_PER_DAY-sdt : -sdt);
                tft.setCursor (b.x, b.y+b.h/2+4);
                tft.print (F("R "));
                prHM (rdt >= 0 ? rdt : SECS_PER_DAY+rdt);
                tft.print (F(" ago"));
            }
        }
    }
}

/* given DE time_t with user offset draw local analog time clock in de_info_b
 */
static void drawAnalogClock (time_t delocal_t)
{
    // find center of largest inscribed circle sitting at the bottom
    int x0, y0, r;
    if (de_info_b.w > de_info_b.h) {
        r = de_info_b.h/2 - 3;
        x0 = de_info_b.x + de_info_b.w/2;
        y0 = de_info_b.y + r;
    } else {
        r = de_info_b.w/2 - 3;
        x0 = de_info_b.x + r;
        y0 = de_info_b.y + de_info_b.h/2;
    }

    int hr = hour(delocal_t);
    int mn = minute(delocal_t);
    int wd = weekday(delocal_t);
    int dy = day(delocal_t);
    int mo = month(delocal_t);

    // convert hours and minutes to degrees CCW from 3 oclock
    int16_t hr360 = 30*(3-(((hr+24)%12) + mn/60.0F));                 // + partial hour
    int16_t mn360 = 6*(15-mn);

    // start clock face
    tft.fillRect (de_info_b.x, de_info_b.y, de_info_b.w, de_info_b.h-1, RA8875_BLACK);
    tft.drawCircle (x0, y0, r, DE_COLOR);
    for (uint16_t a = 0; a < 360; a += 30)
        tft.fillCircle (x0+0.95F*r*cosf(deg2rad(a)), y0+0.95F*r*sinf(deg2rad(a)), 2, DE_COLOR);

    // draw full length minute hand
    float cosmn = r*cosf(deg2rad(mn360));
    float sinmn = r*sinf(deg2rad(mn360));
    uint16_t farmnx = floorf(x0+0.90F*cosmn+0.5F);
    uint16_t farmny = floorf(y0-0.90F*sinmn+0.5F);                    // -y up
    int16_t nearmnx = floorf(0.04F*sinmn+0.5F);
    int16_t nearmny = floorf(0.04F*cosmn+0.5F);
    tft.drawLine (x0+nearmnx, y0+nearmny, farmnx, farmny, DE_COLOR);
    tft.drawLine (x0-nearmnx, y0-nearmny, farmnx, farmny, DE_COLOR);
    tft.drawCircle (x0, y0, r*0.04F+0.5F, DE_COLOR);

    // draw shorter hour hand
    float coshr = r*cosf(deg2rad(hr360));
    float sinhr = r*sinf(deg2rad(hr360));
    uint16_t farhrx = floorf(x0+0.45F*coshr+0.5F);
    uint16_t farhry = floorf(y0-0.45F*sinhr+0.5F);                    // -y up
    int16_t nearhrx = floorf(0.06F*sinhr+0.5F);
    int16_t nearhry = floorf(0.06F*coshr+0.5F);
    tft.drawLine (x0+nearhrx, y0+nearhry, farhrx, farhry, DE_COLOR);
    tft.drawLine (x0-nearhrx, y0-nearhry, farhrx, farhry, DE_COLOR);
    tft.drawCircle (x0, y0, r*0.06F+0.5F, DE_COLOR);

    // draw time labels too if on
    if (de_time_fmt == DETIME_ANALOG_DTTM) {
        const uint16_t indent = 5;
        const uint16_t rowh = 12;
        const uint16_t charw = 6;
        uint16_t tx = de_info_b.x+indent;
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (DE_COLOR);
        tft.setCursor (tx, y0-r);
        tft.print (dayShortStr(wd));
        tft.setCursor (tx, y0-r+rowh);
        tft.print (hr < 12 ? "AM" : "PM");
        tx = de_info_b.x+de_info_b.w-indent-3*charw;
        tft.setCursor (tx, y0-r);
        tft.print (monthShortStr(mo));
        tx = de_info_b.x+de_info_b.w-indent-charw;
        if (dy > 9)
            tx -= charw;
        tft.setCursor (tx, y0-r+rowh);
        tft.print (dy);
    }
}

/* given DE time_t with user offset draw local digital time clock in de_info_b
 */
static void drawDigitalClock (time_t delocal_t)
{
    // break out
    int hr = hour(delocal_t);
    int mn = minute(delocal_t);
    int wd = weekday(delocal_t);
    int dy = day(delocal_t);
    int mo = month(delocal_t);
    int yr = year(delocal_t);

    // prep
    tft.fillRect (de_info_b.x, de_info_b.y, de_info_b.w, de_info_b.h-1, RA8875_BLACK);

    // format time
    char buf[50];
    if (de_time_fmt == DETIME_DIGITAL_12) {
        int hr12 = hr%12;
        if (hr12 == 0)
            hr12 = 12;
        snprintf (buf, sizeof(buf), _FX("%d:%02d"), hr12, mn);
    } else {
        snprintf (buf, sizeof(buf), _FX("%02d:%02d"), hr, mn);
    }

    // print time
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    uint16_t bw = getTextWidth(buf);
    tft.setCursor (de_info_b.x+(de_info_b.w-bw)/2-4, de_info_b.y + de_info_b.h/2);
    tft.setTextColor (DE_COLOR);
    tft.print (buf);

    // other info
    // N.B. dayShortStr and monthShort return same static pointer
    size_t bl = 0;
    if (getDateFormat() == DF_DMY) {
        bl += snprintf (buf+bl, sizeof(buf)-bl, "%s, ", dayShortStr(wd));
        bl += snprintf (buf+bl, sizeof(buf)-bl, "%d %s %d", dy, monthShortStr(mo), yr);
    } else if (getDateFormat() == DF_MDY) {
        bl += snprintf (buf+bl, sizeof(buf)-bl, "%s ", dayShortStr(wd));
        bl += snprintf (buf+bl, sizeof(buf)-bl, "%s %d, %d", monthShortStr(mo), dy, yr);
    } else if (getDateFormat() == DF_YMD) {
        bl += snprintf (buf+bl, sizeof(buf)-bl, "%s, ", dayShortStr(wd));
        bl += snprintf (buf+bl, sizeof(buf)-bl, "%d %s %d", yr, monthShortStr(mo), dy);
    } else {
        fatalError (_FX("Bug! bad date fmt: %d"), (int)getDateFormat());
    }
    if (de_time_fmt == DETIME_DIGITAL_12)
        bl += snprintf (buf+bl, sizeof(buf)-bl, " %s", hr < 12 ? "AM" : "PM");
    else
        bl += snprintf (buf+bl, sizeof(buf)-bl, " 24h");
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    bw = getTextWidth(buf);
    tft.setCursor (de_info_b.x + (de_info_b.w-bw)/2, de_info_b.y + 4*de_info_b.h/5);
    tft.print (buf);
}

/* draw a calendar in de_info_b below time
 */
void drawCalendar(bool force)
{

    // looks a little better to me if we put a small border around the edges
    #define CAL_BW 4

    // find local time
    tmElements_t tm;
    time_t tnow = nowWO() + de_tz.tz_secs;
    breakTime (tnow, tm);                       // break into components
    // Serial.printf ("cal force= %d YMD %d %d %d\n", force, tm.Year, tm.Month, tm.Day);

    // avoid redraws unless force
    static uint8_t prev_Year, prev_Month, prev_Day;
    if (!force && prev_Year == tm.Year && prev_Month == tm.Month && prev_Day == tm.Day)
        return;
    prev_Year = tm.Year;
    prev_Month = tm.Month;
    prev_Day = tm.Day;

    // cal in box below time
    uint16_t vspace = de_info_b.h/DE_INFO_ROWS;
    uint16_t cal_y = de_info_b.y + vspace;
    uint16_t cal_h = de_info_b.y + de_info_b.h - cal_y;

    // erase all
    tft.fillRect (de_info_b.x, cal_y, de_info_b.w, cal_h, RA8875_BLACK);

    // find column for 1st of this month
    uint8_t today = tm.Day;                     // save today's date, 1 based
    tm.Day = 1;                                 // set 1st
    uint32_t t1st = makeTime(tm);               // synth new time
    uint8_t col1 = weekday (t1st) - 1;          // 0-based column of 1st day of month

    // find number of days in this month
    if (++tm.Month == 13) {                     // advance to next month, which is 1-based
        tm.Month = 1;                           // roll to next year 
        tm.Year++;
    }
    uint32_t t1stmo = makeTime (tm);            // first of next month
    uint8_t dtm = (t1stmo - t1st)/SECSPERDAY;   // n days this month

    // find required number of rows
    int8_t dom = 1-col1;                        // 1-based day of month in first cell, <=0 if prev mon
    uint8_t n_cols = 7;                         // always 7 cols
    uint8_t n_rows = (dtm - dom + 7)/n_cols;    // n rows required
    // Serial.printf ("col1= %d dtm= %d dom= %d n_cols= %d n_rows= %d\n", col1, dtm, dom, n_cols, n_rows);

    // draw grid
    for (uint8_t i = 0; i <= n_rows; i++) {
        uint16_t y = cal_y + i*(de_info_b.h-vspace-1)/n_rows;
        tft.drawLine (de_info_b.x+CAL_BW, y, de_info_b.x + de_info_b.w - CAL_BW, y, DE_COLOR);
    }
    for (uint8_t i = 0; i <= n_cols; i++) {
        uint16_t x = de_info_b.x + CAL_BW + i*(de_info_b.w-2*CAL_BW)/n_cols;
        tft.drawLine (x, cal_y, x, de_info_b.y+de_info_b.h-1, DE_COLOR);
    }

    // fill dates
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    for (uint8_t r = 0; r < n_rows; r++) {
        for (uint8_t c = 0; c < n_cols; c++) {
            if (dom >= 1 && dom <= dtm) {
                tft.setTextColor (dom == today ? RA8875_WHITE : DE_COLOR);
                uint16_t x0 = CAL_BW + de_info_b.x + c*(de_info_b.w-2*CAL_BW)/n_cols + 4;
                if (dom < 10)
                    x0 += 2;
                tft.setCursor (x0, cal_y + r*cal_h/n_rows + 3);
                tft.print (dom);
            }
            dom++;
        }
    }
}

/* start the clock running
 */
void initTime()
{
    // get last UTC offset from ENVROM
    utc_offset = 0;
    NVReadInt32 (NV_UTC_OFFSET, &utc_offset);

    // start using time source
    enableSyncProvider();

    // init doy_on
    if (!NVReadUInt8 (NV_DOY_ON, &doy_on)) {
        doy_on = 0;
        NVWriteUInt8 (NV_DOY_ON, doy_on);
    }
}

/* do not display clocks
 */
void hideClocks()
{
    hide_clocks = true;
}


/* resume displaying clocks and insure everything gets drawn first time
 */
void showClocks()
{
    hide_clocks = false;

    prev_yr = 99;
    prev_mo = 99;
    prev_dy = 99;
    prev_hr = 99;
    prev_mn = 99;
    prev_sc = 99;
    prev_wd = 99;

    drawUTCButton();
}

/* use NTP or GPSD to update time
 */
void enableSyncProvider()
{
    setSyncInterval (TIME_INTERVAL);
    setSyncProvider (getTime);
}

/* like now() but with current user offset
 */
time_t nowWO()
{
    return (now() + utc_offset);
}


/* return current offset from UTC 
 */
int32_t utcOffset()
{
    return (utc_offset);
}


/* return whether time is working for the clock.
 * if not goose occasionally.
 */
bool clockTimeOk()
{
    bool time_ok = timeStatus() == timeSet;
    if (!time_ok) {
        static uint32_t prev_timeok;
        if (timesUp(&prev_timeok, TIME_RETRY))
            setSyncProvider (getTime);                  // force fresh sync attempt
        time_ok = timeStatus() == timeSet;
    }
    return (time_ok);
}

/* draw all clocks if time system has been initialized.
 * N.B. this is called a lot so make it very fast when nothing to do
 */
void updateClocks(bool all)
{
    char buf[32];

    // ignore if disabled
    if (hide_clocks)
        return;

    // get Clock's UTC time now, get out fast if still same second
    time_t t = nowWO();
    int sc = second(t);
    if (sc == prev_sc && !all)
        return;

    // pull apart the time
    int hr = hour(t);
    int mn = minute(t);
    int wd = weekday(t);
    int mo = month(t);
    int dy = day(t);
    int yr = year(t);

    // set to update other times as well
    bool draw_other_times = false;

    resetWatchdog();

    // always draw seconds because we know it has changed
    if (all || (sc/10) != (prev_sc/10)) {

        // Change in tens digit of seconds process normally W2ROW
        uint16_t sx = clock_b.x+2*clock_b.w/3;          // right 1/3 for seconds
        selectFontStyle (BOLD_FONT, SMALL_FONT);
        sprintf (buf, "%02d", sc);                      // includes ones digit
        tft.fillRect(sx, clock_b.y, 30, hms_h/2+4, RA8875_BLACK);  // dont erase ? if present
        tft.setCursor(sx, clock_b.y+hms_h-19);
        tft.setTextColor(HMS_C);
        tft.print(buf);

    } else {

        // Change only in units digit of seconds - process only that digit  W2ROW
        uint16_t sx = clock_b.x+2*clock_b.w/3+15;       // right 1/3 for seconds (15 by experiment) W2ROW
        selectFontStyle (BOLD_FONT, SMALL_FONT);        // W2ROW
        sprintf (buf, "%01d", sc%10);                   // W2ROW
        tft.fillRect(sx, clock_b.y, 15, hms_h/2+4, RA8875_BLACK);  // dont erase ? W2ROW
        tft.setCursor(sx, clock_b.y+hms_h-19);          // W2ROW
        tft.setTextColor(HMS_C);                        // W2ROW
        tft.print(buf);                                 // W2ROW
      
    }

    // check time
    if (clockTimeOk()) {
        if (time_was_bad) {

            // just came back, show and update state
            drawUTCButton();
            tft.fillRect(clock_b.x+2*clock_b.w/3+34, clock_b.y, 25, hms_h+4, RA8875_BLACK);
            Serial.printf (_FX("Time ok Z = %04d-%02d-%02d %02d:%02d:%02d %+d s\n"),
                                yr, mo, dy, hr, mn, sc, -utc_offset);

            time_was_bad = false;
        }
    } else {
        if (!time_was_bad) {

            // just went bad, show and update state
            drawUTCButton();
            selectFontStyle (BOLD_FONT, LARGE_FONT);
            tft.setTextColor(HMS_C);
            tft.setCursor(clock_b.x+clock_b.w-UTC_W-QUESTION_W, clock_b.y+hms_h);
            tft.print('?');

            time_was_bad = true;
        }
    }

    // persist
    prev_sc = sc;

    // draw H:M if either changes
    if (all || mn != prev_mn || hr != prev_hr) {

        resetWatchdog();

        // draw H:M roughly right-justified in left 2/3
        selectFontStyle (BOLD_FONT, LARGE_FONT);
        sprintf (buf, "%02d:%02d", hr, mn);
        uint16_t w = 135;
        int16_t x = clock_b.x+2*clock_b.w/3-w;
        tft.fillRect (x, clock_b.y, w, hms_h+2, RA8875_BLACK);
        tft.setCursor(x, clock_b.y+hms_h);
        tft.setTextColor(HMS_C);
        tft.print(buf);

        // update BC time marker if new hour and up
        if (prev_hr != hr) {
            PlotPane bc_pane = findPaneChoiceNow(PLOT_CH_BC);
            if (bc_pane != PANE_NONE)
                plotBandConditions (plot_b[bc_pane], 0, NULL, NULL);
        }

        // update other info
        draw_other_times = true;

        // persist
        prev_mn = mn;
        prev_hr = hr;
    }

    // draw date if new day
    if (all || dy != prev_dy || wd != prev_wd || mo != prev_mo || yr != prev_yr) {

        resetWatchdog();

        // clear
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.fillRect(clock_b.x, clock_b.y+hms_h+5, clock_b.w-UTC_W, clock_b.h-hms_h-4, RA8875_BLACK);
        uint16_t y = clock_b.y + clock_b.h - 8; // descent
        tft.setTextColor(MDY_C);

        // print in desired format
        if (doy_on) {

            // Weekday DOY <doy> year

            // find day of year
            tmElements_t tm;
            breakTime (t, tm);
            tm.Second = tm.Minute = tm.Hour = 0;
            tm.Month = tm.Day = 1;
            time_t year0 = makeTime (tm);
            int doy = (t - year0) / (24*3600) + 1;

            sprintf (buf, "%s DOY %d  %d", dayShortStr(wd), doy, yr);
            uint16_t bw = getTextWidth (buf);
            int16_t x = clock_b.x + (clock_b.w-UTC_W-bw)/2;
            if (x < 0)
                x = 0;
            tft.setCursor(x, y);
            tft.print(buf);

        } else {

            // N.B. dayShortStr() and monthShortStr() share the same static pointer

            if (getDateFormat() == DF_DMY) {

                // Weekday, date month year

                int l = sprintf (buf, "%s, ", dayShortStr(wd));
                sprintf (buf+l, "%2d %s %d", dy, monthShortStr(mo), yr);
                uint16_t bw = getTextWidth (buf);
                int16_t x = clock_b.x + (clock_b.w-bw-25)/2;
                if (x < 0)
                    x = 0;
                tft.setCursor(x, y);
                tft.print(buf);

            } else if (getDateFormat() == DF_MDY) {

                // Weekday month date, year

                int l = sprintf (buf, "%s  ", dayShortStr(wd));
                sprintf (buf+l, "%s %2d, %d", monthShortStr(mo), dy, yr);
                uint16_t bw = getTextWidth (buf);
                int16_t x = clock_b.x + (clock_b.w-bw-25)/2;
                if (x < 0)
                    x = 0;
                tft.setCursor(x, y);
                tft.print(buf);

            } else if (getDateFormat() == DF_YMD) {

                // Weekday, year month date

                int l = sprintf (buf, "%s,  ", dayShortStr(wd));
                sprintf (buf+l, "%d %s %2d", yr, monthShortStr(mo), dy);
                uint16_t bw = getTextWidth (buf);
                int16_t x = clock_b.x + (clock_b.w-bw-25)/2;
                if (x < 0)
                    x = 0;
                tft.setCursor(x, y);
                tft.print(buf);

            } else {

                fatalError (_FX("Bug! bad format: %d"), (int)getDateFormat());

            }
        }

        // update other info
        draw_other_times = true;

        // persist
        prev_yr = yr;
        prev_mo = mo;
        prev_dy = dy;
        prev_wd = wd;
    }

    if (draw_other_times) {

        // DE pane
        switch (de_time_fmt) {
        case DETIME_CAL:
            drawDECalTime(true);
            drawCalendar(false);
            break;
        case DETIME_ANALOG:     // fallthru
        case DETIME_ANALOG_DTTM:
            drawAnalogClock (t + de_tz.tz_secs);
            break;
        case DETIME_INFO:
            drawDECalTime(false);
            drawDESunRiseSetInfo();
            break;
        case DETIME_DIGITAL_12: // fallthru
        case DETIME_DIGITAL_24: // fallthru
            drawDigitalClock (t + de_tz.tz_secs);
            break;
        default:
            fatalError ("Bug! unknown de fmt %d", de_time_fmt);
            break;
        }

        // DX pane
        if (!dx_info_for_sat) {
            drawDXTime();
            drawDXSunRiseSetInfo();
        }
    }

    // flash plot panes that are rotating
    for (int i = 0; i < PANE_N; i++) {
        if (paneIsRotating((PlotPane)i)) {
            showRotatingBorder ((sc&1) == 1, (PlotPane)i);
        }
    }
}

/* draw DE sun rise and set info
 */
void drawDESunRiseSetInfo()
{
    resetWatchdog();

    time_t trise, tset, t0 = nowWO();
    getSolarRS (t0, de_ll, &trise, &tset);

    tft.setTextColor(DE_COLOR);
    drawRiseSet (t0, trise, tset, desrss_b, desrss, de_tz.tz_secs);
}

/* draw DX sun rise and set info.
 * skip if showing dx prefix there.
 */
void drawDXSunRiseSetInfo()
{
    if (dxsrss == DXSRSS_PREFIX)
        return;

    resetWatchdog();

    time_t trise, tset, t0 = nowWO();
    getSolarRS (t0, dx_ll, &trise, &tset);

    tft.setTextColor(DX_COLOR);
    drawRiseSet (t0, trise, tset, dxsrss_b, dxsrss, dx_tz.tz_secs);

}

/* return whether touch event at s involved the clocks.
 * if so, update utc_offset and possibly restart maps if large change.
 * N.B. we expect caller to call updateClocks if we return true.
 */
bool checkClockTouch (SCoord &s, TouchType tt)
{
    // ignore if not in clock box
    if (!inBox (s, clock_b))
        return (false);

    // find position within box
    int16_t dx = s.x - clock_b.x;
    int16_t dy = s.y - clock_b.y;

    // get time now
    uint32_t real_utc = now();
    uint32_t user_utc = real_utc + utc_offset;

    // see how much time changes
    int32_t off0 = utc_offset;

    // update depending on where touch occurred
    if (dy < hms_h) {
        // touched HMS or utc
        if (dx > clock_b.w-UTC_W) {
            // touched UTC
            if (utc_offset != 0 || !clockTimeOk()) {
                utc_offset = 0;
                setSyncProvider (getTime);
            }
        } else {
            // touched HMS
            uint16_t mid_h = hms_h/2;
            if (dx < clock_b.w/3) {
                // touched hours
                if (dy < mid_h/2)
                    utc_offset += 2*3600;
                else if (dy < mid_h)
                    utc_offset += 3600;
                else if (dy < 3*mid_h/2)
                    utc_offset -= 3600;
                else
                    utc_offset -= 2*3600;
            } else if (dx < 2*clock_b.w/3) {
                // touched minutes
                if (dy < mid_h/2)
                    utc_offset += 10*60;
                else if (dy < mid_h)
                    utc_offset += 60;
                else if (dy < 3*mid_h/2)
                    utc_offset -= 60;
                else
                    utc_offset -= 10*60;
            } else if (dx < clock_b.w-UTC_W-QUESTION_W) {
                // touched seconds -- chop to whole minute
                utc_offset = 60*(user_utc/60) - real_utc;
            }
        }
    } else {
        // DMY
        uint16_t mid_h = hms_h + (clock_b.h-hms_h)/2;
        uint16_t s_n = dx/(clock_b.w/6);              // 0 1 .. 4 5 section
        DateFormat df = getDateFormat();        
        if (s_n <= 1) {
            // touched day of week in any format
            if (dy < mid_h)
                utc_offset += SECSPERDAY;
            else
                utc_offset -= SECSPERDAY;
        } else if ((s_n == 2 && df == DF_MDY) || (s_n == 3 && (df == DF_DMY || df == DF_YMD))) {
            // touched month
            if (tt == TT_HOLD) {
                // just toggle DOY, no change in time
                doy_on = !doy_on;
                NVWriteUInt8 (NV_DOY_ON, doy_on);
                logState();
            } else if (!doy_on) {
                tmElements_t tm;
                breakTime (user_utc, tm);
                if (dy < mid_h) {
                    if (++tm.Month > 12) {
                        tm.Month = 1;
                        tm.Year += 1;
                    }
                } else {
                    if (--tm.Month == 0) {
                        tm.Month = 12;
                        tm.Year -= 1;
                    }
                }
                utc_offset = makeTime(tm) - real_utc;
            }
        } else if ((s_n == 2 && df == DF_DMY) || (s_n == 3 && df == DF_MDY) || (s_n == 4 && df == DF_YMD)) {
            // touched date of month
            if (tt == TT_HOLD) {
                // just toggle DOY, no change in time
                doy_on = !doy_on;
                NVWriteUInt8 (NV_DOY_ON, doy_on);
                logState();
            } else {
                if (dy < mid_h)
                    utc_offset += SECSPERDAY;
                else
                    utc_offset -= SECSPERDAY;
            }
        } else if ((s_n == 2 && df == DF_YMD) || (s_n == 4 && (df == DF_DMY || df == DF_MDY))) {
            // touched year
            tmElements_t tm;
            breakTime (user_utc, tm);
            if (dy < mid_h)
                tm.Year += 1;
            else
                tm.Year -= 1;
            utc_offset = makeTime(tm) - real_utc;
        }
    }

    // save new offset
    NVWriteUInt32 (NV_UTC_OFFSET, utc_offset);

    // show new time
    updateClocks(false);
    drawUTCButton();

    // restart systems if likely effected by time change
    int dt = abs (utc_offset - off0);
    if (dt > 5*60) {
        initWiFiRetry();        // this will also update moon
    } else {
        updateMoonPane (false);
    }
    if (dt >= 30)
        displaySatInfo();

    return (true);
}

/* return DE today's weekday 1..7 == Sun..Sat
 */
int DEWeekday(void)
{
    time_t de_local = nowWO() + de_tz.tz_secs;
    return (weekday (de_local));
}

/* set time to UNIX t, maintaining user's offset, or to UTC if t == 0.
 */
void changeTime (time_t t)
{
    // update offset
    if (t == 0)
        utc_offset = 0;
    else
        utc_offset += t - nowWO();

    // save
    NVWriteUInt32 (NV_UTC_OFFSET, utc_offset);

    // UTC button, normal loop will update clocks
    drawUTCButton();

    // update map
    initEarthMap();
    displaySatInfo();
    updateMoonPane (false);
}

/* show menu of timezone offsets +- 2 from nominal.
 * if user taps ok update tzi.tz_secs and return true, else false.
 */
bool TZMenu (TZInfo &tzi, const LatLong &ll)
{
    // get nominal TZ for this location
    int32_t tz0_secs = getTZ (ll);

    // create menu
    #define N_NEW_TZ 5
    #define MAX_NEW_TZ 20
    #define TZ_MENU_INDENT 10
    MenuItem mitems[N_NEW_TZ];
    char tz_label[N_NEW_TZ][MAX_NEW_TZ];
    for (int i = 0; i < N_NEW_TZ; i++) {
        int32_t tz = tz0_secs + 3600*(i-N_NEW_TZ/2);
        snprintf (tz_label[i], MAX_NEW_TZ, "UTC%+g", tz/3600.0F);
        MenuItem &mi = mitems[i];
        mi.type = MENU_1OFN;
        mi.group = 1;
        mi.set = tz == tzi.tz_secs;
        mi.indent = TZ_MENU_INDENT;
        mi.label = tz_label[i];
    }

    // boxes
    SBox menu_b;
    menu_b.x = tzi.box.x - 20;
    menu_b.y = tzi.box.y + tzi.box.h+2;
    // w/h are set dynamically by runMenu()

    // run
    SBox ok_b;
    Menu menu = {menu_b, ok_b, false, 1, N_NEW_TZ, mitems};
    bool menu_ok = runMenu (menu);

    // erase our box regardless
    tft.fillRect (menu_b.x, menu_b.y, menu_b.w, menu_b.h, RA8875_BLACK);

    // done if cancelled
    if (!menu_ok)
        return (false);

    // update tzi from set item
    for (int i = 0; i < N_NEW_TZ; i++) {
        if (mitems[i].set) {
            tzi.tz_secs = tz0_secs + 3600*(i-N_NEW_TZ/2);
            break;
        }
    }

    return (true);
}

/* draw a TZ control box with current state
 */
void drawTZ (const TZInfo &tzi)
{
    // format as UTC + hours
    char buf[32];
    uint16_t w, h;
    snprintf (buf, sizeof(buf), "UTC%+g", tzi.tz_secs/3600.0F);
    selectFontStyle (BOLD_FONT, FAST_FONT);
    getTextBounds (buf, &w, &h);

    // box
    tft.fillRect (tzi.box.x, tzi.box.y, tzi.box.w, tzi.box.h, RA8875_BLACK);
    tft.drawRect (tzi.box.x, tzi.box.y, tzi.box.w, tzi.box.h, tzi.color);
    tft.setTextColor (tzi.color);
    tft.setCursor (tzi.box.x+(tzi.box.w-w)/2, tzi.box.y+(tzi.box.h-h)/2);
    tft.print (buf);
}
