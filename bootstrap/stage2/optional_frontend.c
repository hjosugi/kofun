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
 *
 * Flow-sensitive narrowing (#312) is layered on top of that typed node and
 * nothing else. It is frontend-only: a refinement is a fact about one edge of
 * the control-flow graph, never a change to a binding's declared type and
 * never something that escapes its function.
 *
 * Recognized conditions, and only these four shapes:
 *
 *   - `x != null` and `null != x`: `x` is `T` on the true edge;
 *   - `x == null` and `null == x`: `x` is `T` on the false edge;
 *   - either of those as the condition of an `if` whose taken branch
 *     definitely returns, which carries the opposite edge's refinement past
 *     the guard.
 *
 * `x` must be a direct local binding (a parameter or a `let`) whose declared
 * type is `Optional(T)`. There is no operator overloading and no user-defined
 * equality in this slice, so "the comparison is not overloaded" holds by
 * construction. Every other condition is still a legal `Bool` condition; it
 * simply refines nothing, and a use of `x` as `T` under it stays an error.
 *
 * Invalidation, applied conservatively — when in doubt the refinement is
 * discarded rather than kept:
 *
 *   - each branch gets its own refinement environment, so a fact never leaks
 *     to a sibling branch;
 *   - joins merge by intersection;
 *   - assigning to `x` discards its refinement, after the replacement has been
 *     checked against the declared `Optional(T)`;
 *   - passing a mutable `x` to a call discards its refinement after the call.
 *     Ownership modes are refused by this frontend, so every call here has
 *     unknown effects and the conservative rule is the only rule;
 *   - an immutable `let x` may keep its refinement across a call, because it
 *     can be neither reassigned nor mutably aliased in this slice;
 *   - a loop backedge discards the refinement of every mutable binding the
 *     body mentions, so nothing loop-carried is assumed on the next iteration.
 *
 * Out of scope, and refused rather than guessed: compound conditions, property
 * and index paths, aliases, captured variables, interprocedural summaries,
 * `match`, safe navigation, truthiness, user-defined equality, general union
 * narrowing, and runtime representation. A refinement through an alias is not
 * an alias analysis — `let y = x` refines `y` alone, and `x` stays optional.
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
#define BINDING_SLOT_LIMIT (PARAMETER_LIMIT + LOCAL_LIMIT)
#define ENVIRONMENT_LIMIT 64u
#define BLOCK_DEPTH_LIMIT 16u
#define REFINEMENT_LIMIT 256u
#define CALL_ARGUMENT_LIMIT 16u

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
    /* `let mut`. Parameters are by-value and never mutable here, so only a
     * mutable local can lose a refinement to a call. */
    bool is_mutable;
} Binding;

/*
 * A refinement environment is one bit per binding slot: set means "this
 * binding is known non-null on this edge". Slots are stable for the whole
 * translation unit, so a fact is keyed by binding identity rather than by
 * name, and an environment can be copied, intersected, and thrown away
 * without touching any declared type.
 */
typedef struct {
    unsigned char narrowed[BINDING_SLOT_LIMIT];
} Environment;

/* A resolved reference to a direct local binding. */
typedef struct {
    bool found;
    bool is_parameter;
    size_t index;    /* into parameters[] or locals[] */
    size_t slot;     /* into Environment.narrowed */
    size_t declared; /* the declared type, never the refined one */
    bool is_mutable;
} BindingRef;

/* An established refinement fact, as it reaches typed IR. */
typedef struct {
    size_t owner_function;
    bool is_parameter;
    size_t index;
    size_t declared;
    size_t narrowed;
    bool on_true;
    size_t start;
    size_t end;
} Refinement;

/* Why a refinement stopped holding. Recorded, not inferred. */
typedef struct {
    size_t owner_function;
    bool is_parameter;
    size_t index;
    const char *reason;
    size_t start;
    size_t end;
} Invalidation;

