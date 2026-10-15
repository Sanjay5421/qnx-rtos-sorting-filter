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
 *  @brief             SPI configuration function.
 *  @param  hdl        SPI driver handler.
 *  @param  spi_dev    SPI device structure.
 *
 *  @return            EOK --success otherwise fail.
 */
int bcm2711_cfg(const void *const hdl, spi_dev_t *spi_dev)
{
    const bcm2711_spi_t *const spi = hdl;
    uintptr_t     const base = spi->vbase;
    spi_cfg_t     *cfg = &spi_dev->devinfo.cfg;
    uint32_t      devctrl = 0;
    uint32_t      divisor = 0;
    uint16_t      cdiv = 0;
    uint32_t      val;

    if (cfg == NULL) {
        return EINVAL;
    }

    if (spi_dev->devinfo.devno > BCM2711_SPI_CS_MAX) {
        spi_slogf(_SLOG_ERROR, "%s: devno(%d) should be smaller than %d", __func__, spi_dev->devinfo.devno, BCM2711_SPI_CS_MAX);
        return EINVAL;
    }

    /* For HW limitation, only 8-bit and 32-bit word width are supported. */
    switch ((cfg->mode & SPI_MODE_WORD_WIDTH_MASK)) {
        /* From BCM2711 Spec, it says:
         * 9.6.3. DMA
         * Note: In order to function correctly, each DMA channel must be set to
         * perform 32-bit transfers when communicating with the SPI.
         * */
        case SPI_MODE_WORD_WIDTH_32:

        /* For BCM2711 SPI:
         * The FIFO is normally accessed with 8-bit width by the CPU and 32-bit width
         * by the DMA engine. Hardware only support 8-bit access for non-DMA mode.
         * */
        case SPI_MODE_WORD_WIDTH_8:
            break;

        default:
            spi_slogf(_SLOG_ERROR, "%s: %d-bit word width is not supported by this controller",
                                __func__, cfg->mode & SPI_MODE_WORD_WIDTH_MASK);
            return EINVAL;
    }

    /* Gather default CS and clock polarity related configuration
     * The SPI module is supplied with a 500MHz reference clock.
     */
    if (cfg->mode & SPI_MODE_CSPOL_HIGH) {
        devctrl |= (SPI_CS_CSPOL_HIGH << SPI_CS_CSPOL);   /* CS Polarity: CS active high */
    }

    if (cfg->mode & SPI_MODE_CPOL_1) {
        devctrl |= (SPI_CS_CPOL_HIGH << SPI_CS_CPOL);     /* Clock Polarity: Rest state of clock high */
    }

    if (cfg->mode & SPI_MODE_CPHA_1) {
        devctrl |= (SPI_CS_CPHA_BEGIN << SPI_CS_CPHA);    /* Clock Phase: SCLK beginning of data bit */
    }

    /* Set SPI_CLK only when it is different than current clock rate */
    if (spi_dev->devinfo.current_clkrate != cfg->clock_rate) {

        /* Calculate the SPI target operational speed.
         * The SPI module is supplied with a 500MHz reference clock.
         */
        divisor = (uint32_t)((spi->input_clk + (cfg->clock_rate / 2)) / cfg->clock_rate);
        spi_slogf(_SLOG_DEBUG1, "%s: target_sclk=%d, dvif=%f, div=%d", __func__,
                        cfg->clock_rate, (float)(spi->input_clk / cfg->clock_rate), divisor);

        if (divisor < BCM2711_SPI_MIN_CDIV) {
            divisor = BCM2711_SPI_MIN_CDIV;
        } else if (divisor >= BCM2711_SPI_MAX_CDIV) {
            divisor = BCM2711_SPI_MAX_CDIV;
        } else {
            /* Divisor must be the multiple of 2. TRM suggests to round down
             * the odded number, but it seems we can get more closed rate
             * by rounding up
             */
            divisor = divisor + (divisor % 2);
        }

        /* Update the best rate */
        cfg->clock_rate = (uint32_t)(spi->input_clk / divisor);

        val = (uint32_t)(spi->input_clk / cfg->clock_rate);
        val = (val >= BCM2711_SPI_MAX_CDIV) ? 0 : val;
        cdiv = (uint16_t)(val);
        out32((base + BCM2711_SPI_CLK), (uint32_t)cdiv);

        /* Update current clock rate */
        spi_dev->devinfo.current_clkrate = cfg->clock_rate;
    } else {
        spi_slogf(_SLOG_DEBUG2, "%s: Device clock rate is already set to %d", __func__, cfg->clock_rate);
    }

    /* Add Chip Select/Physical device ID */
    devctrl |= spi_dev->devinfo.devno;

    /* Update the SPI device devctrl reg value */
    spi_dev->devctrl = devctrl;

    spi_slogf(_SLOG_DEBUG1, "%s: sclk=%d, divisor=%d, devctrl=0x%x", __func__, cfg->clock_rate, divisor, devctrl);

    return EOK;
}
