#!/bin/sh
set -eu

list_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
repo_dir=$(CDPATH= cd -- "$list_dir/../.." && pwd)
work=${TMPDIR:-/tmp}/kofun-list-verify.$$
mkdir -p "$work"

cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

fail() {
    printf 'list checkpoint: FAIL: %s\n' "$*" >&2
    exit 1
}

if find "$list_dir" -type f \( -name '*.py' -o -name '*.kf' \) |
    grep -q .
then
    fail 'forbidden Python or .kf source found'
fi

source_file="$list_dir/list.kofun"
for declaration in \
    'type ListError =' \
    '| ListEmpty' \
    '| ListIndexOutOfBounds(Int, Int)' \
    'fn list_int_empty(' \
    'fn list_int_length(' \
    'fn list_int_is_empty(' \
    'fn list_int_get(' \
    'fn list_int_first(' \
    'fn list_int_last(' \
    'fn list_int_push(' \
    'fn list_int_concat(' \
    'fn list_int_reverse(' \
    'fn list_int_map(' \
    'fn list_int_filter(' \
    'fn list_int_fold(' \
    'fn list_int_contains('
do
    grep -Fq "$declaration" "$source_file" ||
        fail "missing canonical declaration: $declaration"
done

for behavior in \
    'if index < -length || index >= length' \
    'Err(ListIndexOutOfBounds(index, length))' \
    'return Err(ListEmpty)' \
    'return Ok(values[-1])' \
    'return map(values, transform)' \
    'return filter(values, predicate)' \
    'return fold(values, initial, combine)'
do
    grep -Fq "$behavior" "$source_file" ||
        fail "missing canonical behavior: $behavior"
done

if grep -Eq 'trusted|(^|[^a-z_])(print|clock|random|exit)\(' "$source_file"
then
    fail 'canonical List API contains a trusted or nondeterministic effect'
fi

set +e
"$repo_dir/bin/kofun" check "$source_file" \
    >"$work/canonical.check.stdout" 2>"$work/canonical.check.stderr"
canonical_status=$?
set -e
[ "$canonical_status" -ne 0 ] ||
    fail 'canonical ADT source unexpectedly claimed executable codegen'
grep -Fq 'error[E2S21]: ownership slice supports one borrowed List parameter per function' \
    "$work/canonical.check.stderr" ||
    fail 'canonical API did not expose the documented compiler boundary'

checkpoint="$list_dir/tests/checkpoint.kofun"
expected="$list_dir/tests/checkpoint.stdout"
for operation in \
    'len(values)' \
    'values[0]' \
    'values[-1]' \
    'map(values,' \
    'filter(values,' \
    'fold(values,' \
    'fold([],' \
    'filter(values, fn(value: Int) => value > 1)'
do
    grep -Fq "$operation" "$checkpoint" ||
        fail "executable projection is missing: $operation"
done
"$repo_dir/bin/kofun" build "$checkpoint" \
    --target x86_64-linux -o "$work/checkpoint-native" >/dev/null
"$work/checkpoint-native" >"$work/checkpoint-native.stdout"
cmp "$expected" "$work/checkpoint-native.stdout" ||
    fail 'direct x86-64 List vectors differ'

projection="$list_dir/tests/projection.kofun"
projection_expected="$list_dir/tests/projection.stdout"
"$repo_dir/bin/kofun" build "$projection" \
    -o "$work/projection-c11" \
    --emit-c "$work/projection.c" >/dev/null
"$work/projection-c11" >"$work/projection-c11.stdout"
cmp "$projection_expected" "$work/projection-c11.stdout" ||
    fail 'C11 checked-index projection differs'

"$repo_dir/bin/kofun" build "$projection" \
    --target x86_64-linux -o "$work/projection-native" >/dev/null
"$work/projection-native" >"$work/projection-native.stdout"
cmp "$projection_expected" "$work/projection-native.stdout" ||
    fail 'direct x86-64 checked-index projection differs'
cmp "$work/projection-c11.stdout" "$work/projection-native.stdout" ||
    fail 'C11 and direct x86-64 checked-index outcomes differ'

printf 'list List[Int] values and pipelines: PASS\n'
printf 'list typed access projection C11/x86-64 differential: PASS\n'
