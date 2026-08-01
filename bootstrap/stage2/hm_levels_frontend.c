#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SOURCE_LIMIT 65536u
#define TOKEN_LIMIT 4096u
#define TYPE_LIMIT 8192u
#define BINDING_LIMIT 1024u
#define USE_LIMIT 4096u
#define GENERAL_LIMIT 64u
#define NAME_LIMIT 64u
#define MESSAGE_LIMIT 512u
#define TYPE_TEXT_LIMIT 2048u
#define PATH_LIMIT 4096u
#define PARSE_DEPTH_LIMIT 128u

typedef enum {
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_TEXT,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_COLON,
    TOKEN_COMMA,
    TOKEN_EQUAL,
    TOKEN_FAT_ARROW,
    TOKEN_THIN_ARROW,
    TOKEN_EOF
} TokenKind;

typedef struct {
    TokenKind kind;
    size_t start;
    size_t end;
} Token;

typedef enum {
    TYPE_INT,
    TYPE_BOOL,
    TYPE_TEXT,
    TYPE_FUNCTION,
    TYPE_META
} TypeKind;

typedef struct {
    TypeKind kind;
    int left;
    int right;
    int link;
    int level;
    unsigned id;
} Type;

typedef struct {
    char name[NAME_LIMIT];
    char identity[NAME_LIMIT * 2u];
    int type;
    int generals[GENERAL_LIMIT];
    size_t general_count;
    size_t start;
    size_t end;
    unsigned scope;
    bool active;
    bool parameter;
} Binding;

typedef struct {
    size_t binding;
    int type;
    size_t start;
    size_t end;
} Use;

typedef struct {
    int type;
    size_t start;
    size_t end;
    bool lambda;
} Expression;

typedef struct {
    char source[SOURCE_LIMIT + 1u];
    size_t source_length;
    Token tokens[TOKEN_LIMIT];
    size_t token_count;
    size_t cursor;
    unsigned parse_depth;
    unsigned type_parse_depth;
    Type types[TYPE_LIMIT];
    size_t type_count;
    unsigned next_meta_id;
    Binding bindings[BINDING_LIMIT];
    size_t binding_count;
    Use uses[USE_LIMIT];
    size_t use_count;
    unsigned scope;
    char defining_name[NAME_LIMIT];
    bool defining;
    bool failed;
    char error_code[16];
    char error_message[MESSAGE_LIMIT];
    size_t error_start;
    size_t error_end;
    bool error_has_related;
    size_t related_start;
    size_t related_end;
    int result_type;
    size_t result_start;
    size_t result_end;
} Frontend;

typedef struct {
    const int *generals;
    size_t general_count;
    int monomorphic[GENERAL_LIMIT * 2u];
    size_t monomorphic_count;
} TypePrinter;

typedef struct {
    char bytes[TYPE_TEXT_LIMIT];
    size_t length;
    bool overflow;
} TextBuffer;

static void set_error(
    Frontend *frontend,
    const char *code,
    size_t start,
    size_t end,
    const char *format,
    ...
) {
    va_list arguments;
    if (frontend->failed) return;
    frontend->failed = true;
    (void)snprintf(frontend->error_code, sizeof(frontend->error_code), "%s", code);
    frontend->error_start = start;
    frontend->error_end = end;
    va_start(arguments, format);
    (void)vsnprintf(
        frontend->error_message,
        sizeof(frontend->error_message),
        format,
        arguments
    );
    va_end(arguments);
}

static void set_related_error(
    Frontend *frontend,
    const char *code,
    size_t start,
    size_t end,
    size_t related_start,
    size_t related_end,
    const char *format,
    ...
) {
    va_list arguments;
    if (frontend->failed) return;
    frontend->failed = true;
    frontend->error_has_related = true;
    frontend->error_start = start;
    frontend->error_end = end;
    frontend->related_start = related_start;
    frontend->related_end = related_end;
    (void)snprintf(frontend->error_code, sizeof(frontend->error_code), "%s", code);
    va_start(arguments, format);
    (void)vsnprintf(
        frontend->error_message,
        sizeof(frontend->error_message),
        format,
        arguments
    );
    va_end(arguments);
}

static bool add_token(
    Frontend *frontend,
    TokenKind kind,
    size_t start,
    size_t end
) {
    Token *token;
    if (frontend->token_count >= TOKEN_LIMIT) {
        set_error(frontend, "HML007", start, end,
            "token limit is %u", (unsigned)TOKEN_LIMIT);
        return false;
    }
    token = &frontend->tokens[frontend->token_count++];
    token->kind = kind;
    token->start = start;
    token->end = end;
    return true;
}

static bool identifier_start(unsigned char byte) {
    return byte == (unsigned char)'_' || isalpha(byte) != 0;
}

static bool identifier_continue(unsigned char byte) {
    return byte == (unsigned char)'_' || isalnum(byte) != 0;
}

