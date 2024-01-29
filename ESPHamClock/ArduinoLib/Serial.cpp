/* simple Serial.cpp
 */

#include "Arduino.h"
#include "Serial.h"


void Serial::begin (int baud)
{
    (void) baud;
}

void Serial::print (void)
{
}

void Serial::print (char c)
{
    printf ("%c", c);
}

void Serial::print (char *s)
{
    printf ("%s", s);
}

void Serial::print (const char *s)
{
    printf ("%s", s);
}

void Serial::print (int i)
{
    printf ("%d", i);
}

void Serial::print (String s)
{
    printf ("%s", s.c_str());
}

void Serial::println (void)
{
    printf ("\n");
}

void Serial::println (char *s)
{
    printf ("%s\n", s);
}

void Serial::println (const char *s)
{
    printf ("%s\n", s);
}

void Serial::println (int i)
{
    printf ("%d\n", i);
}

int Serial::printf (const char *fmt, ...)
{
    // prefix with millis()
    // N.B. don't call now() because getNTPUTC calls print which can get recursive
    uint32_t m = millis();
    fprintf (stdout, "%7u.%03u ", m/1000, m%1000);

    // now the message
    va_list ap;
    va_start (ap, fmt);
    int n = vprintf (fmt, ap);
    va_end (ap);
    fflush (stdout);

    // lint
    return (n);
}

Serial::operator bool()
{
    return (true);
}



class Serial Serial;
