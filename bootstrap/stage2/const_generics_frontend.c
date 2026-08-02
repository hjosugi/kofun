/*
 * Bounded integer const generic frontend (#916).
 *
 * This slice is frontend-only. It parses nominal record declarations that
 * carry at most one parameter — either an ordinary type parameter or a
 * `const` parameter of type `Int` — and type-checks monomorphic functions
 * that mention the resulting instantiations in declarations and annotations.
 *
 * What a const parameter is, and is not:
 *
 *   - It is part of the type's identity. `Fixed[2]` and `Fixed[3]` are
 *     different types, and a mismatch is refused here rather than deferred.
 *   - Its argument is normalized by *value*, not by digits, so `Fixed[02]`
 *     and `Fixed[2]` are the same type.
 *   - It is not a runtime value. It cannot be a field type, an expression, or
 *     a value parameter, so it can never be silently erased into a runtime
 *     field the way a stored scale would be.
 *   - It is not a type. It carries no ownership kind, so RFC-0004's K-SUBST
 *     substitutes nothing through it and K-PARAM does not classify it:
 *     `Fixed[2]` and `Fixed[3]` classify identically while staying distinct
 *     types. Only a *type* argument propagates a kind.
 *
 * The normalized argument forms are disjoint by tag — `const:Int:N` for a
 * const argument, `builtin:X` or `nominal:X` for a type argument — so an
 * instantiation identity stays injective when it is embedded in the
 * `args=` component #936 assembles an ImplementationId from, and therefore in
 * the DictionaryId that derivation produces by dropping only the declaration
 * ordinal.
 *
 * Deliberately out of scope, and refused rather than half-supported: const
 * expressions, arithmetic on type-level values, const parameter inference,
 * const parameters on functions, more than one parameter per declaration,
 * and nested instantiation in an argument position. `Fixed[scale]` decimal
 * semantics stay with #725 Part B; this frontend only supplies the
 * type-level integer parameter that part is waiting on.
 *
 * It emits typed IR and a token tape and nothing else: no monomorphized
 * backend artifact is produced here. `instantiation` rows are the
 * per-literal monomorphization plan a backend would consume; every declared
 * backend records its refusal to consume it in
 * `tests/conformance/capabilities.tsv`.
 */
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
#define IDENTITY_LIMIT 512u
#define ARGUMENT_IDENTITY_LIMIT 256u
#define DISPLAY_LIMIT 384u
#define NOMINAL_LIMIT 64u
#define FIELD_LIMIT 256u
#define FUNCTION_LIMIT 128u
#define PARAMETER_LIMIT 512u
#define LOCAL_LIMIT 512u
#define CALL_LIMIT 512u
#define ARGUMENT_LIMIT 8u
#define INSTANTIATION_LIMIT 8u
#define EXPRESSION_DEPTH_LIMIT 64u

/* The inclusive upper bound on a const argument. A const generic parameter is
 * a type-level integer, not a machine integer, so the bound is a declared
 * frontend budget rather than a host width: exceeding it is refused with
 * E2S149 and never wrapped, clamped, or truncated. */
#define CONST_ARGUMENT_MAX 65535u

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
    TYPE_NOMINAL,
    TYPE_PARAMETER
} TypeKind;

/* What a nominal declaration binds in its bracket list, and therefore what an
 * instantiation of it must supply. */
typedef enum {
    PARAMETER_NONE,
    PARAMETER_TYPE,
    PARAMETER_CONST
} ParameterKind;

/* What an instantiation actually supplied. `ARGUMENT_NONE` is an unapplied
 * nominal, which is only well-formed for a declaration with no parameter. */
typedef enum {
    ARGUMENT_NONE,
    ARGUMENT_TYPE,
    ARGUMENT_CONST
} ArgumentKind;

typedef struct {
    TokenKind kind;
    char text[TEXT_LIMIT];
    size_t start;
    size_t end;
} Token;

/*
 * A type reference is flat on purpose: an argument is a builtin, a
 * non-parameterized nominal, or a const value, and never another
 * instantiation. Nesting is refused where it is written, so this structure
 * cannot describe a type the frontend has not checked.
 */
typedef struct {
    TypeKind kind;
    size_t nominal;
    size_t type_parameter;
    ArgumentKind argument_kind;
    unsigned long const_argument;
    TypeKind argument_type_kind;
    size_t argument_nominal;
    size_t start;
    size_t end;
} TypeRef;

typedef struct {
    char name[TEXT_LIMIT];
    ParameterKind parameter_kind;
    char parameter_name[TEXT_LIMIT];
    size_t parameter_start;
    size_t parameter_end;
    size_t type_parameter;
    size_t field_start;
    size_t field_count;
    size_t body_start;
    size_t body_end;
    size_t start;
    size_t end;
} Nominal;

typedef struct {
    char name[TEXT_LIMIT];
    size_t owner_nominal;
    size_t ordinal;
    size_t start;
    size_t end;
} TypeParameter;

typedef struct {
    char name[TEXT_LIMIT];
    size_t owner_nominal;
    TypeRef type;
    size_t start;
    size_t end;
} Field;

typedef struct {
    char name[TEXT_LIMIT];
    size_t owner_function;
    TypeRef type;
    size_t start;
    size_t end;
} Parameter;

typedef struct {
    char name[TEXT_LIMIT];
    size_t parameter_start;
    size_t parameter_count;
    TypeRef result;
    size_t body_start;
    size_t body_end;
    size_t start;
    size_t end;
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
    size_t argument_count;
    TypeRef result;
    size_t start;
    size_t end;
} Call;

/*
 * One entry per *distinct* instantiation, in first-use order. This is the
 * monomorphization plan: a backend that supports const generics emits one
 * specialization per row, and a backend that does not says so in
 * `tests/conformance/capabilities.tsv` instead of erasing the argument.
 */
typedef struct {
    TypeRef type;
    size_t uses;
    size_t start;
    size_t end;
} Instantiation;

