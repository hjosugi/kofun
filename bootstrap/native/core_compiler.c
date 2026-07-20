/*
 * Audited bootstrap driver for the Kofun-owned direct native encoder.
 *
 * The target-independent frontend parses one deliberately small Kofun Core:
 *
 *   fn main() {
 *       print(CONSTANT_EXPRESSION)
 *   }
 *
 * CONSTANT_EXPRESSION supports non-negative literals, parentheses, +, and *.
 * Its result must be 10..99 so both registered targets exercise the same
 * two-digit runtime formatting contract. The AST is shared. Only instruction
 * selection and encoding are target-specific.
 *
 * This C11 seed is temporary bootstrap machinery. Canonical instruction, ELF,
 * and postfix Core encoders live in encoder.kofun; no Python implementation is
 * used by this compiler.
 */

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    NODE_ADD,
    NODE_MULTIPLY,
    NODE_NEGATE,
    NODE_LIST,
    NODE_INDEX,
    NODE_LENGTH,
} NodeKind;

typedef enum {
    VALUE_INT,
    VALUE_LIST,
} ValueKind;

typedef struct Node Node;

struct Node {
    NodeKind kind;
    ValueKind value_kind;
    int64_t value;
    bool value_known;
    size_t source_line;
    Node *left;
    Node *right;
    Node **items;
    size_t item_count;
};

typedef struct {
    const char *source;
    size_t cursor;
    const char *error;
    size_t main_line;
    size_t print_line;
} Parser;

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

static bool word_continue(char value) {
    return isalnum((unsigned char)value) || value == '_';
}