static bool lex(Frontend *frontend) {
    size_t cursor = 0u;
    while (cursor < frontend->source_length && !frontend->failed) {
        unsigned char byte = (unsigned char)frontend->source[cursor];
        size_t start;
        if (isspace(byte) != 0) {
            cursor++;
            continue;
        }
        if (byte == (unsigned char)'#') {
            while (cursor < frontend->source_length &&
                   frontend->source[cursor] != '\n') cursor++;
            continue;
        }
        start = cursor;
        if (identifier_start(byte)) {
            cursor++;
            while (cursor < frontend->source_length &&
                   identifier_continue((unsigned char)frontend->source[cursor])) {
                cursor++;
            }
            if (cursor - start >= NAME_LIMIT) {
                set_error(frontend, "HML007", start, cursor,
                    "identifier limit is %u bytes", (unsigned)(NAME_LIMIT - 1u));
                break;
            }
            (void)add_token(frontend, TOKEN_IDENTIFIER, start, cursor);
            continue;
        }
        if (isdigit(byte) != 0) {
            cursor++;
            while (cursor < frontend->source_length &&
                   isdigit((unsigned char)frontend->source[cursor]) != 0) cursor++;
            (void)add_token(frontend, TOKEN_NUMBER, start, cursor);
            continue;
        }
        if (byte == (unsigned char)'"') {
            bool closed = false;
            cursor++;
            while (cursor < frontend->source_length) {
                unsigned char current = (unsigned char)frontend->source[cursor++];
                if (current == (unsigned char)'\\') {
                    if (cursor >= frontend->source_length) break;
                    cursor++;
                } else if (current == (unsigned char)'"') {
                    closed = true;
                    break;
                } else if (current == (unsigned char)'\n') {
                    break;
                }
            }
            if (!closed) {
                set_error(frontend, "HML001", start, cursor,
                    "unterminated Text literal");
                break;
            }
            (void)add_token(frontend, TOKEN_TEXT, start, cursor);
            continue;
        }
        switch (byte) {
            case '(':
                cursor++;
                (void)add_token(frontend, TOKEN_LEFT_PAREN, start, cursor);
                break;
            case ')':
                cursor++;
                (void)add_token(frontend, TOKEN_RIGHT_PAREN, start, cursor);
                break;
            case '{':
                cursor++;
                (void)add_token(frontend, TOKEN_LEFT_BRACE, start, cursor);
                break;
            case '}':
                cursor++;
                (void)add_token(frontend, TOKEN_RIGHT_BRACE, start, cursor);
                break;
            case ':':
                cursor++;
                (void)add_token(frontend, TOKEN_COLON, start, cursor);
                break;
            case ',':
                cursor++;
                (void)add_token(frontend, TOKEN_COMMA, start, cursor);
                break;
            case '=':
                cursor++;
                if (cursor < frontend->source_length &&
                    frontend->source[cursor] == '>') {
                    cursor++;
                    (void)add_token(frontend, TOKEN_FAT_ARROW, start, cursor);
                } else {
                    (void)add_token(frontend, TOKEN_EQUAL, start, cursor);
                }
                break;
            case '-':
                cursor++;
                if (cursor < frontend->source_length &&
                    frontend->source[cursor] == '>') {
                    cursor++;
                    (void)add_token(frontend, TOKEN_THIN_ARROW, start, cursor);
                } else {
                    set_error(frontend, "HML006", start, cursor,
                        "operators are outside the bounded HM frontend");
                }
                break;
            default:
                if (byte >= 0x80u) {
                    set_error(frontend, "HML006", start, start + 1u,
                        "non-ASCII identifiers are outside the bounded HM frontend");
                } else {
                    set_error(frontend, "HML006", start, start + 1u,
                        "unsupported token `%c`", frontend->source[start]);
                }
                cursor++;
                break;
        }
    }
    if (!frontend->failed) {
        (void)add_token(
            frontend,
            TOKEN_EOF,
            frontend->source_length,
            frontend->source_length
        );
    }
    return !frontend->failed;
}

static Token *peek(Frontend *frontend) {
    if (frontend->cursor >= frontend->token_count) {
        return &frontend->tokens[frontend->token_count - 1u];
    }
    return &frontend->tokens[frontend->cursor];
}

static bool token_is(const Frontend *frontend, const Token *token, const char *text) {
    size_t length = strlen(text);
    return token->kind == TOKEN_IDENTIFIER && token->end - token->start == length &&
        memcmp(frontend->source + token->start, text, length) == 0;
}

static void token_text(
    const Frontend *frontend,
    const Token *token,
    char destination[NAME_LIMIT]
) {
    size_t length = token->end - token->start;
    if (length >= NAME_LIMIT) length = NAME_LIMIT - 1u;
    memcpy(destination, frontend->source + token->start, length);
    destination[length] = '\0';
}

static bool consume_kind(Frontend *frontend, TokenKind kind, Token *result) {
    Token *token = peek(frontend);
    if (token->kind != kind) return false;
    if (result != NULL) *result = *token;
    frontend->cursor++;
    return true;
}

static bool consume_word(Frontend *frontend, const char *word, Token *result) {
    Token *token = peek(frontend);
    if (!token_is(frontend, token, word)) return false;
    if (result != NULL) *result = *token;
    frontend->cursor++;
    return true;
}

static bool expect_kind(
    Frontend *frontend,
    TokenKind kind,
    const char *description,
    Token *result
) {
    Token *token = peek(frontend);
    if (consume_kind(frontend, kind, result)) return true;
    set_error(frontend, "HML001", token->start, token->end,
        "expected %s", description);
    return false;
}

static bool expect_word(
    Frontend *frontend,
    const char *word,
    Token *result
) {
    Token *token = peek(frontend);
    if (consume_word(frontend, word, result)) return true;
    set_error(frontend, "HML001", token->start, token->end,
        "expected `%s`", word);
    return false;
}

static int add_type(Frontend *frontend, TypeKind kind) {
    Type *type;
    if (frontend->type_count >= TYPE_LIMIT) {
        Token *token = peek(frontend);
        set_error(frontend, "HML007", token->start, token->end,
            "type-node limit is %u", (unsigned)TYPE_LIMIT);
        return -1;
    }
    type = &frontend->types[frontend->type_count];
    memset(type, 0, sizeof(*type));
    type->kind = kind;
    type->left = -1;
    type->right = -1;
    type->link = -1;
    return (int)frontend->type_count++;
}

