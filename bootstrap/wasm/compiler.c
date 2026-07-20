#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MAX_SOURCE_BYTES = 1024 * 1024,
    MAX_BINDINGS = 128,
    MAX_NODES = 1024,
    MAX_STATEMENTS = 256
};

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
} Buffer;

typedef enum {
    TOKEN_EOF,
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_COLON,
    TOKEN_EQUAL,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_FLOOR_DIV,
    TOKEN_PERCENT,
    TOKEN_COMMA
} TokenKind;

typedef struct {
    TokenKind kind;
    const char *start;
    size_t length;
    uint64_t magnitude;
    size_t line;
} Token;

typedef enum {
    NODE_LITERAL,
    NODE_VARIABLE,
    NODE_NEGATE,
    NODE_ADD,
    NODE_SUBTRACT,
    NODE_MULTIPLY,
    NODE_DIVIDE,
    NODE_FLOOR_DIVIDE,
    NODE_FLOOR_MODULO
} NodeKind;

typedef struct {
    NodeKind kind;
    int left;
    int right;
    int binding;
    int64_t value;
} Node;

typedef struct {
    char name[64];
    size_t length;
} Binding;

typedef enum {
    STATEMENT_BIND,
    STATEMENT_PRINT
} StatementKind;

typedef struct {
    StatementKind kind;
    int expression;
    int binding;
} Statement;

typedef struct {
    const char *source;
    size_t length;
    size_t cursor;
    size_t line;
    Token token;
    const char *error;
    size_t error_line;
    Node nodes[MAX_NODES];
    size_t node_count;
    Binding bindings[MAX_BINDINGS];
    size_t binding_count;
    Statement statements[MAX_STATEMENTS];
    size_t statement_count;
    size_t print_count;
} Parser;

static void fatal(const char *message) {
    fprintf(stderr, "kofun wasm32: %s\n", message);
    exit(1);
}

static void *allocate(size_t size) {
    void *result = malloc(size == 0 ? 1 : size);
    if (result == NULL) fatal("out of memory");
    return result;
}

static void buffer_reserve(Buffer *buffer, size_t extra) {
    if (extra > SIZE_MAX - buffer->length) fatal("module is too large");
    size_t wanted = buffer->length + extra;
    if (wanted <= buffer->capacity) return;
    size_t capacity = buffer->capacity == 0 ? 256 : buffer->capacity;
    while (capacity < wanted) {
        if (capacity > SIZE_MAX / 2) fatal("module is too large");
        capacity *= 2;
    }
    uint8_t *grown = realloc(buffer->data, capacity);
    if (grown == NULL) fatal("out of memory");
    buffer->data = grown;
    buffer->capacity = capacity;
}

static void byte(Buffer *buffer, uint8_t value) {
    buffer_reserve(buffer, 1);
    buffer->data[buffer->length++] = value;
}

static void bytes(Buffer *buffer, const void *data, size_t length) {
    buffer_reserve(buffer, length);
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
}

static void uleb(Buffer *buffer, uint64_t value) {
    do {
        uint8_t part = (uint8_t)(value & UINT64_C(0x7f));
        value >>= 7;
        if (value != 0) part |= UINT8_C(0x80);
        byte(buffer, part);
    } while (value != 0);
}

static void sleb(Buffer *buffer, int64_t value) {
    for (;;) {
        uint8_t part = (uint8_t)((uint64_t)value & UINT64_C(0x7f));
        bool sign = (part & UINT8_C(0x40)) != 0;
        int64_t next;
        if (value >= 0) {
            next = value / 128;
        } else {
            next = -1 - ((-1 - value) / 128);
        }
        bool done =
            (next == 0 && !sign) ||
            (next == -1 && sign);
        if (!done) part |= UINT8_C(0x80);
        byte(buffer, part);
        if (done) return;
        value = next;
    }
}

static void wasm_string(Buffer *buffer, const char *value) {
    size_t length = strlen(value);
    uleb(buffer, length);
    bytes(buffer, value, length);
}

