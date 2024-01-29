/* manage perpetual thread dedicated to smooth LED blinking and digital polling.
 */

#include "HamClock.h"


/**********************************************************************************************
 *
 * blinker
 *
 */


#if defined (_IS_UNIX)

/* perpetual thread that repeatedly reads the given blinker hz as a desired rate to blink the given pin.
 * N.B. we assume hz type is small enough to be atomic and doesn't need to be locked
 */
static void * blinkerThread (void *vp)
{
    // get defining struct
    volatile ThreadBlinker &tb = *(ThreadBlinker *)vp;

    // init output pin to false
    mcp.pinMode (tb.pin, OUTPUT);
    mcp.digitalWrite (tb.pin, tb.on_is_low);

    // set our internal polling period and init counter there of.
    useconds_t delay_us = 10000;                // N.B. sets maximum rate that can be achieved
    unsigned n_delay = 0;                       // count of delay_us
    int prev_rate_hz = -100;                    // avoids needless io

    // forever check and implement what tb wants
    for(;;) {

        // end if disabled
        if (tb.disable)
            break;

        usleep (delay_us);
        if (tb.hz == BLINKER_ON_HZ) {
            // constant on
            if (tb.hz != prev_rate_hz)
                mcp.digitalWrite (tb.pin, !tb.on_is_low);
            n_delay = 0;
        } else if (tb.hz == BLINKER_OFF_HZ) {
            // constant off
            if (tb.hz != prev_rate_hz)
                mcp.digitalWrite (tb.pin, tb.on_is_low);
            n_delay = 0;
        } else {
            // blink at blinker rate
            unsigned rate_period_us = 1000000U/tb.hz;
            if (++n_delay*delay_us >= rate_period_us) {
                mcp.digitalWrite (tb.pin, !mcp.digitalRead (tb.pin));
                n_delay = 0;
            }
        }
        prev_rate_hz = tb.hz;
    }

    Serial.printf ("blinkerThread for pin %d exiting\n", tb.pin);
    return (NULL);
}

/* start a blinker thread for the given pin using the given control structure.
 * harmless to call multiple times with the same structure.
 */
void startBinkerThread (volatile ThreadBlinker &tb, int pin, bool on_is_low)
{
    if (!tb.started) {
        tb.started = true;            // don't retry if failed
        tb.pin = pin;
        tb.on_is_low = on_is_low;
        pthread_t tid;
        int e = pthread_create (&tid, NULL, blinkerThread, (void*)&tb); // make volatile again in thread
        if (e != 0)
            Serial.printf (_FX("blinker thread for pin %d failed: %s\n"), tb.pin, strerror(e));
    }
}

/* inform the given tb the given desired rate
 */
void setBlinkerRate (volatile ThreadBlinker &tb, int hz)
{
    tb.hz = hz;
}

/* tell a blinker thread to end
 */
void disableBlinker (volatile ThreadBlinker &tb)
{
    tb.disable = true;
}


#else   // ESP8266


/* ESP has no threads but we still init the pin
 */
void startBinkerThread (volatile ThreadBlinker &tb, int pin, bool on_is_low)
{
    if (!tb.started) {
        tb.started = true;
        tb.pin = pin;
        tb.on_is_low = on_is_low;
        mcp.pinMode (tb.pin, OUTPUT);
        mcp.digitalWrite (tb.pin, tb.on_is_low);
    }
}

/* ESP can't blink so just turn on or off
 */
void setBlinkerRate (volatile ThreadBlinker &tb, int hz)
{
    if (hz == BLINKER_OFF_HZ)
        mcp.digitalWrite (tb.pin, tb.on_is_low);
    else
        mcp.digitalWrite (tb.pin, !tb.on_is_low);
}


/* tell a blinker thread to end
 */
void disableBlinker (volatile ThreadBlinker &tb)
{
    tb.disable = true;
}

#endif // _IS_UNIX

/*
 *
 * /blinker
 *
 **********************************************************************************************/




/**********************************************************************************************
 *
 * poller
 *
 */


#if defined (_IS_UNIX)

/* perpetual thread that repeatedly reads a digital pin at MCPPoller.hz.
 * then client can query as needed with readMCPPoller without waiting inline.
 */
static void * pollerThread (void *vp)
{
    // get defining struct
    volatile MCPPoller &mp = *(MCPPoller *)vp;

    // init input pin
    mcp.pinMode (mp.pin, INPUT_PULLUP);

    // set our internal polling period
    useconds_t poll_period = 1000000/mp.hz;     // polling period

    // forever until disabled
    while (!mp.disable) {

        usleep (poll_period);
        mp.value = mcp.digitalRead (mp.pin);
    }

    Serial.printf ("wirepoller for pin %d exiting\n", mp.pin);
    return (NULL);
}

/* start a poller thread for the given input pin at the given rate using the given control structure.
 * harmless to call multiple times with the same structure.
 */
void startMCPPoller (volatile MCPPoller &mp, int pin, int hz)
{
    if (!mp.started) {
        mp.started = true;            // don't retry if failed
        mp.pin = pin;
        mp.hz = hz;
        pthread_t tid;
        int e = pthread_create (&tid, NULL, pollerThread, (void*)&mp); // make volatile again in thread
        if (e != 0)
            Serial.printf (_FX("poller thread for pin %d failed: %s\n"), mp.pin, strerror(e));
    }
}

/* read the poller pin state from mp
 */
bool readMCPPoller (volatile const MCPPoller &mp)
{
    return (mp.value);
}

/* tell a poller thread to end
 */
void disableMCPPoller (volatile MCPPoller &mp)
{
    mp.disable = true;
}


#else   // ESP8266


/* ESP has no threads but we still init the pin
 */
void startMCPPoller (volatile MCPPoller &mp, int pin, int hz)
{
    if (!mp.started) {
        mp.started = true;
        mp.pin = pin;
        mp.hz = hz;
        mcp.pinMode (mp.pin, INPUT_PULLUP);
    }
}

/* ESP has no choice but to read inline every time
 */
bool readMCPPoller (volatile const MCPPoller &mp)
{
    return (mcp.digitalRead (mp.pin));
}


/* tell a poller thread to end 
 */
void disableMCPPoller (volatile MCPPoller &mp)
{
    mp.disable = true;
}

#endif // _IS_UNIX

/*
 *
 * /poller
 *
 **********************************************************************************************/


