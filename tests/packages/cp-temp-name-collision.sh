#!/usr/bin/env sh
set -eu

# Recreate collisions with the old predictable PID-based temporary names
# before delegating to the real copy program.
parent=$PPID
grandparent=$(ps -o ppid= -p "$parent" | tr -d ' ')

for candidate in "$parent" "$grandparent"; do
    test -n "$candidate" || continue
    ln -sf \
        "$KOFUN_TEST_CACHE_SENTINEL" \
        "$KOFUN_TEST_CACHE/sha256/.tmp.$candidate"
    ln -sf \
        "$KOFUN_TEST_LOCK_SENTINEL" \
        "$KOFUN_TEST_LOCK_DIRECTORY/.kofun-packages-lock.$candidate"
done

exec "$KOFUN_REAL_CP" "$@"