static void section(Buffer *module, uint8_t identifier, const Buffer *payload) {
    byte(module, identifier);
    uleb(module, payload->length);
    bytes(module, payload->data, payload->length);
}

static char *read_source(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "kofun wasm32: cannot open %s: %s\n",
                path, strerror(errno));
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        fatal("cannot seek input");
    }
    long end = ftell(file);
    if (end < 0 || (uint64_t)end > MAX_SOURCE_BYTES) {
        fclose(file);
        fatal("source exceeds 1 MiB wasm32 Core limit");
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        fatal("cannot rewind input");
    }
    size_t size = (size_t)end;
    char *source = allocate(size + 1);
    if (size != 0 && fread(source, 1, size, file) != size) {
        fclose(file);
        free(source);
        fatal("cannot read input");
    }
    if (fclose(file) != 0) {
        free(source);
        fatal("cannot close input");
    }
    source[size] = '\0';
    *length = size;
    return source;
}

static void parse_error(Parser *parser, const char *message) {
    if (parser->error != NULL) return;
    parser->error = message;
    parser->error_line = parser->token.line == 0 ? parser->line : parser->token.line;
}

static bool identifier_start(char value) {
    return isalpha((unsigned char)value) || value == '_';
}

static bool identifier_continue(char value) {
    return isalnum((unsigned char)value) || value == '_';
}

static void next_token(Parser *parser) {
    while (parser->cursor < parser->length) {
        char value = parser->source[parser->cursor];
        if (value == '#') {
            while (parser->cursor < parser->length &&
                   parser->source[parser->cursor] != '\n') {
                ++parser->cursor;
            }
            continue;
        }
        if (!isspace((unsigned char)value)) break;
        if (value == '\n') ++parser->line;
        ++parser->cursor;
    }

    parser->token.start = parser->source + parser->cursor;
    parser->token.length = 0;
    parser->token.magnitude = 0;
    parser->token.line = parser->line;
    if (parser->cursor == parser->length) {
        parser->token.kind = TOKEN_EOF;
        return;
    }

    char value = parser->source[parser->cursor++];
    if (identifier_start(value)) {
        while (parser->cursor < parser->length &&
               identifier_continue(parser->source[parser->cursor])) {
            ++parser->cursor;
        }
        parser->token.kind = TOKEN_IDENTIFIER;
        parser->token.length =
            (size_t)((parser->source + parser->cursor) - parser->token.start);
        return;
    }
    if (isdigit((unsigned char)value)) {
        uint64_t magnitude = (uint64_t)(value - '0');
        while (parser->cursor < parser->length &&
               isdigit((unsigned char)parser->source[parser->cursor])) {
            uint64_t digit =
                (uint64_t)(parser->source[parser->cursor++] - '0');
            if (magnitude > (UINT64_C(9223372036854775808) - digit) / 10) {
                parser->token.kind = TOKEN_INTEGER;
                parser->token.length = (size_t)(
                    (parser->source + parser->cursor) - parser->token.start
                );
                parse_error(parser, "integer literal exceeds Int64");
                return;
            }
            magnitude = magnitude * 10 + digit;
        }
        parser->token.kind = TOKEN_INTEGER;
        parser->token.length =
            (size_t)((parser->source + parser->cursor) - parser->token.start);
        parser->token.magnitude = magnitude;
        return;
    }

    parser->token.length = 1;
    switch (value) {
        case '(':
            parser->token.kind = TOKEN_LEFT_PAREN;
            return;
        case ')':
            parser->token.kind = TOKEN_RIGHT_PAREN;
            return;
        case '{':
            parser->token.kind = TOKEN_LEFT_BRACE;
            return;
        case '}':
            parser->token.kind = TOKEN_RIGHT_BRACE;
            return;
        case ':':
            parser->token.kind = TOKEN_COLON;
            return;
        case '=':
            parser->token.kind = TOKEN_EQUAL;
            return;
        case '+':
            parser->token.kind = TOKEN_PLUS;
            return;
        case '-':
            parser->token.kind = TOKEN_MINUS;
            return;
        case '*':
            parser->token.kind = TOKEN_STAR;
            return;
        case '%':
            parser->token.kind = TOKEN_PERCENT;
            return;
        case ',':
            parser->token.kind = TOKEN_COMMA;
            return;
        case '/':
            if (parser->cursor < parser->length &&
                parser->source[parser->cursor] == '/') {
                ++parser->cursor;
                parser->token.kind = TOKEN_FLOOR_DIV;
                parser->token.length = 2;
            } else {
                parser->token.kind = TOKEN_SLASH;
            }
            return;
        default:
            parser->token.kind = TOKEN_EOF;
            parse_error(parser, "unsupported token in wasm32 arithmetic Core");
            return;
    }
}

