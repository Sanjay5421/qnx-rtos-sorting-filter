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

static int tx_interrupt(DEV_MINIUART *dev)
{
    const uintptr_t base = dev->base;
    int status = 0;
    unsigned ier;

    ier = in32(base + AUX_MU_IER_REG);
    if (ier & IER_TXEN) {
        out32(base + AUX_MU_IER_REG, ier & ~IER_TXEN);

        dev->tty.un.s.tx_tmr = 0; /* clear Timeout */

        /* Send event to io-char, tto() will be processed at thread time */
        atomic_set(&dev->tty.flags, EVENT_TTO);
        status |= 1;
    }

    return status;
}

static int rx_interrupt(DEV_MINIUART *dev) {
    const uintptr_t base = dev->base;
    unsigned key, rxdata;
    int byte_count = 0, status = 0;

    /* limit loop iterations by FIFO size to prevent from running too long */
    while ((in32(base + AUX_MU_STAT_REG) & STAT_SYMBLAVBL) && (byte_count < MINIUART_FIFO_SIZE)) {
        /*
         * Read next character from FIFO
         */
        rxdata = in32(base + AUX_MU_IO_REG);
        key = rxdata & 0xFF;
        status |= tti(&dev->tty, key);
        byte_count++;
    }
    return status;
}

static int do_interrupt(DEV_MINIUART * const dev, const int id)
{
    const uintptr_t base = dev->base;
    int status = 0;
    unsigned iir;

    iir = in32(base + AUX_MU_IIR_REG);

    /* intrrupt pending bit is clear whenever an interrupt is pedning */
    if (!(iir & IIR_INTPENDING)) {

        if (iir & IIR_INTIDRX) {
            status |= rx_interrupt(dev);
        }

        if (iir & IIR_INTIDTX) {
            status |= tx_interrupt(dev);
        }
    }
    return status;
}

static int
interrupt_event_handler (message_context_t * const msgctp, const int code, const unsigned flags, void * const handle)
{
    int status;
    DEV_MINIUART *dev = handle;

    status = do_interrupt (dev, dev->iid);

    if (status) {
        iochar_send_event (&dev->tty);
    }

    InterruptUnmask ((int)dev->intr, dev->iid);
    return (EOK);
}

void ser_attach_intr(DEV_MINIUART *dev)
{
    struct sigevent event;
    // Associate a pulse which will call the event handler.
    event.sigev_code = (short) pulse_attach (ttyctrl.dpp, MSG_FLAG_ALLOC_PULSE, 0, &interrupt_event_handler, dev);
    if (event.sigev_code == -1) {
        MINIUART_SLOG_ERROR("Unable to attach event pulse: %s", strerror(errno));
        return;
    }

    /* Init the pulse for interrupt event */
    event.sigev_notify = SIGEV_PULSE;
    event.sigev_coid = ttyctrl.coid;
    event.sigev_priority = DEFAULT_PRIORITY + 1;
    event.sigev_value.sival_int = 0;

    dev->iid = InterruptAttachEvent ((int)dev->intr, &event, _NTO_INTR_FLAGS_TRK_MSK);
    if (dev->iid == -1) {
        MINIUART_SLOG_ERROR("Unable to attach InterruptEvent: %s", strerror(errno));
    }
}

void ser_detach_intr(DEV_MINIUART *dev)
{
    const uintptr_t base = dev->base;
    unsigned ier;

    // Disable the interrupt
    ier = in32(base + AUX_MU_IER_REG);
    out32(base + AUX_MU_IER_REG, ier & ~(IER_TXEN | IER_RXEN));

    InterruptDetach(dev->iid);
    dev->intr = _NTO_INTR_SPARE;
}
