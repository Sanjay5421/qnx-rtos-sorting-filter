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
 *  @brief             Get SPI DMA DREQ(Data Request).
 *  @param  spi        SPI driver handler.
 *
 *  @return            0: Success, -1: Invalid parameters.
 */
static int get_dma_dreq(bcm2711_spi_t *spi)
{
    int ret = 0;
    const spi_bus_t *const bus = spi->bus_node;
    bcm2711_dma_t   *dma = &spi->dma;

    switch (bus->busno) {
        case 0:
            dma->dmarx_dreq = DREQ_SPI0RX;
            dma->dmatx_dreq = DREQ_SPI0TX;
            break;

        case 1:
            dma->dmarx_dreq = DREQ_SPI1RX;
            dma->dmatx_dreq = DREQ_SPI1TX;
            break;

        case 4:
            dma->dmarx_dreq = DREQ_SPI4RX;
            dma->dmatx_dreq = DREQ_SPI4TX;
            break;

        case 5:
            dma->dmarx_dreq = DREQ_SPI5RX;
            dma->dmatx_dreq = DREQ_SPI5TX;
            break;

        case 6:
            dma->dmarx_dreq = DREQ_SPI6RX;
            dma->dmatx_dreq = DREQ_SPI6TX;
            break;

        default:
            ret = -1;
            break;
    }

    return ret;
}

/**
 *  @brief             Set up DMA TX and RX transfer.
 *  @param  spi        SPI driver handler.
 *  @param  addr       DMA address pointer.
 *
 *  @return            EOK --success otherwise fail.
 */
static int bcm2711_setup_dma(bcm2711_spi_t *spi, dma_addr_t *addr)
{
    dma_transfer_t rx_tinfo = {0};
    dma_transfer_t tx_tinfo = {0};
    int            status;
    bcm2711_dma_t  *const dma = &spi->dma;
    dma_addr_t     dma_addr_tmp;

    /* Set dma_addr_tmp physical addr to be SPI FIFO physical addr */
    dma_addr_tmp.paddr = (off64_t)(spi->pbase + BCM2711_SPI_FIFO);

    dma_addr_tmp.len = spi->xlen;

    /* Get DMA DREQ number which is needed for DMA req_id */
    get_dma_dreq(spi);

    /* Configure TX DMA buffer address and transfer size */
    tx_tinfo.src_addrs = addr;
    tx_tinfo.dst_addrs = &dma_addr_tmp;
    tx_tinfo.xfer_unit_size = spi->dwidth; /* DMA transfer unit size = SPI data width */
    tx_tinfo.xfer_bytes = spi->tnbytes;
    tx_tinfo.mode_flags = 0;
    tx_tinfo.src_fragments = 1;
    tx_tinfo.dst_fragments = 1;
    tx_tinfo.src_flags = DMA_ADDR_FLAG_MEMORY;
    tx_tinfo.dst_flags = DMA_ADDR_FLAG_DEVICE | DMA_ADDR_FLAG_NO_INCREMENT;
    tx_tinfo.req_id = dma->dmatx_dreq;

    /* Need to update addr->len to be the actual TX length
     * Otherwise DMA lib would fail setup_xfer
     * */
    addr->len = spi->tnbytes;

    status = dma->dma_funcs.setup_xfer(dma->tx_ch_handle, &tx_tinfo);
    if (status == -1) {
        spi_slogf(_SLOG_ERROR, "%s: DMA TX setup_xfer failed!(%d)", __func__, errno);
        return errno;
    }

    /* Configure RX DMA buffer address and transfer size */
    rx_tinfo.src_addrs = &dma_addr_tmp;
    rx_tinfo.dst_addrs = addr;
    rx_tinfo.xfer_unit_size = spi->dwidth;
    rx_tinfo.xfer_bytes = spi->rnbytes;
    rx_tinfo.mode_flags = 0;
    rx_tinfo.src_fragments = 1;
    rx_tinfo.dst_fragments = 1;
    rx_tinfo.src_flags = DMA_ADDR_FLAG_DEVICE | DMA_ADDR_FLAG_NO_INCREMENT;
    rx_tinfo.dst_flags = DMA_ADDR_FLAG_MEMORY;
    rx_tinfo.req_id = dma->dmarx_dreq;

    /* Need to update addr->len to be the actual RX length
     * Otherwise DMA lib would fail setup_xfer
     * */
    addr->len = spi->rnbytes;

    status = dma->dma_funcs.setup_xfer(dma->rx_ch_handle, &rx_tinfo);
    if (status == -1) {
        spi_slogf(_SLOG_ERROR, "%s: DMA RX setup_xfer failed!(%d)", __func__, errno);
        return errno;
    }

    return EOK;
}

