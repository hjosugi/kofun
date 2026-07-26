#include "discovery_v1.h"

#include <string.h>

/*
 * Canonical bytes are parsed by expecting them literally.
 *
 * The contract does not merely say a request is JSON that happens to be
 * canonical; it says the canonical bytes *are* the encoding, down to key order,
 * two-space indentation, and the single space after `:`. Matching that layout
 * position by position is therefore both the simplest parser and the strictest
 * one: unknown fields, duplicate keys, reordered keys, reindented output, and
 * trailing data cannot survive it, so none of them needs a separate check that
 * could drift from the emitter. A tolerant parser plus a canonicality audit
 * would be two descriptions of one format, and the pair would eventually
 * disagree.
 */

typedef struct {
    const char *bytes;
    size_t length;
    size_t offset;
    bool failed;
} Cursor;

static void expect_literal(Cursor *cursor, const char *text) {
    size_t span;
    if (cursor->failed) {
        return;
    }
    span = strlen(text);
    if (cursor->length - cursor->offset < span ||
        memcmp(cursor->bytes + cursor->offset, text, span) != 0) {
        cursor->failed = true;
        return;
    }
    cursor->offset += span;
}

static bool is_lower_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

/* `Id`: exactly 64 lowercase hex characters between quotes. */
static void expect_id(Cursor *cursor, char *out) {
    size_t index;
    expect_literal(cursor, "\"");
    if (cursor->failed) {
        return;
    }
    if (cursor->length - cursor->offset < KOFUN_DISCOVERY_ID_CHARS) {
        cursor->failed = true;
        return;
    }
    for (index = 0; index < KOFUN_DISCOVERY_ID_CHARS; index++) {
        char c = cursor->bytes[cursor->offset + index];
        if (!is_lower_hex(c)) {
            cursor->failed = true;
            return;
        }
        out[index] = c;
    }
    out[KOFUN_DISCOVERY_ID_CHARS] = '\0';
    cursor->offset += KOFUN_DISCOVERY_ID_CHARS;
    expect_literal(cursor, "\"");
}

/*
 * Base ten, no sign, and no leading zero except the single digit `0`. The
 * bound is passed in so one routine serves both U32 and U53 without letting a
 * U53 value reach a U32 field.
 */
static int64_t expect_integer(Cursor *cursor, int64_t maximum) {
    int64_t value = 0;
    size_t digits = 0;
    if (cursor->failed) {
        return 0;
    }
    while (cursor->offset + digits < cursor->length) {
        char c = cursor->bytes[cursor->offset + digits];
        if (c < '0' || c > '9') {
            break;
        }
        /* Reject before the multiply so an overflow cannot be observed. */
        if (value > (maximum - (c - '0')) / 10) {
            cursor->failed = true;
            return 0;
        }
        value = (value * 10) + (c - '0');
        digits++;
    }
    if (digits == 0) {
        cursor->failed = true;
        return 0;
    }
    if (digits > 1 && cursor->bytes[cursor->offset] == '0') {
        cursor->failed = true;
        return 0;
    }
    cursor->offset += digits;
    return value;
}

static bool expect_bool(Cursor *cursor) {
    if (cursor->failed) {
        return false;
    }
    if (cursor->length - cursor->offset >= 4u &&
        memcmp(cursor->bytes + cursor->offset, "true", 4u) == 0) {
        cursor->offset += 4u;
        return true;
    }
    expect_literal(cursor, "false");
    return false;
}

/*
 * A bounded text scalar. Control characters are rejected outright and the
 * bytes must be well-formed UTF-8; only `\"` and `\\` are accepted as escapes,
 * because the contract emits every other permitted code point literally.
 *
 * NFC is *not* verified here. Doing so needs the normalization tables under
 * `unicode/`, which this module does not link, so a caller holding those tables
 * remains responsible for that half of the `Name`/`Spelling` rule. Rejecting
 * the easy half silently and calling the scalar validated would be worse than
 * saying which half is checked.
 */