typedef struct {
    Token tokens[TOKEN_LIMIT];
    size_t token_count;
    Nominal nominals[NOMINAL_LIMIT];
    size_t nominal_count;
    TypeParameter type_parameters[NOMINAL_LIMIT];
    size_t type_parameter_count;
    Field fields[FIELD_LIMIT];
    size_t field_count;
    Function functions[FUNCTION_LIMIT];
    size_t function_count;
    Parameter parameters[PARAMETER_LIMIT];
    size_t parameter_count;
    Local locals[LOCAL_LIMIT];
    size_t local_count;
    Call calls[CALL_LIMIT];
    size_t call_count;
    Instantiation instantiations[INSTANTIATION_LIMIT];
    size_t instantiation_count;
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
            "E2S152",
            start,
            end,
            "identifier or literal exceeds the const generic frontend text limit"
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
            "E2S152",
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
                        "E2S152",
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
                    "E2S152",
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
        /* `->` is taken before `-`, so a result arrow never lexes as the sign
         * of a negative const argument. */
        if (source[cursor] == '-' && cursor + 1 < length &&
            source[cursor + 1] == '>') {
            cursor += 2;
            if (!add_token(frontend, TOKEN_ARROW, source, start, cursor)) {
                return false;
            }
            continue;
        }
        if (strchr("[](),:{};=-", source[cursor]) != NULL) {
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
            "E2S152",
            cursor,
            cursor + 1,
            "unsupported byte 0x%02x in bounded const generic syntax",
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
            "E2S152",
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
            "E2S152",
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

static ptrdiff_t find_nominal(const Frontend *frontend, const char *name) {
    size_t index;
    for (index = 0; index < frontend->nominal_count; index += 1) {
        if (strcmp(frontend->nominals[index].name, name) == 0) {
            return (ptrdiff_t)index;
        }
    }
    return -1;
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

static const char *parameter_kind_name(ParameterKind kind) {
    switch (kind) {
        case PARAMETER_NONE: return "none";
        case PARAMETER_TYPE: return "type";
        case PARAMETER_CONST: return "const";
    }
    return "unknown";
}

static void skip_braced_body(const Frontend *frontend, size_t *index) {
    size_t cursor = *index;
    size_t depth = 0;
    while (cursor < frontend->token_count) {
        if (token_is(frontend, cursor, "{")) depth += 1;
        if (token_is(frontend, cursor, "}")) {
            depth -= 1;
            if (depth == 0) {
                *index = cursor + 1;
                return;
            }
        }
        cursor += 1;
    }
    *index = frontend->token_count;
}

/*
 * Parses one bracketed declaration parameter. Exactly one is admissible in
 * this slice, so a second is refused here rather than partially recorded.
 */
static bool parse_declaration_parameter(
    Frontend *frontend,
    size_t nominal_index,
    size_t *cursor
) {
    Nominal *nominal = &frontend->nominals[nominal_index];
    size_t index = *cursor;
    Token *name;
    bool is_const = false;

    if (token_is(frontend, index, "]")) {
        set_error(
            frontend,
            "E2S148",
            frontend->tokens[index - 1].start,
            frontend->tokens[index].end,
            "parameterized type `%s` must declare one parameter; write "
            "`%s[T]` or `%s[const NAME: Int]`",
            nominal->name,
            nominal->name,
            nominal->name
        );
        return false;
    }
    if (token_is(frontend, index, "const")) {
        is_const = true;
        index += 1;
    }
    if (!expect_identifier(
            frontend,
            &index,
            is_const ? "as a const parameter name"
                     : "as a generic type parameter",
            &name
        )) return false;
    nominal->parameter_start = name->start;
    nominal->parameter_end = name->end;
    snprintf(
        nominal->parameter_name,
        sizeof(nominal->parameter_name),
        "%s",
        name->text
    );
    if (is_const) {
        Token *annotation;
        if (!expect_token(
                frontend,
                &index,
                ":",
                "after a const parameter name"
            )) return false;
        if (!expect_identifier(
                frontend,
                &index,
                "as the const parameter type",
                &annotation
            )) return false;
        if (strcmp(annotation->text, "Int") != 0) {
            set_error(
                frontend,
                "E2S148",
                annotation->start,
                annotation->end,
                "const parameter `%s` of `%s` has type `%s`; only `Int` const "
                "parameters exist in this slice",
                nominal->parameter_name,
                nominal->name,
                annotation->text
            );
            return false;
        }
        nominal->parameter_kind = PARAMETER_CONST;
        nominal->parameter_end = annotation->end;
    } else {
        if (token_is(frontend, index, ":")) {
            set_error(
                frontend,
                "E2S148",
                frontend->tokens[index].start,
                frontend->tokens[index].end,
                "generic bounds are unsupported in this frontend slice; "
                "remove the bound or track bounded generics in #332"
            );
            return false;
        }
        nominal->parameter_kind = PARAMETER_TYPE;
        if (frontend->type_parameter_count >= NOMINAL_LIMIT) {
            set_error(
                frontend,
                "E2S152",
                name->start,
                name->end,
                "type parameter count exceeds %u",
                NOMINAL_LIMIT
            );
            return false;
        }
        {
            TypeParameter *parameter =
                &frontend->type_parameters[frontend->type_parameter_count];
            memset(parameter, 0, sizeof(*parameter));
            snprintf(
                parameter->name,
                sizeof(parameter->name),
                "%s",
                name->text
            );
            parameter->owner_nominal = nominal_index;
            parameter->ordinal = 0;
            parameter->start = name->start;
            parameter->end = name->end;
            nominal->type_parameter = frontend->type_parameter_count;
            frontend->type_parameter_count += 1;
        }
    }
    if (!token_is(frontend, index, "]")) {
        set_error(
            frontend,
            "E2S148",
            token_start(frontend, index),
            token_end(frontend, index),
            "type `%s` declares more than one parameter; exactly one type or "
            "const parameter is admissible in this slice",
            nominal->name
        );
        return false;
    }
    *cursor = index;
    return true;
}

static bool parse_nominal_header(Frontend *frontend, size_t *cursor) {
    size_t index = *cursor;
    size_t nominal_index;
    Token *name;
    Nominal *nominal;
    ptrdiff_t duplicate;

    if (frontend->nominal_count >= NOMINAL_LIMIT) {
        set_error(
            frontend,
            "E2S152",
            token_start(frontend, index),
            token_end(frontend, index),
            "type declaration count exceeds %u",
            NOMINAL_LIMIT
        );
        return false;
    }
    if (!expect_token(frontend, &index, "type", "at declaration start")) {
        return false;
    }
    if (!expect_identifier(frontend, &index, "as the type name", &name)) {
        return false;
    }
    duplicate = find_nominal(frontend, name->text);
    if (duplicate >= 0) {
        const Nominal *first = &frontend->nominals[(size_t)duplicate];
        set_error(
            frontend,
            "E2S148",
            name->start,
            name->end,
            "duplicate type `%s`; first declared at bytes %zu..%zu",
            name->text,
            first->start,
            first->end
        );
        return false;
    }
    nominal_index = frontend->nominal_count;
    nominal = &frontend->nominals[nominal_index];
    memset(nominal, 0, sizeof(*nominal));
    snprintf(nominal->name, sizeof(nominal->name), "%s", name->text);
    nominal->parameter_kind = PARAMETER_NONE;
    nominal->start = frontend->tokens[*cursor].start;
    nominal->field_start = frontend->field_count;
    frontend->nominal_count += 1;

    if (token_is(frontend, index, "[")) {
        index += 1;
        if (!parse_declaration_parameter(frontend, nominal_index, &index)) {
            return false;
        }
        index += 1;
    }
    if (!expect_token(frontend, &index, "=", "after the type name")) {
        return false;
    }
    if (!token_is(frontend, index, "{")) {
        set_error(
            frontend,
            "E2S152",
            token_start(frontend, index),
            token_end(frontend, index),
            "expected `{` before the field list of `%s`",
            nominal->name
        );
        return false;
    }
    nominal->body_start = index + 1;
    skip_braced_body(frontend, &index);
    if (index > frontend->token_count || index == 0 ||
        !token_is(frontend, index - 1, "}")) {
        set_error(
            frontend,
            "E2S152",
            nominal->start,
            token_end(frontend, frontend->token_count),
            "type `%s` field list is missing `}`",
            nominal->name
        );
        return false;
    }
    nominal->body_end = index - 1;
    nominal->end = frontend->tokens[index - 1].end;
    *cursor = index;
    return true;
}

/*
 * Reads a const argument. Every rejected shape is named where it is written
 * and refused before any identity is formed, so a malformed argument never
 * reaches the instantiation table and never reaches an artifact.
 */
static bool parse_const_argument(
    Frontend *frontend,
    const Nominal *nominal,
    size_t *cursor,
    unsigned long *value,
    size_t *end
) {
    size_t index = *cursor;
    const Token *token;
    unsigned long parsed = 0;
    size_t digit;

    if (token_is(frontend, index, "-")) {
        size_t start = frontend->tokens[index].start;
        size_t stop = frontend->tokens[index].end;
        if (token_has_kind(frontend, index + 1, TOKEN_INTEGER)) {
            stop = frontend->tokens[index + 1].end;
        }
        set_error(
            frontend,
            "E2S149",
            start,
            stop,
            "const argument to `%s` is negative; the const parameter `%s: Int` "
            "admits `0`..`%u` only",
            nominal->name,
            nominal->parameter_name,
            CONST_ARGUMENT_MAX
        );
        return false;
    }
    if (!token_has_kind(frontend, index, TOKEN_INTEGER)) {
        set_error(
            frontend,
            "E2S149",
            token_start(frontend, index),
            token_end(frontend, index),
            "const argument to `%s` is not an integer literal; const "
            "expressions and const inference are out of scope, so write the "
            "literal `%s` expects",
            nominal->name,
            nominal->parameter_name
        );
        return false;
    }
    token = &frontend->tokens[index];
    for (digit = 0; token->text[digit] != '\0'; digit += 1) {
        unsigned long next = parsed * 10ul +
            (unsigned long)(token->text[digit] - '0');
        if (parsed > CONST_ARGUMENT_MAX || next > CONST_ARGUMENT_MAX) {
            set_error(
                frontend,
                "E2S149",
                token->start,
                token->end,
                "const argument to `%s` exceeds the const parameter budget "
                "`0`..`%u`; the value is refused rather than wrapped",
                nominal->name,
                CONST_ARGUMENT_MAX
            );
            return false;
        }
        parsed = next;
    }
    *value = parsed;
    *end = token->end;
    *cursor = index + 1;
    return true;
}

/*
 * Records one distinct instantiation. Identity is by normalized argument, so
 * `Fixed[02]` folds into `Fixed[2]` and `Fixed[3]` does not.
 */
static bool type_equal(TypeRef left, TypeRef right);

static bool record_instantiation(Frontend *frontend, TypeRef type) {
    size_t index;
    Instantiation *instantiation;
    for (index = 0; index < frontend->instantiation_count; index += 1) {
        if (type_equal(frontend->instantiations[index].type, type)) {
            frontend->instantiations[index].uses += 1;
            return true;
        }
    }
    if (frontend->instantiation_count >= INSTANTIATION_LIMIT) {
        set_error(
            frontend,
            "E2S152",
            type.start,
            type.end,
            "distinct instantiation count exceeds %u; each distinct const "
            "argument is monomorphized separately, so reduce the number of "
            "instantiations",
            INSTANTIATION_LIMIT
        );
        return false;
    }
    instantiation = &frontend->instantiations[frontend->instantiation_count];
    memset(instantiation, 0, sizeof(*instantiation));
    instantiation->type = type;
    instantiation->uses = 1;
    instantiation->start = type.start;
    instantiation->end = type.end;
    frontend->instantiation_count += 1;
    return true;
}

static bool parse_type_ref(
    Frontend *frontend,
    ptrdiff_t owner_nominal,
    size_t *cursor,
    bool allow_void,
    TypeRef *output
) {
    Token *token;
    ptrdiff_t nominal_index;
    const Nominal *nominal;
    size_t index = *cursor;

    if (!expect_identifier(
            frontend,
            &index,
            "as a type in the bounded const generic frontend",
            &token
        )) return false;
    memset(output, 0, sizeof(*output));
    output->start = token->start;
    output->end = token->end;
    output->argument_kind = ARGUMENT_NONE;
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
                "E2S152",
                token->start,
                token->end,
                "`Void` is not valid in this value type position"
            );
            return false;
        }
        output->kind = TYPE_VOID;
        *cursor = index;
        return true;
    } else {
        nominal_index = find_nominal(frontend, token->text);
        if (nominal_index < 0) {
            /* The declaring type's own parameter is the one non-nominal name
             * that can appear here, and each kind fails for its own reason. */
            if (owner_nominal >= 0) {
                const Nominal *owner =
                    &frontend->nominals[(size_t)owner_nominal];
                if (owner->parameter_kind != PARAMETER_NONE &&
                    strcmp(owner->parameter_name, token->text) == 0) {
                    if (owner->parameter_kind == PARAMETER_CONST) {
                        set_error(
                            frontend,
                            "E2S148",
                            token->start,
                            token->end,
                            "const parameter `%s` of `%s` is not a type and "
                            "cannot be a field type; a const parameter is "
                            "part of the type identity and is never stored as "
                            "a runtime field",
                            owner->parameter_name,
                            owner->name
                        );
                        return false;
                    }
                    output->kind = TYPE_PARAMETER;
                    output->type_parameter = owner->type_parameter;
                    *cursor = index;
                    return true;
                }
            }
            set_error(
                frontend,
                "E2S152",
                token->start,
                token->end,
                "unknown type `%s`; declare it with `type %s = { ... }` or "
                "use `Int`, `Bool`, or `Text`",
                token->text,
                token->text
            );
            return false;
        }
        output->kind = TYPE_NOMINAL;
        output->nominal = (size_t)nominal_index;
    }

    nominal = output->kind == TYPE_NOMINAL
        ? &frontend->nominals[output->nominal] : NULL;
    if (!token_is(frontend, index, "[")) {
        if (nominal != NULL && nominal->parameter_kind != PARAMETER_NONE) {
            set_error(
                frontend,
                "E2S150",
                token->start,
                token->end,
                "`%s` expects one %s argument; declaration at bytes "
                "%zu..%zu; write `%s[...]`",
                nominal->name,
                parameter_kind_name(nominal->parameter_kind),
                nominal->start,
                nominal->end,
                nominal->name
            );
            return false;
        }
        *cursor = index;
        return true;
    }
    if (nominal == NULL) {
        set_error(
            frontend,
            "E2S150",
            token->start,
            frontend->tokens[index].end,
            "builtin type `%s` does not accept type arguments",
            token->text
        );
        return false;
    }
    if (nominal->parameter_kind == PARAMETER_NONE) {
        set_error(
            frontend,
            "E2S150",
            frontend->tokens[index].start,
            frontend->tokens[index].end,
            "type `%s` declares no parameter and does not accept arguments; "
            "declaration at bytes %zu..%zu; remove the argument list",
            nominal->name,
            nominal->start,
            nominal->end
        );
        return false;
    }
    index += 1;
    if (nominal->parameter_kind == PARAMETER_CONST) {
        unsigned long value = 0;
        size_t argument_end = 0;
        /* A name that already denotes a type is reported as the kind mismatch
         * it is, rather than as a missing literal. */
        if (token_has_kind(frontend, index, TOKEN_IDENTIFIER) &&
            (find_nominal(frontend, frontend->tokens[index].text) >= 0 ||
             strcmp(frontend->tokens[index].text, "Int") == 0 ||
             strcmp(frontend->tokens[index].text, "Bool") == 0 ||
             strcmp(frontend->tokens[index].text, "Text") == 0)) {
            set_error(
                frontend,
                "E2S149",
                frontend->tokens[index].start,
                frontend->tokens[index].end,
                "`%s` declares the const parameter `%s: Int`, so `%s` is a "
                "type where an integer literal is required",
                nominal->name,
                nominal->parameter_name,
                frontend->tokens[index].text
            );
            return false;
        }
        if (!parse_const_argument(
                frontend,
                nominal,
                &index,
                &value,
                &argument_end
            )) return false;
        output->argument_kind = ARGUMENT_CONST;
        output->const_argument = value;
        output->end = argument_end;
    } else {
        TypeRef argument;
        if (token_has_kind(frontend, index, TOKEN_INTEGER) ||
            token_is(frontend, index, "-")) {
            set_error(
                frontend,
                "E2S149",
                frontend->tokens[index].start,
                token_end(frontend, index),
                "`%s` declares the type parameter `%s`, so an integer literal "
                "is not admissible; declare `%s[const %s: Int]` to take a "
                "const argument",
                nominal->name,
                nominal->parameter_name,
                nominal->name,
                nominal->parameter_name
            );
            return false;
        }
        if (!parse_type_ref(frontend, owner_nominal, &index, false, &argument)) {
            return false;
        }
        if (argument.kind == TYPE_NOMINAL &&
            argument.argument_kind != ARGUMENT_NONE) {
            set_error(
                frontend,
                "E2S150",
                argument.start,
                argument.end,
                "nested instantiation is unsupported in this frontend slice; "
                "give `%s` a builtin or non-parameterized nominal argument",
                nominal->name
            );
            return false;
        }
        if (argument.kind == TYPE_PARAMETER) {
            set_error(
                frontend,
                "E2S150",
                argument.start,
                argument.end,
                "a type parameter is not admissible as an argument to `%s` in "
                "this frontend slice; pass a concrete type",
                nominal->name
            );
            return false;
        }
        output->argument_kind = ARGUMENT_TYPE;
        output->argument_type_kind = argument.kind;
        output->argument_nominal = argument.nominal;
        output->end = argument.end;
    }
    if (token_is(frontend, index, ",")) {
        set_error(
            frontend,
            "E2S150",
            frontend->tokens[index].start,
            frontend->tokens[index].end,
            "`%s` declares one parameter and accepts exactly one argument; "
            "declaration at bytes %zu..%zu; pass exactly one",
            nominal->name,
            nominal->start,
            nominal->end
        );
        return false;
    }
    if (!token_is(frontend, index, "]")) {
        set_error(
            frontend,
            "E2S150",
            token_start(frontend, index),
            token_end(frontend, index),
            "expected `]` after the argument to `%s`",
            nominal->name
        );
        return false;
    }
    output->end = frontend->tokens[index].end;
    index += 1;
    if (!record_instantiation(frontend, *output)) return false;
    *cursor = index;
    return true;
}

