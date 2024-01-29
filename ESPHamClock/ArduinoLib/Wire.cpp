/* Arduino's cpp reimplemented for Linux and FreeBSD with hardware I2C or any UNIX with USB
 * using the I2C-USB bridge from i2cdriver.com by Excamera Labs. However we also support multiple
 * simultaneous clients via the same hw connection using locks to make transactions atomic which is not
 * compatable with the multi-step paradigm of the original design. Thus all clients must be changed to
 * use new atomic read/write transactions.
 *
 * linux
 *   https://www.kernel.org/doc/Documentation/i2c/dev-interface
 * freebsd
 *   man iic
 *   https://vzaigrin.wordpress.com/2014/04/28/working-with-i2c-in-freebsd-on-raspberry-pi/
 * excamera
 *   https://i2cdriver.com/i2cdriver.pdf
 */


#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/file.h>

// get our specific include files
#include "Arduino.h"
#include "Wire.h"
#include "i2cdriver.h"
#if defined (_NATIVE_I2C_LINUX)
    #include <linux/i2c.h>
    #include <linux/i2c-dev.h>
#elif defined (_NATIVE_I2C_FREEBSD)
    #include <dev/iicbus/iic.h>
#endif


// sanity check -- makes no sense to try and support two platforms at once
#if defined(_NATIVE_I2C_LINUX) && defined(_NATIVE_I2C_FREEBSD)
#error can not have both _NATIVE_I2C_LINUX and _NATIVE_I2C_FREEBSD
#endif


// the traditional global Wire object used by Arduino apps and drivers
TwoWire Wire;

// the lock
pthread_mutex_t TwoWire::lock;

/* constructor
 */
TwoWire::TwoWire()
{
        memset (rxdata, 0, sizeof(rxdata));
        memset (txdata, 0, sizeof(txdata));
        i2c_fd = -1;
        dev_addr = 0;
        n_txdata = 0;
        n_rxdata = 0;
        n_retdata = 0;
        transmitting = false;

        memset (&excam, 0, sizeof(excam));
        excam.port = -1;
        filename = NULL;

        // 0 = none
        // 1 = transactions
        // 2 = " plus data
        verbose = 0;

        pthread_mutex_init (&lock, NULL);
}

/* destructor
 */
TwoWire::~TwoWire()
{
        closeConnection();
}

/* new public atomic io methods
 */

bool TwoWire::read8 (uint8_t dev, uint8_t reg, uint8_t &val)
{
        LockIt lk;

        beginTransmission(dev);
        if (write(reg) != 1)
            return (false);
        if (endTransmission() != 0)
            return (false);
        if (requestFrom(dev, 1) != 1)
            return (false);
        val = read();
        return (true);
}

bool TwoWire::read16 (uint8_t dev, uint8_t reg, uint16_t &val)
{
        LockIt lk;

        beginTransmission(dev);
        if (write(reg) != 1)
            return (false);
        if (endTransmission() != 0)
            return (false);
        if (requestFrom(dev, 2) != 2)
            return (false);
        // BE
        val = read();
        val <<= 8;
        val |= read();
        return (true);
}


bool TwoWire::read24 (uint8_t dev, uint8_t reg, uint32_t &val)
{
        LockIt lk;

        beginTransmission(dev);
        if (write(reg) != 1)
            return (false);
        if (endTransmission() != 0)
            return (false);
        if (requestFrom(dev, 3) != 3)
            return (false);
        // BE
        val = read();
        val <<= 8;
        val |= read();
        val <<= 8;
        val |= read();
        return (true);
}

bool TwoWire::read32 (uint8_t dev, uint8_t reg, uint32_t &val)
{
        LockIt lk;

        beginTransmission(dev);
        if (write(reg) != 1)
            return (false);
        if (endTransmission() != 0)
            return (false);
        if (requestFrom(dev, 4) != 4)
            return (false);
        // BE
        val = read();
        val <<= 8;
        val |= read();
        val <<= 8;
        val |= read();
        val <<= 8;
        val |= read();
        return (true);
}



