/*
 * $QNXLicenseC:
 * Copyright 2020,2023,2025 QNX Software Systems.
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <aarch64/bcm2711.h>

#define DRIVE_UNSET     (-1)
#define DRIVE_LOW       0
#define DRIVE_HIGH      1

#define PULL_UNSET      (-1)
#define PULL_NONE       0
#define PULL_DOWN       1
#define PULL_UP         2

#define FUNC_UNSET      (-1)
#define FUNC_IP         0
#define FUNC_OP         1
#define FUNC_A0         4
#define FUNC_A1         5
#define FUNC_A2         6
#define FUNC_A3         7
#define FUNC_A4         3
#define FUNC_A5         2
#define NUM_ALT_FUNCS   6

#define GPIO_MIN        0
#define GPIO_MAX        53

static const char * const gpio_alt_names_2711[] =
{
    "SDA0"      , "SA5"        , "PCLK"      , "SPI3_CE0_N"    , "TXD2"            , "SDA6"        ,
    "SCL0"      , "SA4"        , "DE"        , "SPI3_MISO"     , "RXD2"            , "SCL6"        ,
    "SDA1"      , "SA3"        , "LCD_VSYNC" , "SPI3_MOSI"     , "CTS2"            , "SDA3"        ,
    "SCL1"      , "SA2"        , "LCD_HSYNC" , "SPI3_SCLK"     , "RTS2"            , "SCL3"        ,
    "GPCLK0"    , "SA1"        , "DPI_D0"    , "SPI4_CE0_N"    , "TXD3"            , "SDA3"        ,
    "GPCLK1"    , "SA0"        , "DPI_D1"    , "SPI4_MISO"     , "RXD3"            , "SCL3"        ,
    "GPCLK2"    , "SOE_N_SE"   , "DPI_D2"    , "SPI4_MOSI"     , "CTS3"            , "SDA4"        ,
    "SPI0_CE1_N", "SWE_N_SRW_N", "DPI_D3"    , "SPI4_SCLK"     , "RTS3"            , "SCL4"        ,
    "SPI0_CE0_N", "SD0"        , "DPI_D4"    , "I2CSL_CE_N"    , "TXD4"            , "SDA4"        ,
    "SPI0_MISO" , "SD1"        , "DPI_D5"    , "I2CSL_SDI_MISO", "RXD4"            , "SCL4"        ,
    "SPI0_MOSI" , "SD2"        , "DPI_D6"    , "I2CSL_SDA_MOSI", "CTS4"            , "SDA5"        ,
    "SPI0_SCLK" , "SD3"        , "DPI_D7"    , "I2CSL_SCL_SCLK", "RTS4"            , "SCL5"        ,
    "PWM0_0"    , "SD4"        , "DPI_D8"    , "SPI5_CE0_N"    , "TXD5"            , "SDA5"        ,
    "PWM0_1"    , "SD5"        , "DPI_D9"    , "SPI5_MISO"     , "RXD5"            , "SCL5"        ,
    "TXD0"      , "SD6"        , "DPI_D10"   , "SPI5_MOSI"     , "CTS5"            , "TXD1"        ,
    "RXD0"      , "SD7"        , "DPI_D11"   , "SPI5_SCLK"     , "RTS5"            , "RXD1"        ,
    "-"         , "SD8"        , "DPI_D12"   , "CTS0"          , "SPI1_CE2_N"      , "CTS1"        ,
    "-"         , "SD9"        , "DPI_D13"   , "RTS0"          , "SPI1_CE1_N"      , "RTS1"        ,
    "PCM_CLK"   , "SD10"       , "DPI_D14"   , "SPI6_CE0_N"    , "SPI1_CE0_N"      , "PWM0_0"      ,
    "PCM_FS"    , "SD11"       , "DPI_D15"   , "SPI6_MISO"     , "SPI1_MISO"       , "PWM0_1"      ,
    "PCM_DIN"   , "SD12"       , "DPI_D16"   , "SPI6_MOSI"     , "SPI1_MOSI"       , "GPCLK0"      ,
    "PCM_DOUT"  , "SD13"       , "DPI_D17"   , "SPI6_SCLK"     , "SPI1_SCLK"       , "GPCLK1"      ,
    "SD0_CLK"   , "SD14"       , "DPI_D18"   , "SD1_CLK"       , "ARM_TRST"        , "SDA6"        ,
    "SD0_CMD"   , "SD15"       , "DPI_D19"   , "SD1_CMD"       , "ARM_RTCK"        , "SCL6"        ,
    "SD0_DAT0"  , "SD16"       , "DPI_D20"   , "SD1_DAT0"      , "ARM_TDO"         , "SPI3_CE1_N"  ,
    "SD0_DAT1"  , "SD17"       , "DPI_D21"   , "SD1_DAT1"      , "ARM_TCK"         , "SPI4_CE1_N"  ,
    "SD0_DAT2"  , "-"          , "DPI_D22"   , "SD1_DAT2"      , "ARM_TDI"         , "SPI5_CE1_N"  ,
    "SD0_DAT3"  , "-"          , "DPI_D23"   , "SD1_DAT3"      , "ARM_TMS"         , "SPI6_CE1_N"  ,
    "SDA0"      , "SA5"        , "PCM_CLK"   , "-"             , "MII_A_RX_ERR"    , "RGMII_MDIO"  ,
    "SCL0"      , "SA4"        , "PCM_FS"    , "-"             , "MII_A_TX_ERR"    , "RGMII_MDC"   ,
    "-"         , "SA3"        , "PCM_DIN"   , "CTS0"          , "MII_A_CRS"       , "CTS1"        ,
    "-"         , "SA2"        , "PCM_DOUT"  , "RTS0"          , "MII_A_COL"       , "RTS1"        ,
    "GPCLK0"    , "SA1"        , "-"         , "TXD0"          , "SD_CARD_PRES"    , "TXD1"        ,
    "-"         , "SA0"        , "-"         , "RXD0"          , "SD_CARD_WRPROT"  , "RXD1"        ,
    "GPCLK0"    , "SOE_N_SE"   , "-"         , "SD1_CLK"       , "SD_CARD_LED"     , "RGMII_IRQ"   ,
    "SPI0_CE1_N", "SWE_N_SRW_N", "-"         , "SD1_CMD"       , "RGMII_START_STOP", "-"           ,
    "SPI0_CE0_N", "SD0"        , "TXD0"      , "SD1_DAT0"      , "RGMII_RX_OK"     , "MII_A_RX_ERR",
    "SPI0_MISO" , "SD1"        , "RXD0"      , "SD1_DAT1"      , "RGMII_MDIO"      , "MII_A_TX_ERR",
    "SPI0_MOSI" , "SD2"        , "RTS0"      , "SD1_DAT2"      , "RGMII_MDC"       , "MII_A_CRS"   ,
    "SPI0_SCLK" , "SD3"        , "CTS0"      , "SD1_DAT3"      , "RGMII_IRQ"       , "MII_A_COL"   ,
    "PWM1_0"    , "SD4"        , "-"         , "SD1_DAT4"      , "SPI0_MISO"       , "TXD1"        ,
    "PWM1_1"    , "SD5"        , "-"         , "SD1_DAT5"      , "SPI0_MOSI"       , "RXD1"        ,
    "GPCLK1"    , "SD6"        , "-"         , "SD1_DAT6"      , "SPI0_SCLK"       , "RTS1"        ,
    "GPCLK2"    , "SD7"        , "-"         , "SD1_DAT7"      , "SPI0_CE0_N"      , "CTS1"        ,
    "GPCLK1"    , "SDA0"       , "SDA1"      , "-"             , "SPI0_CE1_N"      , "SD_CARD_VOLT",
    "PWM0_1"    , "SCL0"       , "SCL1"      , "-"             , "SPI0_CE2_N"      , "SD_CARD_PWR0",
    "SDA0"      , "SDA1"       , "SPI0_CE0_N", "-"             , "-"               , "SPI2_CE1_N"  ,
    "SCL0"      , "SCL1"       , "SPI0_MISO" , "-"             , "-"               , "SPI2_CE0_N"  ,
    "SD0_CLK"   , "-"          , "SPI0_MOSI" , "SD1_CLK"       , "ARM_TRST"        , "SPI2_SCLK"   ,
    "SD0_CMD"   , "GPCLK0"     , "SPI0_SCLK" , "SD1_CMD"       , "ARM_RTCK"        , "SPI2_MOSI"   ,
    "SD0_DAT0"  , "GPCLK1"     , "PCM_CLK"   , "SD1_DAT0"      , "ARM_TDO"         , "SPI2_MISO"   ,
    "SD0_DAT1"  , "GPCLK2"     , "PCM_FS"    , "SD1_DAT1"      , "ARM_TCK"         , "SD_CARD_LED" ,
    "SD0_DAT2"  , "PWM0_0"     , "PCM_DIN"   , "SD1_DAT2"      , "ARM_TDI"         , "-"           ,
    "SD0_DAT3"  , "PWM0_1"     , "PCM_DOUT"  , "SD1_DAT3"      , "ARM_TMS"         , "-"           ,
};

#define BLOCK_SIZE          (4*1024)

/* Number of pins serviced by each SET, CLR, LEV, etc. register. */
#define GP_PINS_PER_REG     32

