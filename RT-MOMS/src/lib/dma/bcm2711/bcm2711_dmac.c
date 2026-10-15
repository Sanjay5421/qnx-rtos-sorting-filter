/*
 * Copyright (c) 2022, BlackBerry Limited.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <atomic.h>
#include <sys/slog2.h>
#include <sys/rsrcdbmgr.h>
#include <sys/rsrcdbmsg.h>
#include <hw/dma.h>
#include <aarch64/inout.h>
#include <pthread.h>
#include <aarch64/bcm2711.h>
#include "bcm2711_dmac.h"

#define REVERSED_PRIORITY       0

static int n_users_in_process;
static pthread_mutex_t procs_init_mutex = PTHREAD_MUTEX_INITIALIZER;
static bcm2711_dmac_ctrl_t bcm2711_dmac;

typedef struct dma_ch_info {
    uint32_t offset;     // channel register offset
    uint32_t irq;        // channel irq number
    uint32_t is_dma4;    // dma4 engine
    uint32_t is_dmalite; // lite version of regular dma engine
} dma_ch_info_t;

static const dma_ch_info_t chs_info [] = {
    { .offset = BCM2711_DMA_OFFSET_CH(0),  .irq = BCM2711_DMA0_IRQ,  .is_dma4 = 0, .is_dmalite = 0 },
    { .offset = BCM2711_DMA_OFFSET_CH(1),  .irq = BCM2711_DMA1_IRQ,  .is_dma4 = 0, .is_dmalite = 0 },
    { .offset = BCM2711_DMA_OFFSET_CH(2),  .irq = BCM2711_DMA2_IRQ,  .is_dma4 = 0, .is_dmalite = 0 },
    { .offset = BCM2711_DMA_OFFSET_CH(3),  .irq = BCM2711_DMA3_IRQ,  .is_dma4 = 0, .is_dmalite = 0 },
    { .offset = BCM2711_DMA_OFFSET_CH(4),  .irq = BCM2711_DMA4_IRQ,  .is_dma4 = 0, .is_dmalite = 0 },
    { .offset = BCM2711_DMA_OFFSET_CH(5),  .irq = BCM2711_DMA5_IRQ,  .is_dma4 = 0, .is_dmalite = 0 },
    { .offset = BCM2711_DMA_OFFSET_CH(6),  .irq = BCM2711_DMA6_IRQ,  .is_dma4 = 0, .is_dmalite = 0 },
    { .offset = BCM2711_DMA_OFFSET_CH(7),  .irq = BCM2711_DMA7_IRQ,  .is_dma4 = 0, .is_dmalite = 1 },
    { .offset = BCM2711_DMA_OFFSET_CH(8),  .irq = BCM2711_DMA8_IRQ,  .is_dma4 = 0, .is_dmalite = 1 },
    { .offset = BCM2711_DMA_OFFSET_CH(9),  .irq = BCM2711_DMA9_IRQ,  .is_dma4 = 0, .is_dmalite = 1 },
    { .offset = BCM2711_DMA_OFFSET_CH(10), .irq = BCM2711_DMA10_IRQ, .is_dma4 = 0, .is_dmalite = 1 },
    { .offset = BCM2711_DMA_OFFSET_CH(11), .irq = BCM2711_DMA11_IRQ, .is_dma4 = 1, .is_dmalite = 0 },
    { .offset = BCM2711_DMA_OFFSET_CH(12), .irq = BCM2711_DMA12_IRQ, .is_dma4 = 1, .is_dmalite = 0 },
    { .offset = BCM2711_DMA_OFFSET_CH(13), .irq = BCM2711_DMA13_IRQ, .is_dma4 = 1, .is_dmalite = 0 },
    { .offset = BCM2711_DMA_OFFSET_CH(14), .irq = BCM2711_DMA14_IRQ, .is_dma4 = 1, .is_dmalite = 0 },
};

static void bcm2711_dmac_dump_registers(const dma_channel_t* const chan)
{
    const dma_ch_info_t* const ch_info = &chs_info[chan->chan_idx];

    bcm2711_dma_slogf(SLOG2_ERROR, "chan %d:ENABLE=0x%x, INT_STATUS=0x%x",
                      chan->chan_idx,
                      in32(bcm2711_dmac.vbase + DMA_ENABLE),
                      in32(bcm2711_dmac.vbase + DMA_INT_STATUS));

    if (ch_info->is_dma4) {
        bcm2711_dma_slogf(SLOG2_ERROR, "chan %d(DMA4):CS=0x%x, CB=0x%x, DEBUG=0x%x, TI=0x%x,",
                          chan->chan_idx,
                          in32(chan->ch_regs + DMA4_CS), in32(chan->ch_regs + DMA4_CB),
                          in32(chan->ch_regs + DMA4_DEBUG), in32(chan->ch_regs + DMA4_TI));
        bcm2711_dma_slogf(SLOG2_ERROR, "chan %d(DMA4):SRC=0x%x, SRCI=0x%x, DST=0x%x, DSTI=0x%x",
                          chan->chan_idx,
                          in32(chan->ch_regs + DMA4_SRC), in32(chan->ch_regs + DMA4_SRCI),
                          in32(chan->ch_regs + DMA4_DST), in32(chan->ch_regs + DMA4_DSTI));
        bcm2711_dma_slogf(SLOG2_ERROR, "chan %d(DMA4):LEN=0x%x, NEXT_CB=0x%x, DEBUG2=0x%x",
                          chan->chan_idx,
                          in32(chan->ch_regs + DMA4_LEN), in32(chan->ch_regs + DMA4_NEXT_CB),
                          in32(chan->ch_regs + DMA4_DEBUG2));
    } else {
        bcm2711_dma_slogf(SLOG2_ERROR, "chan %d(DMA):CS=0x%x, CONBLK_AD=0x%x, TI=0x%x, SRC_AD=0x%x, DST_AD=0x%x",
                            chan->chan_idx,
                            in32(chan->ch_regs + DMA_CS), in32(chan->ch_regs + DMA_CONBLK_AD),
                            in32(chan->ch_regs + DMA_TI), in32(chan->ch_regs + DMA_SRC_AD),
                            in32(chan->ch_regs + DMA_DST_AD));
        bcm2711_dma_slogf(SLOG2_ERROR, "chan %d(DMA):DMA_TXFR_LEN=0x%x, STRIDE=0x%x, NXTCONBK=0x%x, DEBUG=0x%x",
                            chan->chan_idx,
                            in32(chan->ch_regs + DMA_TXFR_LEN), in32(chan->ch_regs + DMA_STRIDE),
                            in32(chan->ch_regs + DMA_NXTCONBK), in32(chan->ch_regs + DMA_DEBUG));
    }
}

static void dump_cbs(const uint32_t* const cb_addr, const uint32_t sgs)
{
    /* each control block is 32 byte aligned and
     * it is constructured by 8 dwords.
     */
    for (uint32_t sgi = 0; sgi < sgs; sgi++) {
        const uint32_t idx = (sgi * DMA_CB_SIZE) / 4;
        bcm2711_dma_slogf(SLOG2_ERROR, "cb%d: 0x%08x 0x%08x 0x%08x 0x%08x"
                                       " 0x%08x 0x%08x 0x%08x 0x%08x", sgi,
            cb_addr[idx],     cb_addr[idx + 1], cb_addr[idx + 2], cb_addr[idx + 3],
            cb_addr[idx + 4], cb_addr[idx + 5], cb_addr[idx + 6], cb_addr[idx + 7]);
    }
}

