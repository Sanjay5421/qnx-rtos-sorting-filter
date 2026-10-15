/*
 * $QNXLicenseC:
 * Copyright 2020,2025 QNX Software Systems.
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

#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

#define MBOX_REG_BASE                   (0xfe00b880UL)
#define MBOX_REG_SIZE                   (0x40UL)

#define MBOX_TAG_GET_FIRMWAREREV        (0x00000001U)
#define MBOX_TAG_GET_FIRMWAREVAR        (0x00000002U)
#define MBOX_TAG_GET_FIRMWAREHASH       (0x00000003U)
#define MBOX_TAG_GET_BOARDMODEL         (0x00010001U)
#define MBOX_TAG_GET_BOARDREVISION      (0x00010002U)
#define MBOX_TAG_GET_MACADDRESS         (0x00010003U)
#define MBOX_TAG_GET_BOARDSERIAL        (0x00010004U)
#define MBOX_TAG_GET_ARMMEMORY          (0x00010005U)
#define MBOX_TAG_GET_VCMEMORY           (0x00010006U)
#define MBOX_TAG_GET_CLOCKS             (0x00010007U)

#define MBOX_TAG_GET_POWERSTATE         (0x00020001U)
#define MBOX_TAG_GET_POWERTIMING        (0x00020002U)

#define MBOX_TAG_GET_CLOCKSTATE         (0x00030001U)
#define MBOX_TAG_GET_CLOCKRATE          (0x00030002U)
#define MBOX_TAG_GET_VOLTAGE            (0x00030003U)
#define MBOX_TAG_GET_MAX_CLOCKRATE      (0x00030004U)
#define MBOX_TAG_GET_MAX_VOLTAGE        (0x00030005U)
#define MBOX_TAG_GET_TEMPERATURE        (0x00030006U)
#define MBOX_TAG_GET_MIN_CLOCKRATE      (0x00030007U)
#define MBOX_TAG_GET_MIN_VOLTAGE        (0x00030008U)
#define MBOX_TAG_GET_TURBO              (0x00030009U)
#define MBOX_TAG_GET_MAX_TEMPERATURE    (0x0003000aU)

#define MBOX_TAG_GET_GPIO_STATE         (0x00030041U)
#define MBOX_TAG_GET_GPIO_CONFIG        (0x00030043U)
#define MBOX_TAG_GET_THROTTLED          (0x00030046U)
#define MBOX_TAG_GET_CLOCK_MEASURED     (0x00030047U)
#define MBOX_TAG_NOTIFY_XHCI_RESET      (0x00030058U)

#define MBOX_TAG_GET_CMDLINE            (0x00050001U)
#define MBOX_TAG_GET_DMACHAN            (0x00060001U)

#define MBOX_TAG_SET_MASK               (0x00008000U)


typedef struct {
    uint32_t rdwr;
#define MBOX_SEND_CHANNEL               (8U)
#define MBOX_SEND_CHANNEL_MASK          (0xfU)
    uint32_t unused_0x04;
    uint32_t unused_0x08;
    uint32_t unused_0x0c;
    uint32_t poll;
    uint32_t send;
    uint32_t status;
#define MBOX_STATUS_FULL                (0x80000000U)
#define MBOX_STATUS_EMPTY               (0x40000000U)
    uint32_t config;
} mbox_t;

static void *out = NULL;
static uint32_t physaddr;
static volatile mbox_t *mbox = NULL; // mbox[0]:fromvc mbox[1]:tovc

static int32_t mbox_init(void)
{
    int32_t ret = 0;

    int fd;
    off_t offset;
    fd = posix_typed_mem_open("/sysram&below1G", O_RDWR, POSIX_TYPED_MEM_ALLOCATE_CONTIG);
    if (fd < 0) {
        (void)printf("Failed to open typed memory(%s)\n", strerror( errno ));
        ret = -1;
    }
    else {
        out = mmap64(NULL, 0x1000, (PROT_READ | PROT_WRITE | PROT_NOCACHE), MAP_SHARED, fd, 0);
        if (out == (void *)MAP_FAILED) {
            (void)printf("Failed to allocate buffer!\n");
            ret = -2;
        }
        else {
            if (mem_offset(out, NOFD, 1, &offset, NULL) != EOK) {
                (void)munmap(out, 0x1000);
                (void)printf("Failed to obtain physical address\n");
                ret = -3;
            }
            else {
                physaddr = (uint32_t)offset;
            }
        }
        (void)close(fd);
    }


    if (ret == 0) {
        mbox = (mbox_t *)mmap_device_memory(NULL, MBOX_REG_SIZE, (PROT_READ | PROT_WRITE | PROT_NOCACHE), 0, MBOX_REG_BASE);
        if (mbox == MAP_FAILED) {
            (void)munmap(out, 0x1000);
            (void)printf("Failed to map mbox registers\n");
            ret = -4;
        }
    }

    return ret;
}

static int32_t mbox_msg(const uint32_t tag, void *buf, const uint32_t bufsize)
{
    struct {
        uint32_t hdrsize;
        uint32_t hdrcode;
    #define MBOX_PROCESS_REQUEST            (0U)
    #define MBOX_PROCESS_RESP_OK            (0x80000000U)
    #define MBOX_PROCESS_RESP_ERROR         (0x80000001U)
        uint32_t tag;
        uint32_t tagsize;
        uint32_t tagcode;
    #define MBOX_TAG_REQUEST                (0U << 31)
    #define MBOX_TAG_RESPONSE               (1U << 31)
    #define MBOX_TAG_NULL                   (0U)
        uint8_t buf[bufsize];
        uint32_t tagend;
    } *msg;

    msg = out;
    msg->hdrsize = sizeof(*msg);
    msg->hdrcode = MBOX_PROCESS_REQUEST;
    msg->tag = tag;
    msg->tagsize = bufsize;
    msg->tagcode = MBOX_TAG_REQUEST;
    (void)memcpy((void *)msg->buf, buf, (size_t)bufsize);
    msg->tagend = MBOX_TAG_NULL;

    // check for space on tovc channel to send with timeout
    int32_t wait_cnt = 0;
    for (;;) {
        if ((mbox[1].status & MBOX_STATUS_FULL) == 0U) {
            break;
        }
        (void)usleep(100);
        if (++wait_cnt > 5000) {
            (void)printf("Check for tovc channel full timeout\n");
            return -1;
        }
    }

    // send
    mbox[1].rdwr = (physaddr & ~MBOX_SEND_CHANNEL_MASK) | MBOX_SEND_CHANNEL;

    // wait for tx done
    for (;;) {
        wait_cnt = 0;
        if ((mbox[1].status & MBOX_STATUS_FULL) == 0U) {
            break;
        }
        (void)usleep(100);
        if (++wait_cnt > 5000) {
            (void)printf("Wait for tx done timeout\n");
            return -2;
        }
    }

    for (;;) {
        wait_cnt = 0;
        // wait for response on fromvc channel to receive with timeout
        for (;;) {
            if ((mbox[0].status & MBOX_STATUS_EMPTY) == 0U) {
                break;
            }
            (void)usleep(100);
            if (++wait_cnt > 5000) {
                (void)printf("Wait for response timeout\n");
                return -3;
            }
        }

        // is it the channel we're waiting for?
        const uint32_t rd = mbox[0].rdwr;
        if ((rd & MBOX_SEND_CHANNEL_MASK) == MBOX_SEND_CHANNEL) {
            if (msg->hdrcode == MBOX_PROCESS_RESP_OK) {
                const uint32_t tagcode = msg->tagcode;
                uint32_t len = tagcode & ~MBOX_TAG_RESPONSE;

                if (tag != MBOX_TAG_GET_FIRMWAREHASH) {
                    if (len > bufsize) {
                        // passed in bufsize is not big enough to hold return response
                        len = bufsize;
                    }
                    (void)memcpy(buf, (const void*)msg->buf, (size_t)len);
                    return (int32_t)len;
                }
                else {
                    /* firmwware hash should return 20 bytes but return code indicates 8 only */
                    (void)memcpy(buf, (const void*)msg->buf, (size_t)bufsize);
                    return (int32_t)bufsize;
                }
            }
            else if (msg->hdrcode == MBOX_PROCESS_RESP_ERROR) {
                (void)printf("Mailbox error response\n");
                return -4;
            }
            else {
                return -5;
            }
        }
    }

    return 0;
}

