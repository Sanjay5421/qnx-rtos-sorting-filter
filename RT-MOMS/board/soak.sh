#!/bin/sh
#
# soak.sh -- the repeated-request check: N client runs against one server.
#
#   sh soak.sh [runs]
#
# What it checks, per run:
#   - the client exits 0 and reports a completed run;
#   - the block medians it wrote are identical to the first run's. The medians
#     are a property of the recording, so any difference means something in the
#     pipeline moved between runs -- exactly what M4 asks about ("behaviour
#     under repeated requests").
# It also prints each run's own timing line, so a report can show that the
# service stays put run after run.
#
#   RUNS=10                      number of runs
#   WORK=/var/soak               where the runs happen
#   PAYLOAD=..., LAYOUT=..., CHANNEL=..., RATE=...
#
# Exit status is 0 only if every run succeeded and every run agreed.

set -u

RUNS=${RUNS:-${1:-10}}
WORK=${WORK:-/var/soak}
PAYLOAD=${PAYLOAD:-/data/bin/Chen2020_c1_T25_discharge.bin}
LAYOUT=${LAYOUT:-interleaved}
CHANNEL=${CHANNEL:-voltage}
RATE=${RATE:-100}

mkdir -p "$WORK" || exit 1

# One server for every run: the point is that it stays up and keeps answering.
# The client waits for it, so no sleep is needed before the first run.
sortmed-server > "$WORK/server.log" 2>&1 &
srv=$!

failures=0
first_medians=
i=1
while [ "$i" -le "$RUNS" ]; do
    log=$WORK/run$i.log
    if ( cd "$WORK" && sortmed-client "$PAYLOAD" "$LAYOUT" "$CHANNEL" "$RATE" ) \
            > "$log" 2>&1; then
        rc=0
    else
        rc=$?
    fi

    # medians.csv is rewritten by every run; its checksum is the invariant.
    medians=$(cksum < "$WORK/medians.csv" 2>/dev/null)

    if [ "$rc" -ne 0 ]; then
        echo "run $i: FAILED (exit $rc); see $log"
        failures=$((failures + 1))
    elif [ -z "$first_medians" ]; then
        first_medians=$medians
        echo "run $i: ok"
    elif [ "$medians" != "$first_medians" ]; then
        echo "run $i: DIFFERENT medians -- $medians, first run was $first_medians"
        failures=$((failures + 1))
    else
        echo "run $i: ok, same medians"
    fi

    while read -r line; do
        case "$line" in
            "timing: run_ns"*) echo "        $line" ;;
        esac
    done < "$log"

    i=$((i + 1))
done

kill "$srv" 2>/dev/null
wait "$srv" 2>/dev/null

if [ "$failures" -eq 0 ]; then
    echo "soak: $RUNS runs, all completed, all with identical medians"
    exit 0
fi
echo "soak: $failures of $RUNS runs failed or disagreed"
exit 1
