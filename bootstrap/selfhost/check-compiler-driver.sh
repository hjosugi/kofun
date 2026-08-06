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
    checked_c=${3:-no}
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
    if test "$checked_c" = yes; then
        cmp "bootstrap/selfhost/driver/$stem.c" "$left/output.c" ||
            fail "$label corpus emission differs from the checked-in evidence"
    fi

    "$compiler" -std=c11 -O2 -Wall -Wextra -Werror -I unicode \
        "$left/output.c" -o "$temporary/$stem-program"
    set +e
    "$temporary/$stem-program" \
        >"$temporary/$stem.stdout" 2>"$temporary/$stem.stderr"
    status=$?
    set -e
    test "$status" -eq 1 ||
        fail "$label program must exit 1"
    if test "$checked_c" = yes; then
        cmp "bootstrap/selfhost/driver/$stem.stdout" \
            "$temporary/$stem.stdout" ||
            fail "$label stdout differs from the pinned golden"
    else
        test ! -s "$temporary/$stem.stdout" ||
            fail "$label program wrote unexpected stdout"
    fi
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

# One compact fixture closes four source-profile gaps: a parameterized Void
# helper owns a mutable local, assigns it, and exits through a bare return.
# Both independently-derived compilers must emit the reviewed bytes and the
# program must reproduce its pinned output.
differential_corpus corpus_profile_complete profile-completion

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

# `fail()` is accepted and lowered like an ordinary zero-arity Void builtin,
# then terminates at runtime with exit 1 and no output. Unlike the older bounds
# traps, this fixture also supplies reviewed C because it closes a profile row.
differential_trap_corpus corpus_trap_fail fail-builtin-trap yes

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

# The self-compile gate for #751: A1 must compile the exact canonical S bytes
# into a nonempty C2 in ordinary source-to-C mode. Two runs from distinct
# directories and source names pin determinism and path independence; the
# audited hand-port is a third, independently derived byte differential.
# Finally, C2 must satisfy the repository's strict C11 host boundary.
selfhost_vmem_kib=${KOFUN_SELFHOST_VMEM_KIB:-1572864}
selfhost_timeout_seconds=${KOFUN_SELFHOST_TIMEOUT:-120}
case "$selfhost_vmem_kib" in
    ''|*[!0-9]*) fail "KOFUN_SELFHOST_VMEM_KIB must be a positive integer" ;;
    0) fail "KOFUN_SELFHOST_VMEM_KIB must be a positive integer" ;;
esac
case "$selfhost_timeout_seconds" in
    ''|*[!0-9]*) fail "KOFUN_SELFHOST_TIMEOUT must be a positive integer" ;;
    0) fail "KOFUN_SELFHOST_TIMEOUT must be a positive integer" ;;
esac

selfhost_compile() {
    directory=$1
    compiler_path=$2
    source_name=$3
    (
        # Linux and the CI shell support this bound. Other POSIX shells may
        # not expose -v; they still run the proof, but never turn a portable
        # shell feature check into a product failure.
        if ulimit -v "$selfhost_vmem_kib" 2>/dev/null; then :; fi
        cd "$directory"
        if command -v timeout >/dev/null 2>&1; then
            timeout "${selfhost_timeout_seconds}s" \
                "$compiler_path" "$source_name" C2.c \
                >stdout.txt 2>stderr.txt
        else
            "$compiler_path" "$source_name" C2.c \
                >stdout.txt 2>stderr.txt
        fi
    )
}

mkdir -p "$temporary/self-a" "$temporary/self-b" "$temporary/self-seed"
cp bootstrap/stage1/compiler.kofun "$temporary/self-a/source-left.kofun"
cp bootstrap/stage1/compiler.kofun "$temporary/self-b/source-right.kofun"
cp bootstrap/stage1/compiler.kofun "$temporary/self-seed/source-seed.kofun"
cmp bootstrap/stage1/compiler.kofun "$temporary/self-a/source-left.kofun" ||
    fail "self-compile input differs from canonical S"
cmp bootstrap/stage1/compiler.kofun "$temporary/self-b/source-right.kofun" ||
    fail "repeated self-compile input differs from canonical S"
