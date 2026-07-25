#!/bin/sh

# Shared helpers for kofun.semantic-result/v1. This file is sourced by the
# semantic runner, adapters, and the family-specific fuzz wrappers.

KOFUN_SEMANTIC_CAPABILITY_PROTOCOL=kofun.semantic-capability/v1
KOFUN_SEMANTIC_RESULT_PROTOCOL=kofun.semantic-result/v1
KOFUN_SEMANTIC_CASE_PROTOCOL=kofun.semantic-case/v1
KOFUN_SEMANTIC_FAMILY_PROTOCOL=kofun.semantic-family/v1

semantic_token_is_valid() {
    case ${1-} in
        ''|*[!A-Za-z0-9._-]*) return 1 ;;
        *) return 0 ;;
    esac
}

semantic_role_is_valid() {
    case ${1-} in
        oracle|backend) return 0 ;;
        *) return 1 ;;
    esac
}

semantic_support_is_valid() {
    case ${1-} in
        supported|unsupported) return 0 ;;
        *) return 1 ;;
    esac
}

semantic_status_is_valid() {
    case ${1-} in
        ''|*[!0-9]*|????*) return 1 ;;
    esac
    test "$1" -le 255
}

semantic_capability_write() {
    implementation=$1
    role=$2
    family=$3
    support=$4
    reason=$5

    semantic_token_is_valid "$implementation" &&
        semantic_role_is_valid "$role" &&
        semantic_token_is_valid "$family" &&
        semantic_support_is_valid "$support" ||
        return 2
    case $support:$reason in
        supported:-) ;;
        unsupported:-|'unsupported:') return 2 ;;
        unsupported:*)
            semantic_token_is_valid "$reason" || return 2
            ;;
        *) return 2 ;;
    esac

    printf '%s\t%s\n' \
        protocol "$KOFUN_SEMANTIC_CAPABILITY_PROTOCOL" \
        implementation "$implementation" \
        role "$role" \
        family "$family" \
        support "$support" \
        reason "$reason"
}

semantic_capability_validate() {
    capability=$1
    expected_implementation=$2
    expected_role=$3
    expected_family=$4
    expected_support=$5
    expected_reason=$6
    expected=$(mktemp "${TMPDIR:-/tmp}/kofun-semantic-capability.XXXXXX")
    semantic_capability_write \
        "$expected_implementation" "$expected_role" "$expected_family" \
        "$expected_support" "$expected_reason" >"$expected" || {
        rm -f "$expected"
        return 2
    }
    if ! cmp -s "$expected" "$capability"; then
        rm -f "$expected"
        return 1
    fi
    rm -f "$expected"
}

semantic_result_write() {
    result_dir=$1
    implementation=$2
    role=$3
    family=$4
    support=$5
    status=$6
    stdout_file=$7
    stderr_file=$8
    reason=$9

    semantic_token_is_valid "$implementation" &&
        semantic_role_is_valid "$role" &&
        semantic_token_is_valid "$family" &&
        semantic_support_is_valid "$support" &&
        test -f "$stdout_file" &&
        test -f "$stderr_file" ||
        return 2

    case $support in
        supported)
            semantic_status_is_valid "$status" || return 2
            test "$reason" = - || return 2
            ;;
        unsupported)
            test "$status" = - || return 2
            semantic_token_is_valid "$reason" &&
                test "$reason" != - ||
                return 2
            test ! -s "$stdout_file" && test ! -s "$stderr_file" ||
                return 2
            ;;
    esac

    mkdir -p "$result_dir"
    cp "$stdout_file" "$result_dir/stdout.bin"
    cp "$stderr_file" "$result_dir/stderr.bin"
    {
        printf '%s\t%s\n' \
            protocol "$KOFUN_SEMANTIC_RESULT_PROTOCOL" \
            implementation "$implementation" \
            role "$role" \
            family "$family" \
            support "$support" \
            exit "$status" \
            reason "$reason"
    } >"$result_dir/result.tsv"
}

semantic_result_field() {
    result=$1
    key=$2
    sed -n "s/^${key}	//p" "$result/result.tsv"
}