/**
 *  @brief             DMA channel release and cleanup.
 *  @param  spi        SPI driver handler.
 *
 *  @return            Void.
 */
void bcm2711_fini_dma(bcm2711_spi_t *spi)
{
    bcm2711_dma_t   *const dma = &spi->dma;

    if ((dma->rx_ch_handle == NULL) || (dma->tx_ch_handle == NULL)) {
        return;
    }

    dma->dma_funcs.channel_release(dma->rx_ch_handle);
    dma->dma_funcs.channel_release(dma->tx_ch_handle);

    dma->dma_funcs.fini();
}

/**
 *  @brief             DMA initialization and channel attach.
 *  @param  spi        SPI driver handler.
 *
 *  @return            EOK --success otherwise fail.
 */
int bcm2711_init_dma(bcm2711_spi_t *spi)
{
    spi_bus_t *const bus = spi->bus_node;
    unsigned  rx_ch = 0;
    unsigned  tx_ch = 0;
    bcm2711_dma_t   *dma = &spi->dma;

    /* Set defaults */
    dma->dma_flags = DMA_ATTACH_ANY_CHANNEL | DMA_ATTACH_EVENT_ON_COMPLETE;
    dma->rx_ch_handle = NULL;
    dma->tx_ch_handle = NULL;

    /* Get DMA functions */
    get_dmafuncs(&dma->dma_funcs, (int)sizeof(dma_functions_t));

    /* Init DMA */
    int const status = dma->dma_funcs.init(NULL);
    if (status == -1) {
        spi_slogf(_SLOG_ERROR, "%s: DMA init failed!", __func__);
        return errno;
    }

    /* Attach DMA RX channel */
    dma->rx_ch_handle = dma->dma_funcs.channel_attach(bus->dma_attach_opts, &bus->evt, &rx_ch, BCM2711_SPI_PRIORITY, dma->dma_flags);
    if (dma->rx_ch_handle == NULL) {
        spi_slogf(_SLOG_ERROR, "%s: RX DMA channel_attach failed!", __func__);
        dma->dma_funcs.fini();
        return errno;
    }

    /* Attach DMA TX channel */
    dma->tx_ch_handle = dma->dma_funcs.channel_attach(bus->dma_attach_opts, NULL, &tx_ch, BCM2711_SPI_PRIORITY, dma->dma_flags);
    if (dma->tx_ch_handle == NULL) {
        spi_slogf(_SLOG_ERROR, "%s: TX DMA channel_attach failed!", __func__);
        dma->dma_funcs.channel_release(dma->rx_ch_handle);
        dma->dma_funcs.fini();
        return errno;
    }

    spi_slogf(_SLOG_DEBUG2, "%s: Assigned DMA RX channel: %d, TX channel: %d", __func__, rx_ch, tx_ch);

    return EOK;
}

/**
 *  @brief             Allocate DMA buffer.
 *  @param  hdl        SPI driver handler.
 *  @param  addr       DMA address pointer.
 *  @param  len        DMA buffer size.
 *
 *  @return            EOK --success otherwise fail.
 */
int bcm2711_dma_allocbuf(void *const hdl, dma_addr_t *const addr, const uint32_t len)
{
    int  status;
    bcm2711_spi_t *spi = hdl;
    bcm2711_dma_t *const dma = &spi->dma;

    /* Allocate DMA buffer */
    status = dma->dma_funcs.alloc_buffer(dma->rx_ch_handle, addr, len, DMA_BUF_FLAG_NOCACHE);
    if (status == -1) {
        spi_slogf(_SLOG_ERROR, "%s: DMA alloc_buffer failed!(%s)", __func__, strerror(errno));
        return errno;
    }

    return EOK;
}

/**
 *  @brief             Free DMA buffer.
 *  @param  hdl        SPI driver handler.
 *  @param  addr       DMA address pointer.
 *
 *  @return            EOK --success otherwise fail.
 */
