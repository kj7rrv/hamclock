/* Bare-bones subset of Adafruit's MCP23X17 driver interface enhanced to provide RPi-like GPIO pinmode and
 * digitalRead/Write for this device or for the native RPi header from linux or freebsd.
 */

#ifndef __ADAFRUIT_MCP23X17_H__
#define __ADAFRUIT_MCP23X17_H__

#include <stdint.h>
#include <Arduino.h>
#include <Wire.h>


// registers
#define MCP23X17_IODIR 0x00     // I/O direction register
#define MCP23X17_IPOL 0x01      // Input polarity register
#define MCP23X17_IOCON 0x05     // Configuration register
#define MCP23X17_GPPU 0x06      // Pull-up resistor configuration register
#define MCP23X17_GPIO 0x09      // Port register
#define MCP23X17_OLAT 0x0A      // Output latch register

#define MCP23X17_ADDR 0x20      // Default I2C Address

class Adafruit_MCP23X17 {

    public:

        // init
        Adafruit_MCP23X17(void);
        ~Adafruit_MCP23X17(void);
        bool begin_I2C (uint8_t i2c_addr = MCP23X17_ADDR);

        // main Arduino-like API methods for controlling MCP pins
        void pinMode (uint8_t mcp_pin, uint8_t mode);
        uint8_t digitalRead (uint8_t mcp_pin);
        void digitalWrite (uint8_t mcp_pin, uint8_t value);

    private:

    #if defined (_NATIVE_GPIOD_LINUX)

        // be prepared to use gpiod if new enough

        gpiod_chip *chip;

        // lines cache large enough to accommodate the largest RPi GPIO line number
        #define N_RPI_LINES 50
        gpiod_line *lines[N_RPI_LINES];
        gpiod_line *getGPIODLine (int rpi_pin);

        // set if new enough to be supported
        int pullup_flag;

    #endif

    #if defined (_NATIVE_GPIOBC_LINUX)

        // be prepared to use old broadcom memory mapped interface as fallback

        bool bc_ready;
        volatile uint32_t *bc_gbase;
        inline uint32_t GPIO_SEL_MASK (uint8_t p, uint32_t m) {
            return (m<<(3*(p%10)));
        }
        bool mapGPIOAddress(char ynot[]);

    #endif

    #if defined (_NATIVE_GPIO_FREEBSD)

        gpio_handle_t handle;

    #endif

        // lock to insure the public API is atomic and convenient auto-unlock class
        static pthread_mutex_t lock;
        class LockIt {
            public:
            LockIt() {
                pthread_mutex_lock (&lock);
            }
            ~LockIt() {
                pthread_mutex_unlock (&lock);
            }
        };

        // trace level
        int verbose;

        // i2c addr
        uint8_t my_addr;

        // whether MCP23X17 at MCP23X17_ADDR responds
        bool mcp_found;

        // map of MCP pin number to RPi GPIO number
        bool mapMCP2RPiPin(uint8_t mcp_pin, uint8_t &rpi_pin);  // accommodates MCP_FAKE_KX3
        uint8_t mcp_rpipin_map[MCP_N_LINES];                    // does not include MCP_FAKE_KX3

        // return proper MCP register address for a given MCP pin, _assuming bank 0 configuration_
        uint8_t bank0_reg (uint8_t reg, uint8_t mcp_pin) { return (mcp_pin <= 7 ? 2*reg : 2*reg+1); }

        // return MCP register mask for the given MCP pin number
        uint8_t pin_regmask (uint8_t mcp_pin) { return (1U << (mcp_pin%8)); }

};

#endif // __ADAFRUIT_MCP23X17_H__
