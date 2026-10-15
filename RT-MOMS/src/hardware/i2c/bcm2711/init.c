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

void *bcm2711_init(int argc, char *argv[])
{
    static bcm2711_dev_t dev_st = { 0 };
    bcm2711_dev_t *dev = &dev_st;
    pthread_condattr_t condattr;
    int32_t err = EOK;

    if (-1 == bcm2711_options(dev, argc, argv)) {
        goto fail0;
    }

    dev->regbase = mmap_device_io(dev->reglen, dev->physbase);
    if (dev->regbase == (uintptr_t)MAP_FAILED) {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR, "mmap_device_io failed!");
        goto fail0;
    }

    i2c_reset(dev);

    (void)pthread_mutex_init(&dev->mutex, NULL);
    (void)pthread_condattr_init(&condattr);
    (void)pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
    err = pthread_cond_init(&dev->condvar, &condattr);
    if (err != EOK) {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR, "failed to init condvar");
        (void)pthread_mutex_destroy(&dev->mutex);
        (void)pthread_condattr_destroy(&condattr);
        goto fail1;
    }

    (void)pthread_condattr_destroy(&condattr);

    err = pthread_create(NULL, NULL, &bcm2711_wait_thread, dev);
    if (err != EOK) {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR, "failed to create IST thread for irq %d (%s)",
                dev->intr, strerror(err));
        goto fail1;
    }
    return dev;

fail1:
    munmap_device_io(dev->regbase, dev->reglen);
fail0:
    bcm2711_i2c_slogf(dev, _SLOG_ERROR, "init failed!");
    return NULL;
}

void i2c_reset(bcm2711_dev_t *dev)
{
    disable_interrupt(dev);

    out32(dev->regbase + BCM2711_I2C_CTRREG_OFF, CTRREG_I2CEN | CTRREG_CLEAR);
    out32(dev->regbase + BCM2711_I2C_STATREG_OFF, STATREG_ERR | STATREG_CLKT | STATREG_DONE);
}

void disable_interrupt(bcm2711_dev_t *dev)
{
    out32(dev->regbase + BCM2711_I2C_CTRREG_OFF,
        in32(dev->regbase + BCM2711_I2C_CTRREG_OFF) & ~(CTRREG_INTR | CTRREG_INTT | CTRREG_INTD));
}
