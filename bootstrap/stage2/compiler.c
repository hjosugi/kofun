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
/*
 * Included as source rather than linked, which is the pattern the Unicode
 * module above already sets. `compiler.c` is compiled standalone at thirty
 * call sites across the gates; a link dependency would have to be added to
 * every one of them, while an include leaves all thirty unchanged.
 */
#include "decimal_v1.c"

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

typedef struct {
    uint8_t kind;
    int64_t start;
    int64_t end;
} Stage2DiagnosticAffected;

typedef struct {
    int64_t start;
    int64_t end;
    char label[64];
} Stage2DiagnosticRelated;

typedef struct {
    uint32_t remedy_id;
    int64_t start;
    int64_t end;
    char replacement[64];
} Stage2DiagnosticEdit;

enum {
    STAGE2_DIAGNOSTIC_AFFECTED_MODULE = 1,
    STAGE2_DIAGNOSTIC_AFFECTED_ERROR_SPAN = 2,
    STAGE2_DIAGNOSTIC_AFFECTED_CALL = 3,
    STAGE2_DIAGNOSTIC_AFFECTED_BINDING = 4
};

typedef struct {
    bool present;
    bool has_byte_span;
    bool truncated;
    char code[16];
    char category[32];
    char template_id[64];
    int64_t start;
    int64_t end;
    char fallback[160];
    Stage2DiagnosticAffected affected[4];
    uint16_t affected_count;
    Stage2DiagnosticRelated related[4];
    uint16_t related_count;
    uint32_t remedies[4];
    uint16_t remedy_count;
    Stage2DiagnosticEdit edits[4];
    uint16_t edit_count;
} Stage2StructuredDiagnostic;

typedef struct {
    Stage2StructuredDiagnostic diagnostic;
} Stage2AuthorityContext;

typedef struct {
    char *program_ir;
    char *parse_prefix_ir;
    char *declaration_observations;
    char *scope_hir;
    char *scope_prefix_hir;
    char *semantic_observations;
    char *diagnostic;
    uint8_t exit_class;
    bool token_span_committed;
    bool parse_committed;
    bool scope_committed;
} Stage2AuthorityResult;

/* The lexer asks for the source length on every token query. Recomputing it
 * with strlen made each query linear in the whole file, so a pass that asks
 * once per token was quadratic in file size. The source of a pass is
 * immutable for that pass's lifetime, so one memo entry keyed on its address
 * answers every query; any other pointer falls back to strlen. */
static const char *source_length_text;
static int64_t source_length_value;

static int64_t source_length(const char *source) {
    /* The terminator check rejects a hit whose buffer was reused for longer
     * content at the same address, so a stale entry cannot make a caller scan
     * past the end of the string it was handed. */
    if (source == source_length_text &&
        source[source_length_value] == '\0') {
        return source_length_value;
    }
    source_length_text = source;
    source_length_value = (int64_t)strlen(source);
    return source_length_value;
}

static void *allocate(size_t size);
static void fail(const char *message);

/*
 * The audited seed is deliberately single-threaded.  This pointer is scoped
 * by the compiler-owned authority wrappers below and is never exposed to the
 * semantic sink.  Ordinary compiler commands leave it NULL.
 */
static Stage2AuthorityContext *stage2_active_authority_context;
static char **stage2_active_parse_prefix_output;
static char **stage2_active_scope_prefix_output;
static Buffer *stage2_active_semantic_observer;
static Buffer *stage2_active_declaration_observer;

static void stage2_parse_prefix_observe(const Buffer *ir) {
    char *copy;
    if (stage2_active_parse_prefix_output == NULL || ir == NULL) return;
    copy = allocate(ir->length + 1u);
    memcpy(copy, ir->data, ir->length);
    copy[ir->length] = '\0';
    free(*stage2_active_parse_prefix_output);
    *stage2_active_parse_prefix_output = copy;
}

static void stage2_scope_prefix_observe(const Buffer *hir) {
    char *copy;
    if (stage2_active_scope_prefix_output == NULL || hir == NULL) return;
    copy = allocate(hir->length + 1u);
    memcpy(copy, hir->data, hir->length);
    copy[hir->length] = '\0';
    free(*stage2_active_scope_prefix_output);
    *stage2_active_scope_prefix_output = copy;
}

static void stage2_diagnostic_set(
    const char *code,
    int64_t start,
    int64_t end,
    bool has_byte_span,
    const char *fallback
) {
    Stage2StructuredDiagnostic *diagnostic;
    int template_length;
    int fallback_length;
    if (stage2_active_authority_context == NULL) return;
    diagnostic = &stage2_active_authority_context->diagnostic;
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->present = true;
    diagnostic->has_byte_span = has_byte_span;
    diagnostic->start = has_byte_span ? start : 0;
    diagnostic->end = has_byte_span ? end : 0;
    (void)snprintf(diagnostic->code, sizeof(diagnostic->code), "%s", code);
    (void)snprintf(
        diagnostic->category,
        sizeof(diagnostic->category),
        "%s",
        "stage2"
    );
    template_length = snprintf(
        diagnostic->template_id,
        sizeof(diagnostic->template_id),
        "stage2/%s",
        code
    );
    fallback_length = snprintf(
        diagnostic->fallback,
        sizeof(diagnostic->fallback),
        "%s",
        fallback
    );
    diagnostic->truncated =
        template_length < 0 ||
        template_length >= (int)sizeof(diagnostic->template_id) ||
        fallback_length < 0 ||
        fallback_length >= (int)sizeof(diagnostic->fallback);
    diagnostic->affected[0].kind = has_byte_span ?
        STAGE2_DIAGNOSTIC_AFFECTED_ERROR_SPAN :
        STAGE2_DIAGNOSTIC_AFFECTED_MODULE;
    diagnostic->affected[0].start = diagnostic->start;
    diagnostic->affected[0].end = diagnostic->end;
    diagnostic->affected_count = 1u;
}

static void stage2_diagnostic_affected(
    uint8_t kind,
    int64_t start,
    int64_t end
) {
    Stage2StructuredDiagnostic *diagnostic;
    if (stage2_active_authority_context == NULL) return;
    diagnostic = &stage2_active_authority_context->diagnostic;
    if (!diagnostic->present) return;
    diagnostic->affected[0].kind = kind;
    diagnostic->affected[0].start = start;
    diagnostic->affected[0].end = end;
    diagnostic->affected_count = 1u;
}

static void stage2_diagnostic_related(
    int64_t start,
    int64_t end,
    const char *label
) {
    Stage2StructuredDiagnostic *diagnostic;
    Stage2DiagnosticRelated *related;
    if (stage2_active_authority_context == NULL) return;
    diagnostic = &stage2_active_authority_context->diagnostic;
    if (!diagnostic->present ||
        diagnostic->related_count >=
            sizeof(diagnostic->related) / sizeof(diagnostic->related[0])) {
        return;
    }
    related = &diagnostic->related[diagnostic->related_count++];
    related->start = start;
    related->end = end;
    (void)snprintf(related->label, sizeof(related->label), "%s", label);
}

static void stage2_diagnostic_remedy(uint32_t remedy_id) {
    Stage2StructuredDiagnostic *diagnostic;
    if (stage2_active_authority_context == NULL) return;
    diagnostic = &stage2_active_authority_context->diagnostic;
    if (!diagnostic->present ||
        diagnostic->remedy_count >=
            sizeof(diagnostic->remedies) /
                sizeof(diagnostic->remedies[0])) {
        return;
    }
    diagnostic->remedies[diagnostic->remedy_count++] = remedy_id;
}

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

static void stage2_semantic_observe(const char *format, ...) {
    Buffer line;
    va_list arguments;
    va_list copy;
    int needed;
    if (stage2_active_semantic_observer == NULL || format == NULL) return;
    buffer_init(&line);
    va_start(arguments, format);
    va_copy(copy, arguments);
    needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(arguments);
        free(line.data);
        return;
    }
    buffer_reserve(&line, (size_t)needed);
    (void)vsnprintf(
        line.data,
        line.capacity,
        format,
        arguments
    );
    va_end(arguments);
    line.length = (size_t)needed;
    if (strstr(stage2_active_semantic_observer->data, line.data) == NULL) {
        buffer_append(stage2_active_semantic_observer, line.data);
    }
    free(line.data);
}

static void stage2_declaration_observe(const char *format, ...) {
    va_list arguments;
    if (stage2_active_declaration_observer == NULL || format == NULL) return;
    va_start(arguments, format);
    {
        va_list copy;
        int needed;
        va_copy(copy, arguments);
        needed = vsnprintf(NULL, 0, format, copy);
        va_end(copy);
        if (needed < 0) fail("stage2 seed: formatting failed");
        buffer_reserve(
            stage2_active_declaration_observer,
            (size_t)needed
        );
        (void)vsnprintf(
            stage2_active_declaration_observer->data +
                stage2_active_declaration_observer->length,
            stage2_active_declaration_observer->capacity -
                stage2_active_declaration_observer->length,
            format,
            arguments
        );
        stage2_active_declaration_observer->length += (size_t)needed;
    }
    va_end(arguments);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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

static int64_t token_end_uncached(const char *source, int64_t start) {
    int64_t length = source_length(source);
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
        /* docs/DECIMAL.md: maximal munch with the range exception. A fraction
         * needs a digit after the point, so `1..2` stays Int(1), `..`, Int(2)
         * and `1.` stays Int(1) followed by `.`. */
        if (cursor + 1 < length &&
            source[cursor] == '.' &&
            source[cursor + 1] >= '0' && source[cursor + 1] <= '9') {
            ++cursor;
            while (
                cursor < length &&
                ((source[cursor] >= '0' && source[cursor] <= '9') ||
                 source[cursor] == '_')
            ) {
                ++cursor;
            }
        }
        /* An exponent is only part of the token when digits actually follow,
         * so `1e` remains Int(1) followed by the identifier `e`. */
        if (cursor < length &&
            (source[cursor] == 'e' || source[cursor] == 'E')) {
            int64_t probe = cursor + 1;
            if (probe < length &&
                (source[probe] == '+' || source[probe] == '-')) {
                ++probe;
            }
            if (probe < length &&
                source[probe] >= '0' && source[probe] <= '9') {
                cursor = probe;
                while (
                    cursor < length &&
                    ((source[cursor] >= '0' && source[cursor] <= '9') ||
                     source[cursor] == '_')
                ) {
                    ++cursor;
                }
            }
        }
        /* `f64` is part of the numeric token, not an identifier. */
        if (cursor + 3 <= length &&
            source[cursor] == 'f' &&
            source[cursor + 1] == '6' &&
            source[cursor + 2] == '4') {
            cursor += 3;
        }
        return cursor;
    }
    if (cursor < length && pair_token(source, start)) return cursor + 1;
    return cursor;
}

/* token_end is a pure function of (source, offset) and the scope-HIR walker
 * asks for the same offsets millions of times. One table per source turns the
 * repeat queries into a load. Entries store value + 1 so the zero pages a
 * calloc arrives with mean "unknown", and only queried offsets are ever
 * touched; the table is rebuilt whenever the source or its length changes,
 * and a query outside the table falls back to the scan. */
static const char *token_end_memo_source;
static int64_t token_end_memo_length;
static int64_t *token_end_memo;

static int64_t token_end(const char *source, int64_t start) {
    int64_t length = source_length(source);
    if (source != token_end_memo_source || length != token_end_memo_length) {
        free(token_end_memo);
        token_end_memo = calloc((size_t)length + 1, sizeof(int64_t));
        if (token_end_memo == NULL) fail("stage2 seed: out of memory");
        token_end_memo_source = source;
        token_end_memo_length = length;
    }
    if (start < 0 || start > length) return token_end_uncached(source, start);
    if (token_end_memo[start] != 0) return token_end_memo[start] - 1;
    int64_t value = token_end_uncached(source, start);
    token_end_memo[start] = value + 1;
    return value;
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
            (size_t)source_length(source),
            start,
            NULL)) {
        return keyword_token(source, start) ? "keyword" : "identifier";
    }
    if (first >= '0' && first <= '9') {
        /* An unsuffixed fractional or scientific literal denotes Decimal; the
         * `f64` suffix selects binary64 Float (docs/DECIMAL.md). Neither has a
         * representation yet — the kinds exist so the token contract can be
         * gated and so a backend refuses them explicitly (#717). */
        if (end - start >= 3 &&
            source[end - 3] == 'f' &&
            source[end - 2] == '6' &&
            source[end - 1] == '4') {
            return "float";
        }
        for (int64_t scan = start; scan < end; ++scan) {
            if (source[scan] == '.' ||
                source[scan] == 'e' ||
                source[scan] == 'E') {
                return "decimal";
            }
        }
        return "integer";
    }
    return "punctuation";
}

static int64_t line_at(const char *source, int64_t target) {
    int64_t line = 1;
    for (int64_t cursor = 0; cursor < target && source[cursor] != '\0'; ++cursor) {
        if (source[cursor] == '\n') ++line;
    }
    return line;
}

/*
 * Why the malformed forms are diagnosed here rather than in `token_end`, and
 * what "malformed" means for each of them.
 *
 * `token_end` is deliberately permissive about `.`: a fraction needs a digit
 * after the point, so `1..2` stays `Int`, `..`, `Int` and `1.` stays `Int`
 * followed by the point. Tightening the scanner instead would have to decide
 * between the range operator and a fraction with one character of lookahead,
 * which is the collision `docs/DECIMAL.md` states the range exception exists
 * to avoid. So the scanner keeps producing tokens and this pass reads the
 * result, where `..` and a lone `.` are already distinct tokens.
 *
 * The rules, all measured against `docs/DECIMAL.md:97-118`:
 *
 * - a numeric token immediately followed by an identifier character is one
 *   malformed literal, not a literal beside a name. This is what makes `1e`,
 *   `1e+`, `1.0e` and `1._0` errors, and it is also why `42f64` is one token
 *   while `42 f64` is two: the suffix joins only without a gap.
 * - a numeric token immediately followed by a lone `.` is `1.`, a fraction
 *   with no digits. A following `..` is the range operator and is left alone.
 * - a lone `.` immediately followed by a digit is `.5`, a fraction with no
 *   integer part.
 * - `_` must sit between two digits, so `1_`, `1.0_` and `1_.0` are errors
 *   while `1_000.000_1` is not.
 *
 * `_1` is absent on purpose. It is a well-formed identifier under the
 * identifier grammar, not a numeric literal, and it already reports as an
 * unknown binding at its own byte. Making it lexical here would ban an
 * identifier spelling on the strength of a rule about numbers.
 */
static bool numeric_digit(char symbol) {
    return symbol >= '0' && symbol <= '9';
}

static bool numeric_underscore_error(
    const char *source,
    int64_t start,
    int64_t end
) {
    for (int64_t cursor = start; cursor < end; ++cursor) {
        if (source[cursor] != '_') continue;
        if (cursor == start || cursor + 1 >= end) return true;
        if (
            !numeric_digit(source[cursor - 1]) ||
            !numeric_digit(source[cursor + 1])
        ) {
            return true;
        }
    }
    return false;
}

static bool malformed_numeric_literal(
    const char *source,
    int64_t start,
    int64_t end
) {
    int64_t length = source_length(source);
    const char *kind = token_kind(source, start);
    if (
        strcmp(kind, "integer") == 0 ||
        strcmp(kind, "decimal") == 0 ||
        strcmp(kind, "float") == 0
    ) {
        if (numeric_underscore_error(source, start, end)) return true;
        if (end < length && identifier_start_at(source, (size_t)length, end, NULL)) {
            return true;
        }
        if (end < length && source[end] == '.') {
            return end + 1 >= length || source[end + 1] != '.';
        }
        return false;
    }
    /* A lone `.` — `..` lexes as one two-character token, so a
     * one-character `.` token here is never the range operator. */
    if (end == start + 1 && source[start] == '.') {
        return end < length && numeric_digit(source[end]);
    }
    return false;
}

