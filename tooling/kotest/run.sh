#!/bin/sh
# kotest runner: discovers Kofun unit tests, generates a harness the way the
# Rust test harness collects #[test] functions, builds each unit through
# `kofun emit-c` plus a C11 compiler, and renders the kotest line protocol.
#
# usage:
#   tooling/kotest/run.sh [PATH ...] [--filter SUBSTRING] [--list]
#                         [--watch] [--no-color] [--keep-going]
#
# Test discovery:
#   - a file named *_test.kofun is a test suite: it must not define fn main,
#     and every `fn test_<name>() -> Int` in it is collected.
#   - Go-style companions: when `X_test.kofun` sits next to `X.kofun`, the
#     companion's code is compiled into the unit (its `fn main` demo block
#     removed), so the suite tests the real functions without duplication.
#   - any other .kofun file passed explicitly may embed tests next to its
#     code, Rust-style: its `fn main` block is removed before the generated
#     harness is appended, and its `fn test_*` functions are collected.
#   - a directory argument is searched for *_test.kofun files.
#
# Exit code: 0 when every collected test passes, 1 otherwise, 2 on usage or
# build errors.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KOTEST_LIB="$ROOT/stdlib/testing/kotest.kofun"
CC=${CC:-cc}
CFLAGS_KOTEST="-std=c11 -O2 -Wall -Wextra -Werror \
    -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter"

usage() {
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
}

# ------------------------------------------------------------------ options
filter=''
list_only=false
watch=false
keep_going=false
color=auto
paths=''
while [ "$#" -gt 0 ]; do
    case $1 in
    --filter)
        [ "$#" -ge 2 ] || { usage >&2; exit 2; }
        filter=$2
        shift 2
        ;;
    --list) list_only=true; shift ;;
    --watch) watch=true; shift ;;
    --no-color) color=never; shift ;;
    --keep-going) keep_going=true; shift ;;
    -h|--help) usage; exit 0 ;;
    --*) printf 'kotest: unknown option %s\n' "$1" >&2; exit 2 ;;
    *) paths="$paths
$1"; shift ;;
    esac
done
[ -n "$paths" ] || paths="$ROOT/stdlib
$ROOT/examples/stdlib
$ROOT/tests"

# ------------------------------------------------------------------- colour
if [ "$color" = never ] || [ -n "${NO_COLOR:-}" ] || [ ! -t 1 ]; then
    C_GREEN=''; C_RED=''; C_YELLOW=''; C_DIM=''; C_BOLD=''; C_OFF=''
else
    C_GREEN=$(printf '\033[32m'); C_RED=$(printf '\033[31m')
    C_YELLOW=$(printf '\033[33m'); C_DIM=$(printf '\033[2m')
    C_BOLD=$(printf '\033[1m'); C_OFF=$(printf '\033[0m')
fi

# ---------------------------------------------------------------- discovery
discover() {
    printf '%s\n' "$paths" | while IFS= read -r path; do
        [ -n "$path" ] || continue
        if [ -f "$path" ]; then
            printf '%s\n' "$path"
        elif [ -d "$path" ]; then
            # Suites under a kotest fixtures directory are deliberately red
            # (the gate runs them by explicit path to prove failure is
            # reported), so a directory sweep must not collect them.
            find "$path" -type f -name '*_test.kofun' \
                -not -path '*/kotest/fixtures/*' -print
        else
            printf 'kotest: no such path: %s\n' "$path" >&2
            exit 2
        fi
    done | LC_ALL=C sort -u
}

test_names() {
    sed -n 's/^fn \(test_[a-z0-9_]*\)().*$/\1/p' "$1"
}

# Remove a top-level `fn main` block: sample files embed a demo main that the
# generated harness replaces.  Top-level braces close in column zero, which
# tooling/kotest/README.md states as the required style for embedded tests.
strip_main() {
    awk '
        /^fn main\(/ { inside = 1; next }
        inside == 1 && /^}/ { inside = 0; next }
        inside == 1 { next }
        { print }
    ' "$1"
}

