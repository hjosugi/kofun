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

# One accept corpus through both compiler paths. Every corpus asserts the same
# five things, so they are asserted in one place: a second copy of this sequence
# per corpus was how the loop slice's differential nearly shipped comparing the
# wrong pair. The two compilers run in separate directories with an identically
# named input, which is what keeps the emitted C free of the path it came from.
#
# This is deduplication of the harness, not of the evidence. The two compilers
# it compares stay independently derived — that pair is the differential, and
# collapsing it would delete the property this gate exists to prove.
differential_corpus() {
    stem=$1
    label=$2
    source=bootstrap/selfhost/driver/$stem.kofun
    left=$temporary/$stem-left
    right=$temporary/$stem-right

    mkdir -p "$left" "$right"
    cp "$source" "$left/input.kofun"
    cp "$source" "$right/input.kofun"
    (cd "$left" &&
        "$temporary/kofun-a1" input.kofun output.c >stdout.txt 2>stderr.txt)
    (cd "$right" &&
        "$temporary/kofun-stage1" input.kofun output.c >stdout.txt 2>stderr.txt)

    cmp "$left/output.c" "$right/output.c" ||
        fail "compiler-from-S and the audited seed emit different $label C"
    cmp "$left/stdout.txt" "$right/stdout.txt" ||
        fail "compiler-from-S and the audited seed differ on $label stdout"
    cmp "$left/stderr.txt" "$right/stderr.txt" ||
        fail "compiler-from-S and the audited seed differ on $label stderr"
    cmp "bootstrap/selfhost/driver/$stem.c" "$left/output.c" ||
        fail "$label corpus emission differs from the checked-in evidence"

    "$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
        "$left/output.c" -o "$temporary/$stem-program"
    "$temporary/$stem-program" >"$temporary/$stem.stdout"
    cmp "bootstrap/selfhost/driver/$stem.stdout" "$temporary/$stem.stdout" ||
        fail "$label corpus program output differs from the pinned golden"
}

differential_trap_corpus() {
    stem=$1
    label=$2
    source=bootstrap/selfhost/driver/$stem.kofun
    left=$temporary/$stem-left
    right=$temporary/$stem-right

    mkdir -p "$left" "$right"
    cp "$source" "$left/input.kofun"
    cp "$source" "$right/input.kofun"
    (cd "$left" &&
        "$temporary/kofun-a1" input.kofun output.c >stdout.txt 2>stderr.txt)
    (cd "$right" &&
        "$temporary/kofun-stage1" input.kofun output.c >stdout.txt 2>stderr.txt)

    cmp "$left/output.c" "$right/output.c" ||
        fail "compiler-from-S and the audited seed emit different $label C"
    cmp "$left/stdout.txt" "$right/stdout.txt" ||
        fail "compiler-from-S and the audited seed differ on $label compile stdout"
    cmp "$left/stderr.txt" "$right/stderr.txt" ||
        fail "compiler-from-S and the audited seed differ on $label compile stderr"

    "$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
        "$left/output.c" -o "$temporary/$stem-program"
    set +e
    "$temporary/$stem-program" \
        >"$temporary/$stem.stdout" 2>"$temporary/$stem.stderr"
    status=$?
    set -e
    test "$status" -eq 1 ||
        fail "$label program must exit 1"
    test ! -s "$temporary/$stem.stdout" ||
        fail "$label program wrote unexpected stdout"
    cmp "bootstrap/selfhost/driver/$stem.stderr" "$temporary/$stem.stderr" ||
        fail "$label runtime diagnostic differs from the pinned golden"
}

# The arithmetic corpus is the baseline: same emitted C, stdout, stderr and exit
# status, and the emitted program reproduces the pinned output.
differential_corpus corpus_answer arithmetic

# Top-level declarations are part of S's profile now. A helper with an
# explicit result type must lower identically through the generated compiler
# and the audited hand-port, then execute with its pinned result.
differential_corpus corpus_function function-declaration