static int rsrcdb_create_resources(void)
{
    rsrc_alloc_t req = { 0 };
    int ret = EOK;

    /* query all freed and used blocks */
    ret = rsrcdbmgr_query_name (NULL, 0, 0, -2, BCM2711_DMAC_NAME, 0);
    if (ret < 0) {
        ret = errno;
        bcm2711_dma_slogf(SLOG2_ERROR, "%s: Failed to query bcm2711dma resources", __FUNCTION__);
        return ret;
    } else if (ret > 0) {
        /* Call succeeded, resource exist */
        return EALREADY;
    } else {
        /* create a resource */
        req.start = 0;
        req.end = bcm2711_dmac.num_channels - 1;
        req.flags = RSRCDBMGR_DMA_CHANNEL | RSRCDBMGR_FLAG_NAME | RSRCDBMGR_FLAG_NOREMOVE;
        req.name = BCM2711_DMAC_NAME;

        ret = rsrcdbmgr_create(&req, 1);
        if (ret != 0) {
            ret = errno;
            bcm2711_dma_slogf(SLOG2_ERROR, "%s: Failed creating resources: err %d", __FUNCTION__, ret);
            return ret;
        }
    }

    return ret;
}

static void rsrcdb_detach_resources(const dma_channel_t* const chan)
{
    rsrc_request_t  req = { 0 };
    // release DMA resource
    req.length = 1;
    req.start  = chan->chan_idx;
    req.end    = chan->chan_idx;
    req.flags  = RSRCDBMGR_DMA_CHANNEL | RSRCDBMGR_FLAG_NAME;
    req.name   = BCM2711_DMAC_NAME;
    rsrcdbmgr_detach(&req, 1);
}

static int rsrcdb_attach_channel(const dma_channel_t* const chan, const unsigned* const channel,
                        const uint64_t priority, const dma_attach_flags flags)
{
    rsrc_request_t  req = { 0 };
    req.length = 1;
    req.flags  = RSRCDBMGR_DMA_CHANNEL | RSRCDBMGR_FLAG_NAME | RSRCDBMGR_FLAG_RANGE;
    req.name   = BCM2711_DMAC_NAME;
    req.start  = 0;
    req.end    = bcm2711_dmac.num_channels - 1;

    if ((chan->range_min != -1) && (chan->range_max != -1)) {
        req.start  = (uint64_t)chan->range_min;
        req.end    = (uint64_t)chan->range_max;
    }

    if (flags & DMA_ATTACH_PRIORITY_HIGHEST) {
        req.flags |= REVERSED_PRIORITY ? 0 : RSRCDBMGR_FLAG_TOPDOWN;
    } else if (flags & (DMA_ATTACH_PRIORITY_STRICT | DMA_ATTACH_PRIORITY_ATLEAST)) {
        /* Prio flag is set. Prio must be in range */
        if ((priority < req.start) || (priority > req.end)) {
            bcm2711_dma_slogf(SLOG2_ERROR, "%s: priority %jd is out of range %jd:%jd!", __FUNCTION__,
                    priority, req.start, req.end);
            errno = EINVAL;
            return -1;
        }
        req.start = priority;
        req.end = (flags & DMA_ATTACH_PRIORITY_STRICT) ?
                    priority : (uint64_t) (bcm2711_dmac.num_channels - 1);
    } else if (flags & DMA_ATTACH_ANY_CHANNEL) {
        /* Any channel */
    } else if (channel != NULL) {
        /* if channel in range, use it, if not, then we decide base on the prio. */
        if ((*channel >= req.start) && (*channel <= req.end)) {
            req.start = *channel;
            req.end = *channel;
        } else {
            bcm2711_dma_slogf(SLOG2_ERROR, "%s: Channel is out of range %d",
                                __FUNCTION__, *channel);
            errno = EINVAL;
            return -1;
        }
    } else {
        bcm2711_dma_slogf(SLOG2_ERROR, "%s: Invalid parameters", __FUNCTION__);
        errno = EINVAL;
        return -1;
    }

    if (rsrcdbmgr_attach(&req, 1) == -1) {
        bcm2711_dma_slogf(SLOG2_ERROR, "%s: DMA channel rsrcdbmgr_attach failed: %s", __FUNCTION__, strerror(errno));
        return -1;
    }

    return req.start;
}

static int parse_options(dma_channel_t* const chan, char *options)
{
    static char *dma_opts[] = {
    #define DMAC_OPT_NUM_CBS   0
        "num_cbs",       // number of dma control blocks
    #define DMAC_OPT_RANGE_MIN 1
        "range_min",    // the start of the requested channel range that the a channel to assigned from.
    #define DMAC_OPT_RANGE_MAX 2
        "range_max",    // the start of the requested channel range that the a channel to assigned from.
    #define DMAC_OPT_TMEM      3
        "typed_mem",    // typed mem name for buffer
    #define DMAC_OPT_DEBUG     4
        "debug",        // debug, 1=on, 0=off, default off
        NULL
    };

    char   *value;
    int     opt;

    chan->typed_mem_fd = NOFD;
    chan->num_cbs = DMA_DEFAULT_NUM_CBS;

    while (options && (*options != '\0')) {
        opt = getsubopt(&options, dma_opts, &value);
        if (opt == -1) {
            bcm2711_dma_slogf(SLOG2_ERROR, "%s:%d - getsubopt()failed - %s",
                                    __FUNCTION__, __LINE__, strerror(errno));
            return EINVAL;
        }
        switch (opt) {
            case DMAC_OPT_NUM_CBS:
                chan->num_cbs = (uint32_t) strtoul(value, NULL, 0);
                break;
            case DMAC_OPT_RANGE_MIN:
                chan->range_min = (int16_t) strtoul(value, NULL, 0);
                break;
            case DMAC_OPT_RANGE_MAX:
                chan->range_max = (int16_t) strtoul(value, NULL, 0);
                break;
            case DMAC_OPT_TMEM:
                chan->typed_mem_fd = posix_typed_mem_open(value, O_RDWR,
                                                    POSIX_TYPED_MEM_ALLOCATE_CONTIG);
                if (chan->typed_mem_fd == -1) {
                    bcm2711_dma_slogf(SLOG2_ERROR, "Failed to open typed memory %s: %s",
                                                        value, strerror(errno));
                    return errno;
                }
                break;
            case DMAC_OPT_DEBUG:
                chan->debug = (uint32_t) strtoul(value, NULL, 0);
                break;
            default:
                bcm2711_dma_slogf(SLOG2_ERROR, "%s:%d - Invalid option %s",
                                        __FUNCTION__, __LINE__, dma_opts[opt]);
                return EINVAL;
        }
    }

    /* range_min and range_max are optional, but they must be provided in paris. it is
     * not allowed to define one and leave another one undefined(-1).
     */
    if ((chan->range_min == -1) ^ (chan->range_max == -1)) {
        bcm2711_dma_slogf(SLOG2_ERROR, "%s: Invalid channel range %d:%d", __FUNCTION__,
                            chan->range_min, chan->range_max);
        return (EINVAL);
    }

    if (chan->typed_mem_fd == NOFD) {
        chan->typed_mem_fd = posix_typed_mem_open("sysram&below1G", O_RDWR, POSIX_TYPED_MEM_ALLOCATE_CONTIG);
        if (chan->typed_mem_fd == -1) {
            bcm2711_dma_slogf(SLOG2_ERROR, "Failed to open typed memory sysram&below1G  %s", strerror(errno));
            return errno;
        }
        bcm2711_dma_slogf(SLOG2_WARNING, "%s: typed_mem is not defined, using default(sysram&below1G)", __FUNCTION__);
    }

    return EOK;
}

