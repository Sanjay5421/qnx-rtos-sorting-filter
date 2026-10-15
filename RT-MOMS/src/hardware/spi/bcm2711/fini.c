/*
 * Copyright (c) 2022, BlackBerry Limited.
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

#include "bcm2711spi.h"

/**
 *  @brief             SPI driver cleanup function.
 *  @param  hdl        SPI driver handler.
 *
 *  @return            Void.
 */
void bcm2711spi_fini(void *const hdl)
{
    bcm2711_spi_t *const spi = hdl;

    /* Clean up DMA */
    bcm2711_fini_dma(spi);

    /* Detach the interrupt */
    if (spi->iid != -1) {
        InterruptDetach(spi->iid);
    }

    /* If SPI controller physical addr has been mapped */
    if (spi->vbase != (uintptr_t)MAP_FAILED) {
        /* Disable and unmap SPI */
        out32((spi->vbase + BCM2711_SPI_CS), 0);
        munmap((void *)spi->vbase, BCM2711_SPI_REGLEN);
    }

    free(spi);
}
