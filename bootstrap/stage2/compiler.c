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
        "//", "..", "**", "??", "|>"
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
        "in", "break", "continue", "true", "false"
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

static char *parse_program(const char *source) {
    Buffer ir;
    buffer_init(&ir);
    int64_t length = (int64_t)strlen(source);
    buffer_format(&ir, "kofun-stage2-ir/v1\nsource-bytes|%" PRId64 "\n", length);
    int64_t cursor = skip_trivia(source, 0);
    int64_t functions = 0;
    while (cursor < length) {
        if (!token_equal(source, cursor, "fn")) {
            ir.length = 0;
            ir.data[0] = '\0';
            buffer_format(
                &ir,
                "error[E2S02]: expected top-level `fn` at byte %" PRId64,
                cursor
            );
            return ir.data;
        }
        char *name = function_name(source, cursor);
        int64_t arity = parameter_count(source, cursor);
        int64_t end = function_end(source, cursor);
        if (name[0] == '\0' || arity < 0 || end < 0) {
            free(name);
            ir.length = 0;
            ir.data[0] = '\0';
            buffer_format(
                &ir,
                "error[E2S03]: malformed function at byte %" PRId64,
                cursor
            );
            return ir.data;
        }
        buffer_format(
            &ir,
            "function|%s|%" PRId64 "|%" PRId64 "|%" PRId64 "\n",
            name,
            arity,
            cursor,
            end
        );
        free(name);
        ++functions;
        cursor = skip_trivia(source, end);
    }
    if (functions == 0) {
        ir.length = 0;
        ir.data[0] = '\0';
        buffer_append(&ir, "error[E2S04]: compilation unit has no functions");
        return ir.data;
    }
    buffer_format(&ir, "function-count|%" PRId64 "\n", functions);
    return ir.data;
}

static int64_t expression_end(const char *source, int64_t start);
static char *emit_expression(const char *source, int64_t start, int64_t end);

