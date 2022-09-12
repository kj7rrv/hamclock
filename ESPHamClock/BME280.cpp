/* support up to two Adafruit BME280 humidity, temperature & pressure sensors connected in I2C mode.
 */

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#include "HamClock.h"


// only possible addresses -- correspond to BME_76 and BME_77 indices
#define I2CADDR_1       0x76                    // always at [0] in data arrays below
#define I2CADDR_2       0x77                    // always at [1] in data arrays below

// polling management. total display period eventually approaches N_BME_READINGS * SLOWEST_DT
#define GOSLOWER        (5*60000L)              // take data more slowly after up this long, millis()
#define GOSLOWEST       (60*60000L)             // take data even more slowly after up this long, millis()
#define INITIAL_DT      (5*1000L)               // initial sensing period until GOSLOWER, millis()
#define SLOWER_DT       (60*1000L)              // sensing period after GOSLOWER, millis()
#define SLOWEST_DT      (900*1000L)             // sensing period after GOSLOWEST, millis()

// data management.
static const uint8_t bme_i2c[MAX_N_BME] = {I2CADDR_1, I2CADDR_2};    // N.B. match BME_76 and BME_77 indices
static BMEData *bme_data[MAX_N_BME];            // malloced queues, if found
static Adafruit_BME280 bme_io[MAX_N_BME];       // one for each potential sensor

// time management.
static uint32_t readDT = INITIAL_DT;            // period between readings, millis();
static uint32_t last_reading;                   // last time either sensor was read, millis()
static bool new_data;                           // whether new data has been read but not displayed

// appearance
#define TEMP_COLOR      0xFBEF
#define PRES_COLOR      RA8875_YELLOW
#define HUM_COLOR       RA8875_CYAN
#define DP_COLOR        RA8875_GREEN

/* try to connect to "all" sensors else try to reconnect to ones that originally worked.
 */
static void connectSensors(bool all)
{
#ifdef _SUPPORT_ENVSENSOR

    // skip if don't want external IO
    if (!GPIOOk())
        return;

    // try to (re)open each sensor
    for (int i = 0; i < MAX_N_BME; i++) {

        // skip unless all or succeeded before
        if (!all && !bme_data[i])
            continue;

        uint8_t addr = bme_i2c[i];
        Serial.printf (_FX("BME %strying 0x%x\n"), !bme_data[i] ? "" : "re", addr);
        Adafruit_BME280 &bme = bme_io[i];
        if (!bme.begin(addr)) {
            Serial.println (F("BME init fail"));
            continue;
        }

        // open worked: init if first time
        if (!bme_data[i]) {
            bme_data[i] = (BMEData *) calloc (1, sizeof(BMEData));
            bme_data[i]->i2c = addr;
        }

        // Forced mode sleeps until read; normal mode runs continuously and warms the sensor
        bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1, // temperature
                    Adafruit_BME280::SAMPLING_X1, // pressure
                    Adafruit_BME280::SAMPLING_X1, // humidity
                    Adafruit_BME280::FILTER_OFF,
                    Adafruit_BME280::STANDBY_MS_1000);

        // initial readings are a little flaky, read and discard temp until fairly stable
        #define _N_OK 10
        #define _N_TRY (5*_N_OK)
        #define _TOT_DT 5000    // max millis for entire test
        int n_stable = 0;
        float prev_t = 1e6;
        for (int i = 0; i < _N_TRY && n_stable < _N_OK; i++) {
            float t = bme.readTemperature();
            if (!isnan(t) && t > -40) {
                if (fabsf(t-prev_t) < 1)
                    n_stable++;
                else
                    n_stable = 0;
                prev_t = t;
            }
            wdDelay(_TOT_DT/_N_TRY);
        }

        if (n_stable == _N_OK)
            Serial.println (F("BME init success"));
        else
            Serial.println (F("BME not stable"));
    }

    if (getNBMEConnected() == 0)
        Serial.println(F("BME none found"));

#endif // _SUPPORT_ENVSENSOR
}

/* read the given temperature, pressure and humidity in units determined by useMetricUnits() into
 * next q enttry. if ok advance q and return if ok.
 */
