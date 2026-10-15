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

#ifndef _BCM2711_SPI_H_
#define _BCM2711_SPI_H_

#include <atomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/slog.h>
#include <sys/slogcodes.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <hw/inout.h>

#include <aarch64/bcm2711.h>

#include <hw/io-spi.h>

/* Max supported chip select per controller/bus */
#define BCM2711_SPI_CS_MAX      2

/* Main SPI Registers Offsets */
#define BCM2711_SPI_CS          0x0
#define BCM2711_SPI_FIFO        0x4
#define BCM2711_SPI_CLK         0x8
#define BCM2711_SPI_DLEN        0xC
#define BCM2711_SPI_LTOH        0x10
#define BCM2711_SPI_DC          0x14

/* CS register definitions */
#define SPI_CS_CS           0
#define SPI_CS_CPHA         2
#define SPI_CS_CPOL         3
#define SPI_CS_CLEAR        4
#define SPI_CS_CSPOL        6
#define SPI_CS_TA           7
#define SPI_CS_DMAEN        8
#define SPI_CS_INTD         9
#define SPI_CS_INTR         10
#define SPI_CS_ADCS         11
#define SPI_CS_REN          12
#define SPI_CS_LEN          13
#define SPI_CS_LMONO        14
#define SPI_CS_TE_EN        15
#define SPI_CS_DONE         16
#define SPI_CS_RXD          17
#define SPI_CS_TXD          18
#define SPI_CS_RXR          19
#define SPI_CS_RXF          20
#define SPI_CS_CSPOL0       21
#define SPI_CS_CSPOL1       22
#define SPI_CS_CSPOL2       23
#define SPI_CS_DMA_LEN      24
#define SPI_CS_LEN_LONG     25

#define SPI_CS_CS0                   (0x0)
#define SPI_CS_CS1                   (0x1)
#define SPI_CS_CS2                   (0x2)
#define SPI_CS_CPHA_MIDDLE           (0x0)
#define SPI_CS_CPHA_BEGIN            (0x1)
#define SPI_CS_CPOL_LOW              (0x0)
#define SPI_CS_CPOL_HIGH             (0x1)
#define SPI_CS_CLEAR_NOACTION        (0x0)
#define SPI_CS_CLEAR_CLRTXFIFO       (0x1)
#define SPI_CS_CLEAR_CLRRXFIFO       (0x2)
#define SPI_CS_CSPOL_LOW             (0x0)
#define SPI_CS_CSPOL_HIGH            (0x1)
#define SPI_CS_TA_NOTACTIVE          (0x0)
#define SPI_CS_TA_ACTIVE             (0x1)
#define SPI_CS_DMAEN_DMADIS          (0x0)
#define SPI_CS_DMAEN_DMAEN           (0x1)
#define SPI_CS_INTD_NOINT            (0x0)
#define SPI_CS_INTD_INT              (0x1)
#define SPI_CS_INTR_NOINT            (0x0)
#define SPI_CS_INTR_INT              (0x1)
#define SPI_CS_ADCS_NOTAUTO          (0x0)
#define SPI_CS_ADCS_AUTO             (0x1)
#define SPI_CS_REN_WR                (0x0)
#define SPI_CS_REN_RD                (0x1)
#define SPI_CS_LEN_SPIMASTER         (0x0)
#define SPI_CS_LEN_LOSSIMASTER       (0x1)

#define SPI_CS_CSPOL0_LOW            (0x0)
#define SPI_CS_CSPOL0_HIGH           (0x1)
#define SPI_CS_CSPOL1_LOW            (0x0)
#define SPI_CS_CSPOL1_HIGH           (0x1)
#define SPI_CS_CSPOL2_LOW            (0x0)
#define SPI_CS_CSPOL2_HIGH           (0x1)
#define SPI_CS_DMA_LEN_DMADIS        (0x0)
#define SPI_CS_DMA_LEN_DMAEN         (0x1)
#define SPI_CS_LEN_LONG_BYTE         (0x0)
#define SPI_CS_LEN_LONG_WORD         (0x1)

#define SPI_CS_RXD_FIFOEMPTY         (0x0)
#define SPI_CS_RXD_FIFONOTEMPTY      (0x1)
#define SPI_CS_TXD_FIFOFULL          (0x0)
#define SPI_CS_TXD_FIFONOTFULL       (0x1)
#define SPI_CS_RXR_FIFONOTFULL       (0x0)
#define SPI_CS_RXR_FIFOFULL          (0x1)
#define SPI_CS_RXF_FIFONOTFULL       (0x0)
#define SPI_CS_RXF_FIFOFULL          (0x1)


/* CLK register definitions */
#define SPI_CLK_MASK        0x0000FFFF

