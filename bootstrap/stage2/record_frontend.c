/*
 * Bounded nominal heterogeneous record frontend (issue #546).
 *
 * The accepted v1 record surface is `type Name = { field: Type, ... }` with
 * labelled call-form construction `Name(field: value, ...)` and `value.field`
 * access. This frontend collects declarations, resolves and types one bounded
 * function surface, computes untagged target layout for x86-64 and AArch64,
 * and evaluates the zero-parameter functions so construction, passing,
 * returning, and reading are observed rather than asserted.
 *
 * There is intentionally no module system, generic record, trait, method,
 * mutation, spread/update, pattern destructuring, or backend lowering here.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOURCE_LIMIT (256u * 1024u)
#define TOKEN_LIMIT 4096u
#define TEXT_LIMIT 128u
#define RECORD_LIMIT 16u
#define FIELD_LIMIT 128u
#define ENUM_LIMIT 16u
#define VARIANT_LIMIT 128u
#define FUNCTION_LIMIT 64u
#define PARAM_LIMIT 128u
#define LOCAL_LIMIT 128u
#define EXPR_LIMIT 2048u
#define STMT_LIMIT 1024u
#define ARGUMENT_LIMIT 1024u
#define ARGUMENT_GROUP_LIMIT 32u
#define DEPTH_LIMIT 64u
#define CALL_DEPTH_LIMIT 32u
#define STEP_LIMIT 2000000u
#define LIST_LIMIT 1024u
#define VALUE_TEXT_LIMIT 4096u
#define RENDER_LIMIT 4096u
#define TARGET_COUNT 2u

#define NONE ((size_t)-1)

/* ---------------------------------------------------------------- tokens */

typedef enum {
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_TEXT,
    TOKEN_PUNCTUATION,
    TOKEN_END
} TokenKind;

typedef struct {
    TokenKind kind;
    char text[TEXT_LIMIT];
    size_t start;
    size_t end;
} Token;

/* ----------------------------------------------------------------- types */

typedef enum {
    TYPE_UNKNOWN,
    TYPE_INT,
    TYPE_TEXT,
    TYPE_BOOL,
    TYPE_ENUM,
    TYPE_RECORD,
    TYPE_LIST
} TypeTag;

typedef struct {
    TypeTag tag;
    size_t decl;
    TypeTag element_tag;
    size_t element_decl;
} Type;

typedef struct {
    char name[TEXT_LIMIT];
    char element[TEXT_LIMIT];
    bool is_list;
    bool written;
    size_t start;
    size_t end;
} TypeRef;

/* ---------------------------------------------------------- declarations */

typedef struct {
    char name[TEXT_LIMIT];
    TypeRef written_type;
    Type type;
    size_t offset[TARGET_COUNT];
    size_t size[TARGET_COUNT];
    size_t align[TARGET_COUNT];
    size_t start;
    size_t end;
} Field;

typedef struct {
    char name[TEXT_LIMIT];
    size_t first_field;
    size_t field_count;
    size_t size[TARGET_COUNT];
    size_t align[TARGET_COUNT];
    bool laid_out;
    bool laying_out;
    size_t start;
    size_t end;
} Record;

typedef struct {
    char name[TEXT_LIMIT];
    size_t enum_index;
    size_t local_index;
    size_t start;
    size_t end;
} Variant;

typedef struct {
    char name[TEXT_LIMIT];
    size_t first_variant;
    size_t variant_count;
    size_t start;
    size_t end;
} Enumeration;

typedef enum {
    MODE_VALUE,
    MODE_READ,
    MODE_TAKE
} ParamMode;

typedef struct {
    char name[TEXT_LIMIT];
    TypeRef written_type;
    Type type;
    ParamMode mode;
    size_t start;
    size_t end;
} Param;

typedef struct {
    char name[TEXT_LIMIT];
    size_t first_param;
    size_t param_count;
    TypeRef written_return;
    Type return_type;
    bool has_return;
    size_t body_first;
    size_t start;
    size_t end;
} Function;

/* ------------------------------------------------------------------- ast */

typedef enum {
    EXPR_INT,
    EXPR_TEXT,
    EXPR_BOOL,
    EXPR_NAME,
    EXPR_CALL,
    EXPR_CONSTRUCT,
    EXPR_FIELD,
    EXPR_INDEX,
    EXPR_LIST,
    EXPR_UNARY,
    EXPR_BINARY
} ExprKind;

typedef enum {
    NAME_UNRESOLVED,
    NAME_LOCAL,
    NAME_VARIANT
} NameKind;

typedef enum {
    CALL_UNRESOLVED,
    CALL_FUNCTION,
    CALL_BUILTIN
} CallKind;

typedef struct {
    char label[TEXT_LIMIT];
    bool labelled;
    size_t expr;
    size_t start;
    size_t end;
} Argument;

typedef struct {
    ExprKind kind;
    size_t start;
    size_t end;
    long long integer;
    bool boolean;
    char text[TEXT_LIMIT];
    char operator_text[8];
    size_t left;
    size_t right;
    size_t first_argument;
    size_t argument_count;
    Type type;
    NameKind name_kind;
    CallKind call_kind;
    size_t decl;
    size_t field_index;
} Expr;

typedef enum {
    STMT_LET,
    STMT_ASSIGN,
    STMT_RETURN,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_TAKE,
    STMT_EXPR
} StmtKind;

typedef struct {
    StmtKind kind;
    size_t start;
    size_t end;
    char name[TEXT_LIMIT];
    bool is_mut;
    bool has_annotation;
    TypeRef written_type;
    Type type;
    size_t expr;
    size_t target;
    size_t then_first;
    size_t else_first;
    size_t body_first;
    size_t next;
} Stmt;

/* --------------------------------------------------------------- targets */

typedef struct {
    const char *name;
    const char *data_layout;
    size_t int_size;
    size_t int_align;
    size_t pointer_size;
    size_t pointer_align;
    size_t bool_size;
    size_t bool_align;
    size_t tag_size;
    size_t tag_align;
} Target;

static const Target TARGETS[TARGET_COUNT] = {
    {
        "x86_64-linux",
        "little-endian LP64; Int=int64; Text=(pointer,length); "
        "List=(pointer,length); enum tag=uint8",
        8, 8, 8, 8, 1, 1, 1, 1
    },
    {
        "aarch64-linux",
        "little-endian LP64; Int=int64; Text=(pointer,length); "
        "List=(pointer,length); enum tag=uint8",
        8, 8, 8, 8, 1, 1, 1, 1
    }
};

/* ---------------------------------------------------------------- values */

typedef struct Value {
    TypeTag tag;
    long long integer;
    bool boolean;
    char *text;
    size_t length;
    size_t decl;
    size_t variant;
    struct Value *items;
    size_t item_count;
    TypeTag element_tag;
    size_t element_decl;
} Value;

typedef struct {
    char name[TEXT_LIMIT];
    Value value;
} Binding;

/* ------------------------------------------------------------- frontend */

typedef struct {
    char name[TEXT_LIMIT];
    Type type;
    bool is_mut;
    bool moved;
    bool borrowed;
} Local;

typedef struct {
    char function_name[TEXT_LIMIT];
    size_t record_index;
    size_t expr;
} ConstructFact;

typedef struct {
    char function_name[TEXT_LIMIT];
    size_t record_index;
    size_t field_index;
    size_t expr;
} ReadFact;

typedef struct {
    Token tokens[TOKEN_LIMIT];
    size_t token_count;
    size_t cursor;
    size_t condition_depth;
    size_t depth;

    Record records[RECORD_LIMIT];
    size_t record_count;
    Field fields[FIELD_LIMIT];
    size_t field_count;
    Enumeration enums[ENUM_LIMIT];
    size_t enum_count;
    Variant variants[VARIANT_LIMIT];
    size_t variant_count;
    Function functions[FUNCTION_LIMIT];
    size_t function_count;
    Param params[PARAM_LIMIT];
    size_t param_count;

    Expr exprs[EXPR_LIMIT];
    size_t expr_count;
    Stmt stmts[STMT_LIMIT];
    size_t stmt_count;
    Argument arguments[ARGUMENT_LIMIT];
    size_t argument_count;

    Local locals[LOCAL_LIMIT];
    size_t local_count;
    size_t current_function;

    ConstructFact constructs[EXPR_LIMIT / 8];
    size_t construct_count;
    ReadFact reads[EXPR_LIMIT / 8];
    size_t read_count;

    Binding bindings[LOCAL_LIMIT];
    size_t binding_count;
    size_t frame_base;
    size_t steps;
    size_t call_depth;
    bool returning;
    Value return_value;

    char error[1024];
    bool failed;
} Frontend;

static void set_error(
    Frontend *frontend,
    const char *code,
    size_t start,
    size_t end,
    const char *format,
    ...
) {
    char detail[768];
    va_list arguments;

    if (frontend->failed) return;
    va_start(arguments, format);
    if (vsnprintf(detail, sizeof(detail), format, arguments) < 0) {
        detail[0] = '\0';
    }
    va_end(arguments);
    snprintf(
        frontend->error,
        sizeof(frontend->error),
        "error[%s]: %s at bytes %zu..%zu",
        code,
        detail,
        start,
        end
    );
    frontend->failed = true;
}

/* --------------------------------------------------------------- lexing */

static bool copy_slice(
    Frontend *frontend,
    char *output,
    const char *source,
    size_t start,
    size_t end
) {
    size_t length = end - start;
    if (length == 0 || length >= TEXT_LIMIT) {
        set_error(
            frontend,
            "E2S106",
            start,
            end,
            "token exceeds the bounded record frontend length limit"
        );
        return false;
    }
    memcpy(output, source + start, length);
    output[length] = '\0';
    return true;
}

static bool add_token(
    Frontend *frontend,
    TokenKind kind,
    const char *source,
    size_t start,
    size_t end
) {
    Token *token;
    if (frontend->token_count >= TOKEN_LIMIT) {
        set_error(
            frontend,
            "E2S106",
            start,
            end,
            "token count exceeds %u",
            TOKEN_LIMIT
        );
        return false;
    }
    token = &frontend->tokens[frontend->token_count];
    token->kind = kind;
    token->start = start;
    token->end = end;
    if (kind == TOKEN_END) {
        token->text[0] = '\0';
    } else if (!copy_slice(frontend, token->text, source, start, end)) {
        return false;
    }
    frontend->token_count += 1;
    return true;
}

static bool is_double_punctuation(const char *source, size_t remaining) {
    static const char *pairs[] = {
        "->", "==", "!=", "<=", ">=", "&&", "||", "//"
    };
    size_t index;
    if (remaining < 2) return false;
    for (index = 0; index < sizeof(pairs) / sizeof(pairs[0]); index += 1) {
        if (source[0] == pairs[index][0] && source[1] == pairs[index][1]) {
            return true;
        }
    }
    return false;
}

static bool is_single_punctuation(char symbol) {
    return strchr("{}()[],:.=+-*%<>|!", symbol) != NULL;
}

static bool tokenize(Frontend *frontend, const char *source, size_t length) {
    size_t cursor = 0;
    while (cursor < length) {
        size_t start;
        unsigned char byte = (unsigned char)source[cursor];
        if (isspace(byte)) {
            cursor += 1;
            continue;
        }
        if (source[cursor] == '#') {
            while (cursor < length && source[cursor] != '\n') cursor += 1;
            continue;
        }
        start = cursor;
        if (isalpha(byte) || source[cursor] == '_') {
            cursor += 1;
            while (cursor < length) {
                byte = (unsigned char)source[cursor];
                if (!isalnum(byte) && source[cursor] != '_') break;
                cursor += 1;
            }
            if (!add_token(
                    frontend, TOKEN_IDENTIFIER, source, start, cursor)) {
                return false;
            }
            continue;
        }
        if (isdigit(byte)) {
            cursor += 1;
            while (cursor < length &&
                isdigit((unsigned char)source[cursor])) cursor += 1;
            if (!add_token(frontend, TOKEN_INTEGER, source, start, cursor)) {
                return false;
            }
            continue;
        }
        if (source[cursor] == '"') {
            cursor += 1;
            while (cursor < length && source[cursor] != '"') {
                if (source[cursor] == '\n') {
                    set_error(
                        frontend,
                        "E2S106",
                        start,
                        cursor,
                        "unterminated text literal"
                    );
                    return false;
                }
                if (source[cursor] == '\\' && cursor + 1 < length) cursor += 1;
                cursor += 1;
            }
            if (cursor >= length) {
                set_error(
                    frontend,
                    "E2S106",
                    start,
                    cursor,
                    "unterminated text literal"
                );
                return false;
            }
            cursor += 1;
            if (!add_token(frontend, TOKEN_TEXT, source, start, cursor)) {
                return false;
            }
            continue;
        }
        if (is_double_punctuation(source + cursor, length - cursor)) {
            cursor += 2;
            if (!add_token(
                    frontend, TOKEN_PUNCTUATION, source, start, cursor)) {
                return false;
            }
            continue;
        }
        if (is_single_punctuation(source[cursor])) {
            cursor += 1;
            if (!add_token(
                    frontend, TOKEN_PUNCTUATION, source, start, cursor)) {
                return false;
            }
            continue;
        }
        set_error(
            frontend,
            "E2S106",
            start,
            start + 1,
            "byte 0x%02x is outside the bounded record frontend alphabet",
            (unsigned)byte
        );
        return false;
    }
    return add_token(frontend, TOKEN_END, source, length, length);
}

/* -------------------------------------------------------- token helpers */

static const Token *peek(const Frontend *frontend, size_t offset) {
    size_t index = frontend->cursor + offset;
    if (index >= frontend->token_count) {
        index = frontend->token_count - 1;
    }
    return &frontend->tokens[index];
}

static bool at_end(const Frontend *frontend) {
    return peek(frontend, 0)->kind == TOKEN_END;
}

static bool at_punctuation(const Frontend *frontend, const char *text) {
    const Token *token = peek(frontend, 0);
    return token->kind == TOKEN_PUNCTUATION && strcmp(token->text, text) == 0;
}

static bool at_keyword(const Frontend *frontend, const char *text) {
    const Token *token = peek(frontend, 0);
    return token->kind == TOKEN_IDENTIFIER && strcmp(token->text, text) == 0;
}

static const Token *advance(Frontend *frontend) {
    const Token *token = peek(frontend, 0);
    if (token->kind != TOKEN_END) frontend->cursor += 1;
    return token;
}

static bool accept_punctuation(Frontend *frontend, const char *text) {
    if (!at_punctuation(frontend, text)) return false;
    advance(frontend);
    return true;
}

