/*
 * Bounded Optional frontend (#70).
 *
 * Makes `null` and `T?` real Stage 2 frontend constructs: `null` is classified
 * as a keyword, one postfix `?` is parsed on a primary type, and the result is
 * represented as `Optional(TypeId)` in typed IR. The normative surface is
 * already fixed by #50/#51 in
 * `tests/conformance/syntax/issues_48_60/surface-cases.tsv` and
 * `spec/syntax/EXPRESSIONS_AND_LITERALS.md`; this frontend consumes that
 * contract rather than restating it.
 *
 * The slice stops before runtime representation, coalescing, narrowing,
 * matching, propagation, safe navigation, and any backend lowering. Nothing
 * here implies a tag, a layout, or a niche.
 *
 * Typing, exactly as #50 fixes it:
 *
 *   - `null` has no standalone inferred type;
 *   - under an expected `Optional(T)`, `null` has type `Optional(T)`;
 *   - under an expected `T`, `null` is rejected;
 *   - a `T` may satisfy an expected `Optional(T)` through one explicit
 *     injection rule, written once in `assignable`;
 *   - an `Optional(T)` never satisfies an expected `T`.
 *
 * The suffix binds to the complete primary type before it, so `List[Int]?` is
 * `Optional(List(Int))` and `List[Int?]` is `List(Optional(Int))`. They are
 * structurally distinct and the gate pins both.
 *
 * Diagnostics accumulate rather than stopping at the first one: a malformed
 * suffix recovers to the next declaration boundary so a later independent
 * declaration is still reported, and recovery never fabricates an Optional
 * node from the broken input.
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOURCE_LIMIT (1024u * 1024u)
#define TOKEN_LIMIT 8192u
#define TEXT_LIMIT 128u
#define TYPE_LIMIT 1024u
#define FUNCTION_LIMIT 128u
#define PARAMETER_LIMIT 512u
#define LOCAL_LIMIT 512u
#define RETURN_LIMIT 512u
#define CALL_LIMIT 512u
#define DIAGNOSTIC_LIMIT 32u
#define IDENTITY_LIMIT 512u
#define TYPE_DEPTH_LIMIT 8u

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
    TYPE_LIST,
    TYPE_OPTIONAL
} TypeKind;

typedef struct {
    TokenKind kind;
    char text[TEXT_LIMIT];
    size_t start;
    size_t end;
} Token;

typedef struct {
    TypeKind kind;
    size_t element;       /* TYPE_LIST and TYPE_OPTIONAL element */
    size_t start;
    size_t end;
    size_t suffix_start;  /* the `?` itself, for TYPE_OPTIONAL */
    size_t suffix_end;
} Type;

typedef struct {
    char name[TEXT_LIMIT];
    size_t owner_function;
    size_t type;
    size_t start;
    size_t end;
} Binding;

typedef struct {
    char name[TEXT_LIMIT];
    size_t parameter_start;
    size_t parameter_count;
    size_t result;
    size_t body_start;
    size_t body_end;
    size_t start;
    size_t end;
} Function;

typedef struct {
    size_t owner_function;
    size_t type;     /* the result type this return produces */
    size_t written;  /* the type as written, before any injection */
    bool is_null;
    size_t start;
    size_t end;
} Returned;

typedef struct {
    size_t caller;
    size_t callee;
    size_t start;
    size_t end;
} Call;

typedef struct {
    char text[1024];
    size_t start;
} Diagnostic;

typedef struct {
    Token tokens[TOKEN_LIMIT];
    size_t token_count;
    Type types[TYPE_LIMIT];
    size_t type_count;
    Function functions[FUNCTION_LIMIT];
    size_t function_count;
    Binding parameters[PARAMETER_LIMIT];
    size_t parameter_count;
    Binding locals[LOCAL_LIMIT];
    size_t local_count;
    Returned returns[RETURN_LIMIT];
    size_t return_count;
    Call calls[CALL_LIMIT];
    size_t call_count;
    Diagnostic diagnostics[DIAGNOSTIC_LIMIT];
    size_t diagnostic_count;
    /* Set while a statement is being recovered, so one broken statement
     * reports once instead of cascading. */
    bool recovering;
} Frontend;

static void report(
    Frontend *frontend,
    const char *code,
    size_t start,
    size_t end,
    const char *format,
    ...
) {
    char detail[800];
    va_list arguments;
    Diagnostic *diagnostic;

    if (frontend->recovering) return;
    if (frontend->diagnostic_count >= DIAGNOSTIC_LIMIT) return;
    va_start(arguments, format);
    if (vsnprintf(detail, sizeof(detail), format, arguments) < 0) {
        detail[0] = '\0';
    }
    va_end(arguments);
    diagnostic = &frontend->diagnostics[frontend->diagnostic_count];
    snprintf(
        diagnostic->text,
        sizeof(diagnostic->text),
        "error[%s]: %s at bytes %zu..%zu",
        code,
        detail,
        start,
        end
    );
    diagnostic->start = start;
    frontend->diagnostic_count += 1;
    frontend->recovering = true;
}

