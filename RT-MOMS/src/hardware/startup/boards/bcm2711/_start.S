/*
 * Copyright (c) 2020, 2022 BlackBerry Limited.
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
 * _start.S
 *
 * This code runs as the first instructions after control has transferred
 * from any bootup monitor.  This module defines a minimal bootup stack,
 * and sets SP to this stack, and then starts running C code in _main().
 *
 * For the ARMv8 Fast Model, all processors execute from cold reset in EL3.
 * This code sets up minimal EL2/EL3 state for all cpus and switches to EL1
 * to continue execution:
 * - cpu0 jumps to cstart to perform the startup initialisation
 * - other cpus spin until they are woken up by board_smp_start
 */

	.text
	.align 2

	.global _start
_start:
	/* NOTE:
		Do NOT modify registers X0-X3 before returning on the bootstrap
		processor.
		These registers may contain information provided by the IPL and
		cstart will save them in the boot_regs variable for later perusal
		by other portions of startup.
	*/

	b		cstart