static int dma_alloc_buffer(void* const handle, dma_addr_t* const addr, const unsigned size, const unsigned flags)
{
    const dma_channel_t* const chan = handle;
    int     prot_flags = PROT_READ | PROT_WRITE;

    if ((chan == NULL) || (chan->typed_mem_fd == NOFD)) {
        errno = EINVAL;
        return -1;
    }

    if (flags & DMA_BUF_FLAG_NOCACHE) {
        prot_flags |= PROT_NOCACHE;
    }

    addr->vaddr = mmap(NULL, size, prot_flags, MAP_SHARED, chan->typed_mem_fd, 0);

    if (addr->vaddr == MAP_FAILED) {
        bcm2711_dma_slogf(SLOG2_ERROR, "%s failed: %s", __FUNCTION__, strerror(errno));
        return -1;
    }

    if (mem_offset64(addr->vaddr, NOFD, 1, &addr->paddr, NULL) != 0) {
        bcm2711_dma_slogf(SLOG2_ERROR, "%s failed: %s", __FUNCTION__, strerror(errno));
        return -1;
    }
    addr->len = size;

    return 0;
}

static void dma_free_buffer( void* const handle, dma_addr_t *addr)
{
    if (addr->len) {
        munmap(addr->vaddr, addr->len);
    }
    addr->vaddr = NULL;
    addr->len   = 0;
}

/* convert arm view address to legacy master address */
static inline uint32_t dma_legacy_addr(const uint32_t phys) {
    if ((phys & 0xfc000000) == 0xfc000000) {
        return phys & ~(0x8 << 28); // legacy main peripheral
    }
    return phys | (0xc << 28);      // legacy sdram
}

/* convert arm view address to full 40-bit address */
static inline off64_t dma4_full_addr(const off64_t phys) {
    if ((phys & 0xfffffffffc000000LL) == 0xfc000000LL) {
        return (0x400000000LL | (phys & ~(0x8LL << 28)));
    }
    return (phys & 0xffffffffffLL);
}

static int dma4_setcb(const dma_channel_t* const chan, dma4_cb_t* const scb)
{
    off64_t paddr;
    const dma_cb_params_t* const params = &chan->cb_params;

    /* According to RPI4 spec(p72), on the BCM2711, the width cannot be set
     * larger than 128 bits
     */
    switch (chan->xfer_unit_size) {
        case 4:
            scb->srci  = DMA4_SIZE_32;
            scb->desti = DMA4_SIZE_32;
            break;
        case 8:
            scb->srci  = DMA4_SIZE_64;
            scb->desti = DMA4_SIZE_64;
            break;
        case 16:
            scb->srci  = DMA4_SIZE_128;
            scb->desti = DMA4_SIZE_128;
            break;
        default:
            bcm2711_dma_slogf(SLOG2_ERROR, "%s: unsupported transfer unit size(%u)",
                              __FUNCTION__, chan->xfer_unit_size);
            errno = EINVAL;
            return -1;
    }

    if (params->info & SRC_INC) {
        scb->srci |= DMA4_INC;
    }

    if (params->info & DST_INC) {
        scb->desti |= DMA4_INC;
    }

    if (chan->xfer_type == MEMORY_TO_MEMORY) {
        scb->srci |= DMA4_BURST_LEN(16);
        scb->desti |= DMA4_BURST_LEN(16);
    }

    scb->ti = DMA4_TI_PERMAP(params->dreq) | DMA4_TI_WAIT_WR_RESP;

    if (params->info & SRC_DREQ) {
        scb->ti |= (DMA4_TI_S_DREQ | DMA4_TI_WAIT_RD_RESP);
    } else if (params->info & DST_DREQ) {
        scb->ti |= DMA4_TI_D_DREQ;
    } else {
        /* no dreq signal */
    }

    if (params->info & INT_EN) {
        scb->ti |= DMA4_TI_INTEN;
    }

    /* workaround:
     * some of our memory to memory test observation on channel11(DMA4) indicates that this channel has
     * faster transfer speed and tend to process multiple CBs during one normal interrupt cycle and
     * this lead to some interrupt events are missing during transfer with DMA_ATTACH_EVENT_PER_SEGMENT
     * notification.
     * To workaround this, we add a minimum 1 dummy cycle before the start of next segment transfer and
     * this is only applied to memory-to-memory use case.
     */
    if ((chan->aflags & DMA_ATTACH_EVENT_PER_SEGMENT) &&
        (chan->xfer_type == MEMORY_TO_MEMORY) &&
        (chan->chan_idx == 11)) {
        scb->ti |= DMA4_TI_D_WAITS(0x1) | DMA4_TI_S_WAITS(0x1);
    }

    paddr = dma4_full_addr(params->src_paddr);
    scb->src = ((uint32_t)paddr & UINT32_MAX);
    scb->srci |= ((uint32_t)(paddr >> 32) & 0xff) ;

    paddr = dma4_full_addr(params->dst_paddr);
    scb->dest = ((uint32_t)paddr & UINT32_MAX);
    scb->desti |= ((uint32_t)(paddr >> 32) & 0xff);

    scb->len = params->len;
    /* The address must be 256-bit aligned and so the bottom 5 bits of the byte address are discarded */
    scb->next_cb = (params->nextcb) ? (uint32_t)(dma4_full_addr(params->nextcb) >> 5) : 0;
    return 0;
}

static int dma_setcb(const dma_channel_t* const chan, dma_cb_t* const cb)
{
    const dma_cb_params_t* const params = &chan->cb_params;
    uint32_t paddr;

    switch (chan->xfer_unit_size) {
        case 4:
            cb->ti = DMA_TI_SRC_WIDTH_32 | DMA_TI_DST_WIDTH_32;
            break;
        case 16:
            cb->ti = DMA_TI_SRC_WIDTH_128 | DMA_TI_DST_WIDTH_128;
            break;
        default:
            bcm2711_dma_slogf(SLOG2_ERROR, "%s: unsupported transfer unit size(%u)",
                              __FUNCTION__, chan->xfer_unit_size);
            errno = EINVAL;
            return -1;
    }

    if (params->info & SRC_INC) {
        cb->ti |= DMA_TI_SRC_INC;
    }

    if (params->info & DST_INC) {
        cb->ti |= DMA_TI_DST_INC;
    }

    if (chan->xfer_type == MEMORY_TO_MEMORY) {
        cb->ti |= DMA_TI_BURST(16);
        cb->ti |= DMA_TI_BURST(16);
    }

    /* wait for write response
     * when set this makes the DMA wait until it receives the
     * AXI write response for each write. This ensures that
     * multiple writes cannot get stacked in the AXI bus pipeline.
     */
    cb->ti |= DMA_TI_PERMAP(params->dreq) | DMA_TI_WAIT_RESP;

    if (params->info & SRC_DREQ) {
        cb->ti |= (DMA_TI_SRC_DREQ);
    } else if (params->info & DST_DREQ) {
        cb->ti |= DMA_TI_DST_DREQ;
    } else {
        /* no dreq signal */
    }

    if (params->info & INT_EN) {
        cb->ti |= DMA_TI_INTEN;
    }

    /* workaround:
     * some of our memory to memory test observation on channel3(DMA) indicates that this channel has
     * faster transfer speed and tend to process multiple CBs during one normal interrupt cycle and
     * this lead to some interrupt events are missing during transfer with DMA_ATTACH_EVENT_PER_SEGMENT
     * notification.
     * To workaround this, we add a minimum 1 dummy cycle before the start of next segment transfer and
     * this is only applied to memory-to-memory use case.
     */
    if ((chan->aflags & DMA_ATTACH_EVENT_PER_SEGMENT) &&
        (chan->xfer_type == MEMORY_TO_MEMORY) &&
        (chan->chan_idx == 3)) {
        cb->ti |= DMA_TI_WAITS(0x1);
    }

    paddr = ((uint32_t)params->src_paddr & UINT32_MAX);
    cb->source_ad = dma_legacy_addr(paddr);

    paddr = ((uint32_t)params->dst_paddr & UINT32_MAX);
    cb->dest_ad = dma_legacy_addr(paddr);

    cb->stride = 0;
    cb->txfr_len =  params->len;
    cb->nextconbk = (params->nextcb) ? dma_legacy_addr((uint32_t)params->nextcb) : 0;
    return 0;
}

