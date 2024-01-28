/* manage the PLOT_CH_MOON option
 */

#include "HamClock.h"



/* draw the moon image in the given box.
 */
static void drawMoonImage (const SBox &b)
{
        // prep
        prepPlotBox (b);

        float phase = lunar_cir.phase;
        // Serial.printf (_FX("Phase %g deg\n"), rad2deg(phase));

        const uint16_t mr = HC_MOON_W/2;                            // moon radius on output device
        uint16_t mcx = tft.SCALESZ*(b.x+b.w/2);                     // moon center x "
        uint16_t mcy = tft.SCALESZ*(b.y+b.h/2);                     // moon center y "
        int pix_i = 0;                                              // moon_image index
        for (int16_t dy = -mr; dy < mr; dy++) {                     // scan top-to-bot, matching image
            float Ry = sqrtf(mr*mr-dy*dy);                          // moon circle half-width at y
            int16_t Ryi = floorf(Ry+0.5F);                          // " as int
            for (int16_t dx = -mr; dx < mr; dx++) {                 // scan left-to-right, matching image
                uint16_t pix = pgm_read_word(&moon_image[pix_i++]); // next pixel
                if (dx > -Ryi && dx < Ryi) {                        // if inside moon circle
                    float a = acosf((float)dx/Ryi);                 // looking down from NP CW from right limb
                    if (isnan(a) || (phase > 0 && a > phase) || (phase < 0 && a < phase+M_PIF))
                        pix = RGB565(RGB565_R(pix)/3, RGB565_G(pix)/3, RGB565_B(pix)/3); // unlit side
                    tft.drawPixelRaw (mcx+dx, mcy+dy, pix);
                }
                if ((dy%50) == 0)
                    resetWatchdog();
            }
        }
}

/* update moon pane info for sure and possibly image also.
 * image is in moon_image[HC_MOON_W*HC_MOON_H].
 */
void updateMoonPane (const SBox &box, bool image_too)
{
        resetWatchdog();

        // fresh info at user's effective time
        static time_t prev_full;
        time_t t0 = nowWO();
        getLunarCir (t0, de_ll, lunar_cir);

        // keep the strings so we can erase them exactly next time; using rectangles cuts chits from moon
        static char az_str[10];
        static char el_str[10];
        static char rs_str[10];
        static char rt_str[10];

        // start fresh image if requested or old else just erase previous info

        if (image_too || labs (t0-prev_full) > 3600) {

            // full draw
            drawMoonImage(box);

            // record
            prev_full = t0;

        } else {

            // pane image not drawn so erase previous individual stats
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            tft.setTextColor (RA8875_BLACK);
            tft.setCursor (box.x+1, box.y+2);
            tft.print (az_str);
            tft.setCursor (box.x+box.w-getTextWidth(el_str)-1, box.y+2);
            tft.print (el_str);
            tft.setCursor (box.x+1, box.y+box.h-10);
            tft.print (rs_str);
            tft.setCursor (box.x+box.w-getTextWidth(rt_str)-1, box.y+box.h-10);
            tft.print (rt_str);
        }

        // always draw info, layout similar to SDO

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (DE_COLOR);

        snprintf (az_str, sizeof(az_str), "Az:%.0f", rad2deg(lunar_cir.az));
        tft.setCursor (box.x+1, box.y+2);
        tft.print (az_str);

        snprintf (el_str, sizeof(el_str), "El:%.0f", rad2deg(lunar_cir.el));
        tft.setCursor (box.x+box.w-getTextWidth(el_str)-1, box.y+2);
        tft.print (el_str);

        // show which ever rise or set event comes next
        time_t rise, set;
        getLunarRS (t0, de_ll, &rise, &set);
        if (rise > t0 && (set < t0 || rise - t0 < set - t0))
            snprintf (rs_str, sizeof(rs_str), "R@%02d:%02d", hour(rise+de_tz.tz_secs),
                                                              minute (rise+de_tz.tz_secs));
        else if (set > t0 && (rise < t0 || set - t0 < rise - t0))
            snprintf (rs_str, sizeof(rs_str), "S@%02d:%02d", hour(set+de_tz.tz_secs),
                                                              minute (set+de_tz.tz_secs));
        else 
            strcpy (rs_str, "No R/S");
        tft.setCursor (box.x+1, box.y+box.h-10);
        tft.print (rs_str);

        snprintf (rt_str, sizeof(rt_str), "%.0fm/s", lunar_cir.vel);;
        tft.setCursor (box.x+box.w-getTextWidth(rt_str)-1, box.y+box.h-10);
        tft.print (rt_str);
}



