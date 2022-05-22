/* initial seed of radio control idea.
 * first attempt was simple bit-bang serial to kx3 to set frequency for a spot.
 * now we add hamlib's rigctld and w1hkj's flrig to rig they support without needing _SUPPORT_KX3.
 */


#include "HamClock.h"


/* setRigctldFreq helper to send the given command then read and discard response until find RPRT
 * N.B. we assume cmd already includes trailing \n
 */
static void sendHLCmd (WiFiClient &client, const char cmd[])
{
    // send
    Serial.printf ("RIG: %s", cmd);
    client.print(cmd);

    // absorb reply until fine RPRT
    char buf[64];
    bool ok = 0;
    do {
        ok = getTCPLine (client, buf, sizeof(buf), NULL);
        if (ok)
            Serial.printf ("  %s\n", buf);
    } while (ok && !strstr (buf, "RPRT"));
}

/* connect to rigctld and set the given frequency.
 * this can be used on all platforms.
 */
static void setRigctldFreq (float kHz)
{
    // get host and port, bale if nothing
    char host[NV_RIGHOST_LEN];
    int port;
    if (!getRigctld (host, &port))
        return;

    // connect, bale if can't
    WiFiClient rig_client;
    Serial.printf (_FX("RIG: %s:%d\n"), host, port);
    if (!wifiOk() || !rig_client.connect(host, port)) {
        Serial.printf (_FX("RIG: %s:%d failed\n"), host, port);
        return;
    }

    // stay alive
    updateClocks(false);
    resetWatchdog();

    // send setup commands, require RPRT for each but ignore error values
    #define _MAX_CMD_W 25
    static const char setup_cmds[][_MAX_CMD_W] PROGMEM = {
        "+\\set_split_vfo 0 VFOA\n",
        "+\\set_vfo VFOA\n",
        "+\\set_func RIT 0\n",
        "+\\set_rit 0\n",
        "+\\set_func XIT 0\n",
        "+\\set_xit 0\n",
    };
    for (unsigned i = 0; i < NARRAY(setup_cmds); i++) {
        char cmd[_MAX_CMD_W];
        strcpy_P (cmd, setup_cmds[i]);
        sendHLCmd (rig_client, cmd);
    }

    // send freq
    char fcmd[32];
    snprintf (fcmd, sizeof(fcmd), "+\\set_freq %d\n", (int)(kHz*1000));
    sendHLCmd (rig_client, fcmd);

    // finished
    rig_client.stop();
}

/* setFlrigFreq helper to send and xml-rpc command and discard response
 */
static void sendXMLRPCCmd (WiFiClient &client, const char cmd[], const char value[], const char type[])
{
    static const char hdr_fmt[] PROGMEM =
        "POST /RPC2 HTTP/1.1\r\n"
        "Content-Type: text/xml\r\n"
        "Content-length: %d\r\n"
        "\r\n"
        "%s"
    ;
    static const char body_fmt[] PROGMEM =
        "<?xml version=\"1.0\" encoding=\"us-ascii\"?>\r\n"
        "<methodCall>\r\n"
        "    <methodName>%.50s</methodName>\r\n"
        "    <params>\r\n"
        "        <param><value><%.10s>%.50s</%.10s></value></param>\r\n"
        "    </params>\r\n"
        "</methodCall>\r\n"
    ;

    // copy each to mem
    StackMalloc hdr_fmt_mem(sizeof(hdr_fmt));
    strcpy_P (hdr_fmt_mem.getMem(), hdr_fmt);
    StackMalloc body_fmt_mem(sizeof(body_fmt));
    strcpy_P (body_fmt_mem.getMem(), body_fmt);

    // format body
    #define _BODY_SIZ (sizeof(body_fmt)+150)       // guard with %.Xs in body_fmt[]
    StackMalloc body_mem(_BODY_SIZ);
    char *body_buf = body_mem.getMem();
    int body_l = snprintf (body_buf, _BODY_SIZ, body_fmt_mem.getMem(), cmd, type, value, type);

    // format complete message
    #define _MSG_SIZ (sizeof(hdr_fmt)+20+body_l)
    StackMalloc msg_mem(_MSG_SIZ);
    char *msg_buf = msg_mem.getMem();
    snprintf (msg_buf, _MSG_SIZ, hdr_fmt_mem.getMem(), body_l, body_buf);

    // send
    Serial.printf (_FX("FLRIG: %s %s %s\n"), cmd, value, type);
    client.print(msg_buf);

    // absorb reply until </methodResponse>
    bool ok = 0;
    do {
        ok = getTCPLine (client, msg_buf, _MSG_SIZ, NULL);
        if (ok)
            Serial.printf ("  %s\n", msg_buf);
    } while (ok && !strstr (msg_buf, _FX("</methodResponse>")));
}