# The Bool/comparison slice: all six comparisons, Bool literals and bindings,
# `!`, precedence, and the nested left-associative `&&`/`||` shape. Executing it
# proves real short circuiting — both skipped right operands contain `1 // 0`.
differential_corpus corpus_bool Bool

# The nested-block slice: one C brace per Kofun block, `else if` chains that do
# not accumulate braces, and block-local bindings that leave scope at their `}`
# — the fixture rebinds a freed name afterwards. Executing it proves the skipped
# `else if` condition and the short-circuited `||` operand, both `1 // 0`,
# stayed unevaluated.
differential_corpus corpus_branch nested-block

# The loop slice: one brace pair per loop block, a range whose ends are
# evaluated once into the enclosing scope, a bound name scoped to its own block
# and rebindable after it, and loops nested in each other and in branches.
# Executing it proves the body of a false `while` and of an empty range stayed
# unentered — both hold `1 // 0`.
differential_corpus corpus_loop loop

# Text literals, concatenation, equality and printing pass through both
# independently-derived compilers. The literal containing `(+ || ==)` pins that
# operator and parenthesis bytes inside quotes are never parsed as syntax.
differential_corpus corpus_text Text
! grep -F 'greeting + " " + "compiler"' \
    "$temporary/corpus_text-left/output.c" >/dev/null ||
    fail "Text emission retained the user's expression source"
differential_corpus corpus_text_equality_only Text-equality-only
grep -F 'static bool kofun_rt_text_equal' \
    "$temporary/corpus_text_equality_only-left/output.c" >/dev/null ||
    fail "literal-only Text equality omitted its conditional runtime"

# List[Text] construction, length and indexing must agree between the
# independently-derived compilers. `k字n` pins the Stage 2 profile's
# byte-oriented Text/list semantics.
differential_corpus corpus_list_text List-Text
grep -F 'static KofunTextList kofun_rt_chars' \
    "$temporary/corpus_list_text-left/output.c" >/dev/null ||
    fail "List[Text] emission omitted its conditional runtime"
grep -F 'static const char *kofun_rt_text_index' \
    "$temporary/corpus_list_text-left/output.c" >/dev/null ||
    fail "Text indexing omitted its conditional runtime"

# All 15 profile builtins run through one file/argv corpus. The emitted shim
# deliberately uses the audited Unicode implementation, so this corpus stays on
# ASCII for the documented seed/source is_xid_continue deviation while still
# proving the real Unicode validation path is linked.
stem=corpus_builtins
left=$temporary/$stem-left
right=$temporary/$stem-right
mkdir -p "$left" "$right"
cp bootstrap/selfhost/driver/$stem.kofun "$left/input.kofun"
cp bootstrap/selfhost/driver/$stem.kofun "$right/input.kofun"
(cd "$left" &&
    "$temporary/kofun-a1" input.kofun output.c >stdout.txt 2>stderr.txt)
(cd "$right" &&
    "$temporary/kofun-stage1" input.kofun output.c >stdout.txt 2>stderr.txt)
cmp "$left/output.c" "$right/output.c" ||
    fail "compiler-from-S and the audited seed emit different builtin C"
cmp "$left/stdout.txt" "$right/stdout.txt" ||
    fail "compiler-from-S and the audited seed differ on builtin stdout"
cmp "$left/stderr.txt" "$right/stderr.txt" ||
    fail "compiler-from-S and the audited seed differ on builtin stderr"
cmp bootstrap/selfhost/driver/$stem.c "$left/output.c" ||
    fail "builtin corpus emission differs from the checked-in evidence"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -I unicode \
    "$left/output.c" -o "$temporary/$stem-program"
"$temporary/$stem-program" \
    bootstrap/selfhost/driver/$stem.input "$temporary/$stem.output" \
    >"$temporary/$stem.stdout"
