#!/bin/sh
set -eu

decimal_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
repo_dir=$(CDPATH= cd -- "$decimal_dir/../.." && pwd)
work=${TMPDIR:-/tmp}/kofun-decimal-verify.$$
mkdir -p "$work"

cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

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
for declaration in \
    'type Decimal = {' \
    'type RoundingMode =' \
    '| HalfUp' \
    '| HalfEven' \
    '| TowardZero' \
    '| Floor' \
    '| Ceiling' \
    'type Fixed = {' \
    'fn decimal_divide_exact(' \
    'fn decimal_divide(' \
    'fn fixed_assign('
do
    grep -Fq "$declaration" "$source_file" ||
        fail "missing canonical declaration: $declaration"
done

# There is intentionally no default-mode overload.
[ "$(grep -c '^fn decimal_round(' "$source_file")" -eq 1 ] ||
    fail 'decimal_round must have exactly one public signature'
grep -A5 '^fn decimal_round(' "$source_file" | grep -Fq 'mode: RoundingMode' ||
    fail 'decimal_round does not require a rounding mode'
grep -A7 '^fn decimal_divide(' "$source_file" | grep -Fq 'mode: RoundingMode' ||
    fail 'inexact division does not require a rounding mode'

set +e
"$repo_dir/bin/kofun" check "$source_file" \
    >"$work/canonical.check.stdout" 2>"$work/canonical.check.stderr"
canonical_status=$?
set -e
[ "$canonical_status" -ne 0 ] ||
    fail 'canonical ADT source unexpectedly claimed executable codegen'
grep -Fq 'error[E2S31]: malformed concrete enum declaration' \
    "$work/canonical.check.stderr" ||
    fail 'canonical ADT source did not expose the documented compiler boundary'

"$repo_dir/bin/kofun" run "$decimal_dir/tests/checkpoint.kofun" \
    >"$work/checkpoint.stdout"
cmp "$decimal_dir/tests/checkpoint.stdout" "$work/checkpoint.stdout" ||
    fail 'executable Decimal checkpoint differs'

"$repo_dir/bin/kofun" run "$decimal_dir/examples/ledger_tax.kofun" \
    >"$work/ledger-tax.stdout"
cmp "$decimal_dir/tests/ledger_tax.stdout" "$work/ledger-tax.stdout" ||
    fail 'ledger/tax reference differs'

node "$decimal_dir/tests/float_counterexample.js" \
    >"$work/float-counterexample.stdout"
cmp "$decimal_dir/tests/float_counterexample.stdout" \
    "$work/float-counterexample.stdout" ||
    fail 'binary64 associativity counterexample differs'

evidence="$decimal_dir/tests/law-evidence.json"
grep -Fq '"schema": "kofun.decimal-law-evidence/v1"' "$evidence" ||
    fail 'law evidence schema is missing'
grep -Fq '"cases_checked": 125' "$evidence" ||
    fail 'Decimal associativity case count is missing'
grep -Fq '"status": "failed"' "$evidence" ||
    fail 'Float counterexample status is missing'
grep -Fq '"compiler_law_checker": "not-yet-general"' "$evidence" ||
    fail 'compiler law-checker boundary is not recorded'

[ "$(sed -n '1p' "$work/checkpoint.stdout")" -eq 125 ] ||
    fail 'Decimal associativity did not check all 125 cases'
[ "$(sed -n '2p' "$work/checkpoint.stdout")" -eq 1 ] ||
    fail '0.1 + 0.2 did not equal 0.3 exactly'

printf 'decimal exact arithmetic and rounding: PASS\n'
printf 'decimal bounded associativity (125/125): PASS\n'
printf 'binary64 associativity counterexample: PASS\n'
printf 'decimal fixed ledger/tax reference: PASS\n'
