/*
 * $QNXLicenseC:
 * Copyright 2020, 2024, QNX Software Systems.
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

#include "externs.h"

int tto(TTYDEV * const dev, const int action, const int arg) {
    const TTYBUF * const bup = &dev->obuf;
    DEV_MINIUART *dev_mini = (DEV_MINIUART *) dev;
    const uintptr_t base = dev_mini->base;
    unsigned reg = 0;
    unsigned char c;

    switch (action) {
        case TTO_STTY:
            ser_stty(dev_mini);
            return 0;

        case TTO_CTRL:
            if (arg & _SERCTL_BRK_CHG) {
                reg = in32(base + AUX_MU_LCR_REG);
                if (arg &_SERCTL_BRK) {
                    reg |= LCR_BRK;
                }
                else {
                    reg &= ~LCR_BRK;
                }
                out32(base + AUX_MU_LCR_REG, reg);
            }
            return 0;

        case TTO_LINESTATUS:
            /* miniuart controller doesn't support DTR, DSR, DCD signal */
            return in32(base + AUX_MU_STAT_REG);

        case TTO_DATA: /* FALLTHROUGH */
        case TTO_EVENT:
            break;

        default:
            return 0;
    }

    /*
     * If the OSW_PAGED_OVERRIDE flag is set then allow
     * transmit of character even if output is suspended via
     * the OSW_PAGED flag. This flag implies that the next
     * character in the obuf is a software flow control
     * charater (STOP/START).
     * Note: tx_inject sets it up so that the contol
     *       character is at the start (tail) of the buffer.
     *
     */
    if ((dev_mini->tty.flags & (OHW_PAGED | OSW_PAGED))
            && !(dev_mini->tty.xflags & OSW_PAGED_OVERRIDE)) {
        return 0;
    }

    while (!(in32(base + AUX_MU_STAT_REG) & STAT_TXFULL) && (bup->cnt > 0)) {
        unsigned int error;
        /*
        * Get the next character to print from the output buffer
        */
        dev_lock(&dev_mini->tty);
        c = tto_getchar(&dev_mini->tty);
        dev_unlock(&dev_mini->tty);

        dev_mini->tty.un.s.tx_tmr = iochar_tick_cnt(150, &error);   /* Timeout 150ms */
        if (error > TICKSIZE_ERR_TOLERANCE) {
            MINIUART_SLOG_WARNING("%s: %s TX timer deviation = %u ms",
                                            __func__, dev_mini->tty.name, error);
        }

        out32(base + AUX_MU_IO_REG, c);
        /* Clear the OSW_PAGED_OVERRIDE flag as we only want
         * one character to be transmitted in this case.
         */
        if (dev_mini->tty.xflags & OSW_PAGED_OVERRIDE) {
            atomic_clr(&dev_mini->tty.xflags, OSW_PAGED_OVERRIDE);
            break;
        }
    }
    if (!(dev_mini->tty.flags & (OHW_PAGED|OSW_PAGED)) && (bup->cnt > 0)) {
        reg = in32(base + AUX_MU_IER_REG);
        out32(base + AUX_MU_IER_REG, reg | IER_TXEN);
    }

    /* Check client lists for notify conditions */
    return (tto_checkclients(&dev_mini->tty));
}

void ser_stty(DEV_MINIUART *dev) {
    const uintptr_t base = dev->base;
    unsigned cntl, divd, lcr, reg;
    uint32_t timeout;

    cntl = in32(base + AUX_MU_CNTL_REG);
    dev->cntl = cntl;
    divd = in32(base + AUX_MU_BAUD_REG);
    dev->div = divd;
    lcr = in32(base + AUX_MU_LCR_REG);
    dev->lcr = lcr;

    if (dev->tty.c_cflag & OHFLOW) {
        cntl &= ~CNTL_CTSEN;
        cntl |= CNTL_CTSEN;
    }

    if (dev->tty.c_cflag & IHFLOW) {
        cntl &= ~CNTL_RTSEN;
        cntl |= CNTL_RTSEN;
    }

    divd &= ~0xFFFF;
    divd |= (((dev->clk / (dev->tty.baud * 8)) - 1) & 0xFFFF);

    switch (dev->tty.c_cflag & CSIZE) {
        case CS8:
            lcr &= ~0x1;
            lcr |= LCR_DSIZE8;
            break;
        case CS7:
            lcr &= ~0x1;
            lcr |= LCR_DSIZE7;
            break;
        default:
            MINIUART_SLOG_ERROR("%s: error unsupported csize(%d)", __func__, dev->tty.c_cflag & CSIZE);
            break;
    }

    cntl |= (CNTL_TXEN | CNTL_RXEN);

    if (dev->init_done && (dev->cntl == cntl) && (dev->lcr == lcr) && (dev->div == divd)) {
       return;
    }

    dev->div = divd;
    dev->cntl = cntl;
    dev->lcr = lcr;

    /*
     * Wait for Tx FIFO and shift register empty if the UART is enabled
     */
    timeout = 100000;
    if ((in32(base + AUX_MU_CNTL_REG) & CNTL_TXEN)) {
        while (((in32(base + AUX_MU_STAT_REG) & STAT_TXDONE) == 0) && (timeout != 0)) {
            timeout--;
        }
    }

    if (!timeout) {
        MINIUART_SLOG_WARNING("Timeout waiting for Tx done");
    }

    /* Disable Receiver and Transmitter */
    reg = in32(base + AUX_MU_CNTL_REG);
    reg &= ~(CNTL_TXEN | CNTL_RXEN);
    out32(base + AUX_MU_CNTL_REG, reg);

    /* Disable the interrupt */
    reg = in32(base + AUX_MU_IER_REG);
    out32(base + AUX_MU_IER_REG, reg & ~(IER_TXEN | IER_RXEN));

    /* Set bauderate */
    out32(base + AUX_MU_BAUD_REG, dev->div);

    /* Set line control */
    out32(base + AUX_MU_LCR_REG, dev->lcr);

    /* Set auto CTS/RTS and activate Tx and Rx */
    out32(base + AUX_MU_CNTL_REG, dev->cntl);

    /* Clear FIFOs */
    out32(base + AUX_MU_IIR_REG, IIR_CLRFIFO);

    /* Enable Rx interrupt */
    reg = in32(base + AUX_MU_IER_REG);
    out32(base + AUX_MU_IER_REG, reg | IER_RXEN);

    MINIUART_SLOG_DEBUG("cntl=0x%08x, baud=0x%08x, lcr=0x%08x, iir=0x%08x",
                in32(base + AUX_MU_CNTL_REG),
                in32(base + AUX_MU_BAUD_REG),
                in32(base + AUX_MU_LCR_REG),
                in32(base + AUX_MU_IIR_REG));
}

int drain_check(TTYDEV * const dev, uintptr_t * const count) {
    const TTYBUF * const bup = &dev->obuf;
    DEV_MINIUART * const dev_mini = (DEV_MINIUART *) dev;

    // if the device has DRAINED, return 1
    if ((bup->cnt == 0) && (in32(dev_mini->base + AUX_MU_LSR_REG) & LSR_TXIDLE)) {
        return 1;
    }

    // if the device has not DRAINED, set a timer based on 50ms counts
    // wait for the time it takes for one character to be transmitted
    // out the shift register.  We do this dynamically since the
    // baud rate can change.
    if (count != NULL) {
        *count = (uintptr_t)((dev->baud == 0) ? 0 : ((IO_CHAR_DEFAULT_BITSIZE * MINIUART_FIFO_SIZE) / dev->baud + 1));
    }
    return 0;
}
