/*
 * Copyright (c) 2024, BlackBerry Limited.
 * Copyright (c) 2023 NXP
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

#ifndef EDMA5_H_
#define EDMA5_H_

/**
 * @file        src/lib/dma/public/hw/edma5.h
 * @addtogroup  edma5
 * @{
 */

#include <stdint.h>
#include <hw/dma.h>
#include <hw/edma.h>
#include <soc/nxp/imx9/common/imx_edma5.h>
#include <soc/nxp/imx9/common/imx_edma5_mp.h>
#include <soc/nxp/imx9/common/imx_edma5_channel.h>
#include <soc/nxp/imx9/common/imx_edma5_requests.h>
#include <soc/nxp/imx9/common/imx_edma5_irq_table.h>

#define MAX_DESCRIPTORS 256
/** eDMA driver description string */
#ifndef IMX_DMA5_DESCRIPTION_STR
    #define IMX_DMA5_DESCRIPTION_STR    "i.MX eDMAv5 Controller driver"
#endif



/** @} */ /* End of edma5 */

#endif /* EDMA5_H_ */
