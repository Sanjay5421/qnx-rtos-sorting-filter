/*
 * Copyright (c) 2022, BlackBerry Limited.
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

#include "bcm2711spi.h"

/**
 *  @brief             Set SPI device configuration.
 *  @param hdl         SPI driver handler.
 *  @param spi_dev     SPI device structure pointer.
 *  @param cfg         The spi_cfg_t structure which stores the config info
 *
 *  @return            EOK --success otherwise fail
 */
int bcm2711spi_setcfg(const void *const hdl, spi_dev_t *spi_dev, const spi_cfg_t *const cfg)
{
    memcpy(&(spi_dev->devinfo.cfg), cfg, sizeof(spi_cfg_t));

    return bcm2711_cfg(hdl, spi_dev);
}
