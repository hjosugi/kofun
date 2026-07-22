#include <ctype.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOURCE_LIMIT (1024u * 1024u)
#define TOKEN_LIMIT 4096u
#define TEXT_LIMIT 128u
#define ADT_LIMIT 64u
#define CONSTRUCTOR_LIMIT 256u
#define FUNCTION_LIMIT 256u

typedef enum {
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_TEXT,
    TOKEN_PUNCTUATION,
    TOKEN_ARROW
} TokenKind;

typedef struct {
    TokenKind kind;
    char text[TEXT_LIMIT];
    size_t start;
    size_t end;
} Token;

typedef struct {
    char name[TEXT_LIMIT];
    size_t start;
    size_t end;
    size_t first_constructor;
    size_t constructor_count;
} Adt;

typedef struct {
    char name[TEXT_LIMIT];
    char field_name[TEXT_LIMIT];
    size_t adt_index;
    size_t local_index;
    unsigned arity;
    size_t start;
    size_t end;
} Constructor;

typedef struct {
    size_t token_start;
    size_t token_end;
} FunctionStub;

typedef struct {
    char function_name[TEXT_LIMIT];
    size_t adt_index;
    size_t constructor_index;
    size_t function_start;
    size_t function_end;
    size_t use_start;
    size_t use_end;
    bool has_payload;
} Construction;

typedef struct {
    Token tokens[TOKEN_LIMIT];
    size_t token_count;
    Adt adts[ADT_LIMIT];
    size_t adt_count;
    Constructor constructors[CONSTRUCTOR_LIMIT];
    size_t constructor_count;
    FunctionStub functions[FUNCTION_LIMIT];
    size_t function_count;
    Construction constructions[FUNCTION_LIMIT];
    size_t construction_count;
    char error[1024];
    bool failed;
} Frontend;

static const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case TOKEN_IDENTIFIER: return "identifier";
        case TOKEN_INTEGER: return "integer";
        case TOKEN_TEXT: return "text";
        case TOKEN_PUNCTUATION: return "punctuation";
        case TOKEN_ARROW: return "arrow";
    }
    return "unknown";
}

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

static bool copy_text(
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
            "E2S46",
            start,
            end,
            "identifier or literal exceeds the bounded ADT frontend limit"
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
            "E2S46",
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
    if (!copy_text(frontend, token->text, source, start, end)) return false;
    frontend->token_count += 1;
    return true;
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
                    frontend,
                    TOKEN_IDENTIFIER,
                    source,
                    start,
                    cursor
                )) return false;
            continue;
        }
        if (isdigit(byte)) {
            cursor += 1;
            while (cursor < length &&
                isdigit((unsigned char)source[cursor])) cursor += 1;
            if (!add_token(
                    frontend,
                    TOKEN_INTEGER,
                    source,
                    start,
                    cursor
                )) return false;
            continue;
        }
        if (source[cursor] == '"') {
            bool escaped = false;
            cursor += 1;
            while (cursor < length) {
                char current = source[cursor];
                cursor += 1;
                if (escaped) {
                    escaped = false;
                } else if (current == '\\') {
                    escaped = true;
                } else if (current == '"') {
                    break;
                } else if (current == '\n') {
                    set_error(
                        frontend,
                        "E2S46",
                        start,
                        cursor,
                        "unterminated text literal"
                    );
                    return false;
                }
            }
            if (cursor > length || source[cursor - 1] != '"') {
                set_error(
                    frontend,
                    "E2S46",
                    start,
                    length,
                    "unterminated text literal"
                );
                return false;
            }
            if (!add_token(frontend, TOKEN_TEXT, source, start, cursor)) {
                return false;
            }
            continue;
        }
        if (source[cursor] == '-' && cursor + 1 < length &&
            source[cursor + 1] == '>') {
            cursor += 2;
            if (!add_token(frontend, TOKEN_ARROW, source, start, cursor)) {
                return false;
            }
            continue;
        }
        if (strchr("=|():,{}[];-", source[cursor]) != NULL) {
            cursor += 1;
            if (!add_token(
                    frontend,
                    TOKEN_PUNCTUATION,
                    source,
                    start,
                    cursor
                )) return false;
            continue;
        }
        set_error(
            frontend,
            "E2S46",
            cursor,
            cursor + 1,
            "unsupported byte 0x%02x in bounded ADT syntax",
            byte
        );
        return false;
    }
    return true;
}

