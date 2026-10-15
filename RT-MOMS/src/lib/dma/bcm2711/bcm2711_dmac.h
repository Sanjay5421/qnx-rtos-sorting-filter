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

#ifndef __BCM2711_DMAC_H__
#define __BCM2711_DMAC_H__

#include <inttypes.h>
#include <sys/neutrino.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <hw/dma.h>

#define BCM2711_DMA_REGS_SIZE   0xff4
#define BCM2711_DMA_OFFSET_CH(x) ((x) << 8)
#define BCM2711_DMAC_NAME       "bcm2711dma"
#define DMA_DEFAULT_NUM_CBS     32
#define DMA_CB_SIZE             ((uint32_t)sizeof(dma_cb_t)) /* DMA4 and DMA have the same control block size */
#define MAX_DMA_LEN             (1024 * 1024 * 1024)
#define MAX_LITE_DMA_LEN        (64 * 1024 - 4)
#define DMA4_UNIT_SIZE_BITMAP   (0x4 | 0x8 | 0x10) /* 4bytes, 8bytes, 16bytes */
#define DMA_UNIT_SIZE_BITMAP    (0x4 | 0x10)       /* 4bytes,16 bytes */

#define DMA_MAX_NUM_CBS         4096  /* this will use 1MB memory for all 4096 descriptors */
#define DMA_MAX_DREQ_NUM        31  /* 0 ~31 */

/* hardware specific register descripton */
/* Global registers */
#define DMA_INT_STATUS          0xfe0
#define DMA_ENABLE              0xff0

/* DMA channel registers */
#define DMA_CS                  0x000
    /* DMA/DMALITE CS Register - Control and Status */
    #define DMA_CS_RESET                         (1 << 31) /* (w1sc) dma channel reset */
    #define DMA_CS_ABORT                         (1 << 30) /* (w1sc) abort dma transfer */
    #define DMA_CS_DISDEBUG                      (1 << 29) /* (rw) disable debug pause */
    #define DMA_CS_WAIT_FOR_WRITES               (1 << 28) /* (rw) wait for oustanding writes */
    #define DMA_CS_PANIC_PRIORITY(n)             (((n) & 0xf) << 20) /* (rw) axi panic priority level */
    #define DMA_CS_PANIC_PRIORITY_MSK            (DMA1_CS_PANIC_PRIORITY(15))
    #define DMA_CS_PRIORITY(n)                   (((n) & 0xf) << 16) /* (rw) axi priority level */
    #define DMA_CS_PRIORITY_MSK                  (DMA1_CS_PRIORITY(15))
    #define DMA_CS_ERROR                         (1 <<  8) /* (ro) dma error */
    #define DMA_CS_WAITING_FOR_WRITES            (1 <<  6) /* (ro) waiting for last write */
    #define DMA_CS_WAITING_FOR_DREQ              (1 <<  5) /* (ro) dma paused by dreq */
    #define DMA_CS_PAUSED                        (1 <<  4) /* (ro) dma paused */
    #define DMA_CS_DREQ                          (1 <<  3) /* (ro) dreq state */
    #define DMA_CS_INT                           (1 <<  2) /* (w1c) interrupt status */
    #define DMA_CS_END                           (1 <<  1) /* (w1c) dma end flag */
    #define DMA_CS_ACTIVE                        (1 <<  0) /* (rw) activate dma channel */
    #define DMA_CS_FLAGS_DEVICE                  (DMA_CS_PRIORITY(15) | \
                                                 DMA_CS_PANIC_PRIORITY(15) | \
                                                 DMA_CS_WAIT_FOR_WRITES)
    #define DMA_CS_FLAGS_MEMCPY                  (DMA_CS_PRIORITY(0) | \
                                                 DMA_CS_PANIC_PRIORITY(0) | \
                                                 DMA_CS_WAIT_FOR_WRITES)