/* A use of `x` that the frontend typed as `T` rather than `Optional(T)`. */
typedef struct {
    size_t owner_function;
    bool is_parameter;
    size_t index;
    size_t declared;
    size_t narrowed;
    size_t start;
    size_t end;
} NarrowedUse;

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
    Refinement refinements[REFINEMENT_LIMIT];
    size_t refinement_count;
    Invalidation invalidations[REFINEMENT_LIMIT];
    size_t invalidation_count;
    NarrowedUse narrowed_uses[REFINEMENT_LIMIT];
    size_t narrowed_use_count;
    /* Environments are drawn from a bounded pool rather than the C stack, so
     * deep nesting runs out of pool instead of out of stack. */
    Environment environments[ENVIRONMENT_LIMIT];
    size_t environment_top;
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
        /* `==` and `!=` are single tokens, so `=` stays assignment and a lone
         * `!` is still not syntax. */
        if ((source[cursor] == '=' || source[cursor] == '!') &&
            cursor + 1 < length && source[cursor + 1] == '=') {
            cursor += 2;
            if (!add_token(
                    frontend, TOKEN_PUNCTUATION, source, start, cursor)) {
                return false;
            }
            continue;
        }
        if (strchr("[](),:{}=?+-*.", source[cursor]) != NULL) {
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
    if (frontend->type_count >= TYPE_LIMIT) {
        report(frontend, "E2S141", start, end,
            "source exceeds the Optional frontend type limits");
        return 0;
    }
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

/*
 * Resolves a name to a direct local binding. A local shadows a parameter of
 * the same name, and the most recent local wins, so the reference names one
 * declaration rather than a set of them.
 */
static BindingRef resolve_binding(
    const Frontend *frontend,
    size_t function_index,
    const char *name
) {
    BindingRef reference;
    memset(&reference, 0, sizeof(reference));
    for (size_t step = 0; step < frontend->local_count; ++step) {
        size_t index = frontend->local_count - 1u - step;
        const Binding *local = &frontend->locals[index];
        if (local->owner_function != function_index) continue;
        if (strcmp(local->name, name) != 0) continue;
        reference.found = true;
        reference.is_parameter = false;
        reference.index = index;
        reference.slot = PARAMETER_LIMIT + index;
        reference.declared = local->type;
        reference.is_mutable = local->is_mutable;
        return reference;
    }
    for (size_t index = 0; index < frontend->parameter_count; ++index) {
        const Binding *parameter = &frontend->parameters[index];
        if (parameter->owner_function != function_index) continue;
        if (strcmp(parameter->name, name) != 0) continue;
        reference.found = true;
        reference.is_parameter = true;
        reference.index = index;
        reference.slot = index;
        reference.declared = parameter->type;
        reference.is_mutable = false;
        return reference;
    }
    return reference;
}

static Environment *environment_alloc(Frontend *frontend) {
    Environment *environment;
    if (frontend->environment_top >= ENVIRONMENT_LIMIT) return NULL;
    environment = &frontend->environments[frontend->environment_top];
    frontend->environment_top += 1;
    memset(environment, 0, sizeof(*environment));
    return environment;
}

static Environment *environment_copy(Frontend *frontend, const Environment *of) {
    Environment *environment = environment_alloc(frontend);
    if (environment == NULL) return NULL;
    memcpy(environment, of, sizeof(*environment));
    return environment;
}

/* A join keeps only what both edges agree on. */
static void environment_intersect(Environment *into, const Environment *with) {
    for (size_t slot = 0; slot < BINDING_SLOT_LIMIT; ++slot) {
        if (with->narrowed[slot] == 0) into->narrowed[slot] = 0;
    }
}

static bool is_narrowed(const Environment *environment, size_t slot) {
    return slot < BINDING_SLOT_LIMIT && environment->narrowed[slot] != 0;
}

static void record_invalidation(
    Frontend *frontend,
    size_t function_index,
    const BindingRef *reference,
    const char *reason,
    size_t start,
    size_t end
) {
    Invalidation *invalidation;
    if (frontend->invalidation_count >= REFINEMENT_LIMIT) return;
    invalidation = &frontend->invalidations[frontend->invalidation_count];
    invalidation->owner_function = function_index;
    invalidation->is_parameter = reference->is_parameter;
    invalidation->index = reference->index;
    invalidation->reason = reason;
    invalidation->start = start;
    invalidation->end = end;
    frontend->invalidation_count += 1;
}

/*
 * Discards a refinement and says why. Recording the discard rather than
 * silently clearing the bit is what makes every invalidation rule checkable
 * from the typed IR instead of only from the absence of an error.
 */
static void discard_refinement(
    Frontend *frontend,
    size_t function_index,
    Environment *environment,
    const BindingRef *reference,
    const char *reason,
    size_t start,
    size_t end
) {
    if (!reference->found || reference->slot >= BINDING_SLOT_LIMIT) return;
    if (environment->narrowed[reference->slot] == 0) return;
    environment->narrowed[reference->slot] = 0;
    record_invalidation(frontend, function_index, reference, reason, start,
        end);
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
/*
 * `expression_type` is the type a use of `reference` has right here: the
 * refined `T` when the fact reaches this point, and the declared
 * `Optional(T)` otherwise. The declared type is never rewritten.
 */
static size_t narrowed_type_of(
    Frontend *frontend,
    size_t function_index,
    const Environment *environment,
    const BindingRef *reference,
    size_t start,
    size_t end
) {
    NarrowedUse *use;
    size_t narrowed;
    if (!is_narrowed(environment, reference->slot)) return reference->declared;
    if (frontend->types[reference->declared].kind != TYPE_OPTIONAL) {
        return reference->declared;
    }
    narrowed = frontend->types[reference->declared].element;
    if (frontend->narrowed_use_count < REFINEMENT_LIMIT) {
        use = &frontend->narrowed_uses[frontend->narrowed_use_count];
        use->owner_function = function_index;
        use->is_parameter = reference->is_parameter;
        use->index = reference->index;
        use->declared = reference->declared;
        use->narrowed = narrowed;
        use->start = start;
        use->end = end;
        frontend->narrowed_use_count += 1;
    }
    return narrowed;
}

static bool parse_expression(
    Frontend *frontend,
    size_t function_index,
    size_t *cursor,
    size_t expected,
    Environment *environment,
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
        ptrdiff_t found;

        found = find_function(frontend, name);
        if (found >= 0 && token_is(frontend, *cursor + 1, "(")) {
            const Function *callee = &frontend->functions[(size_t)found];
            size_t slot = 0;
            /* Mutable bindings handed to the callee. This frontend refuses
             * ownership modes, so every call has unknown effects and every
             * mutable argument loses its refinement once the call returns.
             * Immutable bindings cannot be reassigned or mutably aliased in
             * this slice, so they keep theirs. */
            BindingRef exposed[CALL_ARGUMENT_LIMIT];
            size_t exposed_count = 0;
            size_t call_start = start;
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
                    if (token_has_kind(frontend, *cursor, TOKEN_IDENTIFIER) &&
                        (token_is(frontend, *cursor + 1, ",") ||
                         token_is(frontend, *cursor + 1, ")"))) {
                        BindingRef passed = resolve_binding(frontend,
                            function_index, frontend->tokens[*cursor].text);
                        if (passed.found && passed.is_mutable &&
                            exposed_count < CALL_ARGUMENT_LIMIT) {
                            exposed[exposed_count] = passed;
                            exposed_count += 1;
                        }
                    }
                    if (!parse_expression(frontend, function_index, cursor,
                            parameter_type, environment, &argument,
                            &argument_null)) {
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
                call->start = call_start;
                call->end = token_end(frontend, *cursor - 1);
                frontend->call_count += 1;
            }
            for (size_t index = 0; index < exposed_count; ++index) {
                discard_refinement(frontend, function_index, environment,
                    &exposed[index], "call", call_start,
                    token_end(frontend, *cursor - 1));
            }
            *output = callee->result;
        } else {
            BindingRef reference = resolve_binding(frontend, function_index,
                name);
            size_t name_end = token_end(frontend, *cursor);
            if (!reference.found) {
                /* `nil` and `None` are ordinary identifiers, not absence. */
                report(frontend, "E2S139", start, name_end,
                    "unknown name '%s'; the only absent value is 'null'",
                    name);
                return false;
            }
            *cursor += 1;
            /* A path is not a direct binding. Refusing it here is what keeps
             * `p.value != null` and `xs[0] != null` from being narrowed
             * optimistically somewhere else. */
            if (token_is(frontend, *cursor, ".") ||
                token_is(frontend, *cursor, "[")) {
                report(frontend, "E2S142", start,
                    token_end(frontend, *cursor),
                    "'%s' is followed by a property or index path; narrowing "
                    "and this Optional slice recognize a direct local binding "
                    "only", name);
                return false;
            }
            *output = narrowed_type_of(frontend, function_index, environment,
                &reference, start, name_end);
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
                environment, &right, &right_null)) {
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
             token_is(frontend, *cursor, "fn") ||
             token_is(frontend, *cursor, "if") ||
             token_is(frontend, *cursor, "while"))) {
            return;
        }
        *cursor += 1;
    }
}