static int add_meta(Frontend *frontend, int level) {
    int index = add_type(frontend, TYPE_META);
    if (index >= 0) {
        frontend->types[index].level = level;
        frontend->types[index].id = frontend->next_meta_id++;
    }
    return index;
}

static int add_function(Frontend *frontend, int argument, int result) {
    int index = add_type(frontend, TYPE_FUNCTION);
    if (index >= 0) {
        frontend->types[index].left = argument;
        frontend->types[index].right = result;
    }
    return index;
}

static int prune(Frontend *frontend, int type) {
    Type *node;
    if (type < 0 || (size_t)type >= frontend->type_count) return type;
    node = &frontend->types[type];
    if (node->kind == TYPE_META && node->link >= 0) {
        node->link = prune(frontend, node->link);
        return node->link;
    }
    return type;
}

static bool occurs_and_lower(
    Frontend *frontend,
    int meta,
    int type,
    int target_level
) {
    Type *node;
    type = prune(frontend, type);
    if (type == meta) return true;
    node = &frontend->types[type];
    if (node->kind == TYPE_META) {
        if (node->level > target_level) node->level = target_level;
        return false;
    }
    if (node->kind == TYPE_FUNCTION) {
        if (occurs_and_lower(frontend, meta, node->left, target_level)) return true;
        return occurs_and_lower(frontend, meta, node->right, target_level);
    }
    return false;
}

static void buffer_append(TextBuffer *buffer, const char *text) {
    size_t length;
    if (buffer->overflow) return;
    length = strlen(text);
    if (length > sizeof(buffer->bytes) - 1u - buffer->length) {
        buffer->overflow = true;
        return;
    }
    memcpy(buffer->bytes + buffer->length, text, length);
    buffer->length += length;
    buffer->bytes[buffer->length] = '\0';
}

static void buffer_format(TextBuffer *buffer, const char *format, ...) {
    va_list arguments;
    int written;
    size_t remaining;
    if (buffer->overflow) return;
    remaining = sizeof(buffer->bytes) - buffer->length;
    va_start(arguments, format);
    written = vsnprintf(buffer->bytes + buffer->length, remaining, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= remaining) {
        buffer->overflow = true;
        return;
    }
    buffer->length += (size_t)written;
}

static ptrdiff_t printer_general(const TypePrinter *printer, int meta) {
    size_t index;
    for (index = 0u; index < printer->general_count; index++) {
        if (printer->generals[index] == meta) return (ptrdiff_t)index;
    }
    return -1;
}

static size_t printer_monomorphic(TypePrinter *printer, int meta) {
    size_t index;
    for (index = 0u; index < printer->monomorphic_count; index++) {
        if (printer->monomorphic[index] == meta) return index;
    }
    if (printer->monomorphic_count < GENERAL_LIMIT * 2u) {
        index = printer->monomorphic_count++;
        printer->monomorphic[index] = meta;
        return index;
    }
    return GENERAL_LIMIT * 2u;
}

static void print_type_into(
    Frontend *frontend,
    int type,
    TypePrinter *printer,
    TextBuffer *buffer
) {
    Type *node;
    ptrdiff_t general;
    type = prune(frontend, type);
    node = &frontend->types[type];
    switch (node->kind) {
        case TYPE_INT:
            buffer_append(buffer, "Int");
            break;
        case TYPE_BOOL:
            buffer_append(buffer, "Bool");
            break;
        case TYPE_TEXT:
            buffer_append(buffer, "Text");
            break;
        case TYPE_FUNCTION:
            buffer_append(buffer, "(");
            print_type_into(frontend, node->left, printer, buffer);
            buffer_append(buffer, " -> ");
            print_type_into(frontend, node->right, printer, buffer);
            buffer_append(buffer, ")");
            break;
        case TYPE_META:
            general = printer_general(printer, type);
            if (general >= 0) {
                buffer_format(buffer, "'%c", (int)('a' + (general % 26)));
                if (general >= 26) buffer_format(buffer, "%td", general / 26);
            } else {
                buffer_format(buffer, "_%zu", printer_monomorphic(printer, type));
            }
            break;
    }
}

static void type_text(
    Frontend *frontend,
    int type,
    const int *generals,
    size_t general_count,
    char destination[TYPE_TEXT_LIMIT]
) {
    TypePrinter printer;
    TextBuffer buffer;
    memset(&printer, 0, sizeof(printer));
    memset(&buffer, 0, sizeof(buffer));
    printer.generals = generals;
    printer.general_count = general_count;
    print_type_into(frontend, type, &printer, &buffer);
    if (buffer.overflow) {
        (void)snprintf(destination, TYPE_TEXT_LIMIT, "<type-too-large>");
    } else {
        (void)snprintf(destination, TYPE_TEXT_LIMIT, "%s", buffer.bytes);
    }
}

static bool unify(
    Frontend *frontend,
    int left,
    int right,
    size_t start,
    size_t end,
    size_t related_start,
    size_t related_end
) {
    Type *left_node;
    Type *right_node;
    char left_text[TYPE_TEXT_LIMIT];
    char right_text[TYPE_TEXT_LIMIT];
    left = prune(frontend, left);
    right = prune(frontend, right);
    if (left == right) return true;
    left_node = &frontend->types[left];
    right_node = &frontend->types[right];
    if (left_node->kind == TYPE_META) {
        if (occurs_and_lower(frontend, left, right, left_node->level)) {
            set_related_error(frontend, "HML003", start, end,
                related_start, related_end,
                "occurs check failed: a type cannot contain itself");
            return false;
        }
        left_node->link = right;
        return true;
    }
    if (right_node->kind == TYPE_META) {
        return unify(frontend, right, left, start, end, related_start, related_end);
    }
    if (left_node->kind == TYPE_FUNCTION && right_node->kind == TYPE_FUNCTION) {
        if (!unify(frontend, left_node->left, right_node->left,
                start, end, related_start, related_end)) return false;
        return unify(frontend, left_node->right, right_node->right,
            start, end, related_start, related_end);
    }
    if (left_node->kind == right_node->kind) return true;
    type_text(frontend, left, NULL, 0u, left_text);
    type_text(frontend, right, NULL, 0u, right_text);
    set_related_error(frontend, "HML002", start, end,
        related_start, related_end,
        "cannot unify %s with %s", left_text, right_text);
    return false;
}

