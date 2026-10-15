/*
 * Copyright (c) 2020, 2022, BlackBerry Limited.
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

/*
 * Broadcom BCM2711 SOC:
 * BCM2711 - 4x1 (1 core per cluster) Cortex-A72 based SOC
 */

#ifndef	__AARCH64_BCM2711_H_INCLUDED
#define	__AARCH64_BCM2711_H_INCLUDED


/*
 * System Memory Map
 */
//define BCM2711_PERIPH_BASE                0xfc000000
#define BCM2711_CORE_FREQ                   500000000               // 500MHz (default)


/*
 */
#define BCM2711_MBX_IRQ(n)                  (0x20+(n)) // 16
#define BCM2711_PMU_IRQ(n)                  (0x30+(n)) // 4
#define BCM2711_PER_IRQ(n)                  (0x40+(n)) // 16
#define BCM2711_VPU_IRQ(n)                  (0x60+(n)) // 64
#define BCM2711_ETH_PCI_IRQ(n)              (0xa0+(n)) // 57

/*
 * PCIE
 */
#define BCM2711_PCIE_BASE                   0xfd500000
#define BCM2711_PCIE_SIZE                   0x9400
#define BCM2711_PCIE_IRQ                    (96+58)
#define BCM2711_PCIE_INTA                   (160+15)
#define BCM2711_PCIE_INTB                   (160+16)
#define BCM2711_PCIE_INTC                   (160+17)
#define BCM2711_PCIE_INTD                   (160+18)
#define BCM2711_PCIE_MSI                    (160+20)

#define BCM2711_PCIE_OBCPU_ADDR             0x600000000ul   // outbound window
#define BCM2711_PCIE_OBPCI_ADDR             0x0fc000000ul   // bridge memory
#define BCM2711_PCIE_OB_SIZE                (32ul << 20)

#define BCM2711_PCIE_IBCPU_ADDR             0x000000000ul   // inbound window
#define BCM2711_PCIE_IBPCI_ADDR             0x000000000ul
#define BCM2711_PCIE_IB_SIZE                (4ul << 30)

#if BCM2711_PCIE_IB_SIZE == (4ul << 30)
#define BCM2711_PCIE_MSI_TARGET             0xffffffffcul
#else
#define BCM2711_PCIE_MSI_TARGET             0x0fffffffcul
#endif

#define BCM2711_PCIE_REG_ID                 0x043c
#define BCM2711_PCIE_REG_MISC_CTRL          0x4008
#define BCM2711_PCIE_REG_MEM_PCI_LO         0x400c  // win0 @ 0x0c, win1 @ 0x14, win2 @ 0x1c, win3 @ 0x24
#define BCM2711_PCIE_REG_MEM_PCI_HI         0x4010  // win0 @ 0x10, win1 @ 0x18, win2 @ 0x20, win3 @ 0x28
#define BCM2711_PCIE_RC_BAR1_CONFIG_LO      0x402c
#define BCM2711_PCIE_RC_BAR1_CONFIG_HI      0x4030
#define BCM2711_PCIE_RC_BAR2_CONFIG_LO      0x4034
#define BCM2711_PCIE_RC_BAR2_CONFIG_HI      0x4038
#define BCM2711_PCIE_RC_BAR3_CONFIG_LO      0x403c
#define BCM2711_PCIE_RC_BAR3_CONFIG_HI      0x4040
#define BCM2711_PCIE_MSI_BAR_CONFIG_LO      0x4044
#define BCM2711_PCIE_MSI_BAR_CONFIG_HI      0x4048
#define BCM2711_PCIE_MSI_DATA_CONFIG        0x404c
#define BCM2711_PCIE_MISC_EOI_CTRL          0x4060
#define BCM2711_PCIE_MISC_PCIE_CTRL         0x4064
#define BCM2711_PCIE_REG_STATUS             0x4068
#define BCM2711_PCIE_REG_REV                0x406c
#define BCM2711_PCIE_REG_MEM_CPU_LO         0x4070  // win0 @ 0x70, win1 @ 0x74, win2 @ 0x78, win3 @ 0x7c
#define BCM2711_PCIE_REG_MEM_CPU_HI_START   0x4080  // win0 @ 0x80, win1 @ 0x88, win2 @ 0x90, win3 @ 0x98
#define BCM2711_PCIE_REG_MEM_CPU_HI_END     0x4084  // win0 @ 0x84, win1 @ 0x8c, win2 @ 0x94, win3 @ 0x9c
#define BCM2711_PCIE_REG_DEBUG              0x4204
#define BCM2711_PCIE_INT_STATUS             0x4300
#define BCM2711_PCIE_INT_SET                0x4304
#define BCM2711_PCIE_INT_CLEAR              0x4308
#define BCM2711_PCIE_INTMASK_STATUS         0x430c
#define BCM2711_PCIE_INTMASK_SET            0x4310
#define BCM2711_PCIE_INTMASK_CLEAR          0x4314
#define BCM2711_PCIE_MSI_STATUS             0x4500
#define BCM2711_PCIE_MSI_SET                0x4504
#define BCM2711_PCIE_MSI_CLEAR              0x4508
#define BCM2711_PCIE_MSIMASK_STATUS         0x450c
#define BCM2711_PCIE_MSIMASK_SET            0x4510
#define BCM2711_PCIE_MSIMASK_CLEAR          0x4514
#define BCM2711_PCIE_REG_CFG_INDEX          0x9000
#define BCM2711_PCIE_REG_INIT               0x9210

