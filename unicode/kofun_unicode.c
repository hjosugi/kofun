#include "kofun_unicode.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/utf8proc/utf8proc.h"

/*
 * The bootstrap compilers are deliberately single-translation-unit seeds.
 * Including the pinned implementation keeps their build free of system
 * Unicode libraries and locale state.
 */
#include "../vendor/utf8proc/utf8proc.c"

typedef struct {
    uint32_t first;
    uint32_t last;
} KofunUnicodeRange;

typedef struct {
    uint32_t source;
    uint32_t values_offset;
    uint8_t values_length;
} KofunConfusableMapping;

#include "kofun_unicode_tables.inc"

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
} KofunByteBuffer;

typedef struct {
    uint8_t *spelling;
    size_t spelling_length;
    uint8_t *skeleton;
    size_t skeleton_length;
} KofunIdentifierRecord;

const char *kofun_unicode_version(void) {
    return utf8proc_unicode_version();
}

static bool kofun_range_contains(
    const KofunUnicodeRange *ranges,
    size_t count,
    uint32_t codepoint
) {
    size_t low = 0;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (codepoint < ranges[middle].first) {
            high = middle;
        } else if (codepoint > ranges[middle].last) {
            low = middle + 1;
        } else {
            return true;
        }
    }
    return false;
}

bool kofun_unicode_is_xid_start(uint32_t codepoint) {
    return kofun_range_contains(
        kofun_xid_start_ranges,
        sizeof(kofun_xid_start_ranges) / sizeof(kofun_xid_start_ranges[0]),
        codepoint
    );
}

bool kofun_unicode_is_xid_continue(uint32_t codepoint) {
    return kofun_range_contains(
        kofun_xid_continue_ranges,
        sizeof(kofun_xid_continue_ranges) /
            sizeof(kofun_xid_continue_ranges[0]),
        codepoint
    );
}

bool kofun_unicode_decode(
    const uint8_t *bytes,
    size_t length,
    size_t offset,
    uint32_t *codepoint,
    size_t *width
) {
    if (bytes == NULL || offset >= length) return false;
    utf8proc_int32_t decoded = 0;
    utf8proc_ssize_t consumed = utf8proc_iterate(
        bytes + offset,
        (utf8proc_ssize_t)(length - offset),
        &decoded
    );
    if (consumed <= 0) return false;
    if (codepoint != NULL) *codepoint = (uint32_t)decoded;
    if (width != NULL) *width = (size_t)consumed;
    return true;
}

static bool kofun_is_bidi_control(uint32_t codepoint) {
    return codepoint == UINT32_C(0x061c) ||
        codepoint == UINT32_C(0x200e) ||
        codepoint == UINT32_C(0x200f) ||
        (codepoint >= UINT32_C(0x202a) &&
         codepoint <= UINT32_C(0x202e)) ||
        (codepoint >= UINT32_C(0x2066) &&
         codepoint <= UINT32_C(0x206f));
}

static void kofun_copy_text(
    char output[KOFUN_UNICODE_ERROR_TEXT],
    const uint8_t *input,
    size_t length
) {
    size_t copied = length;
    if (copied >= KOFUN_UNICODE_ERROR_TEXT) {
        copied = KOFUN_UNICODE_ERROR_TEXT - 1;
    }
    if (copied > 0) memcpy(output, input, copied);
    output[copied] = '\0';
}

static void kofun_clear_error(KofunUnicodeError *error) {
    if (error == NULL) return;
    memset(error, 0, sizeof(*error));
    error->status = KOFUN_UNICODE_OK;
    error->line = 1;
    error->column = 1;
}

