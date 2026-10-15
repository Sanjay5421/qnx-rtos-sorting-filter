## Boot parameters
define(`__LD_QNX__',            `ldqnx-64.so.2')
define(`__BOOT_ADDR__',         `0x80000')
define(`__ARCH__',              `aarch64le')
define(`__TYPE__',              `raw')
define(`__COMPRESS_ATTR__',     `-compress')
define(`__PROCNTO_MODULES__',   `')
define(`__STARTUP__',           `startup-bcm2711-rpi4')
define(`__STARTUP_OPTS__',      `-v -D miniuart -W 2500 -u reg -r 0,4096,1')
define(`__PROCNTO__',           `procnto-smp-instr')
define(`__PROCNTO_OPTS__',      `-v')

## Console
define(`__CONSOLE__',           `__DEVC_SER1_DEV__')

## ENV profile, use to overwrite the common /etc/profile
#define(`__ENV_PROFILE_FILE__',  `etc/profile')
#define(`__PROFILE_CFG__',       `/__ENV_PROFILE_FILE__ = {
#export HOME=/
#export SYSNAME=nto
#export TERM=qansi
#export PATH=/proc/boot:/sbin:/bin:/usr/bin:/usr/sbin:/usr/libexec
#export LD_LIBRARY_PATH=/proc/boot:/lib:/usr/lib:/lib/dll:/lib/dll/pci
#}')

## Audio driver

## Block driver
define(`__BLOCK_DRVR__', `devb-sdmmc-bcm2711')

#define(`__DEVB_AHCI_START__', `')

#define(`__DEVB_EIDE_DRVR__', `')
#define(`__DEVB_EIDE_OPTS__', `')
#define(`__DEVB_EIDE_DEV__', `')

#define(`__DEVB_NVME_DRVR__', `')
#define(`__DEVB_NVME_OPTS__', `')
#define(`__DEVB_NVME_DEV__', `')

#define(`__DEVB_RAM_DRVR__', `')
#define(`__DEVB_RAM_OPTS__', `')
#define(`__DEVB_RAM_DEV__', `')

define(`__DEVB_SDMMC_DRVR__', `devb-sdmmc-bcm2711')
define(`__DEVB_SDMMC_OPTS__', `mem name=below1G sdio addr=0xfe340000,irq=158 disk name=sd')
define(`__DEVB_SDMMC_DEV__', `/dev/sd0')

define(`__DEVB_SDMMC_START__', `
    #######################################################################
    ## SD memory card / eMMC driver
    ## Note: "bmstr_base" option is required for busmaster memory
    ## address translation
    #######################################################################
    display_msg "Starting SDMMC driver (/dev/sd0)"
    __DEVB_SDMMC_DRVR__ __DEVB_SDMMC_OPTS__
    waitfor __DEVB_SDMMC_DEV__
')

define(`__DEVB_DRVR_START__', `
__DEVB_SDMMC_START__
')

## Network driver
define(`__NET_DRVR__', `devs-genet.so')
define(`__NET_OPTS__', `-m fdt -m phy_fdt -d genet')
define(`__NET_DEV__', `genet0')

## The io-sock use dhcpcd utility by default, uncomment the line below to use dhclient utility
#define(`__NET_DHCLIENT_SUPPORT__', `')

## WIFI Network driver(BCM4339)
#define(`__WIFI_START__', `
    #######################################################################
    ## WIFI Network driver(BCM4339)
    #######################################################################
    #display_msg Starting WIFI Network driver...
    #mount -T io-pkt -o fw=/etc/hotspot/firmware.bin,clm_blob=/etc/hotspot/firmware.clm_blob /lib/dll/devnp-qwdi-2.5_bcm4339-rpi4.so
    #create a wpa_supplicant.conf file in /tmp directory
    #wpa_supplicant -D qwdi -i bcm0 -c /tmp/wpa_supplicant.conf -C /tmp/ &
    #dhclient -m -lf /dev/shmem/dhclient.leases -pf /dev/shmem/dhclient.pid -nw bcm0
#')

## USB host driver
define(`__USB_HOST_DRVR__', `devu-hcd-bcm2711-xhci.so')

define(`__USB_START__', `
    #######################################################################
    ## USB OTG Host Controller driver
    #######################################################################
    sh /proc/boot/usb_start.sh
')

## Persistent storge
#define(`__PERSISTENT_STORAGE_DEVICE__', `/dev/sd0t179')
#define(`__PERSISTENT_STORAGE_MOUNT_POINT__', `')
#define(`__PERSISTENT_STORAGE_MOUNT_OPTS__', `-o sync=optional')
#define(`__PERSISTENT_STORAGE_START__', `')
#define(`__PERSISTENT_STORAGE_FILES__', `')

## Serial driver
define(`__DEVC_DRVR__', `__DEVC_SER1_DRVR__, __DEVC_SER3_DRVR__')

define(`__DEVC_SER1_DRVR__', `devc-serminiuart')
define(`__DEVC_SER1_OPTS__', `-b115200 -c500000000 -e -F -u1 0xfe215000,125')
define(`__DEVC_SER1_DEV__', `/dev/ser1')
define(`__DEVC_SER3_DRVR__', `devc-serpl011')
define(`__DEVC_SER3_OPTS__', `-b115200 -c48000000 -e -F -u3 0xfe201600,153')
define(`__DEVC_SER3_DEV__', `/dev/ser3')

define(`__DEVC_START__', `
    #######################################################################
    ## Console SERIAL driver
    #######################################################################
    display_msg "Starting serial driver (__DEVC_SER1_DEV__)"
    __DEVC_SER1_DRVR__ __DEVC_SER1_OPTS__

    `#'display_msg "Starting serial driver (__DEVC_SER3_DEV__)"
    `#'__DEVC_SER3_DRVR__ __DEVC_SER3_OPTS__
')

## I2C driver
define(`__I2C_DRVR__', `i2c-bcm2711')
define(`__I2C_OPTS__', `-p0xfe804000 --b100000 --u1')
define(`__I2C_DEV__', `/dev/i2c1')

## SPI driver
define(`__IO_SPI_DRVR__', `spi-bcm2711')

define(`__IO_SPI_CFG_CONTENTS__', `
[globals]
verbose=5

[bus]
busno=0
name=spi0
base=0xfe204000
irq=150
input_clock=500000000
bs=rpanic=48,tpanic=16
dma_attach_opts=num_cbs=256,range_min=0,range_max=14,typed_mem=sysram&below1G
dma_thld=4

[dev]
parent_busno=0
devno=0
name=dev0
clock_rate=5000000
cpha=1
cpol=0
bit_order=msb
word_width=32
idle_insert=1

[dev]
parent_busno=0
devno=1
name=dev1
cpha=0
cpol=1
word_width=32
clock_rate=5000000

[bus]
busno=3
name=spi3
base=0xfe204600
irq=151
input_clock=500000000

[dev]
parent_busno=3
devno=0
name=dev0
cpha=0
cpol=1
word_width=8
clock_rate=5000000
')

## Random
#define(`__RANDOM_DRVR__', `')
#define(`__RANDOM_DRVR_OPTS__', `')

## WDT kick
define(`__WDT_DRVR__', `wdtkick')
define(`__WDT_OPTS__', `-W0x24:0x5A028E4C -W0x1c:0x5A000020')

define(`__WDT_START__', `
    ############################################################################################
    ## WatchDog utility
    ## If startup is given -W parameter then the wdtkick utility MUST be uncommented below
    ############################################################################################
    display_msg "Starting wdtkick ..."
    __WDT_DRVR__ __WDT_OPTS__
')

## Customize script
define(`__CUSTOMIZE_SCRIPT_NAME__', `/scripts/board_startup.sh')
#define(`__CUSTOMIZE_SCRIPT_START__', `')
#define(`__CUSTOMIZE_SCRIPT_FILES__', `')

## PCI driver
define(`__PCI_HW_DRVR__', `pci_hw-bcm2711-rpi4.so')
define(`__PCI_HW_MODULE__', `pci_hw-bcm2711-rpi4.so')
define(`__PCI_BUS_SCAN_LIMIT__', `1')
#define(`__PCI_MODULE_BLACKLIST__', `')
define(`__PCI_OPTS__', `--bus-scan-limit=__PCI_BUS_SCAN_LIMIT__ -c')

define(`__PCI_START__', `
    ############################################################################################
    ## PCIe driver
    ############################################################################################
    display_msg "Starting PCI Server ..."
    PCI_HW_MODULE=/lib/dll/pci/pci_hw-bcm2711-rpi4.so
    PCI_BKWD_COMPAT_MODULE=/lib/dll/pci/pci_bkwd_compat.so
    PCI_SLOG_MODULE=/lib/dll/pci/pci_slog2.so
    PCI_DEBUG_MODULE=/lib/dll/pci/pci_debug2.so

    pci-server __PCI_OPTS__ &
    waitfor /dev/pci
')


## Board specific files
#define(`__BOARD_EARLY_START__', `')
#define(`__BOARD_LATE_START__', `')

define(`__BOARD_FILES__', `

################################################################################################
## Script for launching the io-usb-otg
################################################################################################
usb_start.sh = {
#!/bin/sh
    VL805_VERSION=/dev/shmem/vl805_version.txt
    pci-tool -d 1:0:0 --read="CFG:0x50" > $VL805_VERSION
    if grep "50] 00000000" $VL805_VERSION > /dev/null; then
        echo "Inform vc to load vl805 firmware"
        mbox-bcm2711 notifyxhcireset=0x100000 > /dev/null 2>&1
    fi
    echo "Starting USB xHCI controller in the host mode (/dev/usb/*)..."
    io-usb-otg -t memory=below1G -d bcm2711-xhci pindex=0,memory=below1G
    waitfor /dev/usb/io-usb-otg
    waitfor /dev/usb/devu-hcd-bcm2711-xhci.so
}

################################################################################################
## GPIO utility
################################################################################################
/bin/gpio-bcm2711=gpio-bcm2711

################################################################################################
## Mailbox utility
################################################################################################
/bin/mbox-bcm2711=mbox-bcm2711

################################################################################################
## WIFI firmware and WPA utilities
################################################################################################
#[search=../install/etc/firmware] /etc/hotspot/firmware.bin=brcmfmac43455-sdio.bin
#[search=../install/etc/firmware] /etc/hotspot/nvram.txt=brcmfmac43455-sdio.txt
#[search=../install/etc/firmware] /etc/hotspot/firmware.clm_blob=brcmfmac43455-sdio.clm_blob
#/usr/sbin/wpa_cli=wpa_cli
#/usr/sbin/wpa_passphrase=wpa_passphrase
#/usr/sbin/wpa_supplicant=wpa_supplicant

#[perms=0555] /etc/wpa_act={
#!/bin/sh
#case $2 in
#CONNECTED) echo $0 $1 $2 ID=$WPA_ID STR=$WPA_ID_STR >/dev/console; dhcp.$1 -D rpi4 -i $1 ;;
#DISCONNECTED) echo $0 $1 $2 >/dev/console; slay -f dhcp.$1; ifconfig $1 delete ;;
#esac
#}
')