cmp bootstrap/stage1/compiler.kofun "$temporary/self-seed/source-seed.kofun" ||
    fail "audited hand-port input differs from canonical S"

selfhost_compile "$temporary/self-a" "$temporary/kofun-a1" \
    source-left.kofun || fail "A1 could not compile S"
selfhost_compile "$temporary/self-b" "$temporary/kofun-a1" \
    source-right.kofun || fail "A1 repeat could not compile S"
selfhost_compile "$temporary/self-seed" "$temporary/kofun-stage1" \
    source-seed.kofun || fail "the audited hand-port could not compile S"

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

# I/O failure, the other direction: an unwritable destination panics with the
# same bounded runtime shape as a missing input. Nothing pinned this, so a
# regression that started writing a partial file, or exited 0 after failing to
# open the output, would have been silent.
set +e
"$temporary/kofun-a1" bootstrap/selfhost/driver/corpus_answer.kofun \
    "$temporary/no-such-directory/out.c" \
    >"$temporary/unwritable.stdout" 2>"$temporary/unwritable.stderr"
unwritable_status=$?
set -e
test "$unwritable_status" -eq 1 ||
    fail "an unwritable output destination must exit 1"
grep -F 'Kofun runtime error: cannot open output file' \
    "$temporary/unwritable.stderr" >/dev/null ||
    fail "an unwritable output must report the bounded runtime diagnostic"
test ! -s "$temporary/unwritable.stdout" ||
    fail "an unwritable output must not also write a compile diagnostic"
test ! -e "$temporary/no-such-directory" ||
    fail "a failed open must not create the missing output directory"

# Invalid UTF-8 input is refused before any lowering, on stdout, with no C.
#
# The input is written here rather than committed. A `.kofun` file holding
# invalid UTF-8 would be read by every gate that walks the repository corpus,
# so the only safe place for these bytes is a temporary the gate owns.
#
# Read this pin together with `tests/diagnostics/registry.tsv`, which registers
# `EUNICODE001` once, for the native backend, on *stderr*. A1 emits the same
# code on *stdout* from a different emitter, so the registry describes a path
# this one only resembles. The corpus already proves A1's Unicode validation is
# linked; nothing proved it refuses, which is what this asserts.
printf 'fn main() {\n    print(0) // \377\376 bad\n}\n' \
    > "$temporary/invalid-utf8.kofun"
set +e
"$temporary/kofun-a1" "$temporary/invalid-utf8.kofun" \
    "$temporary/invalid-utf8.c" \
    >"$temporary/invalid-utf8.stdout" 2>"$temporary/invalid-utf8.stderr"
invalid_utf8_status=$?
set -e
test "$invalid_utf8_status" -eq 1 ||
    fail "invalid UTF-8 input must exit 1"
# The byte offset is asserted, not just the code. A regression that reported
# the right refusal at the wrong place would otherwise pass.
printf 'error[EUNICODE001] at line 2, column 17 (byte 28): invalid UTF-8\n' |
    cmp - "$temporary/invalid-utf8.stdout" ||
    fail "invalid UTF-8 must report EUNICODE001 at its exact position on stdout"
test ! -s "$temporary/invalid-utf8.stderr" ||
    fail "invalid UTF-8 refusal must not write stderr"
test ! -e "$temporary/invalid-utf8.c" ||
    fail "invalid UTF-8 input must not produce C"

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

