#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CHECK="$ROOT/tests/conformance/check-capabilities.sh"
RUNNER="$ROOT/tests/conformance/run.sh"

work=$(mktemp -d "${TMPDIR:-/tmp}/kofun-capabilities-test.XXXXXX")
trap 'rm -rf "$work"' 0 1 2 15
backends="$work/backends"
corpora="$work/corpora"
manifest="$work/capabilities.tsv"
output="$work/output"
mkdir -p "$backends" "$corpora/sample"

write_alpha_adapter() {
    printf '%s\n' \
        'BACKEND_NAME=alpha' \
        'backend_compile() {' \
        '    output=$2' \
        '    printf "%s\\n" "#!/usr/bin/env sh" "printf '\''ok\\n'\''" >"$output"' \
        '    chmod +x "$output"' \
        '}' \
        >"$backends/alpha.sh"
}

write_supported_manifest() {
    printf '%s\n' \
        'backend	corpus	state	evidence	reason' \
        'alpha	sample	supported	tests/conformance/run.sh	-' \
        >"$manifest"
}

write_sample_corpus() {
    expected_count=${1-1}
    rm -f "$corpora/sample/"*.kofun
    printf '%s\n' \
        'fn expected_cases() -> Int {' \
        "    return $expected_count" \
        '}' \
        'fn corpus_name() -> Text {' \
        '    return "sample"' \
        '}' \
        >"$corpora/sample/expectations.kofun"
    printf '%s\n' \
        '# expect: ok' \
        'fn main() {' \
        '    print("ignored by the fixture adapter")' \
        '}' \
        >"$corpora/sample/case.kofun"
}

run_check() {
    sh "$CHECK" "$manifest" "$backends" "$corpora"
}

run_runner() {
    KOFUN_CONFORMANCE_BACKENDS=$backends \
    KOFUN_CONFORMANCE_CAPABILITIES=$manifest \
    KOFUN_CONFORMANCE_CORPORA=$corpora \
        sh "$RUNNER" "$corpora/sample"
}

expect_failure() {
    label=$1
    pattern=$2
    shift 2
    set +e
    "$@" >"$output.stdout" 2>"$output.stderr"
    status=$?
    set -e
    if test "$status" -eq 0; then
        printf '%s\n' "FAIL: $label was accepted" >&2
        exit 1
    fi
    if ! grep -F "$pattern" "$output.stdout" "$output.stderr" >/dev/null; then
        printf '%s\n' "FAIL: $label produced the wrong diagnostic" >&2
        sed 's/^/  /' "$output.stdout" "$output.stderr" >&2
        exit 1
    fi
}

write_alpha_adapter
write_supported_manifest
write_sample_corpus
run_check >/dev/null
run_runner >/dev/null

printf '%s\n' \
    'alpha	sample	supported	tests/conformance/run.sh	-' \
    >>"$manifest"
expect_failure \
    "duplicate manifest entry" \
    'duplicate entry for `alpha` / `sample`' \
    run_check

write_supported_manifest
printf '%s\n' \
    'alpha	sample	unsupported	-	not implemented' \
    >>"$manifest"
expect_failure \
    "contradictory manifest entry" \
    'contradictory entries for `alpha` / `sample`' \
    run_check

printf '%s\n' \
    'backend	corpus	state	evidence	reason' \
    >"$manifest"
expect_failure \
    "missing manifest entry" \
    'missing entry for `alpha` / `sample`' \
    run_check

write_supported_manifest
printf '%s\n' \
    'ghost	sample	supported	tests/conformance/run.sh	-' \
    >>"$manifest"
expect_failure \
    "unknown backend" \
    'unknown backend `ghost`' \
    run_check

write_supported_manifest
printf '%s\n' \
    'alpha	ghost	supported	tests/conformance/run.sh	-' \
    >>"$manifest"
expect_failure \
    "unknown corpus" \
    'unknown corpus `ghost`' \
    run_check

printf '%s\n' \
    'backend	corpus	state	evidence	reason' \
    'alpha	sample	unsupported	-	-' \
    >"$manifest"
expect_failure \
    "unsupported entry without reason" \
    'unsupported entry lacks a reason for `alpha` / `sample`' \
    run_check

printf '%s\n' \
    'backend	corpus	state	evidence	reason' \
    'alpha	sample	maybe	-	unknown' \
    >"$manifest"
