/*
 * Audited canonical C11 compiler for the Kofun C ABI profile.
 *
 * This intentionally small compiler parses declarations and expressions
 * instead of copying user input into C.  Identifiers, ABI types, arities, and
 * expression types are checked before C11 is emitted. Rewriting this
 * compiler in bootstrap Kofun remains a separate milestone.
 */
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SOURCE (1024U * 1024U)
#define MAX_TEXT 128U
#define MAX_STRUCTS 16U
#define MAX_FIELDS 16U
#define MAX_FUNCTIONS 64U
#define MAX_PARAMS 16U
#define MAX_VARIABLES 64U
#define MAX_ARGS 16U

typedef enum {
    TOK_EOF,
    TOK_ID,
    TOK_INT,
    TOK_STRING,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COLON,
    TOK_COMMA,
    TOK_EQUAL,
    TOK_DOT,
    TOK_ARROW
} TokenKind;

typedef struct {
    TokenKind kind;
    char text[MAX_TEXT];
    size_t line;
    size_t column;
} Token;

typedef struct {
    char name[MAX_TEXT];
} Type;

typedef struct {
    char name[MAX_TEXT];
    Type type;
    size_t offset;
} Field;

typedef struct {
    char name[MAX_TEXT];
    Field fields[MAX_FIELDS];
    size_t field_count;
    size_t size;
    size_t alignment;
} Struct;

typedef struct {
    char name[MAX_TEXT];
    char parameter_names[MAX_PARAMS][MAX_TEXT];
    Type parameter_types[MAX_PARAMS];
    size_t parameter_count;
    Type result;
} Function;

typedef struct {
    char name[MAX_TEXT];
    Type type;
} Variable;

typedef struct {
    char code[2048];
    Type type;
    bool integer_literal;
    int64_t integer_value;
} Expression;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

static const char *source;
static size_t source_index;
static size_t source_line;
static size_t source_column;
static Token token;
static bool failed;

static Struct structs[MAX_STRUCTS];
static size_t struct_count;
static Function functions[MAX_FUNCTIONS];
static size_t function_count;
static Variable variables[MAX_VARIABLES];
static size_t variable_count;

static Buffer declarations;
static Buffer body;

static void error_at(size_t line, size_t column, const char *format, ...) {
    va_list arguments;
    if (failed) {
        return;
    }
    failed = true;
    fprintf(stderr, "error[CABI001] at %zu:%zu: ", line, column);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}

static void error_here(const char *format, ...) {
    va_list arguments;
    if (failed) {
        return;
    }
    failed = true;
    fprintf(stderr, "error[CABI001] at %zu:%zu: ", token.line, token.column);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}

static void buffer_reserve(Buffer *buffer, size_t extra) {
    size_t wanted;
    size_t capacity;
    char *grown;
    if (failed) {
        return;
    }
    if (extra > SIZE_MAX - buffer->length - 1U) {
        error_here("generated output is too large");
        return;
    }
    wanted = buffer->length + extra + 1U;
    if (wanted <= buffer->capacity) {
        return;
    }
    capacity = buffer->capacity == 0U ? 4096U : buffer->capacity;
    while (capacity < wanted) {
        if (capacity > SIZE_MAX / 2U) {
            error_here("generated output is too large");
            return;
        }
        capacity *= 2U;
    }
    grown = realloc(buffer->data, capacity);
    if (grown == NULL) {
        error_here("out of memory");
        return;
    }
    buffer->data = grown;
    buffer->capacity = capacity;
}

static void append(Buffer *buffer, const char *text) {
    size_t length = strlen(text);
    buffer_reserve(buffer, length);
    if (failed) {
        return;
    }
    memcpy(buffer->data + buffer->length, text, length + 1U);
    buffer->length += length;
}

static void appendf(Buffer *buffer, const char *format, ...) {
    va_list arguments;
    va_list copied;
    int length;
    if (failed) {
        return;
    }
    va_start(arguments, format);
    va_copy(copied, arguments);
    length = vsnprintf(NULL, 0U, format, copied);
    va_end(copied);
    if (length < 0) {
        va_end(arguments);
        error_here("could not format generated output");
        return;
    }
    buffer_reserve(buffer, (size_t)length);
    if (!failed) {
        (void)vsnprintf(
            buffer->data + buffer->length,
            buffer->capacity - buffer->length,
            format,
            arguments
        );
        buffer->length += (size_t)length;
    }
    va_end(arguments);
}