/*
 * 10/100/1000-Mbps Ethernet MAC (ENET) Controller
 */
#define BCM2711_ENET_BASE                   0xfd580000
#define BCM2711_ENET_SIZE                   0x8000
//define BCM2711_ENET_IRQ                   (96+58)
#define BCM2711_ENET_IRQA                   (160+29)
#define BCM2711_ENET_IRQB                   (160+30)

/*
 * DMA
 */
#define BCM2711_DMA0_BASE                   0xfe007000
#define BCM2711_DMA0_IRQ                    (96+16)
#define BCM2711_DMA1_BASE                   0xfe007100
#define BCM2711_DMA1_IRQ                    (96+17)
#define BCM2711_DMA2_BASE                   0xfe007200
#define BCM2711_DMA2_IRQ                    (96+18)
#define BCM2711_DMA3_BASE                   0xfe007300
#define BCM2711_DMA3_IRQ                    (96+19)
#define BCM2711_DMA4_BASE                   0xfe007400
#define BCM2711_DMA4_IRQ                    (96+20)
#define BCM2711_DMA5_BASE                   0xfe007500
#define BCM2711_DMA5_IRQ                    (96+21)
#define BCM2711_DMA6_BASE                   0xfe007600
#define BCM2711_DMA6_IRQ                    (96+22)
#define BCM2711_DMA7_BASE                   0xfe007700  // LITE
#define BCM2711_DMA7_IRQ                    (96+23)
#define BCM2711_DMA8_BASE                   0xfe007800  // LITE
#define BCM2711_DMA8_IRQ                    (BCM2711_DMA7_IRQ)
#define BCM2711_DMA9_BASE                   0xfe007900  // LITE
#define BCM2711_DMA9_IRQ                    (96+24)
#define BCM2711_DMA10_BASE                  0xfe007a00  // LITE
#define BCM2711_DMA10_IRQ                   (BCM2711_DMA9_IRQ)
#define BCM2711_DMA11_BASE                  0xfe007b00  // DMA4
#define BCM2711_DMA11_IRQ                   (96+25)
#define BCM2711_DMA12_BASE                  0xfe007c00  // DMA4
#define BCM2711_DMA12_IRQ                   (96+26)
#define BCM2711_DMA13_BASE                  0xfe007d00  // DMA4
#define BCM2711_DMA13_IRQ                   (96+27)
#define BCM2711_DMA14_BASE                  0xfe007e00  // DMA4
#define BCM2711_DMA14_IRQ                   (96+28)

#define BCM2711_DMA_INT_STATUS              0xfe007fe0
#define BCM2711_DMA_INT_ENABLE              0xfe007ff0
#define BCM2711_DMA_SIZE                    0x100

