/*
 * Audited executable seed for bootstrap/stage2/compiler.kofun.
 *
 * The Kofun file is canonical.  This C11 transliteration exists only until the
 * active Kofun bootstrap path can lower the complete Stage 2 source.
 */
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../../unicode/kofun_unicode.c"

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

static void fail(const char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(2);
}

static void *allocate(size_t size) {
    void *value = malloc(size == 0 ? 1 : size);
    if (value == NULL) fail("stage2 seed: out of memory");
    return value;
}

static void buffer_init(Buffer *buffer) {
    buffer->capacity = 256;
    buffer->length = 0;
    buffer->data = allocate(buffer->capacity);
    buffer->data[0] = '\0';
}

static void buffer_reserve(Buffer *buffer, size_t extra) {
    size_t needed = buffer->length + extra + 1;
    if (needed <= buffer->capacity) return;
    size_t capacity = buffer->capacity;
    while (capacity < needed) capacity *= 2;
    char *data = realloc(buffer->data, capacity);
    if (data == NULL) fail("stage2 seed: out of memory");
    buffer->data = data;
    buffer->capacity = capacity;
}

static void buffer_append(Buffer *buffer, const char *text) {
    size_t length = strlen(text);
    buffer_reserve(buffer, length);
    memcpy(buffer->data + buffer->length, text, length + 1);
    buffer->length += length;
}

static void buffer_format(Buffer *buffer, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0) fail("stage2 seed: formatting failed");
    buffer_reserve(buffer, (size_t)needed);
    (void)vsnprintf(
        buffer->data + buffer->length,
        buffer->capacity - buffer->length,
        format,
        arguments
    );
    va_end(arguments);
    buffer->length += (size_t)needed;
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) fail("stage2 seed: cannot open input");
    if (fseek(file, 0, SEEK_END) != 0) fail("stage2 seed: cannot seek input");
    long position = ftell(file);
    if (position < 0) fail("stage2 seed: cannot size input");
    rewind(file);
    size_t size = (size_t)position;
    char *source = allocate(size + 1);
    if (fread(source, 1, size, file) != size) {
        fail("stage2 seed: cannot read input");
    }
    source[size] = '\0';
    if (fclose(file) != 0) fail("stage2 seed: cannot close input");
    return source;
}

static void write_file(const char *path, const char *value) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) fail("stage2 seed: cannot open output");
    size_t length = strlen(value);
    if (fwrite(value, 1, length, file) != length) {
        fail("stage2 seed: cannot write output");
    }
    if (fclose(file) != 0) fail("stage2 seed: cannot close output");
}

static bool same_file(const char *left, const char *right) {
    struct stat left_status;
    struct stat right_status;
    if (strcmp(left, right) == 0) return true;
    if (stat(left, &left_status) != 0 || stat(right, &right_status) != 0) {
        return false;
    }
    return left_status.st_dev == right_status.st_dev &&
           left_status.st_ino == right_status.st_ino;
}

static bool write_file_transactional(const char *path, const char *value) {
    size_t path_length = strlen(path);
    char *temporary = allocate(path_length + 40u);
    FILE *file = NULL;
    unsigned attempt;
    for (attempt = 0u; attempt < 100u; attempt += 1u) {
        (void)snprintf(
            temporary,
            path_length + 40u,
            "%s.kofun-tmp-%u",
            path,
            attempt
        );
        file = fopen(temporary, "wbx");
        if (file != NULL) break;
    }
    if (file == NULL) {
        free(temporary);
        return false;
    }
    size_t length = strlen(value);
    bool write_ok = fwrite(value, 1, length, file) == length;
    bool close_ok = fclose(file) == 0;
    if (!write_ok || !close_ok) {
        (void)remove(temporary);
        free(temporary);
        return false;
    }
    if (rename(temporary, path) != 0) {
        (void)remove(temporary);
        free(temporary);
        return false;
    }
    free(temporary);
    return true;
}

static bool identifier_start_at(
    const char *source,
    size_t length,
    int64_t offset,
    size_t *width
) {
    if (offset < 0 || (uint64_t)offset >= length) return false;
    uint32_t codepoint = 0;
    size_t scalar_width = 0;
    if (!kofun_unicode_decode(
            (const uint8_t *)source,
            length,
            (size_t)offset,
            &codepoint,
            &scalar_width)) {
        return false;
    }
    if (width != NULL) *width = scalar_width;
    return codepoint == '_' || kofun_unicode_is_xid_start(codepoint);
}

static bool identifier_continue_at(
    const char *source,
    size_t length,
    int64_t offset,
    size_t *width
) {
    if (offset < 0 || (uint64_t)offset >= length) return false;
    uint32_t codepoint = 0;
    size_t scalar_width = 0;
    if (!kofun_unicode_decode(
            (const uint8_t *)source,
            length,
            (size_t)offset,
            &codepoint,
            &scalar_width)) {
        return false;
    }
    if (width != NULL) *width = scalar_width;
    return codepoint == '_' || kofun_unicode_is_xid_continue(codepoint);
}

static int64_t skip_trivia(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = start;
    while (cursor < length) {
        unsigned char symbol = (unsigned char)source[cursor];
        if (isspace(symbol)) {
            ++cursor;
        } else if (source[cursor] == '#') {
            while (cursor < length && source[cursor] != '\n') ++cursor;
        } else {
            return cursor;
        }
    }
    return cursor;
}

static int64_t string_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = start + 1;
    bool escaped = false;
    while (cursor < length) {
        char symbol = source[cursor];
        if (escaped) {
            escaped = false;
        } else if (symbol == '\\') {
            escaped = true;
        } else if (symbol == '"') {
            return cursor + 1;
        } else if (symbol == '\n') {
            return -1;
        }
        ++cursor;
    }
    return -1;
}

static bool pair_token(const char *source, int64_t start) {
    static const char *pairs[] = {
        "->", "==", "!=", "<=", ">=", "&&", "||",
        "//", "..", "**", "??", "|>", "=>"
    };
    char pair[3] = {source[start], source[start + 1], '\0'};
    size_t count = sizeof(pairs) / sizeof(pairs[0]);
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(pair, pairs[index]) == 0) return true;
    }
    return false;
}

static int64_t token_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    if (start >= length) return start;
    char first = source[start];
    if (first == '"') return string_end(source, start);
    size_t first_width = 0;
    if (identifier_start_at(
            source,
            (size_t)length,
            start,
            &first_width)) {
        int64_t cursor = start + (int64_t)first_width;
        while (cursor < length) {
            size_t width = 0;
            if (!identifier_continue_at(
                    source,
                    (size_t)length,
                    cursor,
                    &width)) {
                break;
            }
            cursor += (int64_t)width;
        }
        return cursor;
    }
    int64_t cursor = start + 1;
    if (first >= '0' && first <= '9') {
        while (
            cursor < length &&
            ((source[cursor] >= '0' && source[cursor] <= '9') ||
             source[cursor] == '_')
        ) {
            ++cursor;
        }
        return cursor;
    }
    if (cursor < length && pair_token(source, start)) return cursor + 1;
    return cursor;
}

static bool token_equal(const char *source, int64_t start, const char *expected) {
    int64_t end = token_end(source, start);
    size_t length = strlen(expected);
    return end >= start &&
           (size_t)(end - start) == length &&
           strncmp(source + start, expected, length) == 0;
}

static char *token_copy(const char *source, int64_t start) {
    int64_t end = token_end(source, start);
    if (end < start) {
        char *empty = allocate(1);
        empty[0] = '\0';
        return empty;
    }
    size_t length = (size_t)(end - start);
    char *result = allocate(length + 1);
    memcpy(result, source + start, length);
    result[length] = '\0';
    return result;
}

static bool keyword_token(const char *source, int64_t start) {
    static const char *keywords[] = {
        "fn", "let", "mut", "return", "if", "else", "while", "for",
        "in", "break", "continue", "true", "false", "match", "type"
    };
    size_t count = sizeof(keywords) / sizeof(keywords[0]);
    for (size_t index = 0; index < count; ++index) {
        if (token_equal(source, start, keywords[index])) return true;
    }
    return false;
}

static const char *token_kind(const char *source, int64_t start) {
    int64_t end = token_end(source, start);
    if (end <= start) return "invalid";
    char first = source[start];
    if (first == '"') return "string";
    if (identifier_start_at(
            source,
            strlen(source),
            start,
            NULL)) {
        return keyword_token(source, start) ? "keyword" : "identifier";
    }
    if (first >= '0' && first <= '9') return "integer";
    return "punctuation";
}

static int64_t line_at(const char *source, int64_t target) {
    int64_t line = 1;
    for (int64_t cursor = 0; cursor < target && source[cursor] != '\0'; ++cursor) {
        if (source[cursor] == '\n') ++line;
    }
    return line;
}

static char *lex_source(const char *source) {
    Buffer tape;
    buffer_init(&tape);
    KofunUnicodeError unicode_error;
    if (!kofun_unicode_validate_source(
            (const uint8_t *)source,
            strlen(source),
            &unicode_error)) {
        char message[1024];
        kofun_unicode_format_error(
            &unicode_error,
            getenv("KOFUN_DIAGNOSTIC_LOCALE"),
            message,
            sizeof(message)
        );
        buffer_append(&tape, message);
        return tape.data;
    }
    buffer_append(&tape, "kofun-token-tape/v1\n");
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, 0);
    while (cursor < length) {
        int64_t end = token_end(source, cursor);
        if (end <= cursor) {
            tape.length = 0;
            tape.data[0] = '\0';
            buffer_format(
                &tape,
                "error[E2S01]: unterminated string at byte %" PRId64,
                cursor
            );
            return tape.data;
        }
        buffer_format(
            &tape,
            "%s|%" PRId64 "|%" PRId64 "|%" PRId64 "\n",
            token_kind(source, cursor),
            cursor,
            end,
            line_at(source, cursor)
        );
        cursor = skip_trivia(source, end);
    }
    return tape.data;
}

static int64_t balanced_end(
    const char *source,
    int64_t start,
    const char *open,
    const char *close
);
static char *owned_text(const char *text);

/*
 * General patterns are a syntax-only boundary.  The executable Core below
 * still implements only Bool and payload-free enum matching, but both paths
 * classify their arm heads through this parser instead of interpreting the
 * first token themselves.
 *
 * Node records are post-order: child ids are known before their owning node is
 * written.  Delimiter records use the owner's source start, which is unique
 * inside a match, so comma and `|` spans remain available even before the
 * owner id is allocated.
 */
#define PATTERN_DEPTH_LIMIT 32
#define PATTERN_NODE_LIMIT 256

typedef enum {
    PATTERN_WILDCARD,
    PATTERN_LITERAL,
    PATTERN_NAME,
    PATTERN_CONSTRUCTOR,
    PATTERN_OR,
    PATTERN_PARENTHESIZED,
    PATTERN_ERROR
} PatternKind;

typedef struct {
    const char *source;
    int64_t next_node_id;
    int64_t nodes;
    int64_t errors;
    int64_t limit_error_id;
} PatternParser;

typedef struct {
    int64_t end;
    int64_t root;
    PatternKind kind;
    bool fatal;
    Buffer records;
} ParsedPattern;

typedef struct {
    int64_t end;
    PatternKind kind;
} PatternSummary;

static ParsedPattern parse_pattern_or(
    PatternParser *parser,
    int64_t start,
    int64_t depth
);
static PatternSummary pattern_summary(const char *source, int64_t start);

static ParsedPattern parsed_pattern_init(int64_t start) {
    ParsedPattern result;
    result.end = start;
    result.root = -1;
    result.kind = PATTERN_ERROR;
    result.fatal = false;
    buffer_init(&result.records);
    return result;
}

static int64_t pattern_recovery_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, start);
    int64_t paren_depth = 0;
    int64_t bracket_depth = 0;
    while (cursor < length) {
        if (token_equal(source, cursor, "=>")) return cursor;
        if (token_equal(source, cursor, "{") && paren_depth == 0 &&
            bracket_depth == 0) {
            return cursor;
        }
        if (token_equal(source, cursor, "}") && paren_depth == 0 &&
            bracket_depth == 0) {
            return cursor;
        }
        if (token_equal(source, cursor, ",") && paren_depth == 0 &&
            bracket_depth == 0) {
            return cursor;
        }
        if (token_equal(source, cursor, "(")) {
            ++paren_depth;
        } else if (token_equal(source, cursor, ")")) {
            if (paren_depth == 0) return cursor;
            --paren_depth;
        } else if (token_equal(source, cursor, "[")) {
            ++bracket_depth;
        } else if (token_equal(source, cursor, "]")) {
            if (bracket_depth == 0) return cursor;
            --bracket_depth;
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return cursor;
}

static ParsedPattern pattern_error(
    PatternParser *parser,
    int64_t start,
    int64_t end,
    const char *reason,
    bool fatal
) {
    ParsedPattern result = parsed_pattern_init(end);
    if (end < start) end = start;
    if (parser->nodes >= PATTERN_NODE_LIMIT) {
        result.fatal = true;
        return result;
    }
    int64_t id = parser->next_node_id++;
    ++parser->nodes;
    ++parser->errors;
    result.root = id;
    result.kind = PATTERN_ERROR;
    result.fatal = fatal;
    buffer_format(
        &result.records,
        "node|%" PRId64 "|ErrorPattern|%" PRId64 "|%" PRId64
        "|%s\n"
        "pattern-diagnostic|E2S58|%s|%" PRId64 "|%" PRId64 "\n",
        id,
        start,
        end,
        reason,
        reason,
        start,
        end
    );
    return result;
}

static ParsedPattern pattern_limit_error(
    PatternParser *parser,
    int64_t start
) {
    int64_t end = token_end(parser->source, start);
    if (end < start) end = start;
    ParsedPattern result = parsed_pattern_init(end);
    result.fatal = true;
    if (parser->limit_error_id >= 0) {
        result.root = parser->limit_error_id;
        return result;
    }
    int64_t id = parser->next_node_id++;
    parser->limit_error_id = id;
    ++parser->errors;
    result.root = id;
    buffer_format(
        &result.records,
        "node|%" PRId64 "|ErrorPattern|%" PRId64 "|%" PRId64
        "|node-limit\n"
        "pattern-diagnostic|E2S58|node-limit|%" PRId64 "|%" PRId64
        "\n",
        id,
        start,
        end,
        start,
        end
    );
    return result;
}

static bool pattern_node_available(const PatternParser *parser) {
    return parser->nodes < PATTERN_NODE_LIMIT;
}

static void pattern_append_child(Buffer *children, int64_t child) {
    if (children->length > 0) buffer_append(children, ",");
    buffer_format(children, "%" PRId64, child);
}

static bool pattern_stop_token(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    return start >= length || token_equal(source, start, "=>") ||
           token_equal(source, start, ",") ||
           token_equal(source, start, ")") ||
           token_equal(source, start, "}") ||
           token_equal(source, start, "if");
}

static ParsedPattern parse_pattern_atomic(
    PatternParser *parser,
    int64_t start,
    int64_t depth
) {
    const char *source = parser->source;
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, start);
    int64_t checkpoint_node_id = parser->next_node_id;
    int64_t checkpoint_nodes = parser->nodes;
    int64_t checkpoint_errors = parser->errors;
    int64_t checkpoint_limit_error_id = parser->limit_error_id;
    if (!pattern_node_available(parser)) {
        return pattern_limit_error(parser, cursor);
    }
    if (depth > PATTERN_DEPTH_LIMIT) {
        int64_t recovered = pattern_recovery_end(source, cursor);
        return pattern_error(
            parser,
            cursor,
            recovered,
            "depth-limit",
            false
        );
    }
    if (cursor >= length || pattern_stop_token(source, cursor) ||
        token_equal(source, cursor, "|")) {
        int64_t end = cursor < length ? token_end(source, cursor) : cursor;
        return pattern_error(
            parser,
            cursor,
            end,
            "missing-pattern",
            false
        );
    }

    int64_t token_finish = token_end(source, cursor);
    if (token_equal(source, cursor, "{")) {
        int64_t close = balanced_end(source, cursor, "{", "}");
        int64_t end = close < 0 ? pattern_recovery_end(source, cursor) : close;
        return pattern_error(
            parser,
            cursor,
            end,
            "unsupported-record-pattern",
            false
        );
    }
    if (token_equal(source, cursor, "[") ||
        token_equal(source, cursor, "..")) {
        int64_t end = pattern_recovery_end(source, token_finish);
        if (end == token_finish) end = token_finish;
        return pattern_error(
            parser,
            cursor,
            end,
            token_equal(source, cursor, "..") ?
                "unsupported-rest-pattern" : "unsupported-pattern-token",
            false
        );
    }

    if (token_equal(source, cursor, "(")) {
        int64_t inner_start = skip_trivia(source, token_finish);
        ParsedPattern inner = parse_pattern_or(
            parser,
            inner_start,
            depth + 1
        );
        if (inner.fatal) return inner;
        int64_t close = skip_trivia(source, inner.end);
        if (close >= length || !token_equal(source, close, ")")) {
            int64_t recovered = pattern_recovery_end(source, close);
            free(inner.records.data);
            parser->next_node_id = checkpoint_node_id;
            parser->nodes = checkpoint_nodes;
            parser->errors = checkpoint_errors;
            parser->limit_error_id = checkpoint_limit_error_id;
            return pattern_error(
                parser,
                cursor,
                recovered,
                "missing-closing-parenthesis",
                false
            );
        }
        if (!pattern_node_available(parser)) {
            free(inner.records.data);
            return pattern_limit_error(parser, cursor);
        }
        ParsedPattern result = parsed_pattern_init(token_end(source, close));
        buffer_append(&result.records, inner.records.data);
        free(inner.records.data);
        int64_t id = parser->next_node_id++;
        ++parser->nodes;
        result.root = id;
        result.kind = PATTERN_PARENTHESIZED;
        buffer_format(
            &result.records,
            "node|%" PRId64 "|ParenthesizedPattern|%" PRId64
            "|%" PRId64 "|%" PRId64 "|%" PRId64 "|%" PRId64
            "|%" PRId64 "|%" PRId64 "\n",
            id,
            cursor,
            result.end,
            cursor,
            token_finish,
            close,
            token_end(source, close),
            inner.root
        );
        return result;
    }

    const char *kind = token_kind(source, cursor);
    bool literal = token_equal(source, cursor, "true") ||
                   token_equal(source, cursor, "false") ||
                   token_equal(source, cursor, "null") ||
                   strcmp(kind, "integer") == 0;
    if (token_equal(source, cursor, "_")) {
        ParsedPattern result = parsed_pattern_init(token_finish);
        int64_t id = parser->next_node_id++;
        ++parser->nodes;
        result.root = id;
        result.kind = PATTERN_WILDCARD;
        buffer_format(
            &result.records,
            "node|%" PRId64 "|WildcardPattern|%" PRId64 "|%" PRId64
            "\n",
            id,
            cursor,
            token_finish
        );
        return result;
    }
    if (literal) {
        ParsedPattern result = parsed_pattern_init(token_finish);
        int64_t id = parser->next_node_id++;
        ++parser->nodes;
        result.root = id;
        result.kind = PATTERN_LITERAL;
        const char *literal_kind = strcmp(kind, "integer") == 0 ?
            "Int" : (token_equal(source, cursor, "null") ? "Null" : "Bool");
        char *literal_token = token_copy(source, cursor);
        buffer_format(
            &result.records,
            "node|%" PRId64 "|LiteralPattern|%" PRId64 "|%" PRId64
            "|%s|%s|%" PRId64 "|%" PRId64 "\n",
            id,
            cursor,
            token_finish,
            literal_kind,
            literal_token,
            cursor,
            token_finish
        );
        free(literal_token);
        return result;
    }
    if (strcmp(kind, "identifier") != 0) {
        int64_t recovered = pattern_recovery_end(source, token_finish);
        if (recovered == token_finish) recovered = token_finish;
        return pattern_error(
            parser,
            cursor,
            recovered,
            "unsupported-pattern-token",
            false
        );
    }

    int64_t after_name = skip_trivia(source, token_finish);
    if (after_name < length && token_equal(source, after_name, "{")) {
        int64_t close = balanced_end(source, after_name, "{", "}");
        int64_t end = close < 0 ? pattern_recovery_end(source, after_name) : close;
        return pattern_error(
            parser,
            cursor,
            end,
            "unsupported-record-pattern",
            false
        );
    }
    if (after_name >= length || !token_equal(source, after_name, "(")) {
        ParsedPattern result = parsed_pattern_init(token_finish);
        int64_t id = parser->next_node_id++;
        ++parser->nodes;
        result.root = id;
        result.kind = PATTERN_NAME;
        char *name = token_copy(source, cursor);
        buffer_format(
            &result.records,
            "node|%" PRId64 "|NamePattern|%" PRId64 "|%" PRId64
            "|%s|%" PRId64 "|%" PRId64 "\n",
            id,
            cursor,
            token_finish,
            name,
            cursor,
            token_finish
        );
        free(name);
        return result;
    }

    Buffer records;
    Buffer children;
    buffer_init(&records);
    buffer_init(&children);
    int64_t open = after_name;
    int64_t payload = skip_trivia(source, token_end(source, open));
    int64_t payload_count = 0;
    int64_t close = -1;
    if (payload < length && token_equal(source, payload, ")")) {
        free(records.data);
        free(children.data);
        return pattern_error(
            parser,
            cursor,
            token_end(source, payload),
            "empty-constructor-payload",
            false
        );
    } else {
        while (payload < length) {
            ParsedPattern child = parse_pattern_or(
                parser,
                payload,
                depth + 1
            );
            if (child.fatal) {
                free(records.data);
                free(children.data);
                return child;
            }
            buffer_append(&records, child.records.data);
            free(child.records.data);
            pattern_append_child(&children, child.root);
            ++payload_count;
            int64_t separator = skip_trivia(source, child.end);
            if (separator < length && token_equal(source, separator, ",")) {
                buffer_format(
                    &records,
                    "delimiter|ConstructorPattern|%" PRId64
                    "|payload-comma|%" PRId64 "|%" PRId64
                    "|%" PRId64 "\n",
                    cursor,
                    payload_count - 1,
                    separator,
                    token_end(source, separator)
                );
                payload = skip_trivia(source, token_end(source, separator));
                if (payload < length && token_equal(source, payload, ")")) {
                    close = payload;
                    break;
                }
                continue;
            }
            if (separator < length && token_equal(source, separator, ")")) {
                close = separator;
                break;
            }
            int64_t recovered = pattern_recovery_end(source, separator);
            if (recovered < length && token_equal(source, recovered, ")")) {
                recovered = token_end(source, recovered);
            }
            free(records.data);
            free(children.data);
            parser->next_node_id = checkpoint_node_id;
            parser->nodes = checkpoint_nodes;
            parser->errors = checkpoint_errors;
            parser->limit_error_id = checkpoint_limit_error_id;
            return pattern_error(
                parser,
                cursor,
                recovered,
                pattern_stop_token(source, separator) ?
                    "missing-closing-parenthesis" : "missing-comma",
                false
            );
        }
    }
    if (close < 0) {
        int64_t recovered = pattern_recovery_end(source, payload);
        free(records.data);
        free(children.data);
        parser->next_node_id = checkpoint_node_id;
        parser->nodes = checkpoint_nodes;
        parser->errors = checkpoint_errors;
        parser->limit_error_id = checkpoint_limit_error_id;
        return pattern_error(
            parser,
            cursor,
            recovered,
            "missing-closing-parenthesis",
            false
        );
    }
    if (!pattern_node_available(parser)) {
        free(records.data);
        free(children.data);
        return pattern_limit_error(parser, cursor);
    }
    ParsedPattern result = parsed_pattern_init(token_end(source, close));
    free(result.records.data);
    result.records = records;
    int64_t id = parser->next_node_id++;
    ++parser->nodes;
    result.root = id;
    result.kind = PATTERN_CONSTRUCTOR;
    char *name = token_copy(source, cursor);
    buffer_format(
        &result.records,
        "node|%" PRId64 "|ConstructorPattern|%" PRId64 "|%" PRId64
        "|%s|%" PRId64 "|%" PRId64 "|%" PRId64 "|%" PRId64
        "|%" PRId64 "|%" PRId64 "|%" PRId64 "|%s\n",
        id,
        cursor,
        result.end,
        name,
        cursor,
        token_finish,
        open,
        token_end(source, open),
        close,
        token_end(source, close),
        payload_count,
        children.data
    );
    free(name);
    free(children.data);
    return result;
}

static ParsedPattern parse_pattern_or(
    PatternParser *parser,
    int64_t start,
    int64_t depth
) {
    const char *source = parser->source;
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, start);
    int64_t checkpoint_node_id = parser->next_node_id;
    int64_t checkpoint_nodes = parser->nodes;
    int64_t checkpoint_errors = parser->errors;
    int64_t checkpoint_limit_error_id = parser->limit_error_id;
    if (cursor < length &&
        (token_equal(source, cursor, "|") ||
         token_equal(source, cursor, "||"))) {
        return pattern_error(
            parser,
            cursor,
            token_end(source, cursor),
            token_equal(source, cursor, "||") ? "doubled-or" : "leading-or",
            false
        );
    }

    ParsedPattern first = parse_pattern_atomic(parser, cursor, depth);
    if (first.fatal) return first;
    int64_t separator = skip_trivia(source, first.end);
    if (separator < length && token_equal(source, separator, "||")) {
        free(first.records.data);
        parser->next_node_id = checkpoint_node_id;
        parser->nodes = checkpoint_nodes;
        parser->errors = checkpoint_errors;
        parser->limit_error_id = checkpoint_limit_error_id;
        return pattern_error(
            parser,
            cursor,
            token_end(source, separator),
            "doubled-or",
            false
        );
    }
    if (separator >= length || !token_equal(source, separator, "|")) {
        if (separator < length && token_equal(source, separator, "..")) {
            int64_t recovered = pattern_recovery_end(
                source,
                token_end(source, separator)
            );
            free(first.records.data);
            parser->next_node_id = checkpoint_node_id;
            parser->nodes = checkpoint_nodes;
            parser->errors = checkpoint_errors;
            parser->limit_error_id = checkpoint_limit_error_id;
            return pattern_error(
                parser,
                cursor,
                recovered,
                "unsupported-range-pattern",
                false
            );
        }
        return first;
    }

    Buffer records;
    Buffer children;
    buffer_init(&records);
    buffer_init(&children);
    buffer_append(&records, first.records.data);
    free(first.records.data);
    pattern_append_child(&children, first.root);
    int64_t alternatives = 1;
    int64_t end = first.end;
    while (separator < length && token_equal(source, separator, "|")) {
        buffer_format(
            &records,
            "separator|OrPattern|%" PRId64 "|%" PRId64 "|%" PRId64
            "|%" PRId64 "\n",
            cursor,
            alternatives - 1,
            separator,
            token_end(source, separator)
        );
        int64_t next = skip_trivia(source, token_end(source, separator));
        if (next < length &&
            (token_equal(source, next, "|") ||
             token_equal(source, next, "||"))) {
            free(records.data);
            free(children.data);
            parser->next_node_id = checkpoint_node_id;
            parser->nodes = checkpoint_nodes;
            parser->errors = checkpoint_errors;
            parser->limit_error_id = checkpoint_limit_error_id;
            return pattern_error(
                parser,
                cursor,
                token_end(source, next),
                "doubled-or",
                false
            );
        }
        if (pattern_stop_token(source, next)) {
            free(records.data);
            free(children.data);
            parser->next_node_id = checkpoint_node_id;
            parser->nodes = checkpoint_nodes;
            parser->errors = checkpoint_errors;
            parser->limit_error_id = checkpoint_limit_error_id;
            return pattern_error(
                parser,
                cursor,
                token_end(source, separator),
                "trailing-or",
                false
            );
        }
        ParsedPattern alternative = parse_pattern_atomic(
            parser,
            next,
            depth
        );
        if (alternative.fatal) {
            free(records.data);
            free(children.data);
            return alternative;
        }
        buffer_append(&records, alternative.records.data);
        free(alternative.records.data);
        pattern_append_child(&children, alternative.root);
        ++alternatives;
        end = alternative.end;
        separator = skip_trivia(source, alternative.end);
    }
    if (separator < length &&
        (token_equal(source, separator, "||") ||
         token_equal(source, separator, ".."))) {
        bool doubled = token_equal(source, separator, "||");
        int64_t recovered = doubled ? token_end(source, separator) :
            pattern_recovery_end(source, token_end(source, separator));
        free(records.data);
        free(children.data);
        parser->next_node_id = checkpoint_node_id;
        parser->nodes = checkpoint_nodes;
        parser->errors = checkpoint_errors;
        parser->limit_error_id = checkpoint_limit_error_id;
        return pattern_error(
            parser,
            cursor,
            recovered,
            doubled ? "doubled-or" : "unsupported-range-pattern",
            false
        );
    }
    if (!pattern_node_available(parser)) {
        free(records.data);
        free(children.data);
        return pattern_limit_error(parser, cursor);
    }
    ParsedPattern result = parsed_pattern_init(end);
    free(result.records.data);
    result.records = records;
    int64_t id = parser->next_node_id++;
    ++parser->nodes;
    result.root = id;
    result.kind = PATTERN_OR;
    buffer_format(
        &result.records,
        "node|%" PRId64 "|OrPattern|%" PRId64 "|%" PRId64
        "|%" PRId64 "|%s\n",
        id,
        cursor,
        end,
        alternatives,
        children.data
    );
    free(children.data);
    return result;
}

