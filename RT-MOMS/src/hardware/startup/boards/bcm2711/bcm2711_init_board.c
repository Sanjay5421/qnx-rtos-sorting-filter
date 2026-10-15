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
#include <stdint.h>
#include <sys/types.h>
#include "bcm2711_startup.h"

#define GPIO_MIN    0
#define GPIO_MAX    53

#define GPSET0      0x1CU
#define GPSET1      0x20U
#define GPCLR0      0x28U
#define GPCLR1      0x2CU
#define GPLEV0      0x34U
#define GPLEV1      0x38U

#define GPPUPPDN0   0xE4U    /* Pin pull-up/down for pins 15:0  */
#define GPPUPPDN1   0XE8U    /* Pin pull-up/down for pins 31:16 */
#define GPPUPPDN2   0xECU    /* Pin pull-up/down for pins 47:32 */
#define GPPUPPDN3   0xF0U    /* Pin pull-up/down for pins 57:48 */

static void bcm2711_set_gpio_fsel(const uint32_t gpio, const uint32_t fsel)
{
    const uint32_t reg = (gpio / 10) * 4;
    const uint32_t sel = gpio % 10;
    uint32_t val;

    if ((gpio < GPIO_MIN) || (gpio > GPIO_MAX)) {
        return;
    }

    val = in32(BCM2711_GPIO_BASE + reg);
    val &= ~(0x7 << (3 * sel));
    val |=  (fsel & 0x7) << (3 * sel);

    out32(BCM2711_GPIO_BASE + reg, val);
}

#ifdef DEBUG
static int bcm2711_get_gpio_fsel(const uint32_t gpio)
{
    /* GPIOFSEL0-GPIOFSEL5 with 10 sels per 32 bit reg,
     * 3 bits per sel (so bits 0:29 used)
     */
    const uint32_t reg = (gpio / 10) * 4;
    const uint32_t sel = gpio % 10;
    uint32_t val;

    if ((gpio < GPIO_MIN) || (gpio > GPIO_MAX)) {
        return -1;
    }

    val = in32(BCM2711_GPIO_BASE + reg);
    val = (val >> (3 * sel)) & 0x7;
    return val;
}

static int bcm2711_get_gpio_level(uint32_t gpio)
{
    uint32_t val;
    uint32_t reg;

    if ((gpio < GPIO_MIN) || (gpio > GPIO_MAX)) {
        return -1;
    }

    reg = (gpio < 32) ? GPLEV0 : GPLEV1;
    gpio = (gpio < 32) ? gpio : (gpio - 32);
    val = in32(BCM2711_GPIO_BASE + reg);
    val = (val >> gpio) & 0x1;
    return val;
}

static int bcm2711_get_gpio_pull(const uint32_t gpio)
{
    uint32_t val;
    const uint32_t reg = GPPUPPDN0 + (gpio >> 4) * 4;
    const uint32_t shift = (gpio & 0xf) << 1;

    if ((gpio < GPIO_MIN) || (gpio > GPIO_MAX)) {
        return -1;
    }
    val = in32(BCM2711_GPIO_BASE + reg);
    val = (val >> shift) & 0x3;
    return val;
}
#endif /* DEBUG */

static void bcm2711_set_gpio_level(uint32_t gpio, const uint32_t level)
{
    uint32_t reg;

    if ((gpio < GPIO_MIN) || (gpio > GPIO_MAX)) {
        return;
    }

    if (level) {
        reg = (gpio < 32) ? GPSET0 : GPSET1;
    }
    else {
        reg = (gpio < 32) ? GPCLR0 : GPCLR1;
    }

    gpio = (gpio < 32) ? gpio : (gpio - 32);
    out32(BCM2711_GPIO_BASE + reg, (1U << gpio));
    return;
}

static void bcm2711_gpio_set_pull(const uint32_t gpio, const uint32_t type)
{
    const uint32_t reg = GPPUPPDN0 + (gpio >> 4) * 4;
    const uint32_t shift = (gpio & 0xf) << 1;
    uint32_t val;

    if ((gpio < GPIO_MIN) || (gpio > GPIO_MAX)) {
        return;
    }

    if ((type < 0) || (type > 2)) {
        return;
    }

    val = in32(BCM2711_GPIO_BASE + reg);
    val &= ~(3 << shift);
    val |= (type << shift);
    out32(BCM2711_GPIO_BASE + reg, val);
    return;
}

static void module_pinmux(bcm2711_pinmux_t * pinmux)
{
    if (pinmux == NULL) {
        return;
    }
    while(!pinmux->is_last) {
        bcm2711_set_gpio_fsel(pinmux->pin_num, pinmux->fsel);
        bcm2711_gpio_set_pull(pinmux->pin_num, pinmux->pull);

        if (pinmux->fsel == FUNC_OP) {
            bcm2711_set_gpio_level(pinmux->pin_num, pinmux->gpio_lvl);
        }
        pinmux++;
    }
    return;
}

static void module_setup(const bcm2711_config_t * const config)
{
    if (!config) {
        return;
    }
    return module_pinmux(config->pinmux);
}

void init_board(void)
{
    const bcm2711_config_t *config;
    size_t num_devs = 0;
    size_t i;

    config = bcm2711_get_board_config(&num_devs);

    if (config) {
        for(i = 0; i < num_devs; i++) {
            module_setup(&config[i]);
        }
    }
    return;
}