/* The one narrowing fact a recognized condition produces. */
typedef struct {
    bool present;
    BindingRef subject;
    bool on_true; /* the refinement holds on the true edge */
    size_t start;
    size_t end;
} NarrowCondition;

static void record_refinement(
    Frontend *frontend,
    size_t function_index,
    const NarrowCondition *narrow
) {
    Refinement *fact;
    if (frontend->refinement_count >= REFINEMENT_LIMIT) return;
    fact = &frontend->refinements[frontend->refinement_count];
    fact->owner_function = function_index;
    fact->is_parameter = narrow->subject.is_parameter;
    fact->index = narrow->subject.index;
    fact->declared = narrow->subject.declared;
    fact->narrowed = frontend->types[narrow->subject.declared].element;
    fact->on_true = narrow->on_true;
    fact->start = narrow->start;
    fact->end = narrow->end;
    frontend->refinement_count += 1;
}

static bool is_equality(const Frontend *frontend, size_t index) {
    return token_is(frontend, index, "==") || token_is(frontend, index, "!=");
}

/*
 * The whole recognized set, matched syntactically before anything is typed:
 * `x == null`, `x != null`, `null == x`, `null != x`, and nothing else. The
 * comparison must be the entire condition — the block's `{` has to follow it
 * — so a longer expression that merely contains one of these shapes is not
 * mistaken for it.
 */