# --------------------------------------------------- the A1 host boundary
#
# A1 is the one place in the self-host chain where hand-written C touches the
# operating system: argument decoding, `read_text`, transactional `write_text`,
# and allocation. Everything above ran it at -O2 with no instrumentation, so a
# use-after-free or a signed overflow on that boundary would have shown up as a
# green run. `grep -ril fsanitize bootstrap/ tests/ Taskfile.yml` named 28 files
# before this section and not one of them was under `selfhost/`.
#
# Two claims are made separately here, because one cannot substitute for the
# other:
#
#   1. A1 is *instrumented* — proved by the sanitizer runtime's symbols being
#      present in the binary that ran.
#   2. The sanitizer arms *report* — proved by isolated faults built with the
#      identical flags, each of which must fail with its own diagnostic.
#
# Without (2) a clean run cannot be distinguished from a sanitizer that was
# never linked, and UndefinedBehaviorSanitizer without `-fno-sanitize-recover`
# prints its finding and still exits 0. Without (1), (2) would only prove the
# toolchain works. The discipline is `tests/interop/bindgen-c/check-sanitizers.sh`.
sanitize_flags='-fsanitize=address,undefined -fno-sanitize-recover=all'
# `detect_leaks=0` is deliberate, and it is the one sanitizer property this
# gate does not assert. A1's runtime allocates and never frees: `kofun_rt_alloc`
# is a `malloc` wrapper and the prelude defines no deallocation entry point at
# all, so every Text it builds lives until the process exits. That is the
# allocation model for a compiler that runs once and exits, not an oversight —
# LeakSanitizer would report the model rather than a defect, and with
# `detect_leaks=1` this gate reports a five-figure leak on a clean run.
#
# The model is asserted below rather than assumed, so if a deallocation path is
# ever added the assertion fails and this decision gets revisited instead of
# quietly outliving its reason. Use-after-free and buffer overflow — the errors
# a never-free allocator can still commit — stay armed.
sanitize_flags_reason='A1 never frees; see the detect_leaks note above'
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:detect_stack_use_after_return=1
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
export ASAN_OPTIONS UBSAN_OPTIONS

# The never-free model, pinned. `free(` appears in S.c only inside the string
# literals A1 emits into the programs it compiles, so a count of its own calls
# is not what this checks; the absence of a deallocation entry point is.
for deallocator in kofun_rt_free kofun_rt_dealloc kofun_rt_release; do
    if grep -q "^[a-zA-Z].*$deallocator" bootstrap/selfhost/driver/S.c; then
        fail "S.c defines $deallocator, so A1 no longer never-frees; re-enable detect_leaks (reason recorded: $sanitize_flags_reason)"
    fi
done
grep -q '^void \*kofun_rt_alloc(' bootstrap/selfhost/driver/S.c ||
    fail "S.c no longer defines kofun_rt_alloc; the allocation model this gate reasons about has changed"

printf 'int main(void) { return 0; }\n' > "$temporary/link-probe.c"
sanitize_cc=''
for candidate in "$compiler" clang gcc cc; do
    command -v "$candidate" >/dev/null 2>&1 || continue
    # shellcheck disable=SC2086
    if "$candidate" -std=c11 $sanitize_flags "$temporary/link-probe.c" \
        -o "$temporary/link-probe" >"$temporary/link-probe.log" 2>&1
    then
        sanitize_cc=$candidate
        break
    fi
done
test -n "$sanitize_cc" ||
    fail "no available C compiler can link -fsanitize=address,undefined; last attempt said: $(cat "$temporary/link-probe.log")"

# shellcheck disable=SC2086
"$sanitize_cc" -std=c11 -O1 -g -Wall -Wextra -Werror \
    -fno-omit-frame-pointer $sanitize_flags \
    -I unicode "$temporary/S.c" -o "$temporary/kofun-a1-sanitized" ||
    fail "A1 does not build under the sanitizers"

# Claim (1). `nm` is not universally available; when it is absent the symbol
# scan falls back to the binary itself, which is what `strings` would have
# read anyway. Either way an uninstrumented binary has no `__asan_` in it.
if command -v nm >/dev/null 2>&1; then
    nm "$temporary/kofun-a1-sanitized" > "$temporary/a1.syms" 2>/dev/null ||
        cp "$temporary/kofun-a1-sanitized" "$temporary/a1.syms"
else
    cp "$temporary/kofun-a1-sanitized" "$temporary/a1.syms"
fi
grep -a -q -- '__asan_' "$temporary/a1.syms" ||
    fail "the sanitized A1 carries no AddressSanitizer symbols; every clean run below would prove nothing"

