#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SERVER="$ROOT/tooling/lsp/kofun-lsp"

fail() {
    printf '%s\n' "lsp visibility: $*" >&2
    exit 1
}

command -v node >/dev/null 2>&1 ||
    fail "node is required to drive the language server"
test -x "$SERVER" ||
    fail "the language server is not executable: $SERVER"

node --check "$ROOT/tooling/lsp/visibility.js" ||
    fail "tooling/lsp/visibility.js is not valid JavaScript"
node --check "$ROOT/tests/lsp/visibility_test.js" ||
    fail "tests/lsp/visibility_test.js is not valid JavaScript"

# The decision function on its own, before any transport is involved. A rule
# this small is worth checking directly: every server-level assertion below
# depends on it, and a failure here says "the rule is wrong" rather than "the
# editor answered oddly".
node -e '
const assert = require("assert");
const { accessible, declaredVisibility, rank } =
    require("'"$ROOT"'/tooling/lsp/visibility.js");

const here = { fileId: "a.kofun", packageId: "p" };
const sameFile = (visibility) =>
    ({ fileId: "a.kofun", packageId: "p", visibility });
const otherFile = (visibility) =>
    ({ fileId: "b.kofun", packageId: "p", visibility });
const otherPackage = (visibility) =>
    ({ fileId: "b.kofun", packageId: "q", visibility });

for (const visibility of ["private", "internal", "pub", undefined]) {
    assert.ok(accessible(sameFile(visibility), here),
        `same file must reach its own ${visibility} declaration`);
}
assert.ok(!accessible(otherFile("private"), here),
    "another file`s private declaration must not be reachable");
assert.ok(!accessible(otherFile(undefined), here),
    "an omitted modifier is private, so it must not cross a file boundary");
assert.ok(accessible(otherFile("internal"), here),
    "internal must reach another file in the same package");
assert.ok(!accessible(otherPackage("internal"), here),
    "internal must not cross a package boundary");
assert.ok(accessible(otherPackage("pub"), here),
    "pub must cross a package boundary");
assert.ok(!accessible({ fileId: "b.kofun", packageId: null, visibility: "pub" }, here),
    "an anonymous single-file package is non-importable, so even pub must not cross");
assert.ok(!accessible(otherFile("pub"), { fileId: "a.kofun", packageId: null }),
    "a caller with no package identity must reach nothing outside its own file");

// spec/modules/visibility.md: these are explicitly not aliases. Reading one as
// the modifier it resembles would widen an API on a spelling the language does
// not have.
for (const spelling of ["public", "protected", "pub(crate)", "pub(super)", "PUB"]) {
    assert.strictEqual(declaredVisibility(spelling), "private",
        `${spelling} is not an alias and must resolve to private`);
    assert.ok(!accessible(otherFile(spelling), here),
        `${spelling} must not grant access across a file boundary`);
}

// pub(to path). The table lists it; the same document records restricted
// visibility as follow-up work, so it must not be widened here.
assert.ok(!accessible(otherFile("restricted"), here),
    "restricted visibility is undecided, so it must fail closed outside its file");

assert.ok(rank("private") < rank("restricted"), "private ranks below restricted");
assert.ok(rank("restricted") < rank("internal"), "restricted ranks below internal");
assert.ok(rank("internal") < rank("pub"), "internal ranks below pub");

assert.ok(!accessible(null, here), "a missing declaration must not be reachable");
assert.ok(!accessible(otherFile("pub"), null), "a missing caller must reach nothing");

console.log("PASS visibility: the decision function matches spec/modules/visibility.md");
' || fail "the visibility decision function disagrees with spec/modules/visibility.md"

node "$ROOT/tests/lsp/visibility_test.js" "$SERVER" ||
    fail "the language server does not apply the visibility decision to completion or navigation"

printf '%s\n' \
    "PASS: LSP completion and navigation filter by caller context"