static bool type_equal(TypeRef left, TypeRef right) {
    if (left.kind != right.kind) return false;
    if (left.kind == TYPE_PARAMETER) {
        return left.type_parameter == right.type_parameter;
    }
    if (left.kind != TYPE_NOMINAL) return true;
    if (left.nominal != right.nominal) return false;
    if (left.argument_kind != right.argument_kind) return false;
    if (left.argument_kind == ARGUMENT_CONST) {
        return left.const_argument == right.const_argument;
    }
    if (left.argument_kind == ARGUMENT_TYPE) {
        if (left.argument_type_kind != right.argument_type_kind) return false;
        return left.argument_type_kind != TYPE_NOMINAL ||
            left.argument_nominal == right.argument_nominal;
    }
    return true;
}

/*
 * The normalized argument form. A const argument is tagged `const:Int:` and a
 * type argument keeps its own `builtin:`/`nominal:` tag, so the two can never
 * collide inside an enclosing identity.
 */
static void argument_id(
    const Frontend *frontend,
    TypeRef type,
    char output[ARGUMENT_IDENTITY_LIMIT]
) {
    size_t size = ARGUMENT_IDENTITY_LIMIT;
    if (type.argument_kind == ARGUMENT_CONST) {
        snprintf(output, size, "const:Int:%lu", type.const_argument);
        return;
    }
    switch (type.argument_type_kind) {
        case TYPE_INT: snprintf(output, size, "builtin:Int"); return;
        case TYPE_BOOL: snprintf(output, size, "builtin:Bool"); return;
        case TYPE_TEXT: snprintf(output, size, "builtin:Text"); return;
        case TYPE_NOMINAL:
            snprintf(
                output,
                size,
                "nominal:%s",
                frontend->nominals[type.argument_nominal].name
            );
            return;
        case TYPE_VOID:
        case TYPE_PARAMETER:
            break;
    }
    snprintf(output, size, "invalid");
}

