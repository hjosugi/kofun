#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
cd "$repo_root"

meta=bootstrap/selfhost/profile.meta
profile=bootstrap/selfhost/profile.tsv
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/kofun-selfhost-profile.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

fail() {
    printf '%s\n' "FAIL: self-host profile: $*" >&2
    exit 1
}

meta_value() {
    key=$1
    awk -F '|' -v key="$key" '
        $1 == key { value = $2; count += 1 }
        END {
            if (count != 1 || value == "") {
                exit 1
            }
            print value
        }
    ' "$meta"
}

test "$(meta_value schema)" = "kofun.selfhost-profile/v1" ||
    fail "unsupported metadata schema"
source_path=$(meta_value canonical_source) ||
    fail "canonical_source must appear exactly once"
expected_sha=$(meta_value source_sha256) ||
    fail "source_sha256 must appear exactly once"
test -f "$source_path" || fail "canonical source is missing: $source_path"

actual_sha=$(sha256sum "$source_path" | awk '{ print $1 }')
test "$actual_sha" = "$expected_sha" ||
    fail "$source_path changed; review profile rows and update source_sha256"

expected_header='category|feature|source_evidence|frontend|c11|self_compiler|positive|negative|differential|status'
header=$(sed -n '1p' "$profile")
test "$header" = "$expected_header" || fail "unexpected profile header"

awk -F '|' '
    NR == 1 { next }
    NF != 10 {
        printf "profile row %d has %d fields, expected 10\n", NR, NF > "/dev/stderr"
        failed = 1
        next
    }
    $1 == "" || $2 == "" {
        printf "profile row %d has an empty key\n", NR > "/dev/stderr"
        failed = 1
    }
    $10 != "missing" && $10 != "partial" && $10 != "complete" {
        printf "profile row %d has invalid status %s\n", NR, $10 > "/dev/stderr"
        failed = 1
    }
    {
        for (field = 3; field <= 9; field += 1) {
            if ($field == "") {
                printf "profile row %d has empty evidence field %d\n", NR, field > "/dev/stderr"
                failed = 1
            }
            if ($10 == "complete" && $field ~ /^planned:/) {
                printf "complete row %d still has planned evidence\n", NR > "/dev/stderr"
                failed = 1
            }
        }
    }
    END { exit failed }
' "$profile" || fail "invalid profile row"

tail -n +2 "$profile" > "$tmp_dir/rows"
LC_ALL=C sort -t '|' -k1,1 -k2,2 -c "$tmp_dir/rows" 2>/dev/null ||
    fail "profile rows must be sorted by category and feature"
cut -d '|' -f 1,2 "$tmp_dir/rows" > "$tmp_dir/expected-inventory"
test "$(wc -l < "$tmp_dir/expected-inventory")" -eq \
     "$(LC_ALL=C sort -u "$tmp_dir/expected-inventory" | wc -l)" ||
    fail "duplicate category/feature key"

awk -F '|' '
    NR == 1 { next }
    {
        for (field = 3; field <= 9; field += 1) {
            if ($field !~ /^planned:/) {
                print $field
            }
        }
    }
' "$profile" | LC_ALL=C sort -u > "$tmp_dir/evidence-paths"
while IFS= read -r evidence_path; do
    test -e "$evidence_path" ||
        fail "evidence path does not exist: $evidence_path"
done < "$tmp_dir/evidence-paths"

# Ignore comments and the continuation lines of the large emitted-C template.
# The remaining text is the language surface of S, not C tokens inside strings.
awk '
    /^[[:space:]]*#/ { next }
    /^[[:space:]]*"/ { next }
    { print }
' "$source_path" > "$tmp_dir/outer-source"

sed -n \
    's/^[[:space:]]*fn[[:space:]]\([A-Za-z_][A-Za-z0-9_]*\)(.*/\1/p' \
    "$tmp_dir/outer-source" | LC_ALL=C sort -u > "$tmp_dir/functions"
awk '!/^[[:space:]]*fn[[:space:]]/' "$tmp_dir/outer-source" |
    grep -Eo '[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(' |
    sed 's/[[:space:]]*(//' |
    LC_ALL=C sort -u > "$tmp_dir/calls"

comm -23 "$tmp_dir/calls" "$tmp_dir/functions" |
    sed 's/^/builtin|/' > "$tmp_dir/actual-inventory"

has() {
    grep -Eq "$1" "$tmp_dir/outer-source"
}

has '^[[:space:]]*} else if[[:space:]]' &&
    printf '%s\n' 'control|else-if' >> "$tmp_dir/actual-inventory"
has '^[[:space:]]*for[[:space:]].*[[:space:]]in[[:space:]].*\.\.' &&
    printf '%s\n' 'control|for-range' >> "$tmp_dir/actual-inventory"
has '^[[:space:]]*if[[:space:]]' &&
    printf '%s\n' 'control|if' >> "$tmp_dir/actual-inventory"
has '^[[:space:]]*return[[:space:]]+[^[:space:]]' &&
    printf '%s\n' 'control|return-value' >> "$tmp_dir/actual-inventory"
has '^[[:space:]]*return[[:space:]]*$' &&
    printf '%s\n' 'control|return-void' >> "$tmp_dir/actual-inventory"
has '^[[:space:]]*while[[:space:]]' &&
    printf '%s\n' 'control|while' >> "$tmp_dir/actual-inventory"