static bool token_is(const Frontend *frontend, size_t index, const char *text) {
    return index < frontend->token_count &&
        strcmp(frontend->tokens[index].text, text) == 0;
}

static bool token_has_kind(
    const Frontend *frontend,
    size_t index,
    TokenKind kind
) {
    return index < frontend->token_count &&
        frontend->tokens[index].kind == kind;
}

static bool expect_token(
    Frontend *frontend,
    size_t *index,
    const char *text,
    const char *context
) {
    size_t start = *index < frontend->token_count
        ? frontend->tokens[*index].start
        : (frontend->token_count == 0
            ? 0
            : frontend->tokens[frontend->token_count - 1].end);
    size_t end = *index < frontend->token_count
        ? frontend->tokens[*index].end
        : start;
    if (!token_is(frontend, *index, text)) {
        set_error(
            frontend,
            "E2S46",
            start,
            end,
            "expected `%s` %s",
            text,
            context
        );
        return false;
    }
    *index += 1;
    return true;
}

static bool expect_identifier(
    Frontend *frontend,
    size_t *index,
    const char *context,
    Token **output
) {
    size_t start = *index < frontend->token_count
        ? frontend->tokens[*index].start
        : (frontend->token_count == 0
            ? 0
            : frontend->tokens[frontend->token_count - 1].end);
    size_t end = *index < frontend->token_count
        ? frontend->tokens[*index].end
        : start;
    if (!token_has_kind(frontend, *index, TOKEN_IDENTIFIER)) {
        set_error(
            frontend,
            "E2S46",
            start,
            end,
            "expected an identifier %s",
            context
        );
        return false;
    }
    *output = &frontend->tokens[*index];
    *index += 1;
    return true;
}

static ptrdiff_t find_adt(const Frontend *frontend, const char *name) {
    size_t index;
    for (index = 0; index < frontend->adt_count; index += 1) {
        if (strcmp(frontend->adts[index].name, name) == 0) {
            return (ptrdiff_t)index;
        }
    }
    return -1;
}

static ptrdiff_t find_constructor(
    const Frontend *frontend,
    const char *name
) {
    size_t index;
    for (index = 0; index < frontend->constructor_count; index += 1) {
        if (strcmp(frontend->constructors[index].name, name) == 0) {
            return (ptrdiff_t)index;
        }
    }
    return -1;
}

static bool add_constructor(
    Frontend *frontend,
    size_t adt_index,
    Token *name,
    Token *field,
    unsigned arity,
    size_t end
) {
    ptrdiff_t existing = find_constructor(frontend, name->text);
    Constructor *constructor;
    if (existing >= 0) {
        const Constructor *first = &frontend->constructors[(size_t)existing];
        if (first->adt_index == adt_index) {
            set_error(
                frontend,
                "E2S37",
                name->start,
                name->end,
                "duplicate constructor `%s`; first declared at bytes %zu..%zu",
                name->text,
                first->start,
                first->end
            );
        } else {
            set_error(
                frontend,
                "E2S38",
                name->start,
                name->end,
                "ambiguous constructor `%s` across `%s` and `%s`; first declared at bytes %zu..%zu",
                name->text,
                frontend->adts[first->adt_index].name,
                frontend->adts[adt_index].name,
                first->start,
                first->end
            );
        }
        return false;
    }
    if (frontend->constructor_count >= CONSTRUCTOR_LIMIT) {
        set_error(
            frontend,
            "E2S46",
            name->start,
            name->end,
            "constructor count exceeds %u",
            CONSTRUCTOR_LIMIT
        );
        return false;
    }
    constructor = &frontend->constructors[frontend->constructor_count];
    snprintf(constructor->name, sizeof(constructor->name), "%s", name->text);
    if (field != NULL) {
        snprintf(
            constructor->field_name,
            sizeof(constructor->field_name),
            "%s",
            field->text
        );
    } else {
        constructor->field_name[0] = '\0';
    }
    constructor->adt_index = adt_index;
    constructor->local_index = frontend->adts[adt_index].constructor_count;
    constructor->arity = arity;
    constructor->start = name->start;
    constructor->end = end;
    frontend->constructor_count += 1;
    frontend->adts[adt_index].constructor_count += 1;
    return true;
}