static bool token_is(const Parser *parser, const char *value) {
    size_t length = strlen(value);
    return parser->token.kind == TOKEN_IDENTIFIER &&
           parser->token.length == length &&
           memcmp(parser->token.start, value, length) == 0;
}

static bool consume(Parser *parser, TokenKind kind) {
    if (parser->token.kind != kind) return false;
    next_token(parser);
    return true;
}

static bool consume_word(Parser *parser, const char *value) {
    if (!token_is(parser, value)) return false;
    next_token(parser);
    return true;
}

static bool expect(Parser *parser, TokenKind kind, const char *message) {
    if (!consume(parser, kind)) {
        parse_error(parser, message);
        return false;
    }
    return true;
}

static bool expect_word(Parser *parser, const char *value, const char *message) {
    if (!consume_word(parser, value)) {
        parse_error(parser, message);
        return false;
    }
    return true;
}

static int add_node(
    Parser *parser,
    NodeKind kind,
    int left,
    int right,
    int binding,
    int64_t value
) {
    if (parser->node_count == MAX_NODES) {
        parse_error(parser, "too many expressions in wasm32 Core");
        return -1;
    }
    int index = (int)parser->node_count++;
    parser->nodes[index] = (Node){
        .kind = kind,
        .left = left,
        .right = right,
        .binding = binding,
        .value = value
    };
    return index;
}

static int find_binding(
    const Parser *parser,
    const char *name,
    size_t length
) {
    for (size_t index = parser->binding_count; index > 0; --index) {
        const Binding *binding = &parser->bindings[index - 1];
        if (binding->length == length &&
            memcmp(binding->name, name, length) == 0) {
            return (int)(index - 1);
        }
    }
    return -1;
}

static int parse_expression(Parser *parser);

static int parse_primary(Parser *parser) {
    if (consume(parser, TOKEN_LEFT_PAREN)) {
        int expression = parse_expression(parser);
        expect(parser, TOKEN_RIGHT_PAREN,
               "expected `)` in wasm32 Core expression");
        return expression;
    }
    if (parser->token.kind == TOKEN_INTEGER) {
        uint64_t magnitude = parser->token.magnitude;
        if (magnitude > INT64_MAX) {
            parse_error(parser, "positive integer literal exceeds Int64");
            return -1;
        }
        next_token(parser);
        return add_node(parser, NODE_LITERAL, -1, -1, -1,
                        (int64_t)magnitude);
    }
    if (parser->token.kind == TOKEN_IDENTIFIER) {
        const char *name = parser->token.start;
        size_t length = parser->token.length;
        int binding = find_binding(parser, name, length);
        if (binding < 0) {
            parse_error(parser, "unknown binding in wasm32 Core expression");
            return -1;
        }
        next_token(parser);
        return add_node(parser, NODE_VARIABLE, -1, -1, binding, 0);
    }
    parse_error(parser, "expected Int expression in wasm32 Core");
    return -1;
}