static bool failed(const Frontend *frontend) {
    return frontend->diagnostic_count > 0;
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

static bool add_token(
    Frontend *frontend,
    TokenKind kind,
    const char *source,
    size_t start,
    size_t end
) {
    Token *token;
    size_t length = end - start;
    if (frontend->token_count >= TOKEN_LIMIT || length >= TEXT_LIMIT ||
        length == 0) {
        report(frontend, "E2S141", start, end,
            "source exceeds the Optional frontend limits");
        return false;
    }
    token = &frontend->tokens[frontend->token_count];
    token->kind = kind;
    token->start = start;
    token->end = end;
    memcpy(token->text, source + start, length);
    token->text[length] = '\0';
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
            if (!add_token(frontend, TOKEN_IDENTIFIER, source, start, cursor)) {
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
                if (source[cursor] == '\\' && cursor + 1 < length) cursor += 1;
                cursor += 1;
            }
            if (cursor >= length) {
                report(frontend, "E2S141", start, length,
                    "unterminated text literal");
                return false;
            }
            cursor += 1;
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
        if (strchr("[](),:{}=?+-*", source[cursor]) != NULL) {
            cursor += 1;
            if (!add_token(
                    frontend, TOKEN_PUNCTUATION, source, start, cursor)) {
                return false;
            }
            continue;
        }
        report(frontend, "E2S141", cursor, cursor + 1,
            "unsupported byte 0x%02x in bounded Optional syntax", byte);
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

static size_t add_type(
    Frontend *frontend,
    TypeKind kind,
    size_t element,
    size_t start,
    size_t end
) {
    Type *type;
    if (frontend->type_count >= TYPE_LIMIT) return 0;
    type = &frontend->types[frontend->type_count];
    type->kind = kind;
    type->element = element;
    type->start = start;
    type->end = end;
    type->suffix_start = 0;
    type->suffix_end = 0;
    frontend->type_count += 1;
    return frontend->type_count - 1;
}

/* Two types are the same when their whole structure is. Optional(Int) is not
 * Int, and List(Optional(Int)) is not Optional(List(Int)). */
static bool type_equal(const Frontend *frontend, size_t left, size_t right) {
    size_t depth = 0;
    while (depth < TYPE_DEPTH_LIMIT) {
        const Type *a = &frontend->types[left];
        const Type *b = &frontend->types[right];
        if (a->kind != b->kind) return false;
        if (a->kind != TYPE_LIST && a->kind != TYPE_OPTIONAL) return true;
        left = a->element;
        right = b->element;
        depth += 1;
    }
    return false;
}

/*
 * The single injection rule. A `T` satisfies an expected `Optional(T)`; an
 * `Optional(T)` never satisfies an expected `T`. Nothing else widens, so the
 * rule cannot spread past an expected optional context.
 */
static bool assignable(
    const Frontend *frontend,
    size_t expected,
    size_t actual
) {
    if (type_equal(frontend, expected, actual)) return true;
    if (frontend->types[expected].kind != TYPE_OPTIONAL) return false;
    return type_equal(frontend, frontend->types[expected].element, actual);
}

static void type_id(
    const Frontend *frontend,
    size_t index,
    char *output,
    size_t size
) {
    const Type *type = &frontend->types[index];
    char inner[IDENTITY_LIMIT / 2];
    switch (type->kind) {
        case TYPE_INT: snprintf(output, size, "builtin:Int"); return;
        case TYPE_BOOL: snprintf(output, size, "builtin:Bool"); return;
        case TYPE_TEXT: snprintf(output, size, "builtin:Text"); return;
        case TYPE_VOID: snprintf(output, size, "builtin:Void"); return;
        case TYPE_LIST:
            type_id(frontend, type->element, inner, sizeof(inner));
            snprintf(output, size, "List(%s)", inner);
            return;
        case TYPE_OPTIONAL:
            type_id(frontend, type->element, inner, sizeof(inner));
            snprintf(output, size, "Optional(%s)", inner);
            return;
    }
    snprintf(output, size, "builtin:Void");
}

/* Parses a primary type and at most one `?` suffix. */
static bool parse_type(Frontend *frontend, size_t *cursor, size_t *output) {
    size_t start = token_start(frontend, *cursor);
    size_t primary;

    if (token_is(frontend, *cursor, "?")) {
        report(frontend, "E2S138", start, token_end(frontend, *cursor),
            "'?' is a postfix type suffix; a prefix '?' is not Optional "
            "syntax");
        return false;
    }
    if (token_is(frontend, *cursor, "read") ||
        token_is(frontend, *cursor, "take")) {
        char mode[TEXT_LIMIT];
        size_t mode_end;
        memcpy(mode, frontend->tokens[*cursor].text, TEXT_LIMIT);
        mode_end = token_end(frontend, *cursor);
        *cursor += 1;
        /* An ownership mode followed by an optional is deliberately refused:
         * optional ownership modes are specified separately. */
        {
            size_t inner;
            size_t saved = frontend->diagnostic_count;
            bool saved_recovering = frontend->recovering;
            if (parse_type(frontend, cursor, &inner) &&
                frontend->types[inner].kind == TYPE_OPTIONAL) {
                frontend->diagnostic_count = saved;
                frontend->recovering = saved_recovering;
                report(frontend, "E2S138", start,
                    token_end(frontend, *cursor - 1),
                    "an optional ownership mode ('%s T?') is not specified "
                    "yet; write the mode over a non-optional type",
                    mode);
                return false;
            }
        }
        report(frontend, "E2S138", start, mode_end,
            "ownership modes are outside this Optional frontend slice");
        return false;
    }
    if (token_is(frontend, *cursor, "List")) {
        size_t element;
        *cursor += 1;
        if (!token_is(frontend, *cursor, "[")) {
            report(frontend, "E2S141", start, token_end(frontend, *cursor),
                "expected '[' after 'List'");
            return false;
        }
        *cursor += 1;
        if (!parse_type(frontend, cursor, &element)) return false;
        if (!token_is(frontend, *cursor, "]")) {
            report(frontend, "E2S141", start, token_end(frontend, *cursor),
                "expected ']' closing 'List['");
            return false;
        }
        *cursor += 1;
        primary = add_type(frontend, TYPE_LIST, element, start,
            token_end(frontend, *cursor - 1));
    } else if (token_has_kind(frontend, *cursor, TOKEN_IDENTIFIER)) {
        const char *name = frontend->tokens[*cursor].text;
        TypeKind kind;
        if (strcmp(name, "Int") == 0) {
            kind = TYPE_INT;
        } else if (strcmp(name, "Bool") == 0) {
            kind = TYPE_BOOL;
        } else if (strcmp(name, "Text") == 0) {
            kind = TYPE_TEXT;
        } else if (strcmp(name, "Void") == 0) {
            kind = TYPE_VOID;
        } else {
            report(frontend, "E2S141", start, token_end(frontend, *cursor),
                "unknown type '%s'", name);
            return false;
        }
        primary = add_type(frontend, kind, 0, start,
            token_end(frontend, *cursor));
        *cursor += 1;
    } else {
        report(frontend, "E2S141", start, token_end(frontend, *cursor),
            "expected a type");
        return false;
    }

    if (!token_is(frontend, *cursor, "?")) {
        *output = primary;
        return true;
    }
    {
        size_t suffix_start = token_start(frontend, *cursor);
        size_t suffix_end = token_end(frontend, *cursor);
        size_t optional;
        *cursor += 1;
        if (frontend->types[primary].kind == TYPE_VOID) {
            report(frontend, "E2S137", start, suffix_end,
                "'Void?' is not a type; Void has no absent value");
            return false;
        }
        if (token_is(frontend, *cursor, "?")) {
            report(frontend, "E2S137", start, token_end(frontend, *cursor),
                /* `?\?'` rather than `??'`: the latter is a trigraph. */
                "'T?\?' is not a type; nested optionals do not normalize and "
                "one suffix is the whole of this slice");
            return false;
        }
        optional = add_type(frontend, TYPE_OPTIONAL, primary, start,
            suffix_end);
        frontend->types[optional].suffix_start = suffix_start;
        frontend->types[optional].suffix_end = suffix_end;
        *output = optional;
        return true;
    }
}

static ptrdiff_t find_binding(
    const Frontend *frontend,
    size_t function_index,
    const char *name,
    bool *is_parameter
) {
    for (size_t index = 0; index < frontend->local_count; ++index) {
        const Binding *local = &frontend->locals[index];
        if (local->owner_function != function_index) continue;
        if (strcmp(local->name, name) != 0) continue;
        *is_parameter = false;
        return (ptrdiff_t)index;
    }
    for (size_t index = 0; index < frontend->parameter_count; ++index) {
        const Binding *parameter = &frontend->parameters[index];
        if (parameter->owner_function != function_index) continue;
        if (strcmp(parameter->name, name) != 0) continue;
        *is_parameter = true;
        return (ptrdiff_t)index;
    }
    return -1;
}

static ptrdiff_t find_function(const Frontend *frontend, const char *name) {
    for (size_t index = 0; index < frontend->function_count; ++index) {
        if (strcmp(frontend->functions[index].name, name) == 0) {
            return (ptrdiff_t)index;
        }
    }
    return -1;
}

/*
 * `expected` is the contextual type, or SIZE_MAX when there is none. `null`
 * is the only expression whose type comes entirely from that context.
 */
static bool parse_expression(
    Frontend *frontend,
    size_t function_index,
    size_t *cursor,
    size_t expected,
    size_t *output,
    bool *produced_null
) {
    size_t start = token_start(frontend, *cursor);
    *produced_null = false;

    if (token_is(frontend, *cursor, "null")) {
        size_t end = token_end(frontend, *cursor);
        *cursor += 1;
        *produced_null = true;
        if (expected == (size_t)-1) {
            report(frontend, "E2S134", start, end,
                "'null' has no standalone type; annotate the binding with an "
                "optional type so 'null' has an expected type");
            return false;
        }
        if (frontend->types[expected].kind != TYPE_OPTIONAL) {
            char wanted[IDENTITY_LIMIT];
            type_id(frontend, expected, wanted, sizeof(wanted));
            report(frontend, "E2S135", start, end,
                "'null' is not a %s; only an optional type has an absent "
                "value", wanted);
            return false;
        }
        *output = expected;
        return true;
    }
    if (token_has_kind(frontend, *cursor, TOKEN_INTEGER)) {
        *output = add_type(frontend, TYPE_INT, 0, start,
            token_end(frontend, *cursor));
        *cursor += 1;
    } else if (token_has_kind(frontend, *cursor, TOKEN_TEXT)) {
        *output = add_type(frontend, TYPE_TEXT, 0, start,
            token_end(frontend, *cursor));
        *cursor += 1;
    } else if (token_is(frontend, *cursor, "true") ||
        token_is(frontend, *cursor, "false")) {
        *output = add_type(frontend, TYPE_BOOL, 0, start,
            token_end(frontend, *cursor));
        *cursor += 1;
    } else if (token_has_kind(frontend, *cursor, TOKEN_IDENTIFIER)) {
        const char *name = frontend->tokens[*cursor].text;
        bool is_parameter = false;
        ptrdiff_t found;

        found = find_function(frontend, name);
        if (found >= 0 && token_is(frontend, *cursor + 1, "(")) {
            const Function *callee = &frontend->functions[(size_t)found];
            size_t slot = 0;
            Call *call;
            *cursor += 2;
            if (!token_is(frontend, *cursor, ")")) {
                for (;;) {
                    size_t argument;
                    bool argument_null = false;
                    size_t parameter_type = (size_t)-1;
                    if (slot < callee->parameter_count) {
                        parameter_type = frontend->parameters[
                            callee->parameter_start + slot].type;
                    }
                    if (!parse_expression(frontend, function_index, cursor,
                            parameter_type, &argument, &argument_null)) {
                        return false;
                    }
                    if (parameter_type != (size_t)-1 &&
                        !assignable(frontend, parameter_type, argument)) {
                        char wanted[IDENTITY_LIMIT];
                        char actual[IDENTITY_LIMIT];
                        type_id(frontend, parameter_type, wanted,
                            sizeof(wanted));
                        type_id(frontend, argument, actual, sizeof(actual));
                        report(frontend, "E2S136", start,
                            token_end(frontend, *cursor - 1),
                            "argument %zu of '%s' expects %s but %s was "
                            "written; an optional never satisfies a "
                            "non-optional",
                            slot + 1, callee->name, wanted, actual);
                        return false;
                    }
                    slot += 1;
                    if (token_is(frontend, *cursor, ",")) {
                        *cursor += 1;
                        continue;
                    }
                    break;
                }
            }
            if (!token_is(frontend, *cursor, ")")) {
                report(frontend, "E2S141", start,
                    token_end(frontend, *cursor),
                    "expected ')' closing the call");
                return false;
            }
            *cursor += 1;
            if (slot != callee->parameter_count) {
                report(frontend, "E2S141", start,
                    token_end(frontend, *cursor - 1),
                    "'%s' takes %zu argument(s) but %zu were written",
                    callee->name, callee->parameter_count, slot);
                return false;
            }
            if (frontend->call_count < CALL_LIMIT) {
                call = &frontend->calls[frontend->call_count];
                call->caller = function_index;
                call->callee = (size_t)found;
                call->start = start;
                call->end = token_end(frontend, *cursor - 1);
                frontend->call_count += 1;
            }
            *output = callee->result;
        } else {
            found = find_binding(frontend, function_index, name,
                &is_parameter);
            if (found < 0) {
                /* `nil` and `None` are ordinary identifiers, not absence. */
                report(frontend, "E2S139", start,
                    token_end(frontend, *cursor),
                    "unknown name '%s'; the only absent value is 'null'",
                    name);
                return false;
            }
            *output = is_parameter
                ? frontend->parameters[(size_t)found].type
                : frontend->locals[(size_t)found].type;
            *cursor += 1;
        }
    } else {
        report(frontend, "E2S141", start, token_end(frontend, *cursor),
            "expected an expression");
        return false;
    }

    /* One binary arithmetic operator, so an optional operand is refused where
     * a concrete one is required. */
    if (token_is(frontend, *cursor, "+") || token_is(frontend, *cursor, "-") ||
        token_is(frontend, *cursor, "*")) {
        size_t right;
        bool right_null = false;
        size_t operator_start = token_start(frontend, *cursor);
        char operator_text[TEXT_LIMIT];
        memcpy(operator_text, frontend->tokens[*cursor].text, TEXT_LIMIT);
        *cursor += 1;
        if (frontend->types[*output].kind == TYPE_OPTIONAL) {
            char actual[IDENTITY_LIMIT];
            type_id(frontend, *output, actual, sizeof(actual));
            report(frontend, "E2S136", start, operator_start,
                "'%s' requires a concrete operand but the left operand is %s; "
                "an optional must be resolved before it is used",
                operator_text, actual);
            return false;
        }
        if (!parse_expression(frontend, function_index, cursor, (size_t)-1,
                &right, &right_null)) {
            return false;
        }
        if (frontend->types[right].kind == TYPE_OPTIONAL) {
            char actual[IDENTITY_LIMIT];
            type_id(frontend, right, actual, sizeof(actual));
            report(frontend, "E2S136", operator_start,
                token_end(frontend, *cursor - 1),
                "'%s' requires a concrete operand but the right operand is "
                "%s; an optional must be resolved before it is used",
                operator_text, actual);
            return false;
        }
    }
    return true;
}

/* Skips to the next declaration boundary so one broken statement does not
 * hide the declarations after it. No semantic node is built from the skipped
 * tokens. */
static void recover_to_boundary(Frontend *frontend, size_t *cursor) {
    size_t depth = 0;
    while (*cursor < frontend->token_count) {
        if (token_is(frontend, *cursor, "{")) depth += 1;
        if (token_is(frontend, *cursor, "}")) {
            if (depth == 0) return;
            depth -= 1;
        }
        if (depth == 0 &&
            (token_is(frontend, *cursor, "let") ||
             token_is(frontend, *cursor, "return") ||
             token_is(frontend, *cursor, "fn"))) {
            return;
        }
        *cursor += 1;
    }
}

static bool parse_function_body(Frontend *frontend, size_t function_index) {
    Function *function = &frontend->functions[function_index];
    size_t cursor = function->body_start;
    size_t depth = 0;

    if (!token_is(frontend, cursor, "{")) return true;
    cursor += 1;
    while (cursor < frontend->token_count) {
        frontend->recovering = false;
        /* A nested block's `}` closes that block, not the function body, so
         * statements after an `if` are still reached. */
        if (token_is(frontend, cursor, "{")) {
            depth += 1;
            cursor += 1;
            continue;
        }
        if (token_is(frontend, cursor, "}")) {
            if (depth == 0) break;
            depth -= 1;
            cursor += 1;
            continue;
        }
        if (token_is(frontend, cursor, "let")) {
            Binding *local;
            size_t start = token_start(frontend, cursor);
            size_t annotation = (size_t)-1;
            size_t value;
            bool value_null = false;
            char name[TEXT_LIMIT];

            cursor += 1;
            if (!token_has_kind(frontend, cursor, TOKEN_IDENTIFIER)) {
                report(frontend, "E2S141", start,
                    token_end(frontend, cursor), "expected a binding name");
                recover_to_boundary(frontend, &cursor);
                continue;
            }
            memcpy(name, frontend->tokens[cursor].text, TEXT_LIMIT);
            cursor += 1;
            if (token_is(frontend, cursor, ":")) {
                cursor += 1;
                if (!parse_type(frontend, &cursor, &annotation)) {
                    recover_to_boundary(frontend, &cursor);
                    continue;
                }
            }
            if (!token_is(frontend, cursor, "=")) {
                report(frontend, "E2S141", start,
                    token_end(frontend, cursor), "expected '=' in 'let'");
                recover_to_boundary(frontend, &cursor);
                continue;
            }
            cursor += 1;
            if (!parse_expression(frontend, function_index, &cursor,
                    annotation, &value, &value_null)) {
                recover_to_boundary(frontend, &cursor);
                continue;
            }
            if (annotation != (size_t)-1 &&
                !assignable(frontend, annotation, value)) {
                char wanted[IDENTITY_LIMIT];
                char actual[IDENTITY_LIMIT];
                type_id(frontend, annotation, wanted, sizeof(wanted));
                type_id(frontend, value, actual, sizeof(actual));
                report(frontend, "E2S136", start,
                    token_end(frontend, cursor - 1),
                    "binding '%s' is %s but the initializer is %s; an "
                    "optional never satisfies a non-optional",
                    name, wanted, actual);
                recover_to_boundary(frontend, &cursor);
                continue;
            }
            if (frontend->local_count >= LOCAL_LIMIT) return false;
            local = &frontend->locals[frontend->local_count];
            memcpy(local->name, name, TEXT_LIMIT);
            local->owner_function = function_index;
            local->type = annotation == (size_t)-1 ? value : annotation;
            local->start = start;
            local->end = token_end(frontend, cursor - 1);
            frontend->local_count += 1;
            continue;
        }
        if (token_is(frontend, cursor, "return")) {
            Returned *returned;
            size_t start = token_start(frontend, cursor);
            size_t value;
            bool value_null = false;
            cursor += 1;
            if (!parse_expression(frontend, function_index, &cursor,
                    function->result, &value, &value_null)) {
                recover_to_boundary(frontend, &cursor);
                continue;
            }
            if (!assignable(frontend, function->result, value)) {
                char wanted[IDENTITY_LIMIT];
                char actual[IDENTITY_LIMIT];
                type_id(frontend, function->result, wanted, sizeof(wanted));
                type_id(frontend, value, actual, sizeof(actual));
                report(frontend, "E2S136", start,
                    token_end(frontend, cursor - 1),
                    "'%s' returns %s but this returns %s; an optional never "
                    "satisfies a non-optional",
                    function->name, wanted, actual);
                recover_to_boundary(frontend, &cursor);
                continue;
            }
            if (frontend->return_count >= RETURN_LIMIT) return false;
            returned = &frontend->returns[frontend->return_count];
            returned->owner_function = function_index;
            /* A concrete T under an expected Optional(T) is injected, so the
             * return produces the result type while the written type is kept
             * beside it. */
            returned->type = function->result;
            returned->written = value;
            returned->is_null = value_null;
            returned->start = start;
            returned->end = token_end(frontend, cursor - 1);
            frontend->return_count += 1;
            continue;
        }
        if (token_is(frontend, cursor, "if")) {
            size_t start = token_start(frontend, cursor);
            cursor += 1;
            if (token_is(frontend, cursor, "null")) {
                report(frontend, "E2S140", start,
                    token_end(frontend, cursor),
                    "'null' is not a condition; this language has no "
                    "truthiness and 'null' is not a Bool");
                recover_to_boundary(frontend, &cursor);
                continue;
            }
            {
                size_t condition;
                bool condition_null = false;
                size_t expected = add_type(frontend, TYPE_BOOL, 0, start,
                    start);
                if (!parse_expression(frontend, function_index, &cursor,
                        expected, &condition, &condition_null)) {
                    recover_to_boundary(frontend, &cursor);
                    continue;
                }
                if (frontend->types[condition].kind != TYPE_BOOL) {
                    char actual[IDENTITY_LIMIT];
                    type_id(frontend, condition, actual, sizeof(actual));
                    report(frontend, "E2S140", start,
                        token_end(frontend, cursor - 1),
                        "a condition must be a Bool but this is %s; there is "
                        "no truthiness", actual);
                    recover_to_boundary(frontend, &cursor);
                    continue;
                }
            }
            continue;
        }
        cursor += 1;
    }
    frontend->recovering = false;
    return true;
}

static bool collect_functions(Frontend *frontend) {
    size_t cursor = 0;
    while (cursor < frontend->token_count) {
        Function *function;
        char name[TEXT_LIMIT];
        size_t index = frontend->function_count;
        size_t start = token_start(frontend, cursor);

        frontend->recovering = false;
        if (!token_is(frontend, cursor, "fn")) {
            report(frontend, "E2S141", start, token_end(frontend, cursor),
                "expected a function declaration");
            return false;
        }
        if (frontend->function_count >= FUNCTION_LIMIT) return false;
        cursor += 1;
        if (!token_has_kind(frontend, cursor, TOKEN_IDENTIFIER)) {
            report(frontend, "E2S141", start, token_end(frontend, cursor),
                "expected a function name");
            return false;
        }
        memcpy(name, frontend->tokens[cursor].text, TEXT_LIMIT);
        cursor += 1;
        function = &frontend->functions[index];
        memset(function, 0, sizeof(*function));
        memcpy(function->name, name, TEXT_LIMIT);
        function->start = start;
        function->parameter_start = frontend->parameter_count;
        frontend->function_count += 1;

        if (!token_is(frontend, cursor, "(")) {
            report(frontend, "E2S141", start, token_end(frontend, cursor),
                "expected '(' after the function name");
            return false;
        }
        cursor += 1;
        if (!token_is(frontend, cursor, ")")) {
            for (;;) {
                Binding *parameter;
                char parameter_name[TEXT_LIMIT];
                size_t parameter_start = token_start(frontend, cursor);
                size_t type;
                if (!token_has_kind(frontend, cursor, TOKEN_IDENTIFIER)) {
                    report(frontend, "E2S141", parameter_start,
                        token_end(frontend, cursor),
                        "expected a parameter name");
                    return false;
                }
                memcpy(parameter_name, frontend->tokens[cursor].text,
                    TEXT_LIMIT);
                cursor += 1;
                if (!token_is(frontend, cursor, ":")) {
                    report(frontend, "E2S141", parameter_start,
                        token_end(frontend, cursor),
                        "expected ':' after the parameter name");
                    return false;
                }
                cursor += 1;
                if (!parse_type(frontend, &cursor, &type)) return false;
                if (frontend->parameter_count >= PARAMETER_LIMIT) return false;
                parameter = &frontend->parameters[frontend->parameter_count];
                memcpy(parameter->name, parameter_name, TEXT_LIMIT);
                parameter->owner_function = index;
                parameter->type = type;
                parameter->start = parameter_start;
                parameter->end = token_end(frontend, cursor - 1);
                frontend->parameter_count += 1;
                function->parameter_count += 1;
                if (token_is(frontend, cursor, ",")) {
                    cursor += 1;
                    continue;
                }
                break;
            }
        }
        if (!token_is(frontend, cursor, ")")) {
            report(frontend, "E2S141", start, token_end(frontend, cursor),
                "expected ')' closing the parameter list");
            return false;
        }
        cursor += 1;
        if (token_has_kind(frontend, cursor, TOKEN_ARROW)) {
            cursor += 1;
            if (!parse_type(frontend, &cursor, &function->result)) return false;
        } else {
            function->result = add_type(frontend, TYPE_VOID, 0,
                token_start(frontend, cursor), token_start(frontend, cursor));
        }
        if (!token_is(frontend, cursor, "{")) {
            report(frontend, "E2S141", start, token_end(frontend, cursor),
                "expected a function body");
            return false;
        }
        function->body_start = cursor;
        {
            size_t depth = 0;
            do {
                if (cursor >= frontend->token_count) {
                    report(frontend, "E2S141", start, start,
                        "function '%s' body is not closed", function->name);
                    return false;
                }
                if (token_is(frontend, cursor, "{")) depth += 1;
                if (token_is(frontend, cursor, "}")) depth -= 1;
                cursor += 1;
            } while (depth > 0);
        }
        function->body_end = cursor;
        function->end = token_end(frontend, cursor - 1);
    }
    return true;
}

static char *read_source(const char *path, size_t *length_output) {
    FILE *file = fopen(path, "rb");
    char *buffer;
    size_t length;

    if (file == NULL) return NULL;
    buffer = malloc(SOURCE_LIMIT + 1u);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }
    length = fread(buffer, 1, SOURCE_LIMIT, file);
    if (ferror(file) != 0) {
        free(buffer);
        fclose(file);
        return NULL;
    }
    fclose(file);
    buffer[length] = '\0';
    *length_output = length;
    return buffer;
}

static bool write_ir(const Frontend *frontend, const char *path) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    fprintf(file, "kofun-optional-ir/v1\n");

    for (size_t index = 0; index < frontend->function_count; ++index) {
        const Function *function = &frontend->functions[index];
        char result[IDENTITY_LIMIT];
        type_id(frontend, function->result, result, sizeof(result));
        fprintf(
            file,
            "function|function-id=function:%s|name=%s|parameters=%zu"
            "|result=%s|span=%zu..%zu\n",
            function->name,
            function->name,
            function->parameter_count,
            result,
            function->start,
            function->end
        );
    }
    for (size_t index = 0; index < frontend->parameter_count; ++index) {
        const Binding *parameter = &frontend->parameters[index];
        const Type *type = &frontend->types[parameter->type];
        char identity[IDENTITY_LIMIT];
        type_id(frontend, parameter->type, identity, sizeof(identity));
        fprintf(
            file,
            "parameter|owner=function:%s|name=%s|type=%s|type-span=%zu..%zu"
            "|suffix-span=%zu..%zu|span=%zu..%zu\n",
            frontend->functions[parameter->owner_function].name,
            parameter->name,
            identity,
            type->start,
            type->end,
            type->suffix_start,
            type->suffix_end,
            parameter->start,
            parameter->end
        );
    }
    for (size_t index = 0; index < frontend->local_count; ++index) {
        const Binding *local = &frontend->locals[index];
        const Type *type = &frontend->types[local->type];
        char identity[IDENTITY_LIMIT];
        type_id(frontend, local->type, identity, sizeof(identity));
        fprintf(
            file,
            "local|owner=function:%s|name=%s|type=%s|type-span=%zu..%zu"
            "|suffix-span=%zu..%zu|span=%zu..%zu\n",
            frontend->functions[local->owner_function].name,
            local->name,
            identity,
            type->start,
            type->end,
            type->suffix_start,
            type->suffix_end,
            local->start,
            local->end
        );
    }
    for (size_t index = 0; index < frontend->return_count; ++index) {
        const Returned *returned = &frontend->returns[index];
        char identity[IDENTITY_LIMIT];
        char written[IDENTITY_LIMIT];
        bool injected;
        type_id(frontend, returned->type, identity, sizeof(identity));
        type_id(frontend, returned->written, written, sizeof(written));
        injected = !returned->is_null &&
            !type_equal(frontend, returned->type, returned->written);
        fprintf(
            file,
            "return|owner=function:%s|type=%s|written=%s|injected=%s|null=%s"
            "|span=%zu..%zu\n",
            frontend->functions[returned->owner_function].name,
            identity,
            written,
            injected ? "yes" : "no",
            returned->is_null ? "yes" : "no",
            returned->start,
            returned->end
        );
    }
    for (size_t index = 0; index < frontend->call_count; ++index) {
        const Call *call = &frontend->calls[index];
        char result[IDENTITY_LIMIT];
        type_id(frontend, frontend->functions[call->callee].result, result,
            sizeof(result));
        fprintf(
            file,
            "call|caller=function:%s|callee=function:%s|result=%s"
            "|use-span=%zu..%zu\n",
            frontend->functions[call->caller].name,
            frontend->functions[call->callee].name,
            result,
            call->start,
            call->end
        );
    }
    if (fclose(file) != 0) return false;
    return true;
}

