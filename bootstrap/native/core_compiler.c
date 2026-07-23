/*
 * Audited bootstrap driver for the Kofun-owned direct native encoder.
 *
 * The target-independent frontend parses deliberately small Kofun Core
 * profiles. The shared scalar/List/Text profile starts with:
 *
 *   fn main() {
 *       print(CONSTANT_EXPRESSION)
 *   }
 *
 * CONSTANT_EXPRESSION supports a narrow integer, List[Int], and Text Core.
 * A second x86-64 Int profile supports multiple functions, arguments, return
 * values, forward calls, recursion, comparison guards, and checked arithmetic.
 * Unsupported target/type combinations fail before an image is written.
 *
 * This C11 seed is temporary bootstrap machinery. Canonical instruction, ELF,
 * and postfix Core encoders live in encoder.kofun; no Python implementation is
 * used by this compiler.
 */

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../unicode/kofun_unicode.c"

enum {
    ELF_HEADER_SIZE = 64,
    PROGRAM_HEADER_SIZE = 56,
    PROGRAM_HEADER_COUNT = 2,
    TEXT_OFFSET = ELF_HEADER_SIZE +
        PROGRAM_HEADER_SIZE * PROGRAM_HEADER_COUNT,
    PAGE_SIZE = 4096,
};

static const uint64_t IMAGE_BASE = UINT64_C(0x400000);
static const uint64_t DATA_ADDRESS = UINT64_C(0x401000);

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
} Bytes;

typedef struct {
    size_t *offsets;
    size_t *lines;
    size_t length;
    size_t capacity;
} LineRows;

static void fatal(const char *message) {
    fprintf(stderr, "kofun native: %s\n", message);
    exit(2);
}

static void *allocate(size_t size) {
    void *value = malloc(size == 0 ? 1 : size);
    if (value == NULL) fatal("out of memory");
    return value;
}

static void bytes_init(Bytes *bytes) {
    bytes->length = 0;
    bytes->capacity = 256;
    bytes->data = allocate(bytes->capacity);
}

static void bytes_reserve(Bytes *bytes, size_t extra) {
    if (extra > SIZE_MAX - bytes->length) fatal("image is too large");
    size_t needed = bytes->length + extra;
    if (needed <= bytes->capacity) return;
    size_t capacity = bytes->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) fatal("image is too large");
        capacity *= 2;
    }
    uint8_t *data = realloc(bytes->data, capacity);
    if (data == NULL) fatal("out of memory");
    bytes->data = data;
    bytes->capacity = capacity;
}

static void byte(Bytes *bytes, uint8_t value) {
    bytes_reserve(bytes, 1);
    bytes->data[bytes->length++] = value;
}

static void u16_le(Bytes *bytes, uint16_t value) {
    byte(bytes, (uint8_t)value);
    byte(bytes, (uint8_t)(value >> 8));
}

static void u32_le(Bytes *bytes, uint32_t value) {
    byte(bytes, (uint8_t)value);
    byte(bytes, (uint8_t)(value >> 8));
    byte(bytes, (uint8_t)(value >> 16));
    byte(bytes, (uint8_t)(value >> 24));
}

static void u64_le(Bytes *bytes, uint64_t value) {
    u32_le(bytes, (uint32_t)value);
    u32_le(bytes, (uint32_t)(value >> 32));
}

static void bytes_pad_to(Bytes *bytes, size_t length) {
    while (bytes->length < length) byte(bytes, 0);
}

static void line_rows_init(LineRows *rows) {
    rows->length = 0;
    rows->capacity = 16;
    rows->offsets = allocate(rows->capacity * sizeof(*rows->offsets));
    rows->lines = allocate(rows->capacity * sizeof(*rows->lines));
}

static void line_row(LineRows *rows, size_t offset, size_t line) {
    if (line == 0) fatal("source line must be positive");
    if (rows->length > 0 && rows->lines[rows->length - 1] == line) return;
    if (rows->length == rows->capacity) {
        if (rows->capacity > SIZE_MAX / 2) fatal("too many debug line rows");
        rows->capacity *= 2;
        size_t *offsets = realloc(
            rows->offsets,
            rows->capacity * sizeof(*rows->offsets)
        );
        size_t *lines = realloc(
            rows->lines,
            rows->capacity * sizeof(*rows->lines)
        );
        if (offsets == NULL || lines == NULL) fatal("out of memory");
        rows->offsets = offsets;
        rows->lines = lines;
    }
    rows->offsets[rows->length] = offset;
    rows->lines[rows->length] = line;
    ++rows->length;
}

static void line_rows_free(LineRows *rows) {
    free(rows->offsets);
    free(rows->lines);
}

static char *read_source(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "kofun native: cannot read %s: %s\n",
                path, strerror(errno));
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) fatal("cannot seek source");
    long size = ftell(file);
    if (size < 0) fatal("cannot measure source");
    if (fseek(file, 0, SEEK_SET) != 0) fatal("cannot rewind source");
    char *source = allocate((size_t)size + 1);
    size_t read = fread(source, 1, (size_t)size, file);
    if (read != (size_t)size || ferror(file)) fatal("cannot read source");
    if (fclose(file) != 0) fatal("cannot close source");
    source[read] = '\0';
    return source;
}

typedef enum {
    NODE_LITERAL,
    NODE_TEXT_LITERAL,
    NODE_ADD,
    NODE_TEXT_CONCAT,
    NODE_TEXT_EQUAL,
    NODE_TEXT_NOT_EQUAL,
    NODE_INT_EQUAL,
    NODE_INT_NOT_EQUAL,
    NODE_INT_LESS,
    NODE_INT_LESS_EQUAL,
    NODE_INT_GREATER,
    NODE_INT_GREATER_EQUAL,
    NODE_MULTIPLY,
    NODE_NEGATE,
    NODE_VARIABLE,
    NODE_PARAMETER,
    NODE_LET,
    NODE_LIST,
    NODE_CHARS,
    NODE_CODEPOINTS,
    NODE_BYTES,
    NODE_INDEX,
    NODE_LENGTH,
    NODE_MAP,
    NODE_FILTER,
    NODE_FOLD,
} NodeKind;

typedef enum {
    VALUE_INT,
    VALUE_BOOL,
    VALUE_TEXT,
    VALUE_LIST,
} ValueKind;

typedef struct Node Node;

struct Node {
    NodeKind kind;
    ValueKind value_kind;
    int64_t value;
    bool value_known;
    uint8_t *text_value;
    size_t text_length;
    size_t text_codepoints;
    size_t text_graphemes;
    ValueKind element_kind;
    size_t source_line;
    Node *left;
    Node *right;
    Node *third;
    Node **items;
    size_t item_count;
    size_t slot;
};

enum {
    MAX_CORE_BINDINGS = 64,
    MAX_CORE_NAME = 64,
};

typedef struct {
    char name[MAX_CORE_NAME];
    ValueKind value_kind;
    ValueKind element_kind;
    size_t item_count;
    int64_t value;
    bool value_known;
    size_t slot;
    bool parameter;
} Binding;

typedef struct {
    const char *source;
    size_t cursor;
    const char *error;
    size_t main_line;
    size_t print_line;
    Binding bindings[MAX_CORE_BINDINGS + 2];
    size_t binding_count;
    size_t local_count;
    size_t max_lambda_parameters;
} Parser;

enum {
    MAX_CORE_FUNCTIONS = 64,
    MAX_CORE_PARAMETERS = 6,
    MAX_CORE_STATEMENTS = 64,
};

typedef enum {
    FUNCTION_VALUE_INT,
    FUNCTION_VALUE_BOOL,
} FunctionValueKind;

typedef enum {
    FUNCTION_LITERAL,
    FUNCTION_PARAMETER,
    FUNCTION_CALL,
    FUNCTION_ADD,
    FUNCTION_SUBTRACT,
    FUNCTION_MULTIPLY,
    FUNCTION_FLOOR_DIVIDE,
    FUNCTION_FLOOR_MODULO,
    FUNCTION_NEGATE,
    FUNCTION_EQUAL,
    FUNCTION_NOT_EQUAL,
    FUNCTION_LESS,
    FUNCTION_LESS_EQUAL,
    FUNCTION_GREATER,
    FUNCTION_GREATER_EQUAL,
} FunctionExpressionKind;

typedef struct FunctionExpression FunctionExpression;

struct FunctionExpression {
    FunctionExpressionKind kind;
    FunctionValueKind value_kind;
    int64_t value;
    size_t source_line;
    size_t slot;
    size_t function_index;
    FunctionExpression *left;
    FunctionExpression *right;
    FunctionExpression **arguments;
    size_t argument_count;
};

typedef enum {
    FUNCTION_STATEMENT_IF_RETURN,
    FUNCTION_STATEMENT_RETURN,
    FUNCTION_STATEMENT_PRINT,
    FUNCTION_STATEMENT_LET,
    FUNCTION_STATEMENT_EXPRESSION,
} FunctionStatementKind;

typedef struct {
    FunctionStatementKind kind;
    FunctionExpression *condition;
    FunctionExpression *value;
    size_t source_line;
    size_t slot;
} FunctionStatement;

typedef struct {
    char name[MAX_CORE_NAME];
    char parameters[MAX_CORE_PARAMETERS][MAX_CORE_NAME];
    size_t parameter_count;
    char locals[MAX_CORE_BINDINGS][MAX_CORE_NAME];
    size_t local_count;
    size_t declaration_line;
    size_t body_start;
    size_t body_end;
    FunctionStatement statements[MAX_CORE_STATEMENTS];
    size_t statement_count;
} FunctionDeclaration;

typedef struct {
    const char *source;
    FunctionDeclaration functions[MAX_CORE_FUNCTIONS];
    size_t function_count;
    size_t main_index;
    bool extended_numeric;
    bool requires_extended_numeric_lowering;
    bool canonical_runtime_errors;
} FunctionProgram;

typedef struct {
    const char *source;
    size_t cursor;
    size_t limit;
    char error[256];
    FunctionProgram *program;
    FunctionDeclaration *function;
} FunctionParser;

static size_t source_line(const char *source, size_t offset) {
    size_t line = 1;
    for (size_t index = 0; index < offset; ++index) {
        if (source[index] == '\n') ++line;
    }
    return line;
}

static void skip_trivia(Parser *parser) {
    for (;;) {
        while (isspace((unsigned char)parser->source[parser->cursor])) {
            ++parser->cursor;
        }
        if (parser->source[parser->cursor] != '#') return;
        while (parser->source[parser->cursor] != '\0' &&
               parser->source[parser->cursor] != '\n') {
            ++parser->cursor;
        }
    }
}