#define GPSET0              7
#define GPSET1              8
#define GPCLR0              10
#define GPCLR1              11
#define GPLEV0              13
#define GPLEV1              14
#define GPPUD               37
#define GPPUDCLK0           38
#define GPPUDCLK1           39

/* 2711 has a different mechanism for pin pull-up/down/enable  */
#define GPPUPPDN0           57        /* Pin pull-up/down for pins 15:0  */
#define GPPUPPDN1           58        /* Pin pull-up/down for pins 31:16 */
#define GPPUPPDN2           59        /* Pin pull-up/down for pins 47:32 */
#define GPPUPPDN3           60        /* Pin pull-up/down for pins 57:48 */

/* Pointer to HW */
static volatile uint32_t *gpio_base;

static void print_gpio_alts_info(const unsigned gpio)
{
    const int gpio_default_pullstate[] = {
        /*GPIO0-8 UP*/
        PULL_UP,PULL_UP,PULL_UP,PULL_UP,PULL_UP,PULL_UP,PULL_UP,PULL_UP,PULL_UP,
        /*GPIO9-27 DOWN*/
        PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,
        PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,
        PULL_DOWN,
        /*GPIO28-29 NONE*/
        PULL_NONE,PULL_NONE,
        /*GPIO30-33 DOWN*/
        PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,
        /*GPIO34-36 UP*/
        PULL_UP,PULL_UP,PULL_UP,
        /*GPIO37-43 DOWN*/
        PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,PULL_DOWN,
        /*GPIO44-45 NONE*/
        PULL_NONE,PULL_NONE,
         /*GPIO46-53 UP*/
        PULL_UP,PULL_UP,PULL_UP,PULL_UP,PULL_UP,PULL_UP,PULL_UP,PULL_UP
    };

    printf("%d", gpio);
    if (gpio_default_pullstate[gpio] == PULL_NONE) {
        printf(", NONE");
    } else if (gpio_default_pullstate[gpio] == PULL_DOWN) {
        printf(", DOWN");
    } else {
        printf(", UP");
    }
    for (unsigned alt = 0; alt < NUM_ALT_FUNCS; alt++) {
        printf(", %s", gpio_alt_names_2711[gpio * NUM_ALT_FUNCS + alt]);
    }
    printf("\n");
}

