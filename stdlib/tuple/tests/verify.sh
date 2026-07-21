#!/bin/sh
set -eu

tuple_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
repo_dir=$(CDPATH= cd -- "$tuple_dir/../.." && pwd)
work=${TMPDIR:-/tmp}/kofun-tuple-verify.$$
mkdir -p "$work"

cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

fail() {
    printf 'tuple checkpoint: FAIL: %s\n' "$*" >&2
    exit 1
}

if find "$tuple_dir" -type f \( -name '*.py' -o -name '*.kf' \) |
    grep -q .
then
    fail 'forbidden Python or .kf source found'
fi

source_file="$tuple_dir/tuple.kofun"
for declaration in \
    'let TUPLE2_ARITY = 2' \
    'fn tuple2_int(' \
    'fn tuple2_int_first(' \
    'fn tuple2_int_second(' \
    'fn tuple2_int_swap(' \
    'fn tuple2_int_map(' \
    'fn tuple2_int_bimap(' \
    'fn tuple2_int_fold(' \
    'fn tuple2_int_contains(' \
    'fn tuple2_int_same(' \
    'fn tuple2_int_to_list('
do
    grep -Fq "$declaration" "$source_file" ||
        fail "missing canonical declaration: $declaration"
done

for behavior in \
    'return (first, second)' \
    'return pair[0]' \
    'return pair[1]' \
    'return (pair[1], pair[0])' \
    'return (transform(pair[0]), transform(pair[1]))' \
    'return combine(combine(initial, pair[0]), pair[1])' \
    'return pair[0] == target || pair[1] == target' \
    'return left[0] == right[0] && left[1] == right[1]' \
    'return [pair[0], pair[1]]'
do
    grep -Fq "$behavior" "$source_file" ||
        fail "missing canonical behavior: $behavior"
done

if grep -Eq 'trusted|(^|[^a-z_])(print|clock|random|exit)\(' "$source_file"
then
    fail 'canonical Tuple API contains a trusted or nondeterministic effect'
fi

set +e
"$repo_dir/bin/kofun" check "$source_file" \
    >"$work/canonical.check.stdout" 2>"$work/canonical.check.stderr"
canonical_status=$?
set -e
[ "$canonical_status" -ne 0 ] ||
    fail 'canonical Tuple source unexpectedly claimed executable codegen'
grep -Fq 'error[E2S02]: expected top-level `fn` or `type`' \
    "$work/canonical.check.stderr" ||
    fail 'canonical API did not expose the documented compiler boundary'

checkpoint="$tuple_dir/tests/checkpoint.kofun"
expected="$tuple_dir/tests/checkpoint.stdout"
for operation in \
    'tuple_first(2, 3)' \
    'tuple_second(2, 3)' \
    'tuple_swap_first(2, 3)' \
    'tuple_swap_second(2, 3)' \
    'tuple_map(tuple_first(2, 3))' \
    'tuple_map(tuple_second(2, 3))' \
    'tuple_fold(1, 2, 3)' \
    'tuple_contains(2, 3, 3)' \
    'tuple_same(2, 3, 2, 4)'
do
    grep -Fq "$operation" "$checkpoint" ||
        fail "executable projection is missing: $operation"
done

"$repo_dir/bin/kofun" build "$checkpoint" \
    -o "$work/checkpoint-c11" \
    --emit-c "$work/checkpoint.c" >/dev/null
"$work/checkpoint-c11" >"$work/checkpoint-c11.stdout"
cmp "$expected" "$work/checkpoint-c11.stdout" ||
    fail 'C11 Tuple projection vectors differ'

"$repo_dir/bin/kofun" build "$checkpoint" \
    --target x86_64-linux -o "$work/checkpoint-native" >/dev/null
"$work/checkpoint-native" >"$work/checkpoint-native.stdout"
cmp "$expected" "$work/checkpoint-native.stdout" ||
    fail 'direct x86-64 Tuple projection vectors differ'
cmp "$work/checkpoint-c11.stdout" "$work/checkpoint-native.stdout" ||
    fail 'C11 and direct x86-64 Tuple projections differ'

printf 'tuple Tuple[Int, Int] canonical surface: PASS\n'
printf 'tuple scalar projection C11/x86-64 differential: PASS\n'
