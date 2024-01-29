/* Adafruit_LTR329 reduced to bare minimum and use thread-safe Wire.
 */

#include "Arduino.h"
#include <Wire.h>

#include "Adafruit_LTR329_LTR303.h"

/*!
 *    @brief  Instantiates a new LTR329 class
 */
Adafruit_LTR329::Adafruit_LTR329(void)
{
}


/*!
 *    @brief  Setups the hardware for talking to the LTR329
 *    @param  theWire An optional pointer to an I2C interface
 *    @return True if initialization was successful, otherwise false.
 */
bool Adafruit_LTR329::begin(TwoWire *theWire)
{
    uint8_t val;

    if (!Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_PART_ID, val) || val != 0xA0) {
        printf ("LTR329: bad part ID: 0x%02X != 0x%02X\n", val, 0xA0);
        return (false);
    }
    if (!Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_MANU_ID, val) || val != 0x05) {
        printf ("LTR329: bad manu ID: 0x%02X != 0x%02X\n", val, 0x05);
        return (false);
    }

    // OK now we can do a soft reset
    if (!Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_ALS_CTRL, val))
        return (false);
    val |= 2;
    if (!Wire.write8 (LTR329_I2CADDR_DEFAULT, LTR329_ALS_CTRL, val))
        return (false);
    delay(20);
    if (!Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_ALS_CTRL, val))
        return (false);
    if (val & 2)
        return (false);

    // main screen turn on
    if (!Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_ALS_CTRL, val))
        return (false);
    val |= 1;
    if (!Wire.write8 (LTR329_I2CADDR_DEFAULT, LTR329_ALS_CTRL, val))
        return (false);
    if (!Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_ALS_CTRL, val) || (val & 1) != 1)
        return (false);
    delay(200);

    return true;
}

/*!
 *  @brief  Set the sensor gain
 *  @param  gain The desired gain: LTR3XX_GAIN_1, LTR3XX_GAIN_2, LTR3XX_GAIN_4
 *  LTR3XX_GAIN_8, LTR3XX_GAIN_48 or LTR3XX_GAIN_96
 */
void Adafruit_LTR329::setGain(ltr329_gain_t gain)
{
    uint8_t val;
    if (Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_ALS_CTRL, val)) {
        val = (val & 3) | (gain << 2);
        (void) Wire.write8 (LTR329_I2CADDR_DEFAULT, LTR329_ALS_CTRL, val);
    } else
        printf ("LTR329 setGain(%d) failed\n", (int)gain);
}

/*!
 *  @brief  Get the sensor's gain
 *  @returns gain The current gain: LTR3XX_GAIN_1, LTR3XX_GAIN_2, LTR3XX_GAIN_4
 *  LTR3XX_GAIN_8, LTR3XX_GAIN_48 or LTR3XX_GAIN_96
 */
ltr329_gain_t Adafruit_LTR329::getGain(void)
{
    uint8_t val = 0;
    if (Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_ALS_CTRL, val))
        val = (val >> 2) & 7;
    return ((ltr329_gain_t)val);
}

/*!
 *  @brief  Set the sensor integration time. Longer times are more sensitive but
 *  take longer to read!
 *  @param  inttime The desired integration time (in millis):
 *  LTR3XX_INTEGTIME_50, LTR3XX_INTEGTIME_100, LTR3XX_INTEGTIME_150,
 *  LTR3XX_INTEGTIME_200,LTR3XX_INTEGTIME_250, LTR3XX_INTEGTIME_300,
 *  LTR3XX_INTEGTIME_350, LTR3XX_INTEGTIME_400,
 */
void Adafruit_LTR329::setIntegrationTime(ltr329_integrationtime_t inttime)
{
    uint8_t val;
    if (Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_MEAS_RATE, val)) {
        val = (val & 7) | ((uint8_t)inttime << 3);
        (void) Wire.write8 (LTR329_I2CADDR_DEFAULT, LTR329_MEAS_RATE, val);
    } else
        printf ("LTR329 setIntegrationTime(%d) failed\n", (int)inttime);
}