static inline int check_segment_params(const dma_ch_info_t* const ch_info,
            const dma_transfer_t* const tinfo, const uint32_t sg_idx, const bool is_dst)
{
    const uint32_t xfer_unit_size = tinfo->xfer_unit_size;
    const uint32_t len = (is_dst == true) ? tinfo->dst_addrs[sg_idx].len : tinfo->src_addrs[sg_idx].len;
    const off64_t paddr = (is_dst == true) ? tinfo->dst_addrs[sg_idx].paddr : tinfo->src_addrs[sg_idx].paddr;

    /* address shouldn't be larger than 32 bit for dma and dmalite engines */
    if ((paddr > (off64_t)UINT32_MAX) && !(ch_info->is_dma4)) {
        bcm2711_dma_slogf(SLOG2_ERROR, "error invalid %s address(0x%lx) for this segment(%d)",
                                (is_dst == true) ? "dst" : "src", paddr, sg_idx);
        return -1;
    }

    /* len have to be a multiple of xfer_unit_size */
    if (len % xfer_unit_size) {
        bcm2711_dma_slogf(SLOG2_ERROR, "error len %d is not a multiple of unit size %d",
                          len, xfer_unit_size);
        return -1;
    }

    if (ch_info->is_dmalite && (len > MAX_LITE_DMA_LEN)) {
        bcm2711_dma_slogf(SLOG2_ERROR, "invalid xfer len(%u), max len of dmalite channels(7~10) is %u",
                            len, MAX_LITE_DMA_LEN);
        return -1;
    } else if (len > MAX_DMA_LEN) {
        bcm2711_dma_slogf(SLOG2_ERROR, "invalid xfer len(%d), max len of dma channels(0~6 and 11~14) is %u)",
                            len, MAX_DMA_LEN);
        return -1;
    } else {
        return 0;
    }
}

static dma_xfer_types_t get_xfer_type(dma_xfer_flags src_flags, dma_xfer_flags dst_flags)
{
    #define DMA_XFER_TYPE_FLAG ( DMA_ADDR_FLAG_IO | DMA_ADDR_FLAG_MEMORY | DMA_ADDR_FLAG_DEVICE)
    #define NUM_SRC_TYPES 3
    #define NUM_DST_TYPES 3
    int32_t src_type, dst_type;
    int32_t sflags_setcount, dflags_setcount;
    static const dma_xfer_types_t xfer_types[NUM_SRC_TYPES][NUM_DST_TYPES] = {
        {IO_TO_IO, IO_TO_MEMORY, IO_TO_DEVICE},
        {MEMORY_TO_IO, MEMORY_TO_MEMORY, MEMORY_TO_DEVICE},
        {DEVICE_TO_IO, DEVICE_TO_MEMORY, DEVICE_TO_DEVICE}};

    src_flags = src_flags & DMA_XFER_TYPE_FLAG;
    dst_flags = dst_flags & DMA_XFER_TYPE_FLAG;

    sflags_setcount = __builtin_popcount(src_flags);
    dflags_setcount = __builtin_popcount(dst_flags);

    src_type = src_flags >> 1;
    dst_type = dst_flags >> 1;

    /* 1 bit in DMA_XFER_TYPE_FLAG must be set
     * and index should be inside tthe boundary of xfer_types
     */
    if ((sflags_setcount != 1) ||
        (dflags_setcount != 1) ||
        (src_type >= NUM_SRC_TYPES) ||
        (dst_type >= NUM_DST_TYPES)) {
        return UNKNOWN_XFER_TYPE;
    }

    return xfer_types[src_type][dst_type];
}