static bool accept_keyword(Frontend *frontend, const char *text) {
    if (!at_keyword(frontend, text)) return false;
    advance(frontend);
    return true;
}

static bool expect_punctuation(Frontend *frontend, const char *text) {
    const Token *token = peek(frontend, 0);
    if (accept_punctuation(frontend, text)) return true;
    set_error(
        frontend,
        "E2S107",
        token->start,
        token->end,
        "expected `%s` in the bounded record surface",
        text
    );
    return false;
}

static bool expect_identifier(Frontend *frontend, char *output) {
    const Token *token = peek(frontend, 0);
    if (token->kind != TOKEN_IDENTIFIER) {
        set_error(
            frontend,
            "E2S107",
            token->start,
            token->end,
            "expected an identifier in the bounded record surface"
        );
        return false;
    }
    memcpy(output, token->text, TEXT_LIMIT);
    advance(frontend);
    return true;
}

/* ------------------------------------------------------------ type names */

static const Record *record_at(const Frontend *frontend, size_t index) {
    return &frontend->records[index];
}

static void write_type_name(
    const Frontend *frontend,
    Type type,
    char *output,
    size_t capacity
) {
    switch (type.tag) {
        case TYPE_INT: snprintf(output, capacity, "Int"); return;
        case TYPE_TEXT: snprintf(output, capacity, "Text"); return;
        case TYPE_BOOL: snprintf(output, capacity, "Bool"); return;
        case TYPE_ENUM:
            snprintf(output, capacity, "%s", frontend->enums[type.decl].name);
            return;
        case TYPE_RECORD:
            snprintf(output, capacity, "%s", record_at(frontend, type.decl)->name);
            return;
        case TYPE_LIST: {
            Type element;
            char inner[TEXT_LIMIT];
            element.tag = type.element_tag;
            element.decl = type.element_decl;
            element.element_tag = TYPE_UNKNOWN;
            element.element_decl = NONE;
            write_type_name(frontend, element, inner, sizeof(inner));
            snprintf(output, capacity, "List[%.*s]",
                (int)(TEXT_LIMIT - 8), inner);
            return;
        }
        case TYPE_UNKNOWN:
        default:
            snprintf(output, capacity, "?");
            return;
    }
}

static bool same_type(Type left, Type right) {
    if (left.tag != right.tag) return false;
    if (left.tag == TYPE_ENUM || left.tag == TYPE_RECORD) {
        return left.decl == right.decl;
    }
    if (left.tag == TYPE_LIST) {
        if (left.element_tag == TYPE_UNKNOWN ||
            right.element_tag == TYPE_UNKNOWN) {
            return true;
        }
        if (left.element_tag != right.element_tag) return false;
        if (left.element_tag == TYPE_ENUM || left.element_tag == TYPE_RECORD) {
            return left.element_decl == right.element_decl;
        }
        return true;
    }
    return true;
}

static Type make_type(TypeTag tag, size_t decl) {
    Type type;
    type.tag = tag;
    type.decl = decl;
    type.element_tag = TYPE_UNKNOWN;
    type.element_decl = NONE;
    return type;
}

/* --------------------------------------------------------------- parsing */

static size_t new_expr(Frontend *frontend, ExprKind kind) {
    Expr *expr;
    size_t index = frontend->expr_count;
    if (index >= EXPR_LIMIT) {
        set_error(
            frontend, "E2S106", 0, 0,
            "expression count exceeds %u", EXPR_LIMIT);
        return NONE;
    }
    frontend->expr_count += 1;
    expr = &frontend->exprs[index];
    memset(expr, 0, sizeof(*expr));
    expr->kind = kind;
    expr->left = NONE;
    expr->right = NONE;
    expr->first_argument = NONE;
    expr->argument_count = 0;
    expr->type = make_type(TYPE_UNKNOWN, NONE);
    expr->name_kind = NAME_UNRESOLVED;
    expr->call_kind = CALL_UNRESOLVED;
    expr->decl = NONE;
    expr->field_index = NONE;
    return index;
}

static size_t new_stmt(Frontend *frontend, StmtKind kind) {
    Stmt *stmt;
    size_t index = frontend->stmt_count;
    if (index >= STMT_LIMIT) {
        set_error(
            frontend, "E2S106", 0, 0,
            "statement count exceeds %u", STMT_LIMIT);
        return NONE;
    }
    frontend->stmt_count += 1;
    stmt = &frontend->stmts[index];
    memset(stmt, 0, sizeof(*stmt));
    stmt->kind = kind;
    stmt->expr = NONE;
    stmt->target = NONE;
    stmt->then_first = NONE;
    stmt->else_first = NONE;
    stmt->body_first = NONE;
    stmt->next = NONE;
    stmt->type = make_type(TYPE_UNKNOWN, NONE);
    return index;
}

static bool parse_type_ref(Frontend *frontend, TypeRef *output) {
    const Token *token = peek(frontend, 0);
    memset(output, 0, sizeof(*output));
    output->start = token->start;
    if (!expect_identifier(frontend, output->name)) return false;
    output->end = frontend->tokens[frontend->cursor - 1].end;
    output->written = true;
    if (at_punctuation(frontend, "[")) {
        if (strcmp(output->name, "List") != 0) {
            set_error(
                frontend,
                "E2S111",
                output->start,
                peek(frontend, 0)->end,
                "generic type `%s` is unsupported in the v1 record surface",
                output->name
            );
            return false;
        }
        advance(frontend);
        if (!expect_identifier(frontend, output->element)) return false;
        if (!expect_punctuation(frontend, "]")) return false;
        output->is_list = true;
        output->end = frontend->tokens[frontend->cursor - 1].end;
    }
    return true;
}

static size_t parse_expression(Frontend *frontend);

/*
 * Sub-expressions append to the shared argument arena while they parse, so an
 * argument list is collected locally and only then copied into one contiguous
 * arena range.
 */
static bool commit_arguments(
    Frontend *frontend,
    const Argument *pending,
    size_t pending_count,
    size_t start,
    size_t end,
    size_t *first
) {
    size_t index;
    if (frontend->argument_count + pending_count > ARGUMENT_LIMIT) {
        set_error(
            frontend, "E2S106", start, end,
            "argument count exceeds %u", ARGUMENT_LIMIT);
        return false;
    }
    *first = frontend->argument_count;
    for (index = 0; index < pending_count; index += 1) {
        frontend->arguments[frontend->argument_count] = pending[index];
        frontend->argument_count += 1;
    }
    return true;
}

static bool parse_arguments(
    Frontend *frontend,
    const char *closing,
    bool allow_labels,
    size_t *first,
    size_t *count
) {
    Argument pending[ARGUMENT_GROUP_LIMIT];
    size_t pending_count = 0;
    size_t start = peek(frontend, 0)->start;

    memset(pending, 0, sizeof(pending));
    *first = NONE;
    *count = 0;
    while (!at_punctuation(frontend, closing)) {
        Argument *argument;
        const Token *token = peek(frontend, 0);
        size_t expression;
        if (pending_count >= ARGUMENT_GROUP_LIMIT) {
            set_error(
                frontend, "E2S106", token->start, token->end,
                "one argument list exceeds %u entries", ARGUMENT_GROUP_LIMIT);
            return false;
        }
        argument = &pending[pending_count];
        memset(argument, 0, sizeof(*argument));
        argument->start = token->start;
        if (allow_labels && token->kind == TOKEN_IDENTIFIER &&
            peek(frontend, 1)->kind == TOKEN_PUNCTUATION &&
            strcmp(peek(frontend, 1)->text, ":") == 0) {
            memcpy(argument->label, token->text, TEXT_LIMIT);
            argument->labelled = true;
            advance(frontend);
            advance(frontend);
        }
        expression = parse_expression(frontend);
        if (expression == NONE) return false;
        argument->expr = expression;
        argument->end = frontend->exprs[expression].end;
        pending_count += 1;
        if (!accept_punctuation(frontend, ",")) break;
        if (at_punctuation(frontend, closing)) break;
    }
    *count = pending_count;
    return commit_arguments(
        frontend, pending, pending_count, start,
        frontend->tokens[frontend->cursor - 1].end, first);
}

static size_t parse_primary(Frontend *frontend) {
    const Token *token = peek(frontend, 0);
    size_t index;

    if (token->kind == TOKEN_INTEGER) {
        index = new_expr(frontend, EXPR_INT);
        if (index == NONE) return NONE;
        frontend->exprs[index].integer = strtoll(token->text, NULL, 10);
        frontend->exprs[index].start = token->start;
        frontend->exprs[index].end = token->end;
        advance(frontend);
        return index;
    }
    if (token->kind == TOKEN_TEXT) {
        size_t position = 1;
        size_t written = 0;
        char decoded[TEXT_LIMIT];
        size_t raw_length = strlen(token->text);
        while (position + 1 < raw_length && written + 1 < TEXT_LIMIT) {
            char symbol = token->text[position];
            if (symbol == '\\' && position + 2 < raw_length) {
                position += 1;
                switch (token->text[position]) {
                    case 'n': symbol = '\n'; break;
                    case 't': symbol = '\t'; break;
                    case '\\': symbol = '\\'; break;
                    case '"': symbol = '"'; break;
                    default:
                        set_error(
                            frontend, "E2S106", token->start, token->end,
                            "unsupported escape `\\%c` in a text literal",
                            token->text[position]);
                        return NONE;
                }
            }
            decoded[written] = symbol;
            written += 1;
            position += 1;
        }
        decoded[written] = '\0';
        index = new_expr(frontend, EXPR_TEXT);
        if (index == NONE) return NONE;
        memcpy(frontend->exprs[index].text, decoded, written + 1);
        frontend->exprs[index].integer = (long long)written;
        frontend->exprs[index].start = token->start;
        frontend->exprs[index].end = token->end;
        advance(frontend);
        return index;
    }
    if (at_keyword(frontend, "true") || at_keyword(frontend, "false")) {
        index = new_expr(frontend, EXPR_BOOL);
        if (index == NONE) return NONE;
        frontend->exprs[index].boolean = strcmp(token->text, "true") == 0;
        frontend->exprs[index].start = token->start;
        frontend->exprs[index].end = token->end;
        advance(frontend);
        return index;
    }
    if (at_punctuation(frontend, "(")) {
        size_t inner;
        advance(frontend);
        inner = parse_expression(frontend);
        if (inner == NONE) return NONE;
        if (!expect_punctuation(frontend, ")")) return NONE;
        return inner;
    }
    if (at_punctuation(frontend, "[")) {
        size_t first;
        size_t count;
        size_t end;
        size_t saved_condition = frontend->condition_depth;
        advance(frontend);
        frontend->condition_depth = 0;
        if (!parse_arguments(frontend, "]", false, &first, &count)) {
            return NONE;
        }
        frontend->condition_depth = saved_condition;
        if (!expect_punctuation(frontend, "]")) return NONE;
        end = frontend->tokens[frontend->cursor - 1].end;
        index = new_expr(frontend, EXPR_LIST);
        if (index == NONE) return NONE;
        frontend->exprs[index].first_argument = first;
        frontend->exprs[index].argument_count = count;
        frontend->exprs[index].start = token->start;
        frontend->exprs[index].end = end;
        return index;
    }
    if (at_punctuation(frontend, "{")) {
        set_error(
            frontend,
            "E2S125",
            token->start,
            token->end,
            "`{` never starts an expression in the v1 record surface; "
            "map literals are not accepted and record construction is "
            "written `Name(field: value)`"
        );
        return NONE;
    }
    if (token->kind == TOKEN_IDENTIFIER) {
        char name[TEXT_LIMIT];
        memcpy(name, token->text, TEXT_LIMIT);
        advance(frontend);
        if (at_punctuation(frontend, "{") && frontend->condition_depth == 0) {
            set_error(
                frontend,
                "E2S119",
                token->start,
                peek(frontend, 0)->end,
                "`%s { ... }` is not record construction; write "
                "`%s(field: value)`",
                name,
                name
            );
            return NONE;
        }
        index = new_expr(frontend, EXPR_NAME);
        if (index == NONE) return NONE;
        memcpy(frontend->exprs[index].text, name, TEXT_LIMIT);
        frontend->exprs[index].start = token->start;
        frontend->exprs[index].end = token->end;
        return index;
    }
    set_error(
        frontend,
        "E2S107",
        token->start,
        token->end,
        "unexpected token `%s` in the bounded record surface",
        token->kind == TOKEN_END ? "<end of file>" : token->text
    );
    return NONE;
}

static size_t parse_postfix(Frontend *frontend) {
    size_t value = parse_primary(frontend);
    if (value == NONE) return NONE;
    for (;;) {
        if (at_punctuation(frontend, ".")) {
            size_t index;
            char field[TEXT_LIMIT];
            size_t start = frontend->exprs[value].start;
            advance(frontend);
            if (!expect_identifier(frontend, field)) return NONE;
            index = new_expr(frontend, EXPR_FIELD);
            if (index == NONE) return NONE;
            memcpy(frontend->exprs[index].text, field, TEXT_LIMIT);
            frontend->exprs[index].left = value;
            frontend->exprs[index].start = start;
            frontend->exprs[index].end =
                frontend->tokens[frontend->cursor - 1].end;
            value = index;
            continue;
        }
        if (at_punctuation(frontend, "(")) {
            size_t first;
            size_t count;
            size_t index;
            size_t saved_condition = frontend->condition_depth;
            if (frontend->exprs[value].kind != EXPR_NAME) {
                set_error(
                    frontend,
                    "E2S107",
                    frontend->exprs[value].start,
                    frontend->exprs[value].end,
                    "only a named function or record type may be called"
                );
                return NONE;
            }
            advance(frontend);
            frontend->condition_depth = 0;
            if (!parse_arguments(frontend, ")", true, &first, &count)) {
                return NONE;
            }
            frontend->condition_depth = saved_condition;
            if (!expect_punctuation(frontend, ")")) return NONE;
            index = new_expr(frontend, EXPR_CALL);
            if (index == NONE) return NONE;
            memcpy(
                frontend->exprs[index].text,
                frontend->exprs[value].text,
                TEXT_LIMIT
            );
            frontend->exprs[index].first_argument = first;
            frontend->exprs[index].argument_count = count;
            frontend->exprs[index].start = frontend->exprs[value].start;
            frontend->exprs[index].end =
                frontend->tokens[frontend->cursor - 1].end;
            value = index;
            continue;
        }
        if (at_punctuation(frontend, "[")) {
            size_t subscript;
            size_t index;
            size_t saved_condition = frontend->condition_depth;
            advance(frontend);
            frontend->condition_depth = 0;
            subscript = parse_expression(frontend);
            frontend->condition_depth = saved_condition;
            if (subscript == NONE) return NONE;
            if (!expect_punctuation(frontend, "]")) return NONE;
            index = new_expr(frontend, EXPR_INDEX);
            if (index == NONE) return NONE;
            frontend->exprs[index].left = value;
            frontend->exprs[index].right = subscript;
            frontend->exprs[index].start = frontend->exprs[value].start;
            frontend->exprs[index].end =
                frontend->tokens[frontend->cursor - 1].end;
            value = index;
            continue;
        }
        break;
    }
    return value;
}