static bool identifier_start_at(
    const char *source,
    size_t length,
    size_t offset,
    size_t *width
) {
    if (offset >= length) return false;
    uint32_t codepoint = 0;
    size_t scalar_width = 0;
    if (!kofun_unicode_decode(
            (const uint8_t *)source,
            length,
            offset,
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
    size_t offset,
    size_t *width
) {
    if (offset >= length) return false;
    uint32_t codepoint = 0;
    size_t scalar_width = 0;
    if (!kofun_unicode_decode(
            (const uint8_t *)source,
            length,
            offset,
            &codepoint,
            &scalar_width)) {
        return false;
    }
    if (width != NULL) *width = scalar_width;
    return codepoint == '_' || kofun_unicode_is_xid_continue(codepoint);
}

static bool consume_word(Parser *parser, const char *word) {
    skip_trivia(parser);
    size_t length = strlen(word);
    if (strncmp(parser->source + parser->cursor, word, length) != 0 ||
        identifier_continue_at(
            parser->source,
            strlen(parser->source),
            parser->cursor + length,
            NULL)) {
        return false;
    }
    parser->cursor += length;
    return true;
}

static bool consume_char(Parser *parser, char wanted) {
    skip_trivia(parser);
    if (parser->source[parser->cursor] != wanted) return false;
    ++parser->cursor;
    return true;
}

static void parse_error(Parser *parser, const char *message) {
    if (parser->error == NULL) parser->error = message;
}

static Node *node(
    NodeKind kind,
    ValueKind value_kind,
    int64_t value,
    bool value_known,
    size_t line,
    Node *left,
    Node *right
) {
    Node *result = allocate(sizeof(*result));
    result->kind = kind;
    result->value_kind = value_kind;
    result->value = value;
    result->value_known = value_known;
    result->text_value = NULL;
    result->text_length = 0;
    result->text_codepoints = 0;
    result->text_graphemes = 0;
    result->element_kind = VALUE_INT;
    result->source_line = line;
    result->left = left;
    result->right = right;
    result->third = NULL;
    result->items = NULL;
    result->item_count = 0;
    result->slot = 0;
    return result;
}

static Node *parse_expression(Parser *parser);
static void free_node(Node *expression);

static uint8_t *copy_bytes(const uint8_t *bytes, size_t length) {
    uint8_t *copy = allocate(length);
    if (length > 0) memcpy(copy, bytes, length);
    return copy;
}

static Node *text_node(
    NodeKind kind,
    const uint8_t *bytes,
    size_t length,
    size_t codepoints,
    size_t graphemes,
    size_t line,
    Node *left,
    Node *right
) {
    Node *result = node(
        kind,
        VALUE_TEXT,
        0,
        true,
        line,
        left,
        right
    );
    result->text_value = copy_bytes(bytes, length);
    result->text_length = length;
    result->text_codepoints = codepoints;
    result->text_graphemes = graphemes;
    return result;
}

static Node *parse_text_literal(Parser *parser, size_t literal_at) {
    Bytes bytes;
    bytes_init(&bytes);
    while (parser->source[parser->cursor] != '\0' &&
           parser->source[parser->cursor] != '"') {
        unsigned char value =
            (unsigned char)parser->source[parser->cursor++];
        if (value == '\\') {
            char escaped = parser->source[parser->cursor++];
            if (escaped == 'n') value = '\n';
            else if (escaped == 'r') value = '\r';
            else if (escaped == 't') value = '\t';
            else if (escaped == '\\') value = '\\';
            else if (escaped == '"') value = '"';
            else {
                parse_error(parser, "unsupported Text escape");
                free(bytes.data);
                return NULL;
            }
        }
        byte(&bytes, (uint8_t)value);
    }
    if (parser->source[parser->cursor] != '"') {
        parse_error(parser, "unterminated Text literal");
        free(bytes.data);
        return NULL;
    }
    ++parser->cursor;
    size_t codepoints = 0;
    size_t graphemes = 0;
    if (!kofun_unicode_codepoint_count(
            bytes.data, bytes.length, &codepoints) ||
        !kofun_unicode_grapheme_count(
            bytes.data, bytes.length, &graphemes)) {
        parse_error(parser, "Text literal is not valid UTF-8");
        free(bytes.data);
        return NULL;
    }
    Node *result = text_node(
        NODE_TEXT_LITERAL,
        bytes.data,
        bytes.length,
        codepoints,
        graphemes,
        source_line(parser->source, literal_at),
        NULL,
        NULL
    );
    free(bytes.data);
    return result;
}

static bool parse_identifier(
    Parser *parser,
    char name[MAX_CORE_NAME]
) {
    skip_trivia(parser);
    size_t length = strlen(parser->source);
    size_t first_width = 0;
    if (!identifier_start_at(
            parser->source,
            length,
            parser->cursor,
            &first_width)) {
        return false;
    }
    size_t start = parser->cursor;
    parser->cursor += first_width;
    while (identifier_continue_at(
            parser->source,
            length,
            parser->cursor,
            &first_width)) {
        parser->cursor += first_width;
    }
    size_t name_length = parser->cursor - start;
    if (name_length >= MAX_CORE_NAME) {
        parse_error(parser, "native Core identifier is too long");
        return false;
    }
    memcpy(name, parser->source + start, name_length);
    name[name_length] = '\0';
    return true;
}

static const Binding *find_binding(
    const Parser *parser,
    const char *name
) {
    for (size_t index = parser->binding_count; index > 0; --index) {
        const Binding *binding = &parser->bindings[index - 1];
        if (strcmp(binding->name, name) == 0) return binding;
    }
    return NULL;
}

static bool add_binding(
    Parser *parser,
    const char *name,
    ValueKind value_kind,
    ValueKind element_kind,
    size_t item_count,
    int64_t value,
    bool value_known,
    size_t slot,
    bool parameter
) {
    if (parser->binding_count >= MAX_CORE_BINDINGS + 2) {
        parse_error(parser, "native Core has too many bindings");
        return false;
    }
    Binding *binding = &parser->bindings[parser->binding_count++];
    memcpy(binding->name, name, strlen(name) + 1);
    binding->value_kind = value_kind;
    binding->element_kind = element_kind;
    binding->item_count = item_count;
    binding->value = value;
    binding->value_known = value_known;
    binding->slot = slot;
    binding->parameter = parameter;
    return true;
}

static Node *parse_lambda(Parser *parser, size_t parameter_count) {
    if (!consume_word(parser, "fn") || !consume_char(parser, '(')) {
        parse_error(parser, "expected native Core `fn(...) =>` lambda");
        return NULL;
    }
    char names[2][MAX_CORE_NAME] = {{0}};
    for (size_t index = 0; index < parameter_count; ++index) {
        if (!parse_identifier(parser, names[index])) {
            parse_error(parser, "expected lambda parameter name");
            return NULL;
        }
        if (consume_char(parser, ':') &&
            !consume_word(parser, "Int")) {
            parse_error(parser, "native Core lambda parameters must be Int");
            return NULL;
        }
        if (index + 1 < parameter_count && !consume_char(parser, ',')) {
            parse_error(parser, "expected `,` between lambda parameters");
            return NULL;
        }
    }
    if (!consume_char(parser, ')')) {
        parse_error(parser, "expected `)` after lambda parameters");
        return NULL;
    }
    skip_trivia(parser);
    if (parser->source[parser->cursor] != '=' ||
        parser->source[parser->cursor + 1] != '>') {
        parse_error(parser, "expected `=>` after lambda parameters");
        return NULL;
    }
    parser->cursor += 2;

    size_t outer_count = parser->binding_count;
    if (parameter_count > parser->max_lambda_parameters) {
        parser->max_lambda_parameters = parameter_count;
    }
    for (size_t index = 0; index < parameter_count; ++index) {
        if (!add_binding(
                parser,
                names[index],
                VALUE_INT,
                VALUE_INT,
                0,
                0,
                false,
                parser->local_count + index,
                true)) {
            parser->binding_count = outer_count;
            return NULL;
        }
    }
    Node *body = parse_expression(parser);
    parser->binding_count = outer_count;
    return body;
}

static bool contains_higher_order(const Node *expression) {
    if (expression == NULL) return false;
    if (expression->kind == NODE_MAP ||
        expression->kind == NODE_FILTER ||
        expression->kind == NODE_FOLD) {
        return true;
    }
    if (contains_higher_order(expression->left) ||
        contains_higher_order(expression->right) ||
        contains_higher_order(expression->third)) {
        return true;
    }
    for (size_t index = 0;
         expression->items != NULL && index < expression->item_count;
         ++index) {
        if (contains_higher_order(expression->items[index])) return true;
    }
    return false;
}

static Node *parse_higher_order(
    Parser *parser,
    NodeKind kind,
    size_t call_at
) {
    if (!consume_char(parser, '(')) {
        parse_error(parser, "expected `(` after List operation");
        return NULL;
    }
    Node *list = parse_expression(parser);
    if (!consume_char(parser, ',')) {
        parse_error(parser, "expected `,` after List argument");
        return list;
    }
    Node *initial = NULL;
    size_t parameters = 1;
    if (kind == NODE_FOLD) {
        initial = parse_expression(parser);
        if (!consume_char(parser, ',')) {
            parse_error(parser, "expected `,` before fold lambda");
            return list;
        }
        parameters = 2;
    }
    Node *lambda = parse_lambda(parser, parameters);
    if (!consume_char(parser, ')')) {
        parse_error(parser, "expected `)` after List operation");
    }
    if (list == NULL ||
        list->value_kind != VALUE_LIST ||
        list->element_kind != VALUE_INT) {
        parse_error(parser, "native Core List operation requires List[Int]");
        return list;
    }
    if (kind == NODE_FOLD &&
        (initial == NULL || initial->value_kind != VALUE_INT)) {
        parse_error(parser, "native Core fold initial value must be Int");
        return list;
    }
    ValueKind expected =
        kind == NODE_FILTER ? VALUE_BOOL : VALUE_INT;
    if (lambda == NULL || lambda->value_kind != expected) {
        parse_error(
            parser,
            kind == NODE_FILTER
                ? "native Core filter lambda must return Bool"
                : "native Core map/fold lambda must return Int"
        );
        return list;
    }
    if (contains_higher_order(lambda)) {
        parse_error(
            parser,
            "native Core List lambdas cannot contain nested List operations"
        );
        return list;
    }

    Node *result = node(
        kind,
        kind == NODE_FOLD ? VALUE_INT : VALUE_LIST,
        0,
        false,
        source_line(parser->source, call_at),
        list,
        kind == NODE_FOLD ? initial : lambda
    );
    result->slot = parser->local_count;
    if (kind == NODE_FOLD) {
        result->third = lambda;
    } else {
        result->element_kind = VALUE_INT;
        result->item_count =
            kind == NODE_MAP ? list->item_count : 0;
    }
    return result;
}

static Node *parse_text_view(
    Parser *parser,
    NodeKind kind,
    size_t call_at
) {
    if (!consume_char(parser, '(')) {
        parse_error(parser, "expected `(` after Text view");
        return NULL;
    }
    Node *value = parse_expression(parser);
    if (!consume_char(parser, ')')) {
        parse_error(parser, "expected `)` after Text view argument");
    }
    if (value == NULL || value->value_kind != VALUE_TEXT) {
        parse_error(parser, "Text view argument must be Text");
        return value;
    }

    Node *list = node(
        kind,
        VALUE_LIST,
        0,
        value->value_known,
        source_line(parser->source, call_at),
        value,
        NULL
    );
    list->element_kind =
        kind == NODE_BYTES ? VALUE_INT : VALUE_TEXT;
    if (kind == NODE_BYTES) {
        list->item_count = value->text_length;
    } else if (kind == NODE_CODEPOINTS) {
        list->item_count = value->text_codepoints;
    } else {
        list->item_count = value->text_graphemes;
    }
    list->items = allocate(
        list->item_count * sizeof(*list->items)
    );

    for (size_t index = 0; index < list->item_count; ++index) {
        if (kind == NODE_BYTES) {
            list->items[index] = node(
                NODE_LITERAL,
                VALUE_INT,
                value->text_value[index],
                true,
                source_line(parser->source, call_at),
                NULL,
                NULL
            );
            continue;
        }

        size_t byte_at = 0;
        size_t width = 0;
        bool found = kind == NODE_CODEPOINTS
            ? kofun_unicode_codepoint_at(
                value->text_value,
                value->text_length,
                index,
                &byte_at,
                &width
            )
            : kofun_unicode_grapheme_at(
                value->text_value,
                value->text_length,
                index,
                &byte_at,
                &width
            );
        if (!found) fatal("validated Text view became invalid");
        size_t codepoints = 0;
        size_t graphemes = 0;
        if (!kofun_unicode_codepoint_count(
                value->text_value + byte_at,
                width,
                &codepoints) ||
            !kofun_unicode_grapheme_count(
                value->text_value + byte_at,
                width,
                &graphemes)) {
            fatal("validated Text view became invalid");
        }
        list->items[index] = text_node(
            NODE_TEXT_LITERAL,
            value->text_value + byte_at,
            width,
            codepoints,
            graphemes,
            source_line(parser->source, call_at),
            NULL,
            NULL
        );
    }
    return list;
}

static Node *parse_atom(Parser *parser) {
    skip_trivia(parser);
    if (consume_char(parser, '(')) {
        Node *inside = parse_expression(parser);
        if (!consume_char(parser, ')')) {
            parse_error(parser, "expected `)` in Core expression");
        }
        return inside;
    }

    skip_trivia(parser);
    size_t literal_at = parser->cursor;
    if (parser->source[parser->cursor] == '"') {
        ++parser->cursor;
        return parse_text_literal(parser, literal_at);
    }

    if (consume_char(parser, '[')) {
        Node **items = NULL;
        size_t length = 0;
        size_t capacity = 0;
        ValueKind element_kind = VALUE_INT;
        skip_trivia(parser);
        if (!consume_char(parser, ']')) {
            for (;;) {
                Node *item = parse_expression(parser);
                if (item == NULL || parser->error != NULL) break;
                if (item->value_kind != VALUE_INT &&
                    item->value_kind != VALUE_TEXT) {
                    parse_error(
                        parser,
                        "native Core lists require Int or Text elements"
                    );
                    break;
                }
                if (length == 0) {
                    element_kind = item->value_kind;
                } else if (item->value_kind != element_kind) {
                    parse_error(
                        parser,
                        "native Core list elements must have one type"
                    );
                    break;
                }
                if (length == capacity) {
                    capacity = capacity == 0 ? 4 : capacity * 2;
                    Node **grown = realloc(items, capacity * sizeof(*items));
                    if (grown == NULL) fatal("out of memory");
                    items = grown;
                }
                items[length++] = item;
                if (consume_char(parser, ']')) break;
                if (!consume_char(parser, ',')) {
                    parse_error(parser, "expected `,` or `]` in List[Int]");
                    break;
                }
                if (consume_char(parser, ']')) break;
            }
        }
        Node *list = node(
            NODE_LIST,
            VALUE_LIST,
            0,
            true,
            source_line(parser->source, literal_at),
            NULL,
            NULL
        );
        list->items = items;
        list->item_count = length;
        list->element_kind = element_kind;
        return list;
    }

    skip_trivia(parser);
    if (consume_word(parser, "map")) {
        return parse_higher_order(
            parser,
            NODE_MAP,
            literal_at
        );
    }

    skip_trivia(parser);
    if (consume_word(parser, "filter")) {
        return parse_higher_order(
            parser,
            NODE_FILTER,
            literal_at
        );
    }

    skip_trivia(parser);
    if (consume_word(parser, "fold")) {
        return parse_higher_order(
            parser,
            NODE_FOLD,
            literal_at
        );
    }

    skip_trivia(parser);
    if (consume_word(parser, "chars")) {
        return parse_text_view(
            parser,
            NODE_CHARS,
            literal_at
        );
    }

    skip_trivia(parser);
    if (consume_word(parser, "codepoints")) {
        return parse_text_view(
            parser,
            NODE_CODEPOINTS,
            literal_at
        );
    }

    skip_trivia(parser);
    if (consume_word(parser, "bytes")) {
        return parse_text_view(
            parser,
            NODE_BYTES,
            literal_at
        );
    }

    skip_trivia(parser);
    if (consume_word(parser, "len")) {
        if (!consume_char(parser, '(')) {
            parse_error(parser, "expected `(` after `len`");
            return NULL;
        }
        Node *value = parse_expression(parser);
        if (!consume_char(parser, ')')) {
            parse_error(parser, "expected `)` after `len` argument");
        }
        if (value != NULL &&
            value->value_kind != VALUE_LIST &&
            value->value_kind != VALUE_TEXT) {
            parse_error(
                parser,
                "`len` native Core argument must be List or Text"
            );
        }
        return node(
            NODE_LENGTH,
            VALUE_INT,
            value == NULL
                ? 0
                : (int64_t)(
                    value->value_kind == VALUE_TEXT
                        ? value->text_graphemes
                        : value->item_count
                ),
            value != NULL && value->value_known,
            source_line(parser->source, literal_at),
            value,
            NULL
        );
    }

    skip_trivia(parser);
    if (identifier_start_at(
            parser->source,
            strlen(parser->source),
            parser->cursor,
            NULL)) {
        char name[MAX_CORE_NAME];
        if (!parse_identifier(parser, name)) return NULL;
        const Binding *binding = find_binding(parser, name);
        if (binding == NULL) {
            parse_error(parser, "unknown native Core binding");
            return NULL;
        }
        Node *variable = node(
            binding->parameter ? NODE_PARAMETER : NODE_VARIABLE,
            binding->value_kind,
            binding->value,
            binding->value_known,
            source_line(parser->source, literal_at),
            NULL,
            NULL
        );
        variable->element_kind = binding->element_kind;
        variable->item_count = binding->item_count;
        variable->slot = binding->slot;
        return variable;
    }

    skip_trivia(parser);
    if (!isdigit((unsigned char)parser->source[parser->cursor])) {
        parse_error(
            parser,
            "expected integer, binding, Text, List, or Core call"
        );
        return NULL;
    }

    uint64_t value = 0;
    while (isdigit((unsigned char)parser->source[parser->cursor])) {
        unsigned digit =
            (unsigned)(parser->source[parser->cursor++] - '0');
        if (value > (UINT64_C(65535) - digit) / 10) {
            parse_error(parser, "Core literal exceeds 65535");
            return NULL;
        }
        value = value * 10 + digit;
    }
    return node(
        NODE_LITERAL,
        VALUE_INT,
        (int64_t)value,
        true,
        source_line(parser->source, literal_at),
        NULL,
        NULL
    );
}

static Node *parse_primary(Parser *parser) {
    Node *value = parse_atom(parser);
    while (value != NULL && parser->error == NULL) {
        skip_trivia(parser);
        if (!consume_char(parser, '[')) break;
        size_t index_at = parser->cursor - 1;
        Node *index = parse_expression(parser);
        if (!consume_char(parser, ']')) {
            parse_error(parser, "expected `]` after native Core index");
            return value;
        }
        if ((value->value_kind != VALUE_LIST &&
             value->value_kind != VALUE_TEXT) ||
            index == NULL ||
            index->value_kind != VALUE_INT) {
            parse_error(parser, "native Core indexing requires List or Text");
            return value;
        }

        int64_t resolved = 0;
        uint8_t *resolved_text = NULL;
        size_t resolved_text_length = 0;
        size_t target_length =
            value->value_kind == VALUE_TEXT
                ? value->text_graphemes
                : value->item_count;
        ValueKind result_kind =
            value->value_kind == VALUE_TEXT
                ? VALUE_TEXT
                : value->element_kind;
        bool known = value->value_known && index->value_known;
        if (known) {
            int64_t wanted = index->value;
            if (wanted < 0) wanted += (int64_t)target_length;
            if (wanted < 0 || (uint64_t)wanted >= target_length) {
                known = false;
            } else if (value->value_kind == VALUE_TEXT) {
                size_t byte_at = 0;
                if (!kofun_unicode_grapheme_at(
                        value->text_value,
                        value->text_length,
                        (size_t)wanted,
                        &byte_at,
                        &resolved_text_length)) {
                    fatal("validated Text became invalid");
                }
                resolved_text = copy_bytes(
                    value->text_value + byte_at,
                    resolved_text_length
                );
            } else {
                Node *item = value->items[(size_t)wanted];
                known = item->value_known;
                if (result_kind == VALUE_TEXT) {
                    resolved_text_length = item->text_length;
                    resolved_text = copy_bytes(
                        item->text_value,
                        item->text_length
                    );
                } else {
                    resolved = item->value;
                }
            }
        }
        Node *indexed = node(
            NODE_INDEX,
            result_kind,
            resolved,
            known,
            source_line(parser->source, index_at),
            value,
            index
        );
        if (known && result_kind == VALUE_TEXT) {
            indexed->text_value = resolved_text;
            indexed->text_length = resolved_text_length;
            if (!kofun_unicode_codepoint_count(
                    resolved_text,
                    resolved_text_length,
                    &indexed->text_codepoints) ||
                !kofun_unicode_grapheme_count(
                    resolved_text,
                    resolved_text_length,
                    &indexed->text_graphemes)) {
                fatal("validated indexed Text became invalid");
            }
        } else {
            free(resolved_text);
        }
        value = indexed;
    }
    return value;
}

static Node *parse_unary(Parser *parser) {
    skip_trivia(parser);
    size_t operator_at = parser->cursor;
    if (consume_char(parser, '-')) {
        Node *operand = parse_unary(parser);
        if (operand == NULL || operand->value_kind != VALUE_INT) {
            parse_error(parser, "native Core unary `-` requires Int");
            return operand;
        }
        return node(
            NODE_NEGATE,
            VALUE_INT,
            operand->value_known ? -operand->value : 0,
            operand->value_known,
            source_line(parser->source, operator_at),
            operand,
            NULL
        );
    }
    return parse_primary(parser);
}

static bool checked_value(
    Parser *parser,
    NodeKind kind,
    int64_t left,
    int64_t right,
    int64_t *result
) {
    if (left < 0 || right < 0) {
        parse_error(parser, "Core arithmetic requires non-negative operands");
        return false;
    }
    if (kind == NODE_ADD) {
        if ((uint64_t)left > UINT64_C(65535) - (uint64_t)right) {
            parse_error(parser, "Core addition exceeds 65535");
            return false;
        }
        *result = left + right;
        return true;
    }
    if (right != 0 &&
        (uint64_t)left > UINT64_C(65535) / (uint64_t)right) {
        parse_error(parser, "Core multiplication exceeds 65535");
        return false;
    }
    *result = left * right;
    return true;
}

static Node *parse_product(Parser *parser) {
    Node *left = parse_unary(parser);
    while (parser->error == NULL) {
        skip_trivia(parser);
        if (parser->source[parser->cursor] != '*') break;
        size_t operator_at = parser->cursor;
        ++parser->cursor;
        Node *right = parse_unary(parser);
        if (right == NULL) return left;
        if (left->value_kind != VALUE_INT ||
            right->value_kind != VALUE_INT) {
            parse_error(parser, "operator `*` requires Int operands");
            return left;
        }
        int64_t value = 0;
        if (!checked_value(
                parser, NODE_MULTIPLY, left->value, right->value, &value)) {
            return left;
        }
        left = node(
            NODE_MULTIPLY,
            VALUE_INT,
            value,
            left->value_known && right->value_known,
            source_line(parser->source, operator_at),
            left,
            right
        );
    }
    return left;
}

static Node *parse_sum(Parser *parser) {
    Node *left = parse_product(parser);
    while (parser->error == NULL) {
        skip_trivia(parser);
        if (parser->source[parser->cursor] != '+') break;
        size_t operator_at = parser->cursor;
        ++parser->cursor;
        Node *right = parse_product(parser);
        if (right == NULL) return left;
        if (left->value_kind == VALUE_TEXT &&
            right->value_kind == VALUE_TEXT) {
            if (left->text_length > SIZE_MAX - right->text_length) {
                fatal("Text concatenation is too large");
            }
            size_t length = left->text_length + right->text_length;
            uint8_t *joined = allocate(length);
            if (left->text_length > 0) {
                memcpy(joined, left->text_value, left->text_length);
            }
            if (right->text_length > 0) {
                memcpy(
                    joined + left->text_length,
                    right->text_value,
                    right->text_length
                );
            }
            Node *combined = text_node(
                NODE_TEXT_CONCAT,
                joined,
                length,
                left->text_codepoints + right->text_codepoints,
                0,
                source_line(parser->source, operator_at),
                left,
                right
            );
            combined->value_known =
                left->value_known && right->value_known;
            if (!kofun_unicode_grapheme_count(
                    joined,
                    length,
                    &combined->text_graphemes)) {
                fatal("validated concatenated Text became invalid");
            }
            free(joined);
            left = combined;
            continue;
        }
        if (left->value_kind != VALUE_INT ||
            right->value_kind != VALUE_INT) {
            parse_error(
                parser,
                "operator `+` requires two Int or two Text operands"
            );
            return left;
        }
        int64_t value = 0;
        if (!checked_value(
                parser, NODE_ADD, left->value, right->value, &value)) {
            return left;
        }
        left = node(
            NODE_ADD,
            VALUE_INT,
            value,
            left->value_known && right->value_known,
            source_line(parser->source, operator_at),
            left,
            right
        );
    }
    return left;
}

static Node *parse_expression(Parser *parser) {
    Node *left = parse_sum(parser);
    while (parser->error == NULL) {
        skip_trivia(parser);
        size_t operator_at = parser->cursor;
        char first = parser->source[parser->cursor];
        char second = parser->source[parser->cursor + 1];
        bool equal = first == '=' && second == '=';
        bool not_equal = first == '!' && second == '=';
        bool less_equal = first == '<' && second == '=';
        bool greater_equal = first == '>' && second == '=';
        bool less = first == '<' && !less_equal;
        bool greater = first == '>' && !greater_equal;
        if (!equal && !not_equal && !less_equal &&
            !greater_equal && !less && !greater) {
            break;
        }
        parser->cursor +=
            equal || not_equal || less_equal || greater_equal ? 2 : 1;
        Node *right = parse_sum(parser);
        if (right == NULL) return left;
        bool known = left->value_known && right->value_known;
        bool result = false;
        NodeKind kind;
        if (left->value_kind == VALUE_TEXT &&
            right->value_kind == VALUE_TEXT &&
            (equal || not_equal)) {
            kind = equal ? NODE_TEXT_EQUAL : NODE_TEXT_NOT_EQUAL;
            if (known) {
                bool same =
                    left->text_length == right->text_length &&
                    memcmp(
                        left->text_value,
                        right->text_value,
                        left->text_length
                    ) == 0;
                result = equal ? same : !same;
            }
        } else if (left->value_kind == VALUE_INT &&
                   right->value_kind == VALUE_INT) {
            if (equal) kind = NODE_INT_EQUAL;
            else if (not_equal) kind = NODE_INT_NOT_EQUAL;
            else if (less) kind = NODE_INT_LESS;
            else if (less_equal) kind = NODE_INT_LESS_EQUAL;
            else if (greater) kind = NODE_INT_GREATER;
            else kind = NODE_INT_GREATER_EQUAL;
            if (known) {
                if (equal) result = left->value == right->value;
                else if (not_equal) result = left->value != right->value;
                else if (less) result = left->value < right->value;
                else if (less_equal) result = left->value <= right->value;
                else if (greater) result = left->value > right->value;
                else result = left->value >= right->value;
            }
        } else {
            parse_error(
                parser,
                "native Core comparison requires matching Int or Text"
            );
            return left;
        }
        left = node(
            kind,
            VALUE_BOOL,
            result,
            known,
            source_line(parser->source, operator_at),
            left,
            right
        );
    }
    return left;
}

static Node *parse_program(Parser *parser) {
    Node *initializers[MAX_CORE_BINDINGS] = {0};
    size_t let_count = 0;
    skip_trivia(parser);
    parser->main_line = source_line(parser->source, parser->cursor);
    if (!consume_word(parser, "fn") ||
        !consume_word(parser, "main") ||
        !consume_char(parser, '(') ||
        !consume_char(parser, ')') ||
        !consume_char(parser, '{')) {
        parse_error(
            parser,
            "native Core requires `fn main() { print(EXPRESSION) }`"
        );
        return NULL;
    }

    while (consume_word(parser, "let")) {
        char name[MAX_CORE_NAME];
        if (!parse_identifier(parser, name)) {
            parse_error(parser, "expected binding name after `let`");
            return NULL;
        }
        if (find_binding(parser, name) != NULL) {
            parse_error(parser, "duplicate native Core binding");
            return NULL;
        }
        if (!consume_char(parser, '=')) {
            parse_error(parser, "expected `=` after binding name");
            return NULL;
        }
        Node *initializer = parse_expression(parser);
        if (initializer == NULL) return NULL;
        if (initializer->value_kind != VALUE_INT &&
            !(initializer->value_kind == VALUE_LIST &&
              initializer->element_kind == VALUE_INT)) {
            parse_error(
                parser,
                "native Core bindings currently require Int or List[Int]"
            );
            return initializer;
        }
        if (let_count >= MAX_CORE_BINDINGS) {
            parse_error(parser, "native Core has too many let bindings");
            return initializer;
        }
        size_t slot = parser->local_count++;
        if (!add_binding(
                parser,
                name,
                initializer->value_kind,
                initializer->element_kind,
                initializer->item_count,
                initializer->value,
                initializer->value_kind == VALUE_INT &&
                    initializer->value_known,
                slot,
                false)) {
            return initializer;
        }
        initializers[let_count++] = initializer;
    }

    skip_trivia(parser);
    parser->print_line = source_line(parser->source, parser->cursor);
    if (!consume_word(parser, "print") || !consume_char(parser, '(')) {
        parse_error(
            parser,
            "native Core requires `fn main() { print(EXPRESSION) }`"
        );
        for (size_t index = 0; index < let_count; ++index) {
            free_node(initializers[index]);
        }
        return NULL;
    }

    Node *expression = parse_expression(parser);
    if (!consume_char(parser, ')') || !consume_char(parser, '}')) {
        parse_error(
            parser,
            "native Core requires exactly one print expression"
        );
        return expression;
    }
    skip_trivia(parser);
    if (parser->source[parser->cursor] != '\0') {
        parse_error(parser, "unexpected source after native Core main");
    }
    if (expression != NULL &&
        expression->value_kind != VALUE_INT &&
        expression->value_kind != VALUE_BOOL &&
        expression->value_kind != VALUE_TEXT) {
        parse_error(
            parser,
            "native Core print expression must produce Int, Bool, or Text"
        );
    } else if (expression != NULL &&
        expression->value_kind == VALUE_INT &&
        expression->value_known &&
        (expression->value < 10 || expression->value > 99)) {
        parse_error(parser, "native Core print result must be 10..99");
    }
    for (size_t index = let_count; index > 0; --index) {
        Node *body = expression;
        Node *let = node(
            NODE_LET,
            body == NULL ? VALUE_INT : body->value_kind,
            body == NULL ? 0 : body->value,
            body != NULL && body->value_known,
            initializers[index - 1]->source_line,
            initializers[index - 1],
            body
        );
        let->element_kind =
            body == NULL ? VALUE_INT : body->element_kind;
        let->item_count = body == NULL ? 0 : body->item_count;
        let->slot = index - 1;
        expression = let;
    }
    return expression;
}

static void function_skip_trivia(FunctionParser *parser) {
    for (;;) {
        while (parser->cursor < parser->limit &&
               isspace((unsigned char)parser->source[parser->cursor])) {
            ++parser->cursor;
        }
        if (parser->cursor >= parser->limit ||
            parser->source[parser->cursor] != '#') {
            return;
        }
        while (parser->cursor < parser->limit &&
               parser->source[parser->cursor] != '\n') {
            ++parser->cursor;
        }
    }
}

static void function_error(
    FunctionParser *parser,
    const char *format,
    ...
) {
    if (parser->error[0] != '\0') return;
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(
        parser->error,
        sizeof(parser->error),
        format,
        arguments
    );
    va_end(arguments);
}

static bool function_consume_word(
    FunctionParser *parser,
    const char *word
) {
    function_skip_trivia(parser);
    size_t length = strlen(word);
    if (parser->cursor > parser->limit ||
        parser->limit - parser->cursor < length ||
        strncmp(parser->source + parser->cursor, word, length) != 0 ||
        (parser->cursor + length < parser->limit &&
         identifier_continue_at(
             parser->source,
             parser->limit,
             parser->cursor + length,
             NULL))) {
        return false;
    }
    parser->cursor += length;
    return true;
}

static bool function_consume_char(
    FunctionParser *parser,
    char wanted
) {
    function_skip_trivia(parser);
    if (parser->cursor >= parser->limit ||
        parser->source[parser->cursor] != wanted) {
        return false;
    }
    ++parser->cursor;
    return true;
}

static bool function_consume_pair(
    FunctionParser *parser,
    char first,
    char second
) {
    function_skip_trivia(parser);
    if (parser->cursor + 1 >= parser->limit ||
        parser->source[parser->cursor] != first ||
        parser->source[parser->cursor + 1] != second) {
        return false;
    }
    parser->cursor += 2;
    return true;
}

static bool function_identifier(
    FunctionParser *parser,
    char name[MAX_CORE_NAME]
) {
    function_skip_trivia(parser);
    size_t width = 0;
    if (!identifier_start_at(
            parser->source,
            parser->limit,
            parser->cursor,
            &width)) {
        return false;
    }
    size_t start = parser->cursor;
    parser->cursor += width;
    while (identifier_continue_at(
            parser->source,
            parser->limit,
            parser->cursor,
            &width)) {
        parser->cursor += width;
    }
    size_t length = parser->cursor - start;
    if (length >= MAX_CORE_NAME) {
        function_error(parser, "native Core function name is too long");
        return false;
    }
    memcpy(name, parser->source + start, length);
    name[length] = '\0';
    return true;
}

static size_t function_find(
    const FunctionProgram *program,
    const char *name
) {
    for (size_t index = 0; index < program->function_count; ++index) {
        if (strcmp(program->functions[index].name, name) == 0) {
            return index;
        }
    }
    return SIZE_MAX;
}

static size_t function_parameter_find(
    const FunctionDeclaration *function,
    const char *name
) {
    for (size_t index = 0; index < function->parameter_count; ++index) {
        if (strcmp(function->parameters[index], name) == 0) {
            return index;
        }
    }
    return SIZE_MAX;
}

static size_t function_local_find(
    const FunctionDeclaration *function,
    const char *name
) {
    for (size_t index = 0; index < function->local_count; ++index) {
        if (strcmp(function->locals[index], name) == 0) {
            return index;
        }
    }
    return SIZE_MAX;
}

static size_t function_binding_find(
    const FunctionDeclaration *function,
    const char *name
) {
    size_t parameter = function_parameter_find(function, name);
    if (parameter != SIZE_MAX) return parameter;
    size_t local = function_local_find(function, name);
    if (local == SIZE_MAX) return SIZE_MAX;
    return function->parameter_count + local;
}

static bool function_body_end(
    FunctionParser *parser,
    size_t *body_end
) {
    size_t depth = 1;
    bool in_text = false;
    bool escaped = false;
    while (parser->cursor < parser->limit) {
        char value = parser->source[parser->cursor++];
        if (in_text) {
            if (escaped) {
                escaped = false;
            } else if (value == '\\') {
                escaped = true;
            } else if (value == '"') {
                in_text = false;
            }
            continue;
        }
        if (value == '"') {
            in_text = true;
            continue;
        }
        if (value == '#') {
            while (parser->cursor < parser->limit &&
                   parser->source[parser->cursor] != '\n') {
                ++parser->cursor;
            }
            continue;
        }
        if (value == '{') {
            ++depth;
        } else if (value == '}') {
            --depth;
            if (depth == 0) {
                *body_end = parser->cursor - 1;
                return true;
            }
        }
    }
    function_error(parser, "unterminated native Core function body");
    return false;
}

static void function_expression_free(FunctionExpression *expression) {
    if (expression == NULL) return;
    function_expression_free(expression->left);
    function_expression_free(expression->right);
    for (size_t index = 0; index < expression->argument_count; ++index) {
        function_expression_free(expression->arguments[index]);
    }
    free(expression->arguments);
    free(expression);
}

static void function_program_free(FunctionProgram *program) {
    for (size_t function_index = 0;
         function_index < program->function_count;
         ++function_index) {
        FunctionDeclaration *function =
            &program->functions[function_index];
        for (size_t statement_index = 0;
             statement_index < function->statement_count;
             ++statement_index) {
            FunctionStatement *statement =
                &function->statements[statement_index];
            function_expression_free(statement->condition);
            function_expression_free(statement->value);
        }
        function->statement_count = 0;
    }
    program->function_count = 0;
}

static bool function_headers(
    const char *source,
    FunctionProgram *program,
    char error[256],
    size_t *error_at
) {
    memset(program, 0, sizeof(*program));
    program->source = source;
    program->main_index = SIZE_MAX;
    FunctionParser parser = {
        .source = source,
        .cursor = 0,
        .limit = strlen(source),
    };

    while (true) {
        function_skip_trivia(&parser);
        if (parser.cursor >= parser.limit) break;
        if (program->function_count >= MAX_CORE_FUNCTIONS) {
            function_error(
                &parser,
                "native Core has too many functions"
            );
            break;
        }
        size_t declaration_at = parser.cursor;
        if (!function_consume_word(&parser, "fn")) {
            function_error(
                &parser,
                "expected top-level native Core function"
            );
            break;
        }

        FunctionDeclaration *function =
            &program->functions[program->function_count];
        function->declaration_line = source_line(source, declaration_at);
        if (!function_identifier(&parser, function->name)) {
            function_error(&parser, "expected native Core function name");
            break;
        }
        if (function_find(program, function->name) != SIZE_MAX) {
            function_error(
                &parser,
                "duplicate native Core function `%s`",
                function->name
            );
            break;
        }
        if (!function_consume_char(&parser, '(')) {
            function_error(
                &parser,
                "expected `(` after native Core function name"
            );
            break;
        }
        function_skip_trivia(&parser);
        if (!function_consume_char(&parser, ')')) {
            for (;;) {
                if (function->parameter_count >= MAX_CORE_PARAMETERS) {
                    function_error(
                        &parser,
                        "native Core functions support at most six arguments"
                    );
                    break;
                }
                char *parameter =
                    function->parameters[function->parameter_count];
                if (!function_identifier(&parser, parameter)) {
                    function_error(
                        &parser,
                        "expected native Core parameter name"
                    );
                    break;
                }
                if (function_parameter_find(function, parameter) !=
                    SIZE_MAX) {
                    function_error(
                        &parser,
                        "duplicate native Core parameter `%s`",
                        parameter
                    );
                    break;
                }
                if (!function_consume_char(&parser, ':') ||
                    !function_consume_word(&parser, "Int")) {
                    function_error(
                        &parser,
                        "native Core function parameters must have type Int"
                    );
                    break;
                }
                ++function->parameter_count;
                if (function_consume_char(&parser, ')')) break;
                if (!function_consume_char(&parser, ',')) {
                    function_error(
                        &parser,
                        "expected `,` between native Core parameters"
                    );
                    break;
                }
            }
        }
        if (parser.error[0] != '\0') break;

        bool is_main = strcmp(function->name, "main") == 0;
        if (function_consume_pair(&parser, '-', '>')) {
            if (!function_consume_word(&parser, "Int")) {
                function_error(
                    &parser,
                    "native Core functions must return Int"
                );
                break;
            }
        } else if (!is_main) {
            function_error(
                &parser,
                "native Core helper functions require `-> Int`"
            );
            break;
        }
        if (is_main && function->parameter_count != 0) {
            function_error(
                &parser,
                "native Core main must not accept arguments"
            );
            break;
        }
        if (!function_consume_char(&parser, '{')) {
            function_error(
                &parser,
                "expected `{` to start native Core function body"
            );
            break;
        }
        function->body_start = parser.cursor;
        if (!function_body_end(&parser, &function->body_end)) break;
        if (is_main) program->main_index = program->function_count;
        ++program->function_count;
    }

    if (parser.error[0] == '\0' && program->main_index == SIZE_MAX) {
        function_error(&parser, "native Core program has no main function");
    }
    if (parser.error[0] != '\0') {
        memcpy(error, parser.error, sizeof(parser.error));
        *error_at = parser.cursor;
        return false;
    }
    return true;
}

static FunctionExpression *function_expression(
    FunctionExpressionKind kind,
    FunctionValueKind value_kind,
    size_t line,
    FunctionExpression *left,
    FunctionExpression *right
) {
    FunctionExpression *expression = allocate(sizeof(*expression));
    expression->kind = kind;
    expression->value_kind = value_kind;
    expression->value = 0;
    expression->source_line = line;
    expression->slot = 0;
    expression->function_index = 0;
    expression->left = left;
    expression->right = right;
    expression->arguments = NULL;
    expression->argument_count = 0;
    return expression;
}

static FunctionExpression *function_parse_expression(
    FunctionParser *parser
);

static FunctionExpression *function_parse_atom(FunctionParser *parser) {
    function_skip_trivia(parser);
    size_t atom_at = parser->cursor;
    if (function_consume_char(parser, '(')) {
        FunctionExpression *inside = function_parse_expression(parser);
        if (!function_consume_char(parser, ')')) {
            function_error(
                parser,
                "expected `)` in native Core function expression"
            );
        }
        return inside;
    }

    function_skip_trivia(parser);
    if (parser->cursor < parser->limit &&
        isdigit((unsigned char)parser->source[parser->cursor])) {
        uint64_t value = 0;
        const uint64_t limit = (uint64_t)INT64_MAX;
        while (parser->cursor < parser->limit &&
               isdigit((unsigned char)parser->source[parser->cursor])) {
            unsigned digit =
                (unsigned)(parser->source[parser->cursor++] - '0');
            if (value > (limit - digit) / 10) {
                function_error(
                    parser,
                    "native Core integer literal exceeds Int64"
                );
                return NULL;
            }
            value = value * 10 + digit;
        }
        if (value > UINT64_C(65535)) {
            parser->program->extended_numeric = true;
            parser->program->requires_extended_numeric_lowering = true;
        }
        FunctionExpression *literal = function_expression(
            FUNCTION_LITERAL,
            FUNCTION_VALUE_INT,
            source_line(parser->source, atom_at),
            NULL,
            NULL
        );
        literal->value = (int64_t)value;
        return literal;
    }

    char name[MAX_CORE_NAME];
    if (!function_identifier(parser, name)) {
        function_error(
            parser,
            "expected Int expression in native Core function"
        );
        return NULL;
    }
    if (function_consume_char(parser, '(')) {
        size_t target = function_find(parser->program, name);
        if (target == SIZE_MAX) {
            function_error(
                parser,
                "unknown native Core function `%s`",
                name
            );
            return NULL;
        }
        if (target == parser->program->main_index) {
            function_error(parser, "native Core main cannot be called");
            return NULL;
        }
        FunctionExpression *call = function_expression(
            FUNCTION_CALL,
            FUNCTION_VALUE_INT,
            source_line(parser->source, atom_at),
            NULL,
            NULL
        );
        call->function_index = target;
        size_t expected =
            parser->program->functions[target].parameter_count;
        if (expected > 0) {
            call->arguments = allocate(
                expected * sizeof(*call->arguments)
            );
        }
        function_skip_trivia(parser);
        if (!function_consume_char(parser, ')')) {
            for (;;) {
                if (call->argument_count >= expected) {
                    function_error(
                        parser,
                        "native Core function `%s` expects %zu arguments",
                        name,
                        expected
                    );
                    return call;
                }
                FunctionExpression *argument =
                    function_parse_expression(parser);
                if (argument == NULL) return call;
                if (argument->value_kind != FUNCTION_VALUE_INT) {
                    function_error(
                        parser,
                        "native Core function arguments must have type Int"
                    );
                    return call;
                }
                call->arguments[call->argument_count++] = argument;
                if (function_consume_char(parser, ')')) break;
                if (!function_consume_char(parser, ',')) {
                    function_error(
                        parser,
                        "expected `,` between native Core arguments"
                    );
                    return call;
                }
            }
        }
        if (call->argument_count != expected) {
            function_error(
                parser,
                "native Core function `%s` expects %zu arguments, got %zu",
                name,
                expected,
                call->argument_count
            );
        }
        return call;
    }

    size_t binding_slot = function_binding_find(parser->function, name);
    if (binding_slot == SIZE_MAX) {
        function_error(parser, "unknown native Core binding `%s`", name);
        return NULL;
    }
    FunctionExpression *binding = function_expression(
        FUNCTION_PARAMETER,
        FUNCTION_VALUE_INT,
        source_line(parser->source, atom_at),
        NULL,
        NULL
    );
    binding->slot = binding_slot;
    return binding;
}

static FunctionExpression *function_parse_unary(FunctionParser *parser) {
    function_skip_trivia(parser);
    size_t operator_at = parser->cursor;
    if (function_consume_char(parser, '-')) {
        parser->program->extended_numeric = true;
        FunctionExpression *value = function_parse_unary(parser);
        if (value != NULL &&
            value->value_kind != FUNCTION_VALUE_INT) {
            function_error(
                parser,
                "native Core unary `-` requires Int"
            );
        }
        return function_expression(
            FUNCTION_NEGATE,
            FUNCTION_VALUE_INT,
            source_line(parser->source, operator_at),
            value,
            NULL
        );
    }
    return function_parse_atom(parser);
}

static FunctionExpression *function_parse_product(FunctionParser *parser) {
    FunctionExpression *left = function_parse_unary(parser);
    while (parser->error[0] == '\0') {
        function_skip_trivia(parser);
        if (parser->cursor >= parser->limit) break;
        FunctionExpressionKind kind;
        size_t operator_at = parser->cursor;
        if (parser->source[parser->cursor] == '*') {
            kind = FUNCTION_MULTIPLY;
            ++parser->cursor;
        } else if (parser->source[parser->cursor] == '%') {
            kind = FUNCTION_FLOOR_MODULO;
            ++parser->cursor;
            parser->program->extended_numeric = true;
            parser->program->requires_extended_numeric_lowering = true;
        } else if (parser->cursor + 1 < parser->limit &&
                   parser->source[parser->cursor] == '/' &&
                   parser->source[parser->cursor + 1] == '/') {
            kind = FUNCTION_FLOOR_DIVIDE;
            parser->cursor += 2;
            parser->program->extended_numeric = true;
            parser->program->requires_extended_numeric_lowering = true;
        } else {
            break;
        }
        FunctionExpression *right = function_parse_unary(parser);
        if (left == NULL || right == NULL) return left;
        if (left->value_kind != FUNCTION_VALUE_INT ||
            right->value_kind != FUNCTION_VALUE_INT) {
            function_error(
                parser,
                "native Core product operator requires Int operands"
            );
            return left;
        }
        left = function_expression(
            kind,
            FUNCTION_VALUE_INT,
            source_line(parser->source, operator_at),
            left,
            right
        );
    }
    return left;
}

static FunctionExpression *function_parse_sum(FunctionParser *parser) {
    FunctionExpression *left = function_parse_product(parser);
    while (parser->error[0] == '\0') {
        function_skip_trivia(parser);
        if (parser->cursor >= parser->limit ||
            (parser->source[parser->cursor] != '+' &&
             parser->source[parser->cursor] != '-')) {
            break;
        }
        char operator = parser->source[parser->cursor];
        size_t operator_at = parser->cursor++;
        if (operator == '-') parser->program->extended_numeric = true;
        FunctionExpression *right = function_parse_product(parser);
        if (left == NULL || right == NULL) return left;
        if (left->value_kind != FUNCTION_VALUE_INT ||
            right->value_kind != FUNCTION_VALUE_INT) {
            function_error(
                parser,
                "native Core arithmetic requires Int operands"
            );
            return left;
        }
        left = function_expression(
            operator == '+' ? FUNCTION_ADD : FUNCTION_SUBTRACT,
            FUNCTION_VALUE_INT,
            source_line(parser->source, operator_at),
            left,
            right
        );
    }
    return left;
}

static FunctionExpression *function_parse_expression(
    FunctionParser *parser
) {
    FunctionExpression *left = function_parse_sum(parser);
    if (left == NULL || parser->error[0] != '\0') return left;
    function_skip_trivia(parser);
    size_t operator_at = parser->cursor;
    FunctionExpressionKind kind;
    bool comparison = true;
    if (function_consume_pair(parser, '=', '=')) {
        kind = FUNCTION_EQUAL;
    } else if (function_consume_pair(parser, '!', '=')) {
        kind = FUNCTION_NOT_EQUAL;
    } else if (function_consume_pair(parser, '<', '=')) {
        kind = FUNCTION_LESS_EQUAL;
    } else if (function_consume_pair(parser, '>', '=')) {
        kind = FUNCTION_GREATER_EQUAL;
    } else if (function_consume_char(parser, '<')) {
        kind = FUNCTION_LESS;
    } else if (function_consume_char(parser, '>')) {
        kind = FUNCTION_GREATER;
    } else {
        comparison = false;
        kind = FUNCTION_EQUAL;
    }
    if (!comparison) return left;

    FunctionExpression *right = function_parse_sum(parser);
    if (right == NULL) return left;
    if (left->value_kind != FUNCTION_VALUE_INT ||
        right->value_kind != FUNCTION_VALUE_INT) {
        function_error(
            parser,
            "native Core comparison requires Int operands"
        );
        return left;
    }
    return function_expression(
        kind,
        FUNCTION_VALUE_BOOL,
        source_line(parser->source, operator_at),
        left,
        right
    );
}

static bool function_statement_add(
    FunctionParser *parser,
    FunctionDeclaration *function,
    FunctionStatement statement
) {
    if (function->statement_count >= MAX_CORE_STATEMENTS) {
        function_error(
            parser,
            "native Core function has too many statements"
        );
        return false;
    }
    function->statements[function->statement_count++] = statement;
    return true;
}

static bool function_bodies(
    FunctionProgram *program,
    char error[256],
    size_t *error_at
) {
    for (size_t function_index = 0;
         function_index < program->function_count;
         ++function_index) {
        FunctionDeclaration *function =
            &program->functions[function_index];
        FunctionParser parser = {
            .source = program->source,
            .cursor = function->body_start,
            .limit = function->body_end,
            .program = program,
            .function = function,
        };
        bool is_main = function_index == program->main_index;
        while (true) {
            function_skip_trivia(&parser);
            if (parser.cursor >= parser.limit) break;
            size_t statement_at = parser.cursor;
            FunctionStatement statement = {
                .source_line = source_line(program->source, statement_at),
            };
            if (function_consume_word(&parser, "let")) {
                statement.kind = FUNCTION_STATEMENT_LET;
                if (function->local_count >= MAX_CORE_BINDINGS) {
                    function_error(
                        &parser,
                        "native Core function has too many local bindings"
                    );
                } else {
                    char name[MAX_CORE_NAME];
                    if (!function_identifier(&parser, name)) {
                        function_error(
                            &parser,
                            "expected native Core local binding name"
                        );
                    } else if (
                        function_binding_find(function, name) != SIZE_MAX) {
                        function_error(
                            &parser,
                            "duplicate native Core binding `%s`",
                            name
                        );
                    } else if (!function_consume_char(&parser, '=')) {
                        function_error(
                            &parser,
                            "expected `=` after native Core local binding"
                        );
                    } else {
                        statement.value =
                            function_parse_expression(&parser);
                        if (statement.value == NULL ||
                            statement.value->value_kind !=
                                FUNCTION_VALUE_INT) {
                            function_error(
                                &parser,
                                "native Core local binding must be Int"
                            );
                        } else {
                            statement.slot =
                                function->parameter_count +
                                function->local_count;
                            memcpy(
                                function->locals[function->local_count],
                                name,
                                sizeof(name)
                            );
                            ++function->local_count;
                            program->extended_numeric = true;
                            program->requires_extended_numeric_lowering =
                                true;
                        }
                    }
                }
            } else if (function_consume_word(&parser, "if")) {
                statement.kind = FUNCTION_STATEMENT_IF_RETURN;
                statement.condition = function_parse_expression(&parser);
                if (statement.condition == NULL ||
                    statement.condition->value_kind !=
                        FUNCTION_VALUE_BOOL) {
                    function_error(
                        &parser,
                        "native Core if condition must have type Bool"
                    );
                } else if (!function_consume_char(&parser, '{') ||
                           !function_consume_word(&parser, "return")) {
                    function_error(
                        &parser,
                        "native Core if body must be `{ return Int }`"
                    );
                } else {
                    statement.value =
                        function_parse_expression(&parser);
                    if (statement.value == NULL ||
                        statement.value->value_kind !=
                            FUNCTION_VALUE_INT ||
                        !function_consume_char(&parser, '}')) {
                        function_error(
                            &parser,
                            "native Core if body must return Int"
                        );
                    }
                }
            } else if (function_consume_word(&parser, "return")) {
                statement.kind = FUNCTION_STATEMENT_RETURN;
                statement.value = function_parse_expression(&parser);
                if (statement.value == NULL ||
                    statement.value->value_kind != FUNCTION_VALUE_INT) {
                    function_error(
                        &parser,
                        "native Core function must return Int"
                    );
                }
            } else if (function_consume_word(&parser, "print")) {
                statement.kind = FUNCTION_STATEMENT_PRINT;
                for (size_t previous = 0;
                     previous < function->statement_count;
                     ++previous) {
                    if (function->statements[previous].kind ==
                        FUNCTION_STATEMENT_PRINT) {
                        program->extended_numeric = true;
                        break;
                    }
                }
                if (!is_main) {
                    function_error(
                        &parser,
                        "native Core print is only supported in main"
                    );
                } else if (!function_consume_char(&parser, '(')) {
                    function_error(
                        &parser,
                        "expected `(` after native Core print"
                    );
                } else {
                    statement.value =
                        function_parse_expression(&parser);
                    if (statement.value == NULL ||
                        statement.value->value_kind !=
                            FUNCTION_VALUE_INT ||
                        !function_consume_char(&parser, ')')) {
                        function_error(
                            &parser,
                            "native Core print requires one Int"
                        );
                    }
                }
            } else {
                statement.kind = FUNCTION_STATEMENT_EXPRESSION;
                statement.value = function_parse_expression(&parser);
                if (statement.value == NULL ||
                    statement.value->value_kind != FUNCTION_VALUE_INT) {
                    function_error(
                        &parser,
                        "native Core expression statement must produce Int"
                    );
                }
            }
            if (!function_statement_add(&parser, function, statement) ||
                parser.error[0] != '\0') {
                break;
            }
        }
        if (parser.error[0] == '\0' && !is_main) {
            if (function->statement_count == 0 ||
                function->statements[
                    function->statement_count - 1
                ].kind != FUNCTION_STATEMENT_RETURN) {
                function_error(
                    &parser,
                    "native Core helper function must end with return"
                );
            }
        }
        if (parser.error[0] != '\0') {
            memcpy(error, parser.error, sizeof(parser.error));
            *error_at = parser.cursor;
            return false;
        }
    }
    return true;
}

static size_t register_depth(const Node *expression) {
    if (expression->kind == NODE_LITERAL ||
        expression->kind == NODE_TEXT_LITERAL ||
        expression->kind == NODE_VARIABLE ||
        expression->kind == NODE_PARAMETER) {
        return 1;
    }
    if (expression->kind == NODE_LET) {
        size_t initializer = register_depth(expression->left);
        size_t body = register_depth(expression->right);
        return initializer > body ? initializer : body;
    }
    if (expression->kind == NODE_NEGATE ||
        expression->kind == NODE_LENGTH) {
        return register_depth(expression->left);
    }
    if (expression->kind == NODE_LIST ||
        expression->kind == NODE_CHARS ||
        expression->kind == NODE_CODEPOINTS ||
        expression->kind == NODE_BYTES) {
        size_t depth = 1;
        if (expression->kind == NODE_CHARS ||
            expression->kind == NODE_CODEPOINTS ||
            expression->kind == NODE_BYTES) {
            depth = register_depth(expression->left);
        }
        for (size_t index = 0; index < expression->item_count; ++index) {
            size_t item = 1 + register_depth(expression->items[index]);
            if (item > depth) depth = item;
        }
        return depth;
    }
    size_t left = register_depth(expression->left);
    size_t right = register_depth(expression->right);
    size_t with_left_live = 1 + right;
    return left > with_left_live ? left : with_left_live;
}

static void free_node(Node *expression) {
    if (expression == NULL) return;
    free_node(expression->left);
    free_node(expression->right);
    free_node(expression->third);
    for (size_t index = 0;
         expression->items != NULL && index < expression->item_count;
         ++index) {
        free_node(expression->items[index]);
    }
    free(expression->text_value);
    free(expression->items);
    free(expression);
}

static bool uses_list(const Node *expression) {
    if (expression == NULL) return false;
    if (expression->kind == NODE_LIST ||
        expression->kind == NODE_CHARS ||
        expression->kind == NODE_CODEPOINTS ||
        expression->kind == NODE_BYTES ||
        expression->kind == NODE_MAP ||
        expression->kind == NODE_FILTER ||
        expression->kind == NODE_FOLD ||
        (expression->kind == NODE_INDEX &&
         expression->left->value_kind == VALUE_LIST) ||
        (expression->kind == NODE_LENGTH &&
         expression->left->value_kind == VALUE_LIST)) {
        return true;
    }
    if (uses_list(expression->left) ||
        uses_list(expression->right) ||
        uses_list(expression->third)) {
        return true;
    }
    for (size_t index = 0;
         expression->items != NULL && index < expression->item_count;
         ++index) {
        if (uses_list(expression->items[index])) return true;
    }
    return false;
}

static bool uses_text(const Node *expression) {
    if (expression == NULL) return false;
    if (expression->value_kind == VALUE_TEXT ||
        expression->element_kind == VALUE_TEXT ||
        expression->kind == NODE_TEXT_EQUAL ||
        expression->kind == NODE_TEXT_NOT_EQUAL ||
        (expression->kind == NODE_LENGTH &&
         expression->left->value_kind == VALUE_TEXT)) {
        return true;
    }
    if (uses_text(expression->left) ||
        uses_text(expression->right) ||
        uses_text(expression->third)) {
        return true;
    }
    for (size_t index = 0;
         expression->items != NULL && index < expression->item_count;
         ++index) {
        if (uses_text(expression->items[index])) return true;
    }
    return false;
}

static bool uses_local_bindings(const Node *expression) {
    if (expression == NULL) return false;
    if (expression->kind == NODE_LET ||
        expression->kind == NODE_VARIABLE ||
        expression->kind == NODE_PARAMETER) {
        return true;
    }
    if (uses_local_bindings(expression->left) ||
        uses_local_bindings(expression->right) ||
        uses_local_bindings(expression->third)) {
        return true;
    }
    for (size_t index = 0;
         expression->items != NULL && index < expression->item_count;
         ++index) {
        if (uses_local_bindings(expression->items[index])) return true;
    }
    return false;
}

static void x64_mov_eax_imm32(Bytes *text, uint32_t value) {
    byte(text, UINT8_C(0xb8));
    u32_le(text, value);
}

typedef struct {
    size_t *fields;
    size_t length;
    size_t capacity;
} Offsets;

typedef struct {
    size_t field;
    const Node *literal;
} TextFixup;

typedef struct {
    TextFixup *items;
    size_t length;
    size_t capacity;
} TextFixups;

typedef struct {
    bool used;
    Offsets alloc_calls;
    Offsets oom_jumps;
    Offsets list_index_jumps;
    Offsets text_index_jumps;
    Offsets text_concat_calls;
    Offsets text_equal_calls;
    Offsets text_length_calls;
    Offsets text_index_calls;
    Offsets text_chars_calls;
    Offsets newline_addresses;
    Offsets bool_true_addresses;
    Offsets bool_false_addresses;
    TextFixups text_literals;
} X64Runtime;

static void offsets_add(Offsets *offsets, size_t field) {
    if (offsets->length == offsets->capacity) {
        size_t capacity =
            offsets->capacity == 0 ? 8 : offsets->capacity * 2;
        size_t *grown = realloc(
            offsets->fields,
            capacity * sizeof(*offsets->fields)
        );
        if (grown == NULL) fatal("out of memory");
        offsets->fields = grown;
        offsets->capacity = capacity;
    }
    offsets->fields[offsets->length++] = field;
}

static void text_fixups_add(
    TextFixups *fixups,
    size_t field,
    const Node *literal
) {
    if (fixups->length == fixups->capacity) {
        size_t capacity =
            fixups->capacity == 0 ? 8 : fixups->capacity * 2;
        TextFixup *grown = realloc(
            fixups->items,
            capacity * sizeof(*fixups->items)
        );
        if (grown == NULL) fatal("out of memory");
        fixups->items = grown;
        fixups->capacity = capacity;
    }
    fixups->items[fixups->length++] = (TextFixup){
        .field = field,
        .literal = literal,
    };
}

static void x64_runtime_free(X64Runtime *runtime) {
    free(runtime->alloc_calls.fields);
    free(runtime->oom_jumps.fields);
    free(runtime->list_index_jumps.fields);
    free(runtime->text_index_jumps.fields);
    free(runtime->text_concat_calls.fields);
    free(runtime->text_equal_calls.fields);
    free(runtime->text_length_calls.fields);
    free(runtime->text_index_calls.fields);
    free(runtime->text_chars_calls.fields);
    free(runtime->newline_addresses.fields);
    free(runtime->bool_true_addresses.fields);
    free(runtime->bool_false_addresses.fields);
    free(runtime->text_literals.items);
}

static void x64_patch_rel32(Bytes *text, size_t field, size_t target) {
    int64_t displacement =
        (int64_t)target - (int64_t)(field + sizeof(uint32_t));
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        fatal("x86-64 native Core rel32 is out of range");
    }
    uint32_t encoded = (uint32_t)(int32_t)displacement;
    if (field > text->length || text->length - field < sizeof(encoded)) {
        fatal("x86-64 native Core rel32 field is outside text");
    }
    for (unsigned index = 0; index < 4; ++index) {
        text->data[field + index] =
            (uint8_t)(encoded >> (index * 8));
    }
}

static void x64_rel32_placeholder(
    Bytes *text,
    uint8_t first,
    uint8_t second,
    Offsets *offsets
) {
    byte(text, first);
    byte(text, second);
    offsets_add(offsets, text->length);
    u32_le(text, 0);
}

static void x64_call_alloc(Bytes *text, X64Runtime *runtime) {
    runtime->used = true;
    byte(text, UINT8_C(0xe8));
    offsets_add(&runtime->alloc_calls, text->length);
    u32_le(text, 0);
}

static void x64_call_runtime(
    Bytes *text,
    X64Runtime *runtime,
    Offsets *calls
) {
    runtime->used = true;
    byte(text, UINT8_C(0xe8));
    offsets_add(calls, text->length);
    u32_le(text, 0);
}

static uint32_t x64_local_displacement(size_t slot) {
    if (slot >= (size_t)INT32_MAX / sizeof(uint64_t)) {
        fatal("native Core local frame is too large");
    }
    int32_t displacement =
        -(int32_t)((slot + 1) * sizeof(uint64_t));
    return (uint32_t)displacement;
}

static void x64_load_local(
    Bytes *text,
    size_t slot
) {
    byte(text, UINT8_C(0x48));
    byte(text, UINT8_C(0x8b));
    byte(text, UINT8_C(0x85)); /* mov rax, [rbp + disp32] */
    u32_le(text, x64_local_displacement(slot));
}

static void x64_store_local(
    Bytes *text,
    size_t slot
) {
    byte(text, UINT8_C(0x48));
    byte(text, UINT8_C(0x89));
    byte(text, UINT8_C(0x85)); /* mov [rbp + disp32], rax */
    u32_le(text, x64_local_displacement(slot));
}

static size_t x64_local_jcc(Bytes *text, uint8_t condition);
static size_t x64_local_jmp(Bytes *text);
static void x64_emit(
    Bytes *text,
    const uint8_t *instructions,
    size_t length
);

static void x64_expression(
    Bytes *text,
    const Node *expression,
    LineRows *rows,
    X64Runtime *runtime
) {
    if (expression->kind == NODE_INDEX &&
        expression->left->value_kind == VALUE_TEXT &&
        expression->left->value_known &&
        expression->right->value_known &&
        !expression->value_known) {
        runtime->used = true;
        byte(text, UINT8_C(0xe9)); /* known grapheme OOB -> Text error */
        offsets_add(&runtime->text_index_jumps, text->length);
        u32_le(text, 0);
        x64_mov_eax_imm32(text, 0);
        byte(text, UINT8_C(0x50)); /* unreachable stack result */
        return;
    }

    if (expression->kind == NODE_LENGTH &&
        expression->value_known) {
        line_row(rows, text->length, expression->source_line);
        x64_mov_eax_imm32(text, (uint32_t)expression->value);
        byte(text, UINT8_C(0x50)); /* push compile-time length */
        return;
    }

    if (expression->value_kind == VALUE_TEXT &&
        expression->value_known &&
        expression->text_value != NULL &&
        expression->kind != NODE_TEXT_LITERAL &&
        expression->kind != NODE_TEXT_CONCAT) {
        runtime->used = true;
        line_row(rows, text->length, expression->source_line);
        byte(text, UINT8_C(0xb8)); /* mov eax, Text address */
        text_fixups_add(
            &runtime->text_literals,
            text->length,
            expression
        );
        u32_le(text, 0);
        byte(text, UINT8_C(0x50)); /* push folded Text */
        return;
    }

    if (expression->kind == NODE_LITERAL) {
        line_row(rows, text->length, expression->source_line);
        x64_mov_eax_imm32(text, (uint32_t)expression->value);
        byte(text, UINT8_C(0x50)); /* push rax */
        return;
    }

    if (expression->kind == NODE_VARIABLE ||
        expression->kind == NODE_PARAMETER) {
        line_row(rows, text->length, expression->source_line);
        x64_load_local(text, expression->slot);
        byte(text, UINT8_C(0x50)); /* push local value */
        return;
    }

    if (expression->kind == NODE_LET) {
        x64_expression(text, expression->left, rows, runtime);
        line_row(rows, text->length, expression->source_line);
        byte(text, UINT8_C(0x58)); /* pop initializer */
        x64_store_local(text, expression->slot);
        x64_expression(text, expression->right, rows, runtime);
        return;
    }

    if (expression->kind == NODE_TEXT_LITERAL) {
        runtime->used = true;
        line_row(rows, text->length, expression->source_line);
        byte(text, UINT8_C(0xb8)); /* mov eax, Text address */
        text_fixups_add(
            &runtime->text_literals,
            text->length,
            expression
        );
        u32_le(text, 0);
        byte(text, UINT8_C(0x50)); /* push Text */
        return;
    }

    if (expression->kind == NODE_NEGATE) {
        x64_expression(text, expression->left, rows, runtime);
        line_row(rows, text->length, expression->source_line);
        byte(text, UINT8_C(0x58)); /* pop rax */
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0xf7));
        byte(text, UINT8_C(0xd8)); /* neg rax */
        byte(text, UINT8_C(0x50)); /* push result */
        return;
    }

    if (expression->kind == NODE_CHARS &&
        !expression->value_known) {
        x64_expression(text, expression->left, rows, runtime);
        line_row(rows, text->length, expression->source_line);
        byte(text, UINT8_C(0x5f)); /* pop rdi: source Text */
        x64_call_runtime(
            text,
            runtime,
            &runtime->text_chars_calls
        );
        byte(text, UINT8_C(0x50)); /* push List[Text] */
        return;
    }

    if (expression->kind == NODE_LIST ||
        expression->kind == NODE_CHARS ||
        expression->kind == NODE_CODEPOINTS ||
        expression->kind == NODE_BYTES) {
        if (expression->item_count >
            (UINT32_MAX - 8) / sizeof(uint64_t)) {
            fatal("native Core list is too large");
        }
        runtime->used = true;
        line_row(rows, text->length, expression->source_line);
        uint32_t bytes = (uint32_t)(
            8 + expression->item_count * sizeof(uint64_t)
        );
        byte(text, UINT8_C(0xbf)); /* mov edi, allocation size */
        u32_le(text, bytes);
        x64_call_alloc(text, runtime);
        byte(text, UINT8_C(0x50)); /* keep list pointer on stack */

        byte(text, UINT8_C(0xb9)); /* mov ecx, element count */
        u32_le(text, (uint32_t)expression->item_count);
        const uint8_t list_from_stack[] = {
            UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x14), UINT8_C(0x24),
        };
        for (size_t index = 0; index < sizeof(list_from_stack); ++index) {
            byte(text, list_from_stack[index]); /* mov rdx, [rsp] */
        }
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x89));
        byte(text, UINT8_C(0x0a)); /* mov [rdx], rcx */

        for (size_t index = 0; index < expression->item_count; ++index) {
            x64_expression(text, expression->items[index], rows, runtime);
            byte(text, UINT8_C(0x59)); /* pop rcx */
            for (size_t part = 0; part < sizeof(list_from_stack); ++part) {
                byte(text, list_from_stack[part]); /* mov rdx, [rsp] */
            }
            byte(text, UINT8_C(0x48));
            byte(text, UINT8_C(0x89));
            byte(text, UINT8_C(0x8a)); /* mov [rdx + disp32], rcx */
            u32_le(text, (uint32_t)(8 + index * sizeof(uint64_t)));
        }
        return;
    }

    if (expression->kind == NODE_MAP) {
        x64_expression(text, expression->left, rows, runtime);
        line_row(rows, text->length, expression->source_line);
        byte(text, UINT8_C(0x5b)); /* pop rbx: source list */
        const uint8_t map_allocate[] = {
            UINT8_C(0x4c), UINT8_C(0x8b), UINT8_C(0x33), /* r14 = len */
            UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xf7), /* rdi = len */
            UINT8_C(0x48), UINT8_C(0xc1), UINT8_C(0xe7), UINT8_C(0x03),
            UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xc7), UINT8_C(0x08),
        };
        x64_emit(text, map_allocate, sizeof(map_allocate));
        x64_call_alloc(text, runtime);
        const uint8_t map_open[] = {
            UINT8_C(0x49), UINT8_C(0x89), UINT8_C(0xc4), /* r12 = output */
            UINT8_C(0x4d), UINT8_C(0x89), UINT8_C(0x34), UINT8_C(0x24),
            UINT8_C(0x45), UINT8_C(0x31), UINT8_C(0xed), /* r13 = 0 */
        };
        x64_emit(text, map_open, sizeof(map_open));
        size_t loop = text->length;
        const uint8_t map_compare[] = {
            UINT8_C(0x4d), UINT8_C(0x39), UINT8_C(0xf5), /* r13 vs r14 */
        };
        x64_emit(text, map_compare, sizeof(map_compare));
        size_t done = x64_local_jcc(text, UINT8_C(0x8d)); /* jge */
        const uint8_t map_load[] = {
            UINT8_C(0x4a), UINT8_C(0x8b), UINT8_C(0x44),
            UINT8_C(0xeb), UINT8_C(0x08),
        };
        x64_emit(text, map_load, sizeof(map_load));
        x64_store_local(text, expression->slot);
        x64_expression(text, expression->right, rows, runtime);
        byte(text, UINT8_C(0x58)); /* pop mapped element */
        const uint8_t map_store_next[] = {
            UINT8_C(0x4b), UINT8_C(0x89), UINT8_C(0x44),
            UINT8_C(0xec), UINT8_C(0x08),
            UINT8_C(0x49), UINT8_C(0xff), UINT8_C(0xc5), /* inc r13 */
        };
        x64_emit(text, map_store_next, sizeof(map_store_next));
        size_t back = x64_local_jmp(text);
        size_t done_at = text->length;
        byte(text, UINT8_C(0x41));
        byte(text, UINT8_C(0x54)); /* push r12 */
        x64_patch_rel32(text, done, done_at);
        x64_patch_rel32(text, back, loop);
        return;
    }

    if (expression->kind == NODE_FILTER) {
        x64_expression(text, expression->left, rows, runtime);
        line_row(rows, text->length, expression->source_line);
        byte(text, UINT8_C(0x5b)); /* pop rbx: source list */
        const uint8_t filter_allocate[] = {
            UINT8_C(0x4c), UINT8_C(0x8b), UINT8_C(0x3b), /* r15 = len */
            UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xff), /* rdi = len */
            UINT8_C(0x48), UINT8_C(0xc1), UINT8_C(0xe7), UINT8_C(0x03),
            UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xc7), UINT8_C(0x08),
        };
        x64_emit(text, filter_allocate, sizeof(filter_allocate));
        x64_call_alloc(text, runtime);
        const uint8_t filter_open[] = {
            UINT8_C(0x49), UINT8_C(0x89), UINT8_C(0xc4), /* r12 = output */
            UINT8_C(0x45), UINT8_C(0x31), UINT8_C(0xed), /* r13 = index */
            UINT8_C(0x45), UINT8_C(0x31), UINT8_C(0xf6), /* r14 = count */
        };
        x64_emit(text, filter_open, sizeof(filter_open));
        size_t loop = text->length;
        const uint8_t filter_compare[] = {
            UINT8_C(0x4d), UINT8_C(0x39), UINT8_C(0xfd), /* r13 vs r15 */
        };
        x64_emit(text, filter_compare, sizeof(filter_compare));
        size_t done = x64_local_jcc(text, UINT8_C(0x8d)); /* jge */
        const uint8_t filter_load[] = {
            UINT8_C(0x4a), UINT8_C(0x8b), UINT8_C(0x44),
            UINT8_C(0xeb), UINT8_C(0x08),
        };
        x64_emit(text, filter_load, sizeof(filter_load));
        x64_store_local(text, expression->slot);
        x64_expression(text, expression->right, rows, runtime);
        byte(text, UINT8_C(0x58)); /* pop predicate */
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x85));
        byte(text, UINT8_C(0xc0)); /* test rax, rax */
        size_t skip = x64_local_jcc(text, UINT8_C(0x84)); /* je */
        x64_load_local(text, expression->slot);
        const uint8_t filter_store[] = {
            UINT8_C(0x4b), UINT8_C(0x89), UINT8_C(0x44),
            UINT8_C(0xf4), UINT8_C(0x08),
            UINT8_C(0x49), UINT8_C(0xff), UINT8_C(0xc6), /* inc r14 */
        };
        x64_emit(text, filter_store, sizeof(filter_store));
        size_t skip_at = text->length;
        const uint8_t filter_next[] = {
            UINT8_C(0x49), UINT8_C(0xff), UINT8_C(0xc5), /* inc r13 */
        };
        x64_emit(text, filter_next, sizeof(filter_next));
        size_t back = x64_local_jmp(text);
        size_t done_at = text->length;
        const uint8_t filter_close[] = {
            UINT8_C(0x4d), UINT8_C(0x89), UINT8_C(0x34), UINT8_C(0x24),
            UINT8_C(0x41), UINT8_C(0x54), /* push r12 */
        };
        x64_emit(text, filter_close, sizeof(filter_close));
        x64_patch_rel32(text, done, done_at);
        x64_patch_rel32(text, skip, skip_at);
        x64_patch_rel32(text, back, loop);
        return;
    }

    if (expression->kind == NODE_FOLD) {
        x64_expression(text, expression->left, rows, runtime);
        x64_expression(text, expression->right, rows, runtime);
        line_row(rows, text->length, expression->source_line);
        byte(text, UINT8_C(0x41));
        byte(text, UINT8_C(0x5e)); /* pop r14: accumulator */
        byte(text, UINT8_C(0x5b)); /* pop rbx: source list */
        const uint8_t fold_open[] = {
            UINT8_C(0x4c), UINT8_C(0x8b), UINT8_C(0x2b), /* r13 = len */
            UINT8_C(0x45), UINT8_C(0x31), UINT8_C(0xe4), /* r12 = index */
        };
        x64_emit(text, fold_open, sizeof(fold_open));
        size_t loop = text->length;
        const uint8_t fold_compare[] = {
            UINT8_C(0x4d), UINT8_C(0x39), UINT8_C(0xec), /* r12 vs r13 */
        };
        x64_emit(text, fold_compare, sizeof(fold_compare));
        size_t done = x64_local_jcc(text, UINT8_C(0x8d)); /* jge */
        byte(text, UINT8_C(0x4c));
        byte(text, UINT8_C(0x89));
        byte(text, UINT8_C(0xf0)); /* mov rax, r14 */
        x64_store_local(text, expression->slot);
        const uint8_t fold_load[] = {
            UINT8_C(0x4a), UINT8_C(0x8b), UINT8_C(0x44),
            UINT8_C(0xe3), UINT8_C(0x08),
        };
        x64_emit(text, fold_load, sizeof(fold_load));
        x64_store_local(text, expression->slot + 1);
        x64_expression(text, expression->third, rows, runtime);
        byte(text, UINT8_C(0x41));
        byte(text, UINT8_C(0x5e)); /* pop r14: next accumulator */
        byte(text, UINT8_C(0x49));
        byte(text, UINT8_C(0xff));
        byte(text, UINT8_C(0xc4)); /* inc r12 */
        size_t back = x64_local_jmp(text);
        size_t done_at = text->length;
        byte(text, UINT8_C(0x41));
        byte(text, UINT8_C(0x56)); /* push r14 */
        x64_patch_rel32(text, done, done_at);
        x64_patch_rel32(text, back, loop);
        return;
    }

    if (expression->kind == NODE_LENGTH) {
        x64_expression(text, expression->left, rows, runtime);
        line_row(rows, text->length, expression->source_line);
        byte(text, UINT8_C(0x5f)); /* pop rdi */
        if (expression->left->value_kind == VALUE_TEXT) {
            x64_call_runtime(
                text,
                runtime,
                &runtime->text_length_calls
            );
        } else {
            byte(text, UINT8_C(0x48));
            byte(text, UINT8_C(0x8b));
            byte(text, UINT8_C(0x07)); /* mov rax, [rdi] */
        }
        byte(text, UINT8_C(0x50)); /* push length */
        return;
    }

    if (expression->kind == NODE_INDEX) {
        runtime->used = true;
        x64_expression(text, expression->left, rows, runtime);
        x64_expression(text, expression->right, rows, runtime);
        line_row(rows, text->length, expression->source_line);
        if (expression->left->value_kind == VALUE_TEXT) {
            byte(text, UINT8_C(0x5e)); /* pop rsi: codepoint index */
            byte(text, UINT8_C(0x5f)); /* pop rdi: Text */
            x64_call_runtime(
                text,
                runtime,
                &runtime->text_index_calls
            );
            byte(text, UINT8_C(0x50)); /* push one-codepoint Text */
            return;
        }
        byte(text, UINT8_C(0x59)); /* pop rcx: index */
        byte(text, UINT8_C(0x5a)); /* pop rdx: list */
        byte(text, UINT8_C(0x4c));
        byte(text, UINT8_C(0x8b));
        byte(text, UINT8_C(0x02)); /* mov r8, [rdx] */
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x85));
        byte(text, UINT8_C(0xc9)); /* test rcx, rcx */
        byte(text, UINT8_C(0x0f));
        byte(text, UINT8_C(0x89)); /* jns nonnegative */
        size_t nonnegative = text->length;
        u32_le(text, 0);
        byte(text, UINT8_C(0x4c));
        byte(text, UINT8_C(0x01));
        byte(text, UINT8_C(0xc1)); /* add rcx, r8 */
        x64_patch_rel32(text, nonnegative, text->length);

        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x85));
        byte(text, UINT8_C(0xc9)); /* test rcx, rcx */
        x64_rel32_placeholder(
            text,
            UINT8_C(0x0f),
            UINT8_C(0x88),
            &runtime->list_index_jumps
        ); /* js index error */
        byte(text, UINT8_C(0x4c));
        byte(text, UINT8_C(0x39));
        byte(text, UINT8_C(0xc1)); /* cmp rcx, r8 */
        x64_rel32_placeholder(
            text,
            UINT8_C(0x0f),
            UINT8_C(0x8d),
            &runtime->list_index_jumps
        ); /* jge index error */
        const uint8_t load[] = {
            UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x44),
            UINT8_C(0xca), UINT8_C(0x08),
        };
        for (size_t index = 0; index < sizeof(load); ++index) {
            byte(text, load[index]); /* mov rax, [rdx + rcx*8 + 8] */
        }
        byte(text, UINT8_C(0x50)); /* push element */
        return;
    }

    if (expression->kind == NODE_TEXT_CONCAT ||
        expression->kind == NODE_TEXT_EQUAL ||
        expression->kind == NODE_TEXT_NOT_EQUAL) {
        x64_expression(text, expression->left, rows, runtime);
        x64_expression(text, expression->right, rows, runtime);
        line_row(rows, text->length, expression->source_line);
        byte(text, UINT8_C(0x5e)); /* pop rsi: right Text */
        byte(text, UINT8_C(0x5f)); /* pop rdi: left Text */
        if (expression->kind == NODE_TEXT_CONCAT) {
            x64_call_runtime(
                text,
                runtime,
                &runtime->text_concat_calls
            );
        } else {
            x64_call_runtime(
                text,
                runtime,
                &runtime->text_equal_calls
            );
            if (expression->kind == NODE_TEXT_NOT_EQUAL) {
                byte(text, UINT8_C(0x83));
                byte(text, UINT8_C(0xf0));
                byte(text, UINT8_C(0x01)); /* xor eax, 1 */
            }
        }
        byte(text, UINT8_C(0x50)); /* push result */
        return;
    }

    if (expression->kind == NODE_INT_EQUAL ||
        expression->kind == NODE_INT_NOT_EQUAL ||
        expression->kind == NODE_INT_LESS ||
        expression->kind == NODE_INT_LESS_EQUAL ||
        expression->kind == NODE_INT_GREATER ||
        expression->kind == NODE_INT_GREATER_EQUAL) {
        x64_expression(text, expression->left, rows, runtime);
        x64_expression(text, expression->right, rows, runtime);
        line_row(rows, text->length, expression->source_line);
        byte(text, UINT8_C(0x59)); /* pop rcx: right */
        byte(text, UINT8_C(0x58)); /* pop rax: left */
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x39));
        byte(text, UINT8_C(0xc8)); /* cmp rax, rcx */
        byte(text, UINT8_C(0x0f));
        uint8_t condition = UINT8_C(0x94); /* sete */
        if (expression->kind == NODE_INT_NOT_EQUAL) {
            condition = UINT8_C(0x95);
        } else if (expression->kind == NODE_INT_LESS) {
            condition = UINT8_C(0x9c);
        } else if (expression->kind == NODE_INT_LESS_EQUAL) {
            condition = UINT8_C(0x9e);
        } else if (expression->kind == NODE_INT_GREATER) {
            condition = UINT8_C(0x9f);
        } else if (expression->kind == NODE_INT_GREATER_EQUAL) {
            condition = UINT8_C(0x9d);
        }
        byte(text, condition);
        byte(text, UINT8_C(0xc0)); /* setcc al */
        byte(text, UINT8_C(0x0f));
        byte(text, UINT8_C(0xb6));
        byte(text, UINT8_C(0xc0)); /* movzx eax, al */
        byte(text, UINT8_C(0x50));
        return;
    }

    x64_expression(text, expression->left, rows, runtime);
    x64_expression(text, expression->right, rows, runtime);
    line_row(rows, text->length, expression->source_line);
    byte(text, UINT8_C(0x59)); /* pop rcx */
    byte(text, UINT8_C(0x58)); /* pop rax */
    if (expression->kind == NODE_ADD) {
        byte(text, UINT8_C(0x01));
        byte(text, UINT8_C(0xc8)); /* add eax, ecx */
    } else {
        byte(text, UINT8_C(0x0f));
        byte(text, UINT8_C(0xaf));
        byte(text, UINT8_C(0xc1)); /* imul eax, ecx */
    }
    byte(text, UINT8_C(0x50)); /* push result */
}