static int check_tinfo_params(const dma_channel_t* const chan, const dma_transfer_t* const tinfo)
{
    const dma_ch_info_t* const ch_info = &chs_info[chan->chan_idx];
    uint32_t sgi;
    uint32_t src_total_len = 0;
    uint32_t dst_total_len = 0;
    int status = 0;

    const dma_xfer_types_t xfer_type = get_xfer_type(tinfo->src_flags, tinfo->dst_flags);
    if ((xfer_type == UNKNOWN_XFER_TYPE) ||
        (xfer_type == DEVICE_TO_DEVICE)) {
        bcm2711_dma_slogf(SLOG2_ERROR, "error unsupported transfer type(%u)!",
                                        (uint32_t)xfer_type);
        errno = EINVAL;
        return -1;
    }

    /* Validate if transfer size if compatible with bytes transfer */
    if (tinfo->xfer_bytes % tinfo->xfer_unit_size) {
        bcm2711_dma_slogf(SLOG2_ERROR, "xfer_bytes(%d) is not aligned with unit size(%d)",
                        tinfo->xfer_bytes, tinfo->xfer_unit_size);
        errno = EINVAL;
        return -1;
    }

    for (sgi = 0; sgi < tinfo->src_fragments; sgi++) {
        status = check_segment_params(ch_info, tinfo, sgi, (bool)false);
        if (status != 0) {
            return -1;
        }
        /* unaligned fixed address is not supported */
        if ((tinfo->src_flags & DMA_ADDR_FLAG_NO_INCREMENT) &&
            (tinfo->src_addrs[sgi].paddr % (int)tinfo->xfer_unit_size)) {
            bcm2711_dma_slogf(SLOG2_ERROR, "source address is fixed but it is unaligned, src_paddr 0x%lx",
                              tinfo->src_addrs[sgi].paddr);
            return -1;
        }
        src_total_len += tinfo->src_addrs[sgi].len;
    }

    for (sgi = 0; sgi < tinfo->dst_fragments; sgi++) {
        status = check_segment_params(ch_info, tinfo, sgi, (bool)true);
        if (status != 0) {
            return -1;
        }
        /* unaligned fixed address is not supported */
        if ((tinfo->dst_flags & DMA_ADDR_FLAG_NO_INCREMENT) &&
            (tinfo->dst_addrs[sgi].paddr % (int)tinfo->xfer_unit_size)) {
            bcm2711_dma_slogf(SLOG2_ERROR, "destination address is fixed but it is unaligned, dst_paddr 0x%lx",
                              tinfo->dst_addrs[sgi].paddr);
            return -1;
        }
        dst_total_len += tinfo->dst_addrs[sgi].len;
    }

    /*
     * Copying fragments from one side to the other or vice versa can be:
     * 1 fragment to 1 fragment
     * Multiple fragments to 1 fragment
     * Multiple fragments to multiple fragments (src fragments are equal to dst fragments)
     */
    if (tinfo->src_flags & DMA_ADDR_FLAG_DEVICE) {
        /* Device to Memory */
        if (dst_total_len != tinfo->xfer_bytes) {
            status = -1;
        }
    } else if (tinfo->dst_flags & DMA_ADDR_FLAG_DEVICE) {
        /* Memory to Device */
        if (src_total_len != tinfo->xfer_bytes) {
            status = -2;
        }
    } else if ( tinfo->src_fragments == tinfo->dst_fragments) {
        /* Memory to Memory */
        if ((src_total_len != tinfo->xfer_bytes) ||
            (dst_total_len != tinfo->xfer_bytes)) {
            status = -3;
        }
    } else if (tinfo->src_fragments == 1) {
        /* Memory to Memory */
        if (dst_total_len != tinfo->xfer_bytes) {
            status = -4;
        }
    } else if (tinfo->dst_fragments == 1) {
        /* Memory to Memory */
        if (src_total_len != tinfo->xfer_bytes) {
            status = -5;
        }
    } else {
        status = -6;
    }

    if (status != 0) {
        bcm2711_dma_slogf(SLOG2_ERROR, "[%d] Invalid lengths: frags %d:%d total lens %d:%d xfer_bytes %d status %d",
                chan->chan_idx, tinfo->src_fragments, tinfo->dst_fragments,
                src_total_len, dst_total_len, tinfo->xfer_bytes, status);
        return -1;
    }

    if ((tinfo->src_flags | tinfo->dst_flags) & DMA_ADDR_FLAG_DEVICE) {
        if (tinfo->req_id >= DMA_MAX_DREQ_NUM) {
            bcm2711_dma_slogf(SLOG2_ERROR, "[%d] Error: Hw req id %d is out of range (%d)",
                    chan->chan_idx, tinfo->req_id, DMA_MAX_DREQ_NUM);
            return -1;
        }
    }

    return 0;
}

static int dma_setup_cbs(void* const handle, const dma_transfer_t* const tinfo)
{
    dma_channel_t* const chan = handle;
    const dma_ch_info_t* const ch_info = &chs_info[chan->chan_idx];
    dma_cb_params_t* const params = &chan->cb_params;
    off64_t  cb_pbase;
    dma_cb_t *cb = NULL;
    const uint32_t sgs = max(tinfo->src_fragments, tinfo->dst_fragments);

    /* If segments are larger than the default value(128), client need to adjust
     * this number by using attach option.
     */
    if (sgs > chan->num_cbs) {
        bcm2711_dma_slogf(SLOG2_ERROR, "segment number(%d) is larger than CB numbers(%d)",
                          sgs, chan->num_cbs);
        errno = EINVAL;
        return -1;
    }

    if (chan->cb.len == 0) {
        if (dma_alloc_buffer(handle,
                             &chan->cb,
                             chan->num_cbs * DMA_CB_SIZE,
                             DMA_BUF_FLAG_NOCACHE) != 0) {
                bcm2711_dma_slogf(SLOG2_ERROR, "%s: bcm2711 DMA chan %d buf alloc failed: %s", __FUNCTION__,
                                  chan->chan_idx, strerror(errno));
                return -1;
        }
    }
    cb_pbase = chan->cb.paddr;
    cb = chan->cb.vaddr;
    chan->frgs_num = sgs;
    chan->frg_count = sgs;

    for (uint32_t sgi = 0; sgi < sgs; sgi++) {
        const uint32_t src_frag = ((sgi < tinfo->src_fragments) ? sgi : 0);
        const uint32_t dst_frag = ((sgi < tinfo->dst_fragments) ? sgi : 0);

        params->src_paddr =  tinfo->src_addrs[src_frag].paddr;
        params->dst_paddr =  tinfo->dst_addrs[dst_frag].paddr;

        /* Find the total xfer size for the fragment */
        if (tinfo->src_fragments > 1) {
            params->len = tinfo->src_addrs[src_frag].len;
        } else if (tinfo->dst_fragments > 1) {
            params->len = tinfo->dst_addrs[dst_frag].len;
        } else {
            /* Not fragmented/segmented copying */
            params->len = tinfo->xfer_bytes;
        }

        params->info &= ~INT_EN;
        if (sgi != (sgs - 1)) {
            /* regular segments */
            params->nextcb = cb_pbase + (((int32_t)sgi + 1) * (int32_t)DMA_CB_SIZE);
        } else {
            /* last segment */
            if (tinfo->mode_flags & DMA_MODE_FLAG_REPEAT) {
                /*wrap the next CB address to the head for cyclic mode */
                params->nextcb = cb_pbase;
            } else {
                /* set the next CB address to 0 to indicate the end of all transfers */
                params->nextcb = 0;
            }
            /* enable interrupt if this is a per transfer notification */
            if (chan->aflags & DMA_ATTACH_EVENT_ON_COMPLETE) {
                params->info |= INT_EN;
            }
        }
        /* enable interrrupt if this is a per segment/fragment notification
         * DMA_ATTACH_EVENT_PER_SEGMENT and DMA_ATTACH_EVENT_ON_COMPLETE are
         * mutual exclusive.
         */
        if (chan->aflags & DMA_ATTACH_EVENT_PER_SEGMENT) {
                params->info |= INT_EN;
        }

        /* set control block structure */
        if (ch_info->is_dma4) {
            if (dma4_setcb(chan, (dma4_cb_t *) cb) != 0) {
                return -1;
            }
        } else {
            if (dma_setcb(chan, cb) != 0) {
                return -1;
            }
        }

        cb++;
    }

    if (chan->debug) {
        dump_cbs((uint32_t *)chan->cb.vaddr, sgs);
    }

    return 0;
}

static int dma_setup_xfer(void* const handle, const dma_transfer_t* const tinfo)
{
    dma_channel_t* const chan = handle;
    dma_cb_params_t* const params = &chan->cb_params;

    if ((chan == NULL) || (tinfo == NULL)) {
        errno = EINVAL;
        return -1;
    }

    if (check_tinfo_params(chan, tinfo) != 0) {
        errno = EINVAL;
        return -1;
    }

    if (chan->status & DMACH_BUSY) {
        errno = EBUSY;
        return -1;
    }

    memset(params, 0, sizeof(dma_cb_params_t));
    if (!(tinfo->src_flags & DMA_ADDR_FLAG_NO_INCREMENT)) {
        params->info |= SRC_INC;
    }

    if (!(tinfo->dst_flags & DMA_ADDR_FLAG_NO_INCREMENT)) {
        params->info |= DST_INC;
    }

    /* Hardware triggered DMA transfer */
    if (tinfo->src_flags & DMA_ADDR_FLAG_DEVICE) {
        params->info |= SRC_DREQ;
        params->dreq = tinfo->req_id;
    } else if (tinfo->dst_flags & DMA_ADDR_FLAG_DEVICE) {
        params->info |= DST_DREQ;
        params->dreq = tinfo->req_id;
    } else {
        /* both src and dst are not peripherals */
    }

    chan->xfer_type = get_xfer_type(tinfo->src_flags, tinfo->dst_flags);
    chan->mflags = tinfo->mode_flags;
    chan->xfer_unit_size = tinfo->xfer_unit_size;

    return dma_setup_cbs(chan, tinfo);
}

