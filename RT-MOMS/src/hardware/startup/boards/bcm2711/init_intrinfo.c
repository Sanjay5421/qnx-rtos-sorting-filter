/*
 * Copyright (c) 2020, 2022, BlackBerry Limited.
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
 *
 * This file may contain contributions from others, either as
 * contributors under the License or as licensors under other terms.
 * Please review this entire file for other proprietary rights or license
 * notices, as well as the QNX Development Suite License Guide at
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#include "init_intrinfo.h"
#include <startup.h>
#include <aarch64/bcm2711.h>

static const paddr_t bcm2711_msi_base = BCM2711_PCIE_BASE + 0x4000;

void init_intrinfo(void)
{
    const struct startup_intrinfo interrupts[] =
    {
        /* MSI interrupts (32) starting at 0x200 */
        {	.vector_base = 0x200,           // vector base (MSI 0x200 - 0x21F)
            .num_vectors = 32,              // 32 MSIs
            .cascade_vector = BCM2711_PCIE_MSI,
            .cpu_intr_base = 0,             // CPU vector base (MSI from 0 - 31)
            .cpu_intr_stride = 0,
            .flags = INTR_FLAG_MSI,
            .id = { .genflags = INTR_GENFLAG_NOGLITCH, .size = 0, .rtn = &interrupt_id_bcm2711_msi },
            .eoi = { .genflags = INTR_GENFLAG_LOAD_INTRMASK, .size = 0, .rtn = &interrupt_eoi_bcm2711_msi },
            .mask = &interrupt_mask_bcm2711_msi,
            .unmask = &interrupt_unmask_bcm2711_msi,
            .config = 0,
            .patch_data = (paddr_t*)&bcm2711_msi_base,
        },
    };

    /*
     * Initialise GIC
     */
#define	GICD_PADDR		0xff841000
#define	GICC_PADDR		0xff842000
    gic_v2_init(GICD_PADDR, GICC_PADDR);

    /*
     * Add interrupt callouts
     */
    add_interrupt_array(interrupts, sizeof interrupts);
}
