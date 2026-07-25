#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -P -- "$(dirname -- "$0")/../.." && pwd)
CHECK="$ROOT/tests/conformance/check-capabilities.sh"
RUNNER="$ROOT/tests/conformance/run.sh"
DIGEST="$ROOT/tests/conformance/observation-digest.sh"

work=$(mktemp -d "${TMPDIR:-/tmp}/kofun-capabilities-test.XXXXXX")
untracked_evidence="$ROOT/build/capabilities-untracked-evidence.$$"
trap 'rm -rf "$work"; rm -f "$untracked_evidence"' 0 1 2 15
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

write_sample_expectations() {
    names="$work/sample-case-names"
    : >"$names"
    for source in "$corpora/sample/"*.kofun; do
        test -f "$source" || continue
        test "$(basename "$source")" != expectations.kofun || continue
        basename "$source" >>"$names"
    done
    LC_ALL=C sort "$names" >"$names.sorted"
    expected_count=$(awk 'END { print NR + 0 }' "$names.sorted")
    expected_digest=$(sh "$DIGEST" "$corpora/sample")
    printf '%s\n' \
        'fn corpus_name() -> Text {' \
        '    return "sample"' \
        '}' \
        '' \
        'fn expected_cases() -> Int {' \
        "    return $expected_count" \
        '}' \
        >"$corpora/sample/expectations.kofun"
    printf '%s\n' \
        '' \
        'fn case_names() -> List[Text] {' \
        '    return [' \
        >>"$corpora/sample/expectations.kofun"
    while IFS= read -r name; do
        printf '        "%s",\n' "$name" \
            >>"$corpora/sample/expectations.kofun"
    done <"$names.sorted"
    printf '%s\n' \
        '    ]' \
        '}' \
        '' \
        'fn observations_sha256() -> Text {' \
        "    return \"$expected_digest\"" \
        '}' \
        >>"$corpora/sample/expectations.kofun"
}