bool kofun_unicode_grapheme_next(
    const uint8_t *text,
    size_t length,
    size_t offset,
    size_t *next
) {
    uint32_t previous = 0;
    size_t previous_width = 0;
    if (!kofun_unicode_decode(
            text, length, offset, &previous, &previous_width)) {
        return false;
    }

    size_t cursor = offset + previous_width;
    utf8proc_int32_t state = 0;
    while (cursor < length) {
        uint32_t current = 0;
        size_t current_width = 0;
        if (!kofun_unicode_decode(
                text, length, cursor, &current, &current_width)) {
            return false;
        }
        if (utf8proc_grapheme_break_stateful(
                (utf8proc_int32_t)previous,
                (utf8proc_int32_t)current,
                &state)) {
            if (next != NULL) *next = cursor;
            return true;
        }
        previous = current;
        cursor += current_width;
    }
    if (next != NULL) *next = length;
    return true;
}

bool kofun_unicode_grapheme_count(
    const uint8_t *text,
    size_t length,
    size_t *count
) {
    if (text == NULL && length != 0) return false;
    size_t cursor = 0;
    size_t result = 0;
    while (cursor < length) {
        size_t next = 0;
        if (!kofun_unicode_grapheme_next(text, length, cursor, &next) ||
            next <= cursor) {
            return false;
        }
        cursor = next;
        ++result;
    }
    if (count != NULL) *count = result;
    return true;
}

bool kofun_unicode_grapheme_at(
    const uint8_t *text,
    size_t length,
    size_t index,
    size_t *offset,
    size_t *grapheme_length
) {
    size_t cursor = 0;
    size_t current = 0;
    while (cursor < length) {
        size_t next = 0;
        if (!kofun_unicode_grapheme_next(text, length, cursor, &next)) {
            return false;
        }
        if (current == index) {
            if (offset != NULL) *offset = cursor;
            if (grapheme_length != NULL) *grapheme_length = next - cursor;
            return true;
        }
        cursor = next;
        ++current;
    }
    return false;
}

bool kofun_unicode_codepoint_count(
    const uint8_t *text,
    size_t length,
    size_t *count
) {
    if (text == NULL && length != 0) return false;
    size_t cursor = 0;
    size_t result = 0;
    while (cursor < length) {
        size_t width = 0;
        if (!kofun_unicode_decode(text, length, cursor, NULL, &width)) {
            return false;
        }
        cursor += width;
        ++result;
    }
    if (count != NULL) *count = result;
    return true;
}

bool kofun_unicode_codepoint_at(
    const uint8_t *text,
    size_t length,
    size_t index,
    size_t *offset,
    size_t *codepoint_length
) {
    size_t cursor = 0;
    size_t current = 0;
    while (cursor < length) {
        size_t width = 0;
        if (!kofun_unicode_decode(text, length, cursor, NULL, &width)) {
            return false;
        }
        if (current == index) {
            if (offset != NULL) *offset = cursor;
            if (codepoint_length != NULL) *codepoint_length = width;
            return true;
        }
        cursor += width;
        ++current;
    }
    return false;
}

size_t kofun_unicode_byte_length(const uint8_t *text, size_t length) {
    (void)text;
    return length;
}

static bool kofun_is_emoji_property(const utf8proc_property_t *property) {
    return property->boundclass == UTF8PROC_BOUNDCLASS_REGIONAL_INDICATOR ||
        property->boundclass == UTF8PROC_BOUNDCLASS_E_BASE ||
        property->boundclass == UTF8PROC_BOUNDCLASS_E_MODIFIER ||
        property->boundclass == UTF8PROC_BOUNDCLASS_GLUE_AFTER_ZWJ ||
        property->boundclass == UTF8PROC_BOUNDCLASS_E_BASE_GAZ ||
        property->boundclass == UTF8PROC_BOUNDCLASS_EXTENDED_PICTOGRAPHIC ||
        property->boundclass == UTF8PROC_BOUNDCLASS_E_ZWG;
}