static void x64_mov_r32_imm32(Bytes *text, uint8_t opcode, uint32_t value) {
    byte(text, opcode);
    u32_le(text, value);
}

static void x64_syscall(Bytes *text) {
    byte(text, UINT8_C(0x0f));
    byte(text, UINT8_C(0x05));
}

static size_t x64_diagnostic(
    Bytes *text,
    uint32_t length,
    uint32_t status
) {
    x64_mov_r32_imm32(text, UINT8_C(0xb8), 1); /* write */
    x64_mov_r32_imm32(text, UINT8_C(0xbf), 2); /* stderr */
    byte(text, UINT8_C(0xbe)); /* mov esi, message address */
    size_t address_field = text->length;
    u32_le(text, 0);
    x64_mov_r32_imm32(text, UINT8_C(0xba), length);
    x64_syscall(text);
    x64_mov_r32_imm32(text, UINT8_C(0xb8), 60); /* exit */
    x64_mov_r32_imm32(text, UINT8_C(0xbf), status);
    x64_syscall(text);
    byte(text, UINT8_C(0x0f));
    byte(text, UINT8_C(0x0b)); /* ud2 */
    return address_field;
}

static void x64_patch_u32(Bytes *text, size_t field, uint32_t value) {
    if (field > text->length || text->length - field < 4) {
        fatal("x86-64 native Core u32 field is outside text");
    }
    for (unsigned index = 0; index < 4; ++index) {
        text->data[field + index] =
            (uint8_t)(value >> (index * 8));
    }
}

