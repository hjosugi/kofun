#ifndef KOFUN_WASM_TEXT_PROFILE_H
#define KOFUN_WASM_TEXT_PROFILE_H

/* Bounded Text frontend and lowering for wasm32-hostabi1 (#1002).
 *
 * This lives beside the legacy Int Core instead of widening it.  The parser
 * accepts only immutable Text literals/bindings and direct Text calls; every
 * other operation fails before module publication.  Literal objects are
 * created through the one #1001 allocator and carry the v1 u64 byte header.
 */

enum {
    PROFILE_MAX_FUNCTIONS = 64,
    PROFILE_MAX_BINDINGS = 128,
    PROFILE_MAX_NODES = 1024,
    PROFILE_MAX_STATEMENTS = 256,
    PROFILE_MAX_ARGUMENTS = 512
};

typedef enum {
    PT_EOF,
    PT_IDENTIFIER,
    PT_STRING,
    PT_LEFT_PAREN,
    PT_RIGHT_PAREN,
    PT_LEFT_BRACE,
    PT_RIGHT_BRACE,
    PT_COLON,
    PT_EQUAL,
    PT_ARROW,
    PT_COMMA
} ProfileTokenKind;

typedef struct {
    ProfileTokenKind kind;
    const char *start;
    size_t length;
    size_t line;
} ProfileToken;

typedef enum { PX_LITERAL, PX_LOCAL, PX_CALL } ProfileExpressionKind;

typedef struct {
    ProfileExpressionKind kind;
    const char *literal;
    size_t literal_length;
    int local;
    char callee[NAME_CAPACITY];
    size_t callee_length;
    int callee_index;
    int argument_start;
    int argument_count;
    size_t line;
} ProfileExpression;

typedef enum { PS_BIND, PS_PRINT, PS_RETURN } ProfileStatementKind;

typedef struct {
    ProfileStatementKind kind;
    int expression;
    int local;
    int next;
} ProfileStatement;

typedef struct {
    char name[NAME_CAPACITY];
    size_t name_length;
    int parameter_count;
    int local_count;
    bool returns_text;
    bool saw_return;
    int body;
    size_t line;
} ProfileFunction;

typedef struct {
    char name[NAME_CAPACITY];
    size_t name_length;
    int owner;
} ProfileBinding;

typedef struct {
    const char *source;
    size_t length;
    size_t cursor;
    size_t line;
    ProfileToken token;
    const char *error;
    size_t error_line;
    ProfileExpression expressions[PROFILE_MAX_NODES];
    size_t expression_count;
    ProfileStatement statements[PROFILE_MAX_STATEMENTS];
    size_t statement_count;
    ProfileFunction functions[PROFILE_MAX_FUNCTIONS];
    size_t function_count;
    ProfileBinding bindings[PROFILE_MAX_BINDINGS];
    size_t binding_count;
    int arguments[PROFILE_MAX_ARGUMENTS];
    size_t argument_count;
    int current_function;
    int main_index;
    size_t expression_nesting;
} ProfileParser;

static void profile_error_at(
    ProfileParser *parser,
    const char *message,
    size_t line
) {
    if (parser->error != NULL) return;
    parser->error = message;
    parser->error_line = line == 0 ? parser->line : line;
}

static void profile_error(ProfileParser *parser, const char *message) {
    profile_error_at(parser, message, parser->token.line);
}

