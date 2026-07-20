#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${CC:-cc}
AR=${AR:-ar}

test "$#" -eq 2 || {
    printf '%s\n' \
        "usage: framework/http/build.sh SOURCE.kofun OUTPUT" >&2
    exit 2
}

SOURCE=$1
OUTPUT=$2

test -f "$SOURCE" || {
    printf '%s\n' "http-build: source not found: $SOURCE" >&2
    exit 2
}
grep -Fqx 'import framework.http' "$SOURCE" || {
    printf '%s\n' \
        "http-build: source must declare \`import framework.http\`" >&2
    exit 2
}

case $OUTPUT in
    /*) ;;
    *) OUTPUT="$PWD/$OUTPUT" ;;
esac

OUTPUT_DIR=$(dirname "$OUTPUT")
mkdir -p "$OUTPUT_DIR"
WORK=${KOFUN_HTTP_BUILD_WORK:-"$OUTPUT_DIR/.http-build"}
mkdir -p "$WORK"

GENERATED="$WORK/application.kofun"
{
    sed -n '1,$p' "$ROOT/framework/http/api.kofun"
    sed '/^import framework\.http$/d' "$SOURCE"
} >"$GENERATED"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$ROOT/framework/http/include" \
    -c "$ROOT/framework/http/src/kofun_http.c" \
    -o "$WORK/kofun_http.o"
"$AR" rcs "$WORK/libkofun_http.a" "$WORK/kofun_http.o"

"$ROOT/bin/kofun" build "$GENERATED" \
    --backend c \
    --c-abi \
    --link-library "$WORK/libkofun_http.a" \
    --emit-c "$WORK/application.c" \
    -o "$OUTPUT"