/************************************************************************************************
 *
 * Moon DE/DX elevation plotting
 *
 ************************************************************************************************/



// moon elevation plot parameters and handy helpers
#define MP_TB           60                                      // top plot border
#define MP_LB           60                                      // left plot border
#define MP_RB           20                                      // right plot border
#define MP_BB           50                                      // bottom plot border
#define MP_NI           10                                      // next-both-up table indent
#define MP_MT           5                                       // up marker line thickness
#define MP_TL           2                                       // tick length
#define MP_PH           (map_b.h - MP_TB - MP_BB)               // plot height
#define MP_PW           (map_b.w - MP_LB - MP_RB)               // plot width
#define MP_X0           (map_b.x + MP_LB)                       // x coord of plot left
#define MP_DUR          (2*24*3600)                             // plot duration, seconds
#define MP_DT           (MP_DUR/100)                            // plot step size, seconds
#define MP_US           60                                      // micro step refined time, seconds
#define MP_TO           (30*1000)                               // time out, millis
#define MP_FC           RGB565(65,200,65)                       // fill color 
#define MP_TT           7                                       // timeline marker thickness
#define MP_E2Y(E)       ((uint16_t)(map_b.y+MP_TB + MP_PH*(M_PI_2F-(E))/M_PIF + 0.5F)) // elev to y coord
#define MP_T2X(T)       ((uint16_t)(MP_X0 + MP_PW*((T)-t0)/MP_DUR))     // time_t to x coord
#define MP_X2T(X)       ((time_t)(t0 + MP_DUR*((X)-MP_X0)/MP_PW))       // x coord to time_t


/* draw everything in the moon EME plot except the elevation plots, Resume button and the "Next Up" table.
 * t0 is nowWO()
 */
