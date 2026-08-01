#!/bin/sh
set -eu

decimal_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
repo_dir=$(CDPATH= cd -- "$decimal_dir/../.." && pwd)
work=${TMPDIR:-/tmp}/kofun-decimal-verify.$$
mkdir -p "$work"
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail() {
    printf 'decimal checkpoint: FAIL: %s\n' "$*" >&2
    exit 1
}

if find "$decimal_dir" -type f \( -name '*.py' -o -name '*.kf' \) |
    grep -q .
then
    fail 'forbidden Python or .kf source found'
fi

source_file="$decimal_dir/decimal.kofun"
if grep -Eq '^type[[:space:]]+(Decimal|Fixed)[[:space:]]*=' "$source_file"; then
    fail 'native Decimal or future Fixed was redeclared as a stdlib record'
fi
if grep -Fq 'significand: Int' "$source_file"; then
    fail 'retired Int64 Decimal significand remains in the stdlib checkpoint'
fi
for surface in \
    'Decimal.round(value, destination_scale, mode)' \
    'Decimal.divide(left, right, destination_scale, mode)' \
    'Decimal.format(value, display_scale)' \
    'Decimal.parse(text)' \
    'HalfUp' 'HalfEven' 'TowardZero' 'Floor' 'Ceiling'
do
    grep -Fq "$surface" "$source_file" ||
        fail "missing native Decimal surface: $surface"
done

"$repo_dir/bin/kofun" run "$source_file" >"$work/canonical.stdout"
printf '1\n2.00\n' >"$work/canonical.expected"
cmp "$work/canonical.expected" "$work/canonical.stdout" ||
    fail 'canonical native Decimal checkpoint differs'

"$repo_dir/bin/kofun" run "$decimal_dir/tests/checkpoint.kofun" \
    >"$work/checkpoint.stdout"
cmp "$decimal_dir/tests/checkpoint.stdout" "$work/checkpoint.stdout" ||
    fail 'native Decimal rounding checkpoint differs'

"$repo_dir/bin/kofun" run "$decimal_dir/examples/ledger_tax.kofun" \
    >"$work/ledger-tax.stdout"
cmp "$decimal_dir/tests/ledger_tax.stdout" "$work/ledger-tax.stdout" ||
    fail 'native Decimal ledger/tax reference differs'
node "$decimal_dir/tests/ledger_tax_reference.mjs" \
    >"$work/ledger-tax-reference.stdout"
cmp "$work/ledger-tax-reference.stdout" "$work/ledger-tax.stdout" ||
    fail 'ledger/tax output differs from the independent BigInt reference'

node "$decimal_dir/tests/float_counterexample.js" \
    >"$work/float-counterexample.stdout"
cmp "$decimal_dir/tests/float_counterexample.stdout" \
    "$work/float-counterexample.stdout" ||
    fail 'binary64 associativity counterexample differs'

node "$decimal_dir/tests/generate_law_evidence.mjs" ||
    fail 'versioned Decimal/Float law evidence is stale or does not execute'
node "$decimal_dir/tests/validate_law_evidence.mjs" \
    "$repo_dir/spec/law-evidence-v2.schema.json" \
    "$decimal_dir/tests/law-evidence.json" \
    "$decimal_dir/tests/float-associativity-evidence.json" ||
    fail 'law evidence does not satisfy kofun.law-evidence/v2'
grep -Fq '"assurance": "bounded-exhaustive"' \
    "$decimal_dir/tests/law-evidence.json" ||
    fail 'bounded Decimal evidence lost its bounded-exhaustive label'
grep -Fq '"status": "failed"' \
    "$decimal_dir/tests/float-associativity-evidence.json" ||
    fail 'Float associativity evidence no longer records failure'
grep -Fq '"counterexample": {' \
    "$decimal_dir/tests/float-associativity-evidence.json" ||
    fail 'Float associativity evidence lost its witness'

set +e
KOFUN_CONFORMANCE_REQUIRE_ALL=1 \
    sh "$repo_dir/tests/conformance/run.sh" \
        "$repo_dir/tests/conformance/decimal-arithmetic" \
        >"$work/backend-differential.stdout" \
        2>"$work/backend-differential.stderr"
differential_status=$?
set -e
[ "$differential_status" -eq 1 ] ||
    fail "strict backend differential returned $differential_status instead of 1"
grep -Fq 'PASS [c11-stage2]' "$work/backend-differential.stdout" ||
    fail 'supported c11-stage2 backend did not execute Decimal conformance'
for backend in c11-stage1 native-aarch64 native-x86_64 wasm32-node
do
    grep -Fq "UNSUPPORTED [$backend] corpus decimal-arithmetic" \
        "$work/backend-differential.stdout" ||
        fail "unsupported backend $backend did not fail explicitly"
done
unsupported_failures=$(grep -c \
    'unsupported backend is a conformance failure' \
    "$work/backend-differential.stdout")
[ "$unsupported_failures" -eq 4 ] ||
    fail "strict differential recorded $unsupported_failures unsupported failures instead of 4"

[ "$(sed -n '1p' "$work/checkpoint.stdout")" -eq 15 ] ||
    fail 'native Decimal checkpoint did not execute all 15 observations'

printf 'compiler-native Decimal arithmetic and rounding: PASS\n'
printf 'five signed rounding modes: PASS\n'
printf 'explicit rounded division and format/parse: PASS\n'
printf 'binary64 associativity counterexample: PASS\n'
printf 'native Decimal ledger/tax reference: PASS\n'
printf 'versioned bounded-exhaustive law evidence: PASS\n'
printf 'all-backend fail-closed differential evidence: PASS\n'