static bool readSensor (int device)
{
    // skip if don't want any external IO
    if (!GPIOOk())
        return (false);

    // get data pointer, skip if not used
    BMEData *dp = bme_data[device];
    if (!dp)
        return (false);
    Adafruit_BME280 &bme = bme_io[device];

    // note attempt time whether or not we succeed
    last_reading = millis();

    // success?
    bool ok = false;

    // go
    resetWatchdog();
    bme.takeForcedMeasurement();
    float t = bme.readTemperature();                                                    // C
    float p = bme.readPressure();                                                       // Pascals
    float h = bme.readHumidity();                                                       // percent
    // Serial.printf ("BME Raw T %g P %g H %g\n", t, p, h);
    if (isnan(t) || t < -40 || isnan(p) || isnan(h)) {
        // try restarting
        Serial.printf (_FX("BME %x read err\n"), dp->i2c);
        connectSensors(false);
    } else {
        // all good
        if (useMetricUnits()) {
            // want C and hPa
            dp->t[dp->q_head] = BMEPACK_T(t + getBMETempCorr(device));                  // already C
            dp->p[dp->q_head] = BMEPACK_hPa(p/100 + getBMEPresCorr(device));            // Pascals to hPa
        } else {
            // want F and inches Hg
            dp->t[dp->q_head] = BMEPACK_T(1.8*t + 32.0 + getBMETempCorr(device));       // C to F
            dp->p[dp->q_head] = BMEPACK_inHg(p / 3386.39 + getBMEPresCorr(device));     // Pascals to in Hg
        }
        dp->h[dp->q_head] = BMEPACK_H(h);
        dp->u[dp->q_head] = now();

        // Serial.printf (_FX("BME %u %x %7.2f %7.2f %7.2f\n"), dp->u[dp->q_head], dp->i2c,
                            // BMEUNPACK_T(dp->t[dp->q_head]), BMEUNPACK_P(dp->p[dp->q_head]),
                            // BMEUNPACK_H(dp->h[dp->q_head])); 

        // advance q
        dp->q_head = (dp->q_head+1)%N_BME_READINGS;
        ok = true;

        // note fresh data is available
        new_data = true;
    }

    // return whether success
    return (ok);
}

/* read all sensors, return whether either was successful
 */
static bool readSensors(void)
{
    bool ok = false;

    for (int device = 0; device < MAX_N_BME; device++)
        if (readSensor (device))
            ok = true;

    return (ok);
}


/* convert temperature and relative humidity to dewpoint.
 * both temp units are as per useMetricUnits().
 * http://irtfweb.ifa.hawaii.edu/~tcs3/tcs3/Misc/Dewpoint_Calculation_Humidity_Sensor_E.pdf
 */
float dewPoint (float T, float RH)
{
    // beware
    if (RH <= 0)
        return (0);

    // want C
    if (!useMetricUnits())
        T = 5.0F/9.0F*(T-32);           // F to C
    float H = (log10f(RH)-2)/0.4343F + (17.62F*T)/(243.12F+T);
    float Dp = 243.12F*H/(17.62F-H);
    if (!useMetricUnits())
        Dp = 9.0F/5.0F*Dp + 32;         // C to F
    return (Dp);
}

/* plot the given sensor data type choice in the given box, if said choice is one of ours
 */
