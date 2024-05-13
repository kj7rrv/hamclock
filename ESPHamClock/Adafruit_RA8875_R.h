/* this is the same interface as Adafruit_RA8875 with a few additions so 
 * on UNIX systems it can draw on X11 or RPi /dev/fb0.
 *
 * N.B. we only remimplented the functions we use, we don't claim this works with everything.
 */

#ifndef _Adafruit_RA8875_R_H
#define _Adafruit_RA8875_R_H

#include <stdarg.h>

#include <Adafruit_GFX.h>
#include <Adafruit_RA8875.h>

class Adafruit_RA8875_R : public Adafruit_RA8875 {

    public:

	Adafruit_RA8875_R(uint8_t CS, uint8_t RST) : Adafruit_RA8875::Adafruit_RA8875(CS, RST)
	{
	}

	void textSetCursor(uint16_t x, uint16_t y)
	{
	    if (rotation == 2) {
		x = width() - 1 - x;
		y = height() - 1 - y;
	    }
	    Adafruit_RA8875::textSetCursor(x, y);
	}

	void printf (const char *fmt, ...)
	{
	    char line[1024];
	    va_list ap;
	    va_start (ap, fmt);
	    vsnprintf (line, sizeof(line), fmt, ap);
	    va_end (ap);
	    Adafruit_RA8875::print(line);
	}

	void drawPixel(int16_t x, int16_t y, uint16_t color)
	{
	    if (rotation == 2) {
		x = width() - 1 - x;
		y = height() - 1 - y;
	    }
	    Adafruit_RA8875::drawPixel(x, y, color);
	}

	void drawPixels (uint16_t *p, uint32_t count, int16_t x, int16_t y)
	{
	    if (rotation == 2) {
		x = width() - 1 - x - (count-1);
		y = height() - 1 - y;
	    }
	    Adafruit_RA8875::drawPixels (p, count, x, y);
	}

	void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		x1 = width()  - 1 - x1;
		y1 = height() - 1 - y1;
	    }
	    Adafruit_RA8875::drawLine(x0, y0, x1, y1, color);
	}

        // non-standard
	void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thickness, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		x1 = width()  - 1 - x1;
		y1 = height() - 1 - y1;
	    }
            Adafruit_RA8875::drawLine(x0, y0, x1, y1, thickness, color);
	}

        // non-standard
	void drawLineRaw (int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thickness, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		x1 = width()  - 1 - x1;
		y1 = height() - 1 - y1;
	    }
            Adafruit_RA8875::drawLineRaw (x0, y0, x1, y1, thickness, color);
	}

	void drawRect (int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		w = -w;
		h = -h;
	    }
	    Adafruit_RA8875::drawRect(x0, y0, w, h, color);
	}

        // non-standard
	void drawRectRaw (int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width() - 1 - x0;
		y0 = height() - 1 - y0;
	    }
            Adafruit_RA8875::drawRectRaw (x0, y0, w, h, color);
	}

	void fillRect(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		w = -w;
		h = -h;
	    }
	    Adafruit_RA8875::fillRect(x0, y0, w, h, color);
	}

        // non-standard
	void fillRectRaw(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		w = -w;
		h = -h;
	    }
            Adafruit_RA8875::fillRectRaw (x0, y0, w, h, color);
	}

	void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width() - 1 - x0;
		y0 = height() - 1 - y0;
	    }
	    Adafruit_RA8875::drawCircle(x0, y0, r, color);
	}

        // non-standard
	void drawCircleRaw(int16_t x0, int16_t y0, int16_t r, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width() - 1 - x0;
		y0 = height() - 1 - y0;
	    }
            Adafruit_RA8875::drawCircleRaw (x0, y0, r, color);
	}

	void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width() - 1 - x0;
		y0 = height() - 1 - y0;
	    }
	    Adafruit_RA8875::fillCircle(x0, y0, r, color);
	}

        // non-standard
	void fillCircleRaw(int16_t x0, int16_t y0, int16_t r, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width() - 1 - x0;
		y0 = height() - 1 - y0;
	    }
            Adafruit_RA8875::fillCircleRaw (x0, y0, r, color);
	}

	void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		x1 = width()  - 1 - x1;
		y1 = height() - 1 - y1;
		x2 = width()  - 1 - x2;
		y2 = height() - 1 - y2;
	    }
	    Adafruit_RA8875::drawTriangle(x0, y0, x1, y1, x2, y2, color);
	}

	void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		x1 = width()  - 1 - x1;
		y1 = height() - 1 - y1;
		x2 = width()  - 1 - x2;
		y2 = height() - 1 - y2;
	    }
            Adafruit_RA8875::fillTriangle(x0, y0, x1, y1, x2, y2, color);
	}

        void setTextWrap (bool on)
        {
            (void) on;      // not used
        }

        // non-standard
        void fillPolygon (const SCoord poly[], int n_poly, uint16_t color)
        {
            if (n_poly < 1)
                return;
            if (n_poly == 1)
                drawPixel (poly[0].x, poly[0].y, color);
            else if (n_poly == 2)
                drawLine (poly[0].x, poly[0].y, poly[1].x, poly[1].y, color);
            else {
                // raster scan each triangle sweeping from first point -- assumes convex shape
                for (int i = 1; i < n_poly-1; i++) {
                    Adafruit_RA8875::fillTriangle (poly[0].x, poly[0].y, poly[i].x, poly[i].y,
                                            poly[i+1].x, poly[i+1].y, color);
                }
            }
        }

        // non-standard
        void drawPolygon (const SCoord poly[], int n_poly, uint16_t color)
        {
            if (n_poly < 1)
                return;
            if (n_poly == 1)
                drawPixel (poly[0].x, poly[0].y, color);
            else if (n_poly == 2)
                drawLine (poly[0].x, poly[0].y, poly[1].x, poly[1].y, color);
            else {
                for (int i = 0; i < n_poly-1; i++)
                    Adafruit_RA8875::drawLine (poly[i].x, poly[i].y, poly[i+1].x, poly[i+1].y, color);
                Adafruit_RA8875::drawLine (poly[n_poly-1].x, poly[n_poly-1].y, poly[0].x, poly[0].y, color);
            }
        }

};

#endif // _Adafruit_RA8875_R_H