static void drawMPSetup (time_t t0)
{
        resetWatchdog();

        // grid lines color
        const uint16_t dark = RGB565(50,50,50);

        // title
        const char *title = "Lunar Elevation at DE and DX";
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        uint16_t tw = getTextWidth(title);
        tft.setCursor (map_b.x + (map_b.w-tw)/2, map_b.y + 30);
        tft.setTextColor (RA8875_WHITE);
        tft.print (title);

        // x and y axes
        tft.drawLine (MP_X0, MP_E2Y(-M_PI_2F), MP_X0, MP_E2Y(M_PI_2F), BRGRAY);
        tft.drawLine (MP_X0, MP_E2Y(-M_PI_2F), MP_X0 + MP_PW, MP_E2Y(-M_PI_2F), BRGRAY);

        // center line
        tft.drawLine (MP_X0, MP_E2Y(0), MP_X0+MP_PW, MP_E2Y(0), GRAY);

        // horizontal grid lines
        for (int i = -80; i <= 90; i += 10)
            tft.drawLine (MP_X0+MP_TL, MP_E2Y(deg2rad(i)), MP_X0 + MP_PW, MP_E2Y(deg2rad(i)), dark);
        tft.drawLine (MP_X0+MP_TL, MP_E2Y(0), MP_X0 + MP_PW, MP_E2Y(0), GRAY);

        // y labels
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(BRGRAY);
        tft.setCursor (MP_X0 - 20, MP_E2Y(M_PI_2F) - 4);
        tft.print ("+90");
        tft.setCursor (MP_X0 - 10, MP_E2Y(0) - 4);
        tft.print ("0");
        tft.setCursor (MP_X0 - 20, MP_E2Y(-M_PI_2F) - 4);
        tft.print ("-90");
        tft.setCursor(MP_X0-17, MP_E2Y(deg2rad(50)));
        tft.print(F("Up"));
        tft.setCursor(MP_X0-29, MP_E2Y(deg2rad(-45)));
        tft.print(F("Down"));
        const char estr[] = "Elevation";
        const int estr_l = strlen(estr);
        for (int i = 0; i < estr_l; i++) {
            tft.setCursor(MP_X0-42, MP_E2Y(deg2rad(45-10*i)));
            tft.print(estr[i]);
        }

        // y tick marks
        for (int i = -80; i <= 90; i += 10)
            tft.drawLine (MP_X0, MP_E2Y(deg2rad(i)), MP_X0+MP_TL, MP_E2Y(deg2rad(i)), BRGRAY);

        // time zone labels
        uint16_t de_y = MP_E2Y(-M_PI_2F) + 6;
        uint16_t dx_y = MP_E2Y(-M_PI_2F) + MP_BB/2 - 4;
        uint16_t utc_y = MP_E2Y(-M_PI_2F) + MP_BB - 6-8;
        tft.setTextColor (DE_COLOR);
        tft.setCursor (MP_X0-53, de_y);
        tft.print ("DE hour");
        tft.setTextColor (DX_COLOR);
        tft.setCursor (MP_X0-53, dx_y);
        tft.print ("DX");
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (MP_X0-53, utc_y);
        tft.print ("UTC");

        // x axis time line and vertical grid lines, mark each even hour.
        // N.B. check every 15 minutes for oddball time zones (looking at you Australia)
        tft.drawLine (MP_X0, dx_y-3, MP_X0+MP_PW, dx_y-3, BRGRAY);
        tft.drawLine (MP_X0, utc_y-3, MP_X0+MP_PW, utc_y-3, BRGRAY);
        int prev_de_hr = hour (t0 + de_tz.tz_secs);
        int prev_dx_hr = hour (t0 + dx_tz.tz_secs);
        int prev_utc_hr = hour (t0);
        for (time_t t = 900*(t0/900+1); t < t0 + MP_DUR; t += 900) {

            // get x coord of this time
            uint16_t x = MP_T2X(t);

            // get times in each zone
            int de_hr = hour (t + de_tz.tz_secs);
            int dx_hr = hour (t + dx_tz.tz_secs);
            int utc_hr = hour (t);

            // plot each time zone every 2 hours
            if (prev_de_hr != de_hr && (de_hr%2)==0) {
                tft.drawLine (x, MP_E2Y(-M_PI_2F), x, MP_E2Y(M_PI_2F), dark);
                tft.drawLine (x, MP_E2Y(-M_PI_2F), x, MP_E2Y(-M_PI_2F)-MP_TL, RA8875_WHITE);
                tft.setTextColor (DE_COLOR);
                tft.setCursor (x-(de_hr<10?3:6), de_y);         // center X or XX
                tft.print (de_hr);
            }
            if (prev_dx_hr != dx_hr && (dx_hr%2)==0) {
                tft.drawLine (x, dx_y-3, x, dx_y-3-MP_TL, RA8875_WHITE);
                tft.setTextColor (DX_COLOR);
                tft.setCursor (x-(dx_hr<10?3:6), dx_y);
                tft.print (dx_hr);
            }
            if (prev_utc_hr != utc_hr && (utc_hr%2)==0) {
                tft.drawLine (x, utc_y-3, x, utc_y-3-MP_TL, RA8875_WHITE);
                tft.setTextColor (RA8875_WHITE);
                tft.setCursor (x-(utc_hr<10?3:6), utc_y);
                tft.print (utc_hr);
            }

            // retain for next loop
            prev_de_hr = de_hr;
            prev_dx_hr = dx_hr;
            prev_utc_hr = utc_hr;
        }
}

