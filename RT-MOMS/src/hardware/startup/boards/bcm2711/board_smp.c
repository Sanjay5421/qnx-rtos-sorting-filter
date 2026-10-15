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

unsigned
board_smp_num_cpu(void) {
    return 4;
}

void
board_smp_init(struct smp_entry *smp, const unsigned num_cpus)
{
    smp->send_ipi = (void *)&sendipi_gic_v2;

    /* Need to turn on the IPI interrupt for mailbox 0 on all cores */
    uint32_t cpu;
    for (cpu = 0; cpu < num_cpus; ++cpu) {
        const uint32_t mbic_addr = 0x40000050 + 0x4 * cpu;
        uint32_t mbic = in32(mbic_addr);
        mbic |= 0x1;
        out32(mbic_addr, mbic);
    }
}

static void
wake_aux_cpu(void)
{
    // same assenbly works in 32 and 64 bit
    __asm__ __volatile__(
        "dsb sy\n"
        "sev\n"
        : : : "memory"
    );
}

int
board_smp_start(unsigned cpu, void (*start)(void))
{
    typedef void (*start_t)(void);
    volatile start_t *cpu_start = (start_t *)(0xd8L + cpu * 8);
    *cpu_start = start;

    wake_aux_cpu();

    return 1;
}

unsigned
board_smp_adjust_num(unsigned cpu)
{
    return cpu;
}