static bool recognize_null_comparison(
    const Frontend *frontend,
    size_t cursor,
    size_t *name_index,
    size_t *operator_index,
    size_t *null_index
) {
    if (token_has_kind(frontend, cursor, TOKEN_IDENTIFIER) &&
        !token_is(frontend, cursor, "null") &&
        is_equality(frontend, cursor + 1) &&
        token_is(frontend, cursor + 2, "null") &&
        token_is(frontend, cursor + 3, "{")) {
        *name_index = cursor;
        *operator_index = cursor + 1;
        *null_index = cursor + 2;
        return true;
    }
    if (token_is(frontend, cursor, "null") &&
        is_equality(frontend, cursor + 1) &&
        token_has_kind(frontend, cursor + 2, TOKEN_IDENTIFIER) &&
        !token_is(frontend, cursor + 2, "null") &&
        token_is(frontend, cursor + 3, "{")) {
        *name_index = cursor + 2;
        *operator_index = cursor + 1;
        *null_index = cursor;
        return true;
    }
    return false;
}

/*
 * Parses a condition and, when it is one of the recognized shapes over a
 * direct local `Optional(T)` binding, reports the refinement it establishes.
 * Every other condition is still typed; it simply refines nothing.
 */
static bool parse_condition(
    Frontend *frontend,
    size_t function_index,
    size_t *cursor,
    Environment *environment,
    size_t *output,
    NarrowCondition *narrow
) {
    size_t start = token_start(frontend, *cursor);
    size_t name_index;
    size_t operator_index;
    size_t null_index;
    size_t left;
    bool left_null = false;

    narrow->present = false;

    if (recognize_null_comparison(frontend, *cursor, &name_index,
            &operator_index, &null_index)) {
        BindingRef subject = resolve_binding(frontend, function_index,
            frontend->tokens[name_index].text);
        size_t end = token_end(frontend, *cursor + 2);
        if (!subject.found) {
            report(frontend, "E2S139", token_start(frontend, name_index),
                token_end(frontend, name_index),
                "unknown name '%s'; the only absent value is 'null'",
                frontend->tokens[name_index].text);
            return false;
        }
        if (frontend->types[subject.declared].kind != TYPE_OPTIONAL) {
            char wanted[IDENTITY_LIMIT];
            type_id(frontend, subject.declared, wanted, sizeof(wanted));
            report(frontend, "E2S135", token_start(frontend, null_index),
                token_end(frontend, null_index),
                "'null' is not a %s; only an optional type has an absent "
                "value", wanted);
            return false;
        }
        narrow->present = true;
        narrow->subject = subject;
        narrow->on_true = token_is(frontend, operator_index, "!=");
        narrow->start = start;
        narrow->end = end;
        *cursor += 3;
        *output = add_type(frontend, TYPE_BOOL, 0, start, end);
        return true;
    }

    /* `null` outside the recognized shapes is refused rather than guessed at:
     * an unsupported narrowing shape must stay an error. */
    if (token_is(frontend, *cursor, "null")) {
        report(frontend, "E2S142", start, token_end(frontend, *cursor + 2),
            "'null' appears in an unrecognized condition; narrowing "
            "recognizes only 'x == null', 'null == x', 'x != null', and "
            "'null != x' over a direct local binding");
        return false;
    }
    if (!parse_expression(frontend, function_index, cursor, (size_t)-1,
            environment, &left, &left_null)) {
        return false;
    }
    if (is_equality(frontend, *cursor)) {
        size_t right;
        bool right_null = false;
        if (token_is(frontend, *cursor + 1, "null")) {
            report(frontend, "E2S142", start,
                token_end(frontend, *cursor + 1),
                "'null' appears in an unrecognized condition; narrowing "
                "recognizes only 'x == null', 'null == x', 'x != null', and "
                "'null != x' over a direct local binding");
            return false;
        }
        *cursor += 1;
        if (!parse_expression(frontend, function_index, cursor, (size_t)-1,
                environment, &right, &right_null)) {
            return false;
        }
        /* A comparison is not a narrowing site unless it is one of the four
         * shapes above, so this one only has to type-check. */
        if (!assignable(frontend, left, right) &&
            !assignable(frontend, right, left)) {
            char wanted[IDENTITY_LIMIT];
            char actual[IDENTITY_LIMIT];
            type_id(frontend, left, wanted, sizeof(wanted));
            type_id(frontend, right, actual, sizeof(actual));
            report(frontend, "E2S136", start,
                token_end(frontend, *cursor - 1),
                "a comparison needs comparable operands but this compares %s "
                "with %s", wanted, actual);
            return false;
        }
        *output = add_type(frontend, TYPE_BOOL, 0, start,
            token_end(frontend, *cursor - 1));
        return true;
    }
    *output = left;
    return true;
}