/* draw both elevation plots.
 * return rough start and end times +- MP_DT of first period in which moon is up for both, with complications:
 *   start == t0 means plot period started both-up;
 *   end == 0 means both-up never ended within plot duration;
 *   both above means always both-up;
 *   start == 0 means never both-up, end has no meaning
 * t0 is nowWO()
 */
static void drawMPElPlot (time_t t0, time_t &t_start, time_t &t_end)
{
        resetWatchdog();

        // reset start/end so we can set with first occurance
        t_start = t_end = 0;

        // previous location in order to build line segments and find when both just up or down
        uint16_t prev_x = 0, prev_de_y = 0, prev_dx_y = 0;
        bool prev_both_up = false;

        // handy
        uint16_t x_step = MP_T2X(MP_DT) - MP_T2X(0);    // time step x change
        uint16_t elm90y = MP_E2Y(deg2rad(-90));         // y of -90 el

        // work across plot
        for (time_t t = t0; t <= t0 + MP_DUR; t += MP_DT) {
            resetWatchdog();

            // find circumstances at time t
            AstroCir de_ac, dx_ac;
            getLunarCir (t, de_ll, de_ac);
            getLunarCir (t, dx_ll, dx_ac);
            uint16_t de_y = MP_E2Y(de_ac.el);
            uint16_t dx_y = MP_E2Y(dx_ac.el);
            uint16_t x = MP_T2X(t);

            // check both_up
            bool both_up = de_ac.el > 0 && dx_ac.el > 0;

            // emphasize when both up
            if (!prev_both_up && both_up) {
                // approximate this starting half step left of x .. beware left edge
                uint16_t left_x = x - x_step/2;
                if (left_x < MP_X0)
                    left_x = MP_X0;
                tft.fillRect (left_x, elm90y-MP_TT, x_step/2 + 1, MP_TT, MP_FC);
            } else if (prev_both_up && both_up) {
                // mark entire step
                tft.fillRect (prev_x, elm90y-MP_TT, x_step + 1, MP_TT, MP_FC);
            } else if (prev_both_up && !both_up) {
                // approximate this stopping half step right of prev_x .. beware right edge
                uint16_t width = x_step/2;
                if (x + width > MP_X0 + MP_PW)
                    width = MP_X0 + MP_PW - x;
                tft.fillRect (prev_x, elm90y-MP_TT, width, MP_TT, MP_FC);
            }

            // continue line segment connected to previous location
            if (t > t0) {
                tft.drawLine (prev_x, prev_de_y, x, de_y, DE_COLOR);
                tft.drawLine (prev_x, prev_dx_y, x, dx_y, DX_COLOR);
            }

            // note when first both up or down
            if (both_up) {
                if (t_start == 0)
                    t_start = t;
            } else if (prev_both_up) {
                if (t_end == 0)
                    t_end = t;
            }

            // save for next iteration
            prev_x = x;
            prev_de_y = de_y;
            prev_dx_y = dx_y;
            prev_both_up = both_up;
        }

        Serial.printf (_FX("MP: rough start %02d:%02d end %02d:%02d\n"),
                                hour(t_start), minute(t_start),
                                hour(t_end), minute(t_end));
}

/* given plot start time and approximate times for both-up start and end, refine and draw table.
 * N.B. see drawMPElPlot comments for special cases.
 */