static void type_id(
    const Frontend *frontend,
    TypeRef type,
    char output[IDENTITY_LIMIT]
) {
    size_t size = IDENTITY_LIMIT;
    switch (type.kind) {
        case TYPE_INT:
            snprintf(output, size, "builtin:Int");
            return;
        case TYPE_BOOL:
            snprintf(output, size, "builtin:Bool");
            return;
        case TYPE_TEXT:
            snprintf(output, size, "builtin:Text");
            return;
        case TYPE_VOID:
            snprintf(output, size, "builtin:Void");
            return;
        case TYPE_NOMINAL: {
            const Nominal *nominal = &frontend->nominals[type.nominal];
            char argument[ARGUMENT_IDENTITY_LIMIT];
            if (type.argument_kind == ARGUMENT_NONE) {
                snprintf(output, size, "nominal:%s", nominal->name);
                return;
            }
            argument_id(frontend, type, argument);
            snprintf(
                output,
                size,
                "nominal:%s/args=%s",
                nominal->name,
                argument
            );
            return;
        }
        case TYPE_PARAMETER: {
            const TypeParameter *parameter =
                &frontend->type_parameters[type.type_parameter];
            snprintf(
                output,
                size,
                "type-parameter:nominal:%s:%zu",
                frontend->nominals[parameter->owner_nominal].name,
                parameter->ordinal
            );
            return;
        }
    }
    snprintf(output, size, "invalid");
}