static void x64_emit(
    Bytes *text,
    const uint8_t *instructions,
    size_t length
) {
    for (size_t index = 0; index < length; ++index) {
        byte(text, instructions[index]);
    }
}

static size_t x64_local_jcc(
    Bytes *text,
    uint8_t condition
) {
    byte(text, UINT8_C(0x0f));
    byte(text, condition);
    size_t field = text->length;
    u32_le(text, 0);
    return field;
}

static size_t x64_local_jmp(Bytes *text) {
    byte(text, UINT8_C(0xe9));
    size_t field = text->length;
    u32_le(text, 0);
    return field;
}

static void x64_runtime(Bytes *text, X64Runtime *runtime) {
    if (!runtime->used) return;

    size_t allocate_at = text->length;
    byte(text, UINT8_C(0x89));
    byte(text, UINT8_C(0xfe)); /* mov esi, edi */
    byte(text, UINT8_C(0x81));
    byte(text, UINT8_C(0xfe)); /* cmp esi, 1 MiB */
    u32_le(text, UINT32_C(1) << 20);
    x64_rel32_placeholder(
        text,
        UINT8_C(0x0f),
        UINT8_C(0x87),
        &runtime->oom_jumps
    ); /* ja oom */
    x64_mov_r32_imm32(
        text,
        UINT8_C(0xbe),
        UINT32_C(1) << 20
    ); /* one fixed-size mmap chunk */
    byte(text, UINT8_C(0x31));
    byte(text, UINT8_C(0xff)); /* xor edi, edi */
    x64_mov_r32_imm32(
        text,
        UINT8_C(0xba),
        UINT32_C(0x3)
    ); /* PROT_READ | PROT_WRITE */
    byte(text, UINT8_C(0x41));
    byte(text, UINT8_C(0xba));
    u32_le(text, UINT32_C(0x22)); /* r10d = MAP_PRIVATE | MAP_ANONYMOUS */
    const uint8_t minus_one[] = {
        UINT8_C(0x49), UINT8_C(0xc7), UINT8_C(0xc0),
        UINT8_C(0xff), UINT8_C(0xff), UINT8_C(0xff), UINT8_C(0xff),
    };
    for (size_t index = 0; index < sizeof(minus_one); ++index) {
        byte(text, minus_one[index]); /* mov r8, -1 */
    }
    byte(text, UINT8_C(0x45));
    byte(text, UINT8_C(0x31));
    byte(text, UINT8_C(0xc9)); /* xor r9d, r9d */
    x64_mov_r32_imm32(text, UINT8_C(0xb8), 9); /* mmap */
    x64_syscall(text);
    byte(text, UINT8_C(0x48));
    byte(text, UINT8_C(0x3d)); /* cmp rax, -4095 */
    u32_le(text, UINT32_C(0xfffff001));
    x64_rel32_placeholder(
        text,
        UINT8_C(0x0f),
        UINT8_C(0x83),
        &runtime->oom_jumps
    ); /* jae oom */
    byte(text, UINT8_C(0xc3)); /* ret */

    size_t text_length_at = text->length;
    const uint8_t text_length_open[] = {
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x17), /* mov rdx, [rdi] */
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xc7), UINT8_C(0x08),
        UINT8_C(0x31), UINT8_C(0xc0), /* xor eax, eax */
    };
    x64_emit(
        text,
        text_length_open,
        sizeof(text_length_open)
    );
    size_t text_length_loop = text->length;
    const uint8_t text_length_test[] = {
        UINT8_C(0x48), UINT8_C(0x85), UINT8_C(0xd2), /* test rdx, rdx */
    };
    x64_emit(text, text_length_test, sizeof(text_length_test));
    size_t text_length_done_jump =
        x64_local_jcc(text, UINT8_C(0x84)); /* je done */
    const uint8_t text_length_byte[] = {
        UINT8_C(0x0f), UINT8_C(0xb6), UINT8_C(0x0f), /* movzx ecx, [rdi] */
        UINT8_C(0x80), UINT8_C(0xe1), UINT8_C(0xc0), /* and cl, 0xc0 */
        UINT8_C(0x80), UINT8_C(0xf9), UINT8_C(0x80), /* cmp cl, 0x80 */
    };
    x64_emit(text, text_length_byte, sizeof(text_length_byte));
    size_t text_length_skip_jump =
        x64_local_jcc(text, UINT8_C(0x84)); /* je skip */
    const uint8_t text_length_increment[] = {
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xc0), /* inc rax */
    };
    x64_emit(
        text,
        text_length_increment,
        sizeof(text_length_increment)
    );
    size_t text_length_skip = text->length;
    const uint8_t text_length_next[] = {
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xc7), /* inc rdi */
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xca), /* dec rdx */
    };
    x64_emit(text, text_length_next, sizeof(text_length_next));
    size_t text_length_back = x64_local_jmp(text);
    size_t text_length_done = text->length;
    byte(text, UINT8_C(0xc3)); /* ret */
    x64_patch_rel32(text, text_length_done_jump, text_length_done);
    x64_patch_rel32(text, text_length_skip_jump, text_length_skip);
    x64_patch_rel32(text, text_length_back, text_length_loop);

    size_t text_equal_at = text->length;
    const uint8_t text_equal_open[] = {
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x17), /* mov rdx, [rdi] */
        UINT8_C(0x48), UINT8_C(0x3b), UINT8_C(0x16), /* cmp rdx, [rsi] */
    };
    x64_emit(text, text_equal_open, sizeof(text_equal_open));
    size_t text_equal_false_length =
        x64_local_jcc(text, UINT8_C(0x85)); /* jne false */
    const uint8_t text_equal_data[] = {
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xc7), UINT8_C(0x08),
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xc6), UINT8_C(0x08),
    };
    x64_emit(text, text_equal_data, sizeof(text_equal_data));
    size_t text_equal_loop = text->length;
    const uint8_t text_equal_test[] = {
        UINT8_C(0x48), UINT8_C(0x85), UINT8_C(0xd2), /* test rdx, rdx */
    };
    x64_emit(text, text_equal_test, sizeof(text_equal_test));
    size_t text_equal_true_jump =
        x64_local_jcc(text, UINT8_C(0x84)); /* je true */
    const uint8_t text_equal_byte[] = {
        UINT8_C(0x8a), UINT8_C(0x07), /* mov al, [rdi] */
        UINT8_C(0x3a), UINT8_C(0x06), /* cmp al, [rsi] */
    };
    x64_emit(text, text_equal_byte, sizeof(text_equal_byte));
    size_t text_equal_false_byte =
        x64_local_jcc(text, UINT8_C(0x85)); /* jne false */
    const uint8_t text_equal_next[] = {
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xc7), /* inc rdi */
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xc6), /* inc rsi */
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xca), /* dec rdx */
    };
    x64_emit(text, text_equal_next, sizeof(text_equal_next));
    size_t text_equal_back = x64_local_jmp(text);
    size_t text_equal_true = text->length;
    x64_mov_eax_imm32(text, 1);
    byte(text, UINT8_C(0xc3)); /* ret */
    size_t text_equal_false = text->length;
    byte(text, UINT8_C(0x31));
    byte(text, UINT8_C(0xc0)); /* xor eax, eax */
    byte(text, UINT8_C(0xc3)); /* ret */
    x64_patch_rel32(text, text_equal_false_length, text_equal_false);
    x64_patch_rel32(text, text_equal_true_jump, text_equal_true);
    x64_patch_rel32(text, text_equal_false_byte, text_equal_false);
    x64_patch_rel32(text, text_equal_back, text_equal_loop);

    size_t text_concat_at = text->length;
    const uint8_t text_concat_open[] = {
        UINT8_C(0x53),                         /* push rbx */
        UINT8_C(0x41), UINT8_C(0x54),         /* push r12 */
        UINT8_C(0x41), UINT8_C(0x55),         /* push r13 */
        UINT8_C(0x41), UINT8_C(0x56),         /* push r14 */
        UINT8_C(0x41), UINT8_C(0x57),         /* push r15 */
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xfb), /* rbx = rdi */
        UINT8_C(0x49), UINT8_C(0x89), UINT8_C(0xf4), /* r12 = rsi */
        UINT8_C(0x4c), UINT8_C(0x8b), UINT8_C(0x2b), /* r13 = [rbx] */
        UINT8_C(0x4d), UINT8_C(0x8b), UINT8_C(0x34), UINT8_C(0x24),
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xef), /* rdi = r13 */
        UINT8_C(0x4c), UINT8_C(0x01), UINT8_C(0xf7), /* rdi += r14 */
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xc7), UINT8_C(0x08),
    };
    x64_emit(text, text_concat_open, sizeof(text_concat_open));
    x64_call_alloc(text, runtime);
    const uint8_t text_concat_header[] = {
        UINT8_C(0x49), UINT8_C(0x89), UINT8_C(0xc7), /* r15 = rax */
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xe9), /* rcx = r13 */
        UINT8_C(0x4c), UINT8_C(0x01), UINT8_C(0xf1), /* rcx += r14 */
        UINT8_C(0x49), UINT8_C(0x89), UINT8_C(0x0f), /* [r15] = rcx */
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xff), /* rdi = r15 */
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xc7), UINT8_C(0x08),
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xde), /* rsi = rbx */
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xc6), UINT8_C(0x08),
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xea), /* rdx = r13 */
    };
    x64_emit(text, text_concat_header, sizeof(text_concat_header));
    size_t concat_left_test = text->length;
    const uint8_t concat_test[] = {
        UINT8_C(0x48), UINT8_C(0x85), UINT8_C(0xd2),
    };
    x64_emit(text, concat_test, sizeof(concat_test));
    size_t concat_left_done =
        x64_local_jcc(text, UINT8_C(0x84)); /* je right */
    const uint8_t concat_copy[] = {
        UINT8_C(0x8a), UINT8_C(0x06), /* mov al, [rsi] */
        UINT8_C(0x88), UINT8_C(0x07), /* mov [rdi], al */
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xc6),
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xc7),
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xca),
    };
    x64_emit(text, concat_copy, sizeof(concat_copy));
    size_t concat_left_back = x64_local_jmp(text);
    size_t concat_right = text->length;
    const uint8_t concat_right_open[] = {
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xe6), /* rsi = r12 */
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xc6), UINT8_C(0x08),
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xf2), /* rdx = r14 */
    };
    x64_emit(text, concat_right_open, sizeof(concat_right_open));
    size_t concat_right_test = text->length;
    x64_emit(text, concat_test, sizeof(concat_test));
    size_t concat_right_done =
        x64_local_jcc(text, UINT8_C(0x84)); /* je done */
    x64_emit(text, concat_copy, sizeof(concat_copy));
    size_t concat_right_back = x64_local_jmp(text);
    size_t concat_done = text->length;
    const uint8_t text_concat_close[] = {
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xf8), /* rax = r15 */
        UINT8_C(0x41), UINT8_C(0x5f),
        UINT8_C(0x41), UINT8_C(0x5e),
        UINT8_C(0x41), UINT8_C(0x5d),
        UINT8_C(0x41), UINT8_C(0x5c),
        UINT8_C(0x5b),
        UINT8_C(0xc3),
    };
    x64_emit(text, text_concat_close, sizeof(text_concat_close));
    x64_patch_rel32(text, concat_left_done, concat_right);
    x64_patch_rel32(text, concat_left_back, concat_left_test);
    x64_patch_rel32(text, concat_right_done, concat_done);
    x64_patch_rel32(text, concat_right_back, concat_right_test);

    size_t text_index_at = text->length;
    const uint8_t text_index_open[] = {
        UINT8_C(0x53),
        UINT8_C(0x41), UINT8_C(0x54),
        UINT8_C(0x41), UINT8_C(0x55),
        UINT8_C(0x41), UINT8_C(0x56),
        UINT8_C(0x41), UINT8_C(0x57),
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xfb), /* rbx = Text */
        UINT8_C(0x49), UINT8_C(0x89), UINT8_C(0xf4), /* r12 = index */
        UINT8_C(0x4d), UINT8_C(0x85), UINT8_C(0xe4), /* test r12, r12 */
    };
    x64_emit(text, text_index_open, sizeof(text_index_open));
    size_t text_index_nonnegative =
        x64_local_jcc(text, UINT8_C(0x89)); /* jns */
    const uint8_t text_index_length_arg[] = {
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xdf), /* rdi = rbx */
        UINT8_C(0xe8),
    };
    x64_emit(
        text,
        text_index_length_arg,
        sizeof(text_index_length_arg)
    );
    size_t text_index_length_call = text->length;
    u32_le(text, 0);
    x64_patch_rel32(text, text_index_length_call, text_length_at);
    const uint8_t text_index_adjust[] = {
        UINT8_C(0x49), UINT8_C(0x01), UINT8_C(0xc4), /* r12 += rax */
    };
    x64_emit(text, text_index_adjust, sizeof(text_index_adjust));
    size_t text_index_nonnegative_at = text->length;
    x64_patch_rel32(
        text,
        text_index_nonnegative,
        text_index_nonnegative_at
    );
    const uint8_t text_index_negative_test[] = {
        UINT8_C(0x4d), UINT8_C(0x85), UINT8_C(0xe4),
    };
    x64_emit(
        text,
        text_index_negative_test,
        sizeof(text_index_negative_test)
    );
    x64_rel32_placeholder(
        text,
        UINT8_C(0x0f),
        UINT8_C(0x88),
        &runtime->text_index_jumps
    ); /* js Text index error */
    const uint8_t text_index_scan_open[] = {
        UINT8_C(0x4c), UINT8_C(0x8b), UINT8_C(0x2b), /* r13 = byte len */
        UINT8_C(0x4c), UINT8_C(0x8d), UINT8_C(0x73), UINT8_C(0x08),
        UINT8_C(0x45), UINT8_C(0x31), UINT8_C(0xff), /* r15 = cp index */
    };
    x64_emit(text, text_index_scan_open, sizeof(text_index_scan_open));
    size_t text_index_scan = text->length;
    const uint8_t text_index_remaining_test[] = {
        UINT8_C(0x4d), UINT8_C(0x85), UINT8_C(0xed),
    };
    x64_emit(
        text,
        text_index_remaining_test,
        sizeof(text_index_remaining_test)
    );
    x64_rel32_placeholder(
        text,
        UINT8_C(0x0f),
        UINT8_C(0x84),
        &runtime->text_index_jumps
    ); /* je Text index error */
    const uint8_t text_index_compare[] = {
        UINT8_C(0x4d), UINT8_C(0x39), UINT8_C(0xe7), /* cmp r15, r12 */
    };
    x64_emit(text, text_index_compare, sizeof(text_index_compare));
    size_t text_index_found =
        x64_local_jcc(text, UINT8_C(0x84)); /* je found */
    const uint8_t text_index_advance[] = {
        UINT8_C(0x49), UINT8_C(0xff), UINT8_C(0xc6), /* inc r14 */
        UINT8_C(0x49), UINT8_C(0xff), UINT8_C(0xcd), /* dec r13 */
    };
    x64_emit(text, text_index_advance, sizeof(text_index_advance));
    size_t text_index_continuation = text->length;
    x64_emit(
        text,
        text_index_remaining_test,
        sizeof(text_index_remaining_test)
    );
    size_t text_index_next_cp =
        x64_local_jcc(text, UINT8_C(0x84)); /* je next cp */
    const uint8_t text_index_cont_byte[] = {
        UINT8_C(0x41), UINT8_C(0x0f), UINT8_C(0xb6), UINT8_C(0x06),
        UINT8_C(0x24), UINT8_C(0xc0), /* and al, 0xc0 */
        UINT8_C(0x3c), UINT8_C(0x80), /* cmp al, 0x80 */
    };
    x64_emit(text, text_index_cont_byte, sizeof(text_index_cont_byte));
    size_t text_index_not_cont =
        x64_local_jcc(text, UINT8_C(0x85)); /* jne next cp */
    x64_emit(text, text_index_advance, sizeof(text_index_advance));
    size_t text_index_cont_back = x64_local_jmp(text);
    size_t text_index_next_cp_at = text->length;
    const uint8_t text_index_cp_increment[] = {
        UINT8_C(0x49), UINT8_C(0xff), UINT8_C(0xc7), /* inc r15 */
    };
    x64_emit(
        text,
        text_index_cp_increment,
        sizeof(text_index_cp_increment)
    );
    size_t text_index_scan_back = x64_local_jmp(text);
    size_t text_index_found_at = text->length;
    const uint8_t text_index_width_open[] = {
        UINT8_C(0x41), UINT8_C(0xbf),
        UINT8_C(0x01), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    };
    x64_emit(text, text_index_width_open, sizeof(text_index_width_open));
    size_t text_index_width = text->length;
    const uint8_t text_index_width_compare[] = {
        UINT8_C(0x4d), UINT8_C(0x39), UINT8_C(0xef), /* cmp r15, r13 */
    };
    x64_emit(
        text,
        text_index_width_compare,
        sizeof(text_index_width_compare)
    );
    size_t text_index_width_done =
        x64_local_jcc(text, UINT8_C(0x8d)); /* jge */
    const uint8_t text_index_width_byte[] = {
        UINT8_C(0x43), UINT8_C(0x0f), UINT8_C(0xb6),
        UINT8_C(0x04), UINT8_C(0x3e), /* byte [r14 + r15] */
        UINT8_C(0x24), UINT8_C(0xc0),
        UINT8_C(0x3c), UINT8_C(0x80),
    };
    x64_emit(
        text,
        text_index_width_byte,
        sizeof(text_index_width_byte)
    );
    size_t text_index_width_not_cont =
        x64_local_jcc(text, UINT8_C(0x85)); /* jne done */
    x64_emit(
        text,
        text_index_cp_increment,
        sizeof(text_index_cp_increment)
    );
    size_t text_index_width_back = x64_local_jmp(text);
    size_t text_index_width_done_at = text->length;
    const uint8_t text_index_allocate_size[] = {
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xff), /* rdi = width */
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xc7), UINT8_C(0x08),
    };
    x64_emit(
        text,
        text_index_allocate_size,
        sizeof(text_index_allocate_size)
    );
    x64_call_alloc(text, runtime);
    const uint8_t text_index_copy_open[] = {
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0x38), /* [rax] = width */
        UINT8_C(0x48), UINT8_C(0x8d), UINT8_C(0x78), UINT8_C(0x08),
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xf6), /* rsi = start */
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xfa), /* rdx = width */
    };
    x64_emit(
        text,
        text_index_copy_open,
        sizeof(text_index_copy_open)
    );
    size_t text_index_copy = text->length;
    x64_emit(text, concat_test, sizeof(concat_test));
    size_t text_index_copy_done =
        x64_local_jcc(text, UINT8_C(0x84));
    const uint8_t text_index_copy_byte[] = {
        UINT8_C(0x8a), UINT8_C(0x0e), /* mov cl, [rsi] */
        UINT8_C(0x88), UINT8_C(0x0f), /* mov [rdi], cl */
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xc6),
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xc7),
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xca),
    };
    x64_emit(
        text,
        text_index_copy_byte,
        sizeof(text_index_copy_byte)
    );
    size_t text_index_copy_back = x64_local_jmp(text);
    size_t text_index_copy_done_at = text->length;
    const uint8_t text_index_close[] = {
        UINT8_C(0x41), UINT8_C(0x5f),
        UINT8_C(0x41), UINT8_C(0x5e),
        UINT8_C(0x41), UINT8_C(0x5d),
        UINT8_C(0x41), UINT8_C(0x5c),
        UINT8_C(0x5b),
        UINT8_C(0xc3),
    };
    x64_emit(text, text_index_close, sizeof(text_index_close));
    x64_patch_rel32(text, text_index_found, text_index_found_at);
    x64_patch_rel32(text, text_index_next_cp, text_index_next_cp_at);
    x64_patch_rel32(text, text_index_not_cont, text_index_next_cp_at);
    x64_patch_rel32(
        text,
        text_index_cont_back,
        text_index_continuation
    );
    x64_patch_rel32(text, text_index_scan_back, text_index_scan);
    x64_patch_rel32(
        text,
        text_index_width_done,
        text_index_width_done_at
    );
    x64_patch_rel32(
        text,
        text_index_width_not_cont,
        text_index_width_done_at
    );
    x64_patch_rel32(text, text_index_width_back, text_index_width);
    x64_patch_rel32(
        text,
        text_index_copy_done,
        text_index_copy_done_at
    );
    x64_patch_rel32(text, text_index_copy_back, text_index_copy);

    size_t text_chars_at = text->length;
    const uint8_t text_chars_open[] = {
        UINT8_C(0x55),                         /* push rbp */
        UINT8_C(0x53),                         /* push rbx */
        UINT8_C(0x41), UINT8_C(0x54),
        UINT8_C(0x41), UINT8_C(0x55),
        UINT8_C(0x41), UINT8_C(0x56),
        UINT8_C(0x41), UINT8_C(0x57),
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xfb), /* rbx = Text */
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xdf), /* rdi = Text */
        UINT8_C(0xe8),
    };
    x64_emit(text, text_chars_open, sizeof(text_chars_open));
    size_t text_chars_length_call = text->length;
    u32_le(text, 0);
    x64_patch_rel32(text, text_chars_length_call, text_length_at);
    const uint8_t text_chars_allocate_list[] = {
        UINT8_C(0x49), UINT8_C(0x89), UINT8_C(0xc4), /* r12 = count */
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xe7), /* rdi = count */
        UINT8_C(0x48), UINT8_C(0xc1), UINT8_C(0xe7), UINT8_C(0x03),
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xc7), UINT8_C(0x08),
    };
    x64_emit(
        text,
        text_chars_allocate_list,
        sizeof(text_chars_allocate_list)
    );
    x64_call_alloc(text, runtime);
    const uint8_t text_chars_list_header[] = {
        UINT8_C(0x49), UINT8_C(0x89), UINT8_C(0xc5), /* r13 = list */
        UINT8_C(0x4d), UINT8_C(0x89), UINT8_C(0x65), UINT8_C(0x00),
        UINT8_C(0x4c), UINT8_C(0x8d), UINT8_C(0x73), UINT8_C(0x08),
        UINT8_C(0x4c), UINT8_C(0x8b), UINT8_C(0x3b), /* r15 = bytes */
        UINT8_C(0x31), UINT8_C(0xed), /* ebp = element index */
    };
    x64_emit(
        text,
        text_chars_list_header,
        sizeof(text_chars_list_header)
    );
    size_t text_chars_loop = text->length;
    const uint8_t text_chars_remaining_test[] = {
        UINT8_C(0x4d), UINT8_C(0x85), UINT8_C(0xff), /* test r15, r15 */
    };
    x64_emit(
        text,
        text_chars_remaining_test,
        sizeof(text_chars_remaining_test)
    );
    size_t text_chars_done =
        x64_local_jcc(text, UINT8_C(0x84)); /* je done */
    x64_mov_r32_imm32(text, UINT8_C(0xbb), 1); /* ebx = width */
    size_t text_chars_width = text->length;
    const uint8_t text_chars_width_compare[] = {
        UINT8_C(0x4c), UINT8_C(0x39), UINT8_C(0xfb), /* cmp rbx, r15 */
    };
    x64_emit(
        text,
        text_chars_width_compare,
        sizeof(text_chars_width_compare)
    );
    size_t text_chars_width_done =
        x64_local_jcc(text, UINT8_C(0x8d)); /* jge */
    const uint8_t text_chars_width_byte[] = {
        UINT8_C(0x41), UINT8_C(0x0f), UINT8_C(0xb6),
        UINT8_C(0x04), UINT8_C(0x1e), /* byte [r14 + rbx] */
        UINT8_C(0x24), UINT8_C(0xc0),
        UINT8_C(0x3c), UINT8_C(0x80),
    };
    x64_emit(
        text,
        text_chars_width_byte,
        sizeof(text_chars_width_byte)
    );
    size_t text_chars_not_cont =
        x64_local_jcc(text, UINT8_C(0x85)); /* jne width done */
    const uint8_t text_chars_width_increment[] = {
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xc3), /* inc rbx */
    };
    x64_emit(
        text,
        text_chars_width_increment,
        sizeof(text_chars_width_increment)
    );
    size_t text_chars_width_back = x64_local_jmp(text);
    size_t text_chars_width_done_at = text->length;
    const uint8_t text_chars_allocate_text[] = {
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xdf), /* rdi = width */
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xc7), UINT8_C(0x08),
    };
    x64_emit(
        text,
        text_chars_allocate_text,
        sizeof(text_chars_allocate_text)
    );
    x64_call_alloc(text, runtime);
    const uint8_t text_chars_store[] = {
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0x18), /* [rax] = width */
        UINT8_C(0x49), UINT8_C(0x89), UINT8_C(0x44),
        UINT8_C(0xed), UINT8_C(0x08), /* list[rbp] = rax */
        UINT8_C(0x48), UINT8_C(0x8d), UINT8_C(0x78), UINT8_C(0x08),
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xf6), /* rsi = cursor */
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xda), /* rdx = width */
    };
    x64_emit(text, text_chars_store, sizeof(text_chars_store));
    size_t text_chars_copy = text->length;
    x64_emit(text, concat_test, sizeof(concat_test));
    size_t text_chars_copy_done =
        x64_local_jcc(text, UINT8_C(0x84));
    x64_emit(
        text,
        text_index_copy_byte,
        sizeof(text_index_copy_byte)
    );
    size_t text_chars_copy_back = x64_local_jmp(text);
    size_t text_chars_copy_done_at = text->length;
    const uint8_t text_chars_next[] = {
        UINT8_C(0x49), UINT8_C(0x01), UINT8_C(0xde), /* r14 += rbx */
        UINT8_C(0x49), UINT8_C(0x29), UINT8_C(0xdf), /* r15 -= rbx */
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xc5), /* inc rbp */
    };
    x64_emit(text, text_chars_next, sizeof(text_chars_next));
    size_t text_chars_back = x64_local_jmp(text);
    size_t text_chars_done_at = text->length;
    const uint8_t text_chars_close[] = {
        UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0xe8), /* rax = list */
        UINT8_C(0x41), UINT8_C(0x5f),
        UINT8_C(0x41), UINT8_C(0x5e),
        UINT8_C(0x41), UINT8_C(0x5d),
        UINT8_C(0x41), UINT8_C(0x5c),
        UINT8_C(0x5b),
        UINT8_C(0x5d),
        UINT8_C(0xc3),
    };
    x64_emit(text, text_chars_close, sizeof(text_chars_close));
    x64_patch_rel32(text, text_chars_done, text_chars_done_at);
    x64_patch_rel32(
        text,
        text_chars_width_done,
        text_chars_width_done_at
    );
    x64_patch_rel32(
        text,
        text_chars_not_cont,
        text_chars_width_done_at
    );
    x64_patch_rel32(text, text_chars_width_back, text_chars_width);
    x64_patch_rel32(
        text,
        text_chars_copy_done,
        text_chars_copy_done_at
    );
    x64_patch_rel32(text, text_chars_copy_back, text_chars_copy);
    x64_patch_rel32(text, text_chars_back, text_chars_loop);

    size_t oom_at = text->length;
    const char oom_message[] = "kofun: out of memory\n";
    size_t oom_address = x64_diagnostic(
        text,
        (uint32_t)(sizeof(oom_message) - 1),
        70
    );

    size_t list_index_at = text->length;
    const char list_index_message[] =
        "kofun: list index out of range\n";
    size_t list_index_address = x64_diagnostic(
        text,
        (uint32_t)(sizeof(list_index_message) - 1),
        1
    );

    size_t text_index_error_at = text->length;
    const char text_index_message[] =
        "kofun: text index out of range\n";
    size_t text_index_address = x64_diagnostic(
        text,
        (uint32_t)(sizeof(text_index_message) - 1),
        1
    );

    size_t oom_message_at = text->length;
    for (size_t index = 0; index < sizeof(oom_message) - 1; ++index) {
        byte(text, (uint8_t)oom_message[index]);
    }
    size_t list_index_message_at = text->length;
    for (
        size_t index = 0;
        index < sizeof(list_index_message) - 1;
        ++index
    ) {
        byte(text, (uint8_t)list_index_message[index]);
    }
    size_t text_index_message_at = text->length;
    for (
        size_t index = 0;
        index < sizeof(text_index_message) - 1;
        ++index
    ) {
        byte(text, (uint8_t)text_index_message[index]);
    }
    size_t newline_at = text->length;
    byte(text, '\n');
    size_t bool_true_at = text->length;
    const char bool_true[] = "true\n";
    x64_emit(
        text,
        (const uint8_t *)bool_true,
        sizeof(bool_true) - 1
    );
    size_t bool_false_at = text->length;
    const char bool_false[] = "false\n";
    x64_emit(
        text,
        (const uint8_t *)bool_false,
        sizeof(bool_false) - 1
    );

    for (size_t index = 0; index < runtime->alloc_calls.length; ++index) {
        x64_patch_rel32(
            text,
            runtime->alloc_calls.fields[index],
            allocate_at
        );
    }
    for (size_t index = 0; index < runtime->oom_jumps.length; ++index) {
        x64_patch_rel32(
            text,
            runtime->oom_jumps.fields[index],
            oom_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->list_index_jumps.length;
        ++index
    ) {
        x64_patch_rel32(
            text,
            runtime->list_index_jumps.fields[index],
            list_index_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_index_jumps.length;
        ++index
    ) {
        x64_patch_rel32(
            text,
            runtime->text_index_jumps.fields[index],
            text_index_error_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_concat_calls.length;
        ++index
    ) {
        x64_patch_rel32(
            text,
            runtime->text_concat_calls.fields[index],
            text_concat_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_equal_calls.length;
        ++index
    ) {
        x64_patch_rel32(
            text,
            runtime->text_equal_calls.fields[index],
            text_equal_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_length_calls.length;
        ++index
    ) {
        x64_patch_rel32(
            text,
            runtime->text_length_calls.fields[index],
            text_length_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_index_calls.length;
        ++index
    ) {
        x64_patch_rel32(
            text,
            runtime->text_index_calls.fields[index],
            text_index_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_chars_calls.length;
        ++index
    ) {
        x64_patch_rel32(
            text,
            runtime->text_chars_calls.fields[index],
            text_chars_at
        );
    }
    x64_patch_u32(
        text,
        oom_address,
        (uint32_t)(IMAGE_BASE + TEXT_OFFSET + oom_message_at)
    );
    x64_patch_u32(
        text,
        list_index_address,
        (uint32_t)(
            IMAGE_BASE + TEXT_OFFSET + list_index_message_at
        )
    );
    x64_patch_u32(
        text,
        text_index_address,
        (uint32_t)(
            IMAGE_BASE + TEXT_OFFSET + text_index_message_at
        )
    );
    for (
        size_t index = 0;
        index < runtime->newline_addresses.length;
        ++index
    ) {
        x64_patch_u32(
            text,
            runtime->newline_addresses.fields[index],
            (uint32_t)(IMAGE_BASE + TEXT_OFFSET + newline_at)
        );
    }
    for (
        size_t index = 0;
        index < runtime->bool_true_addresses.length;
        ++index
    ) {
        x64_patch_u32(
            text,
            runtime->bool_true_addresses.fields[index],
            (uint32_t)(IMAGE_BASE + TEXT_OFFSET + bool_true_at)
        );
    }
    for (
        size_t index = 0;
        index < runtime->bool_false_addresses.length;
        ++index
    ) {
        x64_patch_u32(
            text,
            runtime->bool_false_addresses.fields[index],
            (uint32_t)(IMAGE_BASE + TEXT_OFFSET + bool_false_at)
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_literals.length;
        ++index
    ) {
        while (text->length % sizeof(uint64_t) != 0) byte(text, 0);
        size_t literal_at = text->length;
        const Node *literal = runtime->text_literals.items[index].literal;
        u64_le(text, (uint64_t)literal->text_length);
        x64_emit(text, literal->text_value, literal->text_length);
        x64_patch_u32(
            text,
            runtime->text_literals.items[index].field,
            (uint32_t)(IMAGE_BASE + TEXT_OFFSET + literal_at)
        );
    }
}

static void x64_text(
    Bytes *text,
    const Node *expression,
    LineRows *rows,
    size_t print_line,
    size_t local_count
) {
    X64Runtime runtime = {0};
    if (local_count > 0) {
        if (local_count > UINT32_MAX / sizeof(uint64_t)) {
            fatal("native Core local frame is too large");
        }
        const uint8_t frame_open[] = {
            UINT8_C(0x55),                         /* push rbp */
            UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xe5),
            UINT8_C(0x48), UINT8_C(0x81), UINT8_C(0xec),
        };
        x64_emit(text, frame_open, sizeof(frame_open));
        u32_le(text, (uint32_t)(local_count * sizeof(uint64_t)));
    }
    x64_expression(text, expression, rows, &runtime);
    line_row(rows, text->length, print_line);
    byte(text, UINT8_C(0x58)); /* pop rax */

    if (expression->value_kind == VALUE_INT) {
        byte(text, UINT8_C(0x31));
        byte(text, UINT8_C(0xd2)); /* xor edx, edx */
        x64_mov_r32_imm32(text, UINT8_C(0xb9), 10); /* mov ecx, 10 */
        byte(text, UINT8_C(0xf7));
        byte(text, UINT8_C(0xf1)); /* div ecx */
        byte(text, UINT8_C(0x04));
        byte(text, UINT8_C(48)); /* add al, '0' */
        byte(text, UINT8_C(0x80));
        byte(text, UINT8_C(0xc2));
        byte(text, UINT8_C(48)); /* add dl, '0' */

        const uint8_t tens[] = {
            UINT8_C(0x88), UINT8_C(0x04), UINT8_C(0x25),
            UINT8_C(0x00), UINT8_C(0x10), UINT8_C(0x40), UINT8_C(0x00),
        };
        const uint8_t ones[] = {
            UINT8_C(0x88), UINT8_C(0x14), UINT8_C(0x25),
            UINT8_C(0x01), UINT8_C(0x10), UINT8_C(0x40), UINT8_C(0x00),
        };
        for (size_t index = 0; index < sizeof(tens); ++index) {
            byte(text, tens[index]);
        }
        for (size_t index = 0; index < sizeof(ones); ++index) {
            byte(text, ones[index]);
        }

        x64_mov_r32_imm32(text, UINT8_C(0xb8), 1); /* write */
        x64_mov_r32_imm32(text, UINT8_C(0xbf), 1); /* stdout */
        x64_mov_r32_imm32(
            text, UINT8_C(0xbe), (uint32_t)DATA_ADDRESS
        );
        x64_mov_r32_imm32(text, UINT8_C(0xba), 3);
        x64_syscall(text);
    } else if (expression->value_kind == VALUE_TEXT) {
        runtime.used = true;
        const uint8_t text_output[] = {
            UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x10),
            UINT8_C(0x48), UINT8_C(0x8d), UINT8_C(0x70), UINT8_C(0x08),
        };
        x64_emit(text, text_output, sizeof(text_output));
        x64_mov_r32_imm32(text, UINT8_C(0xb8), 1);
        x64_mov_r32_imm32(text, UINT8_C(0xbf), 1);
        x64_syscall(text);
        x64_mov_r32_imm32(text, UINT8_C(0xb8), 1);
        x64_mov_r32_imm32(text, UINT8_C(0xbf), 1);
        byte(text, UINT8_C(0xbe));
        offsets_add(&runtime.newline_addresses, text->length);
        u32_le(text, 0);
        x64_mov_r32_imm32(text, UINT8_C(0xba), 1);
        x64_syscall(text);
    } else {
        runtime.used = true;
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x85));
        byte(text, UINT8_C(0xc0)); /* test rax, rax */
        size_t false_jump =
            x64_local_jcc(text, UINT8_C(0x84)); /* je false */
        x64_mov_r32_imm32(text, UINT8_C(0xb8), 1);
        x64_mov_r32_imm32(text, UINT8_C(0xbf), 1);
        byte(text, UINT8_C(0xbe));
        offsets_add(&runtime.bool_true_addresses, text->length);
        u32_le(text, 0);
        x64_mov_r32_imm32(text, UINT8_C(0xba), 5);
        x64_syscall(text);
        size_t bool_done = x64_local_jmp(text);
        size_t bool_false = text->length;
        x64_mov_r32_imm32(text, UINT8_C(0xb8), 1);
        x64_mov_r32_imm32(text, UINT8_C(0xbf), 1);
        byte(text, UINT8_C(0xbe));
        offsets_add(&runtime.bool_false_addresses, text->length);
        u32_le(text, 0);
        x64_mov_r32_imm32(text, UINT8_C(0xba), 6);
        x64_syscall(text);
        size_t bool_done_at = text->length;
        x64_patch_rel32(text, false_jump, bool_false);
        x64_patch_rel32(text, bool_done, bool_done_at);
    }
    x64_mov_r32_imm32(text, UINT8_C(0xb8), 60); /* exit */
    x64_mov_r32_imm32(text, UINT8_C(0xbf), 0);
    x64_syscall(text);
    x64_runtime(text, &runtime);
    x64_runtime_free(&runtime);
}

typedef struct {
    size_t field;
    size_t function_index;
} FunctionCallFixup;

typedef struct {
    FunctionCallFixup *items;
    size_t length;
    size_t capacity;
} FunctionCallFixups;

typedef struct {
    FunctionCallFixups calls;
    Offsets print_calls;
    Offsets overflow_jumps;
    Offsets add_overflow_jumps;
    Offsets subtract_overflow_jumps;
    Offsets multiply_overflow_jumps;
    Offsets negate_overflow_jumps;
    Offsets divide_zero_jumps;
    Offsets divide_overflow_jumps;
    Offsets modulo_zero_jumps;
    bool canonical_errors;
} FunctionEmitter;

static void function_call_fixup_add(
    FunctionCallFixups *fixups,
    size_t field,
    size_t function_index
) {
    if (fixups->length == fixups->capacity) {
        size_t capacity =
            fixups->capacity == 0 ? 8 : fixups->capacity * 2;
        FunctionCallFixup *grown = realloc(
            fixups->items,
            capacity * sizeof(*fixups->items)
        );
        if (grown == NULL) fatal("out of memory");
        fixups->items = grown;
        fixups->capacity = capacity;
    }
    fixups->items[fixups->length++] = (FunctionCallFixup){
        .field = field,
        .function_index = function_index,
    };
}

static void function_emitter_free(FunctionEmitter *emitter) {
    free(emitter->calls.items);
    free(emitter->print_calls.fields);
    free(emitter->overflow_jumps.fields);
    free(emitter->add_overflow_jumps.fields);
    free(emitter->subtract_overflow_jumps.fields);
    free(emitter->multiply_overflow_jumps.fields);
    free(emitter->negate_overflow_jumps.fields);
    free(emitter->divide_zero_jumps.fields);
    free(emitter->divide_overflow_jumps.fields);
    free(emitter->modulo_zero_jumps.fields);
}

static void x64_function_call(
    Bytes *text,
    FunctionEmitter *emitter,
    size_t function_index
) {
    byte(text, UINT8_C(0xe8));
    function_call_fixup_add(
        &emitter->calls,
        text->length,
        function_index
    );
    u32_le(text, 0);
}

static void x64_function_overflow_jump(
    Bytes *text,
    FunctionEmitter *emitter,
    FunctionExpressionKind kind
) {
    Offsets *jumps = &emitter->overflow_jumps;
    if (emitter->canonical_errors) {
        if (kind == FUNCTION_ADD) {
            jumps = &emitter->add_overflow_jumps;
        } else if (kind == FUNCTION_SUBTRACT) {
            jumps = &emitter->subtract_overflow_jumps;
        } else if (kind == FUNCTION_MULTIPLY) {
            jumps = &emitter->multiply_overflow_jumps;
        } else if (kind == FUNCTION_NEGATE) {
            jumps = &emitter->negate_overflow_jumps;
        }
    }
    x64_rel32_placeholder(
        text,
        UINT8_C(0x0f),
        UINT8_C(0x80),
        jumps
    );
}

static void x64_function_expression(
    Bytes *text,
    const FunctionExpression *expression,
    FunctionEmitter *emitter
) {
    if (expression->kind == FUNCTION_LITERAL) {
        if ((uint64_t)expression->value <= UINT32_MAX) {
            byte(text, UINT8_C(0xb8)); /* mov eax, immediate */
            u32_le(text, (uint32_t)expression->value);
        } else {
            byte(text, UINT8_C(0x48));
            byte(text, UINT8_C(0xb8)); /* mov rax, immediate */
            u64_le(text, (uint64_t)expression->value);
        }
        byte(text, UINT8_C(0x50)); /* push rax */
        return;
    }
    if (expression->kind == FUNCTION_PARAMETER) {
        x64_load_local(text, expression->slot);
        byte(text, UINT8_C(0x50)); /* push parameter */
        return;
    }
    if (expression->kind == FUNCTION_CALL) {
        for (size_t index = 0; index < expression->argument_count; ++index) {
            x64_function_expression(
                text,
                expression->arguments[index],
                emitter
            );
        }
        for (size_t index = expression->argument_count; index > 0; --index) {
            switch (index - 1) {
                case 0:
                    byte(text, UINT8_C(0x5f)); /* pop rdi */
                    break;
                case 1:
                    byte(text, UINT8_C(0x5e)); /* pop rsi */
                    break;
                case 2:
                    byte(text, UINT8_C(0x5a)); /* pop rdx */
                    break;
                case 3:
                    byte(text, UINT8_C(0x59)); /* pop rcx */
                    break;
                case 4:
                    byte(text, UINT8_C(0x41));
                    byte(text, UINT8_C(0x58)); /* pop r8 */
                    break;
                case 5:
                    byte(text, UINT8_C(0x41));
                    byte(text, UINT8_C(0x59)); /* pop r9 */
                    break;
                default:
                    fatal("native Core call has too many arguments");
            }
        }
        x64_function_call(text, emitter, expression->function_index);
        byte(text, UINT8_C(0x50)); /* push return value */
        return;
    }
    if (expression->kind == FUNCTION_NEGATE) {
        x64_function_expression(text, expression->left, emitter);
        byte(text, UINT8_C(0x58)); /* pop rax */
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0xf7));
        byte(text, UINT8_C(0xd8)); /* neg rax */
        x64_function_overflow_jump(
            text,
            emitter,
            FUNCTION_NEGATE
        );
        byte(text, UINT8_C(0x50));
        return;
    }

    x64_function_expression(text, expression->left, emitter);
    x64_function_expression(text, expression->right, emitter);
    byte(text, UINT8_C(0x59)); /* pop rcx: right */
    byte(text, UINT8_C(0x58)); /* pop rax: left */
    if (expression->kind == FUNCTION_ADD) {
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x01));
        byte(text, UINT8_C(0xc8)); /* add rax, rcx */
        x64_function_overflow_jump(text, emitter, FUNCTION_ADD);
    } else if (expression->kind == FUNCTION_SUBTRACT) {
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x29));
        byte(text, UINT8_C(0xc8)); /* sub rax, rcx */
        x64_function_overflow_jump(text, emitter, FUNCTION_SUBTRACT);
    } else if (expression->kind == FUNCTION_MULTIPLY) {
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x0f));
        byte(text, UINT8_C(0xaf));
        byte(text, UINT8_C(0xc1)); /* imul rax, rcx */
        x64_function_overflow_jump(text, emitter, FUNCTION_MULTIPLY);
    } else if (expression->kind == FUNCTION_FLOOR_DIVIDE ||
               expression->kind == FUNCTION_FLOOR_MODULO) {
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x85));
        byte(text, UINT8_C(0xc9)); /* test rcx, rcx */
        x64_rel32_placeholder(
            text,
            UINT8_C(0x0f),
            UINT8_C(0x84),
            expression->kind == FUNCTION_FLOOR_DIVIDE
                ? &emitter->divide_zero_jumps
                : &emitter->modulo_zero_jumps
        );

        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x83));
        byte(text, UINT8_C(0xf9));
        byte(text, UINT8_C(0xff)); /* cmp rcx, -1 */
        size_t not_minus_one =
            x64_local_jcc(text, UINT8_C(0x85)); /* jne divide */
        byte(text, UINT8_C(0x49));
        byte(text, UINT8_C(0xb8)); /* mov r8, INT64_MIN */
        u64_le(text, UINT64_C(0x8000000000000000));
        byte(text, UINT8_C(0x4c));
        byte(text, UINT8_C(0x39));
        byte(text, UINT8_C(0xc0)); /* cmp rax, r8 */
        if (expression->kind == FUNCTION_FLOOR_DIVIDE) {
            x64_rel32_placeholder(
                text,
                UINT8_C(0x0f),
                UINT8_C(0x84),
                &emitter->divide_overflow_jumps
            );
        } else {
            size_t not_overflow =
                x64_local_jcc(text, UINT8_C(0x85)); /* jne divide */
            byte(text, UINT8_C(0x31));
            byte(text, UINT8_C(0xc0)); /* xor eax, eax */
            size_t modulo_done = x64_local_jmp(text);
            size_t divide_at = text->length;
            x64_patch_rel32(text, not_minus_one, divide_at);
            x64_patch_rel32(text, not_overflow, divide_at);
            byte(text, UINT8_C(0x48));
            byte(text, UINT8_C(0x99)); /* cqo */
            byte(text, UINT8_C(0x48));
            byte(text, UINT8_C(0xf7));
            byte(text, UINT8_C(0xf9)); /* idiv rcx */
            byte(text, UINT8_C(0x48));
            byte(text, UINT8_C(0x85));
            byte(text, UINT8_C(0xd2)); /* test rdx, rdx */
            size_t remainder_done =
                x64_local_jcc(text, UINT8_C(0x84)); /* je result */
            byte(text, UINT8_C(0x49));
            byte(text, UINT8_C(0x89));
            byte(text, UINT8_C(0xd0)); /* mov r8, rdx */
            byte(text, UINT8_C(0x49));
            byte(text, UINT8_C(0x31));
            byte(text, UINT8_C(0xc8)); /* xor r8, rcx */
            byte(text, UINT8_C(0x4d));
            byte(text, UINT8_C(0x85));
            byte(text, UINT8_C(0xc0)); /* test r8, r8 */
            size_t same_sign =
                x64_local_jcc(text, UINT8_C(0x89)); /* jns result */
            byte(text, UINT8_C(0x48));
            byte(text, UINT8_C(0x01));
            byte(text, UINT8_C(0xca)); /* add rdx, rcx */
            size_t result_at = text->length;
            byte(text, UINT8_C(0x48));
            byte(text, UINT8_C(0x89));
            byte(text, UINT8_C(0xd0)); /* mov rax, rdx */
            size_t done_at = text->length;
            x64_patch_rel32(text, modulo_done, done_at);
            x64_patch_rel32(text, remainder_done, result_at);
            x64_patch_rel32(text, same_sign, result_at);
            byte(text, UINT8_C(0x50));
            return;
        }
        size_t divide_at = text->length;
        x64_patch_rel32(text, not_minus_one, divide_at);
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x99)); /* cqo */
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0xf7));
        byte(text, UINT8_C(0xf9)); /* idiv rcx */
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x85));
        byte(text, UINT8_C(0xd2)); /* test remainder */
        size_t division_done =
            x64_local_jcc(text, UINT8_C(0x84)); /* je done */
        byte(text, UINT8_C(0x49));
        byte(text, UINT8_C(0x89));
        byte(text, UINT8_C(0xd0)); /* mov r8, rdx */
        byte(text, UINT8_C(0x49));
        byte(text, UINT8_C(0x31));
        byte(text, UINT8_C(0xc8)); /* xor r8, rcx */
        byte(text, UINT8_C(0x4d));
        byte(text, UINT8_C(0x85));
        byte(text, UINT8_C(0xc0)); /* test r8, r8 */
        size_t division_same_sign =
            x64_local_jcc(text, UINT8_C(0x89)); /* jns done */
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0xff));
        byte(text, UINT8_C(0xc8)); /* dec rax */
        size_t division_done_at = text->length;
        x64_patch_rel32(text, division_done, division_done_at);
        x64_patch_rel32(
            text,
            division_same_sign,
            division_done_at
        );
    } else {
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x39));
        byte(text, UINT8_C(0xc8)); /* cmp rax, rcx */
        byte(text, UINT8_C(0x0f));
        uint8_t condition = UINT8_C(0x94);
        if (expression->kind == FUNCTION_NOT_EQUAL) {
            condition = UINT8_C(0x95);
        } else if (expression->kind == FUNCTION_LESS) {
            condition = UINT8_C(0x9c);
        } else if (expression->kind == FUNCTION_LESS_EQUAL) {
            condition = UINT8_C(0x9e);
        } else if (expression->kind == FUNCTION_GREATER) {
            condition = UINT8_C(0x9f);
        } else if (expression->kind == FUNCTION_GREATER_EQUAL) {
            condition = UINT8_C(0x9d);
        }
        byte(text, condition);
        byte(text, UINT8_C(0xc0)); /* setcc al */
        byte(text, UINT8_C(0x0f));
        byte(text, UINT8_C(0xb6));
        byte(text, UINT8_C(0xc0)); /* movzx eax, al */
    }
    byte(text, UINT8_C(0x50));
}

