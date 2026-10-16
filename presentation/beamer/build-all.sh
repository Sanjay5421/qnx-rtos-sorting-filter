#!/bin/sh
# Build every theme so they can be compared side by side.
#
#   ./build-all.sh
#
# Each theme compiles in its own folder; the shared sources stay one level up,
# so nothing needs copying. The PDFs land next to their wrappers:
#
#   theme-a/main-a.pdf  …  theme-d/main-d.pdf
#
# Run from this directory (presentation/beamer).

set -e

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

for t in a b c d; do
    dir="$root/theme-$t"
    [ -f "$dir/main-$t.tex" ] || continue

    printf '=== theme %s\n' "$t"
    ( cd "$dir" && pdflatex -interaction=nonstopmode "main-$t.tex" > build.log 2>&1 \
                && pdflatex -interaction=nonstopmode "main-$t.tex" > build.log 2>&1 ) || {
        printf '    FAILED — see %s\n' "$dir/build.log"
        continue
    }

    if grep -q '^! ' "$dir/build.log"; then
        printf '    errors:\n'
        grep '^! ' "$dir/build.log" | sed 's/^/      /'
    else
        pages=$(grep -o 'Output written on [^(]*([0-9]* page' "$dir/build.log" \
                | grep -o '([0-9]*' | tr -d '(')
        printf '    ok — %s pages\n' "$pages"
    fi
done

printf '\nPDFs:\n'
ls -1 "$root"/theme-*/main-*.pdf 2>/dev/null | sed 's/^/  /'
