#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/records"
CC=${CC:-cc}
WORK=${KOFUN_RECORD_FRONTEND_WORK:-"$ROOT/build/record-frontend"}

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
case $WORK in
    */record-frontend|*/record-frontend.*) ;;
    *) fail "work directory must end in record-frontend[.suffix]: $WORK" ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$ROOT/bootstrap/stage2/record_frontend.c" \
    -o "$WORK/kofun-record-frontend"

frontend() {
    stem=$1
    label=$2
    "$WORK/kofun-record-frontend" "$CASES/$stem.kofun" \
        "$WORK/$label.ir" "$WORK/$label.layout" "$WORK/$label.run"
}

require_line() {
    file=$1
    needle=$2
    grep -Fq "$needle" "$file" || fail "$file does not contain: $needle"
}

# ------------------------------------------------------------------ tokens
# The Token-shaped record is the accepted proof: an enumeration, a `Text`, and
# an `Int` field constructed, passed, returned, and read by a real scanner.

frontend token_pipeline token_pipeline
frontend token_pipeline token_pipeline.second
for artifact in ir layout run; do
    cmp "$WORK/token_pipeline.$artifact" \
        "$WORK/token_pipeline.second.$artifact" ||
        fail "repeated record $artifact differs"
    cmp "$CASES/token_pipeline.$artifact" \
        "$WORK/token_pipeline.$artifact" ||
        fail "token pipeline $artifact golden differs"
done

require_line "$WORK/token_pipeline.ir" \
    'record|record-id=record:Token|name=Token'
require_line "$WORK/token_pipeline.ir" \
    'field|field-id=field:record:Token:0|record-id=record:Token|name=kind|index=0|type=TokenKind'
require_line "$WORK/token_pipeline.ir" \
    'field|field-id=field:record:Token:2|record-id=record:Token|name=start|index=2|type=Int'

# Written field order is free; declaration order is the stored order.
require_line "$WORK/token_pipeline.ir" \
    'written=kind,text,start|declared=kind,text,start'
require_line "$WORK/token_pipeline.ir" \
    'written=start,kind,text|declared=kind,text,start'

# Every field read carries the declared field type, not an inferred one.
require_line "$WORK/token_pipeline.ir" \
    'read|function=describe|record-id=record:Token|field-id=field:record:Token:0|name=kind|type=TokenKind'
require_line "$WORK/token_pipeline.ir" \
    'read|function=width|record-id=record:Token|field-id=field:record:Token:1|name=text|type=Text'
require_line "$WORK/token_pipeline.ir" \
    'read|function=last_start|record-id=record:Token|field-id=field:record:Token:2|name=start|type=Int'

# Records pass and return across function boundaries under `read` access.
require_line "$WORK/token_pipeline.ir" \
    'param|function=describe|index=0|name=token|type=Token|access=read'
require_line "$WORK/token_pipeline.ir" \
    'function|name=scan|params=1|result=List[Token]'

# ------------------------------------------------------------------ layout
# Layout is untagged, declaration ordered, and named per target data layout.

for target in x86_64-linux aarch64-linux; do
    require_line "$WORK/token_pipeline.layout" \
        "target|name=$target|data-layout=little-endian LP64;"
    require_line "$WORK/token_pipeline.layout" \
        "record|target=$target|record-id=record:Token|size=32|align=8|payload=25|tagged=false|fields=3"
    require_line "$WORK/token_pipeline.layout" \
        "field|target=$target|record-id=record:Token|index=0|name=kind|type=TokenKind|offset=0|size=1|align=1"
    require_line "$WORK/token_pipeline.layout" \
        "field|target=$target|record-id=record:Token|index=2|name=start|type=Int|offset=24|size=8|align=8"
done
require_line "$WORK/token_pipeline.layout" \
    'agreement|record-id=record:Token|targets=x86_64-linux,aarch64-linux|identical=true'

# The agreement line is a claim; recompute it from the emitted rows instead of
# trusting it.
grep -E '^(record|field)\|target=x86_64-linux\|' "$WORK/token_pipeline.layout" |
    sed 's/|target=x86_64-linux|/|/' >"$WORK/layout.x86_64"
grep -E '^(record|field)\|target=aarch64-linux\|' "$WORK/token_pipeline.layout" |
    sed 's/|target=aarch64-linux|/|/' >"$WORK/layout.aarch64"
cmp "$WORK/layout.x86_64" "$WORK/layout.aarch64" ||
    fail 'x86-64 and AArch64 record layout disagree'
test -s "$WORK/layout.x86_64" || fail 'no per-target layout rows were emitted'