static size_t parse_unary(Frontend *frontend) {
    if (at_punctuation(frontend, "-") || at_punctuation(frontend, "!")) {
        const Token *token = peek(frontend, 0);
        char symbol[8];
        size_t operand;
        size_t index;
        snprintf(symbol, sizeof(symbol), "%.7s", token->text);
        advance(frontend);
        operand = parse_unary(frontend);
        if (operand == NONE) return NONE;
        index = new_expr(frontend, EXPR_UNARY);
        if (index == NONE) return NONE;
        memcpy(frontend->exprs[index].operator_text, symbol, sizeof(symbol));
        frontend->exprs[index].left = operand;
        frontend->exprs[index].start = token->start;
        frontend->exprs[index].end = frontend->exprs[operand].end;
        return index;
    }
    return parse_postfix(frontend);
}

typedef struct {
    const char *symbols[4];
    size_t count;
} Level;

static const Level LEVELS[] = {
    {{"||", NULL, NULL, NULL}, 1},
    {{"&&", NULL, NULL, NULL}, 1},
    {{"==", "!=", NULL, NULL}, 2},
    {{"<", "<=", ">", ">="}, 4},
    {{"+", "-", NULL, NULL}, 2},
    {{"*", "//", "%", NULL}, 3}
};

#define LEVEL_COUNT (sizeof(LEVELS) / sizeof(LEVELS[0]))

static size_t parse_binary(Frontend *frontend, size_t level) {
    size_t left;
    if (level >= LEVEL_COUNT) return parse_unary(frontend);
    if (frontend->depth >= DEPTH_LIMIT) {
        const Token *token = peek(frontend, 0);
        set_error(
            frontend, "E2S106", token->start, token->end,
            "expression nesting exceeds %u", DEPTH_LIMIT);
        return NONE;
    }
    frontend->depth += 1;
    left = parse_binary(frontend, level + 1);
    frontend->depth -= 1;
    if (left == NONE) return NONE;
    for (;;) {
        size_t choice;
        bool matched = false;
        char symbol[8];
        size_t right;
        size_t index;
        for (choice = 0; choice < LEVELS[level].count; choice += 1) {
            if (at_punctuation(frontend, LEVELS[level].symbols[choice])) {
                snprintf(symbol, sizeof(symbol), "%s",
                    LEVELS[level].symbols[choice]);
                matched = true;
                break;
            }
        }
        if (!matched) break;
        advance(frontend);
        frontend->depth += 1;
        right = parse_binary(frontend, level + 1);
        frontend->depth -= 1;
        if (right == NONE) return NONE;
        index = new_expr(frontend, EXPR_BINARY);
        if (index == NONE) return NONE;
        memcpy(frontend->exprs[index].operator_text, symbol, sizeof(symbol));
        frontend->exprs[index].left = left;
        frontend->exprs[index].right = right;
        frontend->exprs[index].start = frontend->exprs[left].start;
        frontend->exprs[index].end = frontend->exprs[right].end;
        left = index;
    }
    return left;
}

static size_t parse_expression(Frontend *frontend) {
    return parse_binary(frontend, 0);
}

static size_t parse_condition(Frontend *frontend) {
    size_t condition;
    frontend->condition_depth += 1;
    condition = parse_expression(frontend);
    frontend->condition_depth -= 1;
    return condition;
}

static size_t parse_block(Frontend *frontend);
static size_t parse_statement(Frontend *frontend);

static size_t parse_if(Frontend *frontend) {
    const Token *token = peek(frontend, 0);
    size_t index;
    size_t condition;
    size_t then_first;
    advance(frontend);
    condition = parse_condition(frontend);
    if (condition == NONE) return NONE;
    then_first = parse_block(frontend);
    if (frontend->failed) return NONE;
    index = new_stmt(frontend, STMT_IF);
    if (index == NONE) return NONE;
    frontend->stmts[index].expr = condition;
    frontend->stmts[index].then_first = then_first;
    frontend->stmts[index].start = token->start;
    frontend->stmts[index].end = frontend->tokens[frontend->cursor - 1].end;
    if (accept_keyword(frontend, "else")) {
        if (at_keyword(frontend, "if")) {
            size_t nested = parse_if(frontend);
            if (nested == NONE) return NONE;
            frontend->stmts[index].else_first = nested;
        } else {
            size_t else_first = parse_block(frontend);
            if (frontend->failed) return NONE;
            frontend->stmts[index].else_first = else_first;
        }
        frontend->stmts[index].end =
            frontend->tokens[frontend->cursor - 1].end;
    }
    return index;
}

static size_t parse_statement(Frontend *frontend) {
    const Token *token = peek(frontend, 0);
    size_t index;

    if (at_keyword(frontend, "let")) {
        size_t value;
        advance(frontend);
        index = new_stmt(frontend, STMT_LET);
        if (index == NONE) return NONE;
        frontend->stmts[index].is_mut = accept_keyword(frontend, "mut");
        if (!expect_identifier(frontend, frontend->stmts[index].name)) {
            return NONE;
        }
        if (accept_punctuation(frontend, ":")) {
            if (!parse_type_ref(
                    frontend, &frontend->stmts[index].written_type)) {
                return NONE;
            }
            frontend->stmts[index].has_annotation = true;
        }
        if (!expect_punctuation(frontend, "=")) return NONE;
        value = parse_expression(frontend);
        if (value == NONE) return NONE;
        frontend->stmts[index].expr = value;
        frontend->stmts[index].start = token->start;
        frontend->stmts[index].end = frontend->exprs[value].end;
        return index;
    }
    if (at_keyword(frontend, "return")) {
        size_t value;
        advance(frontend);
        value = parse_expression(frontend);
        if (value == NONE) return NONE;
        index = new_stmt(frontend, STMT_RETURN);
        if (index == NONE) return NONE;
        frontend->stmts[index].expr = value;
        frontend->stmts[index].start = token->start;
        frontend->stmts[index].end = frontend->exprs[value].end;
        return index;
    }
    if (at_keyword(frontend, "if")) {
        return parse_if(frontend);
    }
    if (at_keyword(frontend, "while")) {
        size_t condition;
        size_t body;
        advance(frontend);
        condition = parse_condition(frontend);
        if (condition == NONE) return NONE;
        body = parse_block(frontend);
        if (frontend->failed) return NONE;
        index = new_stmt(frontend, STMT_WHILE);
        if (index == NONE) return NONE;
        frontend->stmts[index].expr = condition;
        frontend->stmts[index].body_first = body;
        frontend->stmts[index].start = token->start;
        frontend->stmts[index].end = frontend->tokens[frontend->cursor - 1].end;
        return index;
    }
    if (at_keyword(frontend, "for")) {
        size_t iterable;
        size_t body;
        char name[TEXT_LIMIT];
        advance(frontend);
        if (!expect_identifier(frontend, name)) return NONE;
        if (!accept_keyword(frontend, "in")) {
            set_error(
                frontend, "E2S107", peek(frontend, 0)->start,
                peek(frontend, 0)->end, "expected `in` after the loop binding");
            return NONE;
        }
        iterable = parse_condition(frontend);
        if (iterable == NONE) return NONE;
        body = parse_block(frontend);
        if (frontend->failed) return NONE;
        index = new_stmt(frontend, STMT_FOR);
        if (index == NONE) return NONE;
        memcpy(frontend->stmts[index].name, name, TEXT_LIMIT);
        frontend->stmts[index].expr = iterable;
        frontend->stmts[index].body_first = body;
        frontend->stmts[index].start = token->start;
        frontend->stmts[index].end = frontend->tokens[frontend->cursor - 1].end;
        return index;
    }
    if (at_keyword(frontend, "take")) {
        size_t target;
        advance(frontend);
        target = parse_expression(frontend);
        if (target == NONE) return NONE;
        index = new_stmt(frontend, STMT_TAKE);
        if (index == NONE) return NONE;
        frontend->stmts[index].target = target;
        frontend->stmts[index].start = token->start;
        frontend->stmts[index].end = frontend->exprs[target].end;
        return index;
    }
    {
        size_t value = parse_expression(frontend);
        if (value == NONE) return NONE;
        if (at_punctuation(frontend, "=")) {
            size_t assigned;
            advance(frontend);
            assigned = parse_expression(frontend);
            if (assigned == NONE) return NONE;
            index = new_stmt(frontend, STMT_ASSIGN);
            if (index == NONE) return NONE;
            frontend->stmts[index].target = value;
            frontend->stmts[index].expr = assigned;
            frontend->stmts[index].start = frontend->exprs[value].start;
            frontend->stmts[index].end = frontend->exprs[assigned].end;
            return index;
        }
        index = new_stmt(frontend, STMT_EXPR);
        if (index == NONE) return NONE;
        frontend->stmts[index].expr = value;
        frontend->stmts[index].start = frontend->exprs[value].start;
        frontend->stmts[index].end = frontend->exprs[value].end;
        return index;
    }
}

static size_t parse_block(Frontend *frontend) {
    size_t first = NONE;
    size_t last = NONE;
    if (!expect_punctuation(frontend, "{")) return NONE;
    while (!at_punctuation(frontend, "}")) {
        size_t statement;
        if (at_end(frontend)) {
            set_error(
                frontend, "E2S107", peek(frontend, 0)->start,
                peek(frontend, 0)->end, "unterminated block");
            return NONE;
        }
        statement = parse_statement(frontend);
        if (statement == NONE) return NONE;
        if (first == NONE) {
            first = statement;
        } else {
            frontend->stmts[last].next = statement;
        }
        last = statement;
    }
    if (!expect_punctuation(frontend, "}")) return NONE;
    return first;
}

static bool parse_record_declaration(Frontend *frontend, size_t start) {
    Record *record;
    char name[TEXT_LIMIT];
    size_t index;
    size_t name_start;
    size_t name_end;

    if (!expect_identifier(frontend, name)) return false;
    name_start = frontend->tokens[frontend->cursor - 1].start;
    name_end = frontend->tokens[frontend->cursor - 1].end;
    if (at_punctuation(frontend, "[")) {
        set_error(
            frontend,
            "E2S111",
            start,
            peek(frontend, 0)->end,
            "generic record `%s` is unsupported in the v1 record surface",
            name
        );
        return false;
    }
    if (!expect_punctuation(frontend, "=")) return false;
    if (at_punctuation(frontend, "|")) {
        /* flat payload-free enumeration used by record field types */
        Enumeration *enumeration;
        if (frontend->enum_count >= ENUM_LIMIT) {
            set_error(frontend, "E2S106", start, start + 1,
                "enumeration count exceeds %u", ENUM_LIMIT);
            return false;
        }
        for (index = 0; index < frontend->enum_count; index += 1) {
            if (strcmp(frontend->enums[index].name, name) == 0) {
                set_error(
                    frontend, "E2S108", name_start, name_end,
                    "duplicate type `%s`; first declared at bytes %zu..%zu",
                    name, frontend->enums[index].start,
                    frontend->enums[index].end);
                return false;
            }
        }
        for (index = 0; index < frontend->record_count; index += 1) {
            if (strcmp(frontend->records[index].name, name) == 0) {
                set_error(
                    frontend, "E2S108", name_start, name_end,
                    "duplicate type `%s`; first declared at bytes %zu..%zu",
                    name, frontend->records[index].start,
                    frontend->records[index].end);
                return false;
            }
        }
        enumeration = &frontend->enums[frontend->enum_count];
        memset(enumeration, 0, sizeof(*enumeration));
        memcpy(enumeration->name, name, TEXT_LIMIT);
        enumeration->first_variant = frontend->variant_count;
        enumeration->start = start;
        while (accept_punctuation(frontend, "|")) {
            Variant *variant;
            const Token *token = peek(frontend, 0);
            char variant_name[TEXT_LIMIT];
            size_t existing;
            if (!expect_identifier(frontend, variant_name)) return false;
            if (at_punctuation(frontend, "(")) {
                set_error(
                    frontend, "E2S107", token->start, peek(frontend, 0)->end,
                    "constructor payloads are outside this gate; "
                    "`%s` must be a flat variant", variant_name);
                return false;
            }
            if (frontend->variant_count >= VARIANT_LIMIT) {
                set_error(frontend, "E2S106", token->start, token->end,
                    "variant count exceeds %u", VARIANT_LIMIT);
                return false;
            }
            for (existing = 0; existing < frontend->variant_count;
                existing += 1) {
                if (strcmp(frontend->variants[existing].name,
                        variant_name) == 0) {
                    set_error(
                        frontend, "E2S108", token->start, token->end,
                        "duplicate variant `%s`; first declared at "
                        "bytes %zu..%zu", variant_name,
                        frontend->variants[existing].start,
                        frontend->variants[existing].end);
                    return false;
                }
            }
            variant = &frontend->variants[frontend->variant_count];
            memset(variant, 0, sizeof(*variant));
            memcpy(variant->name, variant_name, TEXT_LIMIT);
            variant->enum_index = frontend->enum_count;
            variant->local_index = enumeration->variant_count;
            variant->start = token->start;
            variant->end = token->end;
            frontend->variant_count += 1;
            enumeration->variant_count += 1;
        }
        if (enumeration->variant_count < 2) {
            set_error(
                frontend, "E2S107", start,
                frontend->tokens[frontend->cursor - 1].end,
                "enumeration `%s` needs at least two flat variants", name);
            return false;
        }
        enumeration->end = frontend->tokens[frontend->cursor - 1].end;
        frontend->enum_count += 1;
        return true;
    }
    if (!at_punctuation(frontend, "{")) {
        set_error(
            frontend,
            "E2S107",
            peek(frontend, 0)->start,
            peek(frontend, 0)->end,
            "expected `{` to open the record body of `%s`",
            name
        );
        return false;
    }
    advance(frontend);
    if (frontend->record_count >= RECORD_LIMIT) {
        set_error(frontend, "E2S106", start, start + 1,
            "record count exceeds %u", RECORD_LIMIT);
        return false;
    }
    for (index = 0; index < frontend->record_count; index += 1) {
        if (strcmp(frontend->records[index].name, name) == 0) {
            set_error(
                frontend, "E2S108", name_start, name_end,
                "duplicate type `%s`; first declared at bytes %zu..%zu",
                name, frontend->records[index].start,
                frontend->records[index].end);
            return false;
        }
    }
    for (index = 0; index < frontend->enum_count; index += 1) {
        if (strcmp(frontend->enums[index].name, name) == 0) {
            set_error(
                frontend, "E2S108", name_start, name_end,
                "duplicate type `%s`; first declared at bytes %zu..%zu",
                name, frontend->enums[index].start,
                frontend->enums[index].end);
            return false;
        }
    }
    record = &frontend->records[frontend->record_count];
    memset(record, 0, sizeof(*record));
    memcpy(record->name, name, TEXT_LIMIT);
    record->first_field = frontend->field_count;
    record->start = start;
    while (!at_punctuation(frontend, "}")) {
        Field *field;
        const Token *token = peek(frontend, 0);
        char field_name[TEXT_LIMIT];
        size_t existing;
        if (at_end(frontend)) {
            set_error(frontend, "E2S107", token->start, token->end,
                "unterminated record body for `%s`", name);
            return false;
        }
        if (!expect_identifier(frontend, field_name)) return false;
        for (existing = record->first_field;
            existing < record->first_field + record->field_count;
            existing += 1) {
            if (strcmp(frontend->fields[existing].name, field_name) == 0) {
                set_error(
                    frontend,
                    "E2S109",
                    token->start,
                    token->end,
                    "duplicate field `%s` in record `%s`; first declared at "
                    "bytes %zu..%zu",
                    field_name,
                    name,
                    frontend->fields[existing].start,
                    frontend->fields[existing].end
                );
                return false;
            }
        }
        if (!expect_punctuation(frontend, ":")) return false;
        if (frontend->field_count >= FIELD_LIMIT) {
            set_error(frontend, "E2S106", token->start, token->end,
                "field count exceeds %u", FIELD_LIMIT);
            return false;
        }
        field = &frontend->fields[frontend->field_count];
        memset(field, 0, sizeof(*field));
        memcpy(field->name, field_name, TEXT_LIMIT);
        field->start = token->start;
        if (!parse_type_ref(frontend, &field->written_type)) return false;
        field->end = frontend->tokens[frontend->cursor - 1].end;
        frontend->field_count += 1;
        record->field_count += 1;
        if (!accept_punctuation(frontend, ",")) break;
    }
    if (!expect_punctuation(frontend, "}")) return false;
    if (record->field_count == 0) {
        set_error(
            frontend, "E2S107", start,
            frontend->tokens[frontend->cursor - 1].end,
            "record `%s` needs at least one field", name);
        return false;
    }
    record->end = frontend->tokens[frontend->cursor - 1].end;
    frontend->record_count += 1;
    return true;
}