static int parse_unary(Parser *parser) {
    if (consume(parser, TOKEN_PLUS)) return parse_unary(parser);
    if (consume(parser, TOKEN_MINUS)) {
        if (parser->token.kind == TOKEN_INTEGER) {
            uint64_t magnitude = parser->token.magnitude;
            next_token(parser);
            int64_t value =
                magnitude == UINT64_C(9223372036854775808)
                    ? INT64_MIN
                    : -(int64_t)magnitude;
            return add_node(parser, NODE_LITERAL, -1, -1, -1, value);
        }
        int operand = parse_unary(parser);
        return add_node(parser, NODE_NEGATE, operand, -1, -1, 0);
    }
    return parse_primary(parser);
}

static int parse_term(Parser *parser) {
    int left = parse_unary(parser);
    while (parser->error == NULL) {
        NodeKind kind;
        if (consume(parser, TOKEN_STAR)) {
            kind = NODE_MULTIPLY;
        } else if (consume(parser, TOKEN_SLASH)) {
            kind = NODE_DIVIDE;
        } else if (consume(parser, TOKEN_FLOOR_DIV)) {
            kind = NODE_FLOOR_DIVIDE;
        } else if (consume(parser, TOKEN_PERCENT)) {
            kind = NODE_FLOOR_MODULO;
        } else {
            break;
        }
        int right = parse_unary(parser);
        left = add_node(parser, kind, left, right, -1, 0);
    }
    return left;
}

static int parse_expression(Parser *parser) {
    int left = parse_term(parser);
    while (parser->error == NULL) {
        NodeKind kind;
        if (consume(parser, TOKEN_PLUS)) {
            kind = NODE_ADD;
        } else if (consume(parser, TOKEN_MINUS)) {
            kind = NODE_SUBTRACT;
        } else {
            break;
        }
        int right = parse_term(parser);
        left = add_node(parser, kind, left, right, -1, 0);
    }
    return left;
}

static void add_statement(
    Parser *parser,
    StatementKind kind,
    int expression,
    int binding
) {
    if (parser->statement_count == MAX_STATEMENTS) {
        parse_error(parser, "too many statements in wasm32 Core");
        return;
    }
    parser->statements[parser->statement_count++] = (Statement){
        .kind = kind,
        .expression = expression,
        .binding = binding
    };
}

static void parse_binding(Parser *parser) {
    if (parser->binding_count == MAX_BINDINGS) {
        parse_error(parser, "too many bindings in wasm32 Core");
        return;
    }
    if (parser->token.kind != TOKEN_IDENTIFIER) {
        parse_error(parser, "expected binding name after `let`");
        return;
    }
    const char *name = parser->token.start;
    size_t length = parser->token.length;
    if (length >= sizeof(parser->bindings[0].name)) {
        parse_error(parser, "binding name is too long");
        return;
    }
    if (find_binding(parser, name, length) >= 0) {
        parse_error(parser, "duplicate binding in wasm32 Core");
        return;
    }
    next_token(parser);
    if (consume(parser, TOKEN_COLON)) {
        if (!expect_word(parser, "Int",
                         "wasm32 arithmetic Core supports only Int bindings")) {
            return;
        }
    }
    if (!expect(parser, TOKEN_EQUAL, "expected `=` in wasm32 Core binding")) {
        return;
    }
    int expression = parse_expression(parser);
    if (parser->error != NULL) return;

    int binding = (int)parser->binding_count++;
    Binding *target = &parser->bindings[binding];
    memcpy(target->name, name, length);
    target->name[length] = '\0';
    target->length = length;
    add_statement(parser, STATEMENT_BIND, expression, binding);
}

static void parse_print(Parser *parser) {
    if (!expect(parser, TOKEN_LEFT_PAREN,
                "expected `(` after print in wasm32 Core")) {
        return;
    }
    int expression = parse_expression(parser);
    if (!expect(parser, TOKEN_RIGHT_PAREN,
                "expected `)` after print expression")) {
        return;
    }
    add_statement(parser, STATEMENT_PRINT, expression, -1);
    ++parser->print_count;
}