static int get_gpio_fsel(const unsigned gpio)
{
    /* GPIOFSEL0-GPIOFSEL5 with 10 sels per 32 bit reg,
       3 bits per sel (so bits 0:29 used) */
    const uint32_t reg = gpio / 10;
    const uint32_t sel = gpio % 10;
    if (gpio >= GPIO_MAX) {
        return -1;
    }
    return (int)((*(gpio_base + reg)) >> (3 * sel)) & 0x7;
}

static int set_gpio_fsel(const unsigned gpio, const int fsel)
{
    uint32_t reg = gpio / 10;
    const uint32_t sel = gpio % 10;
    uint32_t val;
    if (gpio >= GPIO_MAX) {
        return -1;
    }
    val = *(gpio_base + reg);
    val &= ~(0x7 << (3 * sel));
    val |= (fsel & 0x7) << (3 * sel);
    *(gpio_base + reg) = val;
    return 0;
}

static int get_gpio_level(unsigned gpio)
{
    if (gpio >= GPIO_MAX) {
        return -1;
    }
    if (gpio < GP_PINS_PER_REG) {
        return ((*(gpio_base + GPLEV0)) >> gpio) & 0x1;
    } else {
        gpio = gpio - GP_PINS_PER_REG;
        return ((*(gpio_base + GPLEV1)) >> gpio) & 0x1;
    }
}