static bool parse_type(Frontend *frontend, size_t *cursor) {
    size_t index = *cursor;
    size_t type_start = frontend->tokens[index].start;
    size_t adt_index;
    Token *name;
    Adt *adt;

    index += 1;
    if (!expect_identifier(frontend, &index, "after `type`", &name)) {
        return false;
    }
    if (token_is(frontend, index, "[")) {
        set_error(
            frontend,
            "E2S45",
            frontend->tokens[index].start,
            frontend->tokens[index].end,
            "generic ADTs are unsupported in this frontend slice"
        );
        return false;
    }
    {
        ptrdiff_t duplicate = find_adt(frontend, name->text);
        if (duplicate >= 0) {
            const Adt *first = &frontend->adts[(size_t)duplicate];
            set_error(
                frontend,
                "E2S36",
                name->start,
                name->end,
                "duplicate type `%s`; first declared at bytes %zu..%zu",
                name->text,
                first->start,
                first->end
            );
            return false;
        }
    }
    if (frontend->adt_count >= ADT_LIMIT) {
        set_error(
            frontend,
            "E2S46",
            name->start,
            name->end,
            "ADT count exceeds %u",
            ADT_LIMIT
        );
        return false;
    }
    adt_index = frontend->adt_count;
    adt = &frontend->adts[adt_index];
    memset(adt, 0, sizeof(*adt));
    snprintf(adt->name, sizeof(adt->name), "%s", name->text);
    adt->start = type_start;
    adt->first_constructor = frontend->constructor_count;
    frontend->adt_count += 1;

    if (!expect_token(frontend, &index, "=", "after the type name")) {
        return false;
    }
    if (!token_is(frontend, index, "|")) {
        size_t start = index < frontend->token_count
            ? frontend->tokens[index].start
            : name->end;
        size_t end = index < frontend->token_count
            ? frontend->tokens[index].end
            : start;
        set_error(
            frontend,
            "E2S46",
            start,
            end,
            "the first ADT variant must start with `|`"
        );
        return false;
    }

    while (token_is(frontend, index, "|")) {
        Token *constructor_name;
        Token *field_name = NULL;
        unsigned arity = 0;
        size_t constructor_end;
        index += 1;
        if (!expect_identifier(
                frontend,
                &index,
                "after variant `|`",
                &constructor_name
            )) return false;
        constructor_end = constructor_name->end;
        if (token_is(frontend, index, "(")) {
            Token *payload_type;
            arity = 1;
            index += 1;
            if (!expect_identifier(
                    frontend,
                    &index,
                    "as the constructor field name",
                    &field_name
                )) return false;
            if (!expect_token(frontend, &index, ":", "after the field name")) {
                return false;
            }
            if (!expect_identifier(
                    frontend,
                    &index,
                    "as the constructor payload type",
                    &payload_type
                )) return false;
            if (strcmp(payload_type->text, adt->name) == 0) {
                set_error(
                    frontend,
                    "E2S45",
                    payload_type->start,
                    payload_type->end,
                    "recursive ADT payload `%s` is unsupported in this slice",
                    payload_type->text
                );
                return false;
            }
            if (strcmp(payload_type->text, "Int") != 0) {
                set_error(
                    frontend,
                    "E2S40",
                    payload_type->start,
                    payload_type->end,
                    "constructor `%s` payload type `%s` is unsupported; expected `Int`",
                    constructor_name->text,
                    payload_type->text
                );
                return false;
            }
            if (token_is(frontend, index, ",")) {
                set_error(
                    frontend,
                    "E2S41",
                    frontend->tokens[index].start,
                    frontend->tokens[index].end,
                    "constructor `%s` has more than one payload field",
                    constructor_name->text
                );
                return false;
            }
            if (!expect_token(
                    frontend,
                    &index,
                    ")",
                    "after the constructor payload"
                )) return false;
            constructor_end = frontend->tokens[index - 1].end;
        }
        if (!add_constructor(
                frontend,
                adt_index,
                constructor_name,
                field_name,
                arity,
                constructor_end
            )) return false;
        adt->end = constructor_end;
    }

    if (adt->constructor_count < 2) {
        set_error(
            frontend,
            "E2S39",
            name->start,
            adt->end,
            "type `%s` must declare at least two variants",
            adt->name
        );
        return false;
    }
    if (index < frontend->token_count &&
        !token_is(frontend, index, "type") &&
        !token_is(frontend, index, "fn")) {
        set_error(
            frontend,
            "E2S46",
            frontend->tokens[index].start,
            frontend->tokens[index].end,
            "unexpected token `%s` after variants of `%s`",
            frontend->tokens[index].text,
            adt->name
        );
        return false;
    }
    *cursor = index;
    return true;
}