static bool profile_utf8_valid(const uint8_t *text, size_t length) {
    size_t index = 0;
    while (index < length) {
        uint8_t first = text[index++];
        if (first <= UINT8_C(0x7f)) {
            continue;
        }
        uint32_t value;
        size_t continuation;
        uint32_t minimum;
        if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf)) {
            value = first & UINT8_C(0x1f);
            continuation = 1;
            minimum = UINT32_C(0x80);
        } else if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef)) {
            value = first & UINT8_C(0x0f);
            continuation = 2;
            minimum = UINT32_C(0x800);
        } else if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4)) {
            value = first & UINT8_C(0x07);
            continuation = 3;
            minimum = UINT32_C(0x10000);
        } else {
            return false;
        }
        if (continuation > length - index) return false;
        for (size_t part = 0; part < continuation; ++part) {
            uint8_t next = text[index++];
            if ((next & UINT8_C(0xc0)) != UINT8_C(0x80)) return false;
            value = (value << 6) | (uint32_t)(next & UINT8_C(0x3f));
        }
        if (value < minimum || value > UINT32_C(0x10ffff) ||
            (value >= UINT32_C(0xd800) && value <= UINT32_C(0xdfff))) {
            return false;
        }
    }
    return true;
}

static void profile_next(ProfileParser *parser) {
    while (parser->cursor < parser->length) {
        unsigned char value = (unsigned char)parser->source[parser->cursor];
        if (value == '#') {
            while (parser->cursor < parser->length &&
                   parser->source[parser->cursor] != '\n') {
                ++parser->cursor;
            }
            continue;
        }
        if (!isspace(value)) break;
        if (value == '\n') ++parser->line;
        ++parser->cursor;
    }
    parser->token = (ProfileToken){
        .kind = PT_EOF,
        .start = parser->source + parser->cursor,
        .length = 0,
        .line = parser->line
    };
    if (parser->cursor == parser->length) return;

    const char *start = parser->source + parser->cursor;
    unsigned char value = (unsigned char)parser->source[parser->cursor++];
    if (identifier_start((char)value)) {
        while (parser->cursor < parser->length &&
               identifier_continue(parser->source[parser->cursor])) {
            ++parser->cursor;
        }
        parser->token.kind = PT_IDENTIFIER;
        parser->token.start = start;
        parser->token.length =
            (size_t)((parser->source + parser->cursor) - start);
        return;
    }
    if (value == '"') {
        const char *payload = parser->source + parser->cursor;
        while (parser->cursor < parser->length &&
               parser->source[parser->cursor] != '"') {
            unsigned char inner =
                (unsigned char)parser->source[parser->cursor];
            if (inner == '\\') {
                profile_error(parser,
                    "wasm32-hostabi1 Text does not support escapes");
                return;
            }
            if (inner == '\n' || inner == '\r' || inner == 0) {
                profile_error(parser,
                    "unterminated Text literal in wasm32-hostabi1");
                return;
            }
            ++parser->cursor;
        }
        if (parser->cursor == parser->length) {
            profile_error(parser,
                "unterminated Text literal in wasm32-hostabi1");
            return;
        }
        size_t payload_length =
            (size_t)((parser->source + parser->cursor) - payload);
        ++parser->cursor;
        if (!profile_utf8_valid((const uint8_t *)payload, payload_length)) {
            profile_error(parser,
                "Text literal is not well-formed UTF-8");
            return;
        }
        parser->token.kind = PT_STRING;
        parser->token.start = payload;
        parser->token.length = payload_length;
        return;
    }
    parser->token.start = start;
    parser->token.length = 1;
    switch (value) {
        case '(':
            parser->token.kind = PT_LEFT_PAREN;
            return;
        case ')':
            parser->token.kind = PT_RIGHT_PAREN;
            return;
        case '{':
            parser->token.kind = PT_LEFT_BRACE;
            return;
        case '}':
            parser->token.kind = PT_RIGHT_BRACE;
            return;
        case ':':
            parser->token.kind = PT_COLON;
            return;
        case '=':
            parser->token.kind = PT_EQUAL;
            return;
        case ',':
            parser->token.kind = PT_COMMA;
            return;
        case '-':
            if (parser->cursor < parser->length &&
                parser->source[parser->cursor] == '>') {
                ++parser->cursor;
                parser->token.kind = PT_ARROW;
                parser->token.length = 2;
                return;
            }
            break;
        default:
            break;
    }
    profile_error(parser,
        "unsupported operation in wasm32-hostabi1 Text slice");
}

