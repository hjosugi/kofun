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

static bool identifier_start(char symbol) {
    return symbol == '_' ||
           (symbol >= 'a' && symbol <= 'z') ||
           (symbol >= 'A' && symbol <= 'Z');
}

static bool identifier_continue(char symbol) {
    return identifier_start(symbol) || (symbol >= '0' && symbol <= '9');
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
    int64_t cursor = start + 1;
    if (identifier_start(first)) {
        while (cursor < length && identifier_continue(source[cursor])) ++cursor;
        return cursor;
    }
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
    if (identifier_start(first)) {
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

static int64_t next_function_start(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, start);
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
    int64_t cursor = skip_trivia(source, 0);
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
        buffer_format(&output, "kofun_fn_%s(", name);
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
        if (argument_end < 0) return -1;
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

static char *validate_core_calls(const char *source) {
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
            int64_t value_start = skip_trivia(
                source,
                token_end(source, cursor)
            );
            int64_t direct_end = skip_trivia(
                source,
                token_end(source, value_start)
            );
            int64_t arms_open = -1;
            if (
                strcmp(token_kind(source, value_start), "identifier") == 0 &&
                token_equal(source, direct_end, "{")
            ) {
                arms_open = direct_end;
            } else {
                int64_t value_close = condition_end(source, value_start);
                if (value_close >= 0) {
                    arms_open = skip_trivia(source, value_close);
                }
            }
            if (
                arms_open >= 0 && arms_open < target &&
                token_equal(source, arms_open, "{")
            ) {
                int64_t arm_cursor = skip_trivia(
                    source,
                    token_end(source, arms_open)
                );
                while (
                    arm_cursor <= target &&
                    !token_equal(source, arm_cursor, "}")
                ) {
                    if (arm_cursor == target) return true;
                    int64_t arrow = skip_trivia(
                        source,
                        token_end(source, arm_cursor)
                    );
                    if (token_equal(source, arrow, "if")) {
                        int64_t guard_start = skip_trivia(
                            source,
                            token_end(source, arrow)
                        );
                        int64_t guard_end = condition_end(
                            source,
                            guard_start
                        );
                        if (guard_end < 0) {
                            arm_cursor = target + 1;
                        } else {
                            arrow = skip_trivia(source, guard_end);
                        }
                    }
                    if (
                        arm_cursor <= target &&
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

static char *scope_hir_error(
    Buffer *hir,
    const char *message,
    int64_t cursor
) {
    free(hir->data);
    return lower_error("E2S35", message, cursor);
}

static char *build_scope_hir(const char *source) {
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
                if (token_equal(source, after_name, ":")) {
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
    char *call_check = validate_core_calls(source);
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
            free(prototypes.data);
            free(bodies.data);
            return error.data;
        }
        bool is_main = strcmp(name, "main") == 0;
        int64_t arity = parameter_count(source, cursor);
        char *parameters = core_parameters(source, hir, cursor);
        if (strncmp(parameters, "error[", 6) == 0) {
            free(name);
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
                name,
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
            free(prototypes.data);
            free(bodies.data);
            return error.data;
        }
        char *body = lower_body(source, hir, open, is_main, true, open);
        if (strncmp(body, "error[", 6) == 0) {
            free(parameters);
            free(name);
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
                name,
                c_parameters
            );
            buffer_append(&bodies, body);
            buffer_append(&bodies, "}\n");
        }
        free(body);
        free(parameters);
        free(name);
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

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--check-ownership") == 0) {
        return check_ownership_file(argv[2]);
    }
    if (argc == 4 && strcmp(argv[1], "--parse-patterns") == 0) {
        return parse_patterns_file(argv[2], argv[3]);
    }
    if (argc != 5) {
        fputs(
            "usage: kofun-stage2 INPUT.kofun OUTPUT.kofun OUTPUT.ir OUTPUT.tokens\n"
            "       kofun-stage2 --check-ownership INPUT.kofun\n"
            "       kofun-stage2 --parse-patterns INPUT.kofun OUTPUT.patterns\n",
            stdout
        );
        return 2;
    }

    char *source = read_file(argv[1]);
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
    write_file(argv[3], ir);
    write_file(argv[4], tokens);
    if (ends_with(argv[2], ".c")) {
        char *pattern_check = validate_executable_patterns(source);
        if (strncmp(pattern_check, "error[", 6) == 0) {
            puts(pattern_check);
            free(pattern_check);
            free(ir);
            free(tokens);
            free(source);
            return 1;
        }
        free(pattern_check);
        char *hir = build_scope_hir(source);
        if (strncmp(hir, "error[", 6) == 0) {
            puts(hir);
            free(hir);
            free(ir);
            free(tokens);
            free(source);
            return 1;
        }
        char *c_source = lower_c(source, hir);
        if (strncmp(c_source, "error[", 6) == 0) {
            puts(c_source);
            free(c_source);
            free(hir);
            free(ir);
            free(tokens);
            free(source);
            return 1;
        }
        Buffer combined_ir;
        buffer_init(&combined_ir);
        buffer_append(&combined_ir, ir);
        buffer_append(&combined_ir, hir);
        write_file(argv[3], combined_ir.data);
        write_file(argv[2], c_source);
        free(combined_ir.data);
        free(c_source);
        free(hir);
    } else {
        write_file(argv[2], source);
    }
    puts(argv[2]);
    free(ir);
    free(tokens);
    free(source);
    return 0;
}