bool kofun_unicode_display_width(
    const uint8_t *text,
    size_t length,
    bool east_asian_ambiguous_wide,
    size_t *columns
) {
    if (text == NULL && length != 0) return false;
    size_t cursor = 0;
    size_t total = 0;
    while (cursor < length) {
        size_t cluster_end = 0;
        if (!kofun_unicode_grapheme_next(
                text, length, cursor, &cluster_end)) {
            return false;
        }
        size_t cluster_width = 0;
        bool emoji = false;
        size_t scalar = cursor;
        while (scalar < cluster_end) {
            uint32_t codepoint = 0;
            size_t width = 0;
            if (!kofun_unicode_decode(
                    text, length, scalar, &codepoint, &width)) {
                return false;
            }
            const utf8proc_property_t *property =
                utf8proc_get_property((utf8proc_int32_t)codepoint);
            int scalar_width =
                utf8proc_charwidth((utf8proc_int32_t)codepoint);
            if (east_asian_ambiguous_wide &&
                scalar_width == 1 &&
                utf8proc_charwidth_ambiguous(
                    (utf8proc_int32_t)codepoint)) {
                scalar_width = 2;
            }
            if (scalar_width > 0 &&
                (size_t)scalar_width > cluster_width) {
                cluster_width = (size_t)scalar_width;
            }
            if (kofun_is_emoji_property(property) ||
                codepoint == UINT32_C(0xfe0f)) {
                emoji = true;
            }
            scalar += width;
        }
        if (emoji && cluster_width < 2) cluster_width = 2;
        if (total > SIZE_MAX - cluster_width) return false;
        total += cluster_width;
        cursor = cluster_end;
    }
    if (columns != NULL) *columns = total;
    return true;
}

void kofun_unicode_source_position(
    const uint8_t *source,
    size_t length,
    size_t byte_offset,
    size_t *line,
    size_t *column
) {
    size_t target = byte_offset < length ? byte_offset : length;
    size_t current_line = 1;
    size_t current_column = 1;
    size_t cursor = 0;
    while (cursor < target) {
        if (source[cursor] == '\r' &&
            cursor + 1 < target &&
            source[cursor + 1] == '\n') {
            ++current_line;
            current_column = 1;
            cursor += 2;
            continue;
        }
        if (source[cursor] == '\n') {
            ++current_line;
            current_column = 1;
            ++cursor;
            continue;
        }
        size_t next = 0;
        if (!kofun_unicode_grapheme_next(
                source, target, cursor, &next) ||
            next <= cursor) {
            uint32_t ignored = 0;
            size_t width = 0;
            if (!kofun_unicode_decode(
                    source, target, cursor, &ignored, &width)) {
                break;
            }
            next = cursor + width;
        }
        cursor = next;
        ++current_column;
    }
    if (line != NULL) *line = current_line;
    if (column != NULL) *column = current_column;
}

static void kofun_set_error(
    KofunUnicodeError *error,
    KofunUnicodeStatus status,
    const uint8_t *source,
    size_t length,
    size_t byte_offset,
    uint32_t codepoint
) {
    if (error == NULL) return;
    kofun_clear_error(error);
    error->status = status;
    error->byte_offset = byte_offset;
    error->codepoint = codepoint;
    kofun_unicode_source_position(
        source,
        length,
        byte_offset,
        &error->line,
        &error->column
    );
}

static bool kofun_buffer_reserve(
    KofunByteBuffer *buffer,
    size_t extra
) {
    if (extra > SIZE_MAX - buffer->length - 1) return false;
    size_t needed = buffer->length + extra + 1;
    if (needed <= buffer->capacity) return true;
    size_t capacity = buffer->capacity == 0 ? 64 : buffer->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    uint8_t *data = realloc(buffer->data, capacity);
    if (data == NULL) return false;
    buffer->data = data;
    buffer->capacity = capacity;
    return true;
}