has '\|\||&&|![^=]' &&
    printf '%s\n' 'expression|boolean-operators' >> "$tmp_dir/actual-inventory"
has '==|!=|<=|>=|[[:space:]][<>][[:space:]]' &&
    printf '%s\n' 'expression|comparison' >> "$tmp_dir/actual-inventory"
comm -12 "$tmp_dir/calls" "$tmp_dir/functions" | grep -q . &&
    printf '%s\n' 'expression|direct-call' >> "$tmp_dir/actual-inventory"
has '\[[^]]+\]' &&
    printf '%s\n' 'expression|indexing' >> "$tmp_dir/actual-inventory"
has '=[^"]*[[:space:]][+-][[:space:]][A-Za-z0-9_(]' &&
    printf '%s\n' 'expression|integer-arithmetic' >> "$tmp_dir/actual-inventory"
has 'read_text\([^)]*\)[[:space:]]*\+[[:space:]]*"' &&
    printf '%s\n' 'expression|text-concatenation' >> "$tmp_dir/actual-inventory"

grep -qx 'args' "$tmp_dir/calls" &&
    printf '%s\n' 'host|command-line' >> "$tmp_dir/actual-inventory"
grep -qx 'read_text' "$tmp_dir/calls" &&
    printf '%s\n' 'host|file-read' >> "$tmp_dir/actual-inventory"
grep -qx 'write_text' "$tmp_dir/calls" &&
    printf '%s\n' 'host|file-write' >> "$tmp_dir/actual-inventory"
grep -qx 'print' "$tmp_dir/calls" &&
    printf '%s\n' 'host|stdout' >> "$tmp_dir/actual-inventory"

has '(^|[^A-Za-z0-9_])(true|false)([^A-Za-z0-9_]|$)' &&
    printf '%s\n' 'literal|Bool' >> "$tmp_dir/actual-inventory"
has '(^|[^A-Za-z0-9_])[0-9]+([^A-Za-z0-9_]|$)' &&
    printf '%s\n' 'literal|Int' >> "$tmp_dir/actual-inventory"
has '"([^"\\]|\\.)*"' &&
    printf '%s\n' 'literal|Text' >> "$tmp_dir/actual-inventory"

has '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*=[[:space:]]*[^=]' &&
    printf '%s\n' 'statement|assignment' >> "$tmp_dir/actual-inventory"
has '^[[:space:]]*let[[:space:]]+[A-Za-z_]' &&
    printf '%s\n' 'statement|immutable-local' >> "$tmp_dir/actual-inventory"
has '^[[:space:]]*let[[:space:]]+mut[[:space:]]' &&
    printf '%s\n' 'statement|mutable-local' >> "$tmp_dir/actual-inventory"
has '^[[:space:]]*fn[[:space:]]+[A-Za-z_][A-Za-z0-9_]*\([^)]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*:[[:space:]]*[^)]' &&
    printf '%s\n' 'syntax|function-parameter' >> "$tmp_dir/actual-inventory"
has '^[[:space:]]*fn[[:space:]].*\)[[:space:]]*->[[:space:]]*' &&
    printf '%s\n' 'syntax|function-result' >> "$tmp_dir/actual-inventory"
test -s "$tmp_dir/functions" &&
    printf '%s\n' 'syntax|top-level-function' >> "$tmp_dir/actual-inventory"

has '(^|[^A-Za-z0-9_])Bool([^A-Za-z0-9_]|$)' &&
    printf '%s\n' 'type|Bool' >> "$tmp_dir/actual-inventory"
has 'let([[:space:]]+mut)?[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*=[[:space:]]*[0-9]+' &&
    printf '%s\n' 'type|Int' >> "$tmp_dir/actual-inventory"
grep -Eq '^(args|chars)$' "$tmp_dir/calls" &&
    printf '%s\n' 'type|List[Text]' >> "$tmp_dir/actual-inventory"
has '(^|[^A-Za-z0-9_])Text([^A-Za-z0-9_]|$)' &&
    printf '%s\n' 'type|Text' >> "$tmp_dir/actual-inventory"
has '^[[:space:]]*fn[[:space:]]+[A-Za-z_][A-Za-z0-9_]*\([^)]*\)[[:space:]]*\{' &&
    printf '%s\n' 'type|Void' >> "$tmp_dir/actual-inventory"

# These constructs are intentionally outside the first profile. If S starts
# using one, emit a new inventory key so the manifest comparison fails.
for construct in import law match module trait type; do
    if has "^[[:space:]]*$construct[[:space:]]"; then
        printf '%s\n' "syntax|$construct" >> "$tmp_dir/actual-inventory"
    fi
done
has '\|>' && printf '%s\n' 'expression|pipeline' >> "$tmp_dir/actual-inventory"

LC_ALL=C sort -u "$tmp_dir/actual-inventory" -o "$tmp_dir/actual-inventory"
if ! cmp -s "$tmp_dir/expected-inventory" "$tmp_dir/actual-inventory"; then
    diff -u "$tmp_dir/expected-inventory" "$tmp_dir/actual-inventory" >&2 || :
    fail "canonical source feature inventory changed"
fi

printf '%s\n' \
    "PASS: first self-host profile pins $source_path ($actual_sha)" \
    "PASS: $(wc -l < "$tmp_dir/actual-inventory") source features have explicit coverage rows"