semantic_result_validate() {
    result=$1
    expected_implementation=$2
    expected_role=$3
    expected_family=$4
    expected_support=$5

    test -d "$result" &&
        test -f "$result/result.tsv" &&
        test -f "$result/stdout.bin" &&
        test -f "$result/stderr.bin" ||
        return 1
    awk -F '	' '
        NR == 1 && $1 == "protocol" && NF == 2 { next }
        NR == 2 && $1 == "implementation" && NF == 2 { next }
        NR == 3 && $1 == "role" && NF == 2 { next }
        NR == 4 && $1 == "family" && NF == 2 { next }
        NR == 5 && $1 == "support" && NF == 2 { next }
        NR == 6 && $1 == "exit" && NF == 2 { next }
        NR == 7 && $1 == "reason" && NF == 2 { next }
        { exit 1 }
        END { if (NR != 7) exit 1 }
    ' "$result/result.tsv" || return 1

    protocol=$(semantic_result_field "$result" protocol)
    implementation=$(semantic_result_field "$result" implementation)
    role=$(semantic_result_field "$result" role)
    family=$(semantic_result_field "$result" family)
    support=$(semantic_result_field "$result" support)
    status=$(semantic_result_field "$result" exit)
    reason=$(semantic_result_field "$result" reason)

    test "$protocol" = "$KOFUN_SEMANTIC_RESULT_PROTOCOL" &&
        test "$implementation" = "$expected_implementation" &&
        test "$role" = "$expected_role" &&
        test "$family" = "$expected_family" &&
        test "$support" = "$expected_support" &&
        semantic_token_is_valid "$implementation" &&
        semantic_role_is_valid "$role" &&
        semantic_token_is_valid "$family" &&
        semantic_support_is_valid "$support" ||
        return 1

    case $support in
        supported)
            semantic_status_is_valid "$status" &&
                test "$reason" = - ||
                return 1
            ;;
        unsupported)
            test "$status" = - &&
                semantic_token_is_valid "$reason" &&
                test "$reason" != - &&
                test ! -s "$result/stdout.bin" &&
                test ! -s "$result/stderr.bin" ||
                return 1
            ;;
    esac
}

semantic_results_compare() {
    oracle=$1
    backend=$2
    oracle_status=$(semantic_result_field "$oracle" exit)
    backend_status=$(semantic_result_field "$backend" exit)

    if test "$oracle_status" != "$backend_status"; then
        KOFUN_SEMANTIC_MISMATCH="exit status mismatch: oracle $oracle_status, backend $backend_status"
        return 1
    fi
    if ! cmp -s "$oracle/stdout.bin" "$backend/stdout.bin"; then
        KOFUN_SEMANTIC_MISMATCH='stdout mismatch'
        return 1
    fi
    if ! cmp -s "$oracle/stderr.bin" "$backend/stderr.bin"; then
        KOFUN_SEMANTIC_MISMATCH='stderr mismatch'
        return 1
    fi
    KOFUN_SEMANTIC_MISMATCH=
}

semantic_wrap_observation_pair() {
    family=$1
    case_work=$2
    oracle_id=$3
    backend_id=$4
    expected_stdout=$5
    expected_stderr=$6
    expected_status=$7
    actual_stdout=$8
    actual_stderr=$9
    shift 9
    actual_status=$1

    protocol_work="$case_work/semantic-protocol"
    oracle_result="$protocol_work/$oracle_id"
    backend_result="$protocol_work/$backend_id"
    semantic_result_write \
        "$oracle_result" "$oracle_id" oracle "$family" supported \
        "$expected_status" "$expected_stdout" "$expected_stderr" - &&
        semantic_result_write \
            "$backend_result" "$backend_id" backend "$family" supported \
            "$actual_status" "$actual_stdout" "$actual_stderr" - &&
        semantic_result_validate \
            "$oracle_result" "$oracle_id" oracle "$family" supported &&
        semantic_result_validate \
            "$backend_result" "$backend_id" backend "$family" supported &&
        semantic_results_compare "$oracle_result" "$backend_result"
}

semantic_case_field() {
    case_file=$1
    key=$2
    sed -n "s/^${key}	//p" "$case_file"
}

semantic_record_tool() {
    destination=$1
    label=$2
    path=$3
    semantic_token_is_valid "$label" && test -f "$path" || return 2
    checksum=$(cksum "$path" | awk '{ print $1 }')
    size=$(cksum "$path" | awk '{ print $2 }')
    printf 'cksum\t%s\t%s\t%s\t%s\n' \
        "$label" "$checksum" "$size" "$path" >>"$destination"
}
