/* Subset of Adafruit's MCP23X17 driver interface modified for HamClock to include RPi header from
 * linux or freebsd.
 *
 * Linux gpiod and libgpiod
 *   https://www.ics.com/blog/gpio-programming-exploring-libgpiod-library
 *   https://libgpiod.readthedocs.io/en/latest/
 *   https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/tree/include/gpiod.h
 *   https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/tree/examples
 *   https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git
 *   https://phwl.org/assets/images/2021/02/libgpiod-ref.pdf
 *   apt install gpiod libgpiod-dev libgpiod-doc
 *
 * FreeBSD gpio:
 *   https://www.unix.com/man-page/freebsd/3/gpio_open/
 *
 * MCP23017:
 *   https://cdn-shop.adafruit.com/datasheets/mcp23017.pdf
 */

#include <Adafruit_MCP23X17.h>


// the lock
pthread_mutex_t Adafruit_MCP23X17::lock;


/* constructor
 */
Adafruit_MCP23X17::Adafruit_MCP23X17(void)
{
    // populate map from MCP23X17 pin assignments to RPi GPIO
    mcp_rpipin_map[SW_CD_RED_PIN]   = 13;       // header 33
    mcp_rpipin_map[SW_CD_GRN_PIN]   = 19;       // header 35
    mcp_rpipin_map[SW_CD_RESET_PIN] = 26;       // header 37
    mcp_rpipin_map[SW_ALARMOUT_PIN] = 06;       // header 31
    mcp_rpipin_map[SW_ALARMOFF_PIN] = 05;       // header 29
    mcp_rpipin_map[SATALARM_PIN]    = 20;       // header 38
    mcp_rpipin_map[ONAIR_PIN]       = 21;       // header 40

    // no MCP yet
    mcp_found = false;

    // shh?
    verbose = 0;

    #if defined(_NATIVE_GPIOD_LINUX)

        chip = NULL;

    #endif

    #if defined(_NATIVE_GPIOBC_LINUX)

        bc_ready = false;

    #endif

    #if defined(_NATIVE_GPIO_FREEBSD)

        handle = GPIO_INVALID_HANDLE;

    #endif

    pthread_mutex_init (&lock, NULL);
}


/* destructor
 */
Adafruit_MCP23X17::~Adafruit_MCP23X17(void)
{
    #if defined(_NATIVE_GPIOD_LINUX)

        if (chip) {
            for (int i = 0; i < MCP_N_LINES; i++)
                if (lines[i])
                    gpiod_line_release (lines[i]);
            gpiod_chip_close (chip);
            chip = NULL;
        }

    #endif

    #if defined(_NATIVE_GPIO_FREEBSD)

        if (handle != GPIO_INVALID_HANDLE) {
            gpio_close (handle);
            handle = GPIO_INVALID_HANDLE;
        }

    #endif
}



#if defined(_NATIVE_GPIOD_LINUX)

/* provide a gpiod_line cache for the given RPi pin
 */
gpiod_line *Adafruit_MCP23X17::getGPIODLine (int rpi_pin)
{
    if (rpi_pin < 0 || rpi_pin >= N_RPI_LINES) {
        printf ("MCP: getGPIODLine(%d): preposterous rpi_pin number", rpi_pin);
        exit(1);
    }

    // set up line on first call
    if (!lines[rpi_pin]) {
        lines[rpi_pin] = gpiod_chip_get_line (chip, rpi_pin);
        if (!lines[rpi_pin]) {
            printf ("MCP: getGPIODLine(%d): no line for rpi_pin\n", rpi_pin);
            exit(1);
        }
    }

    return (lines[rpi_pin]);
}

#endif // _NATIVE_GPIOD_LINUX


#if defined(_NATIVE_GPIOBC_LINUX)

/* set bc_gbase so it points to the physical address of the GPIO controller.
 * return true if ok, else false with brief excuse in ynot[].
 */