#define DMA_CONBLK_AD           0x004
#define DMA_TI                  0x008
    /* DMA/DMALITE TI Register - Transfer Information */
    #define DMA_TI_NO_WIDE_BURSTS                (1 << 26) /* (rw) don't do wide writes as 2-beat burst */
    #define DMA_TI_WAITS(n)                      ((n) << 21) /* (rw) add wait cycles */
    #define DMA_TI_WAITS_MSK                     (DMA_TI_WAITS(0x1f))
    #define DMA_TI_PERMAP(n)                     ((n) << 16) /* (rw) dreq pacing */
    #define DMA_TI_PERMAP_MSK                    (DMA_TI_PERMAP(0x1f))
    #define DMA_TI_BURST(n)                      ((min((n),16) - 1) << 12) /* (rw) burst transfer length in words */
    #define DMA_TI_BURST_MSK                     (DMA_TI_BURST(16))
    #define DMA_TI_SRC_IGNORE                    (1 << 11) /* (rw) ignore reads */
    #define DMA_TI_SRC_DREQ                      (1 << 10) /* (rw) dreq paces reads */
    #define DMA_TI_SRC_WIDTH_128                 (1 <<  9) /* (rw) 128-bit source read flag */
    #define DMA_TI_SRC_WIDTH_32                  (0 <<  9) /* (rw) 32-bit source read flag */
    #define DMA_TI_SRC_INC                       (1 <<  8) /* (rw) source address increment */
    #define DMA_TI_DST_IGNORE                    (1 <<  7) /* (rw) ignore writes */
    #define DMA_TI_DST_DREQ                      (1 <<  6) /* (rw) dreq paces writes */
    #define DMA_TI_DST_WIDTH_128                 (1 <<  5) /* (rw) 128-bit destination write flag */
    #define DMA_TI_DST_WIDTH_32                  (0 <<  5) /* (rw) 32-bit destination write flag */
    #define DMA_TI_DST_INC                       (1 <<  4) /* (rw) destination address increment */
    #define DMA_TI_WAIT_RESP                     (1 <<  3) /* (rw) wait for write responses */
    #define DMA_TI_2DMODE                        (1 <<  1) /* (rw) 2D mode */
    #define DMA_TI_INTEN                         (1 <<  0) /* (rw) interrupt enable for current cb */

#define DMA_SRC_AD              0x00C
#define DMA_DST_AD              0x010
#define DMA_TXFR_LEN            0x014
#define DMA_STRIDE              0x018
#define DMA_NXTCONBK            0x01C
#define DMA_DEBUG               0x020
    /* DMA/DMALITE DEBUG Register */
    #define DMA_DEBUG_LITE                       (1 << 28) /* (ro) dma lite flag */
    #define DMA_DEBUG_VERSION                    (7 << 25) /* (ro) dma version */
    #define DMA_DEBUG_DMA_STATE                  (0x1FF << 16) /* (ro) dma state machine state */
    #define DMA_DEBUG_DMA_ID                     (0xFF << 8) /* (ro) dma axi id */
    #define DMA_DEBUG_OUTSTANDING_WRITES         (0xF << 4) /* (ro) dma outstanding write counter */
    #define DMA_DEBUG_READ_ERROR                 (1 <<  2) /* (rw) slave read response error */
    #define DMA_DEBUG_FIFO_ERROR                 (1 <<  1) /* (rw) fifo error */
    #define DMA_DEBUG_READ_LAST_NOT_SET_ERROR    (1 <<  0) /* (rw) read last not set error */