static bool collect_function_stub(Frontend *frontend, size_t *cursor) {
    size_t index = *cursor;
    size_t open = index;
    size_t depth = 0;
    bool found_open = false;
    FunctionStub *stub;

    while (index < frontend->token_count) {
        if (token_is(frontend, index, "{")) {
            found_open = true;
            open = index;
            break;
        }
        if (token_is(frontend, index, "type") && index != *cursor) break;
        index += 1;
    }
    if (!found_open) {
        const Token *token = &frontend->tokens[*cursor];
        set_error(
            frontend,
            "E2S46",
            token->start,
            token->end,
            "function declaration is missing a body"
        );
        return false;
    }
    index = open;
    while (index < frontend->token_count) {
        if (token_is(frontend, index, "{")) depth += 1;
        if (token_is(frontend, index, "}")) {
            if (depth == 0) break;
            depth -= 1;
            if (depth == 0) {
                index += 1;
                break;
            }
        }
        index += 1;
    }
    if (depth != 0) {
        set_error(
            frontend,
            "E2S46",
            frontend->tokens[open].start,
            frontend->tokens[frontend->token_count - 1].end,
            "function body is missing `}`"
        );
        return false;
    }
    if (frontend->function_count >= FUNCTION_LIMIT) {
        set_error(
            frontend,
            "E2S46",
            frontend->tokens[*cursor].start,
            frontend->tokens[*cursor].end,
            "function count exceeds %u",
            FUNCTION_LIMIT
        );
        return false;
    }
    stub = &frontend->functions[frontend->function_count];
    stub->token_start = *cursor;
    stub->token_end = index;
    frontend->function_count += 1;
    *cursor = index;
    return true;
}

static bool collect_declarations(Frontend *frontend) {
    size_t cursor = 0;
    while (cursor < frontend->token_count) {
        if (token_is(frontend, cursor, "type")) {
            if (!parse_type(frontend, &cursor)) return false;
        } else if (token_is(frontend, cursor, "fn")) {
            if (!collect_function_stub(frontend, &cursor)) return false;
        } else {
            Token *token = &frontend->tokens[cursor];
            set_error(
                frontend,
                "E2S46",
                token->start,
                token->end,
                "unsupported top-level token `%s`; expected `type` or `fn`",
                token->text
            );
            return false;
        }
    }
    if (frontend->adt_count == 0) {
        set_error(
            frontend,
            "E2S46",
            0,
            0,
            "bounded ADT frontend requires at least one type declaration"
        );
        return false;
    }
    return true;
}