cmp bootstrap/selfhost/driver/$stem.stdout "$temporary/$stem.stdout" ||
    fail "builtin corpus stdout differs from the pinned golden"
cmp bootstrap/selfhost/driver/$stem.output "$temporary/$stem.output" ||
    fail "builtin corpus file output differs from the pinned golden"
test "$(sha256sum bootstrap/selfhost/driver/corpus_answer.c |
    awk '{ print $1 }')" = \
    673d6e62ad7947fc878420eea1dffb9e3f13e942adda71f1f972b31575616499 ||
    fail "the frozen arithmetic corpus changed in the builtin slice"

# Bounds checks are runtime failures, not frontend refusals. Pin both receiver
# kinds to exit 1 with one exact R010 diagnostic and no stdout.
differential_trap_corpus corpus_trap_list_index List-Text-index-trap
differential_trap_corpus corpus_trap_text_index Text-index-trap

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

# The fixed-point gate for #751: A1 must compile the exact canonical S bytes
# into a nonempty C2 in ordinary source-to-C mode. Two runs from distinct
# directories pin determinism and path independence; the audited hand-port is
# a third, independently derived byte differential. Finally, C2 must satisfy
# the repository's strict C11 host boundary.
selfhost_vmem_kib=${KOFUN_SELFHOST_VMEM_KIB:-1572864}
case "$selfhost_vmem_kib" in
    ''|*[!0-9]*) fail "KOFUN_SELFHOST_VMEM_KIB must be a positive integer" ;;
    0) fail "KOFUN_SELFHOST_VMEM_KIB must be a positive integer" ;;
esac

selfhost_compile() {
    directory=$1
    compiler_path=$2
    (
        # Linux and the CI shell support this bound. Other POSIX shells may
        # not expose -v; they still run the proof, but never turn a portable
        # shell feature check into a product failure.
        if ulimit -v "$selfhost_vmem_kib" 2>/dev/null; then :; fi
        cd "$directory"
        "$compiler_path" input.kofun C2.c >stdout.txt 2>stderr.txt
    )
}

mkdir -p "$temporary/self-a" "$temporary/self-b" "$temporary/self-seed"
cp bootstrap/stage1/compiler.kofun "$temporary/self-a/input.kofun"
cp bootstrap/stage1/compiler.kofun "$temporary/self-b/input.kofun"
cp bootstrap/stage1/compiler.kofun "$temporary/self-seed/input.kofun"
cmp bootstrap/stage1/compiler.kofun "$temporary/self-a/input.kofun" ||
    fail "self-compile input differs from canonical S"

selfhost_compile "$temporary/self-a" "$temporary/kofun-a1"
selfhost_compile "$temporary/self-b" "$temporary/kofun-a1"
selfhost_compile "$temporary/self-seed" "$temporary/kofun-stage1"

test -s "$temporary/self-a/C2.c" || fail "A1 produced an empty C2"
cmp "$temporary/self-a/C2.c" "$temporary/self-b/C2.c" ||
    fail "A1(S) is not deterministic and path-independent"
cmp "$temporary/self-a/C2.c" "$temporary/self-seed/C2.c" ||
    fail "A1 and the audited hand-port emit different C2 bytes"
cmp "$temporary/self-a/stdout.txt" "$temporary/self-b/stdout.txt" ||
    fail "A1(S) stdout is not deterministic and path-independent"
cmp "$temporary/self-a/stderr.txt" "$temporary/self-b/stderr.txt" ||
    fail "A1(S) stderr is not deterministic and path-independent"
cmp "$temporary/self-a/stdout.txt" "$temporary/self-seed/stdout.txt" ||
    fail "A1 and the audited hand-port differ on S stdout"
cmp "$temporary/self-a/stderr.txt" "$temporary/self-seed/stderr.txt" ||
    fail "A1 and the audited hand-port differ on S stderr"