static int dma_start(void* const handle)
{
    dma_channel_t* const chan = handle;
    const dma_ch_info_t* const ch_info = &chs_info[chan->chan_idx];
    uint32_t reg;

    if (atomic_set_value(&chan->status, DMACH_BUSY) & DMACH_BUSY) {
        errno = EBUSY;
        return -1;
    }

    /* Only three registers(CS, CB and DEBUG) in each channel's register set
     * are directly writable. The other registers (TI, SRC, DST, LEN, STRIDE & NEXTCB)
     * are automatically loaded from a Control Block data structure from external memory.
     */
    if (ch_info->is_dma4) {
        out32(chan->ch_regs + DMA4_CB, (uint32_t)(dma4_full_addr(chan->cb.paddr) >> 5));
        reg = (chan->xfer_type == MEMORY_TO_MEMORY) ?
                        (uint32_t)DMA4_CS_FLAGS_MEMCPY : (uint32_t)DMA4_CS_FLAGS_DEVICE;

        out32(chan->ch_regs + DMA4_CS, reg | DMA4_CS_ACTIVE);
    } else {
        out32(chan->ch_regs + DMA_CONBLK_AD, dma_legacy_addr((uint32_t)chan->cb.paddr));
        reg = (chan->xfer_type == MEMORY_TO_MEMORY) ?
                        (uint32_t)DMA_CS_FLAGS_MEMCPY : (uint32_t)DMA_CS_FLAGS_DEVICE;
        out32(chan->ch_regs + DMA_CS, reg | DMA_CS_ACTIVE);
    }

    return 0;
}

static unsigned bytes_left_dma(const dma_channel_t* const chan)
{
    const dma_ch_info_t* const ch_info = &chs_info[chan->chan_idx];
    uint32_t xfer_count   = 0;
    uint32_t bytes_nxtcbs = 0;
    const uint32_t len_mask = (ch_info->is_dmalite) ? 0xFFFF : 0x3FFFFFFF; // dmalite channel only has 16bit transfer length

    xfer_count = (in32(chan->ch_regs + DMA_DEBUG) & DMA_DEBUG_OUTSTANDING_WRITES) >> 4;
    if (chan->frgs_num) {
        const uint32_t cur_cb = in32(chan->ch_regs + DMA_CONBLK_AD);
        uint32_t next_frg = (uint32_t)(cur_cb - dma_legacy_addr((uint32_t)chan->cb.paddr)) / DMA_CB_SIZE;
        if (next_frg) {
            const dma_cb_t* const cb = chan->cb.vaddr;
            while (next_frg < chan->frgs_num) {
                bytes_nxtcbs += (cb[next_frg++].txfr_len & len_mask);
            }
        }
    }
    return (xfer_count * chan->xfer_unit_size + bytes_nxtcbs);
}

static unsigned bytes_left_dma4(const dma_channel_t* const chan)
{
    uint32_t xfer_count   = 0;
    uint32_t bytes_nxtcbs = 0;
    const uint32_t len_mask = 0x3FFFFFFF;

    xfer_count = in32(chan->ch_regs + DMA4_DEBUG2) & DMA4_DEBUG2_OUTSTANDING_WRITES_MSK;

    if (chan->frgs_num) {
        const off64_t cur_cb = ((off64_t)in32(chan->ch_regs + DMA4_CB) & (off64_t)UINT_MAX) << 5;
        uint32_t next_frg = (uint32_t)(cur_cb - (dma4_full_addr(chan->cb.paddr))) / DMA_CB_SIZE;
        if (next_frg) {
            const dma4_cb_t* const scb = chan->cb.vaddr;
            while (next_frg < chan->frgs_num) {
                bytes_nxtcbs += (scb[next_frg++].len & len_mask);
            }
        }
    }
    return (xfer_count * chan->xfer_unit_size + bytes_nxtcbs);
}

static unsigned dma_bytes_left(void* const handle)
{
    const dma_channel_t* const chan = handle;
    const dma_ch_info_t* const ch_info = &chs_info[chan->chan_idx];

    if (ch_info->is_dma4) {
        return bytes_left_dma4(chan);
    } else {
        return bytes_left_dma(chan);
    }
}

static int dma_abort(const dma_channel_t* const chan)
{
    int timeout = 10000;
    int ret = 0;

    /*
     * A zero control block address means the channel is idle.
     * (The ACTIVE flag in the CS register is not a reliable indicator.)
     */
    if (!in32(chan->ch_regs + DMA_CONBLK_AD)) {
        return 0;
    }

    /* Write 0 to the active bit - Pause the DMA */
    out32(chan->ch_regs + DMA_CS, 0);

    /* wait for the CS_ACTIVE bit to clear automatically as result of the abort */
    while ((in32(chan->ch_regs + DMA_CS) & DMA_CS_WAITING_FOR_WRITES) && timeout) {
        nanospin_ns(1000);
        timeout--;
    }

    if (!timeout) {
        bcm2711_dma_slogf(SLOG2_ERROR, "%s: failed to complete outstanding writes",
                          __FUNCTION__);
        errno = EIO;
        ret = -1;
    }

    out32(chan->ch_regs + DMA_CS, (uint32_t)DMA_CS_RESET);
    return ret;
}

static int dma4_abort(const dma_channel_t* const chan)
{
    int timeout = 1000;  /* 1ms */
    int ret = EOK;

    /*
     * A zero control block address means the channel is idle.
     * (The ACTIVE flag in the CS register is not a reliable indicator.)
     */
    if (!in32(chan->ch_regs + DMA4_CB)) {
        return 0;
    }

    /* Write 0 to the active bit - Pause the DMA */
    out32(chan->ch_regs + DMA4_CS, 0);

    /* wait for the CS_ACTIVE bit to clear automatically as result of the abort */
    while ((in32(chan->ch_regs + DMA4_CS) & DMA4_CS_WAITING_FOR_WRITES) && timeout) {
        nanospin_ns(1000);   /* 1us */
        timeout--;
    }

    if (!timeout) {
        bcm2711_dma_slogf(SLOG2_ERROR, "%s: failed to complete outstanding writes",
                          __FUNCTION__);
        errno = EIO;
        ret = -1;
    }

    out32(chan->ch_regs + DMA4_DEBUG, DMA4_DEBUG_RESET);
    return ret;
}

static int dma_xfer_abort(void* const handle)
{
    dma_channel_t* const chan = handle;
    const dma_ch_info_t* const ch_info = &chs_info[chan->chan_idx];
    int ret;

    if (ch_info->is_dma4) {
        ret = dma4_abort(chan);
    } else {
        ret = dma_abort(chan);
    }

    atomic_clr_value(&chan->status, DMACH_BUSY);
    return ret;
}