static bool general_contains(const int *generals, size_t count, int meta) {
    size_t index;
    for (index = 0u; index < count; index++) {
        if (generals[index] == meta) return true;
    }
    return false;
}

static void collect_generals(
    Frontend *frontend,
    int type,
    int environment_level,
    int generals[GENERAL_LIMIT],
    size_t *count
) {
    Type *node;
    type = prune(frontend, type);
    node = &frontend->types[type];
    if (node->kind == TYPE_META) {
        if (node->level > environment_level &&
            !general_contains(generals, *count, type)) {
            if (*count >= GENERAL_LIMIT) {
                Token *token = peek(frontend);
                set_error(frontend, "HML007", token->start, token->end,
                    "generalized-variable limit is %u", (unsigned)GENERAL_LIMIT);
                return;
            }
            generals[(*count)++] = type;
        }
        return;
    }
    if (node->kind == TYPE_FUNCTION) {
        collect_generals(frontend, node->left, environment_level, generals, count);
        collect_generals(frontend, node->right, environment_level, generals, count);
    }
}

static int compare_meta_id(const void *left, const void *right, void *context) {
    const Frontend *frontend = context;
    int left_index = *(const int *)left;
    int right_index = *(const int *)right;
    unsigned left_id = frontend->types[left_index].id;
    unsigned right_id = frontend->types[right_index].id;
    return left_id < right_id ? -1 : left_id > right_id ? 1 : 0;
}

static void sort_generals(Frontend *frontend, int *generals, size_t count) {
    size_t left;
    for (left = 1u; left < count; left++) {
        int value = generals[left];
        size_t cursor = left;
        while (cursor > 0u &&
               compare_meta_id(&value, &generals[cursor - 1u], frontend) < 0) {
            generals[cursor] = generals[cursor - 1u];
            cursor--;
        }
        generals[cursor] = value;
    }
}

static int instantiate_type(
    Frontend *frontend,
    int type,
    const int *generals,
    size_t general_count,
    int *fresh,
    int level
) {
    Type *node;
    size_t index;
    type = prune(frontend, type);
    node = &frontend->types[type];
    if (node->kind == TYPE_META) {
        for (index = 0u; index < general_count; index++) {
            if (generals[index] == type) {
                if (fresh[index] < 0) fresh[index] = add_meta(frontend, level);
                return fresh[index];
            }
        }
        return type;
    }
    if (node->kind == TYPE_FUNCTION) {
        int argument = instantiate_type(
            frontend, node->left, generals, general_count, fresh, level);
        int result = instantiate_type(
            frontend, node->right, generals, general_count, fresh, level);
        if (frontend->failed) return -1;
        return add_function(frontend, argument, result);
    }
    return type;
}

static ptrdiff_t find_binding(const Frontend *frontend, const char *name) {
    size_t cursor = frontend->binding_count;
    while (cursor > 0u) {
        const Binding *binding = &frontend->bindings[--cursor];
        if (binding->active && strcmp(binding->name, name) == 0) {
            return (ptrdiff_t)cursor;
        }
    }
    return -1;
}

static ptrdiff_t add_binding(
    Frontend *frontend,
    const char *name,
    int type,
    const int *generals,
    size_t general_count,
    size_t start,
    size_t end,
    bool parameter
) {
    Binding *binding;
    size_t index;
    unsigned shadow = 0u;
    if (frontend->binding_count >= BINDING_LIMIT) {
        set_error(frontend, "HML007", start, end,
            "binding limit is %u", (unsigned)BINDING_LIMIT);
        return -1;
    }
    for (index = 0u; index < frontend->binding_count; index++) {
        if (strcmp(frontend->bindings[index].name, name) == 0) shadow++;
    }
    binding = &frontend->bindings[frontend->binding_count];
    memset(binding, 0, sizeof(*binding));
    (void)snprintf(binding->name, sizeof(binding->name), "%s", name);
    (void)snprintf(binding->identity, sizeof(binding->identity),
        "binding:%s:%u", name, shadow);
    binding->type = type;
    binding->general_count = general_count;
    for (index = 0u; index < general_count; index++) {
        binding->generals[index] = generals[index];
    }
    binding->start = start;
    binding->end = end;
    binding->scope = frontend->scope;
    binding->active = true;
    binding->parameter = parameter;
    return (ptrdiff_t)frontend->binding_count++;
}

static bool add_use(
    Frontend *frontend,
    size_t binding,
    int type,
    size_t start,
    size_t end
) {
    Use *use;
    if (frontend->use_count >= USE_LIMIT) {
        set_error(frontend, "HML007", start, end,
            "use limit is %u", (unsigned)USE_LIMIT);
        return false;
    }
    use = &frontend->uses[frontend->use_count++];
    use->binding = binding;
    use->type = type;
    use->start = start;
    use->end = end;
    return true;
}

static void deactivate_from(Frontend *frontend, size_t start) {
    size_t index;
    for (index = start; index < frontend->binding_count; index++) {
        frontend->bindings[index].active = false;
    }
}

static int parse_type(Frontend *frontend);
static Expression parse_expression(Frontend *frontend, int level);

