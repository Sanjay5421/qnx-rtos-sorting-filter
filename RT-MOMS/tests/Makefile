# Host-side tests and tools for the RT-MOMS project sources.
#
#   mingw32-make check          build and run every test binary
#   mingw32-make demo           print the block stream the client will offload
#   mingw32-make demo-sensor    stream a recording sample by sample
#   mingw32-make demo-pipeline  sort the recording in its 64 KiB blocks
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
#
# The two threaded binaries (test_mergesort, pipeline_demo) link
# ../app/mergesort.c, which needs POSIX threads; see THREADCC below.

# `?=` would not help here: make predefines CC as `cc`, so it is never
# undefined. Override the built-in default, but let a command line or the
# environment win, e.g. mingw32-make check CC=qcc on the target.
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS ?= -std=c99 -Wall -Wextra -O2 -g

APP     = ../app
DATASET = ../../data/artifacts/dataset

SOURCES        = $(APP)/sensor.c $(APP)/protocol.c $(APP)/block_loader.c
THREAD_SOURCES = $(SOURCES) $(APP)/mergesort.c
HEADERS        = $(APP)/sensor.h $(APP)/protocol.h $(APP)/block_loader.h \
                 $(APP)/mergesort.h

TESTS    = test_sensor test_protocol test_block_loader test_mergesort
DEMOS    = sensor_demo loader_demo pipeline_demo
PLAIN    = test_sensor test_protocol test_block_loader sensor_demo loader_demo
THREADED = test_mergesort pipeline_demo

# Windows needs the extension on the binary it has just produced.
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)

BINS = $(addsuffix $(EXE),$(TESTS) $(DEMOS))

#
# ../app/mergesort.c includes <pthread.h>, which MinGW.org's gcc 6.3 does not
# ship at all - a property of this host, not of the project: on QNX (and on
# Linux) $(CC) has POSIX threads and the probe below then picks $(CC) itself.
# Where it does not, the winpthreads-capable compiler from MSYS2 is used, so
# `mingw32-make check` works on either host with nothing set by hand. Point
# THREADCC at another compiler to override the choice.
#
MINGW64 := $(firstword $(wildcard C:/msys64/mingw64/bin/gcc.exe \
                                  C:/msys64/mingw64/bin/x86_64-w64-mingw32-gcc.exe))

ifeq ($(origin THREADCC),undefined)
# The probe stays on one physical line on purpose: a backslash-newline inside
# $(shell) is not collapsed the way it is in a plain assignment. The \# is the
# makefile escape for a literal #, which would otherwise start a comment.
THREAD_PROBE = printf '\#include <pthread.h>\nstatic void *w(void *a) { (void)a; return 0; }\nint main(void) { pthread_t t; void *r; if (pthread_create(&t, 0, w, 0) != 0) return 1; return pthread_join(t, &r); }\n'
THREADCC := $(shell $(THREAD_PROBE) > .threadprobe.c && $(CC) $(CFLAGS) .threadprobe.c -o .threadprobe$(EXE) >/dev/null 2>&1 && echo $(CC) || echo $(MINGW64))
endif

.PHONY: all check demo demo-sensor demo-pipeline clean

all: $(BINS)

# Every binary links the whole application-side library: it is small, and a
# per-target source list would only be one more thing to keep in step.
$(addsuffix $(EXE),$(PLAIN)): %$(EXE): %.c $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(SOURCES)

$(addsuffix $(EXE),$(THREADED)): %$(EXE): %.c $(THREAD_SOURCES) $(HEADERS)
	@if [ -z "$(THREADCC)" ]; then \
		echo "no C compiler with POSIX threads found - set THREADCC, e.g."; \
		echo "  mingw32-make check THREADCC=/c/msys64/mingw64/bin/gcc.exe"; \
		exit 1; \
	fi
	$(THREADCC) $(CFLAGS) -o $@ $< $(THREAD_SOURCES)

check: $(addsuffix $(EXE),$(TESTS))
	@set -e; for t in $(TESTS); do \
		echo "=== $$t"; \
		./$$t$(EXE) $(DATASET); \
	done

demo: loader_demo$(EXE)
	./loader_demo$(EXE) $(DATASET)/bin/channels/r1_voltage.bin planar voltage 100

demo-sensor: sensor_demo$(EXE)
	./sensor_demo$(EXE) $(DATASET)/bin/channels/r1_voltage.bin 10 0 1 20

demo-pipeline: pipeline_demo$(EXE)
	./pipeline_demo$(EXE) $(DATASET)/bin/channels/r1_voltage.bin planar voltage 100 4

clean:
	-rm -f $(BINS) *.o .threadprobe.c .threadprobe$(EXE)
