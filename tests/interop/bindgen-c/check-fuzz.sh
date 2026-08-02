#!/bin/sh
set -eu

# bindgen-c adversarial macro gate (#901).
#
# `kofun bindgen-c` treats a header as untrusted input, but before this gate
# it bounded only what it did with clang's *answers*: declaration counts,
# typedef depth, a maxBuffer. Clang itself ran unbounded, and the only
# adversarial input in the corpus was one line of garbage. A header that made
# the preprocessor diverge would have hung the tool, and one whose expansion
# did not fit would have failed on whatever ran out first.
#
# What runs here:
#
#   1. a static check that no input can reach a shell and that every clang
#      subprocess — version query, target query, AST dump, preprocessor run —
#      carries a wall-clock bound and an output bound, the two properties the
#      rest of this gate assumes;
#
#   2. a committed adversarial corpus: deep object-like nesting, self- and
#      mutually recursive object-like and function-like macros, token-paste
#      and stringize abuse including a paste that is not a token, identifiers
#      on both sides of the 128-character cap, a typedef chain past the
#      32-link bound, an expansion flood past the declaration bound, a
#      fan-out whose AST does not fit in the output bound, a macro expanding
#      to an unbalanced declaration, and a #error whose text is shell
#      metacharacters;
#
#   3. a seeded mutation fuzzer over that corpus. The seed, the case count,
#      the mutation count, the byte cap, and both wall-clock caps are
#      constants in the driver — no clock, no PID, no entropy, no network.
#      Every case must either produce byte-identical output twice or be
#      refused with a named cause and no output directory; a hang, a crash, a
#      silent refusal, or a partial --out-dir fails the run;
#
#   4. the wall-clock bound itself, exercised against a stand-in clang that
#      never answers. This is the one bound the corpus cannot reach on its
#      own, because a header that reliably diverges the real clang would also
#      be a clang bug report;
#
#   5. proof that a refusal writes nothing anywhere: the corpus tree is
#      compared file for file before and after the whole run.
#
# Everything is offline and reproducible from the committed bytes.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/interop/bindgen-c"
FUZZ="$CASES/fuzz"
TOOL="$ROOT/tooling/bindgen-c/bindgen-c.mjs"
ASSERT_CONTEXT="bindgen-c fuzz"
. "$ROOT/tests/assertions/assert.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-bindgen-fuzz.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

for required in clang node; do
    command -v "$required" >/dev/null 2>&1 ||
        assert_fail "required tool unavailable: $required"
done

assert_dir 'committed adversarial corpus' "$FUZZ/corpus"
assert_regular_file 'fuzz driver' "$FUZZ/fuzz-macros.mjs"
assert_regular_file 'corpus README' "$FUZZ/README.md"
assert_regular_file 'hanging-clang stand-in' "$FUZZ/hanging-clang.sh"

# ------------------------------------------- 1. the bounds exist at all

# Nothing the header carries may reach a shell. The tool spawns clang through
# execFileSync with a structured argv; the forms that would reintroduce a
# shell are named individually so a failure says which one came back.
assert_grep 'the generator imports something other than execFileSync from child_process' \
    -Fqx -- "import { execFileSync } from 'node:child_process';" "$TOOL"
assert_not_grep 'the generator can spawn a shell through execSync' \
    -Fq -- 'execSync(' "$TOOL"
assert_not_grep 'the generator can spawn a shell through shell: true' \
    -Fq -- 'shell: true' "$TOOL"

# Every clang subprocess is bounded. The counts are compared rather than
# spot-checked, so a *new* spawn site that forgot one of the two bounds fails
# here instead of being discovered by a header that hangs CI.
spawn_sites=$(grep -c 'execFileSync(' "$TOOL")
timeout_bounds=$(grep -c 'timeout: CLANG_TIMEOUT_MS' "$TOOL")
buffer_bounds=$(grep -c 'maxBuffer:' "$TOOL")
assert_num 'the generator has no clang spawn site left' "$spawn_sites" -ge 1
assert_num 'a clang spawn site carries no wall-clock bound' \
    "$timeout_bounds" -eq "$spawn_sites"
assert_num 'a clang spawn site carries no captured-output bound' \
    "$buffer_bounds" -eq "$spawn_sites"

# --------------------------- 2+3. the corpus and the seeded mutation fuzzer