static bool profile_token_is(
    const ProfileParser *parser,
    const char *word
) {
    size_t length = strlen(word);
    return parser->token.kind == PT_IDENTIFIER &&
           parser->token.length == length &&
           memcmp(parser->token.start, word, length) == 0;
}

static bool profile_consume(ProfileParser *parser, ProfileTokenKind kind) {
    if (parser->token.kind != kind) return false;
    profile_next(parser);
    return true;
}

static bool profile_consume_word(ProfileParser *parser, const char *word) {
    if (!profile_token_is(parser, word)) return false;
    profile_next(parser);
    return true;
}

static bool profile_expect(
    ProfileParser *parser,
    ProfileTokenKind kind,
    const char *message
) {
    if (profile_consume(parser, kind)) return true;
    profile_error(parser, message);
    return false;
}

static bool profile_expect_word(
    ProfileParser *parser,
    const char *word,
    const char *message
) {
    if (profile_consume_word(parser, word)) return true;
    profile_error(parser, message);
    return false;
}

static bool profile_copy_name(
    ProfileParser *parser,
    char target[NAME_CAPACITY],
    size_t *length,
    const char *too_long
) {
    if (parser->token.kind != PT_IDENTIFIER) return false;
    if (parser->token.length >= NAME_CAPACITY) {
        profile_error(parser, too_long);
        return false;
    }
    *length = parser->token.length;
    memcpy(target, parser->token.start, *length);
    target[*length] = '\0';
    profile_next(parser);
    return true;
}

static int profile_find_local(
    const ProfileParser *parser,
    int owner,
    const char *name,
    size_t length
) {
    int local = 0;
    for (size_t index = 0; index < parser->binding_count; ++index) {
        const ProfileBinding *binding = &parser->bindings[index];
        if (binding->owner != owner) continue;
        if (binding->name_length == length &&
            memcmp(binding->name, name, length) == 0) {
            return local;
        }
        ++local;
    }
    return -1;
}

static int profile_add_local(
    ProfileParser *parser,
    int owner,
    const char *name,
    size_t length
) {
    if (profile_find_local(parser, owner, name, length) >= 0) {
        profile_error(parser,
            "duplicate parameter or binding in wasm32-hostabi1 Text function");
        return -1;
    }
    if (parser->binding_count == PROFILE_MAX_BINDINGS) {
        profile_error(parser,
            "too many bindings in wasm32-hostabi1 Text slice");
        return -1;
    }
    ProfileBinding *binding = &parser->bindings[parser->binding_count++];
    memcpy(binding->name, name, length);
    binding->name[length] = '\0';
    binding->name_length = length;
    binding->owner = owner;
    return parser->functions[owner].local_count++;
}

static int profile_add_expression(
    ProfileParser *parser,
    ProfileExpression expression
) {
    if (parser->expression_count == PROFILE_MAX_NODES) {
        profile_error(parser,
            "too many expressions in wasm32-hostabi1 Text slice");
        return -1;
    }
    int index = (int)parser->expression_count++;
    parser->expressions[index] = expression;
    return index;
}

static int profile_parse_expression(ProfileParser *parser);

