#!/bin/sh
# ============================================================================
#   Real-Time Multi-Core Sorting and Median Filter Service
#
#   The client offloads 64 KiB blocks of unsorted float32 time-series metrics to
#   the server. The server sorts each block with a parallel merge sort over four
#   worker threads -- mutexes and condition variables guard the merge phases --
#   applies a rolling Hampel median filter to remove sensor outliers, and signals
#   completion with a QNX pulse carrying the median, with the sorted and filtered
#   arrays left in shared memory for the client to read.
#
#   Anmol Govindarajapuram Krishnan   -  CB.EN.U4EEE23103
#   Mauli Rajguru                     -  CB.EN.U4EEE23120
#   Sanjay TS                         -  CB.EN.U4EEE23136
#
#   26ELC331 Real-Time Operating Systems
#   Date of evaluation: 17-10-2026
# ============================================================================

#
# perf-sweep.sh -- run the filter-width (or worker-count) sweep on the board.
#
#   sh perf-sweep.sh width   [out_dir]
#   sh perf-sweep.sh threads [out_dir]
#
# width    one server for the whole sweep. The rolling-filter width travels in
#          each submit (proto_submit_t.filter_width), so the server is never
#          restarted: one client run per (width, channel, repetition).
# threads  the merge-pool size is a server setting, so the server is started
#          once per worker count; every case runs at FILTER_WIDTH.
#
# Each case gets its own directory under out_dir, holding the client's four
# output files and its log; every case appends one entry to the manifest, and
# the server's log holds the per-block `perf:` lines. Copy out_dir back to the
# PC and score it:
#
#   uv run python tools/filter_sweep.py <out_dir> --out run-figures/perf
#
# Environment overrides, all optional:
#
#   WIDTHS="3 5 7 9 11 13 15 21 31"   widths to sweep                  [width]
#   THREADS_LIST="1 2 3 4"            worker counts to sweep         [threads]
#   FILTER_WIDTH=9                    width used in the threads mode
#   CHANNELS="voltage current temperature time"
#   REPS=3                            client runs per case
#   PAYLOAD=/data/bin/Chen2020_c1_T25_discharge.bin
#   LAYOUT=interleaved                planar | interleaved
#   RATE=100                          nominal rate flag the client prints
#   OUT=/var/mnt/perf                 where the case directories go
#   ALLOW_ANY=1                       skip the "is this the card?" check
#
# Space: a case is about 720 KiB (three 240 000-byte arrays plus the CSV), so
# the default width sweep is roughly 80 MiB. That check below exists because
# the sweep writing into /var -- a 16 MiB RAM filesystem -- would fill up and
# fail only at the end.
#
# If the sweep is interrupted, the server is still running: `slay sortmed-server`
# on the board stops it (or kill the pid the script started).

set -u

WIDTHS=${WIDTHS:-"3 5 7 9 11 13 15 21 31"}
THREADS_LIST=${THREADS_LIST:-"1 2 3 4"}
FILTER_WIDTH=${FILTER_WIDTH:-9}
CHANNELS=${CHANNELS:-"voltage current temperature time"}
REPS=${REPS:-3}
PAYLOAD=${PAYLOAD:-/data/bin/Chen2020_c1_T25_discharge.bin}
LAYOUT=${LAYOUT:-interleaved}
RATE=${RATE:-100}
OUT=${OUT:-/var/mnt/perf}

mode=${1:-}
if [ "$mode" != "width" ] && [ "$mode" != "threads" ]; then
    echo "usage: sh perf-sweep.sh width|threads [out_dir]" >&2
    exit 2