static int parse_type_atom(Frontend *frontend) {
    Token token;
    if (consume_word(frontend, "Int", &token)) return 0;
    if (consume_word(frontend, "Bool", &token)) return 1;
    if (consume_word(frontend, "Text", &token)) return 2;
    if (consume_kind(frontend, TOKEN_LEFT_PAREN, &token)) {
        int type = parse_type(frontend);
        (void)expect_kind(frontend, TOKEN_RIGHT_PAREN, "`)` in type", NULL);
        return type;
    }
    token = *peek(frontend);
    if (token.kind == TOKEN_IDENTIFIER) {
        char name[NAME_LIMIT];
        token_text(frontend, &token, name);
        set_error(frontend, "HML006", token.start, token.end,
            "named type `%s` is outside the bounded HM frontend", name);
    } else {
        set_error(frontend, "HML001", token.start, token.end,
            "expected Int, Bool, Text, or a parenthesized function type");
    }
    return -1;
}

static int parse_type(Frontend *frontend) {
    int left;
    frontend->type_parse_depth++;
    if (frontend->type_parse_depth > PARSE_DEPTH_LIMIT) {
        Token *token = peek(frontend);
        set_error(frontend, "HML007", token->start, token->end,
            "type nesting limit is %u", (unsigned)PARSE_DEPTH_LIMIT);
        frontend->type_parse_depth--;
        return -1;
    }
    left = parse_type_atom(frontend);
    if (frontend->failed) {
        frontend->type_parse_depth--;
        return -1;
    }
    if (consume_kind(frontend, TOKEN_THIN_ARROW, NULL)) {
        int right = parse_type(frontend);
        if (frontend->failed) {
            frontend->type_parse_depth--;
            return -1;
        }
        left = add_function(frontend, left, right);
    }
    frontend->type_parse_depth--;
    return left;
}

static bool unsupported_word(const char *name) {
    static const char *const words[] = {
        "async", "await", "edit", "effect", "handle", "impl", "match",
        "mut", "perform", "read", "record", "take", "trait", "type",
        "var", "with"
    };
    size_t index;
    for (index = 0u; index < sizeof(words) / sizeof(words[0]); index++) {
        if (strcmp(name, words[index]) == 0) return true;
    }
    return false;
}

static Expression expression_failure(void) {
    Expression result;
    memset(&result, 0, sizeof(result));
    result.type = -1;
    return result;
}

static Expression parse_block(Frontend *frontend, int level) {
    Token open;
    Token close;
    Expression result = expression_failure();
    size_t binding_start;
    unsigned previous_scope;
    if (!expect_kind(frontend, TOKEN_LEFT_BRACE, "`{`", &open)) return result;
    previous_scope = frontend->scope;
    frontend->scope++;
    binding_start = frontend->binding_count;
    while (consume_word(frontend, "let", NULL)) {
        Token name_token;
        char name[NAME_LIMIT];
        int annotation = -1;
        Expression initializer;
        int generals[GENERAL_LIMIT];
        size_t general_count = 0u;
        if (consume_word(frontend, "mut", &name_token)) {
            set_error(frontend, "HML006", name_token.start, name_token.end,
                "mutable bindings are outside the bounded HM frontend");
            break;
        }
        if (!expect_kind(frontend, TOKEN_IDENTIFIER, "a binding name", &name_token)) {
            break;
        }
        token_text(frontend, &name_token, name);
        if (unsupported_word(name) || strcmp(name, "fn") == 0 ||
            strcmp(name, "let") == 0) {
            set_error(frontend, "HML006", name_token.start, name_token.end,
                "`%s` cannot be a binding in this frontend", name);
            break;
        }
        if (consume_kind(frontend, TOKEN_COLON, NULL)) {
            annotation = parse_type(frontend);
            if (frontend->failed) break;
        }
        if (!expect_kind(frontend, TOKEN_EQUAL, "`=` in let binding", NULL)) break;
        frontend->defining = true;
        (void)snprintf(frontend->defining_name,
            sizeof(frontend->defining_name), "%s", name);
        initializer = parse_expression(frontend, level + 1);
        frontend->defining = false;
        frontend->defining_name[0] = '\0';
        if (frontend->failed) break;
        if (annotation >= 0 && !unify(frontend, annotation, initializer.type,
                name_token.start, name_token.end,
                initializer.start, initializer.end)) break;
        if (initializer.lambda) {
            collect_generals(frontend, initializer.type, level,
                generals, &general_count);
            sort_generals(frontend, generals, general_count);
        }
        (void)add_binding(frontend, name, initializer.type,
            generals, general_count, name_token.start, initializer.end, false);
        if (frontend->failed) break;
    }
    if (!frontend->failed) {
        if (peek(frontend)->kind == TOKEN_RIGHT_BRACE) {
            Token *token = peek(frontend);
            set_error(frontend, "HML001", token->start, token->end,
                "a block must end in a value expression");
        } else {
            result = parse_expression(frontend, level);
        }
    }
    if (!frontend->failed &&
        expect_kind(frontend, TOKEN_RIGHT_BRACE, "`}`", &close)) {
        result.start = open.start;
        result.end = close.end;
    }
    deactivate_from(frontend, binding_start);
    frontend->scope = previous_scope;
    return frontend->failed ? expression_failure() : result;
}