# The corpus and the self-compile both go through the sanitized binary. The
# self-compile is the interesting one: it is the largest input A1 ever reads
# and the only one whose output approaches a megabyte, so it exercises the
# allocation and write paths at a scale the corpora do not.
#
# No `ulimit -v` here. AddressSanitizer reserves a very large virtual address
# space at startup, so the ceiling that bounds the ordinary self-compile would
# refuse this run for a reason that has nothing to do with the code.
sanitized_corpus_checked=0
for source in bootstrap/selfhost/driver/corpus_answer.kofun \
              bootstrap/selfhost/driver/corpus_builtins.kofun \
              bootstrap/selfhost/driver/corpus_text.kofun \
              bootstrap/selfhost/driver/corpus_list_text.kofun
do
    stem=$(basename "$source" .kofun)
    set +e
    "$temporary/kofun-a1-sanitized" "$source" \
        "$temporary/sanitized-$stem.c" \
        >"$temporary/sanitized-$stem.stdout" \
        2>"$temporary/sanitized-$stem.stderr"
    sanitized_status=$?
    set -e
    test "$sanitized_status" -eq 0 ||
        fail "$stem exited $sanitized_status under the sanitizers: $(head -c 2048 "$temporary/sanitized-$stem.stderr")"
    test ! -s "$temporary/sanitized-$stem.stderr" ||
        fail "$stem produced a sanitizer diagnostic: $(head -c 2048 "$temporary/sanitized-$stem.stderr")"
    # Instrumentation must not change what A1 emits, so the sanitized run is
    # held to the same checked-in evidence the uninstrumented run matched.
    cmp "bootstrap/selfhost/driver/$stem.c" "$temporary/sanitized-$stem.c" ||
        fail "$stem emitted different C under the sanitizers"
    sanitized_corpus_checked=$((sanitized_corpus_checked + 1))
done
test "$sanitized_corpus_checked" -eq 4 ||
    fail "ran $sanitized_corpus_checked sanitized corpora, expected 4"

mkdir -p "$temporary/self-sanitized"
cp bootstrap/stage1/compiler.kofun "$temporary/self-sanitized/source-left.kofun"
set +e
(
    cd "$temporary/self-sanitized"
    "$temporary/kofun-a1-sanitized" source-left.kofun C2.c \
        >stdout.txt 2>stderr.txt
)
sanitized_selfhost_status=$?
set -e
test "$sanitized_selfhost_status" -eq 0 ||
    fail "A1(S) exited $sanitized_selfhost_status under the sanitizers: $(head -c 2048 "$temporary/self-sanitized/stderr.txt")"
test ! -s "$temporary/self-sanitized/stderr.txt" ||
    fail "A1(S) produced a sanitizer diagnostic: $(head -c 2048 "$temporary/self-sanitized/stderr.txt")"
cmp "$temporary/self-a/C2.c" "$temporary/self-sanitized/C2.c" ||
    fail "instrumentation changed the C2 bytes A1 emits"

# The two failure paths pinned above also run instrumented. A refusal path is
# where an allocation is abandoned mid-flight, which is exactly where a leak or
# a double free hides from an accept-only corpus.
set +e
"$temporary/kofun-a1-sanitized" "$temporary/invalid-utf8.kofun" \
    "$temporary/sanitized-invalid.c" \
    >"$temporary/sanitized-invalid.stdout" \
    2>"$temporary/sanitized-invalid.stderr"
sanitized_invalid_status=$?
"$temporary/kofun-a1-sanitized" bootstrap/selfhost/driver/corpus_reject.kofun \
    "$temporary/sanitized-reject.c" \
    >"$temporary/sanitized-reject.stdout" \
    2>"$temporary/sanitized-reject.stderr"
sanitized_reject_status=$?
set -e
test "$sanitized_invalid_status" -eq 1 ||
    fail "the sanitized invalid-UTF-8 refusal exited $sanitized_invalid_status, expected 1"
test "$sanitized_reject_status" -ne 0 ||
    fail "the sanitized reject corpus must exit nonzero"
test ! -s "$temporary/sanitized-invalid.stderr" ||
    fail "the invalid-UTF-8 refusal produced a sanitizer diagnostic: $(head -c 2048 "$temporary/sanitized-invalid.stderr")"
test ! -s "$temporary/sanitized-reject.stderr" ||
    fail "the reject corpus produced a sanitizer diagnostic: $(head -c 2048 "$temporary/sanitized-reject.stderr")"
