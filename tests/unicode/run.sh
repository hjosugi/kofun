#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_UNICODE_WORK:-"$ROOT/build/unicode"}
CC=${CC:-cc}

mkdir -p "$WORK"

(
    cd "$ROOT/unicode"
    sha256sum -c SHA256SUMS
)

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/tests/unicode/unicode_test.c" \
    "$ROOT/unicode/kofun_unicode.c" \
    -o "$WORK/unicode-test"

"$WORK/unicode-test"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/stage2/compiler.c" \
    -o "$WORK/kofun-stage2"

"$WORK/kofun-stage2" \
    "$ROOT/tests/unicode/stage2_identifiers.kofun" \
    "$WORK/stage2-identifiers.c" \
    "$WORK/stage2-identifiers.ir" \
    "$WORK/stage2-identifiers.tokens" >/dev/null

grep -F 'function|main|0|' "$WORK/stage2-identifiers.ir" >/dev/null
grep -F 'identifier|' "$WORK/stage2-identifiers.tokens" >/dev/null
grep -F 'kofun_fn_k_u005408_u008A08' \
    "$WORK/stage2-identifiers.c" >/dev/null

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/stage2-identifiers.c" \
    -o "$WORK/stage2-identifiers"
test "$("$WORK/stage2-identifiers")" = "42"

set +e
KOFUN_DIAGNOSTIC_LOCALE=ja_JP \
    "$WORK/kofun-stage2" \
    "$ROOT/tests/unicode/non_nfc_identifier.kofun" \
    "$WORK/non-nfc.c" \
    "$WORK/non-nfc.ir" \
    "$WORK/non-nfc.tokens" \
    >"$WORK/non-nfc.stdout" 2>"$WORK/non-nfc.stderr"
non_nfc_status=$?

"$WORK/kofun-stage2" \
    "$ROOT/tests/unicode/confusable_identifier.kofun" \
    "$WORK/confusable.c" \
    "$WORK/confusable.ir" \
    "$WORK/confusable.tokens" \
    >"$WORK/confusable.stdout" 2>"$WORK/confusable.stderr"
confusable_status=$?
set -e

test "$non_nfc_status" -eq 1
test "$confusable_status" -eq 1
test ! -s "$WORK/non-nfc.stderr"
test ! -s "$WORK/confusable.stderr"
grep -F 'error[EUNICODE005]' "$WORK/non-nfc.stdout" >/dev/null
grep -F 'NFCではありません' "$WORK/non-nfc.stdout" >/dev/null
grep -F 'error[EUNICODE006]' "$WORK/confusable.stdout" >/dev/null
grep -F 'confusable with `paypal`' "$WORK/confusable.stdout" >/dev/null

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/native/core_compiler.c" \
    -o "$WORK/kofun-native-core"
"$WORK/kofun-native-core" \
    "$ROOT/tests/unicode/native_identifiers.kofun" \
    x86_64-linux \
    "$WORK/native-identifiers"
chmod +x "$WORK/native-identifiers"
test "$("$WORK/native-identifiers")" = "42"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/stage1/compiler.c" \
    -lm \
    -o "$WORK/kofun-stage1"
"$WORK/kofun-stage1" \
    "$ROOT/tests/unicode/native_identifiers.kofun" \
    "$WORK/stage1-identifiers.c" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/stage1-identifiers.c" \
    -o "$WORK/stage1-identifiers"
test "$("$WORK/stage1-identifiers")" = "42"

printf '%s\n' \
    "PASS: Stage 2 lowered Japanese and Hangul identifiers through ASCII-safe C names" \
    "PASS: Stage 1 and native Core resolved Japanese and Hangul bindings" \
    "PASS: Stage 2 rejected non-NFC and confusable identifiers with localized diagnostics"