static Expression parse_lambda(Frontend *frontend, Token start, int level) {
    Expression result = expression_failure();
    Token parameter_token;
    char parameter_name[NAME_LIMIT];
    int parameter_type;
    int annotation = -1;
    ptrdiff_t parameter;
    size_t binding_start = frontend->binding_count;
    unsigned previous_scope = frontend->scope;
    if (!expect_kind(frontend, TOKEN_LEFT_PAREN, "`(` after fn", NULL)) return result;
    if (!expect_kind(frontend, TOKEN_IDENTIFIER, "one lambda parameter", &parameter_token)) {
        return result;
    }
    token_text(frontend, &parameter_token, parameter_name);
    if (consume_kind(frontend, TOKEN_COMMA, NULL)) {
        set_error(frontend, "HML006", parameter_token.start, peek(frontend)->end,
            "multi-parameter lambdas are outside the bounded HM frontend");
        return result;
    }
    if (consume_kind(frontend, TOKEN_COLON, NULL)) {
        annotation = parse_type(frontend);
        if (frontend->failed) return result;
    }
    if (!expect_kind(frontend, TOKEN_RIGHT_PAREN, "`)` after lambda parameter", NULL) ||
        !expect_kind(frontend, TOKEN_FAT_ARROW, "`=>` after lambda parameter", NULL)) {
        return result;
    }
    parameter_type = add_meta(frontend, level + 1);
    if (annotation >= 0 && !unify(frontend, parameter_type, annotation,
            parameter_token.start, parameter_token.end,
            parameter_token.start, parameter_token.end)) return result;
    frontend->scope++;
    parameter = add_binding(frontend, parameter_name, parameter_type,
        NULL, 0u, parameter_token.start, parameter_token.end, true);
    if (parameter < 0) return result;
    if (peek(frontend)->kind == TOKEN_LEFT_BRACE) {
        result = parse_block(frontend, level + 1);
    } else {
        result = parse_expression(frontend, level + 1);
    }
    if (!frontend->failed) {
        result.type = add_function(frontend, parameter_type, result.type);
        result.start = start.start;
        result.lambda = true;
    }
    deactivate_from(frontend, binding_start);
    frontend->scope = previous_scope;
    return frontend->failed ? expression_failure() : result;
}

static Expression parse_primary(Frontend *frontend, int level) {
    Token token = *peek(frontend);
    Expression result = expression_failure();
    if (consume_kind(frontend, TOKEN_NUMBER, &token)) {
        result.type = 0;
        result.start = token.start;
        result.end = token.end;
        return result;
    }
    if (consume_kind(frontend, TOKEN_TEXT, &token)) {
        result.type = 2;
        result.start = token.start;
        result.end = token.end;
        return result;
    }
    if (consume_word(frontend, "true", &token) ||
        consume_word(frontend, "false", &token)) {
        result.type = 1;
        result.start = token.start;
        result.end = token.end;
        return result;
    }
    if (consume_word(frontend, "fn", &token)) {
        return parse_lambda(frontend, token, level);
    }
    if (consume_kind(frontend, TOKEN_LEFT_PAREN, &token)) {
        result = parse_expression(frontend, level);
        (void)expect_kind(frontend, TOKEN_RIGHT_PAREN, "`)`", &token);
        if (!frontend->failed) result.end = token.end;
        return result;
    }
    if (token.kind == TOKEN_LEFT_BRACE) {
        set_error(frontend, "HML006", token.start, token.end,
            "record and row expressions are outside the bounded HM frontend");
        return result;
    }
    if (consume_kind(frontend, TOKEN_IDENTIFIER, &token)) {
        char name[NAME_LIMIT];
        ptrdiff_t binding_index;
        int fresh[GENERAL_LIMIT];
        size_t index;
        token_text(frontend, &token, name);
        if (unsupported_word(name)) {
            set_error(frontend, "HML006", token.start, token.end,
                "`%s` is outside the bounded HM frontend", name);
            return result;
        }
        binding_index = find_binding(frontend, name);
        if (binding_index < 0) {
            if (frontend->defining && strcmp(frontend->defining_name, name) == 0) {
                set_error(frontend, "HML005", token.start, token.end,
                    "recursive binding `%s` is outside the bounded HM frontend", name);
            } else {
                set_error(frontend, "HML004", token.start, token.end,
                    "unknown local binding `%s`", name);
            }
            return result;
        }
        for (index = 0u; index < GENERAL_LIMIT; index++) fresh[index] = -1;
        result.type = instantiate_type(
            frontend,
            frontend->bindings[binding_index].type,
            frontend->bindings[binding_index].generals,
            frontend->bindings[binding_index].general_count,
            fresh,
            level
        );
        result.start = token.start;
        result.end = token.end;
        (void)add_use(frontend, (size_t)binding_index, result.type,
            token.start, token.end);
        return result;
    }
    set_error(frontend, "HML001", token.start, token.end,
        "expected a literal, local binding, lambda, or parenthesized expression");
    return result;
}

static Expression parse_expression(Frontend *frontend, int level) {
    Expression result;
    frontend->parse_depth++;
    if (frontend->parse_depth > PARSE_DEPTH_LIMIT) {
        Token *token = peek(frontend);
        set_error(frontend, "HML007", token->start, token->end,
            "expression nesting limit is %u", (unsigned)PARSE_DEPTH_LIMIT);
        frontend->parse_depth--;
        return expression_failure();
    }
    result = parse_primary(frontend, level);
    while (!frontend->failed && peek(frontend)->kind == TOKEN_LEFT_PAREN) {
        Token open;
        Token close;
        Expression argument;
        int call_result;
        int expected;
        (void)consume_kind(frontend, TOKEN_LEFT_PAREN, &open);
        argument = parse_expression(frontend, level);
        if (consume_kind(frontend, TOKEN_COMMA, NULL)) {
            set_error(frontend, "HML006", open.start, peek(frontend)->end,
                "multi-argument calls are outside the bounded HM frontend");
            break;
        }
        if (!expect_kind(frontend, TOKEN_RIGHT_PAREN, "`)` after argument", &close)) {
            break;
        }
        call_result = add_meta(frontend, level);
        expected = add_function(frontend, argument.type, call_result);
        if (frontend->failed || !unify(frontend, result.type, expected,
                result.start, result.end, argument.start, argument.end)) break;
        result.type = call_result;
        result.end = close.end;
        result.lambda = false;
    }
    frontend->parse_depth--;
    return frontend->failed ? expression_failure() : result;
}