static void expect_text(Cursor *cursor, char *out, size_t maximum) {
    size_t written = 0;
    expect_literal(cursor, "\"");
    while (!cursor->failed && cursor->offset < cursor->length) {
        unsigned char c = (unsigned char)cursor->bytes[cursor->offset];
        size_t sequence;
        if (c == '"') {
            cursor->offset++;
            out[written] = '\0';
            return;
        }
        if (c < 0x20u || c == 0x7fu) {
            cursor->failed = true;
            return;
        }
        if (c == '\\') {
            char next;
            if (cursor->offset + 1u >= cursor->length) {
                cursor->failed = true;
                return;
            }
            next = cursor->bytes[cursor->offset + 1u];
            if (next != '"' && next != '\\') {
                cursor->failed = true;
                return;
            }
            if (written + 1u > maximum) {
                cursor->failed = true;
                return;
            }
            out[written++] = next;
            cursor->offset += 2u;
            continue;
        }
        /* UTF-8 well-formedness, including the surrogate and overlong bans. */
        if (c < 0x80u) {
            sequence = 1u;
        } else if ((c & 0xe0u) == 0xc0u && c >= 0xc2u) {
            sequence = 2u;
        } else if ((c & 0xf0u) == 0xe0u) {
            sequence = 3u;
        } else if ((c & 0xf8u) == 0xf0u && c <= 0xf4u) {
            sequence = 4u;
        } else {
            cursor->failed = true;
            return;
        }
        if (cursor->offset + sequence > cursor->length ||
            written + sequence > maximum) {
            cursor->failed = true;
            return;
        }
        {
            size_t index;
            for (index = 1u; index < sequence; index++) {
                unsigned char cont =
                    (unsigned char)cursor->bytes[cursor->offset + index];
                if ((cont & 0xc0u) != 0x80u) {
                    cursor->failed = true;
                    return;
                }
            }
            if (sequence == 3u) {
                unsigned char second =
                    (unsigned char)cursor->bytes[cursor->offset + 1u];
                if (c == 0xe0u && second < 0xa0u) {
                    cursor->failed = true; /* overlong */
                    return;
                }
                if (c == 0xedu && second >= 0xa0u) {
                    cursor->failed = true; /* surrogate */
                    return;
                }
            }
            if (sequence == 4u) {
                unsigned char second =
                    (unsigned char)cursor->bytes[cursor->offset + 1u];
                if (c == 0xf0u && second < 0x90u) {
                    cursor->failed = true; /* overlong */
                    return;
                }
                if (c == 0xf4u && second >= 0x90u) {
                    cursor->failed = true; /* above U+10FFFF */
                    return;
                }
            }
            memcpy(out + written, cursor->bytes + cursor->offset, sequence);
            written += sequence;
            cursor->offset += sequence;
        }
    }
    cursor->failed = true;
}

static KofunDiscoveryQueryKind parse_query_kind(Cursor *cursor) {
    static const struct {
        const char *spelling;
        KofunDiscoveryQueryKind kind;
    } kinds[] = {
        {"\"explain-operation\"", KOFUN_DISCOVERY_QUERY_EXPLAIN_OPERATION},
        {"\"operations\"", KOFUN_DISCOVERY_QUERY_OPERATIONS},
        {"\"type-and-operations\"", KOFUN_DISCOVERY_QUERY_TYPE_AND_OPERATIONS},
        {"\"type\"", KOFUN_DISCOVERY_QUERY_TYPE},
    };
    size_t index;
    if (cursor->failed) {
        return KOFUN_DISCOVERY_QUERY_TYPE;
    }
    for (index = 0; index < sizeof(kinds) / sizeof(kinds[0]); index++) {
        size_t span = strlen(kinds[index].spelling);
        if (cursor->length - cursor->offset >= span &&
            memcmp(cursor->bytes + cursor->offset, kinds[index].spelling,
                   span) == 0) {
            cursor->offset += span;
            return kinds[index].kind;
        }
    }
    cursor->failed = true;
    return KOFUN_DISCOVERY_QUERY_TYPE;
}