static void fmt_d(const void *const buf, const uint32_t len) {
    for (uint32_t i = 0; i < (len / sizeof(int32_t)); i++) {
        (void)printf("%d ", ((const int32_t *) buf)[i]);
    }
    (void)printf("\n");
}

static void fmt_s(const void *const buf, const uint32_t len) {
    (void)printf("%.*s\n", len, (const char *) buf);
}

static void fmt_u(const void *const buf, const uint32_t len) {
    for (uint32_t i = 0; i < (len / sizeof(uint32_t)); i++) {
        (void)printf("%u ", ((const uint32_t *) buf)[i]);
    }
    (void)printf("\n");
}

static void fmt_x(const void *const buf, const uint32_t len) {
    /* mac address returns 6 byte, need to round up to get last two octets */
    const uint32_t length = len + ((uint32_t)sizeof(uint32_t) - 1U);
    for (uint32_t i = 0; i < (length / sizeof(uint32_t)); i++) {
        (void)printf("%08x ", ((const uint32_t *) buf)[i]);
    }
    (void)printf("\n");
}

static void fmt_x_str(const void *const buf, const uint32_t len) {
    for (uint32_t i = 0; i < len / sizeof(uint32_t); i++) {
        (void)printf("%08x", ((const uint32_t *) buf)[i]);
    }
    (void)printf("\n");
}