static bool parse_program(Parser *parser) {
    parser->line = 1;
    next_token(parser);
    if (!expect_word(parser, "fn", "wasm32 Core requires `fn main()`") ||
        !expect_word(parser, "main", "wasm32 Core requires `fn main()`") ||
        !expect(parser, TOKEN_LEFT_PAREN, "expected `(` after main") ||
        !expect(parser, TOKEN_RIGHT_PAREN, "expected `)` after main") ||
        !expect(parser, TOKEN_LEFT_BRACE, "expected `{` before main body")) {
        return false;
    }
    while (parser->error == NULL &&
           parser->token.kind != TOKEN_RIGHT_BRACE &&
           parser->token.kind != TOKEN_EOF) {
        if (consume_word(parser, "let")) {
            parse_binding(parser);
        } else if (consume_word(parser, "print")) {
            parse_print(parser);
        } else {
            parse_error(parser,
                        "wasm32 Core supports only `let` and `print` statements");
        }
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, "expected `}` after main body")) {
        return false;
    }
    if (parser->token.kind != TOKEN_EOF) {
        parse_error(parser, "unexpected source after `fn main`");
    }
    if (parser->print_count == 0) {
        parse_error(parser, "wasm32 Core main must print at least one Int");
    }
    return parser->error == NULL;
}

enum {
    OP_UNREACHABLE = 0x00,
    OP_IF = 0x04,
    OP_END = 0x0b,
    OP_CALL = 0x10,
    OP_LOCAL_GET = 0x20,
    OP_LOCAL_SET = 0x21,
    OP_I32_CONST = 0x41,
    OP_I64_CONST = 0x42,
    OP_I32_EQZ = 0x45,
    OP_I32_NE = 0x47,
    OP_I64_EQZ = 0x50,
    OP_I64_EQ = 0x51,
    OP_I64_NE = 0x52,
    OP_I64_LT_S = 0x53,
    OP_I32_AND = 0x71,
    OP_I32_OR = 0x72,
    OP_I64_ADD = 0x7c,
    OP_I64_SUB = 0x7d,
    OP_I64_MUL = 0x7e,
    OP_I64_DIV_S = 0x7f,
    OP_I64_REM_S = 0x81,
    OP_I64_AND = 0x83,
    OP_I64_XOR = 0x85
};

enum {
    ERROR_ADD_OVERFLOW = 1,
    ERROR_SUBTRACT_OVERFLOW = 2,
    ERROR_MULTIPLY_OVERFLOW = 3,
    ERROR_NEGATE_OVERFLOW = 4,
    ERROR_DIVIDE_ZERO = 5,
    ERROR_DIVIDE_OVERFLOW = 6,
    ERROR_FLOOR_DIVIDE_ZERO = 7,
    ERROR_FLOOR_DIVIDE_OVERFLOW = 8,
    ERROR_MODULO_ZERO = 9
};

static uint32_t node_local(const Parser *parser, int node) {
    return (uint32_t)(parser->binding_count + (size_t)node * 3);
}

static uint32_t node_aux(const Parser *parser, int node, uint32_t offset) {
    return node_local(parser, node) + offset;
}

static void instruction_index(Buffer *body, uint8_t opcode, uint32_t index) {
    byte(body, opcode);
    uleb(body, index);
}

static void i64_const(Buffer *body, int64_t value) {
    byte(body, OP_I64_CONST);
    sleb(body, value);
}

static void panic_with(Buffer *body, int code) {
    byte(body, OP_I32_CONST);
    sleb(body, code);
    instruction_index(body, OP_CALL, 1);
    byte(body, OP_UNREACHABLE);
}

static void begin_if(Buffer *body) {
    byte(body, OP_IF);
    byte(body, 0x40);
}