static char current_char(void) {
    return source[source_index];
}

static char advance_char(void) {
    char value = source[source_index];
    if (value == '\0') {
        return value;
    }
    ++source_index;
    if (value == '\n') {
        ++source_line;
        source_column = 1U;
    } else {
        ++source_column;
    }
    return value;
}

static void token_text(const char *start, size_t length) {
    if (length >= sizeof(token.text)) {
        error_at(token.line, token.column, "token is too long");
        token.text[0] = '\0';
        return;
    }
    memcpy(token.text, start, length);
    token.text[length] = '\0';
}

static void next_token(void) {
    const char *start;
    size_t length;
    char quote;
    token.text[0] = '\0';
    for (;;) {
        while (isspace((unsigned char)current_char())) {
            (void)advance_char();
        }
        if (current_char() != '#') {
            break;
        }
        while (current_char() != '\0' && current_char() != '\n') {
            (void)advance_char();
        }
    }

    token.line = source_line;
    token.column = source_column;
    start = source + source_index;

    if (current_char() == '\0') {
        token.kind = TOK_EOF;
        return;
    }
    if (isalpha((unsigned char)current_char()) || current_char() == '_') {
        (void)advance_char();
        while (
            isalnum((unsigned char)current_char()) || current_char() == '_'
        ) {
            (void)advance_char();
        }
        token.kind = TOK_ID;
        token_text(start, (size_t)(source + source_index - start));
        return;
    }
    if (isdigit((unsigned char)current_char()) ||
        (current_char() == '-' &&
         isdigit((unsigned char)source[source_index + 1U]))) {
        if (current_char() == '-') {
            (void)advance_char();
        }
        while (isdigit((unsigned char)current_char())) {
            (void)advance_char();
        }
        token.kind = TOK_INT;
        token_text(start, (size_t)(source + source_index - start));
        return;
    }
    if (current_char() == '"' || current_char() == '\'') {
        quote = advance_char();
        while (current_char() != '\0' && current_char() != quote) {
            if (current_char() == '\n') {
                error_at(token.line, token.column, "newline in string literal");
                return;
            }
            if (current_char() == '\\') {
                (void)advance_char();
                if (current_char() == '\0' || current_char() == '\n') {
                    error_at(
                        token.line,
                        token.column,
                        "unterminated string escape"
                    );
                    return;
                }
            }
            (void)advance_char();
        }
        if (current_char() != quote) {
            error_at(token.line, token.column, "unterminated string literal");
            return;
        }
        (void)advance_char();
        token.kind = TOK_STRING;
        token_text(start, (size_t)(source + source_index - start));
        return;
    }

    if (current_char() == '-' && source[source_index + 1U] == '>') {
        (void)advance_char();
        (void)advance_char();
        token.kind = TOK_ARROW;
        strcpy(token.text, "->");
        return;
    }

    switch (advance_char()) {
        case '(': token.kind = TOK_LPAREN; break;
        case ')': token.kind = TOK_RPAREN; break;
        case '{': token.kind = TOK_LBRACE; break;
        case '}': token.kind = TOK_RBRACE; break;
        case ':': token.kind = TOK_COLON; break;
        case ',': token.kind = TOK_COMMA; break;
        case '=': token.kind = TOK_EQUAL; break;
        case '.': token.kind = TOK_DOT; break;
        default:
            length = (size_t)(source + source_index - start);
            token_text(start, length);
            error_at(
                token.line,
                token.column,
                "unexpected character `%s`",
                token.text
            );
            return;
    }
    token_text(start, 1U);
}

static bool token_is(const char *text) {
    return token.kind == TOK_ID && strcmp(token.text, text) == 0;
}

static bool accept(TokenKind kind) {
    if (token.kind != kind) {
        return false;
    }
    next_token();
    return true;
}

static void expect(TokenKind kind, const char *description) {
    if (failed) {
        return;
    }
    if (token.kind != kind) {
        error_here("expected %s, found `%s`", description, token.text);
        return;
    }
    next_token();
}

static void expect_word(const char *word) {
    if (failed) {
        return;
    }
    if (!token_is(word)) {
        error_here("expected `%s`, found `%s`", word, token.text);
        return;
    }
    next_token();
}