static bool write_tokens(const Frontend *frontend, const char *path) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    fprintf(file, "kofun-optional-tokens/v1\n");
    for (size_t index = 0; index < frontend->token_count; ++index) {
        const Token *token = &frontend->tokens[index];
        const char *kind = "punctuation";
        switch (token->kind) {
            case TOKEN_IDENTIFIER:
                kind = strcmp(token->text, "null") == 0 ? "null" : "identifier";
                break;
            case TOKEN_INTEGER: kind = "integer"; break;
            case TOKEN_TEXT: kind = "text"; break;
            case TOKEN_ARROW: kind = "arrow"; break;
            case TOKEN_PUNCTUATION: kind = "punctuation"; break;
        }
        fprintf(file, "%s|%zu..%zu\n", kind, token->start, token->end);
    }
    if (fclose(file) != 0) return false;
    return true;
}

int main(int argc, char **argv) {
    Frontend *frontend;
    char *source;
    size_t length = 0;
    int status = 0;

    if (argc != 4) {
        fprintf(stderr, "usage: kofun-optional-frontend SOURCE IR TOKENS\n");
        return 2;
    }
    source = read_source(argv[1], &length);
    if (source == NULL) {
        fprintf(stderr, "kofun-optional-frontend: cannot read %s\n", argv[1]);
        return 2;
    }
    frontend = calloc(1, sizeof(*frontend));
    if (frontend == NULL) {
        free(source);
        fprintf(stderr, "kofun-optional-frontend: out of memory\n");
        return 2;
    }

    if (tokenize(frontend, source, length) && collect_functions(frontend)) {
        for (size_t index = 0; index < frontend->function_count; ++index) {
            if (!parse_function_body(frontend, index)) break;
        }
    }
    if (failed(frontend)) {
        for (size_t index = 0; index < frontend->diagnostic_count; ++index) {
            printf("%s\n", frontend->diagnostics[index].text);
        }
        status = 1;
    } else if (!write_ir(frontend, argv[2]) ||
        !write_tokens(frontend, argv[3])) {
        fprintf(stderr, "kofun-optional-frontend: cannot write output\n");
        status = 2;
    }

    free(frontend);
    free(source);
    return status;
}