static int profile_parse_expression_inner(ProfileParser *parser) {
    size_t line = parser->token.line;
    if (parser->token.kind == PT_STRING) {
        const char *literal = parser->token.start;
        size_t length = parser->token.length;
        if (length > (size_t)(KOFUN_WASM_PAGE_BYTES -
                              KOFUN_WASM_OBJECT_HEADER_BYTES)) {
            profile_error(parser,
                "Text literal exceeds the wasm32-hostabi1 arena capacity");
            return -1;
        }
        profile_next(parser);
        return profile_add_expression(parser, (ProfileExpression){
            .kind = PX_LITERAL,
            .literal = literal,
            .literal_length = length,
            .local = -1,
            .callee_index = -1,
            .argument_start = 0,
            .argument_count = 0,
            .line = line
        });
    }
    if (parser->token.kind != PT_IDENTIFIER) {
        profile_error(parser,
            "expected Text literal, binding, or direct call");
        return -1;
    }
    char name[NAME_CAPACITY];
    size_t name_length = 0;
    if (!profile_copy_name(parser, name, &name_length,
                           "Text name is too long")) {
        return -1;
    }
    if (profile_consume(parser, PT_LEFT_PAREN)) {
        size_t argument_start = parser->argument_count;
        int argument_count = 0;
        if (parser->token.kind != PT_RIGHT_PAREN) {
            for (;;) {
                if (argument_count == MAX_PARAMETERS) {
                    profile_error(parser,
                        "direct Text call exceeds six arguments");
                    return -1;
                }
                int argument = profile_parse_expression(parser);
                if (argument < 0) return -1;
                if (parser->argument_count == PROFILE_MAX_ARGUMENTS) {
                    profile_error(parser,
                        "too many direct Text call arguments");
                    return -1;
                }
                parser->arguments[parser->argument_count++] = argument;
                ++argument_count;
                if (!profile_consume(parser, PT_COMMA)) break;
            }
        }
        if (!profile_expect(parser, PT_RIGHT_PAREN,
                            "expected `)` after direct Text call")) {
            return -1;
        }
        ProfileExpression expression = {
            .kind = PX_CALL,
            .local = -1,
            .callee_length = name_length,
            .callee_index = -1,
            .argument_start = (int)argument_start,
            .argument_count = argument_count,
            .line = line
        };
        memcpy(expression.callee, name, name_length + 1);
        return profile_add_expression(parser, expression);
    }
    int local = profile_find_local(
        parser, parser->current_function, name, name_length
    );
    if (local < 0) {
        profile_error_at(parser,
            "unknown Text binding in wasm32-hostabi1", line);
        return -1;
    }
    return profile_add_expression(parser, (ProfileExpression){
        .kind = PX_LOCAL,
        .local = local,
        .callee_index = -1,
        .line = line
    });
}

static int profile_parse_expression(ProfileParser *parser) {
    if (parser->expression_nesting == MAX_EXPRESSION_NESTING) {
        profile_error(parser,
            "Text expression nesting exceeds wasm32 limit of 256");
        return -1;
    }
    ++parser->expression_nesting;
    int result = profile_parse_expression_inner(parser);
    --parser->expression_nesting;
    return result;
}

static int profile_add_statement(
    ProfileParser *parser,
    ProfileStatement statement
) {
    if (parser->statement_count == PROFILE_MAX_STATEMENTS) {
        profile_error(parser,
            "too many statements in wasm32-hostabi1 Text slice");
        return -1;
    }
    int index = (int)parser->statement_count++;
    parser->statements[index] = statement;
    return index;
}

