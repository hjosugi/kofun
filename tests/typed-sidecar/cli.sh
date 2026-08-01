#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_TYPED_SIDECAR_CLI_WORK:-"$ROOT/build/typed-sidecar-cli"}
COMPLETE="$ROOT/tests/typed-sidecar/fixtures/stage2_events.kofun"
FAILED="$ROOT/bootstrap/stage2/function_unknown_error.kofun"
BORROWED="$ROOT/bootstrap/stage2/fixtures/borrowed_copy_int.kofun"
ASSERT_CONTEXT='typed-sidecar cli'
. "$ROOT/tests/assertions/assert.sh"

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

case $WORK in
    */typed-sidecar-cli|*/typed-sidecar-cli.*) ;;
    *) fail "work directory must end in typed-sidecar-cli[.suffix]: $WORK" ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK/output" "$WORK/build"

run_kofun() {
    KOFUN_BUILD_DIR="$WORK/build/stage1" \
    KOFUN_STAGE2_BUILD_DIR="$WORK/build/stage2" \
    KOFUN_STAGE2_EVENTS_BUILD_DIR="$WORK/build/events" \
        "$ROOT/bin/kofun" "$@"
}

expect_status() (
    expected=$1
    label=$2
    shift 2
    set +e
    run_kofun "$@" >"$WORK/$label.stdout" 2>"$WORK/$label.stderr"
    actual=$?
    set -e
    test "$actual" -eq "$expected" ||
        fail "$label exited $actual instead of $expected"
)

run_kofun --help >"$WORK/help"
grep -q -- '--emit-typed-sidecar' "$WORK/help"
grep -q 'non-authoritative tooling data' "$WORK/help"

expect_status 0 complete check "$COMPLETE" \
    --emit-typed-sidecar "$WORK/output/complete.kofun-semantic.json" \
    --generation 1
grep -Fx "ok: $COMPLETE" "$WORK/complete.stdout" >/dev/null
assert_file_empty "complete.stderr" "$WORK/complete.stderr"
assert_file_nonempty "output/complete.kofun-semantic.json" \
    "$WORK/output/complete.kofun-semantic.json"

expect_status 0 borrowed-plain check "$BORROWED"
expect_status 0 borrowed-sidecar check "$BORROWED" \
    --emit-typed-sidecar "$WORK/output/borrowed.json" --generation 4
cmp "$WORK/borrowed-plain.stdout" "$WORK/borrowed-sidecar.stdout"
cmp "$WORK/borrowed-plain.stderr" "$WORK/borrowed-sidecar.stderr"
assert_file_nonempty "output/borrowed.json" "$WORK/output/borrowed.json"

expect_status 0 reverse-order check "$COMPLETE" \
    --generation 2 \
    --emit-typed-sidecar "$WORK/output/reverse.kofun-semantic.json"
expect_status 0 maximum check "$COMPLETE" \
    --generation 9007199254740991 \
    --emit-typed-sidecar "$WORK/output/maximum.kofun-semantic.json"

node --input-type=module - \
    "$WORK/output/complete.kofun-semantic.json" \
    "$WORK/output/reverse.kofun-semantic.json" <<'NODE'
import assert from "node:assert/strict";
import fs from "node:fs";
import { readTypedSidecar } from "./tooling/typed-sidecar/codec.mjs";
for (const [index, filename] of process.argv.slice(2).entries()) {
  const result = readTypedSidecar(fs.readFileSync(filename));
  assert.equal(result.ok, true, result.error?.message);
  assert.equal(result.document.authoritative, false);
  assert.equal(result.document.generation.sequence, index + 1);
}
NODE

for value in '' 00 01 +1 -1 1e3 1_000 9007199254740992
do
    label=$(printf '%s' "${value:-empty}" | tr '+_-' 'pmm')
    expect_status 2 "generation-$label" check "$COMPLETE" \
        --emit-typed-sidecar "$WORK/output/generation-$label.json" \
        --generation "$value"
    grep -q '^ETS01: ' "$WORK/generation-$label.stderr"
done

expect_status 2 missing-generation check "$COMPLETE" \
    --emit-typed-sidecar "$WORK/output/missing.json"