bool kofun_discovery_request_parse(const char *bytes, size_t length,
                                   KofunDiscoveryRequest *out,
                                   KofunDiscoveryReason *reason) {
    Cursor cursor;
    int64_t generation;
    int64_t byte_offset;
    int64_t expression_end;
    int64_t expression_start;

    if (out == NULL || reason == NULL) {
        return false;
    }
    *reason = KOFUN_DISCOVERY_REASON_INVALID_REQUEST;
    if (bytes == NULL) {
        return false;
    }
    /* Checked before anything is read, as the limit rule requires. */
    if (length > KOFUN_DISCOVERY_MAX_REQUEST_BYTES) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    cursor.bytes = bytes;
    cursor.length = length;
    cursor.offset = 0;
    cursor.failed = false;

    expect_literal(&cursor, "{\n  \"analysis\": {\n    \"file_id\": ");
    expect_id(&cursor, out->analysis.file_id);
    expect_literal(&cursor, ",\n    \"generation\": ");
    generation = expect_integer(&cursor, KOFUN_DISCOVERY_U53_MAX);
    expect_literal(&cursor, ",\n    \"interface_set_sha256\": ");
    expect_id(&cursor, out->analysis.interface_set_sha256);
    expect_literal(&cursor, ",\n    \"semantic_compatibility\": ");
    expect_text(&cursor, out->analysis.semantic_compatibility,
                KOFUN_DISCOVERY_MAX_COMPATIBILITY_BYTES);
    expect_literal(&cursor, ",\n    \"source_sha256\": ");
    expect_id(&cursor, out->analysis.source_sha256);
    expect_literal(&cursor, "\n  },\n  \"position\": {\n    \"byte_offset\": ");
    byte_offset = expect_integer(&cursor, KOFUN_DISCOVERY_U32_MAX);
    expect_literal(&cursor, ",\n    \"expression\": {\n      \"end\": ");
    expression_end = expect_integer(&cursor, KOFUN_DISCOVERY_U32_MAX);
    expect_literal(&cursor, ",\n      \"start\": ");
    expression_start = expect_integer(&cursor, KOFUN_DISCOVERY_U32_MAX);
    expect_literal(&cursor,
                   "\n    }\n  },\n  \"query\": {\n    "
                   "\"include_unavailable\": ");
    out->include_unavailable = expect_bool(&cursor);
    expect_literal(&cursor, ",\n    \"kind\": ");
    out->kind = parse_query_kind(&cursor);
    expect_literal(&cursor, ",\n    \"spelling\": ");
    if (!cursor.failed && cursor.length - cursor.offset >= 4u &&
        memcmp(cursor.bytes + cursor.offset, "null", 4u) == 0) {
        cursor.offset += 4u;
        out->has_spelling = false;
    } else {
        expect_text(&cursor, out->spelling, KOFUN_DISCOVERY_MAX_SPELLING_BYTES);
        out->has_spelling = true;
        /* `Spelling` is 1-1024 bytes: empty is not a spelling. */
        if (!cursor.failed && out->spelling[0] == '\0') {
            cursor.failed = true;
        }
    }
    expect_literal(&cursor, "\n  },\n  \"schema\": \"");
    expect_literal(&cursor, KOFUN_DISCOVERY_REQUEST_SCHEMA);
    expect_literal(&cursor, "\"\n}\n");

    if (cursor.failed || cursor.offset != length) {
        return false;
    }
    if (out->analysis.semantic_compatibility[0] == '\0') {
        return false;
    }

    /*
     * `spelling` is required non-null only for `explain-operation`, and
     * required null otherwise — a rule in both directions, so echoing a
     * spelling into another query kind is a rejection rather than a value that
     * is quietly ignored.
     */
    if ((out->kind == KOFUN_DISCOVERY_QUERY_EXPLAIN_OPERATION) !=
        out->has_spelling) {
        return false;
    }

    out->analysis.generation = generation;
    out->byte_offset = (uint32_t)byte_offset;
    out->expression_start = (uint32_t)expression_start;
    out->expression_end = (uint32_t)expression_end;

    /*
     * Structurally sound but inconsistent offsets are `invalid-position`, not
     * `invalid-request`; the contract separates them and a client can act on
     * the difference.
     */
    if (expression_start > expression_end || byte_offset < expression_start ||
        byte_offset > expression_end) {
        *reason = KOFUN_DISCOVERY_REASON_INVALID_POSITION;
        return false;
    }

    *reason = KOFUN_DISCOVERY_REASON_NONE;
    return true;
}

