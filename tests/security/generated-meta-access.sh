#!/usr/bin/env sh

set -eu

# Stage 0 has no accepted macro or meta producer. Keep that absence a
# fail-closed security boundary: syntax that looks like it could manufacture
# declarations must be refused before it can publish an artifact or echo the
# private-looking authority material carried by the probe.

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_GENERATED_META_SECURITY_WORK:-"$ROOT/build/${KOFUN_GATE_WORK_NAMESPACE:+$KOFUN_GATE_WORK_NAMESPACE/}generated-meta-access"}

ASSERT_CONTEXT='generated/meta access'
. "$ROOT/tests/assertions/assert.sh"
. "$ROOT/bootstrap/stage2/build.sh"

fail() {
    printf '%s\n' "FAIL: generated/meta access: $*" >&2
    exit 1
}

# Like assert_not_grep, but its failure is deliberately bounded: neither the
# matched compiler bytes nor the private sentinel used to detect them is
# copied into the gate diagnostic.
assert_no_disclosure() {
    assert_no_disclosure_label=$1
    shift
    if grep "$@" >/dev/null 2>&1; then
        assert_no_disclosure_status=0
    else
        assert_no_disclosure_status=$?
    fi
    case $assert_no_disclosure_status in
        0) fail "$assert_no_disclosure_label" ;;
        1) ;;
        *) fail "$assert_no_disclosure_label could not be checked" ;;
    esac
}

case $WORK in
    */generated-meta-access|*/generated-meta-access.*) ;;
    *) fail "work directory must end in generated-meta-access[.suffix]: $WORK" ;;
esac

rm -rf "$WORK"
mkdir -p "$WORK/source-a" "$WORK/source-b" "$WORK/runs"
kofun_stage2_build "$ROOT" "$WORK/kofun-stage2"

write_probe() {
    write_probe_name=$1
    write_probe_path=$2
    case $write_probe_name in
        meta-fn)
            printf '%s\n' \
                'meta fn hiddenGeneratedMeta() -> Int {' \
                '    let package_id = "1111111111111111111111111111111111111111111111111111111111111111"' \
                '    let symbol_id = "2222222222222222222222222222222222222222222222222222222222222222"' \
                '    let private_source = "/private/dependency/hidden-source.kofun"' \
                '    return 1' \
                '}' \
                '' \
                'fn main() -> Int {' \
                '    return 0' \
                '}' >"$write_probe_path"
            ;;
        token-macro)
            printf '%s\n' \
                'macro hiddenGeneratedMacro(value) {' \
                '    private-source-marker-584' \
                '    /private/dependency/hidden-source.kofun' \
                '    1111111111111111111111111111111111111111111111111111111111111111' \
                '    2222222222222222222222222222222222222222222222222222222222222222' \
                '}' \
                '' \
                'fn main() -> Int {' \
                '    return 0' \
                '}' >"$write_probe_path"
            ;;
        derive)
            printf '%s\n' \
                '@derive(hiddenGeneratedDerive)' \
                'type Visible = {' \
                '    private_source_marker_584: Int,' \
                '}' \
                '' \
                '# /private/dependency/hidden-source.kofun' \
                '# PackageId 1111111111111111111111111111111111111111111111111111111111111111' \
                '# SymbolId 2222222222222222222222222222222222222222222222222222222222222222' \
                'fn main() -> Int {' \
                '    return 0' \
                '}' >"$write_probe_path"
            ;;
        *) fail "unknown probe: $write_probe_name" ;;
    esac
}

