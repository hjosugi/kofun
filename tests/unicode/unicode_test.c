#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../unicode/kofun_unicode.h"

static void expect_valid_source(const char *source) {
    KofunUnicodeError error;
    assert(kofun_unicode_validate_source(
        (const uint8_t *)source,
        strlen(source),
        &error
    ));
    assert(error.status == KOFUN_UNICODE_OK);
}

static KofunUnicodeError expect_invalid_source(
    const char *source,
    KofunUnicodeStatus status
) {
    KofunUnicodeError error;
    assert(!kofun_unicode_validate_source(
        (const uint8_t *)source,
        strlen(source),
        &error
    ));
    assert(error.status == status);
    return error;
}

static void expect_graphemes(const char *text, size_t expected) {
    size_t actual = SIZE_MAX;
    assert(kofun_unicode_grapheme_count(
        (const uint8_t *)text,
        strlen(text),
        &actual
    ));
    assert(actual == expected);
}

static void expect_codepoints(const char *text, size_t expected) {
    size_t actual = SIZE_MAX;
    assert(kofun_unicode_codepoint_count(
        (const uint8_t *)text,
        strlen(text),
        &actual
    ));
    assert(actual == expected);
}

static void expect_width(
    const char *text,
    bool ambiguous_wide,
    size_t expected
) {
    size_t actual = SIZE_MAX;
    assert(kofun_unicode_display_width(
        (const uint8_t *)text,
        strlen(text),
        ambiguous_wide,
        &actual
    ));
    assert(actual == expected);
}

int main(void) {
    assert(strcmp(kofun_unicode_version(), KOFUN_UNICODE_VERSION) == 0);

    expect_valid_source(
        "fn 面積(幅: Int, 高さ: Int) -> Int {\n"
        "    let 결과 = 幅 * 高さ\n"
        "    return 결과\n"
        "}\n"
        "fn العربية(قيمة: Int) -> Int { return قيمة }\n"
        "fn עברית(ערך: Int) -> Int { return ערך }\n"
        "fn हिन्दी(मान: Int) -> Int { return मान }\n"
        "fn ภาษาไทย(ค่า: Int) -> Int { return ค่า }\n"
    );

    assert(kofun_unicode_is_xid_start(UINT32_C(0x65e5)));
    assert(kofun_unicode_is_xid_start(UINT32_C(0xd55c)));
    assert(kofun_unicode_is_xid_continue(UINT32_C(0x3099)));
    assert(!kofun_unicode_is_xid_start(UINT32_C(0x1f680)));

    KofunUnicodeError non_nfc = expect_invalid_source(
        "fn main() { let cafe\xCC\x81 = 1 }\n",
        KOFUN_UNICODE_NON_NFC_IDENTIFIER
    );
    assert(strcmp(non_nfc.replacement, "café") == 0);

    non_nfc = expect_invalid_source(
        "fn は\xE3\x82\x99() {}\n",
        KOFUN_UNICODE_NON_NFC_IDENTIFIER
    );
    assert(strcmp(non_nfc.replacement, "ば") == 0);

    non_nfc = expect_invalid_source(
        "fn \xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB() {}\n",
        KOFUN_UNICODE_NON_NFC_IDENTIFIER
    );
    assert(strcmp(non_nfc.replacement, "한") == 0);

    KofunUnicodeError bidi = expect_invalid_source(
        "fn main() { let safe = \"x\xE2\x80\xAEy\" }\n",
        KOFUN_UNICODE_BIDI_CONTROL
    );
    assert(bidi.codepoint == UINT32_C(0x202e));

    KofunUnicodeError confusable = expect_invalid_source(
        "fn main() {\n"
        "    let paypal = 1\n"
        "    let p\xD0\xB0ypal = 2\n"
        "}\n",
        KOFUN_UNICODE_CONFUSABLE_IDENTIFIER
    );
    assert(strcmp(confusable.identifier, "pаypal") == 0);
    assert(strcmp(confusable.conflicting_identifier, "paypal") == 0);

    (void)expect_invalid_source(
        "fn 緇() {}\n"
        "fn \xF0\xB1\xB9\xBC() {}\n",
        KOFUN_UNICODE_CONFUSABLE_IDENTIFIER
    );

    (void)expect_invalid_source(
        "fn 🚀() {}\n",
        KOFUN_UNICODE_INVALID_IDENTIFIER
    );

    expect_graphemes("古墳", 2);
    expect_graphemes("は\xE3\x82\x99", 1);
    expect_graphemes("\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB"
                     "\xE1\x84\x80\xE1\x85\xB3\xE1\x86\xAF", 2);
    expect_graphemes("한글", 2);
    expect_graphemes("نَمَ", 2);
    expect_graphemes("שָׁ", 1);
    expect_graphemes("नमस्ते", 3);
    expect_graphemes("กำ", 1);
    expect_graphemes("👩🏽", 1);
    expect_graphemes("👨‍👩‍👧‍👦", 1);
    expect_graphemes("🇯🇵", 1);

    expect_codepoints("नमस्ते", 6);
    expect_codepoints("👨‍👩‍👧‍👦", 7);
    assert(kofun_unicode_byte_length(
        (const uint8_t *)"古",
        strlen("古")
    ) == 3);

    size_t offset = SIZE_MAX;
    size_t grapheme_length = SIZE_MAX;
    assert(kofun_unicode_grapheme_at(
        (const uint8_t *)"A👨‍👩‍👧‍👦한",
        strlen("A👨‍👩‍👧‍👦한"),
        1,
        &offset,
        &grapheme_length
    ));
    assert(offset == 1);
    assert(grapheme_length == strlen("👨‍👩‍👧‍👦"));

    expect_width("古墳", false, 4);
    expect_width("e\xCC\x81", false, 1);
    expect_width("한글", false, 4);
    expect_width("👩🏽", false, 2);
    expect_width("👨‍👩‍👧‍👦", false, 2);
    expect_width("🇯🇵", false, 2);
    expect_width("·", false, 1);
    expect_width("·", true, 2);

    KofunUnicodeError invalid = expect_invalid_source(
        "fn 日本語() {\n    let 値 = 🚀\n}\n",
        KOFUN_UNICODE_INVALID_IDENTIFIER
    );
    assert(invalid.line == 2);
    assert(invalid.column == 13);

    char message[1024];
    kofun_unicode_format_error(
        &non_nfc,
        "ja_JP",
        message,
        sizeof(message)
    );
    assert(strstr(message, "NFCではありません") != NULL);
    kofun_unicode_format_error(
        &non_nfc,
        "fr_FR",
        message,
        sizeof(message)
    );
    assert(strstr(message, "is not NFC") != NULL);

    puts("PASS: Unicode 17 identifiers, security, graphemes, and width");
    return 0;
}
