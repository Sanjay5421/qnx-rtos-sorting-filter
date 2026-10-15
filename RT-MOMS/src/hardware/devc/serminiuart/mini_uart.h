/*
 * $QNXLicenseC:
 * Copyright 2020, QNX Software Systems.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You
 * may not reproduce, modify or distribute this software except in
 * compliance with the License. You may obtain a copy of the License
 * at: http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 *
 * This file may contain contributions from others, either as
 * contributors under the License or as licensors under other terms.
 * Please review this entire file for other proprietary rights or license
 * notices, as well as the QNX Development Suite License Guide at
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#ifndef	__MINI_UART_INC__
#define	__MINI_UART_INC__

#define MINIUART_FIFO_SIZE  0x8

#define	MINIUART_SIZE       0x100

#define AUX_ENABLES         0x04    // Auxiliary enables

#define AUX_MU_IO_REG       0x40    // Mini Uart I/O Data
#define AUX_MU_IER_REG      0x44    // Mini Uart Interrupt Enable
    #define IER_TXEN        (1 << 1)
    #define IER_RXEN        (1 << 0)
#define AUX_MU_IIR_REG      0x48    // Mini Uart Interrupt Identify
    #define IIR_CLRFIFO     ((1 << 1) | (1 << 2 ))
    #define IIR_INTIDRX     (0x4)
    #define IIR_INTIDTX     (0x2)
    #define IIR_INTPENDING  (1 << 0)
#define AUX_MU_LCR_REG      0x4c    // Mini Uart Line Control
    #define LCR_BRK         (1 << 6)
    #define LCR_DSIZE8      (1 << 0)
    #define LCR_DSIZE7      (0 << 0)
#define AUX_MU_MCR_REG      0x50    // Mini Uart Modem Control
#define AUX_MU_LSR_REG      0x54    // Mini Uart Line Status
    #define LSR_TXIDLE      (1 << 6)
#define AUX_MU_MSR_REG      0x58    // Mini Uart Modem Status
    #define MSR_CTSSTAT     (1 << 4)
#define AUX_MU_SCRATCH      0x5c    // Mini Uart Scratch
#define AUX_MU_CNTL_REG     0x60    // Mini Uart Extra Control
    #define CNTL_RXEN       (1 << 0)
    #define CNTL_TXEN       (1 << 1)
    #define CNTL_RTSEN      (1 << 2)
    #define CNTL_CTSEN      (1 << 3)
#define AUX_MU_STAT_REG     0x64    // Mini Uart Extra Status
    #define STAT_SYMBLAVBL  (1 << 0)
    #define STAT_SPACEAVBL  (1 << 1)
    #define STAT_TXFULL     (1 << 5)
    #define STAT_RTSSTAT    (1 << 6)
    #define STAT_TXDONE     (1 << 9)
#define AUX_MU_BAUD_REG     0x68    // Mini Uart Baudrate

#endif  /* __MINI_UART_INC__ */