static int set_gpio_value(unsigned gpio, const int value)
{
    if (gpio >= GPIO_MAX) {
        return -1;
    }
    if (value != 0) {
        if (gpio < GP_PINS_PER_REG) {
            *(gpio_base + GPSET0) = (uint32_t)(0x1 << gpio);
        } else {
            gpio -= GP_PINS_PER_REG;
            *(gpio_base + GPSET1) = (uint32_t)(0x1 << gpio);
        }
    } else {
        if (gpio < GP_PINS_PER_REG) {
            *(gpio_base + GPCLR0) = (uint32_t)(0x1 << gpio);
        } else {
            gpio -= GP_PINS_PER_REG;
            *(gpio_base + GPCLR1) = (uint32_t)(0x1 << gpio);
        }
    }
    return 0;
}

static int gpio_fsel_to_namestr(const unsigned gpio, const int fsel, char * const name)
{
    unsigned altfn = 0;
    if (gpio >= GPIO_MAX) {
        return -1;
    }

    switch (fsel) {
        case FUNC_IP:
            return sprintf(name, "INPUT");
        case FUNC_OP:
            return sprintf(name, "OUTPUT");
        case FUNC_A5:
            altfn = 5;
            break;
        case FUNC_A4:
            altfn = 4;
            break;
        case FUNC_A0:
            altfn = 0;
            break;
        case FUNC_A1:
            altfn = 1;
            break;
        case FUNC_A2:
            altfn = 2;
            break;
        case FUNC_A3:
            altfn = 3;
            break;
        default:
            return -1;
    }
    return sprintf(name, "%s", gpio_alt_names_2711[gpio * NUM_ALT_FUNCS + altfn]);
}

/*
 * type:
 *   0 = no pull
 *   1 = pull down
 *   2 = pull up
 */
static int gpio_set_pull(const unsigned gpio, const int type)
{
    if (gpio >= GPIO_MAX) {
        return -1;
    }
    if ((type < PULL_NONE) || (type > PULL_UP)) {
        return -1;
    }

    uint32_t pullreg = (GPPUPPDN0 + (gpio >> 4));
    const uint32_t pullshift = (uint32_t)((gpio & 0xf) << 1);
    unsigned int pullbits;
    unsigned int pull;

    switch (type) {
        case PULL_NONE:
            pull = 0;
            break;
        case PULL_UP:
            pull = 1;
            break;
        case PULL_DOWN:
            pull = 2;
            break;
        default:
            return 1; /* An illegal value */
    }

    pullbits = *(gpio_base + pullreg);
    pullbits &= ~(3 << pullshift);
    pullbits |= (pull << pullshift);
    *(gpio_base + pullreg) = pullbits;

    return 0;
}

