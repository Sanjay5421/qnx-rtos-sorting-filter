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
# client-wait-test.sh -- prerequisite 7 on the board: the client waits for the
# server, and gives up after ten seconds.
#
#   sh client-wait-test.sh [payload]
#
# Case 1  no server at all. The client must reach its ten-second deadline, say
#         so, and exit non-zero -- not fail instantly and not hang.
# Case 2  the client starts first, the server three seconds later. The client
#         must connect by itself (its own output reports the wait), complete a
#         run, and write its files.
#
# The evidence is the client's own output, which reports how long it waited and
# how many attempts it made, so this script needs nothing but sh, the client,
# the server and the payload: it runs on the board as it stands.
#
#   WORK=/var/client-wait-test   where the runs happen
#   PAYLOAD=..., LAYOUT=..., CHANNEL=..., RATE=...
#
# Exit status is 0 only if both cases behave as required.

set -u

PAYLOAD=${1:-/data/bin/Chen2020_c1_T25_discharge.bin}
LAYOUT=${LAYOUT:-interleaved}
CHANNEL=${CHANNEL:-voltage}
RATE=${RATE:-100}
WORK=${WORK:-/var/client-wait-test}

mkdir -p "$WORK" || exit 1
failures=0

echo "case 1: no server -- the client must give up after about ten seconds"
if ( cd "$WORK" && sortmed-client "$PAYLOAD" "$LAYOUT" "$CHANNEL" "$RATE" ) \
        > "$WORK/case1.log" 2>&1; then
    rc=0
else
    rc=$?
fi

waited=0
wrote=0
while read -r line; do
    case "$line" in
        *"after 10."*)      waited=1 ;;
        "output written:"*) wrote=1 ;;
    esac
done < "$WORK/case1.log"

if [ "$rc" -eq 0 ]; then
    echo "  FAIL: the client exited 0 with no server running"
    failures=$((failures + 1))
elif [ "$wrote" -eq 1 ]; then
    echo "  FAIL: the client completed a run with no server running"
    failures=$((failures + 1))
elif [ "$waited" -eq 0 ]; then
    echo "  FAIL: the message does not report the ten-second deadline;"
    echo "        see $WORK/case1.log"
    failures=$((failures + 1))
else
    echo "  ok: exit $rc, and the deadline the client reported was reached"
fi

echo "case 2: server three seconds late -- the client must connect by itself"
( cd "$WORK" && sortmed-client "$PAYLOAD" "$LAYOUT" "$CHANNEL" "$RATE" ) \
    > "$WORK/case2.log" 2>&1 &
client_pid=$!
sleep 3
sortmed-server > "$WORK/server.log" 2>&1 &
srv=$!
wait "$client_pid"
rc=$?
kill "$srv" 2>/dev/null
wait "$srv" 2>/dev/null

connected=0
wrote=0
while read -r line; do
    case "$line" in
        "connected:"*)      connected=1 ;;
        "output written:"*) wrote=1 ;;
    esac
done < "$WORK/case2.log"

if [ "$rc" -ne 0 ]; then
    echo "  FAIL: the client exited $rc; see $WORK/case2.log"
    failures=$((failures + 1))
elif [ "$wrote" -ne 1 ]; then
    echo "  FAIL: the client never reported a completed run;"
    echo "        see $WORK/case2.log"
    failures=$((failures + 1))
elif [ "$connected" -ne 1 ]; then
    echo "  FAIL: the client did not report waiting for the server;"
    echo "        see $WORK/case2.log"
    failures=$((failures + 1))
else
    echo "  ok: the client waited for the server, then completed the run"
fi

if [ "$failures" -eq 0 ]; then
    echo "client-wait-test: both cases pass"
    exit 0
fi
echo "client-wait-test: $failures case(s) failed"
exit 1