run_refusal() {
    run_refusal_label=$1
    run_refusal_source=$2
    run_refusal_code=$3
    run_refusal_expected=$4
    run_refusal_dir="$WORK/runs/$run_refusal_label"
    mkdir -p "$run_refusal_dir"

    set +e
    (
        cd "$run_refusal_dir"
        "$WORK/kofun-stage2" "$run_refusal_source" \
            output.c output.ir output.tokens \
            >compiler.stdout 2>compiler.stderr
    )
    run_refusal_status=$?
    set -e

    if test "$run_refusal_status" -eq 0; then
        fail "$run_refusal_label is now accepted; replace this negative row with a producer-specific positive visibility child"
    fi
    assert_num "$run_refusal_label exit status" \
        "$run_refusal_status" -eq 1
    assert_file_nonempty "$run_refusal_label diagnostic stdout" \
        "$run_refusal_dir/compiler.stdout"
    assert_file_empty "$run_refusal_label internal stderr" \
        "$run_refusal_dir/compiler.stderr"
    assert_grep "$run_refusal_label diagnostic identity" \
        -Fq -- "error[$run_refusal_code]:" \
        "$run_refusal_dir/compiler.stdout"

    # Check disclosure before any assertion that could reproduce the actual
    # compiler line in its own failure message.
    assert_no_disclosure "$run_refusal_label disclosed a declaration name" \
        -E -- 'hiddenGenerated|private_source_marker_584' \
        "$run_refusal_dir/compiler.stdout"
    assert_no_disclosure "$run_refusal_label disclosed source content" \
        -Fq -- 'private-source-marker-584' \
        "$run_refusal_dir/compiler.stdout"
    assert_no_disclosure "$run_refusal_label disclosed a private path" \
        -Fq -- '/private/dependency/hidden-source.kofun' \
        "$run_refusal_dir/compiler.stdout"
    assert_no_disclosure "$run_refusal_label disclosed a PackageId" \
        -Fq -- '1111111111111111111111111111111111111111111111111111111111111111' \
        "$run_refusal_dir/compiler.stdout"
    assert_no_disclosure "$run_refusal_label disclosed a SymbolId" \
        -Fq -- '2222222222222222222222222222222222222222222222222222222222222222' \
        "$run_refusal_dir/compiler.stdout"
    assert_no_disclosure "$run_refusal_label disclosed the checkout path" \
        -Fq -- "$ROOT" "$run_refusal_dir/compiler.stdout"
    assert_no_disclosure "$run_refusal_label disclosed the work path" \
        -Fq -- "$WORK" "$run_refusal_dir/compiler.stdout"
    if test "$(cat "$run_refusal_dir/compiler.stdout")" != \
        "$run_refusal_expected"; then
        fail "$run_refusal_label exact diagnostic changed"
    fi

    assert_absent "$run_refusal_label partial C artifact" \
        "$run_refusal_dir/output.c"
    assert_absent "$run_refusal_label partial IR artifact" \
        "$run_refusal_dir/output.ir"
    assert_absent "$run_refusal_label partial token artifact" \
        "$run_refusal_dir/output.tokens"

    # No publication may hide below a directory, FIFO, or dangling symlink.
    # Finding an unexpected top-level entry is sufficient: any nested
    # publication first requires that unexpected directory entry. Avoid
    # GNU-only find options so the owning gate remains portable.
    run_refusal_unexpected=0
    for run_refusal_entry in \
        "$run_refusal_dir"/* \
        "$run_refusal_dir"/.[!.]* \
        "$run_refusal_dir"/..?*
    do
        if test ! -e "$run_refusal_entry" && test ! -L "$run_refusal_entry"; then
            continue
        fi
        case $run_refusal_entry in
            "$run_refusal_dir/compiler.stdout"|"$run_refusal_dir/compiler.stderr") ;;
            *) run_refusal_unexpected=1 ;;
        esac
    done
    assert_num "$run_refusal_label unexpected publication artifact" \
        "$run_refusal_unexpected" -eq 0
}

compare_observations() {
    compare_label=$1
    compare_left=$2
    compare_right=$3
    cmp "$WORK/runs/$compare_left/compiler.stdout" \
        "$WORK/runs/$compare_right/compiler.stdout" ||
        fail "$compare_label stdout differs"
    cmp "$WORK/runs/$compare_left/compiler.stderr" \
        "$WORK/runs/$compare_right/compiler.stderr" ||
        fail "$compare_label stderr differs"
}

for probe in meta-fn token-macro derive; do
    write_probe "$probe" "$WORK/source-a/$probe.kofun"
    cp "$WORK/source-a/$probe.kofun" "$WORK/source-b/$probe.kofun"
done

META_FN_DIAGNOSTIC='error[E2S33]: unknown visibility modifier `meta`; expected `pub`, `internal`, or `private` at bytes 0..4'
TOP_LEVEL_DIAGNOSTIC='error[E2S02]: expected top-level `fn`, `type`, or `let` at byte 0'

run_refusal meta-fn.first "$WORK/source-a/meta-fn.kofun" \
    E2S33 "$META_FN_DIAGNOSTIC"
run_refusal meta-fn.second "$WORK/source-a/meta-fn.kofun" \
    E2S33 "$META_FN_DIAGNOSTIC"
run_refusal meta-fn.remapped "$WORK/source-b/meta-fn.kofun" \
    E2S33 "$META_FN_DIAGNOSTIC"

run_refusal token-macro.first "$WORK/source-a/token-macro.kofun" \
    E2S02 "$TOP_LEVEL_DIAGNOSTIC"
run_refusal token-macro.second "$WORK/source-a/token-macro.kofun" \
    E2S02 "$TOP_LEVEL_DIAGNOSTIC"
run_refusal token-macro.remapped "$WORK/source-b/token-macro.kofun" \
    E2S02 "$TOP_LEVEL_DIAGNOSTIC"

run_refusal derive.first "$WORK/source-a/derive.kofun" \
    E2S02 "$TOP_LEVEL_DIAGNOSTIC"
run_refusal derive.second "$WORK/source-a/derive.kofun" \
    E2S02 "$TOP_LEVEL_DIAGNOSTIC"
run_refusal derive.remapped "$WORK/source-b/derive.kofun" \
    E2S02 "$TOP_LEVEL_DIAGNOSTIC"

for probe in meta-fn token-macro derive; do
    compare_observations "$probe repeated" \
        "$probe.first" "$probe.second"
    compare_observations "$probe path-remapped" \
        "$probe.first" "$probe.remapped"
done

# The compiler receives sources outside each output directory. Refuse any
# source-adjacent publication too, and keep the controlled root itself closed
# over only the compiler, source roots, and run roots this gate created.
for source_root in "$WORK/source-a" "$WORK/source-b"; do
    source_root_unexpected=0
    for source_entry in \
        "$source_root"/* \
        "$source_root"/.[!.]* \
        "$source_root"/..?*
    do
        if test ! -e "$source_entry" && test ! -L "$source_entry"; then
            continue
        fi
        case $source_entry in
            "$source_root/meta-fn.kofun"|\
            "$source_root/token-macro.kofun"|\
            "$source_root/derive.kofun") ;;
            *) source_root_unexpected=1 ;;
        esac
    done
    assert_num "source-adjacent publication artifact" \
        "$source_root_unexpected" -eq 0
done

runs_root_unexpected=0
for runs_entry in "$WORK/runs"/* "$WORK/runs"/.[!.]* "$WORK/runs"/..?*; do
    if test ! -e "$runs_entry" && test ! -L "$runs_entry"; then
        continue
    fi
    case $runs_entry in
        "$WORK/runs/meta-fn.first"|\
        "$WORK/runs/meta-fn.second"|\
        "$WORK/runs/meta-fn.remapped"|\
        "$WORK/runs/token-macro.first"|\
        "$WORK/runs/token-macro.second"|\
        "$WORK/runs/token-macro.remapped"|\
        "$WORK/runs/derive.first"|\
        "$WORK/runs/derive.second"|\
        "$WORK/runs/derive.remapped") ;;
        *) runs_root_unexpected=1 ;;
    esac
done
assert_num "runs-root publication artifact" "$runs_root_unexpected" -eq 0

work_root_unexpected=0
for work_entry in "$WORK"/* "$WORK"/.[!.]* "$WORK"/..?*; do
    if test ! -e "$work_entry" && test ! -L "$work_entry"; then
        continue
    fi
    case $work_entry in
        "$WORK/kofun-stage2"|"$WORK/source-a"|"$WORK/source-b"|"$WORK/runs") ;;
        *) work_root_unexpected=1 ;;
    esac
done
assert_num "work-root publication artifact" "$work_root_unexpected" -eq 0

printf '%s\n' \
    'PASS: unsupported meta fn, token macro, and derive producers fail closed' \
    'PASS: generated/meta refusals disclose no private authority material' \
    'PASS: generated/meta refusals leave no publication artifact' \
    'PASS: repeated and path-remapped refusal observations are byte-identical'
