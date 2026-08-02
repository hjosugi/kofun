#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
HERE="$ROOT/spec/reuse-candidate-v1"
VALIDATOR="$HERE/validate.mjs"
SPEC="$ROOT/spec/reuse-candidate-v1.md"
WORK=${KOFUN_REUSE_CANDIDATE_WORK:-"$ROOT/build/reuse-candidate"}
ASSERT_CONTEXT='reuse-candidate v1'
. "$ROOT/tests/assertions/assert.sh"

case $WORK in
    */reuse-candidate|*/reuse-candidate.*) ;;
    *) assert_fail "work directory must end in reuse-candidate[.suffix]: $WORK" ;;
esac

rm -rf "$WORK"
mkdir -p "$WORK/run-a" "$WORK/run-b"

node --check "$VALIDATOR"
node --check "$HERE/check.mjs"
node "$HERE/check.mjs"
sh "$ROOT/spec/aggregate-layout-v1/check.sh"

valid_count=0
for fixture in "$HERE"/valid/*.json; do
    stem=${fixture##*/}
    (
        cd "$WORK/run-a"
        node "$VALIDATOR" "$fixture"
    ) >"$WORK/run-a/$stem"
    (
        cd "$WORK/run-b"
        node "$VALIDATOR" "$fixture"
    ) >"$WORK/run-b/$stem"
    cmp "$fixture" "$WORK/run-a/$stem" ||
        assert_fail "$stem is not its own canonical validated form"
    cmp "$WORK/run-a/$stem" "$WORK/run-b/$stem" ||
        assert_fail "$stem differs across clean repeated runs"
    assert_not_grep "$stem contains no absolute checkout path" -F "$ROOT" "$WORK/run-a/$stem"
    valid_count=$((valid_count + 1))
done
assert_num 'committed valid-vector count' "$valid_count" -eq 9

invalid_count=0
: >"$WORK/error-identities"
while IFS='	' read -r fixture code name; do
    case $fixture in ''|'#'*) continue ;; esac
    for run in run-a run-b; do
        status=0
        (
            cd "$WORK/$run"
            node "$VALIDATOR" "$HERE/invalid/$fixture"
        ) >"$WORK/$run/$fixture.stdout" 2>"$WORK/$run/$fixture.stderr" || status=$?
        assert_num "$fixture $run exit status" "$status" -eq 1
        assert_file_empty "$fixture $run partial record" "$WORK/$run/$fixture.stdout"
        assert_grep "$fixture $run named rejection" -E \
            "^reuse-candidate: $code $name:" "$WORK/$run/$fixture.stderr"
        assert_not_grep "$fixture $run diagnostic contains no absolute path" \
            -F "$ROOT" "$WORK/$run/$fixture.stderr"
    done
    cmp "$WORK/run-a/$fixture.stderr" "$WORK/run-b/$fixture.stderr" ||
        assert_fail "$fixture rejection differs across clean repeated runs"
    printf '%s %s\n' "$code" "$name" >>"$WORK/error-identities"
    invalid_count=$((invalid_count + 1))
done <"$HERE/invalid/expected.tsv"
assert_num 'committed invalid-vector count' "$invalid_count" -eq 15
unique_errors=$(sort -u "$WORK/error-identities" | wc -l | tr -d ' ')
assert_num 'distinct invalid-vector error identities' "$unique_errors" -eq "$invalid_count"

# A hand edit to a golden is refused, not absorbed. The first edit changes one
# observer word on a statically guaranteed record, which is exactly the class of
# quiet edit that would let a borrowed view observe partial mutation; the second
# only reformats, and is caught by the canonical-form comparison above.
GOLDEN="$HERE/valid/same-size-unique.json"
assert_grep 'hand-edit source is a guaranteed golden' -F \
    '"reuse_statically_guaranteed": true' "$GOLDEN"
sed 's/"borrowed_view": "absent"/"borrowed_view": "live"/' "$GOLDEN" >"$WORK/hand-edited.json"
cmp -s "$GOLDEN" "$WORK/hand-edited.json" &&
    assert_fail 'the hand edit must actually change the golden'