int main(int argc, char **argv) {
    typedef struct {
        const char *str;
        void (*fmt)(const void * const buf, const uint32_t len);
        uint32_t tag;
        uint32_t len;
    } cmd_options_t;

    static const cmd_options_t cmd_options[] = {
        { .str="clockrate",         .fmt=fmt_u,     .tag=MBOX_TAG_GET_CLOCKRATE,       .len=2 },
        { .str="clockratemeasured", .fmt=fmt_u,     .tag=MBOX_TAG_GET_CLOCK_MEASURED,  .len=2 },
        { .str="clocks",            .fmt=fmt_d,     .tag=MBOX_TAG_GET_CLOCKS,          .len=40 },
        { .str="clockstate",        .fmt=fmt_d,     .tag=MBOX_TAG_GET_CLOCKSTATE,      .len=2 },
        { .str="cmdline",           .fmt=fmt_s,     .tag=MBOX_TAG_GET_CMDLINE,         .len=256 },
        { .str="dmachan",           .fmt=fmt_x,     .tag=MBOX_TAG_GET_DMACHAN,         .len=2 },
        { .str="firmwarerev",       .fmt=fmt_x,     .tag=MBOX_TAG_GET_FIRMWAREREV,     .len=2 },
        { .str="firmwarevar",       .fmt=fmt_x,     .tag=MBOX_TAG_GET_FIRMWAREVAR,     .len=1 },
        { .str="firmwarehash",      .fmt=fmt_x_str, .tag=MBOX_TAG_GET_FIRMWAREHASH,    .len=5 },
        { .str="gpioconfig",        .fmt=fmt_x,     .tag=MBOX_TAG_GET_GPIO_CONFIG,     .len=5 },
        { .str="gpiostate",         .fmt=fmt_d,     .tag=MBOX_TAG_GET_GPIO_STATE,      .len=2 },
        { .str="macaddress",        .fmt=fmt_x,     .tag=MBOX_TAG_GET_MACADDRESS,      .len=2 },
        { .str="maxclockrate",      .fmt=fmt_u,     .tag=MBOX_TAG_GET_MAX_CLOCKRATE,   .len=2 },
        { .str="maxtemperature",    .fmt=fmt_d,     .tag=MBOX_TAG_GET_MAX_TEMPERATURE, .len=2 },
        { .str="maxvoltage",        .fmt=fmt_d,     .tag=MBOX_TAG_GET_MAX_VOLTAGE,     .len=2 },
        { .str="memory",            .fmt=fmt_x,     .tag=MBOX_TAG_GET_ARMMEMORY,       .len=2 },
        { .str="minclockrate",      .fmt=fmt_u,     .tag=MBOX_TAG_GET_MIN_CLOCKRATE,   .len=2 },
        { .str="minvoltage",        .fmt=fmt_d,     .tag=MBOX_TAG_GET_MIN_VOLTAGE,     .len=2 },
        { .str="model",             .fmt=fmt_d,     .tag=MBOX_TAG_GET_BOARDMODEL,      .len=2 },
        { .str="notifyxhcireset",   .fmt=fmt_x,     .tag=MBOX_TAG_NOTIFY_XHCI_RESET,   .len=2 },
        { .str="powerstate",        .fmt=fmt_d,     .tag=MBOX_TAG_GET_POWERSTATE,      .len=2 },
        { .str="powertiming",       .fmt=fmt_d,     .tag=MBOX_TAG_GET_POWERTIMING,     .len=2 },
        { .str="revision",          .fmt=fmt_x,     .tag=MBOX_TAG_GET_BOARDREVISION,   .len=1 },
        { .str="serial",            .fmt=fmt_x,     .tag=MBOX_TAG_GET_BOARDSERIAL,     .len=2 },
        { .str="temperature",       .fmt=fmt_d,     .tag=MBOX_TAG_GET_TEMPERATURE,     .len=2 },
        { .str="throttled",         .fmt=fmt_x,     .tag=MBOX_TAG_GET_THROTTLED,       .len=1 },
        { .str="turbo",             .fmt=fmt_x,     .tag=MBOX_TAG_GET_TURBO,           .len=2 },
        { .str="vcmemory",          .fmt=fmt_x,     .tag=MBOX_TAG_GET_VCMEMORY,        .len=2 },
        { .str="voltage",           .fmt=fmt_d,     .tag=MBOX_TAG_GET_VOLTAGE,         .len=2 },
    };

    int status = EXIT_SUCCESS;

    if (mbox_init() == 0) {
        while (*++argv) {
            uint32_t set = 0;
            char *opt;
            const char *const str = *argv;
            uint32_t i, num_arg = 0, opt_arg[256+1] = { };
            uint32_t len = (uint32_t) strcspn(str, ":=");
            int32_t found_invalid_arg = 0;

            switch (str[len]) {
                case '=':
                    set = 1U;
                    /* fall through */
                case ':':
                    opt = (char *)str + len;
                    *opt++ = '\0';
                    while (num_arg < (sizeof(opt_arg) / sizeof(opt_arg[0]))) {
                        unsigned long arg_val = strtoul(opt, &opt, 0);
                        if (((errno == ERANGE) && (arg_val == ULONG_MAX)) ||
                            ((errno == EINVAL) && (arg_val == 0UL)) ||
                            ((errno == EOK) && (arg_val >= 0x100000000UL))) {
                            found_invalid_arg = 1;
                        }
                        else {
                            opt_arg[num_arg++] = (uint32_t)arg_val;
                        }
                        if (*opt++ != ':') {
                            break;
                        }
                    }
                    break;
                default:
                    break;
            }

            for (i = 0; i < (sizeof(cmd_options) / sizeof(cmd_options[0])); i++) {
                if (strcmp(str, cmd_options[i].str) == 0) {
                    if (found_invalid_arg == 1) {
                        (void)printf("Invalid \"%s\" argument\n", str);
                    }
                    else {
                        int32_t ret;
                        uint32_t mbox_tag = cmd_options[i].tag;
                        uint32_t msg_len = cmd_options[i].len;
                        if (num_arg > msg_len) {
                            msg_len = num_arg;
                        }
                        if ((set != 0U) && (mbox_tag != MBOX_TAG_NOTIFY_XHCI_RESET)) {
                            mbox_tag |= MBOX_TAG_SET_MASK;
                        }
                        ret = mbox_msg(mbox_tag, (void *)opt_arg, (msg_len * (uint32_t)sizeof(opt_arg[0])));
                        if (ret > 0) {
                            msg_len = (uint32_t) ret;
                            cmd_options[i].fmt(opt_arg, msg_len);
                        }
                    }
                    break;
                }
            }
            if (i >= (sizeof(cmd_options) / sizeof(cmd_options[0]))) {
                /* check if TAG is entered */
                unsigned long arg_val = strtoul(str, &opt, 0);
                if (((errno == ERANGE) && (arg_val == ULONG_MAX)) ||
                    ((errno == EINVAL) && (arg_val == 0UL)) ||
                    ((errno == EOK) && (arg_val >= 0x100000000UL))) {
                    (void)printf("Invalid command id: \"%s\"\n", str);
                }
                else if (found_invalid_arg == 1) {
                    (void)printf("Invalid \"%s\" argument\n", str);
                    break;
                }
                else {
                    uint32_t mbox_tag = (uint32_t) arg_val;
                    int32_t ret;
                    uint32_t msg_len;
                    if (num_arg != 0U) {
                        msg_len = num_arg;
                    }
                    else {
                        msg_len = 2;
                    }
                    if (set != 0U) {
                        mbox_tag |= MBOX_TAG_SET_MASK;
                    }
                    ret = mbox_msg(mbox_tag, (void *)opt_arg, (msg_len * (uint32_t)sizeof(opt_arg[0])));
                    if (ret > 0) {
                        msg_len = (uint32_t) ret;
                        fmt_x(opt_arg, msg_len);
                    }
                }
            }
        }
    }
    else {
        status = EXIT_FAILURE;
    }

    return status;
}