static bool parse_function(Frontend *frontend, size_t start) {
    Function *function;
    char name[TEXT_LIMIT];
    size_t index;
    size_t name_start;
    size_t name_end;

    if (!expect_identifier(frontend, name)) return false;
    name_start = frontend->tokens[frontend->cursor - 1].start;
    name_end = frontend->tokens[frontend->cursor - 1].end;
    if (frontend->function_count >= FUNCTION_LIMIT) {
        set_error(frontend, "E2S106", start, start + 1,
            "function count exceeds %u", FUNCTION_LIMIT);
        return false;
    }
    for (index = 0; index < frontend->function_count; index += 1) {
        if (strcmp(frontend->functions[index].name, name) == 0) {
            set_error(
                frontend, "E2S108", name_start, name_end,
                "duplicate function `%s`; first declared at bytes %zu..%zu",
                name, frontend->functions[index].start,
                frontend->functions[index].end);
            return false;
        }
    }
    function = &frontend->functions[frontend->function_count];
    memset(function, 0, sizeof(*function));
    memcpy(function->name, name, TEXT_LIMIT);
    function->first_param = frontend->param_count;
    function->start = start;
    function->body_first = NONE;
    if (!expect_punctuation(frontend, "(")) return false;
    while (!at_punctuation(frontend, ")")) {
        Param *param;
        const Token *token = peek(frontend, 0);
        if (at_end(frontend)) {
            set_error(frontend, "E2S107", token->start, token->end,
                "unterminated parameter list for `%s`", name);
            return false;
        }
        if (frontend->param_count >= PARAM_LIMIT) {
            set_error(frontend, "E2S106", token->start, token->end,
                "parameter count exceeds %u", PARAM_LIMIT);
            return false;
        }
        param = &frontend->params[frontend->param_count];
        memset(param, 0, sizeof(*param));
        param->mode = MODE_VALUE;
        param->start = token->start;
        if (accept_keyword(frontend, "read")) {
            param->mode = MODE_READ;
        } else if (accept_keyword(frontend, "take")) {
            param->mode = MODE_TAKE;
        } else if (at_keyword(frontend, "edit")) {
            set_error(
                frontend,
                "E2S121",
                token->start,
                peek(frontend, 0)->end,
                "`edit` access to a record is unsupported in v1; record "
                "fields are immutable"
            );
            return false;
        }
        if (!expect_identifier(frontend, param->name)) return false;
        if (!expect_punctuation(frontend, ":")) return false;
        if (!parse_type_ref(frontend, &param->written_type)) return false;
        param->end = frontend->tokens[frontend->cursor - 1].end;
        frontend->param_count += 1;
        function->param_count += 1;
        if (!accept_punctuation(frontend, ",")) break;
    }
    if (!expect_punctuation(frontend, ")")) return false;
    if (accept_punctuation(frontend, "->")) {
        if (!parse_type_ref(frontend, &function->written_return)) return false;
        function->has_return = true;
    }
    function->body_first = parse_block(frontend);
    if (frontend->failed) return false;
    function->end = frontend->tokens[frontend->cursor - 1].end;
    frontend->function_count += 1;
    return true;
}

static bool parse_program(Frontend *frontend) {
    while (!at_end(frontend)) {
        const Token *token = peek(frontend, 0);
        if (at_keyword(frontend, "type")) {
            advance(frontend);
            if (!parse_record_declaration(frontend, token->start)) return false;
            continue;
        }
        if (at_keyword(frontend, "fn")) {
            advance(frontend);
            if (!parse_function(frontend, token->start)) return false;
            continue;
        }
        set_error(
            frontend,
            "E2S107",
            token->start,
            token->end,
            "expected `type` or `fn` at the top level, found `%s`",
            token->text
        );
        return false;
    }
    if (frontend->record_count == 0) {
        set_error(frontend, "E2S107", 0, 0,
            "the record gate requires at least one record declaration");
        return false;
    }
    return true;
}

/* ------------------------------------------------------ type resolution */

static bool resolve_type_ref(
    Frontend *frontend,
    const TypeRef *written,
    Type *output
) {
    const char *name = written->is_list ? written->element : written->name;
    size_t index;
    Type resolved = make_type(TYPE_UNKNOWN, NONE);

    if (strcmp(name, "Int") == 0) {
        resolved = make_type(TYPE_INT, NONE);
    } else if (strcmp(name, "Text") == 0) {
        resolved = make_type(TYPE_TEXT, NONE);
    } else if (strcmp(name, "Bool") == 0) {
        resolved = make_type(TYPE_BOOL, NONE);
    } else {
        for (index = 0; index < frontend->record_count; index += 1) {
            if (strcmp(frontend->records[index].name, name) == 0) {
                resolved = make_type(TYPE_RECORD, index);
                break;
            }
        }
        if (resolved.tag == TYPE_UNKNOWN) {
            for (index = 0; index < frontend->enum_count; index += 1) {
                if (strcmp(frontend->enums[index].name, name) == 0) {
                    resolved = make_type(TYPE_ENUM, index);
                    break;
                }
            }
        }
    }
    if (resolved.tag == TYPE_UNKNOWN) {
        set_error(
            frontend,
            "E2S110",
            written->start,
            written->end,
            "unknown type `%s`",
            name
        );
        return false;
    }
    if (written->is_list) {
        Type list = make_type(TYPE_LIST, NONE);
        list.element_tag = resolved.tag;
        list.element_decl = resolved.decl;
        *output = list;
        return true;
    }
    *output = resolved;
    return true;
}

static bool resolve_declarations(Frontend *frontend) {
    size_t index;
    for (index = 0; index < frontend->field_count; index += 1) {
        Field *field = &frontend->fields[index];
        if (!resolve_type_ref(frontend, &field->written_type, &field->type)) {
            return false;
        }
    }
    for (index = 0; index < frontend->param_count; index += 1) {
        Param *param = &frontend->params[index];
        if (!resolve_type_ref(frontend, &param->written_type, &param->type)) {
            return false;
        }
    }
    for (index = 0; index < frontend->function_count; index += 1) {
        Function *function = &frontend->functions[index];
        function->return_type = make_type(TYPE_UNKNOWN, NONE);
        if (function->has_return &&
            !resolve_type_ref(
                frontend, &function->written_return,
                &function->return_type)) {
            return false;
        }
    }
    return true;
}

/* --------------------------------------------------------------- layout */

static size_t round_up(size_t value, size_t alignment) {
    size_t remainder;
    if (alignment <= 1) return value;
    remainder = value % alignment;
    if (remainder == 0) return value;
    return value + (alignment - remainder);
}

static bool layout_record(Frontend *frontend, size_t record_index);

static bool scalar_layout(
    Frontend *frontend,
    Type type,
    size_t target,
    size_t *size,
    size_t *align
) {
    const Target *profile = &TARGETS[target];
    switch (type.tag) {
        case TYPE_INT:
            *size = profile->int_size;
            *align = profile->int_align;
            return true;
        case TYPE_BOOL:
            *size = profile->bool_size;
            *align = profile->bool_align;
            return true;
        case TYPE_ENUM:
            *size = profile->tag_size;
            *align = profile->tag_align;
            return true;
        case TYPE_TEXT:
        case TYPE_LIST:
            *size = profile->pointer_size * 2;
            *align = profile->pointer_align;
            return true;
        case TYPE_RECORD:
            if (!layout_record(frontend, type.decl)) return false;
            *size = frontend->records[type.decl].size[target];
            *align = frontend->records[type.decl].align[target];
            return true;
        case TYPE_UNKNOWN:
        default:
            set_error(frontend, "E2S110", 0, 0, "unresolved field type");
            return false;
    }
}

static bool layout_record(Frontend *frontend, size_t record_index) {
    Record *record = &frontend->records[record_index];
    size_t target;
    size_t index;

    if (record->laid_out) return true;
    if (record->laying_out) {
        set_error(
            frontend,
            "E2S112",
            record->start,
            record->end,
            "recursive record `%s` is unsupported in the v1 record surface",
            record->name
        );
        return false;
    }
    record->laying_out = true;
    for (target = 0; target < TARGET_COUNT; target += 1) {
        size_t cursor = 0;
        size_t alignment = 1;
        for (index = 0; index < record->field_count; index += 1) {
            Field *field = &frontend->fields[record->first_field + index];
            size_t size;
            size_t align;
            if (!scalar_layout(frontend, field->type, target, &size, &align)) {
                return false;
            }
            field->size[target] = size;
            field->align[target] = align;
            field->offset[target] = round_up(cursor, align);
            cursor = field->offset[target] + size;
            if (align > alignment) alignment = align;
        }
        record->align[target] = alignment;
        record->size[target] = round_up(cursor, alignment);
    }
    record->laying_out = false;
    record->laid_out = true;
    return true;
}

static bool layout_all(Frontend *frontend) {
    size_t index;
    for (index = 0; index < frontend->record_count; index += 1) {
        if (!layout_record(frontend, index)) return false;
    }
    return true;
}

/* ------------------------------------------------------------- checking */

typedef struct {
    const char *name;
    size_t arity;
} Builtin;

static const Builtin BUILTINS[] = {
    {"len", 1},
    {"char_code", 2},
    {"slice", 3},
    {"count", 1},
    {"push", 2}
};

#define BUILTIN_COUNT (sizeof(BUILTINS) / sizeof(BUILTINS[0]))

static size_t find_builtin(const char *name) {
    size_t index;
    for (index = 0; index < BUILTIN_COUNT; index += 1) {
        if (strcmp(BUILTINS[index].name, name) == 0) return index;
    }
    return NONE;
}

static size_t find_local(const Frontend *frontend, const char *name) {
    size_t index = frontend->local_count;
    while (index > 0) {
        index -= 1;
        if (strcmp(frontend->locals[index].name, name) == 0) return index;
    }
    return NONE;
}

static size_t find_record(const Frontend *frontend, const char *name) {
    size_t index;
    for (index = 0; index < frontend->record_count; index += 1) {
        if (strcmp(frontend->records[index].name, name) == 0) return index;
    }
    return NONE;
}

static size_t find_function(const Frontend *frontend, const char *name) {
    size_t index;
    for (index = 0; index < frontend->function_count; index += 1) {
        if (strcmp(frontend->functions[index].name, name) == 0) return index;
    }
    return NONE;
}

static size_t find_variant(const Frontend *frontend, const char *name) {
    size_t index;
    for (index = 0; index < frontend->variant_count; index += 1) {
        if (strcmp(frontend->variants[index].name, name) == 0) return index;
    }
    return NONE;
}

static bool push_local(
    Frontend *frontend,
    const char *name,
    Type type,
    bool is_mut,
    bool borrowed,
    size_t start,
    size_t end
) {
    Local *local;
    if (frontend->local_count >= LOCAL_LIMIT) {
        set_error(frontend, "E2S106", start, end,
            "local binding count exceeds %u", LOCAL_LIMIT);
        return false;
    }
    local = &frontend->locals[frontend->local_count];
    memset(local, 0, sizeof(*local));
    memcpy(local->name, name, TEXT_LIMIT);
    local->type = type;
    local->is_mut = is_mut;
    local->moved = false;
    local->borrowed = borrowed;
    frontend->local_count += 1;
    return true;
}

static bool check_expr(Frontend *frontend, size_t expr_index, Type *output);

static bool record_construct_fact(
    Frontend *frontend,
    size_t record_index,
    size_t expr_index
) {
    ConstructFact *fact;
    if (frontend->construct_count >= EXPR_LIMIT / 8) return true;
    fact = &frontend->constructs[frontend->construct_count];
    memset(fact, 0, sizeof(*fact));
    memcpy(
        fact->function_name,
        frontend->functions[frontend->current_function].name,
        TEXT_LIMIT
    );
    fact->record_index = record_index;
    fact->expr = expr_index;
    frontend->construct_count += 1;
    return true;
}