expect_failure \
    "unknown capability state" \
    'unknown state `maybe` for `alpha` / `sample`' \
    run_check

write_supported_manifest
printf '%s\n' \
    'BACKEND_NAME=alpha' \
    'BACKEND_CORPORA=sample' \
    'backend_compile() { return 0; }' \
    >"$backends/alpha.sh"
expect_failure \
    "adapter capability authority drift" \
    'adapter carries independent BACKEND_CORPORA authority' \
    run_check

printf '%s\n' \
    'BACKEND_NAME=renamed' \
    'backend_compile() { return 0; }' \
    >"$backends/alpha.sh"
expect_failure \
    "adapter identity drift" \
    'adapter identity mismatch: alpha declares renamed' \
    run_check

write_alpha_adapter
printf '%s\n' \
    'fn backend_names() -> List[Text] {' \
    '    return ["alpha"]' \
    '}' \
    >"$corpora/sample/expectations.kofun"
expect_failure \
    "expectation capability authority drift" \
    'expectations must not define backend capability authority' \
    run_check

write_sample_corpus
printf '%s\n' \
    'BACKEND_NAME=beta' \
    'backend_compile() { return 1; }' \
    >"$backends/beta.sh"
printf '%s\n' \
    'backend	corpus	state	evidence	reason' \
    'alpha	sample	supported	tests/conformance/run.sh	-' \
    'beta	sample	supported	tests/conformance/run.sh	-' \
    >"$manifest"
run_check >/dev/null
rm "$backends/alpha.sh"
expect_failure \
    "adapter disappearance" \
    'unknown backend `alpha`' \
    run_check
rm "$backends/beta.sh"
write_alpha_adapter
write_supported_manifest

printf '%s\n' \
    'BACKEND_NAME=alpha' \
    'backend_compile() {' \
    '    printf "%s\\n" "fixture adapter refuses the case"' \
    '    return 125' \
    '}' \
    >"$backends/alpha.sh"
expect_failure \
    "supported adapter returned 125" \
    'supported capability returned status 125' \
    run_runner

write_alpha_adapter
write_sample_corpus 2
printf '%s\n' \
    '# expect: ok' \
    'fn main() {' \
    '    print("ignored by the fixture adapter")' \
    '}' \
    >"$corpora/sample/skip.kofun"
printf '%s\n' \
    'BACKEND_NAME=alpha' \
    'backend_compile() {' \
    '    source=$1' \
    '    output=$2' \
    '    case $source in' \
    '        *skip.kofun)' \
    '            printf "%s\\n" "fixture adapter refuses one supported case"' \
    '            return 125' \
    '            ;;' \
    '    esac' \
    '    printf "%s\\n" "#!/usr/bin/env sh" "printf '\''ok\\n'\''" >"$output"' \
    '    chmod +x "$output"' \
    '}' \
    >"$backends/alpha.sh"
expect_failure \
    "partially skipped supported corpus" \
    'supported capability returned status 125' \
    run_runner

write_sample_corpus
printf '%s\n' \
    'BACKEND_NAME=alpha' \
    'backend_compile() { return 0; }' \
    >"$backends/alpha.sh"
expect_failure \
    "supported adapter produced no executable" \
    'compile failed' \
    run_runner

write_alpha_adapter
rm "$corpora/sample/case.kofun"
expect_failure \
    "expectations and corpus case count drift" \
    'corpus sample expected 1 cases but found 0' \
    run_runner

write_sample_corpus
write_alpha_adapter
printf '%s\n' \
    'BACKEND_NAME=beta' \
    'backend_check_available() {' \
    '    printf "%s\\n" "fixture executor is unavailable"' \
    '    return 125' \
    '}' \
    'backend_compile() { return 1; }' \
    >"$backends/beta.sh"
printf '%s\n' \
    'backend	corpus	state	evidence	reason' \
    'alpha	sample	supported	tests/conformance/run.sh	-' \
    'beta	sample	supported	tests/conformance/run.sh	-' \
    >"$manifest"
run_runner >"$output.stdout" 2>"$output.stderr"
grep -F 'UNAVAILABLE [beta] executor for corpus sample' \
    "$output.stdout" >/dev/null || {
    printf '%s\n' \
        "FAIL: executor availability was treated as capability policy" >&2
    exit 1
}

sh "$CHECK" >/dev/null
printf '%s\n' \
    "PASS: conformance capability manifest rejects policy and coverage drift"
