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
 *  @brief             Wait for SPI transfer complete.
 *  @param  spi        SPI driver handler.
 *  @param  len        SPI transfer data length.
 *
 *  @return            EOK --success otherwise fail.
 */
int bcm2711spi_wait(bcm2711_spi_t *spi, const uint32_t len)
{
    struct timespec  timeout;
    const uint64_t   ns_timeout = spi->dtime * len * 50 * 1000UL;  /* timeout in ns. 50 times for time out */
    int status;
    bcm2711_dma_t *const dma = &spi->dma;
    spi_bus_t *const bus = spi->bus_node;

    clock_gettime(CLOCK_MONOTONIC, &timeout);
    nsec2timespec(&timeout, timespec2nsec(&timeout) + ns_timeout);

    while (1) {
        /* The sem_timedwait_monotonic() function decrements the semaphore on successful completion */
        status = sem_timedwait_monotonic(bus->sem, &timeout);
        if (status == -1) {
            spi_slogf(_SLOG_ERROR, "%s: sem_timedwait_monotonic failed: %s", __func__, strerror(errno));
            status = errno;
            if (spi->dma_active) {
                dma->dma_funcs.xfer_abort(dma->rx_ch_handle);
                dma->dma_funcs.xfer_abort(dma->tx_ch_handle);
            }
            break;
        } else {
            if (spi->dma_active) {
                spi_slogf(_SLOG_DEBUG2, "%s: DMA xfer is done", __func__);

                spi->rcnt = spi->xlen - dma->dma_funcs.bytes_left(dma->rx_ch_handle);

                /* xfer_complete is used by some DMA lib to clear INT flags.
                 * It returns EOK on success, -1 on failure. Errors are logged in DMA lib.
                 * */
                status = dma->dma_funcs.xfer_complete(dma->rx_ch_handle);
                if (status == -1) {
                    spi_slogf(_SLOG_ERROR, "%s: RX xfer_complete with an error", __func__);
                }

                status |= dma->dma_funcs.xfer_complete(dma->tx_ch_handle);
                if (status == -1) {
                    spi_slogf(_SLOG_ERROR, "%s: TX xfer_complete with an error", __func__);
                }

                if (status != EOK) {
                    status = EIO;
                }
                break;
            } else {
                /* Received SPI interrupt event */
                status = process_interrupts(spi);
                InterruptUnmask(spi->irq, spi->iid);
                if (status != 0) {
                    spi_slogf(_SLOG_DEBUG2, "%s: Transfer is not completed yet", __func__);
                } else {
                    /* Transfer is completed. Notify xfer() */
                    spi_slogf(_SLOG_DEBUG2, "%s: Transfer is completed.", __func__);
                    break;
                }
            }
        }
    }

    return status;
}