static bool kofun_buffer_append(
    KofunByteBuffer *buffer,
    const uint8_t *bytes,
    size_t length
) {
    if (!kofun_buffer_reserve(buffer, length)) return false;
    if (length > 0) {
        memcpy(buffer->data + buffer->length, bytes, length);
    }
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool kofun_buffer_append_codepoint(
    KofunByteBuffer *buffer,
    uint32_t codepoint
) {
    uint8_t encoded[4];
    utf8proc_ssize_t length = utf8proc_encode_char(
        (utf8proc_int32_t)codepoint,
        encoded
    );
    return length > 0 &&
        kofun_buffer_append(buffer, encoded, (size_t)length);
}

static const KofunConfusableMapping *kofun_confusable_mapping(
    uint32_t codepoint
) {
    size_t low = 0;
    size_t high = sizeof(kofun_confusable_mappings) /
        sizeof(kofun_confusable_mappings[0]);
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (codepoint < kofun_confusable_mappings[middle].source) {
            high = middle;
        } else if (codepoint >
                   kofun_confusable_mappings[middle].source) {
            low = middle + 1;
        } else {
            return &kofun_confusable_mappings[middle];
        }
    }
    return NULL;
}

static bool kofun_normalize(
    const uint8_t *text,
    size_t length,
    utf8proc_option_t form,
    uint8_t **normalized,
    size_t *normalized_length
) {
    utf8proc_uint8_t *result = NULL;
    utf8proc_ssize_t mapped = utf8proc_map(
        text,
        (utf8proc_ssize_t)length,
        &result,
        UTF8PROC_STABLE | form
    );
    if (mapped < 0) {
        free(result);
        return false;
    }
    *normalized = result;
    *normalized_length = (size_t)mapped;
    return true;
}

static bool kofun_confusable_skeleton(
    const uint8_t *identifier,
    size_t length,
    uint8_t **skeleton,
    size_t *skeleton_length
) {
    uint8_t *decomposed = NULL;
    size_t decomposed_length = 0;
    if (!kofun_normalize(
            identifier,
            length,
            UTF8PROC_DECOMPOSE,
            &decomposed,
            &decomposed_length)) {
        return false;
    }

    KofunByteBuffer mapped = {NULL, 0, 0};
    size_t cursor = 0;
    bool ok = true;
    while (cursor < decomposed_length) {
        uint32_t codepoint = 0;
        size_t width = 0;
        if (!kofun_unicode_decode(
                decomposed,
                decomposed_length,
                cursor,
                &codepoint,
                &width)) {
            ok = false;
            break;
        }
        const KofunConfusableMapping *mapping =
            kofun_confusable_mapping(codepoint);
        if (mapping == NULL) {
            ok = kofun_buffer_append_codepoint(&mapped, codepoint);
        } else {
            for (size_t index = 0;
                 ok && index < mapping->values_length;
                 ++index) {
                ok = kofun_buffer_append_codepoint(
                    &mapped,
                    kofun_confusable_values[
                        mapping->values_offset + index
                    ]
                );
            }
        }
        if (!ok) break;
        cursor += width;
    }
    free(decomposed);
    if (!ok) {
        free(mapped.data);
        return false;
    }

    uint8_t *result = NULL;
    size_t result_length = 0;
    ok = kofun_normalize(
        mapped.data,
        mapped.length,
        UTF8PROC_DECOMPOSE,
        &result,
        &result_length
    );
    free(mapped.data);
    if (!ok) return false;
    *skeleton = result;
    *skeleton_length = result_length;
    return true;
}

static void kofun_free_records(
    KofunIdentifierRecord *records,
    size_t count
) {
    for (size_t index = 0; index < count; ++index) {
        free(records[index].spelling);
        free(records[index].skeleton);
    }
    free(records);
}

