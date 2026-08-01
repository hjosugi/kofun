#!/bin/sh
set -eu

# Counts assertions that fail without saying anything, and holds every file to a
# recorded budget.
#
# A silent assertion is a `test` or `[` command that
#
#   1. is at top level, not inside a function body where it may be a return
#      value rather than an assertion;
#   2. carries no `||` or `&&` handler; and
#   3. is not the condition of an `if`.
#
# Under `set -e` each one aborts its gate with an empty stderr. #814 counted 460
# of them across 43 files and is driving the number to zero; this gate is what
# stops them coming back while that work is in flight.
#
# The budget is exact in both directions. A file over its budget has regressed.
# A file *under* its budget has been improved without the improvement being
# recorded, which leaves slack for the next regression to hide in — so that
# fails too, and the fix is to lower the number in the same change.
#
#   sh tests/assertions/check.sh            check every file against the budget
#   sh tests/assertions/check.sh --count    print the current counts, for
#                                           regenerating the budget file

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUDGET="$ROOT/tests/assertions/budget.tsv"
ASSERT_CONTEXT="assertion budget"
. "$ROOT/tests/assertions/assert.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-assertions.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

# Counts one script. Continuation lines, `||`/`&&` at end of line, heredoc
# bodies, and function bodies are all handled, because each of them otherwise
# turns into a wrong number.
count_file() {
    awk '
        function flush(  s) {
            if (buf == "") return
            s = buf
            sub(/^[ \t]+/, "", s)
            if (start_depth == 0 &&
                s ~ /^(test|\[)[ \t]/ &&
                s !~ /\|\|/ &&
                s !~ /&&/ &&
                s !~ /;[ \t]*then/) {
                n++
            }
            buf = ""
        }
        {
            line = $0
            if (heredoc != "") {
                t = line
                sub(/^[ \t]+/, "", t)
                sub(/[ \t]+$/, "", t)
                if (t == heredoc) heredoc = ""
                next
            }
            if (buf == "") start_depth = depth
            if (line ~ /^[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*\(\)[ \t]*\{/) depth++
            else if (line ~ /^\}/ && depth > 0) depth--

            t = line
            sub(/[ \t]+$/, "", t)
            buf = (buf == "") ? t : buf " " t

            if (buf ~ /\\$/) { sub(/\\$/, "", buf); next }
            if (buf ~ /(\|\||&&)$/) next

            if (line ~ /<<-?[ \t]*['"'"'"]?[A-Za-z_][A-Za-z0-9_]*['"'"'"]?/) {
                tag = line
                sub(/^.*<<-?[ \t]*/, "", tag)
                sub(/[^A-Za-z0-9_'"'"'"].*$/, "", tag)
                gsub(/['"'"'"]/, "", tag)
                if (tag != "") heredoc = tag
            }
            flush()
        }
        END { flush(); print n + 0 }
    ' "$1"
}

scripts() {
    git -C "$ROOT" ls-files '*.sh' |
        grep -v '^vendor/' |
        grep -v '^examples/rust-shim/'
}

scripts | while IFS= read -r f; do
    printf '%s\t%s\n' "$(count_file "$ROOT/$f")" "$f"
done >"$WORK/actual.tsv"

if test "${1:-}" = "--count"; then
    awk -F '\t' '$1 != 0' "$WORK/actual.tsv" | sort -k2,2
    exit 0
fi

assert_file_nonempty "the budget file" "$BUDGET"
grep -v '^#' "$BUDGET" | grep -v '^[[:space:]]*$' | sort -k2,2 >"$WORK/budget.tsv"
awk -F '\t' '$1 != 0' "$WORK/actual.tsv" | sort -k2,2 >"$WORK/actual-nonzero.tsv"

status=0
total=0
while IFS='	' read -r want file; do
    have=$(awk -F '\t' -v f="$file" '$2 == f { print $1 }' "$WORK/actual.tsv")
    if test -z "$have"; then
        printf 'FAIL: assertion budget: %s is in the budget but not in the tree\n' \
            "$file" >&2
        status=1
        continue
    fi
    if test "$have" -gt "$want"; then
        printf 'FAIL: assertion budget: %s has %s silent assertions, budget is %s — %s new one(s)\n' \
            "$file" "$have" "$want" "$((have - want))" >&2
        status=1
    elif test "$have" -lt "$want"; then
        printf 'FAIL: assertion budget: %s has %s silent assertions but its budget still says %s — lower the budget in the same change\n' \
            "$file" "$have" "$want" >&2
        status=1
    fi
    total=$((total + have))
done <"$WORK/budget.tsv"

while IFS='	' read -r have file; do
    if ! awk -F '\t' -v f="$file" '$2 == f { found = 1 } END { exit !found }' \
        "$WORK/budget.tsv"
    then
        printf 'FAIL: assertion budget: %s has %s silent assertions and no budget entry\n' \
            "$file" "$have" >&2
        status=1
    fi
done <"$WORK/actual-nonzero.tsv"

if test "$status" -ne 0; then
    printf '%s\n' \
        'The rule is #814: no gate may exit non-zero without naming the check, the expectation, and the observation. Use tests/assertions/assert.sh.' >&2
    exit 1
fi

printf 'PASS: every script is at its recorded silent-assertion budget (%s remaining, %s at zero)\n' \
    "$total" "$(awk -F '\t' '$1 == 0' "$WORK/budget.tsv" | wc -l | tr -d ' ')"
