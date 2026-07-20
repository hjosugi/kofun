#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
stage2="$root/bootstrap/stage2"

(
    cd "$root"
    sha256sum -c bootstrap/stage2/SHA256SUMS
)

if command -v cc >/dev/null 2>&1; then
    compiler=cc
elif command -v clang >/dev/null 2>&1; then
    compiler=clang
elif command -v gcc >/dev/null 2>&1; then
    compiler=gcc
else
    echo "stage2 check: a C11 compiler is required" >&2
    exit 1
fi

temporary=${TMPDIR:-/tmp}/kofun-stage2-check.$$
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary"

"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$stage2/compiler.c" -o "$temporary/kofun-stage2"

round_trip() {
    name=$1
    source=$2
    "$temporary/kofun-stage2" \
        "$source" \
        "$temporary/$name.kofun" \
        "$temporary/$name.ir" \
        "$temporary/$name.tokens" >/dev/null
    cmp "$source" "$temporary/$name.kofun"
    test -s "$temporary/$name.ir"
    test -s "$temporary/$name.tokens"
    grep '^kofun-stage2-ir/v1$' "$temporary/$name.ir" >/dev/null
    grep '^kofun-token-tape/v1$' "$temporary/$name.tokens" >/dev/null
}

round_trip fixture "$stage2/fixture.kofun"
grep '^function|classify|1|' "$temporary/fixture.ir" >/dev/null
grep '^function|main|0|' "$temporary/fixture.ir" >/dev/null
grep '^function-count|2$' "$temporary/fixture.ir" >/dev/null

round_trip stage1 "$root/bootstrap/stage1/compiler.kofun"
grep '^function|emit_c|1|' "$temporary/stage1.ir" >/dev/null
grep '^function|compile_file|2|' "$temporary/stage1.ir" >/dev/null
grep '^function-count|11$' "$temporary/stage1.ir" >/dev/null

round_trip stage2 "$stage2/compiler.kofun"
grep '^function|lex|1|' "$temporary/stage2.ir" >/dev/null
grep '^function|parse_program|1|' "$temporary/stage2.ir" >/dev/null
grep '^function|lower_c|1|' "$temporary/stage2.ir" >/dev/null
grep '^function|emit_kofun|2|' "$temporary/stage2.ir" >/dev/null
grep '^function|compile_file|4|' "$temporary/stage2.ir" >/dev/null

"$temporary/kofun-stage2" \
    "$stage2/compiler.kofun" \
    "$temporary/stage2-second.kofun" \
    "$temporary/stage2-second.ir" \
    "$temporary/stage2-second.tokens" >/dev/null
cmp "$temporary/stage2.kofun" "$temporary/stage2-second.kofun"
cmp "$temporary/stage2.ir" "$temporary/stage2-second.ir"
cmp "$temporary/stage2.tokens" "$temporary/stage2-second.tokens"

"$temporary/kofun-stage2" \
    "$stage2/core_fixture.kofun" \
    "$temporary/core.c" \
    "$temporary/core.ir" \
    "$temporary/core.tokens" >/dev/null
"$temporary/kofun-stage2" \
    "$stage2/core_fixture.kofun" \
    "$temporary/core-second.c" \
    "$temporary/core-second.ir" \
    "$temporary/core-second.tokens" >/dev/null
cmp "$temporary/core.c" "$temporary/core-second.c"
cmp "$temporary/core.ir" "$temporary/core-second.ir"
cmp "$temporary/core.tokens" "$temporary/core-second.tokens"
grep '^function|main|0|' "$temporary/core.ir" >/dev/null
grep 'kofun_mul' "$temporary/core.c" >/dev/null
grep 'kofun_floor_div' "$temporary/core.c" >/dev/null
grep 'kofun_floor_mod' "$temporary/core.c" >/dev/null
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/core.c" -o "$temporary/core-program"
"$temporary/core-program" >"$temporary/core.stdout" 2>"$temporary/core.stderr"
cmp "$stage2/core_fixture.stdout" "$temporary/core.stdout"
test ! -s "$temporary/core.stderr"

"$temporary/kofun-stage2" \
    "$stage2/core_error_fixture.kofun" \
    "$temporary/core-error.c" \
    "$temporary/core-error.ir" \
    "$temporary/core-error.tokens" >/dev/null
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/core-error.c" -o "$temporary/core-error-program"
set +e
"$temporary/core-error-program" \
    >"$temporary/core-error.stdout" 2>"$temporary/core-error.stderr"
core_error_status=$?
set -e
test "$core_error_status" -eq 1
test ! -s "$temporary/core-error.stdout"
cmp "$stage2/core_error_fixture.stderr" "$temporary/core-error.stderr"

set +e
"$temporary/kofun-stage2" \
    >"$temporary/usage.stdout" 2>"$temporary/usage.stderr"
usage_status=$?
"$temporary/kofun-stage2" \
    "$stage2/malformed.kofun" \
    "$temporary/malformed-output.kofun" \
    "$temporary/malformed.ir" \
    "$temporary/malformed.tokens" \
    >"$temporary/malformed.stdout" 2>"$temporary/malformed.stderr"
malformed_status=$?
"$temporary/kofun-stage2" \
    "$stage2/unsupported_core.kofun" \
    "$temporary/unsupported.c" \
    "$temporary/unsupported.ir" \
    "$temporary/unsupported.tokens" \
    >"$temporary/unsupported.stdout" 2>"$temporary/unsupported.stderr"
unsupported_status=$?
set -e

if test "$usage_status" -ne 2 ||
   test ! -s "$temporary/usage.stdout" ||
   test -s "$temporary/usage.stderr"
then
    echo "stage2 check: usage contract differs from canonical Kofun main" >&2
    exit 1
fi
if test "$malformed_status" -ne 1 ||
   test ! -s "$temporary/malformed.stdout" ||
   test -s "$temporary/malformed.stderr"
then
    echo "stage2 check: malformed-input contract differs from canonical Kofun main" >&2
    exit 1
fi
if test "$unsupported_status" -ne 1 ||
   test ! -s "$temporary/unsupported.stdout" ||
   test -s "$temporary/unsupported.stderr" ||
   test -e "$temporary/unsupported.c"
then
    echo "stage2 check: unsupported Core lowering contract changed" >&2
    exit 1
fi

if find "$stage2" -type f \( -name '*.py' -o -name '*.kf' \) | grep . >/dev/null
then
    echo "stage2 check: Python or .kf file found" >&2
    exit 1
fi

echo "stage2 semantic frontend check passed"