/*
 * The loop backedge. A mutable binding the body mentions may hold a different
 * value on the next iteration, so its refinement is discarded before the body
 * is typed and nothing loop-carried is assumed. An immutable binding cannot
 * change, so its refinement is invariant and survives.
 */
static void discard_loop_variant(
    Frontend *frontend,
    size_t function_index,
    Environment *environment,
    size_t body_open,
    size_t body_close
) {
    for (size_t index = body_open; index < body_close &&
        index < frontend->token_count; ++index) {
        BindingRef mentioned;
        if (!token_has_kind(frontend, index, TOKEN_IDENTIFIER)) continue;
        mentioned = resolve_binding(frontend, function_index,
            frontend->tokens[index].text);
        if (!mentioned.found || !mentioned.is_mutable) continue;
        discard_refinement(frontend, function_index, environment, &mentioned,
            "loop-backedge", token_start(frontend, body_open),
            token_end(frontend, body_close - 1));
    }
}

static bool parse_block(
    Frontend *frontend,
    size_t function_index,
    size_t *cursor,
    Environment *environment,
    size_t depth,
    bool *terminates
);

/*
 * One statement. `*terminates` reports whether control definitely leaves the
 * function here, which is what turns `if x == null { return ... }` into a
 * guard that carries the opposite edge's refinement past it.
 */