static bool profile_parse_body(ProfileParser *parser, ProfileFunction *fn) {
    if (!profile_expect(parser, PT_LEFT_BRACE,
                        "expected `{` before Text function body")) {
        return false;
    }
    int first = -1;
    int last = -1;
    while (parser->error == NULL && parser->token.kind != PT_RIGHT_BRACE &&
           parser->token.kind != PT_EOF) {
        ProfileStatement statement = {
            .expression = -1, .local = -1, .next = -1
        };
        if (profile_consume_word(parser, "let")) {
            char name[NAME_CAPACITY];
            size_t name_length = 0;
            if (!profile_copy_name(parser, name, &name_length,
                                   "Text binding name is too long")) {
                profile_error(parser, "expected binding name after `let`");
                return false;
            }
            if (profile_consume(parser, PT_COLON) &&
                !profile_expect_word(parser, "Text",
                                     "wasm32-hostabi1 bindings must be Text")) {
                return false;
            }
            if (!profile_expect(parser, PT_EQUAL,
                                "expected `=` in Text binding")) {
                return false;
            }
            statement.kind = PS_BIND;
            statement.expression = profile_parse_expression(parser);
            if (statement.expression < 0) return false;
            statement.local = profile_add_local(
                parser, parser->current_function, name, name_length
            );
            if (statement.local < 0) return false;
        } else if (profile_consume_word(parser, "print")) {
            if (!profile_expect(parser, PT_LEFT_PAREN,
                                "expected `(` after print")) return false;
            statement.kind = PS_PRINT;
            statement.expression = profile_parse_expression(parser);
            if (statement.expression < 0) return false;
            if (!profile_expect(parser, PT_RIGHT_PAREN,
                                "expected `)` after print Text")) return false;
        } else if (profile_consume_word(parser, "return")) {
            if (!fn->returns_text) {
                profile_error(parser,
                    "fn main cannot return a Text value in this profile");
                return false;
            }
            statement.kind = PS_RETURN;
            statement.expression = profile_parse_expression(parser);
            if (statement.expression < 0) return false;
            fn->saw_return = true;
        } else {
            profile_error(parser,
                "expected let, print, or return in wasm32-hostabi1 Text slice");
            return false;
        }
        int index = profile_add_statement(parser, statement);
        if (index < 0) return false;
        if (first < 0) first = index;
        if (last >= 0) parser->statements[last].next = index;
        last = index;
    }
    if (!profile_expect(parser, PT_RIGHT_BRACE,
                        "unterminated wasm32-hostabi1 Text function")) {
        return false;
    }
    fn->body = first;
    if (fn->returns_text && !fn->saw_return) {
        profile_error_at(parser,
            "Text-result function must return a Text value", fn->line);
        return false;
    }
    return true;
}