static int64_t primary_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, start);
    if (cursor >= length) return -1;
    const char *kind = token_kind(source, cursor);
    if (strcmp(kind, "integer") == 0 || strcmp(kind, "identifier") == 0) {
        return token_end(source, cursor);
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

static char *emit_primary(const char *source, int64_t start, int64_t end) {
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
        char *name = source_slice(source, cursor, end);
        Buffer output;
        buffer_init(&output);
        buffer_format(&output, "k_%s", name);
        free(name);
        return output.data;
    }
    if (token_equal(source, cursor, "(")) {
        int64_t value_start = skip_trivia(source, token_end(source, cursor));
        int64_t close = skip_trivia(source, expression_end(source, value_start));
        char *value = emit_expression(source, value_start, close);
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

static char *emit_unary(const char *source, int64_t start, int64_t end) {
    int64_t cursor = skip_trivia(source, start);
    if (token_equal(source, cursor, "+")) {
        int64_t value_start = skip_trivia(source, token_end(source, cursor));
        return emit_unary(source, value_start, end);
    }
    if (token_equal(source, cursor, "-")) {
        int64_t value_start = skip_trivia(source, token_end(source, cursor));
        char *value = emit_unary(source, value_start, end);
        Buffer output;
        buffer_init(&output);
        buffer_format(&output, "kofun_neg(%s)", value);
        free(value);
        return output.data;
    }
    return emit_primary(source, cursor, end);
}

static char *emit_product(const char *source, int64_t start, int64_t end) {
    int64_t cursor = unary_end(source, start);
    char *emitted = emit_unary(source, start, cursor);
    int64_t operator_start = skip_trivia(source, cursor);
    while (operator_start < end) {
        char *operator_text = token_copy(source, operator_start);
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        int64_t right_end = unary_end(source, right_start);
        char *right = emit_unary(source, right_start, right_end);
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

static char *emit_expression(const char *source, int64_t start, int64_t end) {
    int64_t cursor = product_end(source, start);
    char *emitted = emit_product(source, start, cursor);
    int64_t operator_start = skip_trivia(source, cursor);
    while (operator_start < end) {
        char *operator_text = token_copy(source, operator_start);
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        int64_t right_end = product_end(source, right_start);
        char *right = emit_product(source, right_start, right_end);
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

static int64_t core_body_open(const char *source, int64_t function_start) {
    int64_t length = (int64_t)strlen(source);
    int64_t parameters = parameter_open(source, function_start);
    if (parameters < 0) return -1;
    int64_t parameters_end = balanced_end(source, parameters, "(", ")");
    if (parameters_end < 0) return -1;
    int64_t cursor = skip_trivia(source, parameters_end);
    if (cursor < length && token_equal(source, cursor, "->")) {
        cursor = skip_trivia(source, token_end(source, cursor));
        if (cursor >= length || !token_equal(source, cursor, "Int")) return -1;
        cursor = skip_trivia(source, token_end(source, cursor));
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

static char *lower_body(const char *source, int64_t open) {
    int64_t length = (int64_t)strlen(source);
    Buffer emitted;
    buffer_init(&emitted);
    int64_t cursor = skip_trivia(source, token_end(source, open));
    bool returned = false;
    while (cursor < length && !token_equal(source, cursor, "}")) {
        if (returned) {
            free(emitted.data);
            return lower_error("E2S14", "statement follows `return`", cursor);
        }
        if (token_equal(source, cursor, "let")) {
            cursor = skip_trivia(source, token_end(source, cursor));
            if (cursor < length && token_equal(source, cursor, "mut")) {
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
            cursor = skip_trivia(source, token_end(source, cursor));
            if (cursor < length && token_equal(source, cursor, ":")) {
                cursor = skip_trivia(source, token_end(source, cursor));
                if (cursor >= length || !token_equal(source, cursor, "Int")) {
                    free(name);
                    free(emitted.data);
                    return lower_error(
                        "E2S11",
                        "Core binding type must be Int",
                        cursor
                    );
                }
                cursor = skip_trivia(source, token_end(source, cursor));
            }
            if (cursor >= length || !token_equal(source, cursor, "=")) {
                free(name);
                free(emitted.data);
                return lower_error("E2S11", "expected `=`", cursor);
            }
            int64_t value_start = skip_trivia(source, token_end(source, cursor));
            int64_t value_end = expression_end(source, value_start);
            if (value_end < 0) {
                free(name);
                free(emitted.data);
                return lower_error("E2S12", "invalid Int expression", value_start);
            }
            char *value = emit_expression(source, value_start, value_end);
            buffer_format(
                &emitted,
                "    int64_t k_%s = %s;\n"
                "    if (kofun_failed) return 1;\n",
                name,
                value
            );
            free(value);
            free(name);
            cursor = skip_trivia(source, value_end);
        } else if (token_equal(source, cursor, "print")) {
            int64_t call_open = skip_trivia(source, token_end(source, cursor));
            if (call_open >= length || !token_equal(source, call_open, "(")) {
                free(emitted.data);
                return lower_error("E2S13", "expected `print(`", cursor);
            }
            int64_t value_start = skip_trivia(source, token_end(source, call_open));
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
            char *value = emit_expression(source, value_start, value_end);
            buffer_format(
                &emitted,
                "    {\n"
                "        int64_t kofun_value = %s;\n"
                "        if (kofun_failed) return 1;\n"
                "        printf(\"%%\" PRId64 \"\\n\", kofun_value);\n"
                "    }\n",
                value
            );
            free(value);
            cursor = skip_trivia(source, token_end(source, call_close));
        } else if (token_equal(source, cursor, "return")) {
            int64_t value_start = skip_trivia(source, token_end(source, cursor));
            if (value_start < length && token_equal(source, value_start, "}")) {
                buffer_append(&emitted, "    return 0;\n");
                cursor = value_start;
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
                char *value = emit_expression(source, value_start, value_end);
                buffer_format(
                    &emitted,
                    "    {\n"
                    "        int64_t kofun_result = %s;\n"
                    "        if (kofun_failed) return 1;\n"
                    "        return (int)kofun_result;\n"
                    "    }\n",
                    value
                );
                free(value);
                cursor = skip_trivia(source, value_end);
            }
            returned = true;
        } else {
            free(emitted.data);
            return lower_error("E2S10", "unsupported Core statement", cursor);
        }
    }
    if (cursor >= length || !token_equal(source, cursor, "}")) {
        free(emitted.data);
        return lower_error("E2S03", "missing function close", -1);
    }
    if (!returned) buffer_append(&emitted, "    return 0;\n");
    return emitted.data;
}

static char *lower_c(const char *source) {
    int64_t length = (int64_t)strlen(source);
    int64_t function_start = skip_trivia(source, 0);
    char *name = function_name(source, function_start);
    int64_t arity = parameter_count(source, function_start);
    if (
        function_start >= length ||
        strcmp(name, "main") != 0 ||
        arity != 0
    ) {
        free(name);
        return lower_error("E2S10", "C11 Core requires one `fn main()`", -1);
    }
    free(name);
    int64_t function_close = function_end(source, function_start);
    if (
        function_close < 0 ||
        skip_trivia(source, function_close) != length
    ) {
        return lower_error(
            "E2S10",
            "C11 Core requires exactly one function",
            -1
        );
    }
    int64_t open = core_body_open(source, function_start);
    if (open < 0) {
        return lower_error(
            "E2S10",
            "Core main return type must be Int",
            -1
        );
    }
    char *body = lower_body(source, open);
    if (strncmp(body, "error[", 6) == 0) return body;

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
        "int main(void) {\n"
        "    (void)kofun_failed;\n"
    );
    buffer_append(&output, body);
    buffer_append(&output, "}\n");
    free(body);
    return output.data;
}

static bool ends_with(const char *value, const char *suffix) {
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    return value_length >= suffix_length &&
           strcmp(value + value_length - suffix_length, suffix) == 0;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fputs(
            "usage: kofun-stage2 INPUT.kofun OUTPUT.kofun OUTPUT.ir OUTPUT.tokens\n",
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
        char *c_source = lower_c(source);
        if (strncmp(c_source, "error[", 6) == 0) {
            puts(c_source);
            free(c_source);
            free(ir);
            free(tokens);
            free(source);
            return 1;
        }
        write_file(argv[2], c_source);
        free(c_source);
    } else {
        write_file(argv[2], source);
    }
    puts(argv[2]);
    free(ir);
    free(tokens);
    free(source);
    return 0;
}