/* connect to flrig and set the given frequency.
 * this can be used on all platforms.
 */
static void setFlrigFreq (float kHz)
{
    // get host and port, bale if nothing
    char host[NV_FLRIGHOST_LEN];
    int port;
    if (!getFlrig (host, &port))
        return;

    // connect, bale if can't
    WiFiClient flrig_client;
    Serial.printf (_FX("FLRIG: %s:%d\n"), host, port);
    if (!wifiOk() || !flrig_client.connect(host, port)) {
        Serial.printf (_FX("FLRIG: %s:%d failed\n"), host, port);
        return;
    }

    // send commands
    char value[20];
    snprintf (value, sizeof(value), "%.0f", kHz*1000);
    sendXMLRPCCmd (flrig_client, _FX("rig.set_split"), "0", "int");
    sendXMLRPCCmd (flrig_client, _FX("rig.set_vfoA"), value, "double");

    // finished
    flrig_client.stop();
}


#if defined(_SUPPORT_KX3)

/* cleanup commands before changing freq:
 *
 *   SB0 = Set Sub Receiver or Dual Watch off
 *   FR0 = Cancel Split on K2, set RX vfo A
 *   FT0 = Set tx vfo A
 *   RT0 = Set RIT off
 *   XT0 = Set XIT off
 *   RC  = Set RIT / XIT to zero
 */
static char cleanup_cmds[] = ";SB0;FR0;FT0;RT0;XT0;RC;";

/* sprintf format to set new frequency, requires float in Hz
 */
static char setfreq_fmt[] = ";FA%011.0f;";


#if defined(_GPIO_ESP)


/**********************************************************************************
 *
 *
 *  send spot frequency to Elecraft radio on Huzzah pin 15.
 *
 *
 **********************************************************************************
 */
 




/* send one bit @ getKX3Baud().
 * N.B. they want mark/sense inverted
 */
static void sendOneBit (uint8_t hi)
{
    uint32_t t0 = ESP.getCycleCount();
    digitalWrite (Elecraft_GPIO, !hi);
    uint32_t bit_time = ESP.getCpuFreqMHz()*1000000UL/getKX3Baud();
    while (ESP.getCycleCount()-t0 < bit_time)
        continue;
}

/* perform one-time preparation for sending commands
 */
static void prepIO()
{
    pinMode(Elecraft_GPIO, OUTPUT);
    sendOneBit (1);
}

/* send the given command.
 */
static void sendOneMessage (const char cmd[])
{
    Serial.printf (_FX("Elecraft: %s\n"), cmd);

    // send each char, 8N1, MSByte first
    char c;
    while ((c = *cmd++) != '\0') {

        // disable interrupts for a clean transmit
        cli();

        sendOneBit (0);                                 // start bit
        for (uint8_t bit = 0; bit < 8; bit++) {         // LSBit first
            sendOneBit (c & 1);
            c >>= 1;
        }
        sendOneBit (1);                                 // stop bit

        // resume interrupts
        sei();
    }
}




/* command radio to the given frequency.
 */
void setRadioSpot (float kHz)
{
    // always try rigctld and flrig
    setRigctldFreq (kHz);
    setFlrigFreq (kHz);

    // ignore if not to use GPIO or baud 0
    if (!GPIOOk() || getKX3Baud() == 0)
        return;

    resetWatchdog();

    // one-time IO setup
    static bool ready;
    if (!ready) {
        prepIO();
        ready = true;
        Serial.println (F("Elecraft: ready"));
    }

    // send cleanup commands
    sendOneMessage (cleanup_cmds);

    // format and send command to change frequency
    char buf[30];
    (void) sprintf (buf, setfreq_fmt, kHz*1e3);
    sendOneMessage (buf);
}

#elif defined(_SUPPORT_GPIO)




/**********************************************************************************
 *
 *
 * hack to send spot frequency to Elecraft radio on RPi GPIO 14 (header pin 8).
 * can not use HW serial because Electraft wants inverted mark/space, thus
 * timing will not be very good.
 *
 *
 **********************************************************************************
 */
 



#include <time.h>
#include "GPIO.h"

/* return our gpio pins to quiescent state
 */
void radioResetIO()
{
    if (!GPIOOk())
        return;
    GPIO& gpio = GPIO::getGPIO();
    if (!gpio.isReady())
        return;
    gpio.setAsInput(Elecraft_GPIO);
}