/* DMA40 channel registers */
#define DMA4_CS                0x000
    #define DMA4_CS_HALT                        (1 << 31) /* (w1sc) dma halt */
    #define DMA4_CS_ABORT                       (1 << 30) /* (w1sc) abort dma transfer */
    #define DMA4_CS_DISDEBUG                    (1 << 29) /* (rw) disable debug pause */
    #define DMA4_CS_WAIT_FOR_WRITES             (1 << 28) /* (rw) wait for oustanding writes */
    #define DMA4_CS_OUTSTANDING_TRANS           (1 << 25) /* (ro) transfers oustanding */
    #define DMA4_CS_BUSY                        (1 << 24) /* (ro) dma busy */
    #define DMA4_CS_PANIC_QOS(n)                (((n) & 0xf) << 20) /* (rw) AXI Panic QOS Level */
    #define DMA4_CS_PANIC_QOS_MSK               (DMA4_CS_PANIC_QOS(15))
    #define DMA4_CS_QOS(n)                      (((n) & 0xf) << 16) /* (rw) AXI QOS Level */
    #define DMA4_CS_QOS_MSK                     (DMA4_CS_QOS(15))
    #define DMA4_CS_ERROR                       (1 << 10) /* (ro) dma error */
    #define DMA4_CS_WAITING_FOR_WRITES          (1 <<  7) /* (ro) waiting for last write */
    #define DMA4_CS_WAITING_FOR_DREQ            (1 <<  6) /* (ro) dma paused by dreq */
    #define DMA4_CS_WR_PAUSED                   (1 <<  5) /* (ro) dma paused */
    #define DMA4_CS_RD_PAUSED                   (1 <<  4) /* (ro) dma paused */
    #define DMA4_CS_DREQ                        (1 <<  3) /* (ro) dreq state */
    #define DMA4_CS_INT                         (1 <<  2) /* (w1c) interrupt status */
    #define DMA4_CS_END                         (1 <<  1) /* (w1c) dma end flag */
    #define DMA4_CS_ACTIVE                      (1 <<  0) /* (rw) activate dma channel */
    #define DMA4_CS_FLAGS_DEVICE                (DMA4_CS_QOS(15) | \
                                                DMA4_CS_PANIC_QOS(15) | \
                                                DMA4_CS_WAIT_FOR_WRITES)
    #define DMA4_CS_FLAGS_MEMCPY                (DMA4_CS_QOS(0) | \
                                                DMA4_CS_PANIC_QOS(0) | \
                                                DMA4_CS_WAIT_FOR_WRITES)
#define DMA4_CB                0x004
#define DMA4_DEBUG             0x00C
/* DMA4 DEBUG Register - Debug */
    #define DMA4_DEBUG_VERSION(n)               ((n) << 28) /* (ro) DMA Version */
    #define DMA4_DEBUG_VERSION_MSK              (DMA4_DEBUG_VERSION(15))
    #define DMA4_DEBUG_ID(n)                    ((n) << 24) /* (ro) dma axi id */
    #define DMA4_DEBUG_ID_MSK                   (DMA4_DEBUG_ID(15))
    #define DMA4_DEBUG_RESET                    (1 << 23)   /* (w1sc) dma reset */
    #define DMA4_DEBUG_W_STATE(n)               ((n) << 18) /* (ro) Write State Machine State */
    #define DMA4_DEBUG_W_STATE_MSK              (DMA4_DEBUG_W_STATE(15))
        /* Returns the value of the DMA4 engine's write state machine.
        * 0 = W_IDLE, 1 = W_PRELOAD, 2 = W_CALC, 3 = W_WRITE4K, 4 = W_READFIFO_EMPTY, 5 = W_WAIT_OUSTANDING
        */
    #define DMA4_DEBUG_R_STATE(n)               ((n) << 14) /* (ro) Read State Machine State */
    #define DMA4_DEBUG_R_STATE_MSK              (DMA4_DEBUG_R_STATE(15))
        /* Returns the value of the DMA4 engine's read state machine.
        * 0 = R_IDLE, 1 = R_WAIT_CB_DATA, 2 = R_CALC, 3 = R_READ4K, 4 = R_READING, 5 = R_READFIFO_FULL, 6 = R_WAIT_WRITE_COMPLETE
        */
    #define DMA4_DEBUG_DISABLE_CLK_GATE         (1 << 11) /* (rw) disable clock gating logic */
    #define DMA4_DEBUG_ABORT_ON_ERROR           (1 << 10) /* (rw) abort on error */
    #define DMA4_DEBUG_HALT_ON_ERROR            (1 <<  9) /* (rw) halt on error */
    #define DMA4_DEBUG_INT_ON_ERROR             (1 <<  8) /* (rw) interrupt on error */
    #define DMA4_DEBUG_READ_CB_ERROR            (1 <<  3) /* (rc) error reading cb */
    #define DMA4_DEBUG_READ_ERROR               (1 <<  2) /* (rc) error during read */
    #define DMA4_DEBUG_FIFO_ERROR               (1 <<  1) /* (rc) fifo error */
    #define DMA4_DEBUG_WRITE_ERROR              (1 <<  0) /* (rc) error during write */
