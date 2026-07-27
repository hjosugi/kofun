#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
cd "$repo_root"

fail() {
    printf '%s\n' "FAIL: selfhost driver: $*" >&2
    exit 1
}

# This gate takes no options. It used to ignore whatever it was handed,
# so an invocation naming a phase this script never implemented — the
# `--phase` interface belongs to check-profile.sh — still reported PASS.
test "$#" -eq 0 ||
    fail "unexpected argument \`$1\`: this gate takes none (\`--phase\` belongs to check-profile.sh)"

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

# The Bool/comparison slice takes the same two compiler paths. Its checked-in
# C proves all six comparisons, Bool literals and bindings, `!`, precedence,
# and the nested left-associative `&&`/`||` shape. Executing it proves real
# short circuiting: both skipped right operands contain `1 // 0`.
mkdir -p "$temporary/bool-left" "$temporary/bool-right"
cp bootstrap/selfhost/driver/corpus_bool.kofun \
    "$temporary/bool-left/input.kofun"
cp bootstrap/selfhost/driver/corpus_bool.kofun \
    "$temporary/bool-right/input.kofun"
(cd "$temporary/bool-left" &&
    "$temporary/kofun-a1" input.kofun output.c >stdout.txt 2>stderr.txt)
(cd "$temporary/bool-right" &&
    "$temporary/kofun-stage1" input.kofun output.c >stdout.txt 2>stderr.txt)
cmp "$temporary/bool-left/output.c" "$temporary/bool-right/output.c" ||
    fail "compiler-from-S and the audited seed emit different Bool C"
cmp "$temporary/bool-left/stdout.txt" "$temporary/bool-right/stdout.txt" ||
    fail "compiler-from-S and the audited seed differ on Bool stdout"
cmp "$temporary/bool-left/stderr.txt" "$temporary/bool-right/stderr.txt" ||
    fail "compiler-from-S and the audited seed differ on Bool stderr"
cmp bootstrap/selfhost/driver/corpus_bool.c \
    "$temporary/bool-left/output.c" ||
    fail "Bool corpus emission differs from the checked-in evidence"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/bool-left/output.c" -o "$temporary/bool-program"
"$temporary/bool-program" >"$temporary/bool.stdout"
cmp bootstrap/selfhost/driver/corpus_bool.stdout "$temporary/bool.stdout" ||
    fail "Bool corpus program output differs from the pinned golden"

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
# Agreement alone is not the criterion: both seeds returned 0 here for as
# long as `main` discarded `compile_file`'s Bool, so this gate reported
# PASS while every refused compile still exited successfully.
test "$reject_status" -ne 0 ||
    fail "an unsupported source must exit nonzero"
cmp bootstrap/selfhost/driver/corpus_reject.stdout \
    "$temporary/reject.stdout" ||
    fail "reject corpus diagnostic differs from the pinned golden"
cmp "$temporary/reject.stdout" "$temporary/reject-seed.stdout" ||
    fail "reject corpus diagnostic diverges from the audited seed"
test ! -e "$temporary/reject.c" ||
    fail "a rejected corpus input must not produce C"
test ! -s "$temporary/reject.stderr" ||
    fail "the reject corpus wrote unexpected stderr"

# Type boundaries introduced with Bool are rejected identically by both
# compilers, exit nonzero, and never leave a partial output artifact.
for fixture in \
    bootstrap/selfhost/driver/corpus_reject_bool_arithmetic.kofun \
    bootstrap/selfhost/driver/corpus_reject_bool_print.kofun \
    bootstrap/selfhost/driver/corpus_reject_bool_annotation.kofun \
    bootstrap/selfhost/driver/corpus_reject_bool_infer_annotation.kofun \
    bootstrap/selfhost/driver/corpus_reject_bool_keyword_binding.kofun \
    bootstrap/selfhost/driver/corpus_reject_bool_order.kofun \
    bootstrap/selfhost/driver/corpus_reject_logical_int.kofun \
    bootstrap/selfhost/driver/corpus_reject_not_int.kofun \
    bootstrap/selfhost/driver/corpus_reject_single_pipe.kofun
do
    stem=$(basename "$fixture" .kofun)
    set +e
    "$temporary/kofun-a1" "$fixture" \
        "$temporary/$stem.c" >"$temporary/$stem.stdout" 2>"$temporary/$stem.stderr"
    a1_status=$?
    "$temporary/kofun-stage1" "$fixture" \
        "$temporary/$stem-seed.c" >"$temporary/$stem-seed.stdout" \
        2>"$temporary/$stem-seed.stderr"
    seed_status=$?
    set -e
    test "$a1_status" -eq "$seed_status" ||
        fail "$stem status diverges from the audited seed"
    test "$a1_status" -ne 0 ||
        fail "$stem must exit nonzero"
    cmp bootstrap/selfhost/driver/corpus_reject.stdout \
        "$temporary/$stem.stdout" ||
        fail "$stem diagnostic differs from the pinned refusal"
    cmp "$temporary/$stem.stdout" "$temporary/$stem-seed.stdout" ||
        fail "$stem diagnostic diverges from the audited seed"
    cmp "$temporary/$stem.stderr" "$temporary/$stem-seed.stderr" ||
        fail "$stem stderr diverges from the audited seed"
    test ! -e "$temporary/$stem.c" ||
        fail "$stem produced C through the compiler from S"
    test ! -e "$temporary/$stem-seed.c" ||
        fail "$stem produced C through the audited seed"
done

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
    "PASS: Int/Bool typing, comparisons, and short-circuiting agree across both seeds" \
    "PASS: emission is deterministic, path-independent, and failure-preserving"