/*
 * PM module
 */
#define BCM2711_PM_BASE                     0xfe100000
#define BCM2711_PM_SIZE                     0x114

#define BCM2711_PM_RSTC                     (0x1c)
#define BCM2711_PM_RSTS                     (0x20)
#define BCM2711_PM_WDOG                     (0x24)

/*
 * Clock module
 */
#define BCM2711_CM_BASE                     0xfe101000
#define BCM2711_CM_SIZE                     0x100

#define BCM2711_CM_GP0                      (14)
#define BCM2711_CM_GP1                      (15)
#define BCM2711_CM_GP2                      (16)
#define	BCM2711_CM_PCM                      (19)
#define	BCM2711_CM_PWM                      (20)

/*
 * GPIO
 */
#define BCM2711_GPIO_BASE                   0xfe200000
#define BCM2711_GPIO_SIZE                   0x100
#define BCM2711_GPIO_FSEL0                  (0x00)
#define BCM2711_GPIO_FSEL1                  (0x04)
#define BCM2711_GPIO_FSEL2                  (0x08)
#define BCM2711_GPIO_FSEL3                  (0x0c)
#define BCM2711_GPIO_FSEL4                  (0x10)
#define BCM2711_GPIO_FSEL5                  (0x14)
#define BCM2711_GPIO_SET0                   (0x1c)
#define BCM2711_GPIO_SET1                   (0x20)
#define BCM2711_GPIO_CLR0                   (0x28)
#define BCM2711_GPIO_CLR1                   (0x2c)
#define BCM2711_GPIO_LEV0                   (0x34)
#define BCM2711_GPIO_LEV1                   (0x38)
#define BCM2711_GPIO_EDS0                   (0x40)
#define BCM2711_GPIO_EDS1                   (0x44)
#define BCM2711_GPIO_EN0                    (0x4c)
#define BCM2711_GPIO_EN1                    (0x50)
#define BCM2711_GPIO_HEN0                   (0x64)
#define BCM2711_GPIO_HEN1                   (0x68)
#define BCM2711_GPIO_LEN0                   (0x70)
#define BCM2711_GPIO_LEN1                   (0x74)
#define BCM2711_GPIO_AREN0                  (0x7c)
#define BCM2711_GPIO_AREN1                  (0x80)
#define BCM2711_GPIO_AFEN0                  (0x88)
#define BCM2711_GPIO_AFEN1                  (0x8c)
#define BCM2711_GPIO_PULL0                  (0xe4)
#define BCM2711_GPIO_PULL1                  (0xe8)
#define BCM2711_GPIO_PULL2                  (0xec)
#define BCM2711_GPIO_PULL3                  (0xf0)

#define BCM2711_GPIO_IRQ0                   (96+49)
#define BCM2711_GPIO_IRQ1                   (96+50)
#define BCM2711_GPIO_IRQ2                   (96+51)
#define BCM2711_GPIO_IRQ3                   (96+52)

/*
 * UART Controllers
 */
#define BCM2711_UART0_BASE                  0xfe201000
#define BCM2711_UART0_IRQ                   (96+57)
#define BCM2711_UART1_BASE                  0xfe215000
#define BCM2711_UART1_IRQ                   (96+29)
#define BCM2711_UART2_BASE                  0xfe201400
#define BCM2711_UART2_IRQ                   (96+57)
#define BCM2711_UART3_BASE                  0xfe201600
#define BCM2711_UART3_IRQ                   (96+57)
#define BCM2711_UART4_BASE                  0xfe201800
#define BCM2711_UART4_IRQ                   (96+57)
#define BCM2711_UART5_BASE                  0xfe201a00
#define BCM2711_UART5_IRQ                   (96+57)

#define BCM2711_UART_SIZE                   0x100

/*
 * EMMC
 */
#define BCM2711_SDHOST_BASE                 0xfe202000
#define BCM2711_SDHOST_IRQ                  (96+56)
#define BCM2711_MMC1_BASE                   0xfe300000
#define BCM2711_MMC1_IRQ                    (96+62)
#define BCM2711_MMC2_BASE                   0xfe340000
#define BCM2711_MMC2_IRQ                    (96+62)
#define BCM2711_MMC_SIZE                    0x1000