static void x64_function_epilogue(Bytes *text) {
    byte(text, UINT8_C(0xc9)); /* leave */
    byte(text, UINT8_C(0xc3)); /* ret */
}

static void x64_function_parameter_store(
    Bytes *text,
    size_t parameter
) {
    static const uint8_t prefixes[MAX_CORE_PARAMETERS][3] = {
        {UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xbd)},
        {UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xb5)},
        {UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0x95)},
        {UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0x8d)},
        {UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0x85)},
        {UINT8_C(0x4c), UINT8_C(0x89), UINT8_C(0x8d)},
    };
    if (parameter >= MAX_CORE_PARAMETERS) {
        fatal("native Core parameter register is unavailable");
    }
    x64_emit(text, prefixes[parameter], sizeof(prefixes[parameter]));
    u32_le(text, x64_local_displacement(parameter));
}

static void x64_function_declaration(
    Bytes *text,
    const FunctionDeclaration *function,
    FunctionEmitter *emitter
) {
    const uint8_t frame_open[] = {
        UINT8_C(0x55),                         /* push rbp */
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xe5),
    };
    x64_emit(text, frame_open, sizeof(frame_open));
    size_t local_slots =
        function->parameter_count + function->local_count;
    if (local_slots > 0) {
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x81));
        byte(text, UINT8_C(0xec)); /* sub rsp, frame bytes */
        u32_le(
            text,
            (uint32_t)(
                local_slots * sizeof(uint64_t)
            )
        );
        for (size_t index = 0;
             index < function->parameter_count;
             ++index) {
            x64_function_parameter_store(text, index);
        }
    }

    bool returned = false;
    for (size_t index = 0; index < function->statement_count; ++index) {
        const FunctionStatement *statement = &function->statements[index];
        if (statement->kind == FUNCTION_STATEMENT_IF_RETURN) {
            x64_function_expression(
                text,
                statement->condition,
                emitter
            );
            byte(text, UINT8_C(0x58)); /* pop condition */
            byte(text, UINT8_C(0x48));
            byte(text, UINT8_C(0x85));
            byte(text, UINT8_C(0xc0)); /* test rax, rax */
            size_t skip = x64_local_jcc(
                text,
                UINT8_C(0x84)
            ); /* je after branch */
            x64_function_expression(text, statement->value, emitter);
            byte(text, UINT8_C(0x58));
            x64_function_epilogue(text);
            x64_patch_rel32(text, skip, text->length);
        } else if (statement->kind == FUNCTION_STATEMENT_RETURN) {
            x64_function_expression(text, statement->value, emitter);
            byte(text, UINT8_C(0x58));
            x64_function_epilogue(text);
            returned = true;
        } else if (statement->kind == FUNCTION_STATEMENT_PRINT) {
            x64_function_expression(text, statement->value, emitter);
            byte(text, UINT8_C(0x5f)); /* pop print argument into rdi */
            byte(text, UINT8_C(0xe8));
            offsets_add(&emitter->print_calls, text->length);
            u32_le(text, 0);
        } else if (statement->kind == FUNCTION_STATEMENT_LET) {
            x64_function_expression(text, statement->value, emitter);
            byte(text, UINT8_C(0x58)); /* pop initializer */
            x64_store_local(text, statement->slot);
        } else {
            x64_function_expression(text, statement->value, emitter);
            byte(text, UINT8_C(0x58)); /* discard expression result */
        }
    }
    if (!returned) {
        byte(text, UINT8_C(0x31));
        byte(text, UINT8_C(0xc0)); /* implicit main return 0 */
        x64_function_epilogue(text);
    }
}

static size_t x64_function_print_runtime(Bytes *text) {
    size_t runtime_at = text->length;
    const uint8_t open[] = {
        UINT8_C(0x55),                         /* push rbp */
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xe5),
        UINT8_C(0x48), UINT8_C(0x83), UINT8_C(0xec), UINT8_C(0x20),
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xf8), /* rax = value */
        UINT8_C(0x45), UINT8_C(0x31), UINT8_C(0xd2), /* r10d = 0 */
        UINT8_C(0xc6), UINT8_C(0x45), UINT8_C(0xff), UINT8_C(0x0a),
        UINT8_C(0x48), UINT8_C(0x8d), UINT8_C(0x75), UINT8_C(0xff),
        UINT8_C(0x48), UINT8_C(0x85), UINT8_C(0xc0), /* test rax */
    };
    x64_emit(text, open, sizeof(open));
    size_t nonnegative =
        x64_local_jcc(text, UINT8_C(0x89)); /* jns magnitude */
    const uint8_t negative[] = {
        UINT8_C(0x48), UINT8_C(0xf7), UINT8_C(0xd8), /* neg rax */
        UINT8_C(0x41), UINT8_C(0xba),
        UINT8_C(0x01), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    };
    x64_emit(text, negative, sizeof(negative));
    size_t magnitude = text->length;
    x64_patch_rel32(text, nonnegative, magnitude);
    const uint8_t zero_test[] = {
        UINT8_C(0x48), UINT8_C(0x85), UINT8_C(0xc0),
    };
    x64_emit(text, zero_test, sizeof(zero_test));
    size_t digits = x64_local_jcc(
        text,
        UINT8_C(0x85)
    ); /* jne digit loop */
    const uint8_t zero[] = {
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xce), /* dec rsi */
        UINT8_C(0xc6), UINT8_C(0x06), UINT8_C(0x30),
    };
    x64_emit(text, zero, sizeof(zero));
    size_t sign = x64_local_jmp(text);
    size_t digits_at = text->length;
    x64_patch_rel32(text, digits, digits_at);
    x64_mov_r32_imm32(text, UINT8_C(0xb9), 10); /* ecx = 10 */
    size_t digit_loop = text->length;
    const uint8_t digit[] = {
        UINT8_C(0x31), UINT8_C(0xd2),             /* xor edx, edx */
        UINT8_C(0x48), UINT8_C(0xf7), UINT8_C(0xf1), /* div rcx */
        UINT8_C(0x80), UINT8_C(0xc2), UINT8_C(0x30),
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xce),
        UINT8_C(0x88), UINT8_C(0x16),             /* [rsi] = dl */
        UINT8_C(0x48), UINT8_C(0x85), UINT8_C(0xc0),
    };
    x64_emit(text, digit, sizeof(digit));
    size_t digit_back =
        x64_local_jcc(text, UINT8_C(0x85)); /* jne loop */
    x64_patch_rel32(text, digit_back, digit_loop);
    size_t sign_at = text->length;
    x64_patch_rel32(text, sign, sign_at);
    const uint8_t sign_test[] = {
        UINT8_C(0x45), UINT8_C(0x85), UINT8_C(0xd2),
    };
    x64_emit(text, sign_test, sizeof(sign_test));
    size_t write = x64_local_jcc(text, UINT8_C(0x84)); /* je write */
    const uint8_t minus[] = {
        UINT8_C(0x48), UINT8_C(0xff), UINT8_C(0xce),
        UINT8_C(0xc6), UINT8_C(0x06), UINT8_C(0x2d),
    };
    x64_emit(text, minus, sizeof(minus));
    size_t write_at = text->length;
    x64_patch_rel32(text, write, write_at);
    const uint8_t output[] = {
        UINT8_C(0x48), UINT8_C(0x89), UINT8_C(0xea), /* rdx = rbp */
        UINT8_C(0x48), UINT8_C(0x29), UINT8_C(0xf2), /* rdx -= rsi */
    };
    x64_emit(text, output, sizeof(output));
    x64_mov_r32_imm32(text, UINT8_C(0xb8), 1);
    x64_mov_r32_imm32(text, UINT8_C(0xbf), 1);
    x64_syscall(text);
    x64_function_epilogue(text);
    return runtime_at;
}

static size_t x64_function_diagnostic(
    Bytes *text,
    const char *message
) {
    size_t diagnostic_at = text->length;
    size_t length = strlen(message);
    if (length > UINT32_MAX) fatal("native diagnostic is too large");
    size_t address_field =
        x64_diagnostic(text, (uint32_t)length, 1);
    size_t message_at = text->length;
    x64_emit(text, (const uint8_t *)message, length);
    x64_patch_u32(
        text,
        address_field,
        (uint32_t)(IMAGE_BASE + TEXT_OFFSET + message_at)
    );
    return diagnostic_at;
}

static void x64_patch_function_jumps(
    Bytes *text,
    const Offsets *jumps,
    size_t target
) {
    for (size_t index = 0; index < jumps->length; ++index) {
        x64_patch_rel32(text, jumps->fields[index], target);
    }
}

static void x64_function_program(
    Bytes *text,
    const FunctionProgram *program
) {
    FunctionEmitter emitter = {
        .canonical_errors = program->canonical_runtime_errors,
    };
    size_t function_addresses[MAX_CORE_FUNCTIONS] = {0};

    x64_function_call(text, &emitter, program->main_index);
    byte(text, UINT8_C(0x89));
    byte(text, UINT8_C(0xc7)); /* mov edi, eax */
    x64_mov_r32_imm32(text, UINT8_C(0xb8), 60);
    x64_syscall(text);
    byte(text, UINT8_C(0x0f));
    byte(text, UINT8_C(0x0b)); /* ud2 after exit */

    for (size_t index = 0; index < program->function_count; ++index) {
        function_addresses[index] = text->length;
        x64_function_declaration(
            text,
            &program->functions[index],
            &emitter
        );
    }

    size_t print_at = x64_function_print_runtime(text);
    size_t overflow_at = x64_function_diagnostic(
        text,
        "kofun: integer overflow\n"
    );
    size_t add_overflow_at = overflow_at;
    size_t subtract_overflow_at = overflow_at;
    size_t multiply_overflow_at = overflow_at;
    size_t negate_overflow_at = overflow_at;
    size_t divide_zero_at = overflow_at;
    size_t divide_overflow_at = overflow_at;
    size_t modulo_zero_at = overflow_at;
    if (emitter.canonical_errors) {
        add_overflow_at = x64_function_diagnostic(
            text,
            "error[R010]: integer overflow in operator `+`\n"
        );
        subtract_overflow_at = x64_function_diagnostic(
            text,
            "error[R010]: integer overflow in operator `-`\n"
        );
        multiply_overflow_at = x64_function_diagnostic(
            text,
            "error[R010]: integer overflow in operator `*`\n"
        );
        negate_overflow_at = x64_function_diagnostic(
            text,
            "error[R010]: integer overflow in unary operator `-`\n"
        );
        divide_zero_at = x64_function_diagnostic(
            text,
            "error[R010]: operator `//` failed: division by zero\n"
        );
        divide_overflow_at = x64_function_diagnostic(
            text,
            "error[R010]: integer overflow in operator `//`\n"
        );
        modulo_zero_at = x64_function_diagnostic(
            text,
            "error[R010]: operator `%` failed: division by zero\n"
        );
    }

    for (size_t index = 0; index < emitter.calls.length; ++index) {
        FunctionCallFixup fixup = emitter.calls.items[index];
        x64_patch_rel32(
            text,
            fixup.field,
            function_addresses[fixup.function_index]
        );
    }
    for (size_t index = 0; index < emitter.print_calls.length; ++index) {
        x64_patch_rel32(
            text,
            emitter.print_calls.fields[index],
            print_at
        );
    }
    x64_patch_function_jumps(
        text, &emitter.overflow_jumps, overflow_at);
    x64_patch_function_jumps(
        text, &emitter.add_overflow_jumps, add_overflow_at);
    x64_patch_function_jumps(
        text,
        &emitter.subtract_overflow_jumps,
        subtract_overflow_at
    );
    x64_patch_function_jumps(
        text,
        &emitter.multiply_overflow_jumps,
        multiply_overflow_at
    );
    x64_patch_function_jumps(
        text, &emitter.negate_overflow_jumps, negate_overflow_at);
    x64_patch_function_jumps(
        text, &emitter.divide_zero_jumps, divide_zero_at);
    x64_patch_function_jumps(
        text, &emitter.divide_overflow_jumps, divide_overflow_at);
    x64_patch_function_jumps(
        text, &emitter.modulo_zero_jumps, modulo_zero_at);
    function_emitter_free(&emitter);
}