static bool kofun_record_identifier(
    const uint8_t *source,
    size_t source_length,
    size_t offset,
    size_t length,
    KofunIdentifierRecord **records,
    size_t *record_count,
    size_t *record_capacity,
    KofunUnicodeError *error
) {
    uint8_t *normalized = NULL;
    size_t normalized_length = 0;
    if (!kofun_normalize(
            source + offset,
            length,
            UTF8PROC_COMPOSE,
            &normalized,
            &normalized_length)) {
        kofun_set_error(
            error,
            KOFUN_UNICODE_OUT_OF_MEMORY,
            source,
            source_length,
            offset,
            0
        );
        return false;
    }
    bool nfc = normalized_length == length &&
        memcmp(normalized, source + offset, length) == 0;
    if (!nfc) {
        kofun_set_error(
            error,
            KOFUN_UNICODE_NON_NFC_IDENTIFIER,
            source,
            source_length,
            offset,
            0
        );
        if (error != NULL) {
            kofun_copy_text(
                error->identifier,
                source + offset,
                length
            );
            kofun_copy_text(
                error->replacement,
                normalized,
                normalized_length
            );
        }
        free(normalized);
        return false;
    }
    free(normalized);

    uint8_t *skeleton = NULL;
    size_t skeleton_length = 0;
    if (!kofun_confusable_skeleton(
            source + offset,
            length,
            &skeleton,
            &skeleton_length)) {
        kofun_set_error(
            error,
            KOFUN_UNICODE_OUT_OF_MEMORY,
            source,
            source_length,
            offset,
            0
        );
        return false;
    }

    for (size_t index = 0; index < *record_count; ++index) {
        KofunIdentifierRecord *previous = &(*records)[index];
        if (previous->skeleton_length != skeleton_length ||
            memcmp(
                previous->skeleton,
                skeleton,
                skeleton_length
            ) != 0) {
            continue;
        }
        bool same_spelling = previous->spelling_length == length &&
            memcmp(previous->spelling, source + offset, length) == 0;
        if (!same_spelling) {
            kofun_set_error(
                error,
                KOFUN_UNICODE_CONFUSABLE_IDENTIFIER,
                source,
                source_length,
                offset,
                0
            );
            if (error != NULL) {
                kofun_copy_text(
                    error->identifier,
                    source + offset,
                    length
                );
                kofun_copy_text(
                    error->conflicting_identifier,
                    previous->spelling,
                    previous->spelling_length
                );
            }
            free(skeleton);
            return false;
        }
        free(skeleton);
        return true;
    }

    if (*record_count == *record_capacity) {
        size_t capacity =
            *record_capacity == 0 ? 32 : *record_capacity * 2;
        if (capacity < *record_capacity ||
            capacity > SIZE_MAX / sizeof(**records)) {
            free(skeleton);
            kofun_set_error(
                error,
                KOFUN_UNICODE_OUT_OF_MEMORY,
                source,
                source_length,
                offset,
                0
            );
            return false;
        }
        KofunIdentifierRecord *grown = realloc(
            *records,
            capacity * sizeof(**records)
        );
        if (grown == NULL) {
            free(skeleton);
            kofun_set_error(
                error,
                KOFUN_UNICODE_OUT_OF_MEMORY,
                source,
                source_length,
                offset,
                0
            );
            return false;
        }
        *records = grown;
        *record_capacity = capacity;
    }

    uint8_t *spelling = malloc(length == 0 ? 1 : length);
    if (spelling == NULL) {
        free(skeleton);
        kofun_set_error(
            error,
            KOFUN_UNICODE_OUT_OF_MEMORY,
            source,
            source_length,
            offset,
            0
        );
        return false;
    }
    if (length > 0) memcpy(spelling, source + offset, length);
    KofunIdentifierRecord *record = &(*records)[(*record_count)++];
    record->spelling = spelling;
    record->spelling_length = length;
    record->skeleton = skeleton;
    record->skeleton_length = skeleton_length;
    return true;
}

