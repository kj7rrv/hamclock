#ifndef _SERIAL_H
#define _SERIAL_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "Arduino.h"

class Serial {

    public:

	void begin (int baud);

	operator bool();

        void print (void);
	void print (char c);
	void print (char *s);
	void print (const char *s);
	void print (int i);
	void print (String s);
        void println (void);
	void println (char *s);
	void println (const char *s);
	void println (int i);

    #if defined(__GNUC__)
        int printf (const char *msg, ...) __attribute__ ((format (__printf__, 2, 3))); // must include _this_
    #else
	int printf (const char *fmt, ...);
    #endif

};

extern class Serial Serial;

#endif // _SERIAL_H
