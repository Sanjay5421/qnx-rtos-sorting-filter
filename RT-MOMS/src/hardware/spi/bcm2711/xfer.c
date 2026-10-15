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
 *  @brief             SPI transfer function.
 *  @param hdl         SPI driver handler.
 *  @param spi_dev     SPI device structure pointer.
 *  @param buf         The buffer which stores the transfer data.
 *  @param tnbytes     The number of transmit bytes.
 *  @param rnbytes     The number of receive bytes.
 *
 *  @return            EOK --success otherwise fail.
 */
int bcm2711spi_xfer(void *const hdl, spi_dev_t *const spi_dev, uint8_t *const buf, const uint32_t tnbytes, const uint32_t rnbytes)
{
    bcm2711_spi_t   *const spi = hdl;
    const uintptr_t base = spi->vbase;
    const spi_cfg_t *const cfg = &spi_dev->devinfo.cfg;
    int             status = EOK;

    spi->dma_active = false;

    /* Decide the transaction type */
    if (tnbytes == 0) {
        /* Read only */
        spi->tnbytes = rnbytes;
        spi->rnbytes = rnbytes;
    } else if (rnbytes == 0) {
        /* Write only */
        spi->tnbytes = tnbytes;
        spi->rnbytes = tnbytes;
    } else {
        /* Bidirectional transaction */
        spi->tnbytes = tnbytes;
        spi->rnbytes = rnbytes;
    }

    /* Capture max transaction length requested by client */
    spi->xlen = max(spi->tnbytes, spi->rnbytes);

    /* Capture word width in bytes */
    spi->dwidth = ((cfg->mode & SPI_MODE_WORD_WIDTH_MASK) >> 3);

    /*
     *  If client sets word_width to 32 to do a DMA xfer while (nbytes < bus->dma_thld),
     * io-spi would switch to call xfer. In this case, we need to update spi->dwidth to 8-bit
     * since hardware only supports 8-bit access for non-DMA mode.
     */
    if (spi->dwidth != 1) {
        spi->dwidth = 1;
    }

    /* Initialize receive and transmit counters */
    spi->rcnt = 0;
    spi->tcnt = 0;

    /* Copy client buffer address */
    spi->pbuf = buf;

    /* Estimate transfer time in us... The calculated dtime is only used for
     * the timeout, so it doesn't have to be that accurate. At higher clock
     * rates, a calculated dtime of 0 would mess-up the timeout calculation, so
     * round up to 1 us
     */
    spi->dtime = (spi->xlen * 1000 * 1000) / cfg->clock_rate;
    if (spi->dtime == 0) {
        spi->dtime = 1;
    }

    /* Reset the SPI interface
     * Clear both FIFOs
     */
    out32((base + BCM2711_SPI_CS), 0x0);
    out32((base + BCM2711_SPI_CS), in32((base + BCM2711_SPI_CS)) | ((SPI_CS_CLEAR_CLRTXFIFO | SPI_CS_CLEAR_CLRRXFIFO) << SPI_CS_CLEAR));

    /*
     * Start exchange. This will immediately trigger a DONE (TX
     * empty) interrupt, upon which we will fill the TX FIFO with the
     * first TX bytes.
     */
    out32((base + BCM2711_SPI_CS), ((SPI_CS_TA_ACTIVE << SPI_CS_TA)  |
                                    (SPI_CS_INTD_INT << SPI_CS_INTD) |
                                    (SPI_CS_INTR_INT << SPI_CS_INTR) |
                                    spi_dev->devctrl));

    /*
     * Pre-filling the TX FIFO up to min(spi->tnbytes, BCM2711_SPI_FIFOLEN)
     * here to avoid the SPI_CS_DONE interrupt doesn't work.
     */
    while ((spi->tcnt < spi->tnbytes) && (spi->tcnt < BCM2711_SPI_FIFOLEN)) {
        spi_slogf(_SLOG_DEBUG2, "%s: Send 0x%x", __func__, *(spi->pbuf + spi->tcnt));
        out32((base + BCM2711_SPI_FIFO), *(spi->pbuf + spi->tcnt));

        /* Update transmit counter */
        spi->tcnt += spi->dwidth;
    }

    /*
     * Wait for exchange to finish
     */
    status = bcm2711spi_wait(spi, spi->xlen);

    /* Disable interrupt and set TA to 0 */
    out32((base + BCM2711_SPI_CS), (in32(base + BCM2711_SPI_CS) &
                                   ~(SPI_CS_TA_ACTIVE << SPI_CS_TA) &
                                   ~(SPI_CS_INTD_INT << SPI_CS_INTD) &
                                   ~(SPI_CS_INTR_INT << SPI_CS_INTR)));
    if (status != EOK) {
        spi_slogf(_SLOG_ERROR, "%s: XFER failed!!!(%s)", __func__, strerror(status));
        return status;
    } else {
        return EOK;
    }
}