bool Adafruit_MCP23X17::mapGPIOAddress(char ynot[])
{
        // acquire exclusive access to kernel physical address
        static const char gpiofilefile[] = "/dev/gpiomem";
        int fd = ::open (gpiofilefile, O_RDWR|O_SYNC);
        if (fd < 0) {
            snprintf (ynot, 50, "%s: %s", gpiofilefile, strerror(errno));
            return (false);
        }
        if (::flock (fd, LOCK_EX|LOCK_NB) < 0) {
            snprintf (ynot, 50, "%s: file in use", gpiofilefile);
            close (fd);
            return(false);
        }

        /* mmap access */
        bc_gbase = (uint32_t *) mmap(NULL, 0xB4, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

        // fd not needed after setting up mmap
        close(fd);

        // check for error, leave bc_gbase 0 if so
        if (bc_gbase == MAP_FAILED) {
            bc_gbase = NULL;
            snprintf (ynot, 50, "mmap: %s", strerror(errno));
            return (false);
        }

        // worked
        return (true);
}

#endif // _NATIVE_GPIOBC_LINUX


/* convert MCP23X17 pin to RPi, including accommodating MCP_FAKE_KX3
 */
bool Adafruit_MCP23X17::mapMCP2RPiPin (uint8_t mcp_pin, uint8_t &rpi_pin)
{
    if (mcp_pin < MCP_N_LINES) {
        rpi_pin = mcp_rpipin_map[mcp_pin];
        return (true);
    } else if (mcp_pin == MCP_FAKE_KX3) {
        // map MCP_FAKE_KX3 to Elecraft_PIN
        rpi_pin = Elecraft_PIN;
        return (true);
    } else {
        return (false);
    }
}


/* start, return whether any GPIO means is ready
 * N.B. at most one is ever activated.
 */
bool Adafruit_MCP23X17::begin_I2C (uint8_t addr)
{
    LockIt lk;

    // save
    my_addr = addr;

    bool any_ok = false;

    #if defined(_NATIVE_GPIOD_LINUX)

        // gpiod is preferred but can't tell at compile time if it's new enough to support pullups.
        // GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP was added as an enum in version 1.6 for sure, maybe 1.5

        if (GPIOOk() && atof(gpiod_version_string()) >= 1.6) {

            // ok, gpiod should be new enough to support pullups

            // the pullup flag is an enum, not a define, so we can't know if we can use it an compile time
            pullup_flag = GPIOD_BIT(5);         // straight from gpiod.h

            chip = gpiod_chip_open_by_number(0);
            if (chip) {
                printf ("MCP: found gpiod linux chip %s\n", gpiod_chip_name(chip));
                any_ok = true;
            } else
                printf ("MCP: gpiod_chip_open_by_number(0) failed: %s\n", strerror(errno));
        }

    #endif

    #if defined(_NATIVE_GPIOBC_LINUX)

        // try broadcom method if gpiod didn't work out for any reason

        if (GPIOOk() && !any_ok) {
            char ynot[1023];
            bc_ready = mapGPIOAddress(ynot);
            if (bc_ready) {
                printf ("MCP: found broadcom linux\n");
                any_ok = true;
            } else
                printf ("MCP: broadcom map failed: %s\n", ynot);
        }

    #endif

    #if defined(_NATIVE_GPIO_FREEBSD)

        if (GPIOOk()) {
            handle = gpio_open(0);
            if (handle != GPIO_INVALID_HANDLE) {
                printf ("MCP: found freebsd gpio\n");
                any_ok = true;
            } else
                printf ("MCP: freebsd gpio_open(0) failed: %s\n", strerror(errno));
        }

    #endif

    // try MCP only if no native access

    if (!any_ok) {

        // check for response by setting bank 0 addressing
        if (Wire.write8 (my_addr, MCP23X17_IOCON, 0)) {
            printf ("MCP: found MCP23017 at 0x%02X\n", my_addr);
            mcp_found = any_ok = true;
        } else
            printf ("MCP: MCP23017 at 0x%02X failed\n", my_addr);
    }

    return (any_ok);
}

/* set the given mcp pin to INPUT, INPUT_PULLUP or OUTPUT
 * N.B. ignore MCP_FAKE_KX3 when using real MCP
 */
void Adafruit_MCP23X17::pinMode (uint8_t mcp_pin, uint8_t mode)
{
    LockIt lk;

    if (verbose)
        printf ("MCP: 0x%02X pinMode (%d, %d == %s)\n", my_addr, mcp_pin, mode,
                mode == INPUT ? "INPUT"
                        : (mode == INPUT_PULLUP ? "INPUT_PULLUP"
                                : (mode == OUTPUT ? "OUTPUT" : "???")));

    #if defined (_NATIVE_GPIOD_LINUX)

        if (chip) {

            // map MCP pin to RPi native
            uint8_t rpi_pin;
            if (!mapMCP2RPiPin (mcp_pin, rpi_pin)) {
                printf ("MCP: gpiod linux pinMode(%d,%d) pin invalid\n", mcp_pin, mode);
                return;
            }

            // optimistic :-)
            bool ok = true;

            // get corresponding control line
            gpiod_line *line = getGPIODLine (rpi_pin);

            // there was talk of allowing line config to change but I find it doesn't work which
            // explains the use of release/request 

            if (mode == INPUT) {
                gpiod_line_release (line);
                ok = gpiod_line_request_input (line, "MCP") == 0;
            } else if (mode == INPUT_PULLUP) {
                gpiod_line_release (line);
                ok = gpiod_line_request_input_flags (line, "MCP", pullup_flag) == 0;
            } else if (mode == OUTPUT) {
                gpiod_line_release (line);
                ok = gpiod_line_request_output (line, "MCP", 1) == 0;
            } else
                printf ("MCP: gpiod linux pinMode(%d,%d) unknown mode\n", mcp_pin, mode);

            if (!ok)
                printf ("MCP: gpiod linux pinMode(%d,%d) failed: %s\n", mcp_pin, mode, strerror(errno));
        }

    #endif

    #if defined (_NATIVE_GPIOBC_LINUX)

        if (bc_ready) {

            // map MCP pin to RPi native
            uint8_t rpi_pin;
            if (!mapMCP2RPiPin (mcp_pin, rpi_pin)) {
                printf ("MCP: broadcom linux pinMode(%d,%d) pin invalid\n", mcp_pin, mode);
                return;
            }

            if (mode == INPUT || mode == INPUT_PULLUP)
                bc_gbase[rpi_pin/10] &= ~GPIO_SEL_MASK(rpi_pin,7);

            if (mode == INPUT_PULLUP) {

                // BCM2835
                bc_gbase[37] = 2;
                bc_gbase[38+rpi_pin/32] = 1UL << (rpi_pin%32);
                bc_gbase[37] = 0;
                bc_gbase[38+rpi_pin/32] = 0;

                // BCM2711
                bc_gbase[57+rpi_pin/16] = (bc_gbase[57+rpi_pin/16] & ~(3UL << 2*(rpi_pin%16)))
                                                | (1UL << 2*(rpi_pin%16));
            }

            if (mode == OUTPUT)
                bc_gbase[rpi_pin/10] = (bc_gbase[rpi_pin/10] & ~GPIO_SEL_MASK(rpi_pin,7))
                                                | GPIO_SEL_MASK(rpi_pin,1);
        }

    #endif

    #if defined(_NATIVE_GPIO_FREEBSD)

        if (handle != GPIO_INVALID_HANDLE) {

            // map pin to RPi native
            uint8_t rpi_pin;
            if (!mapMCP2RPiPin (mcp_pin, rpi_pin)) {
                printf ("MCP: freebsd pinMode(%d,%d) pin invalid\n", mcp_pin, mode);
                return;
            }

            if (mode == INPUT || mode == INPUT_PULLUP) {
                gpio_config_t cfg;
                memset (&cfg, 0, sizeof(cfg));
                cfg.g_pin = rpi_pin;
                cfg.g_flags = GPIO_PIN_INPUT;
                if (mode == INPUT_PULLUP)
                    cfg.g_flags |= GPIO_PIN_PULLUP;
                gpio_pin_set_flags (handle, &cfg);

            } else if (mode == OUTPUT)
                gpio_pin_output (handle, rpi_pin);

            else
                printf ("MCP: freebsd pinMode(%d,%d) unknown mode\n", mcp_pin, mode);
        }

    #endif

    if (mcp_found && mcp_pin != MCP_FAKE_KX3) {

        // read current direction mask
        uint8_t iodir_reg = bank0_reg (MCP23X17_IODIR, mcp_pin);
        uint8_t iodir_mask = pin_regmask (mcp_pin);
        uint8_t iodir_val;
        if (!Wire.read8 (my_addr, iodir_reg, iodir_val)) {
            printf ("MCP: pinMode(%d,%d) read IODIR failed\n", mcp_pin, mode);
            return;
        }

        // set direction bit for this pin

        if (mode == INPUT || mode == INPUT_PULLUP) {

            // set direction mask with bit for this pin on
            iodir_val |= iodir_mask;
            if (!Wire.write8 (my_addr, iodir_reg, iodir_val)) {
                printf ("MCP: pinMode(%d,%d) write IODIR failed\n", mcp_pin, mode);
                return;
            }

            // read pullup mask
            uint8_t pullup_reg = bank0_reg (MCP23X17_GPPU, mcp_pin);
            uint8_t pullup_mask = pin_regmask (mcp_pin);
            uint8_t pullup_val;
            if (!Wire.read8 (my_addr, pullup_reg, pullup_val)) {
                printf ("MCP: pinMode(%d,%d) read PULLUP failed\n", mcp_pin, mode);
                return;
            }

            // modify bit for this pin
            if (mode == INPUT_PULLUP)
                pullup_val |= pullup_mask;
            else
                pullup_val &= ~pullup_mask;
            if (!Wire.write8 (my_addr, pullup_reg, pullup_val)) {
                printf ("MCP: pinMode(%d,%d) write PULLUP failed\n", mcp_pin, mode);
                return;
            }

        } else if (mode == OUTPUT) {

            // set direction mask with bit for this pin off
            iodir_val &= ~iodir_mask;
            if (!Wire.write8 (my_addr, iodir_reg, iodir_val)) {
                printf ("MCP: pinMode(%d,%d) write IODIR failed\n", mcp_pin, mode);
                return;
            }


        } else {

            printf ("MCP: pinMode(%d,%d) unknown mode\n", mcp_pin, mode);
            return;
        }
    }
}

/* return the current pin value HIGH, LOW or 0xff (not that anyone would ever check)
 * N.B. ignore MCP_FAKE_KX3 when using real MCP
 */
uint8_t Adafruit_MCP23X17::digitalRead(uint8_t mcp_pin)
{
    LockIt lk;

    #if defined (_NATIVE_GPIOD_LINUX)

        if (chip) {

            // map pin to RPi native
            uint8_t rpi_pin;
            if (!mapMCP2RPiPin (mcp_pin, rpi_pin)) {
                printf ("MCP: gpiod linux digitalRead(%d) failed: invalid mcp pin\n", mcp_pin);
                return (0xff);
            }

            gpiod_line *line = getGPIODLine (rpi_pin);
            int val = gpiod_line_get_value (line);
            if (val < 0) {
                printf ("MCP: gpiod linux digitalRead(%d) failed: %s\n", mcp_pin, strerror(errno));
                return (0xff);
            }
            if (verbose)
                printf ("MCP: 0x%02X gpiod linux digitalRead (%d) => %d\n", my_addr, mcp_pin, val);
            return (val);
        }

    #endif

    #if defined (_NATIVE_GPIOBC_LINUX)

        if (bc_ready) {

            // map pin to RPi native
            uint8_t rpi_pin;
            if (!mapMCP2RPiPin (mcp_pin, rpi_pin)) {
                printf ("MCP: broadcom linux digitalRead(%d) failed: invalid mcp pin\n", mcp_pin);
                return (0xff);
            }

            int val = (bc_gbase[13+rpi_pin/32] & (1UL<<(rpi_pin%32))) != 0;
            if (verbose)
                printf ("MCP: broadcom linux digitalRead (%d) => %d\n", mcp_pin, val);
            return (val);

        }

    #endif

    #if defined(_NATIVE_GPIO_FREEBSD)

        if (handle != GPIO_INVALID_HANDLE) {

            // map pin to RPi native
            uint8_t rpi_pin;
            if (!mapMCP2RPiPin (mcp_pin, rpi_pin)) {
                printf ("MCP: freebsd digitalRead(%d) failed: invalid mcp pin\n", mcp_pin);
                return (0xff);
            }

            uint8_t val = gpio_pin_get (handle, rpi_pin) == GPIO_VALUE_HIGH ? 1 : 0;
            if (verbose)
                printf ("MCP: 0x%02X freebsd digitalRead (%d) => %d\n", my_addr, mcp_pin, val);
            return (val);

        }

    #endif

    if (mcp_found && mcp_pin != MCP_FAKE_KX3) {

        // reading an input pin should be done from MCP23X17_GPIO, but reading an output
        // pin must read from MCP23X17_OLAT to get the last-written value. thus we must
        // first learn the pin direction then do the appropriate read

        // read direction
        uint8_t iodir_reg = bank0_reg (MCP23X17_IODIR, mcp_pin);
        uint8_t iodir_mask = pin_regmask (mcp_pin);
        uint8_t iodir_val;
        if (!Wire.read8 (my_addr, iodir_reg, iodir_val)) {
            printf ("MCP: read(%d) read IODIR failed\n", mcp_pin);
            return (0xff);
        }

        // read MCP23X17_GPIO if input or MCP23X17_OLAT if output
        if (iodir_val & iodir_mask) {
            // input
            uint8_t gpio_reg = bank0_reg (MCP23X17_GPIO, mcp_pin);
            uint8_t gpio_mask = pin_regmask (mcp_pin);
            uint8_t gpio_val;
            if (!Wire.read8 (my_addr, gpio_reg, gpio_val)) {
                printf ("MCP: read(%d) read GPIO failed\n", mcp_pin);
                return (0xff);
            }
            return ((gpio_val & gpio_mask) != 0);
        } else {
            // output
            uint8_t olat_reg = bank0_reg (MCP23X17_OLAT, mcp_pin);
            uint8_t olat_mask = pin_regmask (mcp_pin);
            uint8_t olat_val;
            if (!Wire.read8 (my_addr, olat_reg, olat_val)) {
                printf ("MCP: read(%d) read OLAT failed\n", mcp_pin);
                return (0xff);
            }
            uint8_t val = (olat_val & olat_mask) != 0;
            if (verbose)
                printf ("MCP: 0x%02X digitalRead (%d) => %d\n", my_addr, mcp_pin, val);
            return (val);
        }
    }

    return (0xff);
}


/* set the given pin to HIGH or LOW
 * N.B. ignore MCP_FAKE_KX3 when using real MCP
 */
void Adafruit_MCP23X17::digitalWrite(uint8_t mcp_pin, uint8_t value)
{
    LockIt lk;

    if (verbose)
        printf ("MCP: 0x%02X digitalWrite (%d,%d)\n", my_addr, mcp_pin, value);

    #if defined(_NATIVE_GPIOD_LINUX)

        if (chip) {

            // map pin to RPi native
            uint8_t rpi_pin;
            if (!mapMCP2RPiPin (mcp_pin, rpi_pin)) {
                printf ("MCP: gpiod linux digitalWrite(%d,%d) failed: invalid pin\n", mcp_pin, value);
                return;
            }

            gpiod_line *line = getGPIODLine (rpi_pin);
            gpiod_line_set_value (line, value);
        }

    #endif

    #if defined(_NATIVE_GPIOBC_LINUX)

        if (bc_ready) {

            // map pin to RPi native
            uint8_t rpi_pin;
            if (!mapMCP2RPiPin (mcp_pin, rpi_pin)) {
                printf ("MCP: broadcom linux digitalWrite(%d,%d) failed: invalid pin\n", mcp_pin, value);
                return;
            }

            if (value)
                bc_gbase[7+rpi_pin/32] = 1UL << (rpi_pin%32);          // set hi
            else
                bc_gbase[10+rpi_pin/32] = 1UL << (rpi_pin%32);         // set low

        }

    #endif

    #if defined(_NATIVE_GPIO_FREEBSD)

        if (handle != GPIO_INVALID_HANDLE) {

            // map pin to RPi native
            uint8_t rpi_pin;
            if (!mapMCP2RPiPin (mcp_pin, rpi_pin)) {
                printf ("MCP: freebsd digitalWrite(%d,%d) failed: invalid pin\n", mcp_pin, value);
                return;
            }

            gpio_pin_set (handle, rpi_pin, value != 0);
        }

    #endif

    if (mcp_found && mcp_pin != MCP_FAKE_KX3) {

        // writing to the OLAT register allows pin values to be read back if desired.

        // first read current latch value
        uint8_t olat_reg = bank0_reg (MCP23X17_OLAT, mcp_pin);
        uint8_t olat_mask = pin_regmask (mcp_pin);
        uint8_t olat_val;
        if (!Wire.read8 (my_addr, olat_reg, olat_val)) {
            printf ("MCP: write(%d,%d) read OLAT failed\n", mcp_pin, value);
            return;
        }

        // write back with new pin value
        if (value)
            olat_val |= olat_mask;
        else
            olat_val &= ~olat_mask;
        if (!Wire.write8 (my_addr, olat_reg, olat_val)) {
            printf ("MCP: write(%d,%d) write OLAT failed\n", mcp_pin, value);
            return;
        }
    }
}