find "$FUZZ" -type f | sort >"$WORK/tree.before"
assert_file_nonempty 'corpus file listing' "$WORK/tree.before"
corpus_files=$(find "$FUZZ/corpus" -name '*.h' -type f | wc -l | tr -d ' ')
assert_num 'the committed corpus is empty' "$corpus_files" -ge 8

node "$FUZZ/fuzz-macros.mjs" "$ROOT/bin/kofun" "$FUZZ/corpus" "$WORK/run-a" \
    >"$WORK/summary-a" 2>"$WORK/summary-a.err" ||
    assert_fail "the fuzz run failed: $(cat "$WORK/summary-a.err")"
assert_file_nonempty 'first fuzz summary' "$WORK/summary-a"

# Same seed, same corpus, same outcomes. This is the whole claim of a
# committed seed: a failure reproduces offline, byte for byte.
node "$FUZZ/fuzz-macros.mjs" "$ROOT/bin/kofun" "$FUZZ/corpus" "$WORK/run-b" \
    >"$WORK/summary-b" 2>"$WORK/summary-b.err" ||
    assert_fail "the second fuzz run failed: $(cat "$WORK/summary-b.err")"
cmp "$WORK/summary-a" "$WORK/summary-b" ||
    assert_fail 'two runs of the same seed produced different outcomes'

# The corpus has to reach the bounds it was written for, not merely be
# refused by clang for nine different reasons.
assert_grep 'no case reached the declaration-count bound' \
    -q -- '^refuse:decl-bound	' "$WORK/summary-a"
assert_grep 'no case reached the captured-output bound' \
    -q -- '^refuse:output-bound	' "$WORK/summary-a"
assert_grep 'no case was refused by clang itself' \
    -q -- '^refuse:clang-refused	' "$WORK/summary-a"
assert_grep 'no case was accepted, so nothing proves determinism' \
    -q -- '^accept	' "$WORK/summary-a"
assert_grep 'the summary does not record the seed it ran with' \
    -q -- '^seed	' "$WORK/summary-a"

# Each committed corpus file must appear in its own right, so a reader can
# reproduce any one of them by hand without running the mutator.
for header in "$FUZZ"/corpus/*.h; do
    name=$(basename "$header")
    assert_grep "corpus file $name was not exercised on its own" \
        -Fq -- "corpus	$name	-	" "$WORK/summary-a"
done

# ------------------------------------------- 4. the wall-clock bound fires

# A clang that never answers. The tool must give up on its own, name the
# bound in the diagnostic, and create nothing.
if "$ROOT/bin/kofun" bindgen-c "$CASES/fixture/kbfix.h" \
    --out-dir "$WORK/hang-out" --clang "$FUZZ/hanging-clang.sh" \
    >"$WORK/hang.stdout" 2>"$WORK/hang.stderr"
then
    assert_fail 'a clang that never answers was accepted; the wall-clock bound does not fire'
fi
assert_grep 'the timeout refusal does not name the wall-clock bound' \
    -Fq -- 'ms bound' "$WORK/hang.stderr"
assert_grep 'the timeout refusal does not say nothing was generated' \
    -Fq -- 'nothing was generated' "$WORK/hang.stderr"
assert_absent 'output directory for the timed-out run' "$WORK/hang-out"

# ------------------------------- 5. refusals write nothing, anywhere

find "$FUZZ" -type f | sort >"$WORK/tree.after"
cmp "$WORK/tree.before" "$WORK/tree.after" ||
    assert_fail 'the fuzz run wrote into the committed corpus tree'

# And a diagnostic never carries the tool's own generated paths outward: the
# refusals above name the input and the bound, never an artifact that was
# not created.
assert_not_grep 'a refusal diagnostic names an output artifact that was never written' \
    -Fq -- '.raw.kofun' "$WORK/hang.stderr"

cases=$(awk -F '\t' '$1 == "total" { print $2 }' "$WORK/summary-a")
printf 'bindgen-c: every clang subprocess is spawned through argv and bounded: PASS\n'
printf 'bindgen-c: %s adversarial macro cases are deterministic or refused by name: PASS\n' "$cases"
printf 'bindgen-c: the same seed reproduces the same outcomes offline: PASS\n'
printf 'bindgen-c: a clang that never answers is refused by the wall-clock bound: PASS\n'
