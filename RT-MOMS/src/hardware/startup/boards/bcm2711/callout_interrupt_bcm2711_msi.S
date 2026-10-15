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


/*
 * BCM2711 specific MSI interrupt callouts.
 */

#include <aarch64/callout.ah>

#define BCM2711_MSI_EOI			0x060		/* offset to eoi register */
#define BCM2711_MSI_STATUS		0x500		/* offset to status register */
#define BCM2711_MSI_INTR_CLR	0x508		/* offset to status clear register */
#define BCM2711_MSI_MASK_STATUS	0x50c		/* offset to mask status register */
#define BCM2711_MSI_MASK_SET	0x510		/* offset to mask set register */
#define BCM2711_MSI_MASK_CLR	0x514		/* offset to mask clear register */

#define BCM2711_MAX_MSI	32

/*
 * -----------------------------------------------------------------------
 * Patch interrupt callouts to access rw data.
 * The first call will also map the uart.
 *
 * Patcher routine takes the following arguments:
 *
 *	x0 - syspage paddr
 *	x1 - vaddr of callout
 *	x2 - offset from start of syspage to start of callout routine
 *	x3 - offset from start of syspage to rw storage
 *	x4 - patch data
 *	x5 - callout_rtn
 * -----------------------------------------------------------------------
 */
.text
.align 4

