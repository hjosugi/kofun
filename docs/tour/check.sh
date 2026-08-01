#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_TOUR_CHECK_WORK:-"$ROOT/build/tour-check"}
ASSERT_CONTEXT='tour'
. "$ROOT/tests/assertions/assert.sh"

for tool in node grep cmp
do
    command -v "$tool" >/dev/null 2>&1 || {
        printf '%s\n' "browser tour gate requires $tool" >&2
        exit 1
    }
done

rm -rf "$WORK"
mkdir -p "$WORK"

for module in app compiler content intelligence kofun-kun runtime share check
do
    node --check "$ROOT/docs/tour/$module.mjs"
done

"$ROOT/bin/kofun" build \
    "$ROOT/examples/wasm-browser/app.kofun" \
    --target wasm32 -o "$WORK/native.wasm" >/dev/null
node "$ROOT/docs/tour/check.mjs" \
    "$ROOT/examples/wasm-browser/app.kofun" "$WORK/native.wasm" \
    >"$WORK/check.stdout"

assert_grep "check.stdout" \
    -Fq \
    'PASS: browser compiler matched the native wasm32 seed byte for byte' \
    "$WORK/check.stdout"
assert_grep "check.stdout" \
    -Fq \
    'PASS: every editable tour step ran with deterministic observations' \
    "$WORK/check.stdout"
assert_grep "check.stdout" \
    -Fq \
    "PASS: hover and completion answered from the compiler's own parse, in scope" \
    "$WORK/check.stdout"
assert_grep "check.stdout" \
    -Fq \
    'PASS: canonical hjosugi-hub Kofun-kun guides every tour step' \
    "$WORK/check.stdout"
assert_grep "docs/tour/index.html" \
    -Fq 'data-editor' "$ROOT/docs/tour/index.html"
# The editor intelligence is only reachable if its surfaces are in the page and
# the mirror the pointer is resolved against is hidden from assistive tech.
assert_grep "docs/tour/index.html" \
    -Fq 'data-editor-mirror' "$ROOT/docs/tour/index.html"
assert_grep "docs/tour/index.html" \
    -Fq 'data-completion' "$ROOT/docs/tour/index.html"
assert_grep "docs/tour/index.html" \
    -Fq 'data-hover-card' "$ROOT/docs/tour/index.html"
assert_grep "docs/tour/index.html" \
    -Fq 'role="listbox"' "$ROOT/docs/tour/index.html"
assert_grep "docs/tour/index.html" \
    -Fq 'aria-autocomplete="list"' "$ROOT/docs/tour/index.html"
assert_grep "docs/tour/index.html" \
    -Fq 'data-direction' "$ROOT/docs/tour/index.html"
assert_grep "docs/tour/index.html" \
    -Fq 'aria-live="polite"' "$ROOT/docs/tour/index.html"
assert_grep "docs/tour/styles.css" \
    -Fq 'inset-inline-start' "$ROOT/docs/tour/styles.css"
assert_grep "docs/tour/styles.css" \
    -Fq '[dir="rtl"]' "$ROOT/docs/tour/styles.css"
assert_not_grep "docs/tour/styles.css" \
    -Eq '(margin|padding|border)-(left|right):' "$ROOT/docs/tour/styles.css"

for language in python typescript go rust
do
    assert_file_nonempty "docs/tour/guides/$language.md" \
        "$ROOT/docs/tour/guides/$language.md"
    assert_grep "docs/tour/guides/$language.md" \
        -Fq 'Where Kofun is worse today' "$ROOT/docs/tour/guides/$language.md"
done

printf '%s\n' \
    'PASS: static browser tour is editable, runnable, and URL-shareable' \
    'PASS: browser compiler matches the current deterministic wasm32 Core' \
    'PASS: logical CSS and direction control cover RTL layout' \
    'PASS: editor hover and completion come from the audited browser compiler'