static void type_display(
    const Frontend *frontend,
    TypeRef type,
    char output[DISPLAY_LIMIT]
) {
    size_t size = DISPLAY_LIMIT;
    switch (type.kind) {
        case TYPE_INT:
            snprintf(output, size, "Int");
            return;
        case TYPE_BOOL:
            snprintf(output, size, "Bool");
            return;
        case TYPE_TEXT:
            snprintf(output, size, "Text");
            return;
        case TYPE_VOID:
            snprintf(output, size, "Void");
            return;
        case TYPE_NOMINAL: {
            const Nominal *nominal = &frontend->nominals[type.nominal];
            if (type.argument_kind == ARGUMENT_NONE) {
                snprintf(output, size, "%s", nominal->name);
                return;
            }
            if (type.argument_kind == ARGUMENT_CONST) {
                snprintf(
                    output,
                    size,
                    "%s[%lu]",
                    nominal->name,
                    type.const_argument
                );
                return;
            }
            switch (type.argument_type_kind) {
                case TYPE_INT:
                    snprintf(output, size, "%s[Int]", nominal->name);
                    return;
                case TYPE_BOOL:
                    snprintf(output, size, "%s[Bool]", nominal->name);
                    return;
                case TYPE_TEXT:
                    snprintf(output, size, "%s[Text]", nominal->name);
                    return;
                case TYPE_NOMINAL:
                    snprintf(
                        output,
                        size,
                        "%s[%s]",
                        nominal->name,
                        frontend->nominals[type.argument_nominal].name
                    );
                    return;
                case TYPE_VOID:
                case TYPE_PARAMETER:
                    break;
            }
            snprintf(output, size, "invalid");
            return;
        }
        case TYPE_PARAMETER:
            snprintf(
                output,
                size,
                "%s",
                frontend->type_parameters[type.type_parameter].name
            );
            return;
    }
    snprintf(output, size, "invalid");
}