static bool profile_parse_program(ProfileParser *parser) {
    parser->line = 1;
    parser->main_index = -1;
    parser->current_function = -1;
    profile_next(parser);
    while (parser->error == NULL && parser->token.kind != PT_EOF) {
        if (parser->function_count == PROFILE_MAX_FUNCTIONS) {
            profile_error(parser,
                "too many functions in wasm32-hostabi1 Text slice");
            break;
        }
        if (!profile_expect_word(parser, "fn",
                                 "expected `fn` declaration")) break;
        int function_index = (int)parser->function_count++;
        ProfileFunction *fn = &parser->functions[function_index];
        memset(fn, 0, sizeof(*fn));
        fn->body = -1;
        fn->line = parser->token.line;
        if (!profile_copy_name(parser, fn->name, &fn->name_length,
                               "function name is too long")) {
            profile_error(parser, "expected function name after `fn`");
            break;
        }
        for (int previous = 0; previous < function_index; ++previous) {
            if (parser->functions[previous].name_length == fn->name_length &&
                memcmp(parser->functions[previous].name,
                       fn->name, fn->name_length) == 0) {
                profile_error_at(parser,
                    "duplicate function in wasm32-hostabi1 Text slice",
                    fn->line);
                break;
            }
        }
        if (parser->error != NULL) break;
        parser->current_function = function_index;
        if (!profile_expect(parser, PT_LEFT_PAREN,
                            "expected `(` after function name")) break;
        if (parser->token.kind != PT_RIGHT_PAREN) {
            for (;;) {
                if (fn->parameter_count == MAX_PARAMETERS) {
                    profile_error(parser,
                        "Text function exceeds six parameters");
                    break;
                }
                char parameter[NAME_CAPACITY];
                size_t parameter_length = 0;
                if (!profile_copy_name(parser, parameter, &parameter_length,
                                       "parameter name is too long")) {
                    profile_error(parser, "expected Text parameter name");
                    break;
                }
                if (!profile_expect(parser, PT_COLON,
                                    "expected `:` after Text parameter") ||
                    !profile_expect_word(parser, "Text",
                                         "wasm32-hostabi1 parameters must be Text")) {
                    break;
                }
                if (profile_add_local(parser, function_index, parameter,
                                      parameter_length) < 0) break;
                ++fn->parameter_count;
                if (!profile_consume(parser, PT_COMMA)) break;
            }
        }
        if (parser->error != NULL ||
            !profile_expect(parser, PT_RIGHT_PAREN,
                            "expected `)` after Text parameters")) break;
        if (profile_consume(parser, PT_ARROW)) {
            if (!profile_expect_word(parser, "Text",
                                     "wasm32-hostabi1 result must be Text")) break;
            fn->returns_text = true;
        }
        if (strcmp(fn->name, "main") == 0) {
            if (parser->main_index >= 0) {
                profile_error_at(parser, "duplicate fn main", fn->line);
                break;
            }
            parser->main_index = function_index;
            if (fn->parameter_count != 0 || fn->returns_text) {
                profile_error_at(parser,
                    "wasm32-hostabi1 requires fn main() with no result",
                    fn->line);
                break;
            }
        } else if (!fn->returns_text) {
            profile_error_at(parser,
                "Text helper requires an -> Text result", fn->line);
            break;
        }
        if (!profile_parse_body(parser, fn)) break;
    }
    if (parser->error == NULL && parser->main_index < 0) {
        profile_error_at(parser,
            "wasm32-hostabi1 requires fn main", 1);
    }
    for (size_t index = 0;
         parser->error == NULL && index < parser->expression_count;
         ++index) {
        ProfileExpression *expression = &parser->expressions[index];
        if (expression->kind != PX_CALL) continue;
        for (size_t candidate = 0; candidate < parser->function_count;
             ++candidate) {
            const ProfileFunction *fn = &parser->functions[candidate];
            if (fn->name_length == expression->callee_length &&
                memcmp(fn->name, expression->callee,
                       expression->callee_length) == 0) {
                expression->callee_index = (int)candidate;
                break;
            }
        }
        if (expression->callee_index < 0) {
            profile_error_at(parser,
                "unknown direct Text function", expression->line);
            break;
        }
        const ProfileFunction *callee =
            &parser->functions[expression->callee_index];
        if (!callee->returns_text ||
            callee->parameter_count != expression->argument_count) {
            profile_error_at(parser,
                "direct Text call has the wrong signature", expression->line);
            break;
        }
    }
    return parser->error == NULL;
}

static bool profile_program_is_empty(const ProfileParser *parser) {
    return parser->function_count == 1 && parser->main_index == 0 &&
           parser->functions[0].body < 0;
}

enum {
    PROFILE_ABORT_INDEX = 0,
    PROFILE_TEXT_OUT_INDEX = 1,
    PROFILE_START_INDEX = 2,
    PROFILE_ALLOC_INDEX = 3,
    PROFILE_USER_INDEX_BASE = 4,
    PROFILE_TYPE_ABORT = 0,
    PROFILE_TYPE_ONE_VOID = 1,
    PROFILE_TYPE_ALLOC = 2,
    PROFILE_TYPE_VOID = 3,
    PROFILE_TYPE_TEXT_BASE = 4,
    PROFILE_TYPE_COUNT = PROFILE_TYPE_TEXT_BASE + MAX_PARAMETERS + 1
};