int bcm2711_dma_freebuf(void *const hdl, dma_addr_t *const addr)
{
    bcm2711_spi_t *spi = hdl;
    bcm2711_dma_t *const dma = &spi->dma;

    if (dma->rx_ch_handle == NULL) {
        return EINVAL;
    }

    dma->dma_funcs.xfer_abort(dma->rx_ch_handle);
    dma->dma_funcs.xfer_abort(dma->tx_ch_handle);

    dma->dma_funcs.free_buffer(dma->rx_ch_handle, addr);

    return EOK;
}

/**
 *  @brief             SPI DMA transfer function.
 *  @param hdl         SPI driver handler.
 *  @param spi_dev     SPI device structure pointer.
 *  @param addr        DMA address pointer.
 *  @param tnbytes     The number of transmit bytes.
 *  @param rnbytes     The number of receive bytes.
 *
 *  @return            EOK --success otherwise fail.
 */
int bcm2711spi_dmaxfer(void *const hdl, spi_dev_t *spi_dev, dma_addr_t * const addr, const uint32_t tnbytes, const uint32_t rnbytes)
{
    bcm2711_spi_t       *spi = hdl;
    const uintptr_t     base = spi->vbase;
    const spi_cfg_t     *const cfg = &spi_dev->devinfo.cfg;
    const bcm2711_dma_t *const dma = &spi->dma;
    uint32_t  val = 0;
    int       status;

    spi->dma_active = true;

    /* Capture word width in bytes */
    spi->dwidth = ((cfg->mode & SPI_MODE_WORD_WIDTH_MASK) >> 3);

    /* Switch to non-DMA mode if word width is 8-bit */
    if (spi->dwidth == 1) {
        return bcm2711spi_xfer(hdl, spi_dev, addr->vaddr, tnbytes, rnbytes);
    }

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

    /* DMA only support 32-bit word transfer */
    if ((spi->dwidth != 4) || ((spi->xlen % spi->dwidth) != 0)) {
        spi_slogf(_SLOG_ERROR, "%s: Error check alignment. word width is %d-byte, xlen is %d",__func__, spi->dwidth, spi->xlen);
        return EINVAL;
    }

    /* Initialize receive counter */
    spi->rcnt = 0;

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

    /* Set SPI DLEN */
    out32((base + BCM2711_SPI_DLEN), spi->xlen);

    /* Set SPI DC */
    val = (dma->rpanic_threshold << SPI_DC_RPANIC) | (dma->rdreq_threshold << SPI_DC_RDREQ) |
          (dma->tpanic_threshold << SPI_DC_TPANIC) | (dma->tdreq_threshold << SPI_DC_TDREQ);
    out32(base + BCM2711_SPI_DC, val);
    spi_slogf(_SLOG_DEBUG2, "%s: rpanic=%d, tpanic=%d, rdreq=%d, tdreq=%d", __func__,
              dma->rpanic_threshold, dma->tpanic_threshold,
              dma->rdreq_threshold, dma->tdreq_threshold);

    /* Setup DMA */
    status = bcm2711_setup_dma(spi, addr);
    if (status != EOK) {
        spi_slogf(_SLOG_ERROR, "%s: bcm2711_setup_dma failed(%s)", __func__, strerror(status));
        return status;
    }

    /* Start DMA RX transaction */
    status = dma->dma_funcs.xfer_start(dma->rx_ch_handle);
    if (status == -1) {
        spi_slogf(_SLOG_ERROR, "%s: DMA RX xfer_start failed(%s)", __func__, strerror(errno));
        return errno;
    }

    /* Start DMA TX transaction */
    status = dma->dma_funcs.xfer_start(dma->tx_ch_handle);
    if (status == -1) {
        spi_slogf(_SLOG_ERROR, "%s: DMA TX xfer_start failed(%s)", __func__, strerror(errno));
        return errno;
    }

    __cpu_membarrier();

    /* Enable DMA transaction on SPI */
    spi_dev->devctrl |= (SPI_CS_DMAEN_DMAEN << SPI_CS_DMAEN) | (SPI_CS_TA_ACTIVE << SPI_CS_TA) | (SPI_CS_ADCS_AUTO << SPI_CS_ADCS);
    out32((base + BCM2711_SPI_CS), spi_dev->devctrl);

    /*
     * Wait for DMA transaction to finish
     */
    spi_slogf(_SLOG_DEBUG2, "%s: waiting for DMA transaction to finish...", __func__);
    status = bcm2711spi_wait(spi, spi->xlen);
    if (status != EOK) {
        spi_slogf(_SLOG_ERROR, "%s: DMA XFER failed!!!", __func__);
        return status;
    }

    return EOK;
}
