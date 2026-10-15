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

enum opt_index {RPANIC, TPANIC, DMA, END};

/**
 *  @brief             Process board specific options which is set in SPI config file.
 *  @param  spi        SPI driver handler.
 *  @param  options    Board specific options.
 *
 *  @return            EOK --success otherwise fail.
 */
static int process_args(bcm2711_spi_t *spi, char *options)
{
    char     *value;
    unsigned val;
    int      opt = 0;
    const char *c;
    bcm2711_dma_t   *dma = &spi->dma;

    char *bcm2711spi_opts[] = {
        [RPANIC] = "rpanic",            /* DMA Read panic Threshold */
        [TPANIC] = "tpanic",            /* DMA Write panic Threshold */
        [END] = NULL
    };

    if (options == NULL) {
        spi_slogf(_SLOG_WARNING, "%s: No board specific args passed in", __func__);
        return EOK;
    }

    while ((options != NULL) && (*options != '\0')) {
        c = options;
        opt = getsubopt(&options, bcm2711spi_opts, &value);
        if (opt == -1) {
            spi_slogf(_SLOG_ERROR, "%s: Unsupported SPI device driver args: %s", __func__, c);
            return EINVAL;
        }

        switch (opt) {
            case RPANIC:
                val = (unsigned)strtoul(value, NULL, 0);
                if ((val > 0) && (val < 64)) {
                    dma->rpanic_threshold = val;
                } else {
                    spi_slogf(_SLOG_ERROR, "%s: Invalid rpanic value %d. Should be 0 ~ 64", __func__, val);
                    return EINVAL;
                }
                break;

            case TPANIC:
                val = (unsigned)strtoul(value, NULL, 0);
                if ((val > 0) && (val < 64)) {
                    dma->tpanic_threshold = val;
                } else {
                    spi_slogf(_SLOG_ERROR, "%s: Invalid tpanic value %d. Should be 0 ~ 64", __func__, val);
                    return EINVAL;
                }
                break;

            default:
                return EINVAL;
        }
    }

    return EOK;
}

/**
 *  @brief             Initialize SPI controller and devices.
 *  @param  spi        SPI driver handler.
 *
 *  @return            EOK --success otherwise fail.
 */
static int init_device(bcm2711_spi_t *spi)
{
    int             status;
    uintptr_t       base;
    spi_bus_t       *const bus = spi->bus_node;
    spi_dev_t       *spi_dev = bus->devlist;

    /*
     * Map in SPI registers
     */
    base = (uintptr_t)mmap(NULL, BCM2711_SPI_REGLEN, PROT_READ | PROT_WRITE | PROT_NOCACHE, MAP_PHYS | MAP_SHARED, NOFD, (off_t)spi->pbase);
    if (base == (uintptr_t)MAP_FAILED) {
        spi_slogf(_SLOG_ERROR, "%s: Failed to map SPI registers(%s)", __func__, strerror(errno));
        return errno;
    }

    spi->vbase = base;

    /* Reset the SPI interface
     * Clear both FIFOs
     */
    out32((base + BCM2711_SPI_CS), 0x0);
    out32((base + BCM2711_SPI_CS), in32((base + BCM2711_SPI_CS)) | ((SPI_CS_CLEAR_CLRTXFIFO | SPI_CS_CLEAR_CLRRXFIFO) << SPI_CS_CLEAR));

    /*
     * Initial device configuration with defaults from config file
     */
    while (spi_dev != NULL) {
        status = bcm2711_cfg(spi, spi_dev);
        if (status != EOK) {
            spi_slogf(_SLOG_ERROR, "%s: bcm2711_cfg failed", __func__);
            return status;
        }
        spi_dev = spi_dev->next;
    }

    /* Init DMA */
    status = bcm2711_init_dma(spi);
    if (status != EOK) {
        spi_slogf(_SLOG_ERROR, "%s: bcm2711_init_dma failed", __func__);
    }

    /* Attach SPI interrupt */
    status = bcm2711_attach_intr(spi);
    if (status != EOK) {
        spi_slogf(_SLOG_ERROR, "%s: bcm2711_attach_intr failed", __func__);
    }

    return status;
}

/**
 *  @brief             SPI initialization.
 *  @param  bus        The SPI bus structure.
 *
 *  @return            EOK --success otherwise fail.
 */
int spi_init(spi_bus_t *bus)
{
    bcm2711_spi_t *spi = NULL;
    int           status = EOK;
    bcm2711_dma_t *dma;

    if (bus == NULL) {
        spi_slogf(_SLOG_ERROR, "%s: SPI bus structure is NULL!", __func__);
        return EINVAL;
    }

    spi = calloc(1, sizeof(bcm2711_spi_t));
    if (spi == NULL) {
        spi_slogf(_SLOG_ERROR, "%s: Failed to alloc mem for bcm2711_spi_t spi", __func__);
        return ENOMEM;
    }

    /* Save spi_ctrl to driver structure */
    spi->spi_ctrl = bus->spi_ctrl;
    spi->bus_node = bus;

    /* Get other SPI driver functions */
    bus->funcs->spi_fini = bcm2711spi_fini;
    bus->funcs->drvinfo  = bcm2711spi_drvinfo;
    bus->funcs->devinfo = bcm2711spi_devinfo;
    bus->funcs->setcfg = bcm2711spi_setcfg;
    bus->funcs->xfer  = bcm2711spi_xfer;
    bus->funcs->dma_xfer  = bcm2711spi_dmaxfer;
    bus->funcs->dma_allocbuf = bcm2711_dma_allocbuf;
    bus->funcs->dma_freebuf = bcm2711_dma_freebuf;

    /* Get controller parameters */
    spi->pbase = bus->pbase;
    spi->irq = bus->irq;
    spi->input_clk = bus->input_clk;

    /* Update SPI transaction buffer boundary */
    bus->buf_max = SPI_BUF_MAX_64K;

    dma = &spi->dma;
    dma->rpanic_threshold = BCM2711_SPI_DEFAULT_RPANIC_TH;
    dma->tpanic_threshold = BCM2711_SPI_DEFAULT_TPANIC_TH;
    dma->rdreq_threshold = BCM2711_SPI_DEFAULT_RDREQ_TH;
    dma->tdreq_threshold = BCM2711_SPI_DEFAULT_TDREQ_TH;
    spi->dma_active = true;
    spi->iid = -1;
    spi->vbase = (uintptr_t)MAP_FAILED;

    /* Process args, override the defaults */
    if (bus->bs != NULL) {
        status = process_args(spi, bus->bs);
        if (status != EOK) {
            spi_slogf(_SLOG_ERROR, "%s: process_args failed", __func__);
            bcm2711spi_fini(spi);
            return status;
        }
    }

    /* Init SPI device */
    status = init_device(spi);
    if (status != EOK) {
        spi_slogf(_SLOG_ERROR, "%s: init_device failed: %s", __func__, strerror(status));
        bcm2711spi_fini(spi);
        return status;
    }

    /*
     * Create SPI chip select devices
     */
    status = spi_create_devs(bus->devlist);
    if (status != EOK) {
        spi_slogf(_SLOG_ERROR, "%s: spi_create_devs for %s failed", __func__, bus->devlist->devinfo.name);
        bcm2711spi_fini(spi);
        return status;
    }

    /* Save driver structure to drvhdl */
    bus->drvhdl = spi;

    return status;
}