static int64_t pattern_match_open(const char *source, int64_t match_start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, token_end(source, match_start));
    int64_t parens = 0;
    int64_t brackets = 0;
    while (cursor < length) {
        if (token_equal(source, cursor, "(") ) {
            ++parens;
        } else if (token_equal(source, cursor, ")")) {
            if (parens > 0) --parens;
        } else if (token_equal(source, cursor, "[")) {
            ++brackets;
        } else if (token_equal(source, cursor, "]")) {
            if (brackets > 0) --brackets;
        } else if (token_equal(source, cursor, "{") && parens == 0 &&
                   brackets == 0) {
            return cursor;
        } else if (token_equal(source, cursor, "}") && parens == 0 &&
                   brackets == 0) {
            return -1;
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return -1;
}

static int64_t pattern_arm_arrow(
    const char *source,
    int64_t start,
    int64_t match_close
) {
    int64_t cursor = skip_trivia(source, start);
    int64_t parens = 0;
    int64_t brackets = 0;
    while (cursor < match_close) {
        if (token_equal(source, cursor, "=>")) return cursor;
        if (token_equal(source, cursor, ",") && parens == 0 &&
            brackets == 0) {
            return -1;
        }
        if (token_equal(source, cursor, "(") ) {
            ++parens;
        } else if (token_equal(source, cursor, ")")) {
            if (parens > 0) --parens;
        } else if (token_equal(source, cursor, "[")) {
            ++brackets;
        } else if (token_equal(source, cursor, "]")) {
            if (brackets > 0) --brackets;
        } else if (token_equal(source, cursor, "{") && parens == 0 &&
                   brackets == 0) {
            return -1;
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return -1;
}

static char *parse_pattern_trees(const char *source) {
    int64_t length = (int64_t)strlen(source);
    Buffer tree;
    buffer_init(&tree);
    buffer_append(
        &tree,
        "kofun-pattern-tree/v1\n"
        "limits|depth|32|nodes-per-compilation|256\n"
    );
    int64_t cursor = skip_trivia(source, 0);
    int64_t match_id = 0;
    PatternParser parser;
    parser.source = source;
    parser.next_node_id = 0;
    parser.nodes = 0;
    parser.errors = 0;
    parser.limit_error_id = -1;
    bool budget_exhausted = false;
    while (cursor < length && !budget_exhausted) {
        if (token_equal(source, cursor, "match")) {
            int64_t open = pattern_match_open(source, cursor);
            int64_t match_end = open < 0 ? -1 :
                balanced_end(source, open, "{", "}");
            if (open >= 0 && match_end >= 0) {
                int64_t close = match_end - 1;
                Buffer arms;
                buffer_init(&arms);
                int64_t arm_cursor = skip_trivia(
                    source,
                    token_end(source, open)
                );
                int64_t arm_id = 0;
                while (arm_cursor < close &&
                       !token_equal(source, arm_cursor, "}")) {
                    int64_t checkpoint_node_id = parser.next_node_id;
                    int64_t checkpoint_nodes = parser.nodes;
                    int64_t checkpoint_errors = parser.errors;
                    int64_t checkpoint_limit_error_id =
                        parser.limit_error_id;
                    ParsedPattern pattern = parse_pattern_or(
                        &parser,
                        arm_cursor,
                        1
                    );
                    int64_t after_pattern = skip_trivia(source, pattern.end);
                    if (!pattern.fatal && pattern.kind != PATTERN_ERROR &&
                        !token_equal(source, after_pattern, "=>") &&
                        !token_equal(source, after_pattern, "if")) {
                        int64_t recovered = pattern_recovery_end(
                            source,
                            after_pattern
                        );
                        free(pattern.records.data);
                        parser.next_node_id = checkpoint_node_id;
                        parser.nodes = checkpoint_nodes;
                        parser.errors = checkpoint_errors;
                        parser.limit_error_id = checkpoint_limit_error_id;
                        pattern = pattern_error(
                            &parser,
                            arm_cursor,
                            recovered,
                            "unexpected-token-after-pattern",
                            false
                        );
                    }
                    buffer_append(&arms, pattern.records.data);
                    free(pattern.records.data);
                    int64_t arrow = pattern_arm_arrow(
                        source,
                        pattern.end,
                        close
                    );
                    buffer_format(
                        &arms,
                        "arm|%" PRId64 "|%" PRId64 "|%" PRId64
                        "|%" PRId64 "|%" PRId64 "|%" PRId64
                        "|%" PRId64 "\n",
                        match_id,
                        arm_id,
                        pattern.root,
                        arm_cursor,
                        pattern.end,
                        arrow,
                        arrow < 0 ? -1 : token_end(source, arrow)
                    );
                    ++arm_id;
                    if (pattern.fatal && parser.limit_error_id >= 0) {
                        budget_exhausted = true;
                        break;
                    }
                    if (arrow < 0) {
                        int64_t recovery = skip_trivia(source, pattern.end);
                        if (recovery < close &&
                            token_equal(source, recovery, ",")) {
                            arm_cursor = skip_trivia(
                                source,
                                token_end(source, recovery)
                            );
                            continue;
                        }
                        break;
                    }
                    int64_t body = skip_trivia(
                        source,
                        token_end(source, arrow)
                    );
                    if (body >= close || !token_equal(source, body, "{")) {
                        arm_cursor = pattern_recovery_end(source, body);
                    } else {
                        int64_t body_end = balanced_end(source, body, "{", "}");
                        if (body_end < 0) break;
                        arm_cursor = skip_trivia(source, body_end);
                    }
                    if (arm_cursor < close &&
                        token_equal(source, arm_cursor, ",")) {
                        arm_cursor = skip_trivia(
                            source,
                            token_end(source, arm_cursor)
                        );
                    }
                }
                buffer_format(
                    &tree,
                    "match|%" PRId64 "|%" PRId64 "|%" PRId64
                    "|%" PRId64 "|%" PRId64 "|%" PRId64
                    "|%" PRId64 "\n",
                    match_id,
                    cursor,
                    open,
                    token_end(source, open),
                    close,
                    token_end(source, close),
                    arm_id
                );
                buffer_append(&tree, arms.data);
                free(arms.data);
                ++match_id;
            }
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    buffer_format(&tree, "match-count|%" PRId64 "\n", match_id);
    return tree.data;
}

static char *validate_executable_patterns(const char *source) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, 0);
    while (cursor < length) {
        if (token_equal(source, cursor, "match")) {
            int64_t open = pattern_match_open(source, cursor);
            int64_t match_end = open < 0 ? -1 :
                balanced_end(source, open, "{", "}");
            if (open >= 0 && match_end >= 0) {
                int64_t close = match_end - 1;
                int64_t arm = skip_trivia(source, token_end(source, open));
                while (arm < close && !token_equal(source, arm, "}")) {
                    PatternSummary summary = pattern_summary(source, arm);
                    bool executable = summary.kind == PATTERN_WILDCARD ||
                        summary.kind == PATTERN_NAME ||
                        (summary.kind == PATTERN_LITERAL &&
                         (token_equal(source, arm, "true") ||
                          token_equal(source, arm, "false")));
                    if (!executable) {
                        Buffer error;
                        buffer_init(&error);
                        buffer_format(
                            &error,
                            "error[E2S24]: general pattern syntax is parsed "
                            "but not executable in Stage 2 Core at byte %"
                            PRId64,
                            arm
                        );
                        return error.data;
                    }
                    int64_t arrow = pattern_arm_arrow(
                        source,
                        summary.end,
                        close
                    );
                    if (arrow < 0) break;
                    int64_t body = skip_trivia(
                        source,
                        token_end(source, arrow)
                    );
                    if (body >= close || !token_equal(source, body, "{")) {
                        break;
                    }
                    int64_t body_end = balanced_end(source, body, "{", "}");
                    if (body_end < 0) break;
                    arm = skip_trivia(source, body_end);
                    if (arm < close && token_equal(source, arm, ",")) {
                        arm = skip_trivia(source, token_end(source, arm));
                    }
                }
            }
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return owned_text("ok");
}

static PatternSummary pattern_summary(const char *source, int64_t start) {
    PatternParser parser;
    parser.source = source;
    parser.next_node_id = 0;
    parser.nodes = 0;
    parser.errors = 0;
    parser.limit_error_id = -1;
    ParsedPattern parsed = parse_pattern_or(&parser, start, 1);
    PatternSummary summary;
    summary.end = parsed.end;
    summary.kind = parsed.kind;
    free(parsed.records.data);
    return summary;
}

static char *pattern_first_error(const char *ir) {
    const char *record = ir;
    char best_reason[64] = "";
    int64_t best_start = -1;
    int64_t best_end = -1;
    while ((record = strstr(record, "pattern-diagnostic|")) != NULL) {
        char code[16];
        char reason[64];
        int64_t start = -1;
        int64_t end = -1;
        if (sscanf(
                record,
                "pattern-diagnostic|%15[^|]|%63[^|]|%" SCNd64
                "|%" SCNd64,
                code,
                reason,
                &start,
                &end
            ) == 4 && (best_start < 0 || start < best_start)) {
            (void)snprintf(best_reason, sizeof(best_reason), "%s", reason);
            best_start = start;
            best_end = end;
        }
        ++record;
    }
    if (best_start < 0) return owned_text("");
    Buffer error;
    buffer_init(&error);
    buffer_format(
        &error,
        "error[E2S58]: invalid pattern (%s) at bytes %" PRId64
        "..%" PRId64,
        best_reason,
        best_start,
        best_end
    );
    return error.data;
}

static int64_t balanced_end(
    const char *source,
    int64_t start,
    const char *open,
    const char *close
) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = start;
    int64_t depth = 0;
    while (cursor < length) {
        cursor = skip_trivia(source, cursor);
        if (cursor >= length) return -1;
        int64_t end = token_end(source, cursor);
        if (end <= cursor) return -1;
        if (token_equal(source, cursor, open)) {
            ++depth;
        } else if (token_equal(source, cursor, close)) {
            --depth;
            if (depth == 0) return end;
        }
        cursor = end;
    }
    return -1;
}

static bool basic_visibility_modifier(const char *source, int64_t start) {
    return token_equal(source, start, "pub") ||
           token_equal(source, start, "internal") ||
           token_equal(source, start, "private");
}

static bool visibility_word(const char *source, int64_t start) {
    return basic_visibility_modifier(source, start) ||
           token_equal(source, start, "public") ||
           token_equal(source, start, "protected");
}

static bool visibility_prefix_candidate(const char *source, int64_t start) {
    if (visibility_word(source, start)) return true;
    if (strcmp(token_kind(source, start), "identifier") != 0) return false;
    int64_t next = skip_trivia(source, token_end(source, start));
    return token_equal(source, next, "fn");
}

static int64_t function_declaration_start(
    const char *source,
    int64_t start
) {
    int64_t length = (int64_t)strlen(source);
    if (token_equal(source, start, "fn")) return start;
    if (!basic_visibility_modifier(source, start)) return -1;
    int64_t after_modifier = skip_trivia(source, token_end(source, start));
    if (
        after_modifier < length &&
        token_equal(source, after_modifier, "fn")
    ) {
        return after_modifier;
    }
    return -1;
}

static const char *visibility_level(const char *source, int64_t start) {
    if (token_equal(source, start, "pub")) return "public";
    if (token_equal(source, start, "internal")) return "internal";
    return "private";
}

static int64_t parameter_open(const char *source, int64_t start);

static char *visibility_prefix_error(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    Buffer error;
    buffer_init(&error);
    if (
        token_equal(source, start, "public") ||
        token_equal(source, start, "protected")
    ) {
        char *alias = token_copy(source, start);
        buffer_format(
            &error,
            "error[E2S34]: unsupported visibility modifier `%s`; "
            "use `pub`, `internal`, or `private` at bytes %" PRId64
            "..%" PRId64,
            alias,
            start,
            token_end(source, start)
        );
        free(alias);
        return error.data;
    }
    if (!basic_visibility_modifier(source, start)) {
        int64_t next = skip_trivia(source, token_end(source, start));
        if (
            strcmp(token_kind(source, start), "identifier") == 0 &&
            next < length && token_equal(source, next, "fn")
        ) {
            char *modifier = token_copy(source, start);
            buffer_format(
                &error,
                "error[E2S33]: unknown visibility modifier `%s`; expected "
                "`pub`, `internal`, or `private` at bytes %" PRId64
                "..%" PRId64,
                modifier,
                start,
                token_end(source, start)
            );
            free(modifier);
        }
        return error.data;
    }

    int64_t next = skip_trivia(source, token_end(source, start));
    if (next < length && token_equal(source, next, "fn")) return error.data;
    if (next < length && basic_visibility_modifier(source, next)) {
        char *first = token_copy(source, start);
        char *second = token_copy(source, next);
        const char *kind = strcmp(first, second) == 0 ? "repeated" : "conflicting";
        buffer_format(
            &error,
            "error[E2S33]: %s visibility modifiers `%s` and `%s` "
            "at bytes %" PRId64 "..%" PRId64,
            kind,
            first,
            second,
            start,
            token_end(source, next)
        );
        free(second);
        free(first);
        return error.data;
    }
    if (token_equal(source, start, "pub") && next < length &&
        token_equal(source, next, "(")) {
        int64_t form_end = balanced_end(source, next, "(", ")");
        if (form_end < 0) form_end = token_end(source, next);
        int64_t form_name = skip_trivia(source, token_end(source, next));
        bool rust_alias = form_name < length &&
                          (token_equal(source, form_name, "crate") ||
                           token_equal(source, form_name, "super") ||
                           token_equal(source, form_name, "in"));
        buffer_format(
            &error,
            "error[E2S34]: %s `pub(...)` visibility is not supported "
            "in this frontend slice at bytes %" PRId64 "..%" PRId64,
            rust_alias ? "Rust-style" : "restricted",
            start,
            form_end
        );
        return error.data;
    }

    char *modifier = token_copy(source, start);
    buffer_format(
        &error,
        "error[E2S33]: visibility modifier `%s` must be followed by a "
        "top-level `fn` declaration at bytes %" PRId64 "..%" PRId64,
        modifier,
        start,
        token_end(source, start)
    );
    free(modifier);
    return error.data;
}

static char *local_visibility_error(
    const char *source,
    int64_t function_start,
    int64_t function_close
) {
    Buffer error;
    buffer_init(&error);
    int64_t open = parameter_open(source, function_start);
    if (open < 0) return error.data;
    int64_t cursor = balanced_end(source, open, "(", ")");
    while (cursor >= 0 && cursor < function_close &&
           !token_equal(source, cursor, "{")) {
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    if (cursor < 0 || cursor >= function_close) return error.data;
    cursor = skip_trivia(source, token_end(source, cursor));
    while (cursor < function_close) {
        bool basic = basic_visibility_modifier(source, cursor);
        bool alias = token_equal(source, cursor, "public") ||
                     token_equal(source, cursor, "protected");
        if (basic || alias) {
            int64_t next = skip_trivia(source, token_end(source, cursor));
            bool declaration_like =
                next < function_close &&
                (token_equal(source, next, "fn") ||
                 token_equal(source, next, "let") ||
                 token_equal(source, next, "var") ||
                 token_equal(source, next, "type") ||
                 basic_visibility_modifier(source, next));
            if (declaration_like) {
                char *modifier = token_copy(source, cursor);
                buffer_format(
                    &error,
                    "error[%s]: visibility modifier `%s` is not supported "
                    "in local scope at bytes %" PRId64 "..%" PRId64,
                    alias ? "E2S34" : "E2S33",
                    modifier,
                    cursor,
                    token_end(source, cursor)
                );
                free(modifier);
                return error.data;
            }
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return error.data;
}

static char *function_name(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t after_fn = skip_trivia(source, token_end(source, start));
    if (
        after_fn >= length ||
        strcmp(token_kind(source, after_fn), "identifier") != 0
    ) {
        char *empty = allocate(1);
        empty[0] = '\0';
        return empty;
    }
    return token_copy(source, after_fn);
}

static int64_t parameter_open(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t after_fn = skip_trivia(source, token_end(source, start));
    int64_t after_name = skip_trivia(source, token_end(source, after_fn));
    if (after_name >= length || !token_equal(source, after_name, "(")) return -1;
    return after_name;
}

static int64_t parameter_count(const char *source, int64_t start) {
    int64_t open = parameter_open(source, start);
    if (open < 0) return -1;
    int64_t close = balanced_end(source, open, "(", ")");
    if (close < 0) return -1;
    int64_t cursor = skip_trivia(source, token_end(source, open));
    if (cursor >= close || token_equal(source, cursor, ")")) return 0;

    int64_t count = 1;
    int64_t paren_depth = 0;
    int64_t bracket_depth = 0;
    while (cursor < close) {
        if (token_equal(source, cursor, "(")) {
            ++paren_depth;
        } else if (token_equal(source, cursor, ")")) {
            if (paren_depth == 0) return count;
            --paren_depth;
        } else if (token_equal(source, cursor, "[")) {
            ++bracket_depth;
        } else if (token_equal(source, cursor, "]")) {
            --bracket_depth;
        } else if (
            token_equal(source, cursor, ",") &&
            paren_depth == 0 &&
            bracket_depth == 0
        ) {
            ++count;
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return count;
}

static int64_t function_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    char *name = function_name(source, start);
    if (!token_equal(source, start, "fn") || name[0] == '\0') {
        free(name);
        return -1;
    }
    free(name);

    int64_t open = parameter_open(source, start);
    if (open < 0) return -1;
    int64_t parameters_end = balanced_end(source, open, "(", ")");
    if (parameters_end < 0) return -1;
    int64_t cursor = skip_trivia(source, parameters_end);

    if (cursor < length && token_equal(source, cursor, "->")) {
        cursor = skip_trivia(source, token_end(source, cursor));
        int64_t type_tokens = 0;
        while (cursor < length && !token_equal(source, cursor, "{")) {
            if (token_equal(source, cursor, "=")) return -1;
            ++type_tokens;
            cursor = skip_trivia(source, token_end(source, cursor));
        }
        if (type_tokens == 0) return -1;
    }
    if (cursor >= length || !token_equal(source, cursor, "{")) return -1;
    return balanced_end(source, cursor, "{", "}");
}

static char *type_name(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t name = skip_trivia(source, token_end(source, start));
    if (
        name >= length ||
        strcmp(token_kind(source, name), "identifier") != 0
    ) {
        char *empty = allocate(1);
        empty[0] = '\0';
        return empty;
    }
    return token_copy(source, name);
}

static int64_t type_declaration_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    char *name_text = type_name(source, start);
    bool valid_start = token_equal(source, start, "type") &&
                       name_text[0] != '\0';
    free(name_text);
    if (!valid_start) return -1;

    int64_t name = skip_trivia(source, token_end(source, start));
    int64_t equals = skip_trivia(source, token_end(source, name));
    if (equals >= length || !token_equal(source, equals, "=")) return -1;
    int64_t pipe = skip_trivia(source, token_end(source, equals));
    int64_t constructors = 0;
    int64_t last_end = -1;
    while (pipe < length && token_equal(source, pipe, "|")) {
        int64_t constructor = skip_trivia(source, token_end(source, pipe));
        if (
            constructor >= length ||
            strcmp(token_kind(source, constructor), "identifier") != 0
        ) {
            return -1;
        }
        ++constructors;
        if (constructors > 64) return -2;
        last_end = token_end(source, constructor);
        pipe = skip_trivia(source, last_end);
        if (pipe < length && token_equal(source, pipe, "(")) {
            int64_t payload_end = balanced_end(source, pipe, "(", ")");
            if (payload_end < 0) return -1;
            last_end = payload_end;
            pipe = skip_trivia(source, payload_end);
        }
    }
    if (constructors == 0) return -1;
    if (
        pipe < length &&
        !token_equal(source, pipe, "fn") &&
        !token_equal(source, pipe, "type") &&
        !visibility_prefix_candidate(source, pipe)
    ) {
        return -1;
    }
    return last_end;
}

static int64_t top_level_end(const char *source, int64_t start) {
    if (token_equal(source, start, "type")) {
        return type_declaration_end(source, start);
    }
    int64_t function_start = function_declaration_start(source, start);
    if (function_start < 0) return -1;
    return function_end(source, function_start);
}

static int64_t after_optional_module_header(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, start);
    if (!token_equal(source, cursor, "module")) return cursor;
    cursor = skip_trivia(source, token_end(source, cursor));
    while (cursor < length && !token_equal(source, cursor, "type") &&
           !token_equal(source, cursor, "fn") &&
           !visibility_prefix_candidate(source, cursor)) {
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return cursor;
}

static int64_t next_function_start(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = after_optional_module_header(source, start);
    while (cursor < length && token_equal(source, cursor, "type")) {
        int64_t end = type_declaration_end(source, cursor);
        if (end <= cursor) return length;
        cursor = skip_trivia(source, end);
    }
    int64_t function_start = function_declaration_start(source, cursor);
    return function_start < 0 ? cursor : function_start;
}

static int64_t enum_declaration_start(
    const char *source,
    const char *wanted
) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = after_optional_module_header(source, 0);
    while (cursor < length) {
        if (token_equal(source, cursor, "type")) {
            char *name = type_name(source, cursor);
            bool found = strcmp(name, wanted) == 0;
            free(name);
            if (found) return cursor;
        }
        int64_t end = top_level_end(source, cursor);
        if (end <= cursor) return -1;
        cursor = skip_trivia(source, end);
    }
    return -1;
}

static int64_t enum_constructor_count(
    const char *source,
    const char *enum_type
) {
    int64_t declaration = enum_declaration_start(source, enum_type);
    if (declaration < 0) return -1;
    int64_t name = skip_trivia(source, token_end(source, declaration));
    int64_t equals = skip_trivia(source, token_end(source, name));
    int64_t pipe = skip_trivia(source, token_end(source, equals));
    int64_t end = type_declaration_end(source, declaration);
    int64_t count = 0;
    while (pipe < end && token_equal(source, pipe, "|")) {
        int64_t constructor = skip_trivia(source, token_end(source, pipe));
        ++count;
        pipe = skip_trivia(source, token_end(source, constructor));
    }
    return count;
}

static int64_t enum_constructor_index(
    const char *source,
    const char *enum_type,
    const char *wanted
) {
    int64_t declaration = enum_declaration_start(source, enum_type);
    if (declaration < 0) return -1;
    int64_t name = skip_trivia(source, token_end(source, declaration));
    int64_t equals = skip_trivia(source, token_end(source, name));
    int64_t pipe = skip_trivia(source, token_end(source, equals));
    int64_t end = type_declaration_end(source, declaration);
    int64_t tag = 0;
    while (pipe < end && token_equal(source, pipe, "|")) {
        int64_t constructor = skip_trivia(source, token_end(source, pipe));
        if (token_equal(source, constructor, wanted)) return tag;
        ++tag;
        pipe = skip_trivia(source, token_end(source, constructor));
    }
    return -1;
}

static bool enum_name_covered(const char *covered, const char *name) {
    Buffer key;
    buffer_init(&key);
    buffer_format(&key, "|%s|", name);
    bool found = strstr(covered, key.data) != NULL;
    free(key.data);
    return found;
}

static bool enum_constructors_covered(
    const char *source,
    const char *enum_type,
    const char *covered
) {
    int64_t declaration = enum_declaration_start(source, enum_type);
    if (declaration < 0) return false;
    int64_t name = skip_trivia(source, token_end(source, declaration));
    int64_t equals = skip_trivia(source, token_end(source, name));
    int64_t pipe = skip_trivia(source, token_end(source, equals));
    int64_t end = type_declaration_end(source, declaration);
    while (pipe < end && token_equal(source, pipe, "|")) {
        int64_t constructor = skip_trivia(source, token_end(source, pipe));
        char *constructor_name = token_copy(source, constructor);
        bool found = enum_name_covered(covered, constructor_name);
        free(constructor_name);
        if (!found) return false;
        pipe = skip_trivia(source, token_end(source, constructor));
    }
    return true;
}

static char *enum_missing_constructors(
    const char *source,
    const char *enum_type,
    const char *covered
) {
    int64_t declaration = enum_declaration_start(source, enum_type);
    int64_t name = skip_trivia(source, token_end(source, declaration));
    int64_t equals = skip_trivia(source, token_end(source, name));
    int64_t pipe = skip_trivia(source, token_end(source, equals));
    int64_t end = type_declaration_end(source, declaration);
    Buffer missing;
    buffer_init(&missing);
    while (pipe < end && token_equal(source, pipe, "|")) {
        int64_t constructor = skip_trivia(source, token_end(source, pipe));
        char *constructor_name = token_copy(source, constructor);
        if (!enum_name_covered(covered, constructor_name)) {
            if (missing.length > 0) buffer_append(&missing, ", ");
            buffer_format(&missing, "`%s`", constructor_name);
        }
        free(constructor_name);
        pipe = skip_trivia(source, token_end(source, constructor));
    }
    return missing.data;
}

static bool reserved_type_name(const char *name) {
    return strcmp(name, "Int") == 0 || strcmp(name, "Bool") == 0 ||
           strcmp(name, "Float") == 0 || strcmp(name, "Unit") == 0 ||
           strcmp(name, "Text") == 0 || strcmp(name, "List") == 0 ||
           strcmp(name, "_") == 0;
}

static char *parse_program(const char *source) {
    Buffer ir;
    Buffer declared_types;
    Buffer declared_constructors;
    buffer_init(&ir);
    buffer_init(&declared_types);
    buffer_init(&declared_constructors);
    buffer_append(&declared_types, "|");
    buffer_append(&declared_constructors, "|");
    int64_t length = (int64_t)strlen(source);
    buffer_format(&ir, "kofun-stage2-ir/v1\nsource-bytes|%" PRId64 "\n", length);
    int64_t cursor = skip_trivia(source, 0);
    int64_t functions = 0;
    int64_t types = 0;
    while (cursor < length) {
        char *visibility_error = visibility_prefix_error(source, cursor);
        if (visibility_error[0] != '\0') {
            free(declared_types.data);
            free(declared_constructors.data);
            free(ir.data);
            return visibility_error;
        }
        free(visibility_error);
        if (token_equal(source, cursor, "type")) {
            char *name = type_name(source, cursor);
            int64_t end = type_declaration_end(source, cursor);
            if (end == -2) {
                free(name);
                free(declared_types.data);
                free(declared_constructors.data);
                ir.length = 0;
                ir.data[0] = '\0';
                buffer_format(
                    &ir,
                    "error[E2S31]: concrete enum constructor limit is 64 "
                    "at byte %" PRId64,
                    cursor
                );
                return ir.data;
            }
            if (name[0] == '\0' || end < 0) {
                free(name);
                free(declared_types.data);
                free(declared_constructors.data);
                ir.length = 0;
                ir.data[0] = '\0';
                buffer_format(
                    &ir,
                    "error[E2S31]: malformed concrete enum declaration "
                    "at byte %" PRId64,
                    cursor
                );
                return ir.data;
            }
            if (reserved_type_name(name)) {
                Buffer error;
                buffer_init(&error);
                buffer_format(
                    &error,
                    "error[E2S31]: concrete enum cannot shadow built-in "
                    "type `%s` at byte %" PRId64,
                    name,
                    cursor
                );
                free(name);
                free(declared_types.data);
                free(declared_constructors.data);
                free(ir.data);
                return error.data;
            }
            ++types;
            if (types > 32) {
                free(name);
                free(declared_types.data);
                free(declared_constructors.data);
                ir.length = 0;
                ir.data[0] = '\0';
                buffer_format(
                    &ir,
                    "error[E2S31]: concrete enum limit is 32 types "
                    "at byte %" PRId64,
                    cursor
                );
                return ir.data;
            }
            if (enum_name_covered(declared_types.data, name)) {
                Buffer error;
                buffer_init(&error);
                buffer_format(
                    &error,
                    "error[E2S31]: duplicate concrete enum type `%s` "
                    "at byte %" PRId64,
                    name,
                    cursor
                );
                free(name);
                free(declared_types.data);
                free(declared_constructors.data);
                free(ir.data);
                return error.data;
            }
            if (enum_name_covered(declared_constructors.data, name)) {
                Buffer error;
                buffer_init(&error);
                buffer_format(
                    &error,
                    "error[E2S31]: concrete enum type `%s` conflicts "
                    "with a constructor at byte %" PRId64,
                    name,
                    cursor
                );
                free(name);
                free(declared_types.data);
                free(declared_constructors.data);
                free(ir.data);
                return error.data;
            }
            buffer_append(&declared_types, name);
            buffer_append(&declared_types, "|");
            int64_t count = enum_constructor_count(source, name);
            if (count < 1 || count > 64) {
                Buffer error;
                buffer_init(&error);
                if (count < 1) {
                    buffer_format(
                        &error,
                        "error[E2S31]: concrete enum must declare a "
                        "constructor at byte %" PRId64,
                        cursor
                    );
                } else {
                    buffer_format(
                        &error,
                        "error[E2S31]: concrete enum constructor limit is "
                        "64 at byte %" PRId64,
                        cursor
                    );
                }
                free(name);
                free(declared_types.data);
                free(declared_constructors.data);
                free(ir.data);
                return error.data;
            }
            buffer_format(
                &ir,
                "type|%s|%" PRId64 "|%" PRId64 "|%" PRId64 "\n",
                name,
                count,
                cursor,
                end
            );
            int64_t name_cursor = skip_trivia(
                source,
                token_end(source, cursor)
            );
            int64_t equals = skip_trivia(
                source,
                token_end(source, name_cursor)
            );
            int64_t pipe = skip_trivia(source, token_end(source, equals));
            int64_t tag = 0;
            while (pipe < end && token_equal(source, pipe, "|")) {
                int64_t constructor = skip_trivia(
                    source,
                    token_end(source, pipe)
                );
                char *constructor_name = token_copy(source, constructor);
                if (strcmp(constructor_name, "_") == 0) {
                    Buffer error;
                    buffer_init(&error);
                    buffer_format(
                        &error,
                        "error[E2S31]: `_` is reserved for enum catch-all "
                        "patterns at byte %" PRId64,
                        constructor
                    );
                    free(constructor_name);
                    free(name);
                    free(declared_types.data);
                    free(declared_constructors.data);
                    free(ir.data);
                    return error.data;
                }
                if (
                    enum_name_covered(
                        declared_constructors.data,
                        constructor_name
                    )
                ) {
                    Buffer error;
                    buffer_init(&error);
                    buffer_format(
                        &error,
                        "error[E2S31]: duplicate concrete enum constructor "
                        "`%s` at byte %" PRId64,
                        constructor_name,
                        constructor
                    );
                    free(constructor_name);
                    free(name);
                    free(declared_types.data);
                    free(declared_constructors.data);
                    free(ir.data);
                    return error.data;
                }
                if (enum_name_covered(declared_types.data, constructor_name)) {
                    Buffer error;
                    buffer_init(&error);
                    buffer_format(
                        &error,
                        "error[E2S31]: concrete enum constructor `%s` "
                        "conflicts with an enum type at byte %" PRId64,
                        constructor_name,
                        constructor
                    );
                    free(constructor_name);
                    free(name);
                    free(declared_types.data);
                    free(declared_constructors.data);
                    free(ir.data);
                    return error.data;
                }
                buffer_append(&declared_constructors, constructor_name);
                buffer_append(&declared_constructors, "|");
                buffer_format(
                    &ir,
                    "constructor|%s|%s|%" PRId64 "|%" PRId64
                    "|%" PRId64 "\n",
                    constructor_name,
                    name,
                    tag,
                    constructor,
                    token_end(source, constructor)
                );
                free(constructor_name);
                ++tag;
                pipe = skip_trivia(source, token_end(source, constructor));
            }
            free(name);
            cursor = skip_trivia(source, end);
        } else if (function_declaration_start(source, cursor) < 0) {
            free(declared_types.data);
            free(declared_constructors.data);
            ir.length = 0;
            ir.data[0] = '\0';
            buffer_format(
                &ir,
                "error[E2S02]: expected top-level `fn` or `type` "
                "at byte %" PRId64,
                cursor
            );
            return ir.data;
        } else {
            int64_t declaration_start = cursor;
            int64_t function_start = function_declaration_start(
                source,
                declaration_start
            );
            char *name = function_name(source, function_start);
            int64_t arity = parameter_count(source, function_start);
            int64_t end = function_end(source, function_start);
            if (name[0] == '\0' || arity < 0 || end < 0) {
                free(name);
                free(declared_types.data);
                free(declared_constructors.data);
                ir.length = 0;
                ir.data[0] = '\0';
                buffer_format(
                    &ir,
                    "error[E2S03]: malformed function at byte %" PRId64,
                    function_start
                );
                return ir.data;
            }
            char *local_error = local_visibility_error(
                source,
                function_start,
                end
            );
            if (local_error[0] != '\0') {
                free(name);
                free(declared_types.data);
                free(declared_constructors.data);
                free(ir.data);
                return local_error;
            }
            free(local_error);
            bool explicit_visibility = declaration_start != function_start;
            int64_t modifier_start = explicit_visibility ? declaration_start : -1;
            int64_t modifier_end = explicit_visibility ?
                token_end(source, declaration_start) : -1;
            buffer_format(
                &ir,
                "function|%s|%" PRId64 "|%" PRId64 "|%" PRId64
                "|%s|%s|%" PRId64 "|%" PRId64 "|%" PRId64
                "|%" PRId64 "|file:0|symbol:%" PRId64 "\n",
                name,
                arity,
                function_start,
                end,
                visibility_level(source, declaration_start),
                explicit_visibility ? "explicit" : "implicit",
                modifier_start,
                modifier_end,
                declaration_start,
                end,
                functions
            );
            free(name);
            ++functions;
            cursor = skip_trivia(source, end);
        }
    }
    free(declared_types.data);
    free(declared_constructors.data);
    if (functions == 0) {
        ir.length = 0;
        ir.data[0] = '\0';
        buffer_append(&ir, "error[E2S04]: compilation unit has no functions");
        return ir.data;
    }
    buffer_format(&ir, "function-count|%" PRId64 "\n", functions);
    char *patterns = parse_pattern_trees(source);
    char *pattern_error = pattern_first_error(patterns);
    if (pattern_error[0] != '\0') {
        free(patterns);
        free(ir.data);
        return pattern_error;
    }
    free(pattern_error);
    buffer_append(&ir, patterns);
    free(patterns);
    return ir.data;
}

static char *owned_text(const char *text) {
    size_t length = strlen(text);
    char *copy = allocate(length + 1);
    memcpy(copy, text, length + 1);
    return copy;
}

static char *enum_constructor_owner(
    const char *source,
    const char *wanted
) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, 0);
    while (cursor < length) {
        if (token_equal(source, cursor, "type")) {
            char *enum_type = type_name(source, cursor);
            if (enum_constructor_index(source, enum_type, wanted) >= 0) {
                return enum_type;
            }
            free(enum_type);
        }
        int64_t end = top_level_end(source, cursor);
        if (end <= cursor) return owned_text("");
        cursor = skip_trivia(source, end);
    }
    return owned_text("");
}

static char *enum_declaration_names(
    const char *source,
    bool constructors
) {
    int64_t length = (int64_t)strlen(source);
    Buffer names;
    buffer_init(&names);
    buffer_append(&names, "|");
    int64_t cursor = skip_trivia(source, 0);
    while (cursor < length) {
        if (token_equal(source, cursor, "type")) {
            if (constructors) {
                int64_t type_cursor = skip_trivia(
                    source,
                    token_end(source, cursor)
                );
                int64_t equals = skip_trivia(
                    source,
                    token_end(source, type_cursor)
                );
                int64_t pipe = skip_trivia(
                    source,
                    token_end(source, equals)
                );
                int64_t end = type_declaration_end(source, cursor);
                while (pipe < end && token_equal(source, pipe, "|")) {
                    int64_t constructor = skip_trivia(
                        source,
                        token_end(source, pipe)
                    );
                    char *name = token_copy(source, constructor);
                    buffer_append(&names, name);
                    buffer_append(&names, "|");
                    free(name);
                    pipe = skip_trivia(
                        source,
                        token_end(source, constructor)
                    );
                }
            } else {
                char *name = type_name(source, cursor);
                buffer_append(&names, name);
                buffer_append(&names, "|");
                free(name);
            }
        }
        int64_t end = top_level_end(source, cursor);
        if (end <= cursor) return names.data;
        cursor = skip_trivia(source, end);
    }
    return names.data;
}

static bool copy_type(const char *type_name) {
    return strcmp(type_name, "Int") == 0 ||
           strcmp(type_name, "Float") == 0 ||
           strcmp(type_name, "Bool") == 0 ||
           strcmp(type_name, "Unit") == 0;
}

static int64_t return_move_at(
    const char *source,
    int64_t body_open,
    int64_t body_end,
    const char *element_name
) {
    int64_t cursor = skip_trivia(source, token_end(source, body_open));
    while (cursor < body_end) {
        if (token_equal(source, cursor, "return")) {
            int64_t return_line = line_at(source, cursor);
            int64_t value_cursor = skip_trivia(
                source,
                token_end(source, cursor)
            );
            while (
                value_cursor < body_end &&
                line_at(source, value_cursor) == return_line
            ) {
                if (token_equal(source, value_cursor, element_name)) {
                    return value_cursor;
                }
                value_cursor = skip_trivia(
                    source,
                    token_end(source, value_cursor)
                );
            }
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return -1;
}

static char *borrowed_collection_check(const char *source) {
    int64_t length = (int64_t)strlen(source);
    int64_t function_cursor = next_function_start(source, 0);
    int64_t recognized_loops = 0;
    while (function_cursor < length) {
        int64_t parameters_open = parameter_open(source, function_cursor);
        if (parameters_open < 0) {
            return owned_text("error[E2S03]: malformed function");
        }
        int64_t parameters_end = balanced_end(
            source,
            parameters_open,
            "(",
            ")"
        );
        if (parameters_end < 0) {
            return owned_text("error[E2S03]: malformed parameters");
        }

        char *borrowed_name = owned_text("");
        char *element_type = owned_text("");
        int64_t borrowed_lists = 0;
        int64_t parameter_cursor = skip_trivia(
            source,
            token_end(source, parameters_open)
        );
        while (
            parameter_cursor < parameters_end &&
            !token_equal(source, parameter_cursor, ")")
        ) {
            if (token_equal(source, parameter_cursor, "read")) {
                int64_t name_cursor = skip_trivia(
                    source,
                    token_end(source, parameter_cursor)
                );
                int64_t colon_cursor = skip_trivia(
                    source,
                    token_end(source, name_cursor)
                );
                int64_t list_cursor = skip_trivia(
                    source,
                    token_end(source, colon_cursor)
                );
                int64_t bracket_cursor = skip_trivia(
                    source,
                    token_end(source, list_cursor)
                );
                int64_t element_cursor = skip_trivia(
                    source,
                    token_end(source, bracket_cursor)
                );
                if (
                    strcmp(token_kind(source, name_cursor), "identifier") == 0 &&
                    token_equal(source, colon_cursor, ":") &&
                    token_equal(source, list_cursor, "List") &&
                    token_equal(source, bracket_cursor, "[") &&
                    strcmp(token_kind(source, element_cursor), "identifier") == 0
                ) {
                    ++borrowed_lists;
                    if (borrowed_lists > 1) {
                        free(element_type);
                        free(borrowed_name);
                        return owned_text(
                            "error[E2S21]: ownership slice supports one "
                            "borrowed List parameter per function"
                        );
                    }
                    free(borrowed_name);
                    free(element_type);
                    borrowed_name = token_copy(source, name_cursor);
                    element_type = token_copy(source, element_cursor);
                }
            }
            parameter_cursor = skip_trivia(
                source,
                token_end(source, parameter_cursor)
            );
        }

        int64_t function_end_cursor = function_end(source, function_cursor);
        if (function_end_cursor < 0) {
            free(element_type);
            free(borrowed_name);
            return owned_text("error[E2S03]: malformed function body");
        }
        int64_t body_open = skip_trivia(source, parameters_end);
        while (
            body_open < function_end_cursor &&
            !token_equal(source, body_open, "{")
        ) {
            body_open = skip_trivia(source, token_end(source, body_open));
        }
        if (body_open >= function_end_cursor) {
            free(element_type);
            free(borrowed_name);
            return owned_text("error[E2S03]: malformed function body");
        }

        int64_t cursor = skip_trivia(source, token_end(source, body_open));
        while (cursor < function_end_cursor) {
            if (token_equal(source, cursor, "for")) {
                int64_t element_cursor = skip_trivia(
                    source,
                    token_end(source, cursor)
                );
                int64_t in_cursor = skip_trivia(
                    source,
                    token_end(source, element_cursor)
                );
                int64_t collection_cursor = skip_trivia(
                    source,
                    token_end(source, in_cursor)
                );
                int64_t loop_open = skip_trivia(
                    source,
                    token_end(source, collection_cursor)
                );
                if (
                    strcmp(token_kind(source, element_cursor), "identifier") == 0 &&
                    token_equal(source, in_cursor, "in") &&
                    strcmp(token_kind(source, collection_cursor), "identifier") == 0 &&
                    token_equal(source, loop_open, "{")
                ) {
                    int64_t loop_end = balanced_end(
                        source,
                        loop_open,
                        "{",
                        "}"
                    );
                    if (loop_end < 0) {
                        free(element_type);
                        free(borrowed_name);
                        return owned_text("error[E2S03]: malformed for body");
                    }
                    if (
                        borrowed_name[0] != '\0' &&
                        token_equal(source, collection_cursor, borrowed_name)
                    ) {
                        ++recognized_loops;
                        char *element_name = token_copy(source, element_cursor);
                        int64_t move_at = return_move_at(
                            source,
                            loop_open,
                            loop_end,
                            element_name
                        );
                        if (move_at >= 0 && !copy_type(element_type)) {
                            Buffer error;
                            buffer_init(&error);
                            buffer_format(
                                &error,
                                "error[E007]: cannot move non-Copy element "
                                "`%s: %s` out of borrowed collection `%s` "
                                "at line %" PRId64 "; return a Copy scalar "
                                "or clone the element",
                                element_name,
                                element_type,
                                borrowed_name,
                                line_at(source, move_at)
                            );
                            free(element_name);
                            free(element_type);
                            free(borrowed_name);
                            return error.data;
                        }
                        free(element_name);
                    }
                }
            }
            cursor = skip_trivia(source, token_end(source, cursor));
        }
        free(element_type);
        free(borrowed_name);
        function_cursor = next_function_start(source, function_end_cursor);
    }
    if (recognized_loops == 0) {
        return owned_text(
            "error[E2S20]: Stage 2 ownership slice requires "
            "`for element in read_list`"
        );
    }
    return owned_text("ok");
}

static int64_t expression_end(const char *source, int64_t start);
static char *emit_expression(
    const char *source,
    const char *hir,
    int64_t start,
    int64_t end
);
static char *hir_use_binding_id(const char *hir, int64_t use_start);
static char *hir_definition_id_at(
    const char *hir,
    int64_t declaration_start
);
static char *hir_binding_field(
    const char *hir,
    const char *binding_id,
    int field
);

static int64_t primary_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, start);
    if (cursor >= length) return -1;
    const char *kind = token_kind(source, cursor);
    if (strcmp(kind, "integer") == 0) {
        return token_end(source, cursor);
    }
    if (strcmp(kind, "identifier") == 0) {
        int64_t open = skip_trivia(source, token_end(source, cursor));
        if (open >= length || !token_equal(source, open, "(")) {
            return token_end(source, cursor);
        }
        int64_t argument = skip_trivia(source, token_end(source, open));
        if (argument < length && token_equal(source, argument, ")")) {
            return token_end(source, argument);
        }
        while (argument < length) {
            int64_t argument_end = expression_end(source, argument);
            if (argument_end < 0) return -1;
            int64_t separator = skip_trivia(source, argument_end);
            if (separator < length && token_equal(source, separator, ")")) {
                return token_end(source, separator);
            }
            if (separator >= length || !token_equal(source, separator, ",")) {
                return -1;
            }
            argument = skip_trivia(source, token_end(source, separator));
        }
        return -1;
    }
    if (token_equal(source, cursor, "(")) {
        int64_t value_start = skip_trivia(source, token_end(source, cursor));
        int64_t value_end = expression_end(source, value_start);
        if (value_end < 0) return -1;
        int64_t close = skip_trivia(source, value_end);
        if (close >= length || !token_equal(source, close, ")")) return -1;
        return token_end(source, close);
    }
    return -1;
}

static int64_t unary_end(const char *source, int64_t start) {
    int64_t cursor = skip_trivia(source, start);
    if (token_equal(source, cursor, "+") || token_equal(source, cursor, "-")) {
        return unary_end(source, skip_trivia(source, token_end(source, cursor)));
    }
    return primary_end(source, cursor);
}

static int64_t product_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = unary_end(source, start);
    if (cursor < 0) return -1;
    int64_t operator_start = skip_trivia(source, cursor);
    while (
        operator_start < length &&
        (token_equal(source, operator_start, "*") ||
         token_equal(source, operator_start, "//") ||
         token_equal(source, operator_start, "%"))
    ) {
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        cursor = unary_end(source, right_start);
        if (cursor < 0) return -1;
        operator_start = skip_trivia(source, cursor);
    }
    return cursor;
}

static int64_t expression_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = product_end(source, start);
    if (cursor < 0) return -1;
    int64_t operator_start = skip_trivia(source, cursor);
    while (
        operator_start < length &&
        (token_equal(source, operator_start, "+") ||
         token_equal(source, operator_start, "-"))
    ) {
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        cursor = product_end(source, right_start);
        if (cursor < 0) return -1;
        operator_start = skip_trivia(source, cursor);
    }
    return cursor;
}

static char *source_slice(const char *source, int64_t start, int64_t end) {
    if (end < start) end = start;
    size_t length = (size_t)(end - start);
    char *value = allocate(length + 1);
    memcpy(value, source + start, length);
    value[length] = '\0';
    return value;
}

static char *c_identifier_name(const char *identifier) {
    bool ascii = true;
    for (size_t index = 0; identifier[index] != '\0'; ++index) {
        if ((unsigned char)identifier[index] >= UINT8_C(0x80)) {
            ascii = false;
            break;
        }
    }
    if (ascii) return owned_text(identifier);

    Buffer output;
    buffer_init(&output);
    buffer_append(&output, "k");
    size_t length = strlen(identifier);
    size_t cursor = 0;
    while (cursor < length) {
        uint32_t codepoint = 0;
        size_t width = 0;
        if (!kofun_unicode_decode(
                (const uint8_t *)identifier,
                length,
                cursor,
                &codepoint,
                &width)) {
            free(output.data);
            return owned_text("k_invalid");
        }
        buffer_format(&output, "_u%06" PRIX32, codepoint);
        cursor += width;
    }
    return output.data;
}

static char *format_two(const char *name, const char *left, const char *right) {
    Buffer output;
    buffer_init(&output);
    buffer_format(&output, "%s(%s, %s)", name, left, right);
    return output.data;
}

static char *emit_primary(
    const char *source,
    const char *hir,
    int64_t start,
    int64_t end
) {
    int64_t cursor = skip_trivia(source, start);
    const char *kind = token_kind(source, cursor);
    if (strcmp(kind, "integer") == 0) {
        char *literal = source_slice(source, cursor, end);
        Buffer output;
        buffer_init(&output);
        buffer_append(&output, "INT64_C(");
        for (size_t index = 0; literal[index] != '\0'; ++index) {
            if (literal[index] != '_') {
                char symbol[2] = {literal[index], '\0'};
                buffer_append(&output, symbol);
            }
        }
        buffer_append(&output, ")");
        free(literal);
        return output.data;
    }
    if (strcmp(kind, "identifier") == 0) {
        char *name = token_copy(source, cursor);
        int64_t open = skip_trivia(source, token_end(source, cursor));
        Buffer output;
        buffer_init(&output);
        if (open >= end || !token_equal(source, open, "(")) {
            char *binding_id = hir_use_binding_id(hir, cursor);
            buffer_format(&output, "k_b%s", binding_id);
            free(binding_id);
            free(name);
            return output.data;
        }
        char *c_name = c_identifier_name(name);
        buffer_format(&output, "kofun_fn_%s(", c_name);
        free(c_name);
        int64_t argument = skip_trivia(source, token_end(source, open));
        int64_t arguments = 0;
        while (argument < end && !token_equal(source, argument, ")")) {
            int64_t argument_end = expression_end(source, argument);
            char *value = emit_expression(
                source,
                hir,
                argument,
                argument_end
            );
            if (arguments > 0) buffer_append(&output, ", ");
            buffer_append(&output, value);
            free(value);
            ++arguments;
            int64_t separator = skip_trivia(source, argument_end);
            if (separator < end && token_equal(source, separator, ",")) {
                argument = skip_trivia(source, token_end(source, separator));
            } else {
                argument = separator;
            }
        }
        buffer_append(&output, ")");
        free(name);
        return output.data;
    }
    if (token_equal(source, cursor, "(")) {
        int64_t value_start = skip_trivia(source, token_end(source, cursor));
        int64_t close = skip_trivia(source, expression_end(source, value_start));
        char *value = emit_expression(source, hir, value_start, close);
        Buffer output;
        buffer_init(&output);
        buffer_format(&output, "(%s)", value);
        free(value);
        return output.data;
    }
    char *empty = allocate(1);
    empty[0] = '\0';
    return empty;
}

static char *emit_unary(
    const char *source,
    const char *hir,
    int64_t start,
    int64_t end
) {
    int64_t cursor = skip_trivia(source, start);
    if (token_equal(source, cursor, "+")) {
        int64_t value_start = skip_trivia(source, token_end(source, cursor));
        return emit_unary(source, hir, value_start, end);
    }
    if (token_equal(source, cursor, "-")) {
        int64_t value_start = skip_trivia(source, token_end(source, cursor));
        char *value = emit_unary(source, hir, value_start, end);
        Buffer output;
        buffer_init(&output);
        buffer_format(&output, "kofun_neg(%s)", value);
        free(value);
        return output.data;
    }
    return emit_primary(source, hir, cursor, end);
}

static char *emit_product(
    const char *source,
    const char *hir,
    int64_t start,
    int64_t end
) {
    int64_t cursor = unary_end(source, start);
    char *emitted = emit_unary(source, hir, start, cursor);
    int64_t operator_start = skip_trivia(source, cursor);
    while (operator_start < end) {
        char *operator_text = token_copy(source, operator_start);
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        int64_t right_end = unary_end(source, right_start);
        char *right = emit_unary(source, hir, right_start, right_end);
        char *combined = emitted;
        if (strcmp(operator_text, "*") == 0) {
            combined = format_two("kofun_mul", emitted, right);
        } else if (strcmp(operator_text, "//") == 0) {
            combined = format_two("kofun_floor_div", emitted, right);
        } else if (strcmp(operator_text, "%") == 0) {
            combined = format_two("kofun_floor_mod", emitted, right);
        }
        if (combined != emitted) free(emitted);
        free(right);
        free(operator_text);
        emitted = combined;
        cursor = right_end;
        operator_start = skip_trivia(source, cursor);
    }
    return emitted;
}

static char *emit_expression(
    const char *source,
    const char *hir,
    int64_t start,
    int64_t end
) {
    int64_t cursor = product_end(source, start);
    char *emitted = emit_product(source, hir, start, cursor);
    int64_t operator_start = skip_trivia(source, cursor);
    while (operator_start < end) {
        char *operator_text = token_copy(source, operator_start);
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        int64_t right_end = product_end(source, right_start);
        char *right = emit_product(source, hir, right_start, right_end);
        char *combined = emitted;
        if (strcmp(operator_text, "+") == 0) {
            combined = format_two("kofun_add", emitted, right);
        } else if (strcmp(operator_text, "-") == 0) {
            combined = format_two("kofun_sub", emitted, right);
        }
        if (combined != emitted) free(emitted);
        free(right);
        free(operator_text);
        emitted = combined;
        cursor = right_end;
        operator_start = skip_trivia(source, cursor);
    }
    return emitted;
}

static char *lower_error(
    const char *code,
    const char *message,
    int64_t cursor
);

static int64_t function_arity(const char *source, const char *wanted) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = next_function_start(source, 0);
    int64_t found = -1;
    while (cursor < length) {
        char *name = function_name(source, cursor);
        if (strcmp(name, wanted) == 0) {
            if (found >= 0) {
                free(name);
                return -2;
            }
            found = parameter_count(source, cursor);
        }
        free(name);
        cursor = next_function_start(source, function_end(source, cursor));
    }
    return found;
}

static int64_t call_arity(const char *source, int64_t open) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, token_end(source, open));
    if (cursor < length && token_equal(source, cursor, ")")) return 0;
    int64_t arity = 0;
    while (cursor < length) {
        int64_t argument_end = expression_end(source, cursor);
        /* Text-literal arguments are single tokens outside the bounded
         * arithmetic expression grammar. */
        if (argument_end < 0) argument_end = token_end(source, cursor);
        ++arity;
        int64_t separator = skip_trivia(source, argument_end);
        if (separator < length && token_equal(source, separator, ")")) {
            return arity;
        }
        if (separator >= length || !token_equal(source, separator, ",")) {
            return -1;
        }
        cursor = skip_trivia(source, token_end(source, separator));
    }
    return -1;
}