void drawOneBME280Pane (const SBox &box, PlotChoice ch)
{
    resetWatchdog();

    for (int i = 0; i < MAX_N_BME; i++) {

        // get data pointer, skip if not used
        BMEData *dp = bme_data[i];
        if (!dp)
            continue;

        // prepare the appropriate plot
        int16_t *q;
        char title[32];
        uint16_t color;
        switch (ch) {
        case PLOT_CH_TEMPERATURE:
            q = dp->t;
            if (useMetricUnits())
                snprintf (title, sizeof(title), _FX("I2C %x: Temperature, C"), bme_i2c[i]);
            else
                snprintf (title, sizeof(title), _FX("I2C %x: Temperature, F"), bme_i2c[i]);
            color = TEMP_COLOR;
            break;
        case PLOT_CH_PRESSURE:
            q = dp->p;
            if (useMetricUnits())
                snprintf (title, sizeof(title), _FX("I2C %x: Pressure, hPa"), bme_i2c[i]);
            else
                snprintf (title, sizeof(title), _FX("I2C %x: Pressure, inHg"), bme_i2c[i]);
            color = PRES_COLOR;
            break;
        case PLOT_CH_HUMIDITY:
            q = dp->h;
            snprintf (title, sizeof(title), _FX("I2C %x: Humidity, %%"), bme_i2c[i]);
            color = HUM_COLOR;
            break;
        case PLOT_CH_DEWPOINT:
            q = NULL;               // DP is derived, see below
            if (useMetricUnits())
                snprintf (title, sizeof(title), _FX("I2C %x: Dew point, C"), bme_i2c[i]);
            else
                snprintf (title, sizeof(title), _FX("I2C %x: Dew point, F"), bme_i2c[i]);
            color = DP_COLOR;
            break;
        default: 
            // not showing a sensor in this box
            return;
        }

        // x axis depends on time span
        const char *xlabel;
        float time_scale;
        if (readDT >= SLOWEST_DT ) {
            xlabel = "Hours";
            time_scale = -3600.0F;
        } else {
            xlabel = "Minutes";
            time_scale = -60.0F;
        }

        // build linear x and y
        StackMalloc x_mem(N_BME_READINGS*sizeof(float));
        StackMalloc y_mem(N_BME_READINGS*sizeof(float));
        float *x = (float *) x_mem.getMem();
        float *y = (float *) y_mem.getMem();
        time_t t0 = now();
        uint8_t nxy = 0;                                        // count entries with valid times
        resetWatchdog();
        float value_now = 0;                                    // latest value is last
        for (int j = 0; j < N_BME_READINGS; j++) {
            uint8_t qj = (dp->q_head + j) % N_BME_READINGS;     // oldest .. newest == qhead .. qhead-1
            if (dp->u[qj] > 0) {                                // skip if time not set
                x[nxy] = (t0 - dp->u[qj])/time_scale;           // minutes ago .. beware unsigned time_t
                if (ch == PLOT_CH_DEWPOINT) {
                    value_now = y[nxy] = dewPoint (BMEUNPACK_T(dp->t[qj]), BMEUNPACK_H(dp->h[qj]));
                } else if (ch == PLOT_CH_TEMPERATURE) {
                    value_now = y[nxy] = BMEUNPACK_T(q[qj]);
                } else if (ch == PLOT_CH_PRESSURE) {
                    value_now = y[nxy] = BMEUNPACK_P(q[qj]);
                } else if (ch == PLOT_CH_HUMIDITY) {
                    value_now = y[nxy] = BMEUNPACK_H(q[qj]);
                }
                nxy++;
            }
        }

        // prep plot box
        SBox plbox = box;                                       // start assuming whole
        if (getNBMEConnected() > 1) {
            plbox.h /= 2;                                       // 2 sensors so plot must be half height
            if (i > 0)
                plbox.y += plbox.h;                             // second sensor uses lower half
        }

        // plot in plbox, showing a bit more precision for imperial pressure
        if (ch == PLOT_CH_PRESSURE && !useMetricUnits()) {
            char buf[32];
            sprintf (buf, "%.2f", value_now);
            plotXYstr (plbox, x, y, nxy, xlabel, title, color, 0, 0, buf);
        } else {
            plotXY (plbox, x, y, nxy, xlabel, title, color, 0, 0, value_now);
        }
    }
}

/* try to connect to sensors, reset brb_mode and brb_rotset to something benign if no longer appropriate
 */
void initBME280()
{
    connectSensors(true);

    if (!NVReadUInt8 (NV_BRB_ROTSET, &brb_rotset)
                        || (brb_rotset & (1<<brb_mode)) == 0
                        || ((brb_rotset & (1 << BRB_SHOW_BME76)) && !bme_data[BME_76])
                        || ((brb_rotset & (1 << BRB_SHOW_BME77)) && !bme_data[BME_77])) {
        Serial.printf (_FX("BME: Resetting initial brb_rotset 0x%x to 0x%x\n"),
                                brb_rotset, 1<<BRB_SHOW_SWSTATS);
        brb_mode = BRB_SHOW_SWSTATS;
        brb_rotset = 1 << BRB_SHOW_SWSTATS;
        NVWriteUInt8 (NV_BRB_ROTSET, brb_rotset);
    }
}


/* retrieve pointer to the given sensor data if connected, else NULL.
 * make a fresh read if desired.
 * index 0 always for 76, 1 for 77.
 */
const BMEData *getBMEData (BMEIndex device, bool fresh_read)
{
    if (fresh_read)
        (void) readSensor (device);

    return (bme_data[(int)device]);
}

/* take a new reading if it's time.
 * N.B. ignore if no sensors connected or clock not set.
 */
void readBME280 ()
{
    resetWatchdog();

    new_data = false;

    if (getNBMEConnected() == 0 || !clockTimeOk())
        return;

    uint32_t t0 = millis();

    if (!last_reading || t0 - last_reading >= readDT) {

        // read new values into queues and advance cadence
        if (readSensors()) {

            // gradually slow
            switch (readDT) {
            case INITIAL_DT:
                if (t0 > GOSLOWER)
                    readDT = SLOWER_DT;
                break;
            case SLOWER_DT:
                if (t0 > GOSLOWEST)
                    readDT = SLOWEST_DT;
                break;
            }
        }
    }
}

