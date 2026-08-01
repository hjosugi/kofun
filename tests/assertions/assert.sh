#!/bin/sh

# Assertion helpers for gate scripts. This file is sourced, runs nothing on its
# own, and is deliberately not executable.
#
# Every gate in this repository runs under `set -eu`, where the cheapest
# assertion is a bare `test`:
#
#     test "$actual" = "$expected"
#
# When that fails, `set -e` exits and *nothing is printed*. The gate returns 1
# with an empty stderr, and the reader has to bisect the script to find which
# comparison fired. #794 records one instance of that costing real time — the
# native gate's pinned-digest check, failing with an empty stderr. #814 counts
# 460 of them.
#
# These helpers exist so the failing check names itself. Each takes a label
# first, prints one line naming the label, the expectation, and the
# observation, and exits 1.
#
#     ASSERT_CONTEXT=native
#     . "$ROOT/tests/assertions/assert.sh"
#
#     assert_num "ELF image size for $stem" "$size" -eq 4099
#     FAIL: native: ELF image size for core_return_42: expected -eq 4099, got 4103
#
# ASSERT_CONTEXT is optional and names the gate; without it the prefix is just
# `FAIL: `.
#
# There is deliberately no `fail()` here. Fifty-two scripts already define their
# own with a gate-specific prefix, and a sourced definition would silently win
# or lose depending on source order. Use `assert_fail` for the checks these
# helpers do not cover.

assert_fail() {
    printf 'FAIL: %s%s\n' "${ASSERT_CONTEXT:+$ASSERT_CONTEXT: }" "$1" >&2
    exit 1
}

# assert_eq LABEL ACTUAL EXPECTED — string equality.
assert_eq() {
    if test "$2" != "$3"; then
        assert_fail "$1: expected '$3', got '$2'"
    fi
}

# assert_ne LABEL ACTUAL FORBIDDEN — string inequality.
assert_ne() {
    if test "$2" = "$3"; then
        assert_fail "$1: expected anything but '$3', got '$2'"
    fi
}

# assert_num LABEL ACTUAL OP EXPECTED — numeric comparison, OP as `test` spells
# it: -eq -ne -gt -ge -lt -le.
assert_num() {
    if ! test "$2" "$3" "$4"; then
        assert_fail "$1: expected $3 $4, got $2"
    fi
}

# assert_nonempty LABEL VALUE — the value is a non-empty string.
assert_nonempty() {
    if test -z "$2"; then
        assert_fail "$1: expected a non-empty value, got nothing"
    fi
}

# assert_empty LABEL VALUE — the value is the empty string.
assert_empty() {
    if test -n "$2"; then
        assert_fail "$1: expected nothing, got '$2'"
    fi
}

# assert_file_nonempty LABEL PATH — the file exists and has content.
assert_file_nonempty() {
    if ! test -s "$2"; then
        if test -e "$2"; then
            assert_fail "$1: $2 is empty"
        fi
        assert_fail "$1: $2 does not exist"
    fi
}

# assert_file_empty LABEL PATH — the file has no content. A missing file
# satisfies this, exactly as `test ! -s` does.
assert_file_empty() {
    if test -s "$2"; then
        assert_fail "$1: expected $2 to be empty, it holds $(wc -c <"$2" | tr -d ' ') bytes"
    fi
}

# assert_present LABEL PATH — the path exists, of any type.
assert_present() {
    if ! test -e "$2"; then
        assert_fail "$1: expected $2 to exist"
    fi
}

# assert_absent LABEL PATH — the path does not exist.
assert_absent() {
    if test -e "$2"; then
        assert_fail "$1: expected $2 not to exist"
    fi
}

# assert_dir LABEL PATH — the path exists and is a directory.
assert_dir() {
    if ! test -d "$2"; then
        assert_fail "$1: expected $2 to be a directory"
    fi
}