/*
 * The 16 host builtins of the frozen self-host profile (#618/#619), keyed by
 * arity. `print` stays a statement-level special case. `len` is one name here;
 * its Text/List[Text] overload is resolved by type, not arity. Builtin calls
 * are known and arity-checked, but the bounded Int C11 slice cannot lower
 * them yet, so accepted uses classify as unsupported lowering, never as an
 * unknown-function source error.
 */
static int64_t builtin_arity(const char *name) {
    static const struct {
        const char *name;
        int64_t arity;
    } builtins[] = {
        {"args", 0},
        {"chars", 1},
        {"contains", 2},
        {"find", 2},
        {"is_digit", 1},
        {"is_space", 1},
        {"is_xid_continue", 1},
        {"len", 1},
        {"read_text", 1},
        {"replace", 3},
        {"starts_with", 2},
        {"text_slice", 3},
        {"trim", 1},
        {"validate_unicode_source", 1},
        {"write_text", 2},
    };
    size_t count = sizeof(builtins) / sizeof(builtins[0]);
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(name, builtins[index].name) == 0) {
            return builtins[index].arity;
        }
    }
    return -1;
}

static char *initializer_type(
    const char *source,
    const char *hir,
    int64_t function_open,
    int64_t initializer
);
static bool value_control(const char *source, int64_t cursor);
static char *function_return_type(const char *source, const char *wanted);

static bool newline_between(
    const char *source,
    int64_t start,
    int64_t end
) {
    for (int64_t at = start; at < end; ++at) {
        if (source[at] == '\n') return true;
    }
    return false;
}

/*
 * Parameter types of the profile builtins, `|`-separated in order.
 * `len` accepts either Text or List (its only overload); every other
 * signature is exact.
 */
static const char *builtin_parameter_types(const char *name) {
    static const struct {
        const char *name;
        const char *parameters;
    } builtins[] = {
        {"args", ""},
        {"chars", "Text"},
        {"contains", "Text|Text"},
        {"find", "Text|Text"},
        {"is_digit", "Text"},
        {"is_space", "Text"},
        {"is_xid_continue", "Text"},
        {"len", "TextOrList"},
        {"read_text", "Text"},
        {"replace", "Text|Text|Text"},
        {"starts_with", "Text|Text"},
        {"text_slice", "Text|Int|Int"},
        {"trim", "Text"},
        {"validate_unicode_source", "Text"},
        {"write_text", "Text|Text"},
    };
    size_t count = sizeof(builtins) / sizeof(builtins[0]);
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(name, builtins[index].name) == 0) {
            return builtins[index].parameters;
        }
    }
    return NULL;
}

/* Body `{` of the function declaration that contains `position`. */
static int64_t enclosing_function_open(
    const char *source,
    int64_t position
) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = next_function_start(source, 0);
    while (cursor < length) {
        int64_t close = function_end(source, cursor);
        if (cursor <= position && position < close) {
            int64_t parameters = parameter_open(source, cursor);
            if (parameters < 0) return -1;
            int64_t parameters_close = balanced_end(
                source,
                parameters,
                "(",
                ")"
            );
            if (parameters_close < 0) return -1;
            int64_t open = skip_trivia(source, parameters_close);
            while (open < close && !token_equal(source, open, "{")) {
                open = skip_trivia(source, token_end(source, open));
            }
            return open < close ? open : -1;
        }
        cursor = next_function_start(source, close);
    }
    return -1;
}

/*
 * Check one builtin call's argument types against its frozen signature.
 * Arguments whose bounded type cannot be established (value-control
 * initializers) are skipped rather than rejected. Returns an owned
 * error string or empty text.
 */
static char *builtin_argument_check(
    const char *source,
    const char *hir,
    const char *name,
    int64_t call_name,
    int64_t open
) {
    const char *parameters = builtin_parameter_types(name);
    if (parameters == NULL) return owned_text("");
    int64_t function_open = enclosing_function_open(source, call_name);
    if (function_open < 0) return owned_text("");
    int64_t length = (int64_t)strlen(source);
    int64_t argument = skip_trivia(source, token_end(source, open));
    const char *expected = parameters;
    int64_t index = 1;
    while (
        argument < length &&
        !token_equal(source, argument, ")") &&
        expected[0] != '\0'
    ) {
        size_t expected_length = strcspn(expected, "|");
        if (!value_control(source, argument)) {
            char *actual = initializer_type(
                source,
                hir,
                function_open,
                argument
            );
            bool matches;
            if (strncmp(expected, "TextOrList", expected_length) == 0) {
                matches = strcmp(actual, "Text") == 0 ||
                    strcmp(actual, "List") == 0;
            } else {
                matches =
                    strlen(actual) == expected_length &&
                    strncmp(actual, expected, expected_length) == 0;
            }
            if (!matches) {
                Buffer error;
                buffer_init(&error);
                buffer_format(
                    &error,
                    "error[E2S15]: builtin `%s` expects %.*s for "
                    "argument %" PRId64 ", got %s at byte %" PRId64,
                    name,
                    (int)expected_length,
                    expected,
                    index,
                    actual,
                    argument
                );
                free(actual);
                return error.data;
            }
            free(actual);
        }
        int64_t argument_end = expression_end(source, argument);
        /* Text-literal arguments are single tokens outside the bounded
         * arithmetic expression grammar. */
        if (argument_end < 0) argument_end = token_end(source, argument);
        int64_t separator = skip_trivia(source, argument_end);
        if (separator >= length || !token_equal(source, separator, ",")) {
            break;
        }
        argument = skip_trivia(source, token_end(source, separator));
        expected += expected_length;
        if (expected[0] == '|') ++expected;
        ++index;
    }
    return owned_text("");
}

/*
 * Bounded condition and return typing for the whole profile surface,
 * ordered before the unsupported-lowering classification so the frozen
 * self-host source is fully checked. Statement `if`/`while` conditions
 * must not be confidently non-Bool (the E2S23 message shape is reused
 * byte for byte for `if`); value returns must not confidently mismatch
 * the declared result type. Match guards, value-position `if`, and
 * value-control operands are skipped rather than guessed.
 */
static char *validate_core_types(const char *source, const char *hir) {
    int64_t length = (int64_t)strlen(source);
    int64_t function_start = next_function_start(source, 0);
    while (function_start < length) {
        int64_t function_close = function_end(source, function_start);
        char *name = function_name(source, function_start);
        char *declared = function_return_type(source, name);
        int64_t function_open = enclosing_function_open(
            source,
            function_start < function_close ?
                function_close - 1 : function_start
        );
        if (function_open < 0) {
            free(name);
            free(declared);
            function_start = next_function_start(source, function_close);
            continue;
        }
        int64_t previous_start = function_open;
        int64_t cursor = skip_trivia(
            source,
            token_end(source, function_open)
        );
        while (cursor < function_close) {
            bool statement_context =
                token_equal(source, previous_start, "{") ||
                token_equal(source, previous_start, "}") ||
                token_equal(source, previous_start, "else") ||
                newline_between(
                    source,
                    token_end(source, previous_start),
                    cursor
                );
            if (
                (token_equal(source, cursor, "if") && statement_context) ||
                (token_equal(source, cursor, "while") && statement_context)
            ) {
                int64_t condition = skip_trivia(
                    source,
                    token_end(source, cursor)
                );
                if (condition < function_close) {
                    char *condition_type = initializer_type(
                        source,
                        hir,
                        function_open,
                        condition
                    );
                    bool wrong =
                        strcmp(condition_type, "Int") == 0 ||
                        strcmp(condition_type, "Text") == 0 ||
                        strcmp(condition_type, "List") == 0;
                    free(condition_type);
                    if (wrong) {
                        Buffer error;
                        buffer_init(&error);
                        if (token_equal(source, cursor, "if")) {
                            buffer_format(
                                &error,
                                "error[E2S23]: if condition must be Bool "
                                "or an Int comparison at byte %" PRId64,
                                condition
                            );
                        } else {
                            buffer_format(
                                &error,
                                "error[E2S23]: while condition must be "
                                "Bool at byte %" PRId64,
                                condition
                            );
                        }
                        free(name);
                        free(declared);
                        return error.data;
                    }
                }
            }
            if (
                token_equal(source, cursor, "return") &&
                statement_context &&
                declared[0] != '\0' &&
                strcmp(declared, "Void") != 0
            ) {
                int64_t value = skip_trivia(
                    source,
                    token_end(source, cursor)
                );
                bool bare =
                    value >= function_close ||
                    token_equal(source, value, "}") ||
                    newline_between(
                        source,
                        token_end(source, cursor),
                        value
                    );
                if (!bare && !value_control(source, value)) {
                    char *value_type = initializer_type(
                        source,
                        hir,
                        function_open,
                        value
                    );
                    bool known =
                        strcmp(value_type, "Int") == 0 ||
                        strcmp(value_type, "Bool") == 0 ||
                        strcmp(value_type, "Text") == 0 ||
                        strcmp(value_type, "List") == 0;
                    if (known && strcmp(value_type, declared) != 0) {
                        Buffer error;
                        buffer_init(&error);
                        buffer_format(
                            &error,
                            "error[E2S15]: Core function `%s` returns %s, "
                            "expected %s at byte %" PRId64,
                            name,
                            value_type,
                            declared,
                            value
                        );
                        free(value_type);
                        free(name);
                        free(declared);
                        return error.data;
                    }
                    free(value_type);
                }
            }
            previous_start = cursor;
            cursor = skip_trivia(source, token_end(source, cursor));
        }
        free(name);
        free(declared);
        function_start = next_function_start(source, function_close);
    }
    return owned_text("ok");
}

static char *validate_core_calls(const char *source, const char *hir) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = next_function_start(source, 0);
    char *previous = owned_text("");
    while (cursor < length) {
        if (strcmp(token_kind(source, cursor), "identifier") == 0) {
            char *name = token_copy(source, cursor);
            int64_t open = skip_trivia(source, token_end(source, cursor));
            if (
                strcmp(previous, "fn") != 0 &&
                strcmp(name, "print") != 0 &&
                open < length &&
                token_equal(source, open, "(")
            ) {
                int64_t expected = function_arity(source, name);
                if (expected == -2) {
                    Buffer error;
                    buffer_init(&error);
                    buffer_format(
                        &error,
                        "error[E2S16]: duplicate Core function `%s` "
                        "at byte %" PRId64,
                        name,
                        cursor
                    );
                    free(name);
                    free(previous);
                    return error.data;
                }
                if (expected < 0) {
                    int64_t builtin_expected = builtin_arity(name);
                    if (builtin_expected < 0) {
                        Buffer error;
                        buffer_init(&error);
                        buffer_format(
                            &error,
                            "error[E2S16]: unknown Core function `%s` "
                            "at byte %" PRId64,
                            name,
                            cursor
                        );
                        free(name);
                        free(previous);
                        return error.data;
                    }
                    int64_t builtin_actual = call_arity(source, open);
                    if (builtin_actual != builtin_expected) {
                        Buffer error;
                        buffer_init(&error);
                        buffer_format(
                            &error,
                            "error[E2S17]: Core function `%s` expects %" PRId64
                            " arguments, got %" PRId64 " at byte %" PRId64,
                            name,
                            builtin_expected,
                            builtin_actual,
                            cursor
                        );
                        free(name);
                        free(previous);
                        return error.data;
                    }
                    char *argument_error = builtin_argument_check(
                        source,
                        hir,
                        name,
                        cursor,
                        open
                    );
                    if (argument_error[0] != '\0') {
                        free(name);
                        free(previous);
                        return argument_error;
                    }
                    free(argument_error);
                    Buffer error;
                    buffer_init(&error);
                    buffer_format(
                        &error,
                        "error[E2S10]: unsupported Core builtin call `%s` "
                        "at byte %" PRId64,
                        name,
                        cursor
                    );
                    free(name);
                    free(previous);
                    return error.data;
                }
                int64_t actual = call_arity(source, open);
                if (actual != expected) {
                    Buffer error;
                    buffer_init(&error);
                    buffer_format(
                        &error,
                        "error[E2S17]: Core function `%s` expects %" PRId64
                        " arguments, got %" PRId64 " at byte %" PRId64,
                        name,
                        expected,
                        actual,
                        cursor
                    );
                    free(name);
                    free(previous);
                    return error.data;
                }
            }
            free(name);
        }
        free(previous);
        previous = token_copy(source, cursor);
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    free(previous);
    return owned_text("ok");
}

static char *core_parameters(
    const char *source,
    const char *hir,
    int64_t function_start
) {
    int64_t parameters = parameter_open(source, function_start);
    if (parameters < 0) {
        return owned_text("error[E2S15]: malformed Core parameter list");
    }
    int64_t parameters_end = balanced_end(source, parameters, "(", ")");
    if (parameters_end < 0) {
        return owned_text("error[E2S15]: malformed Core parameter list");
    }
    int64_t cursor = skip_trivia(source, token_end(source, parameters));
    Buffer emitted;
    buffer_init(&emitted);
    int64_t count = 0;
    while (cursor < parameters_end && !token_equal(source, cursor, ")")) {
        if (strcmp(token_kind(source, cursor), "identifier") != 0) {
            free(emitted.data);
            return lower_error(
                "E2S15",
                "expected Core parameter name",
                cursor
            );
        }
        char *name = token_copy(source, cursor);
        int64_t colon = skip_trivia(source, token_end(source, cursor));
        int64_t type_cursor = skip_trivia(source, token_end(source, colon));
        if (
            colon >= parameters_end ||
            !token_equal(source, colon, ":") ||
            type_cursor >= parameters_end ||
            !token_equal(source, type_cursor, "Int")
        ) {
            free(name);
            free(emitted.data);
            return lower_error(
                "E2S15",
                "Core parameters must have type Int",
                cursor
            );
        }
        if (count > 0) buffer_append(&emitted, ", ");
        char *binding_id = hir_definition_id_at(hir, cursor);
        buffer_format(&emitted, "int64_t k_b%s", binding_id);
        free(binding_id);
        free(name);
        ++count;
        int64_t separator = skip_trivia(source, token_end(source, type_cursor));
        if (separator < parameters_end && token_equal(source, separator, ",")) {
            cursor = skip_trivia(source, token_end(source, separator));
        } else {
            cursor = separator;
        }
    }
    return emitted.data;
}

static bool comparison_operator(const char *source, int64_t cursor) {
    return token_equal(source, cursor, "==") ||
           token_equal(source, cursor, "!=") ||
           token_equal(source, cursor, "<") ||
           token_equal(source, cursor, "<=") ||
           token_equal(source, cursor, ">") ||
           token_equal(source, cursor, ">=");
}

static int64_t condition_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, start);
    if (
        token_equal(source, cursor, "true") ||
        token_equal(source, cursor, "false")
    ) {
        return token_end(source, cursor);
    }
    int64_t left_end = expression_end(source, cursor);
    if (left_end < 0) return -1;
    int64_t operator_start = skip_trivia(source, left_end);
    if (
        operator_start >= length ||
        !comparison_operator(source, operator_start)
    ) {
        return -1;
    }
    int64_t right_start = skip_trivia(
        source,
        token_end(source, operator_start)
    );
    return expression_end(source, right_start);
}

static char *emit_condition_into(
    const char *source,
    const char *hir,
    int64_t start,
    int64_t end,
    const char *target,
    const char *failure_result,
    const char *indent
) {
    int64_t cursor = skip_trivia(source, start);
    if (
        token_equal(source, cursor, "true") ||
        token_equal(source, cursor, "false")
    ) {
        char *literal = token_copy(source, cursor);
        Buffer output;
        buffer_init(&output);
        buffer_format(
            &output,
            "%sbool %s = %s;\n",
            indent,
            target,
            literal
        );
        free(literal);
        return output.data;
    }
    int64_t left_end = expression_end(source, cursor);
    int64_t operator_start = skip_trivia(source, left_end);
    int64_t right_start = skip_trivia(
        source,
        token_end(source, operator_start)
    );
    char *left = emit_expression(source, hir, cursor, left_end);
    char *operator_text = token_copy(source, operator_start);
    char *right = emit_expression(source, hir, right_start, end);
    Buffer output;
    buffer_init(&output);
    buffer_format(
        &output,
        "%sint64_t kofun_condition_left = %s;\n"
        "%sif (kofun_failed) return %s;\n"
        "%sint64_t kofun_condition_right = %s;\n"
        "%sif (kofun_failed) return %s;\n"
        "%sbool %s = kofun_condition_left %s kofun_condition_right;\n",
        indent,
        left,
        indent,
        failure_result,
        indent,
        right,
        indent,
        failure_result,
        indent,
        target,
        operator_text
    );
    free(left);
    free(operator_text);
    free(right);
    return output.data;
}

typedef struct {
    int64_t condition_start;
    int64_t condition_end;
    int64_t then_start;
    int64_t then_end;
    int64_t else_start;
    int64_t else_end;
    int64_t end;
} ValueIfParts;

typedef struct {
    int64_t value_start;
    int64_t value_end;
    int64_t arms_open;
    int64_t end;
} ValueMatchParts;

static char *parse_value_if(
    const char *source,
    int64_t start,
    ValueIfParts *parts
);

static char *parse_value_match(
    const char *source,
    int64_t start,
    ValueMatchParts *parts
);

static char *value_if_branch_end(
    const char *source,
    int64_t start,
    int64_t *end
) {
    int64_t cursor = skip_trivia(source, start);
    if (token_equal(source, cursor, "print")) {
        return lower_error(
            "E2S28",
            "value-position if branch must produce Int, not Void",
            cursor
        );
    }
    if (token_equal(source, cursor, "if")) {
        ValueIfParts nested;
        char *result = parse_value_if(source, cursor, &nested);
        if (strncmp(result, "error[", 6) == 0) return result;
        free(result);
        *end = nested.end;
        return owned_text("ok");
    }
    if (token_equal(source, cursor, "match")) {
        ValueMatchParts nested;
        char *result = parse_value_match(source, cursor, &nested);
        if (strncmp(result, "error[", 6) == 0) return result;
        free(result);
        *end = nested.end;
        return owned_text("ok");
    }
    *end = expression_end(source, cursor);
    if (*end < 0) {
        return lower_error(
            "E2S28",
            "value-position if branch must produce Int",
            cursor
        );
    }
    return owned_text("ok");
}

static char *parse_value_if(
    const char *source,
    int64_t start,
    ValueIfParts *parts
) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, start);
    if (cursor >= length || !token_equal(source, cursor, "if")) {
        return lower_error(
            "E2S28",
            "expected value-position `if`",
            cursor
        );
    }

    parts->condition_start = skip_trivia(
        source,
        token_end(source, cursor)
    );
    parts->condition_end = condition_end(source, parts->condition_start);
    if (parts->condition_end < 0) {
        return lower_error(
            "E2S23",
            "if condition must be Bool or an Int comparison",
            parts->condition_start
        );
    }

    int64_t then_open = skip_trivia(source, parts->condition_end);
    if (then_open >= length || !token_equal(source, then_open, "{")) {
        return lower_error(
            "E2S18",
            "expected `{` after if condition",
            then_open
        );
    }
    parts->then_start = skip_trivia(
        source,
        token_end(source, then_open)
    );
    char *then_result = value_if_branch_end(
        source,
        parts->then_start,
        &parts->then_end
    );
    if (strncmp(then_result, "error[", 6) == 0) return then_result;
    free(then_result);
    int64_t then_close = skip_trivia(source, parts->then_end);
    if (then_close >= length || !token_equal(source, then_close, "}")) {
        return lower_error(
            "E2S28",
            "value-position if branch must contain one final Int expression",
            then_close
        );
    }

    int64_t else_keyword = skip_trivia(
        source,
        token_end(source, then_close)
    );
    if (
        else_keyword >= length ||
        !token_equal(source, else_keyword, "else")
    ) {
        return lower_error(
            "E2S27",
            "value-position if requires `else`",
            else_keyword
        );
    }
    int64_t else_open = skip_trivia(
        source,
        token_end(source, else_keyword)
    );
    if (else_open >= length || !token_equal(source, else_open, "{")) {
        return lower_error(
            "E2S18",
            "expected `{` after `else`",
            else_open
        );
    }
    parts->else_start = skip_trivia(
        source,
        token_end(source, else_open)
    );
    char *else_result = value_if_branch_end(
        source,
        parts->else_start,
        &parts->else_end
    );
    if (strncmp(else_result, "error[", 6) == 0) return else_result;
    free(else_result);
    int64_t else_close = skip_trivia(source, parts->else_end);
    if (else_close >= length || !token_equal(source, else_close, "}")) {
        return lower_error(
            "E2S28",
            "value-position if branch must contain one final Int expression",
            else_close
        );
    }
    parts->end = token_end(source, else_close);
    return owned_text("ok");
}

static char *parse_value_match(
    const char *source,
    int64_t start,
    ValueMatchParts *parts
) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, start);
    if (cursor >= length || !token_equal(source, cursor, "match")) {
        return lower_error(
            "E2S30",
            "expected value-position `match`",
            cursor
        );
    }

    parts->value_start = skip_trivia(source, token_end(source, cursor));
    parts->value_end = condition_end(source, parts->value_start);
    if (parts->value_end < 0) {
        return lower_error(
            "E2S24",
            "bounded match scrutinee must be Bool",
            parts->value_start
        );
    }
    parts->arms_open = skip_trivia(source, parts->value_end);
    if (
        parts->arms_open >= length ||
        !token_equal(source, parts->arms_open, "{")
    ) {
        return lower_error(
            "E2S24",
            "expected `{` after match scrutinee",
            parts->arms_open
        );
    }

    int64_t arm_cursor = skip_trivia(
        source,
        token_end(source, parts->arms_open)
    );
    bool covered_true = false;
    bool covered_false = false;
    bool seen_catchall = false;
    while (
        arm_cursor < length &&
        !token_equal(source, arm_cursor, "}")
    ) {
        int64_t pattern_start = arm_cursor;
        PatternSummary pattern = pattern_summary(source, pattern_start);
        bool pattern_true = pattern.kind == PATTERN_LITERAL &&
                            token_equal(source, pattern_start, "true");
        bool pattern_false = pattern.kind == PATTERN_LITERAL &&
                             token_equal(source, pattern_start, "false");
        bool pattern_catchall = pattern.kind == PATTERN_WILDCARD;
        if (seen_catchall) {
            return lower_error(
                "E2S26",
                "pattern after catch-all is unreachable",
                pattern_start
            );
        }
        if (pattern_true && covered_true) {
            return lower_error(
                "E2S26",
                "duplicate `true` pattern is unreachable",
                pattern_start
            );
        }
        if (pattern_false && covered_false) {
            return lower_error(
                "E2S26",
                "duplicate `false` pattern is unreachable",
                pattern_start
            );
        }
        if (pattern_catchall && covered_true && covered_false) {
            return lower_error(
                "E2S26",
                "catch-all pattern is unreachable",
                pattern_start
            );
        }
        if (!pattern_true && !pattern_false && !pattern_catchall) {
            return lower_error(
                "E2S24",
                "bounded Bool pattern must be `true`, `false`, or `_`",
                pattern_start
            );
        }

        int64_t after_pattern = skip_trivia(
            source,
            pattern.end
        );
        bool guarded = false;
        int64_t arrow = after_pattern;
        if (arrow < length && token_equal(source, arrow, "if")) {
            guarded = true;
            int64_t guard_start = skip_trivia(
                source,
                token_end(source, arrow)
            );
            int64_t guard_end = condition_end(source, guard_start);
            if (guard_end < 0) {
                return lower_error(
                    "E2S29",
                    "match guard must be Bool or an Int comparison",
                    guard_start
                );
            }
            arrow = skip_trivia(source, guard_end);
        }
        if (arrow >= length || !token_equal(source, arrow, "=>")) {
            return lower_error(
                "E2S24",
                "expected `=>` after Bool pattern",
                arrow
            );
        }
        int64_t arm_open = skip_trivia(
            source,
            token_end(source, arrow)
        );
        if (arm_open >= length || !token_equal(source, arm_open, "{")) {
            return lower_error(
                "E2S24",
                "bounded Bool match arm must use a block",
                arm_open
            );
        }

        int64_t arm_start = skip_trivia(
            source,
            token_end(source, arm_open)
        );
        int64_t arm_end = -1;
        if (token_equal(source, arm_start, "if")) {
            ValueIfParts nested;
            char *result = parse_value_if(source, arm_start, &nested);
            if (strncmp(result, "error[", 6) == 0) return result;
            free(result);
            arm_end = nested.end;
        } else if (token_equal(source, arm_start, "match")) {
            ValueMatchParts nested;
            char *result = parse_value_match(source, arm_start, &nested);
            if (strncmp(result, "error[", 6) == 0) return result;
            free(result);
            arm_end = nested.end;
        } else {
            if (token_equal(source, arm_start, "print")) {
                return lower_error(
                    "E2S30",
                    "value-position match arm must produce Int, not Void",
                    arm_start
                );
            }
            arm_end = expression_end(source, arm_start);
            if (arm_end < 0) {
                return lower_error(
                    "E2S30",
                    "value-position match arm must produce Int",
                    arm_start
                );
            }
        }
        int64_t arm_close = skip_trivia(source, arm_end);
        if (arm_close >= length || !token_equal(source, arm_close, "}")) {
            return lower_error(
                "E2S30",
                "value-position match arm must contain one final Int expression",
                arm_close
            );
        }

        if (!guarded) {
            if (pattern_true) {
                covered_true = true;
            } else if (pattern_false) {
                covered_false = true;
            } else {
                covered_true = true;
                covered_false = true;
                seen_catchall = true;
            }
        }
        arm_cursor = skip_trivia(source, token_end(source, arm_close));
        if (
            arm_cursor < length &&
            token_equal(source, arm_cursor, ",")
        ) {
            arm_cursor = skip_trivia(
                source,
                token_end(source, arm_cursor)
            );
        } else if (
            arm_cursor >= length ||
            !token_equal(source, arm_cursor, "}")
        ) {
            return lower_error(
                "E2S24",
                "expected `,` between match arms",
                arm_cursor
            );
        }
    }

    if (arm_cursor >= length || !token_equal(source, arm_cursor, "}")) {
        return lower_error(
            "E2S24",
            "missing `}` after match arms",
            parts->arms_open
        );
    }
    if (!covered_true && !covered_false) {
        return lower_error(
            "E2S25",
            "non-exhaustive Bool match; missing patterns `true`, `false`",
            cursor
        );
    }
    if (!covered_true) {
        return lower_error(
            "E2S25",
            "non-exhaustive Bool match; missing pattern `true`",
            cursor
        );
    }
    if (!covered_false) {
        return lower_error(
            "E2S25",
            "non-exhaustive Bool match; missing pattern `false`",
            cursor
        );
    }
    parts->end = token_end(source, arm_cursor);
    return owned_text("ok");
}

static bool value_control(const char *source, int64_t cursor) {
    return token_equal(source, cursor, "if") ||
           token_equal(source, cursor, "match");
}

static char *parse_value_control(
    const char *source,
    int64_t start,
    int64_t *end
) {
    int64_t cursor = skip_trivia(source, start);
    if (token_equal(source, cursor, "if")) {
        ValueIfParts parts;
        char *result = parse_value_if(source, cursor, &parts);
        if (strncmp(result, "error[", 6) == 0) return result;
        *end = parts.end;
        return result;
    }
    ValueMatchParts parts;
    char *result = parse_value_match(source, cursor, &parts);
    if (strncmp(result, "error[", 6) == 0) return result;
    *end = parts.end;
    return result;
}

static char *emit_value_match_into(
    const char *source,
    const char *hir,
    int64_t start,
    int64_t end,
    const char *target,
    const char *failure_result
);

static char *emit_value_into(
    const char *source,
    const char *hir,
    int64_t start,
    int64_t end,
    const char *target,
    const char *failure_result
) {
    int64_t cursor = skip_trivia(source, start);
    if (token_equal(source, cursor, "match")) {
        return emit_value_match_into(
            source,
            hir,
            cursor,
            end,
            target,
            failure_result
        );
    }
    if (!token_equal(source, cursor, "if")) {
        char *value = emit_expression(source, hir, cursor, end);
        Buffer emitted;
        buffer_init(&emitted);
        buffer_format(
            &emitted,
            "    %s = %s;\n"
            "    if (kofun_failed) return %s;\n",
            target,
            value,
            failure_result
        );
        free(value);
        return emitted.data;
    }

    ValueIfParts parts;
    char *result = parse_value_if(source, cursor, &parts);
    if (strncmp(result, "error[", 6) == 0) return result;
    free(result);
    char *condition = emit_condition_into(
        source,
        hir,
        parts.condition_start,
        parts.condition_end,
        "kofun_value_condition",
        failure_result,
        "        "
    );
    char *then_body = emit_value_into(
        source,
        hir,
        parts.then_start,
        parts.then_end,
        target,
        failure_result
    );
    if (strncmp(then_body, "error[", 6) == 0) {
        free(condition);
        return then_body;
    }
    char *else_body = emit_value_into(
        source,
        hir,
        parts.else_start,
        parts.else_end,
        target,
        failure_result
    );
    if (strncmp(else_body, "error[", 6) == 0) {
        free(condition);
        free(then_body);
        return else_body;
    }
    Buffer emitted;
    buffer_init(&emitted);
    buffer_format(
        &emitted,
        "    {\n"
        "%s"
        "        if (kofun_value_condition) {\n"
        "%s"
        "        } else {\n"
        "%s"
        "        }\n"
        "    }\n",
        condition,
        then_body,
        else_body
    );
    free(condition);
    free(then_body);
    free(else_body);
    return emitted.data;
}

static char *emit_value_match_into(
    const char *source,
    const char *hir,
    int64_t start,
    int64_t end,
    const char *target,
    const char *failure_result
) {
    ValueMatchParts parts;
    char *result = parse_value_match(source, start, &parts);
    if (strncmp(result, "error[", 6) == 0) return result;
    free(result);

    Buffer dispatch;
    buffer_init(&dispatch);
    int64_t arm_cursor = skip_trivia(
        source,
        token_end(source, parts.arms_open)
    );
    while (arm_cursor < end && !token_equal(source, arm_cursor, "}")) {
        PatternSummary pattern = pattern_summary(source, arm_cursor);
        bool pattern_true = pattern.kind == PATTERN_LITERAL &&
                            token_equal(source, arm_cursor, "true");
        bool pattern_false = pattern.kind == PATTERN_LITERAL &&
                             token_equal(source, arm_cursor, "false");
        int64_t arrow = skip_trivia(
            source,
            pattern.end
        );
        bool guarded = false;
        int64_t guard_start = -1;
        int64_t guard_end = -1;
        if (arrow < end && token_equal(source, arrow, "if")) {
            guarded = true;
            guard_start = skip_trivia(source, token_end(source, arrow));
            guard_end = condition_end(source, guard_start);
            arrow = skip_trivia(source, guard_end);
        }

        int64_t arm_open = skip_trivia(source, token_end(source, arrow));
        int64_t arm_start = skip_trivia(
            source,
            token_end(source, arm_open)
        );
        int64_t arm_end = -1;
        if (value_control(source, arm_start)) {
            char *arm_result = parse_value_control(
                source,
                arm_start,
                &arm_end
            );
            if (strncmp(arm_result, "error[", 6) == 0) {
                free(dispatch.data);
                return arm_result;
            }
            free(arm_result);
        } else {
            arm_end = expression_end(source, arm_start);
        }

        char *arm_body = emit_value_into(
            source,
            hir,
            arm_start,
            arm_end,
            target,
            failure_result
        );
        if (strncmp(arm_body, "error[", 6) == 0) {
            free(dispatch.data);
            return arm_body;
        }
        const char *pattern_condition = "true";
        if (pattern_true) {
            pattern_condition = "kofun_match_value";
        } else if (pattern_false) {
            pattern_condition = "!kofun_match_value";
        }

        if (guarded) {
            char *guard = emit_condition_into(
                source,
                hir,
                guard_start,
                guard_end,
                "kofun_match_guard",
                failure_result,
                "            "
            );
            buffer_format(
                &dispatch,
                "        if (!kofun_match_selected && %s) {\n"
                "%s"
                "            if (kofun_match_guard) {\n"
                "%s"
                "                kofun_match_selected = true;\n"
                "            }\n"
                "        }\n",
                pattern_condition,
                guard,
                arm_body
            );
            free(guard);
        } else {
            buffer_format(
                &dispatch,
                "        if (!kofun_match_selected && %s) {\n"
                "%s"
                "            kofun_match_selected = true;\n"
                "        }\n",
                pattern_condition,
                arm_body
            );
        }
        free(arm_body);

        int64_t arm_close = skip_trivia(source, arm_end);
        arm_cursor = skip_trivia(source, token_end(source, arm_close));
        if (arm_cursor < end && token_equal(source, arm_cursor, ",")) {
            arm_cursor = skip_trivia(
                source,
                token_end(source, arm_cursor)
            );
        }
    }

    char *match_value = emit_condition_into(
        source,
        hir,
        parts.value_start,
        parts.value_end,
        "kofun_match_value",
        failure_result,
        "        "
    );
    Buffer emitted;
    buffer_init(&emitted);
    buffer_format(
        &emitted,
        "    {\n"
        "%s"
        "        (void)kofun_match_value;\n"
        "        bool kofun_match_selected = false;\n"
        "%s"
        "    }\n",
        match_value,
        dispatch.data
    );
    free(match_value);
    free(dispatch.data);
    return emitted.data;
}

static int64_t core_body_open(
    const char *source,
    const char *hir,
    int64_t function_start,
    bool is_main
) {
    int64_t length = (int64_t)strlen(source);
    int64_t parameters = parameter_open(source, function_start);
    if (parameters < 0) return -1;
    int64_t parameters_end = balanced_end(source, parameters, "(", ")");
    if (parameters_end < 0) return -1;
    char *parameter_text = core_parameters(source, hir, function_start);
    bool parameters_valid = strncmp(parameter_text, "error[", 6) != 0;
    free(parameter_text);
    if (!parameters_valid) return -1;
    int64_t cursor = skip_trivia(source, parameters_end);
    if (cursor < length && token_equal(source, cursor, "->")) {
        cursor = skip_trivia(source, token_end(source, cursor));
        if (cursor >= length || !token_equal(source, cursor, "Int")) return -1;
        cursor = skip_trivia(source, token_end(source, cursor));
    } else if (!is_main) {
        return -1;
    }
    if (cursor >= length || !token_equal(source, cursor, "{")) return -1;
    return cursor;
}

static char *lower_error(const char *code, const char *message, int64_t cursor) {
    Buffer error;
    buffer_init(&error);
    if (cursor >= 0) {
        buffer_format(&error, "error[%s]: %s at byte %" PRId64, code, message, cursor);
    } else {
        buffer_format(&error, "error[%s]: %s", code, message);
    }
    return error.data;
}