/* send one bit @ getKX3Baud(), bit time multiplied by correction factor.
 * N.B. they want mark/sense inverted
 * N.B. this can be too long depending on kernel scheduling. Performance might be improved by
 *      assigning this process to a dedicated processor affinity and disable being scheduled using isolcpus.
 *      man pthread_setaffinity_np
 *      https://www.kernel.org/doc/html/v4.10/admin-guide/kernel-parameters.html
 */
static void sendOneBit (int hi, float correction)
{
    // get time now
    struct timespec t0, t1;
    clock_gettime (CLOCK_MONOTONIC, &t0);

    // set bit (remember: Elecraft wants inverted mark/sense)
    GPIO& gpio = GPIO::getGPIO();
    gpio.setHiLo (Elecraft_GPIO, !hi);

    // wait for one bit duration with modified correction including nominal correction
    uint32_t baud = getKX3Baud();
    float overhead = 1.0F - 0.08F*baud/38400;          // measured on pi 4
    unsigned long bit_ns = 1000000000UL/baud*overhead*correction;
    unsigned long dt_ns;
    do {
        clock_gettime (CLOCK_MONOTONIC, &t1);
        dt_ns = 1000000000UL*(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec);
    } while (dt_ns < bit_ns);
}

/* perform one-time preparation for sending commands
 */
static void prepIO()
{
    // init Elecraft pin
    GPIO& gpio = GPIO::getGPIO();
    gpio.setAsOutput(Elecraft_GPIO);
    sendOneBit (1, 1.0F);
}


/* send the given string with the given time correction factor.
 * return total nsecs.
 */
static uint32_t sendOneString (float correction, const char str[])
{
    // get current scheduler and priority
    int orig_sched = sched_getscheduler(0);
    struct sched_param orig_param;
    sched_getparam (0, &orig_param);

    // attempt setting high priority
    struct sched_param hi_param;
    hi_param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    bool hipri_ok = sched_setscheduler (0, SCHED_FIFO, &hi_param) == 0;
    if (!hipri_ok)
        printf (_FX("Failed to set new prioity %d: %s\n"), hi_param.sched_priority, strerror(errno));

    // get starting time
    struct timespec t0, t1;
    clock_gettime (CLOCK_MONOTONIC, &t0);

    // send each char, 8N1, MSByte first
    char c;
    while ((c = *str++) != '\0') {
        sendOneBit (0, correction);                         // start bit
        for (uint8_t bit = 0; bit < 8; bit++) {             // LSBit first
            sendOneBit (c & 1, correction);                 // data bit
            c >>= 1;
        }
        sendOneBit (1, correction);                         // stop bit
    }

    // record finish time
    clock_gettime (CLOCK_MONOTONIC, &t1);

    // restore original priority
    if (hipri_ok)
        sched_setscheduler (0, orig_sched, &orig_param);

    // return duration in nsec
    return (1000000000UL*(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec));
}

/* send the given command.
 */
static void sendOneMessage (const char cmd[])
{
    // len
    size_t cmd_l = strlen(cmd);

    // compute ideal time to send command
    uint32_t bit_ns = 1000000000UL/getKX3Baud();           // ns per bit
    uint32_t cmd_ns = cmd_l*10*bit_ns;                     // start + 8N1
  
    // send with no speed correction
    uint32_t ns0 = sendOneString (1.0F, cmd);

    // compute measured correction factor
    float correction = (float)cmd_ns/ns0;

    // repeat if correction more than 1 percent
    uint32_t ns1 = 0;
    if (correction < 0.99F || correction > 1.01F) {
        usleep (500000);    // don't pummel
        ns1 = sendOneString (correction, cmd);
    }

    printf (_FX("Elecraft: correction= %g cmd= %u ns0= %u ns1= %u ns\n"), correction, cmd_ns, ns0, ns1);

}


/* command radio to the given frequency.
 */
void setRadioSpot (float kHz)
{
    // always try rigctld and flrig
    setRigctldFreq (kHz);
    setFlrigFreq (kHz);

    // ignore if not to use GPIO or baud 0
    if (!GPIOOk() || getKX3Baud() == 0)
        return;

    // one-time IO setup
    resetWatchdog();
    static bool ready;
    if (!ready) {
        prepIO();
        ready = true;
        Serial.println (F("Elecraft: ready"));
    }

    // send cleanup commands
    sendOneMessage (cleanup_cmds);

    // format and send command to change frequency
    char buf[30];
    (void) sprintf (buf, setfreq_fmt, kHz*1e3);
    sendOneMessage (buf);
}

#endif // _SUPPORT_GPIO

#else  // !_SUPPORT_KX3


/* same for system without any gpio
 */
void setRadioSpot (float kHz)
{
    // always try rigctld and flrig
    setRigctldFreq (kHz);
    setFlrigFreq (kHz);
}

#endif // _SUPPORT_KX3