static void drawMPBothUpTable (time_t t0, time_t t_start, time_t t_end)
{
        bool always_both_up = t_start == t0 && !t_end;
        bool never_both_up = t_start == 0;
        bool finite_both_up = !always_both_up && !never_both_up;
        char buf[50];

        // search around times in finer steps to refine to nearest MP_US
        time_t better_start = 0, better_end = 0;
        if (finite_both_up) {
            AstroCir de_ac, dx_ac;

            // find better start unless now
            if (t_start > t0) {
                bool both_up = true;
                for (better_start = t_start - MP_US; both_up; better_start -= MP_US) {
                    getLunarCir (better_start, de_ll, de_ac);
                    getLunarCir (better_start, dx_ll, dx_ac);
                    both_up = de_ac.el > 0 && dx_ac.el > 0;
                }
                better_start += 2*MP_US;            // return to last known both_up
            } else {
                better_start = t0;
            }

            // find better end
            bool both_up = false;
            for (better_end = t_end - MP_US; !both_up; better_end -= MP_US) {
                getLunarCir (better_end, de_ll, de_ac);
                getLunarCir (better_end, dx_ll, dx_ac);
                both_up = de_ac.el > 0 && dx_ac.el > 0;
            }
            better_end += 2*MP_US;              // return to last known !both_up

            Serial.printf (_FX("MP: better start %02d:%02d end %02d:%02d\n"),
                                hour(better_start), minute(better_start),
                                hour(better_end), minute(better_end));
        }

        // table title
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (map_b.x+MP_NI, map_b.y+5);
        if (always_both_up) {
            tft.print (F("Both always up"));
            return;
        }
        if (never_both_up) {
            tft.print (F("Never both up"));
            return;
        }
        int dt = better_end - better_start;
        snprintf (buf, sizeof(buf), _FX("Next both up %02dh%02d"), dt/3600, (dt%3600)/60);
        tft.print (buf);


        // DE row
        if (better_start == t0)  {
            snprintf (buf, sizeof(buf), _FX("DE    now    %02d:%02d"),
                    hour(better_end+de_tz.tz_secs), minute(better_end+de_tz.tz_secs));
        } else {
            snprintf (buf, sizeof(buf), _FX("DE   %02d:%02d   %02d:%02d"),
                    hour(better_start+de_tz.tz_secs), minute(better_start+de_tz.tz_secs),
                    hour(better_end+de_tz.tz_secs), minute(better_end+de_tz.tz_secs));
        }
        tft.setTextColor (DE_COLOR);
        tft.setCursor (map_b.x+MP_NI, map_b.y+15);
        tft.print (buf);

        // DX row
        if (better_start == t0)  {
            snprintf (buf, sizeof(buf), _FX("DX    now    %02d:%02d"),
                    hour(better_end+dx_tz.tz_secs), minute(better_end+dx_tz.tz_secs));
        } else {
            snprintf (buf, sizeof(buf), _FX("DX   %02d:%02d   %02d:%02d"),
                    hour(better_start+dx_tz.tz_secs), minute(better_start+dx_tz.tz_secs),
                    hour(better_end+dx_tz.tz_secs), minute(better_end+dx_tz.tz_secs));
        }
        tft.setTextColor (DX_COLOR);
        tft.setCursor (map_b.x+MP_NI, map_b.y+25);
        tft.print (buf);

        // UTC rows
        if (better_start == t0)  {
            snprintf (buf, sizeof(buf), _FX("UTC   now    %02d:%02d"),
                    hour(better_end), minute(better_end));
        } else {
            snprintf (buf, sizeof(buf), _FX("UTC  %02d:%02d   %02d:%02d"),
                    hour(better_start), minute(better_start),
                    hour(better_end), minute(better_end));
        }
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (map_b.x+MP_NI, map_b.y+35);
        tft.print (buf);
}

/* draw popup in the given box for time t
 */