patch_intr:
	sub		sp, sp, #32
	stp		x19, x30, [sp]
	stp		x17, x18, [sp, #16]

	add		x19, x0, x2			/* x19 = address of callout routine */

	/*
	 * Calculate controller index. The first call establishes the base address
	 * of controller 0 and all other controllers are offset by 0x1000 from that
	 */
	adr		x6, Lpaddr_first	/* x6 = &Laddr_first */
	ldr		x1, [x6]			/* x1 = Lpaddr_first */
	ldr		x17, [x4]			/* x17 = patch data */
	cbnz	x1, 1f
	str		x17, [x6]			/* Lpaddr_first = patch data */
	mov		x1, x17				/* Lpaddr_first was unset so initialize x1 for calc */
1:

	/*
	 * Check if we have already mapped this controllers registers
	 */
	adr		x6, Lvaddr			/* x6 = &Lvaddr[0] */
	ldr		x0, [x6]			/* x0 = Lvaddr[controller #] */
	cbnz	x0, 0f

	/*
	 * Map the BCM2711 MSI registers
	 */
	mov		w0, #0x1000
	ldr		x1, [x4]
	bl		callout_io_map
	/* x0 = mmap()'d address of controller registers */
	adr		x6, Lvaddr
	ldr		x1, [x6]					/* x1 = Lvaddr[0] */
	str		x0, [x6]
0:
	/*
	 * Patch the callout routine with the address of the IER/IMR/ISR registers
	 * - patch argument in x0
	 * - location to patch specified as first parameter
	 * - remaining 2 parameters are scratch registers
	 */
	CALLOUT_PATCH	x19, w6, w7

	ldp		x19, x30, [sp]
	ldp		x17, x18, [sp, #16]
	add		sp, sp, #32
	ret

/*
 * -----------------------------------------------------------------------
 * Identify interrupt source
 *
 * On entry
 * - syspage pointer in x20
 *
 * On exit
 * - interrupt id returned in x19
 * -----------------------------------------------------------------------
 */

CALLOUT_START(interrupt_id_bcm2711_msi, 0, patch_intr)

	/* Get the MSI base address ( MSI */
	mov		x7, #0x0000
	movk	x7, #0x0000, lsl #16
	movk	x7, #0x0000, lsl #32
	movk	x7, #0x0000, lsl #48

	mov		x19, #-1

	/*
	 * Read Interrupt Mask and Status
	*/
	ldr		w0, [x7, #BCM2711_MSI_MASK_STATUS]
	ldr		w1, [x7, #BCM2711_MSI_STATUS]
	/* clear any masked bits from the status register */
	bics	w1, w1, w0
	/* if no more bits set we're done */
	cbz		w1, 0f

	/* calculate the interrupt number to be returned */
	rbit	w2, w1
	clz		w1, w2


	/* if ID not between 0 and 31, there's nothing to do */
	cmp		w1, #BCM2711_MAX_MSI
	bge		0f

	mov		w19, w1
	/* create the bit mask to clear the status register */
	mov		w2, #1
	lsl		w1, w2, w19
	str		w1, [x7, #BCM2711_MSI_MASK_SET]
	str		w1, [x7, #BCM2711_MSI_INTR_CLR]

0:

CALLOUT_END(interrupt_id_bcm2711_msi)

/*
 * -----------------------------------------------------------------------
 * Acknowledge specified interrupt
 *
 * On entry
 * - x19 - contains interrupt id
 * - x20 - contains syspage pointer (INTR_GENFLAG_LOAD_SYSPAGE was used)
 * - x22 - contains the interrupt mask count (INTR_GENFLAG_LOAD_INTRMASK was used)
 * -----------------------------------------------------------------------
 */
CALLOUT_START(interrupt_eoi_bcm2711_msi, 0, patch_intr)

	/* Get the MSI base address (patched) */
	mov		x7, #0x0000
	movk	x7, #0x0000, lsl #16
	movk	x7, #0x0000, lsl #32
	movk	x7, #0x0000, lsl #48

	/* only unmask if the mask count is 0 */
	cbnz	x22, 0f

	/* if ID not between 0 and 31, there's nothing to do */
	cmp		w19, #BCM2711_MAX_MSI
	bge		0f

	mov		w2, #1
	lsl		w1, w2, w19
	str		w1, [x7, #BCM2711_MSI_MASK_CLR]

	str		w2, [x7, #BCM2711_MSI_EOI]
0:

CALLOUT_END(interrupt_eoi_bcm2711_msi)

/*
 * -----------------------------------------------------------------------
 * Mask specified interrupt
 *
 * On entry:
 * - x0 - _syspage_ptr
 * - x1 - vector number (relative to controller)
 *
 * Returns:
 * - x0 - error status
 * -----------------------------------------------------------------------
 */
CALLOUT_START(interrupt_mask_bcm2711_msi, 0, patch_intr)

	/* Get the MSI base address (patched) */
	mov		x7, #0x0000
	movk	x7, #0x0000, lsl #16
	movk	x7, #0x0000, lsl #32
	movk	x7, #0x0000, lsl #48

	mov		x0, #-1

	/* if ID not between 0 and 31, error and there's nothing to do */
	cmp		w1, #BCM2711_MAX_MSI
	bge		0f

	mov		w2, #1
	lsl		w1, w2, w1
	str		w1, [x7, #BCM2711_MSI_MASK_SET]

	mov		x0, #0
0:
	ret

CALLOUT_END(interrupt_mask_bcm2711_msi)

/*
 * -----------------------------------------------------------------------
 * Unmask specified interrupt
 *
 * On entry:
 * - x0 - _syspage_ptr
 * - x1 - vector number (relative to controller)
 *
 * Returns:
 * - x0 - error status
 * -----------------------------------------------------------------------
 */
CALLOUT_START(interrupt_unmask_bcm2711_msi, 0, patch_intr)

	/* Get the MSI base address (patched) */
	mov		x7, #0x0000
	movk	x7, #0x0000, lsl #16
	movk	x7, #0x0000, lsl #32
	movk	x7, #0x0000, lsl #48

	mov		x0, #-1

	/* if ID not between 0 and 31, error and there's nothing to do */
	cmp		w1, #BCM2711_MAX_MSI
	bge		0f

	mov		w2, #1
	lsl		w1, w2, w1
	str		w1, [x7, #BCM2711_MSI_MASK_CLR]

	mov		x0, #0
0:
	ret

CALLOUT_END(interrupt_unmask_bcm2711_msi)

.data
.align 3
Lvaddr:	.word	0,0	/* 8 bytes of storage for a uintptr_t for the controller */
Lpaddr_first: .word		0,0	/* 8 bytes of storage for paddr_t of first patch_data */
