# Host-side tests and tools for the RT-MOMS project sources.
#
#   mingw32-make check        build and run every test binary
#   mingw32-make demo         print the block stream the client will offload
#   mingw32-make demo-sensor  stream a recording sample by sample
#   mingw32-make clean
#
# The same sources are cross-compiled for the target later; nothing here is
# host-specific except the compiler name:
#
#   qcc -Vgcc_ntoaarch64le -o test_sensor test_sensor.c ../app/sensor.c
#
# Without make at all, a test binary is built and run like this:
#
#   gcc -std=c99 -Wall -Wextra -O2 -g -o test_protocol test_protocol.c \
#       ../app/sensor.c ../app/protocol.c ../app/block_loader.c
#   ./test_protocol

# `?=` would not help here: make predefines CC as `cc`, so it is never
# undefined. Override the built-in default, but let a command line or the
# environment win, e.g. mingw32-make check CC=qcc on the target.
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS  ?= -std=c99 -Wall -Wextra -O2 -g

APP     = ../app
DATASET = ../../data/artifacts/dataset

SOURCES = $(APP)/sensor.c $(APP)/protocol.c $(APP)/block_loader.c
HEADERS = $(APP)/sensor.h $(APP)/protocol.h $(APP)/block_loader.h

TESTS = test_sensor test_protocol test_block_loader
DEMOS = sensor_demo loader_demo

# Windows needs the extension on the binary it has just produced.
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)

BINS = $(addsuffix $(EXE),$(TESTS) $(DEMOS))

.PHONY: all check demo demo-sensor clean

all: $(BINS)

# Every binary links the whole application-side library: it is small, and a
# per-target source list would only be one more thing to keep in step.
$(BINS): %$(EXE): %.c $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(SOURCES)

check: $(addsuffix $(EXE),$(TESTS))
	@set -e; for t in $(TESTS); do \
		echo "=== $$t"; \
		./$$t$(EXE) $(DATASET); \
	done

demo: loader_demo$(EXE)
	./loader_demo$(EXE) $(DATASET)/bin/channels/r1_voltage.bin planar voltage 100

demo-sensor: sensor_demo$(EXE)
	./sensor_demo$(EXE) $(DATASET)/bin/channels/r1_voltage.bin 10 0 1 20

clean:
	-rm -f $(BINS) $(TESTS) $(DEMOS) *.o
