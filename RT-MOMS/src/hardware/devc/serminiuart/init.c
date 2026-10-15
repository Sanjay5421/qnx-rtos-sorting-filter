/*
 * $QNXLicenseC:
 * Copyright 2020, 2023-2024, BlackBerry Limited.
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
#include <sys/mman.h>
#include <sys/trace.h>

void
create_device(TTYINIT * const dip)
{
    DEV_MINIUART	*dev;

    /*
     * Get a device entry and the input/output buffers for it.
     */
    dev = calloc(1, sizeof(*dev));

    if (dev == NULL) {
        MINIUART_SLOG_ERROR("Unable to allocate device entry: %s", strerror(errno));
        goto fail;
    }

    /*
     * Get buffers.
     */
    dev->tty.ibuf.size = dip->isize;
    dev->tty.ibuf.buff = malloc(dev->tty.ibuf.size);
    if (dev->tty.ibuf.buff == NULL) {
        MINIUART_SLOG_ERROR("Failed to allocate input buffer");
        goto fail1;
    }
    dev->tty.ibuf.head = dev->tty.ibuf.buff;
    dev->tty.ibuf.tail = dev->tty.ibuf.buff;

    dev->tty.obuf.size = dip->osize;
    dev->tty.obuf.buff = malloc(dev->tty.obuf.size);
    if (dev->tty.obuf.buff == NULL) {
        MINIUART_SLOG_ERROR("Failed to allocate output buffer");
        goto fail2;
    }
    dev->tty.obuf.head = dev->tty.obuf.buff;
    dev->tty.obuf.tail = dev->tty.obuf.buff;

    dev->tty.cbuf.size = dip->csize;
    dev->tty.cbuf.buff = malloc(dev->tty.cbuf.size);
    if (dev->tty.cbuf.buff == NULL) {
        MINIUART_SLOG_ERROR("Failed to allocate canonical buffer");
        goto fail3;
    }
    dev->tty.cbuf.head = dev->tty.cbuf.buff;
    dev->tty.cbuf.tail = dev->tty.cbuf.buff;

    dev->tty.highwater = dev->tty.ibuf.size - ((dev->tty.ibuf.size < 128) ? (dev->tty.ibuf.size/4) : 100);

    strlcpy(dev->tty.name, dip->name, sizeof(dev->tty.name));

    dev->tty.baud = dip->baud;
    dev->intr = dip->intr;
    dev->clk  = dip->clk;
    dev->div  = dip->div;

    dev->tty.flags = EDIT_INSERT | LOSES_TX_INTR;
    dev->tty.c_cflag = dip->c_cflag;
    dev->tty.c_iflag = dip->c_iflag;
    dev->tty.c_lflag = dip->c_lflag;
    dev->tty.c_oflag = dip->c_oflag;
    dev->tty.verbose = dip->verbose;
    /*
     * Map device registers
     */
    dev->base = mmap_device_io (MINIUART_SIZE, dip->port);
    if (dev->base == (uintptr_t)MAP_FAILED) {
        MINIUART_SLOG_ERROR("mmap_device_io failed");
        goto fail4;
    }

    /*
     * Initialize termios cc codes to an ANSI terminal.
     */
    ttc(TTC_INIT_CC, &dev->tty, 0);

    /*
     * Initialize the device's name.
     * Assume that the basename is set in device name.  This will attach
     * to the path assigned by the unit number/minor number combination
     */
    ttc(TTC_INIT_TTYNAME, &dev->tty, (int)(SET_NAME_NUMBER(dip->unit) | NUMBER_DEV_FROM_USER));

    /*
     * Only setup IRQ handler for non-pcmcia devices.
     * Pcmcia devices will have this done later when card is inserted.
     */
    if ((dip->port != 0) && (dip->intr != _NTO_INTR_SPARE)) {
        ser_attach_intr(dev);
        ser_stty(dev);
    }

    /*
     * Attach the resource manager
     */
    ttc(TTC_INIT_ATTACH, &dev->tty, 0);
    atomic_set(&dev->init_done, 0x1);
    return ;

fail4:
    free(dev->tty.cbuf.buff);
fail3:
    free(dev->tty.obuf.buff);
fail2:
    free(dev->tty.ibuf.buff);
fail1:
    free (dev);
fail:
    exit(1);
}
