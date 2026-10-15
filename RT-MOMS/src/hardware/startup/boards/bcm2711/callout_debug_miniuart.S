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

#include <aarch64/callout.ah>

/*
 * The BCM2711 Mini UART.
 */

#define AUX_MU_IO_REG   0x40
#define AUX_MU_LSR_REG  0x54

/*
 * Patch callouts
 *
 * Patcher routine takes the following arguments:
 *	x0 - syspage paddr
 *	x1 - vaddr of callout
 *	x2 - offset from start of syspage to start of callout routine
 *	x3 - offset from start of syspage to rw storage
 *	x4 - patch data
 *	x5 - callout_rtn
 */
patch_debug:
	sub		sp, sp, #16
	stp		x19, x30, [sp]

	add		x19, x0, x2					// address of callout routine

	// Map registers
	mov		x0, #0x100
	ldr		x1, [x4]
	bl		callout_io_map

	// Patch with virtual address in x0
	CALLOUT_PATCH	x19, w6, w7

	ldp		x19, x30, [sp]
	add		sp, sp, #16
	ret

/*
 * -----------------------------------------------------------------------
 * void	display_char_miniuart(struct sypage_entry *, char)
 *
 * x0: syspage pointer
 * x1: character
 * -----------------------------------------------------------------------
 */
 CALLOUT_START(display_char_miniuart, 0, patch_debug)
	mov		x7, #0xabcd				// register base
	movk	x7, #0xabcd, lsl #16
	movk	x7, #0xabcd, lsl #32
	movk	x7, #0xabcd, lsl #48

	// Wait for idle
0:
	ldr		w2, [x7, #AUX_MU_LSR_REG]
	tbz		w2, #5, 0b				// bit 5, or 0x20

	// Send the character
	and		w2, w1, #0xff
	str		w2, [x7, #AUX_MU_IO_REG]

	// Make sure it's transmitted
1:
	ldr		w2, [x7, #AUX_MU_LSR_REG]
	tbz		w2, #6, 1b				// bit 6, or 0x40

	ret
CALLOUT_END(display_char_miniuart)

CALLOUT_START(poll_key_miniuart, 0, patch_debug)
	mov		x7, #0xabcd				// UART base address (patched)
	movk	x7, #0xabcd, lsl #16
	movk	x7, #0xabcd, lsl #32
	movk	x7, #0xabcd, lsl #48

	/*
	 * If RXRDY, read character, otherwise return -1
	 */
	ldrb	w1, [x7,#AUX_MU_LSR_REG]
	mov		x0, #-1					// assume no character
	tst		w1, #1					// bit 0, or 0x01
	beq		1f
	ldrb	w0, [x7,#AUX_MU_IO_REG]
1:
	ret
CALLOUT_END(poll_key_miniuart)

CALLOUT_START(break_detect_miniuart, 0, 0)
	mov		x0, #0
	ret
CALLOUT_END(break_detect_miniuart)