static bool parse_statement(
    Frontend *frontend,
    size_t function_index,
    size_t *cursor,
    Environment *environment,
    size_t depth,
    bool *terminates
) {
    Function *function = &frontend->functions[function_index];
    size_t start = token_start(frontend, *cursor);

    *terminates = false;

    if (token_is(frontend, *cursor, "{")) {
        return parse_block(frontend, function_index, cursor, environment,
            depth + 1, terminates);
    }

    if (token_is(frontend, *cursor, "let")) {
        Binding *local;
        size_t annotation = (size_t)-1;
        size_t value;
        bool value_null = false;
        bool is_mutable = false;
        char name[TEXT_LIMIT];

        *cursor += 1;
        if (token_is(frontend, *cursor, "mut")) {
            is_mutable = true;
            *cursor += 1;
        }
        if (!token_has_kind(frontend, *cursor, TOKEN_IDENTIFIER)) {
            report(frontend, "E2S141", start, token_end(frontend, *cursor),
                "expected a binding name");
            recover_to_boundary(frontend, cursor);
            return true;
        }
        memcpy(name, frontend->tokens[*cursor].text, TEXT_LIMIT);
        *cursor += 1;
        if (token_is(frontend, *cursor, ":")) {
            *cursor += 1;
            if (!parse_type(frontend, cursor, &annotation)) {
                recover_to_boundary(frontend, cursor);
                return true;
            }
        }
        if (!token_is(frontend, *cursor, "=")) {
            report(frontend, "E2S141", start, token_end(frontend, *cursor),
                "expected '=' in 'let'");
            recover_to_boundary(frontend, cursor);
            return true;
        }
        *cursor += 1;
        if (!parse_expression(frontend, function_index, cursor, annotation,
                environment, &value, &value_null)) {
            recover_to_boundary(frontend, cursor);
            return true;
        }
        if (annotation != (size_t)-1 &&
            !assignable(frontend, annotation, value)) {
            char wanted[IDENTITY_LIMIT];
            char actual[IDENTITY_LIMIT];
            type_id(frontend, annotation, wanted, sizeof(wanted));
            type_id(frontend, value, actual, sizeof(actual));
            report(frontend, "E2S136", start,
                token_end(frontend, *cursor - 1),
                "binding '%s' is %s but the initializer is %s; an "
                "optional never satisfies a non-optional",
                name, wanted, actual);
            recover_to_boundary(frontend, cursor);
            return true;
        }
        if (frontend->local_count >= LOCAL_LIMIT) {
            report(frontend, "E2S141", start, token_end(frontend, *cursor - 1),
                "source exceeds the Optional frontend limits");
            return false;
        }
        local = &frontend->locals[frontend->local_count];
        memcpy(local->name, name, TEXT_LIMIT);
        local->owner_function = function_index;
        local->type = annotation == (size_t)-1 ? value : annotation;
        local->start = start;
        local->end = token_end(frontend, *cursor - 1);
        local->is_mutable = is_mutable;
        /* A fresh binding carries no refinement: narrowing is established by
         * a recognized condition, never by an initializer. */
        environment->narrowed[PARAMETER_LIMIT + frontend->local_count] = 0;
        frontend->local_count += 1;
        return true;
    }

    if (token_is(frontend, *cursor, "return")) {
        Returned *returned;
        size_t value;
        bool value_null = false;
        *cursor += 1;
        *terminates = true;
        if (!parse_expression(frontend, function_index, cursor,
                function->result, environment, &value, &value_null)) {
            recover_to_boundary(frontend, cursor);
            return true;
        }
        if (!assignable(frontend, function->result, value)) {
            char wanted[IDENTITY_LIMIT];
            char actual[IDENTITY_LIMIT];
            type_id(frontend, function->result, wanted, sizeof(wanted));
            type_id(frontend, value, actual, sizeof(actual));
            report(frontend, "E2S136", start,
                token_end(frontend, *cursor - 1),
                "'%s' returns %s but this returns %s; an optional never "
                "satisfies a non-optional",
                function->name, wanted, actual);
            recover_to_boundary(frontend, cursor);
            return true;
        }
        if (frontend->return_count >= RETURN_LIMIT) {
            report(frontend, "E2S141", start, token_end(frontend, *cursor - 1),
                "source exceeds the Optional frontend limits");
            return false;
        }
        returned = &frontend->returns[frontend->return_count];
        returned->owner_function = function_index;
        /* A concrete T under an expected Optional(T) is injected, so the
         * return produces the result type while the written type is kept
         * beside it. */
        returned->type = function->result;
        returned->written = value;
        returned->is_null = value_null;
        returned->start = start;
        returned->end = token_end(frontend, *cursor - 1);
        frontend->return_count += 1;
        return true;
    }

    if (token_is(frontend, *cursor, "if")) {
        NarrowCondition narrow;
        Environment *then_environment;
        Environment *else_environment;
        bool then_terminates = false;
        bool else_terminates = false;
        size_t mark = frontend->environment_top;
        size_t condition;

        *cursor += 1;
        if (token_is(frontend, *cursor, "null") &&
            !is_equality(frontend, *cursor + 1)) {
            report(frontend, "E2S140", start, token_end(frontend, *cursor),
                "'null' is not a condition; this language has no "
                "truthiness and 'null' is not a Bool");
            recover_to_boundary(frontend, cursor);
            return true;
        }
        if (!parse_condition(frontend, function_index, cursor, environment,
                &condition, &narrow)) {
            recover_to_boundary(frontend, cursor);
            return true;
        }
        if (frontend->types[condition].kind != TYPE_BOOL) {
            char actual[IDENTITY_LIMIT];
            type_id(frontend, condition, actual, sizeof(actual));
            report(frontend, "E2S140", start,
                token_end(frontend, *cursor - 1),
                "a condition must be a Bool but this is %s; there is "
                "no truthiness", actual);
            recover_to_boundary(frontend, cursor);
            return true;
        }

        /* Each edge gets its own environment, so a fact established for one
         * branch cannot be observed by its sibling. */
        then_environment = environment_copy(frontend, environment);
        else_environment = environment_copy(frontend, environment);
        if (then_environment == NULL || else_environment == NULL) {
            report(frontend, "E2S141", start, token_end(frontend, *cursor - 1),
                "branch nesting exceeds the Optional frontend limits");
            frontend->environment_top = mark;
            return false;
        }
        if (narrow.present && narrow.subject.slot < BINDING_SLOT_LIMIT) {
            Environment *edge = narrow.on_true
                ? then_environment
                : else_environment;
            edge->narrowed[narrow.subject.slot] = 1;
            record_refinement(frontend, function_index, &narrow);
        }
        if (!parse_block(frontend, function_index, cursor, then_environment,
                depth + 1, &then_terminates)) {
            frontend->environment_top = mark;
            return false;
        }
        if (token_is(frontend, *cursor, "else")) {
            *cursor += 1;
            frontend->recovering = false;
            if (token_is(frontend, *cursor, "if")) {
                if (!parse_statement(frontend, function_index, cursor,
                        else_environment, depth + 1, &else_terminates)) {
                    frontend->environment_top = mark;
                    return false;
                }
            } else if (!parse_block(frontend, function_index, cursor,
                    else_environment, depth + 1, &else_terminates)) {
                frontend->environment_top = mark;
                return false;
            }
        }

        /* The join. A branch that definitely returns contributes nothing to
         * the continuation, which is exactly what makes the early-return
         * guard carry the opposite edge's refinement forward; otherwise the
         * two edges merge by intersection. */
        if (then_terminates && !else_terminates) {
            memcpy(environment, else_environment, sizeof(*environment));
        } else if (!then_terminates && else_terminates) {
            memcpy(environment, then_environment, sizeof(*environment));
        } else {
            memcpy(environment, then_environment, sizeof(*environment));
            environment_intersect(environment, else_environment);
        }
        *terminates = then_terminates && else_terminates;
        frontend->environment_top = mark;
        return true;
    }

    if (token_is(frontend, *cursor, "while")) {
        NarrowCondition narrow;
        Environment *body_environment;
        bool body_terminates = false;
        size_t mark = frontend->environment_top;
        size_t condition;
        size_t body_open = *cursor;
        size_t body_close;
        size_t brace = 0;

        *cursor += 1;
        while (body_open < frontend->token_count &&
            !token_is(frontend, body_open, "{")) {
            body_open += 1;
        }
        body_close = body_open;
        do {
            if (body_close >= frontend->token_count) break;
            if (token_is(frontend, body_close, "{")) brace += 1;
            if (token_is(frontend, body_close, "}")) brace -= 1;
            body_close += 1;
        } while (brace > 0);

        /* The backedge is applied before the header is typed, because the
         * header is re-evaluated on every iteration too. */
        discard_loop_variant(frontend, function_index, environment, body_open,
            body_close);
        if (!parse_condition(frontend, function_index, cursor, environment,
                &condition, &narrow)) {
            recover_to_boundary(frontend, cursor);
            return true;
        }
        if (frontend->types[condition].kind != TYPE_BOOL) {
            char actual[IDENTITY_LIMIT];
            type_id(frontend, condition, actual, sizeof(actual));
            report(frontend, "E2S140", start,
                token_end(frontend, *cursor - 1),
                "a condition must be a Bool but this is %s; there is "
                "no truthiness", actual);
            recover_to_boundary(frontend, cursor);
            return true;
        }
        /* Narrowing from a loop header is not part of this slice, so the
         * recognized condition types normally and refines nothing. */
        body_environment = environment_copy(frontend, environment);
        if (body_environment == NULL) {
            report(frontend, "E2S141", start, token_end(frontend, *cursor - 1),
                "branch nesting exceeds the Optional frontend limits");
            frontend->environment_top = mark;
            return false;
        }
        if (!parse_block(frontend, function_index, cursor, body_environment,
                depth + 1, &body_terminates)) {
            frontend->environment_top = mark;
            return false;
        }
        /* A loop may run zero times, so only facts that hold both without the
         * body and after it survive. */
        environment_intersect(environment, body_environment);
        frontend->environment_top = mark;
        return true;
    }

    if (token_has_kind(frontend, *cursor, TOKEN_IDENTIFIER) &&
        token_is(frontend, *cursor + 1, "=")) {
        BindingRef target = resolve_binding(frontend, function_index,
            frontend->tokens[*cursor].text);
        char name[TEXT_LIMIT];
        size_t value;
        bool value_null = false;

        memcpy(name, frontend->tokens[*cursor].text, TEXT_LIMIT);
        if (!target.found) {
            report(frontend, "E2S139", start, token_end(frontend, *cursor),
                "unknown name '%s'; the only absent value is 'null'", name);
            recover_to_boundary(frontend, cursor);
            return true;
        }
        if (!target.is_mutable) {
            report(frontend, "E2S143", start, token_end(frontend, *cursor),
                "cannot assign to immutable binding '%s'; declare it with "
                "'let mut'", name);
            recover_to_boundary(frontend, cursor);
            return true;
        }
        *cursor += 2;
        if (!parse_expression(frontend, function_index, cursor,
                target.declared, environment, &value, &value_null)) {
            recover_to_boundary(frontend, cursor);
            return true;
        }
        /* The replacement is checked against the declared type, never against
         * the refined one: narrowing does not change what may be stored. */
        if (!assignable(frontend, target.declared, value)) {
            char wanted[IDENTITY_LIMIT];
            char actual[IDENTITY_LIMIT];
            type_id(frontend, target.declared, wanted, sizeof(wanted));
            type_id(frontend, value, actual, sizeof(actual));
            report(frontend, "E2S136", start,
                token_end(frontend, *cursor - 1),
                "binding '%s' is %s but this assignment writes %s; an "
                "optional never satisfies a non-optional",
                name, wanted, actual);
            recover_to_boundary(frontend, cursor);
            return true;
        }
        discard_refinement(frontend, function_index, environment, &target,
            "assignment", start, token_end(frontend, *cursor - 1));
        return true;
    }

    if (token_has_kind(frontend, *cursor, TOKEN_IDENTIFIER) &&
        token_is(frontend, *cursor + 1, "(")) {
        size_t value;
        bool value_null = false;
        if (!parse_expression(frontend, function_index, cursor, (size_t)-1,
                environment, &value, &value_null)) {
            recover_to_boundary(frontend, cursor);
            return true;
        }
        return true;
    }

    report(frontend, "E2S141", start, token_end(frontend, *cursor),
        "unsupported statement in the bounded Optional frontend");
    recover_to_boundary(frontend, cursor);
    return true;
}