static void check_division_pair(
    Buffer *body,
    uint32_t left,
    uint32_t right,
    int zero_code,
    int overflow_code
) {
    instruction_index(body, OP_LOCAL_GET, right);
    byte(body, OP_I64_EQZ);
    begin_if(body);
    panic_with(body, zero_code);
    byte(body, OP_END);

    instruction_index(body, OP_LOCAL_GET, left);
    i64_const(body, INT64_MIN);
    byte(body, OP_I64_EQ);
    instruction_index(body, OP_LOCAL_GET, right);
    i64_const(body, -1);
    byte(body, OP_I64_EQ);
    byte(body, OP_I32_AND);
    begin_if(body);
    panic_with(body, overflow_code);
    byte(body, OP_END);
}

static void emit_expression(const Parser *parser, int index, Buffer *body) {
    const Node *node = &parser->nodes[index];
    uint32_t target = node_local(parser, index);
    if (node->kind == NODE_LITERAL) {
        i64_const(body, node->value);
        instruction_index(body, OP_LOCAL_SET, target);
        return;
    }
    if (node->kind == NODE_VARIABLE) {
        instruction_index(body, OP_LOCAL_GET, (uint32_t)node->binding);
        instruction_index(body, OP_LOCAL_SET, target);
        return;
    }

    emit_expression(parser, node->left, body);
    uint32_t left = node_local(parser, node->left);
    if (node->kind == NODE_NEGATE) {
        instruction_index(body, OP_LOCAL_GET, left);
        i64_const(body, INT64_MIN);
        byte(body, OP_I64_EQ);
        begin_if(body);
        panic_with(body, ERROR_NEGATE_OVERFLOW);
        byte(body, OP_END);
        i64_const(body, 0);
        instruction_index(body, OP_LOCAL_GET, left);
        byte(body, OP_I64_SUB);
        instruction_index(body, OP_LOCAL_SET, target);
        return;
    }

    emit_expression(parser, node->right, body);
    uint32_t right = node_local(parser, node->right);
    instruction_index(body, OP_LOCAL_GET, left);
    instruction_index(body, OP_LOCAL_GET, right);

    if (node->kind == NODE_ADD) {
        byte(body, OP_I64_ADD);
        instruction_index(body, OP_LOCAL_SET, target);
        instruction_index(body, OP_LOCAL_GET, left);
        instruction_index(body, OP_LOCAL_GET, target);
        byte(body, OP_I64_XOR);
        instruction_index(body, OP_LOCAL_GET, right);
        instruction_index(body, OP_LOCAL_GET, target);
        byte(body, OP_I64_XOR);
        byte(body, OP_I64_AND);
        i64_const(body, 0);
        byte(body, OP_I64_LT_S);
        begin_if(body);
        panic_with(body, ERROR_ADD_OVERFLOW);
        byte(body, OP_END);
        return;
    }
    if (node->kind == NODE_SUBTRACT) {
        byte(body, OP_I64_SUB);
        instruction_index(body, OP_LOCAL_SET, target);
        instruction_index(body, OP_LOCAL_GET, left);
        instruction_index(body, OP_LOCAL_GET, right);
        byte(body, OP_I64_XOR);
        instruction_index(body, OP_LOCAL_GET, left);
        instruction_index(body, OP_LOCAL_GET, target);
        byte(body, OP_I64_XOR);
        byte(body, OP_I64_AND);
        i64_const(body, 0);
        byte(body, OP_I64_LT_S);
        begin_if(body);
        panic_with(body, ERROR_SUBTRACT_OVERFLOW);
        byte(body, OP_END);
        return;
    }
    if (node->kind == NODE_MULTIPLY) {
        byte(body, OP_I64_MUL);
        instruction_index(body, OP_LOCAL_SET, target);

        instruction_index(body, OP_LOCAL_GET, left);
        i64_const(body, -1);
        byte(body, OP_I64_EQ);
        instruction_index(body, OP_LOCAL_GET, right);
        i64_const(body, INT64_MIN);
        byte(body, OP_I64_EQ);
        byte(body, OP_I32_AND);
        instruction_index(body, OP_LOCAL_GET, right);
        i64_const(body, -1);
        byte(body, OP_I64_EQ);
        instruction_index(body, OP_LOCAL_GET, left);
        i64_const(body, INT64_MIN);
        byte(body, OP_I64_EQ);
        byte(body, OP_I32_AND);
        byte(body, OP_I32_OR);
        begin_if(body);
        panic_with(body, ERROR_MULTIPLY_OVERFLOW);
        byte(body, OP_END);

        instruction_index(body, OP_LOCAL_GET, right);
        byte(body, OP_I64_EQZ);
        byte(body, OP_I32_EQZ);
        begin_if(body);
        instruction_index(body, OP_LOCAL_GET, target);
        instruction_index(body, OP_LOCAL_GET, right);
        byte(body, OP_I64_DIV_S);
        instruction_index(body, OP_LOCAL_GET, left);
        byte(body, OP_I64_NE);
        begin_if(body);
        panic_with(body, ERROR_MULTIPLY_OVERFLOW);
        byte(body, OP_END);
        byte(body, OP_END);
        return;
    }
    if (node->kind == NODE_DIVIDE) {
        check_division_pair(
            body, left, right, ERROR_DIVIDE_ZERO, ERROR_DIVIDE_OVERFLOW
        );
        byte(body, OP_I64_DIV_S);
        instruction_index(body, OP_LOCAL_SET, target);
        return;
    }
    if (node->kind == NODE_FLOOR_DIVIDE) {
        check_division_pair(
            body, left, right,
            ERROR_FLOOR_DIVIDE_ZERO,
            ERROR_FLOOR_DIVIDE_OVERFLOW
        );
        byte(body, OP_I64_DIV_S);
        instruction_index(body, OP_LOCAL_SET, target);
        instruction_index(body, OP_LOCAL_GET, left);
        instruction_index(body, OP_LOCAL_GET, right);
        byte(body, OP_I64_REM_S);
        instruction_index(body, OP_LOCAL_SET, node_aux(parser, index, 1));

        instruction_index(body, OP_LOCAL_GET, node_aux(parser, index, 1));
        i64_const(body, 0);
        byte(body, OP_I64_NE);
        instruction_index(body, OP_LOCAL_GET, node_aux(parser, index, 1));
        i64_const(body, 0);
        byte(body, OP_I64_LT_S);
        instruction_index(body, OP_LOCAL_GET, right);
        i64_const(body, 0);
        byte(body, OP_I64_LT_S);
        byte(body, OP_I32_NE);
        byte(body, OP_I32_AND);
        begin_if(body);
        instruction_index(body, OP_LOCAL_GET, target);
        i64_const(body, 1);
        byte(body, OP_I64_SUB);
        instruction_index(body, OP_LOCAL_SET, target);
        byte(body, OP_END);
        return;
    }
    if (node->kind == NODE_FLOOR_MODULO) {
        instruction_index(body, OP_LOCAL_GET, right);
        byte(body, OP_I64_EQZ);
        begin_if(body);
        panic_with(body, ERROR_MODULO_ZERO);
        byte(body, OP_END);
        byte(body, OP_I64_REM_S);
        instruction_index(body, OP_LOCAL_SET, target);

        instruction_index(body, OP_LOCAL_GET, target);
        i64_const(body, 0);
        byte(body, OP_I64_NE);
        instruction_index(body, OP_LOCAL_GET, target);
        i64_const(body, 0);
        byte(body, OP_I64_LT_S);
        instruction_index(body, OP_LOCAL_GET, right);
        i64_const(body, 0);
        byte(body, OP_I64_LT_S);
        byte(body, OP_I32_NE);
        byte(body, OP_I32_AND);
        begin_if(body);
        instruction_index(body, OP_LOCAL_GET, target);
        instruction_index(body, OP_LOCAL_GET, right);
        byte(body, OP_I64_ADD);
        instruction_index(body, OP_LOCAL_SET, target);
        byte(body, OP_END);
        return;
    }
    fatal("internal unsupported wasm32 expression");
}

