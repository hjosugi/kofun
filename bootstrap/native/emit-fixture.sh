#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
NATIVE="$ROOT/bootstrap/native"
KOFUN="$ROOT/bin/kofun"

usage() {
    printf '%s\n' "usage: $0 [-g] -o OUTPUT" >&2
}

debug=false
output=
while test "$#" -gt 0; do
    case $1 in
        -g)
            debug=true
            shift
            ;;
        -o)
            test "$#" -ge 2 || {
                usage
                exit 2
            }
            output=$2
            shift 2
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

test -n "$output" || {
    usage
    exit 2
}

work=$(mktemp -d "${TMPDIR:-/tmp}/kofun-native-fixture.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$(dirname -- "$output")"
temporary="$output.tmp.$$"
trap 'rm -rf "$work"; rm -f "$temporary"' EXIT HUP INT TERM

if "$debug"; then
    fixture="$NATIVE/fixtures/core_answer_debug.packed.kofun"
    format=packed
    expected_size=1360
else
    fixture="$NATIVE/fixtures/core_answer.rle.kofun"
    format=rle
    expected_size=231
fi

"$KOFUN" build "$fixture" \
    -o "$work/emitter" \
    --emit-c "$work/emitter.c" >/dev/null
"$work/emitter" >"$work/encoded"

: >"$temporary"
pending=
while IFS= read -r field; do
    case $field in
        ''|*[!0-9]*)
            printf '%s\n' "native fixture: invalid numeric field: $field" >&2
            exit 1
            ;;
    esac

    if test -z "$pending"; then
        pending=$field
        continue
    fi

    if test "$format" = rle; then
        byte=$pending
        count=$field
        test "$byte" -le 255 || {
            printf '%s\n' "native fixture: byte outside 0..255: $byte" >&2
            exit 1
        }
        test "$count" -gt 0 || {
            printf '%s\n' "native fixture: run length must be positive" >&2
            exit 1
        }
        octal=$(printf '%03o' "$byte")
        index=0
        while test "$index" -lt "$count"; do
            printf "\\$octal" >>"$temporary"
            index=$((index + 1))
        done
    else
        word=$pending
        count=$field
        test "$count" -ge 1 && test "$count" -le 6 || {
            printf '%s\n' \
                "native fixture: packed byte count must be within 1..6" >&2
            exit 1
        }
        index=0
        while test "$index" -lt "$count"; do
            byte=$((word % 256))
            octal=$(printf '%03o' "$byte")
            printf "\\$octal" >>"$temporary"
            word=$((word / 256))
            index=$((index + 1))
        done
        test "$word" -eq 0 || {
            printf '%s\n' "native fixture: packed word exceeds byte count" >&2
            exit 1
        }
    fi
    pending=
done <"$work/encoded"

test -z "$pending" || {
    printf '%s\n' "native fixture: encoded stream has an incomplete pair" >&2
    exit 1
}
test "$(wc -c <"$temporary" | tr -d ' ')" -eq "$expected_size"
chmod +x "$temporary"
mv "$temporary" "$output"