static bool record_read_fact(
    Frontend *frontend,
    size_t record_index,
    size_t field_index,
    size_t expr_index
) {
    ReadFact *fact;
    if (frontend->read_count >= EXPR_LIMIT / 8) return true;
    fact = &frontend->reads[frontend->read_count];
    memset(fact, 0, sizeof(*fact));
    memcpy(
        fact->function_name,
        frontend->functions[frontend->current_function].name,
        TEXT_LIMIT
    );
    fact->record_index = record_index;
    fact->field_index = field_index;
    fact->expr = expr_index;
    frontend->read_count += 1;
    return true;
}

static bool check_construction(
    Frontend *frontend,
    size_t expr_index,
    size_t record_index
) {
    Expr *expr = &frontend->exprs[expr_index];
    const Record *record = &frontend->records[record_index];
    size_t seen[FIELD_LIMIT];
    size_t index;
    size_t argument;

    for (index = 0; index < record->field_count; index += 1) seen[index] = NONE;
    for (argument = 0; argument < expr->argument_count; argument += 1) {
        Argument *item = &frontend->arguments[expr->first_argument + argument];
        size_t field_index = NONE;
        Type actual;
        Type expected;
        char expected_name[TEXT_LIMIT];
        char actual_name[TEXT_LIMIT];
        if (!item->labelled) {
            set_error(
                frontend,
                "E2S118",
                item->start,
                item->end,
                "record `%s` is constructed with labelled fields; write "
                "`%s(field: value)`",
                record->name,
                record->name
            );
            return false;
        }
        for (index = 0; index < record->field_count; index += 1) {
            if (strcmp(
                    frontend->fields[record->first_field + index].name,
                    item->label) == 0) {
                field_index = index;
                break;
            }
        }
        if (field_index == NONE) {
            set_error(
                frontend,
                "E2S116",
                item->start,
                item->end,
                "record `%s` has no field `%s`",
                record->name,
                item->label
            );
            return false;
        }
        if (seen[field_index] != NONE) {
            set_error(
                frontend,
                "E2S114",
                item->start,
                item->end,
                "duplicate field `%s` in `%s` construction; first supplied at "
                "bytes %zu..%zu",
                item->label,
                record->name,
                frontend->arguments[seen[field_index]].start,
                frontend->arguments[seen[field_index]].end
            );
            return false;
        }
        seen[field_index] = expr->first_argument + argument;
        if (!check_expr(frontend, item->expr, &actual)) return false;
        expected = frontend->fields[record->first_field + field_index].type;
        if (!same_type(expected, actual)) {
            write_type_name(
                frontend, expected, expected_name, sizeof(expected_name));
            write_type_name(
                frontend, actual, actual_name, sizeof(actual_name));
            set_error(
                frontend,
                "E2S117",
                item->start,
                item->end,
                "field `%s` of record `%s` expects `%s`, found `%s`",
                item->label,
                record->name,
                expected_name,
                actual_name
            );
            return false;
        }
    }
    for (index = 0; index < record->field_count; index += 1) {
        if (seen[index] == NONE) {
            set_error(
                frontend,
                "E2S115",
                expr->start,
                expr->end,
                "record `%s` construction is missing field `%s` declared at "
                "bytes %zu..%zu",
                record->name,
                frontend->fields[record->first_field + index].name,
                frontend->fields[record->first_field + index].start,
                frontend->fields[record->first_field + index].end
            );
            return false;
        }
    }
    expr->kind = EXPR_CONSTRUCT;
    expr->decl = record_index;
    expr->type = make_type(TYPE_RECORD, record_index);
    return record_construct_fact(frontend, record_index, expr_index);
}

static bool check_builtin_call(
    Frontend *frontend,
    size_t expr_index,
    size_t builtin_index
) {
    Expr *expr = &frontend->exprs[expr_index];
    const Builtin *builtin = &BUILTINS[builtin_index];
    Type argument_types[3];
    size_t index;
    char found[TEXT_LIMIT];

    if (expr->argument_count != builtin->arity) {
        set_error(
            frontend, "E2S124", expr->start, expr->end,
            "builtin `%s` expects %zu arguments, found %zu",
            builtin->name, builtin->arity, expr->argument_count);
        return false;
    }
    for (index = 0; index < expr->argument_count; index += 1) {
        Argument *item = &frontend->arguments[expr->first_argument + index];
        if (item->labelled) {
            set_error(
                frontend, "E2S118", item->start, item->end,
                "builtin `%s` does not accept labelled arguments",
                builtin->name);
            return false;
        }
        if (!check_expr(frontend, item->expr, &argument_types[index])) {
            return false;
        }
    }
    expr->call_kind = CALL_BUILTIN;
    expr->decl = builtin_index;
    if (strcmp(builtin->name, "len") == 0) {
        if (argument_types[0].tag != TYPE_TEXT) {
            write_type_name(frontend, argument_types[0], found, sizeof(found));
            set_error(frontend, "E2S124", expr->start, expr->end,
                "`len` expects `Text`, found `%s`", found);
            return false;
        }
        expr->type = make_type(TYPE_INT, NONE);
        return true;
    }
    if (strcmp(builtin->name, "char_code") == 0) {
        if (argument_types[0].tag != TYPE_TEXT ||
            argument_types[1].tag != TYPE_INT) {
            set_error(frontend, "E2S124", expr->start, expr->end,
                "`char_code` expects `(Text, Int)`");
            return false;
        }
        expr->type = make_type(TYPE_INT, NONE);
        return true;
    }
    if (strcmp(builtin->name, "slice") == 0) {
        if (argument_types[0].tag != TYPE_TEXT ||
            argument_types[1].tag != TYPE_INT ||
            argument_types[2].tag != TYPE_INT) {
            set_error(frontend, "E2S124", expr->start, expr->end,
                "`slice` expects `(Text, Int, Int)`");
            return false;
        }
        expr->type = make_type(TYPE_TEXT, NONE);
        return true;
    }
    if (strcmp(builtin->name, "count") == 0) {
        if (argument_types[0].tag != TYPE_LIST) {
            write_type_name(frontend, argument_types[0], found, sizeof(found));
            set_error(frontend, "E2S124", expr->start, expr->end,
                "`count` expects a list, found `%s`", found);
            return false;
        }
        expr->type = make_type(TYPE_INT, NONE);
        return true;
    }
    /* push */
    if (argument_types[0].tag != TYPE_LIST) {
        write_type_name(frontend, argument_types[0], found, sizeof(found));
        set_error(frontend, "E2S124", expr->start, expr->end,
            "`push` expects a list as its first argument, found `%s`", found);
        return false;
    }
    if (argument_types[0].element_tag != TYPE_UNKNOWN) {
        Type element = make_type(
            argument_types[0].element_tag, argument_types[0].element_decl);
        if (!same_type(element, argument_types[1])) {
            char expected[TEXT_LIMIT];
            write_type_name(frontend, element, expected, sizeof(expected));
            write_type_name(frontend, argument_types[1], found, sizeof(found));
            set_error(frontend, "E2S124", expr->start, expr->end,
                "`push` expects `%s`, found `%s`", expected, found);
            return false;
        }
        expr->type = argument_types[0];
        return true;
    }
    expr->type = make_type(TYPE_LIST, NONE);
    expr->type.element_tag = argument_types[1].tag;
    expr->type.element_decl = argument_types[1].decl;
    return true;
}

static bool check_call(Frontend *frontend, size_t expr_index) {
    Expr *expr = &frontend->exprs[expr_index];
    size_t record_index = find_record(frontend, expr->text);
    size_t function_index;
    size_t builtin_index;
    size_t index;
    bool labelled = false;

    for (index = 0; index < expr->argument_count; index += 1) {
        if (frontend->arguments[expr->first_argument + index].labelled) {
            labelled = true;
            break;
        }
    }
    if (record_index != NONE) {
        return check_construction(frontend, expr_index, record_index);
    }
    function_index = find_function(frontend, expr->text);
    builtin_index = find_builtin(expr->text);
    if (function_index == NONE && builtin_index == NONE) {
        if (find_variant(frontend, expr->text) != NONE) {
            set_error(
                frontend, "E2S107", expr->start, expr->end,
                "variant `%s` is flat and takes no arguments", expr->text);
            return false;
        }
        set_error(
            frontend,
            "E2S113",
            expr->start,
            expr->end,
            "unknown function or record type `%s`",
            expr->text
        );
        return false;
    }
    if (labelled) {
        set_error(
            frontend,
            "E2S118",
            expr->start,
            expr->end,
            "labelled arguments select record construction; function `%s` "
            "takes positional arguments",
            expr->text
        );
        return false;
    }
    if (function_index == NONE) {
        return check_builtin_call(frontend, expr_index, builtin_index);
    }
    {
        const Function *function = &frontend->functions[function_index];
        if (expr->argument_count != function->param_count) {
            set_error(
                frontend,
                "E2S124",
                expr->start,
                expr->end,
                "function `%s` expects %zu arguments, found %zu",
                function->name,
                function->param_count,
                expr->argument_count
            );
            return false;
        }
        for (index = 0; index < expr->argument_count; index += 1) {
            Argument *item =
                &frontend->arguments[expr->first_argument + index];
            const Param *param =
                &frontend->params[function->first_param + index];
            Type actual;
            if (!check_expr(frontend, item->expr, &actual)) return false;
            if (!same_type(param->type, actual)) {
                char expected_name[TEXT_LIMIT];
                char actual_name[TEXT_LIMIT];
                write_type_name(
                    frontend, param->type, expected_name,
                    sizeof(expected_name));
                write_type_name(
                    frontend, actual, actual_name, sizeof(actual_name));
                set_error(
                    frontend,
                    "E2S124",
                    item->start,
                    item->end,
                    "parameter `%s` of `%s` expects `%s`, found `%s`",
                    param->name,
                    function->name,
                    expected_name,
                    actual_name
                );
                return false;
            }
        }
        if (!function->has_return) {
            set_error(
                frontend, "E2S124", expr->start, expr->end,
                "function `%s` has no result to use in an expression",
                function->name);
            return false;
        }
        expr->call_kind = CALL_FUNCTION;
        expr->decl = function_index;
        expr->type = function->return_type;
        return true;
    }
}

static bool check_expr(Frontend *frontend, size_t expr_index, Type *output) {
    Expr *expr = &frontend->exprs[expr_index];

    switch (expr->kind) {
        case EXPR_INT:
            expr->type = make_type(TYPE_INT, NONE);
            break;
        case EXPR_TEXT:
            expr->type = make_type(TYPE_TEXT, NONE);
            break;
        case EXPR_BOOL:
            expr->type = make_type(TYPE_BOOL, NONE);
            break;
        case EXPR_NAME: {
            size_t local = find_local(frontend, expr->text);
            size_t variant;
            if (local != NONE) {
                if (frontend->locals[local].moved) {
                    set_error(
                        frontend,
                        "E2S123",
                        expr->start,
                        expr->end,
                        "`%s` was moved by `take` and cannot be used again",
                        expr->text
                    );
                    return false;
                }
                expr->name_kind = NAME_LOCAL;
                expr->decl = local;
                expr->type = frontend->locals[local].type;
                break;
            }
            variant = find_variant(frontend, expr->text);
            if (variant != NONE) {
                expr->name_kind = NAME_VARIANT;
                expr->decl = variant;
                expr->type = make_type(
                    TYPE_ENUM, frontend->variants[variant].enum_index);
                break;
            }
            if (find_record(frontend, expr->text) != NONE) {
                set_error(
                    frontend,
                    "E2S118",
                    expr->start,
                    expr->end,
                    "record type `%s` is not a value; construct it with "
                    "`%s(field: value)`",
                    expr->text,
                    expr->text
                );
                return false;
            }
            set_error(
                frontend,
                "E2S113",
                expr->start,
                expr->end,
                "unknown name `%s`",
                expr->text
            );
            return false;
        }
        case EXPR_CALL:
            if (!check_call(frontend, expr_index)) return false;
            break;
        case EXPR_CONSTRUCT:
            break;
        case EXPR_FIELD: {
            Type base;
            const Record *record;
            size_t index;
            size_t field_index = NONE;
            if (!check_expr(frontend, expr->left, &base)) return false;
            if (base.tag != TYPE_RECORD) {
                char found[TEXT_LIMIT];
                write_type_name(frontend, base, found, sizeof(found));
                set_error(
                    frontend,
                    "E2S120",
                    expr->start,
                    expr->end,
                    "`%s` has type `%s`, which has no fields",
                    expr->text,
                    found
                );
                return false;
            }
            record = &frontend->records[base.decl];
            for (index = 0; index < record->field_count; index += 1) {
                if (strcmp(
                        frontend->fields[record->first_field + index].name,
                        expr->text) == 0) {
                    field_index = index;
                    break;
                }
            }
            if (field_index == NONE) {
                set_error(
                    frontend,
                    "E2S120",
                    expr->start,
                    expr->end,
                    "record `%s` has no field `%s`",
                    record->name,
                    expr->text
                );
                return false;
            }
            expr->decl = base.decl;
            expr->field_index = field_index;
            expr->type =
                frontend->fields[record->first_field + field_index].type;
            if (!record_read_fact(frontend, base.decl, field_index, expr_index)) {
                return false;
            }
            break;
        }
        case EXPR_INDEX: {
            Type base;
            Type subscript;
            if (!check_expr(frontend, expr->left, &base)) return false;
            if (!check_expr(frontend, expr->right, &subscript)) return false;
            if (base.tag != TYPE_LIST || subscript.tag != TYPE_INT) {
                set_error(
                    frontend, "E2S124", expr->start, expr->end,
                    "indexing requires `List[T]` and an `Int` position");
                return false;
            }
            expr->type = make_type(base.element_tag, base.element_decl);
            break;
        }
        case EXPR_LIST: {
            Type element = make_type(TYPE_UNKNOWN, NONE);
            size_t index;
            for (index = 0; index < expr->argument_count; index += 1) {
                Argument *item =
                    &frontend->arguments[expr->first_argument + index];
                Type actual;
                if (!check_expr(frontend, item->expr, &actual)) return false;
                if (index == 0) {
                    element = actual;
                } else if (!same_type(element, actual)) {
                    char expected_name[TEXT_LIMIT];
                    char actual_name[TEXT_LIMIT];
                    write_type_name(
                        frontend, element, expected_name,
                        sizeof(expected_name));
                    write_type_name(
                        frontend, actual, actual_name, sizeof(actual_name));
                    set_error(
                        frontend,
                        "E2S124",
                        item->start,
                        item->end,
                        "list elements must share one type; expected `%s`, "
                        "found `%s`",
                        expected_name,
                        actual_name
                    );
                    return false;
                }
            }
            expr->type = make_type(TYPE_LIST, NONE);
            expr->type.element_tag = element.tag;
            expr->type.element_decl = element.decl;
            break;
        }
        case EXPR_UNARY: {
            Type operand;
            if (!check_expr(frontend, expr->left, &operand)) return false;
            if (strcmp(expr->operator_text, "-") == 0) {
                if (operand.tag != TYPE_INT) {
                    set_error(frontend, "E2S124", expr->start, expr->end,
                        "unary `-` requires `Int`");
                    return false;
                }
                expr->type = make_type(TYPE_INT, NONE);
            } else {
                if (operand.tag != TYPE_BOOL) {
                    set_error(frontend, "E2S124", expr->start, expr->end,
                        "unary `!` requires `Bool`");
                    return false;
                }
                expr->type = make_type(TYPE_BOOL, NONE);
            }
            break;
        }
        case EXPR_BINARY: {
            Type left;
            Type right;
            const char *symbol = expr->operator_text;
            if (!check_expr(frontend, expr->left, &left)) return false;
            if (!check_expr(frontend, expr->right, &right)) return false;
            if (strcmp(symbol, "==") == 0 || strcmp(symbol, "!=") == 0) {
                if (!same_type(left, right) || left.tag == TYPE_LIST ||
                    left.tag == TYPE_RECORD) {
                    set_error(
                        frontend, "E2S124", expr->start, expr->end,
                        "`%s` compares two `Int`, `Text`, `Bool`, or "
                        "enumeration values", symbol);
                    return false;
                }
                expr->type = make_type(TYPE_BOOL, NONE);
                break;
            }
            if (strcmp(symbol, "&&") == 0 || strcmp(symbol, "||") == 0) {
                if (left.tag != TYPE_BOOL || right.tag != TYPE_BOOL) {
                    set_error(frontend, "E2S124", expr->start, expr->end,
                        "`%s` requires `Bool` operands", symbol);
                    return false;
                }
                expr->type = make_type(TYPE_BOOL, NONE);
                break;
            }
            if (strcmp(symbol, "<") == 0 || strcmp(symbol, "<=") == 0 ||
                strcmp(symbol, ">") == 0 || strcmp(symbol, ">=") == 0) {
                if (left.tag != TYPE_INT || right.tag != TYPE_INT) {
                    set_error(frontend, "E2S124", expr->start, expr->end,
                        "`%s` requires `Int` operands", symbol);
                    return false;
                }
                expr->type = make_type(TYPE_BOOL, NONE);
                break;
            }
            if (strcmp(symbol, "+") == 0 && left.tag == TYPE_TEXT &&
                right.tag == TYPE_TEXT) {
                expr->type = make_type(TYPE_TEXT, NONE);
                break;
            }
            if (left.tag != TYPE_INT || right.tag != TYPE_INT) {
                set_error(frontend, "E2S124", expr->start, expr->end,
                    "`%s` requires `Int` operands", symbol);
                return false;
            }
            expr->type = make_type(TYPE_INT, NONE);
            break;
        }
        default:
            set_error(frontend, "E2S107", expr->start, expr->end,
                "unsupported expression form");
            return false;
    }
    *output = expr->type;
    return true;
}