static Buffer emit_module(const Parser *parser) {
    Buffer module = {0};
    static const uint8_t header[] = {
        0x00, 0x61, 0x73, 0x6d,
        0x01, 0x00, 0x00, 0x00
    };
    bytes(&module, header, sizeof(header));

    Buffer types = {0};
    uleb(&types, 3);
    byte(&types, 0x60);
    uleb(&types, 1);
    byte(&types, 0x7e);
    uleb(&types, 0);
    byte(&types, 0x60);
    uleb(&types, 1);
    byte(&types, 0x7f);
    uleb(&types, 0);
    byte(&types, 0x60);
    uleb(&types, 0);
    uleb(&types, 0);
    section(&module, 1, &types);

    Buffer imports = {0};
    uleb(&imports, 2);
    wasm_string(&imports, "kofun");
    wasm_string(&imports, "print_i64");
    byte(&imports, 0x00);
    uleb(&imports, 0);
    wasm_string(&imports, "kofun");
    wasm_string(&imports, "panic");
    byte(&imports, 0x00);
    uleb(&imports, 1);
    section(&module, 2, &imports);

    Buffer functions = {0};
    uleb(&functions, 1);
    uleb(&functions, 2);
    section(&module, 3, &functions);

    Buffer exports = {0};
    uleb(&exports, 1);
    wasm_string(&exports, "main");
    byte(&exports, 0x00);
    uleb(&exports, 2);
    section(&module, 7, &exports);

    Buffer body = {0};
    uint64_t local_count =
        parser->binding_count + parser->node_count * UINT64_C(3);
    if (local_count == 0) {
        uleb(&body, 0);
    } else {
        uleb(&body, 1);
        uleb(&body, local_count);
        byte(&body, 0x7e);
    }
    for (size_t index = 0; index < parser->statement_count; ++index) {
        const Statement *statement = &parser->statements[index];
        emit_expression(parser, statement->expression, &body);
        uint32_t value = node_local(parser, statement->expression);
        instruction_index(&body, OP_LOCAL_GET, value);
        if (statement->kind == STATEMENT_BIND) {
            instruction_index(
                &body, OP_LOCAL_SET, (uint32_t)statement->binding
            );
        } else {
            instruction_index(&body, OP_CALL, 0);
        }
    }
    byte(&body, OP_END);

    Buffer code = {0};
    uleb(&code, 1);
    uleb(&code, body.length);
    bytes(&code, body.data, body.length);
    section(&module, 10, &code);

    free(types.data);
    free(imports.data);
    free(functions.data);
    free(exports.data);
    free(body.data);
    free(code.data);
    return module;
}

static bool write_module(const char *path, const Buffer *module) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "kofun wasm32: cannot open output %s: %s\n",
                path, strerror(errno));
        return false;
    }
    bool okay =
        fwrite(module->data, 1, module->length, file) == module->length;
    if (fclose(file) != 0) okay = false;
    if (!okay) {
        remove(path);
        fprintf(stderr, "kofun wasm32: cannot write output %s\n", path);
    }
    return okay;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: kofun-wasm-core INPUT.kofun OUTPUT.wasm\n");
        return 2;
    }

    size_t length = 0;
    char *source = read_source(argv[1], &length);
    if (source == NULL) return 1;
    Parser parser = {
        .source = source,
        .length = length
    };
    bool parsed = parse_program(&parser);
    if (!parsed) {
        fprintf(stderr, "kofun wasm32: line %zu: %s\n",
                parser.error_line,
                parser.error == NULL ? "invalid source" : parser.error);
        free(source);
        return 1;
    }

    Buffer module = emit_module(&parser);
    bool written = write_module(argv[2], &module);
    free(module.data);
    free(source);
    return written ? 0 : 1;
}