static void profile_emit_expression(
    const ProfileParser *parser,
    const ProfileFunction *fn,
    int expression_index,
    Buffer *body
) {
    const ProfileExpression *expression =
        &parser->expressions[expression_index];
    if (expression->kind == PX_LOCAL) {
        instruction_index(body, OP_LOCAL_GET, (uint32_t)expression->local);
        return;
    }
    if (expression->kind == PX_CALL) {
        for (int argument = 0; argument < expression->argument_count;
             ++argument) {
            profile_emit_expression(
                parser, fn,
                parser->arguments[expression->argument_start + argument],
                body
            );
        }
        instruction_index(
            body, OP_CALL,
            (uint32_t)(PROFILE_USER_INDEX_BASE + expression->callee_index)
        );
        return;
    }

    uint32_t scratch = (uint32_t)fn->local_count;
    size_t object_size = KOFUN_WASM_OBJECT_HEADER_BYTES +
                         expression->literal_length;
    byte(body, OP_I32_CONST);
    sleb(body, (int64_t)object_size);
    byte(body, OP_I32_CONST);
    sleb(body, 8);
    instruction_index(body, OP_CALL, PROFILE_ALLOC_INDEX);
    instruction_index(body, OP_LOCAL_SET, scratch);
    instruction_index(body, OP_LOCAL_GET, scratch);
    byte(body, OP_I32_EQZ);
    begin_if(body);
    byte(body, OP_I32_CONST);
    sleb(body, 2);
    byte(body, OP_I32_CONST);
    sleb(body, (int64_t)object_size);
    instruction_index(body, OP_CALL, PROFILE_ABORT_INDEX);
    byte(body, OP_END);

    uint8_t object_header[KOFUN_WASM_OBJECT_HEADER_BYTES];
    kofun_wasm_write_u64_header(
        object_header, (uint64_t)expression->literal_length
    );
    for (size_t index = 0; index < KOFUN_WASM_OBJECT_HEADER_BYTES; ++index) {
        instruction_index(body, OP_LOCAL_GET, scratch);
        byte(body, OP_I32_CONST);
        sleb(body, object_header[index]);
        byte(body, 0x3a); /* i32.store8, alignment 1. */
        uleb(body, 0);
        uleb(body, index);
    }
    for (size_t index = 0; index < expression->literal_length; ++index) {
        instruction_index(body, OP_LOCAL_GET, scratch);
        byte(body, OP_I32_CONST);
        sleb(body, (uint8_t)expression->literal[index]);
        byte(body, 0x3a); /* i32.store8, alignment 1. */
        uleb(body, 0);
        uleb(body, KOFUN_WASM_OBJECT_HEADER_BYTES + index);
    }
    instruction_index(body, OP_LOCAL_GET, scratch);
}

static Buffer profile_emit_user_body(
    const ProfileParser *parser,
    const ProfileFunction *fn
) {
    Buffer body = {0};
    uint64_t declared =
        (uint64_t)(fn->local_count - fn->parameter_count) + 1;
    uleb(&body, 1);
    uleb(&body, declared);
    byte(&body, 0x7f);
    for (int statement_index = fn->body; statement_index >= 0;
         statement_index = parser->statements[statement_index].next) {
        const ProfileStatement *statement =
            &parser->statements[statement_index];
        profile_emit_expression(parser, fn, statement->expression, &body);
        if (statement->kind == PS_BIND) {
            instruction_index(
                &body, OP_LOCAL_SET, (uint32_t)statement->local
            );
        } else if (statement->kind == PS_PRINT) {
            instruction_index(&body, OP_CALL, PROFILE_TEXT_OUT_INDEX);
        } else {
            byte(&body, OP_RETURN);
        }
    }
    byte(&body, OP_END);
    return body;
}

