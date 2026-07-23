#ifndef KOFUN_UNICODE_H
#define KOFUN_UNICODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KOFUN_UNICODE_VERSION "17.0.0"
#define KOFUN_UNICODE_ERROR_TEXT 256

typedef enum {
    KOFUN_UNICODE_OK = 0,
    KOFUN_UNICODE_INVALID_UTF8,
    KOFUN_UNICODE_NUL,
    KOFUN_UNICODE_BIDI_CONTROL,
    KOFUN_UNICODE_INVALID_IDENTIFIER,
    KOFUN_UNICODE_NON_NFC_IDENTIFIER,
    KOFUN_UNICODE_CONFUSABLE_IDENTIFIER,
    KOFUN_UNICODE_OUT_OF_MEMORY,
} KofunUnicodeStatus;

typedef struct {
    KofunUnicodeStatus status;
    size_t byte_offset;
    size_t line;
    size_t column;
    uint32_t codepoint;
    char identifier[KOFUN_UNICODE_ERROR_TEXT];
    char replacement[KOFUN_UNICODE_ERROR_TEXT];
    char conflicting_identifier[KOFUN_UNICODE_ERROR_TEXT];
} KofunUnicodeError;

const char *kofun_unicode_version(void);

bool kofun_unicode_decode(
    const uint8_t *bytes,
    size_t length,
    size_t offset,
    uint32_t *codepoint,
    size_t *width
);

bool kofun_unicode_is_xid_start(uint32_t codepoint);
bool kofun_unicode_is_xid_continue(uint32_t codepoint);

bool kofun_unicode_validate_source(
    const uint8_t *source,
    size_t length,
    KofunUnicodeError *error
);

void kofun_unicode_source_position(
    const uint8_t *source,
    size_t length,
    size_t byte_offset,
    size_t *line,
    size_t *column
);

const char *kofun_unicode_error_code(KofunUnicodeStatus status);

void kofun_unicode_format_error(
    const KofunUnicodeError *error,
    const char *locale,
    char *output,
    size_t output_size
);

bool kofun_unicode_grapheme_next(
    const uint8_t *text,
    size_t length,
    size_t offset,
    size_t *next
);

bool kofun_unicode_grapheme_count(
    const uint8_t *text,
    size_t length,
    size_t *count
);

bool kofun_unicode_grapheme_at(
    const uint8_t *text,
    size_t length,
    size_t index,
    size_t *offset,
    size_t *grapheme_length
);

bool kofun_unicode_codepoint_count(
    const uint8_t *text,
    size_t length,
    size_t *count
);

bool kofun_unicode_codepoint_at(
    const uint8_t *text,
    size_t length,
    size_t index,
    size_t *offset,
    size_t *codepoint_length
);

size_t kofun_unicode_byte_length(const uint8_t *text, size_t length);

bool kofun_unicode_display_width(
    const uint8_t *text,
    size_t length,
    bool east_asian_ambiguous_wide,
    size_t *columns
);

#endif