write_sample_corpus() {
    rm -f "$corpora/sample/"*.kofun
    printf '%s\n' \
        '# expect: ok' \
        'fn main() {' \
        '    print("ignored by the fixture adapter")' \
        '}' \
        >"$corpora/sample/case.kofun"
    write_sample_expectations
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

run_rogue_runner() {
    KOFUN_CONFORMANCE_BACKENDS=$backends \
    KOFUN_CONFORMANCE_CAPABILITIES=$manifest \
    KOFUN_CONFORMANCE_CORPORA=$corpora \
        sh "$RUNNER" "$work/rogue/sample"
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
write_sample_corpus
printf '%s\n' \
    'fn backend_names() -> List[Text] {' \
    '    return ["alpha"]' \
    '}' \
    >>"$corpora/sample/expectations.kofun"
expect_failure \
    "expectation capability authority drift" \
    'expectations must not define backend capability authority' \
    run_check

write_sample_corpus
sed \
    's/^fn corpus_name() -> Text {$/fn corpus_name(bogus: Int) -> Bogus {/' \
    "$corpora/sample/expectations.kofun" >"$output"
mv "$output" "$corpora/sample/expectations.kofun"
expect_failure \
    "malformed corpus_name signature" \
    'expectations must declare one literal corpus_name' \
    run_check

write_sample_corpus
sed \
    's/^fn expected_cases() -> Int {$/fn expected_cases(bogus: Int) -> Bogus {/' \
    "$corpora/sample/expectations.kofun" >"$output"
mv "$output" "$corpora/sample/expectations.kofun"
expect_failure \
    "malformed expected_cases signature" \
    'expectations must declare one literal expected_cases count' \
    run_check

write_sample_corpus
sed \
    's/^fn case_names() -> List\[Text\] {$/fn case_names(bogus: Int) -> Bogus {/' \
    "$corpora/sample/expectations.kofun" >"$output"
mv "$output" "$corpora/sample/expectations.kofun"
expect_failure \
    "malformed case_names signature" \
    'expectations must declare one literal case_names list' \
    run_check

write_sample_corpus
sed \
    's/^fn observations_sha256() -> Text {$/fn observations_sha256(bogus: Int) -> Bogus {/' \
    "$corpora/sample/expectations.kofun" >"$output"
mv "$output" "$corpora/sample/expectations.kofun"
expect_failure \
    "malformed observations_sha256 signature" \
    'expectations must declare one literal observations_sha256 digest' \
    run_check

write_sample_corpus
sed '$d' "$corpora/sample/expectations.kofun" >"$output"
mv "$output" "$corpora/sample/expectations.kofun"
expect_failure \
    "unterminated expectations function" \
    'expectations must declare one literal observations_sha256 digest' \
    run_check

write_sample_corpus
printf '%s\n' \
    'fn corpus_name(bogus: Int) -> Bogus {' \
    '    return "sample"' \
    '}' \
    >>"$corpora/sample/expectations.kofun"
expect_failure \
    "extra malformed reserved declaration" \
    'expectations must declare one literal corpus_name' \
    run_check

write_sample_corpus
sed '$s/^}$/} trailing garbage/' \
    "$corpora/sample/expectations.kofun" >"$output"
mv "$output" "$corpora/sample/expectations.kofun"
expect_failure \
    "noncanonical closing brace" \
    'expectations must declare one literal observations_sha256 digest' \
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
write_sample_corpus
printf '%s\n' \
    '# expect: ok' \
    'fn main() {' \
    '    print("ignored by the fixture adapter")' \
    '}' \
    >"$corpora/sample/skip.kofun"
write_sample_expectations
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
mkdir -p "$work/rogue/sample"
cp "$corpora/sample/expectations.kofun" "$work/rogue/sample/"
cp "$corpora/sample/case.kofun" "$work/rogue/sample/"
expect_failure \
    "same-name unregistered corpus substitution" \
    'selected corpus is not the registered sample corpus' \
    run_rogue_runner

write_sample_corpus
mv "$corpora/sample/case.kofun" "$corpora/sample/replaced.kofun"
expect_failure \
    "same-count corpus name replacement" \
    'case_names does not match its .kofun files' \
    run_check

write_sample_corpus
printf '%s\n' \
    '# expect: changed' \
    'fn main() {' \
    '    print("ignored by the fixture adapter")' \
    '}' \
    >"$corpora/sample/case.kofun"
expect_failure \
    "stale observation digest" \
    'observation digest does not match its # expect-* headers' \
    run_check

write_sample_corpus
printf '%s\n' \
    'backend	corpus	state	evidence	reason' \
    'alpha	sample	supported	/	-' \
    >"$manifest"
expect_failure \
    "absolute evidence path" \
    'evidence path must be a normalized repository-relative file' \
    run_check

printf '%s\n' \
    'backend	corpus	state	evidence	reason' \
    'alpha	sample	supported	tests/conformance	-' \
    >"$manifest"
expect_failure \
    "directory evidence path" \
    'evidence path not found' \
    run_check

mkdir -p "$ROOT/build"
printf '%s\n' "not repository evidence" >"$untracked_evidence"
untracked_relative=${untracked_evidence#"$ROOT/"}
printf '%s\n' \
    'backend	corpus	state	evidence	reason' \
    "alpha	sample	supported	$untracked_relative	-" \
    >"$manifest"
expect_failure \
    "untracked evidence path" \
    'evidence path is not tracked' \
    run_check

write_sample_corpus
printf '%s\n' \
    'fn main() {' \
    '    print("no explicit observation")' \
    '}' \
    >"$corpora/sample/case.kofun"
expect_failure \
    "case without explicit expectation" \
    'case has no explicit # expect-* header' \
    sh "$DIGEST" "$corpora/sample"

printf '%s\n' \
    '# expect-exit: 124' \
    'fn main() {' \
    '    print("reserved status")' \
    '}' \
    >"$corpora/sample/case.kofun"
expect_failure \
    "reserved timeout exit status" \
    'expected exit 124 is reserved for the timeout harness' \
    sh "$DIGEST" "$corpora/sample"

printf '%b\n' \
    '# expect:\tignored' \
    'fn main() {' \
    '    print("mismatched header grammar")' \
    '}' \
    >"$corpora/sample/case.kofun"
expect_failure \
    "tab-form expectation header" \
    'case has no explicit # expect-* header' \
    sh "$DIGEST" "$corpora/sample"

printf '%s\n' \
    '# expect-exit: 200' \
    'fn main() {' \
    '    print("out-of-range status")' \
    '}' \
    >"$corpora/sample/case.kofun"
expect_failure \
    "out-of-range expected exit status" \
    'expected exit must be between 0 and 127' \
    sh "$DIGEST" "$corpora/sample"

write_sample_corpus
write_alpha_adapter
printf '%s\n' \
    'BACKEND_NAME=beta' \
    'backend_check_available() {' \
    '    printf "%s\\n" "fixture executor is unavailable"' \
    '    return 125' \
    '}' \
    'backend_compile() {' \
    '    output=$2' \
    '    printf "%s\\n" "#!/usr/bin/env sh" "printf '\''ok\\n'\''" >"$output"' \
    '    chmod +x "$output"' \
    '}' \
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
grep -F 'BUILD PASS [beta]' "$output.stdout" >/dev/null || {
    printf '%s\n' \
        "FAIL: unavailable executor bypassed target compilation" >&2
    exit 1
}

printf '%s\n' \
    'BACKEND_NAME=beta' \
    'backend_check_available() {' \
    '    printf "%s\\n" "fixture executor is unavailable"' \
    '    return 125' \
    '}' \
    'backend_compile() { return 1; }' \
    >"$backends/beta.sh"
expect_failure \
    "unavailable executor hid compile failure" \
    'compile failed' \
    run_runner

# Rejection cases. The observation is a refusal rather than a run, so each way
# a backend could appear to refuse without refusing has to fail on its own.
rm -f "$backends/beta.sh"
write_supported_manifest

write_reject_corpus() {
    rm -f "$corpora/sample/"*.kofun
    printf '%s\n' \
        '# expect-reject: the fixture backend must refuse this source' \
        'fn main() {' \
        '    print("refused")' \
        '}' \
        >"$corpora/sample/reject.kofun"
    write_sample_expectations
}

write_reject_corpus

# The adapter that compiles everything must not pass a case whose contract is
# that nothing compiles.
write_alpha_adapter
expect_failure \
    "backend compiled a refused construct" \
    'compiled a source the specification refuses' \
    run_runner

printf '%s\n' \
    'BACKEND_NAME=alpha' \
    'backend_compile() { return 1; }' \
    >"$backends/alpha.sh"
expect_failure \
    "silent refusal" \
    'refused the source without a diagnostic' \
    run_runner

printf '%s\n' \
    'BACKEND_NAME=alpha' \
    'backend_compile() {' \
    '    output=$2' \
    '    printf "%s\\n" "#!/usr/bin/env sh" >"$output"' \
    '    printf "%s\\n" "alpha refuses this source" >&2' \
    '    return 1' \
    '}' \
    >"$backends/alpha.sh"
expect_failure \
    "refusal that left an artifact" \
    'refused the source but left an artifact' \
    run_runner

# A refusal filed as a capability gap is still a refusal before execution, so
# status 125 passes here where it would fail an executable case.
printf '%s\n' \
    'BACKEND_NAME=alpha' \
    'backend_compile() {' \
    '    printf "%s\\n" "alpha does not implement this construct" >&2' \
    '    return 125' \
    '}' \
    >"$backends/alpha.sh"
run_runner >"$output.stdout" 2>"$output.stderr"
grep -F 'REJECT PASS [alpha]' "$output.stdout" >/dev/null || {
    printf '%s\n' \
        "FAIL: a refusal reported as status 125 was not accepted" >&2
    sed 's/^/  /' "$output.stdout" "$output.stderr" >&2
    exit 1
}
grep -F 'refused: 1/1 cases refused before execution by alpha' \
    "$output.stdout" >/dev/null || {
    printf '%s\n' "FAIL: refusals were not reported separately" >&2
    exit 1
}

# One file may not carry both contracts.
printf '%s\n' \
    '# expect-reject: the fixture backend must refuse this source' \
    '# expect: ok' \
    'fn main() {' \
    '    print("refused")' \
    '}' \
    >"$corpora/sample/reject.kofun"
expect_failure \
    "case declaring both a refusal and a run" \
    'rejection case must not declare execution observations' \
    run_check

write_alpha_adapter
write_sample_corpus
write_supported_manifest

sh "$CHECK" >/dev/null
printf '%s\n' \
    "PASS: conformance capability manifest rejects policy and coverage drift"