hand_edit_status=0
node "$VALIDATOR" "$WORK/hand-edited.json" \
    >"$WORK/hand-edited.stdout" 2>"$WORK/hand-edited.stderr" || hand_edit_status=$?
assert_num 'hand-edited golden exit status' "$hand_edit_status" -eq 1
assert_file_empty 'hand-edited golden partial output' "$WORK/hand-edited.stdout"
assert_grep 'hand-edited golden named rejection' -E \
    '^reuse-candidate: RCV117 observer-live:' "$WORK/hand-edited.stderr"

tr -d '\n' <"$GOLDEN" >"$WORK/reformatted.json"
node "$VALIDATOR" "$WORK/reformatted.json" >"$WORK/reformatted.canonical"
cmp -s "$WORK/reformatted.json" "$WORK/reformatted.canonical" &&
    assert_fail 'a reformatted golden must not pass as its own canonical form'
cmp "$GOLDEN" "$WORK/reformatted.canonical" ||
    assert_fail 'canonical output must restore the committed golden bytes'

awk 'BEGIN {
    printf "{\"schema\":\"kofun.reuse-candidate/v1\",\"padding\":\""
    for (i = 0; i < 65536; i++) printf "x"
    printf "\"}\n"
}' >"$WORK/oversized.json"
oversized_status=0
node "$VALIDATOR" "$WORK/oversized.json" \
    >"$WORK/oversized.stdout" 2>"$WORK/oversized.stderr" || oversized_status=$?
assert_num 'oversized record exit status' "$oversized_status" -eq 1
assert_file_empty 'oversized record partial output' "$WORK/oversized.stdout"
assert_grep 'oversized record named rejection' -E \
    '^reuse-candidate: RCV003 limit-exceeded:' "$WORK/oversized.stderr"

assert_grep 'semantic preservation rule' -F \
    'MUST NOT change returned values' "$SPEC"
assert_grep 'ensure_move boundary' -F \
    'compiler.ensure_move(value)` is NOT constructor-storage uniqueness evidence' "$SPEC"
assert_grep 'remark instability' -F 'Remark wording is intentionally unstable' "$SPEC"
assert_grep 'no implemented reuse' -F \
    'No backend in the committed table honours a reuse guarantee' "$SPEC"
assert_grep 'partial-mutation rule' -F \
    'no observer can witness a partially rewritten node' "$SPEC"
assert_grep 'no silent weakening' -F 'MUST NOT silently weaken' "$SPEC"
for reason in incompatible-size incompatible-alignment incompatible-layout pinned \
    ffi-exposed weakly-referenced borrowed-view closure-capture possible-alias \
    owned-field-hazard backend-limitation
do
    assert_grep "closed reason $reason" -F "\`$reason\`" "$SPEC"
done
for observer in borrowed_view closure_capture weak_reference foreign_pointer alias
do
    assert_grep "observer $observer" -F "\`$observer\`" "$SPEC"
done

# No registered backend honours a reuse guarantee at this commit, so no valid
# vector may claim one is honoured. This keeps the corpus from drifting ahead of
# the tree it describes.
assert_not_grep 'no vector claims an honoured guarantee' -F \
    '"guarantee_disposition": "honour"' "$HERE/valid/same-size-unique.json"
honours=$(grep -lF '"reuse_support": "honours-guarantee"' "$HERE"/valid/*.json | wc -l | tr -d ' ')
assert_num 'valid vectors claiming an honouring backend' "$honours" -eq 0

printf '%s\n' \
    'PASS: ReuseCandidate v1 accepts nine canonical provenance/layout/observer vectors' \
    'PASS: fifteen invalid vectors fail with distinct named errors and no partial record' \
    'PASS: a hand-edited and a reformatted golden are both refused' \
    'PASS: AggregateLayout joins, remarks, and semantic-preservation boundaries are executable' \
    'PASS: no committed vector claims a reuse guarantee a registered backend could honour'
