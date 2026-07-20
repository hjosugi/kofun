#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KOFUN="$ROOT/bin/kofun"
WORK=${KOFUN_PACKAGE_TEST_WORK:-"$ROOT/build/package-manager-test"}
CC=${CC:-cc}

rm -rf "$WORK"
mkdir -p "$WORK/project" "$WORK/upstream" "$WORK/cache"
cp "$ROOT/tests/packages/consumer.kofun" "$WORK/project/main.kofun"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    -c "$ROOT/tests/packages/answer.c" -o "$WORK/upstream/answer.o"
ar rcsD "$WORK/upstream/libanswer.a" "$WORK/upstream/answer.o"
cp "$WORK/upstream/libanswer.a" "$WORK/original-libanswer.a"

printf '%s\n' \
    'format = 1' \
    '' \
    '[dependency.answer]' \
    'source = "file:../upstream/libanswer.a"' \
    'kind = "static-library"' \
    >"$WORK/project/kofun.packages.toml"

package_command() (
    cd "$WORK/project"
    KOFUN_PACKAGE_CACHE="$WORK/cache" "$KOFUN" package "$@"
)

package_command lock >"$WORK/lock.stdout"
grep -Fqx 'locked 1 package(s)' "$WORK/lock.stdout"
cp "$WORK/project/kofun.packages.lock" "$WORK/first.lock"
package_command lock >"$WORK/relock.stdout"
cmp "$WORK/first.lock" "$WORK/project/kofun.packages.lock"

expected_hash=$(sha256sum "$WORK/original-libanswer.a" | awk '{ print $1 }')
grep -Fqx "sha256 = \"$expected_hash\"" \
    "$WORK/project/kofun.packages.lock"
cache_entry=$WORK/cache/sha256/$expected_hash
test -f "$cache_entry"
test "$(sha256sum "$cache_entry" | awk '{ print $1 }')" = "$expected_hash"
test "$(package_command path answer --offline)" = "$cache_entry"

# Package use cannot silently opt an ordinary build into the host-C ABI.
set +e
(
    cd "$WORK/project"
    KOFUN_PACKAGE_CACHE="$WORK/cache" \
        "$KOFUN" build main.kofun --package answer \
        -o "$WORK/project/implicit-c-abi"
) >"$WORK/implicit.stdout" 2>"$WORK/implicit.stderr"
implicit_status=$?
set -e
test "$implicit_status" -ne 0
test ! -e "$WORK/project/implicit-c-abi"
grep -q -- '--package requires --backend c --c-abi' \
    "$WORK/implicit.stderr"

# The declared source disappears. The content-addressed cache alone must be
# sufficient for both an explicit fetch and the actual Kofun C ABI build.
rm -rf "$WORK/upstream"
package_command fetch --offline >"$WORK/offline-fetch.stdout"
grep -Fqx 'fetched 1 package(s)' "$WORK/offline-fetch.stdout"
(
    cd "$WORK/project"
    KOFUN_PACKAGE_CACHE="$WORK/cache" \
        "$KOFUN" build main.kofun \
        --backend c --c-abi --package answer --offline \
        -o "$WORK/project/app" >/dev/null
)
test "$("$WORK/project/app")" = 42

# Cache bytes are verified on every use rather than trusted because the path
# happens to contain the expected digest.
chmod u+w "$cache_entry"
printf '%s\n' corrupt >"$cache_entry"
set +e
package_command fetch --offline \
    >"$WORK/corrupt.stdout" 2>"$WORK/corrupt.stderr"
corrupt_status=$?
set -e
test "$corrupt_status" -ne 0
grep -q 'cached content hash mismatch' "$WORK/corrupt.stderr"

# A missing cache entry may be fetched online/file-locally, but the fetched
# artifact still has to match the exact lock hash.
rm -f "$cache_entry"
mkdir -p "$WORK/upstream"
sed 's/return 42/return 41/' \
    "$ROOT/tests/packages/answer.c" >"$WORK/upstream/answer.c"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    -c "$WORK/upstream/answer.c" -o "$WORK/upstream/answer.o"
ar rcsD "$WORK/upstream/libanswer.a" "$WORK/upstream/answer.o"
set +e
package_command fetch \
    >"$WORK/mismatch.stdout" 2>"$WORK/mismatch.stderr"
mismatch_status=$?
set -e
test "$mismatch_status" -ne 0
grep -q 'content hash mismatch for package answer' "$WORK/mismatch.stderr"
test ! -e "$cache_entry"

set +e
package_command fetch --offline \
    >"$WORK/miss.stdout" 2>"$WORK/miss.stderr"
miss_status=$?
set -e
test "$miss_status" -ne 0
grep -q 'offline cache miss for package answer' "$WORK/miss.stderr"

printf '%s\n' \
    "PASS: dependency declaration locked exact SHA-256 artifact bytes" \
    "PASS: Kofun linked and called the declared external package" \
    "PASS: offline fetch and build used only the content-addressed cache" \
    "PASS: corrupt cache and changed upstream content failed verification"
