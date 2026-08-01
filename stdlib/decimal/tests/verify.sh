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

node "$decimal_dir/tests/float_counterexample.js" \
    >"$work/float-counterexample.stdout"
cmp "$decimal_dir/tests/float_counterexample.stdout" \
    "$work/float-counterexample.stdout" ||
    fail 'binary64 associativity counterexample differs'

evidence="$decimal_dir/tests/law-evidence.json"
grep -Fq '"schema": "kofun.decimal-law-evidence/v1"' "$evidence" ||
    fail 'law evidence schema is missing'
grep -Fq '"assurance": "bounded-examples"' "$evidence" ||
    fail 'native bounded-example assurance is missing'
grep -Fq '"cases_checked": 15' "$evidence" ||
    fail 'native Decimal case count is missing'
grep -Fq '"status": "failed"' "$evidence" ||
    fail 'Float counterexample status is missing'
grep -Fq '"compiler_law_checker": "not-yet-general"' "$evidence" ||
    fail 'compiler law-checker boundary is not recorded'

[ "$(sed -n '1p' "$work/checkpoint.stdout")" -eq 15 ] ||
    fail 'native Decimal checkpoint did not execute all 15 observations'

printf 'compiler-native Decimal arithmetic and rounding: PASS\n'
printf 'five signed rounding modes: PASS\n'
printf 'explicit rounded division and format/parse: PASS\n'
printf 'binary64 associativity counterexample: PASS\n'
printf 'native Decimal ledger/tax reference: PASS\n'