cmp "$temporary/invalid-utf8.stdout" "$temporary/sanitized-invalid.stdout" ||
    fail "instrumentation changed the invalid-UTF-8 diagnostic"
cmp bootstrap/selfhost/driver/corpus_reject.stdout \
    "$temporary/sanitized-reject.stdout" ||
    fail "instrumentation changed the reject corpus diagnostic"

# Claim (2). One isolated fault per arm, built with the identical flags. Each
# must fail, and must fail with its own arm's diagnostic and not the other's —
# a probe that tripped both would prove neither.
sanitizer_arm_probe() {
    probe_name=$1
    probe_source=$2
    expected=$3
    forbidden=$4

    # shellcheck disable=SC2086
    "$sanitize_cc" -std=c11 -O1 -g -fno-omit-frame-pointer $sanitize_flags \
        "$probe_source" -o "$temporary/$probe_name" \
        2>"$temporary/$probe_name.build.err" ||
        fail "the $probe_name probe did not compile: $(cat "$temporary/$probe_name.build.err")"
    set +e
    "$temporary/$probe_name" >"$temporary/$probe_name.out" \
        2>"$temporary/$probe_name.err"
    probe_status=$?
    set -e
    test "$probe_status" -ne 0 ||
        fail "the $probe_name probe exited 0; that sanitizer arm is not armed and every clean run above proves nothing"
    grep -F -q -- "$expected" "$temporary/$probe_name.err" ||
        fail "the $probe_name probe failed without its arm-specific diagnostic: $(head -c 2048 "$temporary/$probe_name.err")"
    # `if` rather than `grep ... && fail`: under `set -e` an AND-OR list whose
    # first command fails is the ordinary case here, and shells disagree about
    # whether the list's own nonzero status then aborts the script.
    if grep -F -q -- "$forbidden" "$temporary/$probe_name.err"; then
        fail "the $probe_name probe also tripped $forbidden, so it isolates nothing"
    fi
}

cat > "$temporary/probe-address.c" <<'PROBE'
#include <stdlib.h>
#include <string.h>
int main(void) {
    /* The pointer, the size and the index are all opaque on purpose.
       `-fsanitize=undefined` includes an object-size check that reports this
       overflow *before* AddressSanitizer sees it whenever it can trace the
       allocation, which makes the probe fail for the wrong arm and prove
       nothing about the arm it is named for. Making `size` and the index
       volatile is not enough — the check still resolves the object through
       the pointer at runtime. A `volatile` *pointer* is, because the
       allocation's provenance stops being visible to that pass. */
    volatile size_t size = 8;
    char *volatile block = malloc(size);
    if (block == NULL) return 2;
    memset((char *)block, 'a', size);
    volatile size_t past_the_end = size;
    block[past_the_end] = 'x';
    int escaped = block[past_the_end];
    free((char *)block);
    return escaped == 'x' ? 0 : 3;
}
PROBE
cat > "$temporary/probe-undefined.c" <<'PROBE'
#include <limits.h>
#include <stdlib.h>
int main(void) {
    int high = INT_MAX;
    /* `volatile` and the environment read keep the overflow out of reach of
       constant folding, which would otherwise make this a compile error. */
    volatile int step = (getenv("KOFUN_PROBE_ZERO") == NULL) ? 1 : 0;
    return high + step;
}
PROBE

sanitizer_arm_probe address "$temporary/probe-address.c" \
    'ERROR: AddressSanitizer' 'runtime error:'
sanitizer_arm_probe undefined "$temporary/probe-undefined.c" \
    'runtime error: signed integer overflow' 'ERROR: AddressSanitizer'

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
    "PASS: A1 compiles canonical S into deterministic C2 that matches the hand-port and compiles as strict C11" \
    "PASS: an unwritable output and invalid UTF-8 input each exit 1 with their exact bounded diagnostic and no artifact" \
    "PASS: the A1 host boundary runs the corpora, A1(S), and both refusal paths clean under ASan+UBSan" \
    "PASS: A1 still never frees, which is why leak detection is off and memory-error detection is not" \
    "PASS: isolated address and undefined-behavior faults still report, so the sanitizer arms are armed"