static void a64_word(Bytes *text, uint32_t instruction) {
    u32_le(text, instruction);
}

static void a64_movz(Bytes *text, unsigned reg, unsigned value) {
    a64_word(
        text,
        UINT32_C(0xd2800000) |
            ((uint32_t)value << 5) |
            (uint32_t)reg
    );
}

static void a64_movk_lsl16(Bytes *text, unsigned reg, unsigned value) {
    a64_word(
        text,
        UINT32_C(0xf2a00000) |
            ((uint32_t)value << 5) |
            (uint32_t)reg
    );
}

static void a64_add(
    Bytes *text,
    unsigned destination,
    unsigned left,
    unsigned right
) {
    a64_word(
        text,
        UINT32_C(0x8b000000) |
            ((uint32_t)right << 16) |
            ((uint32_t)left << 5) |
            (uint32_t)destination
    );
}

static void a64_multiply(
    Bytes *text,
    unsigned destination,
    unsigned left,
    unsigned right
) {
    a64_word(
        text,
        UINT32_C(0x9b007c00) |
            ((uint32_t)right << 16) |
            ((uint32_t)left << 5) |
            (uint32_t)destination
    );
}

static void a64_expression(
    Bytes *text,
    const Node *expression,
    unsigned *depth
) {
    if (expression->kind == NODE_LITERAL) {
        a64_movz(text, *depth, (unsigned)expression->value);
        ++*depth;
        return;
    }

    a64_expression(text, expression->left, depth);
    a64_expression(text, expression->right, depth);
    unsigned left = *depth - 2;
    unsigned right = *depth - 1;
    if (expression->kind == NODE_ADD) {
        a64_add(text, left, left, right);
    } else {
        a64_multiply(text, left, left, right);
    }
    --*depth;
}

static void a64_add_immediate(
    Bytes *text,
    unsigned destination,
    unsigned source,
    unsigned value
) {
    a64_word(
        text,
        UINT32_C(0x91000000) |
            ((uint32_t)value << 10) |
            ((uint32_t)source << 5) |
            (uint32_t)destination
    );
}

static void a64_udiv(
    Bytes *text,
    unsigned destination,
    unsigned left,
    unsigned right
) {
    a64_word(
        text,
        UINT32_C(0x9ac00800) |
            ((uint32_t)right << 16) |
            ((uint32_t)left << 5) |
            (uint32_t)destination
    );
}

static void a64_msub(
    Bytes *text,
    unsigned destination,
    unsigned left,
    unsigned right,
    unsigned accumulator
) {
    a64_word(
        text,
        UINT32_C(0x9b008000) |
            ((uint32_t)right << 16) |
            ((uint32_t)accumulator << 10) |
            ((uint32_t)left << 5) |
            (uint32_t)destination
    );
}

static void a64_strb(
    Bytes *text,
    unsigned source,
    unsigned address,
    unsigned offset
) {
    a64_word(
        text,
        UINT32_C(0x39000000) |
            ((uint32_t)offset << 10) |
            ((uint32_t)address << 5) |
            (uint32_t)source
    );
}

static void a64_svc(Bytes *text) {
    a64_word(text, UINT32_C(0xd4000001));
}

static void a64_text(Bytes *text, const Node *expression) {
    unsigned depth = 0;
    a64_expression(text, expression, &depth);
    if (depth != 1) fatal("invalid AArch64 Core register stack");

    a64_movz(text, 3, 10);
    a64_udiv(text, 4, 0, 3);
    a64_msub(text, 5, 4, 3, 0);
    a64_add_immediate(text, 4, 4, 48);
    a64_add_immediate(text, 5, 5, 48);
    a64_movz(text, 1, UINT16_C(0x1000));
    a64_movk_lsl16(text, 1, UINT16_C(0x40));
    a64_strb(text, 4, 1, 0);
    a64_strb(text, 5, 1, 1);

    a64_movz(text, 0, 1);  /* stdout */
    a64_movz(text, 2, 3);  /* length */
    a64_movz(text, 8, 64); /* write */
    a64_svc(text);
    a64_movz(text, 0, 0);
    a64_movz(text, 8, 93); /* exit */
    a64_svc(text);
}

/*
 * AArch64 user-defined Int function Core.
 *
 * This mirrors the x86-64 function profile (x64_function_program) instruction
 * for instruction, using the same target-independent parsed FunctionProgram.
 * It is a straightforward stack machine: every intermediate value lives on the
 * native stack, so no register allocator is required. The stack pointer is
 * kept 16-byte aligned at all times (each push/pop moves it by 16 bytes and
 * every frame is a 16-byte multiple), which Linux requires for `sp`.
 *
 * Every fixed-register instruction word below was cross-checked against
 * `llvm-mc --triple=aarch64 --show-encoding`.
 */

static uint32_t a64_read_word(const Bytes *text, size_t field) {
    if (field > text->length || text->length - field < 4) {
        fatal("aarch64 instruction field is outside text");
    }
    return (uint32_t)text->data[field] |
        ((uint32_t)text->data[field + 1] << 8) |
        ((uint32_t)text->data[field + 2] << 16) |
        ((uint32_t)text->data[field + 3] << 24);
}

static void a64_write_word(Bytes *text, size_t field, uint32_t word) {
    if (field > text->length || text->length - field < 4) {
        fatal("aarch64 instruction field is outside text");
    }
    for (unsigned index = 0; index < 4; ++index) {
        text->data[field + index] = (uint8_t)(word >> (index * 8));
    }
}

/* Patch a 26-bit branch immediate (B/BL), scaled by 4, PC-relative. */
static void a64_patch_imm26(Bytes *text, size_t field, size_t target) {
    int64_t displacement = (int64_t)target - (int64_t)field;
    if (displacement % 4 != 0) {
        fatal("aarch64 branch target is not 4-byte aligned");
    }
    int64_t immediate = displacement / 4;
    if (immediate < -(INT64_C(1) << 25) ||
        immediate >= (INT64_C(1) << 25)) {
        fatal("aarch64 imm26 branch is out of range");
    }
    uint32_t word = a64_read_word(text, field);
    word = (word & ~UINT32_C(0x03ffffff)) |
        ((uint32_t)immediate & UINT32_C(0x03ffffff));
    a64_write_word(text, field, word);
}

/* Patch a 19-bit branch immediate (B.cond/CBZ/CBNZ) at bits [23:5]. */
static void a64_patch_imm19(Bytes *text, size_t field, size_t target) {
    int64_t displacement = (int64_t)target - (int64_t)field;
    if (displacement % 4 != 0) {
        fatal("aarch64 conditional target is not 4-byte aligned");
    }
    int64_t immediate = displacement / 4;
    if (immediate < -(INT64_C(1) << 18) ||
        immediate >= (INT64_C(1) << 18)) {
        fatal("aarch64 imm19 branch is out of range");
    }
    uint32_t word = a64_read_word(text, field);
    word = (word & ~(UINT32_C(0x7ffff) << 5)) |
        (((uint32_t)immediate & UINT32_C(0x7ffff)) << 5);
    a64_write_word(text, field, word);
}

/* Patch a 16-bit MOVZ/MOVK immediate at bits [20:5]. */
static void a64_patch_mov_imm16(Bytes *text, size_t field, uint32_t value) {
    uint32_t word = a64_read_word(text, field);
    word = (word & ~(UINT32_C(0xffff) << 5)) |
        ((value & UINT32_C(0xffff)) << 5);
    a64_write_word(text, field, word);
}

/* push xN : str xN, [sp, #-16]! */
static void a64_push(Bytes *text, unsigned reg) {
    a64_word(text, UINT32_C(0xf81f0fe0) | (reg & UINT32_C(0x1f)));
}

/* pop xN : ldr xN, [sp], #16 */
static void a64_pop(Bytes *text, unsigned reg) {
    a64_word(text, UINT32_C(0xf84107e0) | (reg & UINT32_C(0x1f)));
}

/* sub sp, sp, #frame (frame is a 16-byte multiple) */
static void a64_sub_sp(Bytes *text, uint32_t frame) {
    if (frame > 0xfff) fatal("aarch64 Core frame is too large");
    a64_word(text, UINT32_C(0xd10003ff) | ((frame & UINT32_C(0xfff)) << 10));
}

static uint32_t a64_local_imm9(size_t slot) {
    if (slot >= (size_t)0x1f) {
        fatal("aarch64 Core local frame is too large");
    }
    int32_t offset = -(int32_t)((slot + 1) * sizeof(uint64_t));
    return (uint32_t)offset & UINT32_C(0x1ff);
}

/* stur xreg, [x29, #-(slot+1)*8] */
static void a64_store_param(Bytes *text, unsigned reg, size_t slot) {
    a64_word(
        text,
        UINT32_C(0xf8000000) |
            (a64_local_imm9(slot) << 12) |
            (UINT32_C(29) << 5) |
            (reg & UINT32_C(0x1f))
    );
}

/* ldur xreg, [x29, #-(slot+1)*8] */
static void a64_load_param(Bytes *text, unsigned reg, size_t slot) {
    a64_word(
        text,
        UINT32_C(0xf8400000) |
            (a64_local_imm9(slot) << 12) |
            (UINT32_C(29) << 5) |
            (reg & UINT32_C(0x1f))
    );
}

/* Load a 32-bit zero-extended immediate, matching the x86-64 mov eax path. */
static void a64_load_immediate(Bytes *text, unsigned reg, int64_t value) {
    uint32_t bits = (uint32_t)value;
    a64_movz(text, reg, bits & UINT32_C(0xffff));
    if ((bits >> 16) != 0) {
        a64_movk_lsl16(text, reg, (bits >> 16) & UINT32_C(0xffff));
    }
}

static void a64_function_epilogue(Bytes *text) {
    a64_word(text, UINT32_C(0x910003bf)); /* mov sp, x29 */
    a64_word(text, UINT32_C(0xa8c17bfd)); /* ldp x29, x30, [sp], #16 */
    a64_word(text, UINT32_C(0xd65f03c0)); /* ret */
}

static void a64_overflow_jump(
    Bytes *text,
    FunctionEmitter *emitter,
    uint32_t conditional
) {
    offsets_add(&emitter->overflow_jumps, text->length);
    a64_word(text, conditional);
}

static void a64_function_call(
    Bytes *text,
    FunctionEmitter *emitter,
    size_t function_index
) {
    function_call_fixup_add(&emitter->calls, text->length, function_index);
    a64_word(text, UINT32_C(0x94000000)); /* bl (patched) */
}

static void a64_function_expression(
    Bytes *text,
    const FunctionExpression *expression,
    FunctionEmitter *emitter
) {
    if (expression->kind == FUNCTION_LITERAL) {
        a64_load_immediate(text, 0, expression->value);
        a64_push(text, 0);
        return;
    }
    if (expression->kind == FUNCTION_PARAMETER) {
        a64_load_param(text, 0, expression->slot);
        a64_push(text, 0);
        return;
    }
    if (expression->kind == FUNCTION_CALL) {
        for (size_t index = 0; index < expression->argument_count; ++index) {
            a64_function_expression(
                text,
                expression->arguments[index],
                emitter
            );
        }
        for (size_t index = expression->argument_count; index > 0; --index) {
            if (index - 1 >= MAX_CORE_PARAMETERS) {
                fatal("native Core call has too many arguments");
            }
            a64_pop(text, (unsigned)(index - 1));
        }
        a64_function_call(text, emitter, expression->function_index);
        a64_push(text, 0); /* push return value */
        return;
    }
    if (expression->kind == FUNCTION_NEGATE) {
        a64_function_expression(text, expression->left, emitter);
        a64_pop(text, 0);
        a64_word(text, UINT32_C(0xeb0003e0)); /* negs x0, x0 */
        a64_overflow_jump(text, emitter, UINT32_C(0x54000006)); /* b.vs */
        a64_push(text, 0);
        return;
    }

    a64_function_expression(text, expression->left, emitter);
    a64_function_expression(text, expression->right, emitter);
    a64_pop(text, 1); /* right -> x1 */
    a64_pop(text, 0); /* left  -> x0 */
    if (expression->kind == FUNCTION_ADD) {
        a64_word(text, UINT32_C(0xab010000)); /* adds x0, x0, x1 */
        a64_overflow_jump(text, emitter, UINT32_C(0x54000006)); /* b.vs */
    } else if (expression->kind == FUNCTION_SUBTRACT) {
        a64_word(text, UINT32_C(0xeb010000)); /* subs x0, x0, x1 */
        a64_overflow_jump(text, emitter, UINT32_C(0x54000006)); /* b.vs */
    } else if (expression->kind == FUNCTION_MULTIPLY) {
        a64_word(text, UINT32_C(0x9b017c09)); /* mul   x9, x0, x1 */
        a64_word(text, UINT32_C(0x9b417c0a)); /* smulh x10, x0, x1 */
        a64_word(text, UINT32_C(0x937ffd2b)); /* asr   x11, x9, #63 */
        a64_word(text, UINT32_C(0xeb0b015f)); /* cmp   x10, x11 */
        a64_overflow_jump(text, emitter, UINT32_C(0x54000001)); /* b.ne */
        a64_word(text, UINT32_C(0xaa0903e0)); /* mov   x0, x9 */
    } else {
        a64_word(text, UINT32_C(0xeb01001f)); /* cmp x0, x1 */
        uint32_t set = UINT32_C(0x9a9f17e0); /* cset x0, eq */
        if (expression->kind == FUNCTION_NOT_EQUAL) {
            set = UINT32_C(0x9a9f07e0); /* cset x0, ne */
        } else if (expression->kind == FUNCTION_LESS) {
            set = UINT32_C(0x9a9fa7e0); /* cset x0, lt */
        } else if (expression->kind == FUNCTION_LESS_EQUAL) {
            set = UINT32_C(0x9a9fc7e0); /* cset x0, le */
        } else if (expression->kind == FUNCTION_GREATER) {
            set = UINT32_C(0x9a9fd7e0); /* cset x0, gt */
        } else if (expression->kind == FUNCTION_GREATER_EQUAL) {
            set = UINT32_C(0x9a9fb7e0); /* cset x0, ge */
        }
        a64_word(text, set);
    }
    a64_push(text, 0);
}

static void a64_function_declaration(
    Bytes *text,
    const FunctionDeclaration *function,
    FunctionEmitter *emitter
) {
    a64_word(text, UINT32_C(0xa9bf7bfd)); /* stp x29, x30, [sp, #-16]! */
    a64_word(text, UINT32_C(0x910003fd)); /* mov x29, sp */
    if (function->parameter_count > 0) {
        uint32_t frame = (uint32_t)(
            ((function->parameter_count * sizeof(uint64_t)) + 15) /
                16 * 16
        );
        a64_sub_sp(text, frame);
        for (size_t index = 0;
             index < function->parameter_count;
             ++index) {
            a64_store_param(text, (unsigned)index, index);
        }
    }

    bool returned = false;
    for (size_t index = 0; index < function->statement_count; ++index) {
        const FunctionStatement *statement = &function->statements[index];
        if (statement->kind == FUNCTION_STATEMENT_IF_RETURN) {
            a64_function_expression(text, statement->condition, emitter);
            a64_pop(text, 0);
            size_t skip = text->length;
            a64_word(text, UINT32_C(0xb4000000)); /* cbz x0, skip */
            a64_function_expression(text, statement->value, emitter);
            a64_pop(text, 0);
            a64_function_epilogue(text);
            a64_patch_imm19(text, skip, text->length);
        } else if (statement->kind == FUNCTION_STATEMENT_RETURN) {
            a64_function_expression(text, statement->value, emitter);
            a64_pop(text, 0);
            a64_function_epilogue(text);
            returned = true;
        } else if (statement->kind == FUNCTION_STATEMENT_PRINT) {
            a64_function_expression(text, statement->value, emitter);
            a64_pop(text, 0); /* print argument -> x0 */
            offsets_add(&emitter->print_calls, text->length);
            a64_word(text, UINT32_C(0x94000000)); /* bl print (patched) */
        } else {
            a64_function_expression(text, statement->value, emitter);
            a64_pop(text, 0); /* discard expression result */
        }
    }
    if (!returned) {
        a64_movz(text, 0, 0); /* implicit main return 0 */
        a64_function_epilogue(text);
    }
}

/*
 * Print a signed 64-bit integer in decimal followed by a newline, matching
 * x64_function_print_runtime. The value arrives in x0; digits are written into
 * a stack buffer from the right and the whole run is emitted with one write(2).
 */
static size_t a64_function_print_runtime(Bytes *text) {
    size_t runtime_at = text->length;
    a64_word(text, UINT32_C(0xa9bf7bfd)); /* stp x29, x30, [sp, #-16]! */
    a64_word(text, UINT32_C(0x910003fd)); /* mov x29, sp */
    a64_sub_sp(text, 32);                 /* sub sp, sp, #32 (buffer) */
    a64_word(text, UINT32_C(0xaa0003eb)); /* mov  x11, x0 (value) */
    a64_movz(text, 10, 0);                /* mov  x10, #0 (sign flag) */
    a64_movz(text, 14, 10);               /* mov  x14, #10 ('\n') */
    a64_word(text, UINT32_C(0xd10007a9)); /* sub  x9, x29, #1 */
    a64_word(text, UINT32_C(0x3900012e)); /* strb w14, [x9] */
    a64_word(text, UINT32_C(0xf100017f)); /* cmp  x11, #0 */
    size_t nonnegative = text->length;
    a64_word(text, UINT32_C(0x5400000a)); /* b.ge magnitude */
    a64_word(text, UINT32_C(0xcb0b03eb)); /* neg  x11, x11 */
    a64_movz(text, 10, 1);                /* mov  x10, #1 (negative) */
    size_t magnitude = text->length;
    a64_patch_imm19(text, nonnegative, magnitude);
    size_t to_digits = text->length;
    a64_word(text, UINT32_C(0xb500000b)); /* cbnz x11, digits */
    a64_word(text, UINT32_C(0xd1000529)); /* sub  x9, x9, #1 */
    a64_movz(text, 14, 48);               /* mov  x14, #'0' */
    a64_word(text, UINT32_C(0x3900012e)); /* strb w14, [x9] */
    size_t to_sign = text->length;
    a64_word(text, UINT32_C(0x14000000)); /* b sign */
    size_t digits = text->length;
    a64_patch_imm19(text, to_digits, digits);
    a64_movz(text, 13, 10);               /* mov  x13, #10 */
    size_t digit_loop = text->length;
    a64_word(text, UINT32_C(0x9acd096c)); /* udiv x12, x11, x13 */
    a64_word(text, UINT32_C(0x9b0dad8e)); /* msub x14, x12, x13, x11 */
    a64_word(text, UINT32_C(0x9100c1ce)); /* add  x14, x14, #48 */
    a64_word(text, UINT32_C(0xd1000529)); /* sub  x9, x9, #1 */
    a64_word(text, UINT32_C(0x3900012e)); /* strb w14, [x9] */
    a64_word(text, UINT32_C(0xaa0c03eb)); /* mov  x11, x12 */
    size_t digit_back = text->length;
    a64_word(text, UINT32_C(0xb500000b)); /* cbnz x11, digit_loop */
    a64_patch_imm19(text, digit_back, digit_loop);
    size_t sign = text->length;
    a64_patch_imm26(text, to_sign, sign);
    size_t to_write = text->length;
    a64_word(text, UINT32_C(0xb400000a)); /* cbz x10, write */
    a64_word(text, UINT32_C(0xd1000529)); /* sub  x9, x9, #1 */
    a64_movz(text, 14, 45);               /* mov  x14, #'-' */
    a64_word(text, UINT32_C(0x3900012e)); /* strb w14, [x9] */
    size_t write = text->length;
    a64_patch_imm19(text, to_write, write);
    a64_word(text, UINT32_C(0xcb0903a2)); /* sub  x2, x29, x9 (length) */
    a64_word(text, UINT32_C(0xaa0903e1)); /* mov  x1, x9 (buffer) */
    a64_movz(text, 0, 1);                 /* mov  x0, #1 (stdout) */
    a64_movz(text, 8, 64);                /* mov  x8, #64 (write) */
    a64_svc(text);
    a64_function_epilogue(text);
    return runtime_at;
}

static void a64_function_program(
    Bytes *text,
    const FunctionProgram *program
) {
    FunctionEmitter emitter = {0};
    size_t function_addresses[MAX_CORE_FUNCTIONS] = {0};

    size_t entry_call = text->length;
    a64_word(text, UINT32_C(0x94000000)); /* bl main (patched) */
    a64_movz(text, 8, 93);                /* mov x8, #93 (exit) */
    a64_svc(text);
    a64_word(text, UINT32_C(0xd4200000)); /* brk #0 after exit */

    for (size_t index = 0; index < program->function_count; ++index) {
        function_addresses[index] = text->length;
        a64_function_declaration(
            text,
            &program->functions[index],
            &emitter
        );
    }

    size_t print_at = a64_function_print_runtime(text);

    size_t overflow_at = text->length;
    static const char overflow_message[] = "kofun: integer overflow\n";
    size_t overflow_length = sizeof(overflow_message) - 1;
    size_t message_low_field = text->length;
    a64_movz(text, 1, 0);                 /* mov x1, #0 (message low, patched) */
    size_t message_high_field = text->length;
    a64_movk_lsl16(text, 1, 0);           /* movk x1, #0, lsl 16 (patched) */
    a64_movz(text, 0, 2);                 /* mov x0, #2 (stderr) */
    a64_movz(text, 2, (unsigned)overflow_length); /* mov x2, #length */
    a64_movz(text, 8, 64);                /* mov x8, #64 (write) */
    a64_svc(text);
    a64_movz(text, 0, 1);                 /* mov x0, #1 (exit code) */
    a64_movz(text, 8, 93);                /* mov x8, #93 (exit) */
    a64_svc(text);
    a64_word(text, UINT32_C(0xd4200000)); /* brk #0 */
    size_t message_at = text->length;
    for (size_t index = 0; index < overflow_length; ++index) {
        byte(text, (uint8_t)overflow_message[index]);
    }

    uint64_t message_address =
        IMAGE_BASE + (uint64_t)TEXT_OFFSET + (uint64_t)message_at;
    a64_patch_mov_imm16(
        text,
        message_low_field,
        (uint32_t)(message_address & UINT64_C(0xffff))
    );
    a64_patch_mov_imm16(
        text,
        message_high_field,
        (uint32_t)((message_address >> 16) & UINT64_C(0xffff))
    );

    a64_patch_imm26(
        text,
        entry_call,
        function_addresses[program->main_index]
    );
    for (size_t index = 0; index < emitter.calls.length; ++index) {
        FunctionCallFixup fixup = emitter.calls.items[index];
        a64_patch_imm26(
            text,
            fixup.field,
            function_addresses[fixup.function_index]
        );
    }
    for (size_t index = 0; index < emitter.print_calls.length; ++index) {
        a64_patch_imm26(text, emitter.print_calls.fields[index], print_at);
    }
    for (size_t index = 0; index < emitter.overflow_jumps.length; ++index) {
        a64_patch_imm19(
            text,
            emitter.overflow_jumps.fields[index],
            overflow_at
        );
    }
    function_emitter_free(&emitter);
}

/*
 * AArch64 local/List/Text Core.
 *
 * Values use the same stack-machine discipline and aggregate ABIs as x86-64:
 * List is `[length: i64][element: i64] * length`; Text is
 * `[UTF-8 byte length: i64][bytes]`. x19..x26 hold loop state across runtime
 * calls; expression temporaries use x0/x1/x9 and 16-byte stack cells. Every
 * fixed word was checked with
 * `llvm-mc --triple=aarch64 --show-encoding`.
 */

typedef struct {
    size_t low_field;
    size_t high_field;
    const Node *literal;
} A64TextFixup;

typedef struct {
    A64TextFixup *items;
    size_t length;
    size_t capacity;
} A64TextFixups;

typedef struct {
    bool used;
    Offsets allocate_calls;
    Offsets oom_jumps;
    Offsets list_index_jumps;
    Offsets text_index_jumps;
    Offsets text_concat_calls;
    Offsets text_equal_calls;
    Offsets text_length_calls;
    Offsets text_index_calls;
    Offsets text_chars_calls;
    A64TextFixups text_literals;
} A64CoreRuntime;

static void a64_text_fixups_add(
    A64TextFixups *fixups,
    size_t low_field,
    size_t high_field,
    const Node *literal
) {
    if (fixups->length == fixups->capacity) {
        size_t capacity =
            fixups->capacity == 0 ? 8 : fixups->capacity * 2;
        A64TextFixup *grown = realloc(
            fixups->items,
            capacity * sizeof(*fixups->items)
        );
        if (grown == NULL) fatal("out of memory");
        fixups->items = grown;
        fixups->capacity = capacity;
    }
    fixups->items[fixups->length++] = (A64TextFixup){
        .low_field = low_field,
        .high_field = high_field,
        .literal = literal,
    };
}

static void a64_core_runtime_free(A64CoreRuntime *runtime) {
    free(runtime->allocate_calls.fields);
    free(runtime->oom_jumps.fields);
    free(runtime->list_index_jumps.fields);
    free(runtime->text_index_jumps.fields);
    free(runtime->text_concat_calls.fields);
    free(runtime->text_equal_calls.fields);
    free(runtime->text_length_calls.fields);
    free(runtime->text_index_calls.fields);
    free(runtime->text_chars_calls.fields);
    free(runtime->text_literals.items);
}

static void a64_move_register(
    Bytes *text,
    unsigned destination,
    unsigned source
) {
    a64_word(
        text,
        UINT32_C(0xaa0003e0) |
            ((uint32_t)source << 16) |
            (uint32_t)destination
    );
}

static void a64_subtract(
    Bytes *text,
    unsigned destination,
    unsigned left,
    unsigned right
) {
    a64_word(
        text,
        UINT32_C(0xcb000000) |
            ((uint32_t)right << 16) |
            ((uint32_t)left << 5) |
            (uint32_t)destination
    );
}

static void a64_compare(
    Bytes *text,
    unsigned left,
    unsigned right
) {
    a64_word(
        text,
        UINT32_C(0xeb00001f) |
            ((uint32_t)right << 16) |
            ((uint32_t)left << 5)
    );
}

static void a64_compare_zero(Bytes *text, unsigned source) {
    a64_word(
        text,
        UINT32_C(0xf100001f) | ((uint32_t)source << 5)
    );
}

static void a64_load_u64(
    Bytes *text,
    unsigned destination,
    unsigned address,
    unsigned offset
) {
    if (offset % sizeof(uint64_t) != 0 ||
        offset / sizeof(uint64_t) > UINT32_C(0xfff)) {
        fatal("aarch64 Core load offset is out of range");
    }
    a64_word(
        text,
        UINT32_C(0xf9400000) |
            ((uint32_t)(offset / sizeof(uint64_t)) << 10) |
            ((uint32_t)address << 5) |
            (uint32_t)destination
    );
}

static void a64_store_u64(
    Bytes *text,
    unsigned source,
    unsigned address,
    unsigned offset
) {
    if (offset % sizeof(uint64_t) != 0 ||
        offset / sizeof(uint64_t) > UINT32_C(0xfff)) {
        fatal("aarch64 Core store offset is out of range");
    }
    a64_word(
        text,
        UINT32_C(0xf9000000) |
            ((uint32_t)(offset / sizeof(uint64_t)) << 10) |
            ((uint32_t)address << 5) |
            (uint32_t)source
    );
}

static void a64_load_indexed(
    Bytes *text,
    unsigned destination,
    unsigned address,
    unsigned index
) {
    a64_word(
        text,
        UINT32_C(0xf8607800) |
            ((uint32_t)index << 16) |
            ((uint32_t)address << 5) |
            (uint32_t)destination
    );
}

static void a64_store_indexed(
    Bytes *text,
    unsigned source,
    unsigned address,
    unsigned index
) {
    a64_word(
        text,
        UINT32_C(0xf8207800) |
            ((uint32_t)index << 16) |
            ((uint32_t)address << 5) |
            (uint32_t)source
    );
}

static void a64_load_u8(
    Bytes *text,
    unsigned destination,
    unsigned address
) {
    a64_word(
        text,
        UINT32_C(0x39400000) |
            ((uint32_t)address << 5) |
            (uint32_t)destination
    );
}

static void a64_load_u8_indexed(
    Bytes *text,
    unsigned destination,
    unsigned address,
    unsigned index
) {
    a64_word(
        text,
        UINT32_C(0x38606800) |
            ((uint32_t)index << 16) |
            ((uint32_t)address << 5) |
            (uint32_t)destination
    );
}

static void a64_store_u8_indexed(
    Bytes *text,
    unsigned source,
    unsigned address,
    unsigned index
) {
    a64_word(
        text,
        UINT32_C(0x38206800) |
            ((uint32_t)index << 16) |
            ((uint32_t)address << 5) |
            (uint32_t)source
    );
}

static void a64_and(
    Bytes *text,
    unsigned destination,
    unsigned left,
    unsigned right
) {
    a64_word(
        text,
        UINT32_C(0x8a000000) |
            ((uint32_t)right << 16) |
            ((uint32_t)left << 5) |
            (uint32_t)destination
    );
}

static void a64_shift_left_three(
    Bytes *text,
    unsigned destination,
    unsigned source
) {
    a64_word(
        text,
        UINT32_C(0xd37df000) |
            ((uint32_t)source << 5) |
            (uint32_t)destination
    );
}

