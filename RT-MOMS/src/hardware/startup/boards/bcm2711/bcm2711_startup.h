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

#ifndef __BCM2711_BOARD_H__
#define __BCM2711_BOARD_H__

/* Options flags */
#define BCM2711_WDT_ENABLE      (1 << 0)

#define BCM2711_WDT_USE_DEFAULT (15000U) /* 15000ms */

#define DRIVE_LOW   0
#define DRIVE_HIGH  1

#define PULL_NONE   0
#define PULL_UP     1
#define PULL_DOWN   2

#define FUNC_IP     0
#define FUNC_OP     1
#define FUNC_A0     4
#define FUNC_A1     5
#define FUNC_A2     6
#define FUNC_A3     7
#define FUNC_A4     3
#define FUNC_A5     2

typedef struct _bcm2711_pinmux {
    uint8_t pin_num;
    uint8_t fsel;
    uint8_t pull;
    uint8_t gpio_lvl;
    uint8_t is_last;
} bcm2711_pinmux_t;

typedef struct _bcm2711_config {
    bcm2711_pinmux_t *pinmux;
} bcm2711_config_t;

const bcm2711_config_t *bcm2711_get_board_config(size_t * const num_devs);
const unsigned get_memory_size(uint32_t board_rev);
const char *get_soc_name(uint32_t board_rev);
const char *get_mfr_name(uint32_t board_rev);
const char * get_board_name(uint32_t board_rev);
const char *get_board_serial(void);

void bcm2711_init_raminfo(void);
void bcm2711_wdt_enable(const uint32_t timeout_value);
void init_board(void);

void init_miniuart(unsigned channel, const char *init, const char *defaults);
void put_miniuart(int character);

extern struct callout_rtn display_char_miniuart;
extern struct callout_rtn poll_key_miniuart;
extern struct callout_rtn break_detect_miniuart;
extern struct callout_rtn reboot_bcm2711;

#endif /* __BCM2711_BOARD_H__ */