static bool parse_block(
    Frontend *frontend,
    size_t function_index,
    size_t *cursor,
    Environment *environment,
    size_t depth,
    bool *terminates
) {
    bool block_terminates = false;

    *terminates = false;
    if (depth >= BLOCK_DEPTH_LIMIT) {
        report(frontend, "E2S141", token_start(frontend, *cursor),
            token_end(frontend, *cursor),
            "block nesting exceeds the Optional frontend limits");
        return false;
    }
    if (!token_is(frontend, *cursor, "{")) {
        report(frontend, "E2S141", token_start(frontend, *cursor),
            token_end(frontend, *cursor), "expected '{' opening a block");
        return false;
    }
    *cursor += 1;
    while (*cursor < frontend->token_count &&
        !token_is(frontend, *cursor, "}")) {
        bool statement_terminates = false;
        size_t before = *cursor;
        frontend->recovering = false;
        if (!parse_statement(frontend, function_index, cursor, environment,
                depth, &statement_terminates)) {
            return false;
        }
        if (statement_terminates) block_terminates = true;
        /* Recovery that lands on the same token would otherwise spin. */
        if (*cursor == before) *cursor += 1;
    }
    if (!token_is(frontend, *cursor, "}")) {
        report(frontend, "E2S141", token_start(frontend, *cursor),
            token_end(frontend, *cursor), "expected '}' closing a block");
        return false;
    }
    *cursor += 1;
    frontend->recovering = false;
    *terminates = block_terminates;
    return true;
}

