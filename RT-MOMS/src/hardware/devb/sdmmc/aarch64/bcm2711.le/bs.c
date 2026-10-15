/*
 * $QNXLicenseC:
 * Copyright 2020, 2024 QNX Software Systems.
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

/* Module Description:  board specific interface */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <hw/inout.h>
#include <sys/mman.h>
#include <drvr/hwinfo.h>
#include <sdhci.h>

/*
 * Raspberry PI4 regulator "regulator-sd-io-1v8" is controlled by gpio "VDD_SD_IO_SEL" pin 4 in expgpio.
 * and gpio pins in expgpio group is implemented using mbox property tag: 0x00030041
 */
#define EXP_GPIO_BASE                 128
#define EXP_GPIO_VDD_SD_IO_SEL        4

#define EXP_GPIO_VDD_SD_IO_SEL_3_3V   0
#define EXP_GPIO_VDD_SD_IO_SEL_1_8V   1

#define MBOX_TAG_NULL                 0x00000000
#define MBOX_TAG_GET_GPIO_STATE       0x00030041

#define MBOX_REGS                     0xfe00b880

#define MBOX_PROCESS_REQUEST          0
#define MBOX_TAG_REQUEST              (0U << 31)
#define MBOX_TAG_RESPONSE             (1U << 31)
#define MBOX_STATUS_FULL              0x80000000
#define MBOX_STATUS_EMPTY             0x40000000
#define MBOX_SEND_CHANNEL             8
#define MBOX_SEND_CHANNEL_MASK        0xf

struct mbox_msg_header {
    uint32_t size;
    uint32_t code;
};

struct mbox_msg_tag {
    uint32_t tag;
    uint32_t size;
    uint32_t code;
};

struct gpio_state {
    uint32_t gpio;
    uint32_t state;
};

struct mbox_msg_gpio_state {
    struct mbox_msg_header header;
    struct mbox_msg_tag tag;
    struct gpio_state  body;
    uint32_t end;
};

/* Structure describing board specific configuration */
typedef struct _bcm2711_ext {
    int          (*signal_voltage)(sdio_hc_t *, uint32_t);
    int          (*sdhci_tune)(sdio_hc_t *hc, uint32_t op);
} bcm2711_ext_t;

static void bcm2711_get_sdmmc_dma_range(sdio_hc_t * const hc)
{
    sdio_hc_cfg_t * const cfg = &hc->cfg;
    unsigned const hwioff = hwi_find_device("bcm2711-sdmmc", 0);
    if (hwioff != HWI_NULL_OFF) {
        const hwi_tag * const tag = hwi_tag_find(hwioff, HWI_TAG_NAME_location, NULL);
        if (tag != NULL) {
            cfg->bmstr_xlat = tag->location.base;
            sdio_slogf(_SLOGC_SDIODI, _SLOG_INFO, hc->cfg.verbosity, 0,
                       "%s: found SDMMC DMA range in syspage %lx", __func__, cfg->bmstr_xlat);
        } else {
            sdio_slogf(_SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0,
                       "%s: failed to find bcm2711-sdmmc location tag", __func__);
        }
    } else {
        sdio_slogf(_SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0,
                   "%s: failed to find bcm2711-sdmmc tag", __func__);
    }
}

static int bcm2711_bs_args(sdio_hc_t *hc, char *options)
{
    char    *value;
    int        opt;
    sdio_hc_cfg_t    *cfg = &hc->cfg;

    static char     *opts[] = {
#define BUSMASTER_BASE        0
    "bmstr_base",
    NULL};

    while (options && (*options != '\0')) {
        opt = sdio_hc_getsubopt( &options, opts, &value);
        if (opt == -1) {
            sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0, "%s: invalid BS options %s", __func__, options);
            return EINVAL;
        }

        switch (opt) {
            case BUSMASTER_BASE:
                if (value == NULL) {
                    sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0, "%s(%d): invalid BS options %s",
                        __func__, __LINE__,  options);
                    return EINVAL;
                }
                cfg->bmstr_xlat = strtoull(value, NULL, 0);
                break;
            default:
                break;
        }
    }

    return EOK;
}

