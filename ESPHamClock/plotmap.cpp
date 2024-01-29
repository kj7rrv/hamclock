/* a generic mechanism to plot a server file on map_b
 */

#include "HamClock.h"

// layout
#define TOPB    46                      // top border
#define BOTB    20                      // bottom border
#define LEFTB   30                      // left border
#define RIGHTB  15                      // right border
#define TICKL   5                       // tick mark length
#define PLOTW   (map_b.w-LEFTB-RIGHTB)  // plot width
#define PLOTH   (map_b.h-TOPB-BOTB)     // plot height
#define AXISXL  (map_b.x + LEFTB)       // axis x left
#define AXISXR  (AXISXL + PLOTW)        // axis x right
#define AXISYT  (map_b.y + TOPB)        // axis y top
#define AXISYB  (AXISYT + PLOTH)        // axis y bottom

// colors
#define AXISC   BRGRAY                  // axis color
#define GRIDC   GRAY                    // grid color
#define LABELC  RA8875_WHITE            // label color
#define TITLEC  RA8875_WHITE            // title color

// data
#define NXTICKS 20                      // nominal number of x tickmarks
#define NYTICKS 10                      // nominal number of y tickmarks
#define DX2GX(x)  (AXISXL + (float)PLOTW*((x)-xticks[0])/(xticks[n_xticks-1]-xticks[0])) // data to graphics x
#define DY2GY(y)  (AXISYB - (float)PLOTH*((y)-yticks[0])/(yticks[n_yticks-1]-yticks[0])) // data to graphics y


/* read and plot the given server file.
 */
void plotMap (const char *filename, const char *title, uint16_t color)
{
    // erase
    fillSBox (map_b, RA8875_BLACK);

    // base of filename
    const char *file_slash = strrchr (filename, '/');
    const char *file_base = file_slash ? file_slash + 1 : filename;

    // read data
    float *x_data = NULL;
    float *y_data = NULL;
    float min_x = 1e10, max_x = -1e10;
    float min_y = 1e10, max_y = -1e10;
    int n_data = 0;
    WiFiClient map_client;
    bool ok = false;

    Serial.println (filename);
    if (wifiOk() && map_client.connect (backend_host, backend_port)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpHCGET (map_client, backend_host, filename);

        // skip response header
        if (!httpSkipHeader (map_client)) {
            mapMsg (true, 2000, _FX("%s: Header is short"), file_base);
            goto out;
        }

        // read lines, adding to x_data[] and y_data[]
        char line[100];
        while (getTCPLine (map_client, line, sizeof(line), NULL)) {

            // crack
            float x, y;
            if (sscanf (line, "%f %f", &x, &y) != 2) {
                Serial.printf (_FX("PMAP: bad line: %s\n"), line);
                mapMsg (true, 2000, _FX("%s: Data is corrupted"), file_base);
                goto out;
            }

            // grow
            float *new_x = (float *) realloc (x_data, (n_data+1) * sizeof(float));
            float *new_y = (float *) realloc (y_data, (n_data+1) * sizeof(float));
            if (!new_x || !new_y) {
                mapMsg (true, 2000, _FX("%s: Insufficient memory"), file_base);
                goto out;
            }

            // add
            x_data = new_x;
            y_data = new_y;
            x_data[n_data] = x;
            y_data[n_data] = y;
            n_data += 1;

            // find extrema
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }

        Serial.printf (_FX("PMAP: read %d points\n"), n_data);

        // require at least a few points
        if (n_data < 10) {
            mapMsg (true, 2000, _FX("%s: File is short"), file_base);
            goto out;
        }

        // ok!
        ok = true;
    }

out:

    if (ok) {

        // title
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (TITLEC);
        uint16_t tw = getTextWidth(title);
        tft.setCursor (map_b.x + (map_b.w - tw)/2, map_b.y + 30);
        tft.print (title);

        // find tickmarks
        float xticks[NXTICKS+2], yticks[NYTICKS+2];
        int n_xticks = tickmarks (min_x, max_x, NXTICKS, xticks);
        int n_yticks = tickmarks (min_y, max_y, NYTICKS, yticks);

        // draw axes
        tft.drawLine (AXISXL, AXISYT, AXISXL, AXISYB, AXISC);
        tft.drawLine (AXISXL, AXISYB, AXISXR, AXISYB, AXISC);

        // draw grids
        tft.setTextColor (LABELC);
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        for (int i = 0; i < n_xticks; i++) {
            uint16_t x = DX2GX(xticks[i]);
            tft.drawLine (x, AXISYT, x, AXISYB+TICKL, GRIDC);
            tft.setCursor (x-20, AXISYB+TICKL+4);
            tft.printf("%g", xticks[i]);
        }
        for (int i = 0; i < n_yticks; i++) {
            uint16_t y = DY2GY(yticks[i]);
            tft.drawLine (AXISXL-TICKL, y, AXISXR, y, GRIDC);
            tft.setCursor (AXISXL-LEFTB+1, y-4);
            tft.printf("%g", yticks[i]);
        }

        // finally the data
        uint16_t prev_x = 0, prev_y = 0;
        for (int i = 0; i < n_data; i++) {
            uint16_t x = DX2GX(x_data[i]);
            uint16_t y = DY2GY(y_data[i]);
            if (i > 0)
                tft.drawLine (prev_x, prev_y, x, y, color);
            prev_x = x;
            prev_y = y;
        }

        // create resume button box
        SBox resume_b;
        resume_b.w = 100;
        resume_b.x = map_b.x + map_b.w - resume_b.w - LEFTB;
        resume_b.h = 40;
        resume_b.y = map_b.y + 4;
        const char button_name[] = "Resume";
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        drawStringInBox (button_name, resume_b, false, RA8875_GREEN);

        // see it all now
        tft.drawPR();

        // report info for tap times until time out or tap Resume button
        SCoord s;
        char c;
        UserInput ui = {
            map_b,
            NULL,
            false,
            30000,
            true,
            s,
            c,
        };
        (void) waitForUser(ui);

        // ack
        drawStringInBox (button_name, resume_b, true, RA8875_GREEN);
        tft.drawPR();

    }


    // record mem usage before freeing
    printFreeHeap (F("plotMap"));

    // clean up, any error is already reported
    free (x_data);
    free (y_data);
    map_client.stop();
}