static void copy_identifier(char output[MAX_TEXT], const char *role) {
    if (failed) {
        return;
    }
    if (token.kind != TOK_ID) {
        error_here("expected %s identifier, found `%s`", role, token.text);
        return;
    }
    strcpy(output, token.text);
    next_token();
}

static Struct *find_struct(const char *name) {
    size_t index;
    for (index = 0U; index < struct_count; ++index) {
        if (strcmp(structs[index].name, name) == 0) {
            return &structs[index];
        }
    }
    return NULL;
}

static Function *find_function(const char *name) {
    size_t index;
    for (index = 0U; index < function_count; ++index) {
        if (strcmp(functions[index].name, name) == 0) {
            return &functions[index];
        }
    }
    return NULL;
}

static Variable *find_variable(const char *name) {
    size_t index;
    for (index = variable_count; index > 0U; --index) {
        if (strcmp(variables[index - 1U].name, name) == 0) {
            return &variables[index - 1U];
        }
    }
    return NULL;
}

static bool primitive_type(const char *name) {
    static const char *names[] = {
        "Unit", "Bool", "I8", "I16", "I32", "I64",
        "U8", "U16", "U32", "U64", "F32", "F64",
        "CInt", "CUInt", "CLong", "CULong", "CSize",
        "CStr", "CBytes"
    };
    size_t index;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (strcmp(name, names[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool known_type(const Type *type) {
    return primitive_type(type->name) || find_struct(type->name) != NULL;
}

static Type parse_type(void) {
    Type result;
    result.name[0] = '\0';
    copy_identifier(result.name, "type");
    if (!failed && !known_type(&result)) {
        error_here("unknown C ABI type `%s`", result.name);
    }
    return result;
}

static const char *c_type(const Type *type) {
    if (strcmp(type->name, "Unit") == 0) return "void";
    if (strcmp(type->name, "Bool") == 0) return "_Bool";
    if (strcmp(type->name, "I8") == 0) return "int8_t";
    if (strcmp(type->name, "I16") == 0) return "int16_t";
    if (strcmp(type->name, "I32") == 0) return "int32_t";
    if (strcmp(type->name, "I64") == 0) return "int64_t";
    if (strcmp(type->name, "U8") == 0) return "uint8_t";
    if (strcmp(type->name, "U16") == 0) return "uint16_t";
    if (strcmp(type->name, "U32") == 0) return "uint32_t";
    if (strcmp(type->name, "U64") == 0) return "uint64_t";
    if (strcmp(type->name, "F32") == 0) return "float";
    if (strcmp(type->name, "F64") == 0) return "double";
    if (strcmp(type->name, "CInt") == 0) return "int";
    if (strcmp(type->name, "CUInt") == 0) return "unsigned int";
    if (strcmp(type->name, "CLong") == 0) return "long";
    if (strcmp(type->name, "CULong") == 0) return "unsigned long";
    if (strcmp(type->name, "CSize") == 0) return "size_t";
    if (strcmp(type->name, "CStr") == 0) return "const char *";
    if (strcmp(type->name, "CBytes") == 0) return "const void *";
    return type->name;
}

static void type_size_alignment(
    const Type *type,
    size_t *size,
    size_t *alignment
) {
    Struct *record;
    if (strcmp(type->name, "Unit") == 0) {
        *size = 0U; *alignment = 1U;
    } else if (
        strcmp(type->name, "Bool") == 0 ||
        strcmp(type->name, "I8") == 0 ||
        strcmp(type->name, "U8") == 0
    ) {
        *size = 1U; *alignment = 1U;
    } else if (
        strcmp(type->name, "I16") == 0 ||
        strcmp(type->name, "U16") == 0
    ) {
        *size = 2U; *alignment = 2U;
    } else if (
        strcmp(type->name, "I32") == 0 ||
        strcmp(type->name, "U32") == 0 ||
        strcmp(type->name, "F32") == 0 ||
        strcmp(type->name, "CInt") == 0 ||
        strcmp(type->name, "CUInt") == 0
    ) {
        *size = 4U; *alignment = 4U;
    } else if (
        strcmp(type->name, "I64") == 0 ||
        strcmp(type->name, "U64") == 0 ||
        strcmp(type->name, "F64") == 0 ||
        strcmp(type->name, "CLong") == 0 ||
        strcmp(type->name, "CULong") == 0 ||
        strcmp(type->name, "CSize") == 0 ||
        strcmp(type->name, "CStr") == 0 ||
        strcmp(type->name, "CBytes") == 0
    ) {
        *size = 8U; *alignment = 8U;
    } else {
        record = find_struct(type->name);
        if (record == NULL) {
            *size = 0U; *alignment = 1U;
        } else {
            *size = record->size;
            *alignment = record->alignment;
        }
    }
}

static bool same_type(const Type *left, const Type *right) {
    return strcmp(left->name, right->name) == 0;
}

static bool integer_type(const Type *type) {
    return strcmp(type->name, "Bool") == 0 ||
           strcmp(type->name, "I8") == 0 ||
           strcmp(type->name, "I16") == 0 ||
           strcmp(type->name, "I32") == 0 ||
           strcmp(type->name, "I64") == 0 ||
           strcmp(type->name, "U8") == 0 ||
           strcmp(type->name, "U16") == 0 ||
           strcmp(type->name, "U32") == 0 ||
           strcmp(type->name, "U64") == 0 ||
           strcmp(type->name, "CInt") == 0 ||
           strcmp(type->name, "CUInt") == 0 ||
           strcmp(type->name, "CLong") == 0 ||
           strcmp(type->name, "CULong") == 0 ||
           strcmp(type->name, "CSize") == 0;
}

static bool compatible(const Expression *expression, const Type *wanted) {
    if (same_type(&expression->type, wanted)) {
        return true;
    }
    if (
        strcmp(expression->type.name, "CStr") == 0 &&
        strcmp(wanted->name, "CBytes") == 0
    ) {
        return true;
    }
    if (!expression->integer_literal || !integer_type(wanted)) {
        return false;
    }
    if (strcmp(wanted->name, "Bool") == 0) {
        return expression->integer_value == 0 ||
               expression->integer_value == 1;
    }
    if (strcmp(wanted->name, "I8") == 0) {
        return expression->integer_value >= INT8_MIN &&
               expression->integer_value <= INT8_MAX;
    }
    if (strcmp(wanted->name, "I16") == 0) {
        return expression->integer_value >= INT16_MIN &&
               expression->integer_value <= INT16_MAX;
    }
    if (
        strcmp(wanted->name, "I32") == 0 ||
        strcmp(wanted->name, "CInt") == 0
    ) {
        return expression->integer_value >= INT32_MIN &&
               expression->integer_value <= INT32_MAX;
    }
    if (strcmp(wanted->name, "U8") == 0) {
        return expression->integer_value >= 0 &&
               expression->integer_value <= UINT8_MAX;
    }
    if (strcmp(wanted->name, "U16") == 0) {
        return expression->integer_value >= 0 &&
               expression->integer_value <= UINT16_MAX;
    }
    if (
        strcmp(wanted->name, "U32") == 0 ||
        strcmp(wanted->name, "CUInt") == 0
    ) {
        return expression->integer_value >= 0 &&
               (uint64_t)expression->integer_value <= UINT32_MAX;
    }
    if (
        strcmp(wanted->name, "U64") == 0 ||
        strcmp(wanted->name, "CULong") == 0 ||
        strcmp(wanted->name, "CSize") == 0
    ) {
        return expression->integer_value >= 0;
    }
    return true;
}

static void parse_struct(void) {
    Struct *record;
    char record_name[MAX_TEXT];
    size_t size = 0U;
    size_t maximum_alignment = 1U;
    size_t field_size;
    size_t field_alignment;

    expect_word("repr");
    expect(TOK_LPAREN, "`(`");
    expect_word("C");
    expect(TOK_RPAREN, "`)`");
    expect_word("struct");
    if (failed) return;
    if (struct_count >= MAX_STRUCTS) {
        error_here("too many repr(C) structs");
        return;
    }
    copy_identifier(record_name, "struct");
    if (
        primitive_type(record_name) ||
        find_struct(record_name) != NULL ||
        find_function(record_name) != NULL
    ) {
        error_here("duplicate or reserved declaration `%s`", record_name);
        return;
    }
    record = &structs[struct_count++];
    memset(record, 0, sizeof(*record));
    strcpy(record->name, record_name);
    expect(TOK_LBRACE, "`{`");
    appendf(&declarations, "typedef struct %s {\n", record->name);
    while (!failed && token.kind != TOK_RBRACE) {
        Field *field;
        size_t prior;
        if (record->field_count >= MAX_FIELDS) {
            error_here("too many fields in `%s`", record->name);
            return;
        }
        field = &record->fields[record->field_count++];
        copy_identifier(field->name, "field");
        for (prior = 0U; prior + 1U < record->field_count; ++prior) {
            if (strcmp(record->fields[prior].name, field->name) == 0) {
                error_here("duplicate field `%s.%s`", record->name, field->name);
                return;
            }
        }
        expect(TOK_COLON, "`:`");
        field->type = parse_type();
        if (strcmp(field->type.name, record->name) == 0) {
            error_here(
                "repr(C) struct `%s` cannot contain itself by value",
                record->name
            );
            return;
        }
        if (strcmp(field->type.name, "Unit") == 0) {
            error_here("repr(C) field `%s` cannot have type Unit", field->name);
            return;
        }
        type_size_alignment(&field->type, &field_size, &field_alignment);
        size = (size + field_alignment - 1U) /
               field_alignment * field_alignment;
        field->offset = size;
        size += field_size;
        if (field_alignment > maximum_alignment) {
            maximum_alignment = field_alignment;
        }
        appendf(
            &declarations,
            "    %s %s;\n",
            c_type(&field->type),
            field->name
        );
        (void)accept(TOK_COMMA);
    }
    expect(TOK_RBRACE, "`}`");
    if (record->field_count == 0U) {
        error_here("repr(C) struct `%s` must not be empty", record->name);
        return;
    }
    size = (size + maximum_alignment - 1U) /
           maximum_alignment * maximum_alignment;
    record->size = size;
    record->alignment = maximum_alignment;
    appendf(&declarations, "} %s;\n", record->name);
    appendf(
        &declarations,
        "_Static_assert(sizeof(%s) == %zu, \"C ABI size: %s\");\n",
        record->name,
        record->size,
        record->name
    );
    appendf(
        &declarations,
        "_Static_assert(_Alignof(%s) == %zu, \"C ABI alignment: %s\");\n",
        record->name,
        record->alignment,
        record->name
    );
    for (field_size = 0U; field_size < record->field_count; ++field_size) {
        appendf(
            &declarations,
            "_Static_assert(offsetof(%s, %s) == %zu, "
            "\"C ABI offset: %s.%s\");\n",
            record->name,
            record->fields[field_size].name,
            record->fields[field_size].offset,
            record->name,
            record->fields[field_size].name
        );
    }
    append(&declarations, "\n");
}

static void parse_extern(void) {
    Function *function;
    size_t prior;
    expect_word("extern");
    if (
        token.kind != TOK_STRING ||
        (strcmp(token.text, "\"C\"") != 0 &&
         strcmp(token.text, "'C'") != 0)
    ) {
        error_here("only `extern \"C\"` is supported");
        return;
    }
    next_token();
    expect_word("fn");
    if (failed) return;
    if (function_count >= MAX_FUNCTIONS) {
        error_here("too many extern functions");
        return;
    }
    function = &functions[function_count++];
    memset(function, 0, sizeof(*function));
    copy_identifier(function->name, "function");
    if (find_struct(function->name) != NULL) {
        error_here("duplicate declaration `%s`", function->name);
        return;
    }
    for (prior = 0U; prior + 1U < function_count; ++prior) {
        if (strcmp(functions[prior].name, function->name) == 0) {
            error_here("duplicate extern function `%s`", function->name);
            return;
        }
    }
    expect(TOK_LPAREN, "`(`");
    while (!failed && token.kind != TOK_RPAREN) {
        size_t parameter;
        if (function->parameter_count >= MAX_PARAMS) {
            error_here("too many parameters for `%s`", function->name);
            return;
        }
        parameter = function->parameter_count++;
        copy_identifier(
            function->parameter_names[parameter],
            "parameter"
        );
        expect(TOK_COLON, "`:`");
        function->parameter_types[parameter] = parse_type();
        if (
            strcmp(function->parameter_types[parameter].name, "Unit") == 0
        ) {
            error_here("parameter cannot have type Unit");
            return;
        }
        if (!accept(TOK_COMMA)) {
            break;
        }
    }
    expect(TOK_RPAREN, "`)`");
    expect(TOK_ARROW, "`->`");
    function->result = parse_type();
    appendf(
        &declarations,
        "extern %s %s(",
        c_type(&function->result),
        function->name
    );
    if (function->parameter_count == 0U) {
        append(&declarations, "void");
    }
    for (prior = 0U; prior < function->parameter_count; ++prior) {
        if (prior > 0U) append(&declarations, ", ");
        appendf(
            &declarations,
            "%s %s",
            c_type(&function->parameter_types[prior]),
            function->parameter_names[prior]
        );
    }
    append(&declarations, ");\n");
}

static Expression parse_expression(void);

static Expression parse_call(const char *name) {
    Expression result;
    Expression arguments[MAX_ARGS];
    size_t argument_count = 0U;
    size_t index;
    Struct *record = find_struct(name);
    Function *function = find_function(name);
    memset(&result, 0, sizeof(result));
    expect(TOK_LPAREN, "`(`");
    while (!failed && token.kind != TOK_RPAREN) {
        if (argument_count >= MAX_ARGS) {
            error_here("too many call arguments");
            return result;
        }
        arguments[argument_count++] = parse_expression();
        if (!accept(TOK_COMMA)) break;
    }
    expect(TOK_RPAREN, "`)`");
    if (record == NULL && function == NULL) {
        error_here("unknown foreign function or struct constructor `%s`", name);
        return result;
    }
    if (record != NULL) {
        if (argument_count != record->field_count) {
            error_here(
                "`%s` expects %zu fields, got %zu",
                name,
                record->field_count,
                argument_count
            );
            return result;
        }
        strcpy(result.type.name, record->name);
        (void)snprintf(result.code, sizeof(result.code), "(%s){", record->name);
        for (index = 0U; index < argument_count; ++index) {
            if (!compatible(&arguments[index], &record->fields[index].type)) {
                error_here(
                    "field `%s.%s` expects %s, got %s",
                    record->name,
                    record->fields[index].name,
                    record->fields[index].type.name,
                    arguments[index].type.name
                );
                return result;
            }
            if (index > 0U) {
                strncat(
                    result.code,
                    ", ",
                    sizeof(result.code) - strlen(result.code) - 1U
                );
            }
            strncat(
                result.code,
                arguments[index].code,
                sizeof(result.code) - strlen(result.code) - 1U
            );
        }
        strncat(
            result.code,
            "}",
            sizeof(result.code) - strlen(result.code) - 1U
        );
        return result;
    }

    if (argument_count != function->parameter_count) {
        error_here(
            "`%s` expects %zu arguments, got %zu",
            name,
            function->parameter_count,
            argument_count
        );
        return result;
    }
    result.type = function->result;
    (void)snprintf(result.code, sizeof(result.code), "%s(", function->name);
    for (index = 0U; index < argument_count; ++index) {
        if (!compatible(&arguments[index], &function->parameter_types[index])) {
            error_here(
                "argument %zu of `%s` expects %s, got %s",
                index + 1U,
                name,
                function->parameter_types[index].name,
                arguments[index].type.name
            );
            return result;
        }
        if (index > 0U) {
            strncat(
                result.code,
                ", ",
                sizeof(result.code) - strlen(result.code) - 1U
            );
        }
        strncat(
            result.code,
            arguments[index].code,
            sizeof(result.code) - strlen(result.code) - 1U
        );
    }
    strncat(
        result.code,
        ")",
        sizeof(result.code) - strlen(result.code) - 1U
    );
    return result;
}

static Expression parse_expression(void) {
    Expression result;
    char name[MAX_TEXT];
    Variable *variable;
    Struct *record;
    size_t field;
    char *end;
    long long value;
    memset(&result, 0, sizeof(result));

    if (token.kind == TOK_STRING) {
        if (token.text[0] != '"') {
            error_here("CStr expressions require double-quoted strings");
            return result;
        }
        strcpy(result.code, token.text);
        strcpy(result.type.name, "CStr");
        next_token();
        return result;
    }
    if (token.kind == TOK_INT) {
        errno = 0;
        value = strtoll(token.text, &end, 10);
        if (errno == ERANGE || *end != '\0') {
            error_here("integer literal is outside I64");
            return result;
        }
        strcpy(result.code, token.text);
        strcpy(result.type.name, "I64");
        result.integer_literal = true;
        result.integer_value = (int64_t)value;
        next_token();
        return result;
    }
    if (token.kind != TOK_ID) {
        error_here("expected C ABI expression, found `%s`", token.text);
        return result;
    }
    strcpy(name, token.text);
    next_token();
    if (token.kind == TOK_LPAREN) {
        return parse_call(name);
    }
    variable = find_variable(name);
    if (variable == NULL) {
        error_here("unknown local `%s`", name);
        return result;
    }
    strcpy(result.code, name);
    result.type = variable->type;
    if (!accept(TOK_DOT)) {
        return result;
    }
    record = find_struct(variable->type.name);
    if (record == NULL) {
        error_here("field access requires a repr(C) struct, got %s", result.type.name);
        return result;
    }
    if (token.kind != TOK_ID) {
        error_here("expected field name");
        return result;
    }
    for (field = 0U; field < record->field_count; ++field) {
        if (strcmp(record->fields[field].name, token.text) == 0) {
            strncat(
                result.code,
                ".",
                sizeof(result.code) - strlen(result.code) - 1U
            );
            strncat(
                result.code,
                token.text,
                sizeof(result.code) - strlen(result.code) - 1U
            );
            result.type = record->fields[field].type;
            next_token();
            return result;
        }
    }
    error_here("unknown field `%s.%s`", record->name, token.text);
    return result;
}

static const char *print_format(const Type *type) {
    if (strcmp(type->name, "Bool") == 0) return "%d\\n";
    if (strcmp(type->name, "I8") == 0) return "%" PRId8 "\\n";
    if (strcmp(type->name, "I16") == 0) return "%" PRId16 "\\n";
    if (strcmp(type->name, "I32") == 0) return "%" PRId32 "\\n";
    if (strcmp(type->name, "I64") == 0) return "%" PRId64 "\\n";
    if (strcmp(type->name, "U8") == 0) return "%" PRIu8 "\\n";
    if (strcmp(type->name, "U16") == 0) return "%" PRIu16 "\\n";
    if (strcmp(type->name, "U32") == 0) return "%" PRIu32 "\\n";
    if (strcmp(type->name, "U64") == 0) return "%" PRIu64 "\\n";
    if (strcmp(type->name, "CInt") == 0) return "%d\\n";
    if (strcmp(type->name, "CUInt") == 0) return "%u\\n";
    if (strcmp(type->name, "CLong") == 0) return "%ld\\n";
    if (strcmp(type->name, "CULong") == 0) return "%lu\\n";
    if (strcmp(type->name, "CSize") == 0) return "%zu\\n";
    return NULL;
}

static void parse_statement(void) {
    Expression expression;
    const char *format;
    if (token_is("let")) {
        Variable *variable;
        size_t prior;
        next_token();
        if (variable_count >= MAX_VARIABLES) {
            error_here("too many local bindings");
            return;
        }
        variable = &variables[variable_count++];
        memset(variable, 0, sizeof(*variable));
        copy_identifier(variable->name, "local");
        for (prior = 0U; prior + 1U < variable_count; ++prior) {
            if (strcmp(variables[prior].name, variable->name) == 0) {
                error_here("duplicate local `%s`", variable->name);
                return;
            }
        }
        expect(TOK_EQUAL, "`=`");
        expression = parse_expression();
        if (strcmp(expression.type.name, "Unit") == 0) {
            error_here("cannot bind Unit result");
            return;
        }
        variable->type = expression.type;
        appendf(
            &body,
            "    %s %s = %s;\n",
            c_type(&variable->type),
            variable->name,
            expression.code
        );
        return;
    }
    if (token_is("print")) {
        next_token();
        expect(TOK_LPAREN, "`(`");
        expression = parse_expression();
        expect(TOK_RPAREN, "`)`");
        format = print_format(&expression.type);
        if (format == NULL) {
            error_here("print does not support C ABI type `%s`", expression.type.name);
            return;
        }
        appendf(&body, "    printf(\"%s\", %s);\n", format, expression.code);
        return;
    }
    if (token_is("return")) {
        next_token();
        expression = parse_expression();
        if (!integer_type(&expression.type)) {
            error_here("main return must be an integer");
            return;
        }
        appendf(&body, "    return (int)(%s);\n", expression.code);
        return;
    }
    expression = parse_expression();
    if (failed) return;
    if (strcmp(expression.type.name, "Unit") == 0) {
        appendf(&body, "    %s;\n", expression.code);
    } else {
        appendf(&body, "    (void)%s;\n", expression.code);
    }
}

static void parse_main(void) {
    expect_word("fn");
    if (!token_is("main")) {
        error_here("C ABI profile supports only `fn main()` definitions");
        return;
    }
    next_token();
    expect(TOK_LPAREN, "`(`");
    expect(TOK_RPAREN, "`)`");
    expect(TOK_LBRACE, "`{`");
    variable_count = 0U;
    while (!failed && token.kind != TOK_RBRACE && token.kind != TOK_EOF) {
        parse_statement();
    }
    expect(TOK_RBRACE, "`}`");
}

static bool parse_source(void) {
    bool main_seen = false;
    next_token();
    while (!failed && token.kind != TOK_EOF) {
        if (token_is("repr")) {
            if (main_seen) {
                error_here("declarations must appear before `fn main()`");
                break;
            }
            parse_struct();
        } else if (token_is("extern")) {
            if (main_seen) {
                error_here("declarations must appear before `fn main()`");
                break;
            }
            parse_extern();
        } else if (token_is("fn")) {
            if (main_seen) {
                error_here("C ABI profile requires exactly one `fn main()`");
                break;
            }
            main_seen = true;
            parse_main();
        } else {
            error_here(
                "expected `repr(C) struct`, `extern \"C\" fn`, or `fn main()`"
            );
        }
    }
    if (!failed && !main_seen) {
        error_here("C ABI profile requires `fn main()`");
    }
    return !failed;
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    long length;
    char *data;
    size_t read_length;
    if (file == NULL) {
        fprintf(stderr, "error[CABI002]: cannot open input: %s\n", path);
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        fprintf(stderr, "error[CABI002]: cannot seek input: %s\n", path);
        return NULL;
    }
    length = ftell(file);
    if (length < 0L || (unsigned long)length > MAX_SOURCE) {
        fclose(file);
        fprintf(stderr, "error[CABI002]: input exceeds 1 MiB: %s\n", path);
        return NULL;
    }
    rewind(file);
    data = malloc((size_t)length + 1U);
    if (data == NULL) {
        fclose(file);
        fprintf(stderr, "error[CABI002]: out of memory\n");
        return NULL;
    }
    read_length = fread(data, 1U, (size_t)length, file);
    if (read_length != (size_t)length || ferror(file)) {
        free(data);
        fclose(file);
        fprintf(stderr, "error[CABI002]: cannot read input: %s\n", path);
        return NULL;
    }
    data[read_length] = '\0';
    fclose(file);
    return data;
}

static bool write_output(const char *path) {
    FILE *file = fopen(path, "wb");
    static const char header[] =
        "/* Generated by the Kofun C ABI profile compiler. */\n"
        "#include <inttypes.h>\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n"
        "#include <stdio.h>\n\n"
        "_Static_assert(sizeof(int) == 4, \"C ABI profile requires 32-bit int\");\n"
        "_Static_assert(sizeof(long) == 8, \"C ABI profile requires LP64 long\");\n"
        "_Static_assert(sizeof(void *) == 8, \"C ABI profile requires 64-bit pointers\");\n\n";
    if (file == NULL) {
        fprintf(stderr, "error[CABI002]: cannot open output: %s\n", path);
        return false;
    }
    if (
        fwrite(header, 1U, sizeof(header) - 1U, file) != sizeof(header) - 1U ||
        fwrite(declarations.data, 1U, declarations.length, file) !=
            declarations.length ||
        fwrite("\nint main(void) {\n", 1U, 18U, file) != 18U ||
        fwrite(body.data, 1U, body.length, file) != body.length ||
        fwrite("    return 0;\n}\n", 1U, 16U, file) != 16U ||
        fclose(file) != 0
    ) {
        fprintf(stderr, "error[CABI002]: cannot write output: %s\n", path);
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    char *input;
    if (argc != 3) {
        fprintf(stderr, "usage: kofun-c-abi INPUT.kofun OUTPUT.c\n");
        return 2;
    }
    input = read_file(argv[1]);
    if (input == NULL) {
        return 1;
    }
    source = input;
    source_index = 0U;
    source_line = 1U;
    source_column = 1U;
    declarations.data = NULL;
    body.data = NULL;
    append(&declarations, "");
    append(&body, "");
    if (!failed && parse_source() && write_output(argv[2])) {
        free(declarations.data);
        free(body.data);
        free(input);
        return 0;
    }
    free(declarations.data);
    free(body.data);
    free(input);
    return 1;
}