fi
if [ $# -ge 2 ]; then
    OUT=$2
fi

if [ "${ALLOW_ANY:-0}" != "1" ]; then
    parent=${OUT%/*}
    if [ "$parent" = "$OUT" ]; then
        parent=.
    fi
    if [ ! -f "$parent/ifs-rpi4.bin" ] && [ ! -f "$parent/config.txt" ]; then
        echo "perf-sweep: '$parent' does not look like the mounted card, and" >&2
        echo "the sweep needs the space. Mount the card first (ls /dev/sd* to" >&2
        echo "find it, the FAT partition is the one with a t suffix):" >&2
        echo "  mount -t dos /dev/sd0t12 /var/mnt" >&2
        echo "Or point OUT at another writable place; ALLOW_ANY=1 skips this." >&2
        exit 2
    fi
fi
mkdir -p "$OUT" || exit 1
MANIFEST=$OUT/sweep.manifest
: > "$MANIFEST" || exit 1

RUN_INDEX=0
TOTAL_CASES=0
SRV_PID=
SERVER_LOG=

start_server()
{
    # $1 = worker threads, $2 = log file name
    sortmed-server -t "$1" > "$OUT/$2" 2>&1 &
    SRV_PID=$!
    SERVER_LOG=$2
    RUN_INDEX=0
    # No waitfor and no sleep: the client retries name_open() to a ten-second
    # deadline, so a server that is still starting is not a race. Every case
    # therefore also exercises prerequisite 7.
}

stop_server()
{
    if [ -n "$SRV_PID" ]; then
        kill "$SRV_PID" 2>/dev/null
        wait "$SRV_PID" 2>/dev/null
        sleep 1
        SRV_PID=
    fi
}

run_case()
{
    # $1 = width, $2 = threads, $3 = channel, $4 = repetition
    width=$1
    threads=$2
    channel=$3
    rep=$4

    RUN_INDEX=$((RUN_INDEX + 1))
    TOTAL_CASES=$((TOTAL_CASES + 1))
    case_name=w${width}_t${threads}_${channel}_r${rep}
    case_dir=$OUT/$case_name

    mkdir -p "$case_dir" || return 1
    if ! ( cd "$case_dir" &&
           sortmed-client -v -w "$width" "$PAYLOAD" "$LAYOUT" "$channel" \
                          "$RATE" > client.log 2>&1 ); then
        echo "perf-sweep: case $case_name failed; see $case_dir/client.log" >&2
        return 1
    fi

    {
        echo "run=$RUN_INDEX"
        echo "case=$case_name"
        echo "width=$width"
        echo "threads=$threads"
        echo "channel=$channel"
        echo "rep=$rep"
        echo "server=$SERVER_LOG"
        echo "payload=$PAYLOAD"
        echo "layout=$LAYOUT"
        echo "rate=$RATE"
        echo
    } >> "$MANIFEST"

    echo "  [$TOTAL_CASES] $case_name"
    return 0
}

echo "perf-sweep: mode=$mode out=$OUT reps=$REPS channels='$CHANNELS'"
if [ "$mode" = "width" ]; then
    echo "perf-sweep: widths='$WIDTHS' (one server, width per submit)"
else
    echo "perf-sweep: threads='$THREADS_LIST' at width $FILTER_WIDTH"
fi
echo "perf-sweep: payload=$PAYLOAD ($LAYOUT, $RATE Hz)"

if [ "$mode" = "width" ]; then
    start_server 4 server.log
    for w in $WIDTHS; do
        for ch in $CHANNELS; do
            rep=1
            while [ "$rep" -le "$REPS" ]; do
                run_case "$w" 4 "$ch" "$rep" || { stop_server; exit 1; }
                rep=$((rep + 1))
            done
        done
    done
    stop_server
else
    for t in $THREADS_LIST; do
        start_server "$t" "server-t$t.log"
        for ch in $CHANNELS; do
            rep=1
            while [ "$rep" -le "$REPS" ]; do
                run_case "$FILTER_WIDTH" "$t" "$ch" "$rep" || { stop_server; exit 1; }
                rep=$((rep + 1))
            done
        done
        stop_server
    done
fi

sync
echo "perf-sweep: done, $TOTAL_CASES cases in $OUT"
echo "perf-sweep: copy it back and score with"
echo "  uv run python tools/filter_sweep.py <out_dir> --out run-figures/perf"