static bool parse_function(Frontend *frontend, const FunctionStub *stub) {
    size_t index = stub->token_start;
    Token *function_name;
    Token *return_type;
    Token *constructor_name;
    ptrdiff_t adt_index;
    ptrdiff_t constructor_index;
    const Constructor *constructor;
    Construction *construction;
    size_t use_end;
    bool has_payload = false;

    if (!expect_token(frontend, &index, "fn", "at function start")) return false;
    if (!expect_identifier(
            frontend,
            &index,
            "as the function name",
            &function_name
        )) return false;
    if (!expect_token(frontend, &index, "(", "after the function name")) {
        return false;
    }
    if (!expect_token(
            frontend,
            &index,
            ")",
            "because this ADT slice accepts no function parameters"
        )) return false;
    if (!expect_token(frontend, &index, "->", "before the ADT return type")) {
        return false;
    }
    if (!expect_identifier(
            frontend,
            &index,
            "as the ADT return type",
            &return_type
        )) return false;
    adt_index = find_adt(frontend, return_type->text);
    if (adt_index < 0) {
        set_error(
            frontend,
            "E2S46",
            return_type->start,
            return_type->end,
            "function `%s` return type `%s` is not a declared bounded ADT",
            function_name->text,
            return_type->text
        );
        return false;
    }
    if (!expect_token(frontend, &index, "{", "before the function body")) {
        return false;
    }
    if (!expect_token(
            frontend,
            &index,
            "return",
            "as the only statement in the bounded ADT function"
        )) return false;
    if (!expect_identifier(
            frontend,
            &index,
            "as a constructor expression",
            &constructor_name
        )) return false;
    constructor_index = find_constructor(frontend, constructor_name->text);
    if (constructor_index < 0) {
        set_error(
            frontend,
            "E2S44",
            constructor_name->start,
            constructor_name->end,
            "unknown constructor `%s`",
            constructor_name->text
        );
        return false;
    }
    constructor = &frontend->constructors[(size_t)constructor_index];
    if (constructor->adt_index != (size_t)adt_index) {
        set_error(
            frontend,
            "E2S43",
            constructor_name->start,
            constructor_name->end,
            "constructor `%s` belongs to `%s`, but `%s` returns `%s`; declared at bytes %zu..%zu",
            constructor->name,
            frontend->adts[constructor->adt_index].name,
            function_name->text,
            return_type->text,
            constructor->start,
            constructor->end
        );
        return false;
    }
    use_end = constructor_name->end;
    if (constructor->arity == 0) {
        if (token_is(frontend, index, "(")) {
            set_error(
                frontend,
                "E2S42",
                constructor_name->start,
                frontend->tokens[index].end,
                "constructor `%s` expects 0 arguments; declared at bytes %zu..%zu",
                constructor->name,
                constructor->start,
                constructor->end
            );
            return false;
        }
    } else {
        size_t argument_start;
        size_t argument_end;
        if (!token_is(frontend, index, "(")) {
            set_error(
                frontend,
                "E2S42",
                constructor_name->start,
                constructor_name->end,
                "constructor `%s` expects 1 `Int` argument; declared at bytes %zu..%zu",
                constructor->name,
                constructor->start,
                constructor->end
            );
            return false;
        }
        index += 1;
        argument_start = index < frontend->token_count
            ? frontend->tokens[index].start
            : constructor_name->end;
        if (token_is(frontend, index, "-")) index += 1;
        if (!token_has_kind(frontend, index, TOKEN_INTEGER)) {
            argument_end = index < frontend->token_count
                ? frontend->tokens[index].end
                : argument_start;
            set_error(
                frontend,
                "E2S43",
                argument_start,
                argument_end,
                "constructor `%s` payload must be `Int`; declared at bytes %zu..%zu",
                constructor->name,
                constructor->start,
                constructor->end
            );
            return false;
        }
        index += 1;
        if (token_is(frontend, index, ",")) {
            set_error(
                frontend,
                "E2S42",
                constructor_name->start,
                frontend->tokens[index].end,
                "constructor `%s` expects exactly 1 argument; declared at bytes %zu..%zu",
                constructor->name,
                constructor->start,
                constructor->end
            );
            return false;
        }
        if (!expect_token(
                frontend,
                &index,
                ")",
                "after the constructor argument"
            )) return false;
        use_end = frontend->tokens[index - 1].end;
        has_payload = true;
    }
    if (token_is(frontend, index, ";")) index += 1;
    if (!expect_token(frontend, &index, "}", "after the return expression")) {
        return false;
    }
    if (index != stub->token_end) {
        Token *token = &frontend->tokens[index];
        set_error(
            frontend,
            "E2S46",
            token->start,
            token->end,
            "unexpected token `%s` after the bounded function body",
            token->text
        );
        return false;
    }
    construction = &frontend->constructions[frontend->construction_count];
    snprintf(
        construction->function_name,
        sizeof(construction->function_name),
        "%s",
        function_name->text
    );
    construction->adt_index = (size_t)adt_index;
    construction->constructor_index = (size_t)constructor_index;
    construction->function_start = frontend->tokens[stub->token_start].start;
    construction->function_end = frontend->tokens[stub->token_end - 1].end;
    construction->use_start = constructor_name->start;
    construction->use_end = use_end;
    construction->has_payload = has_payload;
    frontend->construction_count += 1;
    return true;
}