#define DMA4_TI                0x010
    /* DMA4 TI Register - Transfer Information */
    #define DMA4_TI_D_WAITS(n)                  ((n) << 24) /* (rw) Write Wait Cycles */
    #define DMA4_TI_D_WAITS_MSK                 (DMA4_TI_D_WAITS(255))
        /* This slows down the DMA throughput by setting the number of dummy cycles before each AXI Write operation is started.
        * A value of 0 means that no wait cycles are to be added. Waits are counted down when the DMA4 wants to do a wait and
        * the bus is available and the writes aren't paused because of DREQS or some other reason.
        */
    #define DMA4_TI_S_WAITS(n)                  ((n) << 16)     /* (rw) Read Wait Cycles */
    #define DMA4_TI_S_WAITS_MSK                 (DMA4_TI_S_WAITS(255))
        /* This slows down the DMA throughput by setting the number of dummy cycles burnt before each DMA AXI read operation is started.
        * A value of 0 means that no wait cycles are to be added. Waits are counted down when the DMA4 wants to do a read and
        * the bus is available and the reads aren't paused because of DREQS or some other reason.
        */
    #define DMA4_TI_D_DREQ                      (1 << 15) /* (rw) dreq paces writes */
    #define DMA4_TI_S_DREQ                      (1 << 14) /* (rw) dreq paces reads */
    #define DMA4_TI_PERMAP(n)                   ((n) << 9) /* (rw) Peripheral Mapping */
    #define DMA4_TI_PERMAP_MSK                  (DMA4_TI_PERMAP(0x1F))
    #define DMA4_TI_WAIT_RD_RESP                (1 <<  3) /* (rw) wait for read responses */
    #define DMA4_TI_WAIT_WR_RESP                (1 <<  2) /* (rw) wait for write responses */
    #define DMA4_TI_2DMODE                      (1 <<  1) /* (rw) 2D mode */
    #define DMA4_TI_INTEN                       (1 <<  0) /* (rw) interrupt enable for current cb */
#define DMA4_SRC               0x014
#define DMA4_SRCI              0x018
#define DMA4_DST               0x01C
#define DMA4_DSTI              0x020
    #define DMA4_STRIDE(x)                      ((x) << 16) /* For 2D mode */
    #define DMA4_IGNORE                         (1 << 15)
    #define DMA4_SIZE_32                        (0 << 13)
    #define DMA4_SIZE_64                        (1 << 13)
    #define DMA4_SIZE_128                       (2 << 13)
    #define DMA4_SIZE_256                       (3 << 13)
    #define DMA4_INC                            (1 << 12)
    #define DMA4_BURST_LEN(x)	                ((min((x),16) - 1) << 8)
#define DMA4_LEN               0x024
#define DMA4_NEXT_CB           0x028
#define DMA4_DEBUG2            0x02C
/* DMA4 DEBUG2 Register - More Debug */
    #define DMA4_DEBUG2_OUTSTANDING_READS(n)    ((n) << 16)     /* (ro) Outstanding Read Words Count */
    #define DMA4_DEBUG2_OUTSTANDING_READS_MSK   (DMA4_DEBUG2_OUTSTANDING_READS(0x1ff))
        /* This keeps count of the number of read words that have been requested and the number that have actually been returned.
        * This should be zero at the end of every transfer
        */
    /* 15:09 # Reserved - Write as 0, read as don't care */
    #define DMA4_DEBUG2_OUTSTANDING_WRITES(n)   ((n) << 0)      /* (ro) Outstanding Write Response Count */
    #define DMA4_DEBUG2_OUTSTANDING_WRITES_MSK  (DMA4_DEBUG2_OUTSTANDING_WRITES(0x1ff))
        /* This keeps count of the number of write requests that have been generated and the number of responses that have been returned.
        * This should be zero at the end of every transfer
        */

typedef enum {
     IO_TO_IO,
     IO_TO_MEMORY,
     IO_TO_DEVICE,
     MEMORY_TO_IO,
     MEMORY_TO_MEMORY,
     MEMORY_TO_DEVICE,
     DEVICE_TO_IO,
     DEVICE_TO_MEMORY,
     DEVICE_TO_DEVICE,
     UNKNOWN_XFER_TYPE,
} dma_xfer_types_t;

