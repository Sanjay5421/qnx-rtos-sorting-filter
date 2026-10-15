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

#ifndef __RPI4_BOARD_CONFIG_INCLUDE__
#define __RPI4_BOARD_CONFIG_INCLUDE__
#include "bcm2711_startup.h"

extern const bcm2711_config_t rpi_board_config[3];

const bcm2711_config_t rpi_board_config[] = {
    {   // UART
        .pinmux = (bcm2711_pinmux_t[])
        {
            { .pin_num = 14, .fsel = FUNC_A5, .pull = PULL_UP, .gpio_lvl = 0 }, /* TXD0 */
            { .pin_num = 15, .fsel = FUNC_A5, .pull = PULL_UP, .gpio_lvl = 0 }, /* RXD0 */
            { .pin_num =  4, .fsel = FUNC_A4, .pull = PULL_UP, .gpio_lvl = 0 }, /* TXD3 */
            { .pin_num =  5, .fsel = FUNC_A4, .pull = PULL_UP, .gpio_lvl = 0 }, /* RXD3 */
            { .is_last = 1 }
        },
    },
    {   // SPI
        .pinmux = (bcm2711_pinmux_t[])
        {
            { .pin_num =  7, .fsel = FUNC_A0, .pull = PULL_UP,   .gpio_lvl = 0 }, /* SPI0_CE1_N */
            { .pin_num =  8, .fsel = FUNC_A0, .pull = PULL_UP,   .gpio_lvl = 0 }, /* SPI0_CE0_N */
            { .pin_num =  9, .fsel = FUNC_A0, .pull = PULL_NONE, .gpio_lvl = 0 }, /* SPI0_MISO */
            { .pin_num = 10, .fsel = FUNC_A0, .pull = PULL_NONE, .gpio_lvl = 0 }, /* SPI0_MOSI */
            { .pin_num = 11, .fsel = FUNC_A0, .pull = PULL_NONE, .gpio_lvl = 0 }, /* SPI0_SCLK */
            { .is_last = 1 }
        },
    },

    {   // I2C
        .pinmux = (bcm2711_pinmux_t[])
        {
            { .pin_num = 0, .fsel = FUNC_A0, .pull = PULL_UP, .gpio_lvl = 0 }, /* SDA0 */
            { .pin_num = 1, .fsel = FUNC_A0, .pull = PULL_UP, .gpio_lvl = 0 }, /* SCL0 */
            { .pin_num = 2, .fsel = FUNC_A0, .pull = PULL_UP, .gpio_lvl = 0 }, /* SDA1 */
            { .pin_num = 3, .fsel = FUNC_A0, .pull = PULL_UP, .gpio_lvl = 0 }, /* SCL1 */
            { .is_last = 1 }
        },
    },
};

#endif /* __RPI4_BOARD_CONFIG_INCLUDE__ */