bool kofun_unicode_validate_source(
    const uint8_t *source,
    size_t length,
    KofunUnicodeError *error
) {
    kofun_clear_error(error);
    if (source == NULL && length != 0) {
        if (error != NULL) error->status = KOFUN_UNICODE_INVALID_UTF8;
        return false;
    }

    size_t cursor = 0;
    while (cursor < length) {
        uint32_t codepoint = 0;
        size_t width = 0;
        if (!kofun_unicode_decode(
                source, length, cursor, &codepoint, &width)) {
            kofun_set_error(
                error,
                KOFUN_UNICODE_INVALID_UTF8,
                source,
                length,
                cursor,
                0
            );
            return false;
        }
        if (codepoint == 0) {
            kofun_set_error(
                error,
                KOFUN_UNICODE_NUL,
                source,
                length,
                cursor,
                codepoint
            );
            return false;
        }
        if (kofun_is_bidi_control(codepoint)) {
            kofun_set_error(
                error,
                KOFUN_UNICODE_BIDI_CONTROL,
                source,
                length,
                cursor,
                codepoint
            );
            return false;
        }
        cursor += width;
    }

    KofunIdentifierRecord *records = NULL;
    size_t record_count = 0;
    size_t record_capacity = 0;
    bool in_string = false;
    bool in_comment = false;
    bool escaped = false;
    cursor = 0;
    while (cursor < length) {
        uint32_t codepoint = 0;
        size_t width = 0;
        (void)kofun_unicode_decode(
            source, length, cursor, &codepoint, &width);

        if (in_comment) {
            if (codepoint == '\n') in_comment = false;
            cursor += width;
            continue;
        }
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (codepoint == '\\') {
                escaped = true;
            } else if (codepoint == '"') {
                in_string = false;
            }
            cursor += width;
            continue;
        }
        if (codepoint == '#') {
            in_comment = true;
            cursor += width;
            continue;
        }
        if (codepoint == '"') {
            in_string = true;
            cursor += width;
            continue;
        }

        if (codepoint == '_' ||
            kofun_unicode_is_xid_start(codepoint)) {
            size_t identifier_at = cursor;
            cursor += width;
            while (cursor < length) {
                uint32_t following = 0;
                size_t following_width = 0;
                (void)kofun_unicode_decode(
                    source,
                    length,
                    cursor,
                    &following,
                    &following_width
                );
                if (following != '_' &&
                    !kofun_unicode_is_xid_continue(following)) {
                    break;
                }
                cursor += following_width;
            }
            if (!kofun_record_identifier(
                    source,
                    length,
                    identifier_at,
                    cursor - identifier_at,
                    &records,
                    &record_count,
                    &record_capacity,
                    error)) {
                kofun_free_records(records, record_count);
                return false;
            }
            continue;
        }

        if (codepoint >= UINT32_C(0x80) ||
            (kofun_unicode_is_xid_continue(codepoint) &&
             !(codepoint >= '0' && codepoint <= '9'))) {
            kofun_set_error(
                error,
                KOFUN_UNICODE_INVALID_IDENTIFIER,
                source,
                length,
                cursor,
                codepoint
            );
            kofun_free_records(records, record_count);
            return false;
        }
        cursor += width;
    }

    kofun_free_records(records, record_count);
    return true;
}

const char *kofun_unicode_error_code(KofunUnicodeStatus status) {
    switch (status) {
        case KOFUN_UNICODE_INVALID_UTF8: return "EUNICODE001";
        case KOFUN_UNICODE_NUL: return "EUNICODE002";
        case KOFUN_UNICODE_BIDI_CONTROL: return "EUNICODE003";
        case KOFUN_UNICODE_INVALID_IDENTIFIER: return "EUNICODE004";
        case KOFUN_UNICODE_NON_NFC_IDENTIFIER: return "EUNICODE005";
        case KOFUN_UNICODE_CONFUSABLE_IDENTIFIER: return "EUNICODE006";
        case KOFUN_UNICODE_OUT_OF_MEMORY: return "EUNICODE007";
        case KOFUN_UNICODE_OK: return "OK";
    }
    return "EUNICODE000";
}

