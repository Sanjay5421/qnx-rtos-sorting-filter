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

#include <startup.h>
#include <hw/hwinfo_private.h>
#include <drvr/hwinfo.h>
#include <hw/hwinfo_bcm2711.h>
#include <aarch64/bcm2711.h>
#include "mbox.h"
#include <libfdt.h>

/**
 * Add the DMA range for the SDMMC device found in the FDT to the hwinfo section
 * in the syspage.
 * The range is added only if the FDT was previously initialized, and the node
 * for the SDMMC device is found.
 * @param   mem     The offset of the asinfo parent node
 */
static void
add_sdmmc_dma(void)
{
    // Find the MMC bus node.
    int node_offset =
        fdt_node_offset_by_compatible(fdt, 0, "brcm,bcm2711-emmc2");
    if (node_offset < 0) {
        return;
    }

    // Get the parent node.
    node_offset = fdt_parent_offset(fdt, node_offset);
    if (node_offset < 0) {
        return;
    }

    // Find the dma-range property.
    int prop_len;
    struct fdt_property const * const prop =
        fdt_get_property(fdt, node_offset, "dma-ranges", &prop_len);

    if (prop == NULL) {
        return;
    }

    if (prop_len < (sizeof(fdt32_t) * 5)) {
        return;
    }

    // Extract the start address from the first two cells, and the length from
    // the fifth cell.
    fdt32_t const * const   data = (fdt32_t const *)prop->data;
    uint64_t const          start = ((uint64_t)fdt32_to_cpu(data[1]) |
                                     ((uint64_t)fdt32_to_cpu(data[0]) << 32));
    uint64_t const          size = (uint64_t)fdt32_to_cpu(data[4]);

    hwiattr_dma_t dma_attr = HWIATTR_DMA_T_INITIALIZER;
    HWIATTR_DMA_SET_LOCATION(&dma_attr, start, size, 0, AS_NULL_OFF);
    hwidev_add_dma("bcm2711-sdmmc", &dma_attr, HWI_NULL_OFF);
}

void init_hwinfo(void)
{
    const unsigned hwi_bus_internal = 0;
    /* Add ENET */
    {
        unsigned i, hwi_off;
        uint8_t mac[6];
        hwiattr_enet_t attr = HWIATTR_ENET_T_INITIALIZER;
        hwiattr_common_t common_attr = HWIATTR_COMMON_INITIALIZER;
        HWIATTR_ENET_SET_NUM_IRQ(&attr, 1);

        uint32_t irqs[] = {
            BCM2711_ENET_IRQA,
            BCM2711_ENET_IRQB
        };

        HWIATTR_SET_NUM_IRQ(&common_attr, NUM_ELTS(irqs));

        /* Create DWC0 */
        HWIATTR_ENET_SET_LOCATION(&attr, BCM2711_ENET_BASE,
                BCM2711_ENET_SIZE, 0, hwi_find_as(BCM2711_ENET_BASE, 1));
        hwi_off = hwidev_add_enet(BCM2711_HWI_GENET, &attr, hwi_bus_internal);
        hwitag_add_common(hwi_off, &attr);
        ASSERT(hwi_find_unit(hwi_off) == 0);

        /* Add IRQ number */
        for(i = 0; i < NUM_ELTS(irqs); i++) {
            hwitag_set_ivec(hwi_off, i, irqs[i]);
        }

        mbox_get_board_mac_address(mac);
        hwitag_add_nicaddr(hwi_off, mac, sizeof(mac));
    }

    /* add the WATCHDOG device */
    {
        unsigned hwi_off;
        hwiattr_timer_t attr = HWIATTR_TIMER_T_INITIALIZER;
        const struct hwi_inputclk clksrc_kick = {.clk = 500, .div = 1};
        HWIATTR_TIMER_SET_NUM_CLK(&attr, 1);
        HWIATTR_TIMER_SET_LOCATION(&attr, BCM2711_PM_BASE, BCM2711_PM_SIZE, 0, hwi_find_as(BCM2711_PM_BASE, 1));
        hwi_off = hwidev_add_timer("wdog", &attr,  HWI_NULL_OFF);
        ASSERT(hwi_off != HWI_NULL_OFF);
        hwitag_set_inputclk(hwi_off, 0, (struct hwi_inputclk *)&clksrc_kick);

        hwi_off = hwidev_add("wdt,options", 0, HWI_NULL_OFF);
        hwitag_add_regname(hwi_off, "write_width", 32);
        hwitag_add_regname(hwi_off, "enable_width", 32);
    }

    if (fdt != NULL) {
        add_sdmmc_dma();
    }

}
