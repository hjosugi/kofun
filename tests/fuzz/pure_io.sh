#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${CC:-cc}
WORK=${KOFUN_PURE_IO_FUZZ_WORK:-"$ROOT/build/pure-io-fuzz"}

command -v "$CC" >/dev/null 2>&1 || {
    printf '%s\n' 'pure/io fuzz: a C11 compiler is required' >&2
    exit 1
}
command -v node >/dev/null 2>&1 || {
    printf '%s\n' 'pure/io fuzz: Node.js is required' >&2
    exit 1
}
case $WORK in
    */pure-io-fuzz|*/pure-io-fuzz.*) ;;
    *) printf '%s\n' "pure/io fuzz: unsafe work directory $WORK" >&2; exit 1 ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$ROOT/tests/fuzz/pure_io_model.c" \
    -o "$WORK/model"
node "$ROOT/tests/fuzz/pure_io_oracle.mjs" "$WORK/model"
