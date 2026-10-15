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
#include "proto.h"

void bcm2711_fini(void *hdl)
{
    bcm2711_dev_t  *dev = hdl;

    if (dev != NULL) {
        if (dev->iid != -1) {
            InterruptDetach(dev->iid);
        }
        munmap_device_io(dev->regbase, dev->reglen);
        (void)pthread_mutex_destroy(&dev->mutex);
        (void)pthread_cond_destroy(&dev->condvar);
    }
}