# ------------------------------------------------------------------ harness
emit_harness() {
    unit_label=$1
    names_file=$2
    printf '\nfn main() -> Int {\n'
    printf '    let mut kotest_total = 0\n'
    printf '    let mut kotest_failed = 0\n'
    index=0
    while IFS= read -r name; do
        [ -n "$name" ] || continue
        index=$((index + 1))
        cat <<HARNESS
    kotest_total = kotest_total + 1
    print("KOTEST-RUN $unit_label.$name")
    let kotest_outcome_$index = $name()
    if kotest_outcome_$index == 0 {
        print("KOTEST-OK $unit_label.$name")
    } else {
        print("KOTEST-FAILED $unit_label.$name")
        kotest_failed = kotest_failed + 1
    }
HARNESS
    done <"$names_file"
    cat <<'HARNESS'
    let kotest_internal = kotest_selfcheck()
    if kotest_internal != 0 {
        print("KOTEST-FAILED kotest.selfcheck")
        kotest_total = kotest_total + 1
        kotest_failed = kotest_failed + 1
    }
    return kotest_summary(kotest_total, kotest_failed)
HARNESS
    printf '}\n'
}

# ------------------------------------------------------------------- render
render() {
    in_summary=false
    while IFS= read -r line; do
        if $in_summary; then
            # The summary block is consumed by the aggregator; the shell
            # prints the human summary line instead.
            continue
        fi
        case $line in
        'KOTEST-RUN '*)
            printf '%s❯ %s%s\n' "$C_DIM" "${line#KOTEST-RUN }" "$C_OFF" ;;
        'KOTEST-OK '*)
            printf '  %s✓ %s%s\n' "$C_GREEN" "${line#KOTEST-OK }" "$C_OFF" ;;
        'KOTEST-FAILED '*)
            printf '  %s✗ %s%s\n' "$C_RED" "${line#KOTEST-FAILED }" "$C_OFF" ;;
        'KOTEST-ASSERT-FAIL '*)
            printf '    %s%s%s\n' "$C_RED" "$line" "$C_OFF" ;;
        'KOTEST-SUMMARY')
            in_summary=true ;;
        *)
            printf '    %s\n' "$line" ;;
        esac
    done
}

# --------------------------------------------------------------- run a unit
run_unit() {
    source_file=$1
    work=$2
    stem=$(basename "$source_file" .kofun)
    unit="$work/$stem.unit.kofun"
    names="$work/$stem.names"

    test_names "$source_file" >"$names.all"
    if [ -n "$filter" ]; then
        grep -F -- "$filter" "$names.all" >"$names" || : >"$names"
    else
        cp "$names.all" "$names"
    fi
    if ! [ -s "$names" ]; then
        return 3
    fi

    case $source_file in
    *_test.kofun)
        if grep -q '^fn main(' "$source_file"; then
            printf 'kotest: %s: a *_test.kofun suite must not define fn main\n' \
                "$source_file" >&2
            return 2
        fi
        companion="${source_file%_test.kofun}.kofun"
        if [ -f "$companion" ]; then
            {
                cat "$KOTEST_LIB"
                printf '\n'
                strip_main "$companion"
                printf '\n'
                cat "$source_file"
            } >"$unit"
        else
            { cat "$KOTEST_LIB"; printf '\n'; cat "$source_file"; } >"$unit"
        fi
        ;;
    *)
        { cat "$KOTEST_LIB"; printf '\n'; strip_main "$source_file"; } >"$unit"
        ;;
    esac
    emit_harness "$stem" "$names" >>"$unit"

    if ! "$ROOT/bin/kofun" emit-c "$unit" "$work/$stem.c" \
        >"$work/$stem.emit.log" 2>&1; then
        printf '%skotest: BUILD FAIL %s%s\n' "$C_RED" "$source_file" "$C_OFF"
        sed 's/^/    /' "$work/$stem.emit.log"
        printf '    %sunit kept at %s%s\n' "$C_DIM" "$unit" "$C_OFF"
        return 2
    fi
    # shellcheck disable=SC2086
    if ! "$CC" $CFLAGS_KOTEST \
        -I"$ROOT/bootstrap/stage2" -I"$ROOT/unicode" -I"$ROOT/vendor/utf8proc" \
        "$work/$stem.c" -o "$work/$stem.bin" 2>"$work/$stem.cc.log"; then
        printf '%skotest: C BUILD FAIL %s%s\n' "$C_RED" "$source_file" "$C_OFF"
        sed 's/^/    /' "$work/$stem.cc.log"
        return 2
    fi

    set +e
    "$work/$stem.bin" >"$work/$stem.out" 2>"$work/$stem.err"
    unit_status=$?
    set -e
    render <"$work/$stem.out"
    if [ -s "$work/$stem.err" ]; then
        printf '%skotest: unexpected stderr from %s%s\n' \
            "$C_RED" "$source_file" "$C_OFF"
        sed 's/^/    /' "$work/$stem.err"
        [ "$unit_status" -ne 0 ] || unit_status=2
    fi
    return "$unit_status"
}

