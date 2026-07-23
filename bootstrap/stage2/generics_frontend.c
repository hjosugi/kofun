#include <ctype.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOURCE_LIMIT (1024u * 1024u)
#define TOKEN_LIMIT 8192u
#define TEXT_LIMIT 128u
#define FUNCTION_LIMIT 128u
#define TYPE_PARAMETER_LIMIT 256u
#define PARAMETER_LIMIT 512u
#define LOCAL_LIMIT 512u
#define CALL_LIMIT 512u
#define TYPE_ARGUMENT_LIMIT 8u
#define EXPRESSION_DEPTH_LIMIT 64u

typedef enum {
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_TEXT,
    TOKEN_PUNCTUATION,
    TOKEN_ARROW
} TokenKind;

typedef enum {
    TYPE_INT,
    TYPE_BOOL,
    TYPE_TEXT,
    TYPE_VOID,
    TYPE_PARAMETER
} TypeKind;

typedef struct {
    TokenKind kind;
    char text[TEXT_LIMIT];
    size_t start;
    size_t end;
} Token;

typedef struct {
    TypeKind kind;
    size_t type_parameter;
    size_t start;
    size_t end;
} TypeRef;

typedef struct {
    char name[TEXT_LIMIT];
    size_t owner_function;
    size_t ordinal;
    size_t start;
    size_t end;
    bool used_in_signature;
} TypeParameter;

typedef struct {
    char name[TEXT_LIMIT];
    size_t owner_function;
    TypeRef type;
    size_t start;
    size_t end;
} Parameter;

typedef struct {
    char name[TEXT_LIMIT];
    size_t start;
    size_t end;
    size_t type_parameter_start;
    size_t type_parameter_count;
    size_t parameter_start;
    size_t parameter_count;
    TypeRef result;
    size_t body_start;
    size_t body_end;
    bool has_return;
} Function;

typedef struct {
    char name[TEXT_LIMIT];
    size_t owner_function;
    TypeRef type;
    size_t start;
    size_t end;
} Local;

typedef struct {
    size_t caller;
    size_t callee;
    size_t start;
    size_t end;
    TypeRef type_arguments[TYPE_ARGUMENT_LIMIT];
    size_t type_argument_count;
    TypeRef argument_types[TYPE_ARGUMENT_LIMIT];
    size_t argument_count;
    TypeRef result;
} Call;