static void drawMPPopup (const time_t t, const SBox &popup_b)
{
        resetWatchdog();

        // circumstances at t
        AstroCir de_ac, dx_ac;
        getLunarCir (t, de_ll, de_ac);
        getLunarCir (t, dx_ll, dx_ac);

        // prep popup rectangle
        fillSBox (popup_b, RA8875_BLACK);
        drawSBox (popup_b, RA8875_WHITE);

        // draw column headings
        tft.setTextColor (RA8875_WHITE);
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setCursor (popup_b.x+4, popup_b.y+2);
        tft.print (F("     Time   Az   El"));

        // draw time, el and az at each location
        char buf[100];

        snprintf (buf, sizeof(buf), _FX("DE  %02d:%02d  %3.0f %4.0f"), hour(t+de_tz.tz_secs),
                minute(t+de_tz.tz_secs), rad2deg(de_ac.az), rad2deg(de_ac.el));
        tft.setCursor (popup_b.x+4, popup_b.y+14);
        tft.setTextColor(DE_COLOR);
        tft.print (buf);

        snprintf (buf, sizeof(buf), _FX("DX  %02d:%02d  %3.0f %4.0f"), hour(t+dx_tz.tz_secs),
                minute(t+dx_tz.tz_secs), rad2deg(dx_ac.az), rad2deg(dx_ac.el));
        tft.setCursor (popup_b.x+4, popup_b.y+24);
        tft.setTextColor(DX_COLOR);
        tft.print (buf);

        snprintf (buf, sizeof(buf), _FX("UTC %02d:%02d"), hour(t), minute(t));
        tft.setCursor (popup_b.x+4, popup_b.y+34);
        tft.setTextColor(RA8875_WHITE);
        tft.print (buf);

        // now
        tft.drawPR();
}

/* plot lunar elevation vs time on map_b. time goes forward a few days. label in DE DX local and UTC.
 */
void drawMoonElPlot()
{
        // start now
        time_t t0 = nowWO();

        // erase
        fillSBox (map_b, RA8875_BLACK);

        // draw boilerplate
        drawMPSetup (t0);

        // draw elevation plot, find first period when both up
        time_t t_start, t_end;
        drawMPElPlot (t0, t_start, t_end);

        // refine and draw both-up table
        drawMPBothUpTable (t0, t_start, t_end);

        // create resume button box
        SBox resume_b;
        resume_b.w = 100;
        resume_b.x = map_b.x + map_b.w - resume_b.w - MP_RB;
        resume_b.h = 40;
        resume_b.y = map_b.y + 4;
        const char button_name[] = "Resume";
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        drawStringInBox (button_name, resume_b, false, RA8875_GREEN);

        // see it all now
        tft.drawPR();

        // popup history for erasing
        bool popup_is_up = false;
        SBox popup_b = {0,0,0,0};

        // report info for tap times until time out or tap Resume button
        SCoord s;
        char c;
        UserInput ui = {
            map_b,
            NULL,
            false,
            MP_TO,
            true,
            s,
            c,
        };
        while (waitForUser(ui)) {

            // done if return, esc or tap Resume button
            if (c == '\r' || c == '\n' || c == 27 || inBox (s, resume_b))
                break;

            // first erase previous popup, if any
            if (popup_is_up) {
                fillSBox (popup_b, RA8875_BLACK);
                drawMPSetup (t0);
                drawMPElPlot (t0, t_start, t_end);
                popup_is_up = false;
            }

            // show new popup if tap within the plot area
            if (s.x > MP_X0 && s.x < MP_X0 + MP_PW && s.y > MP_E2Y(M_PI_2F) && s.y < MP_E2Y(-M_PI_2F)) {

                resetWatchdog();

                // popup at s
                popup_b.x = s.x;
                popup_b.y = s.y;
                popup_b.w = 122;
                popup_b.h = 45;

                // insure entirely over plot
                if (popup_b.x + popup_b.w > MP_X0 + MP_PW)
                    popup_b.x = MP_X0 + MP_PW - popup_b.w;
                if (popup_b.y + popup_b.h > MP_E2Y(-M_PI_2F) - MP_MT)
                    popup_b.y = MP_E2Y(-M_PI_2F) - MP_MT - popup_b.h;

                // draw popup
                drawMPPopup (MP_X2T(s.x), popup_b);

                // note popup is now up
                popup_is_up = true;
            }
        }

        // ack
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        drawStringInBox (button_name, resume_b, true, RA8875_GREEN);
        tft.drawPR();


}
