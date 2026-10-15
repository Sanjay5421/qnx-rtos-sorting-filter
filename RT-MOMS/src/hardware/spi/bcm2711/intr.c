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
 *  @brief             Process SPI interrupts.
 *  @param  bus        The SPI bus structure
 *
 *  @return            0: Transfer complete; -1: Transfer not complete
 */
int process_interrupts(bcm2711_spi_t *spi)
{
    uint32_t data;
    const uintptr_t base = spi->vbase;

    /* Transfer is complete */
    if ((in32(base + BCM2711_SPI_CS) & (1 << SPI_CS_DONE))) {
        /* If there's still data to send AND
         * TX FIFO has space for at least 1 byte
         */
        while ((spi->tcnt < spi->tnbytes) && (in32(base + BCM2711_SPI_CS) & (1 << SPI_CS_TXD))) {
            spi_slogf(_SLOG_DEBUG2, "%s: Send 0x%x", __func__, *(spi->pbuf + spi->tcnt));
            out32((base + BCM2711_SPI_FIFO), *(spi->pbuf + spi->tcnt));

            /* Update transmit counter */
            spi->tcnt += spi->dwidth;
        }

        /* If RX FIFO is not empty AND have more data to read,
         * read data from FIFO until RX FIFO is empty(RXD=0)
         */
        while ((in32(base + BCM2711_SPI_CS) & (SPI_CS_RXD_FIFONOTEMPTY << SPI_CS_RXD)) && (spi->rcnt < spi->rnbytes)) {
            data = in32(base + BCM2711_SPI_FIFO);
            spi_slogf(_SLOG_DEBUG2, "%s: Recv 0x%x", __func__, data);
            *(spi->pbuf + spi->rcnt) = (uint8_t)data;

            /* Update receive counter */
            spi->rcnt += spi->dwidth;
        }

        /* Transfer not complete */
        if (spi->rcnt < spi->rnbytes) {
            /* Don't unblock xfer function until complete */
            return -1;
        }

        /* Exchange done... */
        spi_slogf(_SLOG_INFO, "%s: Exchange done...tcnt=%d, rcnt=%d", __func__, spi->tcnt, spi->rcnt);

        /* There's no more data to send, set TA to 0 to clear bit SPI_CS_DONE */
        out32((base + BCM2711_SPI_CS), (in32(base + BCM2711_SPI_CS) & ~(SPI_CS_TA_ACTIVE << SPI_CS_TA)));
        return 0;
    }

    return -1;
}

/**
 *  @brief             Attach SPI interrupts.
 *  @param  bus        The SPI bus structure.
 *
 *  @return            EOK --success otherwise fail.
 */
int bcm2711_attach_intr(bcm2711_spi_t *spi)
{
    const spi_bus_t *const bus = spi->bus_node;

    /*
     * Attach SPI interrupt
     */
    spi->iid = InterruptAttachEvent(spi->irq, &bus->evt, _NTO_INTR_FLAGS_TRK_MSK);
    if (spi->iid == -1) {
        spi_slogf(_SLOG_ERROR, "%s: InterruptAttachEvent failed: %s", __func__, strerror(errno));
        return errno;
    } else {
        spi_slogf(_SLOG_DEBUG1, "%s: InterruptAttachEvent succeeded: irq %d", __func__, spi->irq);
    }

    return EOK;
}