static int64_t parent_block_open(
    const char *source,
    int64_t function_open,
    int64_t child_open
) {
    int64_t cursor = function_open;
    int64_t parent = -1;
    while (cursor < child_open) {
        if (token_equal(source, cursor, "{")) {
            int64_t candidate_end = balanced_end(source, cursor, "{", "}");
            if (candidate_end > child_open) parent = cursor;
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return parent;
}

static const char *scope_kind_for_open(
    const char *source,
    int64_t function_open,
    int64_t wanted_open
) {
    int64_t cursor = function_open;
    const char *previous = "";
    while (cursor < wanted_open) {
        if (token_equal(source, cursor, "if")) {
            int64_t condition_start = skip_trivia(
                source,
                token_end(source, cursor)
            );
            int64_t condition_close = condition_end(
                source,
                condition_start
            );
            if (
                condition_close >= 0 &&
                skip_trivia(source, condition_close) == wanted_open
            ) {
                return "if-then";
            }
        }
        if (token_equal(source, cursor, "else")) {
            previous = "else";
        } else if (token_equal(source, cursor, "=>")) {
            previous = "=>";
        } else {
            previous = "";
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    if (strcmp(previous, "else") == 0) return "if-else";
    if (strcmp(previous, "=>") == 0) return "match-arm";
    return "block";
}

static bool enum_declaration_syntax_token(
    const char *source,
    int64_t function_open,
    int64_t target
) {
    int64_t cursor = skip_trivia(source, token_end(source, function_open));
    while (cursor <= target) {
        if (token_equal(source, cursor, "let")) {
            int64_t name = skip_trivia(source, token_end(source, cursor));
            if (token_equal(source, name, "mut")) {
                name = skip_trivia(source, token_end(source, name));
            }
            if (name == target) return true;
            int64_t colon = skip_trivia(source, token_end(source, name));
            if (token_equal(source, colon, ":")) {
                int64_t type_cursor = skip_trivia(
                    source,
                    token_end(source, colon)
                );
                if (type_cursor == target) return true;
                int64_t equals = skip_trivia(
                    source,
                    token_end(source, type_cursor)
                );
                int64_t initializer = skip_trivia(
                    source,
                    token_end(source, equals)
                );
                if (
                    initializer == target &&
                    token_equal(source, equals, "=")
                ) {
                    char *enum_type = token_copy(source, type_cursor);
                    bool valid = enum_constructor_count(source, enum_type) >= 0;
                    free(enum_type);
                    if (valid) return true;
                }
            }
        }
        if (token_equal(source, cursor, "for")) {
            int64_t name = skip_trivia(source, token_end(source, cursor));
            if (name == target) return true;
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return false;
}

static bool enum_initializer_constructor_token(
    const char *source,
    int64_t function_open,
    int64_t target
) {
    int64_t cursor = skip_trivia(source, token_end(source, function_open));
    while (cursor <= target) {
        if (token_equal(source, cursor, "let")) {
            int64_t name = skip_trivia(source, token_end(source, cursor));
            if (token_equal(source, name, "mut")) {
                name = skip_trivia(source, token_end(source, name));
            }
            int64_t colon = skip_trivia(source, token_end(source, name));
            if (token_equal(source, colon, ":")) {
                int64_t type_cursor = skip_trivia(
                    source,
                    token_end(source, colon)
                );
                int64_t equals = skip_trivia(
                    source,
                    token_end(source, type_cursor)
                );
                int64_t initializer = skip_trivia(
                    source,
                    token_end(source, equals)
                );
                if (
                    initializer == target &&
                    token_equal(source, equals, "=")
                ) {
                    char *enum_type = token_copy(source, type_cursor);
                    char *constructor = token_copy(source, initializer);
                    char *owner = enum_constructor_owner(source, constructor);
                    bool valid =
                        enum_constructor_count(source, enum_type) >= 0 &&
                        owner[0] != '\0';
                    free(enum_type);
                    free(constructor);
                    free(owner);
                    return valid;
                }
            }
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return false;
}

static bool enum_match_pattern_token(
    const char *source,
    int64_t function_open,
    int64_t target
) {
    int64_t cursor = skip_trivia(source, token_end(source, function_open));
    while (cursor < target) {
        if (token_equal(source, cursor, "match")) {
            int64_t arms_open = pattern_match_open(source, cursor);
            if (
                arms_open >= 0 && arms_open < target &&
                token_equal(source, arms_open, "{")
            ) {
                int64_t match_end = balanced_end(
                    source,
                    arms_open,
                    "{",
                    "}"
                );
                int64_t match_close = match_end < 0 ? -1 : match_end - 1;
                int64_t arm_cursor = skip_trivia(
                    source,
                    token_end(source, arms_open)
                );
                while (
                    match_close >= 0 && arm_cursor <= target &&
                    arm_cursor < match_close &&
                    !token_equal(source, arm_cursor, "}")
                ) {
                    PatternSummary pattern = pattern_summary(
                        source,
                        arm_cursor
                    );
                    if (target >= arm_cursor && target < pattern.end) {
                        return true;
                    }
                    int64_t arrow = pattern_arm_arrow(
                        source,
                        pattern.end,
                        match_close
                    );
                    if (
                        arm_cursor <= target &&
                        arrow >= 0 &&
                        token_equal(source, arrow, "=>")
                    ) {
                        int64_t arm_open = skip_trivia(
                            source,
                            token_end(source, arrow)
                        );
                        int64_t arm_end = balanced_end(
                            source,
                            arm_open,
                            "{",
                            "}"
                        );
                        if (arm_end < 0) {
                            arm_cursor = target + 1;
                        } else if (arm_end <= target) {
                            arm_cursor = skip_trivia(source, arm_end);
                            if (token_equal(source, arm_cursor, ",")) {
                                arm_cursor = skip_trivia(
                                    source,
                                    token_end(source, arm_cursor)
                                );
                            }
                        } else {
                            arm_cursor = target + 1;
                        }
                    } else {
                        arm_cursor = target + 1;
                    }
                }
            }
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return false;
}

static int64_t text_find_from(
    const char *value,
    const char *wanted,
    int64_t start
) {
    int64_t value_length = (int64_t)strlen(value);
    int64_t wanted_length = (int64_t)strlen(wanted);
    if (wanted_length == 0) return start;
    for (
        int64_t cursor = start;
        cursor + wanted_length <= value_length;
        ++cursor
    ) {
        if (
            strncmp(
                value + cursor,
                wanted,
                (size_t)wanted_length
            ) == 0
        ) {
            return cursor;
        }
    }
    return -1;
}

static int64_t decimal_value(const char *value) {
    int64_t cursor = 0;
    int64_t sign = 1;
    int64_t length = (int64_t)strlen(value);
    if (length > 0 && value[0] == '-') {
        sign = -1;
        cursor = 1;
    }
    int64_t result = 0;
    while (cursor < length) {
        if (value[cursor] < '0' || value[cursor] > '9') return -1;
        result = result * 10 + (value[cursor] - '0');
        ++cursor;
    }
    return result * sign;
}

static int64_t hir_record_start(
    const char *hir,
    const char *kind,
    int64_t start
) {
    Buffer needle;
    buffer_init(&needle);
    buffer_format(&needle, "\n%s|", kind);
    int64_t found = text_find_from(hir, needle.data, start);
    free(needle.data);
    return found < 0 ? -1 : found + 1;
}

static char *hir_field(
    const char *hir,
    int64_t line_start,
    int wanted
) {
    int64_t length = (int64_t)strlen(hir);
    int64_t cursor = line_start;
    int field = 0;
    int64_t field_start = line_start;
    while (cursor < length) {
        if (hir[cursor] == '|' || hir[cursor] == '\n') {
            if (field == wanted) {
                size_t field_length = (size_t)(cursor - field_start);
                char *result = allocate(field_length + 1);
                memcpy(result, hir + field_start, field_length);
                result[field_length] = '\0';
                return result;
            }
            if (hir[cursor] == '\n') return owned_text("");
            ++field;
            field_start = cursor + 1;
        }
        ++cursor;
    }
    if (field == wanted) {
        size_t field_length = (size_t)(cursor - field_start);
        char *result = allocate(field_length + 1);
        memcpy(result, hir + field_start, field_length);
        result[field_length] = '\0';
        return result;
    }
    return owned_text("");
}

static char *hir_same_scope_declaration(
    const char *hir,
    const char *scope_id,
    const char *name
) {
    int64_t line = hir_record_start(hir, "binding", 0);
    while (line >= 0) {
        char *binding_scope = hir_field(hir, line, 2);
        char *binding_name = hir_field(hir, line, 3);
        bool found =
            strcmp(binding_scope, scope_id) == 0 &&
            strcmp(binding_name, name) == 0;
        free(binding_scope);
        free(binding_name);
        if (found) return hir_field(hir, line, 8);
        line = hir_record_start(hir, "binding", line + 1);
    }
    return owned_text("");
}

static char *hir_scope_id_for_open(const char *hir, int64_t open) {
    int64_t line = hir_record_start(hir, "scope", 0);
    while (line >= 0) {
        char *open_text = hir_field(hir, line, 4);
        bool found = decimal_value(open_text) == open;
        free(open_text);
        if (found) return hir_field(hir, line, 1);
        line = hir_record_start(hir, "scope", line + 1);
    }
    return owned_text("");
}

static char *hir_scope_field(
    const char *hir,
    const char *scope_id,
    int field
) {
    int64_t line = hir_record_start(hir, "scope", 0);
    while (line >= 0) {
        char *candidate = hir_field(hir, line, 1);
        bool found = strcmp(candidate, scope_id) == 0;
        free(candidate);
        if (found) return hir_field(hir, line, field);
        line = hir_record_start(hir, "scope", line + 1);
    }
    return owned_text("");
}

static char *hir_binding_field(
    const char *hir,
    const char *binding_id,
    int field
) {
    int64_t line = hir_record_start(hir, "binding", 0);
    while (line >= 0) {
        char *candidate = hir_field(hir, line, 1);
        bool found = strcmp(candidate, binding_id) == 0;
        free(candidate);
        if (found) return hir_field(hir, line, field);
        line = hir_record_start(hir, "binding", line + 1);
    }
    return owned_text("");
}

static char *hir_definition_id_at(
    const char *hir,
    int64_t declaration_start
) {
    int64_t line = hir_record_start(hir, "binding", 0);
    while (line >= 0) {
        char *start_text = hir_field(hir, line, 8);
        bool found = decimal_value(start_text) == declaration_start;
        free(start_text);
        if (found) return hir_field(hir, line, 1);
        line = hir_record_start(hir, "binding", line + 1);
    }
    return owned_text("");
}

static char *hir_use_binding_id(const char *hir, int64_t use_start) {
    int64_t line = hir_record_start(hir, "use", 0);
    while (line >= 0) {
        char *start_text = hir_field(hir, line, 1);
        bool found = decimal_value(start_text) == use_start;
        free(start_text);
        if (found) return hir_field(hir, line, 4);
        line = hir_record_start(hir, "use", line + 1);
    }
    return owned_text("");
}

static char *hir_resolve_binding(
    const char *hir,
    const char *current_scope,
    int64_t use_start,
    const char *name
) {
    char *scope_id = owned_text(current_scope);
    while (scope_id[0] != '\0' && strcmp(scope_id, "-1") != 0) {
        char *resolved = owned_text("");
        int64_t line = hir_record_start(hir, "binding", 0);
        while (line >= 0) {
            char *binding_scope = hir_field(hir, line, 2);
            char *binding_name = hir_field(hir, line, 3);
            char *visible_text = hir_field(hir, line, 10);
            bool matches =
                strcmp(binding_scope, scope_id) == 0 &&
                strcmp(binding_name, name) == 0 &&
                decimal_value(visible_text) <= use_start;
            free(binding_scope);
            free(binding_name);
            free(visible_text);
            if (matches) {
                free(resolved);
                resolved = hir_field(hir, line, 1);
            }
            line = hir_record_start(hir, "binding", line + 1);
        }
        if (resolved[0] != '\0') {
            free(scope_id);
            return resolved;
        }
        free(resolved);
        char *parent = hir_scope_field(hir, scope_id, 2);
        free(scope_id);
        scope_id = parent;
    }
    free(scope_id);
    return owned_text("");
}

static char *hir_pending_declaration(
    const char *hir,
    const char *current_scope,
    int64_t use_start,
    const char *name
) {
    char *scope_id = owned_text(current_scope);
    while (scope_id[0] != '\0' && strcmp(scope_id, "-1") != 0) {
        int64_t line = hir_record_start(hir, "binding", 0);
        while (line >= 0) {
            char *scope = hir_field(hir, line, 2);
            char *binding_name = hir_field(hir, line, 3);
            char *declaration_text = hir_field(hir, line, 8);
            char *visible_text = hir_field(hir, line, 10);
            int64_t declaration = decimal_value(declaration_text);
            int64_t visible = decimal_value(visible_text);
            bool found =
                strcmp(scope, scope_id) == 0 &&
                strcmp(binding_name, name) == 0 &&
                declaration < use_start && use_start < visible;
            free(scope);
            free(binding_name);
            free(visible_text);
            if (found) {
                free(scope_id);
                return declaration_text;
            }
            free(declaration_text);
            line = hir_record_start(hir, "binding", line + 1);
        }
        char *parent = hir_scope_field(hir, scope_id, 2);
        free(scope_id);
        scope_id = parent;
    }
    free(scope_id);
    return owned_text("");
}

static char *hir_scope_root(const char *hir, const char *start_scope) {
    char *scope_id = owned_text(start_scope);
    char *parent = hir_scope_field(hir, scope_id, 2);
    while (parent[0] != '\0' && strcmp(parent, "-1") != 0) {
        free(scope_id);
        scope_id = parent;
        parent = hir_scope_field(hir, scope_id, 2);
    }
    free(parent);
    return scope_id;
}

static char *hir_any_declaration(
    const char *hir,
    const char *current_scope,
    int64_t use_start,
    const char *name
) {
    char *current_root = hir_scope_root(hir, current_scope);
    int64_t line = hir_record_start(hir, "binding", 0);
    while (line >= 0) {
        char *binding_name = hir_field(hir, line, 3);
        char *binding_scope = hir_field(hir, line, 2);
        char *declaration_text = hir_field(hir, line, 8);
        char *binding_root = hir_scope_root(hir, binding_scope);
        bool found =
            strcmp(binding_name, name) == 0 &&
            decimal_value(declaration_text) < use_start &&
            strcmp(binding_root, current_root) == 0;
        free(binding_name);
        free(binding_scope);
        free(binding_root);
        if (found) {
            free(current_root);
            return declaration_text;
        }
        free(declaration_text);
        line = hir_record_start(hir, "binding", line + 1);
    }
    free(current_root);
    return owned_text("");
}

static int64_t scope_depth_for_open(
    const char *source,
    int64_t function_open,
    int64_t wanted_open
) {
    int64_t cursor = function_open;
    int64_t depth = 0;
    while (cursor <= wanted_open) {
        if (token_equal(source, cursor, "{")) {
            ++depth;
            if (cursor == wanted_open) return depth;
        } else if (token_equal(source, cursor, "}")) {
            --depth;
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return -1;
}

/*
 * Result types of the 16 profile builtins for `let` initializer typing.
 * The scope-HIR type vocabulary stays the existing single tokens
 * (Int/Bool/Text/List/Void); List[Text] element typing belongs to the
 * selfhost-HIR emitter. Returns NULL for a non-builtin name.
 */
static const char *builtin_return_type(const char *name) {
    static const struct {
        const char *name;
        const char *result;
    } builtins[] = {
        {"args", "List"},
        {"chars", "List"},
        {"contains", "Bool"},
        {"find", "Int"},
        {"is_digit", "Bool"},
        {"is_space", "Bool"},
        {"is_xid_continue", "Bool"},
        {"len", "Int"},
        {"read_text", "Text"},
        {"replace", "Text"},
        {"starts_with", "Bool"},
        {"text_slice", "Text"},
        {"trim", "Text"},
        {"validate_unicode_source", "Text"},
        {"write_text", "Void"},
    };
    size_t count = sizeof(builtins) / sizeof(builtins[0]);
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(name, builtins[index].name) == 0) {
            return builtins[index].result;
        }
    }
    return NULL;
}

/* Declared result type of a user function: the token after `->`, `Void`
 * when there is no arrow, empty when the function is not declared. */
static char *function_return_type(const char *source, const char *wanted) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = next_function_start(source, 0);
    while (cursor < length) {
        char *name = function_name(source, cursor);
        bool match = strcmp(name, wanted) == 0;
        free(name);
        if (match) {
            int64_t parameters = parameter_open(source, cursor);
            if (parameters < 0) return owned_text("");
            int64_t parameters_end = balanced_end(
                source,
                parameters,
                "(",
                ")"
            );
            if (parameters_end < 0) return owned_text("");
            int64_t after = skip_trivia(source, parameters_end);
            if (after < length && token_equal(source, after, "->")) {
                int64_t type_cursor = skip_trivia(
                    source,
                    token_end(source, after)
                );
                if (type_cursor < length) {
                    return token_copy(source, type_cursor);
                }
                return owned_text("");
            }
            return owned_text("Void");
        }
        cursor = next_function_start(source, function_end(source, cursor));
    }
    return owned_text("");
}

/*
 * Bounded initializer typing for unannotated `let` bindings. Top-level
 * comparison and boolean operators make the value Bool; otherwise the
 * profile's operands are homogeneous, so the first primary decides:
 * literals by token kind, calls by declared or builtin result type, names
 * by their resolved binding, bare enum constructors by their owner. The
 * conservative fallback is the historical Int default, never an error.
 */
static char *initializer_type(
    const char *source,
    const char *hir,
    int64_t function_open,
    int64_t initializer
) {
    int64_t length = (int64_t)strlen(source);
    int64_t end = expression_end(source, initializer);
    if (end < 0) end = token_end(source, initializer);
    /* The operator scan covers the whole initializer line: it ends at the
     * first newline outside parentheses, not at the bounded arithmetic
     * expression end, so `1 < 2` and multi-line parenthesized calls are
     * both seen completely. */
    int64_t depth = 0;
    int64_t walk = initializer;
    int64_t previous_end = initializer;
    while (walk < length) {
        bool newline = false;
        for (int64_t at = previous_end; at < walk; ++at) {
            if (source[at] == '\n') {
                newline = true;
                break;
            }
        }
        if (depth == 0 && newline) break;
        if (token_equal(source, walk, "{")) break;
        if (token_equal(source, walk, "(")) {
            ++depth;
        } else if (token_equal(source, walk, ")")) {
            --depth;
        } else if (
            depth == 0 &&
            (token_equal(source, walk, "==") ||
             token_equal(source, walk, "!=") ||
             token_equal(source, walk, "<") ||
             token_equal(source, walk, "<=") ||
             token_equal(source, walk, ">") ||
             token_equal(source, walk, ">=") ||
             token_equal(source, walk, "&&") ||
             token_equal(source, walk, "||") ||
             token_equal(source, walk, "!"))
        ) {
            return owned_text("Bool");
        }
        previous_end = token_end(source, walk);
        walk = skip_trivia(source, previous_end);
    }
    int64_t cursor = skip_trivia(source, initializer);
    while (
        cursor < end &&
        (token_equal(source, cursor, "(") ||
         token_equal(source, cursor, "-") ||
         token_equal(source, cursor, "+"))
    ) {
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    if (cursor >= end) return owned_text("Int");
    const char *kind = token_kind(source, cursor);
    if (strcmp(kind, "integer") == 0) return owned_text("Int");
    if (strcmp(kind, "string") == 0) return owned_text("Text");
    if (
        token_equal(source, cursor, "true") ||
        token_equal(source, cursor, "false")
    ) {
        return owned_text("Bool");
    }
    if (strcmp(kind, "identifier") == 0) {
        char *name = token_copy(source, cursor);
        /* Call and index detection must not stop at the bounded
         * expression end: profile initializers may continue across
         * lines, and `[` follows the resolved primary directly. */
        int64_t open = skip_trivia(source, token_end(source, cursor));
        if (open < length && token_equal(source, open, "(")) {
            char *declared = function_return_type(source, name);
            if (declared[0] != '\0') {
                free(name);
                return declared;
            }
            free(declared);
            const char *builtin = builtin_return_type(name);
            free(name);
            if (builtin != NULL) return owned_text(builtin);
            return owned_text("Int");
        }
        int64_t scope_open = parent_block_open(
            source,
            function_open,
            cursor
        );
        char *scope_id = hir_scope_id_for_open(hir, scope_open);
        char *binding_id = hir_resolve_binding(hir, scope_id, cursor, name);
        free(scope_id);
        if (binding_id[0] != '\0') {
            char *type = hir_binding_field(hir, binding_id, 5);
            free(binding_id);
            free(name);
            if (type[0] != '\0') {
                /* Indexing the profile's List[Text] yields its Text
                 * element. */
                bool indexed =
                    open < length && token_equal(source, open, "[");
                if (indexed && strcmp(type, "List") == 0) {
                    free(type);
                    return owned_text("Text");
                }
                return type;
            }
            free(type);
            return owned_text("Int");
        }
        free(binding_id);
        char *owner = enum_constructor_owner(source, name);
        free(name);
        if (owner[0] != '\0') return owner;
        free(owner);
    }
    return owned_text("Int");
}

static char *scope_hir_error(
    Buffer *hir,
    const char *message,
    int64_t cursor
) {
    free(hir->data);
    return lower_error("E2S35", message, cursor);
}

static char *build_scope_hir_mode(
    const char *source,
    bool preserve_pattern_candidates
) {
    int64_t length = (int64_t)strlen(source);
    Buffer hir;
    buffer_init(&hir);
    buffer_append(&hir, "kofun-scope-hir/v1\n");
    int64_t next_scope_id = 0;
    int64_t next_binding_id = 0;
    int64_t function_start = next_function_start(source, 0);
    while (function_start < length) {
        int64_t function_close = function_end(source, function_start);
        int64_t parameters = parameter_open(source, function_start);
        int64_t parameters_close = balanced_end(
            source,
            parameters,
            "(",
            ")"
        );
        int64_t function_open = skip_trivia(source, parameters_close);
        while (
            function_open < function_close &&
            !token_equal(source, function_open, "{")
        ) {
            function_open = skip_trivia(
                source,
                token_end(source, function_open)
            );
        }
        int64_t parameter_scope = next_scope_id++;
        int64_t body_scope = next_scope_id++;
        int64_t scope_count = 2;
        buffer_format(
            &hir,
            "hir-function|%" PRId64 "|%" PRId64 "|%" PRId64 "\n"
            "scope|%" PRId64 "|-1|parameters|%" PRId64 "|%" PRId64
            "|0\n"
            "scope|%" PRId64 "|%" PRId64 "|function-body|%" PRId64
            "|%" PRId64 "|1\n",
            function_start,
            parameter_scope,
            body_scope,
            parameter_scope,
            parameters,
            parameters_close,
            body_scope,
            parameter_scope,
            function_open,
            function_close
        );

        int64_t cursor = skip_trivia(
            source,
            token_end(source, function_open)
        );
        while (cursor < function_close) {
            if (token_equal(source, cursor, "{")) {
                int64_t depth = scope_depth_for_open(
                    source,
                    function_open,
                    cursor
                );
                if (depth > 32) {
                    return scope_hir_error(
                        &hir,
                        "lexical scope depth limit is 32",
                        cursor
                    );
                }
                ++scope_count;
                if (scope_count > 256) {
                    return scope_hir_error(
                        &hir,
                        "lexical scope limit is 256 per function",
                        cursor
                    );
                }
                int64_t parent_open = parent_block_open(
                    source,
                    function_open,
                    cursor
                );
                char *parent_scope = hir_scope_id_for_open(
                    hir.data,
                    parent_open
                );
                int64_t close = balanced_end(source, cursor, "{", "}");
                const char *scope_kind = scope_kind_for_open(
                    source,
                    function_open,
                    cursor
                );
                buffer_format(
                    &hir,
                    "scope|%" PRId64 "|%s|%s|%" PRId64 "|%" PRId64
                    "|%" PRId64 "\n",
                    next_scope_id++,
                    parent_scope,
                    scope_kind,
                    cursor,
                    close,
                    depth
                );
                free(parent_scope);
            }
            cursor = skip_trivia(source, token_end(source, cursor));
        }

        int64_t binding_count = 0;
        int64_t parameter_cursor = skip_trivia(
            source,
            token_end(source, parameters)
        );
        while (
            parameter_cursor < parameters_close &&
            !token_equal(source, parameter_cursor, ")")
        ) {
            int64_t name = parameter_cursor;
            int64_t colon = skip_trivia(source, token_end(source, name));
            int64_t type_cursor = skip_trivia(
                source,
                token_end(source, colon)
            );
            char *name_text = token_copy(source, name);
            char parameter_scope_text[32];
            snprintf(
                parameter_scope_text,
                sizeof(parameter_scope_text),
                "%" PRId64,
                parameter_scope
            );
            char *first_declaration = hir_same_scope_declaration(
                hir.data,
                parameter_scope_text,
                name_text
            );
            if (first_declaration[0] != '\0') {
                Buffer error;
                buffer_init(&error);
                buffer_format(
                    &error,
                    "error[E2S47]: duplicate binding `%s` in lexical "
                    "scope at byte %" PRId64
                    "; first declaration at byte %s",
                    name_text,
                    name,
                    first_declaration
                );
                free(name_text);
                free(first_declaration);
                free(hir.data);
                return error.data;
            }
            free(first_declaration);
            ++binding_count;
            if (binding_count > 256) {
                free(name_text);
                return scope_hir_error(
                    &hir,
                    "lexical binding limit is 256 per function",
                    name
                );
            }
            char *type_text = token_copy(source, type_cursor);
            buffer_format(
                &hir,
                "binding|%" PRId64 "|%" PRId64 "|%s|immutable|%s|copy|"
                "initialized|%" PRId64 "|%" PRId64 "|%" PRId64 "\n",
                next_binding_id++,
                parameter_scope,
                name_text,
                type_text,
                name,
                token_end(source, name),
                token_end(source, name)
            );
            free(name_text);
            free(type_text);
            int64_t separator = skip_trivia(
                source,
                token_end(source, type_cursor)
            );
            if (
                separator < parameters_close &&
                token_equal(source, separator, ",")
            ) {
                parameter_cursor = skip_trivia(
                    source,
                    token_end(source, separator)
                );
            } else {
                parameter_cursor = separator;
            }
        }

        cursor = skip_trivia(source, token_end(source, function_open));
        while (cursor < function_close) {
            if (token_equal(source, cursor, "let")) {
                int64_t name = skip_trivia(
                    source,
                    token_end(source, cursor)
                );
                const char *mutability = "immutable";
                if (token_equal(source, name, "mut")) {
                    mutability = "mutable";
                    name = skip_trivia(source, token_end(source, name));
                }
                int64_t after_name = skip_trivia(
                    source,
                    token_end(source, name)
                );
                char *binding_type = owned_text("Int");
                bool annotated = false;
                if (token_equal(source, after_name, ":")) {
                    annotated = true;
                    int64_t type_cursor = skip_trivia(
                        source,
                        token_end(source, after_name)
                    );
                    free(binding_type);
                    binding_type = token_copy(source, type_cursor);
                    after_name = skip_trivia(
                        source,
                        token_end(source, type_cursor)
                    );
                }
                int64_t initializer = skip_trivia(
                    source,
                    token_end(source, after_name)
                );
                int64_t visible_start = -1;
                if (value_control(source, initializer)) {
                    char *value_result = parse_value_control(
                        source,
                        initializer,
                        &visible_start
                    );
                    free(value_result);
                } else {
                    visible_start = expression_end(source, initializer);
                    if (!annotated) {
                        free(binding_type);
                        binding_type = initializer_type(
                            source,
                            hir.data,
                            function_open,
                            initializer
                        );
                    }
                }
                if (visible_start < 0) {
                    visible_start = token_end(source, initializer);
                }
                int64_t scope_open = parent_block_open(
                    source,
                    function_open,
                    name
                );
                char *scope_id = hir_scope_id_for_open(hir.data, scope_open);
                char *name_text = token_copy(source, name);
                char *first_declaration = hir_same_scope_declaration(
                    hir.data,
                    scope_id,
                    name_text
                );
                if (first_declaration[0] != '\0') {
                    Buffer error;
                    buffer_init(&error);
                    buffer_format(
                        &error,
                        "error[E2S47]: duplicate binding `%s` in lexical "
                        "scope at byte %" PRId64
                        "; first declaration at byte %s",
                        name_text,
                        name,
                        first_declaration
                    );
                    free(name_text);
                    free(first_declaration);
                    free(binding_type);
                    free(scope_id);
                    free(hir.data);
                    return error.data;
                }
                free(first_declaration);
                ++binding_count;
                if (binding_count > 256) {
                    free(name_text);
                    free(binding_type);
                    free(scope_id);
                    return scope_hir_error(
                        &hir,
                        "lexical binding limit is 256 per function",
                        name
                    );
                }
                const char *ownership =
                    strcmp(binding_type, "Text") == 0 ||
                    strcmp(binding_type, "List") == 0 ? "gc" : "copy";
                buffer_format(
                    &hir,
                    "binding|%" PRId64 "|%s|%s|%s|%s|%s|initialized|"
                    "%" PRId64 "|%" PRId64 "|%" PRId64 "\n",
                    next_binding_id++,
                    scope_id,
                    name_text,
                    mutability,
                    binding_type,
                    ownership,
                    name,
                    token_end(source, name),
                    visible_start
                );
                free(name_text);
                free(binding_type);
                free(scope_id);
            }
            if (token_equal(source, cursor, "for")) {
                int64_t name = skip_trivia(
                    source,
                    token_end(source, cursor)
                );
                int64_t body_open = name;
                while (
                    body_open < function_close &&
                    !token_equal(source, body_open, "{")
                ) {
                    body_open = skip_trivia(
                        source,
                        token_end(source, body_open)
                    );
                }
                if (
                    name < function_close &&
                    body_open < function_close &&
                    strcmp(token_kind(source, name), "identifier") == 0
                ) {
                    char *scope_id = hir_scope_id_for_open(
                        hir.data,
                        body_open
                    );
                    char *name_text = token_copy(source, name);
                    char *first_declaration = hir_same_scope_declaration(
                        hir.data,
                        scope_id,
                        name_text
                    );
                    if (first_declaration[0] != '\0') {
                        Buffer error;
                        buffer_init(&error);
                        buffer_format(
                            &error,
                            "error[E2S47]: duplicate binding `%s` in lexical "
                            "scope at byte %" PRId64
                            "; first declaration at byte %s",
                            name_text,
                            name,
                            first_declaration
                        );
                        free(name_text);
                        free(first_declaration);
                        free(scope_id);
                        free(hir.data);
                        return error.data;
                    }
                    free(first_declaration);
                    ++binding_count;
                    if (binding_count > 256) {
                        free(name_text);
                        free(scope_id);
                        return scope_hir_error(
                            &hir,
                            "lexical binding limit is 256 per function",
                            name
                        );
                    }
                    buffer_format(
                        &hir,
                        "binding|%" PRId64 "|%s|%s|immutable|Int|copy|"
                        "initialized|%" PRId64 "|%" PRId64 "|%" PRId64 "\n",
                        next_binding_id++,
                        scope_id,
                        name_text,
                        name,
                        token_end(source, name),
                        token_end(source, name)
                    );
                    free(name_text);
                    free(scope_id);
                }
            }
            cursor = skip_trivia(source, token_end(source, cursor));
        }

        int64_t use_count = 0;
        bool unresolved_assignment = false;
        cursor = skip_trivia(source, token_end(source, function_open));
        while (cursor < function_close) {
            if (strcmp(token_kind(source, cursor), "identifier") == 0) {
                char *name = token_copy(source, cursor);
                bool declaration_token = enum_declaration_syntax_token(
                    source,
                    function_open,
                    cursor
                );
                bool initializer_token = enum_initializer_constructor_token(
                    source,
                    function_open,
                    cursor
                );
                bool pattern_token = enum_match_pattern_token(
                    source,
                    function_open,
                    cursor
                );
                if (
                    !declaration_token && !initializer_token &&
                    !pattern_token && !token_equal(source, cursor, "print") &&
                    !token_equal(source, cursor, "_")
                ) {
                    int64_t scope_open = parent_block_open(
                        source,
                        function_open,
                        cursor
                    );
                    char *scope_id = hir_scope_id_for_open(
                        hir.data,
                        scope_open
                    );
                    char *binding_id = hir_resolve_binding(
                        hir.data,
                        scope_id,
                        cursor,
                        name
                    );
                    int64_t after = skip_trivia(
                        source,
                        token_end(source, cursor)
                    );
                    const char *role = token_equal(source, after, "=") ?
                        "assign" : "read";
                    if (binding_id[0] != '\0') {
                        ++use_count;
                        if (use_count > 256) {
                            free(name);
                            free(scope_id);
                            free(binding_id);
                            return scope_hir_error(
                                &hir,
                                "lexical use limit is 256 per function",
                                cursor
                            );
                        }
                        buffer_format(
                            &hir,
                            "use|%" PRId64 "|%" PRId64 "|%s|%s|%s\n",
                            cursor,
                            token_end(source, cursor),
                            scope_id,
                            binding_id,
                            role
                        );
                    } else if (strcmp(role, "assign") == 0) {
                        ++use_count;
                        if (use_count > 256) {
                            free(name);
                            free(scope_id);
                            free(binding_id);
                            free(hir.data);
                            return lower_error(
                                "E2S35",
                                "lexical use limit is 256 per function",
                                cursor
                            );
                        }
                        buffer_format(
                            &hir,
                            "use|%" PRId64 "|%" PRId64 "|%s|-1|assign\n",
                            cursor,
                            token_end(source, cursor),
                            scope_id
                        );
                        unresolved_assignment = true;
                    } else if (
                        preserve_pattern_candidates &&
                        !token_equal(source, after, "(")
                    ) {
                        ++use_count;
                        if (use_count > 256) {
                            free(name);
                            free(scope_id);
                            free(binding_id);
                            return scope_hir_error(
                                &hir,
                                "lexical use limit is 256 per function",
                                cursor
                            );
                        }
                        buffer_format(
                            &hir,
                            "candidate-use|%" PRId64 "|%" PRId64
                            "|%s|%s|%s\n",
                            cursor,
                            token_end(source, cursor),
                            scope_id,
                            name,
                            role
                        );
                    } else if (
                        !token_equal(source, after, "(") &&
                        !unresolved_assignment
                    ) {
                        char *owner = enum_constructor_owner(source, name);
                        char *pending = hir_pending_declaration(
                            hir.data,
                            scope_id,
                            cursor,
                            name
                        );
                            if (pending[0] != '\0') {
                                Buffer message;
                                buffer_init(&message);
                                buffer_format(
                                    &message,
                                    "error[E2S35]: binding `%s` is not "
                                    "initialized at byte "
                                    "%" PRId64 "; declaration at byte %s",
                                    name,
                                    cursor,
                                    pending
                                );
                                free(name);
                                free(scope_id);
                                free(binding_id);
                                free(owner);
                                free(pending);
                                free(hir.data);
                                return message.data;
                            }
                            free(pending);
                            char *escaped = hir_any_declaration(
                                hir.data,
                                scope_id,
                                cursor,
                                name
                            );
                            if (escaped[0] != '\0') {
                                Buffer message;
                                buffer_init(&message);
                                buffer_format(
                                    &message,
                                    "error[E2S35]: binding `%s` is outside its "
                                    "lexical scope at byte %" PRId64
                                    "; declaration at byte %s",
                                    name,
                                    cursor,
                                    escaped
                                );
                                free(name);
                                free(scope_id);
                                free(binding_id);
                                free(owner);
                                free(escaped);
                                free(hir.data);
                                return message.data;
                            }
                            free(escaped);
                        if (owner[0] == '\0') {
                            Buffer message;
                            buffer_init(&message);
                            buffer_format(
                                &message,
                                "error[E2S35]: unknown lexical binding `%s` "
                                "at byte %" PRId64,
                                name,
                                cursor
                            );
                            free(name);
                            free(scope_id);
                            free(binding_id);
                            free(owner);
                            free(hir.data);
                            return message.data;
                        }
                        free(owner);
                    }
                    free(scope_id);
                    free(binding_id);
                }
                free(name);
            }
            cursor = skip_trivia(source, token_end(source, cursor));
        }
        function_start = next_function_start(source, function_close);
    }
    return hir.data;
}

static char *build_scope_hir(const char *source) {
    return build_scope_hir_mode(source, false);
}

static char *validate_enum_uses(const char *source, const char *hir) {
    int64_t length = (int64_t)strlen(source);
    char *constructor_names = enum_declaration_names(source, true);
    if (strcmp(constructor_names, "|") == 0) {
        free(constructor_names);
        return owned_text("ok");
    }
    int64_t function_start = next_function_start(source, 0);
    while (function_start < length) {
        int64_t function_close = function_end(source, function_start);
        int64_t parameters = parameter_open(source, function_start);
        int64_t parameters_close = balanced_end(
            source,
            parameters,
            "(",
            ")"
        );
        int64_t function_open = skip_trivia(source, parameters_close);
        while (
            function_open < function_close &&
            !token_equal(source, function_open, "{")
        ) {
            function_open = skip_trivia(
                source,
                token_end(source, function_open)
            );
        }
        int64_t related_identifiers = 0;
        int64_t cursor = skip_trivia(
            source,
            token_end(source, function_open)
        );
        char *previous = owned_text("");
        while (cursor < function_close) {
            if (strcmp(token_kind(source, cursor), "identifier") == 0) {
                char *name = token_copy(source, cursor);
                bool pattern_token = enum_match_pattern_token(
                    source,
                    function_open,
                    cursor
                );
                bool initializer_token =
                    enum_initializer_constructor_token(
                        source,
                        function_open,
                        cursor
                    );
                bool declaration_token = enum_declaration_syntax_token(
                    source,
                    function_open,
                    cursor
                );
                char *binding_id = hir_use_binding_id(hir, cursor);
                char *binding_type = hir_binding_field(
                    hir,
                    binding_id,
                    5
                );
                bool binding_enum =
                    binding_type[0] != '\0' &&
                    enum_constructor_count(source, binding_type) >= 0;
                bool constructor_named = enum_name_covered(
                    constructor_names,
                    name
                );
                bool related =
                    pattern_token || initializer_token || binding_enum ||
                    (
                        constructor_named && binding_id[0] == '\0' &&
                        !declaration_token
                    );
                if (related) {
                    ++related_identifiers;
                    if (related_identifiers > 256) {
                        Buffer error;
                        buffer_init(&error);
                        buffer_format(
                            &error,
                            "error[E2S32]: enum-related identifier use "
                            "limit is 256 per function at byte %" PRId64,
                            cursor
                        );
                        free(name);
                        free(binding_id);
                        free(binding_type);
                        free(previous);
                        free(constructor_names);
                        return error.data;
                    }
                    if (
                        !pattern_token && !initializer_token &&
                        !declaration_token
                    ) {
                        if (binding_enum) {
                            int64_t after = skip_trivia(
                                source,
                                token_end(source, cursor)
                            );
                            bool match_scrutinee =
                                strcmp(previous, "match") == 0 &&
                                token_equal(source, after, "{");
                            if (!match_scrutinee) {
                                Buffer error;
                                buffer_init(&error);
                                buffer_format(
                                    &error,
                                    "error[E2S32]: concrete enum binding "
                                    "`%s` is match-only in this Core "
                                    "slice at byte %" PRId64,
                                    name,
                                    cursor
                                );
                                free(name);
                                free(binding_id);
                                free(binding_type);
                                free(previous);
                                free(constructor_names);
                                return error.data;
                            }
                        } else if (
                            constructor_named && binding_id[0] == '\0'
                        ) {
                            int64_t after = skip_trivia(
                                source,
                                token_end(source, cursor)
                            );
                            bool resolved_function_call =
                                token_equal(source, after, "(") &&
                                function_arity(source, name) >= 0;
                            if (!resolved_function_call) {
                                char *constructor_owner =
                                    enum_constructor_owner(
                                        source,
                                        name
                                    );
                                if (constructor_owner[0] != '\0') {
                                    Buffer error;
                                    buffer_init(&error);
                                    buffer_format(
                                        &error,
                                        "error[E2S32]: concrete enum "
                                        "constructor `%s` is only valid in an "
                                        "explicitly typed enum initializer or "
                                        "match pattern at byte %" PRId64,
                                        name,
                                        cursor
                                    );
                                    free(name);
                                    free(binding_id);
                                    free(binding_type);
                                    free(constructor_owner);
                                    free(previous);
                                    free(constructor_names);
                                    return error.data;
                                }
                                free(constructor_owner);
                            }
                        }
                    }
                }
                free(binding_id);
                free(binding_type);
                free(name);
            }
            free(previous);
            previous = token_copy(source, cursor);
            cursor = skip_trivia(source, token_end(source, cursor));
        }
        free(previous);
        function_start = next_function_start(source, function_close);
    }
    free(constructor_names);
    return owned_text("ok");
}

static int64_t enum_match_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t value_start = skip_trivia(source, token_end(source, start));
    int64_t arms_open = skip_trivia(source, token_end(source, value_start));
    if (arms_open >= length || !token_equal(source, arms_open, "{")) {
        return -1;
    }
    return balanced_end(source, arms_open, "{", "}");
}

static char *lower_body(
    const char *source,
    const char *hir,
    int64_t open,
    bool is_main,
    bool append_default,
    int64_t function_open
);

static char *lower_enum_match_error(
    Buffer *covered,
    Buffer *dispatch,
    const char *code,
    const char *message,
    int64_t cursor
) {
    free(covered->data);
    free(dispatch->data);
    return lower_error(code, message, cursor);
}

static char *lower_enum_match(
    const char *source,
    const char *hir,
    int64_t match_start,
    const char *enum_type,
    bool is_main,
    int64_t function_open
) {
    int64_t length = (int64_t)strlen(source);
    int64_t value_start = skip_trivia(
        source,
        token_end(source, match_start)
    );
    int64_t arms_open = skip_trivia(source, token_end(source, value_start));
    Buffer covered;
    Buffer dispatch;
    buffer_init(&covered);
    buffer_init(&dispatch);
    buffer_append(&covered, "|");
    if (arms_open >= length || !token_equal(source, arms_open, "{")) {
        return lower_enum_match_error(
            &covered,
            &dispatch,
            "E2S24",
            "expected `{` after enum match scrutinee",
            arms_open
        );
    }

    int64_t arm_cursor = skip_trivia(
        source,
        token_end(source, arms_open)
    );
    bool seen_catchall = false;
    const char *failure_result = is_main ? "1" : "0";
    while (arm_cursor < length && !token_equal(source, arm_cursor, "}")) {
        int64_t pattern_start = arm_cursor;
        PatternSummary pattern_summary_value = pattern_summary(
            source,
            pattern_start
        );
        char *pattern = token_copy(source, pattern_start);
        bool catchall = pattern_summary_value.kind == PATTERN_WILDCARD;
        if (seen_catchall) {
            free(pattern);
            return lower_enum_match_error(
                &covered,
                &dispatch,
                "E2S26",
                "pattern after catch-all is unreachable",
                pattern_start
            );
        }
        int64_t tag = -1;
        if (catchall) {
            if (enum_constructors_covered(source, enum_type, covered.data)) {
                free(pattern);
                return lower_enum_match_error(
                    &covered,
                    &dispatch,
                    "E2S26",
                    "catch-all pattern is unreachable",
                    pattern_start
                );
            }
        } else {
            if (pattern_summary_value.kind != PATTERN_NAME) {
                free(pattern);
                return lower_enum_match_error(
                    &covered,
                    &dispatch,
                    "E2S32",
                    "enum pattern must name a constructor or `_`",
                    pattern_start
                );
            }
            tag = enum_constructor_index(source, enum_type, pattern);
            if (tag < 0) {
                Buffer message;
                buffer_init(&message);
                buffer_format(
                    &message,
                    "constructor `%s` does not belong to enum `%s`",
                    pattern,
                    enum_type
                );
                free(pattern);
                char *error = lower_enum_match_error(
                    &covered,
                    &dispatch,
                    "E2S32",
                    message.data,
                    pattern_start
                );
                free(message.data);
                return error;
            }
            if (enum_name_covered(covered.data, pattern)) {
                Buffer message;
                buffer_init(&message);
                buffer_format(
                    &message,
                    "duplicate enum constructor pattern `%s` is unreachable",
                    pattern
                );
                free(pattern);
                char *error = lower_enum_match_error(
                    &covered,
                    &dispatch,
                    "E2S26",
                    message.data,
                    pattern_start
                );
                free(message.data);
                return error;
            }
        }

        int64_t arrow = skip_trivia(
            source,
            pattern_summary_value.end
        );
        bool guarded = false;
        int64_t guard_start = -1;
        int64_t guard_end = -1;
        if (arrow < length && token_equal(source, arrow, "if")) {
            guarded = true;
            guard_start = skip_trivia(source, token_end(source, arrow));
            guard_end = condition_end(source, guard_start);
            if (guard_end < 0) {
                free(pattern);
                return lower_enum_match_error(
                    &covered,
                    &dispatch,
                    "E2S29",
                    "match guard must be Bool or an Int comparison",
                    guard_start
                );
            }
            arrow = skip_trivia(source, guard_end);
        }
        if (arrow >= length || !token_equal(source, arrow, "=>")) {
            free(pattern);
            return lower_enum_match_error(
                &covered,
                &dispatch,
                "E2S24",
                "expected `=>` after enum pattern",
                arrow
            );
        }
        int64_t arm_open = skip_trivia(source, token_end(source, arrow));
        if (arm_open >= length || !token_equal(source, arm_open, "{")) {
            free(pattern);
            return lower_enum_match_error(
                &covered,
                &dispatch,
                "E2S24",
                "bounded enum match arm must use a block",
                arm_open
            );
        }
        int64_t arm_close = balanced_end(source, arm_open, "{", "}");
        if (arm_close < 0) {
            free(pattern);
            return lower_enum_match_error(
                &covered,
                &dispatch,
                "E2S24",
                "missing `}` after enum match arm",
                arm_open
            );
        }
        char *arm_body = lower_body(
            source,
            hir,
            arm_open,
            is_main,
            false,
            function_open
        );
        if (strncmp(arm_body, "error[", 6) == 0) {
            free(pattern);
            free(covered.data);
            free(dispatch.data);
            return arm_body;
        }

        Buffer pattern_condition;
        buffer_init(&pattern_condition);
        if (catchall) {
            buffer_append(&pattern_condition, "true");
        } else {
            buffer_format(
                &pattern_condition,
                "kofun_match_value == INT64_C(%" PRId64 ")",
                tag
            );
        }
        if (guarded) {
            char *guard = emit_condition_into(
                source,
                hir,
                guard_start,
                guard_end,
                "kofun_match_guard",
                failure_result,
                "            "
            );
            buffer_format(
                &dispatch,
                "        if (!kofun_match_selected && %s) {\n"
                "%s"
                "            if (kofun_match_guard) {\n"
                "%s"
                "                kofun_match_selected = true;\n"
                "            }\n"
                "        }\n",
                pattern_condition.data,
                guard,
                arm_body
            );
            free(guard);
        } else {
            buffer_format(
                &dispatch,
                "        if (!kofun_match_selected && %s) {\n"
                "%s"
                "            kofun_match_selected = true;\n"
                "        }\n",
                pattern_condition.data,
                arm_body
            );
            if (catchall) {
                seen_catchall = true;
            } else {
                buffer_append(&covered, pattern);
                buffer_append(&covered, "|");
            }
        }
        free(pattern_condition.data);
        free(arm_body);
        free(pattern);

        arm_cursor = skip_trivia(source, arm_close);
        if (arm_cursor < length && token_equal(source, arm_cursor, ",")) {
            arm_cursor = skip_trivia(
                source,
                token_end(source, arm_cursor)
            );
        } else if (
            arm_cursor >= length ||
            !token_equal(source, arm_cursor, "}")
        ) {
            return lower_enum_match_error(
                &covered,
                &dispatch,
                "E2S24",
                "expected `,` between enum match arms",
                arm_cursor
            );
        }
    }
    if (arm_cursor >= length || !token_equal(source, arm_cursor, "}")) {
        return lower_enum_match_error(
            &covered,
            &dispatch,
            "E2S24",
            "missing `}` after enum match arms",
            arms_open
        );
    }
    if (
        !seen_catchall &&
        !enum_constructors_covered(source, enum_type, covered.data)
    ) {
        char *missing = enum_missing_constructors(
            source,
            enum_type,
            covered.data
        );
        Buffer message;
        buffer_init(&message);
        buffer_format(
            &message,
            "non-exhaustive enum `%s` match; missing constructors %s",
            enum_type,
            missing
        );
        free(missing);
        char *error = lower_enum_match_error(
            &covered,
            &dispatch,
            "E2S25",
            message.data,
            match_start
        );
        free(message.data);
        return error;
    }

    char *binding_id = hir_use_binding_id(hir, value_start);
    Buffer emitted;
    buffer_init(&emitted);
    buffer_format(
        &emitted,
        "    {\n"
        "        int64_t kofun_match_value = k_b%s;\n"
        "        (void)kofun_match_value;\n"
        "        bool kofun_match_selected = false;\n"
        "%s"
        "    }\n",
        binding_id,
        dispatch.data
    );
    free(binding_id);
    free(covered.data);
    free(dispatch.data);
    return emitted.data;
}

static char *assignment_error(
    const char *message,
    const char *name,
    int64_t cursor,
    const char *hint
) {
    Buffer error;
    buffer_init(&error);
    buffer_format(
        &error,
        "error[E2S22]: %s `%s` at byte %" PRId64 "; %s",
        message,
        name,
        cursor,
        hint
    );
    return error.data;
}

static char *lower_match_error(
    Buffer *emitted,
    Buffer *dispatch,
    const char *code,
    const char *message,
    int64_t cursor
) {
    free(dispatch->data);
    free(emitted->data);
    return lower_error(code, message, cursor);
}

static char *lower_body(
    const char *source,
    const char *hir,
    int64_t open,
    bool is_main,
    bool append_default,
    int64_t function_open
) {
    int64_t length = (int64_t)strlen(source);
    Buffer emitted;
    buffer_init(&emitted);
    int64_t cursor = skip_trivia(source, token_end(source, open));
    bool returned = false;
    const char *failure_result = is_main ? "1" : "0";
    while (cursor < length && !token_equal(source, cursor, "}")) {
        if (returned) {
            free(emitted.data);
            return lower_error("E2S14", "statement follows `return`", cursor);
        }
        if (token_equal(source, cursor, "let")) {
            cursor = skip_trivia(source, token_end(source, cursor));
            bool mutable = false;
            if (cursor < length && token_equal(source, cursor, "mut")) {
                mutable = true;
                cursor = skip_trivia(source, token_end(source, cursor));
            }
            if (
                cursor >= length ||
                strcmp(token_kind(source, cursor), "identifier") != 0
            ) {
                free(emitted.data);
                return lower_error("E2S11", "expected binding name", cursor);
            }
            char *name = token_copy(source, cursor);
            char *binding_id = hir_definition_id_at(hir, cursor);
            char *enum_type = NULL;
            cursor = skip_trivia(source, token_end(source, cursor));
            if (cursor < length && token_equal(source, cursor, ":")) {
                cursor = skip_trivia(source, token_end(source, cursor));
                if (
                    cursor >= length ||
                    strcmp(token_kind(source, cursor), "identifier") != 0
                ) {
                    free(binding_id);
                    free(name);
                    free(emitted.data);
                    return lower_error(
                        "E2S11",
                        "expected Core binding type",
                        cursor
                    );
                }
                char *declared_type = token_copy(source, cursor);
                if (strcmp(declared_type, "Int") != 0) {
                    if (enum_constructor_count(source, declared_type) < 0) {
                        Buffer message;
                        buffer_init(&message);
                        buffer_format(
                            &message,
                            "unknown concrete enum type `%s`",
                            declared_type
                        );
                        free(declared_type);
                        free(binding_id);
                        free(name);
                        free(emitted.data);
                        char *error = lower_error(
                            "E2S32",
                            message.data,
                            cursor
                        );
                        free(message.data);
                        return error;
                    }
                    enum_type = declared_type;
                } else {
                    free(declared_type);
                }
                cursor = skip_trivia(source, token_end(source, cursor));
            }
            if (cursor >= length || !token_equal(source, cursor, "=")) {
                free(enum_type);
                free(binding_id);
                free(name);
                free(emitted.data);
                return lower_error("E2S11", "expected `=`", cursor);
            }
            int64_t value_start = skip_trivia(source, token_end(source, cursor));
            if (enum_type != NULL) {
                if (mutable) {
                    free(enum_type);
                    free(binding_id);
                    free(name);
                    free(emitted.data);
                    return lower_error(
                        "E2S32",
                        "concrete enum bindings are immutable in this Core slice",
                        value_start
                    );
                }
                if (
                    value_start >= length ||
                    strcmp(token_kind(source, value_start), "identifier") != 0
                ) {
                    free(enum_type);
                    free(binding_id);
                    free(name);
                    free(emitted.data);
                    return lower_error(
                        "E2S32",
                        "concrete enum initializer must name a constructor",
                        value_start
                    );
                }
                char *constructor = token_copy(source, value_start);
                int64_t tag = enum_constructor_index(
                    source,
                    enum_type,
                    constructor
                );
                if (tag < 0) {
                    Buffer message;
                    buffer_init(&message);
                    buffer_format(
                        &message,
                        "constructor `%s` does not belong to enum `%s`",
                        constructor,
                        enum_type
                    );
                    free(constructor);
                    free(enum_type);
                    free(binding_id);
                    free(name);
                    free(emitted.data);
                    char *error = lower_error(
                        "E2S32",
                        message.data,
                        value_start
                    );
                    free(message.data);
                    return error;
                }
                buffer_format(
                    &emitted,
                    "    int64_t k_b%s = INT64_C(%" PRId64 ");\n",
                    binding_id,
                    tag
                );
                free(constructor);
                free(enum_type);
                free(name);
                free(binding_id);
                cursor = skip_trivia(source, token_end(source, value_start));
                continue;
            }
            if (value_control(source, value_start)) {
                int64_t value_end = -1;
                char *result = parse_value_control(
                    source,
                    value_start,
                    &value_end
                );
                if (strncmp(result, "error[", 6) == 0) {
                    free(binding_id);
                    free(name);
                    free(emitted.data);
                    return result;
                }
                free(result);
                Buffer target;
                buffer_init(&target);
                buffer_format(&target, "k_b%s", binding_id);
                char *value_body = emit_value_into(
                    source,
                    hir,
                    value_start,
                    value_end,
                    target.data,
                    failure_result
                );
                if (strncmp(value_body, "error[", 6) == 0) {
                    free(target.data);
                    free(binding_id);
                    free(name);
                    free(emitted.data);
                    return value_body;
                }
                buffer_format(
                    &emitted,
                    "    int64_t k_b%s = INT64_C(0);\n"
                    "%s",
                    binding_id,
                    value_body
                );
                free(value_body);
                free(target.data);
                free(name);
                free(binding_id);
                cursor = skip_trivia(source, value_end);
                continue;
            }
            int64_t value_end = expression_end(source, value_start);
            if (value_end < 0) {
                free(binding_id);
                free(name);
                free(emitted.data);
                return lower_error("E2S12", "invalid Int expression", value_start);
            }
            char *value = emit_expression(source, hir, value_start, value_end);
            buffer_format(
                &emitted,
                "    int64_t k_b%s = %s;\n"
                "    if (kofun_failed) return %s;\n",
                binding_id,
                value,
                failure_result
            );
            free(value);
            free(name);
            free(binding_id);
            cursor = skip_trivia(source, value_end);
        } else if (token_equal(source, cursor, "print")) {
            int64_t call_open = skip_trivia(source, token_end(source, cursor));
            if (call_open >= length || !token_equal(source, call_open, "(")) {
                free(emitted.data);
                return lower_error("E2S13", "expected `print(`", cursor);
            }
            int64_t value_start = skip_trivia(source, token_end(source, call_open));
            if (value_control(source, value_start)) {
                int64_t value_end = -1;
                char *result = parse_value_control(
                    source,
                    value_start,
                    &value_end
                );
                if (strncmp(result, "error[", 6) == 0) {
                    free(emitted.data);
                    return result;
                }
                free(result);
                int64_t call_close = skip_trivia(source, value_end);
                if (
                    call_close >= length ||
                    !token_equal(source, call_close, ")")
                ) {
                    free(emitted.data);
                    return lower_error(
                        "E2S13",
                        "expected `)`",
                        call_close
                    );
                }
                char *value_body = emit_value_into(
                    source,
                    hir,
                    value_start,
                    value_end,
                    "kofun_value",
                    failure_result
                );
                if (strncmp(value_body, "error[", 6) == 0) {
                    free(emitted.data);
                    return value_body;
                }
                buffer_format(
                    &emitted,
                    "    {\n"
                    "        int64_t kofun_value = INT64_C(0);\n"
                    "%s"
                    "        printf(\"%%\" PRId64 \"\\n\", kofun_value);\n"
                    "    }\n",
                    value_body
                );
                free(value_body);
                cursor = skip_trivia(
                    source,
                    token_end(source, call_close)
                );
                continue;
            }
            int64_t value_end = expression_end(source, value_start);
            if (value_end < 0) {
                free(emitted.data);
                return lower_error("E2S12", "invalid Int expression", value_start);
            }
            int64_t call_close = skip_trivia(source, value_end);
            if (call_close >= length || !token_equal(source, call_close, ")")) {
                free(emitted.data);
                return lower_error("E2S13", "expected `)`", call_close);
            }
            char *value = emit_expression(source, hir, value_start, value_end);
            buffer_format(
                &emitted,
                "    {\n"
                "        int64_t kofun_value = %s;\n"
                "        if (kofun_failed) return %s;\n"
                "        printf(\"%%\" PRId64 \"\\n\", kofun_value);\n"
                "    }\n",
                value,
                failure_result
            );
            free(value);
            cursor = skip_trivia(source, token_end(source, call_close));
        } else if (token_equal(source, cursor, "if")) {
            int64_t condition_start = skip_trivia(
                source,
                token_end(source, cursor)
            );
            int64_t condition_close = condition_end(source, condition_start);
            if (condition_close < 0) {
                free(emitted.data);
                return lower_error(
                    "E2S23",
                    "if condition must be Bool or an Int comparison",
                    condition_start
                );
            }
            int64_t branch_open = skip_trivia(source, condition_close);
            if (
                branch_open >= length ||
                !token_equal(source, branch_open, "{")
            ) {
                free(emitted.data);
                return lower_error(
                    "E2S18",
                    "expected `{` after if condition",
                    branch_open
                );
            }
            int64_t branch_close = balanced_end(
                source,
                branch_open,
                "{",
                "}"
            );
            if (branch_close < 0) {
                free(emitted.data);
                return lower_error(
                    "E2S18",
                    "missing `}` after if branch",
                    branch_open
                );
            }
            char *branch_body = lower_body(
                source,
                hir,
                branch_open,
                is_main,
                false,
                function_open
            );
            if (strncmp(branch_body, "error[", 6) == 0) {
                free(emitted.data);
                return branch_body;
            }
            char *condition = emit_condition_into(
                source,
                hir,
                condition_start,
                condition_close,
                "kofun_condition",
                failure_result,
                "        "
            );
            buffer_format(
                &emitted,
                "    {\n"
                "%s"
                "        if (kofun_condition) {\n"
                "%s"
                "        }",
                condition,
                branch_body
            );
            free(condition);
            free(branch_body);
            cursor = skip_trivia(source, branch_close);
            if (cursor < length && token_equal(source, cursor, "else")) {
                int64_t else_open = skip_trivia(
                    source,
                    token_end(source, cursor)
                );
                if (
                    else_open >= length ||
                    !token_equal(source, else_open, "{")
                ) {
                    free(emitted.data);
                    return lower_error(
                        "E2S18",
                        "expected `{` after `else`",
                        else_open
                    );
                }
                int64_t else_close = balanced_end(
                    source,
                    else_open,
                    "{",
                    "}"
                );
                if (else_close < 0) {
                    free(emitted.data);
                    return lower_error(
                        "E2S18",
                        "missing `}` after else branch",
                        else_open
                    );
                }
                char *else_body = lower_body(
                    source,
                    hir,
                    else_open,
                    is_main,
                    false,
                    function_open
                );
                if (strncmp(else_body, "error[", 6) == 0) {
                    free(emitted.data);
                    return else_body;
                }
                buffer_format(&emitted, " else {\n%s        }", else_body);
                free(else_body);
                cursor = skip_trivia(source, else_close);
            }
            buffer_append(&emitted, "\n    }\n");
        } else if (token_equal(source, cursor, "match")) {
            int64_t match_start = cursor;
            int64_t value_start = skip_trivia(
                source,
                token_end(source, cursor)
            );
            int64_t direct_end = skip_trivia(
                source,
                token_end(source, value_start)
            );
            if (
                strcmp(token_kind(source, value_start), "identifier") == 0 &&
                direct_end < length &&
                token_equal(source, direct_end, "{")
            ) {
                char *value_name = token_copy(source, value_start);
                char *enum_binding = hir_use_binding_id(hir, value_start);
                char *enum_type = hir_binding_field(
                    hir,
                    enum_binding,
                    5
                );
                if (
                    enum_type[0] == '\0' ||
                    strcmp(enum_type, "Int") == 0 ||
                    enum_constructor_count(source, enum_type) < 0
                ) {
                    Buffer message;
                    buffer_init(&message);
                    buffer_format(
                        &message,
                        "enum match scrutinee `%s` must be a preceding "
                        "explicitly typed enum binding",
                        value_name
                    );
                    free(enum_type);
                    free(enum_binding);
                    free(value_name);
                    free(emitted.data);
                    char *error = lower_error(
                        "E2S32",
                        message.data,
                        value_start
                    );
                    free(message.data);
                    return error;
                }
                char *match_body = lower_enum_match(
                    source,
                    hir,
                    match_start,
                    enum_type,
                    is_main,
                    function_open
                );
                free(enum_type);
                free(enum_binding);
                free(value_name);
                if (strncmp(match_body, "error[", 6) == 0) {
                    free(emitted.data);
                    return match_body;
                }
                buffer_append(&emitted, match_body);
                free(match_body);
                int64_t match_end = enum_match_end(source, match_start);
                cursor = skip_trivia(source, match_end);
            } else {
            int64_t value_end = condition_end(source, value_start);
            Buffer dispatch;
            buffer_init(&dispatch);
            if (value_end < 0) {
                return lower_match_error(
                    &emitted,
                    &dispatch,
                    "E2S24",
                    "bounded match scrutinee must be Bool",
                    value_start
                );
            }
            int64_t arms_open = skip_trivia(source, value_end);
            if (
                arms_open >= length ||
                !token_equal(source, arms_open, "{")
            ) {
                return lower_match_error(
                    &emitted,
                    &dispatch,
                    "E2S24",
                    "expected `{` after match scrutinee",
                    arms_open
                );
            }
            int64_t arm_cursor = skip_trivia(
                source,
                token_end(source, arms_open)
            );
            bool covered_true = false;
            bool covered_false = false;
            bool seen_catchall = false;
            while (
                arm_cursor < length &&
                !token_equal(source, arm_cursor, "}")
            ) {
                int64_t pattern_start = arm_cursor;
                PatternSummary pattern = pattern_summary(
                    source,
                    pattern_start
                );
                bool pattern_true = pattern.kind == PATTERN_LITERAL &&
                                    token_equal(
                                        source,
                                        pattern_start,
                                        "true"
                                    );
                bool pattern_false = pattern.kind == PATTERN_LITERAL &&
                                     token_equal(
                                         source,
                                         pattern_start,
                                         "false"
                                     );
                bool pattern_catchall =
                    pattern.kind == PATTERN_WILDCARD;
                if (seen_catchall) {
                    return lower_match_error(
                        &emitted,
                        &dispatch,
                        "E2S26",
                        "pattern after catch-all is unreachable",
                        pattern_start
                    );
                }
                if (pattern_true && covered_true) {
                    return lower_match_error(
                        &emitted,
                        &dispatch,
                        "E2S26",
                        "duplicate `true` pattern is unreachable",
                        pattern_start
                    );
                }
                if (pattern_false && covered_false) {
                    return lower_match_error(
                        &emitted,
                        &dispatch,
                        "E2S26",
                        "duplicate `false` pattern is unreachable",
                        pattern_start
                    );
                }
                if (
                    pattern_catchall &&
                    covered_true &&
                    covered_false
                ) {
                    return lower_match_error(
                        &emitted,
                        &dispatch,
                        "E2S26",
                        "catch-all pattern is unreachable",
                        pattern_start
                    );
                }
                if (
                    !pattern_true &&
                    !pattern_false &&
                    !pattern_catchall
                ) {
                    return lower_match_error(
                        &emitted,
                        &dispatch,
                        "E2S24",
                        "bounded Bool pattern must be `true`, `false`, or `_`",
                        pattern_start
                    );
                }
                int64_t after_pattern = skip_trivia(
                    source,
                    pattern.end
                );
                bool guarded = false;
                int64_t guard_start = -1;
                int64_t guard_end = -1;
                if (
                    after_pattern < length &&
                    token_equal(source, after_pattern, "if")
                ) {
                    guarded = true;
                    guard_start = skip_trivia(
                        source,
                        token_end(source, after_pattern)
                    );
                    guard_end = condition_end(source, guard_start);
                    if (guard_end < 0) {
                        return lower_match_error(
                            &emitted,
                            &dispatch,
                            "E2S29",
                            "match guard must be Bool or an Int comparison",
                            guard_start
                        );
                    }
                    after_pattern = skip_trivia(
                        source,
                        guard_end
                    );
                }
                if (
                    after_pattern >= length ||
                    !token_equal(source, after_pattern, "=>")
                ) {
                    return lower_match_error(
                        &emitted,
                        &dispatch,
                        "E2S24",
                        "expected `=>` after Bool pattern",
                        after_pattern
                    );
                }
                int64_t arm_open = skip_trivia(
                    source,
                    token_end(source, after_pattern)
                );
                if (
                    arm_open >= length ||
                    !token_equal(source, arm_open, "{")
                ) {
                    return lower_match_error(
                        &emitted,
                        &dispatch,
                        "E2S24",
                        "bounded Bool match arm must use a block",
                        arm_open
                    );
                }
                int64_t arm_close = balanced_end(
                    source,
                    arm_open,
                    "{",
                    "}"
                );
                if (arm_close < 0) {
                    return lower_match_error(
                        &emitted,
                        &dispatch,
                        "E2S24",
                        "missing `}` after match arm",
                        arm_open
                    );
                }
                char *arm_body = lower_body(
                    source,
                    hir,
                    arm_open,
                    is_main,
                    false,
                    function_open
                );
                if (strncmp(arm_body, "error[", 6) == 0) {
                    free(dispatch.data);
                    free(emitted.data);
                    return arm_body;
                }

                const char *pattern_condition = "true";
                if (pattern_true) {
                    pattern_condition = "kofun_match_value";
                } else if (pattern_false) {
                    pattern_condition = "!kofun_match_value";
                }
                if (guarded) {
                    char *guard = emit_condition_into(
                        source,
                        hir,
                        guard_start,
                        guard_end,
                        "kofun_match_guard",
                        failure_result,
                        "            "
                    );
                    buffer_format(
                        &dispatch,
                        "        if (!kofun_match_selected && %s) {\n"
                        "%s"
                        "            if (kofun_match_guard) {\n"
                        "%s"
                        "                kofun_match_selected = true;\n"
                        "            }\n"
                        "        }\n",
                        pattern_condition,
                        guard,
                        arm_body
                    );
                    free(guard);
                } else {
                    buffer_format(
                        &dispatch,
                        "        if (!kofun_match_selected && %s) {\n"
                        "%s"
                        "            kofun_match_selected = true;\n"
                        "        }\n",
                        pattern_condition,
                        arm_body
                    );
                    if (pattern_true) {
                        covered_true = true;
                    } else if (pattern_false) {
                        covered_false = true;
                    } else {
                        covered_true = true;
                        covered_false = true;
                        seen_catchall = true;
                    }
                }
                free(arm_body);
                arm_cursor = skip_trivia(source, arm_close);
                if (
                    arm_cursor < length &&
                    token_equal(source, arm_cursor, ",")
                ) {
                    arm_cursor = skip_trivia(
                        source,
                        token_end(source, arm_cursor)
                    );
                } else if (
                    arm_cursor >= length ||
                    !token_equal(source, arm_cursor, "}")
                ) {
                    return lower_match_error(
                        &emitted,
                        &dispatch,
                        "E2S24",
                        "expected `,` between match arms",
                        arm_cursor
                    );
                }
            }
            if (
                arm_cursor >= length ||
                !token_equal(source, arm_cursor, "}")
            ) {
                return lower_match_error(
                    &emitted,
                    &dispatch,
                    "E2S24",
                    "missing `}` after match arms",
                    arms_open
                );
            }
            if (!covered_true && !covered_false) {
                return lower_match_error(
                    &emitted,
                    &dispatch,
                    "E2S25",
                    "non-exhaustive Bool match; missing patterns `true`, `false`",
                    match_start
                );
            }
            if (!covered_true) {
                return lower_match_error(
                    &emitted,
                    &dispatch,
                    "E2S25",
                    "non-exhaustive Bool match; missing pattern `true`",
                    match_start
                );
            }
            if (!covered_false) {
                return lower_match_error(
                    &emitted,
                    &dispatch,
                    "E2S25",
                    "non-exhaustive Bool match; missing pattern `false`",
                    match_start
                );
            }
            char *match_value = emit_condition_into(
                source,
                hir,
                value_start,
                value_end,
                "kofun_match_value",
                failure_result,
                "        "
            );
            buffer_format(
                &emitted,
                "    {\n"
                "%s"
                "        (void)kofun_match_value;\n"
                "        bool kofun_match_selected = false;\n"
                "%s"
                "    }\n",
                match_value,
                dispatch.data
            );
            free(match_value);
            free(dispatch.data);
            cursor = skip_trivia(source, token_end(source, arm_cursor));
            }
        } else if (token_equal(source, cursor, "return")) {
            int64_t value_start = skip_trivia(source, token_end(source, cursor));
            if (value_start < length && token_equal(source, value_start, "}")) {
                buffer_append(&emitted, "    return 0;\n");
                cursor = value_start;
            } else if (value_control(source, value_start)) {
                int64_t value_end = -1;
                char *result = parse_value_control(
                    source,
                    value_start,
                    &value_end
                );
                if (strncmp(result, "error[", 6) == 0) {
                    free(emitted.data);
                    return result;
                }
                free(result);
                char *value_body = emit_value_into(
                    source,
                    hir,
                    value_start,
                    value_end,
                    "kofun_result",
                    failure_result
                );
                if (strncmp(value_body, "error[", 6) == 0) {
                    free(emitted.data);
                    return value_body;
                }
                buffer_append(
                    &emitted,
                    "    {\n"
                    "        int64_t kofun_result = INT64_C(0);\n"
                );
                buffer_append(&emitted, value_body);
                if (is_main) {
                    buffer_append(
                        &emitted,
                        "        return (int)kofun_result;\n"
                    );
                } else {
                    buffer_append(
                        &emitted,
                        "        return kofun_result;\n"
                    );
                }
                buffer_append(&emitted, "    }\n");
                free(value_body);
                cursor = skip_trivia(source, value_end);
            } else {
                int64_t value_end = expression_end(source, value_start);
                if (value_end < 0) {
                    free(emitted.data);
                    return lower_error(
                        "E2S12",
                        "invalid return expression",
                        value_start
                    );
                }
                char *value = emit_expression(
                    source,
                    hir,
                    value_start,
                    value_end
                );
                buffer_format(
                    &emitted,
                    "    {\n"
                    "        int64_t kofun_result = %s;\n"
                    "        if (kofun_failed) return %s;\n",
                    value,
                    failure_result
                );
                if (is_main) {
                    buffer_append(
                        &emitted,
                        "        return (int)kofun_result;\n"
                    );
                } else {
                    buffer_append(
                        &emitted,
                        "        return kofun_result;\n"
                    );
                }
                buffer_append(&emitted, "    }\n");
                free(value);
                cursor = skip_trivia(source, value_end);
            }
            returned = true;
        } else if (
            strcmp(token_kind(source, cursor), "identifier") == 0
        ) {
            int64_t assignment_start = cursor;
            char *name = token_copy(source, cursor);
            int64_t equals = skip_trivia(source, token_end(source, cursor));
            if (equals < length && token_equal(source, equals, "=")) {
                char *binding_id = hir_use_binding_id(
                    hir,
                    assignment_start
                );
                if (
                    binding_id[0] == '\0' ||
                    strcmp(binding_id, "-1") == 0
                ) {
                    char *error = assignment_error(
                        "unknown assignment target",
                        name,
                        assignment_start,
                        "declare it before assignment"
                    );
                    free(binding_id);
                    free(name);
                    free(emitted.data);
                    return error;
                }
                char *mutability = hir_binding_field(hir, binding_id, 4);
                if (strcmp(mutability, "mutable") != 0) {
                    char *error = assignment_error(
                        "cannot assign to immutable binding",
                        name,
                        assignment_start,
                        "declare it with `let mut`"
                    );
                    free(mutability);
                    free(binding_id);
                    free(name);
                    free(emitted.data);
                    return error;
                }
                int64_t value_start = skip_trivia(
                    source,
                    token_end(source, equals)
                );
                if (value_control(source, value_start)) {
                    int64_t value_end = -1;
                    char *result = parse_value_control(
                        source,
                        value_start,
                        &value_end
                    );
                    if (strncmp(result, "error[", 6) == 0) {
                        free(mutability);
                        free(binding_id);
                        free(name);
                        free(emitted.data);
                        return result;
                    }
                    free(result);
                    char *value_body = emit_value_into(
                        source,
                        hir,
                        value_start,
                        value_end,
                        "kofun_replacement",
                        failure_result
                    );
                    if (strncmp(value_body, "error[", 6) == 0) {
                        free(mutability);
                        free(binding_id);
                        free(name);
                        free(emitted.data);
                        return value_body;
                    }
                    buffer_append(
                        &emitted,
                        "    {\n"
                        "        int64_t kofun_replacement = INT64_C(0);\n"
                    );
                    buffer_append(&emitted, value_body);
                    buffer_format(
                        &emitted,
                        "        k_b%s = kofun_replacement;\n"
                        "    }\n",
                        binding_id
                    );
                    free(value_body);
                    free(mutability);
                    free(binding_id);
                    free(name);
                    cursor = skip_trivia(source, value_end);
                    continue;
                }
                int64_t value_end = expression_end(source, value_start);
                if (value_end < 0) {
                    free(mutability);
                    free(binding_id);
                    free(name);
                    free(emitted.data);
                    return lower_error(
                        "E2S12",
                        "invalid Int expression",
                        value_start
                    );
                }
                char *value = emit_expression(
                    source,
                    hir,
                    value_start,
                    value_end
                );
                buffer_format(
                    &emitted,
                    "    {\n"
                    "        int64_t kofun_replacement = %s;\n"
                    "        if (kofun_failed) return %s;\n"
                    "        k_b%s = kofun_replacement;\n"
                    "    }\n",
                    value,
                    failure_result,
                    binding_id
                );
                free(value);
                free(mutability);
                free(binding_id);
                cursor = skip_trivia(source, value_end);
            } else {
                int64_t value_end = expression_end(source, cursor);
                if (value_end < 0) {
                    free(name);
                    free(emitted.data);
                    return lower_error(
                        "E2S12",
                        "invalid expression statement",
                        cursor
                    );
                }
                char *value = emit_expression(source, hir, cursor, value_end);
                buffer_format(
                    &emitted,
                    "    (void)%s;\n"
                    "    if (kofun_failed) return %s;\n",
                    value,
                    failure_result
                );
                free(value);
                cursor = skip_trivia(source, value_end);
            }
            free(name);
        } else {
            free(emitted.data);
            return lower_error("E2S10", "unsupported Core statement", cursor);
        }
    }
    if (cursor >= length || !token_equal(source, cursor, "}")) {
        free(emitted.data);
        return lower_error("E2S03", "missing function close", -1);
    }
    if (!returned && append_default && !is_main) {
        free(emitted.data);
        return lower_error(
            "E2S19",
            "Core function may complete without returning Int",
            open
        );
    }
    if (!returned && append_default) {
        buffer_append(&emitted, "    return 0;\n");
    }
    return emitted.data;
}

static char *lower_c(const char *source, const char *hir) {
    int64_t length = (int64_t)strlen(source);
    char *enum_use_check = validate_enum_uses(source, hir);
    if (strncmp(enum_use_check, "error[", 6) == 0) {
        return enum_use_check;
    }
    free(enum_use_check);
    char *type_check = validate_core_types(source, hir);
    if (strncmp(type_check, "error[", 6) == 0) return type_check;
    free(type_check);
    char *call_check = validate_core_calls(source, hir);
    if (strncmp(call_check, "error[", 6) == 0) return call_check;
    free(call_check);

    Buffer prototypes;
    Buffer bodies;
    buffer_init(&prototypes);
    buffer_init(&bodies);
    int64_t cursor = next_function_start(source, 0);
    int64_t main_count = 0;
    while (cursor < length) {
        char *name = function_name(source, cursor);
        char *c_name = c_identifier_name(name);
        if (function_arity(source, name) == -2) {
            Buffer error;
            buffer_init(&error);
            buffer_format(
                &error,
                "error[E2S16]: duplicate Core function `%s` "
                "at byte %" PRId64,
                name,
                cursor
            );
            free(name);
            free(c_name);
            free(prototypes.data);
            free(bodies.data);
            return error.data;
        }
        bool is_main = strcmp(name, "main") == 0;
        int64_t arity = parameter_count(source, cursor);
        char *parameters = core_parameters(source, hir, cursor);
        if (strncmp(parameters, "error[", 6) == 0) {
            free(name);
            free(c_name);
            free(prototypes.data);
            free(bodies.data);
            return parameters;
        }
        const char *c_parameters =
            parameters[0] == '\0' ? "void" : parameters;
        if (is_main) {
            ++main_count;
            if (arity != 0) {
                free(parameters);
                free(name);
                free(c_name);
                free(prototypes.data);
                free(bodies.data);
                return lower_error(
                    "E2S15",
                    "Core main must have zero parameters",
                    -1
                );
            }
        } else {
            buffer_format(
                &prototypes,
                "static int64_t kofun_fn_%s(%s);\n",
                c_name,
                c_parameters
            );
        }
        int64_t open = core_body_open(source, hir, cursor, is_main);
        if (open < 0) {
            Buffer error;
            buffer_init(&error);
            buffer_format(
                &error,
                "error[E2S15]: Core function `%s` requires Int parameters "
                "and an Int return",
                name
            );
            free(parameters);
            free(name);
            free(c_name);
            free(prototypes.data);
            free(bodies.data);
            return error.data;
        }
        char *body = lower_body(source, hir, open, is_main, true, open);
        if (strncmp(body, "error[", 6) == 0) {
            free(parameters);
            free(name);
            free(c_name);
            free(prototypes.data);
            free(bodies.data);
            return body;
        }
        if (is_main) {
            buffer_append(
                &bodies,
                "int main(void) {\n"
                "    (void)kofun_failed;\n"
                "    (void)kofun_add;\n"
                "    (void)kofun_sub;\n"
                "    (void)kofun_mul;\n"
                "    (void)kofun_neg;\n"
                "    (void)kofun_floor_div;\n"
                "    (void)kofun_floor_mod;\n"
            );
            buffer_append(&bodies, body);
            buffer_append(&bodies, "}\n");
        } else {
            buffer_format(
                &bodies,
                "static int64_t kofun_fn_%s(%s) {\n",
                c_name,
                c_parameters
            );
            buffer_append(&bodies, body);
            buffer_append(&bodies, "}\n");
        }
        free(body);
        free(parameters);
        free(name);
        free(c_name);
        cursor = next_function_start(source, function_end(source, cursor));
    }
    if (main_count != 1) {
        free(prototypes.data);
        free(bodies.data);
        return lower_error(
            "E2S15",
            "C11 Core requires exactly one `fn main()`",
            -1
        );
    }
    Buffer output;
    buffer_init(&output);
    buffer_append(
        &output,
        "/* Generated by the Kofun-written Stage 2 Core lowerer. */\n"
        "#include <inttypes.h>\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stdio.h>\n\n"
        "static bool kofun_failed;\n"
        "static inline void kofun_error(const char *message) {\n"
        "    if (!kofun_failed) { fputs(message, stderr); fputc('\\n', stderr); }\n"
        "    kofun_failed = true;\n"
        "}\n"
        "static inline int64_t kofun_add(int64_t a, int64_t b) {\n"
        "    int64_t r; if (__builtin_add_overflow(a, b, &r)) {\n"
        "        kofun_error(\"error[R010]: integer overflow in operator `+`\"); return 0;\n"
        "    } return r;\n"
        "}\n"
        "static inline int64_t kofun_sub(int64_t a, int64_t b) {\n"
        "    int64_t r; if (__builtin_sub_overflow(a, b, &r)) {\n"
        "        kofun_error(\"error[R010]: integer overflow in operator `-`\"); return 0;\n"
        "    } return r;\n"
        "}\n"
        "static inline int64_t kofun_mul(int64_t a, int64_t b) {\n"
        "    int64_t r; if (__builtin_mul_overflow(a, b, &r)) {\n"
        "        kofun_error(\"error[R010]: integer overflow in operator `*`\"); return 0;\n"
        "    } return r;\n"
        "}\n"
        "static inline int64_t kofun_neg(int64_t value) {\n"
        "    if (value == INT64_MIN) {\n"
        "        kofun_error(\"error[R010]: integer overflow in unary operator `-`\"); return 0;\n"
        "    } return -value;\n"
        "}\n"
        "static inline int64_t kofun_floor_div(int64_t a, int64_t b) {\n"
        "    if (b == 0) {\n"
        "        kofun_error(\"error[R010]: operator `//` failed: division by zero\"); return 0;\n"
        "    }\n"
        "    if (a == INT64_MIN && b == -1) {\n"
        "        kofun_error(\"error[R010]: integer overflow in operator `//`\"); return 0;\n"
        "    }\n"
        "    int64_t q = a / b; int64_t r = a % b;\n"
        "    if (r != 0 && ((r < 0) != (b < 0))) { --q; }\n"
        "    return q;\n"
        "}\n"
        "static inline int64_t kofun_floor_mod(int64_t a, int64_t b) {\n"
        "    if (b == 0) {\n"
        "        kofun_error(\"error[R010]: operator `%` failed: division by zero\"); return 0;\n"
        "    }\n"
        "    if (a == INT64_MIN && b == -1) return 0;\n"
        "    int64_t r = a % b;\n"
        "    if (r != 0 && ((r < 0) != (b < 0))) { r += b; }\n"
        "    return r;\n"
        "}\n\n"
    );
    buffer_append(&output, prototypes.data);
    buffer_append(&output, "\n");
    buffer_append(&output, bodies.data);
    free(prototypes.data);
    free(bodies.data);
    return output.data;
}

static bool ends_with(const char *value, const char *suffix) {
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    return value_length >= suffix_length &&
           strcmp(value + value_length - suffix_length, suffix) == 0;
}

static bool unsupported_lowering_error(const char *diagnostic) {
    return strncmp(
               diagnostic,
               "error[E2S10]: unsupported Core statement",
               strlen("error[E2S10]: unsupported Core statement")
           ) == 0 ||
           strncmp(
               diagnostic,
               "error[E2S10]: unsupported Core builtin call ",
               strlen("error[E2S10]: unsupported Core builtin call ")
           ) == 0 ||
           strncmp(
               diagnostic,
               "error[E2S24]: general pattern syntax is parsed ",
               strlen("error[E2S24]: general pattern syntax is parsed ")
           ) == 0;
}

static int compile_file(
    const char *input,
    const char *output,
    const char *ir_output,
    const char *tokens_output
) {
    char *source = read_file(input);
    char *tokens = lex_source(source);
    if (strncmp(tokens, "error[", 6) == 0) {
        puts(tokens);
        free(tokens);
        free(source);
        return 1;
    }
    char *ir = parse_program(source);
    if (strncmp(ir, "error[", 6) == 0) {
        puts(ir);
        free(ir);
        free(tokens);
        free(source);
        return 1;
    }
    write_file(ir_output, ir);
    write_file(tokens_output, tokens);
    if (ends_with(output, ".c")) {
        char *pattern_check = validate_executable_patterns(source);
        if (strncmp(pattern_check, "error[", 6) == 0) {
            int status = unsupported_lowering_error(pattern_check) ? 3 : 1;
            puts(pattern_check);
            free(pattern_check);
            free(ir);
            free(tokens);
            free(source);
            return status;
        }
        free(pattern_check);
        char *hir = build_scope_hir(source);
        if (strncmp(hir, "error[", 6) == 0) {
            char *ownership = borrowed_collection_check(source);
            int status = strcmp(ownership, "ok") == 0 ? 3 : 1;
            puts(hir);
            free(ownership);
            free(hir);
            free(ir);
            free(tokens);
            free(source);
            return status;
        }
        char *c_source = lower_c(source, hir);
        if (strncmp(c_source, "error[", 6) == 0) {
            int status = unsupported_lowering_error(c_source) ? 3 : 1;
            puts(c_source);
            free(c_source);
            free(hir);
            free(ir);
            free(tokens);
            free(source);
            return status;
        }
        Buffer combined_ir;
        buffer_init(&combined_ir);
        buffer_append(&combined_ir, ir);
        buffer_append(&combined_ir, hir);
        write_file(ir_output, combined_ir.data);
        write_file(output, c_source);
        free(combined_ir.data);
        free(c_source);
        free(hir);
    } else {
        write_file(output, source);
    }
    puts(output);
    free(ir);
    free(tokens);
    free(source);
    return 0;
}

static int check_ownership_file(const char *path) {
    char *source = read_file(path);
    char *tokens = lex_source(source);
    if (strncmp(tokens, "error[", 6) == 0) {
        puts(tokens);
        free(tokens);
        free(source);
        return 1;
    }
    char *ir = parse_program(source);
    if (strncmp(ir, "error[", 6) == 0) {
        puts(ir);
        free(ir);
        free(tokens);
        free(source);
        return 1;
    }
    char *result = borrowed_collection_check(source);
    bool ok = strcmp(result, "ok") == 0;
    if (!ok) puts(result);
    free(result);
    free(ir);
    free(tokens);
    free(source);
    return ok ? 0 : 1;
}

static int parse_patterns_file(const char *input, const char *output) {
    char *source = read_file(input);
    char *tokens = lex_source(source);
    if (strncmp(tokens, "error[", 6) == 0) {
        puts(tokens);
        free(tokens);
        free(source);
        return 1;
    }
    free(tokens);
    char *tree = parse_pattern_trees(source);
    write_file(output, tree);
    char *error = pattern_first_error(tree);
    bool ok = error[0] == '\0';
    if (!ok) puts(error);
    free(error);
    free(tree);
    free(source);
    return ok ? 0 : 1;
}

/*
 * kofun.selfhost-hir/v1 emitter (bootstrap/selfhost/hir-v1.md).
 *
 * One typed pre-order walk over the frozen profile surface produces the
 * complete document: deduplicated type table, scope tree, symbols,
 * bindings, and per-function node records. Any construct outside the
 * profile rejects the whole document with diagnostics plus explicit
 * `unsupported` records; a partial typed document is never written.
 */

enum {
    SH_MAX_TYPES = 64,
    SH_MAX_ENV = 512,
    SH_MAX_DEPTH = 32,
};

typedef struct {
    const char *source;
    int64_t length;
    Buffer types;
    Buffer scopes;
    Buffer symbols;
    Buffer bindings;
    Buffer nodes;
    Buffer diagnostics;
    char type_keys[SH_MAX_TYPES][80];
    int64_t type_count;
    int64_t next_scope;
    int64_t next_symbol;
    int64_t next_binding;
    int64_t next_node;
    struct {
        char name[64];
        char type[16];
        int64_t binding_id;
        bool is_mutable;
    } env[SH_MAX_ENV];
    int64_t env_count;
    int64_t scope_stack[SH_MAX_DEPTH];
    int64_t scope_depth;
    struct {
        char name[64];
        char result[16];
        int64_t symbol_id;
        int64_t arity;
        char parameters[8][16];
    } functions[64];
    int64_t function_count;
    int64_t builtin_symbols[16];
    int64_t len_list_symbol;
    char *error;
    char error_code[8];
    char error_message[128];
    int64_t error_at;
} Sh;

static void sh_fail(Sh *sh, const char *code, const char *message, int64_t at) {
    if (sh->error != NULL) return;
    Buffer error;
    buffer_init(&error);
    if (at >= 0) {
        buffer_format(
            &error,
            "error[%s]: %s at byte %" PRId64,
            code,
            message,
            at
        );
    } else {
        buffer_format(&error, "error[%s]: %s", code, message);
    }
    sh->error = error.data;
    snprintf(sh->error_code, sizeof(sh->error_code), "%s", code);
    snprintf(sh->error_message, sizeof(sh->error_message), "%s", message);
    sh->error_at = at;
}

/* hir-v1 record escaping: `\` -> `\\`, `|` -> `\p`, newline -> `\n`. */
static void sh_escaped(Buffer *out, const char *text) {
    for (size_t index = 0; text[index] != '\0'; ++index) {
        char symbol = text[index];
        if (symbol == '\\') {
            buffer_append(out, "\\\\");
        } else if (symbol == '|') {
            buffer_append(out, "\\p");
        } else if (symbol == '\n') {
            buffer_append(out, "\\n");
        } else {
            char one[2] = {symbol, '\0'};
            buffer_append(out, one);
        }
    }
}

static int64_t sh_type_id_key(Sh *sh, const char *key) {
    for (int64_t index = 0; index < sh->type_count; ++index) {
        if (strcmp(sh->type_keys[index], key) == 0) return index;
    }
    if (sh->type_count >= SH_MAX_TYPES) {
        sh_fail(sh, "E2S35", "selfhost-HIR type limit is 64", -1);
        return 0;
    }
    snprintf(
        sh->type_keys[sh->type_count],
        sizeof(sh->type_keys[0]),
        "%s",
        key
    );
    buffer_format(&sh->types, "type|%" PRId64 "|%s\n", sh->type_count, key);
    return sh->type_count++;
}

/* Surface names Int/Bool/Text/Void/List map onto the closed universe. */
static int64_t sh_scalar_type_id(Sh *sh, const char *surface) {
    if (strcmp(surface, "Int") == 0) return sh_type_id_key(sh, "int");
    if (strcmp(surface, "Bool") == 0) return sh_type_id_key(sh, "bool");
    if (strcmp(surface, "Text") == 0) return sh_type_id_key(sh, "text");
    if (strcmp(surface, "Void") == 0) return sh_type_id_key(sh, "void");
    if (strcmp(surface, "List") == 0) {
        return sh_type_id_key(sh, "list-text");
    }
    sh_fail(sh, "E2S15", "type is outside the frozen profile", -1);
    return 0;
}

static int64_t sh_fn_type_id(
    Sh *sh,
    const char *result,
    char parameters[][16],
    int64_t arity
) {
    char key[80];
    int64_t written = snprintf(
        key,
        sizeof(key),
        "fn|%" PRId64,
        sh_scalar_type_id(sh, result)
    );
    for (int64_t index = 0; index < arity; ++index) {
        written += snprintf(
            key + written,
            sizeof(key) - (size_t)written,
            "|%" PRId64,
            sh_scalar_type_id(sh, parameters[index])
        );
    }
    return sh_type_id_key(sh, key);
}

static void sh_scope_open(
    Sh *sh,
    const char *kind,
    int64_t start,
    int64_t end
) {
    int64_t parent = sh->scope_depth > 0 ?
        sh->scope_stack[sh->scope_depth - 1] : sh->next_scope;
    if (sh->scope_depth >= SH_MAX_DEPTH) {
        sh_fail(sh, "E2S35", "lexical scope depth limit is 32", start);
        return;
    }
    buffer_format(
        &sh->scopes,
        "scope|%" PRId64 "|%" PRId64 "|%s|%" PRId64 "|%" PRId64 "\n",
        sh->next_scope,
        parent,
        kind,
        start,
        end
    );
    sh->scope_stack[sh->scope_depth++] = sh->next_scope++;
}

static void sh_scope_close(Sh *sh, int64_t saved_env) {
    if (sh->scope_depth > 0) --sh->scope_depth;
    sh->env_count = saved_env;
}

static int64_t sh_bind(
    Sh *sh,
    const char *name,
    const char *type,
    bool is_mutable,
    const char *symbol_kind,
    int64_t name_start,
    int64_t name_end
) {
    if (sh->env_count >= SH_MAX_ENV) {
        sh_fail(sh, "E2S35", "lexical binding limit is 512", name_start);
        return -1;
    }
    int64_t scope = sh->scope_stack[sh->scope_depth - 1];
    int64_t symbol_id = sh->next_symbol++;
    buffer_format(
        &sh->symbols,
        "symbol|%" PRId64 "|%s|%s|%" PRId64 "|%" PRId64 "|%" PRId64 "\n",
        symbol_id,
        symbol_kind,
        name,
        sh_scalar_type_id(sh, type),
        name_start,
        name_end
    );
    int64_t binding_id = sh->next_binding++;
    buffer_format(
        &sh->bindings,
        "binding|%" PRId64 "|%" PRId64 "|%" PRId64 "|%s|%s|%" PRId64
        "|%" PRId64 "\n",
        binding_id,
        scope,
        symbol_id,
        name,
        is_mutable ? "mut" : "imm",
        name_start,
        name_end
    );
    snprintf(
        sh->env[sh->env_count].name,
        sizeof(sh->env[0].name),
        "%s",
        name
    );
    snprintf(
        sh->env[sh->env_count].type,
        sizeof(sh->env[0].type),
        "%s",
        type
    );
    sh->env[sh->env_count].binding_id = binding_id;
    sh->env[sh->env_count].is_mutable = is_mutable;
    ++sh->env_count;
    return binding_id;
}

static int64_t sh_resolve(Sh *sh, const char *name) {
    for (int64_t index = sh->env_count - 1; index >= 0; --index) {
        if (strcmp(sh->env[index].name, name) == 0) return index;
    }
    return -1;
}

static const char *sh_ownership(const char *type, bool edit) {
    if (edit) return "edit";
    if (strcmp(type, "Text") == 0 || strcmp(type, "List") == 0) {
        return "read";
    }
    return "copy";
}


typedef struct ShExpr ShExpr;
struct ShExpr {
    char kind[16];
    int64_t start;
    int64_t end;
    char type[16];
    char op[4];
    char *text;
    int64_t symbol_id;
    int64_t binding_id;
    ShExpr *left;
    ShExpr *right;
    ShExpr *arguments[8];
    int64_t argument_count;
};

typedef struct ShStmt ShStmt;
typedef struct ShBlock ShBlock;
struct ShBlock {
    int64_t scope_id;
    ShStmt *statements[64];
    int64_t count;
};
struct ShStmt {
    char kind[12];
    int64_t start;
    int64_t end;
    ShExpr *value;
    int64_t binding_id;
    ShBlock *body;
    char else_kind[8];
    ShStmt *else_if;
    ShBlock *else_block;
};

static void sh_free_expr(ShExpr *expr) {
    if (expr == NULL) return;
    free(expr->text);
    sh_free_expr(expr->left);
    sh_free_expr(expr->right);
    for (int64_t index = 0; index < expr->argument_count; ++index) {
        sh_free_expr(expr->arguments[index]);
    }
    free(expr);
}

static void sh_free_stmt(ShStmt *statement);

static void sh_free_block(ShBlock *block) {
    if (block == NULL) return;
    for (int64_t index = 0; index < block->count; ++index) {
        sh_free_stmt(block->statements[index]);
    }
    free(block);
}

static void sh_free_stmt(ShStmt *statement) {
    if (statement == NULL) return;
    sh_free_expr(statement->value);
    sh_free_block(statement->body);
    sh_free_stmt(statement->else_if);
    sh_free_block(statement->else_block);
    free(statement);
}

static ShExpr *sh_expr_new(const char *kind, int64_t start, int64_t end) {
    ShExpr *expr = allocate(sizeof(*expr));
    memset(expr, 0, sizeof(*expr));
    snprintf(expr->kind, sizeof(expr->kind), "%s", kind);
    expr->start = start;
    expr->end = end;
    return expr;
}

static ShExpr *sh_parse_expr(Sh *sh, int64_t *cursor);

static int64_t sh_function_index(Sh *sh, const char *name) {
    for (int64_t index = 0; index < sh->function_count; ++index) {
        if (strcmp(sh->functions[index].name, name) == 0) return index;
    }
    return -1;
}

static ShExpr *sh_parse_primary(Sh *sh, int64_t *cursor) {
    int64_t at = *cursor;
    if (at >= sh->length) {
        sh_fail(sh, "E2S12", "expected expression", at);
        return NULL;
    }
    const char *kind = token_kind(sh->source, at);
    int64_t end = token_end(sh->source, at);
    if (strcmp(kind, "integer") == 0) {
        ShExpr *expr = sh_expr_new("literal-int", at, end);
        snprintf(expr->type, sizeof(expr->type), "Int");
        expr->text = token_copy(sh->source, at);
        *cursor = skip_trivia(sh->source, end);
        return expr;
    }
    if (strcmp(kind, "string") == 0) {
        ShExpr *expr = sh_expr_new("literal-text", at, end);
        snprintf(expr->type, sizeof(expr->type), "Text");
        expr->text = token_copy(sh->source, at);
        *cursor = skip_trivia(sh->source, end);
        return expr;
    }
    if (
        token_equal(sh->source, at, "true") ||
        token_equal(sh->source, at, "false")
    ) {
        ShExpr *expr = sh_expr_new("literal-bool", at, end);
        snprintf(expr->type, sizeof(expr->type), "Bool");
        expr->text = token_copy(sh->source, at);
        *cursor = skip_trivia(sh->source, end);
        return expr;
    }
    if (token_equal(sh->source, at, "(")) {
        *cursor = skip_trivia(sh->source, end);
        ShExpr *inner = sh_parse_expr(sh, cursor);
        if (inner == NULL) return NULL;
        if (
            *cursor >= sh->length ||
            !token_equal(sh->source, *cursor, ")")
        ) {
            sh_fail(sh, "E2S12", "expected `)`", *cursor);
            sh_free_expr(inner);
            return NULL;
        }
        *cursor = skip_trivia(sh->source, token_end(sh->source, *cursor));
        return inner;
    }
    if (strcmp(kind, "identifier") == 0) {
        char *name = token_copy(sh->source, at);
        int64_t after = skip_trivia(sh->source, end);
        if (after < sh->length && token_equal(sh->source, after, "(")) {
            ShExpr *call = sh_expr_new("call", at, end);
            call->text = name;
            *cursor = skip_trivia(sh->source, token_end(sh->source, after));
            while (
                *cursor < sh->length &&
                !token_equal(sh->source, *cursor, ")")
            ) {
                if (call->argument_count >= 8) {
                    sh_fail(sh, "E2S17", "call has too many arguments", at);
                    sh_free_expr(call);
                    return NULL;
                }
                ShExpr *argument = sh_parse_expr(sh, cursor);
                if (argument == NULL) {
                    sh_free_expr(call);
                    return NULL;
                }
                call->arguments[call->argument_count++] = argument;
                if (
                    *cursor < sh->length &&
                    token_equal(sh->source, *cursor, ",")
                ) {
                    *cursor = skip_trivia(
                        sh->source,
                        token_end(sh->source, *cursor)
                    );
                }
            }
            if (*cursor >= sh->length) {
                sh_fail(sh, "E2S12", "expected `)`", at);
                sh_free_expr(call);
                return NULL;
            }
            call->end = token_end(sh->source, *cursor);
            *cursor = skip_trivia(
                sh->source,
                token_end(sh->source, *cursor)
            );
            /* Resolve to a declared function or profile builtin and type
             * the call; `len` picks its overload from the argument. */
            int64_t declared = sh_function_index(sh, call->text);
            if (declared >= 0) {
                if (
                    call->argument_count !=
                    sh->functions[declared].arity
                ) {
                    sh_fail(sh, "E2S17", "wrong call arity", at);
                    sh_free_expr(call);
                    return NULL;
                }
                for (
                    int64_t index = 0;
                    index < call->argument_count;
                    ++index
                ) {
                    if (
                        strcmp(
                            call->arguments[index]->type,
                            sh->functions[declared].parameters[index]
                        ) != 0
                    ) {
                        sh_fail(
                            sh,
                            "E2S15",
                            "call argument type mismatch",
                            call->arguments[index]->start
                        );
                        sh_free_expr(call);
                        return NULL;
                    }
                }
                call->symbol_id = sh->functions[declared].symbol_id;
                snprintf(
                    call->type,
                    sizeof(call->type),
                    "%s",
                    sh->functions[declared].result
                );
                return call;
            }
            int64_t arity = builtin_arity(call->text);
            if (strcmp(call->text, "print") == 0) arity = 1;
            if (arity < 0) {
                sh_fail(sh, "E2S16", "unknown function", at);
                sh_free_expr(call);
                return NULL;
            }
            if (call->argument_count != arity) {
                sh_fail(sh, "E2S17", "wrong builtin arity", at);
                sh_free_expr(call);
                return NULL;
            }
            const char *result = "Void";
            if (strcmp(call->text, "print") != 0) {
                result = builtin_return_type(call->text);
            }
            if (strcmp(call->text, "len") == 0) {
                const char *argument_type = call->arguments[0]->type;
                if (
                    strcmp(argument_type, "Text") != 0 &&
                    strcmp(argument_type, "List") != 0
                ) {
                    sh_fail(
                        sh,
                        "E2S15",
                        "len expects Text or List[Text]",
                        call->arguments[0]->start
                    );
                    sh_free_expr(call);
                    return NULL;
                }
                call->symbol_id =
                    strcmp(argument_type, "List") == 0 ?
                        sh->len_list_symbol :
                        sh->builtin_symbols[7];
            } else if (strcmp(call->text, "print") == 0) {
                if (strcmp(call->arguments[0]->type, "Text") != 0) {
                    sh_fail(
                        sh,
                        "E2S15",
                        "print expects Text in the profile",
                        call->arguments[0]->start
                    );
                    sh_free_expr(call);
                    return NULL;
                }
                call->symbol_id = sh->builtin_symbols[8];
            } else {
                const char *parameters =
                    builtin_parameter_types(call->text);
                const char *expected = parameters;
                for (
                    int64_t index = 0;
                    index < call->argument_count;
                    ++index
                ) {
                    size_t expected_length = strcspn(expected, "|");
                    bool matches =
                        strlen(call->arguments[index]->type) ==
                            expected_length &&
                        strncmp(
                            call->arguments[index]->type,
                            expected,
                            expected_length
                        ) == 0;
                    if (!matches) {
                        sh_fail(
                            sh,
                            "E2S15",
                            "builtin argument type mismatch",
                            call->arguments[index]->start
                        );
                        sh_free_expr(call);
                        return NULL;
                    }
                    expected += expected_length;
                    if (expected[0] == '|') ++expected;
                }
                int64_t slot = -1;
                static const char *ordered[] = {
                    "args", "chars", "contains", "find", "is_digit",
                    "is_space", "is_xid_continue", "len", "print",
                    "read_text", "replace", "starts_with", "text_slice",
                    "trim", "validate_unicode_source", "write_text",
                };
                for (int64_t index = 0; index < 16; ++index) {
                    if (strcmp(ordered[index], call->text) == 0) {
                        slot = index;
                        break;
                    }
                }
                call->symbol_id = sh->builtin_symbols[slot];
            }
            snprintf(call->type, sizeof(call->type), "%s", result);
            return call;
        }
        int64_t resolved = sh_resolve(sh, name);
        if (resolved < 0) {
            sh_fail(sh, "E2S35", "unknown lexical binding", at);
            free(name);
            return NULL;
        }
        ShExpr *reference = sh_expr_new("name", at, end);
        reference->text = name;
        reference->binding_id = sh->env[resolved].binding_id;
        snprintf(
            reference->type,
            sizeof(reference->type),
            "%s",
            sh->env[resolved].type
        );
        *cursor = after;
        return reference;
    }
    sh_fail(sh, "E2S12", "expected expression", at);
    return NULL;
}

static ShExpr *sh_parse_postfix(Sh *sh, int64_t *cursor) {
    ShExpr *base = sh_parse_primary(sh, cursor);
    if (base == NULL) return NULL;
    while (
        *cursor < sh->length &&
        token_equal(sh->source, *cursor, "[")
    ) {
        if (strcmp(base->type, "List") != 0) {
            sh_fail(sh, "E2S15", "only List[Text] can be indexed", *cursor);
            sh_free_expr(base);
            return NULL;
        }
        *cursor = skip_trivia(sh->source, token_end(sh->source, *cursor));
        ShExpr *index_expr = sh_parse_expr(sh, cursor);
        if (index_expr == NULL) {
            sh_free_expr(base);
            return NULL;
        }
        if (strcmp(index_expr->type, "Int") != 0) {
            sh_fail(sh, "E2S15", "index must be Int", index_expr->start);
            sh_free_expr(base);
            sh_free_expr(index_expr);
            return NULL;
        }
        if (
            *cursor >= sh->length ||
            !token_equal(sh->source, *cursor, "]")
        ) {
            sh_fail(sh, "E2S12", "expected `]`", *cursor);
            sh_free_expr(base);
            sh_free_expr(index_expr);
            return NULL;
        }
        int64_t close = token_end(sh->source, *cursor);
        *cursor = skip_trivia(sh->source, close);
        ShExpr *indexed = sh_expr_new("index", base->start, close);
        snprintf(indexed->type, sizeof(indexed->type), "Text");
        indexed->left = base;
        indexed->right = index_expr;
        base = indexed;
    }
    return base;
}

static ShExpr *sh_parse_unary(Sh *sh, int64_t *cursor) {
    int64_t at = *cursor;
    if (at < sh->length && token_equal(sh->source, at, "!")) {
        *cursor = skip_trivia(sh->source, token_end(sh->source, at));
        ShExpr *operand = sh_parse_unary(sh, cursor);
        if (operand == NULL) return NULL;
        if (strcmp(operand->type, "Bool") != 0) {
            sh_fail(sh, "E2S15", "`!` expects Bool", operand->start);
            sh_free_expr(operand);
            return NULL;
        }
        ShExpr *expr = sh_expr_new("unary", at, operand->end);
        snprintf(expr->type, sizeof(expr->type), "Bool");
        snprintf(expr->op, sizeof(expr->op), "!");
        expr->left = operand;
        return expr;
    }
    if (at < sh->length && token_equal(sh->source, at, "-")) {
        *cursor = skip_trivia(sh->source, token_end(sh->source, at));
        ShExpr *operand = sh_parse_unary(sh, cursor);
        if (operand == NULL) return NULL;
        if (strcmp(operand->type, "Int") != 0) {
            sh_fail(sh, "E2S15", "unary `-` expects Int", operand->start);
            sh_free_expr(operand);
            return NULL;
        }
        ShExpr *expr = sh_expr_new("unary", at, operand->end);
        snprintf(expr->type, sizeof(expr->type), "Int");
        snprintf(expr->op, sizeof(expr->op), "-");
        expr->left = operand;
        return expr;
    }
    return sh_parse_postfix(sh, cursor);
}

static bool sh_operator_at(
    Sh *sh,
    int64_t cursor,
    const char *const *operators,
    int64_t count,
    const char **matched
) {
    if (cursor >= sh->length) return false;
    for (int64_t index = 0; index < count; ++index) {
        if (token_equal(sh->source, cursor, operators[index])) {
            *matched = operators[index];
            return true;
        }
    }
    return false;
}

static ShExpr *sh_parse_binary_level(
    Sh *sh,
    int64_t *cursor,
    int64_t level
);

/* Levels: 0 `||`; 1 `&&`; 2 comparisons; 3 `+ -`; 4 `* / // %`. */
static ShExpr *sh_parse_binary_level(
    Sh *sh,
    int64_t *cursor,
    int64_t level
) {
    static const char *const level0[] = {"||"};
    static const char *const level1[] = {"&&"};
    static const char *const level2[] =
        {"==", "!=", "<=", ">=", "<", ">"};
    static const char *const level3[] = {"+", "-"};
    static const char *const level4[] = {"*", "//", "/", "%"};
    static const struct {
        const char *const *operators;
        int64_t count;
    } levels[] = {
        {level0, 1}, {level1, 1}, {level2, 6}, {level3, 2}, {level4, 4},
    };
    if (level > 4) return sh_parse_unary(sh, cursor);
    ShExpr *left = sh_parse_binary_level(sh, cursor, level + 1);
    if (left == NULL) return NULL;
    const char *matched = NULL;
    while (
        sh_operator_at(
            sh,
            *cursor,
            levels[level].operators,
            levels[level].count,
            &matched
        )
    ) {
        int64_t operator_at = *cursor;
        *cursor = skip_trivia(
            sh->source,
            token_end(sh->source, operator_at)
        );
        ShExpr *right = sh_parse_binary_level(sh, cursor, level + 1);
        if (right == NULL) {
            sh_free_expr(left);
            return NULL;
        }
        const char *result = NULL;
        if (level <= 1) {
            if (
                strcmp(left->type, "Bool") != 0 ||
                strcmp(right->type, "Bool") != 0
            ) {
                sh_fail(sh, "E2S15", "logical operands must be Bool",
                        operator_at);
            }
            result = "Bool";
        } else if (level == 2) {
            if (
                strcmp(left->type, right->type) != 0 ||
                strcmp(left->type, "List") == 0 ||
                strcmp(left->type, "Void") == 0
            ) {
                sh_fail(sh, "E2S15",
                        "comparison operands must share a scalar type",
                        operator_at);
            }
            result = "Bool";
        } else if (level == 3 && strcmp(matched, "+") == 0 &&
                   strcmp(left->type, "Text") == 0) {
            if (strcmp(right->type, "Text") != 0) {
                sh_fail(sh, "E2S15", "Text `+` expects Text", operator_at);
            }
            result = "Text";
        } else {
            if (
                strcmp(left->type, "Int") != 0 ||
                strcmp(right->type, "Int") != 0
            ) {
                sh_fail(sh, "E2S15", "arithmetic operands must be Int",
                        operator_at);
            }
            result = "Int";
        }
        if (sh->error != NULL) {
            sh_free_expr(left);
            sh_free_expr(right);
            return NULL;
        }
        ShExpr *parent = sh_expr_new("binary", left->start, right->end);
        snprintf(parent->type, sizeof(parent->type), "%s", result);
        snprintf(parent->op, sizeof(parent->op), "%s", matched);
        parent->left = left;
        parent->right = right;
        left = parent;
    }
    return left;
}

static ShExpr *sh_parse_expr(Sh *sh, int64_t *cursor) {
    return sh_parse_binary_level(sh, cursor, 0);
}

static ShBlock *sh_parse_block(
    Sh *sh,
    int64_t *cursor,
    const char *declared,
    const char *loop_name,
    int64_t loop_name_start,
    int64_t *loop_binding_out
);

static ShStmt *sh_stmt_new(const char *kind, int64_t start) {
    ShStmt *statement = allocate(sizeof(*statement));
    memset(statement, 0, sizeof(*statement));
    snprintf(statement->kind, sizeof(statement->kind), "%s", kind);
    statement->start = start;
    return statement;
}

static ShStmt *sh_parse_stmt(
    Sh *sh,
    int64_t *cursor,
    const char *declared
) {
    int64_t at = *cursor;
    if (token_equal(sh->source, at, "let")) {
        int64_t name = skip_trivia(sh->source, token_end(sh->source, at));
        bool is_mutable = false;
        if (name < sh->length && token_equal(sh->source, name, "mut")) {
            is_mutable = true;
            name = skip_trivia(sh->source, token_end(sh->source, name));
        }
        if (
            name >= sh->length ||
            strcmp(token_kind(sh->source, name), "identifier") != 0
        ) {
            sh_fail(sh, "E2S12", "expected binding name", name);
            return NULL;
        }
        int64_t name_end = token_end(sh->source, name);
        int64_t after = skip_trivia(sh->source, name_end);
        char annotation[16] = "";
        if (after < sh->length && token_equal(sh->source, after, ":")) {
            int64_t type_at = skip_trivia(
                sh->source,
                token_end(sh->source, after)
            );
            char *type_text = token_copy(sh->source, type_at);
            snprintf(annotation, sizeof(annotation), "%s", type_text);
            free(type_text);
            after = skip_trivia(sh->source, token_end(sh->source, type_at));
            if (strcmp(annotation, "List") == 0) {
                /* List [ Text ] */
                after = skip_trivia(sh->source, token_end(sh->source, after));
                after = skip_trivia(sh->source, token_end(sh->source, after));
            }
        }
        if (after >= sh->length || !token_equal(sh->source, after, "=")) {
            sh_fail(sh, "E2S12", "expected `=`", after);
            return NULL;
        }
        *cursor = skip_trivia(sh->source, token_end(sh->source, after));
        ShExpr *value = sh_parse_expr(sh, cursor);
        if (value == NULL) return NULL;
        if (annotation[0] != '\0' &&
            strcmp(annotation, value->type) != 0) {
            sh_fail(sh, "E2S15", "initializer type mismatch", value->start);
            sh_free_expr(value);
            return NULL;
        }
        char *name_text = token_copy(sh->source, name);
        int64_t binding = sh_bind(
            sh,
            name_text,
            value->type,
            is_mutable,
            "local",
            name,
            name_end
        );
        free(name_text);
        if (binding < 0) {
            sh_free_expr(value);
            return NULL;
        }
        ShStmt *statement = sh_stmt_new(
            is_mutable ? "let-mut" : "let",
            at
        );
        statement->end = value->end;
        statement->value = value;
        statement->binding_id = binding;
        return statement;
    }
    if (token_equal(sh->source, at, "if") ||
        token_equal(sh->source, at, "while")) {
        bool is_if = token_equal(sh->source, at, "if");
        *cursor = skip_trivia(sh->source, token_end(sh->source, at));
        ShExpr *condition = sh_parse_expr(sh, cursor);
        if (condition == NULL) return NULL;
        if (strcmp(condition->type, "Bool") != 0) {
            sh_fail(
                sh,
                "E2S23",
                is_if ?
                    "if condition must be Bool or an Int comparison" :
                    "while condition must be Bool",
                condition->start
            );
            sh_free_expr(condition);
            return NULL;
        }
        ShBlock *body = sh_parse_block(sh, cursor, declared, NULL, -1, NULL);
        if (body == NULL) {
            sh_free_expr(condition);
            return NULL;
        }
        ShStmt *statement = sh_stmt_new(is_if ? "if" : "while", at);
        statement->value = condition;
        statement->body = body;
        snprintf(statement->else_kind, sizeof(statement->else_kind), "none");
        if (
            is_if &&
            *cursor < sh->length &&
            token_equal(sh->source, *cursor, "else")
        ) {
            int64_t next = skip_trivia(
                sh->source,
                token_end(sh->source, *cursor)
            );
            if (next < sh->length && token_equal(sh->source, next, "if")) {
                *cursor = next;
                ShStmt *chained = sh_parse_stmt(sh, cursor, declared);
                if (chained == NULL) {
                    sh_free_stmt(statement);
                    return NULL;
                }
                snprintf(
                    statement->else_kind,
                    sizeof(statement->else_kind),
                    "if"
                );
                statement->else_if = chained;
            } else {
                *cursor = next;
                ShBlock *alternative = sh_parse_block(
                    sh,
                    cursor,
                    declared,
                    NULL,
                    -1,
                    NULL
                );
                if (alternative == NULL) {
                    sh_free_stmt(statement);
                    return NULL;
                }
                snprintf(
                    statement->else_kind,
                    sizeof(statement->else_kind),
                    "block"
                );
                statement->else_block = alternative;
            }
        }
        statement->end = *cursor;
        return statement;
    }
    if (token_equal(sh->source, at, "for")) {
        int64_t name = skip_trivia(sh->source, token_end(sh->source, at));
        if (
            name >= sh->length ||
            strcmp(token_kind(sh->source, name), "identifier") != 0
        ) {
            sh_fail(sh, "E2S12", "expected loop variable", name);
            return NULL;
        }
        int64_t in_at = skip_trivia(
            sh->source,
            token_end(sh->source, name)
        );
        if (in_at >= sh->length || !token_equal(sh->source, in_at, "in")) {
            sh_fail(sh, "E2S12", "expected `in`", in_at);
            return NULL;
        }
        *cursor = skip_trivia(sh->source, token_end(sh->source, in_at));
        ShExpr *low = sh_parse_expr(sh, cursor);
        if (low == NULL) return NULL;
        if (
            *cursor >= sh->length ||
            !token_equal(sh->source, *cursor, "..")
        ) {
            sh_fail(sh, "E2S12", "expected `..`", *cursor);
            sh_free_expr(low);
            return NULL;
        }
        *cursor = skip_trivia(sh->source, token_end(sh->source, *cursor));
        ShExpr *high = sh_parse_expr(sh, cursor);
        if (high == NULL) {
            sh_free_expr(low);
            return NULL;
        }
        if (
            strcmp(low->type, "Int") != 0 ||
            strcmp(high->type, "Int") != 0
        ) {
            sh_fail(sh, "E2S15", "range bounds must be Int", low->start);
            sh_free_expr(low);
            sh_free_expr(high);
            return NULL;
        }
        ShExpr *range = sh_expr_new("range", low->start, high->end);
        snprintf(range->type, sizeof(range->type), "Int");
        range->left = low;
        range->right = high;
        char *loop_name = token_copy(sh->source, name);
        int64_t loop_binding = -1;
        ShBlock *body = sh_parse_block(
            sh,
            cursor,
            declared,
            loop_name,
            name,
            &loop_binding
        );
        free(loop_name);
        if (body == NULL) {
            sh_free_expr(range);
            return NULL;
        }
        ShStmt *statement = sh_stmt_new("for-range", at);
        statement->value = range;
        statement->body = body;
        statement->binding_id = loop_binding;
        statement->end = *cursor;
        return statement;
    }
    if (token_equal(sh->source, at, "return")) {
        int64_t value_at = skip_trivia(
            sh->source,
            token_end(sh->source, at)
        );
        bool bare =
            value_at >= sh->length ||
            token_equal(sh->source, value_at, "}") ||
            newline_between(
                sh->source,
                token_end(sh->source, at),
                value_at
            );
        ShStmt *statement = sh_stmt_new("return", at);
        if (bare) {
            if (strcmp(declared, "Void") != 0) {
                sh_fail(sh, "E2S19", "missing return value", at);
                sh_free_stmt(statement);
                return NULL;
            }
            statement->end = token_end(sh->source, at);
            *cursor = value_at;
            return statement;
        }
        *cursor = value_at;
        ShExpr *value = sh_parse_expr(sh, cursor);
        if (value == NULL) {
            sh_free_stmt(statement);
            return NULL;
        }
        if (strcmp(value->type, declared) != 0) {
            sh_fail(sh, "E2S15", "return type mismatch", value->start);
            sh_free_expr(value);
            sh_free_stmt(statement);
            return NULL;
        }
        statement->value = value;
        statement->end = value->end;
        return statement;
    }
    if (strcmp(token_kind(sh->source, at), "identifier") == 0) {
        int64_t after = skip_trivia(sh->source, token_end(sh->source, at));
        if (after < sh->length && token_equal(sh->source, after, "=") &&
            !token_equal(sh->source, after, "==")) {
            int64_t resolved;
            char *name_text = token_copy(sh->source, at);
            resolved = sh_resolve(sh, name_text);
            if (resolved < 0) {
                sh_fail(sh, "E2S35", "unknown lexical binding", at);
                free(name_text);
                return NULL;
            }
            if (!sh->env[resolved].is_mutable) {
                sh_fail(sh, "E2S22", "assignment target is immutable", at);
                free(name_text);
                return NULL;
            }
            free(name_text);
            *cursor = skip_trivia(
                sh->source,
                token_end(sh->source, after)
            );
            ShExpr *value = sh_parse_expr(sh, cursor);
            if (value == NULL) return NULL;
            if (strcmp(value->type, sh->env[resolved].type) != 0) {
                sh_fail(sh, "E2S15", "assignment type mismatch",
                        value->start);
                sh_free_expr(value);
                return NULL;
            }
            ShStmt *statement = sh_stmt_new("assign", at);
            statement->value = value;
            statement->binding_id = sh->env[resolved].binding_id;
            statement->end = value->end;
            return statement;
        }
        ShExpr *value = sh_parse_expr(sh, cursor);
        if (value == NULL) return NULL;
        ShStmt *statement = sh_stmt_new("expr-stmt", at);
        statement->value = value;
        statement->end = value->end;
        return statement;
    }
    sh_fail(sh, "E2S10", "unsupported Core statement", at);
    return NULL;
}

static ShBlock *sh_parse_block(
    Sh *sh,
    int64_t *cursor,
    const char *declared,
    const char *loop_name,
    int64_t loop_name_start,
    int64_t *loop_binding_out
) {
    if (*cursor >= sh->length || !token_equal(sh->source, *cursor, "{")) {
        sh_fail(sh, "E2S18", "expected `{`", *cursor);
        return NULL;
    }
    int64_t open = *cursor;
    int64_t close = balanced_end(sh->source, open, "{", "}");
    if (close < 0) {
        sh_fail(sh, "E2S18", "unbalanced `{`", open);
        return NULL;
    }
    int64_t saved_env = sh->env_count;
    sh_scope_open(sh, "block", open, close);
    if (sh->error != NULL) return NULL;
    ShBlock *block = allocate(sizeof(*block));
    memset(block, 0, sizeof(*block));
    block->scope_id = sh->scope_stack[sh->scope_depth - 1];
    if (loop_name != NULL) {
        int64_t loop_binding = sh_bind(
            sh,
            loop_name,
            "Int",
            false,
            "local",
            loop_name_start,
            token_end(sh->source, loop_name_start)
        );
        if (loop_binding_out != NULL) *loop_binding_out = loop_binding;
    }
    *cursor = skip_trivia(sh->source, token_end(sh->source, open));
    while (
        sh->error == NULL &&
        *cursor < sh->length &&
        !token_equal(sh->source, *cursor, "}")
    ) {
        if (block->count >= 64) {
            sh_fail(sh, "E2S35", "block statement limit is 64", *cursor);
            break;
        }
        ShStmt *statement = sh_parse_stmt(sh, cursor, declared);
        if (statement == NULL) break;
        block->statements[block->count++] = statement;
    }
    if (sh->error == NULL &&
        (*cursor >= sh->length ||
         !token_equal(sh->source, *cursor, "}"))) {
        sh_fail(sh, "E2S18", "expected `}`", *cursor);
    }
    if (sh->error != NULL) {
        sh_scope_close(sh, saved_env);
        sh_free_block(block);
        return NULL;
    }
    *cursor = skip_trivia(sh->source, token_end(sh->source, *cursor));
    sh_scope_close(sh, saved_env);
    return block;
}

/* Decode source string escapes, then re-escape for the record format. */
static void sh_text_literal_field(Buffer *out, const char *token) {
    Buffer decoded;
    buffer_init(&decoded);
    size_t length = strlen(token);
    for (size_t index = 1; index + 1 < length; ++index) {
        char symbol = token[index];
        if (symbol == '\\' && index + 2 < length + 1) {
            char next = token[index + 1];
            char one[2] = {next, '\0'};
            if (next == 'n') one[0] = '\n';
            buffer_append(&decoded, one);
            ++index;
        } else {
            char one[2] = {symbol, '\0'};
            buffer_append(&decoded, one);
        }
    }
    sh_escaped(out, decoded.data);
    free(decoded.data);
}

static int64_t sh_emit_expr(Sh *sh, Buffer *out, ShExpr *expr) {
    int64_t id = sh->next_node++;
    int64_t type_id = sh_scalar_type_id(sh, expr->type);
    const char *ownership = sh_ownership(expr->type, false);
    Buffer children;
    buffer_init(&children);
    Buffer fields;
    buffer_init(&fields);
    if (strcmp(expr->kind, "literal-int") == 0 ||
        strcmp(expr->kind, "literal-bool") == 0) {
        buffer_append(&fields, expr->text);
    } else if (strcmp(expr->kind, "literal-text") == 0) {
        sh_text_literal_field(&fields, expr->text);
    } else if (strcmp(expr->kind, "name") == 0) {
        buffer_format(&fields, "%" PRId64, expr->binding_id);
    } else if (strcmp(expr->kind, "call") == 0) {
        buffer_format(&fields, "%" PRId64, expr->symbol_id);
        for (int64_t index = 0; index < expr->argument_count; ++index) {
            int64_t argument = sh_emit_expr(
                sh,
                &children,
                expr->arguments[index]
            );
            buffer_format(&fields, "|%" PRId64, argument);
        }
    } else if (strcmp(expr->kind, "unary") == 0) {
        int64_t operand = sh_emit_expr(sh, &children, expr->left);
        sh_escaped(&fields, expr->op);
        buffer_format(&fields, "|%" PRId64, operand);
    } else if (strcmp(expr->kind, "binary") == 0) {
        int64_t left = sh_emit_expr(sh, &children, expr->left);
        int64_t right = sh_emit_expr(sh, &children, expr->right);
        sh_escaped(&fields, expr->op);
        buffer_format(&fields, "|%" PRId64 "|%" PRId64, left, right);
    } else if (strcmp(expr->kind, "index") == 0 ||
               strcmp(expr->kind, "range") == 0) {
        int64_t left = sh_emit_expr(sh, &children, expr->left);
        int64_t right = sh_emit_expr(sh, &children, expr->right);
        buffer_format(&fields, "%" PRId64 "|%" PRId64, left, right);
    }
    buffer_format(
        out,
        "node|%" PRId64 "|%s|%" PRId64 "|%" PRId64 "|%" PRId64 "|%s|%s\n",
        id,
        expr->kind,
        expr->start,
        expr->end,
        type_id,
        ownership,
        fields.data
    );
    buffer_append(out, children.data);
    free(children.data);
    free(fields.data);
    return id;
}

static void sh_emit_block(Sh *sh, Buffer *out, ShBlock *block);

static int64_t sh_emit_stmt(Sh *sh, Buffer *out, ShStmt *statement) {
    int64_t id = sh->next_node++;
    int64_t void_id = sh_scalar_type_id(sh, "Void");
    Buffer children;
    buffer_init(&children);
    Buffer fields;
    buffer_init(&fields);
    const char *ownership = "copy";
    if (strcmp(statement->kind, "let") == 0 ||
        strcmp(statement->kind, "let-mut") == 0) {
        int64_t value = sh_emit_expr(sh, &children, statement->value);
        buffer_format(
            &fields,
            "%" PRId64 "|%" PRId64,
            statement->binding_id,
            value
        );
    } else if (strcmp(statement->kind, "assign") == 0) {
        ownership = "edit";
        int64_t value = sh_emit_expr(sh, &children, statement->value);
        buffer_format(
            &fields,
            "%" PRId64 "|%" PRId64,
            statement->binding_id,
            value
        );
    } else if (strcmp(statement->kind, "if") == 0) {
        int64_t condition = sh_emit_expr(sh, &children, statement->value);
        sh_emit_block(sh, &children, statement->body);
        int64_t else_reference = -1;
        if (strcmp(statement->else_kind, "if") == 0) {
            else_reference = sh_emit_stmt(
                sh,
                &children,
                statement->else_if
            );
        } else if (strcmp(statement->else_kind, "block") == 0) {
            else_reference = statement->else_block->scope_id;
            sh_emit_block(sh, &children, statement->else_block);
        }
        buffer_format(
            &fields,
            "%" PRId64 "|%" PRId64 "|%s|%" PRId64,
            condition,
            statement->body->scope_id,
            statement->else_kind,
            else_reference
        );
    } else if (strcmp(statement->kind, "while") == 0) {
        int64_t condition = sh_emit_expr(sh, &children, statement->value);
        sh_emit_block(sh, &children, statement->body);
        buffer_format(
            &fields,
            "%" PRId64 "|%" PRId64,
            condition,
            statement->body->scope_id
        );
    } else if (strcmp(statement->kind, "for-range") == 0) {
        int64_t range = sh_emit_expr(sh, &children, statement->value);
        sh_emit_block(sh, &children, statement->body);
        buffer_format(
            &fields,
            "%" PRId64 "|%" PRId64 "|%" PRId64,
            statement->binding_id,
            range,
            statement->body->scope_id
        );
    } else if (strcmp(statement->kind, "return") == 0) {
        if (statement->value != NULL) {
            int64_t value = sh_emit_expr(sh, &children, statement->value);
            buffer_format(&fields, "%" PRId64, value);
        } else {
            buffer_append(&fields, "none");
        }
    } else if (strcmp(statement->kind, "expr-stmt") == 0) {
        int64_t value = sh_emit_expr(sh, &children, statement->value);
        buffer_format(&fields, "%" PRId64, value);
    }
    buffer_format(
        out,
        "node|%" PRId64 "|%s|%" PRId64 "|%" PRId64 "|%" PRId64 "|%s|%s\n",
        id,
        statement->kind,
        statement->start,
        statement->end,
        void_id,
        ownership,
        fields.data
    );
    buffer_append(out, children.data);
    free(children.data);
    free(fields.data);
    return id;
}

static void sh_emit_block(Sh *sh, Buffer *out, ShBlock *block) {
    for (int64_t index = 0; index < block->count; ++index) {
        sh_emit_stmt(sh, out, block->statements[index]);
    }
}

static bool sh_parse_signature(Sh *sh, int64_t function_start) {
    if (sh->function_count >= 64) {
        sh_fail(sh, "E2S16", "function limit is 64", function_start);
        return false;
    }
    char *name = function_name(sh->source, function_start);
    if (sh_function_index(sh, name) >= 0) {
        sh_fail(sh, "E2S16", "duplicate Core function", function_start);
        free(name);
        return false;
    }
    int64_t slot = sh->function_count;
    snprintf(
        sh->functions[slot].name,
        sizeof(sh->functions[0].name),
        "%s",
        name
    );
    free(name);
    int64_t parameters = parameter_open(sh->source, function_start);
    int64_t parameters_close = parameters >= 0 ?
        balanced_end(sh->source, parameters, "(", ")") : -1;
    if (parameters < 0 || parameters_close < 0) {
        sh_fail(sh, "E2S15", "malformed parameter list", function_start);
        return false;
    }
    int64_t arity = 0;
    int64_t cursor = skip_trivia(
        sh->source,
        token_end(sh->source, parameters)
    );
    while (
        cursor < parameters_close &&
        !token_equal(sh->source, cursor, ")")
    ) {
        if (arity >= 8) {
            sh_fail(sh, "E2S17", "parameter limit is 8", cursor);
            return false;
        }
        int64_t colon = skip_trivia(
            sh->source,
            token_end(sh->source, cursor)
        );
        int64_t type_at = skip_trivia(
            sh->source,
            token_end(sh->source, colon)
        );
        if (
            colon >= parameters_close ||
            !token_equal(sh->source, colon, ":")
        ) {
            sh_fail(sh, "E2S15", "parameter needs `: TYPE`", cursor);
            return false;
        }
        char *type_text = token_copy(sh->source, type_at);
        snprintf(
            sh->functions[slot].parameters[arity],
            sizeof(sh->functions[0].parameters[0]),
            "%s",
            type_text
        );
        free(type_text);
        cursor = skip_trivia(sh->source, token_end(sh->source, type_at));
        if (strcmp(sh->functions[slot].parameters[arity], "List") == 0) {
            /* consume `[ Text ]` */
            cursor = skip_trivia(sh->source, token_end(sh->source, cursor));
            cursor = skip_trivia(sh->source, token_end(sh->source, cursor));
        }
        ++arity;
        if (
            cursor < parameters_close &&
            token_equal(sh->source, cursor, ",")
        ) {
            cursor = skip_trivia(sh->source, token_end(sh->source, cursor));
        }
    }
    sh->functions[slot].arity = arity;
    int64_t after = skip_trivia(sh->source, parameters_close);
    if (after < sh->length && token_equal(sh->source, after, "->")) {
        int64_t result_at = skip_trivia(
            sh->source,
            token_end(sh->source, after)
        );
        char *result_text = token_copy(sh->source, result_at);
        snprintf(
            sh->functions[slot].result,
            sizeof(sh->functions[0].result),
            "%s",
            result_text
        );
        free(result_text);
    } else {
        snprintf(
            sh->functions[slot].result,
            sizeof(sh->functions[0].result),
            "Void"
        );
    }
    ++sh->function_count;
    return true;
}

static char *emit_selfhost_hir_document(
    const char *source,
    const char *path,
    const char *digest,
    bool *complete_out
) {
    Sh sh;
    memset(&sh, 0, sizeof(sh));
    sh.source = source;
    sh.length = (int64_t)strlen(source);
    buffer_init(&sh.types);
    buffer_init(&sh.scopes);
    buffer_init(&sh.symbols);
    buffer_init(&sh.bindings);
    buffer_init(&sh.nodes);
    buffer_init(&sh.diagnostics);
    sh_scope_open(&sh, "module", 0, sh.length);

    int64_t function_start = next_function_start(source, 0);
    if (function_start >= sh.length) {
        sh_fail(&sh, "E2S04", "source declares no functions", 0);
    }
    while (sh.error == NULL && function_start < sh.length) {
        if (!sh_parse_signature(&sh, function_start)) break;
        /* A declaration whose body never closes ends the walk; its body
         * parse reports the exact brace diagnostic later. */
        int64_t signature_close = function_end(source, function_start);
        if (signature_close < 0) break;
        function_start = next_function_start(source, signature_close);
    }

    /* Function symbols and module bindings, in source order. */
    function_start = next_function_start(source, 0);
    for (
        int64_t index = 0;
        sh.error == NULL && index < sh.function_count;
        ++index
    ) {
        int64_t name_at = skip_trivia(
            source,
            token_end(source, function_start)
        );
        int64_t name_end = token_end(source, name_at);
        int64_t fn_type = sh_fn_type_id(
            &sh,
            sh.functions[index].result,
            sh.functions[index].parameters,
            sh.functions[index].arity
        );
        int64_t symbol_id = sh.next_symbol++;
        sh.functions[index].symbol_id = symbol_id;
        buffer_format(
            &sh.symbols,
            "symbol|%" PRId64 "|function|%s|%" PRId64 "|%" PRId64
            "|%" PRId64 "\n",
            symbol_id,
            sh.functions[index].name,
            fn_type,
            name_at,
            name_end
        );
        buffer_format(
            &sh.bindings,
            "binding|%" PRId64 "|0|%" PRId64 "|%s|imm|%" PRId64
            "|%" PRId64 "\n",
            sh.next_binding++,
            symbol_id,
            sh.functions[index].name,
            name_at,
            name_end
        );
        int64_t symbol_close = function_end(source, function_start);
        if (symbol_close < 0) break;
        function_start = next_function_start(source, symbol_close);
    }

    /* The 16 builtin symbols plus the len List[Text] overload. */
    {
        static const struct {
            const char *name;
            const char *result;
            const char *parameters[3];
            int64_t arity;
        } builtins[] = {
            {"args", "List", {NULL}, 0},
            {"chars", "List", {"Text"}, 1},
            {"contains", "Bool", {"Text", "Text"}, 2},
            {"find", "Int", {"Text", "Text"}, 2},
            {"is_digit", "Bool", {"Text"}, 1},
            {"is_space", "Bool", {"Text"}, 1},
            {"is_xid_continue", "Bool", {"Text"}, 1},
            {"len", "Int", {"Text"}, 1},
            {"print", "Void", {"Text"}, 1},
            {"read_text", "Text", {"Text"}, 1},
            {"replace", "Text", {"Text", "Text", "Text"}, 3},
            {"starts_with", "Bool", {"Text", "Text"}, 2},
            {"text_slice", "Text", {"Text", "Int", "Int"}, 3},
            {"trim", "Text", {"Text"}, 1},
            {"validate_unicode_source", "Text", {"Text"}, 1},
            {"write_text", "Void", {"Text", "Text"}, 2},
        };
        for (int64_t index = 0; sh.error == NULL && index < 16; ++index) {
            char parameters[8][16];
            for (int64_t p = 0; p < builtins[index].arity; ++p) {
                snprintf(
                    parameters[p],
                    sizeof(parameters[0]),
                    "%s",
                    builtins[index].parameters[p]
                );
            }
            int64_t fn_type = sh_fn_type_id(
                &sh,
                builtins[index].result,
                parameters,
                builtins[index].arity
            );
            sh.builtin_symbols[index] = sh.next_symbol++;
            buffer_format(
                &sh.symbols,
                "symbol|%" PRId64 "|builtin|%s|%" PRId64 "|0|0\n",
                sh.builtin_symbols[index],
                builtins[index].name,
                fn_type
            );
        }
        if (sh.error == NULL) {
            char list_parameter[8][16];
            snprintf(list_parameter[0], sizeof(list_parameter[0]), "List");
            int64_t fn_type = sh_fn_type_id(&sh, "Int", list_parameter, 1);
            sh.len_list_symbol = sh.next_symbol++;
            buffer_format(
                &sh.symbols,
                "symbol|%" PRId64 "|builtin|len|%" PRId64 "|0|0\n",
                sh.len_list_symbol,
                fn_type
            );
        }
    }

    /* Function scopes, parameter bindings, and typed bodies. */
    function_start = next_function_start(source, 0);
    for (
        int64_t index = 0;
        sh.error == NULL && index < sh.function_count;
        ++index
    ) {
        int64_t function_close = function_end(source, function_start);
        int64_t parameters = parameter_open(source, function_start);
        int64_t parameters_close = balanced_end(
            source,
            parameters,
            "(",
            ")"
        );
        int64_t saved_env = sh.env_count;
        sh_scope_open(&sh, "function", parameters, function_close);
        int64_t cursor = skip_trivia(
            source,
            token_end(source, parameters)
        );
        int64_t parameter_index = 0;
        while (
            sh.error == NULL &&
            cursor < parameters_close &&
            !token_equal(source, cursor, ")")
        ) {
            char *name_text = token_copy(source, cursor);
            sh_bind(
                &sh,
                name_text,
                sh.functions[index].parameters[parameter_index],
                false,
                "parameter",
                cursor,
                token_end(source, cursor)
            );
            free(name_text);
            int64_t colon = skip_trivia(
                source,
                token_end(source, cursor)
            );
            int64_t type_at = skip_trivia(
                source,
                token_end(source, colon)
            );
            cursor = skip_trivia(source, token_end(source, type_at));
            if (
                strcmp(
                    sh.functions[index].parameters[parameter_index],
                    "List"
                ) == 0
            ) {
                cursor = skip_trivia(source, token_end(source, cursor));
                cursor = skip_trivia(source, token_end(source, cursor));
            }
            ++parameter_index;
            if (
                cursor < parameters_close &&
                token_equal(source, cursor, ",")
            ) {
                cursor = skip_trivia(source, token_end(source, cursor));
            }
        }
        int64_t function_scope = sh.scope_stack[sh.scope_depth - 1];
        int64_t body_at = skip_trivia(source, parameters_close);
        while (
            body_at < function_close &&
            !token_equal(source, body_at, "{")
        ) {
            body_at = skip_trivia(source, token_end(source, body_at));
        }
        int64_t body_cursor = body_at;
        ShBlock *body = sh.error == NULL ?
            sh_parse_block(
                &sh,
                &body_cursor,
                sh.functions[index].result,
                NULL,
                -1,
                NULL
            ) : NULL;
        if (body != NULL) {
            int64_t function_node = sh.next_node++;
            buffer_format(
                &sh.nodes,
                "function|%" PRId64 "|%" PRId64 "|%" PRId64 "|%" PRId64
                "|%" PRId64 "\n",
                function_node,
                sh.functions[index].symbol_id,
                function_scope,
                function_start,
                function_close
            );
            sh_emit_block(&sh, &sh.nodes, body);
            sh_free_block(body);
        }
        sh_scope_close(&sh, saved_env);
        if (function_close < 0) break;
        function_start = next_function_start(source, function_close);
    }

    Buffer document;
    buffer_init(&document);
    buffer_append(&document, "schema|kofun.selfhost-hir/v1\n");
    buffer_format(&document, "source|%s|%s\n", path, digest);
    if (sh.error == NULL) {
        buffer_append(&document, "status|complete\n");
        buffer_append(&document, sh.types.data);
        buffer_append(&document, sh.scopes.data);
        buffer_append(&document, sh.symbols.data);
        buffer_append(&document, sh.bindings.data);
        buffer_append(&document, sh.nodes.data);
        *complete_out = true;
    } else {
        buffer_append(&document, "status|rejected\n");
        int64_t at = sh.error_at >= 0 ? sh.error_at : 0;
        int64_t end = at;
        if (at < sh.length) end = token_end(source, at);
        buffer_format(
            &document,
            "diagnostic|%s|%" PRId64 "|%" PRId64 "|",
            sh.error_code,
            at,
            end
        );
        sh_escaped(&document, sh.error_message);
        buffer_append(&document, "\n");
        if (strcmp(sh.error_code, "E2S10") == 0) {
            buffer_format(
                &document,
                "unsupported|%" PRId64 "|%" PRId64 "|statement\n",
                at,
                end
            );
        }
        puts(sh.error);
        *complete_out = false;
    }
    free(sh.error);
    free(sh.types.data);
    free(sh.scopes.data);
    free(sh.symbols.data);
    free(sh.bindings.data);
    free(sh.nodes.data);
    free(sh.diagnostics.data);
    return document.data;
}

static int emit_selfhost_hir_file(
    const char *input,
    const char *output,
    const char *digest
) {
    if (same_file(input, output)) {
        puts("error[E2S35]: selfhost-HIR input and output must be distinct");
        return 2;
    }
    if (strlen(digest) != 64 || strspn(digest, "0123456789abcdef") != 64) {
        puts("error[E2S35]: selfhost-HIR digest must be 64 lowercase hex");
        return 2;
    }
    char *source = read_file(input);
    char *tokens = lex_source(source);
    if (strncmp(tokens, "error[", 6) == 0) {
        puts(tokens);
        free(tokens);
        free(source);
        return 1;
    }
    free(tokens);
    bool complete = false;
    char *document = emit_selfhost_hir_document(
        source,
        input,
        digest,
        &complete
    );
    write_file(output, document);
    free(document);
    free(source);
    return complete ? 0 : 1;
}

/*
 * selfhost-C11 lowering (#620): kofun.selfhost-hir/v1 -> deterministic
 * standalone C11 for the non-looping Text/function profile slice.
 *
 * The document is the only input: node, symbol, binding, scope, and type
 * records drive the lowering; source text is never reparsed. Every
 * expression node lowers post-order to one temporary named after its
 * node id, so argument evaluation is exactly-once and left-to-right, and
 * `&&`/`||` keep short-circuit evaluation through guarded blocks.
 * Constructs outside the slice (mutation, loops, indexing, ranges, and
 * the List/host builtins) classify as unsupported, never as invalid.
 */

enum {
    SL_MAX_RECORDS = 4096,
    SL_MAX_TYPES = 64,
};

typedef struct {
    char *line;
    char *fields[16];
    int64_t field_count;
} SlRecord;

typedef struct {
    char *document;
    SlRecord types[SL_MAX_TYPES];
    char *type_keys[SL_MAX_TYPES];
    int64_t type_count;
    SlRecord scopes[SL_MAX_RECORDS];
    int64_t scope_count;
    SlRecord symbols[SL_MAX_RECORDS];
    int64_t symbol_count;
    SlRecord bindings[SL_MAX_RECORDS];
    int64_t binding_count;
    SlRecord nodes[SL_MAX_RECORDS];
    int64_t node_count;
    char *source_path;
    char *source_digest;
    bool complete;
    char *error;
    int error_exit;
} SlDoc;

static void sl_fail(SlDoc *doc, int exit_code, const char *message) {
    if (doc->error != NULL) return;
    Buffer copy;
    buffer_init(&copy);
    buffer_append(&copy, message);
    doc->error = copy.data;
    doc->error_exit = exit_code;
}

static void sl_fail_name(
    SlDoc *doc,
    int exit_code,
    const char *prefix,
    const char *name
) {
    if (doc->error != NULL) return;
    Buffer copy;
    buffer_init(&copy);
    buffer_format(&copy, "%s`%s`", prefix, name);
    doc->error = copy.data;
    doc->error_exit = exit_code;
}

/* Split one record line in place; fields beyond 16 are an invalid
 * document. Returns false on overflow. */
static bool sl_split(SlRecord *record, char *line) {
    record->line = line;
    record->field_count = 0;
    char *cursor = line;
    while (record->field_count < 16) {
        record->fields[record->field_count++] = cursor;
        char *bar = strchr(cursor, '|');
        if (bar == NULL) return true;
        *bar = '\0';
        cursor = bar + 1;
    }
    return strchr(cursor, '|') == NULL;
}

static const char *sl_field(const SlRecord *record, int64_t index) {
    if (index < 0 || index >= record->field_count) return "";
    return record->fields[index];
}

static int64_t sl_int(const SlRecord *record, int64_t index) {
    return strtoll(sl_field(record, index), NULL, 10);
}

static bool sl_load(SlDoc *doc, const char *text) {
    doc->document = allocate(strlen(text) + 1);
    memcpy(doc->document, text, strlen(text) + 1);
    char *cursor = doc->document;
    int64_t line_index = 0;
    bool schema_seen = false;
    while (*cursor != '\0') {
        char *line = cursor;
        char *newline = strchr(cursor, '\n');
        if (newline == NULL) {
            cursor = line + strlen(line);
        } else {
            *newline = '\0';
            cursor = newline + 1;
        }
        if (line_index == 0) {
            schema_seen = strcmp(line, "schema|kofun.selfhost-hir/v1") == 0;
            if (!schema_seen) {
                sl_fail(doc, 1,
                        "error[E2S35]: selfhost-C11 input is not a "
                        "kofun.selfhost-hir/v1 document");
                return false;
            }
            ++line_index;
            continue;
        }
        (void)schema_seen;
        SlRecord parsed;
        if (!sl_split(&parsed, line)) {
            sl_fail(doc, 1,
                    "error[E2S35]: selfhost-C11 record has too many fields");
            return false;
        }
        const char *tag = sl_field(&parsed, 0);
        SlRecord *slot = NULL;
        if (strcmp(tag, "source") == 0) {
            doc->source_path = allocate(strlen(sl_field(&parsed, 1)) + 1);
            strcpy(doc->source_path, sl_field(&parsed, 1));
            doc->source_digest = allocate(strlen(sl_field(&parsed, 2)) + 1);
            strcpy(doc->source_digest, sl_field(&parsed, 2));
        } else if (strcmp(tag, "status") == 0) {
            doc->complete = strcmp(sl_field(&parsed, 1), "complete") == 0;
        } else if (strcmp(tag, "type") == 0) {
            if (doc->type_count >= SL_MAX_TYPES) {
                sl_fail(doc, 1, "error[E2S35]: selfhost-C11 type limit is 64");
                return false;
            }
            slot = &doc->types[doc->type_count];
            Buffer joined;
            buffer_init(&joined);
            for (int64_t field = 2; field < parsed.field_count; ++field) {
                if (field > 2) buffer_append(&joined, "|");
                buffer_append(&joined, sl_field(&parsed, field));
            }
            doc->type_keys[doc->type_count++] = joined.data;
        } else if (strcmp(tag, "scope") == 0) {
            if (doc->scope_count >= SL_MAX_RECORDS) {
                sl_fail(doc, 1, "error[E2S35]: selfhost-C11 record limit");
                return false;
            }
            slot = &doc->scopes[doc->scope_count++];
        } else if (strcmp(tag, "symbol") == 0) {
            if (doc->symbol_count >= SL_MAX_RECORDS) {
                sl_fail(doc, 1, "error[E2S35]: selfhost-C11 record limit");
                return false;
            }
            slot = &doc->symbols[doc->symbol_count++];
        } else if (strcmp(tag, "binding") == 0) {
            if (doc->binding_count >= SL_MAX_RECORDS) {
                sl_fail(doc, 1, "error[E2S35]: selfhost-C11 record limit");
                return false;
            }
            slot = &doc->bindings[doc->binding_count++];
        } else if (strcmp(tag, "function") == 0 ||
                   strcmp(tag, "node") == 0) {
            if (doc->node_count >= SL_MAX_RECORDS) {
                sl_fail(doc, 1, "error[E2S35]: selfhost-C11 record limit");
                return false;
            }
            slot = &doc->nodes[doc->node_count++];
        }
        if (slot != NULL) {
            *slot = parsed;
        }
        ++line_index;
    }
    if (doc->source_path == NULL) {
        sl_fail(doc, 1,
                "error[E2S35]: selfhost-C11 document has no source record");
        return false;
    }
    if (!doc->complete) {
        sl_fail(doc, 1,
                "error[E2S35]: selfhost-C11 input must be a complete typed "
                "document");
        return false;
    }
    return true;
}

/* The closed type table: id -> key ("int", "bool", "text", "void",
 * "list-text", or "fn|..."). */
static const char *sl_type_key(const SlDoc *doc, int64_t type_id) {
    for (int64_t index = 0; index < doc->type_count; ++index) {
        if (sl_int(&doc->types[index], 1) == type_id) {
            return doc->type_keys[index];
        }
    }
    return "";
}

static const char *sl_c_type(const char *key) {
    if (strcmp(key, "int") == 0) return "int64_t";
    if (strcmp(key, "bool") == 0) return "bool";
    if (strcmp(key, "text") == 0) return "const char *";
    if (strcmp(key, "list-text") == 0) return "kofun_text_list";
    return "";
}

static const SlRecord *sl_symbol(const SlDoc *doc, int64_t symbol_id) {
    for (int64_t index = 0; index < doc->symbol_count; ++index) {
        if (sl_int(&doc->symbols[index], 1) == symbol_id) {
            return &doc->symbols[index];
        }
    }
    return NULL;
}

static const SlRecord *sl_binding(const SlDoc *doc, int64_t binding_id) {
    for (int64_t index = 0; index < doc->binding_count; ++index) {
        if (sl_int(&doc->bindings[index], 1) == binding_id) {
            return &doc->bindings[index];
        }
    }
    return NULL;
}

static const SlRecord *sl_scope(const SlDoc *doc, int64_t scope_id) {
    for (int64_t index = 0; index < doc->scope_count; ++index) {
        if (sl_int(&doc->scopes[index], 1) == scope_id) {
            return &doc->scopes[index];
        }
    }
    return NULL;
}

/* The value type key of a binding: its symbol's recorded type. */
static const char *sl_binding_type(const SlDoc *doc, int64_t binding_id) {
    const SlRecord *binding = sl_binding(doc, binding_id);
    if (binding == NULL) return "";
    const SlRecord *symbol = sl_symbol(doc, sl_int(binding, 3));
    if (symbol == NULL) return "";
    return sl_type_key(doc, sl_int(symbol, 4));
}

/* Result type key of a function-typed symbol: field 1 of its fn key. */
static const char *sl_result_key(const SlDoc *doc, const SlRecord *symbol) {
    const char *key = sl_type_key(doc, sl_int(symbol, 4));
    if (strncmp(key, "fn|", 3) != 0) return "";
    return sl_type_key(doc, strtoll(key + 3, NULL, 10));
}

/* Whether a builtin symbol's single parameter is List[Text] (the len
 * overload outside this slice). */
static bool sl_list_parameter(const SlDoc *doc, const SlRecord *symbol) {
    const char *key = sl_type_key(doc, sl_int(symbol, 4));
    const char *bar = key;
    int64_t seen = 0;
    while (seen < 2 && bar != NULL) {
        bar = strchr(bar, '|');
        if (bar != NULL) ++bar;
        ++seen;
    }
    if (bar == NULL) return false;
    return strcmp(sl_type_key(doc, strtoll(bar, NULL, 10)), "list-text") == 0;
}

/* The audited C runtime shim emitted into every generated program. Text
 * helpers keep the trusted stage-1 seed's observable semantics byte for
 * byte (byte-counted len, byte-offset slicing with clamping, ASCII trim,
 * literal non-overlapping replace); the Unicode builtins consult the same
 * Unicode 17 tables as the Stage 2 lexer via kofun_unicode.c, compiled
 * with the repository's unicode include directory. Allocations use one
 * documented process-lifetime rule: nothing is freed, and allocation
 * failure panics explicitly. */
static const char *sl_prelude =
    "#include <ctype.h>\n"
    "#include <inttypes.h>\n"
    "#include <stdbool.h>\n"
    "#include <stdint.h>\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
    "\n"
    "#include \"kofun_unicode.c\"\n"
    "\n"
    "typedef struct {\n"
    "    int64_t len;\n"
    "    const char **items;\n"
    "} kofun_text_list;\n"
    "\n"
    "static bool kofun_failed;\n"
    "\n"
    "static void kofun_error(const char *message) {\n"
    "    if (!kofun_failed) {\n"
    "        fputs(message, stderr);\n"
    "        fputc('\\n', stderr);\n"
    "    }\n"
    "    kofun_failed = true;\n"
    "}\n"
    "\n"
    "static int64_t kofun_add(int64_t left, int64_t right) {\n"
    "    int64_t result;\n"
    "    if (__builtin_add_overflow(left, right, &result)) {\n"
    "        kofun_error(\"error[R010]: integer overflow in operator `+`\");\n"
    "        return 0;\n"
    "    }\n"
    "    return result;\n"
    "}\n"
    "\n"
    "static int64_t kofun_sub(int64_t left, int64_t right) {\n"
    "    int64_t result;\n"
    "    if (__builtin_sub_overflow(left, right, &result)) {\n"
    "        kofun_error(\"error[R010]: integer overflow in operator `-`\");\n"
    "        return 0;\n"
    "    }\n"
    "    return result;\n"
    "}\n"
    "\n"
    "static int64_t kofun_mul(int64_t left, int64_t right) {\n"
    "    int64_t result;\n"
    "    if (__builtin_mul_overflow(left, right, &result)) {\n"
    "        kofun_error(\"error[R010]: integer overflow in operator `*`\");\n"
    "        return 0;\n"
    "    }\n"
    "    return result;\n"
    "}\n"
    "\n"
    "static int64_t kofun_neg(int64_t value) {\n"
    "    if (value == INT64_MIN) {\n"
    "        kofun_error(\n"
    "            \"error[R010]: integer overflow in unary operator `-`\"\n"
    "        );\n"
    "        return 0;\n"
    "    }\n"
    "    return -value;\n"
    "}\n"
    "\n"
    "static int64_t kofun_floor_div(int64_t left, int64_t right) {\n"
    "    if (right == 0) {\n"
    "        kofun_error(\n"
    "            \"error[R010]: operator `//` failed: division by zero\"\n"
    "        );\n"
    "        return 0;\n"
    "    }\n"
    "    if (left == INT64_MIN && right == -1) {\n"
    "        kofun_error(\"error[R010]: integer overflow in operator `//`\");\n"
    "        return 0;\n"
    "    }\n"
    "    int64_t quotient = left / right;\n"
    "    int64_t remainder = left % right;\n"
    "    if (remainder != 0 && ((remainder < 0) != (right < 0))) {\n"
    "        --quotient;\n"
    "    }\n"
    "    return quotient;\n"
    "}\n"
    "\n"
    "static int64_t kofun_floor_mod(int64_t left, int64_t right) {\n"
    "    if (right == 0) {\n"
    "        kofun_error(\n"
    "            \"error[R010]: operator `%` failed: division by zero\"\n"
    "        );\n"
    "        return 0;\n"
    "    }\n"
    "    if (left == INT64_MIN && right == -1) {\n"
    "        return 0;\n"
    "    }\n"
    "    int64_t remainder = left % right;\n"
    "    if (remainder != 0 && ((remainder < 0) != (right < 0))) {\n"
    "        remainder += right;\n"
    "    }\n"
    "    return remainder;\n"
    "}\n"
    "\n"
    "";

static const char *sl_prelude_text =
    "void kofun_rt_panic(const char *message) {\n"
    "    fprintf(stderr, \"Kofun runtime error: %s\\n\", message);\n"
    "    exit(1);\n"
    "}\n"
    "\n"
    "void *kofun_rt_alloc(size_t size) {\n"
    "    void *value = malloc(size == 0 ? 1 : size);\n"
    "    if (value == NULL) {\n"
    "        kofun_rt_panic(\"out of memory\");\n"
    "    }\n"
    "    return value;\n"
    "}\n"
    "\n"
    "char *kofun_rt_copy_n(const char *value, size_t length) {\n"
    "    char *result = (char *)kofun_rt_alloc(length + 1);\n"
    "    if (length > 0) {\n"
    "        memcpy(result, value, length);\n"
    "    }\n"
    "    result[length] = '\\0';\n"
    "    return result;\n"
    "}\n"
    "\n"
    "char *kofun_rt_text_concat(const char *left, const char *right) {\n"
    "    size_t left_len = strlen(left);\n"
    "    size_t right_len = strlen(right);\n"
    "    char *result = (char *)kofun_rt_alloc(left_len + right_len + 1);\n"
    "    memcpy(result, left, left_len);\n"
    "    memcpy(result + left_len, right, right_len + 1);\n"
    "    return result;\n"
    "}\n"
    "\n"
    "bool kofun_rt_text_equal(const char *left, const char *right) {\n"
    "    return strcmp(left, right) == 0;\n"
    "}\n"
    "\n"
    "int64_t kofun_rt_text_len(const char *value) {\n"
    "    return (int64_t)strlen(value);\n"
    "}\n"
    "\n"
    "int64_t kofun_rt_text_list_len(kofun_text_list values) {\n"
    "    return values.len;\n"
    "}\n"
    "\n"
    "kofun_text_list kofun_rt_chars(const char *value) {\n"
    "    size_t length = strlen(value);\n"
    "    const char **items = (const char **)kofun_rt_alloc(\n"
    "        sizeof(char *) * (length == 0 ? 1 : length)\n"
    "    );\n"
    "    for (size_t index = 0; index < length; ++index) {\n"
    "        items[index] = kofun_rt_copy_n(value + index, 1);\n"
    "    }\n"
    "    kofun_text_list result;\n"
    "    result.len = (int64_t)length;\n"
    "    result.items = items;\n"
    "    return result;\n"
    "}\n"
    "\n"
    "const char *kofun_rt_text_list_get(kofun_text_list values, int64_t index) {\n"
    "    if (index < 0 || index >= values.len) {\n"
    "        kofun_rt_panic(\"List[Text] index out of bounds\");\n"
    "    }\n"
    "    return values.items[index];\n"
    "}\n"
    "\n"
    "bool kofun_rt_text_contains(const char *value, const char *needle) {\n"
    "    return strstr(value, needle) != NULL;\n"
    "}\n"
    "\n"
    "int64_t kofun_rt_find(const char *value, const char *needle) {\n"
    "    const char *found = strstr(value, needle);\n"
    "    return found == NULL ? INT64_C(-1) : (int64_t)(found - value);\n"
    "}\n"
    "\n"
    "char *kofun_rt_text_slice(const char *value, int64_t start, int64_t end) {\n"
    "    int64_t length = (int64_t)strlen(value);\n"
    "    if (start < 0) start = 0;\n"
    "    if (end < start) end = start;\n"
    "    if (start > length) start = length;\n"
    "    if (end > length) end = length;\n"
    "    return kofun_rt_copy_n(value + start, (size_t)(end - start));\n"
    "}\n"
    "\n"
    "char *kofun_rt_trim(const char *value) {\n"
    "    const unsigned char *start = (const unsigned char *)value;\n"
    "    while (*start != '\\0' && isspace(*start)) {\n"
    "        ++start;\n"
    "    }\n"
    "    const unsigned char *end =\n"
    "        (const unsigned char *)value + strlen(value);\n"
    "    while (end > start && isspace(end[-1])) {\n"
    "        --end;\n"
    "    }\n"
    "    return kofun_rt_copy_n((const char *)start, (size_t)(end - start));\n"
    "}\n"
    "\n"
    "";

static const char *sl_prelude_unicode =
    "char *kofun_rt_replace(\n"
    "    const char *value,\n"
    "    const char *old,\n"
    "    const char *replacement\n"
    ") {\n"
    "    size_t old_len = strlen(old);\n"
    "    if (old_len == 0) {\n"
    "        return kofun_rt_copy_n(value, strlen(value));\n"
    "    }\n"
    "    size_t replacement_len = strlen(replacement);\n"
    "    size_t count = 0;\n"
    "    const char *cursor = value;\n"
    "    while ((cursor = strstr(cursor, old)) != NULL) {\n"
    "        ++count;\n"
    "        cursor += old_len;\n"
    "    }\n"
    "    size_t value_len = strlen(value);\n"
    "    size_t result_len;\n"
    "    if (replacement_len >= old_len) {\n"
    "        result_len = value_len + count * (replacement_len - old_len);\n"
    "    } else {\n"
    "        result_len = value_len - count * (old_len - replacement_len);\n"
    "    }\n"
    "    char *result = (char *)kofun_rt_alloc(result_len + 1);\n"
    "    char *out = result;\n"
    "    cursor = value;\n"
    "    const char *match;\n"
    "    while ((match = strstr(cursor, old)) != NULL) {\n"
    "        size_t prefix = (size_t)(match - cursor);\n"
    "        memcpy(out, cursor, prefix);\n"
    "        out += prefix;\n"
    "        memcpy(out, replacement, replacement_len);\n"
    "        out += replacement_len;\n"
    "        cursor = match + old_len;\n"
    "    }\n"
    "    strcpy(out, cursor);\n"
    "    return result;\n"
    "}\n"
    "\n"
    "bool kofun_rt_starts_with(const char *value, const char *prefix) {\n"
    "    size_t prefix_len = strlen(prefix);\n"
    "    return strncmp(value, prefix, prefix_len) == 0;\n"
    "}\n"
    "\n"
    "bool kofun_rt_is_digit(const char *value) {\n"
    "    return value[0] != '\\0' && value[1] == '\\0' &&\n"
    "        isdigit((unsigned char)value[0]) != 0;\n"
    "}\n"
    "\n"
    "bool kofun_rt_is_space(const char *value) {\n"
    "    return value[0] != '\\0' && value[1] == '\\0' &&\n"
    "        isspace((unsigned char)value[0]) != 0;\n"
    "}\n"
    "\n"
    "bool kofun_rt_is_xid_continue(const char *value) {\n"
    "    uint32_t codepoint = 0;\n"
    "    size_t width = 0;\n"
    "    size_t length = strlen(value);\n"
    "    if (!kofun_unicode_decode(\n"
    "            (const uint8_t *)value,\n"
    "            length,\n"
    "            0,\n"
    "            &codepoint,\n"
    "            &width)) {\n"
    "        return false;\n"
    "    }\n"
    "    return kofun_unicode_is_xid_continue(codepoint);\n"
    "}\n"
    "\n"
    "const char *kofun_rt_validate_unicode_source(const char *value) {\n"
    "    KofunUnicodeError unicode_error;\n"
    "    if (kofun_unicode_validate_source(\n"
    "            (const uint8_t *)value,\n"
    "            strlen(value),\n"
    "            &unicode_error)) {\n"
    "        return \"\";\n"
    "    }\n"
    "    char message[1024];\n"
    "    kofun_unicode_format_error(\n"
    "        &unicode_error,\n"
    "        getenv(\"KOFUN_DIAGNOSTIC_LOCALE\"),\n"
    "        message,\n"
    "        sizeof(message)\n"
    "    );\n"
    "    return kofun_rt_copy_n(message, strlen(message));\n"
    "}\n"
    "\n";

typedef struct {
    const SlDoc *doc;
    int64_t first_node;
    int64_t last_node;
    const char *fail_return;
    const char *function_name;
    int64_t indent;
} SlFn;

static const SlRecord *sl_node(const SlFn *fn, int64_t node_id) {
    for (int64_t index = fn->first_node; index < fn->last_node; ++index) {
        if (sl_int(&fn->doc->nodes[index], 1) == node_id) {
            return &fn->doc->nodes[index];
        }
    }
    return NULL;
}

static void sl_indent(const SlFn *fn, Buffer *out) {
    for (int64_t level = 0; level < fn->indent; ++level) {
        buffer_append(out, "    ");
    }
}

/* Emit the failure check after a temporary that can set kofun_failed. */
static void sl_failed_check(const SlFn *fn, Buffer *out) {
    sl_indent(fn, out);
    if (fn->fail_return[0] == '\0') {
        buffer_append(out, "if (kofun_failed) return;\n");
    } else {
        buffer_format(out, "if (kofun_failed) return %s;\n",
                      fn->fail_return);
    }
}

/* Decode the record escaping of a literal-text field, then re-escape the
 * bytes as one C string literal. */
static void sl_c_string(Buffer *out, const char *field) {
    buffer_append(out, "\"");
    for (size_t index = 0; field[index] != '\0'; ++index) {
        char symbol = field[index];
        if (symbol == '\\' && field[index + 1] != '\0') {
            char next = field[index + 1];
            if (next == '\\') symbol = '\\';
            else if (next == 'p') symbol = '|';
            else if (next == 'n') symbol = '\n';
            ++index;
        }
        if (symbol == '\\') buffer_append(out, "\\\\");
        else if (symbol == '"') buffer_append(out, "\\\"");
        else if (symbol == '\n') buffer_append(out, "\\n");
        else {
            char one[2] = {symbol, '\0'};
            buffer_append(out, one);
        }
    }
    buffer_append(out, "\"");
}

static void sl_emit_expr(SlFn *fn, SlDoc *doc, int64_t node_id, Buffer *out);

/* Positional pre-order size of the expression subtree rooted at `index`;
 * used to find where a statement's trailing records begin. */
static int64_t sl_consume_expr(SlFn *fn, int64_t index) {
    const SlRecord *node = &fn->doc->nodes[index];
    const char *kind = sl_field(node, 2);
    int64_t next = index + 1;
    if (strcmp(kind, "call") == 0) {
        for (int64_t field = 8; field < node->field_count; ++field) {
            next = sl_consume_expr(fn, next);
        }
        return next;
    }
    if (strcmp(kind, "unary") == 0) {
        return sl_consume_expr(fn, next);
    }
    if (strcmp(kind, "binary") == 0 || strcmp(kind, "index") == 0 ||
        strcmp(kind, "range") == 0) {
        next = sl_consume_expr(fn, next);
        return sl_consume_expr(fn, next);
    }
    return next;
}

/* Map a slice builtin to its runtime helper; NULL when the builtin is
 * outside the non-looping Text slice. */
static const char *sl_builtin_helper(const char *name) {
    if (strcmp(name, "chars") == 0) return "kofun_rt_chars";
    if (strcmp(name, "contains") == 0) return "kofun_rt_text_contains";
    if (strcmp(name, "find") == 0) return "kofun_rt_find";
    if (strcmp(name, "is_digit") == 0) return "kofun_rt_is_digit";
    if (strcmp(name, "is_space") == 0) return "kofun_rt_is_space";
    if (strcmp(name, "is_xid_continue") == 0) {
        return "kofun_rt_is_xid_continue";
    }
    if (strcmp(name, "len") == 0) return "kofun_rt_text_len";
    if (strcmp(name, "replace") == 0) return "kofun_rt_replace";
    if (strcmp(name, "starts_with") == 0) return "kofun_rt_starts_with";
    if (strcmp(name, "text_slice") == 0) return "kofun_rt_text_slice";
    if (strcmp(name, "trim") == 0) return "kofun_rt_trim";
    if (strcmp(name, "validate_unicode_source") == 0) {
        return "kofun_rt_validate_unicode_source";
    }
    return NULL;
}

static void sl_emit_expr(SlFn *fn, SlDoc *doc, int64_t node_id, Buffer *out) {
    if (doc->error != NULL) return;
    const SlRecord *node = sl_node(fn, node_id);
    if (node == NULL) {
        sl_fail(doc, 1, "error[E2S35]: selfhost-C11 node reference is out "
                        "of range");
        return;
    }
    const char *kind = sl_field(node, 2);
    const char *type_key = sl_type_key(doc, sl_int(node, 5));
    if (strcmp(kind, "literal-int") == 0) {
        sl_indent(fn, out);
        buffer_format(out, "int64_t k_n%" PRId64 " = INT64_C(", node_id);
        const char *digits = sl_field(node, 7);
        for (size_t at = 0; digits[at] != '\0'; ++at) {
            if (digits[at] != '_') {
                char one[2] = {digits[at], '\0'};
                buffer_append(out, one);
            }
        }
        buffer_append(out, ");\n");
        return;
    }
    if (strcmp(kind, "literal-bool") == 0) {
        sl_indent(fn, out);
        buffer_format(out, "bool k_n%" PRId64 " = %s;\n", node_id,
                      sl_field(node, 7));
        return;
    }
    if (strcmp(kind, "literal-text") == 0) {
        sl_indent(fn, out);
        buffer_format(out, "const char *k_n%" PRId64 " = ", node_id);
        sl_c_string(out, sl_field(node, 7));
        buffer_append(out, ";\n");
        return;
    }
    if (strcmp(kind, "name") == 0) {
        if (sl_c_type(type_key)[0] == '\0') {
            sl_fail_name(doc, 3,
                         "error[E2S10]: unsupported selfhost-C11 type ",
                         type_key);
            return;
        }
        sl_indent(fn, out);
        buffer_format(out, "%s k_n%" PRId64 " = k_b%s;\n",
                      sl_c_type(type_key), node_id, sl_field(node, 7));
        return;
    }
    if (strcmp(kind, "call") == 0) {
        const SlRecord *symbol = sl_symbol(doc, sl_int(node, 7));
        if (symbol == NULL) {
            sl_fail(doc, 1, "error[E2S35]: selfhost-C11 call has no symbol");
            return;
        }
        const char *name = sl_field(symbol, 3);
        bool builtin = strcmp(sl_field(symbol, 2), "builtin") == 0;
        const char *helper = NULL;
        if (builtin) {
            helper = sl_builtin_helper(name);
            if (strcmp(name, "len") == 0 &&
                sl_list_parameter(doc, symbol)) {
                helper = "kofun_rt_text_list_len";
            }
            if (helper == NULL) {
                sl_fail_name(doc, 3,
                             "error[E2S10]: unsupported selfhost-C11 "
                             "builtin call ",
                             name);
                return;
            }
        }
        for (int64_t field = 8; field < node->field_count; ++field) {
            sl_emit_expr(fn, doc, sl_int(node, field), out);
            if (doc->error != NULL) return;
        }
        sl_indent(fn, out);
        if (strcmp(type_key, "void") == 0) {
            buffer_format(out, "kofun_fn_%s(", name);
        } else if (builtin) {
            buffer_format(out, "%s k_n%" PRId64 " = %s(",
                          sl_c_type(type_key), node_id, helper);
        } else {
            buffer_format(out, "%s k_n%" PRId64 " = kofun_fn_%s(",
                          sl_c_type(type_key), node_id, name);
        }
        for (int64_t field = 8; field < node->field_count; ++field) {
            if (field > 8) buffer_append(out, ", ");
            buffer_format(out, "k_n%s", sl_field(node, field));
        }
        buffer_append(out, ");\n");
        if (!builtin) {
            sl_failed_check(fn, out);
        }
        return;
    }
    if (strcmp(kind, "unary") == 0) {
        const char *op = sl_field(node, 7);
        int64_t operand = sl_int(node, 8);
        sl_emit_expr(fn, doc, operand, out);
        if (doc->error != NULL) return;
        sl_indent(fn, out);
        if (strcmp(op, "!") == 0) {
            buffer_format(out, "bool k_n%" PRId64 " = !k_n%" PRId64 ";\n",
                          node_id, operand);
        } else {
            buffer_format(out,
                          "int64_t k_n%" PRId64 " = kofun_neg(k_n%" PRId64
                          ");\n",
                          node_id, operand);
            sl_failed_check(fn, out);
        }
        return;
    }
    if (strcmp(kind, "binary") == 0) {
        const char *op = sl_field(node, 7);
        int64_t left = sl_int(node, 8);
        int64_t right = sl_int(node, 9);
        const SlRecord *left_node = sl_node(fn, left);
        const char *left_key = left_node == NULL ?
            "" : sl_type_key(doc, sl_int(left_node, 5));
        bool logical = strcmp(op, "&&") == 0 || strcmp(op, "\\p\\p") == 0;
        bool logical_or = strcmp(op, "\\p\\p") == 0;
        if (logical) {
            sl_emit_expr(fn, doc, left, out);
            if (doc->error != NULL) return;
            sl_indent(fn, out);
            buffer_format(out, "bool k_n%" PRId64 " = k_n%" PRId64 ";\n",
                          node_id, left);
            sl_indent(fn, out);
            if (logical_or) {
                buffer_format(out, "if (!k_n%" PRId64 ") {\n", node_id);
            } else {
                buffer_format(out, "if (k_n%" PRId64 ") {\n", node_id);
            }
            fn->indent += 1;
            sl_emit_expr(fn, doc, right, out);
            if (doc->error != NULL) return;
            sl_indent(fn, out);
            buffer_format(out, "k_n%" PRId64 " = k_n%" PRId64 ";\n",
                          node_id, right);
            fn->indent -= 1;
            sl_indent(fn, out);
            buffer_append(out, "}\n");
            return;
        }
        sl_emit_expr(fn, doc, left, out);
        if (doc->error != NULL) return;
        sl_emit_expr(fn, doc, right, out);
        if (doc->error != NULL) return;
        if (strcmp(op, "+") == 0 && strcmp(left_key, "text") == 0) {
            sl_indent(fn, out);
            buffer_format(out,
                          "const char *k_n%" PRId64
                          " = kofun_rt_text_concat(k_n%" PRId64
                          ", k_n%" PRId64 ");\n",
                          node_id, left, right);
            return;
        }
        if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
            sl_indent(fn, out);
            if (strcmp(left_key, "text") == 0) {
                buffer_format(out,
                              "bool k_n%" PRId64
                              " = %skofun_rt_text_equal(k_n%" PRId64
                              ", k_n%" PRId64 ");\n",
                              node_id,
                              op[0] == '!' ? "!" : "",
                              left, right);
            } else {
                buffer_format(out,
                              "bool k_n%" PRId64 " = k_n%" PRId64
                              " %s k_n%" PRId64 ";\n",
                              node_id, left, op, right);
            }
            return;
        }
        if (strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 ||
            strcmp(op, ">") == 0 || strcmp(op, ">=") == 0) {
            if (strcmp(left_key, "text") == 0) {
                sl_fail_name(doc, 3,
                             "error[E2S10]: unsupported selfhost-C11 "
                             "operator ",
                             op);
                return;
            }
            sl_indent(fn, out);
            buffer_format(out,
                          "bool k_n%" PRId64 " = k_n%" PRId64 " %s k_n%"
                          PRId64 ";\n",
                          node_id, left, op, right);
            return;
        }
        const char *arithmetic = NULL;
        if (strcmp(op, "+") == 0) arithmetic = "kofun_add";
        if (strcmp(op, "-") == 0) arithmetic = "kofun_sub";
        if (strcmp(op, "*") == 0) arithmetic = "kofun_mul";
        if (strcmp(op, "//") == 0) arithmetic = "kofun_floor_div";
        if (strcmp(op, "%") == 0) arithmetic = "kofun_floor_mod";
        if (arithmetic == NULL) {
            sl_fail_name(doc, 3,
                         "error[E2S10]: unsupported selfhost-C11 operator ",
                         op);
            return;
        }
        sl_indent(fn, out);
        buffer_format(out,
                      "int64_t k_n%" PRId64 " = %s(k_n%" PRId64
                      ", k_n%" PRId64 ");\n",
                      node_id, arithmetic, left, right);
        sl_failed_check(fn, out);
        return;
    }
    if (strcmp(kind, "index") == 0) {
        int64_t base = sl_int(node, 7);
        int64_t position = sl_int(node, 8);
        sl_emit_expr(fn, doc, base, out);
        if (doc->error != NULL) return;
        sl_emit_expr(fn, doc, position, out);
        if (doc->error != NULL) return;
        sl_indent(fn, out);
        buffer_format(out,
                      "const char *k_n%" PRId64
                      " = kofun_rt_text_list_get(k_n%" PRId64
                      ", k_n%" PRId64 ");\n",
                      node_id, base, position);
        return;
    }
    sl_fail_name(doc, 3,
                 "error[E2S10]: unsupported selfhost-C11 expression ",
                 kind);
}

/* Emit the statement record at `index`; returns the next record index
 * and reports through `terminal` whether the statement always returns. */
static int64_t sl_emit_statement(
    SlFn *fn,
    SlDoc *doc,
    int64_t index,
    Buffer *out,
    bool *terminal
);

/* Emit the statements whose spans lie inside one block scope. */
static int64_t sl_emit_block(
    SlFn *fn,
    SlDoc *doc,
    int64_t index,
    int64_t scope_id,
    Buffer *out,
    bool *terminal
) {
    const SlRecord *scope = sl_scope(doc, scope_id);
    if (scope == NULL) {
        sl_fail(doc, 1, "error[E2S35]: selfhost-C11 scope reference is out "
                        "of range");
        return index;
    }
    int64_t scope_start = sl_int(scope, 4);
    int64_t scope_end = sl_int(scope, 5);
    *terminal = false;
    while (doc->error == NULL && index < fn->last_node) {
        const SlRecord *node = &fn->doc->nodes[index];
        if (strcmp(sl_field(node, 0), "node") != 0) break;
        int64_t start = sl_int(node, 3);
        if (start < scope_start || start >= scope_end) break;
        index = sl_emit_statement(fn, doc, index, out, terminal);
    }
    return index;
}

static int64_t sl_emit_statement(
    SlFn *fn,
    SlDoc *doc,
    int64_t index,
    Buffer *out,
    bool *terminal
) {
    const SlRecord *node = &fn->doc->nodes[index];
    const char *kind = sl_field(node, 2);
    *terminal = false;
    if (doc->error != NULL) return fn->last_node;
    if (strcmp(kind, "let") == 0 || strcmp(kind, "let-mut") == 0) {
        int64_t value = sl_int(node, 8);
        sl_emit_expr(fn, doc, value, out);
        if (doc->error != NULL) return fn->last_node;
        const char *binding_key = sl_binding_type(doc, sl_int(node, 7));
        if (sl_c_type(binding_key)[0] == '\0') {
            sl_fail_name(doc, 3,
                         "error[E2S10]: unsupported selfhost-C11 type ",
                         binding_key);
            return fn->last_node;
        }
        sl_indent(fn, out);
        buffer_format(out, "%s k_b%s = k_n%" PRId64 ";\n",
                      sl_c_type(binding_key), sl_field(node, 7), value);
        return sl_consume_expr(fn, index + 1);
    }
    if (strcmp(kind, "return") == 0) {
        *terminal = true;
        if (strcmp(sl_field(node, 7), "none") == 0) {
            sl_indent(fn, out);
            if (strcmp(fn->function_name, "main") == 0) {
                buffer_append(out, "return 0;\n");
            } else {
                buffer_append(out, "return;\n");
            }
            return index + 1;
        }
        int64_t value = sl_int(node, 7);
        sl_emit_expr(fn, doc, value, out);
        if (doc->error != NULL) return fn->last_node;
        sl_indent(fn, out);
        if (strcmp(fn->function_name, "main") == 0) {
            buffer_format(out, "return (int)k_n%" PRId64 ";\n", value);
        } else {
            buffer_format(out, "return k_n%" PRId64 ";\n", value);
        }
        return sl_consume_expr(fn, index + 1);
    }
    if (strcmp(kind, "expr-stmt") == 0) {
        int64_t value = sl_int(node, 7);
        sl_emit_expr(fn, doc, value, out);
        if (doc->error != NULL) return fn->last_node;
        const SlRecord *value_node = sl_node(fn, value);
        if (value_node != NULL &&
            strcmp(sl_type_key(doc, sl_int(value_node, 5)), "void") != 0) {
            sl_indent(fn, out);
            buffer_format(out, "(void)k_n%" PRId64 ";\n", value);
        }
        return sl_consume_expr(fn, index + 1);
    }
    if (strcmp(kind, "if") == 0) {
        int64_t condition = sl_int(node, 7);
        int64_t then_scope = sl_int(node, 8);
        const char *else_kind = sl_field(node, 9);
        sl_emit_expr(fn, doc, condition, out);
        if (doc->error != NULL) return fn->last_node;
        sl_indent(fn, out);
        buffer_format(out, "if (k_n%" PRId64 ") {\n", condition);
        fn->indent += 1;
        bool then_terminal = false;
        int64_t walk = sl_consume_expr(fn, index + 1);
        walk = sl_emit_block(fn, doc, walk, then_scope, out,
                             &then_terminal);
        fn->indent -= 1;
        if (doc->error != NULL) return fn->last_node;
        if (strcmp(else_kind, "none") == 0) {
            sl_indent(fn, out);
            buffer_append(out, "}\n");
            return walk;
        }
        sl_indent(fn, out);
        buffer_append(out, "} else {\n");
        fn->indent += 1;
        bool else_terminal = false;
        if (strcmp(else_kind, "block") == 0) {
            walk = sl_emit_block(fn, doc, walk, sl_int(node, 10), out,
                                 &else_terminal);
        } else {
            walk = sl_emit_statement(fn, doc, walk, out, &else_terminal);
        }
        fn->indent -= 1;
        if (doc->error != NULL) return fn->last_node;
        sl_indent(fn, out);
        buffer_append(out, "}\n");
        *terminal = then_terminal && else_terminal;
        return walk;
    }
    if (strcmp(kind, "assign") == 0) {
        int64_t value = sl_int(node, 8);
        sl_emit_expr(fn, doc, value, out);
        if (doc->error != NULL) return fn->last_node;
        sl_indent(fn, out);
        buffer_format(out, "k_b%s = k_n%" PRId64 ";\n",
                      sl_field(node, 7), value);
        return sl_consume_expr(fn, index + 1);
    }
    if (strcmp(kind, "while") == 0) {
        int64_t condition = sl_int(node, 7);
        sl_indent(fn, out);
        buffer_append(out, "for (;;) {\n");
        fn->indent += 1;
        sl_emit_expr(fn, doc, condition, out);
        if (doc->error != NULL) return fn->last_node;
        sl_indent(fn, out);
        buffer_format(out, "if (!k_n%" PRId64 ") break;\n", condition);
        bool body_terminal = false;
        int64_t walk = sl_consume_expr(fn, index + 1);
        walk = sl_emit_block(fn, doc, walk, sl_int(node, 8), out,
                             &body_terminal);
        fn->indent -= 1;
        if (doc->error != NULL) return fn->last_node;
        sl_indent(fn, out);
        buffer_append(out, "}\n");
        return walk;
    }
    if (strcmp(kind, "for-range") == 0) {
        const SlRecord *range = sl_node(fn, sl_int(node, 8));
        if (range == NULL) {
            sl_fail(doc, 1, "error[E2S35]: selfhost-C11 node reference is "
                            "out of range");
            return fn->last_node;
        }
        int64_t low = sl_int(range, 7);
        int64_t high = sl_int(range, 8);
        sl_emit_expr(fn, doc, low, out);
        if (doc->error != NULL) return fn->last_node;
        sl_emit_expr(fn, doc, high, out);
        if (doc->error != NULL) return fn->last_node;
        sl_indent(fn, out);
        buffer_format(out,
                      "for (int64_t k_b%s = k_n%" PRId64 "; k_b%s < k_n%"
                      PRId64 "; ++k_b%s) {\n",
                      sl_field(node, 7), low, sl_field(node, 7), high,
                      sl_field(node, 7));
        fn->indent += 1;
        bool body_terminal = false;
        int64_t walk = sl_consume_expr(fn, index + 1);
        walk = sl_emit_block(fn, doc, walk, sl_int(node, 9), out,
                             &body_terminal);
        fn->indent -= 1;
        if (doc->error != NULL) return fn->last_node;
        sl_indent(fn, out);
        buffer_append(out, "}\n");
        return walk;
    }
    sl_fail_name(doc, 3,
                 "error[E2S10]: unsupported selfhost-C11 statement ", kind);
    return fn->last_node;
}

/* Parameter bindings of one function scope, in binding order. */
static void sl_parameters(
    SlDoc *doc,
    int64_t function_scope,
    Buffer *out,
    int64_t *arity
) {
    *arity = 0;
    for (int64_t index = 0; index < doc->binding_count; ++index) {
        const SlRecord *binding = &doc->bindings[index];
        if (sl_int(binding, 2) != function_scope) continue;
        const SlRecord *symbol = sl_symbol(doc, sl_int(binding, 3));
        if (symbol == NULL ||
            strcmp(sl_field(symbol, 2), "parameter") != 0) {
            continue;
        }
        const char *key = sl_type_key(doc, sl_int(symbol, 4));
        if (sl_c_type(key)[0] == '\0') {
            sl_fail_name(doc, 3,
                         "error[E2S10]: unsupported selfhost-C11 type ",
                         key);
            return;
        }
        if (*arity > 0) buffer_append(out, ", ");
        const char *spelled = "bool ";
        if (strcmp(key, "text") == 0) {
            spelled = "const char *";
        } else if (strcmp(key, "int") == 0) {
            spelled = "int64_t ";
        } else if (strcmp(key, "list-text") == 0) {
            spelled = "kofun_text_list ";
        }
        buffer_format(out, "%sk_b%s", spelled, sl_field(binding, 1));
        *arity += 1;
    }
}

static void sl_emit_function(
    SlDoc *doc,
    int64_t record,
    int64_t first_node,
    int64_t last_node,
    Buffer *prototypes,
    Buffer *bodies,
    Buffer *casts
) {
    const SlRecord *function = &doc->nodes[record];
    const SlRecord *symbol = sl_symbol(doc, sl_int(function, 2));
    if (symbol == NULL) {
        sl_fail(doc, 1,
                "error[E2S35]: selfhost-C11 function has no symbol");
        return;
    }
    const char *name = sl_field(symbol, 3);
    const char *result_key = sl_result_key(doc, symbol);
    bool is_main = strcmp(name, "main") == 0;
    Buffer parameters;
    buffer_init(&parameters);
    int64_t arity = 0;
    sl_parameters(doc, sl_int(function, 3), &parameters, &arity);
    if (doc->error != NULL) {
        free(parameters.data);
        return;
    }
    SlFn fn;
    memset(&fn, 0, sizeof(fn));
    fn.doc = doc;
    fn.first_node = first_node;
    fn.last_node = last_node;
    fn.function_name = name;
    fn.indent = 1;
    if (is_main) {
        fn.fail_return = "1";
    } else if (strcmp(result_key, "int") == 0) {
        fn.fail_return = "INT64_C(0)";
    } else if (strcmp(result_key, "bool") == 0) {
        fn.fail_return = "false";
    } else if (strcmp(result_key, "text") == 0) {
        fn.fail_return = "\"\"";
    } else if (strcmp(result_key, "void") == 0) {
        fn.fail_return = "";
    } else {
        sl_fail_name(doc, 3,
                     "error[E2S10]: unsupported selfhost-C11 type ",
                     result_key);
        free(parameters.data);
        return;
    }
    if (is_main) {
        if (arity != 0) {
            sl_fail(doc, 1,
                    "error[E2S15]: selfhost-C11 `main` takes no parameters");
            free(parameters.data);
            return;
        }
        buffer_append(bodies, "int main(void) {\n");
        buffer_append(bodies, casts->data == NULL ? "" : casts->data);
    } else {
        const char *c_result = strcmp(result_key, "void") == 0 ?
            "void" : sl_c_type(result_key);
        buffer_format(prototypes, "static %s%skofun_fn_%s(%s);\n",
                      c_result,
                      strcmp(result_key, "text") == 0 ? "" : " ",
                      name,
                      arity == 0 ? "void" : parameters.data);
        buffer_format(bodies, "static %s%skofun_fn_%s(%s) {\n",
                      c_result,
                      strcmp(result_key, "text") == 0 ? "" : " ",
                      name,
                      arity == 0 ? "void" : parameters.data);
    }
    bool terminal = false;
    int64_t walk = record + 1;
    while (doc->error == NULL && walk < last_node) {
        walk = sl_emit_statement(&fn, doc, walk, bodies, &terminal);
    }
    free(parameters.data);
    if (doc->error != NULL) return;
    if (!terminal && strcmp(result_key, "void") != 0 && !is_main) {
        sl_fail_name(doc, 1,
                     "error[E2S19]: selfhost-C11 function may complete "
                     "without returning a value: ",
                     name);
        return;
    }
    if (is_main && !terminal) {
        buffer_append(bodies, "    return 0;\n");
    }
    buffer_append(bodies, "}\n\n");
}

static char *sl_lower_document(SlDoc *doc, const char *text) {
    if (!sl_load(doc, text)) return NULL;
    Buffer prototypes;
    buffer_init(&prototypes);
    Buffer bodies;
    buffer_init(&bodies);
    Buffer casts;
    buffer_init(&casts);
    buffer_append(&casts,
                  "    (void)kofun_failed;\n"
                  "    (void)kofun_error;\n"
                  "    (void)kofun_add;\n"
                  "    (void)kofun_sub;\n"
                  "    (void)kofun_mul;\n"
                  "    (void)kofun_neg;\n"
                  "    (void)kofun_floor_div;\n"
                  "    (void)kofun_floor_mod;\n");
    int64_t main_count = 0;
    for (int64_t index = 0; index < doc->node_count; ++index) {
        const SlRecord *node = &doc->nodes[index];
        if (strcmp(sl_field(node, 0), "function") != 0) continue;
        const SlRecord *symbol = sl_symbol(doc, sl_int(node, 2));
        if (symbol == NULL) continue;
        if (strcmp(sl_field(symbol, 3), "main") == 0) {
            ++main_count;
        } else {
            buffer_format(&casts, "    (void)kofun_fn_%s;\n",
                          sl_field(symbol, 3));
        }
    }
    if (main_count != 1) {
        sl_fail(doc, 1,
                "error[E2S16]: selfhost-C11 program needs exactly one "
                "`main`");
    }
    for (int64_t index = 0;
         doc->error == NULL && index < doc->node_count;
         ++index) {
        if (strcmp(sl_field(&doc->nodes[index], 0), "function") != 0) {
            continue;
        }
        int64_t last = index + 1;
        while (last < doc->node_count &&
               strcmp(sl_field(&doc->nodes[last], 0), "function") != 0) {
            ++last;
        }
        sl_emit_function(doc, index, index + 1, last, &prototypes,
                         &bodies, &casts);
    }
    if (doc->error != NULL) {
        free(prototypes.data);
        free(bodies.data);
        free(casts.data);
        return NULL;
    }
    Buffer output;
    buffer_init(&output);
    buffer_append(&output,
                  "/* Generated by kofun-stage2 --lower-selfhost-c11. */\n");
    buffer_format(&output, "/* Source: %s %s */\n\n",
                  doc->source_path, doc->source_digest);
    buffer_append(&output, sl_prelude);
    buffer_append(&output, sl_prelude_text);
    buffer_append(&output, sl_prelude_unicode);
    if (prototypes.data != NULL && prototypes.data[0] != '\0') {
        buffer_append(&output, prototypes.data);
        buffer_append(&output, "\n");
    }
    buffer_append(&output, bodies.data == NULL ? "" : bodies.data);
    free(prototypes.data);
    free(bodies.data);
    free(casts.data);
    return output.data;
}

static void sl_free(SlDoc *doc) {
    for (int64_t index = 0; index < doc->type_count; ++index) {
        free(doc->type_keys[index]);
    }
    free(doc->document);
    free(doc->source_path);
    free(doc->source_digest);
    free(doc->error);
    free(doc);
}

static int lower_selfhost_c11_file(const char *input, const char *output) {
    if (same_file(input, output)) {
        puts("error[E2S35]: selfhost-C11 input and output must be distinct");
        return 2;
    }
    char *text = read_file(input);
    SlDoc *doc = allocate(sizeof(*doc));
    memset(doc, 0, sizeof(*doc));
    char *lowered = sl_lower_document(doc, text);
    if (lowered == NULL) {
        puts(doc->error);
        int exit_code = doc->error_exit;
        sl_free(doc);
        free(text);
        return exit_code;
    }
    write_file(output, lowered);
    free(lowered);
    sl_free(doc);
    free(text);
    return 0;
}

static int emit_scope_hir_file(const char *input, const char *output) {
    if (same_file(input, output)) {
        puts(
            "error[E2S35]: scope-HIR input and output must be distinct"
        );
        return 1;
    }
    char *source = read_file(input);
    char *tokens = lex_source(source);
    if (strncmp(tokens, "error[", 6) == 0) {
        puts(tokens);
        free(tokens);
        free(source);
        return 1;
    }
    free(tokens);
    char *tree = parse_pattern_trees(source);
    char *pattern_error = pattern_first_error(tree);
    free(tree);
    if (pattern_error[0] != '\0') {
        puts(pattern_error);
        free(pattern_error);
        free(source);
        return 1;
    }
    free(pattern_error);
    char *hir = build_scope_hir_mode(source, true);
    if (strncmp(hir, "error[", 6) == 0) {
        puts(hir);
        free(hir);
        free(source);
        return 1;
    }
    if (!write_file_transactional(output, hir)) {
        puts("error[E2S35]: cannot commit scope-HIR output");
        free(hir);
        free(source);
        return 1;
    }
    free(hir);
    free(source);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 6 && strcmp(argv[1], "--compile-outcome") == 0) {
        return compile_file(argv[2], argv[3], argv[4], argv[5]);
    }
    if (argc == 3 && strcmp(argv[1], "--check-ownership") == 0) {
        return check_ownership_file(argv[2]);
    }
    if (argc == 4 && strcmp(argv[1], "--parse-patterns") == 0) {
        return parse_patterns_file(argv[2], argv[3]);
    }
    if (argc == 4 && strcmp(argv[1], "--emit-scope-hir") == 0) {
        return emit_scope_hir_file(argv[2], argv[3]);
    }
    if (argc == 5 && strcmp(argv[1], "--emit-selfhost-hir") == 0) {
        return emit_selfhost_hir_file(argv[2], argv[3], argv[4]);
    }
    if (argc == 4 && strcmp(argv[1], "--lower-selfhost-c11") == 0) {
        return lower_selfhost_c11_file(argv[2], argv[3]);
    }
    if (argc != 5) {
        fputs(
            "usage: kofun-stage2 INPUT.kofun OUTPUT.kofun OUTPUT.ir OUTPUT.tokens\n"
            "       kofun-stage2 --compile-outcome INPUT.kofun OUTPUT.c OUTPUT.ir OUTPUT.tokens\n"
            "       kofun-stage2 --check-ownership INPUT.kofun\n"
            "       kofun-stage2 --parse-patterns INPUT.kofun OUTPUT.patterns\n"
            "       kofun-stage2 --emit-scope-hir INPUT.kofun OUTPUT.scope-hir\n"
            "       kofun-stage2 --emit-selfhost-hir INPUT.kofun OUTPUT.hir SOURCE-SHA256\n"
            "       kofun-stage2 --lower-selfhost-c11 INPUT.hir OUTPUT.c\n",
            stdout
        );
        return 2;
    }
    return compile_file(argv[1], argv[2], argv[3], argv[4]) == 0 ? 0 : 1;
}