/* DLEN register definitions */
#define SPI_DLEN_MASK       0x0000FFFF

/* LTOH register definitions */
#define SPI_LTOH_MASK       0x0000000F

/* DC register */
#define SPI_DC_TDREQ        0
#define SPI_DC_TPANIC       8
#define SPI_DC_RDREQ        16
#define SPI_DC_RPANIC       24

#define SPI_CS_CS_00        0x00
#define SPI_CS_CS_01        0x01
#define SPI_CS_CS_02        0x02

#define BCM2711_SPI_INPUT_CLOCK  500000000
#define BCM2711_SPI_MAX_CDIV     65536
#define BCM2711_SPI_MIN_CDIV     2

#define BCM2711_SPI_PRIORITY            21

#define BCM2711_SPI_REGLEN              0x200

#define BCM2711_SPI_FIFOLEN             16
#define BCM2711_SPI_RD_LEN              12
#define BCM2711_SPI_DEFAULT_TPANIC_TH   16
#define BCM2711_SPI_DEFAULT_RPANIC_TH   48
#define BCM2711_SPI_DEFAULT_TDREQ_TH    32
#define BCM2711_SPI_DEFAULT_RDREQ_TH    32

typedef struct {
    unsigned        dma_flags;
    dma_functions_t dma_funcs;
    void            *rx_ch_handle;
    void            *tx_ch_handle;
    uint32_t        dmarx_dreq;        /* RX DMA DREQ number */
    uint32_t        dmatx_dreq;        /* TX DMA DREQ number */
    uint32_t        rpanic_threshold;  /* DMA Read panic Threshold */
    uint32_t        tpanic_threshold;  /* DMA Write panic Threshold */
    uint32_t        rdreq_threshold;   /* DMA Read Request Threshold */
    uint32_t        tdreq_threshold;   /* DMA Write Request Threshold */
} bcm2711_dma_t;

/* The structure which maintains the various parameters
 * of the SPI module.
 */
typedef struct {
    uint64_t    pbase;               /* Base address for this SPI controller */
    uintptr_t   vbase;               /* Virtual address for pbase */
    uint32_t    irq;
    int         iid;
    uint64_t    input_clk;           /* SPI source clock frequency */
    uint8_t     *pbuf;               /* Client read/write buffer address */
    uint32_t    tnbytes;             /* Full transmit length requested by client */
    uint32_t    rnbytes;             /* Full receive length requested by client */
    uint32_t    xlen;                /* Full transaction length requested by client */
    uint32_t    tcnt;                /* Transmit counter */
    uint32_t    rcnt;                /* Receive counter */
    uint32_t    dwidth;              /* Data width. 1 = 8bits/byte, 2 = 16bits/2 bytes, 4 = 32bits/4 bytes */
    uint32_t    dtime;               /* usec per data, for time out use */
    spi_ctrl_t  *spi_ctrl;           /* The address of spi_ctrl structure */
    spi_bus_t   *bus_node;           /* The address of bus structure which is passed in by spi_init() */
    bool            dma_active;      /* DMA is active */
    bcm2711_dma_t   dma;
} bcm2711_spi_t;

/* Function proto */
int bcm2711_cfg(const void *const hdl, spi_dev_t *spi_dev);
int bcm2711_attach_intr(bcm2711_spi_t *spi);
int bcm2711spi_wait(bcm2711_spi_t *spi, const uint32_t len);
int process_interrupts(bcm2711_spi_t *spi);
int bcm2711_init_dma(bcm2711_spi_t *spi);
void bcm2711_fini_dma(bcm2711_spi_t *spi);

/* Function interface for io-spi */
int spi_init(spi_bus_t *bus);
void bcm2711spi_fini(void *const hdl);
void bcm2711spi_drvinfo(const void *const hdl, spi_drvinfo_t *info);
void bcm2711spi_devinfo(const void *const hdl, const spi_dev_t *const spi_dev, spi_devinfo_t *const info);
int bcm2711spi_setcfg(const void *const hdl, spi_dev_t *spi_dev, const spi_cfg_t *const cfg);
int bcm2711spi_xfer(void *const hdl, spi_dev_t *const spi_dev, uint8_t *const buf, const uint32_t tnbytes, const uint32_t rnbytes);
int bcm2711spi_dmaxfer(void *const hdl, spi_dev_t *spi_dev, dma_addr_t *addr, const uint32_t tnbytes, const uint32_t rnbytes);
int bcm2711_dma_allocbuf(void *const hdl, dma_addr_t *addr, const uint32_t len);
int bcm2711_dma_freebuf(void *const hdl, dma_addr_t *addr);

#endif /* _BCM2711_SPI_H_ */
