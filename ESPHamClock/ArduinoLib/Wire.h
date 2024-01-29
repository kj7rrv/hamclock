#ifndef _WIRE_H
#define _WIRE_H

/* Arduino Wire.cpp for linux or freebsd with native I2C or any UNIX with Excamera Labs I2C-USB bridge.
 * several aggregate read/write methods are added to allow locked multi-threaded transactions.
 */

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "Arduino.h"
#include "i2cdriver.h"

// compat with original Wire.h
#define byte uint8_t

#define MAX_TXBUF       64      // max tx buffer
#define MAX_RXBUF       64      // max rx buffer

class TwoWire {
    public:

        TwoWire(void);
        ~TwoWire(void);

        void begin(void);

        // non-standard but work atomically via lock
        bool read8 (uint8_t dev, uint8_t reg, uint8_t &val);
        bool read16 (uint8_t dev, uint8_t reg, uint16_t &val);
        bool read24 (uint8_t dev, uint8_t reg, uint32_t &val);
        bool read32 (uint8_t dev, uint8_t reg, uint32_t &val);
        bool write8 (uint8_t dev, uint8_t reg, uint8_t val);


    private:

        // these are now private as they don't work in a multi-threaded environment
        void beginTransmission(uint8_t);
        size_t write(uint8_t);
        size_t write(const uint8_t *data, size_t quantity);
        uint8_t endTransmission(bool sendStop=true);
        uint8_t requestFrom(uint8_t, uint8_t);
        int available(void);
        int read(void);

        // lock to insure the public API read/write method is atomic and convenient auto-unlock class
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

        // for native i2c
        int i2c_fd;

        // for Excamera Labs
        I2CDriver excam;

        // used by both
        const char *filename;
        uint8_t dev_addr;
        uint8_t txdata[MAX_TXBUF];
        uint8_t rxdata[MAX_RXBUF];
        int n_txdata, n_rxdata, n_retdata;
        bool transmitting;
        void setDev (uint8_t dev);
        bool openConnection (uint8_t dev);
        void closeConnection (void);

};

extern TwoWire Wire;

#endif // _WIRE_H