/*!
 *  @brief  Get the sensor's integration time for light sensing
 *  @returns The current integration time, in milliseconds.
 *  LTR3XX_INTEGTIME_50, LTR3XX_INTEGTIME_100, LTR3XX_INTEGTIME_150,
 *  LTR3XX_INTEGTIME_200,LTR3XX_INTEGTIME_250, LTR3XX_INTEGTIME_300,
 *  LTR3XX_INTEGTIME_350, LTR3XX_INTEGTIME_400,
 */
ltr329_integrationtime_t Adafruit_LTR329::getIntegrationTime(void)
{
    uint8_t val = 0;
    if (Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_MEAS_RATE, val))
        val = (val >> 3) & 7;
    return ((ltr329_integrationtime_t)val);
}

/*!
 *  @brief  Set the sensor measurement rate. Longer times are needed when
 *  the integration time is longer OR if you want to have lower power usage
 *  @param  rate The desired measurement rate (in millis):
 *  LTR3XX_MEASRATE_50, LTR3XX_MEASRATE_100, LTR3XX_MEASRATE_200,
 *  LTR3XX_MEASRATE_500, LTR3XX_MEASRATE_1000, or LTR3XX_MEASRATE_2000
 */
void Adafruit_LTR329::setMeasurementRate(ltr329_measurerate_t rate)
{
    uint8_t val;
    if (Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_MEAS_RATE, val)) {
        val = (val & 0x38) | ((uint8_t)rate);
        (void) Wire.write8 (LTR329_I2CADDR_DEFAULT, LTR329_MEAS_RATE, val);
    } else
        printf ("LTR329 setMeasurementRate(%d) failed\n", (int)rate);
}

/*!
 *  @brief  Get the sensor's measurement rate.
 *  @returns The current measurement rate (in millis):
 *  LTR3XX_MEASRATE_50, LTR3XX_MEASRATE_100, LTR3XX_MEASRATE_200,
 *  LTR3XX_MEASRATE_500, LTR3XX_MEASRATE_1000, or LTR3XX_MEASRATE_2000
 */
ltr329_measurerate_t Adafruit_LTR329::getMeasurementRate(void)
{
    uint8_t val = 0;
    if (Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_MEAS_RATE, val))
        val &= 7;
    return ((ltr329_measurerate_t)val);
}

/*!
 *  @brief  Checks if new data is available in data register
 *  @returns True on new data available
 */
bool Adafruit_LTR329::newDataAvailable(void)
{
    uint8_t val = 0;
    if (Wire.read8 (LTR329_I2CADDR_DEFAULT, LTR329_STATUS, val)) {
        bool ready = ((val & 0x84) == 0x04);         // valid (yes 0) and new
        if (!ready)
            printf ("LTR329 data not ready: 0x%02x\n", val);
        return (ready);
    } else {
        printf ("LTR329 newDataAvailable failed\n");
        return (false);
    }
}

/*!
 *  @brief  Read both 16-bit channels at once, and place data into argument
 * pointers
 *  @param  ch0 Reference to uint16_t where visible+IR data will be stored
 *  @param  ch1 Reference to uint16_t where IR-only data will be stored
 *  @returns True if data is valid (no over-run), false on bad data
 */
bool Adafruit_LTR329::readBothChannels(uint16_t &ch0, uint16_t &ch1)
{
    uint32_t val;
    if (Wire.read32 (LTR329_I2CADDR_DEFAULT, LTR329_CH1DATA, val)) {
        // val was read as BE: CH1_0 CH1_1 CH0_0 CH0_1
        ch0 = ((val<<8)&0xff00) | ((val>>8)&0xff);
        ch1 = ((val>>8)&0xff00) | ((val>>24)&0xff);
        return (true);
    }
    return (false);
}
