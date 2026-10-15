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
#include "mbox.h"
#include "bcm2711_startup.h"

#define BCM2711_DRAM_BASE      0
#define BCM2711_DRAM_STUB_SIZE KILO(4)
#define BCM2711_DRAM_BASE_1    0x100000000

/* |----------------| <-0x400000000
 * |      SDRAM     |
 * |   (for ARM)    |
 * |----------------| <-0x100000000
 * | ARM local      |
 * | peripherals    |
 * |----------------| <-0x0ff800000
 * |Main peripherals|
 * |----------------| <-0x0fc000000
 * |  SDRAM(for ARM)|
 * |----------------| <-0x040000000
 * |  SDRAM(for VC) |
 * |----------------|
 * | SDRAM(for ARM) |
 * |----------------| <-0x000000000
 */
void bcm2711_init_raminfo(void) {
    const uint32_t vcram_size = mbox_get_vc_memory();
    const paddr_t vcram_start = GIG(1) - vcram_size;
    const uint32_t rev = mbox_get_board_revision();
    uint64_t ramsize = 0;

    /* Avoid touching the stub in startup */
    avoid_ram(BCM2711_DRAM_BASE, BCM2711_DRAM_STUB_SIZE);
    /* Avoid touching the VideoCore memory in startup */
    avoid_ram(vcram_start, vcram_size);

    ramsize = MEG(get_memory_size(rev));
    if (ramsize == 0) {
        ramsize = MEG(256);
        kprintf("Warning: Failed to detect RAM size. Defaulting to %u MB.\n",
            ramsize);
    }

    if (ramsize >= GIG(4)) {
        add_ram(BCM2711_DRAM_BASE, GIG(4) - MEG(64)); // periphs are mapped to 0xfc000000
        if (ramsize >= GIG(8)) {
            add_ram(BCM2711_DRAM_BASE_1, GIG(4));    // for rpi4 8GB target
        }
    } else {
        add_ram(BCM2711_DRAM_BASE, ramsize); // 256M, 512M, 1G, 2G
    }

    /* Reserve VideoCore memory */
    alloc_ram(vcram_start, vcram_size, 0);
    as_add_containing(vcram_start, vcram_start + vcram_size - 1, AS_ATTR_RAM, "vcram", "ram");

    /* Add a below1G tag for legacy peripherals */
    if (ramsize > GIG(1)) {
        as_add_containing(BCM2711_DRAM_BASE,  vcram_start - 1, AS_ATTR_RAM, "below1G", "ram");
    }

    if (debug_flag > 3) {
        kprintf("vcram start-size: %P-%P, memory size: %l\n", vcram_start, vcram_size, ramsize);
    }
}