typedef struct {
    Token tokens[TOKEN_LIMIT];
    size_t token_count;
    Function functions[FUNCTION_LIMIT];
    size_t function_count;
    TypeParameter type_parameters[TYPE_PARAMETER_LIMIT];
    size_t type_parameter_count;
    Parameter parameters[PARAMETER_LIMIT];
    size_t parameter_count;
    Local locals[LOCAL_LIMIT];
    size_t local_count;
    Call calls[CALL_LIMIT];
    size_t call_count;
    char error[1536];
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
    char detail[1200];
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

static size_t token_start(const Frontend *frontend, size_t index) {
    if (index < frontend->token_count) return frontend->tokens[index].start;
    if (frontend->token_count == 0) return 0;
    return frontend->tokens[frontend->token_count - 1].end;
}

static size_t token_end(const Frontend *frontend, size_t index) {
    if (index < frontend->token_count) return frontend->tokens[index].end;
    return token_start(frontend, index);
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
            "E2S84",
            start,
            end,
            "identifier or literal exceeds the generic frontend text limit"
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
            "E2S84",
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
                        "E2S84",
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
                    "E2S84",
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
        if (strchr("[](),:{};=", source[cursor]) != NULL) {
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
            "E2S84",
            cursor,
            cursor + 1,
            "unsupported byte 0x%02x in bounded generic syntax",
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
    if (!token_is(frontend, *index, text)) {
        set_error(
            frontend,
            "E2S84",
            token_start(frontend, *index),
            token_end(frontend, *index),
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
    if (!token_has_kind(frontend, *index, TOKEN_IDENTIFIER)) {
        set_error(
            frontend,
            "E2S84",
            token_start(frontend, *index),
            token_end(frontend, *index),
            "expected an identifier %s",
            context
        );
        return false;
    }
    *output = &frontend->tokens[*index];
    *index += 1;
    return true;
}

static ptrdiff_t find_function(const Frontend *frontend, const char *name) {
    size_t index;
    for (index = 0; index < frontend->function_count; index += 1) {
        if (strcmp(frontend->functions[index].name, name) == 0) {
            return (ptrdiff_t)index;
        }
    }
    return -1;
}

static ptrdiff_t find_type_parameter(
    const Frontend *frontend,
    const Function *function,
    const char *name
) {
    size_t offset;
    for (offset = 0; offset < function->type_parameter_count; offset += 1) {
        size_t index = function->type_parameter_start + offset;
        if (strcmp(frontend->type_parameters[index].name, name) == 0) {
            return (ptrdiff_t)index;
        }
    }
    return -1;
}

static bool parse_type_ref(
    Frontend *frontend,
    size_t function_index,
    size_t *cursor,
    bool allow_void,
    bool signature_use,
    TypeRef *output
) {
    Token *token;
    Function *function = &frontend->functions[function_index];
    ptrdiff_t type_parameter;
    if (!expect_identifier(
            frontend,
            cursor,
            "as a type in the bounded generic frontend",
            &token
        )) return false;
    output->start = token->start;
    output->end = token->end;
    output->type_parameter = 0;
    if (strcmp(token->text, "Int") == 0) {
        output->kind = TYPE_INT;
    } else if (strcmp(token->text, "Bool") == 0) {
        output->kind = TYPE_BOOL;
    } else if (strcmp(token->text, "Text") == 0) {
        output->kind = TYPE_TEXT;
    } else if (strcmp(token->text, "Void") == 0) {
        if (!allow_void) {
            set_error(
                frontend,
                "E2S80",
                token->start,
                token->end,
                "`Void` is not valid in this generic value type position"
            );
            return false;
        }
        output->kind = TYPE_VOID;
    } else {
        type_parameter = find_type_parameter(
            frontend,
            function,
            token->text
        );
        if (type_parameter < 0) {
            set_error(
                frontend,
                "E2S80",
                token->start,
                token->end,
                "unknown type `%s` in `%s`; declare it in `%s[...]` or use "
                "`Int`, `Bool`, or `Text`",
                token->text,
                function->name,
                function->name
            );
            return false;
        }
        output->kind = TYPE_PARAMETER;
        output->type_parameter = (size_t)type_parameter;
        if (signature_use) {
            frontend->type_parameters[(size_t)type_parameter]
                .used_in_signature = true;
        }
    }
    return true;
}

static bool add_type_parameter(
    Frontend *frontend,
    size_t function_index,
    const Token *name
) {
    Function *function = &frontend->functions[function_index];
    TypeParameter *parameter;
    if (function->type_parameter_count >= TYPE_ARGUMENT_LIMIT ||
        frontend->type_parameter_count >= TYPE_PARAMETER_LIMIT) {
        set_error(
            frontend,
            "E2S84",
            name->start,
            name->end,
            "type parameter count exceeds the per-function limit %u; "
            "reduce the declaration to at most %u parameters",
            TYPE_ARGUMENT_LIMIT,
            TYPE_ARGUMENT_LIMIT
        );
        return false;
    }
    if (find_type_parameter(frontend, function, name->text) >= 0) {
        ptrdiff_t first_index = find_type_parameter(
            frontend,
            function,
            name->text
        );
        const TypeParameter *first =
            &frontend->type_parameters[(size_t)first_index];
        set_error(
            frontend,
            "E2S80",
            name->start,
            name->end,
            "duplicate type parameter `%s` in `%s`; first declared at bytes "
            "%zu..%zu; rename or remove the second parameter",
            name->text,
            function->name,
            first->start,
            first->end
        );
        return false;
    }
    parameter = &frontend->type_parameters[frontend->type_parameter_count];
    memset(parameter, 0, sizeof(*parameter));
    snprintf(parameter->name, sizeof(parameter->name), "%s", name->text);
    parameter->owner_function = function_index;
    parameter->ordinal = function->type_parameter_count;
    parameter->start = name->start;
    parameter->end = name->end;
    frontend->type_parameter_count += 1;
    function->type_parameter_count += 1;
    return true;
}

static bool parameter_name_exists(
    const Frontend *frontend,
    const Function *function,
    const char *name
) {
    size_t offset;
    for (offset = 0; offset < function->parameter_count; offset += 1) {
        const Parameter *parameter =
            &frontend->parameters[function->parameter_start + offset];
        if (strcmp(parameter->name, name) == 0) return true;
    }
    return false;
}

static bool add_parameter(
    Frontend *frontend,
    size_t function_index,
    const Token *name,
    TypeRef type
) {
    Function *function = &frontend->functions[function_index];
    Parameter *parameter;
    if (function->parameter_count >= TYPE_ARGUMENT_LIMIT ||
        frontend->parameter_count >= PARAMETER_LIMIT) {
        set_error(
            frontend,
            "E2S84",
            name->start,
            name->end,
            "value parameter count exceeds the per-function limit %u",
            TYPE_ARGUMENT_LIMIT
        );
        return false;
    }
    if (parameter_name_exists(frontend, function, name->text)) {
        set_error(
            frontend,
            "E2S84",
            name->start,
            name->end,
            "duplicate value parameter `%s` in `%s`",
            name->text,
            function->name
        );
        return false;
    }
    parameter = &frontend->parameters[frontend->parameter_count];
    memset(parameter, 0, sizeof(*parameter));
    snprintf(parameter->name, sizeof(parameter->name), "%s", name->text);
    parameter->owner_function = function_index;
    parameter->type = type;
    parameter->start = name->start;
    parameter->end = type.end;
    frontend->parameter_count += 1;
    function->parameter_count += 1;
    return true;
}

static bool parse_function_header(Frontend *frontend, size_t *cursor) {
    size_t index = *cursor;
    size_t function_index;
    size_t depth;
    Token *name;
    Function *function;
    ptrdiff_t duplicate;

    if (frontend->function_count >= FUNCTION_LIMIT) {
        set_error(
            frontend,
            "E2S84",
            token_start(frontend, index),
            token_end(frontend, index),
            "function count exceeds %u",
            FUNCTION_LIMIT
        );
        return false;
    }
    if (!expect_token(frontend, &index, "fn", "at function start")) {
        return false;
    }
    if (!expect_identifier(frontend, &index, "as the function name", &name)) {
        return false;
    }
    duplicate = find_function(frontend, name->text);
    if (duplicate >= 0) {
        const Function *first = &frontend->functions[(size_t)duplicate];
        set_error(
            frontend,
            "E2S84",
            name->start,
            name->end,
            "duplicate function `%s`; first declared at bytes %zu..%zu",
            name->text,
            first->start,
            first->end
        );
        return false;
    }
    function_index = frontend->function_count;
    function = &frontend->functions[function_index];
    memset(function, 0, sizeof(*function));
    snprintf(function->name, sizeof(function->name), "%s", name->text);
    function->start = frontend->tokens[*cursor].start;
    function->type_parameter_start = frontend->type_parameter_count;
    function->parameter_start = frontend->parameter_count;
    frontend->function_count += 1;

    if (token_is(frontend, index, "[")) {
        index += 1;
        if (token_is(frontend, index, "]")) {
            set_error(
                frontend,
                "E2S80",
                frontend->tokens[index - 1].start,
                frontend->tokens[index].end,
                "generic function `%s` must declare at least one type parameter",
                function->name
            );
            return false;
        }
        while (!token_is(frontend, index, "]")) {
            Token *type_parameter;
            if (!expect_identifier(
                    frontend,
                    &index,
                    "as a generic type parameter",
                    &type_parameter
                )) return false;
            if (token_is(frontend, index, ":")) {
                set_error(
                    frontend,
                    "E2S83",
                    frontend->tokens[index].start,
                    frontend->tokens[index].end,
                    "generic bounds are unsupported in this frontend slice; "
                    "remove the bound or track bounded generics in #332"
                );
                return false;
            }
            if (!add_type_parameter(
                    frontend,
                    function_index,
                    type_parameter
                )) return false;
            if (token_is(frontend, index, ",")) {
                index += 1;
                if (token_is(frontend, index, "]")) {
                    set_error(
                        frontend,
                        "E2S84",
                        frontend->tokens[index].start,
                        frontend->tokens[index].end,
                        "trailing comma is unsupported in generic parameter lists"
                    );
                    return false;
                }
            } else if (!token_is(frontend, index, "]")) {
                set_error(
                    frontend,
                    "E2S84",
                    token_start(frontend, index),
                    token_end(frontend, index),
                    "expected `,` or `]` after generic type parameter"
                );
                return false;
            }
        }
        index += 1;
    }

    if (!expect_token(frontend, &index, "(", "after the function name")) {
        return false;
    }
    while (!token_is(frontend, index, ")")) {
        Token *parameter_name;
        TypeRef parameter_type;
        if (!expect_identifier(
                frontend,
                &index,
                "as a value parameter name",
                &parameter_name
            )) return false;
        if (!expect_token(frontend, &index, ":", "after parameter name")) {
            return false;
        }
        if (!parse_type_ref(
                frontend,
                function_index,
                &index,
                false,
                true,
                &parameter_type
            )) return false;
        if (!add_parameter(
                frontend,
                function_index,
                parameter_name,
                parameter_type
            )) return false;
        if (token_is(frontend, index, ",")) {
            index += 1;
            if (token_is(frontend, index, ")")) {
                set_error(
                    frontend,
                    "E2S84",
                    frontend->tokens[index].start,
                    frontend->tokens[index].end,
                    "trailing comma is unsupported in value parameter lists"
                );
                return false;
            }
        } else if (!token_is(frontend, index, ")")) {
            set_error(
                frontend,
                "E2S84",
                token_start(frontend, index),
                token_end(frontend, index),
                "expected `,` or `)` after value parameter"
            );
            return false;
        }
    }
    index += 1;
    if (token_is(frontend, index, "->")) {
        index += 1;
        if (!parse_type_ref(
                frontend,
                function_index,
                &index,
                true,
                true,
                &function->result
            )) return false;
    } else {
        function->result.kind = TYPE_VOID;
        function->result.type_parameter = 0;
        function->result.start = frontend->tokens[index - 1].end;
        function->result.end = function->result.start;
    }
    if (!expect_token(frontend, &index, "{", "before the function body")) {
        return false;
    }
    function->body_start = index;
    depth = 1;
    while (index < frontend->token_count && depth != 0) {
        if (token_is(frontend, index, "{")) depth += 1;
        if (token_is(frontend, index, "}")) depth -= 1;
        if (depth != 0) index += 1;
    }
    if (depth != 0 || index >= frontend->token_count) {
        set_error(
            frontend,
            "E2S84",
            frontend->tokens[function->body_start - 1].start,
            token_end(frontend, frontend->token_count),
            "function `%s` body is missing `}`",
            function->name
        );
        return false;
    }
    function->body_end = index;
    function->end = frontend->tokens[index].end;
    index += 1;

    if (function->type_parameter_count > 0) {
        size_t offset;
        for (
            offset = 0;
            offset < function->type_parameter_count;
            offset += 1
        ) {
            const TypeParameter *parameter = &frontend->type_parameters[
                function->type_parameter_start + offset
            ];
            if (!parameter->used_in_signature) {
                set_error(
                    frontend,
                    "E2S80",
                    parameter->start,
                    parameter->end,
                    "type parameter `%s` in `%s` is unconstrained; use it in "
                    "a value parameter or result type",
                    parameter->name,
                    function->name
                );
                return false;
            }
        }
    }
    *cursor = index;
    return true;
}

static bool collect_headers(Frontend *frontend) {
    size_t cursor = 0;
    while (cursor < frontend->token_count) {
        if (!token_is(frontend, cursor, "fn")) {
            set_error(
                frontend,
                "E2S84",
                frontend->tokens[cursor].start,
                frontend->tokens[cursor].end,
                "unsupported top-level token `%s`; expected `fn`",
                frontend->tokens[cursor].text
            );
            return false;
        }
        if (!parse_function_header(frontend, &cursor)) return false;
    }
    if (frontend->function_count == 0) {
        set_error(
            frontend,
            "E2S84",
            0,
            0,
            "bounded generic frontend requires at least one function"
        );
        return false;
    }
    return true;
}

static void type_id(
    const Frontend *frontend,
    TypeRef type,
    char output[256]
) {
    switch (type.kind) {
        case TYPE_INT:
            snprintf(output, 256, "builtin:Int");
            return;
        case TYPE_BOOL:
            snprintf(output, 256, "builtin:Bool");
            return;
        case TYPE_TEXT:
            snprintf(output, 256, "builtin:Text");
            return;
        case TYPE_VOID:
            snprintf(output, 256, "builtin:Void");
            return;
        case TYPE_PARAMETER: {
            const TypeParameter *parameter =
                &frontend->type_parameters[type.type_parameter];
            const Function *owner =
                &frontend->functions[parameter->owner_function];
            snprintf(
                output,
                256,
                "type-parameter:function:%s:%zu",
                owner->name,
                parameter->ordinal
            );
            return;
        }
    }
    snprintf(output, 256, "invalid");
}

static void type_display(
    const Frontend *frontend,
    TypeRef type,
    char output[TEXT_LIMIT]
) {
    switch (type.kind) {
        case TYPE_INT:
            snprintf(output, TEXT_LIMIT, "Int");
            return;
        case TYPE_BOOL:
            snprintf(output, TEXT_LIMIT, "Bool");
            return;
        case TYPE_TEXT:
            snprintf(output, TEXT_LIMIT, "Text");
            return;
        case TYPE_VOID:
            snprintf(output, TEXT_LIMIT, "Void");
            return;
        case TYPE_PARAMETER:
            snprintf(
                output,
                TEXT_LIMIT,
                "%s",
                frontend->type_parameters[type.type_parameter].name
            );
            return;
    }
    snprintf(output, TEXT_LIMIT, "invalid");
}

static bool type_equal(TypeRef left, TypeRef right) {
    if (left.kind != right.kind) return false;
    return left.kind != TYPE_PARAMETER ||
        left.type_parameter == right.type_parameter;
}

static TypeRef substitute_type(
    const Function *callee,
    TypeRef declared,
    const TypeRef *arguments,
    size_t argument_count
) {
    TypeRef result = declared;
    if (declared.kind == TYPE_PARAMETER &&
        declared.type_parameter >= callee->type_parameter_start &&
        declared.type_parameter <
            callee->type_parameter_start + callee->type_parameter_count) {
        size_t offset =
            declared.type_parameter - callee->type_parameter_start;
        if (offset < argument_count) result = arguments[offset];
    }
    return result;
}

static void substitution_display(
    const Frontend *frontend,
    const Function *callee,
    const TypeRef *arguments,
    size_t argument_count,
    char output[512]
) {
    size_t offset;
    size_t used = 0;
    output[0] = '\0';
    for (offset = 0; offset < argument_count; offset += 1) {
        const TypeParameter *parameter = &frontend->type_parameters[
            callee->type_parameter_start + offset
        ];
        char argument[TEXT_LIMIT];
        int written;
        type_display(frontend, arguments[offset], argument);
        written = snprintf(
            output + used,
            512 - used,
            "%s%s -> %s",
            offset == 0 ? "" : ", ",
            parameter->name,
            argument
        );
        if (written < 0 || (size_t)written >= 512 - used) {
            output[511] = '\0';
            return;
        }
        used += (size_t)written;
    }
    if (argument_count == 0) snprintf(output, 512, "-");
}

static ptrdiff_t find_local(
    const Frontend *frontend,
    size_t function_index,
    const char *name
) {
    size_t index = frontend->local_count;
    while (index > 0) {
        const Local *local = &frontend->locals[index - 1];
        if (local->owner_function == function_index &&
            strcmp(local->name, name) == 0) {
            return (ptrdiff_t)(index - 1);
        }
        index -= 1;
    }
    return -1;
}

static ptrdiff_t find_parameter(
    const Frontend *frontend,
    const Function *function,
    const char *name
) {
    size_t offset;
    for (offset = 0; offset < function->parameter_count; offset += 1) {
        size_t index = function->parameter_start + offset;
        if (strcmp(frontend->parameters[index].name, name) == 0) {
            return (ptrdiff_t)index;
        }
    }
    return -1;
}

static bool parse_expression(
    Frontend *frontend,
    size_t function_index,
    size_t *cursor,
    size_t limit,
    unsigned depth,
    TypeRef *output
);

static bool parse_call(
    Frontend *frontend,
    size_t function_index,
    size_t *cursor,
    size_t limit,
    unsigned depth,
    TypeRef *output
) {
    size_t index = *cursor;
    Token *name = &frontend->tokens[index];
    ptrdiff_t callee_index = find_function(frontend, name->text);
    Function *callee;
    TypeRef type_arguments[TYPE_ARGUMENT_LIMIT];
    TypeRef value_arguments[TYPE_ARGUMENT_LIMIT];
    size_t type_argument_count = 0;
    size_t value_argument_count = 0;
    size_t call_end;
    char substitution[512];

    if (callee_index < 0) {
        set_error(
            frontend,
            "E2S84",
            name->start,
            name->end,
            "unknown function `%s` in bounded generic call",
            name->text
        );
        return false;
    }
    callee = &frontend->functions[(size_t)callee_index];
    index += 1;

    if (token_is(frontend, index, "[")) {
        size_t application_start = frontend->tokens[index].start;
        index += 1;
        while (!token_is(frontend, index, "]")) {
            if (type_argument_count >= TYPE_ARGUMENT_LIMIT) {
                set_error(
                    frontend,
                    "E2S84",
                    token_start(frontend, index),
                    token_end(frontend, index),
                    "type argument count exceeds %u",
                    TYPE_ARGUMENT_LIMIT
                );
                return false;
            }
            if (!parse_type_ref(
                    frontend,
                    function_index,
                    &index,
                    false,
                    false,
                    &type_arguments[type_argument_count]
                )) return false;
            type_argument_count += 1;
            if (token_is(frontend, index, ",")) {
                index += 1;
                if (token_is(frontend, index, "]")) {
                    set_error(
                        frontend,
                        "E2S84",
                        frontend->tokens[index].start,
                        frontend->tokens[index].end,
                        "trailing comma is unsupported in explicit type arguments"
                    );
                    return false;
                }
            } else if (!token_is(frontend, index, "]")) {
                set_error(
                    frontend,
                    "E2S84",
                    token_start(frontend, index),
                    token_end(frontend, index),
                    "expected `,` or `]` after explicit type argument"
                );
                return false;
            }
        }
        call_end = frontend->tokens[index].end;
        index += 1;
        if (callee->type_parameter_count == 0) {
            set_error(
                frontend,
                "E2S81",
                application_start,
                call_end,
                "non-generic function `%s` does not accept explicit type arguments",
                callee->name
            );
            return false;
        }
    } else if (callee->type_parameter_count > 0) {
        set_error(
            frontend,
            "E2S81",
            name->start,
            name->end,
            "generic function `%s` requires %zu explicit type argument%s; write `%s[...]`",
            callee->name,
            callee->type_parameter_count,
            callee->type_parameter_count == 1 ? "" : "s",
            callee->name
        );
        return false;
    }

    if (type_argument_count != callee->type_parameter_count) {
        set_error(
            frontend,
            "E2S81",
            name->start,
            index == 0 ? name->end : frontend->tokens[index - 1].end,
            "generic function `%s` expects %zu explicit type argument%s, "
            "found %zu; declaration at bytes %zu..%zu; pass exactly %zu "
            "before the value arguments",
            callee->name,
            callee->type_parameter_count,
            callee->type_parameter_count == 1 ? "" : "s",
            type_argument_count,
            callee->start,
            callee->end,
            callee->type_parameter_count
        );
        return false;
    }
    if ((size_t)callee_index == function_index &&
        callee->type_parameter_count > 0) {
        set_error(
            frontend,
            "E2S83",
            name->start,
            name->end,
            "recursive generic call to `%s` is unsupported in this frontend "
            "slice; call a non-recursive generic helper",
            callee->name
        );
        return false;
    }
    if (!expect_token(frontend, &index, "(", "after the call target")) {
        return false;
    }
    while (!token_is(frontend, index, ")")) {
        TypeRef actual;
        TypeRef expected;
        char actual_name[TEXT_LIMIT];
        char expected_name[TEXT_LIMIT];
        if (value_argument_count >= TYPE_ARGUMENT_LIMIT) {
            set_error(
                frontend,
                "E2S84",
                token_start(frontend, index),
                token_end(frontend, index),
                "value argument count exceeds %u",
                TYPE_ARGUMENT_LIMIT
            );
            return false;
        }
        if (!parse_expression(
                frontend,
                function_index,
                &index,
                limit,
                depth + 1,
                &actual
            )) return false;
        value_arguments[value_argument_count] = actual;
        if (value_argument_count < callee->parameter_count) {
            const Parameter *parameter = &frontend->parameters[
                callee->parameter_start + value_argument_count
            ];
            expected = substitute_type(
                callee,
                parameter->type,
                type_arguments,
                type_argument_count
            );
            if (!type_equal(actual, expected)) {
                type_display(frontend, actual, actual_name);
                type_display(frontend, expected, expected_name);
                substitution_display(
                    frontend,
                    callee,
                    type_arguments,
                    type_argument_count,
                    substitution
                );
                set_error(
                    frontend,
                    "E2S82",
                    actual.start,
                    actual.end,
                    "argument %zu to `%s` has type `%s`; expected `%s` after "
                    "substitution `%s`; declaration at bytes %zu..%zu; "
                    "change the explicit type argument or value",
                    value_argument_count + 1,
                    callee->name,
                    actual_name,
                    expected_name,
                    substitution,
                    callee->start,
                    callee->end
                );
                return false;
            }
        }
        value_argument_count += 1;
        if (token_is(frontend, index, ",")) {
            index += 1;
            if (token_is(frontend, index, ")")) {
                set_error(
                    frontend,
                    "E2S84",
                    frontend->tokens[index].start,
                    frontend->tokens[index].end,
                    "trailing comma is unsupported in value arguments"
                );
                return false;
            }
        } else if (!token_is(frontend, index, ")")) {
            set_error(
                frontend,
                "E2S84",
                token_start(frontend, index),
                token_end(frontend, index),
                "expected `,` or `)` after value argument"
            );
            return false;
        }
    }
    call_end = frontend->tokens[index].end;
    index += 1;
    if (value_argument_count != callee->parameter_count) {
        set_error(
            frontend,
            "E2S82",
            name->start,
            call_end,
            "function `%s` expects %zu value argument%s, found %zu after type-argument validation",
            callee->name,
            callee->parameter_count,
            callee->parameter_count == 1 ? "" : "s",
            value_argument_count
        );
        return false;
    }
    if (frontend->call_count >= CALL_LIMIT) {
        set_error(
            frontend,
            "E2S84",
            name->start,
            call_end,
            "generic call count exceeds %u",
            CALL_LIMIT
        );
        return false;
    }
    *output = substitute_type(
        callee,
        callee->result,
        type_arguments,
        type_argument_count
    );
    output->start = name->start;
    output->end = call_end;
    {
        Call *call = &frontend->calls[frontend->call_count];
        size_t argument;
        memset(call, 0, sizeof(*call));
        call->caller = function_index;
        call->callee = (size_t)callee_index;
        call->start = name->start;
        call->end = call_end;
        call->type_argument_count = type_argument_count;
        call->argument_count = value_argument_count;
        call->result = *output;
        for (argument = 0; argument < type_argument_count; argument += 1) {
            call->type_arguments[argument] = type_arguments[argument];
        }
        for (argument = 0; argument < value_argument_count; argument += 1) {
            call->argument_types[argument] = value_arguments[argument];
        }
        frontend->call_count += 1;
    }
    *cursor = index;
    return true;
}

static bool parse_expression(
    Frontend *frontend,
    size_t function_index,
    size_t *cursor,
    size_t limit,
    unsigned depth,
    TypeRef *output
) {
    size_t index = *cursor;
    Token *token;
    Function *function = &frontend->functions[function_index];
    ptrdiff_t local;
    ptrdiff_t parameter;

    if (depth > EXPRESSION_DEPTH_LIMIT) {
        set_error(
            frontend,
            "E2S84",
            token_start(frontend, index),
            token_end(frontend, index),
            "expression depth exceeds %u",
            EXPRESSION_DEPTH_LIMIT
        );
        return false;
    }
    if (index >= limit) {
        set_error(
            frontend,
            "E2S84",
            token_start(frontend, index),
            token_end(frontend, index),
            "expected a bounded generic expression"
        );
        return false;
    }
    token = &frontend->tokens[index];
    output->type_parameter = 0;
    output->start = token->start;
    output->end = token->end;
    if (token->kind == TOKEN_INTEGER) {
        output->kind = TYPE_INT;
        *cursor = index + 1;
        return true;
    }
    if (token->kind == TOKEN_TEXT) {
        output->kind = TYPE_TEXT;
        *cursor = index + 1;
        return true;
    }
    if (token->kind != TOKEN_IDENTIFIER) {
        set_error(
            frontend,
            "E2S84",
            token->start,
            token->end,
            "unsupported expression token `%s`",
            token->text
        );
        return false;
    }
    if (strcmp(token->text, "true") == 0 ||
        strcmp(token->text, "false") == 0) {
        output->kind = TYPE_BOOL;
        *cursor = index + 1;
        return true;
    }
    if (token_is(frontend, index + 1, "[") ||
        token_is(frontend, index + 1, "(")) {
        return parse_call(
            frontend,
            function_index,
            cursor,
            limit,
            depth,
            output
        );
    }
    local = find_local(frontend, function_index, token->text);
    if (local >= 0) {
        *output = frontend->locals[(size_t)local].type;
        output->start = token->start;
        output->end = token->end;
        *cursor = index + 1;
        return true;
    }
    parameter = find_parameter(frontend, function, token->text);
    if (parameter >= 0) {
        *output = frontend->parameters[(size_t)parameter].type;
        output->start = token->start;
        output->end = token->end;
        *cursor = index + 1;
        return true;
    }
    if (find_type_parameter(frontend, function, token->text) >= 0) {
        set_error(
            frontend,
            "E2S80",
            token->start,
            token->end,
            "type parameter `%s` in `%s` cannot be used as a runtime value; "
            "use a value parameter or local binding instead",
            token->text,
            function->name
        );
        return false;
    }
    set_error(
        frontend,
        "E2S84",
        token->start,
        token->end,
        "unknown value `%s` in `%s`",
        token->text,
        function->name
    );
    return false;
}

static bool add_local(
    Frontend *frontend,
    size_t function_index,
    const Token *name,
    TypeRef type
) {
    Function *function = &frontend->functions[function_index];
    Local *local;
    if (find_local(frontend, function_index, name->text) >= 0 ||
        find_parameter(frontend, function, name->text) >= 0) {
        set_error(
            frontend,
            "E2S84",
            name->start,
            name->end,
            "duplicate local value `%s` in `%s`",
            name->text,
            function->name
        );
        return false;
    }
    if (frontend->local_count >= LOCAL_LIMIT) {
        set_error(
            frontend,
            "E2S84",
            name->start,
            name->end,
            "local value count exceeds %u",
            LOCAL_LIMIT
        );
        return false;
    }
    local = &frontend->locals[frontend->local_count];
    memset(local, 0, sizeof(*local));
    snprintf(local->name, sizeof(local->name), "%s", name->text);
    local->owner_function = function_index;
    local->type = type;
    local->start = name->start;
    local->end = type.end;
    frontend->local_count += 1;
    return true;
}

static bool require_type(
    Frontend *frontend,
    TypeRef actual,
    TypeRef expected,
    size_t start,
    size_t end,
    const char *context
) {
    char actual_name[TEXT_LIMIT];
    char expected_name[TEXT_LIMIT];
    if (type_equal(actual, expected)) return true;
    type_display(frontend, actual, actual_name);
    type_display(frontend, expected, expected_name);
    set_error(
        frontend,
        "E2S82",
        start,
        end,
        "%s has type `%s`; expected `%s` after explicit generic substitution; "
        "change the annotation or explicit type argument",
        context,
        actual_name,
        expected_name
    );
    return false;
}

static bool parse_function_body(
    Frontend *frontend,
    size_t function_index
) {
    Function *function = &frontend->functions[function_index];
    size_t cursor = function->body_start;
    while (cursor < function->body_end) {
        if (token_is(frontend, cursor, "let")) {
            Token *name;
            TypeRef annotated;
            TypeRef actual;
            cursor += 1;
            if (!expect_identifier(
                    frontend,
                    &cursor,
                    "as a local binding name",
                    &name
                )) return false;
            if (!expect_token(
                    frontend,
                    &cursor,
                    ":",
                    "because generic locals require an explicit type"
                )) return false;
            if (!parse_type_ref(
                    frontend,
                    function_index,
                    &cursor,
                    false,
                    false,
                    &annotated
                )) return false;
            if (!expect_token(frontend, &cursor, "=", "after local type")) {
                return false;
            }
            if (!parse_expression(
                    frontend,
                    function_index,
                    &cursor,
                    function->body_end,
                    0,
                    &actual
                )) return false;
            if (!require_type(
                    frontend,
                    actual,
                    annotated,
                    name->start,
                    actual.end,
                    "annotated local result"
                )) return false;
            if (!add_local(
                    frontend,
                    function_index,
                    name,
                    annotated
                )) return false;
            if (token_is(frontend, cursor, ";")) cursor += 1;
            continue;
        }
        if (token_is(frontend, cursor, "return")) {
            TypeRef actual;
            size_t return_start = frontend->tokens[cursor].start;
            cursor += 1;
            if (function->result.kind == TYPE_VOID) {
                set_error(
                    frontend,
                    "E2S82",
                    return_start,
                    token_end(frontend, cursor),
                    "function `%s` has no declared value result",
                    function->name
                );
                return false;
            }
            if (!parse_expression(
                    frontend,
                    function_index,
                    &cursor,
                    function->body_end,
                    0,
                    &actual
                )) return false;
            if (!require_type(
                    frontend,
                    actual,
                    function->result,
                    return_start,
                    actual.end,
                    "returned expression"
                )) return false;
            function->has_return = true;
            if (token_is(frontend, cursor, ";")) cursor += 1;
            if (cursor != function->body_end) {
                set_error(
                    frontend,
                    "E2S84",
                    token_start(frontend, cursor),
                    token_end(frontend, cursor),
                    "statement follows the terminal return in `%s`",
                    function->name
                );
                return false;
            }
            continue;
        }
        if (token_is(frontend, cursor, "print")) {
            TypeRef printed;
            cursor += 1;
            if (!expect_token(frontend, &cursor, "(", "after `print`")) {
                return false;
            }
            if (!parse_expression(
                    frontend,
                    function_index,
                    &cursor,
                    function->body_end,
                    0,
                    &printed
                )) return false;
            if (printed.kind == TYPE_VOID) {
                set_error(
                    frontend,
                    "E2S82",
                    printed.start,
                    printed.end,
                    "cannot print a `Void` expression"
                );
                return false;
            }
            if (!expect_token(
                    frontend,
                    &cursor,
                    ")",
                    "after the printed expression"
                )) return false;
            if (token_is(frontend, cursor, ";")) cursor += 1;
            continue;
        }
        set_error(
            frontend,
            "E2S84",
            frontend->tokens[cursor].start,
            frontend->tokens[cursor].end,
            "unsupported statement `%s` in bounded generic function `%s`",
            frontend->tokens[cursor].text,
            function->name
        );
        return false;
    }
    if (function->result.kind != TYPE_VOID && !function->has_return) {
        set_error(
            frontend,
            "E2S84",
            function->start,
            function->end,
            "function `%s` with a value result is missing `return`",
            function->name
        );
        return false;
    }
    return true;
}

static bool type_bodies(Frontend *frontend) {
    size_t index;
    for (index = 0; index < frontend->function_count; index += 1) {
        if (!parse_function_body(frontend, index)) return false;
    }
    return true;
}

static bool check_calls_returning_to_generic_origin(
    Frontend *frontend,
    size_t origin,
    size_t current,
    bool visited[FUNCTION_LIMIT]
) {
    size_t index;
    for (index = 0; index < frontend->call_count; index += 1) {
        const Call *call = &frontend->calls[index];
        const Function *caller;
        const Function *callee;
        if (call->caller != current) continue;
        caller = &frontend->functions[call->caller];
        callee = &frontend->functions[call->callee];
        if (call->callee == origin) {
            set_error(
                frontend,
                "E2S83",
                call->start,
                call->end,
                "recursive generic call cycle reaches `%s` through `%s` -> "
                "`%s`; recursive generic calls are unsupported in this "
                "frontend slice",
                frontend->functions[origin].name,
                caller->name,
                callee->name
            );
            return false;
        }
        if (!visited[call->callee]) {
            visited[call->callee] = true;
            if (!check_calls_returning_to_generic_origin(
                    frontend,
                    origin,
                    call->callee,
                    visited
                )) return false;
        }
    }
    return true;
}

static bool reject_generic_call_cycles(Frontend *frontend) {
    size_t origin;
    for (origin = 0; origin < frontend->function_count; origin += 1) {
        bool visited[FUNCTION_LIMIT] = {false};
        if (frontend->functions[origin].type_parameter_count == 0) continue;
        visited[origin] = true;
        if (!check_calls_returning_to_generic_origin(
                frontend,
                origin,
                origin,
                visited
            )) return false;
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

static bool write_type_arguments(
    FILE *output,
    const Frontend *frontend,
    const Call *call
) {
    size_t index;
    if (call->type_argument_count == 0) return fputc('-', output) != EOF;
    for (index = 0; index < call->type_argument_count; index += 1) {
        char id[256];
        type_id(frontend, call->type_arguments[index], id);
        if (fprintf(output, "%s%s", index == 0 ? "" : ",", id) < 0) {
            return false;
        }
    }
    return true;
}

static bool write_substitution(
    FILE *output,
    const Frontend *frontend,
    const Call *call
) {
    const Function *callee = &frontend->functions[call->callee];
    size_t index;
    if (call->type_argument_count == 0) return fputc('-', output) != EOF;
    for (index = 0; index < call->type_argument_count; index += 1) {
        char argument[256];
        char parameter[256];
        TypeRef parameter_type;
        parameter_type.kind = TYPE_PARAMETER;
        parameter_type.type_parameter =
            callee->type_parameter_start + index;
        parameter_type.start = 0;
        parameter_type.end = 0;
        type_id(frontend, parameter_type, parameter);
        type_id(frontend, call->type_arguments[index], argument);
        if (fprintf(
                output,
                "%s%s->%s",
                index == 0 ? "" : ",",
                parameter,
                argument
            ) < 0) return false;
    }
    return true;
}

static bool write_ir(const Frontend *frontend, const char *path) {
    size_t index;
    FILE *output = fopen(path, "wb");
    if (output == NULL) return false;
    if (fprintf(output, "kofun-generics-ir/v1\n") < 0) goto fail;
    for (index = 0; index < frontend->function_count; index += 1) {
        const Function *function = &frontend->functions[index];
        char result[256];
        type_id(frontend, function->result, result);
        if (fprintf(
                output,
                "function|function-id=function:%s|name=%s|"
                "type-parameters=%zu|parameters=%zu|result=%s|"
                "span=%zu..%zu\n",
                function->name,
                function->name,
                function->type_parameter_count,
                function->parameter_count,
                result,
                function->start,
                function->end
            ) < 0) goto fail;
    }
    for (index = 0; index < frontend->type_parameter_count; index += 1) {
        const TypeParameter *parameter = &frontend->type_parameters[index];
        const Function *owner =
            &frontend->functions[parameter->owner_function];
        char id[256];
        TypeRef type;
        type.kind = TYPE_PARAMETER;
        type.type_parameter = index;
        type.start = parameter->start;
        type.end = parameter->end;
        type_id(frontend, type, id);
        if (fprintf(
                output,
                "type-parameter|type-parameter-id=%s|owner=function:%s|"
                "name=%s|index=%zu|span=%zu..%zu\n",
                id,
                owner->name,
                parameter->name,
                parameter->ordinal,
                parameter->start,
                parameter->end
            ) < 0) goto fail;
    }
    for (index = 0; index < frontend->parameter_count; index += 1) {
        const Parameter *parameter = &frontend->parameters[index];
        const Function *owner =
            &frontend->functions[parameter->owner_function];
        char type[256];
        type_id(frontend, parameter->type, type);
        if (fprintf(
                output,
                "parameter|owner=function:%s|name=%s|type=%s|span=%zu..%zu\n",
                owner->name,
                parameter->name,
                type,
                parameter->start,
                parameter->end
            ) < 0) goto fail;
    }
    for (index = 0; index < frontend->local_count; index += 1) {
        const Local *local = &frontend->locals[index];
        const Function *owner =
            &frontend->functions[local->owner_function];
        char type[256];
        type_id(frontend, local->type, type);
        if (fprintf(
                output,
                "local|owner=function:%s|name=%s|type=%s|span=%zu..%zu\n",
                owner->name,
                local->name,
                type,
                local->start,
                local->end
            ) < 0) goto fail;
    }
    for (index = 0; index < frontend->call_count; index += 1) {
        const Call *call = &frontend->calls[index];
        const Function *caller = &frontend->functions[call->caller];
        const Function *callee = &frontend->functions[call->callee];
        char result[256];
        type_id(frontend, call->result, result);
        if (fprintf(
                output,
                "call|caller=function:%s|callee=function:%s|type-arguments=",
                caller->name,
                callee->name
            ) < 0 ||
            !write_type_arguments(output, frontend, call) ||
            fprintf(output, "|substitution=") < 0 ||
            !write_substitution(output, frontend, call) ||
            fprintf(
                output,
                "|value-arguments=%zu|result=%s|use-span=%zu..%zu|declaration-span=%zu..%zu\n",
                call->argument_count,
                result,
                call->start,
                call->end,
                callee->start,
                callee->end
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
    if (fprintf(output, "kofun-generics-token-tape/v1\n") < 0) goto fail;
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
        fprintf(stderr, "usage: kofun-generics-frontend SOURCE IR TOKENS\n");
        return 2;
    }
    remove(argv[2]);
    remove(argv[3]);
    source = read_source(argv[1], &length);
    if (source == NULL) {
        fprintf(
            stderr,
            "kofun-generics-frontend: cannot read bounded source\n"
        );
        return 2;
    }
    if (!tokenize(&frontend, source, length) ||
        !collect_headers(&frontend) ||
        !type_bodies(&frontend) ||
        !reject_generic_call_cycles(&frontend)) {
        printf("%s\n", frontend.error);
        free(source);
        return 1;
    }
    if (!write_ir(&frontend, argv[2]) ||
        !write_tokens(&frontend, argv[3])) {
        remove(argv[2]);
        remove(argv[3]);
        fprintf(
            stderr,
            "kofun-generics-frontend: cannot commit output artifacts\n"
        );
        free(source);
        return 2;
    }
    free(source);
    return 0;
}
