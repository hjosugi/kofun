#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/tests/fuzz/semantic_protocol.sh"

TIMEOUT_SECONDS=${KOFUN_SEMANTIC_TIMEOUT:-10}
case $TIMEOUT_SECONDS in
    ''|*[!0-9]*|0)
        printf '%s\n' 'semantic runner: timeout must be a positive integer' >&2
        exit 2
        ;;
esac
command -v timeout >/dev/null 2>&1 || {
    printf '%s\n' 'semantic runner: timeout command is required' >&2
    exit 2
}

test "$#" -eq 6 || {
    printf '%s\n' \
        'usage: semantic_runner.sh MANIFEST SOURCE CASE-META CASE-WORK FAILURE-ROOT LABEL' >&2
    exit 2
}

manifest=$1
source=$2
case_meta=$3
case_work=$4
failure_root=$5
label=$6
test -f "$manifest" && test -f "$source" && test -f "$case_meta" || {
    printf '%s\n' 'semantic runner: missing manifest, source, or case metadata' >&2
    exit 2
}
semantic_token_is_valid "$label" || {
    printf '%s\n' "semantic runner: invalid label: $label" >&2
    exit 2
}

mkdir -p "$case_work" "$failure_root"
runs="$case_work/runs"
results="$case_work/results"
mkdir -p "$runs" "$results"
: >"$case_work/tools.tsv"

family=$(sed -n '2s/^family	//p' "$manifest")
generator=$(sed -n '3s/^generator	//p' "$manifest")
scope=$(sed -n '4s/^scope	//p' "$manifest")
case_index=$(semantic_case_field "$case_meta" case-index)
seed=$(semantic_case_field "$case_meta" seed)
artifact="$failure_root/$label"
resolved_manifest="$case_work/family.manifest"

record_failure() {
    failure_reason=$1
    rm -rf "$artifact"
    mkdir -p "$artifact"
    cp "$source" "$artifact/source.kofun"
    cp "$case_meta" "$artifact/case.tsv"
    if test -f "$resolved_manifest"; then
        cp "$resolved_manifest" "$artifact/family.manifest"
    else
        cp "$manifest" "$artifact/family.manifest"
    fi
    printf '%s\n' "$failure_reason" >"$artifact/failure.txt"
    test ! -d "$runs" || cp -R "$runs" "$artifact/raw-observations"
    test ! -d "$results" || cp -R "$results" "$artifact/results"
    test ! -f "$case_work/tools.tsv" ||
        cp "$case_work/tools.tsv" "$artifact/tools.tsv"
    reproducer="sh '$ROOT/tests/fuzz/semantic_differential.sh' --replay '$artifact'"
    printf '%s\n' "$reproducer" >"$artifact/reproducer.txt"
    {
        printf '%s\n' '#!/bin/sh'
        printf 'exec sh %s --replay %s\n' \
            "'$ROOT/tests/fuzz/semantic_differential.sh'" "'$artifact'"
    } >"$artifact/reproduce.sh"
    chmod +x "$artifact/reproduce.sh"
    printf '%s\n' \
        "semantic runner: $failure_reason" \
        "semantic runner: replay artifact: $artifact" \
        "semantic runner: reproduce: $reproducer" >&2
    exit 1
}

awk -F '	' '
    NR == 1 {
        if (NF != 2 || $1 != "protocol" ||
            $2 != "kofun.semantic-family/v1") exit 1
        next
    }
    NR == 2 {
        if (NF != 2 || $1 != "family") exit 1
        next
    }
    NR == 3 {
        if (NF != 2 || $1 != "generator") exit 1
        next
    }
    NR == 4 {
        if (NF != 2 || $1 != "scope") exit 1
        next
    }
    NR > 4 {
        if (NF != 6 || $1 != "participant") exit 1
        next
    }
    END { if (NR < 5) exit 1 }
' "$manifest" ||
    record_failure 'malformed family declaration'

semantic_token_is_valid "$family" &&
    semantic_token_is_valid "$generator" &&
    semantic_token_is_valid "$scope" ||
    record_failure 'malformed family identity, generator, or scope'

case_protocol=$(sed -n '1s/^protocol	//p' "$case_meta")
case_family=$(semantic_case_field "$case_meta" family)
case_generator=$(semantic_case_field "$case_meta" generator)
case $case_index:$seed in
    *[!0-9:]*|:*|*:) record_failure 'malformed case index or seed' ;;
esac
test "$case_protocol" = "$KOFUN_SEMANTIC_CASE_PROTOCOL" &&
    test "$case_family" = "$family" &&
    test "$case_generator" = "$generator" ||
    record_failure 'case metadata disagrees with family declaration'