static int get_gpio_pull(const unsigned pinnum)
{
    int pull = PULL_UNSET;
    const uint32_t pull_reg = *(gpio_base + GPPUPPDN0 + (pinnum >> 4));
    const uint8_t pull_bits = (uint8_t)((pull_reg >> ((pinnum & 0xf) << 1)) & 0x3);

    switch (pull_bits) {
        case 0:
            pull = PULL_NONE;
            break;
        case 1:
            pull = PULL_UP;
            break;
        case 2:
            pull = PULL_DOWN;
            break;
        default:
            pull = PULL_UNSET;
            break; /* An illegal value */
    }

    return pull;
}

static int gpio_get(const unsigned pinnum)
{
    char name[512] = { 0 };
    char pullstr[12] = { 0 };
    int level;
    int fsel;
    int pull;

    const char* const gpio_pull_names[] = {"NONE", "DOWN", "UP", "?"};
    const char* const gpio_fsel_alts[] = {" ", " ", "5", "4", "0", "1", "2", "3"};

    fsel = get_gpio_fsel(pinnum);
    gpio_fsel_to_namestr(pinnum, fsel, name);
    level = get_gpio_level(pinnum);
    pullstr[0] = '\0';
    pull = get_gpio_pull(pinnum);
    if (pull != PULL_UNSET) {
        (void)snprintf(pullstr, sizeof(pullstr), " pull=%s", gpio_pull_names[pull & 3]);
    }
    if (fsel < 2) {
        printf("GPIO %d: level=%d fsel=%d func=%s%s\n",
               pinnum, level, fsel, name, pullstr);
    } else {
        printf("GPIO %d: level=%d fsel=%d alt=%s func=%s%s\n",
               pinnum, level, fsel, gpio_fsel_alts[fsel], name, pullstr);
    }
    return 0;
}

static int gpio_set(const unsigned pinnum, const int fsparam, const int drive, const int pull)
{
    /* set function */
    if (fsparam != FUNC_UNSET) {
        set_gpio_fsel(pinnum, fsparam);
    }

    /* set output value (check pin is output first) */
    if (drive != DRIVE_UNSET) {
        if (get_gpio_fsel(pinnum) == 1) {
            set_gpio_value(pinnum, drive);
        } else {
            printf("Can't set pin value, not an output\n");
            return 1;
        }
    }

    /* set pulls */
    if (pull != PULL_UNSET) {
        return gpio_set_pull(pinnum, pull);
    }
    return 0;
}