# ------------------------------------------------------------- evaluation
# Construct, pass, return, and read are observed, not asserted.

require_line "$WORK/token_pipeline.run" \
    'call|function=first_token|result=Token(kind: TokenKind.Identifier, text: "let", start: 0)'
require_line "$WORK/token_pipeline.run" \
    'call|function=scanned|result=[Token(kind: TokenKind.Identifier, text: "let", start: 0), Token(kind: TokenKind.Identifier, text: "x", start: 4), Token(kind: TokenKind.Symbol, text: "=", start: 6), Token(kind: TokenKind.Number, text: "42", start: 8)]'
require_line "$WORK/token_pipeline.run" \
    'call|function=summary|result="id:let id:x sym:= num:42 "'
require_line "$WORK/token_pipeline.run" 'call|function=total_width|result=7'
require_line "$WORK/token_pipeline.run" 'call|function=final_start|result=8'

# ------------------------------------------------------------- ambiguity
# Blocks, conditions, loop iterables, list literals, and flat constructors stay
# separable from record construction.

frontend ambiguity ambiguity
frontend declaration_order declaration_order
cmp "$CASES/ambiguity.run" "$WORK/ambiguity.run" ||
    fail 'ambiguity evaluation golden differs'
cmp "$CASES/ambiguity.layout" "$WORK/ambiguity.layout" ||
    fail 'ambiguity layout golden differs'
cmp "$CASES/declaration_order.run" "$WORK/declaration_order.run" ||
    fail 'declaration order evaluation golden differs'
require_line "$WORK/ambiguity.run" 'call|function=counted|result=3'
require_line "$WORK/ambiguity.run" \
    'call|function=pair|result=[Point(x: 1, y: 2), Point(x: 6, y: 8)]'
require_line "$WORK/ambiguity.run" 'call|function=flagged|result=1'

# Nominal identity does not depend on declaration order, and a construction
# written out of order still stores fields in declaration order.
for stem in ambiguity declaration_order; do
    grep -E '^(record|field)\|' "$WORK/$stem.ir" |
        sed 's/|span=[0-9][0-9]*\.\.[0-9][0-9]*//' >"$WORK/$stem.ids"
done
cmp "$WORK/ambiguity.ids" "$WORK/declaration_order.ids" ||
    fail 'declaration order changed nominal record or field identities'
require_line "$WORK/declaration_order.run" \
    'call|function=shifted|result=Point(x: 2, y: 1)'

# ------------------------------------------------------------ diagnostics

expect_failure() {
    stem=$1
    code=$2
    set +e
    "$WORK/kofun-record-frontend" "$CASES/$stem.kofun" \
        "$WORK/$stem.ir" "$WORK/$stem.layout" "$WORK/$stem.run" \
        >"$WORK/$stem.actual" 2>"$WORK/$stem.internal.stderr"
    status=$?
    set -e
    test "$status" -eq 1 || fail "$stem exited $status instead of 1"
    test ! -s "$WORK/$stem.internal.stderr" ||
        fail "$stem wrote internal stderr"
    test ! -e "$WORK/$stem.ir" || fail "$stem emitted rejected IR"
    test ! -e "$WORK/$stem.layout" || fail "$stem emitted rejected layout"
    test ! -e "$WORK/$stem.run" || fail "$stem emitted rejected evaluation"
    cmp "$CASES/$stem.stderr" "$WORK/$stem.actual" ||
        fail "$stem diagnostic differs"
    grep -F "error[$code]:" "$WORK/$stem.actual" >/dev/null ||
        fail "$stem expected $code"
    printf '%s\n' "PASS record diagnostic: $stem"
}

expect_failure bound_exceeded E2S106
expect_failure malformed_return E2S107
expect_failure duplicate_type E2S108
expect_failure duplicate_declared_field E2S109
expect_failure unknown_field_type E2S110
expect_failure generic_record E2S111
expect_failure recursive_record E2S112
expect_failure unknown_name E2S113
expect_failure duplicate_construction_field E2S114
expect_failure missing_field E2S115
expect_failure unknown_field E2S116
expect_failure wrong_field_type E2S117
expect_failure positional_construction E2S118
expect_failure brace_construction E2S119
expect_failure unknown_field_read E2S120
expect_failure field_assignment E2S121
expect_failure edit_parameter E2S121
expect_failure partial_move E2S122
expect_failure use_after_move E2S123
expect_failure argument_type_mismatch E2S124
expect_failure map_literal E2S125
expect_failure evaluation_failure E2S126