static bool is_utf8_boundary(const char *source, size_t source_length,
                             uint32_t offset) {
    if (offset > source_length) {
        return false;
    }
    if (offset == source_length) {
        return true;
    }
    /* A continuation byte is the one position that is not a boundary. */
    return ((unsigned char)source[offset] & 0xc0u) != 0x80u;
}

bool kofun_discovery_offsets_are_boundaries(const KofunDiscoveryRequest *request,
                                            const char *source,
                                            size_t source_length) {
    if (request == NULL || source == NULL) {
        return false;
    }
    if (request->expression_end > source_length) {
        return false;
    }
    return is_utf8_boundary(source, source_length, request->expression_start) &&
           is_utf8_boundary(source, source_length, request->byte_offset) &&
           is_utf8_boundary(source, source_length, request->expression_end);
}

const char *kofun_discovery_status_name(KofunDiscoveryStatus status) {
    switch (status) {
    case KOFUN_DISCOVERY_STATUS_COMPLETE:
        return "complete";
    case KOFUN_DISCOVERY_STATUS_PARTIAL:
        return "partial";
    case KOFUN_DISCOVERY_STATUS_STALE:
        return "stale";
    case KOFUN_DISCOVERY_STATUS_UNAVAILABLE:
        return "unavailable";
    case KOFUN_DISCOVERY_STATUS_INVALID:
        return "invalid";
    default:
        return NULL;
    }
}

const char *kofun_discovery_reason_name(KofunDiscoveryReason reason) {
    switch (reason) {
    case KOFUN_DISCOVERY_REASON_NONE:
        return NULL;
    case KOFUN_DISCOVERY_REASON_CANCELLED_BEFORE_ANALYSIS:
        return "cancelled-before-analysis";
    case KOFUN_DISCOVERY_REASON_INCOMPLETE_CURRENT_FILE_FACTS:
        return "incomplete-current-file-facts";
    case KOFUN_DISCOVERY_REASON_INVALID_POSITION:
        return "invalid-position";
    case KOFUN_DISCOVERY_REASON_INVALID_REQUEST:
        return "invalid-request";
    case KOFUN_DISCOVERY_REASON_LIMIT_EXHAUSTED:
        return "limit-exhausted";
    case KOFUN_DISCOVERY_REASON_STALE_GENERATION:
        return "stale-generation";
    case KOFUN_DISCOVERY_REASON_STALE_INTERFACE_SET:
        return "stale-interface-set";
    case KOFUN_DISCOVERY_REASON_STALE_SEMANTIC_COMPATIBILITY:
        return "stale-semantic-compatibility";
    case KOFUN_DISCOVERY_REASON_STALE_SOURCE:
        return "stale-source";
    case KOFUN_DISCOVERY_REASON_UNSUPPORTED_IN_PROFILE:
        return "unsupported-in-profile";
    case KOFUN_DISCOVERY_REASON_WRONG_FILE:
        return "wrong-file";
    default:
        return NULL;
    }
}

/* A bounded appender: every write is capacity-checked, so truncation is a
 * failure to emit rather than a short result that looks complete. */
typedef struct {
    char *buffer;
    size_t capacity;
    size_t written;
    bool overflowed;
} Sink;

static void put(Sink *sink, const char *text) {
    size_t span;
    if (sink->overflowed) {
        return;
    }
    span = strlen(text);
    if (sink->written + span > sink->capacity) {
        sink->overflowed = true;
        return;
    }
    memcpy(sink->buffer + sink->written, text, span);
    sink->written += span;
}

static void put_u53(Sink *sink, int64_t value) {
    char digits[21];
    size_t index = sizeof(digits);
    if (value == 0) {
        put(sink, "0");
        return;
    }
    digits[--index] = '\0';
    while (value > 0 && index > 0) {
        digits[--index] = (char)('0' + (value % 10));
        value /= 10;
    }
    put(sink, digits + index);
}