int main(const int argc, char * const argv[])
{
    int ret;

    /* arg parsing */
    int set = 0;
    int get = 0;
    int funcs = 0;
    int pull = PULL_UNSET;
    int fsparam = FUNC_UNSET;
    int drive = DRIVE_UNSET;
    uint64_t pin_mask = 0UL; /* Enough for 0-53 */
    int all_pins = 0;

    if (argc < 2) {
        printf("ERROR: No arguments given - try running 'use %s'\n", argv[0]);
        return 0;
    }

    /* argc 2 or greater, next arg must be set, get or help */
    get = strcmp(argv[1], "get") == 0;
    set = strcmp(argv[1], "set") == 0;
    funcs = strcmp(argv[1], "funcs") == 0;
    if (!set && !get && !funcs) {
        printf("ERROR: Unknown argument \"%s\" try \"use %s\"\n", argv[1], argv[0]);
        return 1;
    }

    if ((get || funcs) && (argc > 3)) {
        printf("ERROR: Too many arguments\n");
        return 1;
    }

    if ((set) && (argc < 4)) {
        printf("ERROR: '%s set' expects input in the form '%s set [gpio number] [pin options]'\n",
            argv[0], argv[0]);
        return 1;
    }

    /* expect pin number(s) next */
    if (argc > 2) {
        const char * pin_opt = argv[2];
        while (pin_opt) {
            unsigned pin, pin2, len;
            ret = sscanf(pin_opt, "%u%n", &pin, &len);
            if ((ret != 1) || (pin >= GPIO_MAX)) {
                break;
            }
            pin_opt += len;

            if (*pin_opt == '-') {
                pin_opt++;
                ret = sscanf(pin_opt, "%u%n", &pin2, &len);
                if ((ret != 1) || (pin2 >= GPIO_MAX)) {
                    break;
                }
                if (pin2 < pin) {
                    const unsigned tmp = pin2;
                    pin2 = pin;
                    pin = tmp;
                }
                pin_opt += len;
            } else {
                pin2 = pin;
            }
            while (pin <= pin2) {
                pin_mask |= (1UL << pin);
                pin++;
            }
            if (*pin_opt == '\0') {
                pin_opt = NULL;
            } else {
                if (*pin_opt != ',') {
                    break;
                }
                pin_opt++;
            }
        }
        if (pin_opt) {
            printf("ERROR: Unknown GPIO \"%s\"\n", pin_opt);
            return 1;
        }
    }

    if (set) {
        /* parse set options */
        for (int arg_num = 3; arg_num < argc; arg_num++) {
            if (strcmp(argv[arg_num], "dh") == 0) {
                drive = DRIVE_HIGH;
            } else if (strcmp(argv[arg_num], "dl") == 0) {
                drive = DRIVE_LOW;
            } else if (strcmp(argv[arg_num], "ip") == 0) {
                fsparam = FUNC_IP;
            } else if (strcmp(argv[arg_num], "op") == 0) {
                fsparam = FUNC_OP;
            } else if (strcmp(argv[arg_num], "a0") == 0) {
                fsparam = FUNC_A0;
            } else if (strcmp(argv[arg_num], "a1") == 0) {
                fsparam = FUNC_A1;
            } else if (strcmp(argv[arg_num], "a2") == 0) {
                fsparam = FUNC_A2;
            } else if (strcmp(argv[arg_num], "a3") == 0) {
                fsparam = FUNC_A3;
            } else if (strcmp(argv[arg_num], "a4") == 0) {
                fsparam = FUNC_A4;
            } else if (strcmp(argv[arg_num], "a5") == 0) {
                fsparam = FUNC_A5;
            } else if (strcmp(argv[arg_num], "pu") == 0) {
                pull = PULL_UP;
            } else if (strcmp(argv[arg_num], "pd") == 0) {
                pull = PULL_DOWN;
            } else if (strcmp(argv[arg_num], "pn") == 0) {
                pull = PULL_NONE;
            } else {
                printf("Unknown argument \"%s\"\n", argv[arg_num]);
                return 1;
            }
        }
    }

    /* end arg parsing */
    if (pin_mask == 0UL) {
        all_pins = 1;
    }

    if (funcs) {
        unsigned pin;
        printf("GPIO, DEFAULT PULL, ALT0, ALT1, ALT2, ALT3, ALT4, ALT5\n");
        for (pin = GPIO_MIN; pin <= GPIO_MAX; pin++) {
            if ((all_pins) || (pin_mask & (1UL << pin))) {
                print_gpio_alts_info(pin);
            }
        }
        return 0;
    /* get or set */
    } else {
        gpio_base = mmap_device_memory(NULL, BLOCK_SIZE, PROT_NOCACHE|PROT_READ|PROT_WRITE, 0, BCM2711_GPIO_BASE);
        if (gpio_base == (uint32_t *) MAP_FAILED) {
            printf("mmap (GPIO) failed: %s\n", strerror (errno));
            return 1;
        }

        unsigned pin;
        for (pin = GPIO_MIN; pin <= GPIO_MAX; pin++) {
            if (all_pins) {
                if (pin == 0) {
                    printf("BANK0 (GPIO 0 to 27):\n");
                }
                if (pin == 28) {
                    printf("BANK1 (GPIO 28 to 45):\n");
                }
                if (pin == 46) {
                    printf("BANK2 (GPIO 46 to 53):\n");
                }
            } else if ((pin_mask & (1UL << pin)) == 0) {
                continue;
            }
            if (get) {
                gpio_get(pin);
            } else {
                if (gpio_set(pin, fsparam, drive, pull)) {
                    return 1;
                }
            }
        }
    }

    return 0;
}
