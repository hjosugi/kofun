#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ROADMAP="$ROOT/spec/roadmap-31-34"
STAGE2="$ROOT/bootstrap/stage2"
CC=${CC:-cc}

case ${1-} in
    "")
        ;;
    --full)
        sh "$STAGE2/check.sh"
        sh "$ROOT/bootstrap/native/check.sh"
        ;;
    *)
        printf '%s\n' "usage: $0 [--full]" >&2
        exit 2
        ;;
esac

command -v "$CC" >/dev/null 2>&1 || {
    printf '%s\n' "roadmap 31-34: C11 compiler not found: $CC" >&2
    exit 1
}

temporary=${TMPDIR:-/tmp}/kofun-roadmap-31-34.$$
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$STAGE2/compiler.c" -o "$temporary/kofun-stage2"

"$temporary/kofun-stage2" \
    "$ROADMAP/current-core-probe.kofun" \
    "$temporary/current-core-probe.c" \
    "$temporary/current-core-probe.ir" \
    "$temporary/current-core-probe.tokens" >/dev/null

grep -q '^kofun-stage2-ir/v1$' "$temporary/current-core-probe.ir"
grep -q '^kofun-token-tape/v1$' "$temporary/current-core-probe.tokens"
grep -q 'kofun_floor_div' "$temporary/current-core-probe.c"
grep -q 'kofun_floor_mod' "$temporary/current-core-probe.c"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/current-core-probe.c" \
    -o "$temporary/current-core-probe"

set +e
"$temporary/current-core-probe" \
    >"$temporary/current-core-probe.stdout" \
    2>"$temporary/current-core-probe.stderr"
status=$?
set -e

test "$status" -eq 42
cmp \
    "$ROADMAP/current-core-probe.stdout" \
    "$temporary/current-core-probe.stdout"
test ! -s "$temporary/current-core-probe.stderr"

grep -q '"stage1_self_recompile": "open"' \
    "$ROOT/bootstrap/manifest.json"
grep -q '"stage1_stage2_artifact_equivalence": "open"' \
    "$ROOT/bootstrap/manifest.json"
sed -n '/"stage2": {/,/^[[:space:]]*}/p' \
    "$ROOT/bootstrap/manifest.json" |
    grep -q '"status": "open"'

if grep -q '"languageServer"' "$ROOT/editor/vscode/package.json"; then
    printf '%s\n' \
        "roadmap 31-34: package unexpectedly declares a language server" >&2
    exit 1
fi

if find "$ROADMAP" -type f \
    \( -name '*.py' -o -name '*.pyc' -o -name '*.pyo' \) |
    grep -q .
then
    printf '%s\n' "roadmap 31-34: Python artifact found" >&2
    exit 1
fi

printf '%s\n' \
    "PASS: current Stage 2 integer Core probe printed -3 and 2, then exited 42" \
    "PASS: Stage 2 self-recompile and artifact-equivalence gates remain open" \
    "PASS: VS Code package remains syntax-only; LSP acceptance remains open"
