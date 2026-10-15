# Host-side tests and tools for the RT-MOMS project sources.
#
#   mingw32-make check     build and run the virtual-sensor tests
#   mingw32-make demo      run the sensor demo over the real payload
#   mingw32-make clean
#
# The same sources are cross-compiled for the target later; nothing here is
# host-specific except the compiler name:
#
#   qcc -Vgcc_ntoaarch64le -o test_sensor test_sensor.c ../app/sensor.c
#
# Without make at all, the two commands "check" performs are:
#
#   gcc -std=c99 -Wall -Wextra -O2 -g -o test_sensor test_sensor.c ../app/sensor.c
#   ./test_sensor ../../data/artifacts/dataset

# `?=` would not help here: make predefines CC as `cc`, so it is never
# undefined. Override the built-in default, but let a command line or the
# environment win, e.g. mingw32-make check CC=qcc on the target.
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS  ?= -std=c99 -Wall -Wextra -O2 -g

SENSOR   = ../app/sensor.c
HEADER   = ../app/sensor.h
DATASET  = ../../data/artifacts/dataset

# Windows needs the extension on the binary it has just produced.
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)

BINS = test_sensor$(EXE) sensor_demo$(EXE)

.PHONY: all check demo clean

all: $(BINS)

test_sensor$(EXE): test_sensor.c $(HEADER) $(SENSOR)
	$(CC) $(CFLAGS) -o $@ test_sensor.c $(SENSOR)

sensor_demo$(EXE): sensor_demo.c $(HEADER) $(SENSOR)
	$(CC) $(CFLAGS) -o $@ sensor_demo.c $(SENSOR)

check: test_sensor$(EXE)
	./test_sensor$(EXE) $(DATASET)

demo: sensor_demo$(EXE)
	./sensor_demo$(EXE) $(DATASET)/bin/channels/r1_voltage.bin 10 0 1 20

clean:
	-rm -f $(BINS) test_sensor sensor_demo *.o
