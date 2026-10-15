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

int bcm2711_options(bcm2711_dev_t *dev, int argc, char *argv[])
{
    int c;
    int prev_optind;
    int done = 0;

    /* Initialization */
    dev->intr = BCM2711_I2C0_IRQ;
    dev->iid = -1;
    dev->physbase = 0;
    dev->regbase = (uintptr_t)MAP_FAILED;
    dev->reglen = BCM2711_I2C_REGLEN;
    dev->slave_addr = 0;
    dev->clk = -1;
    /* Always log messages with severity "ERROR" or above. */
    dev->verbose = _SLOG_ERROR;
    uint64_t val = 0UL;

    while (!done) {
        prev_optind = optind;
        c = getopt(argc, argv, "i:p:s:c:g:v");
        switch (c) {
            case 'p':
                errno = 0;
                val = strtoul(optarg, &optarg, 0);
                if (errno != EOK) {
                    bcm2711_i2c_slogf(dev, _SLOG_ERROR, "Invalid I2C base address, errno: %d", errno);
                    return -1;
                }
                dev->physbase = (unsigned)val;
                break;
            case 's':
                errno = 0;
                val = strtoul(optarg, &optarg, 0);
                if (errno != EOK) {
                    bcm2711_i2c_slogf(dev, _SLOG_ERROR, "Invalid Slave address, errno: %d", errno);
                    return -1;
                }
                dev->slave_addr = (unsigned)val;
                break;
            case 'c':
                errno = 0;
                val = strtoul(optarg, &optarg, 0);
                if (errno != EOK) {
                    bcm2711_i2c_slogf(dev, _SLOG_ERROR, "Invalid I2C input clock, errno: %d", errno);
                    return -1;
                }
                dev->clk = (unsigned)val;
                break;
            case 'i':
                errno = 0;
                val = strtoul(optarg, &optarg, 0);
                if (errno != EOK) {
                    bcm2711_i2c_slogf(dev, _SLOG_ERROR, "Invalid I2C interrupt, errno: %d", errno);
                    return -1;
                }
                dev->intr = (int)val;
                break;
            case 'v':
                dev->verbose++;
                break;
            case '?':
                if (optopt == '-') {
                    ++optind;
                    break;
                }
                return -1;

            case -1:
                if (prev_optind < optind) { /* -- */
                    return -1;
                }
                if (argv[optind] == NULL) {
                    done = 1;
                    break;
                }
                if (*argv[optind] != '-') {
                    ++optind;
                    break;
                }
                return -1;

            case ':':
            default:
                return -1;
        }
    }

    if (dev->clk == 0) {
        dev->clk = 5000; /*default to 100KHz*/
    }

    if (dev->physbase == BCM2711_I2C2_BASE) {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR, "Port2 is not supported as Master port");
        return -1;
    }
    else if ((dev->physbase != BCM2711_I2C0_BASE)  &&
        (dev->physbase != BCM2711_I2C1_BASE) &&
        (dev->physbase != BCM2711_I2C3_BASE) &&
        (dev->physbase != BCM2711_I2C4_BASE) &&
        (dev->physbase != BCM2711_I2C5_BASE) &&
        (dev->physbase != BCM2711_I2C6_BASE)) {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR, "Invalid I2C master base address");
        return -1;
    }
    else {
        bcm2711_i2c_slogf(dev, _SLOG_ERROR, "I2C phy address: 0x%x", dev->physbase);
    }

    return 0;
}