bool TwoWire::write8 (uint8_t dev, uint8_t reg, uint8_t val)
{
        LockIt lk;

        beginTransmission(dev);
        if (write(reg) != 1)
            return (false);
        if (write(val) != 1)
            return (false);
        if (endTransmission() != 0)
            return (false);
        return (true);
}


/* open connection if not already.
 * if this succeeds, either excam.connected or i2c_fd >= 0.
 */
bool TwoWire::openConnection(uint8_t dev)
{
        // get the candidate file name, NULL means op has disabled
	filename = getI2CFilename();
        if (!filename) {
            printf ("I2C: is disabled\n");
            return (false);
        }

        // done if either already open
        if (excam.connected || i2c_fd >= 0) {
            if (verbose > 1)
                printf ("I2C: %s already open\n", filename);
            return (true);
        }

        printf ("I2C: trying %s\n", filename);

        // try I2C-USB first because it fails gracefully if filename is not a tty
        excam.dev_addr = dev;
        i2c_connect (&excam, filename);
        if (excam.connected) {
            // want exclusive access
            if (::flock (excam.port, LOCK_EX|LOCK_NB) < 0) {
                if (errno == EWOULDBLOCK)
                    printf ("I2C: %s: in use by another process\n", filename);
                else
                    printf ("I2C: %s: %s\n", filename, strerror(errno));
                close (excam.port);
                excam.port = -1;
                excam.connected = 0;
            } else {
                // reset is good insurance in case bus was not shut down properly
                if (i2c_reset (&excam))
                    printf ("I2C: USB-I2C bridge %s open ok\n", filename);
                else {
                    printf ("I2C: USB-I2C bridge %s failed to reset\n", filename);
                    close (excam.port);
                    excam.port = -1;
                    excam.connected = 0;
                }
            }
        }

    #if defined(_NATIVE_I2C_LINUX) || defined(_NATIVE_I2C_FREEBSD)
        else if (GPIOOk()) {
            if (verbose)
                printf ("I2C: no USB-I2C, trying native %s\n", filename);
            i2c_fd = ::open(filename, O_RDWR);
            if (i2c_fd >= 0) {
                // want exclusive access
                if (::flock (i2c_fd, LOCK_EX|LOCK_NB) < 0) {
                    if (errno == EWOULDBLOCK)
                        printf ("I2C: %s: in use by another process\n", filename);
                    else
                        printf ("I2C: %s: %s\n", filename, strerror(errno));
                    close (i2c_fd);
                    i2c_fd = -1;
                } else {
                    printf ("I2C: open native %s ok\n", filename);
                }
            } else {
                printf ("I2C: %s failed: %s\n", filename, strerror(errno));
            }
        } else
            printf ("I2C: GPIO is off\n");

    #endif

        // success if either worked
        return (excam.connected || i2c_fd >= 0);
}



/* close connection
 */
void TwoWire::closeConnection()
{
        if (i2c_fd >= 0) {
            if (verbose)
                printf ("I2C: close native I2C\n");
            ::close (i2c_fd);
            i2c_fd = -1;
        } else if (excam.connected) {
            if (verbose)
                printf ("I2C: close Excamera Labs I2C\n");
            close (excam.port);
            excam.port = -1;
            excam.connected = 0;
        } else if (filename && verbose > 1) {
	    printf ("I2C: %s already closed\n", filename);
        }
}

/* set bus dev addr if different
 */
void TwoWire::setDev (uint8_t dev)
{
        // that's it if no change
        if (dev == dev_addr)
            return;

        // save it
        dev_addr = dev;

        bool ok = true;

    #if defined(_NATIVE_I2C_LINUX)
        // must inform linux driver of addr
        if (i2c_fd >= 0 && ioctl(i2c_fd, I2C_SLAVE_FORCE, dev_addr) < 0) {
            printf ("I2C: setDev(0x%02X): %s\n", dev_addr, strerror(errno));
            closeConnection ();         // mark as failed for subsequent use
            ok = false;
        }
    #endif

        if (ok && verbose)
            printf ("I2C: setDev(0x%02X) ok\n", dev_addr);
}

