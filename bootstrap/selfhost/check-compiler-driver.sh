#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
cd "$repo_root"

fail() {
    printf '%s\n' "FAIL: selfhost driver: $*" >&2
    exit 1
}

if command -v cc >/dev/null 2>&1; then
    compiler=cc
elif command -v clang >/dev/null 2>&1; then
    compiler=clang
elif command -v gcc >/dev/null 2>&1; then
    compiler=gcc
else
    fail "a C11 compiler is required"
fi

temporary=${TMPDIR:-/tmp}/kofun-selfhost-driver.$$
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary"

"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    bootstrap/stage2/compiler.c -o "$temporary/kofun-stage2"

# The trusted seed compiles the frozen S as one ordinary source-to-C
# command with no hidden fallback, deterministically, byte-identical to
# the checked-in evidence.
profile_digest=$(awk -F '|' '$1 == "source_sha256" { print $2 }' \
    bootstrap/selfhost/profile.meta)
actual_digest=$(sha256sum bootstrap/stage1/compiler.kofun | awk '{ print $1 }')
test "$profile_digest" = "$actual_digest" ||
    fail "S digest differs from the frozen profile"

"$temporary/kofun-stage2" --selfhost-compile \
    bootstrap/stage1/compiler.kofun "$temporary/S.c" \
    "$profile_digest" >/dev/null
cmp bootstrap/selfhost/driver/S.c "$temporary/S.c" ||
    fail "compiled S differs from the checked-in evidence"
"$temporary/kofun-stage2" --selfhost-compile \
    bootstrap/stage1/compiler.kofun "$temporary/S.second.c" \
    "$profile_digest" >/dev/null
cmp "$temporary/S.c" "$temporary/S.second.c" ||
    fail "compiled S is not deterministic"

# The compiler produced from S is runnable, and its behavior matches the
# audited Stage 1 seed byte for byte on the Core corpus: same emitted C,
# same stdout, same exit status, and the emitted program executes to the
# pinned output.
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    -I unicode "$temporary/S.c" -o "$temporary/kofun-a1"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    bootstrap/stage1/compiler.c -o "$temporary/kofun-stage1"

mkdir -p "$temporary/left" "$temporary/right"
cp bootstrap/selfhost/driver/corpus_answer.kofun "$temporary/left/input.kofun"
cp bootstrap/selfhost/driver/corpus_answer.kofun "$temporary/right/input.kofun"

(cd "$temporary/left" &&
    "$temporary/kofun-a1" input.kofun output.c >stdout.txt 2>stderr.txt)
(cd "$temporary/right" &&
    "$temporary/kofun-stage1" input.kofun output.c >stdout.txt 2>stderr.txt)
cmp "$temporary/left/output.c" "$temporary/right/output.c" ||
    fail "compiler-from-S and the audited seed emit different C"
cmp "$temporary/left/stdout.txt" "$temporary/right/stdout.txt" ||
    fail "compiler-from-S and the audited seed print different stdout"
cmp "$temporary/left/stderr.txt" "$temporary/right/stderr.txt" ||
    fail "compiler-from-S and the audited seed print different stderr"
cmp bootstrap/selfhost/driver/corpus_answer.c "$temporary/left/output.c" ||
    fail "corpus emission differs from the checked-in evidence"

"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/left/output.c" -o "$temporary/corpus-program"
"$temporary/corpus-program" >"$temporary/corpus.stdout"
cmp bootstrap/selfhost/driver/corpus_answer.stdout "$temporary/corpus.stdout" ||
    fail "corpus program output differs from the pinned golden"

# Path remapping: compiling the same relative input from two different
# directories produces byte-identical C — no absolute-path leakage.
mkdir -p "$temporary/remap-a/nested" "$temporary/remap-b"
cp bootstrap/selfhost/driver/corpus_answer.kofun \
    "$temporary/remap-a/nested/program.kofun"
cp bootstrap/selfhost/driver/corpus_answer.kofun \
    "$temporary/remap-b/program.kofun"
(cd "$temporary/remap-a/nested" &&
    "$temporary/kofun-a1" program.kofun remapped.c >/dev/null)
(cd "$temporary/remap-b" &&
    "$temporary/kofun-a1" program.kofun remapped.c >/dev/null)
cmp "$temporary/remap-a/nested/remapped.c" "$temporary/remap-b/remapped.c" ||
    fail "emitted C depends on the compilation directory"

# Failure corpus: an out-of-Core source is refused with the seed's exact
# diagnostic and writes nothing; the seed agrees byte for byte.
set +e
"$temporary/kofun-a1" bootstrap/selfhost/driver/corpus_reject.kofun \
    "$temporary/reject.c" >"$temporary/reject.stdout" 2>"$temporary/reject.stderr"
reject_status=$?
"$temporary/kofun-stage1" bootstrap/selfhost/driver/corpus_reject.kofun \
    "$temporary/reject-seed.c" >"$temporary/reject-seed.stdout" 2>/dev/null
reject_seed_status=$?
set -e
test "$reject_status" -eq "$reject_seed_status" ||
    fail "reject corpus exit status diverges from the audited seed"
cmp bootstrap/selfhost/driver/corpus_reject.stdout \
    "$temporary/reject.stdout" ||
    fail "reject corpus diagnostic differs from the pinned golden"
cmp "$temporary/reject.stdout" "$temporary/reject-seed.stdout" ||
    fail "reject corpus diagnostic diverges from the audited seed"
test ! -e "$temporary/reject.c" ||
    fail "a rejected corpus input must not produce C"
test ! -s "$temporary/reject.stderr" ||
    fail "the reject corpus wrote unexpected stderr"

# I/O failure: a missing input panics with the runtime's bounded message,
# exits 1, and preserves the previous output bytes.
printf 'previous output\n' > "$temporary/preserved.c"
set +e
"$temporary/kofun-a1" "$temporary/does-not-exist.kofun" \
    "$temporary/preserved.c" >"$temporary/io.stdout" 2>"$temporary/io.stderr"
io_status=$?
set -e
test "$io_status" -eq 1 || fail "missing input must exit 1"
grep -F 'Kofun runtime error: cannot open input file' \
    "$temporary/io.stderr" >/dev/null ||
    fail "missing input must report the bounded runtime diagnostic"
printf 'previous output\n' | cmp - "$temporary/preserved.c" ||
    fail "a failed compile must preserve the previous output"

# The driver never falls back: an out-of-profile source is rejected by
# the frontend before any lowering, with exit 1 and no C written.
set +e
"$temporary/kofun-stage2" --selfhost-compile \
    bootstrap/selfhost/frontend/reject_unsupported_match.kofun \
    "$temporary/no-fallback.c" \
    "$(sha256sum bootstrap/selfhost/frontend/reject_unsupported_match.kofun |
        awk '{ print $1 }')" >"$temporary/no-fallback.stdout"
no_fallback_status=$?
set -e
test "$no_fallback_status" -eq 1 ||
    fail "an out-of-profile source must exit 1"
grep '^error\[E2S10\]' "$temporary/no-fallback.stdout" >/dev/null ||
    fail "the driver must surface the frontend diagnostic"
test ! -e "$temporary/no-fallback.c" ||
    fail "a rejected source must not produce C"

printf '%s\n' \
    "PASS: the trusted seed compiles the frozen S into a runnable compiler" \
    "PASS: the compiler from S matches the audited Stage 1 seed byte for byte on the corpus" \
    "PASS: emission is deterministic, path-independent, and failure-preserving"
