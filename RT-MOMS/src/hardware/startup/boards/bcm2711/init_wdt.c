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
#include <aarch64/bcm2711.h>
#include "bcm2711_startup.h"

#define PM_PASSWORD                 0x5a000000
#define PM_WDOG_TIME_SET            0x000fffff
#define PM_RSTC_WRCFG_CLR           0xffffffcf
#define PM_RSTS_HADWRH_SET          0x00000040
#define PM_RSTC_WRCFG_SET           0x00000030
#define PM_RSTC_WRCFG_FULL_RESET    0x00000020
#define PM_RSTC_STOP                0x00000102
#define PM_WDOG_MS_TO_TICKS         67UL
#define WDT_MAX_TICKS               0x000fffff
#define WDT_DEFAULT_TICKS           0x00010000

/* Enable watchdog timer */
void bcm2711_wdt_enable(const uint32_t timeout_value)
{
    const uintptr_t base = (uintptr_t) BCM2711_PM_BASE;
    uint32_t val;
    uint64_t ticks;

    /* Disable watch dog */
    out32(base + BCM2711_PM_RSTC, PM_PASSWORD | PM_RSTC_STOP);

    if (timeout_value  <= 1000) {
        val = WDT_DEFAULT_TICKS;
    } else {
        ticks = timeout_value * PM_WDOG_MS_TO_TICKS;
        if (ticks > WDT_MAX_TICKS) {
            ticks = WDT_MAX_TICKS;
        }
        val = (uint32_t) ticks;
    }
    /* Reload the timer value */
    out32(base + BCM2711_PM_WDOG, PM_PASSWORD | val);

    /* Enable the wdog timer */
    val = in32(base + BCM2711_PM_RSTC) & PM_RSTC_WRCFG_CLR;
    val |= (PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET);
    out32(base + BCM2711_PM_RSTC, val);


}