/*
 * Which statuses may carry an echoed analysis key, and which must carry a
 * reason. Encoding the table once keeps the emitter from being able to produce
 * a shape the contract forbids.
 */
static bool shape_is_permitted(KofunDiscoveryStatus status,
                               KofunDiscoveryReason reason,
                               const KofunDiscoveryAnalysisKey *analysis) {
    if (kofun_discovery_status_name(status) == NULL) {
        return false;
    }
    switch (status) {
    case KOFUN_DISCOVERY_STATUS_INVALID:
        /* `analysis` and `type` are null; a reason explains the failure. */
        return analysis == NULL && reason != KOFUN_DISCOVERY_REASON_NONE;
    case KOFUN_DISCOVERY_STATUS_STALE:
        return analysis != NULL &&
               (reason == KOFUN_DISCOVERY_REASON_WRONG_FILE ||
                reason == KOFUN_DISCOVERY_REASON_STALE_GENERATION ||
                reason == KOFUN_DISCOVERY_REASON_STALE_INTERFACE_SET ||
                reason ==
                    KOFUN_DISCOVERY_REASON_STALE_SEMANTIC_COMPATIBILITY ||
                reason == KOFUN_DISCOVERY_REASON_STALE_SOURCE);
    case KOFUN_DISCOVERY_STATUS_UNAVAILABLE:
        return analysis != NULL && reason != KOFUN_DISCOVERY_REASON_NONE;
    default:
        /* `complete` and `partial` carry facts and are not emitted here. */
        return false;
    }
}

size_t kofun_discovery_result_emit_factless(
    KofunDiscoveryStatus status, KofunDiscoveryReason reason,
    const KofunDiscoveryAnalysisKey *analysis, char *buffer, size_t capacity) {
    Sink sink;
    const char *reason_name;

    if (buffer == NULL || !shape_is_permitted(status, reason, analysis)) {
        return 0;
    }
    if (capacity > KOFUN_DISCOVERY_MAX_RESULT_BYTES) {
        capacity = KOFUN_DISCOVERY_MAX_RESULT_BYTES;
    }
    reason_name = kofun_discovery_reason_name(reason);

    sink.buffer = buffer;
    sink.capacity = capacity;
    sink.written = 0;
    sink.overflowed = false;

    /* Keys in ASCII lexicographic order, two-space indentation, one per line. */
    put(&sink, "{\n  \"analysis\": ");
    if (analysis == NULL) {
        put(&sink, "null");
    } else {
        put(&sink, "{\n    \"file_id\": \"");
        put(&sink, analysis->file_id);
        put(&sink, "\",\n    \"generation\": ");
        put_u53(&sink, analysis->generation);
        put(&sink, ",\n    \"interface_set_sha256\": \"");
        put(&sink, analysis->interface_set_sha256);
        put(&sink, "\",\n    \"semantic_compatibility\": \"");
        put(&sink, analysis->semantic_compatibility);
        put(&sink, "\",\n    \"source_sha256\": \"");
        put(&sink, analysis->source_sha256);
        put(&sink, "\"\n  }");
    }
    put(&sink, ",\n  \"authoritative\": false,\n  \"diagnostics\": [],\n  "
               "\"limit_profile\": \"" KOFUN_DISCOVERY_LIMIT_PROFILE
               "\",\n  \"omissions\": [],\n  \"operations\": [],\n  "
               "\"reason\": ");
    if (reason_name == NULL) {
        put(&sink, "null");
    } else {
        put(&sink, "\"");
        put(&sink, reason_name);
        put(&sink, "\"");
    }
    put(&sink, ",\n  \"schema\": \"" KOFUN_DISCOVERY_RESULT_SCHEMA
               "\",\n  \"status\": \"");
    put(&sink, kofun_discovery_status_name(status));
    put(&sink, "\",\n  \"truncated\": false,\n  \"type\": null\n}\n");

    if (sink.overflowed) {
        return 0;
    }
    return sink.written;
}