static bool consume_word(Parser *parser, const char *word) {
    skip_trivia(parser);
    size_t length = strlen(word);
    if (strncmp(parser->source + parser->cursor, word, length) != 0 ||
        word_continue(parser->source[parser->cursor + length])) {
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
    result->source_line = line;
    result->left = left;
    result->right = right;
    result->items = NULL;
    result->item_count = 0;
    return result;
}

static Node *parse_expression(Parser *parser);

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
    if (consume_char(parser, '[')) {
        Node **items = NULL;
        size_t length = 0;
        size_t capacity = 0;
        skip_trivia(parser);
        if (!consume_char(parser, ']')) {
            for (;;) {
                Node *item = parse_expression(parser);
                if (item == NULL || parser->error != NULL) break;
                if (item->value_kind != VALUE_INT) {
                    parse_error(parser, "List[Int] literal requires Int elements");
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
        return list;
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
        if (value != NULL && value->value_kind != VALUE_LIST) {
            parse_error(parser, "`len` native Core argument must be List[Int]");
        }
        return node(
            NODE_LENGTH,
            VALUE_INT,
            value == NULL ? 0 : (int64_t)value->item_count,
            value != NULL,
            source_line(parser->source, literal_at),
            value,
            NULL
        );
    }

    skip_trivia(parser);
    if (!isdigit((unsigned char)parser->source[parser->cursor])) {
        parse_error(
            parser,
            "expected integer, List[Int], `len`, or `(`"
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
            parse_error(parser, "expected `]` after List[Int] index");
            return value;
        }
        if (value->value_kind != VALUE_LIST ||
            index == NULL ||
            index->value_kind != VALUE_INT) {
            parse_error(parser, "native Core indexing requires List[Int]");
            return value;
        }

        int64_t resolved = 0;
        bool known = index->value_known;
        if (known) {
            int64_t wanted = index->value;
            if (wanted < 0) wanted += (int64_t)value->item_count;
            if (wanted < 0 || (uint64_t)wanted >= value->item_count) {
                known = false;
            } else {
                Node *item = value->items[(size_t)wanted];
                known = item->value_known;
                resolved = item->value;
            }
        }
        value = node(
            NODE_INDEX,
            VALUE_INT,
            resolved,
            known,
            source_line(parser->source, index_at),
            value,
            index
        );
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

static Node *parse_expression(Parser *parser) {
    Node *left = parse_product(parser);
    while (parser->error == NULL) {
        skip_trivia(parser);
        if (parser->source[parser->cursor] != '+') break;
        size_t operator_at = parser->cursor;
        ++parser->cursor;
        Node *right = parse_product(parser);
        if (right == NULL) return left;
        if (left->value_kind != VALUE_INT ||
            right->value_kind != VALUE_INT) {
            parse_error(parser, "operator `+` requires Int operands");
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

static Node *parse_program(Parser *parser) {
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

    skip_trivia(parser);
    parser->print_line = source_line(parser->source, parser->cursor);
    if (!consume_word(parser, "print") || !consume_char(parser, '(')) {
        parse_error(
            parser,
            "native Core requires `fn main() { print(EXPRESSION) }`"
        );
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
    if (expression != NULL && expression->value_kind != VALUE_INT) {
        parse_error(parser, "native Core print expression must produce Int");
    } else if (expression != NULL &&
        expression->value_known &&
        (expression->value < 10 || expression->value > 99)) {
        parse_error(parser, "native Core print result must be 10..99");
    }
    return expression;
}

static size_t register_depth(const Node *expression) {
    if (expression->kind == NODE_LITERAL) return 1;
    if (expression->kind == NODE_NEGATE ||
        expression->kind == NODE_LENGTH) {
        return register_depth(expression->left);
    }
    if (expression->kind == NODE_LIST) {
        size_t depth = 1;
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
    for (size_t index = 0; index < expression->item_count; ++index) {
        free_node(expression->items[index]);
    }
    free(expression->items);
    free(expression);
}

static bool uses_list(const Node *expression) {
    if (expression == NULL) return false;
    if (expression->kind == NODE_LIST ||
        expression->kind == NODE_INDEX ||
        expression->kind == NODE_LENGTH) {
        return true;
    }
    if (uses_list(expression->left) || uses_list(expression->right)) {
        return true;
    }
    for (size_t index = 0; index < expression->item_count; ++index) {
        if (uses_list(expression->items[index])) return true;
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
    bool used;
    Offsets alloc_calls;
    Offsets oom_jumps;
    Offsets index_jumps;
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

static void x64_runtime_free(X64Runtime *runtime) {
    free(runtime->alloc_calls.fields);
    free(runtime->oom_jumps.fields);
    free(runtime->index_jumps.fields);
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

static void x64_expression(
    Bytes *text,
    const Node *expression,
    LineRows *rows,
    X64Runtime *runtime
) {
    if (expression->kind == NODE_LITERAL) {
        line_row(rows, text->length, expression->source_line);
        x64_mov_eax_imm32(text, (uint32_t)expression->value);
        byte(text, UINT8_C(0x50)); /* push rax */
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

    if (expression->kind == NODE_LIST) {
        if (expression->item_count >
            (UINT32_MAX - 8) / sizeof(uint64_t)) {
            fatal("List[Int] literal is too large");
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

    if (expression->kind == NODE_LENGTH) {
        x64_expression(text, expression->left, rows, runtime);
        line_row(rows, text->length, expression->source_line);
        byte(text, UINT8_C(0x58)); /* pop rax */
        byte(text, UINT8_C(0x48));
        byte(text, UINT8_C(0x8b));
        byte(text, UINT8_C(0x00)); /* mov rax, [rax] */
        byte(text, UINT8_C(0x50)); /* push length */
        return;
    }

    if (expression->kind == NODE_INDEX) {
        runtime->used = true;
        x64_expression(text, expression->left, rows, runtime);
        x64_expression(text, expression->right, rows, runtime);
        line_row(rows, text->length, expression->source_line);
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
            &runtime->index_jumps
        ); /* js index error */
        byte(text, UINT8_C(0x4c));
        byte(text, UINT8_C(0x39));
        byte(text, UINT8_C(0xc1)); /* cmp rcx, r8 */
        x64_rel32_placeholder(
            text,
            UINT8_C(0x0f),
            UINT8_C(0x8d),
            &runtime->index_jumps
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

    size_t oom_at = text->length;
    const char oom_message[] = "kofun: out of memory\n";
    size_t oom_address = x64_diagnostic(
        text,
        (uint32_t)(sizeof(oom_message) - 1),
        70
    );

    size_t index_at = text->length;
    const char index_message[] = "kofun: list index out of range\n";
    size_t index_address = x64_diagnostic(
        text,
        (uint32_t)(sizeof(index_message) - 1),
        1
    );

    size_t oom_message_at = text->length;
    for (size_t index = 0; index < sizeof(oom_message) - 1; ++index) {
        byte(text, (uint8_t)oom_message[index]);
    }
    size_t index_message_at = text->length;
    for (size_t index = 0; index < sizeof(index_message) - 1; ++index) {
        byte(text, (uint8_t)index_message[index]);
    }

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
    for (size_t index = 0; index < runtime->index_jumps.length; ++index) {
        x64_patch_rel32(
            text,
            runtime->index_jumps.fields[index],
            index_at
        );
    }
    x64_patch_u32(
        text,
        oom_address,
        (uint32_t)(IMAGE_BASE + TEXT_OFFSET + oom_message_at)
    );
    x64_patch_u32(
        text,
        index_address,
        (uint32_t)(IMAGE_BASE + TEXT_OFFSET + index_message_at)
    );
}

static void x64_text(
    Bytes *text,
    const Node *expression,
    LineRows *rows,
    size_t print_line
) {
    X64Runtime runtime = {0};
    x64_expression(text, expression, rows, &runtime);
    line_row(rows, text->length, print_line);
    byte(text, UINT8_C(0x58)); /* pop rax */

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
    x64_mov_r32_imm32(text, UINT8_C(0xb8), 60); /* exit */
    x64_mov_r32_imm32(text, UINT8_C(0xbf), 0);
    x64_syscall(text);
    x64_runtime(text, &runtime);
    x64_runtime_free(&runtime);
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
    if (aarch64 && uses_list(expression)) {
        fputs(
            "kofun native: AArch64 native Core does not support List[Int] yet\n",
            stderr
        );
        free_node(expression);
        free(source);
        return 1;
    }
    if (aarch64 && register_depth(expression) > 16) {
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
        a64_text(&text, expression);
    } else {
        x64_text(&text, expression, &rows, parser.print_line);
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