static bool resolve_functions(Frontend *frontend) {
    size_t index;
    for (index = 0; index < frontend->function_count; index += 1) {
        if (!parse_function(frontend, &frontend->functions[index])) return false;
    }
    return true;
}

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

static bool write_ir(const Frontend *frontend, const char *path) {
    size_t index;
    FILE *output = fopen(path, "wb");
    if (output == NULL) return false;
    if (fprintf(output, "kofun-adt-ir/v1\n") < 0) goto fail;
    for (index = 0; index < frontend->adt_count; index += 1) {
        const Adt *adt = &frontend->adts[index];
        if (fprintf(
                output,
                "adt|adt-id=adt:%s|name=%s|span=%zu..%zu|constructors=%zu\n",
                adt->name,
                adt->name,
                adt->start,
                adt->end,
                adt->constructor_count
            ) < 0) goto fail;
    }
    for (index = 0; index < frontend->constructor_count; index += 1) {
        const Constructor *constructor = &frontend->constructors[index];
        const Adt *adt = &frontend->adts[constructor->adt_index];
        if (fprintf(
                output,
                "constructor|constructor-id=constructor:adt:%s:%zu|adt-id=adt:%s|name=%s|index=%zu|arity=%u|payload=%s|field=%s|span=%zu..%zu\n",
                adt->name,
                constructor->local_index,
                adt->name,
                constructor->name,
                constructor->local_index,
                constructor->arity,
                constructor->arity == 0 ? "none" : "Int",
                constructor->arity == 0 ? "-" : constructor->field_name,
                constructor->start,
                constructor->end
            ) < 0) goto fail;
    }
    for (index = 0; index < frontend->construction_count; index += 1) {
        const Construction *construction = &frontend->constructions[index];
        const Adt *adt = &frontend->adts[construction->adt_index];
        const Constructor *constructor =
            &frontend->constructors[construction->constructor_index];
        if (fprintf(
                output,
                "function|name=%s|return-adt=adt:%s|span=%zu..%zu\n",
                construction->function_name,
                adt->name,
                construction->function_start,
                construction->function_end
            ) < 0 || fprintf(
                output,
                "construct|function=%s|constructor-id=constructor:adt:%s:%zu|use-span=%zu..%zu|payload=%s\n",
                construction->function_name,
                adt->name,
                constructor->local_index,
                construction->use_start,
                construction->use_end,
                construction->has_payload ? "Int" : "none"
            ) < 0) goto fail;
    }
    return fclose(output) == 0;

fail:
    fclose(output);
    return false;
}

static bool write_tokens(const Frontend *frontend, const char *path) {
    size_t index;
    FILE *output = fopen(path, "wb");
    if (output == NULL) return false;
    if (fprintf(output, "kofun-adt-token-tape/v1\n") < 0) goto fail;
    for (index = 0; index < frontend->token_count; index += 1) {
        const Token *token = &frontend->tokens[index];
        if (fprintf(
                output,
                "%s|%s|%zu|%zu\n",
                token_kind_name(token->kind),
                token->text,
                token->start,
                token->end
            ) < 0) goto fail;
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

    if (argc != 4) {
        fprintf(stderr, "usage: kofun-adt-frontend SOURCE IR TOKENS\n");
        return 2;
    }
    remove(argv[2]);
    remove(argv[3]);
    source = read_source(argv[1], &length);
    if (source == NULL) {
        fprintf(stderr, "kofun-adt-frontend: cannot read bounded source\n");
        return 2;
    }
    if (!tokenize(&frontend, source, length) ||
        !collect_declarations(&frontend) ||
        !resolve_functions(&frontend)) {
        printf("%s\n", frontend.error);
        free(source);
        return 1;
    }
    if (!write_ir(&frontend, argv[2]) ||
        !write_tokens(&frontend, argv[3])) {
        remove(argv[2]);
        remove(argv[3]);
        fprintf(stderr, "kofun-adt-frontend: cannot commit output artifacts\n");
        free(source);
        return 2;
    }
    free(source);
    return 0;
}