static Buffer emit_profile_text_module(const ProfileParser *parser) {
    Buffer module = {0};
    static const uint8_t header[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00
    };
    bytes(&module, header, sizeof(header));

    Buffer types = {0};
    uleb(&types, PROFILE_TYPE_COUNT);
    /* abort(i32, i32), text_out/start(i32), alloc(i32,i32)->i32, void(). */
    byte(&types, 0x60); uleb(&types, 2);
    byte(&types, 0x7f); byte(&types, 0x7f); uleb(&types, 0);
    byte(&types, 0x60); uleb(&types, 1);
    byte(&types, 0x7f); uleb(&types, 0);
    byte(&types, 0x60); uleb(&types, 2);
    byte(&types, 0x7f); byte(&types, 0x7f); uleb(&types, 1);
    byte(&types, 0x7f);
    byte(&types, 0x60); uleb(&types, 0); uleb(&types, 0);
    for (int arity = 0; arity <= MAX_PARAMETERS; ++arity) {
        byte(&types, 0x60);
        uleb(&types, (uint64_t)arity);
        for (int parameter = 0; parameter < arity; ++parameter) {
            byte(&types, 0x7f);
        }
        uleb(&types, 1);
        byte(&types, 0x7f);
    }
    section(&module, 1, &types);

    Buffer imports = {0};
    uleb(&imports, 2);
    wasm_string(&imports, "kofun:host-abi-v1");
    wasm_string(&imports, "abort");
    byte(&imports, 0x00); uleb(&imports, PROFILE_TYPE_ABORT);
    wasm_string(&imports, "kofun:host-abi-v1");
    wasm_string(&imports, "text_out");
    byte(&imports, 0x00); uleb(&imports, PROFILE_TYPE_ONE_VOID);
    section(&module, 2, &imports);

    Buffer functions = {0};
    uleb(&functions, 2 + parser->function_count);
    uleb(&functions, PROFILE_TYPE_ONE_VOID);
    uleb(&functions, PROFILE_TYPE_ALLOC);
    for (size_t index = 0; index < parser->function_count; ++index) {
        const ProfileFunction *fn = &parser->functions[index];
        uleb(&functions, fn->returns_text
            ? (uint64_t)(PROFILE_TYPE_TEXT_BASE + fn->parameter_count)
            : PROFILE_TYPE_VOID);
    }
    section(&module, 3, &functions);

    Buffer memory = {0};
    uleb(&memory, 1); byte(&memory, 0x01); uleb(&memory, 1); uleb(&memory, 1);
    section(&module, 5, &memory);

    Buffer globals = {0};
    uleb(&globals, 2);
    byte(&globals, 0x7f); byte(&globals, 0x00);
    byte(&globals, OP_I32_CONST); sleb(&globals, KOFUN_WASM_HOST_ABI_REVISION);
    byte(&globals, OP_END);
    byte(&globals, 0x7f); byte(&globals, 0x01);
    byte(&globals, OP_I32_CONST); sleb(&globals, KOFUN_WASM_ARENA_BASE);
    byte(&globals, OP_END);
    section(&module, 6, &globals);

    Buffer exports = {0};
    uleb(&exports, 4);
    wasm_string(&exports, "memory"); byte(&exports, 0x02); uleb(&exports, 0);
    wasm_string(&exports, "kofun_abi_version");
    byte(&exports, 0x03); uleb(&exports, 0);
    wasm_string(&exports, "kofun_start");
    byte(&exports, 0x00); uleb(&exports, PROFILE_START_INDEX);
    wasm_string(&exports, "kofun_alloc");
    byte(&exports, 0x00); uleb(&exports, PROFILE_ALLOC_INDEX);
    section(&module, 7, &exports);

    Buffer code = {0};
    uleb(&code, 2 + parser->function_count);
    Buffer start = {0};
    uleb(&start, 0);
    instruction_index(
        &start, OP_CALL,
        (uint32_t)(PROFILE_USER_INDEX_BASE + parser->main_index)
    );
    byte(&start, OP_END);
    uleb(&code, start.length); bytes(&code, start.data, start.length);
    Buffer allocator = emit_profile_allocator_body();
    uleb(&code, allocator.length); bytes(&code, allocator.data, allocator.length);
    for (size_t index = 0; index < parser->function_count; ++index) {
        Buffer body = profile_emit_user_body(parser, &parser->functions[index]);
        uleb(&code, body.length); bytes(&code, body.data, body.length);
        free(body.data);
    }
    section(&module, 10, &code);

    free(types.data); free(imports.data); free(functions.data);
    free(memory.data); free(globals.data); free(exports.data);
    free(start.data); free(allocator.data); free(code.data);
    return module;
}

#endif
