#!/usr/bin/env sh
set -eu

# Change the shared cache after package resolution but before the linker reads
# its arguments. A consistent build links only its already-verified snapshot.
if test ! -e "$KOFUN_TEST_CHANGE_MARKER"; then
    chmod u+w "$KOFUN_TEST_CACHE_ENTRY"
    "$KOFUN_REAL_CP" \
        "$KOFUN_TEST_REPLACEMENT" "$KOFUN_TEST_CACHE_ENTRY"
    : >"$KOFUN_TEST_CHANGE_MARKER"
fi

for argument in "$@"; do
    case $argument in
        */kofun-package-build.*/*.a)
            printf '%s\n' "$argument" >"$KOFUN_TEST_SNAPSHOT_LOG"
            ;;
    esac
done

exec "$KOFUN_REAL_CC" "$@"
