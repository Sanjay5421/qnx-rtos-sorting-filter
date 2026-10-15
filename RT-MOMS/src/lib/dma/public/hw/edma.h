/*
 * Copyright 2016, 2023-2025, BlackBerry Limited.
 * Copyright 2016, Freescale Semiconductor, Inc.
 * Copyright 2017, 2023 NXP
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
#ifndef EDMA_H_
#define EDMA_H_

/**
 * @file        src/lib/dma/public/hw/edma.h
 * @addtogroup  edma
 * @{
 */

#include <stdint.h>
#include <hw/dma.h>
#include <soc/nxp/imx8/common/imx_edma_channel.h>
#include <soc/nxp/imx8/common/imx_edma_mp.h>
#include <soc/nxp/imx8/common/imx_edma_requests.h>

/** Defines maximum number of TCD descriptors. The source or destination buffer consists of one or multiple fragments
 *  of varying lengths. Each fragment configures one TCD descriptor. So maximum number of fragments is equal to maximum
 *  number of TCD descriptors.
 */
#define MAX_DESCRIPTORS 256
/** eDMA driver description string */
#ifndef IMX_DMA_DESCRIPTION_STR
    #define IMX_DMA_DESCRIPTION_STR    "i.MX eDMA Controller driver"
#endif

/* Define default ERRATA values */
#ifndef IMX_DMA0_ERRATA
    #define IMX_DMA0_ERRATA            0
#endif
#ifndef IMX_DMA1_ERRATA
    #define IMX_DMA1_ERRATA            0
#endif
#ifndef IMX_DMA2_ERRATA
    #define IMX_DMA2_ERRATA            0
#endif
#ifndef IMX_DMA3_ERRATA
    #define IMX_DMA3_ERRATA            0
#endif

/* eDMA ERRATA */
#define IMX_EDMA_ERRATA_SBR_ATTR_SWAP  0x1      /* i.MX8QM A0 has swapped RD and WR attributes for eDMA in Audio subsystem */
#define IMX_EDMA_ERRATA_EEOP_ESDA_NOT_WORKING   0x02    /* i.MX93 A0 edma v4 final destination address not written to SDA register */
#define IMX_EDMA_ERRATA_NBYTES_8_MULTIPLES      0x04    /* i.MX93 A0 edma v4 worked only with NBYTES set to multiples of 8 */
#define IMX_EDMA_ERRATA_SINGLE_TRANSFER_ONLY    0x08    /* i.MX93 A0 edma v4 could handle only one active channel at the time */

#define IMX_EDMA_ERRATA_93_A0_V4       0x2      /* i.MX93 11x11 A0 has multiple issues in eDMA v4 */

/** @} */ /* End of edma */

#endif /* EDMA_H_ */
