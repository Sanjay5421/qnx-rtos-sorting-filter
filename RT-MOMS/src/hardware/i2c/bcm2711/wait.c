/*
 * $QNXLicenseC:
 * Copyright 2020,2025, QNX Software Systems.
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

static void pthread_mutex_lock_or_abort(bcm2711_dev_t *dev)
{
    int32_t const ret = pthread_mutex_lock(&dev->mutex);
    if (ret != EOK) {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR, "abort due to lock mutex err: %d", ret);
        abort();
    }
}

static void pthread_mutex_unlock_or_abort(bcm2711_dev_t *dev)
{
    int32_t const ret = pthread_mutex_unlock(&dev->mutex);
    if (ret != EOK) {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR, "abort due to unlock mutex err: %d", ret);
        abort();
    }
}

i2c_status_t bcm2711_wait_complete(bcm2711_dev_t *dev, uint32_t len)
{
    int32_t err;
    i2c_status_t ret = I2C_STATUS_ERROR;
    struct timespec abstime;
    uint64_t bus_cycle, cycles, timeout_ns;

    // Pi4 uses Broadcom Serial Control (BSC) fast-mode (400Kb/s) as master controller with 16-byte FIFO.
    // This function only receives signal when transaction complete or error happens.
    (void)clock_gettime(CLOCK_MONOTONIC, &abstime);

    // setup timeout value based on transaction length and default clock stretch of 64 bus cycle
    bus_cycle = 1000000000ULL / (uint64_t)dev->speed;
    cycles = (((uint64_t)len + 1ULL) * 9ULL) + 64ULL;
    timeout_ns = bus_cycle * cycles * 5ULL;  // 5 times of timeout value
    timeout_ns += (1000000000ULL * (uint64_t)abstime.tv_sec) + (uint64_t)(abstime.tv_nsec);

    nsec2timespec(&abstime, timeout_ns);

    pthread_mutex_lock_or_abort(dev);

    err = pthread_cond_timedwait(&dev->condvar, &dev->mutex, &abstime);
    if (err == EOK) {
        ret = dev->txrx_status;
    }
    else {
        if (err == ETIMEDOUT) {
            bcm2711_i2c_slogf(dev, _SLOG_ERROR, "wait timeout: %d", err);
            ret = I2C_STATUS_ABORT;
        }
        else {
            ret = I2C_STATUS_ERROR;
        }
    }
    pthread_mutex_unlock_or_abort(dev);

    return ret;
}

void *bcm2711_wait_thread(void* arg)
{
    bcm2711_dev_t *dev =  (bcm2711_dev_t *)arg;

    int32_t err;

    disable_interrupt(dev);

    dev->iid = InterruptAttachThread(dev->intr, _NTO_INTR_FLAGS_NO_UNMASK);
    if (dev->iid == -1) {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR, "Failed to attach IST");
        exit(EXIT_FAILURE);
    }
    err = pthread_setschedprio(pthread_self(), BCM2711_I2C_DEF_INTR_PRIORITY);
    if (err != EOK) {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR, "Failed to set IST priority: %d", BCM2711_I2C_DEF_INTR_PRIORITY);
    }

    err = pthread_setname_np(0, "event_handler");
    if (err != EOK) {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR, "Failed to set IST name");
    }

    while (1)  {
        err = InterruptWait(_NTO_INTR_WAIT_FLAGS_FAST | _NTO_INTR_WAIT_FLAGS_UNMASK, NULL);
        if (err != -1) {
            if (dev->txrx == BCM2711_I2C_IDLE) {
                /* interrupt for other controller */
                continue;
            }
            /* Get status */
            dev->status = in32(dev->regbase + BCM2711_I2C_STATREG_OFF) & STATREG_MASK;

            if ((dev->status & (STATREG_ERR | STATREG_CLKT)) != 0U) {
                disable_interrupt(dev);
                bcm2711_i2c_slogf(dev, _SLOG_DEBUG1, "intr (0x%x:%u %u) status: 0x%x ctrl: 0x%x",
                        dev->slave_addr, dev->tot_len, dev->cur_len, dev->status, in32(dev->regbase + BCM2711_I2C_CTRREG_OFF));
                dev->txrx = BCM2711_I2C_IDLE;
                pthread_mutex_lock_or_abort(dev);
                dev->txrx_status = I2C_STATUS_NACK;
                err = pthread_cond_signal(&dev->condvar);
                if (err != EOK) {
                    bcm2711_i2c_slogf(dev, _SLOG_ERROR, "failed to signal wait cond: err = %d", err);
                    pthread_mutex_unlock_or_abort(dev);
                    break;
                }
                pthread_mutex_unlock_or_abort(dev);
            }
            else if ((dev->status & STATREG_DONE) != 0U) {
                disable_interrupt(dev);
                /* Check if any data left */
                read_data(dev);
                dev->txrx = BCM2711_I2C_IDLE;
                pthread_mutex_lock_or_abort(dev);
                dev->txrx_status = I2C_STATUS_DONE;
                err = pthread_cond_signal(&dev->condvar);
                if (err != EOK) {
                    bcm2711_i2c_slogf(dev, _SLOG_ERROR, "failed to signal wait cond: err = %d", err);
                    pthread_mutex_unlock_or_abort(dev);
                    break;
                }
                pthread_mutex_unlock_or_abort(dev);
            }
            else if ((dev->txrx == BCM2711_I2C_RX) && ((dev->status & STATREG_RXR) != 0U)) {
                if ((dev->status & STATREG_RXD) != 0U) {
                    read_data(dev);
                }
            }
            else if ((dev->txrx == BCM2711_I2C_TX) && ((dev->status & STATREG_TXW) != 0U)) {
                if ((dev->status & STATREG_TXD) != 0U) {
                    push_data(dev);
                }
            }
            else {
                // Do nothing
            }
        }
        else {
            bcm2711_i2c_slogf(dev, _SLOG_DEBUG1, "Interrupt Wait unexpected ret: %d", errno);
        }
    }
    return NULL;
}