/* start an I2C session
 */
void TwoWire::begin()
{
        // let beginTransmission do the open
}


/* prepare to send bytes to the I2C slave at the given address
 */
void TwoWire::beginTransmission(uint8_t dev)
{
        // insure ready
        if (!openConnection(dev)) {
            if (filename)       // no filename just means op has disabled i2c
                printf ("I2C: beginTransmission(0x%02X): driver not open\n", dev);
            return;
        }

        // insure correct dev
        setDev(dev);

        // init
        if (verbose)
            printf ("I2C: beginTransmission(0x%02X) ok\n", dev);
        transmitting = true;
        n_txdata = 0;
}


/* buffer another byte to send.
 * returns number so buffered.
 */
size_t TwoWire::write(uint8_t datum)
{
        if (!transmitting)
            return (1);         // yes, this is what the real cpp does

        // buffer if more room
        if (n_txdata < MAX_TXBUF) {
            txdata[n_txdata++] = datum;
            return (1);
        } else {
            printf ("I2C: write buffer overflow\n");
            return (0);
        }
}




/* buffer more bytes to send.
 * returns number so buffered
 */
size_t TwoWire::write(const uint8_t *data, size_t quantity)
{
        if (transmitting) {
            for(size_t i = 0; i < quantity; i++) {
                if(!write(data[i])) {
                    return (i);
                }
            }
        }

        return (quantity);
}


/* this is where the write really happens.
 * if sendStop then send all buffered bytes to the I2C device specified in beginTransmission() then STOP.
 * else don't do anything, we expect requestFrom() will send n_txdata.
 * see twi_writeTO() for return codes:
 *   return 0: ok
 *   return 1: ?
 *   return 2: received NACK on transmit of address
 *   return 3: received NACK on transmit of data
 *   return 4: line busy
 */
uint8_t TwoWire::endTransmission(bool sendStop)
{
        if (verbose > 1) {
            printf ("I2C: endTransmission writing %d bytes to 0x%02X:", n_txdata, dev_addr);
            for (int i = 0; i < n_txdata; i++)
                printf (" %02X", txdata[i]);
            printf ("\n");
        }

        if (!sendStop) {
            if (verbose)
                printf ("I2C: endTransmission(!sendStop)\n");
            return (0); // feign success for now
        }

        bool ok = false;

    #if defined (_NATIVE_I2C_LINUX) 

        if (i2c_fd >= 0)  {

            struct i2c_rdwr_ioctl_data work_queue;
            struct i2c_msg msg[1];

            work_queue.nmsgs = 1;
            work_queue.msgs = msg;

            work_queue.msgs[0].addr = dev_addr;
            work_queue.msgs[0].len = n_txdata;
            work_queue.msgs[0].flags = 0;   // write
            work_queue.msgs[0].buf = txdata;

            ok = ioctl(i2c_fd, I2C_RDWR, &work_queue) >= 0;
            if (!ok) {
                printf ("I2C: endTransmission write %d failed: %s\n", n_txdata, strerror(errno));
            } else {
                if (verbose > 1)
                    printf ("I2C: endTransmission write %d ok\n", n_txdata);
            }

        }

    #endif

    #if defined (_NATIVE_I2C_FREEBSD)

        if (i2c_fd >= 0)  {

            struct iic_msg msgs[1];
            msgs[0].slave = dev_addr << 1;
            msgs[0].flags = IIC_M_WR;
            msgs[0].len = n_txdata;
            msgs[0].buf = txdata;

            struct iic_rdwr_data work_queue;
            work_queue.msgs = msgs;
            work_queue.nmsgs = 1;

            ok = ioctl (i2c_fd, I2CRDWR, &work_queue) >= 0;
            if (!ok) {
                printf ("I2C: endTransmission write %d failed: %s\n", n_txdata, strerror(errno));
            } else {
                if (verbose > 1)
                    printf ("I2C: endTransmission write %d ok\n", n_txdata);
            }
        }

    #endif

        if (excam.connected) {

            // send write-coded start address then send data

            ok = i2c_start (&excam, dev_addr, 0) && i2c_write (&excam, txdata, n_txdata);
            if (!ok) {
                printf ("I2C: endTransmission write %d failed\n", n_txdata);
            } else {
                if (verbose > 1)
                    printf ("I2C: endTransmission write %d ok\n", n_txdata);
            }
            i2c_stop (&excam);
        }

        // regardless, we tried
        n_txdata = 0;

        // done
        transmitting = false;

        return (ok ? 0 : 2);
}