/* draw all panes showing any BME data
 */
void drawBME280Panes()
{
    PlotPane pp;

    pp = findPaneChoiceNow (PLOT_CH_TEMPERATURE);
    if (pp != PANE_NONE)
        drawOneBME280Pane (plot_b[pp], PLOT_CH_TEMPERATURE);
    pp = findPaneChoiceNow (PLOT_CH_PRESSURE);
    if (pp != PANE_NONE)
        drawOneBME280Pane (plot_b[pp], PLOT_CH_PRESSURE);
    pp = findPaneChoiceNow (PLOT_CH_HUMIDITY);
    if (pp != PANE_NONE)
        drawOneBME280Pane (plot_b[pp], PLOT_CH_HUMIDITY);
    pp = findPaneChoiceNow (PLOT_CH_DEWPOINT);
    if (pp != PANE_NONE)
        drawOneBME280Pane (plot_b[pp], PLOT_CH_DEWPOINT);
}

/* return whether new data has been read that has not been displayed
 */
bool newBME280data()
{
    return (new_data);
}


/* return number of connected BME sensors.
 * N.B. only valid after connectSensors()
 */
int getNBMEConnected(void)
{
        return ((bme_data[BME_76] != NULL) + (bme_data[BME_77] != NULL));
}

/* draw the BME stats for brb_mode in NCDXF_b.
 */
void drawBMEStats()
{
    // arrays for drawNCDXFStats()
    char titles[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN];
    char values[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN];
    uint16_t colors[NCDXF_B_NFIELDS];

    // get desired data and name
    const BMEData *dp = NULL;
    const char *name = NULL;
    if (brb_mode == BRB_SHOW_BME76) {
        dp = getBMEData (BME_76, false);
        name = "@76";
    } else if (brb_mode == BRB_SHOW_BME77) {
        dp = getBMEData (BME_77, false);
        name = "@77";
    } else {
        fatalError (_FX("drawBMEStats() brb_mode %d no data"), brb_mode);
        return; // lint
    }

    // but no need for name if only one sensor
    if (getNBMEConnected() == 1)
        name = "";

    // newest data is at head-1
    int qi = (dp->q_head + N_BME_READINGS - 1) % N_BME_READINGS;

    // fill fields for drawNCDXFStats()
    int i = 0;

    snprintf (titles[i], sizeof(titles[i]), "Temp%s", name);
    snprintf (values[i], sizeof(values[i]), "%.1f", BMEUNPACK_T(dp->t[qi]));
    colors[i] = TEMP_COLOR;
    i++;

    strcpy (titles[i], "Humidity");
    snprintf (values[i], sizeof(values[i]), "%.1f", BMEUNPACK_H(dp->h[qi]));
    colors[i] = HUM_COLOR;
    i++;

    strcpy (titles[i], "Dew Pt");
    snprintf (values[i], sizeof(values[i]), "%.1f", dewPoint(BMEUNPACK_T(dp->t[qi]),BMEUNPACK_H(dp->h[qi])));
    colors[i] = DP_COLOR;
    i++;

    strcpy (titles[i], "Pressure");
    if (useMetricUnits())
        snprintf (values[i], sizeof(values[i]), "%.0f", BMEUNPACK_P(dp->p[qi]));
    else
        snprintf (values[i], sizeof(values[i]), "%.2f", BMEUNPACK_P(dp->p[qi]));
    colors[i] = PRES_COLOR;
    i++;

    if (i != NCDXF_B_NFIELDS)
        fatalError (_FX("drawBMEStats wrong count"));

    // do it
    drawNCDXFStats (titles, values, colors);
}

/* called to check whether NCDXF_b is showing BME stats and needs updating.
 */
void updateBMEStats()
{
    if ((brb_mode == BRB_SHOW_BME76 || brb_mode == BRB_SHOW_BME77) && new_data)
        drawNCDXFBox();
}

/* handle a touch in NCDXF_b known to be showing BME stats
 */
void doBMETouch (const SCoord &s)
{
    // list of pane choices
    PlotChoice pcs[NCDXF_B_NFIELDS];            
    pcs[0] = PLOT_CH_TEMPERATURE;
    pcs[1] = PLOT_CH_HUMIDITY;
    pcs[2] = PLOT_CH_DEWPOINT;
    pcs[3] = PLOT_CH_PRESSURE;

    // do it
    doNCDXFStatsTouch (s, pcs);
}
