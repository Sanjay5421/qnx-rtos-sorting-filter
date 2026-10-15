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

#ifndef __PROTO_H_INCLUDED
#define __PROTO_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/neutrino.h>
#include <sys/mman.h>
#include <hw/inout.h>
#include <hw/i2c.h>
#include <sys/slog.h>
#include <sys/slogcodes.h>
#include <aarch64/bcm2711.h>

#define BCM2711_I2C_IDLE    0U
#define BCM2711_I2C_TX      1U
#define BCM2711_I2C_RX      2U

typedef struct _bcm2711_dev {

    uint8_t             *buf;
    unsigned            buflen;
    unsigned            reglen;
    uintptr_t           regbase;
    unsigned            physbase;
    int                 intr;
    int                 iid;
    unsigned            txrx;
    uint32_t            status;
    unsigned            cur_len;
    unsigned            tot_len;
    unsigned            slave_addr;
    unsigned            clk;
    i2c_addrfmt_t       slave_addr_fmt;
    unsigned            verbose;
    unsigned            speed;
    pthread_mutex_t     mutex;
    pthread_cond_t      condvar;
    i2c_status_t        txrx_status;
} bcm2711_dev_t;

#define BCM2711_DEFAULT_VERBOSE         (_SLOG_INFO)
#define INVALID_ADDR                    (0x80U)

#define BCM2711_I2C_CTRREG_OFF          (0x00U)
#define CTRREG_I2CEN                    (0x8000U)   // bit 15  I2C Enable
#define CTRREG_INTR                     (0x400U)    // bit 10  Interrupt on RX
#define CTRREG_INTT                     (0x200U)    // bit 9   Interrupt on TX
#define CTRREG_INTD                     (0x100U)    // bit 8   Interrupt on DONE
#define CTRREG_START                    (0x80U)     // bit 7   Start Transfer
#define CTRREG_CLEAR                    (0x30U)     // bit 5:4 FIFO Clear
#define CTRREG_READ                     (0x1U)      // bit 1   Read Transfer

#define BCM2711_I2C_STATREG_OFF         (0x04U)
#define STATREG_MASK                    (0x000001FFU)
#define STATREG_CLKT                    (0x200U)   // bit 9 Clock Stretch Timeout
#define STATREG_ERR                     (0x100U)   // bit 8 ACK Error
#define STATREG_RXF                     (0x80U)    // bit 7 FIFO Full
#define STATREG_TXE                     (0x40U)    // bit 6 FIFO Empty
#define STATREG_RXD                     (0x20U)    // bit 5 FIFO contains Data
#define STATREG_TXD                     (0x10U)    // bit 4 FIFO can accept Data
#define STATREG_RXR                     (0x8U)     // bit 3 FIFO needs Reading (¾ full)
#define STATREG_TXW                     (0x4U)     // bit 2 FIFO needs Writing (¼ full)
#define STATREG_DONE                    (0x2U)     // bit 1 Transfer Done
#define STATREG_TA                      (0x1U)     // bit 0 Transfer Active

#define BCM2711_I2C_DLENREG_OFF         (0x08U)
#define DLENREG_BIT_MSK                 (0x0000FFFFU)

#define BCM2711_I2C_SLAVEADDRREG_OFF    (0x0CU)
#define SLAVEADDRREG_BIT_MSK            (0x0000007FU)

#define BCM2711_I2C_FIFOREG_OFF         (0x10U)
#define FIFOREG_BIT_MSK                 (0x000000FFU)

#define BCM2711_I2C_CLKDIVREG_OFF       (0x14U)
#define CLKDIVREG_BIT_MSK               (0x0000FFFFU)

#define BCM2711_I2C_DELAYREG_OFF        (0x18U)
#define DELAYREG_FEDL_SHIFT_BITS        (16U)
#define DELAYREG_FEDL_BIT_MSK           (0xFFFF0000U)
#define DELAYREG_REDL_SHIFT_BITS        (0U)
#define DELAYREG_REDL_BIT_MSK           (0x0000FFFFU)

#define BCM2711_I2C_CLKTREG_OFF         (0x1CU)
#define CLKTREG_BIT_MSK                 (0x0000FFFFU)

#define BCM2711_I2C_REGLEN              (0x200U)

#define BCM2711_I2C_DEF_INTR_PRIORITY   21

#define bcm2711_i2c_slogf(dev, level, fmt, ...)     \
    (i2c_slogf((dev)->verbose, (level), (fmt), ##__VA_ARGS__))

//extern bcm2711_dev_t dev_st;
void *bcm2711_init(int argc, char *argv[]);
void bcm2711_fini(void *hdl);
int bcm2711_options(bcm2711_dev_t *dev, int argc, char *argv[]);

int bcm2711_set_slave_addr(void *hdl, unsigned int addr, i2c_addrfmt_t fmt);
int bcm2711_set_bus_speed(void *hdl, unsigned int speed, unsigned int *ospeed);
int bcm2711_version_info(i2c_libversion_t *version);
int bcm2711_driver_info(void *hdl, i2c_driver_info_t *info);
i2c_status_t bcm2711_recv(void *hdl, void *buf, unsigned int len, unsigned int stop);
i2c_status_t bcm2711_send(void *hdl, void *buf, unsigned int len, unsigned int stop);
i2c_status_t bcm2711_wait_complete(bcm2711_dev_t *dev, uint32_t len);
void *bcm2711_wait_thread(void* arg);

void i2c_reset(bcm2711_dev_t *dev);
void push_data(bcm2711_dev_t *dev);
void read_data(bcm2711_dev_t *dev);
void disable_interrupt(bcm2711_dev_t *dev);

#endif  /*  __PROTO_H_INCLUDED */