static bool field_name_exists(
    const Frontend *frontend,
    const Nominal *nominal,
    const char *name
) {
    size_t offset;
    for (offset = 0; offset < nominal->field_count; offset += 1) {
        const Field *field = &frontend->fields[nominal->field_start + offset];
        if (strcmp(field->name, name) == 0) return true;
    }
    return false;
}

static bool parse_fields(Frontend *frontend, size_t nominal_index) {
    Nominal *nominal = &frontend->nominals[nominal_index];
    size_t cursor = nominal->body_start;
    while (cursor < nominal->body_end) {
        Token *name;
        TypeRef type;
        Field *field;
        if (!expect_identifier(frontend, &cursor, "as a field name", &name)) {
            return false;
        }
        if (!expect_token(frontend, &cursor, ":", "after a field name")) {
            return false;
        }
        if (!parse_type_ref(
                frontend,
                (ptrdiff_t)nominal_index,
                &cursor,
                false,
                &type
            )) return false;
        if (field_name_exists(frontend, nominal, name->text)) {
            set_error(
                frontend,
                "E2S148",
                name->start,
                name->end,
                "duplicate field `%s` in `%s`",
                name->text,
                nominal->name
            );
            return false;
        }
        if (frontend->field_count >= FIELD_LIMIT) {
            set_error(
                frontend,
                "E2S152",
                name->start,
                name->end,
                "field count exceeds %u",
                FIELD_LIMIT
            );
            return false;
        }
        field = &frontend->fields[frontend->field_count];
        memset(field, 0, sizeof(*field));
        snprintf(field->name, sizeof(field->name), "%s", name->text);
        field->owner_nominal = nominal_index;
        field->type = type;
        field->start = name->start;
        field->end = type.end;
        frontend->field_count += 1;
        nominal->field_count += 1;
        if (token_is(frontend, cursor, ",")) {
            cursor += 1;
        } else if (cursor < nominal->body_end) {
            set_error(
                frontend,
                "E2S152",
                token_start(frontend, cursor),
                token_end(frontend, cursor),
                "expected `,` after the field type in `%s`",
                nominal->name
            );
            return false;
        }
    }
    if (nominal->field_count == 0) {
        set_error(
            frontend,
            "E2S148",
            nominal->start,
            nominal->end,
            "type `%s` must declare at least one field",
            nominal->name
        );
        return false;
    }
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
    if (function->parameter_count >= ARGUMENT_LIMIT ||
        frontend->parameter_count >= PARAMETER_LIMIT) {
        set_error(
            frontend,
            "E2S152",
            name->start,
            name->end,
            "value parameter count exceeds the per-function limit %u",
            ARGUMENT_LIMIT
        );
        return false;
    }
    if (parameter_name_exists(frontend, function, name->text)) {
        set_error(
            frontend,
            "E2S152",
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
            "E2S152",
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
            "E2S152",
            name->start,
            name->end,
            "duplicate function `%s`; first declared at bytes %zu..%zu",
            name->text,
            first->start,
            first->end
        );
        return false;
    }
    if (token_is(frontend, index, "[")) {
        set_error(
            frontend,
            "E2S148",
            frontend->tokens[index].start,
            frontend->tokens[index].end,
            "const and type parameters on functions are unsupported in this "
            "frontend slice; parameterize a type declaration instead"
        );
        return false;
    }
    function_index = frontend->function_count;
    function = &frontend->functions[function_index];
    memset(function, 0, sizeof(*function));
    snprintf(function->name, sizeof(function->name), "%s", name->text);
    function->start = frontend->tokens[*cursor].start;
    function->parameter_start = frontend->parameter_count;
    frontend->function_count += 1;

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
        if (!parse_type_ref(frontend, -1, &index, false, &parameter_type)) {
            return false;
        }
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
                    "E2S152",
                    frontend->tokens[index].start,
                    frontend->tokens[index].end,
                    "trailing comma is unsupported in value parameter lists"
                );
                return false;
            }
        } else if (!token_is(frontend, index, ")")) {
            set_error(
                frontend,
                "E2S152",
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
        if (!parse_type_ref(frontend, -1, &index, true, &function->result)) {
            return false;
        }
    } else {
        memset(&function->result, 0, sizeof(function->result));
        function->result.kind = TYPE_VOID;
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
            "E2S152",
            frontend->tokens[function->body_start - 1].start,
            token_end(frontend, frontend->token_count),
            "function `%s` body is missing `}`",
            function->name
        );
        return false;
    }
    function->body_end = index;
    function->end = frontend->tokens[index].end;
    *cursor = index + 1;
    return true;
}

/* Type declarations are collected before anything that can mention them, so a
 * declaration's position in the file never changes an identity. */
static bool collect_nominals(Frontend *frontend) {
    size_t cursor = 0;
    while (cursor < frontend->token_count) {
        if (token_is(frontend, cursor, "type")) {
            if (!parse_nominal_header(frontend, &cursor)) return false;
            continue;
        }
        if (token_is(frontend, cursor, "fn")) {
            while (cursor < frontend->token_count &&
                !token_is(frontend, cursor, "{")) cursor += 1;
            if (cursor >= frontend->token_count) {
                set_error(
                    frontend,
                    "E2S152",
                    token_start(frontend, cursor),
                    token_end(frontend, cursor),
                    "function declaration is missing a body"
                );
                return false;
            }
            skip_braced_body(frontend, &cursor);
            continue;
        }
        set_error(
            frontend,
            "E2S152",
            frontend->tokens[cursor].start,
            frontend->tokens[cursor].end,
            "unsupported top-level token `%s`; expected `type` or `fn`",
            frontend->tokens[cursor].text
        );
        return false;
    }
    return true;
}

