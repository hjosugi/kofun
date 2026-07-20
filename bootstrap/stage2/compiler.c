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
        "in", "break", "continue", "true", "false", "match"
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

static char *owned_text(const char *text) {
    size_t length = strlen(text);
    char *copy = allocate(length + 1);
    memcpy(copy, text, length + 1);
    return copy;
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
    int64_t function_cursor = skip_trivia(source, 0);
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
        function_cursor = skip_trivia(source, function_end_cursor);
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
static char *emit_expression(const char *source, int64_t start, int64_t end);

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
        char *name = token_copy(source, cursor);
        int64_t open = skip_trivia(source, token_end(source, cursor));
        Buffer output;
        buffer_init(&output);
        if (open >= end || !token_equal(source, open, "(")) {
            buffer_format(&output, "k_%s", name);
            free(name);
            return output.data;
        }
        buffer_format(&output, "kofun_fn_%s(", name);
        int64_t argument = skip_trivia(source, token_end(source, open));
        int64_t arguments = 0;
        while (argument < end && !token_equal(source, argument, ")")) {
            int64_t argument_end = expression_end(source, argument);
            char *value = emit_expression(source, argument, argument_end);
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

static char *lower_error(
    const char *code,
    const char *message,
    int64_t cursor
);

static int64_t function_arity(const char *source, const char *wanted) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = skip_trivia(source, 0);
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
        cursor = skip_trivia(source, function_end(source, cursor));
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
    int64_t cursor = skip_trivia(source, 0);
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

static char *core_parameters(const char *source, int64_t function_start) {
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
        buffer_format(&emitted, "int64_t k_%s", name);
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

static char *emit_condition(
    const char *source,
    int64_t start,
    int64_t end
) {
    int64_t cursor = skip_trivia(source, start);
    if (
        token_equal(source, cursor, "true") ||
        token_equal(source, cursor, "false")
    ) {
        return token_copy(source, cursor);
    }
    int64_t left_end = expression_end(source, cursor);
    int64_t operator_start = skip_trivia(source, left_end);
    int64_t right_start = skip_trivia(
        source,
        token_end(source, operator_start)
    );
    char *left = emit_expression(source, cursor, left_end);
    char *operator_text = token_copy(source, operator_start);
    char *right = emit_expression(source, right_start, end);
    Buffer output;
    buffer_init(&output);
    buffer_format(&output, "(%s %s %s)", left, operator_text, right);
    free(left);
    free(operator_text);
    free(right);
    return output.data;
}

static int64_t core_body_open(
    const char *source,
    int64_t function_start,
    bool is_main
) {
    int64_t length = (int64_t)strlen(source);
    int64_t parameters = parameter_open(source, function_start);
    if (parameters < 0) return -1;
    int64_t parameters_end = balanced_end(source, parameters, "(", ")");
    if (parameters_end < 0) return -1;
    char *parameter_text = core_parameters(source, function_start);
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

static int binding_mutability_before(
    const char *source,
    int64_t body_open,
    int64_t assignment_start,
    const char *binding_name
) {
    int64_t cursor = skip_trivia(source, token_end(source, body_open));
    int result = -1;
    int depth = 0;
    while (cursor < assignment_start) {
        if (token_equal(source, cursor, "{")) {
            ++depth;
        } else if (token_equal(source, cursor, "}")) {
            --depth;
        } else if (depth == 0 && token_equal(source, cursor, "let")) {
            int64_t name_cursor = skip_trivia(
                source,
                token_end(source, cursor)
            );
            bool mutable = false;
            if (
                name_cursor < assignment_start &&
                token_equal(source, name_cursor, "mut")
            ) {
                mutable = true;
                name_cursor = skip_trivia(
                    source,
                    token_end(source, name_cursor)
                );
            }
            if (
                name_cursor < assignment_start &&
                strcmp(token_kind(source, name_cursor), "identifier") == 0 &&
                token_equal(source, name_cursor, binding_name)
            ) {
                result = mutable ? 1 : 0;
            }
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return result;
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

static void free_match_bodies(
    char *true_body,
    char *false_body,
    char *catchall_body
) {
    free(true_body);
    free(false_body);
    free(catchall_body);
}

static char *lower_body(
    const char *source,
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
                "    if (kofun_failed) return %s;\n",
                name,
                value,
                failure_result
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
                branch_open,
                is_main,
                false,
                function_open
            );
            if (strncmp(branch_body, "error[", 6) == 0) {
                free(emitted.data);
                return branch_body;
            }
            char *condition = emit_condition(
                source,
                condition_start,
                condition_close
            );
            buffer_format(
                &emitted,
                "    {\n"
                "        bool kofun_condition = %s;\n"
                "        if (kofun_failed) return %s;\n"
                "        if (kofun_condition) {\n"
                "%s"
                "        }",
                condition,
                failure_result,
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
            int64_t value_end = condition_end(source, value_start);
            if (value_end < 0) {
                free(emitted.data);
                return lower_error(
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
                free(emitted.data);
                return lower_error(
                    "E2S24",
                    "expected `{` after match scrutinee",
                    arms_open
                );
            }
            int64_t arm_cursor = skip_trivia(
                source,
                token_end(source, arms_open)
            );
            bool seen_true = false;
            bool seen_false = false;
            bool seen_catchall = false;
            char *true_body = NULL;
            char *false_body = NULL;
            char *catchall_body = NULL;
            while (
                arm_cursor < length &&
                !token_equal(source, arm_cursor, "}")
            ) {
                int64_t pattern_start = arm_cursor;
                bool pattern_true = token_equal(
                    source,
                    pattern_start,
                    "true"
                );
                bool pattern_false = token_equal(
                    source,
                    pattern_start,
                    "false"
                );
                bool pattern_catchall = token_equal(
                    source,
                    pattern_start,
                    "_"
                );
                if (seen_catchall) {
                    free_match_bodies(
                        true_body,
                        false_body,
                        catchall_body
                    );
                    free(emitted.data);
                    return lower_error(
                        "E2S26",
                        "pattern after catch-all is unreachable",
                        pattern_start
                    );
                }
                if (pattern_true && seen_true) {
                    free_match_bodies(
                        true_body,
                        false_body,
                        catchall_body
                    );
                    free(emitted.data);
                    return lower_error(
                        "E2S26",
                        "duplicate `true` pattern is unreachable",
                        pattern_start
                    );
                }
                if (pattern_false && seen_false) {
                    free_match_bodies(
                        true_body,
                        false_body,
                        catchall_body
                    );
                    free(emitted.data);
                    return lower_error(
                        "E2S26",
                        "duplicate `false` pattern is unreachable",
                        pattern_start
                    );
                }
                if (pattern_catchall && seen_true && seen_false) {
                    free_match_bodies(
                        true_body,
                        false_body,
                        catchall_body
                    );
                    free(emitted.data);
                    return lower_error(
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
                    free_match_bodies(
                        true_body,
                        false_body,
                        catchall_body
                    );
                    free(emitted.data);
                    return lower_error(
                        "E2S24",
                        "bounded Bool pattern must be `true`, `false`, or `_`",
                        pattern_start
                    );
                }
                int64_t after_pattern = skip_trivia(
                    source,
                    token_end(source, pattern_start)
                );
                if (
                    after_pattern < length &&
                    token_equal(source, after_pattern, "if")
                ) {
                    free_match_bodies(
                        true_body,
                        false_body,
                        catchall_body
                    );
                    free(emitted.data);
                    return lower_error(
                        "E2S24",
                        "guards are outside bounded Bool match",
                        after_pattern
                    );
                }
                if (
                    after_pattern >= length ||
                    !token_equal(source, after_pattern, "=>")
                ) {
                    free_match_bodies(
                        true_body,
                        false_body,
                        catchall_body
                    );
                    free(emitted.data);
                    return lower_error(
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
                    free_match_bodies(
                        true_body,
                        false_body,
                        catchall_body
                    );
                    free(emitted.data);
                    return lower_error(
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
                    free_match_bodies(
                        true_body,
                        false_body,
                        catchall_body
                    );
                    free(emitted.data);
                    return lower_error(
                        "E2S24",
                        "missing `}` after match arm",
                        arm_open
                    );
                }
                char *arm_body = lower_body(
                    source,
                    arm_open,
                    is_main,
                    false,
                    function_open
                );
                if (strncmp(arm_body, "error[", 6) == 0) {
                    free_match_bodies(
                        true_body,
                        false_body,
                        catchall_body
                    );
                    free(emitted.data);
                    return arm_body;
                }
                if (pattern_true) {
                    seen_true = true;
                    true_body = arm_body;
                } else if (pattern_false) {
                    seen_false = true;
                    false_body = arm_body;
                } else {
                    seen_catchall = true;
                    catchall_body = arm_body;
                }
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
                    free_match_bodies(
                        true_body,
                        false_body,
                        catchall_body
                    );
                    free(emitted.data);
                    return lower_error(
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
                free_match_bodies(
                    true_body,
                    false_body,
                    catchall_body
                );
                free(emitted.data);
                return lower_error(
                    "E2S24",
                    "missing `}` after match arms",
                    arms_open
                );
            }
            if (!seen_true && !seen_false && !seen_catchall) {
                free_match_bodies(
                    true_body,
                    false_body,
                    catchall_body
                );
                free(emitted.data);
                return lower_error(
                    "E2S25",
                    "non-exhaustive Bool match; missing patterns `true`, `false`",
                    match_start
                );
            }
            if (!seen_true && !seen_catchall) {
                free_match_bodies(
                    true_body,
                    false_body,
                    catchall_body
                );
                free(emitted.data);
                return lower_error(
                    "E2S25",
                    "non-exhaustive Bool match; missing pattern `true`",
                    match_start
                );
            }
            if (!seen_false && !seen_catchall) {
                free_match_bodies(
                    true_body,
                    false_body,
                    catchall_body
                );
                free(emitted.data);
                return lower_error(
                    "E2S25",
                    "non-exhaustive Bool match; missing pattern `false`",
                    match_start
                );
            }
            const char *emitted_true = seen_true
                ? true_body
                : catchall_body;
            const char *emitted_false = seen_false
                ? false_body
                : catchall_body;
            char *match_value = emit_condition(
                source,
                value_start,
                value_end
            );
            buffer_format(
                &emitted,
                "    {\n"
                "        bool kofun_match_value = %s;\n"
                "        if (kofun_failed) return %s;\n"
                "        if (kofun_match_value) {\n"
                "%s"
                "        } else {\n"
                "%s"
                "        }\n"
                "    }\n",
                match_value,
                failure_result,
                emitted_true,
                emitted_false
            );
            free(match_value);
            free_match_bodies(true_body, false_body, catchall_body);
            cursor = skip_trivia(source, token_end(source, arm_cursor));
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
                int mutability = binding_mutability_before(
                    source,
                    open,
                    assignment_start,
                    name
                );
                if (mutability < 0) {
                    if (
                        open != function_open &&
                        binding_mutability_before(
                            source,
                            function_open,
                            open,
                            name
                        ) >= 0
                    ) {
                        char *error = assignment_error(
                            "cannot assign to outer binding",
                            name,
                            assignment_start,
                            "assign after the branch"
                        );
                        free(name);
                        free(emitted.data);
                        return error;
                    }
                    char *error = assignment_error(
                        "unknown assignment target",
                        name,
                        assignment_start,
                        "declare it before assignment"
                    );
                    free(name);
                    free(emitted.data);
                    return error;
                }
                if (mutability == 0) {
                    char *error = assignment_error(
                        "cannot assign to immutable binding",
                        name,
                        assignment_start,
                        "declare it with `let mut`"
                    );
                    free(name);
                    free(emitted.data);
                    return error;
                }
                int64_t value_start = skip_trivia(
                    source,
                    token_end(source, equals)
                );
                int64_t value_end = expression_end(source, value_start);
                if (value_end < 0) {
                    free(name);
                    free(emitted.data);
                    return lower_error(
                        "E2S12",
                        "invalid Int expression",
                        value_start
                    );
                }
                char *value = emit_expression(source, value_start, value_end);
                buffer_format(
                    &emitted,
                    "    {\n"
                    "        int64_t kofun_replacement = %s;\n"
                    "        if (kofun_failed) return %s;\n"
                    "        k_%s = kofun_replacement;\n"
                    "    }\n",
                    value,
                    failure_result,
                    name
                );
                free(value);
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
                char *value = emit_expression(source, cursor, value_end);
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

static char *lower_c(const char *source) {
    int64_t length = (int64_t)strlen(source);
    char *call_check = validate_core_calls(source);
    if (strncmp(call_check, "error[", 6) == 0) return call_check;
    free(call_check);

    Buffer prototypes;
    Buffer bodies;
    buffer_init(&prototypes);
    buffer_init(&bodies);
    int64_t cursor = skip_trivia(source, 0);
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
        char *parameters = core_parameters(source, cursor);
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
        int64_t open = core_body_open(source, cursor, is_main);
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
        char *body = lower_body(source, open, is_main, true, open);
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
        cursor = skip_trivia(source, function_end(source, cursor));
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

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--check-ownership") == 0) {
        return check_ownership_file(argv[2]);
    }
    if (argc != 5) {
        fputs(
            "usage: kofun-stage2 INPUT.kofun OUTPUT.kofun OUTPUT.ir OUTPUT.tokens\n"
            "       kofun-stage2 --check-ownership INPUT.kofun\n",
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