static char *lex_source(const char *source) {
    Buffer tape;
    buffer_init(&tape);
    KofunUnicodeError unicode_error;
    if (!kofun_unicode_validate_source(
            (const uint8_t *)source,
            (size_t)source_length(source),
            &unicode_error)) {
        char message[1024];
        kofun_unicode_format_error(
            &unicode_error,
            getenv("KOFUN_DIAGNOSTIC_LOCALE"),
            message,
            sizeof(message)
        );
        buffer_append(&tape, message);
        stage2_diagnostic_set(
            kofun_unicode_error_code(unicode_error.status),
            (int64_t)unicode_error.byte_offset,
            (int64_t)unicode_error.byte_offset,
            unicode_error.status != KOFUN_UNICODE_OUT_OF_MEMORY,
            tape.data
        );
        return tape.data;
    }
    buffer_append(&tape, "kofun-token-tape/v1\n");
    int64_t length = source_length(source);
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
            stage2_diagnostic_set(
                "E2S01",
                cursor,
                length,
                true,
                tape.data
            );
            return tape.data;
        }
        if (malformed_numeric_literal(source, cursor, end)) {
            tape.length = 0;
            tape.data[0] = '\0';
            buffer_format(
                &tape,
                "error[E2S98]: malformed numeric literal at byte %" PRId64
                "; `.` and an exponent need digits, and `_` must sit between "
                "two digits",
                cursor
            );
            stage2_diagnostic_set(
                "E2S98",
                cursor,
                end,
                true,
                tape.data
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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

/*
 * The executable constructor pattern is exactly `C(name)` or `C(_)`: one
 * parenthesised payload sub-pattern that is a single token.  A nested payload
 * such as `Ok(Present(x))` stays parsed-but-not-executable here, and whether
 * the constructor belongs to the scrutinee's enum with a matching arity is
 * decided later, where the enum type is known and the diagnostic can name it.
 */
static bool executable_constructor_pattern(const char *source, int64_t arm) {
    int64_t length = source_length(source);
    if (arm >= length || strcmp(token_kind(source, arm), "identifier") != 0) {
        return false;
    }
    int64_t open = skip_trivia(source, token_end(source, arm));
    if (open >= length || !token_equal(source, open, "(")) return false;
    int64_t field = skip_trivia(source, token_end(source, open));
    if (
        field >= length ||
        strcmp(token_kind(source, field), "identifier") != 0
    ) {
        return false;
    }
    int64_t close = skip_trivia(source, token_end(source, field));
    return close < length && token_equal(source, close, ")");
}

static char *validate_executable_patterns(const char *source) {
    int64_t length = source_length(source);
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
                        (summary.kind == PATTERN_CONSTRUCTOR &&
                         executable_constructor_pattern(source, arm)) ||
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
                        stage2_diagnostic_set(
                            "E2S24",
                            arm,
                            arm,
                            true,
                            error.data
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
    stage2_diagnostic_set(
        "E2S58",
        best_start,
        best_end,
        true,
        error.data
    );
    return error.data;
}

static int64_t balanced_end(
    const char *source,
    int64_t start,
    const char *open,
    const char *close
) {
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
        stage2_diagnostic_set(
            "E2S34",
            start,
            token_end(source, start),
            true,
            error.data
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
            stage2_diagnostic_set(
                "E2S33",
                start,
                token_end(source, start),
                true,
                error.data
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
        stage2_diagnostic_set(
            "E2S33",
            start,
            token_end(source, next),
            true,
            error.data
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
        stage2_diagnostic_set(
            "E2S34",
            start,
            form_end,
            true,
            error.data
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
    stage2_diagnostic_set(
        "E2S33",
        start,
        token_end(source, start),
        true,
        error.data
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
                stage2_diagnostic_set(
                    alias ? "E2S34" : "E2S33",
                    cursor,
                    token_end(source, cursor),
                    true,
                    error.data
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
    /* Callers advance with function_end's result, which is -1 for a position
     * that is not a function declaration. Restarting the walk from before the
     * buffer re-emitted every record with fresh identifiers forever, so a
     * rejected position ends the walk instead. */
    if (start < 0) return length;
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
    int64_t length = source_length(source);
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

/*
 * A constructor may carry one parenthesised payload field.  Every walker below
 * steps over that field with this helper: stopping at the `(` instead would
 * truncate the constructor set of any enum whose payload-carrying constructor
 * is not written last, which silently changes coverage rather than failing.
 */
static int64_t enum_constructor_token_end(
    const char *source,
    int64_t constructor
) {
    int64_t length = source_length(source);
    int64_t after = token_end(source, constructor);
    int64_t open = skip_trivia(source, after);
    if (open >= length || !token_equal(source, open, "(")) return after;
    int64_t close = balanced_end(source, open, "(", ")");
    return close < 0 ? after : close;
}

/*
 * The payload field this Core slice lowers is exactly `name: Int`.  The ADT
 * frontend already bounds a constructor to one field with `E2S41`; the extra
 * `Int` requirement here is what lets a constructor value be a tag and one
 * `int64_t`, so a wider field type must fail rather than lower.
 */
static bool enum_payload_field_supported(const char *source, int64_t open) {
    int64_t length = source_length(source);
    int64_t field = skip_trivia(source, token_end(source, open));
    if (
        field >= length ||
        strcmp(token_kind(source, field), "identifier") != 0
    ) {
        return false;
    }
    int64_t colon = skip_trivia(source, token_end(source, field));
    if (colon >= length || !token_equal(source, colon, ":")) return false;
    int64_t type_cursor = skip_trivia(source, token_end(source, colon));
    if (type_cursor >= length || !token_equal(source, type_cursor, "Int")) {
        return false;
    }
    int64_t close = skip_trivia(source, token_end(source, type_cursor));
    return close < length && token_equal(source, close, ")");
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
        pipe = skip_trivia(
            source,
            enum_constructor_token_end(source, constructor)
        );
    }
    return count;
}

/*
 * Payload arity of one named constructor: `0` payload-free, `1` one supported
 * `Int` field, `-1` when the constructor does not belong to the enum, and `-2`
 * when it declares a payload this slice cannot lower.
 */
static int64_t enum_constructor_payload_arity(
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
    while (pipe < end && token_equal(source, pipe, "|")) {
        int64_t constructor = skip_trivia(source, token_end(source, pipe));
        if (token_equal(source, constructor, wanted)) {
            int64_t open = skip_trivia(
                source,
                token_end(source, constructor)
            );
            if (open >= end || !token_equal(source, open, "(")) return 0;
            return enum_payload_field_supported(source, open) ? 1 : -2;
        }
        pipe = skip_trivia(
            source,
            enum_constructor_token_end(source, constructor)
        );
    }
    return -1;
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
        pipe = skip_trivia(
            source,
            enum_constructor_token_end(source, constructor)
        );
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
        pipe = skip_trivia(
            source,
            enum_constructor_token_end(source, constructor)
        );
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
        pipe = skip_trivia(
            source,
            enum_constructor_token_end(source, constructor)
        );
    }
    return missing.data;
}

static bool reserved_type_name(const char *name) {
    return strcmp(name, "Int") == 0 || strcmp(name, "Bool") == 0 ||
           strcmp(name, "Float") == 0 || strcmp(name, "Unit") == 0 ||
           strcmp(name, "Text") == 0 || strcmp(name, "List") == 0 ||
           strcmp(name, "_") == 0;
}

static char *function_return_type(const char *source, const char *wanted);
static char *function_return_type_at(
    const char *source,
    int64_t function_start
);
static char *function_parameter_type(
    const char *source,
    const char *wanted,
    int64_t index
);

static char *parse_program(const char *source) {
    Buffer ir;
    Buffer declared_types;
    Buffer declared_constructors;
    buffer_init(&ir);
    buffer_init(&declared_types);
    buffer_init(&declared_constructors);
    buffer_append(&declared_types, "|");
    buffer_append(&declared_constructors, "|");
    int64_t length = source_length(source);
    buffer_format(&ir, "kofun-stage2-ir/v1\nsource-bytes|%" PRId64 "\n", length);
    stage2_parse_prefix_observe(&ir);
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
                stage2_diagnostic_set(
                    "E2S31",
                    cursor,
                    token_end(source, cursor),
                    true,
                    ir.data
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
                stage2_diagnostic_set(
                    "E2S31",
                    cursor,
                    token_end(source, cursor),
                    true,
                    ir.data
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
                stage2_diagnostic_set(
                    "E2S31",
                    cursor,
                    token_end(source, cursor),
                    true,
                    error.data
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
                stage2_diagnostic_set(
                    "E2S31",
                    cursor,
                    token_end(source, cursor),
                    true,
                    ir.data
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
                stage2_diagnostic_set(
                    "E2S31",
                    cursor,
                    token_end(source, cursor),
                    true,
                    error.data
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
                stage2_diagnostic_set(
                    "E2S31",
                    cursor,
                    token_end(source, cursor),
                    true,
                    error.data
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
                stage2_diagnostic_set(
                    "E2S31",
                    cursor,
                    token_end(source, cursor),
                    true,
                    error.data
                );
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
                    stage2_diagnostic_set(
                        "E2S31",
                        constructor,
                        token_end(source, constructor),
                        true,
                        error.data
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
                    stage2_diagnostic_set(
                        "E2S31",
                        constructor,
                        token_end(source, constructor),
                        true,
                        error.data
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
                    stage2_diagnostic_set(
                        "E2S31",
                        constructor,
                        token_end(source, constructor),
                        true,
                        error.data
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
                pipe = skip_trivia(
                    source,
                    enum_constructor_token_end(source, constructor)
                );
            }
            stage2_parse_prefix_observe(&ir);
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
            stage2_diagnostic_set(
                "E2S02",
                cursor,
                token_end(source, cursor),
                true,
                ir.data
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
                stage2_diagnostic_set(
                    "E2S03",
                    function_start,
                    function_start,
                    true,
                    ir.data
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
            if (stage2_active_declaration_observer != NULL) {
                char *return_type = function_return_type_at(
                    source,
                    function_start
                );
                stage2_declaration_observe(
                    "function|%s|%" PRId64 "|%" PRId64 "|%s\n",
                    name,
                    function_start,
                    end,
                    return_type
                );
                free(return_type);
            }
            stage2_parse_prefix_observe(&ir);
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
        stage2_diagnostic_set("E2S04", 0, 0, false, ir.data);
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
    int64_t length = source_length(source);
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

/*
 * A bare constructor-shaped name must keep the historical E2S32 unknown-name
 * diagnostic. General patterns distinguish fresh value bindings from
 * constructor names lexically: an ASCII-uppercase head is constructor-shaped,
 * while a declared lowercase constructor is still recognized by its symbol.
 */
static bool enum_binding_catchall_name(const char *name) {
    return name[0] != '\0' && !(name[0] >= 'A' && name[0] <= 'Z');
}

static char *enum_declaration_names(
    const char *source,
    bool constructors
) {
    int64_t length = source_length(source);
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
                        enum_constructor_token_end(source, constructor)
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
    int64_t length = source_length(source);
    int64_t function_cursor = next_function_start(source, 0);
    int64_t recognized_loops = 0;
    while (function_cursor < length) {
        int64_t parameters_open = parameter_open(source, function_cursor);
        if (parameters_open < 0) {
            char *error = owned_text("error[E2S03]: malformed function");
            stage2_diagnostic_set("E2S03", 0, 0, false, error);
            return error;
        }
        int64_t parameters_end = balanced_end(
            source,
            parameters_open,
            "(",
            ")"
        );
        if (parameters_end < 0) {
            char *error = owned_text("error[E2S03]: malformed parameters");
            stage2_diagnostic_set("E2S03", 0, 0, false, error);
            return error;
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
                        char *error = owned_text(
                            "error[E2S21]: ownership slice supports one "
                            "borrowed List parameter per function"
                        );
                        free(element_type);
                        free(borrowed_name);
                        stage2_diagnostic_set("E2S21", 0, 0, false, error);
                        return error;
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
            char *error = owned_text(
                "error[E2S03]: malformed function body"
            );
            free(element_type);
            free(borrowed_name);
            stage2_diagnostic_set("E2S03", 0, 0, false, error);
            return error;
        }
        int64_t body_open = skip_trivia(source, parameters_end);
        while (
            body_open < function_end_cursor &&
            !token_equal(source, body_open, "{")
        ) {
            body_open = skip_trivia(source, token_end(source, body_open));
        }
        if (body_open >= function_end_cursor) {
            char *error = owned_text(
                "error[E2S03]: malformed function body"
            );
            free(element_type);
            free(borrowed_name);
            stage2_diagnostic_set("E2S03", 0, 0, false, error);
            return error;
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
                        char *error = owned_text(
                            "error[E2S03]: malformed for body"
                        );
                        free(element_type);
                        free(borrowed_name);
                        stage2_diagnostic_set(
                            "E2S03",
                            0,
                            0,
                            false,
                            error
                        );
                        return error;
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
                            stage2_diagnostic_set(
                                "E007",
                                move_at,
                                token_end(source, move_at),
                                true,
                                error.data
                            );
                            stage2_diagnostic_remedy(1u);
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
        char *error = owned_text(
            "error[E2S20]: Stage 2 ownership slice requires "
            "`for element in read_list`"
        );
        stage2_diagnostic_set("E2S20", 0, 0, false, error);
        return error;
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
static char *emit_primary(
    const char *source,
    const char *hir,
    int64_t start,
    int64_t end
);
static char *lower_error(
    const char *code,
    const char *message,
    int64_t cursor
);
static char *hir_use_binding_id(const char *hir, int64_t use_start);
static char *hir_definition_id_at(
    const char *hir,
    int64_t declaration_start
);
static int64_t lambda_initializer_open(
    const char *source,
    int64_t value_start
);
static int64_t lambda_binding_open(
    const char *source,
    const char *hir,
    const char *binding_id
);
static char *lambda_captures(
    const char *source,
    const char *hir,
    int64_t lambda_open
);
static void append_captures(
    Buffer *output,
    const char *captures,
    int64_t written,
    const char *declaration
);
static int64_t lambda_call_arity(
    const char *source,
    const char *hir,
    int64_t use_start
);
static char *hir_binding_field(
    const char *hir,
    const char *binding_id,
    int field
);
static int64_t lambda_parameters_end(
    const char *source,
    int64_t previous,
    int64_t open
);
static int64_t callable_parameter_type_start(
    const char *source,
    const char *hir,
    const char *binding_id
);
static int64_t callable_call_arity(
    const char *source,
    const char *hir,
    int64_t use_start
);
static int64_t function_arity(const char *source, const char *wanted);
static char *enum_constructor_owner(const char *source, const char *name);
static char *source_slice(const char *source, int64_t start, int64_t end);

/*
 * The byte after a callable type beginning at `start`, or -1 when the tokens
 * there are not one.
 *
 * #552 settled the notation and this Core implements exactly it: `A -> R` for
 * one argument, `(A, B) -> R` for a fixed arity of two or more, and `() -> R`
 * for none. There is no implicit currying, so `A -> B -> R` is not a two-
 * argument callable; it is a one-argument callable returning another, which
 * this Core has no value to represent and therefore does not accept.
 *
 * `Int` is the only type in every domain and result position, because `Int` is
 * the whole type vocabulary a Core parameter has. A callable naming any other
 * type is not a callable type here, so the caller reports it as an ordinary
 * unsupported parameter type rather than as a malformed callable.
 */
static int64_t callable_type_end(const char *source, int64_t start) {
    int64_t length = source_length(source);
    int64_t cursor = skip_trivia(source, start);
    if (cursor >= length) return -1;
    int64_t after_domain = -1;
    if (token_equal(source, cursor, "(")) {
        int64_t close = balanced_end(source, cursor, "(", ")");
        if (close < 0) return -1;
        int64_t element = skip_trivia(source, token_end(source, cursor));
        while (element < close && !token_equal(source, element, ")")) {
            if (!token_equal(source, element, "Int")) return -1;
            int64_t separator = skip_trivia(source, token_end(source, element));
            if (separator < close && token_equal(source, separator, ",")) {
                element = skip_trivia(source, token_end(source, separator));
            } else {
                element = separator;
            }
        }
        after_domain = close;
    } else if (token_equal(source, cursor, "Int")) {
        after_domain = token_end(source, cursor);
    } else {
        return -1;
    }
    int64_t arrow = skip_trivia(source, after_domain);
    if (arrow >= length || !token_equal(source, arrow, "->")) return -1;
    int64_t result = skip_trivia(source, token_end(source, arrow));
    if (result >= length || !token_equal(source, result, "Int")) return -1;
    return token_end(source, result);
}

/*
 * The argument count of the callable type at `start`, or -1 when there is not
 * one there. The bare domain `A -> R` is one argument by construction; a
 * parenthesised domain counts its elements, so `() -> R` is zero.
 */
static int64_t callable_type_arity(const char *source, int64_t start) {
    if (callable_type_end(source, start) < 0) return -1;
    int64_t cursor = skip_trivia(source, start);
    if (!token_equal(source, cursor, "(")) return 1;
    int64_t close = balanced_end(source, cursor, "(", ")");
    if (close < 0) return -1;
    int64_t count = 0;
    int64_t element = skip_trivia(source, token_end(source, cursor));
    while (element < close && !token_equal(source, element, ")")) {
        ++count;
        int64_t separator = skip_trivia(source, token_end(source, element));
        if (separator < close && token_equal(source, separator, ",")) {
            element = skip_trivia(source, token_end(source, separator));
        } else {
            element = separator;
        }
    }
    return count;
}

/*
 * `int64_t (*NAME)(int64_t, ...)` for the callable type at `start`. Every
 * value this Core lowers is an `int64_t`, so a callable is a plain C function
 * pointer and needs no environment; that is exactly why a capturing lambda
 * cannot be one, and `validate_argument_lambda_captures` refuses those.
 */
static char *callable_c_declarator(
    const char *source,
    int64_t start,
    const char *name
) {
    int64_t arity = callable_type_arity(source, start);
    if (arity < 0) return owned_text("");
    Buffer output;
    buffer_init(&output);
    buffer_format(&output, "int64_t (*%s)(", name);
    if (arity == 0) {
        buffer_append(&output, "void");
    } else {
        for (int64_t written = 0; written < arity; ++written) {
            if (written > 0) buffer_append(&output, ", ");
            buffer_append(&output, "int64_t");
        }
    }
    buffer_append(&output, ")");
    return output.data;
}

/*
 * The arrow spelling of the removed `Fn[...]` notation whose `[` is at
 * `open`, or "" when the brackets do not close.
 *
 * #552 settled one spelling for both positions, and every normative document
 * requires a *targeted* rewrite rather than a bare rejection. The last
 * bracket element is the result and the rest are the domain, so `Fn[R]` is
 * `() -> R`, `Fn[A, R]` is `A -> R`, and a historical multi-argument
 * `Fn[A, B, R]` is `(A, B) -> R`. Nested brackets are skipped whole, which is
 * what keeps `Fn[Int, List[Int]]` from splitting inside `List[Int]`.
 */
static char *removed_callable_rewrite(const char *source, int64_t open) {
    int64_t close = balanced_end(source, open, "[", "]");
    if (close < 0) return owned_text("");
    Buffer domain;
    buffer_init(&domain);
    char *result = owned_text("");
    int64_t count = 0;
    int64_t element = skip_trivia(source, token_end(source, open));
    while (element < close && !token_equal(source, element, "]")) {
        int64_t element_start = element;
        int64_t element_end = element;
        while (
            element < close &&
            !token_equal(source, element, ",") &&
            !token_equal(source, element, "]")
        ) {
            if (token_equal(source, element, "[")) {
                int64_t nested = balanced_end(source, element, "[", "]");
                if (nested < 0) {
                    free(domain.data);
                    free(result);
                    return owned_text("");
                }
                element_end = nested;
            } else {
                element_end = token_end(source, element);
            }
            element = skip_trivia(source, element_end);
        }
        if (count > 0) {
            if (domain.length > 0) buffer_append(&domain, ", ");
            buffer_append(&domain, result);
        }
        free(result);
        result = source_slice(source, element_start, element_end);
        ++count;
        if (element < close && token_equal(source, element, ",")) {
            element = skip_trivia(source, token_end(source, element));
        }
    }
    Buffer output;
    buffer_init(&output);
    if (count == 0) {
        free(domain.data);
        free(result);
        free(output.data);
        return owned_text("");
    }
    if (count == 1) {
        buffer_format(&output, "() -> %s", result);
    } else if (count == 2) {
        buffer_format(&output, "%s -> %s", domain.data, result);
    } else {
        buffer_format(&output, "(%s) -> %s", domain.data, result);
    }
    free(domain.data);
    free(result);
    return output.data;
}

/*
 * `Fn[...]` is the callable notation #552 removed. `Fn` stays an ordinary
 * identifier, so only `Fn` immediately followed by `[` is the removed type.
 */
static char *validate_removed_callable_notation(const char *source) {
    int64_t length = source_length(source);
    int64_t cursor = skip_trivia(source, 0);
    while (cursor < length) {
        if (
            token_equal(source, cursor, "Fn") &&
            strcmp(token_kind(source, cursor), "identifier") == 0
        ) {
            int64_t bracket = skip_trivia(source, token_end(source, cursor));
            if (bracket < length && token_equal(source, bracket, "[")) {
                char *rewrite = removed_callable_rewrite(source, bracket);
                if (rewrite[0] != '\0') {
                    Buffer message;
                    buffer_init(&message);
                    buffer_format(
                        &message,
                        "error[E2S97]: `Fn[...]` is not a callable type "
                        "at byte %" PRId64 "; write `%s`",
                        cursor,
                        rewrite
                    );
                    free(rewrite);
                    stage2_diagnostic_set(
                        "E2S97",
                        cursor,
                        token_end(source, cursor),
                        true,
                        message.data
                    );
                    return message.data;
                }
                free(rewrite);
            }
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return owned_text("ok");
}

/*
 * Whether `target` is a whole argument of a call.
 *
 * A bare function name is a value only here. Restricting it to argument
 * position is what keeps `double + 1` an ordinary unknown-binding error: a
 * function name that reached the Int expression grammar would lower to a
 * function address inside integer arithmetic and surface as a C type error
 * instead of a Kofun diagnostic.
 *
 * Two conditions, and both are needed. The token before `target` opens or
 * continues an argument list — a `(` that itself follows an identifier, so a
 * parenthesised group does not qualify, or a `,`. And the token after
 * `target` closes or continues it, so `double` in `print(double + 1)` is
 * excluded while `double` in `apply(double, 21)` is not.
 */
static bool call_argument_position(const char *source, int64_t target) {
    int64_t length = source_length(source);
    int64_t cursor = skip_trivia(source, 0);
    int64_t previous = -1;
    int64_t before_previous = -1;
    while (cursor < target && cursor < length) {
        before_previous = previous;
        previous = cursor;
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    if (cursor != target || previous < 0) return false;
    int64_t after = skip_trivia(source, token_end(source, target));
    if (after >= length) return false;
    if (!token_equal(source, after, ",") && !token_equal(source, after, ")")) {
        return false;
    }
    if (token_equal(source, previous, "(")) {
        return before_previous >= 0 &&
               strcmp(token_kind(source, before_previous), "identifier") == 0;
    }
    return token_equal(source, previous, ",");
}

/*
 * The byte after an argument, which is either an ordinary bounded expression
 * or an arrow lambda passed directly.
 *
 * Only argument position needs this. An argument's preceding token is `(` or
 * `,`, never an identifier, so the constructor-pattern ambiguity that
 * `lambda_parameters_end` guards with its `previous` parameter cannot arise
 * here and -1 is the correct `previous` to pass.
 */
static int64_t argument_end(const char *source, int64_t start) {
    int64_t lambda_end = lambda_parameters_end(source, -1, start);
    if (lambda_end >= 0) return lambda_end;
    return expression_end(source, start);
}

/*
 * Expected type of the call argument beginning exactly at `target`.
 *
 * The older `call_argument_position` predicate is intentionally cheap and
 * handles single-token function values.  Enum constructors are calls
 * themselves (`Ready(8)`), so their first token is not followed by `,` or `)`.
 * This bounded walk finds the enclosing named call and returns its declared
 * parameter type without confusing the constructor's own parentheses for the
 * outer call.
 */
static char *call_argument_expected_type(
    const char *source,
    int64_t target
) {
    int64_t length = source_length(source);
    int64_t cursor = skip_trivia(source, 0);
    while (cursor < target && cursor < length) {
        if (strcmp(token_kind(source, cursor), "identifier") == 0) {
            int64_t open = skip_trivia(
                source,
                token_end(source, cursor)
            );
            if (open < target && token_equal(source, open, "(")) {
                int64_t close = balanced_end(source, open, "(", ")");
                if (close > target) {
                    int64_t argument = skip_trivia(
                        source,
                        token_end(source, open)
                    );
                    int64_t index = 0;
                    while (
                        argument < close &&
                        !token_equal(source, argument, ")")
                    ) {
                        if (argument == target) {
                            char *callee = token_copy(source, cursor);
                            char *type = function_parameter_type(
                                source,
                                callee,
                                index
                            );
                            free(callee);
                            return type;
                        }
                        int64_t end = argument_end(source, argument);
                        if (end < 0) break;
                        int64_t separator = skip_trivia(source, end);
                        if (
                            separator < close &&
                            token_equal(source, separator, ",")
                        ) {
                            argument = skip_trivia(
                                source,
                                token_end(source, separator)
                            );
                            ++index;
                        } else {
                            break;
                        }
                    }
                }
            }
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return owned_text("");
}

/*
 * The lifted name of an arrow lambda written directly in argument position.
 *
 * A `let`-bound lambda is keyed by its binding id, which an anonymous argument
 * does not have. Its keying token's byte offset is the identity that is
 * already unique and already stable across the two walks that must agree —
 * the one that emits the definition and the one that emits the reference.
 */
static char *argument_lambda_name(int64_t open) {
    Buffer output;
    buffer_init(&output);
    buffer_format(&output, "kofun_lambda_at%" PRId64, open);
    return output.data;
}

static int64_t primary_end(const char *source, int64_t start) {
    int64_t length = source_length(source);
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
            int64_t bound = argument_end(source, argument);
            if (bound < 0) return -1;
            int64_t separator = skip_trivia(source, bound);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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

/*
 * Lower one value of the bounded concrete-enum representation.
 *
 * The representation is intentionally uniform for every concrete enum in
 * this slice: declaration-order tag plus one Int payload slot.  Static typing
 * keeps values of different enum declarations from crossing a boundary, while
 * the common internal C shape lets ordinary functions pass and return them
 * without publishing a per-type ABI.
 */
static char *emit_enum_value(
    const char *source,
    const char *hir,
    int64_t start,
    int64_t end,
    const char *enum_type
) {
    int64_t length = source_length(source);
    int64_t cursor = skip_trivia(source, start);
    if (
        cursor >= length ||
        strcmp(token_kind(source, cursor), "identifier") != 0
    ) {
        return lower_error(
            "E2S32",
            "concrete enum value must be a constructor, binding, or call",
            cursor
        );
    }
    char *name = token_copy(source, cursor);
    int64_t open = skip_trivia(source, token_end(source, cursor));
    if (open >= end || !token_equal(source, open, "(")) {
        char *binding_id = hir_use_binding_id(hir, cursor);
        char *binding_type = hir_binding_field(hir, binding_id, 5);
        if (
            binding_id[0] == '\0' ||
            strcmp(binding_type, enum_type) != 0
        ) {
            free(binding_type);
            free(binding_id);
            free(name);
            return lower_error(
                "E2S32",
                "concrete enum binding has the wrong type",
                cursor
            );
        }
        Buffer output;
        buffer_init(&output);
        buffer_format(&output, "k_b%s", binding_id);
        free(binding_type);
        free(binding_id);
        free(name);
        return output.data;
    }

    char *constructor_owner = enum_constructor_owner(source, name);
    if (constructor_owner[0] != '\0') {
        if (strcmp(constructor_owner, enum_type) != 0) {
            free(constructor_owner);
            free(name);
            return lower_error(
                "E2S32",
                "constructor belongs to a different concrete enum",
                cursor
            );
        }
        int64_t tag = enum_constructor_index(source, enum_type, name);
        int64_t arity = enum_constructor_payload_arity(
            source,
            enum_type,
            name
        );
        if (arity < 0) {
            free(constructor_owner);
            free(name);
            return lower_error(
                "E2S32",
                "constructor payload is outside the one-Int slice",
                cursor
            );
        }
        int64_t payload_start = skip_trivia(
            source,
            token_end(source, open)
        );
        bool empty =
            payload_start < length &&
            token_equal(source, payload_start, ")");
        if ((arity == 0) != empty) {
            free(constructor_owner);
            free(name);
            return lower_error(
                "E2S32",
                arity == 1 ?
                    "constructor takes one Int payload" :
                    "constructor takes no payload",
                cursor
            );
        }
        char *payload = owned_text("INT64_C(0)");
        if (arity == 1) {
            int64_t payload_end = expression_end(source, payload_start);
            int64_t close = payload_end < 0 ?
                -1 :
                skip_trivia(source, payload_end);
            if (
                payload_end < 0 ||
                close >= length ||
                !token_equal(source, close, ")")
            ) {
                free(payload);
                free(constructor_owner);
                free(name);
                return lower_error(
                    "E2S32",
                    "concrete enum payload must be one Int expression",
                    payload_start
                );
            }
            free(payload);
            payload = emit_expression(
                source,
                hir,
                payload_start,
                payload_end
            );
        }
        Buffer output;
        buffer_init(&output);
        buffer_format(
            &output,
            "((KofunEnumValue){INT64_C(%" PRId64 "), %s})",
            tag,
            payload
        );
        free(payload);
        free(constructor_owner);
        free(name);
        return output.data;
    }
    free(constructor_owner);

    char *return_type = function_return_type(source, name);
    if (strcmp(return_type, enum_type) == 0) {
        char *output = emit_primary(source, hir, cursor, end);
        free(return_type);
        free(name);
        return output;
    }
    free(return_type);
    free(name);
    return lower_error(
        "E2S32",
        "call does not return the expected concrete enum",
        cursor
    );
}

/*
 * One argument's C expression.
 *
 * The three function-value forms are lowered here rather than in
 * `emit_primary`, because only argument position accepts them and only this
 * caller knows it is in argument position. Everything else lowers through the
 * ordinary expression emitter unchanged.
 */
static char *emit_argument(
    const char *source,
    const char *hir,
    int64_t start,
    int64_t end,
    const char *callee,
    int64_t argument_index
) {
    int64_t cursor = skip_trivia(source, start);
    char *expected_type = function_parameter_type(
        source,
        callee,
        argument_index
    );
    if (enum_constructor_count(source, expected_type) >= 0) {
        char *value = emit_enum_value(
            source,
            hir,
            cursor,
            end,
            expected_type
        );
        free(expected_type);
        return value;
    }
    free(expected_type);
    /* An arrow lambda argument is the address of the function it was lifted
     * to. */
    if (lambda_parameters_end(source, -1, cursor) >= 0) {
        return argument_lambda_name(cursor);
    }
    /* `call_argument_position` scans from the start of the source, so it is
     * tested last in each arm: the cheap HIR lookup rejects the ordinary
     * identifier argument first, and the scan then runs only for the two rare
     * shapes that can actually be function values. */
    if (strcmp(token_kind(source, cursor), "identifier") == 0) {
        char *value_binding = hir_use_binding_id(hir, cursor);
        if (value_binding[0] != '\0') {
            /* A lambda binding used as a value is the address of its lifted
             * function: lifting never declares a C variable for the binding
             * itself, so `k_b<id>` would name nothing. */
            if (
                lambda_binding_open(source, hir, value_binding) >= 0 &&
                call_argument_position(source, cursor)
            ) {
                Buffer output;
                buffer_init(&output);
                buffer_format(&output, "kofun_lambda_%s", value_binding);
                free(value_binding);
                return output.data;
            }
        } else {
            /* A bare name the scope HIR left unresolved that is a declared
             * Core function is that function used as a value. Its lowered
             * form is already an ordinary C function, so its address is the
             * callable. */
            char *name = token_copy(source, cursor);
            char *owner = enum_constructor_owner(source, name);
            bool constructor = owner[0] != '\0';
            free(owner);
            if (
                !constructor &&
                function_arity(source, name) >= 0 &&
                call_argument_position(source, cursor)
            ) {
                char *c_name = c_identifier_name(name);
                Buffer output;
                buffer_init(&output);
                buffer_format(&output, "kofun_fn_%s", c_name);
                free(c_name);
                free(name);
                free(value_binding);
                return output.data;
            }
            free(name);
        }
        free(value_binding);
    }
    return emit_expression(source, hir, start, end);
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
        /* A callee the scope HIR resolved to a binding is either a lifted
         * lambda or a callable-typed parameter; anything else is a top-level
         * function looked up by name. */
        char *callee_binding = hir_use_binding_id(hir, cursor);
        int64_t lambda_open =
            callee_binding[0] == '\0'
                ? -1
                : lambda_binding_open(source, hir, callee_binding);
        bool indirect =
            callee_binding[0] != '\0' &&
            callable_parameter_type_start(source, hir, callee_binding) >= 0;
        if (lambda_open >= 0) {
            buffer_format(&output, "kofun_lambda_%s(", callee_binding);
        } else if (indirect) {
            buffer_format(&output, "k_b%s(", callee_binding);
        } else {
            char *c_name = c_identifier_name(name);
            buffer_format(&output, "kofun_fn_%s(", c_name);
            free(c_name);
        }
        int64_t argument = skip_trivia(source, token_end(source, open));
        int64_t arguments = 0;
        while (argument < end && !token_equal(source, argument, ")")) {
            int64_t bound = argument_end(source, argument);
            char *value = emit_argument(
                source,
                hir,
                argument,
                bound,
                name,
                arguments
            );
            if (arguments > 0) buffer_append(&output, ", ");
            buffer_append(&output, value);
            free(value);
            ++arguments;
            int64_t separator = skip_trivia(source, bound);
            if (separator < end && token_equal(source, separator, ",")) {
                argument = skip_trivia(source, token_end(source, separator));
            } else {
                argument = separator;
            }
        }
        if (lambda_open >= 0) {
            char *captures = lambda_captures(source, hir, lambda_open);
            append_captures(&output, captures, arguments, "");
            free(captures);
        }
        buffer_append(&output, ")");
        free(callee_binding);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
    int64_t cursor = skip_trivia(source, token_end(source, open));
    if (cursor < length && token_equal(source, cursor, ")")) return 0;
    int64_t arity = 0;
    while (cursor < length) {
        int64_t bound = argument_end(source, cursor);
        /* Text-literal arguments are single tokens outside the bounded
         * arithmetic expression grammar. */
        if (bound < 0) bound = token_end(source, cursor);
        ++arity;
        int64_t separator = skip_trivia(source, bound);
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
        {"fail", 0},
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
/* Defined beside the other numeric-type helpers; declared here because the
 * scope walk is the one caller that runs before them. */
static bool numeric_conversion_head(const char *source, int64_t cursor);
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
        {"fail", ""},
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
                stage2_diagnostic_set(
                    "E2S15",
                    argument,
                    token_end(source, argument),
                    true,
                    error.data
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
    int64_t length = source_length(source);
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
                        stage2_diagnostic_set(
                            "E2S23",
                            condition,
                            condition,
                            true,
                            error.data
                        );
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
                        stage2_diagnostic_set(
                            "E2S15",
                            value,
                            token_end(source, value),
                            true,
                            error.data
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
    int64_t length = source_length(source);
    int64_t cursor = next_function_start(source, 0);
    char *previous = owned_text("");
    while (cursor < length) {
        if (strcmp(token_kind(source, cursor), "identifier") == 0) {
            char *name = token_copy(source, cursor);
            int64_t open = skip_trivia(source, token_end(source, cursor));
            /*
             * `C(x)` on a declared enum constructor applies a constructor; it
             * is not a call.  Where such an application may appear is decided
             * by the enum guard in the scope HIR, whose diagnostic names the
             * constructor and its enum, so reporting an unknown callee here
             * would replace that with a misleading one.
             */
            char *constructor_owner = enum_constructor_owner(source, name);
            bool constructor_application =
                constructor_owner[0] != '\0' &&
                function_arity(source, name) < 0;
            free(constructor_owner);
            if (
                strcmp(previous, "fn") != 0 &&
                strcmp(name, "print") != 0 &&
                !constructor_application &&
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
                    stage2_diagnostic_set(
                        "E2S16",
                        cursor,
                        token_end(source, cursor),
                        true,
                        error.data
                    );
                    stage2_diagnostic_affected(
                        STAGE2_DIAGNOSTIC_AFFECTED_CALL,
                        cursor,
                        token_end(source, cursor)
                    );
                    free(name);
                    free(previous);
                    return error.data;
                }
                if (expected < 0) {
                    /* A callee the scope HIR bound to a lambda is lifted to a
                     * top-level function, so it is a known callee even though
                     * it is absent from the source's function table. Feeding
                     * its arity back into `expected` gives a lambda call the
                     * same arity diagnostic a named call already gets. */
                    expected = lambda_call_arity(source, hir, cursor);
                }
                if (expected < 0) {
                    /* A callee bound to a callable-typed parameter is called
                     * through the pointer it holds. Its arity is declared by
                     * the type, so the same arity diagnostic applies. */
                    expected = callable_call_arity(source, hir, cursor);
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
                        stage2_diagnostic_set(
                            "E2S16",
                            cursor,
                            token_end(source, cursor),
                            true,
                            error.data
                        );
                        stage2_diagnostic_affected(
                            STAGE2_DIAGNOSTIC_AFFECTED_CALL,
                            cursor,
                            token_end(source, cursor)
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
                        stage2_diagnostic_set(
                            "E2S17",
                            cursor,
                            token_end(source, cursor),
                            true,
                            error.data
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
                    stage2_diagnostic_set(
                        "E2S10",
                        cursor,
                        token_end(source, cursor),
                        true,
                        error.data
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
                    stage2_diagnostic_set(
                        "E2S17",
                        cursor,
                        token_end(source, cursor),
                        true,
                        error.data
                    );
                    free(name);
                    free(previous);
                    return error.data;
                }
                {
                    int64_t call_end = balanced_end(
                        source,
                        open,
                        "(",
                        ")"
                    );
                    char *return_type = function_return_type(
                        source,
                        name
                    );
                    if (call_end >= 0 && return_type[0] != '\0') {
                        stage2_semantic_observe(
                            "call|function|%s|%" PRId64 "|%" PRId64
                            "|%s\n",
                            name,
                            cursor,
                            call_end,
                            return_type
                        );
                    }
                    free(return_type);
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

static char *malformed_core_parameters_error(void) {
    char *error = owned_text(
        "error[E2S15]: malformed Core parameter list"
    );
    stage2_diagnostic_set("E2S15", 0, 0, false, error);
    return error;
}

static char *core_parameters(
    const char *source,
    const char *hir,
    int64_t function_start
) {
    int64_t parameters = parameter_open(source, function_start);
    if (parameters < 0) {
        return malformed_core_parameters_error();
    }
    int64_t parameters_end = balanced_end(source, parameters, "(", ")");
    if (parameters_end < 0) {
        return malformed_core_parameters_error();
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
            type_cursor >= parameters_end
        ) {
            free(name);
            free(emitted.data);
            return lower_error(
                "E2S15",
                "Core parameters must have type Int or a concrete enum",
                cursor
            );
        }
        /* A callable parameter type is checked before `Int` because its own
         * domain may be the single token `Int`: `f: Int -> Int` starts exactly
         * like `f: Int` and only the `->` after it tells them apart. */
        int64_t callable_end = callable_type_end(source, type_cursor);
        int64_t type_end = -1;
        char *binding_id = hir_definition_id_at(hir, cursor);
        char *declarator = NULL;
        if (callable_end >= 0 && callable_end <= parameters_end) {
            type_end = callable_end;
            Buffer pointer_name;
            buffer_init(&pointer_name);
            buffer_format(&pointer_name, "k_b%s", binding_id);
            declarator = callable_c_declarator(
                source,
                type_cursor,
                pointer_name.data
            );
            free(pointer_name.data);
        } else if (token_equal(source, type_cursor, "Int")) {
            type_end = token_end(source, type_cursor);
            Buffer plain;
            buffer_init(&plain);
            buffer_format(&plain, "int64_t k_b%s", binding_id);
            declarator = plain.data;
        } else if (
            strcmp(token_kind(source, type_cursor), "identifier") == 0
        ) {
            char *parameter_type = token_copy(source, type_cursor);
            if (enum_constructor_count(source, parameter_type) >= 0) {
                type_end = token_end(source, type_cursor);
                Buffer aggregate;
                buffer_init(&aggregate);
                buffer_format(
                    &aggregate,
                    "KofunEnumValue k_b%s",
                    binding_id
                );
                declarator = aggregate.data;
            }
            free(parameter_type);
        }
        free(binding_id);
        if (type_end < 0) {
            free(declarator);
            free(name);
            free(emitted.data);
            return lower_error(
                "E2S15",
                "Core parameters must have type Int or a concrete enum",
                cursor
            );
        }
        if (count > 0) buffer_append(&emitted, ", ");
        buffer_append(&emitted, declarator);
        free(declarator);
        free(name);
        ++count;
        int64_t separator = skip_trivia(source, type_end);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
    stage2_semantic_observe(
        "control|if|%" PRId64 "|%" PRId64 "|Int|%" PRId64
        "|%" PRId64 "\n",
        cursor,
        parts->end,
        parts->condition_start,
        parts->condition_end
    );
    return owned_text("ok");
}

static char *parse_value_match(
    const char *source,
    int64_t start,
    ValueMatchParts *parts
) {
    int64_t length = source_length(source);
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
    stage2_semantic_observe(
        "control|match|%" PRId64 "|%" PRId64 "|Int|%" PRId64
        "|%" PRId64 "\n",
        cursor,
        parts->end,
        parts->value_start,
        parts->value_end
    );
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
    int64_t length = source_length(source);
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
        if (cursor >= length) return -1;
        char *result_type = token_copy(source, cursor);
        bool supported_result =
            strcmp(result_type, "Int") == 0 ||
            enum_constructor_count(source, result_type) >= 0;
        free(result_type);
        if (!supported_result) return -1;
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
    stage2_diagnostic_set(
        code,
        cursor,
        cursor,
        cursor >= 0,
        error.data
    );
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

/*
 * A match arm's guard is written outside the arm body braces but belongs to the
 * arm: `Ready(value) if value == 3` must read the binding the body reads.  This
 * maps a token inside a guard to that arm's body `{`, so a guard use resolves
 * in the scope the payload binding was declared in.  The emitted C agrees: the
 * payload local is declared before the guard, inside the arm's `if`.  Returns
 * -1 for every token that is not inside a guard.
 */
static int64_t match_guard_scope_open(
    const char *source,
    int64_t function_open,
    int64_t target
) {
    int64_t cursor = skip_trivia(source, token_end(source, function_open));
    while (cursor <= target) {
        if (token_equal(source, cursor, "match")) {
            int64_t open = pattern_match_open(source, cursor);
            int64_t match_end = open < 0 ?
                -1 :
                balanced_end(source, open, "{", "}");
            if (open >= 0 && match_end >= 0) {
                int64_t close = match_end - 1;
                int64_t arm = skip_trivia(source, token_end(source, open));
                while (arm < close && !token_equal(source, arm, "}")) {
                    PatternSummary summary = pattern_summary(source, arm);
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
                    if (target >= summary.end && target < arrow) {
                        /*
                         * Only the payload name is redirected.  Every other
                         * guard use keeps the scope it already reported, so
                         * this widening cannot move a use that resolves
                         * without it.
                         */
                        int64_t open = skip_trivia(
                            source,
                            token_end(source, arm)
                        );
                        int64_t field = skip_trivia(
                            source,
                            token_end(source, open)
                        );
                        if (
                            summary.kind == PATTERN_CONSTRUCTOR &&
                            field < close &&
                            strcmp(
                                token_kind(source, field),
                                "identifier"
                            ) == 0 &&
                            !token_equal(source, field, "_")
                        ) {
                            char *payload_name = token_copy(source, field);
                            bool same = token_equal(
                                source,
                                target,
                                payload_name
                            );
                            free(payload_name);
                            if (same) return body;
                        }
                        return -1;
                    }
                    arm = skip_trivia(source, body_end);
                    if (arm < close && token_equal(source, arm, ",")) {
                        arm = skip_trivia(source, token_end(source, arm));
                    }
                }
            }
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return -1;
}

/*
 * Brace depth at `target`. `scope_depth_for_open` reports the depth of a `{`
 * token; a lambda scope opens on `(`, so it needs the running depth instead.
 */
static int64_t block_depth_at(
    const char *source,
    int64_t function_open,
    int64_t target
) {
    int64_t cursor = function_open;
    int64_t depth = 0;
    while (cursor < target) {
        if (token_equal(source, cursor, "{")) {
            ++depth;
        } else if (token_equal(source, cursor, "}")) {
            --depth;
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return depth;
}

/*
 * The byte after an arrow lambda's body, or -1 when `open` is not its parameter
 * list. A lambda is keyed by the `(`, not by `fn`, which makes `fn(x) => e` and
 * the parenthesised forms decided in #547 — `(x, y) => e` and `(x: Int) => e` —
 * one code path.
 *
 * Two conditions identify the parameter list, and both are needed:
 *
 *   1. its `)` is immediately followed by `=>`, and
 *   2. its `(` is NOT immediately preceded by an identifier.
 *
 * Condition 2 is not decoration. A constructor pattern ends in `)` and is
 * followed by `=>` — `Ok(value) => value` and `Err(error) => Err(error)` are
 * all over the shipped stdlib — so condition 1 alone matches every one of them.
 * What separates them is what comes before the `(`: a constructor pattern is
 * preceded by its variant name, a lambda's parameter list by `fn`, `=`, `,` or
 * `(`.
 *
 * The bare `x => e` form is deliberately absent. `IDENT => expr` is already
 * enum match-arm syntax — 176 arms in the shipped stdlib, e.g. `Trace => 0` —
 * so one token of lookahead cannot separate the two. See #547.
 *
 * The body is delimited by the same expression grammar the Core lowers, so a
 * body this Core cannot parse yields -1 and the lambda contributes no scope —
 * the identifiers inside it are then reported by whichever pass would have
 * reported them before.
 */
/*
 * Whether `target` begins a match arm's pattern.
 *
 * `IDENT => expr` is both an arm and a bare lambda, and arms are
 * comma-separated, so the token before it cannot separate them: `,` precedes
 * both `Debug => 1,` and the lambda in `map(xs, x => x * 2)`. Neither can one
 * token of lookahead, which is what #547 assumed. What separates them is
 * position — an arm pattern begins directly inside a `match` block's braces at
 * an arm boundary, and everything else is expression position.
 *
 * Walking arms is what makes this exact rather than approximate. A lambda may
 * appear inside an arm body (`Some(v) => map(xs, y => y + v)`), so "inside a
 * match block" would be wrong; only the arm's own first token is a pattern.
 *
 * The walk restarts at every `match` token, so a nested match is reached by its
 * own iteration rather than by recursion here.
 */
static bool match_arm_pattern_start(const char *source, int64_t target) {
    /*
     * The answer depends only on the source, and every identifier resolution
     * asks it, so the arm starts are collected once per source and then looked
     * up. Re-walking the arms per candidate is what makes the compiler
     * quadratic on a real file: `lambda_scope_open` already consults
     * `lambda_parameters_end` for every token of every function.
     */
    static const char *cached_source = NULL;
    static int64_t cached_length = -1;
    static int64_t *starts = NULL;
    static int64_t start_count = 0;
    int64_t length = source_length(source);
    if (source != cached_source || length != cached_length) {
        free(starts);
        starts = NULL;
        start_count = 0;
        int64_t capacity = 0;
        int64_t cursor = 0;
        while (cursor < length) {
            if (token_equal(source, cursor, "match")) {
                int64_t open = pattern_match_open(source, cursor);
                int64_t match_end =
                    open < 0 ? -1 : balanced_end(source, open, "{", "}");
                if (open >= 0 && match_end >= 0) {
                    int64_t close = match_end - 1;
                    int64_t arm = skip_trivia(source, token_end(source, open));
                    while (arm < close && !token_equal(source, arm, "}")) {
                        if (start_count == capacity) {
                            int64_t grown_capacity =
                                capacity == 0 ? 64 : capacity * 2;
                            int64_t *grown = realloc(
                                starts,
                                (size_t)grown_capacity * sizeof(*starts)
                            );
                            if (grown == NULL) break;
                            starts = grown;
                            capacity = grown_capacity;
                        }
                        starts[start_count++] = arm;
                        int64_t arrow = pattern_arm_arrow(source, arm, close);
                        if (arrow < 0) break;
                        int64_t body =
                            skip_trivia(source, token_end(source, arrow));
                        int64_t body_end = expression_end(source, body);
                        if (body_end < 0) break;
                        int64_t next = skip_trivia(source, body_end);
                        if (next < close && token_equal(source, next, ",")) {
                            next = skip_trivia(source, token_end(source, next));
                        }
                        if (next <= arm) break;
                        arm = next;
                    }
                }
            }
            cursor = skip_trivia(source, token_end(source, cursor));
        }
        cached_source = source;
        cached_length = length;
    }
    for (int64_t index = 0; index < start_count; ++index) {
        if (starts[index] == target) return true;
    }
    return false;
}

static int64_t lambda_parameters_end(
    const char *source,
    int64_t previous,
    int64_t open
) {
    int64_t length = source_length(source);
    if (!token_equal(source, open, "(")) {
        /* The bare single-parameter form `x => e` decided in #547. It is a
         * lambda everywhere an arm pattern is not, which is why the arm check
         * carries the whole decision. */
        if (strcmp(token_kind(source, open), "identifier") != 0) return -1;
        if (keyword_token(source, open)) return -1;
        int64_t bare_arrow = skip_trivia(source, token_end(source, open));
        if (bare_arrow >= length ||
            !token_equal(source, bare_arrow, "=>")) {
            return -1;
        }
        if (match_arm_pattern_start(source, open)) return -1;
        return expression_end(
            source,
            skip_trivia(source, token_end(source, bare_arrow))
        );
    }
    if (
        previous >= 0 &&
        strcmp(token_kind(source, previous), "identifier") == 0
    ) {
        return -1;
    }
    int64_t close = balanced_end(source, open, "(", ")");
    if (close < 0) return -1;
    int64_t arrow = skip_trivia(source, close);
    if (arrow >= length || !token_equal(source, arrow, "=>")) return -1;
    return expression_end(
        source,
        skip_trivia(source, token_end(source, arrow))
    );
}

/*
 * The innermost arrow lambda whose parameters are in scope at `target`, keyed
 * by its `(` so `hir_scope_id_for_open` finds the scope record. -1 when
 * `target` is not inside one.
 */
static int64_t lambda_scope_open(
    const char *source,
    int64_t function_open,
    int64_t target
) {
    int64_t cursor = function_open;
    int64_t previous = -1;
    int64_t found = -1;
    while (cursor < target) {
        if (lambda_parameters_end(source, previous, cursor) > target) {
            found = cursor;
        }
        previous = cursor;
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return found;
}

/*
 * `target` is a parameter name or a parameter type inside an arrow lambda's
 * parameter list. The identifier pass must resolve neither: the name is a
 * declaration, and the type names no binding.
 */
static bool lambda_declaration_syntax_token(
    const char *source,
    int64_t function_open,
    int64_t target
) {
    int64_t cursor = function_open;
    int64_t previous = -1;
    while (cursor <= target) {
        if (lambda_parameters_end(source, previous, cursor) >= 0) {
            if (!token_equal(source, cursor, "(")) {
                /* The bare form has no parameter list: the keying token is
                 * itself the parameter, so it is the only declaration. */
                if (target == cursor) return true;
            } else if (
                target > cursor &&
                target < balanced_end(source, cursor, "(", ")")
            ) {
                return true;
            }
        }
        previous = cursor;
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return false;
}

/*
 * `x: Int => e` annotates a lambda parameter without parentheses. #547 rejects
 * it: `:` is the type-annotation position everywhere else, so the bare form
 * reads as a binding whose type is `Int => e`, and accepting it would foreclose
 * writing function types with an arrow before #552 has decided whether to.
 * Detected forward as IDENT `:` TYPE `=>`; every other annotation position is
 * followed by `=`, `)` or `{`, never by `=>`.
 */
static bool lambda_unparenthesised_annotation(
    const char *source,
    int64_t start
) {
    int64_t length = source_length(source);
    if (strcmp(token_kind(source, start), "identifier") != 0) return false;
    int64_t colon = skip_trivia(source, token_end(source, start));
    if (colon >= length || !token_equal(source, colon, ":")) return false;
    int64_t annotation = skip_trivia(source, token_end(source, colon));
    if (annotation >= length) return false;
    int64_t arrow = skip_trivia(source, token_end(source, annotation));
    return arrow < length && token_equal(source, arrow, "=>");
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
    /* The scope-HIR walker searches the document it is still building, once
     * per record, so this is the pass's inner loop. strstr answers the same
     * question as the per-position strncmp scan it replaces: the first offset
     * at or after `start` where `wanted` occurs, or -1. */
    if (start < 0) return -1;
    if (wanted[0] == '\0') return start;
    const char *hit = strstr(value + start, wanted);
    return hit == NULL ? -1 : (int64_t)(hit - value);
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
    if (line_start < 0) return owned_text("");
    int64_t cursor = line_start;
    int field = 0;
    int64_t field_start = line_start;
    while (hir[cursor] != '\0') {
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


/*
 * Lazy incremental index over scope-HIR "scope", "binding" and "use"
 * records. The builder appends records and immediately consults the
 * document, once per record, so answering each consultation with a whole-
 * document scan made the build quadratic in record count. This index parses
 * each complete record line exactly once and answers by key instead.
 *
 * One slot, keyed on the document pointer. The builder invalidates the slot
 * when it starts a new document; a pointer change (a moved reallocation or a
 * different document) rebuilds from the current bytes, and a grown document
 * re-parses only the lines appended since the previous consultation. Both
 * fall back to a full re-parse, so the index can be dropped without changing
 * a single output byte.
 *
 * Key layout: one tag byte, then unit-separated parts. Values are line
 * offsets in document order, so first-match and last-match callers keep
 * their original selection order.
 */
enum { HIR_INDEX_PARTS = 11 };

typedef struct HirIndexEntry {
    char *key;
    int64_t *lines;
    int64_t count;
    int64_t capacity;
} HirIndexEntry;

static const char *hir_index_doc;
static int64_t hir_index_upto;
static HirIndexEntry *hir_index_entries;
static int64_t hir_index_count;
static int64_t hir_index_capacity;
static Buffer hir_index_key_buffer;

static void hir_index_invalidate(void) {
    for (int64_t at = 0; at < hir_index_capacity; at += 1) {
        free(hir_index_entries[at].key);
        free(hir_index_entries[at].lines);
    }
    free(hir_index_entries);
    hir_index_entries = NULL;
    hir_index_count = 0;
    hir_index_capacity = 0;
    hir_index_doc = NULL;
    hir_index_upto = 0;
    free(hir_index_key_buffer.data);
    hir_index_key_buffer.data = NULL;
    hir_index_key_buffer.length = 0;
    hir_index_key_buffer.capacity = 0;
}

static uint64_t hir_index_hash(const char *key) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (const char *at = key; *at != '\0'; at += 1) {
        hash ^= (uint64_t)(unsigned char)*at;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static HirIndexEntry *hir_index_slot(const char *key) {
    uint64_t probe = hir_index_hash(key) & (uint64_t)(hir_index_capacity - 1);
    for (;;) {
        HirIndexEntry *entry = &hir_index_entries[probe];
        if (entry->key == NULL || strcmp(entry->key, key) == 0) return entry;
        probe = (probe + 1) & (uint64_t)(hir_index_capacity - 1);
    }
}

static void hir_index_grow(void) {
    int64_t old_capacity = hir_index_capacity;
    HirIndexEntry *old_entries = hir_index_entries;
    hir_index_capacity = old_capacity == 0 ? 1024 : old_capacity * 2;
    hir_index_entries = calloc(
        (size_t)hir_index_capacity,
        sizeof(HirIndexEntry)
    );
    if (hir_index_entries == NULL) fail("stage2 seed: out of memory");
    for (int64_t at = 0; at < old_capacity; at += 1) {
        if (old_entries[at].key == NULL) continue;
        *hir_index_slot(old_entries[at].key) = old_entries[at];
    }
    free(old_entries);
}

static void hir_index_add(const char *key, int64_t line, bool first_only) {
    if (hir_index_count * 10 >= hir_index_capacity * 7) hir_index_grow();
    HirIndexEntry *entry = hir_index_slot(key);
    if (entry->key == NULL) {
        entry->key = owned_text(key);
        hir_index_count += 1;
    } else if (first_only) {
        return;
    }
    if (entry->count == entry->capacity) {
        entry->capacity = entry->capacity == 0 ? 2 : entry->capacity * 2;
        int64_t *lines = realloc(
            entry->lines,
            (size_t)entry->capacity * sizeof(int64_t)
        );
        if (lines == NULL) fail("stage2 seed: out of memory");
        entry->lines = lines;
    }
    entry->lines[entry->count] = line;
    entry->count += 1;
}

/* The fields of one record line, as offsets of each '|'-separated part.
 * Returns the number of parts found, up to HIR_INDEX_PARTS. */
static int hir_index_split(
    const char *doc,
    int64_t line,
    int64_t newline,
    int64_t *starts,
    int64_t *ends
) {
    int parts = 0;
    int64_t start = line;
    for (int64_t at = line; at <= newline && parts < HIR_INDEX_PARTS; at += 1) {
        if (at == newline || doc[at] == '|') {
            starts[parts] = start;
            ends[parts] = at;
            parts += 1;
            start = at + 1;
        }
    }
    return parts;
}

/* "tag \x1f first [\x1f second]" in the reused key buffer. Both the refresh
 * loop and every lookup build their keys here, so the wire format has one
 * writer; the buffer is grown in place and freed by hir_index_invalidate. */
static const char *hir_index_key(
    char tag,
    const char *first,
    size_t first_length,
    const char *second,
    size_t second_length
) {
    Buffer *key = &hir_index_key_buffer;
    if (key->data == NULL) buffer_init(key);
    key->length = 0;
    buffer_reserve(key, first_length + second_length + 4);
    key->data[key->length] = tag;
    key->length += 1;
    key->data[key->length] = '\x1f';
    key->length += 1;
    memcpy(key->data + key->length, first, first_length);
    key->length += first_length;
    if (second != NULL) {
        key->data[key->length] = '\x1f';
        key->length += 1;
        memcpy(key->data + key->length, second, second_length);
        key->length += second_length;
    }
    key->data[key->length] = '\0';
    return key->data;
}

static void hir_index_refresh(const char *doc) {
    if (doc != hir_index_doc) {
        hir_index_invalidate();
        hir_index_doc = doc;
    }
    if (hir_index_capacity == 0) hir_index_grow();
    int64_t cursor = hir_index_upto;
    for (;;) {
        int64_t newline = cursor;
        while (doc[newline] != '\0' && doc[newline] != '\n') newline += 1;
        if (doc[newline] == '\0') break;
        int64_t starts[HIR_INDEX_PARTS];
        int64_t ends[HIR_INDEX_PARTS];
        int parts = hir_index_split(doc, cursor, newline, starts, ends);
        int64_t width = ends[0] - starts[0];
        if (width == 5 && strncmp(doc + starts[0], "scope", 5) == 0) {
            if (parts > 1) {
                hir_index_add(
                    hir_index_key(
                        'c', doc + starts[1],
                        (size_t)(ends[1] - starts[1]), NULL, 0
                    ),
                    cursor, true
                );
            }
            if (parts > 4) {
                hir_index_add(
                    hir_index_key(
                        'o', doc + starts[4],
                        (size_t)(ends[4] - starts[4]), NULL, 0
                    ),
                    cursor, true
                );
            }
        } else if (
            width == 7 && strncmp(doc + starts[0], "binding", 7) == 0
        ) {
            if (parts > 1) {
                hir_index_add(
                    hir_index_key(
                        'b', doc + starts[1],
                        (size_t)(ends[1] - starts[1]), NULL, 0
                    ),
                    cursor, true
                );
            }
            if (parts > 3) {
                hir_index_add(
                    hir_index_key(
                        'n', doc + starts[2], (size_t)(ends[2] - starts[2]),
                        doc + starts[3], (size_t)(ends[3] - starts[3])
                    ),
                    cursor, false
                );
            }
            if (parts > 8) {
                hir_index_add(
                    hir_index_key(
                        'd', doc + starts[8],
                        (size_t)(ends[8] - starts[8]), NULL, 0
                    ),
                    cursor, true
                );
            }
        } else if (width == 3 && strncmp(doc + starts[0], "use", 3) == 0) {
            if (parts > 1) {
                hir_index_add(
                    hir_index_key(
                        'u', doc + starts[1],
                        (size_t)(ends[1] - starts[1]), NULL, 0
                    ),
                    cursor, true
                );
            }
        }
        cursor = newline + 1;
    }
    hir_index_upto = cursor;
}

/* The record lines a key names, in document order; NULL when none do. The
 * returned entry borrows the index's table and is valid only until the next
 * index call, which may grow the table; consumers must not hold it across
 * another lookup or refresh. */
static const HirIndexEntry *hir_index_list(
    const char *doc,
    char tag,
    const char *first,
    const char *second
) {
    hir_index_refresh(doc);
    const char *key = hir_index_key(
        tag,
        first, strlen(first),
        second, second == NULL ? 0 : strlen(second)
    );
    HirIndexEntry *entry = hir_index_slot(key);
    return entry->key == NULL ? NULL : entry;
}

/* The first record line a key names, or -1. */
static int64_t hir_index_first(
    const char *doc,
    char tag,
    const char *first,
    const char *second
) {
    const HirIndexEntry *entry = hir_index_list(doc, tag, first, second);
    return entry == NULL ? -1 : entry->lines[0];
}

/* One field of the first record a key names, or "" when no record does. */
static char *hir_index_field(
    const char *doc,
    char tag,
    const char *first,
    const char *second,
    int field
) {
    int64_t line = hir_index_first(doc, tag, first, second);
    if (line < 0) return owned_text("");
    return hir_field(doc, line, field);
}

/* hir_index_field for the integer-valued keys. */
static char *hir_index_field_number(
    const char *doc,
    char tag,
    int64_t number,
    int field
) {
    char text[24];
    snprintf(text, sizeof text, "%" PRId64, number);
    return hir_index_field(doc, tag, text, NULL, field);
}

static char *hir_same_scope_declaration(
    const char *hir,
    const char *scope_id,
    const char *name
) {
    return hir_index_field(hir, 'n', scope_id, name, 8);
}

static char *hir_scope_id_for_open(const char *hir, int64_t open) {
    return hir_index_field_number(hir, 'o', open, 1);
}

/* The byte the binding's declaration starts at, or -1 for an unknown id. */
static int64_t hir_binding_declaration_start(
    const char *hir,
    const char *binding_id
) {
    char *start_text = hir_binding_field(hir, binding_id, 8);
    int64_t start = start_text[0] == '\0' ? -1 : decimal_value(start_text);
    free(start_text);
    return start;
}

/*
 * The `(` of the arrow lambda a `let` initializer is, or -1 when the
 * initializer is something else. `value_start` is the first byte after `=`,
 * so what precedes the parameter list is `=` or `fn` — never an identifier,
 * which is why -1 is the right `previous` for `lambda_parameters_end`: the
 * constructor-pattern ambiguity that argument guards against cannot arise
 * here.
 */
static int64_t lambda_initializer_open(
    const char *source,
    int64_t value_start
) {
    int64_t cursor = skip_trivia(source, value_start);
    if (token_equal(source, cursor, "fn")) {
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    if (lambda_parameters_end(source, -1, cursor) < 0) return -1;
    return cursor;
}

/*
 * The `(` of the lambda a binding holds, or -1 when that binding is not a
 * lambda. This is how a call site reaches the lifted function: the scope HIR
 * has already resolved the callee name to a binding, shadowing included, so
 * the lowering never repeats that resolution by name.
 */
static int64_t lambda_binding_open(
    const char *source,
    const char *hir,
    const char *binding_id
) {
    int64_t declaration_start = hir_binding_declaration_start(hir, binding_id);
    if (declaration_start < 0) return -1;
    int64_t equals = skip_trivia(
        source,
        token_end(source, declaration_start)
    );
    if (!token_equal(source, equals, "=")) return -1;
    return lambda_initializer_open(
        source,
        skip_trivia(source, token_end(source, equals))
    );
}

/*
 * The callable type of the parameter a call site's callee binding declares, as
 * the byte offset of its type, or -1 when the callee is not a callable-typed
 * parameter. This is how an indirect call reaches its arity and its C
 * declarator without repeating name resolution: the scope HIR has already
 * resolved the callee to a binding, shadowing included.
 */
static int64_t callable_parameter_type_start(
    const char *source,
    const char *hir,
    const char *binding_id
) {
    int64_t length = source_length(source);
    int64_t declaration_start = hir_binding_declaration_start(hir, binding_id);
    if (declaration_start < 0) return -1;
    int64_t colon = skip_trivia(source, token_end(source, declaration_start));
    if (colon >= length || !token_equal(source, colon, ":")) return -1;
    int64_t type_cursor = skip_trivia(source, token_end(source, colon));
    if (callable_type_end(source, type_cursor) < 0) return -1;
    return type_cursor;
}

/*
 * The declared arity of the callable-typed parameter a call site resolves to,
 * or -1 when the callee is not one.
 */
static int64_t callable_call_arity(
    const char *source,
    const char *hir,
    int64_t use_start
) {
    char *callee_binding = hir_use_binding_id(hir, use_start);
    if (callee_binding[0] == '\0') {
        free(callee_binding);
        return -1;
    }
    int64_t type_start = callable_parameter_type_start(
        source,
        hir,
        callee_binding
    );
    free(callee_binding);
    if (type_start < 0) return -1;
    return callable_type_arity(source, type_start);
}

/*
 * The binding ids a lambda body reads from outside its own parameter list, in
 * HIR order, separated by `|`, or "" when the lambda captures nothing.
 *
 * A capture is exactly a use inside the lambda whose binding lives in another
 * scope. Lifting passes each one as a trailing parameter, so a lifted lambda
 * stays a plain `int64_t` function and the frozen profile gains no function
 * type. The call site appends the same ids in the same order, and a captured
 * binding is a C local of the enclosing function, so it is in scope wherever
 * the lambda binding itself is.
 */
static char *lambda_captures(
    const char *source,
    const char *hir,
    int64_t lambda_open
) {
    int64_t body_end = lambda_parameters_end(source, -1, lambda_open);
    char *scope_id = hir_scope_id_for_open(hir, lambda_open);
    Buffer captured;
    buffer_init(&captured);
    int64_t line = hir_record_start(hir, "use", 0);
    while (line >= 0) {
        char *start_text = hir_field(hir, line, 1);
        int64_t use_start = decimal_value(start_text);
        free(start_text);
        if (use_start > lambda_open && use_start < body_end) {
            char *binding_id = hir_field(hir, line, 4);
            char *binding_scope = hir_binding_field(hir, binding_id, 2);
            /* A lambda binding in an enclosing scope is not a capture: it has
             * no `int64_t` to pass, and the call reaches its lifted function
             * by name. Counting it would emit a parameter for a C variable
             * that a lambda binding never declares. */
            if (strcmp(binding_scope, scope_id) != 0 &&
                lambda_binding_open(source, hir, binding_id) < 0) {
                /* A body may read the same capture more than once; the
                 * parameter list must name it once. */
                Buffer seen;
                buffer_init(&seen);
                buffer_format(&seen, "|%s|", captured.data);
                Buffer wanted;
                buffer_init(&wanted);
                buffer_format(&wanted, "|%s|", binding_id);
                if (strstr(seen.data, wanted.data) == NULL) {
                    if (captured.length > 0) buffer_append(&captured, "|");
                    buffer_append(&captured, binding_id);
                }
                free(seen.data);
                free(wanted.data);
            }
            free(binding_scope);
            free(binding_id);
        }
        line = hir_record_start(hir, "use", line + 1);
    }
    free(scope_id);
    return captured.data;
}

/* The declared parameter count of the lambda whose parameter list opens at
 * `open`. Captures are invisible to a caller, so they are not counted. */
static int64_t lambda_parameter_count(const char *source, int64_t open) {
    /* The bare form `x => e` is keyed by its single parameter, so it has no
     * list to count and its arity is always one. */
    if (!token_equal(source, open, "(")) return 1;
    int64_t close = balanced_end(source, open, "(", ")");
    if (close < 0) return -1;
    int64_t count = 0;
    int64_t parameter = skip_trivia(source, token_end(source, open));
    while (parameter < close) {
        if (strcmp(token_kind(source, parameter), "identifier") != 0) break;
        ++count;
        int64_t after = skip_trivia(source, token_end(source, parameter));
        if (after < close && token_equal(source, after, ":")) {
            int64_t annotation = skip_trivia(source, token_end(source, after));
            after = skip_trivia(source, token_end(source, annotation));
        }
        if (after < close && token_equal(source, after, ",")) {
            after = skip_trivia(source, token_end(source, after));
        }
        parameter = after;
    }
    return count;
}

/*
 * The declared arity of the lambda a call site resolves to, or -1 when the
 * callee is not a lambda binding.
 */
static int64_t lambda_call_arity(
    const char *source,
    const char *hir,
    int64_t use_start
) {
    char *callee_binding = hir_use_binding_id(hir, use_start);
    if (callee_binding[0] == '\0') {
        free(callee_binding);
        return -1;
    }
    int64_t open = lambda_binding_open(source, hir, callee_binding);
    free(callee_binding);
    if (open < 0) return -1;
    return lambda_parameter_count(source, open);
}

/*
 * Writes one `k_b<id>` per capture into `output`, prefixed by `declaration`
 * for a parameter list and by nothing for an argument list, separating with
 * `, ` when `written` items already precede them. The lifted signature and
 * every call to it go through this one function so their orders cannot drift.
 */
static void append_captures(
    Buffer *output,
    const char *captures,
    int64_t written,
    const char *declaration
) {
    int64_t count = 0;
    size_t cursor = 0;
    while (captures[cursor] != '\0') {
        size_t stop = cursor;
        while (captures[stop] != '\0' && captures[stop] != '|') ++stop;
        if (written > 0 || count > 0) buffer_append(output, ", ");
        buffer_append(output, declaration);
        buffer_append(output, "k_b");
        for (size_t index = cursor; index < stop; ++index) {
            char symbol[2] = {captures[index], '\0'};
            buffer_append(output, symbol);
        }
        ++count;
        cursor = captures[stop] == '\0' ? stop : stop + 1;
    }
}

static char *hir_scope_field(
    const char *hir,
    const char *scope_id,
    int field
) {
    return hir_index_field(hir, 'c', scope_id, NULL, field);
}

static char *hir_binding_field(
    const char *hir,
    const char *binding_id,
    int field
) {
    return hir_index_field(hir, 'b', binding_id, NULL, field);
}

static char *hir_definition_id_at(
    const char *hir,
    int64_t declaration_start
) {
    return hir_index_field_number(hir, 'd', declaration_start, 1);
}

static char *hir_use_binding_id(const char *hir, int64_t use_start) {
    return hir_index_field_number(hir, 'u', use_start, 4);
}

static char *hir_resolve_binding(
    const char *hir,
    const char *current_scope,
    int64_t use_start,
    const char *name
) {
    char *scope_id = owned_text(current_scope);
    while (scope_id[0] != '\0' && strcmp(scope_id, "-1") != 0) {
        const HirIndexEntry *list =
            hir_index_list(hir, 'n', scope_id, name);
        int64_t entries = list == NULL ? 0 : list->count;
        for (int64_t at = entries - 1; at >= 0; at -= 1) {
            int64_t line = list->lines[at];
            char *visible_text = hir_field(hir, line, 10);
            bool visible = decimal_value(visible_text) <= use_start;
            free(visible_text);
            if (visible) {
                free(scope_id);
                return hir_field(hir, line, 1);
            }
        }
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
        const HirIndexEntry *list =
            hir_index_list(hir, 'n', scope_id, name);
        int64_t entries = list == NULL ? 0 : list->count;
        for (int64_t at = 0; at < entries; at += 1) {
            int64_t line = list->lines[at];
            char *declaration_text = hir_field(hir, line, 8);
            char *visible_text = hir_field(hir, line, 10);
            int64_t declaration = decimal_value(declaration_text);
            int64_t visible = decimal_value(visible_text);
            free(visible_text);
            if (declaration < use_start && use_start < visible) {
                free(scope_id);
                return declaration_text;
            }
            free(declaration_text);
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
 * Result types of the 17 profile builtins for `let` initializer typing.
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
        {"fail", "Void"},
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

static char *function_return_type_at(
    const char *source,
    int64_t function_start
) {
    int64_t length = source_length(source);
    int64_t parameters = parameter_open(source, function_start);
    int64_t parameters_end;
    int64_t after;
    if (parameters < 0) return owned_text("");
    parameters_end = balanced_end(source, parameters, "(", ")");
    if (parameters_end < 0) return owned_text("");
    after = skip_trivia(source, parameters_end);
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

/* Declared result type of a user function: the token after `->`, `Void`
 * when there is no arrow, empty when the function is not declared. */
static char *function_return_type(const char *source, const char *wanted) {
    int64_t length = source_length(source);
    int64_t cursor = next_function_start(source, 0);
    while (cursor < length) {
        char *name = function_name(source, cursor);
        bool match = strcmp(name, wanted) == 0;
        free(name);
        if (match) {
            return function_return_type_at(source, cursor);
        }
        cursor = next_function_start(source, function_end(source, cursor));
    }
    return owned_text("");
}

/*
 * Declared type of one named function parameter.  The bounded enum C11 slice
 * needs this at call lowering time because an enum value is a two-word
 * aggregate rather than an Int expression.  Callable and bracketed parameter
 * types are skipped as one type while walking to the requested index, so the
 * helper does not change the existing higher-order parameter grammar.
 */
static char *function_parameter_type_at(
    const char *source,
    int64_t function_start,
    int64_t wanted_index
) {
    int64_t parameters = parameter_open(source, function_start);
    if (parameters < 0) return owned_text("");
    int64_t parameters_end = balanced_end(source, parameters, "(", ")");
    if (parameters_end < 0) return owned_text("");
    int64_t cursor = skip_trivia(source, token_end(source, parameters));
    int64_t index = 0;
    while (
        cursor < parameters_end &&
        !token_equal(source, cursor, ")")
    ) {
        int64_t colon = skip_trivia(source, token_end(source, cursor));
        if (
            colon >= parameters_end ||
            !token_equal(source, colon, ":")
        ) {
            return owned_text("");
        }
        int64_t type_start = skip_trivia(source, token_end(source, colon));
        if (type_start >= parameters_end) return owned_text("");
        if (index == wanted_index) return token_copy(source, type_start);

        int64_t type_end = callable_type_end(source, type_start);
        if (type_end < 0) {
            type_end = token_end(source, type_start);
            int64_t bracket = skip_trivia(source, type_end);
            if (
                bracket < parameters_end &&
                token_equal(source, bracket, "[")
            ) {
                int64_t close = balanced_end(source, bracket, "[", "]");
                if (close < 0) return owned_text("");
                type_end = close;
            }
        }
        int64_t separator = skip_trivia(source, type_end);
        if (
            separator < parameters_end &&
            token_equal(source, separator, ",")
        ) {
            cursor = skip_trivia(source, token_end(source, separator));
        } else {
            cursor = separator;
        }
        ++index;
    }
    return owned_text("");
}

static char *function_parameter_type(
    const char *source,
    const char *wanted,
    int64_t index
) {
    int64_t length = source_length(source);
    int64_t cursor = next_function_start(source, 0);
    while (cursor < length) {
        char *name = function_name(source, cursor);
        bool match = strcmp(name, wanted) == 0;
        free(name);
        if (match) {
            return function_parameter_type_at(source, cursor, index);
        }
        cursor = next_function_start(source, function_end(source, cursor));
    }
    return owned_text("");
}

/* Result type of the function whose body contains `position`. */
static char *function_return_type_containing(
    const char *source,
    int64_t position
) {
    int64_t length = source_length(source);
    int64_t cursor = next_function_start(source, 0);
    while (cursor < length) {
        int64_t end = function_end(source, cursor);
        if (position >= cursor && position < end) {
            return function_return_type_at(source, cursor);
        }
        cursor = next_function_start(source, end);
    }
    return owned_text("");
}

static bool function_result_is_enum(
    const char *source,
    const char *name
) {
    char *type = function_return_type(source, name);
    bool result = enum_constructor_count(source, type) >= 0;
    free(type);
    return result;
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
    int64_t length = source_length(source);
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
    /* #710 frozen decision 2: an unsuffixed fractional or scientific literal
     * denotes Decimal, and the `f64` suffix selects binary64 Float. Recording
     * them here is what puts the type in the scope HIR, so an unannotated
     * `let x = 1.5` is a Decimal binding rather than the historical Int
     * default. */
    if (strcmp(kind, "decimal") == 0) return owned_text("Decimal");
    if (strcmp(kind, "float") == 0) return owned_text("Float");
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
            char *constructor_type = enum_constructor_owner(source, name);
            if (constructor_type[0] != '\0') {
                free(name);
                return constructor_type;
            }
            free(constructor_type);
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
    hir_index_invalidate();
    int64_t length = source_length(source);
    /* The removed callable notation is rejected before any binding is
     * collected, so a source written in it gets the migration diagnostic
     * rather than whatever its multi-token type happens to desynchronise. */
    char *notation_check = validate_removed_callable_notation(source);
    if (strncmp(notation_check, "error[", 6) == 0) return notation_check;
    free(notation_check);
    Buffer hir;
    buffer_init(&hir);
    buffer_append(&hir, "kofun-scope-hir/v1\n");
    stage2_scope_prefix_observe(&hir);
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
        stage2_scope_prefix_observe(&hir);

        int64_t cursor = skip_trivia(
            source,
            token_end(source, function_open)
        );
        int64_t previous = -1;
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
                stage2_scope_prefix_observe(&hir);
                free(parent_scope);
            } else if (lambda_unparenthesised_annotation(source, cursor)) {
                int64_t colon = skip_trivia(source, token_end(source, cursor));
                int64_t annotation = skip_trivia(
                    source,
                    token_end(source, colon)
                );
                char *parameter_name = token_copy(source, cursor);
                char *annotation_text = token_copy(source, annotation);
                Buffer error;
                buffer_init(&error);
                buffer_format(
                    &error,
                    "error[E2S95]: annotated lambda parameter `%s` needs "
                    "parentheses at byte %" PRId64 "; write `(%s: %s) =>`",
                    parameter_name,
                    cursor,
                    parameter_name,
                    annotation_text
                );
                stage2_diagnostic_set(
                    "E2S95",
                    cursor,
                    token_end(source, cursor),
                    true,
                    error.data
                );
                stage2_diagnostic_affected(
                    STAGE2_DIAGNOSTIC_AFFECTED_BINDING,
                    cursor,
                    token_end(source, cursor)
                );
                free(parameter_name);
                free(annotation_text);
                free(hir.data);
                return error.data;
            } else {
                int64_t lambda_close = lambda_parameters_end(
                    source,
                    previous,
                    cursor
                );
                if (lambda_close >= 0) {
                    int64_t lambda_open = cursor;
                    int64_t lambda_depth = block_depth_at(
                        source,
                        function_open,
                        cursor
                    ) + 1;
                    if (lambda_depth > 32) {
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
                    char *lambda_parent = hir_scope_id_for_open(
                        hir.data,
                        parent_block_open(source, function_open, lambda_open)
                    );
                    buffer_format(
                        &hir,
                        "scope|%" PRId64 "|%s|lambda-parameters|%" PRId64
                        "|%" PRId64 "|%" PRId64 "\n",
                        next_scope_id++,
                        lambda_parent,
                        lambda_open,
                        lambda_close,
                        lambda_depth
                    );
                    stage2_scope_prefix_observe(&hir);
                    free(lambda_parent);
                }
            }
            previous = cursor;
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
                stage2_diagnostic_set(
                    "E2S47",
                    name,
                    token_end(source, name),
                    true,
                    error.data
                );
                stage2_diagnostic_affected(
                    STAGE2_DIAGNOSTIC_AFFECTED_BINDING,
                    name,
                    token_end(source, name)
                );
                {
                    int64_t first = decimal_value(first_declaration);
                    stage2_diagnostic_related(
                        first,
                        token_end(source, first),
                        "first declaration"
                    );
                }
                stage2_diagnostic_remedy(2u);
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
            /* A callable type spans several tokens. Stepping over only the
             * first would leave this walk inside the type, so the parameters
             * after it would never be bound and their uses would be reported
             * as unknown lexical bindings. */
            int64_t callable_end = callable_type_end(source, type_cursor);
            int64_t type_end = callable_end >= 0
                ? callable_end
                : token_end(source, type_cursor);
            char *type_text = callable_end >= 0
                ? owned_text("Fn")
                : token_copy(source, type_cursor);
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
            stage2_scope_prefix_observe(&hir);
            free(name_text);
            free(type_text);
            int64_t separator = skip_trivia(source, type_end);
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
        previous = -1;
        while (cursor < function_close) {
            if (lambda_parameters_end(source, previous, cursor) >= 0) {
                int64_t lambda_open = cursor;
                /* The bare form has no parameter list to walk: the parameter
                 * is the keying token itself, so the walk below covers exactly
                 * that one identifier and then stops at the `=>`. */
                bool bare_lambda = !token_equal(source, lambda_open, "(");
                int64_t lambda_close = bare_lambda
                    ? token_end(source, lambda_open)
                    : balanced_end(source, lambda_open, "(", ")");
                char *lambda_scope = hir_scope_id_for_open(
                    hir.data,
                    lambda_open
                );
                int64_t lambda_cursor = bare_lambda
                    ? lambda_open
                    : skip_trivia(source, token_end(source, lambda_open));
                while (
                    lambda_cursor < lambda_close &&
                    !token_equal(source, lambda_cursor, ")")
                ) {
                    int64_t parameter = lambda_cursor;
                    int64_t after = skip_trivia(
                        source,
                        token_end(source, parameter)
                    );
                    char *parameter_type = NULL;
                    if (token_equal(source, after, ":")) {
                        int64_t annotation = skip_trivia(
                            source,
                            token_end(source, after)
                        );
                        parameter_type = token_copy(source, annotation);
                        after = skip_trivia(
                            source,
                            token_end(source, annotation)
                        );
                    }
                    char *parameter_name = token_copy(source, parameter);
                    char *first = hir_same_scope_declaration(
                        hir.data,
                        lambda_scope,
                        parameter_name
                    );
                    if (first[0] != '\0') {
                        Buffer error;
                        buffer_init(&error);
                        buffer_format(
                            &error,
                            "error[E2S47]: duplicate binding `%s` in lexical "
                            "scope at byte %" PRId64
                            "; first declaration at byte %s",
                            parameter_name,
                            parameter,
                            first
                        );
                        stage2_diagnostic_set(
                            "E2S47",
                            parameter,
                            token_end(source, parameter),
                            true,
                            error.data
                        );
                        stage2_diagnostic_affected(
                            STAGE2_DIAGNOSTIC_AFFECTED_BINDING,
                            parameter,
                            token_end(source, parameter)
                        );
                        {
                            int64_t at = decimal_value(first);
                            stage2_diagnostic_related(
                                at,
                                token_end(source, at),
                                "first declaration"
                            );
                        }
                        stage2_diagnostic_remedy(2u);
                        free(parameter_name);
                        free(parameter_type);
                        free(first);
                        free(lambda_scope);
                        free(hir.data);
                        return error.data;
                    }
                    free(first);
                    ++binding_count;
                    if (binding_count > 256) {
                        free(parameter_name);
                        free(parameter_type);
                        free(lambda_scope);
                        return scope_hir_error(
                            &hir,
                            "lexical binding limit is 256 per function",
                            parameter
                        );
                    }
                    buffer_format(
                        &hir,
                        "binding|%" PRId64 "|%s|%s|immutable|%s|copy|"
                        "initialized|%" PRId64 "|%" PRId64 "|%" PRId64 "\n",
                        next_binding_id++,
                        lambda_scope,
                        parameter_name,
                        parameter_type == NULL ? "Int" : parameter_type,
                        parameter,
                        token_end(source, parameter),
                        token_end(source, parameter)
                    );
                    stage2_scope_prefix_observe(&hir);
                    free(parameter_name);
                    free(parameter_type);
                    if (
                        after < lambda_close &&
                        token_equal(source, after, ",")
                    ) {
                        lambda_cursor = skip_trivia(
                            source,
                            token_end(source, after)
                        );
                    } else {
                        lambda_cursor = after;
                    }
                }
                free(lambda_scope);
            }
            previous = cursor;
            cursor = skip_trivia(source, token_end(source, cursor));
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
                    stage2_diagnostic_set(
                        "E2S47",
                        name,
                        token_end(source, name),
                        true,
                        error.data
                    );
                    stage2_diagnostic_affected(
                        STAGE2_DIAGNOSTIC_AFFECTED_BINDING,
                        name,
                        token_end(source, name)
                    );
                    {
                        int64_t first = decimal_value(first_declaration);
                        stage2_diagnostic_related(
                            first,
                            token_end(source, first),
                            "first declaration"
                        );
                    }
                    stage2_diagnostic_remedy(2u);
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
                stage2_scope_prefix_observe(&hir);
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
                        stage2_diagnostic_set(
                            "E2S47",
                            name,
                            token_end(source, name),
                            true,
                            error.data
                        );
                        stage2_diagnostic_affected(
                            STAGE2_DIAGNOSTIC_AFFECTED_BINDING,
                            name,
                            token_end(source, name)
                        );
                        {
                            int64_t first =
                                decimal_value(first_declaration);
                            stage2_diagnostic_related(
                                first,
                                token_end(source, first),
                                "first declaration"
                            );
                        }
                        stage2_diagnostic_remedy(2u);
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
                    stage2_scope_prefix_observe(&hir);
                    free(name_text);
                    free(scope_id);
                }
            }
            /*
             * A constructor pattern that names its payload declares a binding
             * in the arm body's scope, exactly as a `for` name declares one in
             * the loop body.  Arm bodies are skipped whole, so a nested match
             * is reached by its own `match` token and no arm is visited twice.
             *
             * Not in candidate-preserving mode: there the resolved ADT
             * adapter owns pattern bindings and expects the payload name to
             * arrive unresolved as a `candidate-use`.  Declaring it here would
             * resolve the name first and take that input away from it.
             */
            if (
                !preserve_pattern_candidates &&
                token_equal(source, cursor, "match")
            ) {
                int64_t arms_open = pattern_match_open(source, cursor);
                int64_t arms_end = arms_open < 0 ?
                    -1 :
                    balanced_end(source, arms_open, "{", "}");
                int64_t match_close = arms_end < 0 ? -1 : arms_end - 1;
                int64_t arm_cursor = arms_end < 0 ?
                    function_close :
                    skip_trivia(source, token_end(source, arms_open));
                while (
                    arm_cursor < match_close &&
                    arm_cursor < function_close &&
                    !token_equal(source, arm_cursor, "}")
                ) {
                    PatternSummary arm = pattern_summary(source, arm_cursor);
                    int64_t arrow = pattern_arm_arrow(
                        source,
                        arm.end,
                        match_close
                    );
                    if (arrow < 0 || !token_equal(source, arrow, "=>")) break;
                    int64_t body_open = skip_trivia(
                        source,
                        token_end(source, arrow)
                    );
                    int64_t body_end = balanced_end(
                        source,
                        body_open,
                        "{",
                        "}"
                    );
                    if (body_end < 0) break;
                    int64_t open = skip_trivia(
                        source,
                        token_end(source, arm_cursor)
                    );
                    int64_t field = skip_trivia(
                        source,
                        token_end(source, open)
                    );
                    int64_t binding_start = -1;
                    char *pattern_binding_type = owned_text("");
                    if (
                        arm.kind == PATTERN_CONSTRUCTOR &&
                        field < function_close &&
                        strcmp(token_kind(source, field), "identifier") == 0 &&
                        !token_equal(source, field, "_")
                    ) {
                        binding_start = field;
                        free(pattern_binding_type);
                        pattern_binding_type = owned_text("Int");
                    } else if (arm.kind == PATTERN_NAME) {
                        char *pattern_name = token_copy(
                            source,
                            arm_cursor
                        );
                        char *constructor_owner = enum_constructor_owner(
                            source,
                            pattern_name
                        );
                        if (
                            constructor_owner[0] == '\0' &&
                            enum_binding_catchall_name(pattern_name)
                        ) {
                            int64_t scrutinee = skip_trivia(
                                source,
                                token_end(source, cursor)
                            );
                            if (
                                scrutinee < function_close &&
                                strcmp(
                                    token_kind(source, scrutinee),
                                    "identifier"
                                ) == 0
                            ) {
                                char *scrutinee_name = token_copy(
                                    source,
                                    scrutinee
                                );
                                int64_t scrutinee_scope_open =
                                    parent_block_open(
                                        source,
                                        function_open,
                                        scrutinee
                                    );
                                char *scrutinee_scope =
                                    hir_scope_id_for_open(
                                        hir.data,
                                        scrutinee_scope_open
                                    );
                                char *scrutinee_binding =
                                    hir_resolve_binding(
                                        hir.data,
                                        scrutinee_scope,
                                        scrutinee,
                                        scrutinee_name
                                    );
                                char *scrutinee_type = hir_binding_field(
                                    hir.data,
                                    scrutinee_binding,
                                    5
                                );
                                if (
                                    enum_constructor_count(
                                        source,
                                        scrutinee_type
                                    ) >= 0
                                ) {
                                    binding_start = arm_cursor;
                                    free(pattern_binding_type);
                                    pattern_binding_type = owned_text(
                                        scrutinee_type
                                    );
                                }
                                free(scrutinee_type);
                                free(scrutinee_binding);
                                free(scrutinee_scope);
                                free(scrutinee_name);
                            }
                        }
                        free(constructor_owner);
                        free(pattern_name);
                    }
                    if (binding_start >= 0) {
                        char *scope_id = hir_scope_id_for_open(
                            hir.data,
                            body_open
                        );
                        char *name_text = token_copy(
                            source,
                            binding_start
                        );
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
                                "error[E2S47]: duplicate binding `%s` in "
                                "lexical scope at byte %" PRId64
                                "; first declaration at byte %s",
                                name_text,
                                binding_start,
                                first_declaration
                            );
                            stage2_diagnostic_set(
                                "E2S47",
                                binding_start,
                                token_end(source, binding_start),
                                true,
                                error.data
                            );
                            stage2_diagnostic_affected(
                                STAGE2_DIAGNOSTIC_AFFECTED_BINDING,
                                binding_start,
                                token_end(source, binding_start)
                            );
                            {
                                int64_t first =
                                    decimal_value(first_declaration);
                                stage2_diagnostic_related(
                                    first,
                                    token_end(source, first),
                                    "first declaration"
                                );
                            }
                            stage2_diagnostic_remedy(2u);
                            free(name_text);
                            free(first_declaration);
                            free(scope_id);
                            free(pattern_binding_type);
                            free(hir.data);
                            return error.data;
                        }
                        free(first_declaration);
                        ++binding_count;
                        if (binding_count > 256) {
                            free(name_text);
                            free(scope_id);
                            free(pattern_binding_type);
                            return scope_hir_error(
                                &hir,
                                "lexical binding limit is 256 per function",
                                binding_start
                            );
                        }
                        buffer_format(
                            &hir,
                            "binding|%" PRId64 "|%s|%s|immutable|%s|copy|"
                            "initialized|%" PRId64 "|%" PRId64 "|%" PRId64
                            "\n",
                            next_binding_id++,
                            scope_id,
                            name_text,
                            pattern_binding_type,
                            binding_start,
                            token_end(source, binding_start),
                            token_end(source, binding_start)
                        );
                        stage2_scope_prefix_observe(&hir);
                        free(name_text);
                        free(scope_id);
                    }
                    free(pattern_binding_type);
                    arm_cursor = skip_trivia(source, body_end);
                    if (
                        arm_cursor < function_close &&
                        token_equal(source, arm_cursor, ",")
                    ) {
                        arm_cursor = skip_trivia(
                            source,
                            token_end(source, arm_cursor)
                        );
                    }
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
                bool lambda_token = lambda_declaration_syntax_token(
                    source,
                    function_open,
                    cursor
                );
                if (
                    !declaration_token && !initializer_token &&
                    !pattern_token && !lambda_token &&
                    !numeric_conversion_head(source, cursor) &&
                    !token_equal(source, cursor, "print") &&
                    !token_equal(source, cursor, "_")
                ) {
                    int64_t scope_open = lambda_scope_open(
                        source,
                        function_open,
                        cursor
                    );
                    if (scope_open < 0) {
                        scope_open = match_guard_scope_open(
                            source,
                            function_open,
                            cursor
                        );
                    }
                    if (scope_open < 0) {
                        scope_open = parent_block_open(
                            source,
                            function_open,
                            cursor
                        );
                    }
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
                        stage2_scope_prefix_observe(&hir);
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
                        stage2_scope_prefix_observe(&hir);
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
                        stage2_scope_prefix_observe(&hir);
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
                                stage2_diagnostic_set(
                                    "E2S35",
                                    cursor,
                                    token_end(source, cursor),
                                    true,
                                    message.data
                                );
                                {
                                    int64_t declaration =
                                        decimal_value(pending);
                                    stage2_diagnostic_related(
                                        declaration,
                                        token_end(source, declaration),
                                        "declaration"
                                    );
                                }
                                stage2_diagnostic_remedy(3u);
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
                                stage2_diagnostic_set(
                                    "E2S35",
                                    cursor,
                                    token_end(source, cursor),
                                    true,
                                    message.data
                                );
                                {
                                    int64_t declaration =
                                        decimal_value(escaped);
                                    stage2_diagnostic_related(
                                        declaration,
                                        token_end(source, declaration),
                                        "declaration"
                                    );
                                }
                                stage2_diagnostic_remedy(3u);
                                free(name);
                                free(scope_id);
                                free(binding_id);
                                free(owner);
                                free(escaped);
                                free(hir.data);
                                return message.data;
                            }
                            free(escaped);
                        /* A declared Core function passed as an argument is
                         * that function used as a value. It is not a lexical
                         * binding, so nothing resolves it here, and reporting
                         * it as unknown would make the only way to pass a
                         * named function an error. Anywhere but argument
                         * position it stays unknown, so a function name in
                         * arithmetic is still this diagnostic. */
                        if (
                            owner[0] == '\0' &&
                            !(function_arity(source, name) >= 0 &&
                              call_argument_position(source, cursor))
                        ) {
                            Buffer message;
                            buffer_init(&message);
                            buffer_format(
                                &message,
                                "error[E2S35]: unknown lexical binding `%s` "
                                "at byte %" PRId64,
                                name,
                                cursor
                            );
                            stage2_diagnostic_set(
                                "E2S35",
                                cursor,
                                token_end(source, cursor),
                                true,
                                message.data
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
    int64_t length = source_length(source);
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
                        stage2_diagnostic_set(
                            "E2S32",
                            cursor,
                            token_end(source, cursor),
                            true,
                            error.data
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
                            char *argument_type =
                                call_argument_expected_type(source, cursor);
                            char *return_type =
                                function_return_type_containing(
                                    source,
                                    cursor
                                );
                            bool enum_argument =
                                strcmp(argument_type, binding_type) == 0;
                            bool enum_return =
                                strcmp(previous, "return") == 0 &&
                                strcmp(return_type, binding_type) == 0;
                            free(argument_type);
                            free(return_type);
                            if (
                                !match_scrutinee &&
                                !enum_argument &&
                                !enum_return
                            ) {
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
                                stage2_diagnostic_set(
                                    "E2S32",
                                    cursor,
                                    token_end(source, cursor),
                                    true,
                                    error.data
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
                                char *argument_type =
                                    call_argument_expected_type(
                                        source,
                                        cursor
                                    );
                                char *return_type =
                                    function_return_type_containing(
                                        source,
                                        cursor
                                    );
                                bool enum_argument =
                                    constructor_owner[0] != '\0' &&
                                    strcmp(
                                        argument_type,
                                        constructor_owner
                                    ) == 0;
                                bool enum_return =
                                    constructor_owner[0] != '\0' &&
                                    strcmp(previous, "return") == 0 &&
                                    strcmp(
                                        return_type,
                                        constructor_owner
                                    ) == 0;
                                free(argument_type);
                                free(return_type);
                                if (
                                    constructor_owner[0] != '\0' &&
                                    !enum_argument &&
                                    !enum_return
                                ) {
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
                                    stage2_diagnostic_set(
                                        "E2S32",
                                        cursor,
                                        token_end(source, cursor),
                                        true,
                                        error.data
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
                if (constructor_named &&
                    (initializer_token || pattern_token)) {
                    char *constructor_owner = enum_constructor_owner(
                        source,
                        name
                    );
                    if (constructor_owner[0] != '\0') {
                        if (initializer_token) {
                            stage2_semantic_observe(
                                "call|constructor|%s|%" PRId64
                                "|%" PRId64 "|%s\n",
                                name,
                                cursor,
                                token_end(source, cursor),
                                constructor_owner
                            );
                        }
                        if (pattern_token) {
                            PatternSummary summary = pattern_summary(
                                source,
                                cursor
                            );
                            stage2_semantic_observe(
                                "pattern|constructor|%s|%" PRId64
                                "|%" PRId64 "|%s\n",
                                name,
                                cursor,
                                summary.end,
                                constructor_owner
                            );
                        }
                    }
                    free(constructor_owner);
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
    int64_t length = source_length(source);
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
    int64_t length = source_length(source);
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
    char *match_result_type = function_return_type_containing(
        source,
        function_open
    );
    bool match_returns_enum =
        enum_constructor_count(source, match_result_type) >= 0;
    free(match_result_type);
    const char *failure_result =
        is_main ? "1" :
        (match_returns_enum ? "KOFUN_ENUM_ZERO" : "0");
    while (arm_cursor < length && !token_equal(source, arm_cursor, "}")) {
        int64_t pattern_start = arm_cursor;
        PatternSummary pattern_summary_value = pattern_summary(
            source,
            pattern_start
        );
        char *pattern = token_copy(source, pattern_start);
        int64_t tag =
            pattern_summary_value.kind == PATTERN_NAME ||
            pattern_summary_value.kind == PATTERN_CONSTRUCTOR
                ? enum_constructor_index(source, enum_type, pattern)
                : -1;
        bool binding_catchall =
            pattern_summary_value.kind == PATTERN_NAME &&
            tag < 0 &&
            enum_binding_catchall_name(pattern);
        bool catchall =
            pattern_summary_value.kind == PATTERN_WILDCARD ||
            binding_catchall;
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
        /*
         * `-1` means the arm binds no payload name.  Keeping the decision as a
         * source offset rather than an owned id lets every validation path
         * below return without an extra free.
         */
        int64_t payload_name_start = -1;
        int64_t catchall_name_start =
            binding_catchall ? pattern_start : -1;
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
            if (
                pattern_summary_value.kind != PATTERN_NAME &&
                pattern_summary_value.kind != PATTERN_CONSTRUCTOR
            ) {
                free(pattern);
                return lower_enum_match_error(
                    &covered,
                    &dispatch,
                    "E2S32",
                    "enum pattern must name a constructor or `_`",
                    pattern_start
                );
            }
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
            int64_t arity = enum_constructor_payload_arity(
                source,
                enum_type,
                pattern
            );
            bool applied =
                pattern_summary_value.kind == PATTERN_CONSTRUCTOR;
            if (arity < 0) {
                Buffer message;
                buffer_init(&message);
                buffer_format(
                    &message,
                    "constructor `%s` of enum `%s` declares a payload outside "
                    "this Core slice; one `Int` field is supported",
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
            if (applied != (arity == 1)) {
                Buffer message;
                buffer_init(&message);
                buffer_format(
                    &message,
                    arity == 1 ?
                        "constructor pattern `%s` must bind its one `Int` "
                        "payload or use `_`" :
                        "constructor pattern `%s` takes no payload",
                    pattern
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
            if (applied) {
                int64_t open = skip_trivia(
                    source,
                    token_end(source, pattern_start)
                );
                int64_t field = skip_trivia(
                    source,
                    token_end(source, open)
                );
                int64_t close = field >= length ?
                    length :
                    skip_trivia(source, token_end(source, field));
                bool wildcard = token_equal(source, field, "_");
                bool named =
                    field < length &&
                    strcmp(token_kind(source, field), "identifier") == 0 &&
                    !wildcard;
                if (
                    (!wildcard && !named) ||
                    close >= length ||
                    !token_equal(source, close, ")")
                ) {
                    free(pattern);
                    return lower_enum_match_error(
                        &covered,
                        &dispatch,
                        "E2S32",
                        "constructor payload pattern must be one name or `_`",
                        field
                    );
                }
                if (named) payload_name_start = field;
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
                "kofun_match_value.tag == INT64_C(%" PRId64 ")",
                tag
            );
        }
        /*
         * The payload name is declared before the guard, not only before the
         * body, so `Present(value) if value > 5` reads the same local the arm
         * body reads.  The declaration is inside the arm's `if`, so it is only
         * in scope where the constructor actually matched.
         */
        Buffer payload_declaration;
        buffer_init(&payload_declaration);
        if (payload_name_start >= 0) {
            char *payload_id = hir_definition_id_at(hir, payload_name_start);
            if (payload_id[0] == '\0') {
                free(payload_id);
                free(payload_declaration.data);
                free(pattern_condition.data);
                free(arm_body);
                free(pattern);
                return lower_enum_match_error(
                    &covered,
                    &dispatch,
                    "E2S32",
                    "constructor payload binding is unresolved",
                    payload_name_start
                );
            }
            buffer_format(
                &payload_declaration,
                "            int64_t k_b%s = "
                "kofun_match_value.payload;\n"
                "            (void)k_b%s;\n",
                payload_id,
                payload_id
            );
            free(payload_id);
        } else if (catchall_name_start >= 0) {
            char *catchall_id = hir_definition_id_at(
                hir,
                catchall_name_start
            );
            if (catchall_id[0] == '\0') {
                free(catchall_id);
                free(payload_declaration.data);
                free(pattern_condition.data);
                free(arm_body);
                free(pattern);
                return lower_enum_match_error(
                    &covered,
                    &dispatch,
                    "E2S32",
                    "enum catch-all binding is unresolved",
                    catchall_name_start
                );
            }
            buffer_format(
                &payload_declaration,
                "            KofunEnumValue k_b%s = "
                "kofun_match_value;\n"
                "            (void)k_b%s;\n",
                catchall_id,
                catchall_id
            );
            free(catchall_id);
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
                "%s"
                "            if (kofun_match_guard) {\n"
                "%s"
                "                kofun_match_selected = true;\n"
                "            }\n"
                "        }\n",
                pattern_condition.data,
                payload_declaration.data,
                guard,
                arm_body
            );
            free(guard);
        } else {
            buffer_format(
                &dispatch,
                "        if (!kofun_match_selected && %s) {\n"
                "%s"
                "%s"
                "            kofun_match_selected = true;\n"
                "        }\n",
                pattern_condition.data,
                payload_declaration.data,
                arm_body
            );
            if (catchall) {
                seen_catchall = true;
            } else {
                buffer_append(&covered, pattern);
                buffer_append(&covered, "|");
            }
        }
        free(payload_declaration.data);
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
        "        KofunEnumValue kofun_match_value = k_b%s;\n"
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
    stage2_diagnostic_set(
        "E2S22",
        cursor,
        cursor,
        true,
        error.data
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
    int64_t length = source_length(source);
    Buffer emitted;
    buffer_init(&emitted);
    int64_t cursor = skip_trivia(source, token_end(source, open));
    bool returned = false;
    char *body_result_type = function_return_type_containing(
        source,
        function_open
    );
    bool returns_enum =
        enum_constructor_count(source, body_result_type) >= 0;
    free(body_result_type);
    const char *failure_result =
        is_main ? "1" : (returns_enum ? "KOFUN_ENUM_ZERO" : "0");
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
                    value_start < length &&
                    strcmp(token_kind(source, value_start), "identifier") == 0
                ) {
                    char *initializer_name = token_copy(
                        source,
                        value_start
                    );
                    int64_t initializer_open = skip_trivia(
                        source,
                        token_end(source, value_start)
                    );
                    char *initializer_result = function_return_type(
                        source,
                        initializer_name
                    );
                    bool enum_call =
                        initializer_open < length &&
                        token_equal(source, initializer_open, "(") &&
                        strcmp(initializer_result, enum_type) == 0;
                    free(initializer_result);
                    free(initializer_name);
                    if (enum_call) {
                        int64_t value_end = expression_end(
                            source,
                            value_start
                        );
                        char *value = emit_enum_value(
                            source,
                            hir,
                            value_start,
                            value_end,
                            enum_type
                        );
                        if (strncmp(value, "error[", 6) == 0) {
                            free(enum_type);
                            free(binding_id);
                            free(name);
                            free(emitted.data);
                            return value;
                        }
                        buffer_format(
                            &emitted,
                            "    KofunEnumValue k_b%s = %s;\n"
                            "    if (kofun_failed) return %s;\n",
                            binding_id,
                            value,
                            failure_result
                        );
                        free(value);
                        free(enum_type);
                        free(binding_id);
                        free(name);
                        cursor = skip_trivia(source, value_end);
                        continue;
                    }
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
                int64_t arity = enum_constructor_payload_arity(
                    source,
                    enum_type,
                    constructor
                );
                int64_t after_constructor = skip_trivia(
                    source,
                    token_end(source, value_start)
                );
                bool applied = after_constructor < length &&
                               token_equal(source, after_constructor, "(");
                if (arity < 0) {
                    Buffer message;
                    buffer_init(&message);
                    buffer_format(
                        &message,
                        "constructor `%s` of enum `%s` declares a payload "
                        "outside this Core slice; one `Int` field is "
                        "supported",
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
                if (applied != (arity == 1)) {
                    Buffer message;
                    buffer_init(&message);
                    buffer_format(
                        &message,
                        arity == 1 ?
                            "constructor `%s` takes one `Int` payload" :
                            "constructor `%s` takes no payload",
                        constructor
                    );
                    free(constructor);
                    free(enum_type);
                    free(binding_id);
                    free(name);
                    free(emitted.data);
                    char *error = lower_error(
                        "E2S32",
                        message.data,
                        applied ? after_constructor : value_start
                    );
                    free(message.data);
                    return error;
                }
                /*
                 * Every concrete enum binding holds a tag and one payload
                 * slot, whether or not its constructor carries a field.  A
                 * match arm can then read the payload without first proving
                 * which constructor produced the value, and the two locals
                 * stay in step through every later assignment.
                 */
                int64_t constructor_end = token_end(source, value_start);
                if (arity == 1) {
                    int64_t payload_start = skip_trivia(
                        source,
                        token_end(source, after_constructor)
                    );
                    int64_t payload_end = expression_end(
                        source,
                        payload_start
                    );
                    int64_t close = payload_end < 0 ?
                        -1 :
                        skip_trivia(source, payload_end);
                    if (
                        payload_end < 0 ||
                        close >= length ||
                        !token_equal(source, close, ")")
                    ) {
                        free(constructor);
                        free(enum_type);
                        free(binding_id);
                        free(name);
                        free(emitted.data);
                        return lower_error(
                            "E2S32",
                            "concrete enum payload must be one Int expression",
                            payload_start
                        );
                    }
                    char *payload = emit_expression(
                        source,
                        hir,
                        payload_start,
                        payload_end
                    );
                    buffer_format(
                        &emitted,
                        "    KofunEnumValue k_b%s = "
                        "{INT64_C(%" PRId64 "), %s};\n"
                        "    if (kofun_failed) return %s;\n",
                        binding_id,
                        tag,
                        payload,
                        failure_result
                    );
                    free(payload);
                    constructor_end = token_end(source, close);
                } else {
                    buffer_format(
                        &emitted,
                        "    KofunEnumValue k_b%s = "
                        "{INT64_C(%" PRId64 "), INT64_C(0)};\n",
                        binding_id,
                        tag
                    );
                }
                free(constructor);
                free(enum_type);
                free(name);
                free(binding_id);
                cursor = skip_trivia(source, constructor_end);
                continue;
            }
            /*
             * A lambda binding names a lifted top-level function rather than
             * holding an `int64_t`, so this statement emits nothing. Without
             * this the initializer reaches the Int expression grammar and is
             * rejected as `E2S12`, which is what #703 measured.
             */
            int64_t lambda_open = lambda_initializer_open(source, value_start);
            if (lambda_open >= 0) {
                free(name);
                free(binding_id);
                cursor = skip_trivia(
                    source,
                    lambda_parameters_end(source, -1, lambda_open)
                );
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
            int64_t statement_start = cursor;
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
            int64_t statement_end = token_end(source, branch_close);
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
                statement_end = token_end(source, else_close);
                cursor = skip_trivia(source, else_close);
            }
            buffer_append(&emitted, "\n    }\n");
            stage2_semantic_observe(
                "control|if|%" PRId64 "|%" PRId64 "|Unit|%" PRId64
                "|%" PRId64 "\n",
                statement_start,
                statement_end,
                condition_start,
                condition_close
            );
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
                stage2_semantic_observe(
                    "control|match|%" PRId64 "|%" PRId64
                    "|Unit|%" PRId64 "|%" PRId64 "\n",
                    match_start,
                    match_end,
                    value_start,
                    token_end(source, value_start)
                );
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
            stage2_semantic_observe(
                "control|match|%" PRId64 "|%" PRId64 "|Unit|%" PRId64
                "|%" PRId64 "\n",
                match_start,
                token_end(source, arm_cursor),
                value_start,
                value_end
            );
            cursor = skip_trivia(source, token_end(source, arm_cursor));
            }
        } else if (token_equal(source, cursor, "return")) {
            int64_t value_start = skip_trivia(source, token_end(source, cursor));
            if (returns_enum) {
                int64_t value_end = expression_end(source, value_start);
                if (
                    value_end < 0 ||
                    value_start >= length ||
                    token_equal(source, value_start, "}")
                ) {
                    free(emitted.data);
                    return lower_error(
                        "E2S12",
                        "concrete enum return requires one value",
                        value_start
                    );
                }
                char *result_type = function_return_type_containing(
                    source,
                    function_open
                );
                char *value = emit_enum_value(
                    source,
                    hir,
                    value_start,
                    value_end,
                    result_type
                );
                free(result_type);
                if (strncmp(value, "error[", 6) == 0) {
                    free(emitted.data);
                    return value;
                }
                buffer_format(
                    &emitted,
                    "    {\n"
                    "        KofunEnumValue kofun_result = %s;\n"
                    "        if (kofun_failed) return KOFUN_ENUM_ZERO;\n"
                    "        return kofun_result;\n"
                    "    }\n",
                    value
                );
                free(value);
                cursor = skip_trivia(source, value_end);
            } else if (
                value_start < length &&
                token_equal(source, value_start, "}")
            ) {
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
            returns_enum ?
                "Core function may complete without returning its enum" :
                "Core function may complete without returning Int",
            open
        );
    }
    if (!returned && append_default) {
        buffer_append(&emitted, "    return 0;\n");
    }
    return emitted.data;
}

/*
 * Lifts every lambda binding to a top-level `kofun_lambda_<binding>` and
 * appends its prototype and body to the module being built.
 *
 * Lifting rather than a function value is what keeps this inside the frozen
 * profile: Stage 2 lowers every value to `int64_t` and has no function type,
 * no function pointer and no indirect call, so a lambda that stayed a value
 * would need all three. A lifted lambda is an ordinary Core function, and its
 * body lowers through the same expression emitter as any other, because the
 * scope HIR already binds the parameters.
 */
static char *emit_lifted_lambdas(
    const char *source,
    const char *hir,
    Buffer *prototypes,
    Buffer *bodies
) {
    int64_t length = source_length(source);
    int64_t cursor = skip_trivia(source, 0);
    while (cursor < length) {
        if (!token_equal(source, cursor, "let")) {
            cursor = skip_trivia(source, token_end(source, cursor));
            continue;
        }
        int64_t name_start = skip_trivia(source, token_end(source, cursor));
        if (token_equal(source, name_start, "mut")) {
            name_start = skip_trivia(source, token_end(source, name_start));
        }
        if (
            name_start >= length ||
            strcmp(token_kind(source, name_start), "identifier") != 0
        ) {
            cursor = skip_trivia(source, token_end(source, cursor));
            continue;
        }
        int64_t equals = skip_trivia(source, token_end(source, name_start));
        if (equals < length && token_equal(source, equals, ":")) {
            int64_t annotation = skip_trivia(
                source,
                token_end(source, equals)
            );
            equals = skip_trivia(source, token_end(source, annotation));
        }
        if (equals >= length || !token_equal(source, equals, "=")) {
            cursor = skip_trivia(source, token_end(source, cursor));
            continue;
        }
        int64_t open = lambda_initializer_open(
            source,
            skip_trivia(source, token_end(source, equals))
        );
        if (open < 0) {
            cursor = skip_trivia(source, token_end(source, cursor));
            continue;
        }

        char *binding_id = hir_definition_id_at(hir, name_start);
        Buffer signature;
        buffer_init(&signature);
        int64_t parameters = 0;
        /* The bare form is keyed by its single parameter and has no list, so
         * its "close" is the end of that identifier — which is also where the
         * `=>` search below starts. Taking `balanced_end` here would yield -1
         * and walk the source from a negative offset. */
        bool bare_lambda = !token_equal(source, open, "(");
        int64_t close = bare_lambda
            ? token_end(source, open)
            : balanced_end(source, open, "(", ")");
        int64_t parameter = bare_lambda
            ? open
            : skip_trivia(source, token_end(source, open));
        while (parameter < close) {
            if (strcmp(token_kind(source, parameter), "identifier") != 0) {
                break;
            }
            char *parameter_id = hir_definition_id_at(hir, parameter);
            if (parameters > 0) buffer_append(&signature, ", ");
            buffer_format(&signature, "int64_t k_b%s", parameter_id);
            free(parameter_id);
            ++parameters;
            int64_t after = skip_trivia(source, token_end(source, parameter));
            if (after < close && token_equal(source, after, ":")) {
                int64_t annotation = skip_trivia(
                    source,
                    token_end(source, after)
                );
                after = skip_trivia(source, token_end(source, annotation));
            }
            if (after < close && token_equal(source, after, ",")) {
                after = skip_trivia(source, token_end(source, after));
            }
            parameter = after;
        }
        char *captures = lambda_captures(source, hir, open);
        append_captures(&signature, captures, parameters, "int64_t ");
        free(captures);
        const char *c_parameters =
            signature.length == 0 ? "void" : signature.data;

        int64_t arrow = skip_trivia(source, close);
        int64_t body_start = skip_trivia(source, token_end(source, arrow));
        int64_t body_end = lambda_parameters_end(source, -1, open);
        char *value = emit_expression(source, hir, body_start, body_end);
        if (strncmp(value, "error[", 6) == 0) {
            free(signature.data);
            free(binding_id);
            return value;
        }
        buffer_format(
            prototypes,
            "static int64_t kofun_lambda_%s(%s);\n",
            binding_id,
            c_parameters
        );
        buffer_format(
            bodies,
            "static int64_t kofun_lambda_%s(%s) {\n"
            "    {\n"
            "        int64_t kofun_result = %s;\n"
            "        if (kofun_failed) return 0;\n"
            "        return kofun_result;\n"
            "    }\n"
            "}\n",
            binding_id,
            c_parameters,
            value
        );
        free(value);
        free(signature.data);
        free(binding_id);
        cursor = skip_trivia(source, body_end);
    }
    return owned_text("ok");
}

/*
 * Whether the arrow lambda keyed at the current token is written directly in
 * argument position rather than as a `let` initializer.
 *
 * `lambda_parameters_end` documents the four tokens that may precede a
 * parameter list: `fn`, `=`, `,` and `(`. `=` is the initializer position and
 * the other two are argument position, so the preceding token decides — after
 * stepping over `fn`, which may sit in front of either.
 */
static bool argument_position_lambda(
    const char *source,
    int64_t previous,
    int64_t before_previous
) {
    int64_t effective = previous;
    if (effective >= 0 && token_equal(source, effective, "fn")) {
        effective = before_previous;
    }
    if (effective < 0) return false;
    return token_equal(source, effective, "(") ||
           token_equal(source, effective, ",");
}

/*
 * Lifts every arrow lambda written in argument position to a top-level
 * `kofun_lambda_at<offset>`.
 *
 * This walk visits every token rather than jumping over a lambda body, so a
 * lambda nested inside another lambda's argument is lifted too. The `let`
 * initializer walk cannot be reused: an anonymous argument has no binding id
 * to key on, and `emit_lifted_lambdas` keys on exactly that.
 */
static char *emit_lifted_argument_lambdas(
    const char *source,
    const char *hir,
    Buffer *prototypes,
    Buffer *bodies
) {
    int64_t length = source_length(source);
    int64_t cursor = skip_trivia(source, 0);
    int64_t previous = -1;
    int64_t before_previous = -1;
    while (cursor < length) {
        int64_t close = lambda_parameters_end(source, previous, cursor);
        if (
            close >= 0 &&
            argument_position_lambda(source, previous, before_previous)
        ) {
            Buffer signature;
            buffer_init(&signature);
            int64_t parameters = 0;
            bool bare_lambda = !token_equal(source, cursor, "(");
            int64_t parameters_close = bare_lambda
                ? token_end(source, cursor)
                : balanced_end(source, cursor, "(", ")");
            int64_t parameter = bare_lambda
                ? cursor
                : skip_trivia(source, token_end(source, cursor));
            while (parameter < parameters_close) {
                if (strcmp(token_kind(source, parameter), "identifier") != 0) {
                    break;
                }
                char *parameter_id = hir_definition_id_at(hir, parameter);
                if (parameters > 0) buffer_append(&signature, ", ");
                buffer_format(&signature, "int64_t k_b%s", parameter_id);
                free(parameter_id);
                ++parameters;
                int64_t after = skip_trivia(
                    source,
                    token_end(source, parameter)
                );
                if (
                    after < parameters_close && token_equal(source, after, ":")
                ) {
                    int64_t annotation = skip_trivia(
                        source,
                        token_end(source, after)
                    );
                    after = skip_trivia(source, token_end(source, annotation));
                }
                if (
                    after < parameters_close && token_equal(source, after, ",")
                ) {
                    after = skip_trivia(source, token_end(source, after));
                }
                parameter = after;
            }
            const char *c_parameters =
                signature.length == 0 ? "void" : signature.data;
            int64_t arrow = skip_trivia(source, parameters_close);
            int64_t body_start = skip_trivia(source, token_end(source, arrow));
            char *value = emit_expression(source, hir, body_start, close);
            if (strncmp(value, "error[", 6) == 0) {
                free(signature.data);
                return value;
            }
            char *name = argument_lambda_name(cursor);
            buffer_format(
                prototypes,
                "static int64_t %s(%s);\n",
                name,
                c_parameters
            );
            buffer_format(
                bodies,
                "static int64_t %s(%s) {\n"
                "    {\n"
                "        int64_t kofun_result = %s;\n"
                "        if (kofun_failed) return 0;\n"
                "        return kofun_result;\n"
                "    }\n"
                "}\n",
                name,
                c_parameters,
                value
            );
            free(name);
            free(value);
            free(signature.data);
        }
        before_previous = previous;
        previous = cursor;
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return owned_text("ok");
}

/*
 * A capturing lambda cannot be a function value. Lifting passes each capture
 * as a trailing `int64_t` parameter, so the lifted function's C type is wider
 * than the callable type the parameter declares and its address is not that
 * callable. Closure conversion — an environment travelling with the code — is
 * #116 and #370, and #703 puts it out of scope, so this refuses rather than
 * lowering something whose observations would not match.
 */
static char *validate_argument_lambda_captures(
    const char *source,
    const char *hir
) {
    int64_t length = source_length(source);
    int64_t cursor = skip_trivia(source, 0);
    int64_t previous = -1;
    int64_t before_previous = -1;
    while (cursor < length) {
        int64_t close = lambda_parameters_end(source, previous, cursor);
        if (
            close >= 0 &&
            argument_position_lambda(source, previous, before_previous)
        ) {
            char *captures = lambda_captures(source, hir, cursor);
            bool captured = captures[0] != '\0';
            free(captures);
            if (captured) {
                Buffer message;
                buffer_init(&message);
                buffer_format(
                    &message,
                    "error[E2S96]: lambda argument at byte %" PRId64
                    " captures an enclosing binding; pass a lambda that reads "
                    "only its parameters",
                    cursor
                );
                stage2_diagnostic_set(
                    "E2S96",
                    cursor,
                    token_end(source, cursor),
                    true,
                    message.data
                );
                return message.data;
            }
        }
        before_previous = previous;
        previous = cursor;
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return owned_text("ok");
}

static const char *numeric_name(const char *name) {
    if (name == NULL) return "";
    if (strcmp(name, "Int") == 0) return "Int";
    if (strcmp(name, "Decimal") == 0) return "Decimal";
    if (strcmp(name, "Float") == 0) return "Float";
    return "";
}

/*
 * True when `cursor` is the type name heading a `Type.member` path.
 *
 * This is the one place the scope walk must not treat a numeric type name as a
 * binding use. `Decimal` in `Decimal.from_int(3)` names a type, not a value, so
 * resolving it would report `E2S35: unknown lexical binding` — which is what it
 * did before the conversions existed.
 */
static bool numeric_conversion_head(const char *source, int64_t cursor) {
    char *name = token_copy(source, cursor);
    bool numeric = numeric_name(name)[0] != '\0';
    free(name);
    if (!numeric) return false;
    int64_t length = source_length(source);
    int64_t dot = skip_trivia(source, token_end(source, cursor));
    return dot < length && token_equal(source, dot, ".");
}

/*
 * The `Type.member` conversion path beginning at `cursor`, or "" when there is
 * not one. The member must be called: a bare `Decimal.from_int` is a value of a
 * type the language does not have, so only the call form is recognised.
 *
 * The caller owns the result.
 */
static char *numeric_conversion_at(const char *source, int64_t cursor) {
    if (!numeric_conversion_head(source, cursor)) return owned_text("");
    int64_t length = source_length(source);
    int64_t dot = skip_trivia(source, token_end(source, cursor));
    int64_t member = skip_trivia(source, token_end(source, dot));
    if (
        member >= length ||
        strcmp(token_kind(source, member), "identifier") != 0
    ) {
        return owned_text("");
    }
    int64_t open = skip_trivia(source, token_end(source, member));
    if (open >= length || !token_equal(source, open, "(")) {
        return owned_text("");
    }
    char *head = token_copy(source, cursor);
    char *tail = token_copy(source, member);
    Buffer path;
    buffer_init(&path);
    buffer_format(&path, "%s.%s", head, tail);
    free(head);
    free(tail);
    return path.data;
}

/*
 * The result type of a named conversion, or "" when the path is not one.
 *
 * `docs/DECIMAL.md` fixes these three names. `Decimal.from_int` is the only one
 * that can be written today; the other two are recognised here so that
 * `validate_numeric_conversions` can reject them for the right reason rather
 * than calling them unknown.
 */
static const char *numeric_conversion_result(const char *conversion) {
    if (
        strcmp(conversion, "Decimal.from_int") == 0 ||
        strcmp(conversion, "Decimal.from_float") == 0
    ) {
        return "Decimal";
    }
    if (strcmp(conversion, "Float.from_decimal") == 0) return "Float";
    return "";
}

/*
 * The conversion that brings two mixed operand types together, or "" when the
 * pair has none.
 *
 * The pair is unordered because each name works from either side: `1 + 1.5` and
 * `1.5 + 1` are both fixed by converting the Int with `Decimal.from_int`, and
 * which operand it wraps is visible in the source.
 *
 * `Int` and `Float` return "" because `docs/DECIMAL.md` defines no conversion
 * between them in either direction. That is a real gap in the conversion set,
 * not an oversight here — Int to binary64 is exact only below 2^53 and so needs
 * the same policy argument the other inexact conversions do. Saying so beats
 * naming a function that does not exist.
 */
static const char *numeric_conversion_between(
    const char *left,
    const char *right
) {
    if (
        (strcmp(left, "Int") == 0 && strcmp(right, "Decimal") == 0) ||
        (strcmp(left, "Decimal") == 0 && strcmp(right, "Int") == 0)
    ) {
        return "Decimal.from_int";
    }
    if (
        (strcmp(left, "Decimal") == 0 && strcmp(right, "Float") == 0) ||
        (strcmp(left, "Float") == 0 && strcmp(right, "Decimal") == 0)
    ) {
        return "Float.from_decimal";
    }
    return "";
}

static bool arithmetic_operator_at(const char *source, int64_t cursor) {
    return token_equal(source, cursor, "+") ||
           token_equal(source, cursor, "-") ||
           token_equal(source, cursor, "*") ||
           token_equal(source, cursor, "//") ||
           token_equal(source, cursor, "%") ||
           token_equal(source, cursor, "**");
}

/*
 * The numeric type of the *primary* at `start`, or "" when it is not one of
 * the three numeric types.
 *
 * Slice 3 of #710 needs the type of one operand, which `initializer_type`
 * cannot give: that function scans the whole initializer line and returns
 * `Bool` the moment it sees a comparison, so typing the `1` in `1 + 2 < 3`
 * through it yields `Bool`. This looks at the primary and nothing else.
 *
 * "" rather than a default is deliberate. `Text`, `Bool` and unresolved names
 * are not numeric operands, and the mixed-arithmetic check must skip them
 * instead of inventing an `Int` and reporting a mismatch that is not there.
 *
 * The returned pointer is a static string, never owned.
 */
static const char *numeric_primary_type(
    const char *source,
    const char *hir,
    int64_t function_open,
    int64_t start
) {
    int64_t length = source_length(source);
    int64_t cursor = skip_trivia(source, start);
    if (cursor >= length) return "";
    /* Unary sign and a parenthesised group both delegate to what follows. */
    if (
        token_equal(source, cursor, "-") ||
        token_equal(source, cursor, "+") ||
        token_equal(source, cursor, "(")
    ) {
        return numeric_primary_type(
            source,
            hir,
            function_open,
            skip_trivia(source, token_end(source, cursor))
        );
    }
    /* A named conversion is typed by its destination, which is what makes
     * `Decimal.from_int(n) + 1.5` a same-type expression rather than a mix. */
    {
        char *conversion = numeric_conversion_at(source, cursor);
        const char *converted = numeric_conversion_result(conversion);
        free(conversion);
        if (converted[0] != '\0') return converted;
    }
    const char *kind = token_kind(source, cursor);
    if (strcmp(kind, "integer") == 0) return "Int";
    if (strcmp(kind, "decimal") == 0) return "Decimal";
    if (strcmp(kind, "float") == 0) return "Float";
    if (strcmp(kind, "identifier") != 0) return "";

    char *name = token_copy(source, cursor);
    int64_t open = skip_trivia(source, token_end(source, cursor));
    const char *result = "";
    if (open < length && token_equal(source, open, "(")) {
        char *declared = function_return_type(source, name);
        if (declared[0] != '\0') {
            result = numeric_name(declared);
        } else {
            result = numeric_name(builtin_return_type(name));
        }
        free(declared);
        free(name);
        return result;
    }
    int64_t scope_open = parent_block_open(source, function_open, cursor);
    char *scope_id = hir_scope_id_for_open(hir, scope_open);
    char *binding_id = hir_resolve_binding(hir, scope_id, cursor, name);
    free(scope_id);
    if (binding_id[0] != '\0') {
        char *binding_type = hir_binding_field(hir, binding_id, 5);
        result = numeric_name(binding_type);
        free(binding_type);
    }
    free(binding_id);
    free(name);
    return result;
}

/*
 * `Int`, `Decimal` and `Float` never receive implicit promotion (#710 frozen
 * decision 4), so an arithmetic expression mixing two of them is a type error
 * rather than a conversion.
 *
 * Both operand orders are checked by construction: the walk types the primary
 * before each operator and the primary after it, so a rule written for one
 * side only cannot pass. That matters — a promotion bug usually appears on one
 * side.
 */
static char *validate_numeric_operand_types(
    const char *source,
    const char *hir
) {
    int64_t length = source_length(source);
    int64_t function_start = next_function_start(source, 0);
    while (function_start < length) {
        int64_t function_close = function_end(source, function_start);
        int64_t parameters = parameter_open(source, function_start);
        int64_t function_open = parameters >= 0
            ? balanced_end(source, parameters, "(", ")")
            : -1;
        if (function_open >= 0) {
            int64_t cursor = skip_trivia(source, function_open);
            int64_t last_primary = -1;
            while (cursor < function_close) {
                /* A conversion is one primary and its argument is not an
                 * operand of the surrounding expression. Without this skip the
                 * walk stops at the inner literal: `Decimal.from_int(1) + 1`
                 * would read as Int + Int and be accepted, and
                 * `Decimal.from_int(1) + 1.5` would read as Int + Decimal and
                 * be rejected. Both are wrong, and in opposite directions. */
                int64_t advance = -1;
                char *primary_conversion = numeric_conversion_at(
                    source,
                    cursor
                );
                bool is_conversion = primary_conversion[0] != '\0';
                free(primary_conversion);
                if (is_conversion) {
                    last_primary = cursor;
                    int64_t dot = skip_trivia(
                        source,
                        token_end(source, cursor)
                    );
                    int64_t member = skip_trivia(
                        source,
                        token_end(source, dot)
                    );
                    int64_t open = skip_trivia(
                        source,
                        token_end(source, member)
                    );
                    int64_t close = balanced_end(source, open, "(", ")");
                    if (close > 0) advance = skip_trivia(source, close);
                } else if (arithmetic_operator_at(source, cursor) &&
                    last_primary >= 0) {
                    int64_t right = skip_trivia(
                        source,
                        token_end(source, cursor)
                    );
                    const char *left_type = numeric_primary_type(
                        source, hir, function_open, last_primary);
                    const char *right_type = numeric_primary_type(
                        source, hir, function_open, right);
                    if (
                        left_type[0] != '\0' && right_type[0] != '\0' &&
                        strcmp(left_type, right_type) != 0
                    ) {
                        char *operator_text = token_copy(source, cursor);
                        const char *remedy = numeric_conversion_between(
                            left_type,
                            right_type
                        );
                        char advice[80];
                        if (remedy[0] != '\0') {
                            snprintf(
                                advice,
                                sizeof advice,
                                "write %s(...)",
                                remedy
                            );
                        } else {
                            snprintf(
                                advice,
                                sizeof advice,
                                "no conversion between them exists"
                            );
                        }
                        Buffer message;
                        buffer_init(&message);
                        buffer_format(
                            &message,
                            "error[E2S100]: operator `%s` mixes %s and %s "
                            "at byte %" PRId64 "; %s",
                            operator_text,
                            left_type,
                            right_type,
                            cursor,
                            advice
                        );
                        free(operator_text);
                        stage2_diagnostic_set(
                            "E2S100",
                            cursor,
                            token_end(source, cursor),
                            true,
                            message.data
                        );
                        return message.data;
                    }
                    last_primary = right;
                } else {
                    const char *kind = token_kind(source, cursor);
                    if (
                        strcmp(kind, "integer") == 0 ||
                        strcmp(kind, "decimal") == 0 ||
                        strcmp(kind, "float") == 0 ||
                        strcmp(kind, "identifier") == 0
                    ) {
                        last_primary = cursor;
                    }
                }
                if (advance >= 0) {
                    cursor = advance;
                } else {
                    cursor = skip_trivia(source, token_end(source, cursor));
                }
            }
        }
        function_start = next_function_start(source, function_close);
    }
    return owned_text("ok");
}

/*
 * A numeric annotation and its initializer must name the same type.
 *
 * #710 frozen decision 4 removes implicit promotion *in both directions*, so
 * `let x: Decimal = 1` is exactly as wrong as `let x: Int = 1.5`. A checker
 * that rejected only the narrowing direction would still be promoting, just
 * quietly and one way — which is the failure this decision exists to prevent.
 *
 * The initializer is typed through `numeric_primary_type`, not
 * `initializer_type`. That matters for what is *not* reported: the former
 * answers "" for Text, Bool and unresolved names, while the latter falls back
 * to `Int`, and an `Int` invented there would report a mismatch against every
 * non-numeric annotation in the corpus.
 *
 * Mixed arithmetic is already rejected before this runs, so typing the first
 * primary types the whole initializer: what reaches here is homogeneous.
 */
static char *validate_numeric_annotations(
    const char *source,
    const char *hir
) {
    int64_t length = source_length(source);
    int64_t function_start = next_function_start(source, 0);
    while (function_start < length) {
        int64_t function_close = function_end(source, function_start);
        int64_t parameters = parameter_open(source, function_start);
        int64_t function_open = parameters >= 0
            ? balanced_end(source, parameters, "(", ")")
            : -1;
        if (function_open >= 0) {
            int64_t cursor = skip_trivia(source, function_open);
            while (cursor < function_close) {
                if (token_equal(source, cursor, "let")) {
                    int64_t name = skip_trivia(
                        source,
                        token_end(source, cursor)
                    );
                    if (token_equal(source, name, "mut")) {
                        name = skip_trivia(source, token_end(source, name));
                    }
                    int64_t colon = skip_trivia(
                        source,
                        token_end(source, name)
                    );
                    if (token_equal(source, colon, ":")) {
                        int64_t annotation = skip_trivia(
                            source,
                            token_end(source, colon)
                        );
                        char *annotation_text = token_copy(source, annotation);
                        const char *declared = numeric_name(annotation_text);
                        free(annotation_text);
                        int64_t assign = skip_trivia(
                            source,
                            token_end(source, annotation)
                        );
                        if (
                            declared[0] != '\0' &&
                            token_equal(source, assign, "=")
                        ) {
                            int64_t initializer = skip_trivia(
                                source,
                                token_end(source, assign)
                            );
                            const char *actual = "";
                            if (!value_control(source, initializer)) {
                                actual = numeric_primary_type(
                                    source,
                                    hir,
                                    function_open,
                                    initializer
                                );
                            }
                            if (
                                actual[0] != '\0' &&
                                strcmp(actual, declared) != 0
                            ) {
                                char *binding = token_copy(source, name);
                                Buffer message;
                                buffer_init(&message);
                                buffer_format(
                                    &message,
                                    "error[E2S101]: binding `%s` is %s but "
                                    "its value is %s at byte %" PRId64
                                    "; convert explicitly",
                                    binding,
                                    declared,
                                    actual,
                                    initializer
                                );
                                free(binding);
                                stage2_diagnostic_set(
                                    "E2S101",
                                    initializer,
                                    token_end(source, initializer),
                                    true,
                                    message.data
                                );
                                return message.data;
                            }
                        }
                    }
                }
                cursor = skip_trivia(source, token_end(source, cursor));
            }
        }
        function_start = next_function_start(source, function_close);
    }
    return owned_text("ok");
}

/*
 * The three named conversions of `docs/DECIMAL.md`, and what each one costs.
 *
 * Conversions are named and explicit because there is no implicit promotion.
 * But naming a conversion is not enough to make it writable: two of the three
 * cross the decimal/binary boundary and cannot be exact, and `docs/DECIMAL.md`
 * forbids any ambient rounding context that would let the compiler pick a mode
 * on the programmer's behalf.
 *
 * So `Decimal.from_int` is accepted — Int to Decimal is exact for every input,
 * and needs no mode — while `Float.from_decimal` and `Decimal.from_float` are
 * rejected until slice 5 gives them the rounding mode and policy arguments they
 * require. Rejecting them is the honest outcome: accepting either one today
 * would mean choosing a rounding mode silently, which is the single thing the
 * frozen decisions rule out.
 *
 * The refusal for a *valid* conversion comes last, so a program with both a
 * wrong conversion and a right one reports the wrong one.
 *
 * Note for slice 4: lifting the E2S104 refusal exposes `from_int(` to
 * `validate_core_calls`, which will call it an unknown Core function. Nothing
 * reaches that path today because this refusal short-circuits first.
 */
static char *validate_numeric_conversions(
    const char *source,
    const char *hir
) {
    int64_t length = source_length(source);
    int64_t function_start = next_function_start(source, 0);
    int64_t valid_conversion = -1;
    while (function_start < length) {
        int64_t function_close = function_end(source, function_start);
        int64_t parameters = parameter_open(source, function_start);
        int64_t function_open = parameters >= 0
            ? balanced_end(source, parameters, "(", ")")
            : -1;
        if (function_open >= 0) {
            int64_t cursor = skip_trivia(source, function_open);
            while (cursor < function_close) {
                if (numeric_conversion_head(source, cursor)) {
                    char *conversion = numeric_conversion_at(source, cursor);
                    Buffer message;
                    buffer_init(&message);
                    const char *code = NULL;
                    int64_t span = cursor;
                    /* The messages stay short on purpose: the member name is
                     * unbounded and the semantic producer holds a diagnostic
                     * in 160 bytes, so a long name plus a long tail truncates
                     * on one side of the gate only. */
                    if (conversion[0] == '\0') {
                        char *head = token_copy(source, cursor);
                        buffer_format(
                            &message,
                            "error[E2S102]: `%s` has only conversions at byte "
                            "%" PRId64 "; write `Decimal.from_int(value)`",
                            head,
                            cursor
                        );
                        free(head);
                        code = "E2S102";
                    } else if (
                        numeric_conversion_result(conversion)[0] == '\0'
                    ) {
                        buffer_format(
                            &message,
                            "error[E2S102]: unknown conversion `%s` at byte "
                            "%" PRId64 "; known: Decimal.from_int, "
                            "Decimal.from_float, Float.from_decimal",
                            conversion,
                            cursor
                        );
                        code = "E2S102";
                    } else if (strcmp(conversion, "Decimal.from_int") != 0) {
                        buffer_format(
                            &message,
                            "error[E2S103]: `%s` cannot be exact at byte "
                            "%" PRId64 "; it needs a rounding mode "
                            "(#710 slice 5)",
                            conversion,
                            cursor
                        );
                        code = "E2S103";
                    } else {
                        int64_t dot = skip_trivia(
                            source,
                            token_end(source, cursor)
                        );
                        int64_t member = skip_trivia(
                            source,
                            token_end(source, dot)
                        );
                        int64_t open = skip_trivia(
                            source,
                            token_end(source, member)
                        );
                        int64_t actual = call_arity(source, open);
                        if (actual != 1) {
                            buffer_format(
                                &message,
                                "error[E2S17]: Core function `%s` expects 1 "
                                "arguments, got %" PRId64 " at byte %" PRId64,
                                conversion,
                                actual,
                                cursor
                            );
                            code = "E2S17";
                        } else {
                            int64_t argument = skip_trivia(
                                source,
                                token_end(source, open)
                            );
                            const char *argument_type = numeric_primary_type(
                                source,
                                hir,
                                function_open,
                                argument
                            );
                            if (
                                argument_type[0] != '\0' &&
                                strcmp(argument_type, "Int") != 0
                            ) {
                                buffer_format(
                                    &message,
                                    "error[E2S15]: builtin `%s` expects Int "
                                    "for argument 1, got %s at byte %" PRId64,
                                    conversion,
                                    argument_type,
                                    argument
                                );
                                code = "E2S15";
                                span = argument;
                            } else if (valid_conversion < 0) {
                                valid_conversion = cursor;
                            }
                        }
                    }
                    free(conversion);
                    if (code != NULL) {
                        stage2_diagnostic_set(
                            code,
                            span,
                            token_end(source, span),
                            true,
                            message.data
                        );
                        return message.data;
                    }
                    free(message.data);
                }
                cursor = skip_trivia(source, token_end(source, cursor));
            }
        }
        function_start = next_function_start(source, function_close);
    }
    if (valid_conversion >= 0) {
        Buffer message;
        buffer_init(&message);
        buffer_format(
            &message,
            "error[E2S104]: Decimal.from_int at byte %" PRId64
            " has no arithmetic yet (#710 slice 4)",
            valid_conversion
        );
        stage2_diagnostic_set(
            "E2S104",
            valid_conversion,
            token_end(source, valid_conversion),
            true,
            message.data
        );
        return message.data;
    }
    return owned_text("ok");
}

/*
 * A well-formed Decimal or Float literal reaching lowering.
 *
 * #717 lexes these and stops there: the token contract is slice 1 of #710 and
 * the runtime representation is slice 2, so there is nothing yet to lower a
 * Decimal *to*. The refusal names that successor rather than reusing the
 * generic `E2S12`, because "invalid Int expression" says the literal is wrong
 * when what is actually true is that the compiler is unfinished. Lowering
 * through Int, or through a host double, would change the value and is
 * exactly what `docs/DECIMAL.md` forbids.
 */
static char *validate_unsupported_numeric_kinds(const char *source) {
    int64_t length = source_length(source);
    int64_t cursor = skip_trivia(source, 0);
    while (cursor < length) {
        const char *kind = token_kind(source, cursor);
        bool decimal = strcmp(kind, "decimal") == 0;
        if (decimal || strcmp(kind, "float") == 0) {
            int64_t end = token_end(source, cursor);
            /*
             * Construct the value here, even though nothing consumes it yet.
             * That is what puts the profile's limits at the literal's own byte
             * instead of leaving them a library concern: #710's frozen decision
             * 8 requires them to be cross-backend *observable*, and a limit
             * nothing reaches is not observable at all.
             */
            size_t literal_length = (size_t)(end - cursor);
            KofunDecimalStatus status;
            if (decimal) {
                KofunDecimal value;
                status = kofun_decimal_from_literal(
                    source + cursor,
                    literal_length,
                    &value
                );
                kofun_decimal_free(&value);
            } else {
                /* The `f64` suffix is part of the token but not of the
                 * number. */
                double ignored = 0.0;
                status = kofun_float_from_literal(
                    source + cursor,
                    literal_length >= 3 ? literal_length - 3 : 0,
                    &ignored
                );
            }

            Buffer message;
            buffer_init(&message);
            if (status != KOFUN_DECIMAL_OK) {
                buffer_format(
                    &message,
                    "%s at byte %" PRId64,
                    kofun_decimal_status_message(status),
                    cursor
                );
                stage2_diagnostic_set(
                    kofun_decimal_status_code(status),
                    cursor,
                    end,
                    true,
                    message.data
                );
                return message.data;
            }
            /*
             * The literal is representable (slice 2) and typed (slice 3):
             * `Int`, `Decimal` and `Float` are three distinct checker types
             * now. What it still lacks is a *lowering*, so the successor
             * named here is slice 4, which evaluates the operations across
             * the backends. Naming slice 3 would be false — that slice is
             * done, and this literal has a type.
             */
            buffer_format(
                &message,
                "error[E2S99]: %s literal at byte %" PRId64
                " has no lowering yet (#710 slice 4)",
                decimal ? "Decimal" : "Float",
                cursor
            );
            stage2_diagnostic_set(
                "E2S99",
                cursor,
                end,
                true,
                message.data
            );
            return message.data;
        }
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return owned_text("ok");
}

static char *lower_c(const char *source, const char *hir) {
    int64_t length = source_length(source);
    /* A mixed-type expression is reported before the unsupported-kind refusal,
     * so `1 + 1.5` says what is wrong with it rather than reporting the more
     * generic "no arithmetic yet". */
    char *operand_check = validate_numeric_operand_types(source, hir);
    if (strncmp(operand_check, "error[", 6) == 0) return operand_check;
    free(operand_check);
    /* After the operand check, so `let x: Int = 1 + 1.5` reports the mix it
     * contains rather than blaming the annotation for a value that has no
     * single type to compare against. */
    char *annotation_check = validate_numeric_annotations(source, hir);
    if (strncmp(annotation_check, "error[", 6) == 0) return annotation_check;
    free(annotation_check);
    /* After the annotation check, because a conversion is the remedy an
     * annotation mismatch asks for: `let x: Decimal = 1` should say what is
     * wrong with the value, not refuse the conversion the fix would introduce. */
    char *conversion_check = validate_numeric_conversions(source, hir);
    if (strncmp(conversion_check, "error[", 6) == 0) return conversion_check;
    free(conversion_check);
    char *numeric_kind_check = validate_unsupported_numeric_kinds(source);
    if (strncmp(numeric_kind_check, "error[", 6) == 0) {
        return numeric_kind_check;
    }
    free(numeric_kind_check);
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
    char *capture_check = validate_argument_lambda_captures(source, hir);
    if (strncmp(capture_check, "error[", 6) == 0) return capture_check;
    free(capture_check);

    Buffer prototypes;
    Buffer bodies;
    buffer_init(&prototypes);
    buffer_init(&bodies);
    /* Lifted lambdas come first so a Core function can call one that a later
     * function binds. */
    char *lifted = emit_lifted_lambdas(source, hir, &prototypes, &bodies);
    if (strncmp(lifted, "error[", 6) == 0) {
        free(prototypes.data);
        free(bodies.data);
        return lifted;
    }
    free(lifted);
    char *lifted_arguments = emit_lifted_argument_lambdas(
        source,
        hir,
        &prototypes,
        &bodies
    );
    if (strncmp(lifted_arguments, "error[", 6) == 0) {
        free(prototypes.data);
        free(bodies.data);
        return lifted_arguments;
    }
    free(lifted_arguments);
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
            stage2_diagnostic_set(
                "E2S16",
                cursor,
                token_end(source, cursor),
                true,
                error.data
            );
            stage2_diagnostic_affected(
                STAGE2_DIAGNOSTIC_AFFECTED_CALL,
                cursor,
                token_end(source, cursor)
            );
            free(name);
            free(c_name);
            free(prototypes.data);
            free(bodies.data);
            return error.data;
        }
        bool is_main = strcmp(name, "main") == 0;
        const char *c_result =
            function_result_is_enum(source, name) ?
                "KofunEnumValue" :
                "int64_t";
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
                "static %s kofun_fn_%s(%s);\n",
                c_result,
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
                "error[E2S15]: Core function `%s` requires Int or concrete "
                "enum parameters and return",
                name
            );
            stage2_diagnostic_set(
                "E2S15",
                cursor,
                token_end(source, cursor),
                true,
                error.data
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
                "static %s kofun_fn_%s(%s) {\n",
                c_result,
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
        "typedef struct {\n"
        "    int64_t tag;\n"
        "    int64_t payload;\n"
        "} KofunEnumValue;\n"
        "#define KOFUN_ENUM_ZERO "
        "((KofunEnumValue){INT64_C(0), INT64_C(0)})\n\n"
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

#ifdef KOFUN_STAGE2_AUTHORITY_API
static void stage2_diagnostic_reset(Stage2AuthorityContext *context) {
    if (context != NULL) memset(context, 0, sizeof(*context));
}

static bool stage2_compile_outcome(
    const char *source,
    Stage2AuthorityContext *context,
    Stage2AuthorityResult *result
) {
    Stage2AuthorityContext *previous_context =
        stage2_active_authority_context;
    char **previous_parse_prefix_output =
        stage2_active_parse_prefix_output;
    char **previous_scope_prefix_output =
        stage2_active_scope_prefix_output;
    Buffer *previous_semantic_observer =
        stage2_active_semantic_observer;
    Buffer *previous_declaration_observer =
        stage2_active_declaration_observer;
    char *tokens;
    char *pattern_check;
    char *lowered;
    memset(result, 0, sizeof(*result));
    stage2_diagnostic_reset(context);
    stage2_active_authority_context = context;
    stage2_active_parse_prefix_output = &result->parse_prefix_ir;
    stage2_active_scope_prefix_output = &result->scope_prefix_hir;

    tokens = lex_source(source);
    if (strncmp(tokens, "error[", 6) == 0) {
        result->diagnostic = tokens;
        result->exit_class = 1u;
        goto done;
    }
    result->token_span_committed = true;
    free(tokens);

    {
        Buffer declarations;
        buffer_init(&declarations);
        buffer_append(
            &declarations,
            "kofun-stage2-declarations/v1\n"
        );
        stage2_active_declaration_observer = &declarations;
        result->program_ir = parse_program(source);
        stage2_active_declaration_observer =
            previous_declaration_observer;
        /*
         * parse_program may grow and reallocate the observer buffer.  Publish
         * only its final allocation: retaining the pre-parse pointer makes
         * result destruction free storage already released by realloc.
         */
        result->declaration_observations = declarations.data;
    }
    if (strncmp(result->program_ir, "error[", 6) == 0) {
        result->diagnostic = result->program_ir;
        result->program_ir = result->parse_prefix_ir;
        result->parse_prefix_ir = NULL;
        result->parse_committed = result->program_ir != NULL;
        result->exit_class = 1u;
        goto done;
    }
    free(result->parse_prefix_ir);
    result->parse_prefix_ir = NULL;
    result->parse_committed = true;

    pattern_check = validate_executable_patterns(source);
    if (strncmp(pattern_check, "error[", 6) == 0) {
        result->diagnostic = pattern_check;
        result->exit_class =
            unsupported_lowering_error(pattern_check) ? 3u : 1u;
        goto done;
    }
    free(pattern_check);

    result->scope_hir = build_scope_hir(source);
    if (strncmp(result->scope_hir, "error[", 6) == 0) {
        Stage2StructuredDiagnostic saved = context == NULL ?
            (Stage2StructuredDiagnostic){0} : context->diagnostic;
        char *scope_error = result->scope_hir;
        char *ownership;
        result->diagnostic = scope_error;
        result->scope_hir = result->scope_prefix_hir;
        result->scope_prefix_hir = NULL;
        result->scope_committed = result->scope_hir != NULL;
        ownership = borrowed_collection_check(source);
        result->exit_class =
            strncmp(ownership, "error[", 6) == 0 ? 1u : 3u;
        free(ownership);
        if (context != NULL) context->diagnostic = saved;
        goto done;
    }
    free(result->scope_prefix_hir);
    result->scope_prefix_hir = NULL;
    result->scope_committed = true;
    {
        Buffer observations;
        buffer_init(&observations);
        buffer_append(&observations, "kofun-stage2-observations/v1\n");
        stage2_active_semantic_observer = &observations;
        lowered = lower_c(source, result->scope_hir);
        stage2_active_semantic_observer = previous_semantic_observer;
        /* lower_c may reallocate observations; retain the final owner. */
        result->semantic_observations = observations.data;
    }
    if (strncmp(lowered, "error[", 6) == 0) {
        result->diagnostic = lowered;
        result->exit_class =
            unsupported_lowering_error(lowered) ? 3u : 1u;
        goto done;
    }
    free(lowered);
    result->exit_class = 0u;

done:
    stage2_active_declaration_observer =
        previous_declaration_observer;
    stage2_active_semantic_observer = previous_semantic_observer;
    stage2_active_scope_prefix_output = previous_scope_prefix_output;
    stage2_active_parse_prefix_output = previous_parse_prefix_output;
    stage2_active_authority_context = previous_context;
    return true;
}

static bool stage2_ownership_outcome(
    const char *source,
    Stage2AuthorityContext *context,
    Stage2AuthorityResult *result
) {
    Stage2AuthorityContext *previous_context =
        stage2_active_authority_context;
    char **previous_parse_prefix_output =
        stage2_active_parse_prefix_output;
    char **previous_scope_prefix_output =
        stage2_active_scope_prefix_output;
    Buffer *previous_declaration_observer =
        stage2_active_declaration_observer;
    char *tokens;
    char *ownership;
    memset(result, 0, sizeof(*result));
    stage2_diagnostic_reset(context);
    stage2_active_authority_context = context;
    stage2_active_parse_prefix_output = &result->parse_prefix_ir;
    stage2_active_scope_prefix_output = &result->scope_prefix_hir;
    tokens = lex_source(source);
    if (strncmp(tokens, "error[", 6) == 0) {
        result->diagnostic = tokens;
        result->exit_class = 1u;
        goto done;
    }
    result->token_span_committed = true;
    free(tokens);
    {
        Buffer declarations;
        buffer_init(&declarations);
        buffer_append(
            &declarations,
            "kofun-stage2-declarations/v1\n"
        );
        stage2_active_declaration_observer = &declarations;
        result->program_ir = parse_program(source);
        stage2_active_declaration_observer =
            previous_declaration_observer;
        /* parse_program may reallocate declarations; retain the final owner. */
        result->declaration_observations = declarations.data;
    }
    if (strncmp(result->program_ir, "error[", 6) == 0) {
        result->diagnostic = result->program_ir;
        result->program_ir = result->parse_prefix_ir;
        result->parse_prefix_ir = NULL;
        result->parse_committed = result->program_ir != NULL;
        result->exit_class = 1u;
        goto done;
    }
    free(result->parse_prefix_ir);
    result->parse_prefix_ir = NULL;
    result->parse_committed = true;
    {
        Stage2StructuredDiagnostic saved = context == NULL ?
            (Stage2StructuredDiagnostic){0} : context->diagnostic;
        result->scope_hir = build_scope_hir_mode(source, true);
        if (strncmp(result->scope_hir, "error[", 6) == 0) {
            char *scope_error = result->scope_hir;
            result->scope_hir = result->scope_prefix_hir;
            result->scope_prefix_hir = NULL;
            result->scope_committed = result->scope_hir != NULL;
            free(scope_error);
        } else {
            free(result->scope_prefix_hir);
            result->scope_prefix_hir = NULL;
            result->scope_committed = true;
        }
        if (context != NULL) context->diagnostic = saved;
    }
    ownership = borrowed_collection_check(source);
    if (strncmp(ownership, "error[", 6) == 0) {
        result->diagnostic = ownership;
        result->exit_class = 1u;
        goto done;
    }
    free(ownership);
    result->exit_class = 0u;
done:
    stage2_active_declaration_observer =
        previous_declaration_observer;
    stage2_active_scope_prefix_output = previous_scope_prefix_output;
    stage2_active_parse_prefix_output = previous_parse_prefix_output;
    stage2_active_authority_context = previous_context;
    return true;
}

static void stage2_authority_result_destroy(Stage2AuthorityResult *result) {
    if (result == NULL) return;
    free(result->program_ir);
    free(result->parse_prefix_ir);
    free(result->declaration_observations);
    free(result->scope_hir);
    free(result->scope_prefix_hir);
    free(result->semantic_observations);
    free(result->diagnostic);
    memset(result, 0, sizeof(*result));
}
#endif

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
    } functions[128];
    int64_t function_count;
    int64_t builtin_symbols[17];
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
                /*
                 * Positions index `builtin_symbols`, so this order must
                 * match the order those slots are filled in, not the
                 * alphabetical order of the signature tables. `fail`
                 * occupies the tail slot because its symbol is assigned
                 * last, after the len List[Text] overload.
                 */
                static const char *ordered[] = {
                    "args", "chars", "contains", "find", "is_digit",
                    "is_space", "is_xid_continue", "len", "print",
                    "read_text", "replace", "starts_with", "text_slice",
                    "trim", "validate_unicode_source", "write_text",
                    "fail",
                };
                for (int64_t index = 0; index < 17; ++index) {
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
    if (sh->function_count >= 128) {
        sh_fail(sh, "E2S16", "function limit is 128", function_start);
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
        /* A callable parameter type spans several tokens and is recorded under
         * one name, so the arity stays right and the head of the type list
         * keeps naming one parameter each. The frozen self-host profile has no
         * callable values, so `Fn` matches no argument type and a call passing
         * one is rejected by the ordinary mismatch rather than being silently
         * accepted as `Int`. */
        int64_t callable_end = callable_type_end(sh->source, type_at);
        char *type_text = callable_end >= 0
            ? owned_text("Fn")
            : token_copy(sh->source, type_at);
        snprintf(
            sh->functions[slot].parameters[arity],
            sizeof(sh->functions[0].parameters[0]),
            "%s",
            type_text
        );
        free(type_text);
        cursor = callable_end >= 0
            ? skip_trivia(sh->source, callable_end)
            : skip_trivia(sh->source, token_end(sh->source, type_at));
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
    sh.length = source_length(source);
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

    /* The 17 builtin symbols plus the len List[Text] overload. */
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
        /*
         * `fail` is emitted last, after the len List[Text] overload,
         * rather than in its alphabetical place in the table above.
         * Emission order is symbol-id order and those ids are checked-in
         * evidence: alphabetical insertion would renumber thirteen
         * builtins, and appending inside the loop would still push the
         * overload from 17 to 18. Emitting it here leaves every existing
         * id fixed, so the pinned typed-HIR fixtures only gain a line.
         */
        if (sh.error == NULL) {
            char no_parameters[8][16];
            int64_t fn_type = sh_fn_type_id(&sh, "Void", no_parameters, 0);
            sh.builtin_symbols[16] = sh.next_symbol++;
            buffer_format(
                &sh.symbols,
                "symbol|%" PRId64 "|builtin|fail|%" PRId64 "|0|0\n",
                sh.builtin_symbols[16],
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
    SL_MAX_RECORDS = 8192,
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
    "int kofun_runtime_argc = 0;\n"
    "char **kofun_runtime_argv = NULL;\n"
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

/*
 * `kofun_rt_fail` is the whole of the `fail` builtin's host capability:
 * it ends the process with a nonzero status and writes nothing. The
 * program has already printed whatever diagnostic it wants, so adding a
 * message here would change the pinned stdout of every refusing corpus.
 */
static const char *sl_prelude_text =
    "void kofun_rt_panic(const char *message) {\n"
    "    fprintf(stderr, \"Kofun runtime error: %s\\n\", message);\n"
    "    exit(1);\n"
    "}\n"
    "\n"
    "void kofun_rt_fail(void) {\n"
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
    "kofun_text_list kofun_rt_args(void) {\n"
    "    kofun_text_list result;\n"
    "    result.len = (int64_t)kofun_runtime_argc;\n"
    "    result.items = (const char **)kofun_runtime_argv;\n"
    "    return result;\n"
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
    "\n"
    "char *kofun_rt_read_text(const char *path) {\n"
    "    FILE *file = fopen(path, \"rb\");\n"
    "    if (file == NULL) {\n"
    "        kofun_rt_panic(\"cannot open input file\");\n"
    "    }\n"
    "    if (fseek(file, 0, SEEK_END) != 0) {\n"
    "        fclose(file);\n"
    "        kofun_rt_panic(\"cannot seek input file\");\n"
    "    }\n"
    "    long size = ftell(file);\n"
    "    if (size < 0) {\n"
    "        fclose(file);\n"
    "        kofun_rt_panic(\"cannot measure input file\");\n"
    "    }\n"
    "    rewind(file);\n"
    "    char *result = (char *)kofun_rt_alloc((size_t)size + 1);\n"
    "    size_t read = fread(result, 1, (size_t)size, file);\n"
    "    if (read != (size_t)size && ferror(file)) {\n"
    "        fclose(file);\n"
    "        kofun_rt_panic(\"cannot read input file\");\n"
    "    }\n"
    "    result[read] = '\\0';\n"
    "    fclose(file);\n"
    "    return result;\n"
    "}\n"
    "\n"
    "void kofun_rt_write_text(const char *path, const char *value) {\n"
    "    FILE *file = fopen(path, \"wb\");\n"
    "    if (file == NULL) {\n"
    "        kofun_rt_panic(\"cannot open output file\");\n"
    "    }\n"
    "    size_t length = strlen(value);\n"
    "    if (fwrite(value, 1, length, file) != length) {\n"
    "        fclose(file);\n"
    "        kofun_rt_panic(\"cannot write output file\");\n"
    "    }\n"
    "    if (fclose(file) != 0) {\n"
    "        kofun_rt_panic(\"cannot close output file\");\n"
    "    }\n"
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
    if (strcmp(name, "args") == 0) return "kofun_rt_args";
    if (strcmp(name, "chars") == 0) return "kofun_rt_chars";
    if (strcmp(name, "contains") == 0) return "kofun_rt_text_contains";
    if (strcmp(name, "fail") == 0) return "kofun_rt_fail";
    if (strcmp(name, "find") == 0) return "kofun_rt_find";
    if (strcmp(name, "is_digit") == 0) return "kofun_rt_is_digit";
    if (strcmp(name, "is_space") == 0) return "kofun_rt_is_space";
    if (strcmp(name, "is_xid_continue") == 0) {
        return "kofun_rt_is_xid_continue";
    }
    if (strcmp(name, "len") == 0) return "kofun_rt_text_len";
    if (strcmp(name, "print") == 0) return "printf";
    if (strcmp(name, "replace") == 0) return "kofun_rt_replace";
    if (strcmp(name, "starts_with") == 0) return "kofun_rt_starts_with";
    if (strcmp(name, "text_slice") == 0) return "kofun_rt_text_slice";
    if (strcmp(name, "read_text") == 0) return "kofun_rt_read_text";
    if (strcmp(name, "trim") == 0) return "kofun_rt_trim";
    if (strcmp(name, "validate_unicode_source") == 0) {
        return "kofun_rt_validate_unicode_source";
    }
    if (strcmp(name, "write_text") == 0) return "kofun_rt_write_text";
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
        if (builtin && strcmp(name, "print") == 0) {
            buffer_format(out, "printf(\"%%s\\n\", k_n%s);\n",
                          sl_field(node, 8));
            return;
        }
        if (strcmp(type_key, "void") == 0 && builtin) {
            buffer_format(out, "%s(", helper);
        } else if (strcmp(type_key, "void") == 0) {
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
        buffer_append(bodies, "int main(int argc, char **argv) {\n");
        buffer_append(bodies,
                      "    kofun_runtime_argc = argc > 0 ? argc - 1 : 0;\n"
                      "    kofun_runtime_argv = argc > 0 ? argv + 1 : argv;\n");
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

/* The self-host compiler driver: one source-to-C command with no hidden
 * Stage 1/2 fallback. The typed document is produced and lowered in
 * memory; a rejected source prints its stable diagnostic and writes
 * nothing. */
static int selfhost_compile_file(
    const char *input,
    const char *output,
    const char *digest
) {
    if (same_file(input, output)) {
        puts("error[E2S35]: selfhost-compile input and output must be "
             "distinct");
        return 2;
    }
    if (strlen(digest) != 64 || strspn(digest, "0123456789abcdef") != 64) {
        puts("error[E2S35]: selfhost-compile digest must be 64 lowercase "
             "hex");
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
    free(source);
    if (!complete) {
        free(document);
        return 1;
    }
    SlDoc *doc = allocate(sizeof(*doc));
    memset(doc, 0, sizeof(*doc));
    char *lowered = sl_lower_document(doc, document);
    free(document);
    if (lowered == NULL) {
        puts(doc->error);
        int exit_code = doc->error_exit;
        sl_free(doc);
        return exit_code;
    }
    write_file(output, lowered);
    free(lowered);
    sl_free(doc);
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
    if (argc == 5 && strcmp(argv[1], "--selfhost-compile") == 0) {
        return selfhost_compile_file(argv[2], argv[3], argv[4]);
    }
    if (argc != 5) {
        fputs(
            "usage: kofun-stage2 INPUT.kofun OUTPUT.kofun OUTPUT.ir OUTPUT.tokens\n"
            "       kofun-stage2 --compile-outcome INPUT.kofun OUTPUT.c OUTPUT.ir OUTPUT.tokens\n"
            "       kofun-stage2 --check-ownership INPUT.kofun\n"
            "       kofun-stage2 --parse-patterns INPUT.kofun OUTPUT.patterns\n"
            "       kofun-stage2 --emit-scope-hir INPUT.kofun OUTPUT.scope-hir\n"
            "       kofun-stage2 --emit-selfhost-hir INPUT.kofun OUTPUT.hir SOURCE-SHA256\n"
            "       kofun-stage2 --lower-selfhost-c11 INPUT.hir OUTPUT.c\n"
            "       kofun-stage2 --selfhost-compile INPUT.kofun OUTPUT.c SOURCE-SHA256\n",
            stdout
        );
        return 2;
    }
    return compile_file(argv[1], argv[2], argv[3], argv[4]) == 0 ? 0 : 1;
}