static bool collect_functions(Frontend *frontend) {
    size_t cursor = 0;
    while (cursor < frontend->token_count) {
        if (token_is(frontend, cursor, "type")) {
            while (cursor < frontend->token_count &&
                !token_is(frontend, cursor, "{")) cursor += 1;
            skip_braced_body(frontend, &cursor);
            continue;
        }
        if (!parse_function_header(frontend, &cursor)) return false;
    }
    if (frontend->function_count == 0) {
        set_error(
            frontend,
            "E2S152",
            0,
            0,
            "bounded const generic frontend requires at least one function"
        );
        return false;
    }
    return true;
}

static bool collect_fields(Frontend *frontend) {
    size_t index;
    for (index = 0; index < frontend->nominal_count; index += 1) {
        if (!parse_fields(frontend, index)) return false;
    }
    return true;
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
    size_t value_argument_count = 0;
    size_t call_end;

    if (callee_index < 0) {
        set_error(
            frontend,
            "E2S152",
            name->start,
            name->end,
            "unknown function `%s` in bounded const generic call",
            name->text
        );
        return false;
    }
    callee = &frontend->functions[(size_t)callee_index];
    index += 1;
    if (token_is(frontend, index, "[")) {
        set_error(
            frontend,
            "E2S148",
            frontend->tokens[index].start,
            frontend->tokens[index].end,
            "function `%s` takes no const or type arguments in this frontend "
            "slice; a const parameter belongs to a type declaration",
            callee->name
        );
        return false;
    }
    if (!expect_token(frontend, &index, "(", "after the call target")) {
        return false;
    }
    while (!token_is(frontend, index, ")")) {
        TypeRef actual;
        if (value_argument_count >= ARGUMENT_LIMIT) {
            set_error(
                frontend,
                "E2S152",
                token_start(frontend, index),
                token_end(frontend, index),
                "value argument count exceeds %u",
                ARGUMENT_LIMIT
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
        if (value_argument_count < callee->parameter_count) {
            const Parameter *parameter = &frontend->parameters[
                callee->parameter_start + value_argument_count
            ];
            if (!type_equal(actual, parameter->type)) {
                char actual_name[DISPLAY_LIMIT];
                char expected_name[DISPLAY_LIMIT];
                type_display(frontend, actual, actual_name);
                type_display(frontend, parameter->type, expected_name);
                set_error(
                    frontend,
                    "E2S151",
                    actual.start,
                    actual.end,
                    "argument %zu to `%s` has type `%s`; expected `%s`; "
                    "declaration at bytes %zu..%zu; a const argument is part "
                    "of the type identity, so change the value or the "
                    "declared const argument",
                    value_argument_count + 1,
                    callee->name,
                    actual_name,
                    expected_name,
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
                    "E2S152",
                    frontend->tokens[index].start,
                    frontend->tokens[index].end,
                    "trailing comma is unsupported in value arguments"
                );
                return false;
            }
        } else if (!token_is(frontend, index, ")")) {
            set_error(
                frontend,
                "E2S152",
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
            "E2S152",
            name->start,
            call_end,
            "function `%s` expects %zu value argument%s, found %zu",
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
            "E2S152",
            name->start,
            call_end,
            "call count exceeds %u",
            CALL_LIMIT
        );
        return false;
    }
    *output = callee->result;
    output->start = name->start;
    output->end = call_end;
    {
        Call *call = &frontend->calls[frontend->call_count];
        memset(call, 0, sizeof(*call));
        call->caller = function_index;
        call->callee = (size_t)callee_index;
        call->argument_count = value_argument_count;
        call->result = *output;
        call->start = name->start;
        call->end = call_end;
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
            "E2S152",
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
            "E2S152",
            token_start(frontend, index),
            token_end(frontend, index),
            "expected a bounded const generic expression"
        );
        return false;
    }
    token = &frontend->tokens[index];
    memset(output, 0, sizeof(*output));
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
            "E2S152",
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
    {
        /* A const parameter name in expression position is the erasure this
         * slice exists to prevent, so it is named rather than reported as an
         * unknown value. */
        size_t nominal;
        for (nominal = 0; nominal < frontend->nominal_count; nominal += 1) {
            const Nominal *owner = &frontend->nominals[nominal];
            if (owner->parameter_kind != PARAMETER_CONST) continue;
            if (strcmp(owner->parameter_name, token->text) != 0) continue;
            set_error(
                frontend,
                "E2S148",
                token->start,
                token->end,
                "const parameter `%s` of `%s` cannot be used as a runtime "
                "value; it exists only in the type identity",
                owner->parameter_name,
                owner->name
            );
            return false;
        }
    }
    set_error(
        frontend,
        "E2S152",
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
            "E2S152",
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
            "E2S152",
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
    char actual_name[DISPLAY_LIMIT];
    char expected_name[DISPLAY_LIMIT];
    if (type_equal(actual, expected)) return true;
    type_display(frontend, actual, actual_name);
    type_display(frontend, expected, expected_name);
    set_error(
        frontend,
        "E2S151",
        start,
        end,
        "%s has type `%s`; expected `%s`; instantiations differing only in "
        "their const argument are different types",
        context,
        actual_name,
        expected_name
    );
    return false;
}

static bool parse_function_body(Frontend *frontend, size_t function_index) {
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
                    "because const generic locals require an explicit type"
                )) return false;
            if (!parse_type_ref(frontend, -1, &cursor, false, &annotated)) {
                return false;
            }
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
            if (!add_local(frontend, function_index, name, annotated)) {
                return false;
            }
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
                    "E2S152",
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
                    "E2S152",
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
            if (printed.kind != TYPE_INT && printed.kind != TYPE_TEXT &&
                printed.kind != TYPE_BOOL) {
                char printed_name[DISPLAY_LIMIT];
                type_display(frontend, printed, printed_name);
                set_error(
                    frontend,
                    "E2S152",
                    printed.start,
                    printed.end,
                    "cannot print an expression of type `%s`",
                    printed_name
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
            "E2S152",
            frontend->tokens[cursor].start,
            frontend->tokens[cursor].end,
            "unsupported statement `%s` in bounded const generic function `%s`",
            frontend->tokens[cursor].text,
            function->name
        );
        return false;
    }
    if (function->result.kind != TYPE_VOID && !function->has_return) {
        set_error(
            frontend,
            "E2S152",
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
    if (fprintf(output, "kofun-const-generics-ir/v1\n") < 0) goto fail;
    for (index = 0; index < frontend->nominal_count; index += 1) {
        const Nominal *nominal = &frontend->nominals[index];
        if (fprintf(
                output,
                "nominal|nominal-id=nominal:%s|name=%s|parameter=%s|"
                "parameter-name=%s|fields=%zu|span=%zu..%zu\n",
                nominal->name,
                nominal->name,
                parameter_kind_name(nominal->parameter_kind),
                nominal->parameter_kind == PARAMETER_NONE
                    ? "-" : nominal->parameter_name,
                nominal->field_count,
                nominal->start,
                nominal->end
            ) < 0) goto fail;
    }
    for (index = 0; index < frontend->nominal_count; index += 1) {
        const Nominal *nominal = &frontend->nominals[index];
        if (nominal->parameter_kind != PARAMETER_CONST) continue;
        /* `ownership-kind=neutral` is the RFC-0004 statement: a const argument
         * is not a type, so K-SUBST substitutes no kind through it and K-PARAM
         * does not classify it. Every instantiation of this declaration
         * therefore classifies identically while remaining a distinct type. */
        if (fprintf(
                output,
                "const-parameter|const-parameter-id=const-parameter:nominal:%s:0"
                "|owner=nominal:%s|name=%s|type=builtin:Int|index=0|"
                "minimum=0|maximum=%u|ownership-kind=neutral|span=%zu..%zu\n",
                nominal->name,
                nominal->name,
                nominal->parameter_name,
                CONST_ARGUMENT_MAX,
                nominal->parameter_start,
                nominal->parameter_end
            ) < 0) goto fail;
    }
    for (index = 0; index < frontend->type_parameter_count; index += 1) {
        const TypeParameter *parameter = &frontend->type_parameters[index];
        const Nominal *owner = &frontend->nominals[parameter->owner_nominal];
        if (fprintf(
                output,
                "type-parameter|type-parameter-id=type-parameter:nominal:%s:%zu"
                "|owner=nominal:%s|name=%s|index=%zu|"
                "ownership-kind=substituted|span=%zu..%zu\n",
                owner->name,
                parameter->ordinal,
                owner->name,
                parameter->name,
                parameter->ordinal,
                parameter->start,
                parameter->end
            ) < 0) goto fail;
    }
    for (index = 0; index < frontend->field_count; index += 1) {
        const Field *field = &frontend->fields[index];
        const Nominal *owner = &frontend->nominals[field->owner_nominal];
        char type[IDENTITY_LIMIT];
        type_id(frontend, field->type, type);
        if (fprintf(
                output,
                "field|owner=nominal:%s|name=%s|type=%s|span=%zu..%zu\n",
                owner->name,
                field->name,
                type,
                field->start,
                field->end
            ) < 0) goto fail;
    }
    for (index = 0; index < frontend->function_count; index += 1) {
        const Function *function = &frontend->functions[index];
        char result[IDENTITY_LIMIT];
        type_id(frontend, function->result, result);
        if (fprintf(
                output,
                "function|function-id=function:%s|name=%s|parameters=%zu|"
                "result=%s|span=%zu..%zu\n",
                function->name,
                function->name,
                function->parameter_count,
                result,
                function->start,
                function->end
            ) < 0) goto fail;
    }
    for (index = 0; index < frontend->parameter_count; index += 1) {
        const Parameter *parameter = &frontend->parameters[index];
        const Function *owner =
            &frontend->functions[parameter->owner_function];
        char type[IDENTITY_LIMIT];
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
        const Function *owner = &frontend->functions[local->owner_function];
        char type[IDENTITY_LIMIT];
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
        char result[IDENTITY_LIMIT];
        type_id(frontend, call->result, result);
        if (fprintf(
                output,
                "call|caller=function:%s|callee=function:%s|"
                "value-arguments=%zu|result=%s|use-span=%zu..%zu|"
                "declaration-span=%zu..%zu\n",
                caller->name,
                callee->name,
                call->argument_count,
                result,
                call->start,
                call->end,
                callee->start,
                callee->end
            ) < 0) goto fail;
    }
    for (index = 0; index < frontend->instantiation_count; index += 1) {
        const Instantiation *instantiation = &frontend->instantiations[index];
        const Nominal *nominal =
            &frontend->nominals[instantiation->type.nominal];
        char identity[IDENTITY_LIMIT];
        char argument[ARGUMENT_IDENTITY_LIMIT];
        type_id(frontend, instantiation->type, identity);
        argument_id(frontend, instantiation->type, argument);
        if (fprintf(
                output,
                "instantiation|instantiation-id=%s|nominal=nominal:%s|"
                "argument-kind=%s|argument=%s|monomorphization=required|"
                "uses=%zu|first-use-span=%zu..%zu\n",
                identity,
                nominal->name,
                instantiation->type.argument_kind == ARGUMENT_CONST
                    ? "const" : "type",
                argument,
                instantiation->uses,
                instantiation->start,
                instantiation->end
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
    if (fprintf(output, "kofun-const-generics-token-tape/v1\n") < 0) goto fail;
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
        fprintf(
            stderr,
            "usage: kofun-const-generics-frontend SOURCE IR TOKENS\n"
        );
        return 2;
    }
    remove(argv[2]);
    remove(argv[3]);
    source = read_source(argv[1], &length);
    if (source == NULL) {
        fprintf(
            stderr,
            "kofun-const-generics-frontend: cannot read bounded source\n"
        );
        return 2;
    }
    if (!tokenize(&frontend, source, length) ||
        !collect_nominals(&frontend) ||
        !collect_functions(&frontend) ||
        !collect_fields(&frontend) ||
        !type_bodies(&frontend)) {
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
            "kofun-const-generics-frontend: cannot commit output artifacts\n"
        );
        free(source);
        return 2;
    }
    free(source);
    return 0;
}