static int xfer_complete(void* const handle)
{
    dma_channel_t* const chan = handle;
    const dma_ch_info_t* const ch_info = &chs_info[chan->chan_idx];
    uint32_t cs, reg;
    uint32_t status = EOK;

    const unsigned prev_frgcount = atomic_sub_value(&chan->frg_count, 1);

    /* Clear the INT flag to receive further interrupts. Keep the channel
     * active in case the descriptor is cyclic or in case the client has
     * already terminated the descriptor and issued a new one. (May happen
     * if this IRQ handler is threaded.) If the channel is finished, it
     * will remain idle despite the ACTIVE flag being set.
     */
    if (ch_info->is_dma4) {
        cs = in32(chan->ch_regs + DMA4_CS);
        status = (cs & DMA4_CS_ERROR) ? (cs & DMA4_CS_ERROR) : EOK;

        reg = (chan->xfer_type == MEMORY_TO_MEMORY) ?
                        (uint32_t)DMA4_CS_FLAGS_MEMCPY : (uint32_t)DMA4_CS_FLAGS_DEVICE;
        reg |= DMA4_CS_INT | DMA4_CS_END | DMA4_CS_ACTIVE;
        out32(chan->ch_regs + DMA4_CS, reg);
    } else {
        cs = in32(chan->ch_regs + DMA_CS);
        status = (cs & DMA_CS_ERROR) ? (cs & DMA_CS_ERROR) : EOK;

        reg = (chan->xfer_type == MEMORY_TO_MEMORY) ?
                        (uint32_t)DMA_CS_FLAGS_MEMCPY : (uint32_t)DMA_CS_FLAGS_DEVICE;
        reg |= DMA_CS_INT | DMA_CS_END | DMA_CS_ACTIVE;
        out32(chan->ch_regs + DMA_CS, reg);
    }

    if (chan->iid != -1) {
        InterruptUnmask((int)ch_info->irq, chan->iid);
    }

    if (status != EOK) {
        bcm2711_dma_slogf(SLOG2_ERROR, "%s: error found in this transfer (0x%x)",
                            __FUNCTION__, status);
        if (chan->debug) {
            bcm2711_dmac_dump_registers(chan);
        }
        errno = EIO;
        goto abort;
    } else if (chan->aflags & DMA_ATTACH_EVENT_PER_SEGMENT) {
        /* abort the transfer if this is the last fragment */
        if (prev_frgcount == 1) {
            if (chan->debug) {
                bcm2711_dma_slogf(SLOG2_INFO, "%s: last fragment", __FUNCTION__);
            }
            goto abort;
        }
    } else if (!(chan->mflags & DMA_MODE_FLAG_REPEAT)) {
        goto abort;
    } else {
        /* cyclic mode */
    }

    return 0;

abort:
    dma_xfer_abort(handle);
    if (status != EOK) {
        return -1;
    } else {
        return 0;
    }
}

static void dma_query_channel(void* const handle, dma_channel_query_t *chinfo)
{
    const dma_channel_t* const chan = handle;
    const dma_ch_info_t* const ch_info = &chs_info[chan->chan_idx];

    chinfo->chan_idx = chan->chan_idx;
    chinfo->irq      = ch_info->irq;
}

static int dma_driver_info(dma_driver_info_t *info)
{
    info->dma_version_major = DMALIB_VERSION_MAJOR;
    info->dma_version_minor = DMALIB_VERSION_MINOR;
    info->dma_rev           = DMALIB_REVISION;
    info->lib_rev           = 0;
    info->description       = "BCM2711 DMAC Controller";
    info->num_channels      =  bcm2711_dmac.num_channels;
    info->max_priority      =  bcm2711_dmac.num_channels - 1;
    return 0;
}

static int dma_channel_info(const unsigned channel, dma_channel_info_t* const info)
{
    const dma_ch_info_t* const ch_info = &chs_info[channel];
    if (channel > bcm2711_dmac.num_channels) {
        errno = ECHRNG;
        return -1;
    }
    info->max_xfer_size         = (ch_info->is_dmalite) ? (uint32_t)MAX_LITE_DMA_LEN : (uint32_t)MAX_DMA_LEN;
    info->xfer_unit_sizes       = (ch_info->is_dma4) ?
                                          (uint32_t)DMA4_UNIT_SIZE_BITMAP : (uint32_t)DMA_UNIT_SIZE_BITMAP;
    info->max_src_fragments     = DMA_MAX_NUM_CBS;
    info->max_dst_fragments     = DMA_MAX_NUM_CBS;
    /* no support for segmented transfer */
    info->max_src_segments      = 0;
    info->max_dst_segments      = 0;

    /* segmented transfer is not supported, but fragmented transfer(scatter-gather) is supported  */
    info->caps                  = DMA_CAP_SRC_INCREMENT | DMA_CAP_SRC_DECREMENT |
                                  DMA_CAP_DST_INCREMENT | DMA_CAP_DST_DECREMENT |
                                  DMA_CAP_SRC_NO_INCREMENT | DMA_CAP_DST_NO_INCREMENT |
                                  DMA_CAP_DEVICE_TO_MEMORY | DMA_CAP_MEMORY_TO_DEVICE | DMA_CAP_MEMORY_TO_MEMORY |
                                  DMA_CAP_EVENT_ON_COMPLETE | DMA_CAP_EVENT_PER_SEGMENT|
                                  DMA_CAP_MEMORY_TO_MEMORY | DMA_CAP_DST_SCATTER_GATHER | DMA_CAP_SRC_SCATTER_GATHER;

    info->mem_lower_limit       = 0;
    info->mem_upper_limit       = 0x3fffffff;
    info->mem_nocross_boundary  = 0;

    return 0;
}

static void * dma_channel_attach(const char* const options, const struct sigevent* const event, unsigned* const channel,
                const int priority, const dma_attach_flags flags)
{
    dma_channel_t *chan;
    char *optstr = NULL;
    int chan_idx;
    uint32_t reg;
    int ret = EOK;

    /* As per dma api: these flags are mutually exclusive */
    if ((flags & DMA_ATTACH_EVENT_ON_COMPLETE) && (flags & DMA_ATTACH_EVENT_PER_SEGMENT)) {
        bcm2711_dma_slogf(SLOG2_ERROR, "%s: Invalid flags: 0x%x", __FUNCTION__, flags);
        errno = EINVAL;
        return NULL;
    }

    if (options) {
        optstr = strdup(options);
        if (optstr == NULL) {
            bcm2711_dma_slogf(SLOG2_ERROR, "%s: strdup failed", __FUNCTION__);
            return NULL;
        }
    }

    chan = calloc(1, sizeof(*chan));
    if (chan == NULL) {
        ret = errno;
        bcm2711_dma_slogf(SLOG2_ERROR, "%s: calloc failed", __FUNCTION__);
        goto fail0;
    }

    chan->range_min = -1;
    chan->range_max = -1;

    ret = parse_options(chan, optstr);
    if (ret != EOK) {
        bcm2711_dma_slogf(SLOG2_ERROR, "%s: parse options failed", __FUNCTION__);
        goto fail1;
    }

    /* Get a channel assigned from the resources database */
    chan_idx = rsrcdb_attach_channel(chan, channel, (uint64_t)priority, flags);
    if (chan_idx < 0) {
        ret = errno;
        goto fail1;
    }

    if (channel) {
        *channel = (unsigned)chan_idx;
    }

    const dma_ch_info_t* const ch_info = &chs_info[chan_idx];
    chan->chan_idx = (uint32_t)chan_idx;
    chan->ch_regs = bcm2711_dmac.vbase + ch_info->offset;

    pthread_mutex_t* const sync_mutex = dmac_sync_mutex_get();

    ret = pthread_mutex_lock(sync_mutex);
    if (ret != EOK) {
        goto fail1;
    }
    reg = in32(bcm2711_dmac.vbase + DMA_ENABLE) | (1 << chan_idx);
    out32(bcm2711_dmac.vbase + DMA_ENABLE, reg);
    pthread_mutex_unlock(sync_mutex);

    /* pause the channel */
    if (ch_info->is_dma4) {
        out32(chan->ch_regs + DMA4_CS, 0);
    } else {
        out32(chan->ch_regs + DMA_CS, 0);
    }

    chan->iid = -1;
    if ((flags & (DMA_ATTACH_EVENT_ON_COMPLETE | DMA_ATTACH_EVENT_PER_SEGMENT)) && (event != NULL)) {
        chan->iid = InterruptAttachEvent((int)ch_info->irq, event, _NTO_INTR_FLAGS_TRK_MSK);
        if (chan->iid == -1) {
            bcm2711_dma_slogf(SLOG2_ERROR, "%s: bcm2711 DMA chan %d interrupt attach failed: %s", __FUNCTION__,
                                chan->chan_idx, strerror(errno));
            ret = errno;
            goto fail2;
        }
    }

    chan->aflags = flags;
    if (optstr != NULL) {
        free(optstr);
    }
    return (chan);

fail2:
    rsrcdb_detach_resources(chan);
fail1:
    if (chan->typed_mem_fd != NOFD) {
        close(chan->typed_mem_fd);
    }
    free(chan);
fail0:
    free(optstr);
    errno = ret;
    return (NULL);
}