static bool parse_program(Frontend *frontend) {
    Token function_token;
    Token name_token;
    char name[NAME_LIMIT];
    Expression body;
    if (peek(frontend)->kind == TOKEN_IDENTIFIER &&
        !token_is(frontend, peek(frontend), "fn")) {
        char first[NAME_LIMIT];
        token_text(frontend, peek(frontend), first);
        if (unsupported_word(first)) {
            set_error(frontend, "HML006", peek(frontend)->start, peek(frontend)->end,
                "`%s` is outside the bounded HM frontend", first);
            return false;
        }
    }
    if (!expect_word(frontend, "fn", &function_token) ||
        !expect_kind(frontend, TOKEN_IDENTIFIER, "function name", &name_token)) {
        return false;
    }
    token_text(frontend, &name_token, name);
    if (strcmp(name, "main") != 0) {
        set_error(frontend, "HML006", name_token.start, name_token.end,
            "named-function inference is outside this slice; expected `main`");
        return false;
    }
    if (!expect_kind(frontend, TOKEN_LEFT_PAREN, "`(` after main", NULL) ||
        !expect_kind(frontend, TOKEN_RIGHT_PAREN, "`)` after main", NULL)) {
        return false;
    }
    if (peek(frontend)->kind == TOKEN_THIN_ARROW) {
        Token *token = peek(frontend);
        set_error(frontend, "HML006", token->start, token->end,
            "named-function result annotations are outside this slice");
        return false;
    }
    body = parse_block(frontend, 0);
    if (frontend->failed) return false;
    if (peek(frontend)->kind != TOKEN_EOF) {
        Token *token = peek(frontend);
        set_error(frontend, "HML006", token->start, token->end,
            "only one non-recursive `main` wrapper is accepted");
        return false;
    }
    frontend->result_type = body.type;
    frontend->result_start = body.start;
    frontend->result_end = body.end;
    return true;
}

static const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case TOKEN_IDENTIFIER: return "identifier";
        case TOKEN_NUMBER: return "integer";
        case TOKEN_TEXT: return "text";
        case TOKEN_LEFT_PAREN: return "left-paren";
        case TOKEN_RIGHT_PAREN: return "right-paren";
        case TOKEN_LEFT_BRACE: return "left-brace";
        case TOKEN_RIGHT_BRACE: return "right-brace";
        case TOKEN_COLON: return "colon";
        case TOKEN_COMMA: return "comma";
        case TOKEN_EQUAL: return "equal";
        case TOKEN_FAT_ARROW: return "fat-arrow";
        case TOKEN_THIN_ARROW: return "thin-arrow";
        case TOKEN_EOF: return "eof";
    }
    return "unknown";
}

static bool temporary_path(const char *path, char result[PATH_LIMIT]) {
    int written = snprintf(result, PATH_LIMIT, "%s.hm-levels.tmp", path);
    return written >= 0 && (size_t)written < PATH_LIMIT;
}

typedef struct {
    char ir_temporary[PATH_LIMIT];
    char token_temporary[PATH_LIMIT];
} OutputPaths;

static bool same_existing_file(const char *left, const char *right) {
    struct stat left_status;
    struct stat right_status;
    if (stat(left, &left_status) != 0 || stat(right, &right_status) != 0) {
        return false;
    }
    return left_status.st_dev == right_status.st_dev &&
        left_status.st_ino == right_status.st_ino;
}

static bool validate_paths(
    const char *source,
    const char *ir_output,
    const char *token_output,
    OutputPaths *outputs
) {
    const char *paths[5];
    size_t left;
    size_t right;
    if (source[0] == '\0' || ir_output[0] == '\0' || token_output[0] == '\0') {
        return false;
    }
    if (strlen(source) >= PATH_LIMIT || strlen(ir_output) >= PATH_LIMIT ||
        strlen(token_output) >= PATH_LIMIT) {
        return false;
    }
    if (!temporary_path(ir_output, outputs->ir_temporary) ||
        !temporary_path(token_output, outputs->token_temporary)) {
        return false;
    }
    paths[0] = source;
    paths[1] = ir_output;
    paths[2] = token_output;
    paths[3] = outputs->ir_temporary;
    paths[4] = outputs->token_temporary;
    for (left = 0u; left < 5u; left++) {
        for (right = left + 1u; right < 5u; right++) {
            if (strcmp(paths[left], paths[right]) == 0 ||
                same_existing_file(paths[left], paths[right])) {
                return false;
            }
        }
    }
    return true;
}

static void remove_outputs(
    const char *ir_output,
    const char *token_output,
    const OutputPaths *outputs
) {
    (void)remove(ir_output);
    (void)remove(token_output);
    (void)remove(outputs->ir_temporary);
    (void)remove(outputs->token_temporary);
}