/*
 * PCM
 */
#define BCM2711_PCM_BASE                    0xfe203000
#define BCM2711_PCM_IRQ                     (96+55)
#define BCM2711_PCM_SIZE                    0x100

/*
 * SPI
 */
#define BCM2711_SPI0_BASE                   0xfe204000
#define BCM2711_SPI0_IRQ                    (96+54)
#define BCM2711_SPI1_BASE                   0xfe215000
#define BCM2711_SPI1_IRQ                    (96+54)
#define BCM2711_SPI2_BASE                   0xfe215000
#define BCM2711_SPI2_IRQ                    (96+54)
#define BCM2711_SPI3_BASE                   0xfe204600
#define BCM2711_SPI3_IRQ                    (96+54)
#define BCM2711_SPI4_BASE                   0xfe204800
#define BCM2711_SPI4_IRQ                    (96+54)
#define BCM2711_SPI5_BASE                   0xfe204a00
#define BCM2711_SPI5_IRQ                    (96+54)
#define BCM2711_SPI6_BASE                   0xfe204c00
#define BCM2711_SPI6_IRQ                    (96+54)
#define BCM2711_SPI_SIZE                    0x20

/*
 * PACTL
 */
#define BCM2711_PACTL_BASE                  0xfe204e00
#define BCM2711_PACTL_SPI0IRQ               (1<<0)
#define BCM2711_PACTL_SPI1IRQ               (1<<1)
#define BCM2711_PACTL_SPI2IRQ               (1<<2)
#define BCM2711_PACTL_SPI3IRQ               (1<<3)
#define BCM2711_PACTL_SPI4IRQ               (1<<4)
#define BCM2711_PACTL_SPI5IRQ               (1<<5)
#define BCM2711_PACTL_SPI6IRQ               (1<<6)
#define BCM2711_PACTL_I2C0IRQ               (1<<8)
#define BCM2711_PACTL_I2C1IRQ               (1<<9)
#define BCM2711_PACTL_I2C2IRQ               (1<<10)
#define BCM2711_PACTL_I2C3IRQ               (1<<11)
#define BCM2711_PACTL_I2C4IRQ               (1<<12)
#define BCM2711_PACTL_I2C5IRQ               (1<<13)
#define BCM2711_PACTL_I2C6IRQ               (1<<14)
#define BCM2711_PACTL_I2C7IRQ               (1<<15)
#define BCM2711_PACTL_UART5IRQ              (1<<16)
#define BCM2711_PACTL_UART4IRQ              (1<<17)
#define BCM2711_PACTL_UART3IRQ              (1<<18)
#define BCM2711_PACTL_UART2IRQ              (1<<19)
#define BCM2711_PACTL_UART0IRQ              (1<<20)

/*
 * I2C Controllers
 */
#define BCM2711_I2C0_BASE                   0xfe205000
#define BCM2711_I2C0_IRQ                    (96+53)
#define BCM2711_I2C1_BASE                   0xfe804000
#define BCM2711_I2C1_IRQ                    (96+53)
#define BCM2711_I2C2_BASE                   0xfe214000 /*slave*/
#define BCM2711_I2C2_IRQ                    (96+53)
#define BCM2711_I2C3_BASE                   0xfe205600
#define BCM2711_I2C3_IRQ                    (96+53)
#define BCM2711_I2C4_BASE                   0xfe205800
#define BCM2711_I2C4_IRQ                    (96+53)
#define BCM2711_I2C5_BASE                   0xfe205a80
#define BCM2711_I2C5_IRQ                    (96+53)
#define BCM2711_I2C6_BASE                   0xfe205c00
#define BCM2711_I2C6_IRQ                    (96+53)
#define BCM2711_I2C_SIZE                    0x20

/*
 * PWM
 */
#define BCM2711_PWM0_BASE                   0xfe20c000
#define BCM2711_PWM0_DMA                    5
#define BCM2711_PWM1_BASE                   0xfe20c800
#define BCM2711_PWM1_DMA                    1
#define BCM2711_PWM_SIZE                    0x20

