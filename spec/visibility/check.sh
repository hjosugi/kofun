#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SPEC="$ROOT/spec/modules/visibility.md"
FOUNDATIONS="$ROOT/spec/syntax/FOUNDATIONS_AND_CONTROL.md"
SYNTAX_DOC="$ROOT/docs/SYNTAX.md"

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

require_text() {
    file=$1
    needle=$2
    grep -Fq "$needle" "$file" ||
        fail "$file does not contain required text: $needle"
}

visibility_rank() {
    case $1 in
        private) printf '%s\n' 0 ;;
        restricted) printf '%s\n' 1 ;;
        internal) printf '%s\n' 2 ;;
        pub) printf '%s\n' 3 ;;
        *) fail "unknown visibility in reference gate: $1" ;;
    esac
}

can_expose() {
    requested=$1
    component=$2
    test "$(visibility_rank "$requested")" -le \
        "$(visibility_rank "$component")"
}

is_ancestor_module() {
    target=$1
    declaring=$2
    test -n "$target" || return 1
    case $declaring in
        "$target"|"$target".*) return 0 ;;
        *) return 1 ;;
    esac
}

require_text "$SPEC" '| no modifier | private |'
require_text "$SPEC" '| `internal` | every module in the current package |'
require_text "$SPEC" '`public`, `protected`, `pub(crate)`, `pub(super)`, and `pub(in path)` are not'
require_text "$SPEC" 'contextual declaration modifiers'
require_text "$SPEC" 'deliberately excluded from the first executable'
require_text "$FOUNDATIONS" '`pub`, `internal`, and `private` are contextual declaration modifiers'
require_text "$SYNTAX_DOC" 'Public API is intentional'

test "$(visibility_rank private)" -lt "$(visibility_rank restricted)"
test "$(visibility_rank restricted)" -lt "$(visibility_rank internal)"
test "$(visibility_rank internal)" -lt "$(visibility_rank pub)"

can_expose private private || fail 'private declaration cannot use private component'
can_expose internal pub || fail 'internal declaration cannot use public component'
can_expose pub pub || fail 'public declaration cannot use public component'
if can_expose pub internal; then
    fail 'public declaration exposed an internal component'
fi
if can_expose internal private; then
    fail 'internal declaration exposed a private component'
fi

is_ancestor_module user.service user.service.validation ||
    fail 'ancestor module rejected'
is_ancestor_module user.service.validation user.service.validation ||
    fail 'declaring module rejected as its own restricted target'
if is_ancestor_module user.other user.service.validation; then
    fail 'sibling module accepted as restricted target'
fi
if is_ancestor_module user.service.validation.deep user.service.validation; then
    fail 'descendant module accepted as restricted target'
fi

printf '%s\n' \
    'PASS: visibility vocabulary and contextual modifier contract are linked' \
    'PASS: private, restricted, internal, and public ordering is stable' \
    'PASS: public/internal API leakage is rejected by the reference gate' \
    'PASS: restricted visibility accepts only the declaring module or ancestors'
