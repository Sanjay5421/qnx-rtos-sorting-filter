/*
 * $QNXLicenseC:
 * Copyright 2020,2025 QNX Software Systems.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You
 * may not reproduce, modify or distribute this software except in
 * compliance with the License. You may obtain a copy of the License
 * at: http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 *
 * This file may contain contributions from others, either as
 * contributors under the License or as licensors under other terms.
 * Please review this entire file for other proprietary rights or license
 * notices, as well as the QNX Development Suite License Guide at
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#include "proto.h"

i2c_status_t bcm2711_recv(void *hdl, void *buf, unsigned int len, unsigned int stop)
{
    bcm2711_dev_t  *dev = hdl;

    (void)stop;

    if (len <= 0) {
        return I2C_STATUS_DONE;
    }

    if ((dev->slave_addr & INVALID_ADDR) != 0) {
        return I2C_STATUS_ERROR;
    }

    if (dev->slave_addr_fmt != I2C_ADDRFMT_7BIT) {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR,
                "%s: I2C 10 Bit addressing not supported", __FUNCTION__);
        return I2C_STATUS_ERROR;
    }

    i2c_reset(dev);
    dev->tot_len = len;
    dev->cur_len = 0;
    dev->buf     = buf;
    dev->txrx    = BCM2711_I2C_RX;
    dev->status  = 0;

    out32(dev->regbase + BCM2711_I2C_SLAVEADDRREG_OFF, dev->slave_addr);
    out32(dev->regbase + BCM2711_I2C_DLENREG_OFF, dev->tot_len);

    /* Start read */
    out32(dev->regbase + BCM2711_I2C_CTRREG_OFF,
            CTRREG_I2CEN | CTRREG_START | CTRREG_READ | CTRREG_CLEAR);

    /* Enable interrupt*/
    out32(dev->regbase + BCM2711_I2C_CTRREG_OFF,
            in32(dev->regbase + BCM2711_I2C_CTRREG_OFF) | CTRREG_INTD | CTRREG_INTR);

    return bcm2711_wait_complete(dev, len);
}

void read_data(bcm2711_dev_t *dev)
{
    while (dev->cur_len < dev->tot_len) {
        if (dev->status & STATREG_RXD) {
            dev->buf[dev->cur_len++] = (int8_t)in32(dev->regbase + BCM2711_I2C_FIFOREG_OFF);
        }
        else {
            break;
        }

        dev->status = in32(dev->regbase + BCM2711_I2C_STATREG_OFF) & STATREG_MASK;
    }
}
