#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${CC:-cc}
AR=${AR:-ar}

test "$#" -eq 2 || {
    printf '%s\n' "usage: framework/tui/build.sh SOURCE.kofun OUTPUT" >&2
    exit 2
}

SOURCE=$1
OUTPUT=$2
test -f "$SOURCE" || {
    printf '%s\n' "tui-build: source not found: $SOURCE" >&2
    exit 2
}
grep -Fqx 'import framework.tui' "$SOURCE" || {
    printf '%s\n' 'tui-build: source must declare `import framework.tui`' >&2
    exit 2
}

case $OUTPUT in
    /*) ;;
    *) OUTPUT="$PWD/$OUTPUT" ;;
esac
OUTPUT_DIR=$(dirname "$OUTPUT")
mkdir -p "$OUTPUT_DIR"
WORK=${KOFUN_TUI_BUILD_WORK:-"$OUTPUT_DIR/.tui-build"}
mkdir -p "$WORK"

GENERATED="$WORK/application.kofun"
{
    sed -n '1,$p' "$ROOT/framework/tui/api.kofun"
    sed '/^import framework\.tui$/d' "$SOURCE"
} >"$GENERATED"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$ROOT/framework/tui/include" \
    -c "$ROOT/framework/tui/src/kofun_tui.c" \
    -o "$WORK/kofun_tui.o"
"$AR" rcs "$WORK/libkofun_tui.a" "$WORK/kofun_tui.o"

"$ROOT/bin/kofun" build "$GENERATED" \
    --backend c --c-abi \
    --link-library "$WORK/libkofun_tui.a" \
    --emit-c "$WORK/application.c" \
    -o "$OUTPUT"
