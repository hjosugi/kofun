#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
MANIFEST=${1-"$ROOT/tests/conformance/capabilities.tsv"}
BACKENDS=${2-"$ROOT/tests/conformance/backends"}
CORPORA=${3-"$ROOT/tests/conformance"}

test -f "$MANIFEST" || {
    printf '%s\n' \
        "conformance capabilities: manifest not found: $MANIFEST" >&2
    exit 2
}
test -d "$BACKENDS" || {
    printf '%s\n' \
        "conformance capabilities: backend directory not found: $BACKENDS" >&2
    exit 2
}
test -d "$CORPORA" || {
    printf '%s\n' \
        "conformance capabilities: corpus directory not found: $CORPORA" >&2
    exit 2
}

work=$(mktemp -d "${TMPDIR:-/tmp}/kofun-capabilities.XXXXXX")
trap 'rm -rf "$work"' 0 1 2 15

found_backends=0
: >"$work/backends.unsorted"
for adapter in "$BACKENDS"/*.sh; do
    test -f "$adapter" || continue
    found_backends=$((found_backends + 1))
    name=$(basename "${adapter%.sh}")
    case $name in
        *[!A-Za-z0-9_-]*|'')
            printf '%s\n' \
                "conformance capabilities: invalid backend adapter name: $name" >&2
            exit 2
            ;;
    esac
    declared_name=$(
        KOFUN_ROOT=$ROOT
        export KOFUN_ROOT
        unset BACKEND_NAME
        . "$adapter"
        printf '%s\n' "${BACKEND_NAME-}"
    )
    if test -z "$declared_name"; then
        printf '%s\n' \
            "conformance capabilities: adapter has no BACKEND_NAME: $adapter" >&2
        exit 2
    fi
    if test "$declared_name" != "$name"; then
        printf '%s\n' \
            "conformance capabilities: adapter identity mismatch: $name declares $declared_name" >&2
        exit 2
    fi
    if grep -Eq '^[[:space:]]*BACKEND_CORPORA=' "$adapter"; then
        printf '%s\n' \
            "conformance capabilities: adapter carries independent BACKEND_CORPORA authority: $adapter" >&2
        exit 2
    fi
    printf '%s\n' "$name" >>"$work/backends.unsorted"
done
LC_ALL=C sort "$work/backends.unsorted" >"$work/backends"

test "$found_backends" -gt 0 || {
    printf '%s\n' \
        "conformance capabilities: no backend adapters found in $BACKENDS" >&2
    exit 2
}

found_corpora=0
: >"$work/corpora.unsorted"
for expectations in "$CORPORA"/*/expectations.kofun; do
    test -f "$expectations" || continue
    found_corpora=$((found_corpora + 1))
    corpus=$(basename "$(dirname "$expectations")")
    case $corpus in
        *[!A-Za-z0-9_-]*|'')
            printf '%s\n' \
                "conformance capabilities: invalid corpus name: $corpus" >&2
            exit 2
            ;;
    esac
    if grep -Eq \
        '^[[:space:]]*fn[[:space:]]+backend_names[[:space:]]*\(' \
        "$expectations"
    then
        printf '%s\n' \
            "conformance capabilities: expectations must not define backend capability authority: $expectations" >&2
        exit 2
    fi
    expected_count=$(
        awk '
            /^[[:space:]]*fn[[:space:]]+expected_cases[[:space:]]*\(/ {
                inside = 1
                declarations += 1
                next
            }
            inside && /^[[:space:]]*return[[:space:]]+[0-9]+[[:space:]]*$/ {
                value = $0
                sub(/^[[:space:]]*return[[:space:]]+/, "", value)
                sub(/[[:space:]]*$/, "", value)
                print value
                inside = 0
            }
            inside && /^[[:space:]]*}/ {
                inside = 0
            }
            END {
                if (declarations != 1) exit 1
            }
        ' "$expectations"
    ) || {
        printf '%s\n' \
            "conformance capabilities: expectations must declare one literal expected_cases count: $expectations" >&2
        exit 2
    }
    case $expected_count in
        *[!0-9]*|'')
            printf '%s\n' \
                "conformance capabilities: invalid expected_cases count in $expectations" >&2
            exit 2
            ;;
    esac
    actual_count=0
    corpus_dir=$(dirname "$expectations")
    for source in "$corpus_dir"/*.kofun; do
        test -f "$source" || continue
        test "$source" != "$expectations" || continue
        actual_count=$((actual_count + 1))
    done
    if test "$actual_count" -ne "$expected_count"; then
        printf '%s\n' \
            "conformance capabilities: corpus $corpus expected $expected_count cases but found $actual_count" >&2
        exit 2
    fi
    printf '%s\n' "$corpus" >>"$work/corpora.unsorted"
done
LC_ALL=C sort "$work/corpora.unsorted" >"$work/corpora"

test "$found_corpora" -gt 0 || {
    printf '%s\n' \
        "conformance capabilities: no registered corpora found in $CORPORA" >&2
    exit 2
}

if ! awk -F '	' \
    -v backend_file="$work/backends" \
    -v corpus_file="$work/corpora" \
    -v evidence_file="$work/evidence" '
BEGIN {
    while ((getline value < backend_file) > 0) {
        backends[value] = 1
        backend_order[++backend_count] = value
    }
    close(backend_file)
    while ((getline value < corpus_file) > 0) {
        corpora[value] = 1
        corpus_order[++corpus_count] = value
    }
    close(corpus_file)
}
NR == 1 {
    if ($0 != "backend\tcorpus\tstate\tevidence\treason") {
        print "conformance capabilities: invalid manifest header" > "/dev/stderr"
        invalid = 1
    }
    next
}
$0 == "" || $0 ~ /^#/ {
    next
}
{
    if (NF != 5) {
        print "conformance capabilities: malformed row " NR \
            " (expected five tab-separated fields)" > "/dev/stderr"
        invalid = 1
        next
    }
    backend = $1
    corpus = $2
    state = $3
    evidence = $4
    reason = $5
    key = backend SUBSEP corpus

    if (!(backend in backends)) {
        print "conformance capabilities: unknown backend `" backend \
            "` on row " NR > "/dev/stderr"
        invalid = 1
    }
    if (!(corpus in corpora)) {
        print "conformance capabilities: unknown corpus `" corpus \
            "` on row " NR > "/dev/stderr"
        invalid = 1
    }
    if (key in states) {
        if (states[key] != state) {
            print "conformance capabilities: contradictory entries for `" \
                backend "` / `" corpus "`" > "/dev/stderr"
        } else {
            print "conformance capabilities: duplicate entry for `" \
                backend "` / `" corpus "`" > "/dev/stderr"
        }
        invalid = 1
        next
    }
    states[key] = state

    if (state == "supported") {
        if (evidence == "" || evidence == "-") {
            print "conformance capabilities: supported entry lacks evidence for `" \
                backend "` / `" corpus "`" > "/dev/stderr"
            invalid = 1
        } else {
            print evidence > evidence_file
        }
        if (reason != "-") {
            print "conformance capabilities: supported entry must use `-` reason for `" \
                backend "` / `" corpus "`" > "/dev/stderr"
            invalid = 1
        }
    } else if (state == "unsupported") {
        if (evidence != "-") {
            print "conformance capabilities: unsupported entry must use `-` evidence for `" \
                backend "` / `" corpus "`" > "/dev/stderr"
            invalid = 1
        }
        if (reason == "" || reason == "-") {
            print "conformance capabilities: unsupported entry lacks a reason for `" \
                backend "` / `" corpus "`" > "/dev/stderr"
            invalid = 1
        }
    } else {
        print "conformance capabilities: unknown state `" state \
            "` for `" backend "` / `" corpus "`" > "/dev/stderr"
        invalid = 1
    }
}
END {
    if (NR == 0) {
        print "conformance capabilities: empty manifest" > "/dev/stderr"
        invalid = 1
    }
    for (backend_index = 1;
         backend_index <= backend_count;
         backend_index++) {
        backend = backend_order[backend_index]
        for (corpus_index = 1;
             corpus_index <= corpus_count;
             corpus_index++) {
            corpus = corpus_order[corpus_index]
            key = backend SUBSEP corpus
            if (!(key in states)) {
                print "conformance capabilities: missing entry for `" \
                    backend "` / `" corpus "`" > "/dev/stderr"
                invalid = 1
            }
        }
    }
    close(evidence_file)
    exit invalid
}
' "$MANIFEST"
then
    exit 2
fi

if test -f "$work/evidence"; then
    while IFS= read -r evidence; do
        case $evidence in
            /*)
                evidence_path=$evidence
                ;;
            *)
                evidence_path=$ROOT/$evidence
                ;;
        esac
        if test ! -f "$evidence_path" && test ! -d "$evidence_path"; then
            printf '%s\n' \
                "conformance capabilities: evidence path not found: $evidence" >&2
            exit 2
        fi
    done <"$work/evidence"
fi

printf '%s\n' \
    "PASS: conformance capability manifest is complete and canonical"