static void a64_sub_immediate(
    Bytes *text,
    unsigned destination,
    unsigned source,
    unsigned value
) {
    if (value > UINT32_C(0xfff)) {
        fatal("aarch64 Core immediate subtraction is out of range");
    }
    a64_word(
        text,
        UINT32_C(0xd1000000) |
            ((uint32_t)value << 10) |
            ((uint32_t)source << 5) |
            (uint32_t)destination
    );
}

static void a64_load_address(
    Bytes *text,
    unsigned destination,
    uint64_t address
) {
    if (address > UINT32_MAX) {
        fatal("aarch64 Core address exceeds the small static image");
    }
    a64_movz(text, destination, (unsigned)(address & UINT64_C(0xffff)));
    a64_movk_lsl16(
        text,
        destination,
        (unsigned)((address >> 16) & UINT64_C(0xffff))
    );
}

static void a64_load_core_local(
    Bytes *text,
    unsigned destination,
    size_t slot
) {
    if (slot > (UINT32_C(0xfff) / sizeof(uint64_t)) - 1) {
        fatal("aarch64 Core local frame is too large");
    }
    unsigned offset = (unsigned)((slot + 1) * sizeof(uint64_t));
    a64_sub_immediate(text, 9, 29, offset);
    a64_load_u64(text, destination, 9, 0);
}

static void a64_store_core_local(
    Bytes *text,
    unsigned source,
    size_t slot
) {
    if (slot > (UINT32_C(0xfff) / sizeof(uint64_t)) - 1) {
        fatal("aarch64 Core local frame is too large");
    }
    unsigned offset = (unsigned)((slot + 1) * sizeof(uint64_t));
    a64_sub_immediate(text, 9, 29, offset);
    a64_store_u64(text, source, 9, 0);
}

static size_t a64_core_conditional(
    Bytes *text,
    uint32_t instruction
) {
    size_t field = text->length;
    a64_word(text, instruction);
    return field;
}

static size_t a64_core_branch(Bytes *text) {
    size_t field = text->length;
    a64_word(text, UINT32_C(0x14000000));
    return field;
}

static void a64_core_call_allocate(
    Bytes *text,
    A64CoreRuntime *runtime
) {
    runtime->used = true;
    offsets_add(&runtime->allocate_calls, text->length);
    a64_word(text, UINT32_C(0x94000000)); /* bl allocate */
}

static void a64_core_call_runtime(
    Bytes *text,
    A64CoreRuntime *runtime,
    Offsets *calls
) {
    runtime->used = true;
    offsets_add(calls, text->length);
    a64_word(text, UINT32_C(0x94000000)); /* bl runtime helper */
}

static void a64_core_expression(
    Bytes *text,
    const Node *expression,
    A64CoreRuntime *runtime
) {
    if (expression->kind == NODE_INDEX &&
        expression->left->value_kind == VALUE_TEXT &&
        expression->left->value_known &&
        expression->right->value_known &&
        !expression->value_known) {
        runtime->used = true;
        a64_movz(text, 0, 0);
        a64_compare_zero(text, 0);
        offsets_add(&runtime->text_index_jumps, text->length);
        a64_word(text, UINT32_C(0x54000000)); /* b.eq Text error */
        a64_push(text, 0); /* unreachable stack result */
        return;
    }

    if (expression->kind == NODE_LENGTH &&
        expression->value_known) {
        a64_load_immediate(text, 0, expression->value);
        a64_push(text, 0);
        return;
    }

    if (expression->value_kind == VALUE_TEXT &&
        expression->value_known &&
        expression->text_value != NULL &&
        expression->kind != NODE_TEXT_LITERAL &&
        expression->kind != NODE_TEXT_CONCAT) {
        runtime->used = true;
        size_t low_field = text->length;
        a64_movz(text, 0, 0);              /* folded Text low, patched */
        size_t high_field = text->length;
        a64_movk_lsl16(text, 0, 0);        /* folded Text high, patched */
        a64_text_fixups_add(
            &runtime->text_literals,
            low_field,
            high_field,
            expression
        );
        a64_push(text, 0);
        return;
    }

    if (expression->kind == NODE_LITERAL) {
        a64_load_immediate(text, 0, expression->value);
        a64_push(text, 0);
        return;
    }

    if (expression->kind == NODE_VARIABLE ||
        expression->kind == NODE_PARAMETER) {
        a64_load_core_local(text, 0, expression->slot);
        a64_push(text, 0);
        return;
    }

    if (expression->kind == NODE_LET) {
        a64_core_expression(text, expression->left, runtime);
        a64_pop(text, 0);
        a64_store_core_local(text, 0, expression->slot);
        a64_core_expression(text, expression->right, runtime);
        return;
    }

    if (expression->kind == NODE_TEXT_LITERAL) {
        runtime->used = true;
        size_t low_field = text->length;
        a64_movz(text, 0, 0);              /* Text address low, patched */
        size_t high_field = text->length;
        a64_movk_lsl16(text, 0, 0);        /* Text address high, patched */
        a64_text_fixups_add(
            &runtime->text_literals,
            low_field,
            high_field,
            expression
        );
        a64_push(text, 0);
        return;
    }

    if (expression->kind == NODE_NEGATE) {
        a64_core_expression(text, expression->left, runtime);
        a64_pop(text, 0);
        a64_subtract(text, 0, 31, 0); /* neg x0, x0 */
        a64_push(text, 0);
        return;
    }

    if (expression->kind == NODE_CHARS &&
        !expression->value_known) {
        a64_core_expression(text, expression->left, runtime);
        a64_pop(text, 0);                  /* source Text */
        a64_core_call_runtime(
            text,
            runtime,
            &runtime->text_chars_calls
        );
        a64_push(text, 0);                 /* List[Text] */
        return;
    }

    if (expression->kind == NODE_LIST ||
        expression->kind == NODE_CHARS ||
        expression->kind == NODE_CODEPOINTS ||
        expression->kind == NODE_BYTES) {
        if (expression->item_count >
            (UINT32_MAX - sizeof(uint64_t)) / sizeof(uint64_t)) {
            fatal("native Core list is too large");
        }
        uint32_t bytes = (uint32_t)(
            sizeof(uint64_t) +
            expression->item_count * sizeof(uint64_t)
        );
        a64_load_immediate(text, 0, bytes);
        a64_core_call_allocate(text, runtime);
        a64_load_immediate(text, 1, (int64_t)expression->item_count);
        a64_store_u64(text, 1, 0, 0);
        a64_push(text, 0); /* keep the list pointer below each item */

        for (size_t index = 0; index < expression->item_count; ++index) {
            a64_core_expression(text, expression->items[index], runtime);
            a64_pop(text, 1); /* item */
            a64_pop(text, 0); /* list */
            a64_load_immediate(text, 2, (int64_t)index);
            a64_add_immediate(text, 9, 0, 8);
            a64_store_indexed(text, 1, 9, 2);
            a64_push(text, 0);
        }
        return;
    }

    if (expression->kind == NODE_MAP) {
        a64_core_expression(text, expression->left, runtime);
        a64_pop(text, 19);                 /* source */
        a64_load_u64(text, 22, 19, 0);     /* length */
        a64_shift_left_three(text, 0, 22);
        a64_add_immediate(text, 0, 0, 8);
        a64_core_call_allocate(text, runtime);
        a64_move_register(text, 20, 0);    /* output */
        a64_store_u64(text, 22, 20, 0);
        a64_movz(text, 21, 0);             /* index */

        size_t loop = text->length;
        a64_compare(text, 21, 22);
        size_t done =
            a64_core_conditional(text, UINT32_C(0x5400000a)); /* b.ge */
        a64_add_immediate(text, 9, 19, 8);
        a64_load_indexed(text, 0, 9, 21);
        a64_store_core_local(text, 0, expression->slot);
        a64_core_expression(text, expression->right, runtime);
        a64_pop(text, 0);
        a64_add_immediate(text, 9, 20, 8);
        a64_store_indexed(text, 0, 9, 21);
        a64_add_immediate(text, 21, 21, 1);
        size_t back = a64_core_branch(text);
        size_t done_at = text->length;
        a64_patch_imm19(text, done, done_at);
        a64_patch_imm26(text, back, loop);
        a64_push(text, 20);
        return;
    }

    if (expression->kind == NODE_FILTER) {
        a64_core_expression(text, expression->left, runtime);
        a64_pop(text, 19);                 /* source */
        a64_load_u64(text, 23, 19, 0);     /* source length */
        a64_shift_left_three(text, 0, 23);
        a64_add_immediate(text, 0, 0, 8);
        a64_core_call_allocate(text, runtime);
        a64_move_register(text, 20, 0);    /* output */
        a64_movz(text, 21, 0);             /* source index */
        a64_movz(text, 22, 0);             /* output count */

        size_t loop = text->length;
        a64_compare(text, 21, 23);
        size_t done =
            a64_core_conditional(text, UINT32_C(0x5400000a)); /* b.ge */
        a64_add_immediate(text, 9, 19, 8);
        a64_load_indexed(text, 0, 9, 21);
        a64_store_core_local(text, 0, expression->slot);
        a64_core_expression(text, expression->right, runtime);
        a64_pop(text, 0);
        size_t skip =
            a64_core_conditional(text, UINT32_C(0xb4000000)); /* cbz x0 */
        a64_load_core_local(text, 0, expression->slot);
        a64_add_immediate(text, 9, 20, 8);
        a64_store_indexed(text, 0, 9, 22);
        a64_add_immediate(text, 22, 22, 1);
        size_t skip_at = text->length;
        a64_add_immediate(text, 21, 21, 1);
        size_t back = a64_core_branch(text);
        size_t done_at = text->length;
        a64_store_u64(text, 22, 20, 0);
        a64_patch_imm19(text, done, done_at);
        a64_patch_imm19(text, skip, skip_at);
        a64_patch_imm26(text, back, loop);
        a64_push(text, 20);
        return;
    }

    if (expression->kind == NODE_FOLD) {
        a64_core_expression(text, expression->left, runtime);
        a64_core_expression(text, expression->right, runtime);
        a64_pop(text, 22);                 /* accumulator */
        a64_pop(text, 19);                 /* source */
        a64_load_u64(text, 23, 19, 0);     /* length */
        a64_movz(text, 21, 0);             /* index */

        size_t loop = text->length;
        a64_compare(text, 21, 23);
        size_t done =
            a64_core_conditional(text, UINT32_C(0x5400000a)); /* b.ge */
        a64_store_core_local(text, 22, expression->slot);
        a64_add_immediate(text, 9, 19, 8);
        a64_load_indexed(text, 0, 9, 21);
        a64_store_core_local(text, 0, expression->slot + 1);
        a64_core_expression(text, expression->third, runtime);
        a64_pop(text, 22);
        a64_add_immediate(text, 21, 21, 1);
        size_t back = a64_core_branch(text);
        size_t done_at = text->length;
        a64_patch_imm19(text, done, done_at);
        a64_patch_imm26(text, back, loop);
        a64_push(text, 22);
        return;
    }

    if (expression->kind == NODE_LENGTH) {
        a64_core_expression(text, expression->left, runtime);
        a64_pop(text, 0);
        if (expression->left->value_kind == VALUE_TEXT) {
            a64_core_call_runtime(
                text,
                runtime,
                &runtime->text_length_calls
            );
        } else {
            a64_load_u64(text, 0, 0, 0);
        }
        a64_push(text, 0);
        return;
    }

    if (expression->kind == NODE_INDEX) {
        runtime->used = true;
        a64_core_expression(text, expression->left, runtime);
        a64_core_expression(text, expression->right, runtime);
        a64_pop(text, 1);                  /* index */
        a64_pop(text, 2);                  /* Text or List */
        if (expression->left->value_kind == VALUE_TEXT) {
            a64_move_register(text, 0, 2);
            a64_core_call_runtime(
                text,
                runtime,
                &runtime->text_index_calls
            );
            a64_push(text, 0);             /* one-codepoint Text */
            return;
        }
        a64_load_u64(text, 3, 2, 0);       /* length */
        a64_compare_zero(text, 1);
        size_t nonnegative =
            a64_core_conditional(text, UINT32_C(0x5400000a)); /* b.ge */
        a64_add(text, 1, 1, 3);
        size_t nonnegative_at = text->length;
        a64_patch_imm19(text, nonnegative, nonnegative_at);

        a64_compare_zero(text, 1);
        offsets_add(&runtime->list_index_jumps, text->length);
        a64_word(text, UINT32_C(0x5400000b)); /* b.lt list error */
        a64_compare(text, 1, 3);
        offsets_add(&runtime->list_index_jumps, text->length);
        a64_word(text, UINT32_C(0x5400000a)); /* b.ge list error */
        a64_add_immediate(text, 9, 2, 8);
        a64_load_indexed(text, 0, 9, 1);
        a64_push(text, 0);
        return;
    }

    if (expression->kind == NODE_TEXT_CONCAT ||
        expression->kind == NODE_TEXT_EQUAL ||
        expression->kind == NODE_TEXT_NOT_EQUAL) {
        a64_core_expression(text, expression->left, runtime);
        a64_core_expression(text, expression->right, runtime);
        a64_pop(text, 1);                  /* right Text */
        a64_pop(text, 0);                  /* left Text */
        if (expression->kind == NODE_TEXT_CONCAT) {
            a64_core_call_runtime(
                text,
                runtime,
                &runtime->text_concat_calls
            );
        } else {
            a64_core_call_runtime(
                text,
                runtime,
                &runtime->text_equal_calls
            );
            if (expression->kind == NODE_TEXT_NOT_EQUAL) {
                a64_word(text, UINT32_C(0xd2400000)); /* eor x0, x0, #1 */
            }
        }
        a64_push(text, 0);
        return;
    }

    if (expression->kind == NODE_INT_EQUAL ||
        expression->kind == NODE_INT_NOT_EQUAL ||
        expression->kind == NODE_INT_LESS ||
        expression->kind == NODE_INT_LESS_EQUAL ||
        expression->kind == NODE_INT_GREATER ||
        expression->kind == NODE_INT_GREATER_EQUAL) {
        a64_core_expression(text, expression->left, runtime);
        a64_core_expression(text, expression->right, runtime);
        a64_pop(text, 1);
        a64_pop(text, 0);
        a64_compare(text, 0, 1);
        uint32_t instruction = UINT32_C(0x9a9f17e0); /* cset eq */
        if (expression->kind == NODE_INT_NOT_EQUAL) {
            instruction = UINT32_C(0x9a9f07e0);
        } else if (expression->kind == NODE_INT_LESS) {
            instruction = UINT32_C(0x9a9fa7e0);
        } else if (expression->kind == NODE_INT_LESS_EQUAL) {
            instruction = UINT32_C(0x9a9fc7e0);
        } else if (expression->kind == NODE_INT_GREATER) {
            instruction = UINT32_C(0x9a9fd7e0);
        } else if (expression->kind == NODE_INT_GREATER_EQUAL) {
            instruction = UINT32_C(0x9a9fb7e0);
        }
        a64_word(text, instruction);
        a64_push(text, 0);
        return;
    }

    if (expression->kind == NODE_ADD ||
        expression->kind == NODE_MULTIPLY) {
        a64_core_expression(text, expression->left, runtime);
        a64_core_expression(text, expression->right, runtime);
        a64_pop(text, 1);
        a64_pop(text, 0);
        if (expression->kind == NODE_ADD) {
            a64_add(text, 0, 0, 1);
        } else {
            a64_multiply(text, 0, 0, 1);
        }
        a64_push(text, 0);
        return;
    }

    fatal("unsupported expression reached AArch64 aggregate lowering");
}

static void a64_core_diagnostic(
    Bytes *text,
    uint32_t length,
    uint32_t status,
    size_t *low_field,
    size_t *high_field
) {
    *low_field = text->length;
    a64_movz(text, 1, 0);                  /* message low, patched */
    *high_field = text->length;
    a64_movk_lsl16(text, 1, 0);            /* message high, patched */
    a64_movz(text, 0, 2);                  /* stderr */
    a64_movz(text, 2, length);
    a64_movz(text, 8, 64);                 /* write */
    a64_svc(text);
    a64_movz(text, 0, status);
    a64_movz(text, 8, 93);                 /* exit */
    a64_svc(text);
    a64_word(text, UINT32_C(0xd4200000));  /* brk #0 */
}

static void a64_runtime_save(Bytes *text) {
    a64_word(text, UINT32_C(0xa9bf7bfd)); /* stp x29, x30, [sp, #-16]! */
    a64_word(text, UINT32_C(0xa9bf53f3)); /* stp x19, x20, [sp, #-16]! */
    a64_word(text, UINT32_C(0xa9bf5bf5)); /* stp x21, x22, [sp, #-16]! */
    a64_word(text, UINT32_C(0xa9bf63f7)); /* stp x23, x24, [sp, #-16]! */
    a64_word(text, UINT32_C(0xa9bf6bf9)); /* stp x25, x26, [sp, #-16]! */
}

static void a64_runtime_restore(Bytes *text) {
    a64_word(text, UINT32_C(0xa8c16bf9)); /* ldp x25, x26, [sp], #16 */
    a64_word(text, UINT32_C(0xa8c163f7)); /* ldp x23, x24, [sp], #16 */
    a64_word(text, UINT32_C(0xa8c15bf5)); /* ldp x21, x22, [sp], #16 */
    a64_word(text, UINT32_C(0xa8c153f3)); /* ldp x19, x20, [sp], #16 */
    a64_word(text, UINT32_C(0xa8c17bfd)); /* ldp x29, x30, [sp], #16 */
    a64_word(text, UINT32_C(0xd65f03c0)); /* ret */
}

static size_t a64_text_length_runtime(Bytes *text) {
    size_t runtime_at = text->length;
    a64_load_u64(text, 1, 0, 0);           /* remaining UTF-8 bytes */
    a64_add_immediate(text, 2, 0, 8);      /* byte cursor */
    a64_movz(text, 0, 0);                  /* codepoint count */
    a64_movz(text, 9, UINT16_C(0xc0));     /* continuation mask */
    a64_movz(text, 10, UINT16_C(0x80));    /* continuation tag */

    size_t loop = text->length;
    size_t done =
        a64_core_conditional(text, UINT32_C(0xb4000001)); /* cbz x1 */
    a64_load_u8(text, 3, 2);
    a64_and(text, 3, 3, 9);
    a64_compare(text, 3, 10);
    size_t skip =
        a64_core_conditional(text, UINT32_C(0x54000000)); /* b.eq */
    a64_add_immediate(text, 0, 0, 1);
    size_t skip_at = text->length;
    a64_add_immediate(text, 2, 2, 1);
    a64_sub_immediate(text, 1, 1, 1);
    size_t back = a64_core_branch(text);
    size_t done_at = text->length;
    a64_word(text, UINT32_C(0xd65f03c0));  /* ret */

    a64_patch_imm19(text, done, done_at);
    a64_patch_imm19(text, skip, skip_at);
    a64_patch_imm26(text, back, loop);
    return runtime_at;
}

static size_t a64_text_equal_runtime(Bytes *text) {
    size_t runtime_at = text->length;
    a64_load_u64(text, 2, 0, 0);
    a64_load_u64(text, 3, 1, 0);
    a64_compare(text, 2, 3);
    size_t different_length =
        a64_core_conditional(text, UINT32_C(0x54000001)); /* b.ne */
    a64_add_immediate(text, 0, 0, 8);
    a64_add_immediate(text, 1, 1, 8);

    size_t loop = text->length;
    size_t equal =
        a64_core_conditional(text, UINT32_C(0xb4000002)); /* cbz x2 */
    a64_load_u8(text, 4, 0);
    a64_load_u8(text, 5, 1);
    a64_compare(text, 4, 5);
    size_t different_byte =
        a64_core_conditional(text, UINT32_C(0x54000001)); /* b.ne */
    a64_add_immediate(text, 0, 0, 1);
    a64_add_immediate(text, 1, 1, 1);
    a64_sub_immediate(text, 2, 2, 1);
    size_t back = a64_core_branch(text);

    size_t equal_at = text->length;
    a64_movz(text, 0, 1);
    a64_word(text, UINT32_C(0xd65f03c0));  /* ret */
    size_t different_at = text->length;
    a64_movz(text, 0, 0);
    a64_word(text, UINT32_C(0xd65f03c0));  /* ret */

    a64_patch_imm19(text, different_length, different_at);
    a64_patch_imm19(text, equal, equal_at);
    a64_patch_imm19(text, different_byte, different_at);
    a64_patch_imm26(text, back, loop);
    return runtime_at;
}

static size_t a64_text_concat_runtime(
    Bytes *text,
    A64CoreRuntime *runtime
) {
    size_t runtime_at = text->length;
    a64_runtime_save(text);
    a64_move_register(text, 19, 0);        /* left Text */
    a64_move_register(text, 20, 1);        /* right Text */
    a64_load_u64(text, 21, 19, 0);         /* left byte length */
    a64_load_u64(text, 22, 20, 0);         /* right byte length */
    a64_add(text, 23, 21, 22);             /* total bytes */
    a64_add_immediate(text, 0, 23, 8);
    a64_core_call_allocate(text, runtime);
    a64_move_register(text, 24, 0);        /* result Text */
    a64_store_u64(text, 23, 24, 0);
    a64_add_immediate(text, 4, 24, 8);     /* destination */
    a64_add_immediate(text, 5, 19, 8);     /* left source */
    a64_move_register(text, 6, 21);        /* remaining */

    size_t left_loop = text->length;
    size_t left_done =
        a64_core_conditional(text, UINT32_C(0xb4000006)); /* cbz x6 */
    a64_load_u8(text, 7, 5);
    a64_strb(text, 7, 4, 0);
    a64_add_immediate(text, 5, 5, 1);
    a64_add_immediate(text, 4, 4, 1);
    a64_sub_immediate(text, 6, 6, 1);
    size_t left_back = a64_core_branch(text);

    size_t right_at = text->length;
    a64_add_immediate(text, 5, 20, 8);
    a64_move_register(text, 6, 22);
    size_t right_loop = text->length;
    size_t right_done =
        a64_core_conditional(text, UINT32_C(0xb4000006)); /* cbz x6 */
    a64_load_u8(text, 7, 5);
    a64_strb(text, 7, 4, 0);
    a64_add_immediate(text, 5, 5, 1);
    a64_add_immediate(text, 4, 4, 1);
    a64_sub_immediate(text, 6, 6, 1);
    size_t right_back = a64_core_branch(text);

    size_t done_at = text->length;
    a64_move_register(text, 0, 24);
    a64_runtime_restore(text);

    a64_patch_imm19(text, left_done, right_at);
    a64_patch_imm26(text, left_back, left_loop);
    a64_patch_imm19(text, right_done, done_at);
    a64_patch_imm26(text, right_back, right_loop);
    return runtime_at;
}

static size_t a64_text_index_runtime(
    Bytes *text,
    A64CoreRuntime *runtime,
    size_t text_length_at
) {
    size_t runtime_at = text->length;
    a64_runtime_save(text);
    a64_move_register(text, 19, 0);        /* source Text */
    a64_move_register(text, 20, 1);        /* requested codepoint index */
    a64_compare_zero(text, 20);
    size_t nonnegative =
        a64_core_conditional(text, UINT32_C(0x5400000a)); /* b.ge */
    a64_move_register(text, 0, 19);
    size_t length_call = text->length;
    a64_word(text, UINT32_C(0x94000000));  /* bl Text length */
    a64_patch_imm26(text, length_call, text_length_at);
    a64_add(text, 20, 20, 0);
    size_t nonnegative_at = text->length;
    a64_patch_imm19(text, nonnegative, nonnegative_at);
    a64_compare_zero(text, 20);
    offsets_add(&runtime->text_index_jumps, text->length);
    a64_word(text, UINT32_C(0x5400000b));  /* b.lt Text index error */

    a64_load_u64(text, 21, 19, 0);         /* remaining bytes */
    a64_add_immediate(text, 22, 19, 8);    /* codepoint cursor */
    a64_movz(text, 23, 0);                 /* current codepoint index */
    a64_movz(text, 9, UINT16_C(0xc0));     /* continuation mask */
    a64_movz(text, 10, UINT16_C(0x80));    /* continuation tag */

    size_t scan = text->length;
    offsets_add(&runtime->text_index_jumps, text->length);
    a64_word(text, UINT32_C(0xb4000015));  /* cbz x21, Text error */
    a64_compare(text, 23, 20);
    size_t found =
        a64_core_conditional(text, UINT32_C(0x54000000)); /* b.eq */
    a64_add_immediate(text, 22, 22, 1);
    a64_sub_immediate(text, 21, 21, 1);

    size_t continuation = text->length;
    size_t next_if_empty =
        a64_core_conditional(text, UINT32_C(0xb4000015)); /* cbz x21 */
    a64_load_u8(text, 3, 22);
    a64_and(text, 3, 3, 9);
    a64_compare(text, 3, 10);
    size_t next_if_start =
        a64_core_conditional(text, UINT32_C(0x54000001)); /* b.ne */
    a64_add_immediate(text, 22, 22, 1);
    a64_sub_immediate(text, 21, 21, 1);
    size_t continuation_back = a64_core_branch(text);

    size_t next_at = text->length;
    a64_add_immediate(text, 23, 23, 1);
    size_t scan_back = a64_core_branch(text);

    size_t found_at = text->length;
    a64_movz(text, 24, 1);                 /* codepoint byte width */
    size_t width = text->length;
    a64_compare(text, 24, 21);
    size_t width_done =
        a64_core_conditional(text, UINT32_C(0x5400000a)); /* b.ge */
    a64_load_u8_indexed(text, 3, 22, 24);
    a64_and(text, 3, 3, 9);
    a64_compare(text, 3, 10);
    size_t width_start =
        a64_core_conditional(text, UINT32_C(0x54000001)); /* b.ne */
    a64_add_immediate(text, 24, 24, 1);
    size_t width_back = a64_core_branch(text);

    size_t width_done_at = text->length;
    a64_add_immediate(text, 0, 24, 8);
    a64_core_call_allocate(text, runtime);
    a64_move_register(text, 25, 0);        /* result Text */
    a64_store_u64(text, 24, 25, 0);
    a64_add_immediate(text, 1, 25, 8);     /* destination bytes */
    a64_movz(text, 26, 0);                 /* copy index */
    size_t copy = text->length;
    a64_compare(text, 26, 24);
    size_t copy_done =
        a64_core_conditional(text, UINT32_C(0x5400000a)); /* b.ge */
    a64_load_u8_indexed(text, 3, 22, 26);
    a64_store_u8_indexed(text, 3, 1, 26);
    a64_add_immediate(text, 26, 26, 1);
    size_t copy_back = a64_core_branch(text);

    size_t copy_done_at = text->length;
    a64_move_register(text, 0, 25);
    a64_runtime_restore(text);

    a64_patch_imm19(text, found, found_at);
    a64_patch_imm19(text, next_if_empty, next_at);
    a64_patch_imm19(text, next_if_start, next_at);
    a64_patch_imm26(text, continuation_back, continuation);
    a64_patch_imm26(text, scan_back, scan);
    a64_patch_imm19(text, width_done, width_done_at);
    a64_patch_imm19(text, width_start, width_done_at);
    a64_patch_imm26(text, width_back, width);
    a64_patch_imm19(text, copy_done, copy_done_at);
    a64_patch_imm26(text, copy_back, copy);
    return runtime_at;
}

static size_t a64_text_chars_runtime(
    Bytes *text,
    A64CoreRuntime *runtime,
    size_t text_length_at
) {
    size_t runtime_at = text->length;
    a64_runtime_save(text);
    a64_move_register(text, 19, 0);        /* source Text */
    size_t length_call = text->length;
    a64_word(text, UINT32_C(0x94000000));  /* bl Text length */
    a64_patch_imm26(text, length_call, text_length_at);
    a64_move_register(text, 20, 0);        /* codepoint count */
    a64_shift_left_three(text, 0, 20);
    a64_add_immediate(text, 0, 0, 8);
    a64_core_call_allocate(text, runtime);
    a64_move_register(text, 21, 0);        /* result List */
    a64_store_u64(text, 20, 21, 0);
    a64_add_immediate(text, 22, 19, 8);    /* source cursor */
    a64_load_u64(text, 23, 19, 0);         /* remaining bytes */
    a64_movz(text, 24, 0);                 /* List element index */
    a64_movz(text, 9, UINT16_C(0xc0));     /* continuation mask */
    a64_movz(text, 10, UINT16_C(0x80));    /* continuation tag */

    size_t loop = text->length;
    size_t done =
        a64_core_conditional(text, UINT32_C(0xb4000017)); /* cbz x23 */
    a64_movz(text, 25, 1);                 /* codepoint byte width */
    size_t width = text->length;
    a64_compare(text, 25, 23);
    size_t width_done =
        a64_core_conditional(text, UINT32_C(0x5400000a)); /* b.ge */
    a64_load_u8_indexed(text, 3, 22, 25);
    a64_and(text, 3, 3, 9);
    a64_compare(text, 3, 10);
    size_t width_start =
        a64_core_conditional(text, UINT32_C(0x54000001)); /* b.ne */
    a64_add_immediate(text, 25, 25, 1);
    size_t width_back = a64_core_branch(text);

    size_t width_done_at = text->length;
    a64_add_immediate(text, 0, 25, 8);
    a64_core_call_allocate(text, runtime);
    a64_move_register(text, 26, 0);        /* one-codepoint Text */
    a64_store_u64(text, 25, 26, 0);
    a64_add_immediate(text, 1, 21, 8);
    a64_store_indexed(text, 26, 1, 24);
    a64_add_immediate(text, 1, 26, 8);     /* destination bytes */
    a64_movz(text, 0, 0);                  /* copy index */
    size_t copy = text->length;
    a64_compare(text, 0, 25);
    size_t copy_done =
        a64_core_conditional(text, UINT32_C(0x5400000a)); /* b.ge */
    a64_load_u8_indexed(text, 3, 22, 0);
    a64_store_u8_indexed(text, 3, 1, 0);
    a64_add_immediate(text, 0, 0, 1);
    size_t copy_back = a64_core_branch(text);

    size_t copy_done_at = text->length;
    a64_add(text, 22, 22, 25);
    a64_subtract(text, 23, 23, 25);
    a64_add_immediate(text, 24, 24, 1);
    size_t loop_back = a64_core_branch(text);

    size_t done_at = text->length;
    a64_move_register(text, 0, 21);
    a64_runtime_restore(text);

    a64_patch_imm19(text, done, done_at);
    a64_patch_imm19(text, width_done, width_done_at);
    a64_patch_imm19(text, width_start, width_done_at);
    a64_patch_imm26(text, width_back, width);
    a64_patch_imm19(text, copy_done, copy_done_at);
    a64_patch_imm26(text, copy_back, copy);
    a64_patch_imm26(text, loop_back, loop);
    return runtime_at;
}