manifest_dir=$(CDPATH= cd -- "$(dirname -- "$manifest")" && pwd)
participants="$case_work/participants.tsv"
sed -n '5,$p' "$manifest" >"$participants"
oracle_count=$(awk -F '	' '$2 == "oracle" && $4 == "supported" { n++ } END { print n + 0 }' "$participants")
backend_count=$(awk -F '	' '$2 == "backend" && $4 == "supported" { n++ } END { print n + 0 }' "$participants")
test "$oracle_count" -eq 1 ||
    record_failure "missing or ambiguous accepted oracle: found $oracle_count"
test "$backend_count" -gt 0 ||
    record_failure 'missing supported backend implementation'

{
    sed -n '1,4p' "$manifest"
} >"$resolved_manifest"
: >"$case_work/identities"

tab=$(printf '\t')
while IFS="$tab" read -r participant role identity support reason adapter_path
do
    test "$participant" = participant &&
        semantic_role_is_valid "$role" &&
        semantic_token_is_valid "$identity" &&
        semantic_support_is_valid "$support" ||
        record_failure 'malformed participant declaration'
    if grep -Fqx "$identity" "$case_work/identities"; then
        record_failure "duplicate participant identity: $identity"
    fi
    printf '%s\n' "$identity" >>"$case_work/identities"
    case $support:$reason in
        supported:-) ;;
        unsupported:-|'unsupported:')
            record_failure "unsupported participant has no reason: $identity"
            ;;
        unsupported:*)
            semantic_token_is_valid "$reason" ||
                record_failure "invalid unsupported reason: $identity"
            ;;
        *) record_failure "invalid support declaration: $identity" ;;
    esac

    case $adapter_path in
        /*) adapter=$adapter_path ;;
        *) adapter=$manifest_dir/$adapter_path ;;
    esac
    test -f "$adapter" && test -x "$adapter" ||
        record_failure "missing backend or oracle adapter: $identity"
    printf 'participant\t%s\t%s\t%s\t%s\t%s\n' \
        "$role" "$identity" "$support" "$reason" "$adapter" \
        >>"$resolved_manifest"
    semantic_record_tool "$case_work/tools.tsv" "$identity" "$adapter" ||
        record_failure "cannot hash adapter: $identity"

    invocation="$runs/$identity"
    mkdir -p "$invocation"
    set +e
    KOFUN_SEMANTIC_PROTOCOL_LIB="$ROOT/tests/fuzz/semantic_protocol.sh" \
        timeout "$TIMEOUT_SECONDS" "$adapter" capability "$family" \
        >"$invocation/capability.stdout" \
        2>"$invocation/capability.stderr"
    capability_status=$?
    set -e
    printf '%s\n' "$capability_status" >"$invocation/capability.status"
    case $capability_status in
        0) ;;
        124|137)
            record_failure "capability adapter timed out: $identity"
            ;;
        128|129|130|131|132|133|134|135|136|138|139|140|141|142|143)
            record_failure "capability adapter crashed: $identity (status $capability_status)"
            ;;
        *)
            record_failure "capability adapter failed: $identity (status $capability_status)"
            ;;
    esac
    test ! -s "$invocation/capability.stderr" ||
        record_failure "capability adapter wrote transport stderr: $identity"
    semantic_capability_validate \
        "$invocation/capability.stdout" "$identity" "$role" "$family" \
        "$support" "$reason" ||
        record_failure "capability mismatch or malformed capability: $identity"

    test "$support" = supported || continue
    result="$results/$identity"
    adapter_work="$invocation/work"
    mkdir -p "$adapter_work"
    set +e
    KOFUN_SEMANTIC_PROTOCOL_LIB="$ROOT/tests/fuzz/semantic_protocol.sh" \
        timeout "$TIMEOUT_SECONDS" "$adapter" run "$family" \
        "$source" "$case_meta" "$result" "$adapter_work" \
        >"$invocation/run.stdout" 2>"$invocation/run.stderr"
    run_status=$?
    set -e
    printf '%s\n' "$run_status" >"$invocation/run.status"
    case $run_status in
        0) ;;
        124|137) record_failure "adapter timed out: $identity" ;;
        128|129|130|131|132|133|134|135|136|138|139|140|141|142|143)
            record_failure "adapter crashed: $identity (status $run_status)"
            ;;
        *) record_failure "adapter failed: $identity (status $run_status)" ;;
    esac
    test ! -s "$invocation/run.stdout" &&
        test ! -s "$invocation/run.stderr" ||
        record_failure "adapter wrote transport output: $identity"
    semantic_result_validate \
        "$result" "$identity" "$role" "$family" supported ||
        record_failure "missing or malformed adapter result: $identity"
done <"$participants"

oracle_identity=$(awk -F '	' '$2 == "oracle" && $4 == "supported" { print $3 }' "$participants")
oracle_result="$results/$oracle_identity"
while IFS="$tab" read -r participant role identity support reason adapter_path
do
    test "$role" = backend && test "$support" = supported || continue
    if ! semantic_results_compare "$oracle_result" "$results/$identity"; then
        record_failure "$identity: $KOFUN_SEMANTIC_MISMATCH"
    fi
done <"$participants"

exit 0
