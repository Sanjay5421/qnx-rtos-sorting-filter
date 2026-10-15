/*
 * Copyright (c) 2016, 2024, BlackBerry Limited.
 * Copyright 2024 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef EDMA5_RESMGR_H_
#define EDMA5_RESMGR_H_

#include <devctl.h>

#define IMX_EDMA5_RESMGR_PATH "/dev/edma5-resmgr"

typedef struct {
    uint32_t  request;      /* Input : HW Request ID */
    /* Output of IMX_EDMA5_ALLOC_CHANNEL: */
    uint32_t  channel_base; /* Output : Channel base address */
    uint32_t  errata;       /* Output : eDMA errata */
    uint32_t  error_irq;    /* Output : eDMA error irq */
    uint32_t  channel_irq;  /* Output : eDMA channel irq */
    uint32_t  channel;      /* Output : Allocated channel number */
} imx_edma5_channel_t;

/* Allocates a channel. Expects HW Request ID as input. Returns channel information in imx_edma5_channel_t struct.
 * Returns :
             EOK - On successful channel allocation
             ECANCELED - Caller's PID already owns a channel with requested HW request ID
             EINVAL - Issue in HW request ID. Mismatch between peripheral request and DMA instance
             EBUSY - Unable to allocate DMA channel for HW Request ID - HW request already used by another channel.
             EAGAIN - All channels are in use. Unable to allocate any channel
 */
#define IMX_EDMA5_ALLOC_CHANNEL                         (__DIOTF(_DCMD_MISC, (0), imx_edma5_channel_t))

/* Frees a channel. Expects HW Request ID as input.
 * Returns :
             EOK - On successful channel deallocation
             ECANCELED - Common fail. Caller is not an owner of HW Request ID or no channel with HW Request ID found.
             EINVAL - Issue in HW request ID. Mismatch between peripheral request and DMA instance
 */
#define IMX_EDMA5_FREE_CHANNEL                          (__DIOT(_DCMD_MISC, (1), imx_edma5_channel_t))

/* Returns total number of available (not already allocated) channels in all EDMAv5 peripherals.
 * Returns :
             EOK - Always returns EOK.
 */
#define IMX_EDMA5_GET_CHANNEL_COUNT                     (__DIOF(_DCMD_MISC, (2), uint32_t))

#endif /* EDMA5_RESMGR_H_ */