static void a64_core_runtime(
    Bytes *text,
    A64CoreRuntime *runtime
) {
    if (!runtime->used) return;

    size_t allocate_at = text->length;
    a64_word(text, UINT32_C(0xf144001f)); /* cmp x0, #256, lsl #12 */
    offsets_add(&runtime->oom_jumps, text->length);
    a64_word(text, UINT32_C(0x54000008)); /* b.hi oom */
    a64_movz(text, 0, 0);                 /* address */
    a64_movz(text, 1, 0);
    a64_movk_lsl16(text, 1, 16);          /* length = 1 MiB */
    a64_movz(text, 2, 3);                 /* PROT_READ | PROT_WRITE */
    a64_movz(text, 3, 0x22);              /* PRIVATE | ANONYMOUS */
    a64_word(text, UINT32_C(0x92800004)); /* mov x4, #-1 */
    a64_movz(text, 5, 0);                 /* offset */
    a64_movz(text, 8, 222);               /* mmap */
    a64_svc(text);
    a64_word(text, UINT32_C(0xb13ffc1f)); /* cmn x0, #4095 */
    offsets_add(&runtime->oom_jumps, text->length);
    a64_word(text, UINT32_C(0x54000002)); /* b.hs oom */
    a64_word(text, UINT32_C(0xd65f03c0)); /* ret */

    size_t text_length_at = a64_text_length_runtime(text);
    size_t text_equal_at = a64_text_equal_runtime(text);
    size_t text_concat_at = a64_text_concat_runtime(text, runtime);
    size_t text_index_at =
        a64_text_index_runtime(text, runtime, text_length_at);
    size_t text_chars_at =
        a64_text_chars_runtime(text, runtime, text_length_at);

    size_t oom_at = text->length;
    static const char oom_message[] = "kofun: out of memory\n";
    size_t oom_low = 0;
    size_t oom_high = 0;
    a64_core_diagnostic(
        text,
        (uint32_t)(sizeof(oom_message) - 1),
        70,
        &oom_low,
        &oom_high
    );

    size_t list_index_at = text->length;
    static const char list_index_message[] =
        "kofun: list index out of range\n";
    size_t list_low = 0;
    size_t list_high = 0;
    a64_core_diagnostic(
        text,
        (uint32_t)(sizeof(list_index_message) - 1),
        1,
        &list_low,
        &list_high
    );

    size_t text_index_error_at = text->length;
    static const char text_index_message[] =
        "kofun: text index out of range\n";
    size_t text_index_low = 0;
    size_t text_index_high = 0;
    a64_core_diagnostic(
        text,
        (uint32_t)(sizeof(text_index_message) - 1),
        1,
        &text_index_low,
        &text_index_high
    );

    size_t oom_message_at = text->length;
    for (size_t index = 0; index < sizeof(oom_message) - 1; ++index) {
        byte(text, (uint8_t)oom_message[index]);
    }
    size_t list_message_at = text->length;
    for (
        size_t index = 0;
        index < sizeof(list_index_message) - 1;
        ++index
    ) {
        byte(text, (uint8_t)list_index_message[index]);
    }
    size_t text_index_message_at = text->length;
    for (
        size_t index = 0;
        index < sizeof(text_index_message) - 1;
        ++index
    ) {
        byte(text, (uint8_t)text_index_message[index]);
    }

    uint64_t oom_address =
        IMAGE_BASE + (uint64_t)TEXT_OFFSET + (uint64_t)oom_message_at;
    uint64_t list_address =
        IMAGE_BASE + (uint64_t)TEXT_OFFSET + (uint64_t)list_message_at;
    uint64_t text_index_address =
        IMAGE_BASE +
        (uint64_t)TEXT_OFFSET +
        (uint64_t)text_index_message_at;
    a64_patch_mov_imm16(
        text,
        oom_low,
        (uint32_t)(oom_address & UINT64_C(0xffff))
    );
    a64_patch_mov_imm16(
        text,
        oom_high,
        (uint32_t)((oom_address >> 16) & UINT64_C(0xffff))
    );
    a64_patch_mov_imm16(
        text,
        list_low,
        (uint32_t)(list_address & UINT64_C(0xffff))
    );
    a64_patch_mov_imm16(
        text,
        list_high,
        (uint32_t)((list_address >> 16) & UINT64_C(0xffff))
    );
    a64_patch_mov_imm16(
        text,
        text_index_low,
        (uint32_t)(text_index_address & UINT64_C(0xffff))
    );
    a64_patch_mov_imm16(
        text,
        text_index_high,
        (uint32_t)((text_index_address >> 16) & UINT64_C(0xffff))
    );

    for (size_t index = 0; index < runtime->allocate_calls.length; ++index) {
        a64_patch_imm26(
            text,
            runtime->allocate_calls.fields[index],
            allocate_at
        );
    }
    for (size_t index = 0; index < runtime->oom_jumps.length; ++index) {
        a64_patch_imm19(text, runtime->oom_jumps.fields[index], oom_at);
    }
    for (
        size_t index = 0;
        index < runtime->list_index_jumps.length;
        ++index
    ) {
        a64_patch_imm19(
            text,
            runtime->list_index_jumps.fields[index],
            list_index_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_index_jumps.length;
        ++index
    ) {
        a64_patch_imm19(
            text,
            runtime->text_index_jumps.fields[index],
            text_index_error_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_concat_calls.length;
        ++index
    ) {
        a64_patch_imm26(
            text,
            runtime->text_concat_calls.fields[index],
            text_concat_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_equal_calls.length;
        ++index
    ) {
        a64_patch_imm26(
            text,
            runtime->text_equal_calls.fields[index],
            text_equal_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_length_calls.length;
        ++index
    ) {
        a64_patch_imm26(
            text,
            runtime->text_length_calls.fields[index],
            text_length_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_index_calls.length;
        ++index
    ) {
        a64_patch_imm26(
            text,
            runtime->text_index_calls.fields[index],
            text_index_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_chars_calls.length;
        ++index
    ) {
        a64_patch_imm26(
            text,
            runtime->text_chars_calls.fields[index],
            text_chars_at
        );
    }
    for (
        size_t index = 0;
        index < runtime->text_literals.length;
        ++index
    ) {
        while (text->length % sizeof(uint64_t) != 0) byte(text, 0);
        size_t literal_at = text->length;
        const Node *literal = runtime->text_literals.items[index].literal;
        u64_le(text, (uint64_t)literal->text_length);
        for (size_t byte_index = 0;
             byte_index < literal->text_length;
             ++byte_index) {
            byte(text, literal->text_value[byte_index]);
        }
        uint64_t literal_address =
            IMAGE_BASE + (uint64_t)TEXT_OFFSET + (uint64_t)literal_at;
        a64_patch_mov_imm16(
            text,
            runtime->text_literals.items[index].low_field,
            (uint32_t)(literal_address & UINT64_C(0xffff))
        );
        a64_patch_mov_imm16(
            text,
            runtime->text_literals.items[index].high_field,
            (uint32_t)((literal_address >> 16) & UINT64_C(0xffff))
        );
    }
}

static void a64_core_bool_output(Bytes *text) {
    a64_load_address(text, 1, DATA_ADDRESS);
    size_t false_jump =
        a64_core_conditional(text, UINT32_C(0xb4000000)); /* cbz x0 */
    static const char true_text[] = "true\n";
    for (size_t index = 0; index < sizeof(true_text) - 1; ++index) {
        a64_movz(text, 3, (unsigned)(uint8_t)true_text[index]);
        a64_strb(text, 3, 1, (unsigned)index);
    }
    a64_movz(text, 2, (unsigned)(sizeof(true_text) - 1));
    size_t done = a64_core_branch(text);
    size_t false_at = text->length;
    static const char false_text[] = "false\n";
    for (size_t index = 0; index < sizeof(false_text) - 1; ++index) {
        a64_movz(text, 3, (unsigned)(uint8_t)false_text[index]);
        a64_strb(text, 3, 1, (unsigned)index);
    }
    a64_movz(text, 2, (unsigned)(sizeof(false_text) - 1));
    size_t done_at = text->length;
    a64_patch_imm19(text, false_jump, false_at);
    a64_patch_imm26(text, done, done_at);
    a64_movz(text, 0, 1);                  /* stdout */
    a64_movz(text, 8, 64);                 /* write */
    a64_svc(text);
}

static void a64_core_text(
    Bytes *text,
    const Node *expression,
    size_t local_count
) {
    A64CoreRuntime runtime = {0};
    if (local_count > 0) {
        if (local_count > UINT32_C(0xfff) / sizeof(uint64_t)) {
            fatal("aarch64 Core local frame is too large");
        }
        a64_word(text, UINT32_C(0xa9bf7bfd)); /* save fp/lr */
        a64_word(text, UINT32_C(0x910003fd)); /* mov x29, sp */
        uint32_t frame = (uint32_t)(
            ((local_count * sizeof(uint64_t)) + 15) / 16 * 16
        );
        a64_sub_sp(text, frame);
    }

    a64_core_expression(text, expression, &runtime);
    a64_pop(text, 0);
    if (expression->value_kind == VALUE_INT) {
        a64_movz(text, 3, 10);
        a64_udiv(text, 4, 0, 3);
        a64_msub(text, 5, 4, 3, 0);
        a64_add_immediate(text, 4, 4, 48);
        a64_add_immediate(text, 5, 5, 48);
        a64_load_address(text, 1, DATA_ADDRESS);
        a64_strb(text, 4, 1, 0);
        a64_strb(text, 5, 1, 1);
        a64_movz(text, 0, 1);              /* stdout */
        a64_movz(text, 2, 3);              /* includes existing newline */
        a64_movz(text, 8, 64);             /* write */
        a64_svc(text);
    } else if (expression->value_kind == VALUE_BOOL) {
        a64_core_bool_output(text);
    } else if (expression->value_kind == VALUE_TEXT) {
        runtime.used = true;
        a64_load_u64(text, 2, 0, 0);       /* UTF-8 byte length */
        a64_add_immediate(text, 1, 0, 8);  /* UTF-8 bytes */
        a64_movz(text, 0, 1);              /* stdout */
        a64_movz(text, 8, 64);             /* write */
        a64_svc(text);
        a64_load_address(text, 1, DATA_ADDRESS + 2);
        a64_movz(text, 0, 1);              /* stdout */
        a64_movz(text, 2, 1);              /* newline */
        a64_movz(text, 8, 64);             /* write */
        a64_svc(text);
    } else {
        fatal("AArch64 aggregate Core cannot print this value");
    }
    a64_movz(text, 0, 0);
    a64_movz(text, 8, 93);                 /* exit */
    a64_svc(text);
    a64_word(text, UINT32_C(0xd4200000));  /* brk #0 */

    a64_core_runtime(text, &runtime);
    a64_core_runtime_free(&runtime);
}

static void elf_ident(Bytes *image) {
    byte(image, UINT8_C(0x7f));
    byte(image, UINT8_C('E'));
    byte(image, UINT8_C('L'));
    byte(image, UINT8_C('F'));
    byte(image, 2); /* ELFCLASS64 */
    byte(image, 1); /* ELFDATA2LSB */
    byte(image, 1); /* EV_CURRENT */
    byte(image, 0); /* System V */
    for (unsigned index = 0; index < 8; ++index) byte(image, 0);
}

static void elf_header(Bytes *image, uint16_t machine) {
    elf_ident(image);
    u16_le(image, 2); /* ET_EXEC */
    u16_le(image, machine);
    u32_le(image, 1); /* EV_CURRENT */
    u64_le(image, IMAGE_BASE + TEXT_OFFSET);
    u64_le(image, ELF_HEADER_SIZE);
    u64_le(image, 0); /* section headers */
    u32_le(image, 0); /* flags */
    u16_le(image, ELF_HEADER_SIZE);
    u16_le(image, PROGRAM_HEADER_SIZE);
    u16_le(image, PROGRAM_HEADER_COUNT);
    u16_le(image, 0);
    u16_le(image, 0);
    u16_le(image, 0);
}

static void load_segment(
    Bytes *image,
    uint32_t flags,
    uint64_t offset,
    uint64_t address,
    uint64_t file_size,
    uint64_t memory_size
) {
    u32_le(image, 1); /* PT_LOAD */
    u32_le(image, flags);
    u64_le(image, offset);
    u64_le(image, address);
    u64_le(image, address);
    u64_le(image, file_size);
    u64_le(image, memory_size);
    u64_le(image, PAGE_SIZE);
}

static void elf_image(
    Bytes *image,
    uint16_t machine,
    const Bytes *text
) {
    if (text->length > PAGE_SIZE - TEXT_OFFSET) {
        fatal("native Core text exceeds the bounded static RX page");
    }
    uint64_t rx_size = (uint64_t)TEXT_OFFSET + (uint64_t)text->length;
    elf_header(image, machine);
    load_segment(image, 5, 0, IMAGE_BASE, rx_size, rx_size);
    load_segment(
        image, 6, PAGE_SIZE, DATA_ADDRESS, 3, PAGE_SIZE
    );
    if (image->length != TEXT_OFFSET) {
        fatal("internal ELF header size differs");
    }
    bytes_reserve(image, text->length);
    memcpy(image->data + image->length, text->data, text->length);
    image->length += text->length;
    bytes_pad_to(image, PAGE_SIZE);
    byte(image, 0);
    byte(image, 0);
    byte(image, UINT8_C('\n'));
}

static void bytes_append(Bytes *destination, const Bytes *source) {
    bytes_reserve(destination, source->length);
    memcpy(
        destination->data + destination->length,
        source->data,
        source->length
    );
    destination->length += source->length;
}

static void bytes_text(Bytes *bytes, const char *text) {
    size_t length = strlen(text) + 1;
    bytes_reserve(bytes, length);
    memcpy(bytes->data + bytes->length, text, length);
    bytes->length += length;
}

static void bytes_align(Bytes *bytes, size_t alignment) {
    if (alignment == 0) fatal("zero byte alignment");
    size_t remainder = bytes->length % alignment;
    if (remainder != 0) {
        bytes_pad_to(bytes, bytes->length + alignment - remainder);
    }
}

static void patch_u16_le(Bytes *bytes, size_t offset, uint16_t value) {
    if (offset > bytes->length || bytes->length - offset < 2) {
        fatal("ELF u16 patch is outside the image");
    }
    bytes->data[offset] = (uint8_t)value;
    bytes->data[offset + 1] = (uint8_t)(value >> 8);
}

static void patch_u64_le(Bytes *bytes, size_t offset, uint64_t value) {
    if (offset > bytes->length || bytes->length - offset < 8) {
        fatal("ELF u64 patch is outside the image");
    }
    for (unsigned index = 0; index < 8; ++index) {
        bytes->data[offset + index] =
            (uint8_t)(value >> (index * 8));
    }
}

static void uleb128(Bytes *bytes, uint64_t value) {
    do {
        uint8_t encoded = (uint8_t)(value & UINT64_C(0x7f));
        value >>= 7;
        if (value != 0) encoded |= UINT8_C(0x80);
        byte(bytes, encoded);
    } while (value != 0);
}

static void sleb128(Bytes *bytes, int64_t value) {
    bool more = true;
    while (more) {
        uint8_t encoded = (uint8_t)((uint64_t)value & UINT64_C(0x7f));
        bool sign = (encoded & UINT8_C(0x40)) != 0;
        value >>= 7;
        if ((value == 0 && !sign) || (value == -1 && sign)) {
            more = false;
        } else {
            encoded |= UINT8_C(0x80);
        }
        byte(bytes, encoded);
    }
}

static void dwarf_abbreviations(Bytes *abbreviations) {
    /*
     * The canonical encoder.kofun table:
     *   1: compile unit with child DIEs
     *   2: external subprogram with source declaration and address range
     */
    const uint8_t table[] = {
        1, 17, 1,
        37, 14, 19, 5, 3, 14, 16, 23, 17, 1, 18, 7, 0, 0,
        2, 46, 0,
        3, 14, 58, 11, 59, 11, 17, 1, 18, 7, 63, 25, 0, 0,
        0,
    };
    bytes_reserve(abbreviations, sizeof(table));
    memcpy(
        abbreviations->data + abbreviations->length,
        table,
        sizeof(table)
    );
    abbreviations->length += sizeof(table);
}

static void dwarf_strings(
    Bytes *strings,
    const char *source_path,
    uint32_t *source_offset,
    uint32_t *main_offset
) {
    bytes_text(strings, "Kofun bootstrap native encoder");
    if (strings->length > UINT32_MAX) fatal("DWARF string offset overflow");
    *source_offset = (uint32_t)strings->length;
    bytes_text(strings, source_path);
    if (strings->length > UINT32_MAX) fatal("DWARF string offset overflow");
    *main_offset = (uint32_t)strings->length;
    bytes_text(strings, "main");
}

static void dwarf_information(
    Bytes *information,
    uint32_t source_offset,
    uint32_t main_offset,
    size_t main_line,
    size_t text_size
) {
    if (main_line > UINT8_MAX) {
        fatal("native Core debug declaration line exceeds DWARF v4 data1");
    }

    Bytes body;
    bytes_init(&body);
    u16_le(&body, 4); /* DWARF v4 */
    u32_le(&body, 0); /* abbreviation table offset */
    byte(&body, 8);   /* address size */

    uleb128(&body, 1); /* compile-unit abbreviation */
    u32_le(&body, 0);  /* producer string */
    u16_le(&body, UINT16_C(0x8000)); /* implementation-defined Kofun */
    u32_le(&body, source_offset);
    u32_le(&body, 0); /* .debug_line offset */
    u64_le(&body, IMAGE_BASE + TEXT_OFFSET);
    u64_le(&body, (uint64_t)text_size);

    uleb128(&body, 2); /* subprogram abbreviation */
    u32_le(&body, main_offset);
    byte(&body, 1); /* file table index */
    byte(&body, (uint8_t)main_line);
    u64_le(&body, IMAGE_BASE + TEXT_OFFSET);
    u64_le(&body, (uint64_t)text_size);
    byte(&body, 0); /* end compile-unit children */

    if (body.length > UINT32_MAX) fatal("DWARF information is too large");
    u32_le(information, (uint32_t)body.length);
    bytes_append(information, &body);
    free(body.data);
}

static void dwarf_line_table(
    Bytes *lines,
    const char *source_path,
    const LineRows *rows,
    size_t text_size
) {
    if (rows->length == 0) fatal("native Core has no debug line rows");

    Bytes header;
    bytes_init(&header);
    byte(&header, 1); /* minimum instruction length */
    byte(&header, 1); /* maximum operations per instruction */
    byte(&header, 1); /* default_is_stmt */
    byte(&header, UINT8_C(251)); /* line_base = -5 */
    byte(&header, 14); /* line_range */
    byte(&header, 13); /* opcode_base */
    const uint8_t opcode_lengths[] =
        {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
    bytes_reserve(&header, sizeof(opcode_lengths));
    memcpy(
        header.data + header.length,
        opcode_lengths,
        sizeof(opcode_lengths)
    );
    header.length += sizeof(opcode_lengths);
    byte(&header, 0); /* empty directory table */
    bytes_text(&header, source_path);
    uleb128(&header, 0); /* directory index */
    uleb128(&header, 0); /* modification time */
    uleb128(&header, 0); /* file size */
    byte(&header, 0);    /* end file table */

    Bytes program;
    bytes_init(&program);
    byte(&program, 0);
    uleb128(&program, 9);
    byte(&program, 2); /* DW_LNE_set_address */
    u64_le(&program, IMAGE_BASE + TEXT_OFFSET);

    size_t current_offset = 0;
    int64_t current_line = 1;
    for (size_t index = 0; index < rows->length; ++index) {
        size_t offset = rows->offsets[index];
        size_t line = rows->lines[index];
        if (offset < current_offset || offset > text_size) {
            fatal("native Core debug rows are not ordered");
        }
        if (line > (size_t)INT64_MAX) {
            fatal("native Core source has too many lines");
        }
        if (offset != current_offset) {
            byte(&program, 2); /* DW_LNS_advance_pc */
            uleb128(&program, (uint64_t)(offset - current_offset));
        }
        int64_t wanted_line = (int64_t)line;
        if (wanted_line != current_line) {
            byte(&program, 3); /* DW_LNS_advance_line */
            sleb128(&program, wanted_line - current_line);
        }
        byte(&program, 1); /* DW_LNS_copy */
        current_offset = offset;
        current_line = wanted_line;
    }

    if (text_size != current_offset) {
        byte(&program, 2);
        uleb128(&program, (uint64_t)(text_size - current_offset));
    }
    byte(&program, 0);
    uleb128(&program, 1);
    byte(&program, 1); /* DW_LNE_end_sequence */

    Bytes body;
    bytes_init(&body);
    u16_le(&body, 4);
    if (header.length > UINT32_MAX) fatal("DWARF line header is too large");
    u32_le(&body, (uint32_t)header.length);
    bytes_append(&body, &header);
    bytes_append(&body, &program);

    if (body.length > UINT32_MAX) fatal("DWARF line table is too large");
    u32_le(lines, (uint32_t)body.length);
    bytes_append(lines, &body);

    free(body.data);
    free(program.data);
    free(header.data);
}

static void symbol(Bytes *symbols, uint32_t name, uint64_t value, uint64_t size) {
    u32_le(symbols, name);
    byte(symbols, UINT8_C(0x12)); /* STB_GLOBAL | STT_FUNC */
    byte(symbols, 0);
    u16_le(symbols, 1); /* .text */
    u64_le(symbols, value);
    u64_le(symbols, size);
}

static void section_header(
    Bytes *sections,
    uint32_t name,
    uint32_t type,
    uint64_t flags,
    uint64_t address,
    uint64_t offset,
    uint64_t size,
    uint32_t link,
    uint32_t info,
    uint64_t alignment,
    uint64_t entry_size
) {
    u32_le(sections, name);
    u32_le(sections, type);
    u64_le(sections, flags);
    u64_le(sections, address);
    u64_le(sections, offset);
    u64_le(sections, size);
    u32_le(sections, link);
    u32_le(sections, info);
    u64_le(sections, alignment);
    u64_le(sections, entry_size);
}

static void elf_add_debug(
    Bytes *image,
    const Bytes *text,
    const char *source_path,
    size_t main_line,
    const LineRows *rows
) {
    Bytes abbreviations;
    Bytes information;
    Bytes lines;
    Bytes strings;
    Bytes symbols;
    Bytes symbol_strings;
    Bytes section_strings;
    bytes_init(&abbreviations);
    bytes_init(&information);
    bytes_init(&lines);
    bytes_init(&strings);
    bytes_init(&symbols);
    bytes_init(&symbol_strings);
    bytes_init(&section_strings);

    dwarf_abbreviations(&abbreviations);
    uint32_t source_offset = 0;
    uint32_t main_offset = 0;
    dwarf_strings(
        &strings,
        source_path,
        &source_offset,
        &main_offset
    );
    dwarf_information(
        &information,
        source_offset,
        main_offset,
        main_line,
        text->length
    );
    dwarf_line_table(&lines, source_path, rows, text->length);

    bytes_pad_to(&symbols, 24); /* mandatory null symbol */
    symbol(
        &symbols,
        1,
        IMAGE_BASE + TEXT_OFFSET,
        (uint64_t)text->length
    );
    byte(&symbol_strings, 0);
    bytes_text(&symbol_strings, "main");

    byte(&section_strings, 0);
    bytes_text(&section_strings, ".text");
    bytes_text(&section_strings, ".data");
    bytes_text(&section_strings, ".debug_abbrev");
    bytes_text(&section_strings, ".debug_info");
    bytes_text(&section_strings, ".debug_line");
    bytes_text(&section_strings, ".debug_str");
    bytes_text(&section_strings, ".symtab");
    bytes_text(&section_strings, ".strtab");
    bytes_text(&section_strings, ".shstrtab");

    size_t abbreviations_offset = image->length;
    bytes_append(image, &abbreviations);
    size_t information_offset = image->length;
    bytes_append(image, &information);
    size_t lines_offset = image->length;
    bytes_append(image, &lines);
    size_t strings_offset = image->length;
    bytes_append(image, &strings);
    bytes_align(image, 8);
    size_t symbols_offset = image->length;
    bytes_append(image, &symbols);
    size_t symbol_strings_offset = image->length;
    bytes_append(image, &symbol_strings);
    size_t section_strings_offset = image->length;
    bytes_append(image, &section_strings);
    bytes_align(image, 8);
    size_t section_headers_offset = image->length;

    Bytes sections;
    bytes_init(&sections);
    bytes_pad_to(&sections, 64); /* SHT_NULL */
    section_header(
        &sections,
        1, 1, 6,
        IMAGE_BASE + TEXT_OFFSET,
        TEXT_OFFSET,
        text->length,
        0, 0, 16, 0
    );
    section_header(
        &sections,
        7, 1, 3,
        DATA_ADDRESS,
        PAGE_SIZE,
        3,
        0, 0, 1, 0
    );
    section_header(
        &sections,
        13, 1, 0, 0,
        abbreviations_offset, abbreviations.length,
        0, 0, 1, 0
    );
    section_header(
        &sections,
        27, 1, 0, 0,
        information_offset, information.length,
        0, 0, 1, 0
    );
    section_header(
        &sections,
        39, 1, 0, 0,
        lines_offset, lines.length,
        0, 0, 1, 0
    );
    section_header(
        &sections,
        51, 1, 48, 0,
        strings_offset, strings.length,
        0, 0, 1, 1
    );
    section_header(
        &sections,
        62, 2, 0, 0,
        symbols_offset, symbols.length,
        8, 1, 8, 24
    );
    section_header(
        &sections,
        70, 3, 0, 0,
        symbol_strings_offset, symbol_strings.length,
        0, 0, 1, 0
    );
    section_header(
        &sections,
        78, 3, 0, 0,
        section_strings_offset, section_strings.length,
        0, 0, 1, 0
    );
    bytes_append(image, &sections);

    patch_u64_le(image, 40, (uint64_t)section_headers_offset);
    patch_u16_le(image, 58, 64);
    patch_u16_le(image, 60, 10);
    patch_u16_le(image, 62, 9);

    free(sections.data);
    free(section_strings.data);
    free(symbol_strings.data);
    free(symbols.data);
    free(strings.data);
    free(lines.data);
    free(information.data);
    free(abbreviations.data);
}

static bool write_image(const char *path, const Bytes *image) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "kofun native: cannot write %s: %s\n",
                path, strerror(errno));
        return false;
    }
    bool ok =
        fwrite(image->data, 1, image->length, file) == image->length;
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        fprintf(stderr, "kofun native: cannot complete %s\n", path);
    }
    return ok;
}

static void usage(void) {
    fputs(
        "usage: kofun-native-core INPUT.kofun "
        "(x86_64-linux|aarch64-linux) [-g] OUTPUT\n",
        stderr
    );
}

int main(int argc, char **argv) {
    if (argc != 4 && argc != 5) {
        usage();
        return 2;
    }
    bool debug = argc == 5 && strcmp(argv[3], "-g") == 0;
    if (argc == 5 && !debug) {
        usage();
        return 2;
    }
    const char *output_path = debug ? argv[4] : argv[3];

    uint16_t machine = 0;
    bool aarch64 = false;
    if (strcmp(argv[2], "x86_64-linux") == 0) {
        machine = 62; /* EM_X86_64 */
    } else if (strcmp(argv[2], "aarch64-linux") == 0) {
        machine = 183; /* EM_AARCH64 */
        aarch64 = true;
    } else {
        fprintf(stderr, "kofun native: unsupported target: %s\n", argv[2]);
        return 2;
    }
    if (debug && aarch64) {
        fputs(
            "kofun native: -g currently requires x86_64-linux\n",
            stderr
        );
        return 2;
    }

    char *source = read_source(argv[1]);
    if (source == NULL) return 1;
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
        fprintf(stderr, "kofun native: %s\n", message);
        free(source);
        return 1;
    }

    FunctionProgram function_program;
    char function_error_text[256] = {0};
    size_t function_error_at = 0;
    bool function_headers_ok = function_headers(
        source,
        &function_program,
        function_error_text,
        &function_error_at
    );
    bool function_bodies_ok = false;
    bool tried_function_bodies =
        function_headers_ok &&
        (function_program.function_count > 1 || !aarch64);
    if (tried_function_bodies) {
        function_bodies_ok = function_bodies(
            &function_program,
            function_error_text,
            &function_error_at
        );
    }
    bool use_function_core =
        function_bodies_ok &&
        (function_program.function_count > 1 ||
         function_program.extended_numeric);
    if (function_headers_ok &&
        function_program.function_count > 1 &&
        !function_bodies_ok) {
        fprintf(
            stderr,
            "kofun native: unsupported function Core at byte %zu: %s\n",
            function_error_at,
            function_error_text
        );
        function_program_free(&function_program);
        free(source);
        return 1;
    }
    if (use_function_core) {
        if (debug) {
            fputs(
                "kofun native: -g for function Core is not "
                "implemented yet\n",
                stderr
            );
            function_program_free(&function_program);
            free(source);
            return 1;
        }
        if (aarch64 &&
            function_program.requires_extended_numeric_lowering) {
            fputs(
                "kofun native: AArch64 extended numeric function Core "
                "is not implemented yet\n",
                stderr
            );
            function_program_free(&function_program);
            free(source);
            return 1;
        }
        function_program.canonical_runtime_errors =
            function_program.function_count == 1;

        Bytes text;
        bytes_init(&text);
        if (aarch64) {
            a64_function_program(&text, &function_program);
        } else {
            x64_function_program(&text, &function_program);
        }
        Bytes image;
        bytes_init(&image);
        elf_image(&image, machine, &text);
        bool ok = write_image(output_path, &image);
        free(image.data);
        free(text.data);
        function_program_free(&function_program);
        free(source);
        return ok ? 0 : 1;
    }
    function_program_free(&function_program);

    Parser parser = {
        .source = source,
        .cursor = 0,
        .error = NULL,
    };
    Node *expression = parse_program(&parser);
    if (parser.error != NULL || expression == NULL) {
        fprintf(
            stderr,
            "kofun native: unsupported Core at byte %zu: %s\n",
            parser.cursor,
            parser.error == NULL ? "invalid expression" : parser.error
        );
        free_node(expression);
        free(source);
        return 1;
    }
    bool aarch64_aggregate_core =
        aarch64 &&
        (
            uses_list(expression) ||
            uses_text(expression) ||
            uses_local_bindings(expression)
        );
    if (aarch64 && !aarch64_aggregate_core &&
        register_depth(expression) > 16) {
        fprintf(stderr, "kofun native: AArch64 Core needs over 16 registers\n");
        free_node(expression);
        free(source);
        return 1;
    }

    Bytes text;
    bytes_init(&text);
    LineRows rows;
    line_rows_init(&rows);
    if (aarch64) {
        if (aarch64_aggregate_core) {
            a64_core_text(
                &text,
                expression,
                parser.local_count + parser.max_lambda_parameters
            );
        } else {
            a64_text(&text, expression);
        }
    } else {
        x64_text(
            &text,
            expression,
            &rows,
            parser.print_line,
            parser.local_count + parser.max_lambda_parameters
        );
    }

    Bytes image;
    bytes_init(&image);
    elf_image(&image, machine, &text);
    if (debug) {
        elf_add_debug(
            &image,
            &text,
            argv[1],
            parser.main_line,
            &rows
        );
    }
    bool ok = write_image(output_path, &image);

    free(image.data);
    line_rows_free(&rows);
    free(text.data);
    free_node(expression);
    free(source);
    return ok ? 0 : 1;
}