expect_status 2 missing-output check "$COMPLETE" --generation 1
expect_status 2 duplicate-output check "$COMPLETE" \
    --emit-typed-sidecar "$WORK/output/a.json" \
    --emit-typed-sidecar "$WORK/output/b.json"
expect_status 2 duplicate-generation check "$COMPLETE" \
    --generation 1 --generation 2
expect_status 2 unknown-option check "$COMPLETE" \
    --emit-typed-sidecar "$WORK/output/unknown.json" --unknown 1
for label in missing-generation missing-output duplicate-output \
    duplicate-generation unknown-option
do
    grep -q '^ETS01: ' "$WORK/$label.stderr"
done

expect_status 2 dash check "$COMPLETE" \
    --emit-typed-sidecar - --generation 3
mkdir "$WORK/output/directory.json"
expect_status 2 directory check "$COMPLETE" \
    --emit-typed-sidecar "$WORK/output/directory.json" --generation 3
printf '%s\n' keep >"$WORK/output/victim"
ln -s victim "$WORK/output/symlink.json"
expect_status 2 symlink check "$COMPLETE" \
    --emit-typed-sidecar "$WORK/output/symlink.json" --generation 3
expect_status 2 source-output check "$COMPLETE" \
    --emit-typed-sidecar "$COMPLETE" --generation 3
expect_status 2 missing-parent check "$COMPLETE" \
    --emit-typed-sidecar "$WORK/missing/out.json" --generation 3
if command -v mkfifo >/dev/null 2>&1; then
    mkfifo "$WORK/output/fifo.json"
    expect_status 2 fifo check "$COMPLETE" \
        --emit-typed-sidecar "$WORK/output/fifo.json" --generation 3
fi
for label in dash directory symlink source-output missing-parent
do
    grep -q '^ETS01: ' "$WORK/$label.stderr"
done
assert_eq "contents of output/victim" "$(cat "$WORK/output/victim")" keep

expect_status 1 failed-plain check "$FAILED"
expect_status 1 failed check "$FAILED" \
    --emit-typed-sidecar "$WORK/output/failed.json" --generation 20
cmp "$WORK/failed-plain.stdout" "$WORK/failed.stdout"
cmp "$WORK/failed-plain.stderr" "$WORK/failed.stderr"
assert_file_empty "failed.stdout" "$WORK/failed.stdout"
grep -Fq 'error[' "$WORK/failed.stderr"
assert_file_nonempty "output/failed.json" "$WORK/output/failed.json"
node --input-type=module - "$WORK/output/failed.json" <<'NODE'
import assert from "node:assert/strict";
import fs from "node:fs";
import { readTypedSidecar } from "./tooling/typed-sidecar/codec.mjs";
const result = readTypedSidecar(fs.readFileSync(process.argv[2]));
assert.equal(result.ok, true, result.error?.message);
assert.equal(result.document.completeness, "partial");
assert.equal(result.document.source_status, "failed");
assert.ok(result.document.diagnostics.some((item) => item.severity === "error"));
NODE

cp "$WORK/output/complete.kofun-semantic.json" "$WORK/equal.before"
expect_status 3 equal-clean check "$COMPLETE" \
    --emit-typed-sidecar "$WORK/output/complete.kofun-semantic.json" \
    --generation 1
grep -q '^ETS05: ' "$WORK/equal-clean.stderr"
cmp "$WORK/complete.stdout" "$WORK/equal-clean.stdout"
cmp "$WORK/equal.before" "$WORK/output/complete.kofun-semantic.json"

cp "$WORK/output/failed.json" "$WORK/equal-failed.before"
expect_status 1 equal-failed check "$FAILED" \
    --emit-typed-sidecar "$WORK/output/failed.json" --generation 20
cmp "$WORK/failed.stdout" "$WORK/equal-failed.stdout"
sed '$d' "$WORK/equal-failed.stderr" >"$WORK/equal-failed.language.stderr"
cmp "$WORK/failed.stderr" "$WORK/equal-failed.language.stderr"
tail -n 1 "$WORK/equal-failed.stderr" | grep -q '^ETS05: '
cmp "$WORK/equal-failed.before" "$WORK/output/failed.json"

