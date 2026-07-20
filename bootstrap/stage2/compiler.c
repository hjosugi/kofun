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

static bool top_level_boundary(const char *source, int64_t start) {
    if (start == 0) return true;
    int64_t length = (int64_t)strlen(source);
    return start > 0 && start <= length && source[start - 1] == '\n';
}

static int64_t sync_top_level(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = start;
    int64_t scanned = 0;
    while (cursor < length && scanned < 4096) {
        cursor = skip_trivia(source, cursor);
        if (cursor >= length) return length;
        if (
            token_equal(source, cursor, "fn") &&
            top_level_boundary(source, cursor)
        ) {
            return cursor;
        }
        int64_t end = token_end(source, cursor);
        cursor = end <= cursor ? cursor + 1 : end;
        ++scanned;
    }
    return cursor < length ? -1 : length;
}

static char *parse_program(const char *source) {
    Buffer ir;
    Buffer recovered;
    buffer_init(&ir);
    buffer_init(&recovered);
    int64_t length = (int64_t)strlen(source);
    buffer_format(&ir, "kofun-stage2-ir/v1\nsource-bytes|%" PRId64 "\n", length);
    int64_t cursor = skip_trivia(source, 0);
    int64_t functions = 0;
    int64_t diagnostics = 0;
    bool truncated = false;
    while (cursor < length) {
        if (!token_equal(source, cursor, "fn")) {
            int64_t start = cursor;
            int64_t after = token_end(source, cursor);
            int64_t next = sync_top_level(source, after);
            if (next < 0) {
                next = length;
                truncated = true;
            }
            if (next <= cursor) next = cursor + 1;
            buffer_format(
                &recovered,
                "diagnostic|E2S02|%" PRId64 "|%" PRId64
                "|expected-top-level-fn\n",
                start,
                next
            );
            ++diagnostics;
            cursor = next;
            if (diagnostics >= 8 && cursor < length) {
                truncated = true;
                cursor = length;
            }
            continue;
        }
        char *name = function_name(source, cursor);
        int64_t arity = parameter_count(source, cursor);
        int64_t end = function_end(source, cursor);
        if (name[0] == '\0' || arity < 0 || end < 0) {
            free(name);
            int64_t start = cursor;
            int64_t after = token_end(source, cursor);
            int64_t next = sync_top_level(source, after);
            if (next < 0) {
                next = length;
                truncated = true;
            }
            if (next <= cursor) next = cursor + 1;
            buffer_format(
                &recovered,
                "diagnostic|E2S03|%" PRId64 "|%" PRId64
                "|malformed-function\n",
                start,
                next
            );
            ++diagnostics;
            cursor = next;
            if (diagnostics >= 8 && cursor < length) {
                truncated = true;
                cursor = length;
            }
            continue;
        }
        buffer_format(
            &ir,
            "function|%s|%" PRId64 "|%" PRId64 "|%" PRId64 "\n",
            name,
            arity,
            cursor,
            end
        );
        buffer_format(
            &recovered,
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
    if (functions == 0 && diagnostics < 8) {
        buffer_format(
            &recovered,
            "diagnostic|E2S04|0|%" PRId64
            "|compilation-unit-has-no-functions\n",
            length
        );
        ++diagnostics;
    }
    if (diagnostics > 0) {
        Buffer report;
        buffer_init(&report);
        buffer_format(
            &report,
            "error[E2S20]: recovered top-level parse\n"
            "kofun-stage2-recovery/v1\n"
            "source-bytes|%" PRId64 "\n",
            length
        );
        buffer_append(&report, recovered.data);
        buffer_format(
            &report,
            "diagnostic-count|%" PRId64 "\n"
            "function-count|%" PRId64 "\n"
            "diagnostic-limit|8\n"
            "truncated|%s\n",
            diagnostics,
            functions,
            truncated ? "true" : "false"
        );
        free(recovered.data);
        free(ir.data);
        return report.data;
    }
    free(recovered.data);
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
    if (token_equal(source, cursor, "if")) {
        int64_t condition_start = skip_trivia(
            source,
            token_end(source, cursor)
        );
        int64_t condition_end = expression_end(source, condition_start);
        if (condition_end < 0) return -1;
        int64_t then_open = skip_trivia(source, condition_end);
        if (
            then_open >= length ||
            !token_equal(source, then_open, "{")
        ) {
            return -1;
        }
        int64_t then_start = skip_trivia(
            source,
            token_end(source, then_open)
        );
        int64_t then_end = expression_end(source, then_start);
        if (then_end < 0) return -1;
        int64_t then_close = skip_trivia(source, then_end);
        if (
            then_close >= length ||
            !token_equal(source, then_close, "}")
        ) {
            return -1;
        }
        int64_t else_keyword = skip_trivia(
            source,
            token_end(source, then_close)
        );
        if (
            else_keyword >= length ||
            !token_equal(source, else_keyword, "else")
        ) {
            return token_end(source, then_close);
        }
        int64_t else_open = skip_trivia(
            source,
            token_end(source, else_keyword)
        );
        if (
            else_open >= length ||
            !token_equal(source, else_open, "{")
        ) {
            return -1;
        }
        int64_t else_start = skip_trivia(
            source,
            token_end(source, else_open)
        );
        int64_t else_end = expression_end(source, else_start);
        if (else_end < 0) return -1;
        int64_t else_close = skip_trivia(source, else_end);
        if (
            else_close >= length ||
            !token_equal(source, else_close, "}")
        ) {
            return -1;
        }
        return token_end(source, else_close);
    }
    if (
        strcmp(kind, "integer") == 0 ||
        token_equal(source, cursor, "true") ||
        token_equal(source, cursor, "false")
    ) {
        return token_end(source, cursor);
    }
    if (strcmp(kind, "identifier") == 0) {
        int64_t name_end = token_end(source, cursor);
        int64_t bracket = skip_trivia(source, name_end);
        if (bracket < length && token_equal(source, bracket, "[")) {
            int64_t index_start = skip_trivia(
                source,
                token_end(source, bracket)
            );
            int64_t index_end = expression_end(source, index_start);
            if (index_end < 0) return -1;
            int64_t close = skip_trivia(source, index_end);
            if (close >= length || !token_equal(source, close, "]")) return -1;
            return token_end(source, close);
        }
        return name_end;
    }
    if (token_equal(source, cursor, "[")) {
        int64_t item_start = skip_trivia(
            source,
            token_end(source, cursor)
        );
        if (item_start < length && token_equal(source, item_start, "]")) {
            return token_end(source, item_start);
        }
        while (item_start < length) {
            int64_t item_end = expression_end(source, item_start);
            if (item_end < 0) return -1;
            int64_t separator = skip_trivia(source, item_end);
            if (
                separator < length &&
                token_equal(source, separator, "]")
            ) {
                return token_end(source, separator);
            }
            if (
                separator >= length ||
                !token_equal(source, separator, ",")
            ) {
                return -1;
            }
            item_start = skip_trivia(
                source,
                token_end(source, separator)
            );
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
    if (
        token_equal(source, cursor, "+") ||
        token_equal(source, cursor, "-") ||
        token_equal(source, cursor, "!")
    ) {
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

static int64_t sum_end(const char *source, int64_t start) {
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

static bool comparison_operator(const char *source, int64_t start) {
    return token_equal(source, start, "==") ||
           token_equal(source, start, "!=") ||
           token_equal(source, start, "<") ||
           token_equal(source, start, "<=") ||
           token_equal(source, start, ">") ||
           token_equal(source, start, ">=");
}

static int64_t comparison_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = sum_end(source, start);
    if (cursor < 0) return -1;
    int64_t operator_start = skip_trivia(source, cursor);
    if (operator_start < length && comparison_operator(source, operator_start)) {
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        cursor = sum_end(source, right_start);
        if (cursor < 0) return -1;
        int64_t trailing = skip_trivia(source, cursor);
        if (trailing < length && comparison_operator(source, trailing)) {
            return -1;
        }
    }
    return cursor;
}

static int64_t and_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = comparison_end(source, start);
    if (cursor < 0) return -1;
    int64_t operator_start = skip_trivia(source, cursor);
    while (
        operator_start < length &&
        token_equal(source, operator_start, "&&")
    ) {
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        cursor = comparison_end(source, right_start);
        if (cursor < 0) return -1;
        operator_start = skip_trivia(source, cursor);
    }
    return cursor;
}

static int64_t expression_end(const char *source, int64_t start) {
    int64_t length = (int64_t)strlen(source);
    int64_t cursor = and_end(source, start);
    if (cursor < 0) return -1;
    int64_t operator_start = skip_trivia(source, cursor);
    while (
        operator_start < length &&
        token_equal(source, operator_start, "||")
    ) {
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        cursor = and_end(source, right_start);
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
    if (token_equal(source, cursor, "true")) {
        char *value = allocate(5);
        memcpy(value, "true", 5);
        return value;
    }
    if (token_equal(source, cursor, "false")) {
        char *value = allocate(6);
        memcpy(value, "false", 6);
        return value;
    }
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
    if (token_equal(source, cursor, "if")) {
        int64_t condition_start = skip_trivia(
            source,
            token_end(source, cursor)
        );
        int64_t condition_end = expression_end(source, condition_start);
        int64_t then_open = skip_trivia(source, condition_end);
        int64_t then_start = skip_trivia(
            source,
            token_end(source, then_open)
        );
        int64_t then_end = expression_end(source, then_start);
        int64_t then_close = skip_trivia(source, then_end);
        int64_t else_keyword = skip_trivia(
            source,
            token_end(source, then_close)
        );
        int64_t else_open = skip_trivia(
            source,
            token_end(source, else_keyword)
        );
        int64_t else_start = skip_trivia(
            source,
            token_end(source, else_open)
        );
        int64_t else_end = expression_end(source, else_start);
        char *condition = emit_expression(
            source,
            condition_start,
            condition_end
        );
        char *then_value = emit_expression(
            source,
            then_start,
            then_end
        );
        char *else_value = emit_expression(
            source,
            else_start,
            else_end
        );
        Buffer output;
        buffer_init(&output);
        buffer_format(
            &output,
            "((%s) ? (%s) : (%s))",
            condition,
            then_value,
            else_value
        );
        free(condition);
        free(then_value);
        free(else_value);
        return output.data;
    }
    if (strcmp(kind, "identifier") == 0) {
        char *name = token_copy(source, cursor);
        int64_t name_end = token_end(source, cursor);
        int64_t bracket = skip_trivia(source, name_end);
        Buffer output;
        buffer_init(&output);
        if (bracket < end && token_equal(source, bracket, "[")) {
            int64_t index_start = skip_trivia(
                source,
                token_end(source, bracket)
            );
            int64_t index_end = expression_end(source, index_start);
            char *index = emit_expression(source, index_start, index_end);
            buffer_format(
                &output,
                "kofun_list_index(k_%s, %s)",
                name,
                index
            );
            free(index);
        } else {
            buffer_format(&output, "k_%s", name);
        }
        free(name);
        return output.data;
    }
    if (token_equal(source, cursor, "[")) {
        int64_t item_start = skip_trivia(
            source,
            token_end(source, cursor)
        );
        int64_t count = 0;
        Buffer items;
        buffer_init(&items);
        while (item_start < end && !token_equal(source, item_start, "]")) {
            int64_t item_end = expression_end(source, item_start);
            char *item = emit_expression(source, item_start, item_end);
            if (count > 0) buffer_append(&items, ", ");
            buffer_append(&items, item);
            free(item);
            ++count;
            int64_t separator = skip_trivia(source, item_end);
            if (token_equal(source, separator, ",")) {
                item_start = skip_trivia(
                    source,
                    token_end(source, separator)
                );
            } else {
                item_start = separator;
            }
        }
        Buffer output;
        buffer_init(&output);
        buffer_format(
            &output,
            "(kofun_list_int){INT64_C(%" PRId64
            "), (const int64_t[]){%s}}",
            count,
            items.data
        );
        free(items.data);
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
    if (token_equal(source, cursor, "!")) {
        int64_t value_start = skip_trivia(source, token_end(source, cursor));
        char *value = emit_unary(source, value_start, end);
        Buffer output;
        buffer_init(&output);
        buffer_format(&output, "(!%s)", value);
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

static char *emit_sum(const char *source, int64_t start, int64_t end) {
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

static char *emit_comparison(const char *source, int64_t start, int64_t end) {
    int64_t left_end = sum_end(source, start);
    int64_t operator_start = skip_trivia(source, left_end);
    if (operator_start >= end) return emit_sum(source, start, end);
    char *operator_text = token_copy(source, operator_start);
    int64_t right_start = skip_trivia(
        source,
        token_end(source, operator_start)
    );
    char *left = emit_sum(source, start, left_end);
    char *right = emit_sum(source, right_start, end);
    Buffer output;
    buffer_init(&output);
    buffer_format(&output, "(%s %s %s)", left, operator_text, right);
    free(left);
    free(right);
    free(operator_text);
    return output.data;
}

static char *emit_and(const char *source, int64_t start, int64_t end) {
    int64_t cursor = comparison_end(source, start);
    char *emitted = emit_comparison(source, start, cursor);
    int64_t operator_start = skip_trivia(source, cursor);
    while (operator_start < end) {
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        int64_t right_end = comparison_end(source, right_start);
        char *right = emit_comparison(source, right_start, right_end);
        Buffer combined;
        buffer_init(&combined);
        buffer_format(&combined, "(%s && %s)", emitted, right);
        free(emitted);
        free(right);
        emitted = combined.data;
        cursor = right_end;
        operator_start = skip_trivia(source, cursor);
    }
    return emitted;
}

static char *emit_expression(const char *source, int64_t start, int64_t end) {
    int64_t cursor = and_end(source, start);
    char *emitted = emit_and(source, start, cursor);
    int64_t operator_start = skip_trivia(source, cursor);
    while (operator_start < end) {
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        int64_t right_end = and_end(source, right_start);
        char *right = emit_and(source, right_start, right_end);
        Buffer combined;
        buffer_init(&combined);
        buffer_format(&combined, "(%s || %s)", emitted, right);
        free(emitted);
        free(right);
        emitted = combined.data;
        cursor = right_end;
        operator_start = skip_trivia(source, cursor);
    }
    return emitted;
}

static char *copy_text(const char *value) {
    size_t length = strlen(value);
    char *copy = allocate(length + 1);
    memcpy(copy, value, length + 1);
    return copy;
}

static char *type_error(const char *message, int64_t start) {
    Buffer error;
    buffer_init(&error);
    buffer_format(
        &error,
        "error[E2S32]: %s at byte %" PRId64,
        message,
        start
    );
    return error.data;
}

static char *bind_environment(
    const char *environment,
    const char *name,
    const char *value_type,
    bool mutable
) {
    Buffer result;
    buffer_init(&result);
    buffer_format(
        &result,
        ";%s=%s:%s;%s",
        name,
        value_type,
        mutable ? "1" : "0",
        environment
    );
    return result.data;
}

static char *binding_type(const char *environment, const char *name) {
    Buffer marker;
    buffer_init(&marker);
    buffer_format(&marker, ";%s=", name);
    const char *position = strstr(environment, marker.data);
    free(marker.data);
    if (position == NULL) return copy_text("");
    position += strlen(name) + 2;
    const char *colon = strchr(position, ':');
    if (colon == NULL) return copy_text("");
    size_t length = (size_t)(colon - position);
    char *result = allocate(length + 1);
    memcpy(result, position, length);
    result[length] = '\0';
    return result;
}

static bool binding_mutable(const char *environment, const char *name) {
    Buffer marker;
    buffer_init(&marker);
    buffer_format(&marker, ";%s=", name);
    const char *position = strstr(environment, marker.data);
    free(marker.data);
    if (position == NULL) return false;
    position += strlen(name) + 2;
    const char *colon = strchr(position, ':');
    return colon != NULL && colon[1] == '1';
}

static char *expression_type(
    const char *source,
    int64_t start,
    int64_t end,
    const char *environment
);

static char *primary_type(
    const char *source,
    int64_t start,
    int64_t end,
    const char *environment
) {
    int64_t cursor = skip_trivia(source, start);
    char *token = token_copy(source, cursor);
    const char *kind = token_kind(source, cursor);
    if (strcmp(kind, "integer") == 0) {
        free(token);
        return copy_text("Int");
    }
    if (strcmp(token, "true") == 0 || strcmp(token, "false") == 0) {
        free(token);
        return copy_text("Bool");
    }
    if (strcmp(token, "if") == 0) {
        int64_t condition_start = skip_trivia(
            source,
            token_end(source, cursor)
        );
        int64_t condition_end = expression_end(source, condition_start);
        char *condition_type = expression_type(
            source,
            condition_start,
            condition_end,
            environment
        );
        if (strncmp(condition_type, "error[", 6) == 0) {
            free(token);
            return condition_type;
        }
        if (strcmp(condition_type, "Bool") != 0) {
            free(condition_type);
            free(token);
            return type_error(
                "if expression condition requires Bool",
                condition_start
            );
        }
        free(condition_type);
        int64_t then_open = skip_trivia(source, condition_end);
        int64_t then_start = skip_trivia(
            source,
            token_end(source, then_open)
        );
        int64_t then_end = expression_end(source, then_start);
        char *then_type = expression_type(
            source,
            then_start,
            then_end,
            environment
        );
        if (strncmp(then_type, "error[", 6) == 0) {
            free(token);
            return then_type;
        }
        int64_t then_close = skip_trivia(source, then_end);
        int64_t else_keyword = skip_trivia(
            source,
            token_end(source, then_close)
        );
        if (
            else_keyword >= (int64_t)strlen(source) ||
            !token_equal(source, else_keyword, "else")
        ) {
            Buffer error;
            buffer_init(&error);
            buffer_format(
                &error,
                "error[E2S15]: value-position if requires `else` at byte %" PRId64,
                else_keyword
            );
            free(then_type);
            free(token);
            return error.data;
        }
        int64_t else_open = skip_trivia(
            source,
            token_end(source, else_keyword)
        );
        int64_t else_start = skip_trivia(
            source,
            token_end(source, else_open)
        );
        int64_t else_end = expression_end(source, else_start);
        char *else_type = expression_type(
            source,
            else_start,
            else_end,
            environment
        );
        if (strncmp(else_type, "error[", 6) == 0) {
            free(then_type);
            free(token);
            return else_type;
        }
        if (strcmp(then_type, else_type) != 0) {
            free(then_type);
            free(else_type);
            free(token);
            return type_error(
                "if expression branches must have the same type",
                else_start
            );
        }
        if (
            strcmp(then_type, "Int") != 0 &&
            strcmp(then_type, "Bool") != 0
        ) {
            free(then_type);
            free(else_type);
            free(token);
            return type_error(
                "if expression branches must produce Int or Bool",
                then_start
            );
        }
        free(else_type);
        free(token);
        return then_type;
    }
    if (strcmp(token, "[") == 0) {
        int64_t item_start = skip_trivia(
            source,
            token_end(source, cursor)
        );
        if (
            item_start < (int64_t)strlen(source) &&
            token_equal(source, item_start, "]")
        ) {
            free(token);
            return type_error(
                "List[Int] literal requires at least one element",
                cursor
            );
        }
        while (item_start < end && !token_equal(source, item_start, "]")) {
            int64_t item_end = expression_end(source, item_start);
            char *item_type = expression_type(
                source,
                item_start,
                item_end,
                environment
            );
            if (strncmp(item_type, "error[", 6) == 0) {
                free(token);
                return item_type;
            }
            if (strcmp(item_type, "Int") != 0) {
                free(item_type);
                free(token);
                return type_error(
                    "List[Int] element requires Int",
                    item_start
                );
            }
            free(item_type);
            int64_t separator = skip_trivia(source, item_end);
            if (token_equal(source, separator, ",")) {
                item_start = skip_trivia(
                    source,
                    token_end(source, separator)
                );
            } else {
                item_start = separator;
            }
        }
        free(token);
        return copy_text("List[Int]");
    }
    if (strcmp(kind, "identifier") == 0) {
        char *found = binding_type(environment, token);
        if (found[0] == '\0') {
            free(found);
            Buffer error;
            buffer_init(&error);
            buffer_format(
                &error,
                "error[E2S30]: unknown binding `%s` at byte %" PRId64,
                token,
                cursor
            );
            free(token);
            return error.data;
        }
        int64_t name_end = token_end(source, cursor);
        int64_t bracket = skip_trivia(source, name_end);
        if (bracket < end && token_equal(source, bracket, "[")) {
            if (strcmp(found, "List[Int]") != 0) {
                free(found);
                free(token);
                return type_error(
                    "index base requires List[Int]",
                    cursor
                );
            }
            int64_t index_start = skip_trivia(
                source,
                token_end(source, bracket)
            );
            int64_t index_end = expression_end(source, index_start);
            char *index_type = expression_type(
                source,
                index_start,
                index_end,
                environment
            );
            if (strncmp(index_type, "error[", 6) == 0) {
                free(found);
                free(token);
                return index_type;
            }
            if (strcmp(index_type, "Int") != 0) {
                free(index_type);
                free(found);
                free(token);
                return type_error(
                    "List index requires Int",
                    index_start
                );
            }
            free(index_type);
            free(found);
            free(token);
            return copy_text("Int");
        }
        free(token);
        return found;
    }
    if (strcmp(token, "(") == 0) {
        int64_t value_start = skip_trivia(source, token_end(source, cursor));
        int64_t close = skip_trivia(
            source,
            expression_end(source, value_start)
        );
        free(token);
        return expression_type(source, value_start, close, environment);
    }
    free(token);
    return type_error(
        "expected Int, Bool, or List[Int] expression",
        cursor
    );
}

static char *unary_type(
    const char *source,
    int64_t start,
    int64_t end,
    const char *environment
) {
    int64_t cursor = skip_trivia(source, start);
    char *operator_text = token_copy(source, cursor);
    if (
        strcmp(operator_text, "+") == 0 ||
        strcmp(operator_text, "-") == 0 ||
        strcmp(operator_text, "!") == 0
    ) {
        int64_t value_start = skip_trivia(source, token_end(source, cursor));
        char *value_type = unary_type(
            source,
            value_start,
            end,
            environment
        );
        if (strncmp(value_type, "error[", 6) == 0) {
            free(operator_text);
            return value_type;
        }
        if (
            strcmp(operator_text, "!") == 0 &&
            strcmp(value_type, "Bool") != 0
        ) {
            free(operator_text);
            free(value_type);
            return type_error("operator `!` requires Bool", cursor);
        }
        if (
            strcmp(operator_text, "!") != 0 &&
            strcmp(value_type, "Int") != 0
        ) {
            free(operator_text);
            free(value_type);
            return type_error("unary arithmetic requires Int", cursor);
        }
        free(operator_text);
        return value_type;
    }
    free(operator_text);
    return primary_type(source, cursor, end, environment);
}

static char *product_type(
    const char *source,
    int64_t start,
    int64_t end,
    const char *environment
) {
    int64_t cursor = unary_end(source, start);
    char *first = unary_type(source, start, cursor, environment);
    if (strncmp(first, "error[", 6) == 0) return first;
    if (
        strcmp(first, "Int") != 0 &&
        skip_trivia(source, cursor) < end
    ) {
        free(first);
        return type_error("multiplicative operators require Int", start);
    }
    int64_t operator_start = skip_trivia(source, cursor);
    while (operator_start < end) {
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        int64_t right_end = unary_end(source, right_start);
        char *right = unary_type(
            source,
            right_start,
            right_end,
            environment
        );
        if (strncmp(right, "error[", 6) == 0) {
            free(first);
            return right;
        }
        if (strcmp(first, "Int") != 0 || strcmp(right, "Int") != 0) {
            free(first);
            free(right);
            return type_error(
                "multiplicative operators require Int",
                operator_start
            );
        }
        free(right);
        cursor = right_end;
        operator_start = skip_trivia(source, cursor);
    }
    return first;
}

static char *sum_type(
    const char *source,
    int64_t start,
    int64_t end,
    const char *environment
) {
    int64_t cursor = product_end(source, start);
    char *first = product_type(source, start, cursor, environment);
    if (strncmp(first, "error[", 6) == 0) return first;
    if (
        strcmp(first, "Int") != 0 &&
        skip_trivia(source, cursor) < end
    ) {
        free(first);
        return type_error("additive operators require Int", start);
    }
    int64_t operator_start = skip_trivia(source, cursor);
    while (operator_start < end) {
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        int64_t right_end = product_end(source, right_start);
        char *right = product_type(
            source,
            right_start,
            right_end,
            environment
        );
        if (strncmp(right, "error[", 6) == 0) {
            free(first);
            return right;
        }
        if (strcmp(first, "Int") != 0 || strcmp(right, "Int") != 0) {
            free(first);
            free(right);
            return type_error(
                "additive operators require Int",
                operator_start
            );
        }
        free(right);
        cursor = right_end;
        operator_start = skip_trivia(source, cursor);
    }
    return first;
}

static char *comparison_type(
    const char *source,
    int64_t start,
    int64_t end,
    const char *environment
) {
    int64_t left_end = sum_end(source, start);
    char *left = sum_type(source, start, left_end, environment);
    if (strncmp(left, "error[", 6) == 0) return left;
    int64_t operator_start = skip_trivia(source, left_end);
    if (operator_start >= end) return left;
    char *operator_text = token_copy(source, operator_start);
    int64_t right_start = skip_trivia(
        source,
        token_end(source, operator_start)
    );
    char *right = sum_type(source, right_start, end, environment);
    if (strncmp(right, "error[", 6) == 0) {
        free(left);
        free(operator_text);
        return right;
    }
    if (
        strcmp(operator_text, "==") == 0 ||
        strcmp(operator_text, "!=") == 0
    ) {
        if (strcmp(left, right) != 0) {
            free(left);
            free(right);
            free(operator_text);
            return type_error(
                "equality operands must have the same type",
                operator_start
            );
        }
        if (strcmp(left, "List[Int]") == 0) {
            free(left);
            free(right);
            free(operator_text);
            return type_error(
                "List[Int] equality is not supported",
                operator_start
            );
        }
        free(left);
        free(right);
        free(operator_text);
        return copy_text("Bool");
    }
    if (strcmp(left, "Int") != 0 || strcmp(right, "Int") != 0) {
        free(left);
        free(right);
        free(operator_text);
        return type_error("ordered comparison requires Int", operator_start);
    }
    free(left);
    free(right);
    free(operator_text);
    return copy_text("Bool");
}

static char *and_type(
    const char *source,
    int64_t start,
    int64_t end,
    const char *environment
) {
    int64_t cursor = comparison_end(source, start);
    char *first = comparison_type(source, start, cursor, environment);
    if (strncmp(first, "error[", 6) == 0) return first;
    int64_t operator_start = skip_trivia(source, cursor);
    if (operator_start < end && strcmp(first, "Bool") != 0) {
        free(first);
        return type_error("operator `&&` requires Bool", operator_start);
    }
    while (operator_start < end) {
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        int64_t right_end = comparison_end(source, right_start);
        char *right = comparison_type(
            source,
            right_start,
            right_end,
            environment
        );
        if (strncmp(right, "error[", 6) == 0) {
            free(first);
            return right;
        }
        if (strcmp(right, "Bool") != 0) {
            free(first);
            free(right);
            return type_error("operator `&&` requires Bool", operator_start);
        }
        free(right);
        cursor = right_end;
        operator_start = skip_trivia(source, cursor);
    }
    return first;
}

static char *expression_type(
    const char *source,
    int64_t start,
    int64_t end,
    const char *environment
) {
    int64_t cursor = and_end(source, start);
    char *first = and_type(source, start, cursor, environment);
    if (strncmp(first, "error[", 6) == 0) return first;
    int64_t operator_start = skip_trivia(source, cursor);
    if (operator_start < end && strcmp(first, "Bool") != 0) {
        free(first);
        return type_error("operator `||` requires Bool", operator_start);
    }
    while (operator_start < end) {
        int64_t right_start = skip_trivia(
            source,
            token_end(source, operator_start)
        );
        int64_t right_end = and_end(source, right_start);
        char *right = and_type(
            source,
            right_start,
            right_end,
            environment
        );
        if (strncmp(right, "error[", 6) == 0) {
            free(first);
            return right;
        }
        if (strcmp(right, "Bool") != 0) {
            free(first);
            free(right);
            return type_error("operator `||` requires Bool", operator_start);
        }
        free(right);
        cursor = right_end;
        operator_start = skip_trivia(source, cursor);
    }
    return first;
}

static char *indentation(int64_t depth) {
    Buffer result;
    buffer_init(&result);
    for (int64_t index = 0; index < depth; ++index) {
        buffer_append(&result, "    ");
    }
    return result.data;
}

static const char *c_value_type(const char *value_type) {
    if (strcmp(value_type, "Bool") == 0) return "bool";
    if (strcmp(value_type, "List[Int]") == 0) return "kofun_list_int";
    return "int64_t";
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

static char *lower_block(
    const char *source,
    int64_t open,
    const char *environment,
    int64_t depth
) {
    int64_t length = (int64_t)strlen(source);
    Buffer emitted;
    buffer_init(&emitted);
    char *local_environment = copy_text(environment);
    int64_t cursor = skip_trivia(source, token_end(source, open));
    bool returned = false;
    char *prefix = indentation(depth);
    while (cursor < length && !token_equal(source, cursor, "}")) {
        if (returned) {
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
                return lower_error("E2S11", "expected binding name", cursor);
            }
            char *name = token_copy(source, cursor);
            cursor = skip_trivia(source, token_end(source, cursor));
            char *declared_type = copy_text("");
            if (cursor < length && token_equal(source, cursor, ":")) {
                cursor = skip_trivia(source, token_end(source, cursor));
                if (cursor < length && token_equal(source, cursor, "List")) {
                    int64_t list_start = cursor;
                    int64_t element_open = skip_trivia(
                        source,
                        token_end(source, cursor)
                    );
                    if (
                        element_open >= length ||
                        !token_equal(source, element_open, "[")
                    ) {
                        return lower_error(
                            "E2S11",
                            "Core binding type must be Int, Bool, or List[Int]",
                            list_start
                        );
                    }
                    int64_t element = skip_trivia(
                        source,
                        token_end(source, element_open)
                    );
                    if (
                        element >= length ||
                        !token_equal(source, element, "Int")
                    ) {
                        return lower_error(
                            "E2S11",
                            "Core binding type must be Int, Bool, or List[Int]",
                            list_start
                        );
                    }
                    int64_t element_close = skip_trivia(
                        source,
                        token_end(source, element)
                    );
                    if (
                        element_close >= length ||
                        !token_equal(source, element_close, "]")
                    ) {
                        return lower_error(
                            "E2S11",
                            "Core binding type must be Int, Bool, or List[Int]",
                            list_start
                        );
                    }
                    free(declared_type);
                    declared_type = copy_text("List[Int]");
                    cursor = skip_trivia(
                        source,
                        token_end(source, element_close)
                    );
                } else if (
                    cursor < length &&
                    (token_equal(source, cursor, "Int") ||
                     token_equal(source, cursor, "Bool"))
                ) {
                    free(declared_type);
                    declared_type = token_copy(source, cursor);
                    cursor = skip_trivia(source, token_end(source, cursor));
                } else {
                    return lower_error(
                        "E2S11",
                        "Core binding type must be Int, Bool, or List[Int]",
                        cursor
                    );
                }
            }
            if (cursor >= length || !token_equal(source, cursor, "=")) {
                return lower_error("E2S11", "expected `=`", cursor);
            }
            int64_t value_start = skip_trivia(source, token_end(source, cursor));
            int64_t value_end = expression_end(source, value_start);
            if (value_end < 0) {
                return lower_error("E2S12", "invalid expression", value_start);
            }
            char *value_type = expression_type(
                source,
                value_start,
                value_end,
                local_environment
            );
            if (strncmp(value_type, "error[", 6) == 0) return value_type;
            if (
                declared_type[0] != '\0' &&
                strcmp(declared_type, value_type) != 0
            ) {
                return type_error(
                    "binding annotation does not match initializer",
                    value_start
                );
            }
            if (mutable && strcmp(value_type, "List[Int]") == 0) {
                return lower_error(
                    "E2S11",
                    "mutable List[Int] bindings are not supported",
                    value_start
                );
            }
            char *value = emit_expression(source, value_start, value_end);
            buffer_format(
                &emitted,
                "%s%s k_%s = %s;\n"
                "%sif (kofun_failed) return 1;\n",
                prefix,
                c_value_type(value_type),
                name,
                value,
                prefix
            );
            char *next_environment = bind_environment(
                local_environment,
                name,
                value_type,
                mutable
            );
            free(local_environment);
            local_environment = next_environment;
            free(value);
            free(name);
            free(value_type);
            free(declared_type);
            cursor = skip_trivia(source, value_end);
        } else if (token_equal(source, cursor, "print")) {
            int64_t call_open = skip_trivia(source, token_end(source, cursor));
            if (call_open >= length || !token_equal(source, call_open, "(")) {
                return lower_error("E2S13", "expected `print(`", cursor);
            }
            int64_t value_start = skip_trivia(source, token_end(source, call_open));
            int64_t value_end = expression_end(source, value_start);
            if (value_end < 0) {
                return lower_error("E2S12", "invalid expression", value_start);
            }
            char *value_type = expression_type(
                source,
                value_start,
                value_end,
                local_environment
            );
            if (strncmp(value_type, "error[", 6) == 0) return value_type;
            if (strcmp(value_type, "List[Int]") == 0) {
                free(value_type);
                return type_error(
                    "print requires Int or Bool",
                    value_start
                );
            }
            int64_t call_close = skip_trivia(source, value_end);
            if (call_close >= length || !token_equal(source, call_close, ")")) {
                return lower_error("E2S13", "expected `)`", call_close);
            }
            char *value = emit_expression(source, value_start, value_end);
            buffer_format(
                &emitted,
                "%s{\n"
                "%s    %s kofun_value = %s;\n"
                "%s    if (kofun_failed) return 1;\n",
                prefix,
                prefix,
                c_value_type(value_type),
                value,
                prefix
            );
            if (strcmp(value_type, "Bool") == 0) {
                buffer_format(
                    &emitted,
                    "%s    printf(\"%%s\\n\", kofun_value ? \"true\" : \"false\");\n",
                    prefix
                );
            } else {
                buffer_format(
                    &emitted,
                    "%s    printf(\"%%\" PRId64 \"\\n\", kofun_value);\n",
                    prefix
                );
            }
            buffer_format(&emitted, "%s}\n", prefix);
            free(value_type);
            free(value);
            cursor = skip_trivia(source, token_end(source, call_close));
        } else if (token_equal(source, cursor, "if")) {
            int64_t condition_start = skip_trivia(
                source,
                token_end(source, cursor)
            );
            int64_t condition_end = expression_end(source, condition_start);
            if (condition_end < 0) {
                return lower_error(
                    "E2S12",
                    "invalid if condition",
                    condition_start
                );
            }
            char *condition_type = expression_type(
                source,
                condition_start,
                condition_end,
                local_environment
            );
            if (strncmp(condition_type, "error[", 6) == 0) {
                return condition_type;
            }
            if (strcmp(condition_type, "Bool") != 0) {
                return type_error("if condition requires Bool", condition_start);
            }
            int64_t then_open = skip_trivia(source, condition_end);
            if (then_open >= length || !token_equal(source, then_open, "{")) {
                return lower_error(
                    "E2S13",
                    "expected `{` after if condition",
                    then_open
                );
            }
            int64_t then_end = balanced_end(source, then_open, "{", "}");
            if (then_end < 0) {
                return lower_error("E2S03", "missing if block close", -1);
            }
            char *then_body = lower_block(
                source,
                then_open,
                local_environment,
                depth + 2
            );
            if (strncmp(then_body, "error[", 6) == 0) return then_body;
            char *condition = emit_expression(
                source,
                condition_start,
                condition_end
            );
            buffer_format(
                &emitted,
                "%s{\n"
                "%s    bool kofun_condition = %s;\n"
                "%s    if (kofun_failed) return 1;\n"
                "%s    if (kofun_condition) {\n",
                prefix,
                prefix,
                condition,
                prefix,
                prefix
            );
            buffer_append(&emitted, then_body);
            buffer_format(&emitted, "%s    }", prefix);
            cursor = skip_trivia(source, then_end);
            if (cursor < length && token_equal(source, cursor, "else")) {
                int64_t else_open = skip_trivia(
                    source,
                    token_end(source, cursor)
                );
                if (
                    else_open >= length ||
                    !token_equal(source, else_open, "{")
                ) {
                    return lower_error(
                        "E2S13",
                        "expected `{` after else",
                        else_open
                    );
                }
                int64_t else_end = balanced_end(source, else_open, "{", "}");
                if (else_end < 0) {
                    return lower_error("E2S03", "missing else block close", -1);
                }
                char *else_body = lower_block(
                    source,
                    else_open,
                    local_environment,
                    depth + 2
                );
                if (strncmp(else_body, "error[", 6) == 0) return else_body;
                buffer_append(&emitted, " else {\n");
                buffer_append(&emitted, else_body);
                buffer_format(&emitted, "%s    }\n", prefix);
                cursor = skip_trivia(source, else_end);
            } else {
                buffer_append(&emitted, "\n");
            }
            buffer_format(&emitted, "%s}\n", prefix);
        } else if (token_equal(source, cursor, "while")) {
            int64_t condition_start = skip_trivia(
                source,
                token_end(source, cursor)
            );
            int64_t condition_end = expression_end(source, condition_start);
            if (condition_end < 0) {
                return lower_error(
                    "E2S12",
                    "invalid while condition",
                    condition_start
                );
            }
            char *condition_type = expression_type(
                source,
                condition_start,
                condition_end,
                local_environment
            );
            if (strncmp(condition_type, "error[", 6) == 0) {
                return condition_type;
            }
            if (strcmp(condition_type, "Bool") != 0) {
                return type_error(
                    "while condition requires Bool",
                    condition_start
                );
            }
            int64_t loop_open = skip_trivia(source, condition_end);
            if (loop_open >= length || !token_equal(source, loop_open, "{")) {
                return lower_error(
                    "E2S13",
                    "expected `{` after while condition",
                    loop_open
                );
            }
            int64_t loop_end = balanced_end(source, loop_open, "{", "}");
            if (loop_end < 0) {
                return lower_error("E2S03", "missing while block close", -1);
            }
            char *loop_body = lower_block(
                source,
                loop_open,
                local_environment,
                depth + 2
            );
            if (strncmp(loop_body, "error[", 6) == 0) return loop_body;
            char *condition = emit_expression(
                source,
                condition_start,
                condition_end
            );
            buffer_format(
                &emitted,
                "%s{\n"
                "%s    for (;;) {\n"
                "%s        bool kofun_condition = %s;\n"
                "%s        if (kofun_failed) return 1;\n"
                "%s        if (!kofun_condition) break;\n",
                prefix,
                prefix,
                prefix,
                condition,
                prefix,
                prefix
            );
            buffer_append(&emitted, loop_body);
            buffer_format(
                &emitted,
                "%s    }\n"
                "%s}\n",
                prefix,
                prefix
            );
            cursor = skip_trivia(source, loop_end);
        } else if (token_equal(source, cursor, "return")) {
            int64_t value_start = skip_trivia(source, token_end(source, cursor));
            if (value_start < length && token_equal(source, value_start, "}")) {
                buffer_format(&emitted, "%sreturn 0;\n", prefix);
                cursor = value_start;
            } else {
                int64_t value_end = expression_end(source, value_start);
                if (value_end < 0) {
                    return lower_error(
                        "E2S12",
                        "invalid return expression",
                        value_start
                    );
                }
                char *value_type = expression_type(
                    source,
                    value_start,
                    value_end,
                    local_environment
                );
                if (strncmp(value_type, "error[", 6) == 0) return value_type;
                if (strcmp(value_type, "Int") != 0) {
                    return type_error("main return requires Int", value_start);
                }
                char *value = emit_expression(source, value_start, value_end);
                buffer_format(
                    &emitted,
                    "%s{\n"
                    "%s    int64_t kofun_result = %s;\n"
                    "%s    if (kofun_failed) return 1;\n"
                    "%s    return (int)kofun_result;\n"
                    "%s}\n",
                    prefix,
                    prefix,
                    value,
                    prefix,
                    prefix,
                    prefix
                );
                cursor = skip_trivia(source, value_end);
            }
            returned = true;
        } else if (strcmp(token_kind(source, cursor), "identifier") == 0) {
            int64_t name_start = cursor;
            char *name = token_copy(source, cursor);
            char *value_type = binding_type(local_environment, name);
            if (value_type[0] == '\0') {
                Buffer error;
                buffer_init(&error);
                buffer_format(
                    &error,
                    "error[E2S30]: unknown binding `%s` at byte %" PRId64,
                    name,
                    cursor
                );
                return error.data;
            }
            if (!binding_mutable(local_environment, name)) {
                Buffer error;
                buffer_init(&error);
                buffer_format(
                    &error,
                    "error[E2S31]: cannot assign immutable binding `%s` at byte %" PRId64,
                    name,
                    cursor
                );
                return error.data;
            }
            cursor = skip_trivia(source, token_end(source, cursor));
            if (cursor >= length || !token_equal(source, cursor, "=")) {
                return lower_error(
                    "E2S10",
                    "unsupported Core statement",
                    name_start
                );
            }
            int64_t value_start = skip_trivia(source, token_end(source, cursor));
            int64_t value_end = expression_end(source, value_start);
            if (value_end < 0) {
                return lower_error(
                    "E2S12",
                    "invalid assignment expression",
                    value_start
                );
            }
            char *assigned_type = expression_type(
                source,
                value_start,
                value_end,
                local_environment
            );
            if (strncmp(assigned_type, "error[", 6) == 0) {
                return assigned_type;
            }
            if (strcmp(assigned_type, value_type) != 0) {
                return type_error("assignment type mismatch", value_start);
            }
            char *value = emit_expression(source, value_start, value_end);
            buffer_format(
                &emitted,
                "%s{\n"
                "%s    %s kofun_assignment = %s;\n"
                "%s    if (kofun_failed) return 1;\n"
                "%s    k_%s = kofun_assignment;\n"
                "%s}\n",
                prefix,
                prefix,
                c_value_type(value_type),
                value,
                prefix,
                prefix,
                name,
                prefix
            );
            cursor = skip_trivia(source, value_end);
        } else {
            return lower_error("E2S10", "unsupported Core statement", cursor);
        }
    }
    if (cursor >= length || !token_equal(source, cursor, "}")) {
        return lower_error("E2S03", "missing function close", -1);
    }
    if (!returned && depth == 1) {
        buffer_format(&emitted, "%sreturn 0;\n", prefix);
    }
    return emitted.data;
}

static char *lower_body(const char *source, int64_t open) {
    return lower_block(source, open, "", 1);
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
        "typedef struct {\n"
        "    int64_t length;\n"
        "    const int64_t *items;\n"
        "} kofun_list_int;\n\n"
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
        "}\n"
        "static inline int64_t kofun_list_index(kofun_list_int list, int64_t index) {\n"
        "    if (index < 0 || index >= list.length) {\n"
        "        kofun_error(\"error[R023]: List index out of bounds\"); return 0;\n"
        "    }\n"
        "    return list.items[index];\n"
        "}\n\n"
        "int main(void) {\n"
        "    (void)kofun_failed;\n"
        "    (void)kofun_error;\n"
        "    (void)kofun_add;\n"
        "    (void)kofun_sub;\n"
        "    (void)kofun_mul;\n"
        "    (void)kofun_neg;\n"
        "    (void)kofun_floor_div;\n"
        "    (void)kofun_floor_mod;\n"
        "    (void)kofun_list_index;\n"
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
    if (argc != 5 && argc != 6) {
        fputs(
            "usage: kofun-stage2 INPUT.kofun OUTPUT.kofun OUTPUT.ir OUTPUT.tokens\n"
            "       kofun-stage2 --check-ownership INPUT.kofun\n"
            "       kofun-stage2 INPUT.kofun UNUSED.kofun OUTPUT.ir OUTPUT.tokens --recover\n",
            stdout
        );
        return 2;
    }
    bool recovery = argc == 6;
    if (recovery && strcmp(argv[5], "--recover") != 0) {
        fputs(
            "usage: kofun-stage2 INPUT.kofun OUTPUT.kofun OUTPUT.ir OUTPUT.tokens [--recover]\n",
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
    if (recovery) {
        write_file(argv[3], ir);
        write_file(argv[4], tokens);
        if (strncmp(ir, "error[", 6) == 0) {
            puts(ir);
            free(ir);
            free(tokens);
            free(source);
            return 1;
        }
        puts(argv[3]);
        free(ir);
        free(tokens);
        free(source);
        return 0;
    }
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
