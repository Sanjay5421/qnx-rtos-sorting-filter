# RT-MOMS/app/Makefile
#
# Builds the project's programs for the Raspberry Pi and installs them where
# images/rpi4.build picks files up by name.
#
# Driven from the BSP's top-level Makefile: `make install` runs this too, after
# qnxsdp-env.bat has set QNX_HOST and QNX_TARGET.
#
# The host-side test build is separate — see ../tests/Makefile.

TARGET    = aarch64le
# Resolve the compiler from the SDP installation rather than from PATH, so the
# build does not depend on how the shell happens to be set up. qnxsdp-env.bat
# sets QNX_HOST.
QCC      ?= $(if $(QNX_HOST),$(QNX_HOST)/usr/bin/qcc,qcc)
QCFLAGS   = -Vgcc_nto$(TARGET) -std=c99 -D_QNX_SOURCE -Wall -Wextra -O2 -lm

INSTALL_BIN  = ../install/$(TARGET)/bin
INSTALL_DATA = ../install/data
DATA         = ../../data/artifacts/dataset
SRCDIR       = ../tests

SOURCES  = sensor.c protocol.c block_loader.c mergesort.c median.c shared_memory.c
HEADERS  = sensor.h protocol.h block_loader.h mergesort.h median.h shared_memory.h
TEST_PROGRAMS = test_protocol test_sensor test_block_loader test_mergesort \
                test_median test_shared_memory \
                sensor_demo loader_demo pipeline_demo
SERVICE_PROGRAMS = sortmed-server sortmed-client
PROGRAMS = $(TEST_PROGRAMS) $(SERVICE_PROGRAMS)

# mergesort.c uses POSIX threads, which the QNX C library provides without an
# extra library or flag; the host build needs the compiler split described in
# ../tests/Makefile.

# The payload the on-target tests read. It is generated and git-ignored, so a
# missing copy is a warning here and an optional entry in the build file: the
# image is then built without data, and the payload tests report skipped
# rather than failed.
PAYLOAD = $(DATA)/bin/channels/r1_voltage.bin \
          $(DATA)/bin/Chen2020_c1_T25_discharge.bin

.PHONY: all install clean

all: $(PROGRAMS)

$(TEST_PROGRAMS): %: $(SRCDIR)/%.c $(SOURCES) $(HEADERS)
	$(QCC) $(QCFLAGS) -o $@ $< $(SOURCES)

sortmed-server: server.c protocol.c mergesort.c median.c shm_region.c \
                protocol.h mergesort.h median.h shm_region.h
	$(QCC) $(QCFLAGS) -o $@ server.c protocol.c mergesort.c median.c shm_region.c

sortmed-client: client.c sensor.c protocol.c block_loader.c mergesort.c median.c shm_region.c \
                protocol.h sensor.h block_loader.h mergesort.h median.h shm_region.h
	$(QCC) $(QCFLAGS) -o $@ client.c sensor.c protocol.c block_loader.c mergesort.c median.c shm_region.c

install: all
	mkdir -p $(INSTALL_BIN)
	cp $(PROGRAMS) $(INSTALL_BIN)
	@for f in $(PAYLOAD); do \
		if [ -f "$$f" ]; then \
			mkdir -p $(INSTALL_DATA); \
			cp "$$f" $(INSTALL_DATA); \
		else \
			echo "app: $$f is missing - the image will carry no payload"; \
		fi; \
	done

clean:
	-rm -f $(PROGRAMS) *.o
