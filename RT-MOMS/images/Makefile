ROOT_DIR := $(notdir $(CURDIR))
ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

HOST_MKIFS := mkifs
HOST_MKI := mki
HOST_SED := sed
SUFFIXES := .build .bin .raw .ui

# NOTE: This value must match the '[image=<start_addr>]' value in the build file.
IMAGE_LOAD_ADDR = 0x80000

PROCESSOR = aarch64
BUILD_TEMPLATE = $(CURDIR)/../../Templates_build_sdp800
BOARD=rpi4
INSTALL=../install

.PHONY: all clean

#all: ifs-$(BOARD).bin ifs-$(BOARD).ui
all: ifs-$(BOARD).bin

# to boot QNX ifs-rpi4.bin IFS image, add "kernel=ifs-rpi4.bin" to bootable microSD card config.txt file.
ifs-$(BOARD).bin: $(BOARD).build
	$(HOST_MKIFS) -v -r$(INSTALL) $(MKIFSFLAGS) $^ $@

# To boot from U-boot "go" command:
#  - load ifs-rpi4.raw IFS image to 0x80000
#  - load bcm2711-rpi-4-b.dtb to ${fdt_addr}
#  - run "go 0x80000 ${fdt_addr}"
ifs-$(BOARD).raw: $(BOARD).build
	$(CP_HOST) $(BOARD).build $(BOARD)-go.build
	$(HOST_SED) -i 's/u reg/u arg/' $(BOARD)-go.build
	$(HOST_MKIFS) -v -r$(INSTALL) $(MKIFSFLAGS)  $(BOARD)-go.build $@
	$(RM_HOST) $(BOARD)-go.build

# To boot from U-boot "bootm" command:
#  - load ifs-rpi4.ui IFS image to 0x80000
#  - load bcm2711-rpi-4-b.dtb to ${fdt_addr}
#  - run "bootm 0x80000 - ${fdt_addr}"
ifs-$(BOARD).ui: ifs-$(BOARD).bin
	$(HOST_MKI) -a $(IMAGE_LOAD_ADDR) -A arm64 $^ $@

clean:
	$(RM_HOST) ifs-$(BOARD).* *.sym

-include $(BUILD_TEMPLATE)/template.mk