/* ask the I2C slave at the given address to send n bytes.
 * returns the actual number received.
 * N.B. we presume the register to be read has aleady been sent.
 * N.B. if n_txdata > 0, we send that first without a STOP, then read
 */
uint8_t TwoWire::requestFrom(uint8_t dev, uint8_t nbytes)
{
        // clamp size
        if (nbytes > MAX_RXBUF) {
            printf ("I2C: requestFrom(0x%02X,%d) too many bytes, clamping to %d\n", dev, nbytes, MAX_RXBUF);
            nbytes = MAX_RXBUF;
        }

        // insure correct dev
        setDev(dev);

        if (verbose > 1 && n_txdata > 0) {
            printf ("I2C: requestFrom(0x%02X,%d) first writing %d bytes:", dev, nbytes, n_txdata);
            for (int i = 0; i < n_txdata; i++)
                printf (" %02X", txdata[i]);
            printf ("\n");
        }

    #if defined(_NATIVE_I2C_LINUX)

        if (i2c_fd >= 0) {

            // send then recv without intermediate STOP if txdata still not sent
            if (n_txdata > 0) {

                struct i2c_rdwr_ioctl_data work_queue;
                struct i2c_msg msg[2];

                work_queue.nmsgs = 2;
                work_queue.msgs = msg;

                work_queue.msgs[0].addr = dev;
                work_queue.msgs[0].len = n_txdata;
                work_queue.msgs[0].flags = 0;   // write
                work_queue.msgs[0].buf = txdata;

                work_queue.msgs[1].addr = dev;
                work_queue.msgs[1].len = nbytes;
                work_queue.msgs[1].flags = I2C_M_RD;
                work_queue.msgs[1].buf = rxdata;

                if (ioctl(i2c_fd,I2C_RDWR,&work_queue) < 0) {
                    printf ("I2C: requestFrom(0x%02X) W %d R %d failed: %s\n", dev, n_txdata, nbytes,
                                                        strerror(errno));
                    n_rxdata = 0;
                } else {
                    if (verbose > 1)
                        printf ("I2C: requestFrom(0x%02X) W %d R %d ok\n", dev, n_txdata, nbytes);
                    n_rxdata = nbytes;
                }

                // did our best to send
                n_txdata = 0;
                transmitting = false;

            } else {

                struct i2c_rdwr_ioctl_data work_queue;
                struct i2c_msg msg[1];

                work_queue.nmsgs = 1;
                work_queue.msgs = msg;

                work_queue.msgs[0].addr = dev;
                work_queue.msgs[0].len = nbytes;
                work_queue.msgs[0].flags = I2C_M_RD;
                work_queue.msgs[0].buf = rxdata;

                if (ioctl(i2c_fd,I2C_RDWR,&work_queue) < 0) {
                    printf ("I2C: requestFrom(0x%02X) R %d failed: %s\n", dev, nbytes, strerror(errno));
                    n_rxdata = 0;
                } else {
                    if (verbose > 1)
                        printf ("I2C: requestFrom(0x%02X) R %d ok\n", dev, nbytes);
                    n_rxdata = nbytes;
                }
            }
        }

    #endif // _NATIVE_I2C_LINUX

    #if defined(_NATIVE_I2C_FREEBSD)

        if (i2c_fd >= 0) {

            // send then recv without intermediate STOP if txdata still not sent
            if (n_txdata > 0) {

                struct iic_rdwr_data work_queue;
                struct iic_msg msg[2];

                work_queue.nmsgs = 2;
                work_queue.msgs = msg;

                work_queue.msgs[0].slave = dev_addr << 1;
                work_queue.msgs[0].len = n_txdata;
                work_queue.msgs[0].flags = IIC_M_NOSTOP|IIC_M_WR;
                work_queue.msgs[0].buf = txdata;

                work_queue.msgs[1].slave = dev_addr << 1;
                work_queue.msgs[1].len = nbytes;
                work_queue.msgs[1].flags = IIC_M_RD;
                work_queue.msgs[1].buf = rxdata;

                if (ioctl (i2c_fd, I2CRDWR, &work_queue) < 0) {
                    printf ("I2C: requestFrom(0x%02X) W %d R %d failed: %s\n", dev, n_txdata, nbytes,
                                                        strerror(errno));
                    n_rxdata = 0;
                } else {
                    if (verbose > 1)
                        printf ("I2C: requestFrom(0x%02X) W %d R %d ok\n", dev, n_txdata, nbytes);
                    n_rxdata = nbytes;
                }

                // did our best to send
                n_txdata = 0;
                transmitting = false;

            } else {

                // null case
                if (nbytes == 0)
                    return (0);

                struct iic_msg msgs[1];
                msgs[0].slave = dev_addr << 1;
                msgs[0].flags = IIC_M_RD;
                msgs[0].len = nbytes;
                msgs[0].buf = rxdata;

                struct iic_rdwr_data work;
                work.msgs = msgs;
                work.nmsgs = 1;

                if (ioctl (i2c_fd, I2CRDWR, &work) < 0) {
                    printf ("I2C: requestFrom(0x%02X) R %d failed: %s\n", dev, nbytes, strerror(errno));
                    n_rxdata = 0;
                } else {
                    if (verbose > 1)
                        printf ("I2C: requestFrom(0x%02X) R %d ok\n", dev, nbytes);
                    n_rxdata = nbytes;
                }

            }
        }

    #endif // _NATIVE_I2C_FREEBSD

        if (excam.connected) {

            // first send any pending tx bytes without a STOP
            bool ok = true;
            if (n_txdata > 0) {
                ok = i2c_write (&excam, txdata, n_txdata);
                if (!ok)
                    printf ("I2C: requestFrom(0x%02X) prelim W %d failed: %s\n", dev, n_txdata,
                                                                strerror(errno));
                else
                    printf ("I2C: requestFrom(0x%02X) prelim W %d ok\n", dev, n_txdata);

                // did our best to send
                n_txdata = 0;
                transmitting = false;
            }

            // now request the read regardless
            ok = i2c_start (&excam, dev_addr, 1) && i2c_read (&excam, rxdata, nbytes);
            if (!ok) {
                printf ("I2C: requestFrom(0x%02X) R %d failed: %s\n", dev, nbytes, strerror(errno));
                n_rxdata = 0;
            } else {
                n_rxdata = nbytes;
                if (verbose > 1)
                    printf ("I2C: requestFrom(0x%02X) R %d ok\n", dev, nbytes);
            }
            i2c_stop (&excam);
        }

        // prep for reading
        n_retdata = 0;

        // report actual
        return (n_rxdata);
}


/* returns number of bytes available to read
 */
int TwoWire::available(void)
{
        return (n_rxdata);
}


/* returns the next byte received from an earlier requestFrom()
 */
int TwoWire::read(void)
{
        // return in read order
        if (n_retdata < n_rxdata) {
            if (verbose > 1)
                printf ("I2C: read(0x%02X) 0x%02X %d/%d\n", dev_addr, rxdata[n_retdata],n_retdata+1,n_rxdata);
            return (rxdata[n_retdata++]);
        } else {
            printf ("I2C: read(0x%02X) buffer underflow: %d <= %d\n", dev_addr, n_rxdata, n_retdata);
            return (0x99);
        }
}