"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -I unicode \
    "$temporary/self-a/C2.c" -o "$temporary/kofun-a2"

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
# The fixture set is the glob, for the reason bootstrap/stage1/check.sh gives:
# both gates must run the same refusals, and a hand-written list in each is how
# they silently stop doing so. REJECT_FIXTURE_COUNT is asserted in both, so the
# two lists cannot drift apart without a reviewable edit to the same number.
REJECT_FIXTURE_COUNT=31
reject_checked=0
for fixture in bootstrap/selfhost/driver/corpus_reject_*.kofun
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
    reject_checked=$((reject_checked + 1))
done
test "$reject_checked" -eq "$REJECT_FIXTURE_COUNT" ||
    fail "ran $reject_checked refusal fixtures, expected $REJECT_FIXTURE_COUNT"

# Every profile builtin has one wrong-arity and one wrong-type refusal. Keeping
# the cases in one reviewable matrix avoids 30 near-identical source files while
# both independently-derived compilers still see exactly the same full source.
BUILTIN_REJECT_COUNT=30
builtin_reject_checked=0
while IFS='|' read -r label statement
do
    fixture="$temporary/builtin-reject-$label.kofun"
    {
        printf '%s\n' 'fn main() {'
        printf '    %s\n' "$statement"
        printf '%s\n' '    print(0)' '}'
    } >"$fixture"
    set +e
    "$temporary/kofun-a1" "$fixture" \
        "$temporary/builtin-reject-$label.c" \
        >"$temporary/builtin-reject-$label.stdout" \
        2>"$temporary/builtin-reject-$label.stderr"
    a1_status=$?
    "$temporary/kofun-stage1" "$fixture" \
        "$temporary/builtin-reject-$label-seed.c" \
        >"$temporary/builtin-reject-$label-seed.stdout" \
        2>"$temporary/builtin-reject-$label-seed.stderr"
    seed_status=$?
    set -e
    test "$a1_status" -eq "$seed_status" ||
        fail "$label builtin refusal status diverges from the audited seed"
    test "$a1_status" -ne 0 ||
        fail "$label builtin refusal must exit nonzero"
    cmp bootstrap/selfhost/driver/corpus_reject.stdout \
        "$temporary/builtin-reject-$label.stdout" ||
        fail "$label builtin refusal diagnostic differs from the pinned refusal"
    cmp "$temporary/builtin-reject-$label.stdout" \
        "$temporary/builtin-reject-$label-seed.stdout" ||
        fail "$label builtin refusal diagnostic diverges from the audited seed"
    cmp "$temporary/builtin-reject-$label.stderr" \
        "$temporary/builtin-reject-$label-seed.stderr" ||
        fail "$label builtin refusal stderr diverges from the audited seed"
    test ! -e "$temporary/builtin-reject-$label.c" ||
        fail "$label builtin refusal produced C through the compiler from S"
    test ! -e "$temporary/builtin-reject-$label-seed.c" ||
        fail "$label builtin refusal produced C through the audited seed"
    builtin_reject_checked=$((builtin_reject_checked + 1))
done < bootstrap/selfhost/driver/corpus_builtin_rejects.tsv
test "$builtin_reject_checked" -eq "$BUILTIN_REJECT_COUNT" ||
    fail "ran $builtin_reject_checked builtin refusals, expected $BUILTIN_REJECT_COUNT"

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
    "PASS: nested blocks, else-if chains, and block scoping agree across both seeds" \
    "PASS: while and for-range loops, their bound scope and range evaluation agree across both seeds" \
    "PASS: Text parsing, typing, runtime emission and typed refusals agree across both seeds" \
    "PASS: List[Text] construction, length, indexing and bounds traps agree across both seeds" \
    "PASS: all 15 profile builtins and all 30 arity/type refusals agree across both seeds" \
    "PASS: emission is deterministic, path-independent, and failure-preserving" \
    "PASS: A1 compiles canonical S into deterministic C2 that matches the hand-port and compiles as strict C11"