static bool write_ir(Frontend *frontend, const char *path) {
    char temporary[PATH_LIMIT];
    FILE *file;
    size_t index;
    if (!temporary_path(path, temporary)) return false;
    (void)remove(temporary);
    file = fopen(temporary, "wb");
    if (file == NULL) return false;
    if (fprintf(file, "kofun-hm-levels-ir/v1\n") < 0) goto fail;
    for (index = 0u; index < frontend->binding_count; index++) {
        Binding *binding = &frontend->bindings[index];
        char type[TYPE_TEXT_LIMIT];
        size_t general;
        type_text(frontend, binding->type,
            binding->generals, binding->general_count, type);
        if (fprintf(file,
                "binding|binding-id=%s|name=%s|role=%s|scheme=",
                binding->identity,
                binding->name,
                binding->parameter ? "parameter" : "let") < 0) goto fail;
        if (binding->general_count > 0u) {
            if (fputs("forall ", file) == EOF) goto fail;
            for (general = 0u; general < binding->general_count; general++) {
                if (general > 0u && fputc(',', file) == EOF) goto fail;
                if (fprintf(file, "'%c", (int)('a' + (general % 26u))) < 0) goto fail;
                if (general >= 26u && fprintf(file, "%zu", general / 26u) < 0) goto fail;
            }
            if (fputs(". ", file) == EOF) goto fail;
        }
        if (fprintf(file, "%s|span=%zu..%zu\n",
                type, binding->start, binding->end) < 0) goto fail;
    }
    for (index = 0u; index < frontend->use_count; index++) {
        Use *use = &frontend->uses[index];
        Binding *binding = &frontend->bindings[use->binding];
        char type[TYPE_TEXT_LIMIT];
        type_text(frontend, use->type, NULL, 0u, type);
        if (fprintf(file,
                "use|binding-id=%s|name=%s|type=%s|span=%zu..%zu\n",
                binding->identity, binding->name, type,
                use->start, use->end) < 0) goto fail;
    }
    {
        char type[TYPE_TEXT_LIMIT];
        type_text(frontend, frontend->result_type, NULL, 0u, type);
        if (fprintf(file, "result|type=%s|span=%zu..%zu\n",
                type, frontend->result_start, frontend->result_end) < 0) goto fail;
    }
    if (fclose(file) != 0) {
        (void)remove(temporary);
        return false;
    }
    (void)remove(path);
    if (rename(temporary, path) != 0) {
        (void)remove(temporary);
        return false;
    }
    return true;
fail:
    (void)fclose(file);
    (void)remove(temporary);
    return false;
}

static bool write_tokens(Frontend *frontend, const char *path) {
    char temporary[PATH_LIMIT];
    FILE *file;
    size_t index;
    if (!temporary_path(path, temporary)) return false;
    (void)remove(temporary);
    file = fopen(temporary, "wb");
    if (file == NULL) return false;
    if (fputs("kofun-hm-levels-tokens/v1\n", file) == EOF) goto fail;
    for (index = 0u; index < frontend->token_count; index++) {
        Token *token = &frontend->tokens[index];
        if (fprintf(file, "%s|%zu..%zu\n",
                token_kind_name(token->kind), token->start, token->end) < 0) goto fail;
    }
    if (fclose(file) != 0) {
        (void)remove(temporary);
        return false;
    }
    (void)remove(path);
    if (rename(temporary, path) != 0) {
        (void)remove(temporary);
        return false;
    }
    return true;
fail:
    (void)fclose(file);
    (void)remove(temporary);
    return false;
}

static bool read_source(Frontend *frontend, const char *path) {
    FILE *file = fopen(path, "rb");
    size_t read;
    int extra;
    if (file == NULL) return false;
    read = fread(frontend->source, 1u, SOURCE_LIMIT, file);
    if (ferror(file)) {
        (void)fclose(file);
        return false;
    }
    extra = fgetc(file);
    if (fclose(file) != 0) return false;
    if (extra != EOF) {
        set_error(frontend, "HML007", 0u, SOURCE_LIMIT,
            "source limit is %u bytes", (unsigned)SOURCE_LIMIT);
        return true;
    }
    frontend->source_length = read;
    frontend->source[read] = '\0';
    return true;
}

static void print_error(const Frontend *frontend) {
    if (frontend->error_has_related) {
        (void)printf("error[%s]: %s at byte %zu..%zu; related byte %zu..%zu\n",
            frontend->error_code,
            frontend->error_message,
            frontend->error_start,
            frontend->error_end,
            frontend->related_start,
            frontend->related_end);
    } else {
        (void)printf("error[%s]: %s at byte %zu..%zu\n",
            frontend->error_code,
            frontend->error_message,
            frontend->error_start,
            frontend->error_end);
    }
}

int main(int argument_count, char **arguments) {
    Frontend *frontend;
    OutputPaths outputs;
    int status = 0;
    if (argument_count != 4) {
        (void)fprintf(stderr,
            "usage: hm_levels_frontend SOURCE IR_OUTPUT TOKEN_OUTPUT\n");
        return 2;
    }
    if (!validate_paths(arguments[1], arguments[2], arguments[3], &outputs)) {
        (void)fprintf(stderr,
            "hm levels frontend: unsafe or oversized path arguments\n");
        return 2;
    }
    remove_outputs(arguments[2], arguments[3], &outputs);
    frontend = calloc(1u, sizeof(*frontend));
    if (frontend == NULL) {
        (void)fprintf(stderr, "hm levels frontend: allocation failed\n");
        return 2;
    }
    frontend->result_type = -1;
    (void)add_type(frontend, TYPE_INT);
    (void)add_type(frontend, TYPE_BOOL);
    (void)add_type(frontend, TYPE_TEXT);
    if (!read_source(frontend, arguments[1])) {
        (void)fprintf(stderr, "hm levels frontend: cannot read source\n");
        status = 2;
    } else if (frontend->failed || !lex(frontend) || !parse_program(frontend)) {
        print_error(frontend);
        status = 1;
    } else if (!write_ir(frontend, arguments[2]) ||
               !write_tokens(frontend, arguments[3])) {
        remove_outputs(arguments[2], arguments[3], &outputs);
        (void)fprintf(stderr, "hm levels frontend: cannot publish outputs\n");
        status = 2;
    }
    free(frontend);
    return status;
}