static bool kofun_locale_is_japanese(const char *locale) {
    return locale != NULL &&
        locale[0] == 'j' &&
        locale[1] == 'a' &&
        (locale[2] == '\0' ||
         locale[2] == '_' ||
         locale[2] == '-');
}

void kofun_unicode_format_error(
    const KofunUnicodeError *error,
    const char *locale,
    char *output,
    size_t output_size
) {
    if (output == NULL || output_size == 0) return;
    if (error == NULL || error->status == KOFUN_UNICODE_OK) {
        (void)snprintf(output, output_size, "ok");
        return;
    }
    const char *code = kofun_unicode_error_code(error->status);
    bool japanese = kofun_locale_is_japanese(locale);
    switch (error->status) {
        case KOFUN_UNICODE_INVALID_UTF8:
            (void)snprintf(
                output,
                output_size,
                japanese
                    ? "error[%s] %zu行%zu列 (byte %zu): UTF-8として不正なバイト列です"
                    : "error[%s] at line %zu, column %zu (byte %zu): invalid UTF-8",
                code,
                error->line,
                error->column,
                error->byte_offset
            );
            break;
        case KOFUN_UNICODE_NUL:
            (void)snprintf(
                output,
                output_size,
                japanese
                    ? "error[%s] %zu行%zu列 (byte %zu): ソース中のNUL文字は禁止されています"
                    : "error[%s] at line %zu, column %zu (byte %zu): NUL is forbidden in source",
                code,
                error->line,
                error->column,
                error->byte_offset
            );
            break;
        case KOFUN_UNICODE_BIDI_CONTROL:
            (void)snprintf(
                output,
                output_size,
                japanese
                    ? "error[%s] %zu行%zu列 (byte %zu): 双方向制御文字 U+%04" PRIX32 " は禁止されています"
                    : "error[%s] at line %zu, column %zu (byte %zu): bidi control U+%04" PRIX32 " is forbidden",
                code,
                error->line,
                error->column,
                error->byte_offset,
                error->codepoint
            );
            break;
        case KOFUN_UNICODE_INVALID_IDENTIFIER:
            (void)snprintf(
                output,
                output_size,
                japanese
                    ? "error[%s] %zu行%zu列 (byte %zu): U+%04" PRIX32 " は識別子に使用できません"
                    : "error[%s] at line %zu, column %zu (byte %zu): U+%04" PRIX32 " is not valid identifier syntax",
                code,
                error->line,
                error->column,
                error->byte_offset,
                error->codepoint
            );
            break;
        case KOFUN_UNICODE_NON_NFC_IDENTIFIER:
            (void)snprintf(
                output,
                output_size,
                japanese
                    ? "error[%s] %zu行%zu列 (byte %zu): 識別子 `%s` はNFCではありません。`%s` を使用してください"
                    : "error[%s] at line %zu, column %zu (byte %zu): identifier `%s` is not NFC; use `%s`",
                code,
                error->line,
                error->column,
                error->byte_offset,
                error->identifier,
                error->replacement
            );
            break;
        case KOFUN_UNICODE_CONFUSABLE_IDENTIFIER:
            (void)snprintf(
                output,
                output_size,
                japanese
                    ? "error[%s] %zu行%zu列 (byte %zu): 識別子 `%s` は `%s` と見分けにくいため使用できません"
                    : "error[%s] at line %zu, column %zu (byte %zu): identifier `%s` is confusable with `%s`",
                code,
                error->line,
                error->column,
                error->byte_offset,
                error->identifier,
                error->conflicting_identifier
            );
            break;
        case KOFUN_UNICODE_OUT_OF_MEMORY:
            (void)snprintf(
                output,
                output_size,
                japanese
                    ? "error[%s]: Unicode検証中にメモリが不足しました"
                    : "error[%s]: out of memory during Unicode validation",
                code
            );
            break;
        case KOFUN_UNICODE_OK:
            (void)snprintf(output, output_size, "ok");
            break;
    }
}