/*
 * USB (old usb controller)
 */
#define BCM2711_USB_BASE                    0xfe980000
#define BCM2711_USB_SIZE                    0x8000
#define BCM2711_USB_IRQ                     (96+9)
#define BCM2711_XHCI_BASE                   0xfe9c0000
#define BCM2711_XHCI_SIZE                   0x8000
#define BCM2711_XHCI_IRQ                    (160+48)

/*
 * DMA DREQ
 */
#define DREQ_NONE               0               // no DREQ is required
#define DREQ_DSI0               1               // DSI0 / PWM1 (see DMA_CNTRL_MUX bits in PACTL_CS)
#define DREQ_PWM1               1               // DSI0 / PWM1 (see DMA_CNTRL_MUX bits in PACTL_CS)
#define DREQ_PCMTX              2               // PCM TX
#define DREQ_PCMRX              3               // PCM RX
#define DREQ_SMI                4               // SMI
#define DREQ_PWM0               5               // PWM0
#define DREQ_SPI0TX             6               // SPI0 TX
#define DREQ_SPI0RX             7               // SPI0 RX
#define DREQ_BSCSLAVETX         8               // BSC/SPI Slave TX
#define DREQ_SPISLAVETX         8               // BSC/SPI Slave TX
#define DREQ_BSCSLAVERX         9               // BSC/SPI Slave RX
#define DREQ_SPISLAVERX         9               // BSC/SPI Slave RX
#define DREQ_HDMI0              10              // HDMI0
#define DREQ_EMMC               11              // e.MMC
#define DREQ_UART0TX            12              // UART0 TX
#define DREQ_SDHOST             13              // SD HOST
#define DREQ_UART0RX            14              // UART0 RX
#define DREQ_DSI1               15              // DSI1
#define DREQ_SPI1TX             16              // SPI1 TX
#define DREQ_HDMI1              17              // HDMI1
#define DREQ_SPI1RX             18              // SPI1 RX
#define DREQ_SPI4TX             19              // UART3 TX / SPI4 TX (see DMA_CNTRL_MUX bits in PACTL_CS)
#define DREQ_UART3TX            19              // UART3 TX / SPI4 TX (see DMA_CNTRL_MUX bits in PACTL_CS)
#define DREQ_SPI4RX             20              // UART3 RX / SPI4 RX (see DMA_CNTRL_MUX bits in PACTL_CS)
#define DREQ_UART3RX            20              // UART3 RX / SPI4 RX (see DMA_CNTRL_MUX bits in PACTL_CS)
#define DREQ_SPI5TX             21              // UART5 TX / SPI5 TX (see DMA_CNTRL_MUX bits in PACTL_CS)
#define DREQ_UART5TX            21              // UART5 TX / SPI5 TX (see DMA_CNTRL_MUX bits in PACTL_CS)
#define DREQ_SPI5RX             22              // UART5 RX / SPI5 RX (see DMA_CNTRL_MUX bits in PACTL_CS)
#define DREQ_UART5RX            22              // UART5 RX / SPI5 RX (see DMA_CNTRL_MUX bits in PACTL_CS)
#define DREQ_SPI6TX             23              // SPI6 TX
#define DREQ_SCALERFIFO0        24              // Scaler FIFO 0 & SMI
#define DREQ_SCALERSMI0         24              // Scaler FIFO 0 & SMI
#define DREQ_SCALERFIFO1        25              // Scaler FIFO 1 & SMI
#define DREQ_SCALERSMI1         25              // Scaler FIFO 1 & SMI
#define DREQ_SCALERFIFO2        26              // Scaler FIFO 2 & SMI
#define DREQ_SCALERSMI2         26              // Scaler FIFO 2 & SMI
#define DREQ_SPI6RX             27              // SPI6 RX
#define DREQ_UART2TX            28              // UART2 TX
#define DREQ_UART2RX            29              // UART2 RX
#define DREQ_UART4TX            30              // UART4 TX
#define DREQ_UART4RX            31              // UART4 RX

#endif	/* __AARCH64_BCM2711_H_INCLUDED */