typedef struct {
    paddr_t           pbase;
    uintptr_t         vbase;
    uint32_t          num_channels;
    uint32_t          regs_size;
} bcm2711_dmac_ctrl_t;

typedef struct dma_cb_params {
#define WAIT_WR_RESP      (1 << 6)
#define WAIT_RD_RESP      (1 << 5)
#define SRC_INC           (1 << 4)
#define DST_INC           (1 << 3)
#define SRC_DREQ          (1 << 2)
#define DST_DREQ          (1 << 1)
#define INT_EN            (1 << 0)
    uint32_t info;        // transfer information
    uint32_t len;         // transfer len
    uint32_t burst_len;   // burst_len
    uint32_t dreq;        // dma request signal number
    off64_t src_paddr;    // source address
    off64_t dst_paddr;    // destination address
    off64_t nextcb;       // next control block address
} dma_cb_params_t;

typedef struct
{
    uint32_t        chan_idx;
#define DMACH_BUSY     (1 << 0)
#define DMACH_ERROR    (1 << 1)
    unsigned        status;
    uintptr_t       ch_regs;
    uint32_t        xfer_unit_size;
    int             iid;
    int             typed_mem_fd;
    int16_t         range_min;  // Channel range start
    int16_t         range_max;  // Channel range end
    uint32_t        num_cbs;    // number of control blocks
    uint32_t        frgs_num;   // number of fragements
    uint32_t        frg_count;  // fragment count used by xfer_complete
    uint32_t        debug;
    dma_mode_flags  mflags;     // mode flag of current transfer
    dma_attach_flags aflags;    // attach flag of client
    dma_addr_t      cb;
    dma_cb_params_t cb_params;  // control block parameters
    dma_xfer_types_t  xfer_type;
} dma_channel_t;

/* DMA Channel Control Block for DMA/DMALITE Channels (0-10) */
typedef struct dma_cb {
    uint32_t ti;            /* 0x00 Transfer Information */
    uint32_t source_ad;     /* 0x04 Source Address */
    uint32_t dest_ad;       /* 0x08 Destination Address */
    uint32_t txfr_len;      /* 0x0c Transfer Length */
    uint32_t stride;        /* 0x10 2D Mode Stride */
    uint32_t nextconbk;     /* 0x14 Next Control Block Address */
    uint32_t rsvd[2];       /* 0x18 and 0x 1c Reserved (set to zero) */
} dma_cb_t;

/* DMA Channel Control Block for DMA4 Channels (11-14) */
typedef struct dma4_cb {
    uint32_t ti;            /* 0x00 Transfer Information */
    uint32_t src;           /* 0x04 Source Address Low */
    uint32_t srci;          /* 0x08 Source Address High */
    uint32_t dest;          /* 0x0c Destination Address Low */
    uint32_t desti;         /* 0x10 Destination Address High */
    uint32_t len;           /* 0x14 Transfer Length */
    uint32_t next_cb;       /* 0x18 Next Control Block Address */
    uint32_t rsvd;          /* 0x1c Reserved (set to zero) */
} dma4_cb_t;

#define RESERVED_INTERNAL_API __attribute__(( visibility ("hidden") ))

/* prototypes */
extern RESERVED_INTERNAL_API int bcm2711_dma_slog2_init(void);
extern RESERVED_INTERNAL_API int bcm2711_dma_slogf(const unsigned char severity, const char * const fmt, ...) __attribute__ ((format(printf, 2, 3)));

/* sync apis */
extern RESERVED_INTERNAL_API int bcm2711_dmac_sync_init(void);
extern RESERVED_INTERNAL_API void bcm2711_dmac_sync_dinit(void);
extern RESERVED_INTERNAL_API pthread_mutex_t * dmac_sync_mutex_get(void);
extern RESERVED_INTERNAL_API int bcm2711_dmac_sync_is_first_process(void);
extern RESERVED_INTERNAL_API void bcm2711_dmac_sync_process_cnt_incr(void);
extern RESERVED_INTERNAL_API void bcm2711_dmac_sync_process_cnt_decr(void);

#endif /* #ifndef __RCAR_SYSDMAC_H__ */