printf '\303\050' >"$WORK/invalid-utf8.kofun"
expect_status 1 invalid-utf8 check "$WORK/invalid-utf8.kofun" \
    --emit-typed-sidecar "$WORK/output/invalid-utf8.json" --generation 1
assert_absent "output/invalid-utf8.json" "$WORK/output/invalid-utf8.json"

# The production adapter rejects its bounded declaration profile before
# entering an oversized observer transaction.  The public CLI must report one
# bounded ETS04 line, never an allocator/core diagnostic.
: >"$WORK/declaration-limit.kofun"
declaration=0
while test "$declaration" -le 64
do
    printf 'fn f%s(value: Int) -> Int { return value }\n' "$declaration" \
        >>"$WORK/declaration-limit.kofun"
    declaration=$((declaration + 1))
done
printf 'fn main() { print(42) }\n' >>"$WORK/declaration-limit.kofun"
set +e
run_kofun check "$WORK/declaration-limit.kofun" \
    >"$WORK/declaration-limit-plain.stdout" \
    2>"$WORK/declaration-limit-plain.stderr"
declaration_plain_status=$?
run_kofun check "$WORK/declaration-limit.kofun" \
    --emit-typed-sidecar "$WORK/output/declaration-limit.json" \
    --generation 1 \
    >"$WORK/declaration-limit.stdout" \
    2>"$WORK/declaration-limit.stderr"
declaration_sidecar_status=$?
set -e
if test "$declaration_plain_status" -eq 0; then
    test "$declaration_sidecar_status" -eq 3 ||
        fail "clean declaration-limit check did not return tooling status"
else
    test "$declaration_sidecar_status" -eq "$declaration_plain_status" ||
        fail "declaration-limit check changed the source failure status"
fi
cmp "$WORK/declaration-limit-plain.stdout" \
    "$WORK/declaration-limit.stdout"
plain_stderr_lines=$(wc -l <"$WORK/declaration-limit-plain.stderr")
head -n "$plain_stderr_lines" "$WORK/declaration-limit.stderr" \
    >"$WORK/declaration-limit.language.stderr"
cmp "$WORK/declaration-limit-plain.stderr" \
    "$WORK/declaration-limit.language.stderr"
tail -n 1 "$WORK/declaration-limit.stderr" |
    grep -Fqx 'ETS04: semantic producer declaration limit exceeded'
! grep -Eq 'double free|free\\(\\)|Aborted|core dumped|sanitizer' \
    "$WORK/declaration-limit.stderr" ||
    fail "declaration-limit path exposed a process abort"
assert_absent "output/declaration-limit.json" \
    "$WORK/output/declaration-limit.json"

expect_status 2 build-reject build "$COMPLETE" \
    --emit-typed-sidecar "$WORK/output/build.json" --generation 1
expect_status 2 run-reject run "$COMPLETE" \
    --emit-typed-sidecar "$WORK/output/run.json" --generation 1
expect_status 2 test-reject test "$COMPLETE" \
    --emit-typed-sidecar "$WORK/output/test.json" --generation 1
expect_status 2 package-reject package lock \
    --emit-typed-sidecar "$WORK/output/package.json" --generation 1

run_kofun check "$ROOT/bootstrap/fixtures/answer.kofun" \
    >"$WORK/no-flag-a.stdout" 2>"$WORK/no-flag-a.stderr"
run_kofun check "$ROOT/bootstrap/fixtures/answer.kofun" \
    >"$WORK/no-flag-b.stdout" 2>"$WORK/no-flag-b.stderr"
cmp "$WORK/no-flag-a.stdout" "$WORK/no-flag-b.stdout"
cmp "$WORK/no-flag-a.stderr" "$WORK/no-flag-b.stderr"
assert_absent "$ROOT/bootstrap/fixtures/answer.kofun-semantic.json" \
    "$ROOT/bootstrap/fixtures/answer.kofun-semantic.json"

printf '%s\n' \
    'PASS: typed-sidecar CLI grammar, exact fallback channels, races, exit precedence, and no-flag compatibility'