static int mbox_send_msg_gpio_state(sdio_hc_t * const hc, struct gpio_state * const gpiostate, const int set)
{
    struct mbox_msg_gpio_state *gpio_state_msg;
    static void *out;
    static uint32_t physaddr;
    static volatile struct {
        uint32_t rdwr, _x04,   _x08,   _x0c;
        uint32_t poll, send, status, config;

    } *mbox; // [0]:fromvc [1]:tovc
    int fd;
    off_t offset;
    fd = posix_typed_mem_open("/sysram&below1G", O_RDWR, POSIX_TYPED_MEM_ALLOCATE_CONTIG);
    if (fd < 0) {
        sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0, "%s: Failed to open typed memory, %s",
                                __func__, strerror( errno ));
        return -1;
    }
    out = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE | PROT_NOCACHE, MAP_SHARED, fd, 0);
    if (out == MAP_FAILED) {
        sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0, "%s: Failed to allocate buffer!", __func__);
        close(fd);
        return -1;
    }

    if (mem_offset(out, NOFD, 1, &offset, NULL) != EOK) {
        munmap(out, 0x1000);
        close(fd);
        sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0, "%s: Failed to obtain physical address", __func__);
        return -1;
    }

    physaddr = (uint32_t)offset;

    gpio_state_msg = out;
    gpio_state_msg->header.size = sizeof(struct mbox_msg_gpio_state);
    gpio_state_msg->header.code = MBOX_PROCESS_REQUEST;
    gpio_state_msg->tag.tag = MBOX_TAG_GET_GPIO_STATE;
    gpio_state_msg->tag.size = sizeof(struct gpio_state);
    gpio_state_msg->tag.code = MBOX_TAG_REQUEST;
    if (set) {
        gpio_state_msg->tag.tag |= 0x8000;
        gpio_state_msg->body.state = gpiostate->state;
    }
    gpio_state_msg->body.gpio =  EXP_GPIO_BASE + gpiostate->gpio;
    gpio_state_msg->end = MBOX_TAG_NULL;

    int wait_cnt = 0;
    mbox = mmap_device_memory(NULL, 0x80, PROT_NOCACHE|PROT_READ|PROT_WRITE, 0, MBOX_REGS);
    if (mbox == MAP_FAILED) {
        sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0, "%s: Failed to map mbox registers", __func__);
        munmap(out, 0x1000);
        close(fd);
        return -1;
    }

    // check for space on tovc channel to send with timeout
    for (;;) {
        if (!(mbox[1].status & MBOX_STATUS_FULL)) {
            break;
        }
        usleep(100);
        if (++wait_cnt > 5000) {
            sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0, "%s: Wait for response timeout", __func__);
            munmap_device_memory( (void *)mbox, 0x80 );
            munmap(out, 0x1000);
            close(fd);
            return -1;
        }
    }

    // send
    mbox[1].rdwr = (physaddr & ~MBOX_SEND_CHANNEL_MASK) | MBOX_SEND_CHANNEL;

    for (;;) {
        wait_cnt = 0;
        // wait for response on fromvc channel to receive with timeout
        for (;;) {
            if (!(mbox[0].status & MBOX_STATUS_EMPTY)) {
                break;
            }
            usleep(100);
            if (++wait_cnt > 5000) {
                sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0, "%s: Wait for response timeout after sending mbox msg", __func__);
                munmap_device_memory( (void *)mbox, 0x80 );
                munmap(out, 0x1000);
                close(fd);
                return -2;
            }
        }

        // is it the channel we're waiting for?
        const uint32_t rd = mbox[0].rdwr;
        if ((rd & MBOX_SEND_CHANNEL_MASK) == MBOX_SEND_CHANNEL) {
            const uint32_t tagcode = gpio_state_msg->tag.code;
            const uint32_t len = tagcode & ~MBOX_TAG_RESPONSE;
            if (set == 0) {
                if (len != sizeof(struct gpio_state)) {
                    sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0, "%s: invalid mbox tag code=0x%x", __func__, tagcode);
                    return -4;
                }
                gpiostate->gpio = gpio_state_msg->body.gpio;
                gpiostate->state = gpio_state_msg->body.state;
            }
            munmap_device_memory( (void *)mbox, 0x80 );
            munmap(out, 0x1000);
            close(fd);
            return sizeof(struct gpio_state);
        }
    }
    /*NOTREACHED*/

    return -3;
}

static int get_vdd_sd_io_state(sdio_hc_t * const hc)
{
    struct gpio_state gpiostate = { .gpio = EXP_GPIO_VDD_SD_IO_SEL, .state = 0 };
    int ret;

    ret = mbox_send_msg_gpio_state(hc, &gpiostate, 0);
    if (ret == sizeof(struct gpio_state)) {
        sdio_slogf( _SLOGC_SDIODI, _SLOG_INFO, hc->cfg.verbosity, 1, "%s: get VDD_SD_IO_SEL gpio state=%d", __func__, gpiostate.state);
        return gpiostate.state;
    }
    else {
        sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0, "%s: get VDD_SD_IO_SEL gpio state failed, ret=%d", __func__, ret);
        return -1;
    }
}

static int set_vdd_sd_io_state(sdio_hc_t * const hc, const int state)
{
    struct gpio_state gpiostate = { .gpio = EXP_GPIO_VDD_SD_IO_SEL, .state = state };
    int ret;
    sdio_slogf( _SLOGC_SDIODI, _SLOG_INFO, hc->cfg.verbosity, 1, "%s: set VDD_SD_IO_SEL gpio state=%d", __func__, gpiostate.state);

    ret = mbox_send_msg_gpio_state(hc, &gpiostate, 1);
    if (ret == sizeof(struct gpio_state)) {
        sdio_slogf( _SLOGC_SDIODI, _SLOG_INFO, hc->cfg.verbosity, 0, "%s: set VDD_SD_IO_SEL gpio return state=%d", __func__, gpiostate.state);
        return ret;
    }
    else {
        sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0, "%s: set VDD_SD_IO_SEL gpio state: %d failed, ret=%d", __func__, state, ret);
        return -1;
    }
}