static bool check_block(Frontend *frontend, size_t first);

static bool check_statement(Frontend *frontend, size_t stmt_index) {
    Stmt *stmt = &frontend->stmts[stmt_index];
    const Function *function = &frontend->functions[frontend->current_function];

    switch (stmt->kind) {
        case STMT_LET: {
            Type value;
            if (!check_expr(frontend, stmt->expr, &value)) return false;
            if (stmt->has_annotation) {
                Type annotated;
                if (!resolve_type_ref(
                        frontend, &stmt->written_type, &annotated)) {
                    return false;
                }
                if (!same_type(annotated, value)) {
                    char expected_name[TEXT_LIMIT];
                    char actual_name[TEXT_LIMIT];
                    write_type_name(
                        frontend, annotated, expected_name,
                        sizeof(expected_name));
                    write_type_name(
                        frontend, value, actual_name, sizeof(actual_name));
                    set_error(
                        frontend, "E2S124", stmt->start, stmt->end,
                        "`%s` is annotated `%s` but its value has type `%s`",
                        stmt->name, expected_name, actual_name);
                    return false;
                }
                value = annotated;
            } else if (value.tag == TYPE_LIST &&
                value.element_tag == TYPE_UNKNOWN) {
                set_error(
                    frontend, "E2S124", stmt->start, stmt->end,
                    "empty list `%s` needs a `List[T]` annotation", stmt->name);
                return false;
            }
            stmt->type = value;
            return push_local(
                frontend, stmt->name, value, stmt->is_mut, false,
                stmt->start, stmt->end);
        }
        case STMT_ASSIGN: {
            Expr *target = &frontend->exprs[stmt->target];
            Type value;
            size_t local;
            if (target->kind == EXPR_FIELD) {
                set_error(
                    frontend,
                    "E2S121",
                    stmt->start,
                    stmt->end,
                    "record fields are immutable in v1; `%s` cannot be "
                    "assigned",
                    target->text
                );
                return false;
            }
            if (target->kind != EXPR_NAME) {
                set_error(frontend, "E2S107", stmt->start, stmt->end,
                    "only a local binding may be assigned");
                return false;
            }
            local = find_local(frontend, target->text);
            if (local == NONE) {
                set_error(frontend, "E2S113", stmt->start, stmt->end,
                    "unknown name `%s`", target->text);
                return false;
            }
            if (!frontend->locals[local].is_mut) {
                set_error(
                    frontend, "E2S121", stmt->start, stmt->end,
                    "`%s` is immutable; declare it with `let mut` to assign",
                    target->text);
                return false;
            }
            if (!check_expr(frontend, stmt->expr, &value)) return false;
            if (!same_type(frontend->locals[local].type, value)) {
                char expected_name[TEXT_LIMIT];
                char actual_name[TEXT_LIMIT];
                write_type_name(
                    frontend, frontend->locals[local].type, expected_name,
                    sizeof(expected_name));
                write_type_name(
                    frontend, value, actual_name, sizeof(actual_name));
                set_error(
                    frontend, "E2S124", stmt->start, stmt->end,
                    "`%s` has type `%s` and cannot hold `%s`",
                    target->text, expected_name, actual_name);
                return false;
            }
            target->name_kind = NAME_LOCAL;
            target->decl = local;
            target->type = frontend->locals[local].type;
            return true;
        }
        case STMT_RETURN: {
            Type value;
            if (!check_expr(frontend, stmt->expr, &value)) return false;
            if (!function->has_return) {
                set_error(
                    frontend, "E2S124", stmt->start, stmt->end,
                    "function `%s` declares no result type", function->name);
                return false;
            }
            if (!same_type(function->return_type, value)) {
                char expected_name[TEXT_LIMIT];
                char actual_name[TEXT_LIMIT];
                write_type_name(
                    frontend, function->return_type, expected_name,
                    sizeof(expected_name));
                write_type_name(
                    frontend, value, actual_name, sizeof(actual_name));
                set_error(
                    frontend, "E2S124", stmt->start, stmt->end,
                    "function `%s` returns `%s`, found `%s`",
                    function->name, expected_name, actual_name);
                return false;
            }
            return true;
        }
        case STMT_IF: {
            Type condition;
            size_t saved;
            if (!check_expr(frontend, stmt->expr, &condition)) return false;
            if (condition.tag != TYPE_BOOL) {
                set_error(frontend, "E2S124", stmt->start, stmt->end,
                    "`if` requires a `Bool` condition");
                return false;
            }
            saved = frontend->local_count;
            if (!check_block(frontend, stmt->then_first)) return false;
            frontend->local_count = saved;
            if (stmt->else_first != NONE) {
                if (!check_block(frontend, stmt->else_first)) return false;
                frontend->local_count = saved;
            }
            return true;
        }
        case STMT_WHILE: {
            Type condition;
            size_t saved;
            if (!check_expr(frontend, stmt->expr, &condition)) return false;
            if (condition.tag != TYPE_BOOL) {
                set_error(frontend, "E2S124", stmt->start, stmt->end,
                    "`while` requires a `Bool` condition");
                return false;
            }
            saved = frontend->local_count;
            if (!check_block(frontend, stmt->body_first)) return false;
            frontend->local_count = saved;
            return true;
        }
        case STMT_FOR: {
            Type iterable;
            size_t saved;
            if (!check_expr(frontend, stmt->expr, &iterable)) return false;
            if (iterable.tag != TYPE_LIST ||
                iterable.element_tag == TYPE_UNKNOWN) {
                set_error(frontend, "E2S124", stmt->start, stmt->end,
                    "`for` iterates a `List[T]` value");
                return false;
            }
            saved = frontend->local_count;
            if (!push_local(
                    frontend, stmt->name,
                    make_type(iterable.element_tag, iterable.element_decl),
                    false, true, stmt->start, stmt->end)) {
                return false;
            }
            if (!check_block(frontend, stmt->body_first)) return false;
            frontend->local_count = saved;
            return true;
        }
        case STMT_TAKE: {
            Expr *target = &frontend->exprs[stmt->target];
            size_t local;
            if (target->kind == EXPR_FIELD) {
                set_error(
                    frontend,
                    "E2S122",
                    stmt->start,
                    stmt->end,
                    "partial move `take value.%s` is rejected in v1; move the "
                    "whole record instead",
                    target->text
                );
                return false;
            }
            if (target->kind != EXPR_NAME) {
                set_error(frontend, "E2S122", stmt->start, stmt->end,
                    "`take` applies to a whole named binding");
                return false;
            }
            local = find_local(frontend, target->text);
            if (local == NONE) {
                set_error(frontend, "E2S113", stmt->start, stmt->end,
                    "unknown name `%s`", target->text);
                return false;
            }
            if (frontend->locals[local].borrowed) {
                set_error(
                    frontend, "E2S122", stmt->start, stmt->end,
                    "`%s` is borrowed by `read` and cannot be moved",
                    target->text);
                return false;
            }
            if (frontend->locals[local].moved) {
                set_error(
                    frontend, "E2S123", stmt->start, stmt->end,
                    "`%s` was already moved by `take`", target->text);
                return false;
            }
            frontend->locals[local].moved = true;
            target->name_kind = NAME_LOCAL;
            target->decl = local;
            target->type = frontend->locals[local].type;
            return true;
        }
        case STMT_EXPR: {
            Type value;
            return check_expr(frontend, stmt->expr, &value);
        }
        default:
            set_error(frontend, "E2S107", stmt->start, stmt->end,
                "unsupported statement form");
            return false;
    }
}

static bool check_block(Frontend *frontend, size_t first) {
    size_t index = first;
    while (index != NONE) {
        if (!check_statement(frontend, index)) return false;
        index = frontend->stmts[index].next;
    }
    return true;
}