static void dma_channel_release(void* const handle)
{
    dma_channel_t* const chan = handle;
    const dma_ch_info_t* const ch_info = &chs_info[chan->chan_idx];
    uint32_t reg;

    /* pause the channel */
    if (ch_info->is_dma4) {
        out32(chan->ch_regs + DMA4_CS, 0);
    } else {
        out32(chan->ch_regs + DMA_CS, 0);
    }

    pthread_mutex_t* const sync_mutex = dmac_sync_mutex_get();
    pthread_mutex_lock(sync_mutex);
    /* disable the channel */
    reg = in32(bcm2711_dmac.vbase + DMA_ENABLE) & ~(1 << chan->chan_idx);
    out32(bcm2711_dmac.vbase + DMA_ENABLE, reg);
    pthread_mutex_unlock(sync_mutex);

    /* Release descriptor memory */
    if (chan->cb.len > 0) {
        dma_free_buffer(handle, &chan->cb);
    }

    /* detach interrupt */
    if (chan->iid != -1) {
        InterruptDetach(chan->iid);
    }

    /* release DMA channel resource */
    rsrcdb_detach_resources(chan);

    if (chan->typed_mem_fd != NOFD) {
        close(chan->typed_mem_fd);
        chan->typed_mem_fd = NOFD;
    }

    free(handle);
}

static void cleanup_resources(void)
{
    /* decrement the sync counter */
    pthread_mutex_t* const sync_mutex = dmac_sync_mutex_get();
    pthread_mutex_lock(sync_mutex);
    bcm2711_dmac_sync_process_cnt_decr();
    pthread_mutex_unlock(sync_mutex);
    /* release controller resource */
    munmap_device_io(bcm2711_dmac.vbase, bcm2711_dmac.regs_size);
    bcm2711_dmac_sync_dinit();
}

static int dma_init(const char* const options)
{
    int ret = EOK;

    pthread_mutex_lock(&procs_init_mutex);
    n_users_in_process++;

    // Only initialize once per process
    if (n_users_in_process == 1) {
        if (bcm2711_dma_slog2_init() != 0) {
            ret = errno;
            goto fail1;
        }
        if (bcm2711_dmac_sync_init() != 0) {
            ret = errno;
            goto fail1;
        }

        /* controllerspecific parameters */
        bcm2711_dmac.pbase = BCM2711_DMA0_BASE;
        bcm2711_dmac.regs_size = BCM2711_DMA_REGS_SIZE;
        bcm2711_dmac.num_channels = sizeof(chs_info) / sizeof(chs_info[0]);
        bcm2711_dmac.vbase = mmap_device_io(bcm2711_dmac.regs_size, bcm2711_dmac.pbase);
        if (bcm2711_dmac.vbase == (uintptr_t)MAP_FAILED) {
            ret = errno;
            goto fail2;
        }

        pthread_mutex_t* const sync_mutex = dmac_sync_mutex_get();
        ret = pthread_mutex_lock(sync_mutex);
        if (ret != EOK) {
            goto fail2;
        }
        bcm2711_dmac_sync_process_cnt_incr();
        if (bcm2711_dmac_sync_is_first_process() == 1) {
            /* disable all channels */
            out32(bcm2711_dmac.vbase + DMA_ENABLE, 0);
        }

        ret = rsrcdb_create_resources();
        if ((ret != EOK) && (ret != EALREADY)) {
            pthread_mutex_unlock(sync_mutex);
            goto fail3;
        }
        pthread_mutex_unlock(sync_mutex);
    }
    pthread_mutex_unlock(&procs_init_mutex);
    return 0;

fail3:
    munmap_device_io(bcm2711_dmac.vbase, bcm2711_dmac.regs_size);
fail2:
    bcm2711_dmac_sync_dinit();
fail1:
    n_users_in_process--;
    if (n_users_in_process == 0) {
        cleanup_resources();
    }
    pthread_mutex_unlock(&procs_init_mutex);
    errno = ret;
    return -1;
}

static void dma_fini(void)
{
    pthread_mutex_lock(&procs_init_mutex);
    n_users_in_process--;
    if (n_users_in_process == 0) {
        cleanup_resources();
    }
    pthread_mutex_unlock(&procs_init_mutex);
}

int get_dmafuncs(dma_functions_t *funcs, int tabsize)
{
    DMA_ADD_FUNC(funcs, init, dma_init, tabsize);
    DMA_ADD_FUNC(funcs, fini, dma_fini, tabsize);
    DMA_ADD_FUNC(funcs, driver_info, dma_driver_info, tabsize);
    DMA_ADD_FUNC(funcs, channel_info, dma_channel_info, tabsize);
    DMA_ADD_FUNC(funcs, channel_attach, dma_channel_attach, tabsize);
    DMA_ADD_FUNC(funcs, channel_release, dma_channel_release, tabsize);
    DMA_ADD_FUNC(funcs, setup_xfer, dma_setup_xfer, tabsize);
    DMA_ADD_FUNC(funcs, xfer_start, dma_start, tabsize);
    DMA_ADD_FUNC(funcs, xfer_abort, dma_xfer_abort, tabsize);
    DMA_ADD_FUNC(funcs, bytes_left, dma_bytes_left, tabsize);
    DMA_ADD_FUNC(funcs, xfer_complete, xfer_complete, tabsize);
    DMA_ADD_FUNC(funcs, alloc_buffer, dma_alloc_buffer, tabsize);
    DMA_ADD_FUNC(funcs, free_buffer, dma_free_buffer, tabsize);
    DMA_ADD_FUNC(funcs, query_channel, dma_query_channel, tabsize);
    return 0;
}