/*
 * This function is a variant of sdhci_signal_voltage, it calls rpi4 specific gpio/mbox APIs
 * to switch between 3.3v and 1.8v operation
 */
static int bcm2711_signal_voltage(sdio_hc_t * hc, uint32_t signal_voltage)
{
    const bcm2711_ext_t * const ext = hc->bs_hdl;

    const int gpio_sd_iovdd_sel_state = get_vdd_sd_io_state(hc);

    if (gpio_sd_iovdd_sel_state != -1) {
        if ((signal_voltage == SIGNAL_VOLTAGE_3_3) && (gpio_sd_iovdd_sel_state != EXP_GPIO_VDD_SD_IO_SEL_3_3V)) {
            set_vdd_sd_io_state(hc, EXP_GPIO_VDD_SD_IO_SEL_3_3V);
            delay( 5 );
            // force ext->signal_voltage to switch to SIGNAL_VOLTAGE_3_3
            hc->signal_voltage = 0;
        }
        if ((signal_voltage == SIGNAL_VOLTAGE_1_8) && (gpio_sd_iovdd_sel_state != EXP_GPIO_VDD_SD_IO_SEL_1_8V)) {
            set_vdd_sd_io_state(hc, EXP_GPIO_VDD_SD_IO_SEL_1_8V);
            delay( 5 );
            // force ext->signal_voltage to switch to SIGNAL_VOLTAGE_1_8
            hc->signal_voltage = 0;
        }
        else {
            // unknown gpio "SD_IOVDD_SEL" state, do nothing
        }
    }
    else {
        if (signal_voltage == SIGNAL_VOLTAGE_1_8) {
            sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0,
                    "%s: failed to get VDD_SD_IO_SEL gpio state, unable to support 1.8V", __func__);
            signal_voltage = SIGNAL_VOLTAGE_3_3;
            hc->caps &= ~HC_CAP_SV_1_8V;
        }
    }
    return (ext->signal_voltage(hc, signal_voltage));
}

static int bcm2711_sdhci_tune(sdio_hc_t *hc, const uint32_t op)
{
    const bcm2711_ext_t * const ext = hc->bs_hdl;
    int ret;

    ret = ext->sdhci_tune(hc, op);
    if (ret != EOK ) {
        hc->caps &= ~HC_CAP_SV_1_8V;
        /* tunning is done in UHS mode, in case of failure, stop supporting 1.8V so that no more tunning */
        sdio_slogf( _SLOGC_SDIODI, _SLOG_ERROR, hc->cfg.verbosity, 0, "%s: tuning failed (%d)", __FUNCTION__, ret);
    }
    return ret;
}

static int bcm2711_bs_init(sdio_hc_t *hc)
{
    static bcm2711_ext_t ext;
    int    status = EOK;
    sdio_hc_cfg_t * const cfg = &hc->cfg;

    hc->bs_hdl = (void *)&ext;

    bcm2711_get_sdmmc_dma_range(hc);

    if (bcm2711_bs_args(hc, cfg->options)) {
        return EINVAL;
    }

    if(hc->caps & HC_CAP_SLOT_TYPE_EMBEDDED) {
        hc->caps |= HC_CAP_HS200;
    }

    status = sdhci_init(hc);
    if( status != EOK ) {
        return status;
    }

    /* board specific handling of setting voltage and tuning */
    ext.signal_voltage = hc->entry.signal_voltage;
    hc->entry.signal_voltage = bcm2711_signal_voltage;
    ext.sdhci_tune = hc->entry.tune;
    hc->entry.tune = bcm2711_sdhci_tune;

    if(!(hc->caps & HC_CAP_SLOT_TYPE_EMBEDDED)) {
        /* Overwrite some of the capabilities that are set by sdhci_init() */
        hc->caps &= ~HC_CAP_CD_INTR;   /* HC doesn't support card detection interrupt */
        // hc->caps &= ~HC_CAP_SV_1_8V;
    }
    return status;
}

static sdio_product_t sdio_fs_products[] = {
    { .did = SDIO_DEVICE_ID_WILDCARD, .class = 0, .aflags = 0, .name = "bcm2711", .init = bcm2711_bs_init },
};

sdio_vendor_t sdio_vendors[] = {
    { .vid = SDIO_VENDOR_ID_WILDCARD, .name = "Broadcom", .chipsets = sdio_fs_products },
    { .vid = 0, .name = NULL, .chipsets = NULL }
};