# ------------------------------------------------------------------- a pass
run_pass() {
    work=$(mktemp -d "${TMPDIR:-/tmp}/kotest.XXXXXX")
    files=$(discover)
    if [ -z "$files" ]; then
        printf 'kotest: no test files found\n' >&2
        rm -rf "$work"
        return 2
    fi

    if $list_only; then
        printf '%s\n' "$files" | while IFS= read -r file; do
            stem=$(basename "$file" .kofun)
            test_names "$file" | while IFS= read -r name; do
                [ -z "$filter" ] || case $name in
                    *"$filter"*) ;;
                    *) continue ;;
                esac
                printf '%s.%s\n' "$stem" "$name"
            done
        done
        rm -rf "$work"
        return 0
    fi

    pass_total=0
    pass_failed=0
    pass_suites=0
    overall=0
    for file in $files; do
        # The call sits in a condition context so errexit cannot fire on a
        # failing suite, even though run_unit toggles set -e internally.
        if run_unit "$file" "$work"; then
            unit_status=0
        else
            unit_status=$?
        fi
        case $unit_status in
        3) continue ;;
        0) ;;
        *)
            overall=1
            if ! $keep_going && [ "$unit_status" -eq 2 ]; then
                rm -rf "$work"
                return 2
            fi
            ;;
        esac
        pass_suites=$((pass_suites + 1))
        totals=$(awk '
            /^KOTEST-SUMMARY$/ { summary = 1; next }
            summary == 1 && /^tests total:$/ { want = "total"; next }
            summary == 1 && /^tests failed:$/ { want = "failed"; next }
            summary == 1 && want == "total" && /^[0-9]+$/ {
                total += $1; want = ""; next
            }
            summary == 1 && want == "failed" && /^[0-9]+$/ {
                failed += $1; want = ""; next
            }
            END { printf "%d %d", total, failed }
        ' "$work/$(basename "$file" .kofun).out" 2>/dev/null || printf '0 0')
        pass_total=$((pass_total + ${totals% *}))
        pass_failed=$((pass_failed + ${totals#* }))
    done

    printf '\n'
    if [ "$pass_failed" -eq 0 ] && [ "$overall" -eq 0 ]; then
        printf '%s%sTests%s  %s%d passed%s (%d total, %d suites)\n' \
            "$C_BOLD" "$C_GREEN" "$C_OFF" \
            "$C_GREEN" "$pass_total" "$C_OFF" "$pass_total" "$pass_suites"
    else
        printf '%s%sTests%s  %s%d failed%s | %d passed (%d total, %d suites)\n' \
            "$C_BOLD" "$C_RED" "$C_OFF" \
            "$C_RED" "$pass_failed" "$C_OFF" \
            "$((pass_total - pass_failed))" "$pass_total" "$pass_suites"
    fi
    rm -rf "$work"
    return "$overall"
}

# -------------------------------------------------------------------- watch
if $watch; then
    stamp=$(mktemp "${TMPDIR:-/tmp}/kotest-stamp.XXXXXX")
    trap 'rm -f "$stamp"' 0 1 2 15
    while :; do
        clear 2>/dev/null || printf '\033[2J\033[H'
        printf '%skotest watch%s %s(re-runs on change, Ctrl-C quits)%s\n\n' \
            "$C_BOLD" "$C_OFF" "$C_DIM" "$C_OFF"
        if run_pass; then :; else :; fi
        touch "$stamp"
        while :; do
            sleep 1
            changed=$(printf '%s\n' "$paths" | while IFS= read -r path; do
                [ -n "$path" ] && [ -e "$path" ] || continue
                find "$path" -name '*.kofun' -newer "$stamp" -print -quit
            done)
            changed_lib=$(find "$KOTEST_LIB" -newer "$stamp" -print -quit)
            [ -z "$changed$changed_lib" ] || break
        done
    done
fi

run_pass