# Every diagnostic case must be exercised: an unlisted fixture is a gap.
for source in "$CASES"/*.stderr; do
    stem=$(basename "$source" .stderr)
    test -f "$WORK/$stem.actual" || fail "diagnostic fixture $stem is not run"
done

test -z "$(find "$WORK" -type f \
    \( -name '*.generated.c' -o -name '*.o' -o -name '*.native' \) -print)" ||
    fail 'record frontend emitted a backend/runtime artifact'

# ------------------------------------------------------- Stage 2 C11 slice
# The accepted bounded backend slice uses only nominal Int/Bool fields.  It
# must execute construction in either written label order, pass and return the
# nominal value, and read both field types.

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/stage2/compiler.c" \
    -o "$WORK/kofun-stage2"
"$WORK/kofun-stage2" \
    "$CASES/record_functions.kofun" \
    "$WORK/record_functions.c" \
    "$WORK/record_functions.stage2.ir" \
    "$WORK/record_functions.tokens" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$WORK/record_functions.c" \
    -o "$WORK/record_functions"
"$WORK/record_functions" \
    >"$WORK/record_functions.stdout" \
    2>"$WORK/record_functions.stderr"
cmp "$CASES/record_functions.stdout" "$WORK/record_functions.stdout" ||
    fail 'Stage 2 nominal record output differs'
test ! -s "$WORK/record_functions.stderr" ||
    fail 'Stage 2 nominal record program wrote unexpected stderr'
grep -F \
    '_Static_assert(offsetof(KofunRecord_Packet, f_count) == 0,' \
    "$WORK/record_functions.c" >/dev/null ||
    fail 'Stage 2 record count offset disagrees with AggregateLayout'
grep -F \
    '_Static_assert(offsetof(KofunRecord_Packet, f_enabled) == 8,' \
    "$WORK/record_functions.c" >/dev/null ||
    fail 'Stage 2 record Bool offset disagrees with AggregateLayout'
grep -F \
    '_Static_assert(sizeof(KofunRecord_Packet) == 16,' \
    "$WORK/record_functions.c" >/dev/null ||
    fail 'Stage 2 record size disagrees with AggregateLayout'
enabled_line=$(grep -n 'k_b4.f_enabled = true;' \
    "$WORK/record_functions.c" | cut -d: -f1)
count_line=$(grep -n 'k_b4.f_count = INT64_C(41);' \
    "$WORK/record_functions.c" | cut -d: -f1)
test "$enabled_line" -lt "$count_line" ||
    fail 'Stage 2 reordered labelled record field evaluation'
grep '^static int64_t kofun_fn_score(KofunRecord_Packet ' \
    "$WORK/record_functions.c" >/dev/null ||
    fail 'Stage 2 did not lower the nominal record parameter'
grep '^static KofunRecord_Packet kofun_fn_make_packet' \
    "$WORK/record_functions.c" >/dev/null ||
    fail 'Stage 2 did not lower the nominal record result'

expect_stage2_failure() {
    stem=$1
    set +e
    "$WORK/kofun-stage2" \
        "$CASES/$stem.kofun" \
        "$WORK/$stem.c" \
        "$WORK/$stem.stage2.ir" \
        "$WORK/$stem.tokens" \
        >"$WORK/$stem.stage2.actual" \
        2>"$WORK/$stem.stage2.internal.stderr"
    stage2_status=$?
    set -e
    test "$stage2_status" -eq 1 ||
        fail "$stem exited $stage2_status instead of 1"
    cmp "$CASES/$stem.diagnostic" "$WORK/$stem.stage2.actual" ||
        fail "$stem Stage 2 diagnostic differs"
    test ! -s "$WORK/$stem.stage2.internal.stderr" ||
        fail "$stem wrote internal Stage 2 stderr"
    test ! -e "$WORK/$stem.c" ||
        fail "$stem emitted rejected C"
}

expect_stage2_failure stage2_unsupported_field
expect_stage2_failure stage2_direct_construction
expect_stage2_failure stage2_labelled_call

printf '%s\n' \
    'PASS: Token-shaped records construct, pass, return, and read' \
    'PASS: written field order is free and storage follows declaration order' \
    'PASS: nominal record and field identities ignore declaration order' \
    'PASS: layout is untagged and identical on x86-64 and AArch64' \
    'PASS: blocks, conditions, loops, and lists stay separable from records' \
    'PASS: duplicate, missing, unknown, wrong-type, mutation, and move diagnostics are exact' \
    'PASS: Stage 2 executes nominal Int/Bool records in AggregateLayout order'