static bool check_functions(Frontend *frontend) {
    size_t index;
    for (index = 0; index < frontend->function_count; index += 1) {
        Function *function = &frontend->functions[index];
        size_t param;
        size_t last = NONE;
        size_t walk;
        frontend->current_function = index;
        frontend->local_count = 0;
        for (param = 0; param < function->param_count; param += 1) {
            const Param *item =
                &frontend->params[function->first_param + param];
            if (!push_local(
                    frontend, item->name, item->type, false,
                    item->mode == MODE_READ, item->start, item->end)) {
                return false;
            }
        }
        if (!check_block(frontend, function->body_first)) return false;
        walk = function->body_first;
        while (walk != NONE) {
            last = walk;
            walk = frontend->stmts[walk].next;
        }
        if (function->has_return &&
            (last == NONE || frontend->stmts[last].kind != STMT_RETURN)) {
            set_error(
                frontend,
                "E2S124",
                function->start,
                function->end,
                "function `%s` declares a result and must end in `return`",
                function->name
            );
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------ evaluation */

static Value make_int(long long value) {
    Value result;
    memset(&result, 0, sizeof(result));
    result.tag = TYPE_INT;
    result.integer = value;
    return result;
}

static Value make_bool(bool value) {
    Value result;
    memset(&result, 0, sizeof(result));
    result.tag = TYPE_BOOL;
    result.boolean = value;
    return result;
}

static bool make_text(
    Frontend *frontend,
    const char *bytes,
    size_t length,
    Value *output
) {
    char *copy;
    if (length > VALUE_TEXT_LIMIT) {
        set_error(frontend, "E2S126", 0, 0,
            "text value exceeds %u bytes", VALUE_TEXT_LIMIT);
        return false;
    }
    copy = malloc(length + 1);
    if (copy == NULL) {
        set_error(frontend, "E2S126", 0, 0, "out of memory for a text value");
        return false;
    }
    if (length > 0) memcpy(copy, bytes, length);
    copy[length] = '\0';
    memset(output, 0, sizeof(*output));
    output->tag = TYPE_TEXT;
    output->text = copy;
    output->length = length;
    return true;
}

static bool eval_expr(Frontend *frontend, size_t expr_index, Value *output);
static bool eval_block(Frontend *frontend, size_t first);

static size_t find_binding(const Frontend *frontend, const char *name) {
    size_t index = frontend->binding_count;
    while (index > frontend->frame_base) {
        index -= 1;
        if (strcmp(frontend->bindings[index].name, name) == 0) return index;
    }
    return NONE;
}

static bool bind_value(Frontend *frontend, const char *name, Value value) {
    Binding *binding;
    if (frontend->binding_count >= LOCAL_LIMIT) {
        set_error(frontend, "E2S126", 0, 0, "binding count exceeds the limit");
        return false;
    }
    binding = &frontend->bindings[frontend->binding_count];
    memset(binding, 0, sizeof(*binding));
    memcpy(binding->name, name, TEXT_LIMIT);
    binding->value = value;
    frontend->binding_count += 1;
    return true;
}

static bool step(Frontend *frontend, size_t start, size_t end) {
    frontend->steps += 1;
    if (frontend->steps > STEP_LIMIT) {
        set_error(
            frontend, "E2S126", start, end,
            "bounded record evaluation exceeded %u steps", STEP_LIMIT);
        return false;
    }
    return true;
}

static bool call_function(
    Frontend *frontend,
    size_t function_index,
    Value *arguments,
    size_t argument_count,
    size_t start,
    size_t end,
    Value *output
);

static bool eval_builtin(
    Frontend *frontend,
    const Expr *expr,
    Value *arguments,
    Value *output
) {
    const Builtin *builtin = &BUILTINS[expr->decl];
    if (strcmp(builtin->name, "len") == 0) {
        *output = make_int((long long)arguments[0].length);
        return true;
    }
    if (strcmp(builtin->name, "char_code") == 0) {
        long long position = arguments[1].integer;
        if (position < 0 || (size_t)position >= arguments[0].length) {
            set_error(
                frontend, "E2S126", expr->start, expr->end,
                "`char_code` position %lld is outside the text", position);
            return false;
        }
        *output = make_int(
            (long long)(unsigned char)arguments[0].text[position]);
        return true;
    }
    if (strcmp(builtin->name, "slice") == 0) {
        long long from = arguments[1].integer;
        long long to = arguments[2].integer;
        if (from < 0 || to < from || (size_t)to > arguments[0].length) {
            set_error(
                frontend, "E2S126", expr->start, expr->end,
                "`slice` range %lld..%lld is outside the text", from, to);
            return false;
        }
        return make_text(
            frontend, arguments[0].text + from, (size_t)(to - from), output);
    }
    if (strcmp(builtin->name, "count") == 0) {
        *output = make_int((long long)arguments[0].item_count);
        return true;
    }
    /* push */
    {
        Value result = arguments[0];
        size_t count = arguments[0].item_count;
        Value *items;
        if (count + 1 > LIST_LIMIT) {
            set_error(
                frontend, "E2S126", expr->start, expr->end,
                "list length exceeds %u", LIST_LIMIT);
            return false;
        }
        items = malloc(sizeof(Value) * (count + 1));
        if (items == NULL) {
            set_error(frontend, "E2S126", expr->start, expr->end,
                "out of memory for a list value");
            return false;
        }
        if (count > 0) memcpy(items, arguments[0].items, sizeof(Value) * count);
        items[count] = arguments[1];
        result.tag = TYPE_LIST;
        result.items = items;
        result.item_count = count + 1;
        result.element_tag = arguments[1].tag;
        result.element_decl = arguments[1].decl;
        *output = result;
        return true;
    }
}

static bool eval_expr(Frontend *frontend, size_t expr_index, Value *output) {
    const Expr *expr = &frontend->exprs[expr_index];
    if (!step(frontend, expr->start, expr->end)) return false;
    switch (expr->kind) {
        case EXPR_INT:
            *output = make_int(expr->integer);
            return true;
        case EXPR_TEXT:
            return make_text(
                frontend, expr->text, (size_t)expr->integer, output);
        case EXPR_BOOL:
            *output = make_bool(expr->boolean);
            return true;
        case EXPR_NAME: {
            if (expr->name_kind == NAME_VARIANT) {
                Value result;
                memset(&result, 0, sizeof(result));
                result.tag = TYPE_ENUM;
                result.decl = frontend->variants[expr->decl].enum_index;
                result.variant = frontend->variants[expr->decl].local_index;
                *output = result;
                return true;
            }
            {
                size_t binding = find_binding(frontend, expr->text);
                if (binding == NONE) {
                    set_error(frontend, "E2S126", expr->start, expr->end,
                        "`%s` is unbound at run time", expr->text);
                    return false;
                }
                *output = frontend->bindings[binding].value;
                return true;
            }
        }
        case EXPR_CONSTRUCT: {
            const Record *record = &frontend->records[expr->decl];
            Value result;
            Value *items;
            size_t index;
            items = malloc(sizeof(Value) * record->field_count);
            if (items == NULL) {
                set_error(frontend, "E2S126", expr->start, expr->end,
                    "out of memory for a record value");
                return false;
            }
            for (index = 0; index < record->field_count; index += 1) {
                memset(&items[index], 0, sizeof(items[index]));
            }
            /* written order evaluation, declaration order storage */
            for (index = 0; index < expr->argument_count; index += 1) {
                const Argument *item =
                    &frontend->arguments[expr->first_argument + index];
                size_t slot;
                Value value;
                if (!eval_expr(frontend, item->expr, &value)) {
                    free(items);
                    return false;
                }
                for (slot = 0; slot < record->field_count; slot += 1) {
                    if (strcmp(
                            frontend->fields[record->first_field + slot].name,
                            item->label) == 0) {
                        items[slot] = value;
                        break;
                    }
                }
            }
            memset(&result, 0, sizeof(result));
            result.tag = TYPE_RECORD;
            result.decl = expr->decl;
            result.items = items;
            result.item_count = record->field_count;
            *output = result;
            return true;
        }
        case EXPR_CALL: {
            Value arguments[3];
            size_t index;
            if (expr->argument_count > 3) {
                set_error(frontend, "E2S126", expr->start, expr->end,
                    "call arity exceeds the bounded evaluator limit");
                return false;
            }
            for (index = 0; index < expr->argument_count; index += 1) {
                const Argument *item =
                    &frontend->arguments[expr->first_argument + index];
                if (!eval_expr(frontend, item->expr, &arguments[index])) {
                    return false;
                }
            }
            if (expr->call_kind == CALL_BUILTIN) {
                return eval_builtin(frontend, expr, arguments, output);
            }
            return call_function(
                frontend, expr->decl, arguments, expr->argument_count,
                expr->start, expr->end, output);
        }
        case EXPR_FIELD: {
            Value base;
            if (!eval_expr(frontend, expr->left, &base)) return false;
            if (base.tag != TYPE_RECORD ||
                expr->field_index >= base.item_count) {
                set_error(frontend, "E2S126", expr->start, expr->end,
                    "field read failed at run time");
                return false;
            }
            *output = base.items[expr->field_index];
            return true;
        }
        case EXPR_INDEX: {
            Value base;
            Value position;
            if (!eval_expr(frontend, expr->left, &base)) return false;
            if (!eval_expr(frontend, expr->right, &position)) return false;
            if (position.integer < 0 ||
                (size_t)position.integer >= base.item_count) {
                set_error(
                    frontend, "E2S126", expr->start, expr->end,
                    "list position %lld is outside the list",
                    position.integer);
                return false;
            }
            *output = base.items[position.integer];
            return true;
        }
        case EXPR_LIST: {
            Value result;
            Value *items = NULL;
            size_t index;
            if (expr->argument_count > 0) {
                items = malloc(sizeof(Value) * expr->argument_count);
                if (items == NULL) {
                    set_error(frontend, "E2S126", expr->start, expr->end,
                        "out of memory for a list value");
                    return false;
                }
            }
            for (index = 0; index < expr->argument_count; index += 1) {
                const Argument *item =
                    &frontend->arguments[expr->first_argument + index];
                if (!eval_expr(frontend, item->expr, &items[index])) {
                    free(items);
                    return false;
                }
            }
            memset(&result, 0, sizeof(result));
            result.tag = TYPE_LIST;
            result.items = items;
            result.item_count = expr->argument_count;
            result.element_tag = expr->type.element_tag;
            result.element_decl = expr->type.element_decl;
            *output = result;
            return true;
        }
        case EXPR_UNARY: {
            Value operand;
            if (!eval_expr(frontend, expr->left, &operand)) return false;
            if (strcmp(expr->operator_text, "-") == 0) {
                *output = make_int(-operand.integer);
            } else {
                *output = make_bool(!operand.boolean);
            }
            return true;
        }
        case EXPR_BINARY: {
            Value left;
            Value right;
            const char *symbol = expr->operator_text;
            if (!eval_expr(frontend, expr->left, &left)) return false;
            if (strcmp(symbol, "&&") == 0) {
                if (!left.boolean) {
                    *output = make_bool(false);
                    return true;
                }
                if (!eval_expr(frontend, expr->right, &right)) return false;
                *output = make_bool(right.boolean);
                return true;
            }
            if (strcmp(symbol, "||") == 0) {
                if (left.boolean) {
                    *output = make_bool(true);
                    return true;
                }
                if (!eval_expr(frontend, expr->right, &right)) return false;
                *output = make_bool(right.boolean);
                return true;
            }
            if (!eval_expr(frontend, expr->right, &right)) return false;
            if (strcmp(symbol, "==") == 0 || strcmp(symbol, "!=") == 0) {
                bool equal;
                if (left.tag == TYPE_TEXT) {
                    equal = left.length == right.length &&
                        memcmp(left.text, right.text, left.length) == 0;
                } else if (left.tag == TYPE_ENUM) {
                    equal = left.decl == right.decl &&
                        left.variant == right.variant;
                } else if (left.tag == TYPE_BOOL) {
                    equal = left.boolean == right.boolean;
                } else {
                    equal = left.integer == right.integer;
                }
                *output = make_bool(
                    strcmp(symbol, "==") == 0 ? equal : !equal);
                return true;
            }
            if (strcmp(symbol, "+") == 0 && left.tag == TYPE_TEXT) {
                char joined[VALUE_TEXT_LIMIT + 1];
                if (left.length + right.length > VALUE_TEXT_LIMIT) {
                    set_error(frontend, "E2S126", expr->start, expr->end,
                        "text concatenation exceeds %u bytes",
                        VALUE_TEXT_LIMIT);
                    return false;
                }
                memcpy(joined, left.text, left.length);
                memcpy(joined + left.length, right.text, right.length);
                return make_text(
                    frontend, joined, left.length + right.length, output);
            }
            if (strcmp(symbol, "<") == 0) {
                *output = make_bool(left.integer < right.integer);
                return true;
            }
            if (strcmp(symbol, "<=") == 0) {
                *output = make_bool(left.integer <= right.integer);
                return true;
            }
            if (strcmp(symbol, ">") == 0) {
                *output = make_bool(left.integer > right.integer);
                return true;
            }
            if (strcmp(symbol, ">=") == 0) {
                *output = make_bool(left.integer >= right.integer);
                return true;
            }
            if (strcmp(symbol, "+") == 0) {
                *output = make_int(left.integer + right.integer);
                return true;
            }
            if (strcmp(symbol, "-") == 0) {
                *output = make_int(left.integer - right.integer);
                return true;
            }
            if (strcmp(symbol, "*") == 0) {
                *output = make_int(left.integer * right.integer);
                return true;
            }
            if (right.integer == 0) {
                set_error(frontend, "E2S126", expr->start, expr->end,
                    "division by zero");
                return false;
            }
            if (strcmp(symbol, "//") == 0) {
                *output = make_int(left.integer / right.integer);
                return true;
            }
            *output = make_int(left.integer % right.integer);
            return true;
        }
        default:
            set_error(frontend, "E2S126", expr->start, expr->end,
                "unsupported expression at run time");
            return false;
    }
}

static bool eval_statement(Frontend *frontend, size_t stmt_index) {
    const Stmt *stmt = &frontend->stmts[stmt_index];
    if (!step(frontend, stmt->start, stmt->end)) return false;
    switch (stmt->kind) {
        case STMT_LET: {
            Value value;
            if (!eval_expr(frontend, stmt->expr, &value)) return false;
            return bind_value(frontend, stmt->name, value);
        }
        case STMT_ASSIGN: {
            Value value;
            size_t binding;
            if (!eval_expr(frontend, stmt->expr, &value)) return false;
            binding = find_binding(
                frontend, frontend->exprs[stmt->target].text);
            if (binding == NONE) {
                set_error(frontend, "E2S126", stmt->start, stmt->end,
                    "assignment target is unbound at run time");
                return false;
            }
            frontend->bindings[binding].value = value;
            return true;
        }
        case STMT_RETURN: {
            Value value;
            if (!eval_expr(frontend, stmt->expr, &value)) return false;
            frontend->return_value = value;
            frontend->returning = true;
            return true;
        }
        case STMT_IF: {
            Value condition;
            size_t saved = frontend->binding_count;
            if (!eval_expr(frontend, stmt->expr, &condition)) return false;
            if (condition.boolean) {
                if (!eval_block(frontend, stmt->then_first)) return false;
            } else if (stmt->else_first != NONE) {
                if (!eval_block(frontend, stmt->else_first)) return false;
            }
            frontend->binding_count = saved;
            return true;
        }
        case STMT_WHILE: {
            for (;;) {
                Value condition;
                size_t saved = frontend->binding_count;
                if (!step(frontend, stmt->start, stmt->end)) return false;
                if (!eval_expr(frontend, stmt->expr, &condition)) return false;
                if (!condition.boolean) return true;
                if (!eval_block(frontend, stmt->body_first)) return false;
                frontend->binding_count = saved;
                if (frontend->returning) return true;
            }
        }
        case STMT_FOR: {
            Value iterable;
            size_t index;
            if (!eval_expr(frontend, stmt->expr, &iterable)) return false;
            for (index = 0; index < iterable.item_count; index += 1) {
                size_t saved = frontend->binding_count;
                if (!step(frontend, stmt->start, stmt->end)) return false;
                if (!bind_value(
                        frontend, stmt->name, iterable.items[index])) {
                    return false;
                }
                if (!eval_block(frontend, stmt->body_first)) return false;
                frontend->binding_count = saved;
                if (frontend->returning) return true;
            }
            return true;
        }
        case STMT_TAKE:
            return true;
        case STMT_EXPR: {
            Value value;
            return eval_expr(frontend, stmt->expr, &value);
        }
        default:
            set_error(frontend, "E2S126", stmt->start, stmt->end,
                "unsupported statement at run time");
            return false;
    }
}

static bool eval_block(Frontend *frontend, size_t first) {
    size_t index = first;
    while (index != NONE) {
        if (!eval_statement(frontend, index)) return false;
        if (frontend->returning) return true;
        index = frontend->stmts[index].next;
    }
    return true;
}

static bool call_function(
    Frontend *frontend,
    size_t function_index,
    Value *arguments,
    size_t argument_count,
    size_t start,
    size_t end,
    Value *output
) {
    const Function *function = &frontend->functions[function_index];
    size_t saved_bindings = frontend->binding_count;
    size_t saved_base = frontend->frame_base;
    size_t index;
    bool saved_returning = frontend->returning;
    Value saved_return = frontend->return_value;

    if (frontend->call_depth >= CALL_DEPTH_LIMIT) {
        set_error(frontend, "E2S126", start, end,
            "call depth exceeds %u", CALL_DEPTH_LIMIT);
        return false;
    }
    frontend->call_depth += 1;
    frontend->frame_base = frontend->binding_count;
    for (index = 0; index < argument_count; index += 1) {
        if (!bind_value(
                frontend,
                frontend->params[function->first_param + index].name,
                arguments[index])) {
            return false;
        }
    }
    frontend->returning = false;
    if (!eval_block(frontend, function->body_first)) return false;
    if (!frontend->returning && function->has_return) {
        set_error(frontend, "E2S126", start, end,
            "function `%s` produced no result", function->name);
        return false;
    }
    *output = frontend->return_value;
    frontend->binding_count = saved_bindings;
    frontend->frame_base = saved_base;
    frontend->returning = saved_returning;
    frontend->return_value = saved_return;
    frontend->call_depth -= 1;
    return true;
}

/* ---------------------------------------------------------- value output */

typedef struct {
    char bytes[RENDER_LIMIT];
    size_t length;
    bool overflow;
} Rendered;

static void render_append(Rendered *target, const char *text) {
    size_t length = strlen(text);
    if (target->overflow || target->length + length + 1 >= RENDER_LIMIT) {
        target->overflow = true;
        return;
    }
    memcpy(target->bytes + target->length, text, length);
    target->length += length;
    target->bytes[target->length] = '\0';
}

static void render_value(
    const Frontend *frontend,
    const Value *value,
    Rendered *target
) {
    char scratch[TEXT_LIMIT];
    size_t index;
    switch (value->tag) {
        case TYPE_INT:
            snprintf(scratch, sizeof(scratch), "%lld", value->integer);
            render_append(target, scratch);
            return;
        case TYPE_BOOL:
            render_append(target, value->boolean ? "true" : "false");
            return;
        case TYPE_TEXT:
            render_append(target, "\"");
            for (index = 0; index < value->length; index += 1) {
                char symbol = value->text[index];
                if (symbol == '"' || symbol == '\\') {
                    char escaped[3];
                    escaped[0] = '\\';
                    escaped[1] = symbol;
                    escaped[2] = '\0';
                    render_append(target, escaped);
                } else if (symbol == '\n') {
                    render_append(target, "\\n");
                } else if (symbol == '\t') {
                    render_append(target, "\\t");
                } else {
                    char plain[2];
                    plain[0] = symbol;
                    plain[1] = '\0';
                    render_append(target, plain);
                }
            }
            render_append(target, "\"");
            return;
        case TYPE_ENUM: {
            const Enumeration *enumeration = &frontend->enums[value->decl];
            render_append(target, enumeration->name);
            render_append(target, ".");
            render_append(
                target,
                frontend->variants[
                    enumeration->first_variant + value->variant].name);
            return;
        }
        case TYPE_RECORD: {
            const Record *record = &frontend->records[value->decl];
            render_append(target, record->name);
            render_append(target, "(");
            for (index = 0; index < value->item_count; index += 1) {
                if (index > 0) render_append(target, ", ");
                render_append(
                    target,
                    frontend->fields[record->first_field + index].name);
                render_append(target, ": ");
                render_value(frontend, &value->items[index], target);
            }
            render_append(target, ")");
            return;
        }
        case TYPE_LIST:
            render_append(target, "[");
            for (index = 0; index < value->item_count; index += 1) {
                if (index > 0) render_append(target, ", ");
                render_value(frontend, &value->items[index], target);
            }
            render_append(target, "]");
            return;
        case TYPE_UNKNOWN:
        default:
            render_append(target, "<unknown>");
            return;
    }
}

/* ------------------------------------------------------------- artifacts */

static char *read_source(const char *path, size_t *length_output) {
    FILE *input = fopen(path, "rb");
    long length;
    char *source;
    if (input == NULL) return NULL;
    if (fseek(input, 0, SEEK_END) != 0) {
        fclose(input);
        return NULL;
    }
    length = ftell(input);
    if (length < 0 || (unsigned long)length > SOURCE_LIMIT) {
        fclose(input);
        return NULL;
    }
    if (fseek(input, 0, SEEK_SET) != 0) {
        fclose(input);
        return NULL;
    }
    source = malloc((size_t)length + 1);
    if (source == NULL) {
        fclose(input);
        return NULL;
    }
    if (fread(source, 1, (size_t)length, input) != (size_t)length) {
        fclose(input);
        free(source);
        return NULL;
    }
    if (fclose(input) != 0) {
        free(source);
        return NULL;
    }
    source[length] = '\0';
    *length_output = (size_t)length;
    return source;
}

static const char *mode_name(ParamMode mode) {
    switch (mode) {
        case MODE_READ: return "read";
        case MODE_TAKE: return "take";
        case MODE_VALUE:
        default: return "value";
    }
}

static bool write_ir(const Frontend *frontend, const char *path) {
    FILE *output = fopen(path, "wb");
    size_t index;
    size_t inner;
    char type_name[TEXT_LIMIT];

    if (output == NULL) return false;
    if (fprintf(output, "kofun-record-ir/v1\n") < 0) goto fail;
    for (index = 0; index < frontend->enum_count; index += 1) {
        const Enumeration *enumeration = &frontend->enums[index];
        if (fprintf(
                output,
                "enum|enum-id=enum:%s|name=%s|span=%zu..%zu|variants=%zu\n",
                enumeration->name, enumeration->name,
                enumeration->start, enumeration->end,
                enumeration->variant_count) < 0) goto fail;
        for (inner = 0; inner < enumeration->variant_count; inner += 1) {
            const Variant *variant =
                &frontend->variants[enumeration->first_variant + inner];
            if (fprintf(
                    output,
                    "variant|variant-id=variant:enum:%s:%zu|enum-id=enum:%s"
                    "|name=%s|index=%zu|span=%zu..%zu\n",
                    enumeration->name, variant->local_index,
                    enumeration->name, variant->name, variant->local_index,
                    variant->start, variant->end) < 0) goto fail;
        }
    }
    for (index = 0; index < frontend->record_count; index += 1) {
        const Record *record = &frontend->records[index];
        if (fprintf(
                output,
                "record|record-id=record:%s|name=%s|span=%zu..%zu|fields=%zu\n",
                record->name, record->name, record->start, record->end,
                record->field_count) < 0) goto fail;
        for (inner = 0; inner < record->field_count; inner += 1) {
            const Field *field =
                &frontend->fields[record->first_field + inner];
            write_type_name(frontend, field->type, type_name,
                sizeof(type_name));
            if (fprintf(
                    output,
                    "field|field-id=field:record:%s:%zu|record-id=record:%s"
                    "|name=%s|index=%zu|type=%s|span=%zu..%zu\n",
                    record->name, inner, record->name, field->name, inner,
                    type_name, field->start, field->end) < 0) goto fail;
        }
    }
    for (index = 0; index < frontend->function_count; index += 1) {
        const Function *function = &frontend->functions[index];
        if (function->has_return) {
            write_type_name(frontend, function->return_type, type_name,
                sizeof(type_name));
        } else {
            snprintf(type_name, sizeof(type_name), "none");
        }
        if (fprintf(
                output,
                "function|name=%s|params=%zu|result=%s|span=%zu..%zu\n",
                function->name, function->param_count, type_name,
                function->start, function->end) < 0) goto fail;
        for (inner = 0; inner < function->param_count; inner += 1) {
            const Param *param =
                &frontend->params[function->first_param + inner];
            write_type_name(frontend, param->type, type_name,
                sizeof(type_name));
            if (fprintf(
                    output,
                    "param|function=%s|index=%zu|name=%s|type=%s|access=%s"
                    "|span=%zu..%zu\n",
                    function->name, inner, param->name, type_name,
                    mode_name(param->mode), param->start, param->end) < 0) {
                goto fail;
            }
        }
    }
    for (index = 0; index < frontend->construct_count; index += 1) {
        const ConstructFact *fact = &frontend->constructs[index];
        const Expr *expr = &frontend->exprs[fact->expr];
        const Record *record = &frontend->records[fact->record_index];
        if (fprintf(
                output,
                "construct|function=%s|record-id=record:%s|use-span=%zu..%zu"
                "|written=",
                fact->function_name, record->name, expr->start,
                expr->end) < 0) goto fail;
        for (inner = 0; inner < expr->argument_count; inner += 1) {
            const Argument *item =
                &frontend->arguments[expr->first_argument + inner];
            if (fprintf(output, "%s%s", inner > 0 ? "," : "",
                    item->label) < 0) goto fail;
        }
        if (fprintf(output, "|declared=") < 0) goto fail;
        for (inner = 0; inner < record->field_count; inner += 1) {
            if (fprintf(output, "%s%s", inner > 0 ? "," : "",
                    frontend->fields[record->first_field + inner].name) < 0) {
                goto fail;
            }
        }
        if (fprintf(output, "\n") < 0) goto fail;
    }
    for (index = 0; index < frontend->read_count; index += 1) {
        const ReadFact *fact = &frontend->reads[index];
        const Expr *expr = &frontend->exprs[fact->expr];
        const Record *record = &frontend->records[fact->record_index];
        write_type_name(frontend, expr->type, type_name, sizeof(type_name));
        if (fprintf(
                output,
                "read|function=%s|record-id=record:%s"
                "|field-id=field:record:%s:%zu|name=%s|type=%s"
                "|use-span=%zu..%zu\n",
                fact->function_name, record->name, record->name,
                fact->field_index,
                frontend->fields[
                    record->first_field + fact->field_index].name,
                type_name, expr->start, expr->end) < 0) goto fail;
    }
    return fclose(output) == 0;

fail:
    fclose(output);
    return false;
}

static bool write_layout(const Frontend *frontend, const char *path) {
    FILE *output = fopen(path, "wb");
    size_t target;
    size_t index;
    size_t inner;
    char type_name[TEXT_LIMIT];

    if (output == NULL) return false;
    if (fprintf(output, "kofun-record-layout/v1\n") < 0) goto fail;
    for (target = 0; target < TARGET_COUNT; target += 1) {
        const Target *profile = &TARGETS[target];
        if (fprintf(
                output,
                "target|name=%s|data-layout=%s|int=%zu/%zu|pointer=%zu/%zu"
                "|bool=%zu/%zu|tag=%zu/%zu\n",
                profile->name, profile->data_layout,
                profile->int_size, profile->int_align,
                profile->pointer_size, profile->pointer_align,
                profile->bool_size, profile->bool_align,
                profile->tag_size, profile->tag_align) < 0) goto fail;
    }
    for (target = 0; target < TARGET_COUNT; target += 1) {
        const Target *profile = &TARGETS[target];
        for (index = 0; index < frontend->record_count; index += 1) {
            const Record *record = &frontend->records[index];
            size_t occupied = 0;
            for (inner = 0; inner < record->field_count; inner += 1) {
                const Field *field =
                    &frontend->fields[record->first_field + inner];
                occupied += field->size[target];
            }
            if (fprintf(
                    output,
                    "record|target=%s|record-id=record:%s|size=%zu|align=%zu"
                    "|payload=%zu|tagged=false|fields=%zu\n",
                    profile->name, record->name, record->size[target],
                    record->align[target], occupied,
                    record->field_count) < 0) goto fail;
            for (inner = 0; inner < record->field_count; inner += 1) {
                const Field *field =
                    &frontend->fields[record->first_field + inner];
                write_type_name(frontend, field->type, type_name,
                    sizeof(type_name));
                if (fprintf(
                        output,
                        "field|target=%s|record-id=record:%s|index=%zu|name=%s"
                        "|type=%s|offset=%zu|size=%zu|align=%zu\n",
                        profile->name, record->name, inner, field->name,
                        type_name, field->offset[target], field->size[target],
                        field->align[target]) < 0) goto fail;
            }
        }
    }
    for (index = 0; index < frontend->record_count; index += 1) {
        const Record *record = &frontend->records[index];
        bool identical = true;
        for (target = 1; target < TARGET_COUNT; target += 1) {
            if (record->size[target] != record->size[0] ||
                record->align[target] != record->align[0]) {
                identical = false;
            }
            for (inner = 0; inner < record->field_count; inner += 1) {
                const Field *field =
                    &frontend->fields[record->first_field + inner];
                if (field->offset[target] != field->offset[0] ||
                    field->size[target] != field->size[0]) {
                    identical = false;
                }
            }
        }
        if (fprintf(
                output,
                "agreement|record-id=record:%s|targets=%s,%s|identical=%s\n",
                record->name, TARGETS[0].name, TARGETS[1].name,
                identical ? "true" : "false") < 0) goto fail;
    }
    return fclose(output) == 0;

fail:
    fclose(output);
    return false;
}

static bool write_run(Frontend *frontend, const char *path) {
    FILE *output = fopen(path, "wb");
    size_t index;

    if (output == NULL) return false;
    if (fprintf(output, "kofun-record-run/v1\n") < 0) goto fail;
    for (index = 0; index < frontend->function_count; index += 1) {
        const Function *function = &frontend->functions[index];
        Value result;
        Rendered rendered;
        if (function->param_count != 0 || !function->has_return) continue;
        frontend->steps = 0;
        frontend->call_depth = 0;
        frontend->returning = false;
        frontend->binding_count = 0;
        if (!call_function(
                frontend, index, NULL, 0, function->start, function->end,
                &result)) {
            goto fail;
        }
        memset(&rendered, 0, sizeof(rendered));
        render_value(frontend, &result, &rendered);
        if (rendered.overflow) {
            set_error(frontend, "E2S126", function->start, function->end,
                "rendered result of `%s` exceeds %u bytes",
                function->name, RENDER_LIMIT);
            goto fail;
        }
        if (fprintf(
                output, "call|function=%s|result=%s\n",
                function->name, rendered.bytes) < 0) goto fail;
    }
    return fclose(output) == 0;

fail:
    fclose(output);
    return false;
}

int main(int argc, char **argv) {
    static Frontend frontend;
    char *source;
    size_t length;

    if (argc != 5) {
        fprintf(
            stderr,
            "usage: kofun-record-frontend SOURCE IR LAYOUT RUN\n");
        return 2;
    }
    remove(argv[2]);
    remove(argv[3]);
    remove(argv[4]);
    source = read_source(argv[1], &length);
    if (source == NULL) {
        fprintf(stderr, "kofun-record-frontend: cannot read bounded source\n");
        return 2;
    }
    if (!tokenize(&frontend, source, length) ||
        !parse_program(&frontend) ||
        !resolve_declarations(&frontend) ||
        !layout_all(&frontend) ||
        !check_functions(&frontend)) {
        printf("%s\n", frontend.error);
        free(source);
        return 1;
    }
    if (!write_ir(&frontend, argv[2]) ||
        !write_layout(&frontend, argv[3]) ||
        !write_run(&frontend, argv[4])) {
        remove(argv[2]);
        remove(argv[3]);
        remove(argv[4]);
        if (frontend.failed) {
            printf("%s\n", frontend.error);
            free(source);
            return 1;
        }
        fprintf(
            stderr,
            "kofun-record-frontend: cannot commit output artifacts\n");
        free(source);
        return 2;
    }
    free(source);
    return 0;
}