static bool parse_function_body(Frontend *frontend, size_t function_index) {
    Function *function = &frontend->functions[function_index];
    size_t cursor = function->body_start;
    Environment *environment;
    bool terminates = false;

    if (!token_is(frontend, cursor, "{")) return true;
    /* Refinements never escape a function, so the pool starts empty for each
     * one and nothing survives the return. */
    frontend->environment_top = 0;
    environment = environment_alloc(frontend);
    if (environment == NULL) return false;
    frontend->recovering = false;
    (void)parse_block(frontend, function_index, &cursor, environment, 0,
        &terminates);
    frontend->environment_top = 0;
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
    /*
     * Narrowing facts, keyed by binding identity rather than by name, so a
     * consumer can check that a refinement exists exactly where it was
     * established, that every discard has a stated reason, and that a use
     * typed as `T` sits under a fact that reaches it. The declared type is
     * printed beside the refined one on every row: a refinement is an extra
     * fact about an edge, never a rewrite of the declaration.
     */
    for (size_t index = 0; index < frontend->refinement_count; ++index) {
        const Refinement *fact = &frontend->refinements[index];
        const Binding *binding = fact->is_parameter
            ? &frontend->parameters[fact->index]
            : &frontend->locals[fact->index];
        char declared[IDENTITY_LIMIT];
        char narrowed[IDENTITY_LIMIT];
        type_id(frontend, fact->declared, declared, sizeof(declared));
        type_id(frontend, fact->narrowed, narrowed, sizeof(narrowed));
        fprintf(
            file,
            "refinement|owner=function:%s|binding=%s:%s:%s|declared=%s"
            "|narrowed=%s|edge=%s|condition-span=%zu..%zu"
            "|binding-span=%zu..%zu\n",
            frontend->functions[fact->owner_function].name,
            fact->is_parameter ? "parameter" : "local",
            frontend->functions[fact->owner_function].name,
            binding->name,
            declared,
            narrowed,
            fact->on_true ? "true" : "false",
            fact->start,
            fact->end,
            binding->start,
            binding->end
        );
    }
    for (size_t index = 0; index < frontend->invalidation_count; ++index) {
        const Invalidation *invalidation = &frontend->invalidations[index];
        const Binding *binding = invalidation->is_parameter
            ? &frontend->parameters[invalidation->index]
            : &frontend->locals[invalidation->index];
        fprintf(
            file,
            "refinement-discarded|owner=function:%s|binding=%s:%s:%s"
            "|reason=%s|span=%zu..%zu\n",
            frontend->functions[invalidation->owner_function].name,
            invalidation->is_parameter ? "parameter" : "local",
            frontend->functions[invalidation->owner_function].name,
            binding->name,
            invalidation->reason,
            invalidation->start,
            invalidation->end
        );
    }
    for (size_t index = 0; index < frontend->narrowed_use_count; ++index) {
        const NarrowedUse *use = &frontend->narrowed_uses[index];
        const Binding *binding = use->is_parameter
            ? &frontend->parameters[use->index]
            : &frontend->locals[use->index];
        char declared[IDENTITY_LIMIT];
        char narrowed[IDENTITY_LIMIT];
        type_id(frontend, use->declared, declared, sizeof(declared));
        type_id(frontend, use->narrowed, narrowed, sizeof(narrowed));
        fprintf(
            file,
            "narrowed-use|owner=function:%s|binding=%s:%s:%s|declared=%s"
            "|used-as=%s|use-span=%zu..%zu\n",
            frontend->functions[use->owner_function].name,
            use->is_parameter ? "parameter" : "local",
            frontend->functions[use->owner_function].name,
            binding->name,
            declared,
            narrowed,
            use->start,
            use->end
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
