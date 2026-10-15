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

int bcm2711_set_bus_speed(void *hdl, unsigned int speed, unsigned int *ospeed)
{
    bcm2711_dev_t	*dev = hdl;
    unsigned int ckdiv = 0;
    uint16_t rx_delay;
    uint32_t delay_reg;

    dev->speed = speed;

    ckdiv = (BCM2711_CORE_FREQ / dev->speed);
    if (ckdiv > CLKTREG_BIT_MSK) {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR,
                            "Invalid clkdiv %d, bus speed %d  \n", ckdiv, dev->speed);
        errno = EINVAL;
        return -1;
    }
    out32(dev->regbase + BCM2711_I2C_CLKDIVREG_OFF, ckdiv);
    /* Save actual clk value. */
    dev->clk = (BCM2711_CORE_FREQ / ckdiv);
    bcm2711_i2c_slogf(dev, _SLOG_DEBUG1, "%s: clk = %u Hz, ckdiv = 0x%x", __FUNCTION__, dev->clk, ckdiv);

    /* Set delay values */
    rx_delay = (uint16_t)(ckdiv/4);
    bcm2711_i2c_slogf(dev, _SLOG_DEBUG1, "%s: rx_delay = 0x%x", __FUNCTION__, rx_delay);
    delay_reg = in32(dev->regbase + BCM2711_I2C_DELAYREG_OFF);
    delay_reg &= ~(DELAYREG_REDL_BIT_MSK);
    delay_reg |= (((uint32_t)rx_delay << DELAYREG_REDL_SHIFT_BITS) & DELAYREG_REDL_BIT_MSK);
    out32(dev->regbase + BCM2711_I2C_DELAYREG_OFF, delay_reg);

    return 0;
}
