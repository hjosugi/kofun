#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MATRIX="$ROOT/stdlib/capabilities.tsv"

test -f "$MATRIX" || {
    echo "missing capability matrix: $MATRIX" >&2
    exit 1
}

tab=$(printf '\t')
header="capability${tab}tier${tab}state${tab}evidence${tab}issue${tab}note"
actual_header=$(sed -n '1p' "$MATRIX")

test "$actual_header" = "$header" || {
    echo "invalid capability matrix header" >&2
    exit 1
}

awk -F '\t' '
BEGIN {
    ok = 1
    previous = ""
    valid_tier["prelude"] = 1
    valid_tier["portable"] = 1
    valid_tier["platform"] = 1
    valid_tier["official"] = 1
    valid_state["implemented"] = 1
    valid_state["specified"] = 1
    valid_state["planned"] = 1
    valid_state["deferred"] = 1
    valid_state["non-goal"] = 1
}
NR == 1 { next }
{
    if (NF != 6) {
        printf "capability matrix line %d has %d fields, expected 6\n", NR, NF > "/dev/stderr"
        ok = 0
    }
    if ($1 !~ /^[a-z][a-z0-9_]*$/) {
        printf "invalid capability identifier on line %d: %s\n", NR, $1 > "/dev/stderr"
        ok = 0
    }
    if (!valid_tier[$2]) {
        printf "invalid tier on line %d: %s\n", NR, $2 > "/dev/stderr"
        ok = 0
    }
    if (!valid_state[$3]) {
        printf "invalid state on line %d: %s\n", NR, $3 > "/dev/stderr"
        ok = 0
    }
    if ($4 == "" || $5 !~ /^#[0-9]+(,#[0-9]+)*$/ || $6 == "") {
        printf "missing evidence, issue, or note on line %d\n", NR > "/dev/stderr"
        ok = 0
    }
    if (($3 == "implemented" || $3 == "specified") && $4 == "-") {
        printf "%s requires evidence for state %s\n", $1, $3 > "/dev/stderr"
        ok = 0
    }
    if ($1 in seen) {
        printf "duplicate capability: %s\n", $1 > "/dev/stderr"
        ok = 0
    }
    if (previous != "" && $1 <= previous) {
        printf "capabilities are not strictly sorted: %s after %s\n", $1, previous > "/dev/stderr"
        ok = 0
    }
    seen[$1] = 1
    previous = $1
}
END {
    if (NR < 2) {
        print "capability matrix has no rows" > "/dev/stderr"
        ok = 0
    }
    exit ok ? 0 : 1
}
' "$MATRIX"

for required_capability in \
    archive_compression array benchmark_harness binary_heap buffered_io \
    calendar_dates cli_framework clock crypto_tls csv decimal \
    environment_process filesystem hash_checksums http_client http_server \
    json list logging map mime profiling pseudorandom raw_threads regex \
    scoped_concurrency secure_random set temporary_files testing text_bytes \
    toml tuple url vector yaml
do
    grep -q "^${required_capability}${tab}" "$MATRIX" || {
        echo "required capability is missing: $required_capability" >&2
        exit 1
    }
done

for required_state in implemented specified planned deferred non-goal
do
    awk -F '\t' -v state="$required_state" 'NR > 1 && $3 == state { found = 1 } END { exit found ? 0 : 1 }' \
        "$MATRIX" || {
        echo "capability matrix does not exercise state: $required_state" >&2
        exit 1
    }
done

while IFS="$tab" read -r capability tier state evidence issue note
do
    test "$capability" != "capability" || continue
    test "$evidence" != "-" || continue
    test -e "$ROOT/$evidence" || {
        echo "missing evidence for $capability: $evidence" >&2
        exit 1
    }
done < "$MATRIX"

printf '%s\n' "PASS: standard-library capability matrix"
