#ifndef KOFUN_WASM_LIST_PROFILE_H
#define KOFUN_WASM_LIST_PROFILE_H

/* Bounded List[Int]/List[Text] frontend for wasm32-hostabi1 (#1004).
 *
 * The established Text-only frontend remains byte- and import-compatible.
 * Sources containing list syntax enter this separate parser, which admits
 * immutable reference bindings, direct reference calls, len, and checked
 * indexing. Lists use the AggregateLayout v1 u64 header and wasm32 element
 * strides (Int: 8, Text reference: 4).
 */

typedef enum {
    LP_TYPE_INVALID,
    LP_TYPE_INT,
    LP_TYPE_TEXT,
    LP_TYPE_LIST_INT,
    LP_TYPE_LIST_TEXT,
    LP_TYPE_VOID
} ListProfileType;

typedef enum {
    LP_EOF,
    LP_IDENTIFIER,
    LP_INTEGER,
    LP_STRING,
    LP_LEFT_PAREN,
    LP_RIGHT_PAREN,
    LP_LEFT_BRACE,
    LP_RIGHT_BRACE,
    LP_LEFT_BRACKET,
    LP_RIGHT_BRACKET,
    LP_COLON,
    LP_EQUAL,
    LP_ARROW,
    LP_COMMA,
    LP_MINUS
} ListProfileTokenKind;

typedef struct {
    ListProfileTokenKind kind;
    const char *start;
    size_t length;
    uint64_t magnitude;
    size_t line;
} ListProfileToken;

typedef enum {
    LPX_INT,
    LPX_TEXT,
    LPX_LIST,
    LPX_LOCAL,
    LPX_CALL,
    LPX_LEN,
    LPX_INDEX
} ListProfileExpressionKind;

typedef struct {
    ListProfileExpressionKind kind;
    ListProfileType type;
    int64_t integer;
    const char *literal;
    size_t literal_length;
    int local;
    int owner;
    char callee[NAME_CAPACITY];
    size_t callee_length;
    int callee_index;
    int item_start;
    int item_count;
    int operand;
    int index;
    size_t line;
    bool inferring;
} ListProfileExpression;

typedef enum { LPS_BIND, LPS_PRINT, LPS_RETURN } ListProfileStatementKind;

typedef struct {
    ListProfileStatementKind kind;
    int expression;
    int local;
    ListProfileType annotation;
    int next;
} ListProfileStatement;

typedef struct {
    char name[NAME_CAPACITY];
    size_t name_length;
    int parameter_count;
    ListProfileType parameters[MAX_PARAMETERS];
    int local_count;
    ListProfileType result;
    bool saw_return;
    int body;
    size_t line;
} ListProfileFunction;

typedef struct {
    char name[NAME_CAPACITY];
    size_t name_length;
    int owner;
    int local;
    ListProfileType type;
} ListProfileBinding;

typedef struct {
    const char *source;
    size_t length;
    size_t cursor;
    size_t line;
    ListProfileToken token;
    const char *error;
    size_t error_line;
    ListProfileExpression expressions[PROFILE_MAX_NODES];
    size_t expression_count;
    ListProfileStatement statements[PROFILE_MAX_STATEMENTS];
    size_t statement_count;
    ListProfileFunction functions[PROFILE_MAX_FUNCTIONS];
    size_t function_count;
    ListProfileBinding bindings[PROFILE_MAX_BINDINGS];
    size_t binding_count;
    int items[PROFILE_MAX_ARGUMENTS];
    size_t item_count;
    int current_function;
    int main_index;
    size_t expression_nesting;
} ListProfileParser;

static void list_profile_error_at(
    ListProfileParser *parser,
    const char *message,
    size_t line
) {
    if (parser->error != NULL) return;
    parser->error = message;
    parser->error_line = line == 0 ? parser->line : line;
}

static void list_profile_error(
    ListProfileParser *parser,
    const char *message
) {
    list_profile_error_at(parser, message, parser->token.line);
}

static void list_profile_next(ListProfileParser *parser) {
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
    parser->token = (ListProfileToken){
        .kind = LP_EOF,
        .start = parser->source + parser->cursor,
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
        parser->token.kind = LP_IDENTIFIER;
        parser->token.start = start;
        parser->token.length =
            (size_t)((parser->source + parser->cursor) - start);
        return;
    }
    if (isdigit(value)) {
        uint64_t magnitude = (uint64_t)(value - '0');
        while (parser->cursor < parser->length &&
               isdigit((unsigned char)parser->source[parser->cursor])) {
            uint64_t digit =
                (uint64_t)(parser->source[parser->cursor++] - '0');
            if (magnitude > (UINT64_C(9223372036854775808) - digit) / 10) {
                list_profile_error(parser,
                    "integer literal exceeds Int64 in wasm32-hostabi1 List slice");
                return;
            }
            magnitude = magnitude * 10 + digit;
        }
        parser->token.kind = LP_INTEGER;
        parser->token.start = start;
        parser->token.length =
            (size_t)((parser->source + parser->cursor) - start);
        parser->token.magnitude = magnitude;
        return;
    }
    if (value == '"') {
        const char *payload = parser->source + parser->cursor;
        while (parser->cursor < parser->length &&
               parser->source[parser->cursor] != '"') {
            unsigned char inner =
                (unsigned char)parser->source[parser->cursor];
            if (inner == '\\') {
                list_profile_error(parser,
                    "wasm32-hostabi1 List Text does not support escapes");
                return;
            }
            if (inner == '\n' || inner == '\r' || inner == 0) {
                list_profile_error(parser,
                    "unterminated Text literal in wasm32-hostabi1 List slice");
                return;
            }
            ++parser->cursor;
        }
        if (parser->cursor == parser->length) {
            list_profile_error(parser,
                "unterminated Text literal in wasm32-hostabi1 List slice");
            return;
        }
        size_t payload_length =
            (size_t)((parser->source + parser->cursor) - payload);
        ++parser->cursor;
        if (!profile_utf8_valid((const uint8_t *)payload, payload_length)) {
            list_profile_error(parser,
                "Text literal is not well-formed UTF-8");
            return;
        }
        parser->token.kind = LP_STRING;
        parser->token.start = payload;
        parser->token.length = payload_length;
        return;
    }

    parser->token.start = start;
    parser->token.length = 1;
    switch (value) {
        case '(': parser->token.kind = LP_LEFT_PAREN; return;
        case ')': parser->token.kind = LP_RIGHT_PAREN; return;
        case '{': parser->token.kind = LP_LEFT_BRACE; return;
        case '}': parser->token.kind = LP_RIGHT_BRACE; return;
        case '[': parser->token.kind = LP_LEFT_BRACKET; return;
        case ']': parser->token.kind = LP_RIGHT_BRACKET; return;
        case ':': parser->token.kind = LP_COLON; return;
        case '=': parser->token.kind = LP_EQUAL; return;
        case ',': parser->token.kind = LP_COMMA; return;
        case '-':
            if (parser->cursor < parser->length &&
                parser->source[parser->cursor] == '>') {
                ++parser->cursor;
                parser->token.kind = LP_ARROW;
                parser->token.length = 2;
            } else {
                parser->token.kind = LP_MINUS;
            }
            return;
        default:
            list_profile_error(parser,
                "unsupported operation in wasm32-hostabi1 List slice");
            return;
    }
}

static bool list_profile_token_is(
    const ListProfileParser *parser,
    const char *word
) {
    size_t length = strlen(word);
    return parser->token.kind == LP_IDENTIFIER &&
           parser->token.length == length &&
           memcmp(parser->token.start, word, length) == 0;
}

static bool list_profile_consume(
    ListProfileParser *parser,
    ListProfileTokenKind kind
) {
    if (parser->token.kind != kind) return false;
    list_profile_next(parser);
    return true;
}

static bool list_profile_consume_word(
    ListProfileParser *parser,
    const char *word
) {
    if (!list_profile_token_is(parser, word)) return false;
    list_profile_next(parser);
    return true;
}

static bool list_profile_expect(
    ListProfileParser *parser,
    ListProfileTokenKind kind,
    const char *message
) {
    if (list_profile_consume(parser, kind)) return true;
    list_profile_error(parser, message);
    return false;
}

static bool list_profile_expect_word(
    ListProfileParser *parser,
    const char *word,
    const char *message
) {
    if (list_profile_consume_word(parser, word)) return true;
    list_profile_error(parser, message);
    return false;
}

static bool list_profile_copy_name(
    ListProfileParser *parser,
    char target[NAME_CAPACITY],
    size_t *length,
    const char *message
) {
    if (parser->token.kind != LP_IDENTIFIER) return false;
    if (parser->token.length >= NAME_CAPACITY) {
        list_profile_error(parser, message);
        return false;
    }
    *length = parser->token.length;
    memcpy(target, parser->token.start, *length);
    target[*length] = '\0';
    list_profile_next(parser);
    return true;
}

static ListProfileType list_profile_parse_type(ListProfileParser *parser) {
    if (list_profile_consume_word(parser, "Text")) return LP_TYPE_TEXT;
    if (!list_profile_consume_word(parser, "List")) {
        list_profile_error(parser,
            "wasm32-hostabi1 List type must be Text, List[Int], or List[Text]");
        return LP_TYPE_INVALID;
    }
    if (!list_profile_expect(parser, LP_LEFT_BRACKET,
                             "expected `[` after List")) {
        return LP_TYPE_INVALID;
    }
    ListProfileType result = LP_TYPE_INVALID;
    if (list_profile_consume_word(parser, "Int")) {
        result = LP_TYPE_LIST_INT;
    } else if (list_profile_consume_word(parser, "Text")) {
        result = LP_TYPE_LIST_TEXT;
    } else {
        list_profile_error(parser,
            "wasm32-hostabi1 List element must be Int or Text");
    }
    if (!list_profile_expect(parser, LP_RIGHT_BRACKET,
                             "expected `]` after List element type")) {
        return LP_TYPE_INVALID;
    }
    return result;
}

static int list_profile_find_local(
    const ListProfileParser *parser,
    int owner,
    const char *name,
    size_t length
) {
    for (size_t index = 0; index < parser->binding_count; ++index) {
        const ListProfileBinding *binding = &parser->bindings[index];
        if (binding->owner == owner && binding->name_length == length &&
            memcmp(binding->name, name, length) == 0) {
            return binding->local;
        }
    }
    return -1;
}

static ListProfileBinding *list_profile_binding(
    ListProfileParser *parser,
    int owner,
    int local
) {
    for (size_t index = 0; index < parser->binding_count; ++index) {
        ListProfileBinding *binding = &parser->bindings[index];
        if (binding->owner == owner && binding->local == local) return binding;
    }
    return NULL;
}

static int list_profile_add_local(
    ListProfileParser *parser,
    int owner,
    const char *name,
    size_t length,
    ListProfileType type
) {
    if (list_profile_find_local(parser, owner, name, length) >= 0) {
        list_profile_error(parser,
            "duplicate parameter or binding in wasm32-hostabi1 List function");
        return -1;
    }
    if (parser->binding_count == PROFILE_MAX_BINDINGS) {
        list_profile_error(parser,
            "too many bindings in wasm32-hostabi1 List slice");
        return -1;
    }
    int local = parser->functions[owner].local_count++;
    ListProfileBinding *binding = &parser->bindings[parser->binding_count++];
    memcpy(binding->name, name, length);
    binding->name[length] = '\0';
    binding->name_length = length;
    binding->owner = owner;
    binding->local = local;
    binding->type = type;
    return local;
}

static int list_profile_add_expression(
    ListProfileParser *parser,
    ListProfileExpression expression
) {
    if (parser->expression_count == PROFILE_MAX_NODES) {
        list_profile_error(parser,
            "too many expressions in wasm32-hostabi1 List slice");
        return -1;
    }
    int index = (int)parser->expression_count++;
    parser->expressions[index] = expression;
    return index;
}

static bool list_profile_add_item(ListProfileParser *parser, int expression) {
    if (parser->item_count == PROFILE_MAX_ARGUMENTS) {
        list_profile_error(parser,
            "too many list elements or direct-call arguments");
        return false;
    }
    parser->items[parser->item_count++] = expression;
    return true;
}

static int list_profile_parse_expression(ListProfileParser *parser);

static int list_profile_parse_primary(ListProfileParser *parser) {
    size_t line = parser->token.line;
    bool negative = list_profile_consume(parser, LP_MINUS);
    if (negative || parser->token.kind == LP_INTEGER) {
        if (parser->token.kind != LP_INTEGER) {
            list_profile_error(parser,
                "expected integer literal after `-` in List slice");
            return -1;
        }
        uint64_t magnitude = parser->token.magnitude;
        if ((!negative && magnitude > (uint64_t)INT64_MAX) ||
            (negative && magnitude > UINT64_C(9223372036854775808))) {
            list_profile_error(parser,
                "integer literal exceeds Int64 in wasm32-hostabi1 List slice");
            return -1;
        }
        int64_t value = negative
            ? (magnitude == UINT64_C(9223372036854775808)
                ? INT64_MIN : -(int64_t)magnitude)
            : (int64_t)magnitude;
        list_profile_next(parser);
        return list_profile_add_expression(parser, (ListProfileExpression){
            .kind = LPX_INT, .type = LP_TYPE_INT, .integer = value,
            .local = -1, .owner = parser->current_function,
            .callee_index = -1, .operand = -1, .index = -1, .line = line
        });
    }
    if (parser->token.kind == LP_STRING) {
        const char *literal = parser->token.start;
        size_t length = parser->token.length;
        if (length > (size_t)(KOFUN_WASM_PAGE_BYTES -
                              KOFUN_WASM_OBJECT_HEADER_BYTES)) {
            list_profile_error(parser,
                "Text literal exceeds the wasm32-hostabi1 arena capacity");
            return -1;
        }
        list_profile_next(parser);
        return list_profile_add_expression(parser, (ListProfileExpression){
            .kind = LPX_TEXT, .type = LP_TYPE_TEXT,
            .literal = literal, .literal_length = length,
            .local = -1, .owner = parser->current_function,
            .callee_index = -1, .operand = -1, .index = -1, .line = line
        });
    }
    if (list_profile_consume(parser, LP_LEFT_BRACKET)) {
        int parsed[PROFILE_MAX_ARGUMENTS];
        int count = 0;
        if (parser->token.kind != LP_RIGHT_BRACKET) {
            for (;;) {
                if (count == PROFILE_MAX_ARGUMENTS) {
                    list_profile_error(parser,
                        "List literal exceeds the bounded element count");
                    return -1;
                }
                int element = list_profile_parse_expression(parser);
                if (element < 0) return -1;
                parsed[count++] = element;
                if (!list_profile_consume(parser, LP_COMMA)) break;
            }
        }
        if (!list_profile_expect(parser, LP_RIGHT_BRACKET,
                                 "expected `]` after List literal")) {
            return -1;
        }
        int start = (int)parser->item_count;
        for (int item = 0; item < count; ++item) {
            if (!list_profile_add_item(parser, parsed[item])) return -1;
        }
        return list_profile_add_expression(parser, (ListProfileExpression){
            .kind = LPX_LIST, .type = LP_TYPE_INVALID,
            .local = -1, .owner = parser->current_function,
            .callee_index = -1, .item_start = start, .item_count = count,
            .operand = -1, .index = -1, .line = line
        });
    }
    if (parser->token.kind != LP_IDENTIFIER) {
        list_profile_error(parser,
            "expected List/Text/Int literal, binding, or direct call");
        return -1;
    }
    char name[NAME_CAPACITY];
    size_t name_length = 0;
    if (!list_profile_copy_name(parser, name, &name_length,
                                "List expression name is too long")) {
        return -1;
    }
    if (strcmp(name, "len") == 0 &&
        list_profile_consume(parser, LP_LEFT_PAREN)) {
        int operand = list_profile_parse_expression(parser);
        if (operand < 0 ||
            !list_profile_expect(parser, LP_RIGHT_PAREN,
                                 "expected `)` after len argument")) {
            return -1;
        }
        return list_profile_add_expression(parser, (ListProfileExpression){
            .kind = LPX_LEN, .type = LP_TYPE_INT,
            .local = -1, .owner = parser->current_function,
            .callee_index = -1, .operand = operand, .index = -1, .line = line
        });
    }
    if (list_profile_consume(parser, LP_LEFT_PAREN)) {
        int parsed[MAX_PARAMETERS];
        int count = 0;
        if (parser->token.kind != LP_RIGHT_PAREN) {
            for (;;) {
                if (count == MAX_PARAMETERS) {
                    list_profile_error(parser,
                        "direct List call exceeds six arguments");
                    return -1;
                }
                int argument = list_profile_parse_expression(parser);
                if (argument < 0) return -1;
                parsed[count++] = argument;
                if (!list_profile_consume(parser, LP_COMMA)) break;
            }
        }
        if (!list_profile_expect(parser, LP_RIGHT_PAREN,
                                 "expected `)` after direct List call")) {
            return -1;
        }
        int start = (int)parser->item_count;
        for (int argument = 0; argument < count; ++argument) {
            if (!list_profile_add_item(parser, parsed[argument])) return -1;
        }
        ListProfileExpression expression = {
            .kind = LPX_CALL, .type = LP_TYPE_INVALID,
            .local = -1, .owner = parser->current_function,
            .callee_length = name_length, .callee_index = -1,
            .item_start = start, .item_count = count,
            .operand = -1, .index = -1, .line = line
        };
        memcpy(expression.callee, name, name_length + 1);
        return list_profile_add_expression(parser, expression);
    }
    int local = list_profile_find_local(
        parser, parser->current_function, name, name_length
    );
    if (local < 0) {
        list_profile_error_at(parser,
            "unknown binding in wasm32-hostabi1 List slice", line);
        return -1;
    }
    return list_profile_add_expression(parser, (ListProfileExpression){
        .kind = LPX_LOCAL, .type = LP_TYPE_INVALID,
        .local = local, .owner = parser->current_function,
        .callee_index = -1, .operand = -1, .index = -1, .line = line
    });
}

static int list_profile_parse_expression(ListProfileParser *parser) {
    if (parser->expression_nesting == MAX_EXPRESSION_NESTING) {
        list_profile_error(parser,
            "List expression nesting exceeds wasm32 limit of 256");
        return -1;
    }
    ++parser->expression_nesting;
    int result = list_profile_parse_primary(parser);
    while (result >= 0 && parser->error == NULL &&
           list_profile_consume(parser, LP_LEFT_BRACKET)) {
        int index = list_profile_parse_expression(parser);
        if (index < 0 ||
            !list_profile_expect(parser, LP_RIGHT_BRACKET,
                                 "expected `]` after List index")) {
            result = -1;
            break;
        }
        result = list_profile_add_expression(parser, (ListProfileExpression){
            .kind = LPX_INDEX, .type = LP_TYPE_INVALID,
            .local = -1, .owner = parser->current_function,
            .callee_index = -1, .operand = result, .index = index,
            .line = parser->expressions[result].line
        });
    }
    --parser->expression_nesting;
    return result;
}

static int list_profile_add_statement(
    ListProfileParser *parser,
    ListProfileStatement statement
) {
    if (parser->statement_count == PROFILE_MAX_STATEMENTS) {
        list_profile_error(parser,
            "too many statements in wasm32-hostabi1 List slice");
        return -1;
    }
    int index = (int)parser->statement_count++;
    parser->statements[index] = statement;
    return index;
}

static bool list_profile_parse_body(
    ListProfileParser *parser,
    ListProfileFunction *fn
) {
    if (!list_profile_expect(parser, LP_LEFT_BRACE,
                             "expected `{` before List function body")) {
        return false;
    }
    int first = -1;
    int last = -1;
    while (parser->error == NULL && parser->token.kind != LP_RIGHT_BRACE &&
           parser->token.kind != LP_EOF) {
        ListProfileStatement statement = {
            .expression = -1, .local = -1,
            .annotation = LP_TYPE_INVALID, .next = -1
        };
        if (list_profile_consume_word(parser, "let")) {
            char name[NAME_CAPACITY];
            size_t name_length = 0;
            if (!list_profile_copy_name(parser, name, &name_length,
                                        "List binding name is too long")) {
                list_profile_error(parser, "expected binding name after `let`");
                return false;
            }
            if (list_profile_consume(parser, LP_COLON)) {
                statement.annotation = list_profile_parse_type(parser);
                if (statement.annotation == LP_TYPE_INVALID) return false;
            }
            if (!list_profile_expect(parser, LP_EQUAL,
                                     "expected `=` in List binding")) {
                return false;
            }
            statement.kind = LPS_BIND;
            statement.expression = list_profile_parse_expression(parser);
            if (statement.expression < 0) return false;
            statement.local = list_profile_add_local(
                parser, parser->current_function, name, name_length,
                statement.annotation
            );
            if (statement.local < 0) return false;
        } else if (list_profile_consume_word(parser, "print")) {
            if (!list_profile_expect(parser, LP_LEFT_PAREN,
                                     "expected `(` after print")) return false;
            statement.kind = LPS_PRINT;
            statement.expression = list_profile_parse_expression(parser);
            if (statement.expression < 0 ||
                !list_profile_expect(parser, LP_RIGHT_PAREN,
                                     "expected `)` after List output")) {
                return false;
            }
        } else if (list_profile_consume_word(parser, "return")) {
            if (fn->result == LP_TYPE_VOID) {
                list_profile_error(parser,
                    "fn main cannot return a value in this profile");
                return false;
            }
            statement.kind = LPS_RETURN;
            statement.expression = list_profile_parse_expression(parser);
            if (statement.expression < 0) return false;
            fn->saw_return = true;
        } else {
            list_profile_error(parser,
                "expected let, print, or return in wasm32-hostabi1 List slice");
            return false;
        }
        int index = list_profile_add_statement(parser, statement);
        if (index < 0) return false;
        if (first < 0) first = index;
        if (last >= 0) parser->statements[last].next = index;
        last = index;
    }
    if (!list_profile_expect(parser, LP_RIGHT_BRACE,
                             "unterminated wasm32-hostabi1 List function")) {
        return false;
    }
    fn->body = first;
    if (fn->result != LP_TYPE_VOID && !fn->saw_return) {
        list_profile_error_at(parser,
            "List-result function must return a value", fn->line);
        return false;
    }
    return true;
}

static bool list_profile_is_reference(ListProfileType type) {
    return type == LP_TYPE_TEXT || type == LP_TYPE_LIST_INT ||
           type == LP_TYPE_LIST_TEXT;
}

static bool list_profile_type_matches(
    ListProfileParser *parser,
    ListProfileType actual,
    ListProfileType expected,
    size_t line
) {
    if (expected == LP_TYPE_INVALID || actual == expected) return true;
    list_profile_error_at(parser,
        "type mismatch in wasm32-hostabi1 List expression", line);
    return false;
}

static ListProfileType list_profile_infer_expression(
    ListProfileParser *parser,
    int expression_index,
    ListProfileType expected
) {
    ListProfileExpression *expression = &parser->expressions[expression_index];
    if (expression->inferring) {
        list_profile_error_at(parser,
            "recursive List expression cannot be typed", expression->line);
        return LP_TYPE_INVALID;
    }
    if (expression->type != LP_TYPE_INVALID) {
        return list_profile_type_matches(
            parser, expression->type, expected, expression->line
        ) ? expression->type : LP_TYPE_INVALID;
    }
    expression->inferring = true;
    ListProfileType result = LP_TYPE_INVALID;
    if (expression->kind == LPX_LOCAL) {
        ListProfileBinding *binding = list_profile_binding(
            parser, expression->owner, expression->local
        );
        if (binding == NULL || binding->type == LP_TYPE_INVALID) {
            list_profile_error_at(parser,
                "List binding type could not be inferred", expression->line);
        } else {
            result = binding->type;
        }
    } else if (expression->kind == LPX_CALL) {
        if (expression->callee_index < 0) {
            list_profile_error_at(parser,
                "unknown direct List function", expression->line);
        } else {
            const ListProfileFunction *callee =
                &parser->functions[expression->callee_index];
            if (callee->parameter_count != expression->item_count) {
                list_profile_error_at(parser,
                    "direct List call has the wrong arity", expression->line);
            } else {
                for (int argument = 0;
                     argument < expression->item_count && parser->error == NULL;
                     ++argument) {
                    list_profile_infer_expression(
                        parser,
                        parser->items[expression->item_start + argument],
                        callee->parameters[argument]
                    );
                }
                if (parser->error == NULL) result = callee->result;
            }
        }
    } else if (expression->kind == LPX_LIST) {
        if (expected != LP_TYPE_INVALID &&
            expected != LP_TYPE_LIST_INT && expected != LP_TYPE_LIST_TEXT) {
            list_profile_error_at(parser,
                "List literal used where a non-List value is required",
                expression->line);
        } else if (expression->item_count == 0) {
            if (expected == LP_TYPE_INVALID) {
                list_profile_error_at(parser,
                    "empty List literal requires List[Int] or List[Text] context",
                    expression->line);
            } else {
                result = expected;
            }
        } else {
            ListProfileType element_expected = expected == LP_TYPE_LIST_INT
                ? LP_TYPE_INT
                : expected == LP_TYPE_LIST_TEXT ? LP_TYPE_TEXT
                : LP_TYPE_INVALID;
            ListProfileType element = list_profile_infer_expression(
                parser, parser->items[expression->item_start], element_expected
            );
            if (element == LP_TYPE_INT) result = LP_TYPE_LIST_INT;
            else if (element == LP_TYPE_TEXT) result = LP_TYPE_LIST_TEXT;
            else if (parser->error == NULL) {
                list_profile_error_at(parser,
                    "nested Lists are outside wasm32-hostabi1 List v1",
                    expression->line);
            }
            for (int item = 1;
                 item < expression->item_count && parser->error == NULL;
                 ++item) {
                list_profile_infer_expression(
                    parser, parser->items[expression->item_start + item], element
                );
            }
            if (parser->error == NULL && expected != LP_TYPE_INVALID &&
                result != expected) {
                list_profile_type_matches(
                    parser, result, expected, expression->line
                );
            }
        }
    } else if (expression->kind == LPX_LEN) {
        ListProfileType operand = list_profile_infer_expression(
            parser, expression->operand, LP_TYPE_INVALID
        );
        if (operand != LP_TYPE_LIST_INT && operand != LP_TYPE_LIST_TEXT) {
            if (parser->error == NULL) list_profile_error_at(parser,
                "len requires List[Int] or List[Text]", expression->line);
        } else {
            result = LP_TYPE_INT;
        }
    } else if (expression->kind == LPX_INDEX) {
        ListProfileType operand = list_profile_infer_expression(
            parser, expression->operand, LP_TYPE_INVALID
        );
        list_profile_infer_expression(
            parser, expression->index, LP_TYPE_INT
        );
        if (parser->error == NULL) {
            if (operand == LP_TYPE_LIST_INT) result = LP_TYPE_INT;
            else if (operand == LP_TYPE_LIST_TEXT) result = LP_TYPE_TEXT;
            else list_profile_error_at(parser,
                "indexing requires List[Int] or List[Text]", expression->line);
        }
    }
    expression->inferring = false;
    if (result != LP_TYPE_INVALID && parser->error == NULL &&
        list_profile_type_matches(parser, result, expected, expression->line)) {
        expression->type = result;
        return result;
    }
    return LP_TYPE_INVALID;
}

static bool list_profile_finish_types(ListProfileParser *parser) {
    for (size_t function_index = 0;
         function_index < parser->function_count && parser->error == NULL;
         ++function_index) {
        ListProfileFunction *fn = &parser->functions[function_index];
        for (int statement_index = fn->body; statement_index >= 0;
             statement_index = parser->statements[statement_index].next) {
            ListProfileStatement *statement =
                &parser->statements[statement_index];
            ListProfileType expected = statement->kind == LPS_BIND
                ? statement->annotation
                : statement->kind == LPS_RETURN ? fn->result
                : LP_TYPE_INVALID;
            ListProfileType actual = list_profile_infer_expression(
                parser, statement->expression, expected
            );
            if (parser->error != NULL) break;
            if (statement->kind == LPS_BIND) {
                if (!list_profile_is_reference(actual)) {
                    list_profile_error_at(parser,
                        "wasm32-hostabi1 locals must be Text, List[Int], or List[Text]",
                        parser->expressions[statement->expression].line);
                    break;
                }
                ListProfileBinding *binding = list_profile_binding(
                    parser, (int)function_index, statement->local
                );
                if (binding == NULL) {
                    list_profile_error_at(parser,
                        "internal missing List binding", fn->line);
                    break;
                }
                binding->type = actual;
            } else if (statement->kind == LPS_PRINT &&
                       !list_profile_is_reference(actual)) {
                list_profile_error_at(parser,
                    "wasm32-hostabi1 output requires Text, List[Int], or List[Text]",
                    parser->expressions[statement->expression].line);
            }
        }
    }
    return parser->error == NULL;
}

static bool list_profile_parse_program(ListProfileParser *parser) {
    parser->line = 1;
    parser->main_index = -1;
    parser->current_function = -1;
    list_profile_next(parser);
    while (parser->error == NULL && parser->token.kind != LP_EOF) {
        if (parser->function_count == PROFILE_MAX_FUNCTIONS) {
            list_profile_error(parser,
                "too many functions in wasm32-hostabi1 List slice");
            break;
        }
        if (!list_profile_expect_word(parser, "fn",
                                      "expected `fn` declaration")) break;
        int function_index = (int)parser->function_count++;
        ListProfileFunction *fn = &parser->functions[function_index];
        memset(fn, 0, sizeof(*fn));
        fn->body = -1;
        fn->result = LP_TYPE_VOID;
        fn->line = parser->token.line;
        if (!list_profile_copy_name(parser, fn->name, &fn->name_length,
                                    "function name is too long")) {
            list_profile_error(parser, "expected function name after `fn`");
            break;
        }
        for (int previous = 0; previous < function_index; ++previous) {
            if (parser->functions[previous].name_length == fn->name_length &&
                memcmp(parser->functions[previous].name,
                       fn->name, fn->name_length) == 0) {
                list_profile_error_at(parser,
                    "duplicate function in wasm32-hostabi1 List slice", fn->line);
                break;
            }
        }
        if (parser->error != NULL) break;
        parser->current_function = function_index;
        if (!list_profile_expect(parser, LP_LEFT_PAREN,
                                 "expected `(` after function name")) break;
        if (parser->token.kind != LP_RIGHT_PAREN) {
            for (;;) {
                if (fn->parameter_count == MAX_PARAMETERS) {
                    list_profile_error(parser,
                        "List function exceeds six parameters");
                    break;
                }
                char parameter[NAME_CAPACITY];
                size_t parameter_length = 0;
                if (!list_profile_copy_name(
                        parser, parameter, &parameter_length,
                        "parameter name is too long")) {
                    list_profile_error(parser, "expected List parameter name");
                    break;
                }
                if (!list_profile_expect(parser, LP_COLON,
                                         "expected `:` after List parameter")) {
                    break;
                }
                ListProfileType type = list_profile_parse_type(parser);
                if (type == LP_TYPE_INVALID ||
                    list_profile_add_local(
                        parser, function_index, parameter,
                        parameter_length, type
                    ) < 0) {
                    break;
                }
                fn->parameters[fn->parameter_count++] = type;
                if (!list_profile_consume(parser, LP_COMMA)) break;
            }
        }
        if (parser->error != NULL ||
            !list_profile_expect(parser, LP_RIGHT_PAREN,
                                 "expected `)` after List parameters")) break;
        if (list_profile_consume(parser, LP_ARROW)) {
            fn->result = list_profile_parse_type(parser);
            if (fn->result == LP_TYPE_INVALID) break;
        }
        if (strcmp(fn->name, "main") == 0) {
            if (parser->main_index >= 0) {
                list_profile_error_at(parser, "duplicate fn main", fn->line);
                break;
            }
            parser->main_index = function_index;
            if (fn->parameter_count != 0 || fn->result != LP_TYPE_VOID) {
                list_profile_error_at(parser,
                    "wasm32-hostabi1 requires fn main() with no result",
                    fn->line);
                break;
            }
        } else if (!list_profile_is_reference(fn->result)) {
            list_profile_error_at(parser,
                "List helper requires a Text, List[Int], or List[Text] result",
                fn->line);
            break;
        }
        if (!list_profile_parse_body(parser, fn)) break;
    }
    if (parser->error == NULL && parser->main_index < 0) {
        list_profile_error_at(parser,
            "wasm32-hostabi1 requires fn main", 1);
    }
    for (size_t index = 0;
         parser->error == NULL && index < parser->expression_count;
         ++index) {
        ListProfileExpression *expression = &parser->expressions[index];
        if (expression->kind != LPX_CALL) continue;
        for (size_t candidate = 0; candidate < parser->function_count;
             ++candidate) {
            const ListProfileFunction *fn = &parser->functions[candidate];
            if (fn->name_length == expression->callee_length &&
                memcmp(fn->name, expression->callee,
                       expression->callee_length) == 0) {
                expression->callee_index = (int)candidate;
                break;
            }
        }
        if (expression->callee_index < 0) {
            list_profile_error_at(parser,
                "unknown direct List function", expression->line);
        }
    }
    return parser->error == NULL && list_profile_finish_types(parser);
}

static bool profile_source_uses_list(const char *source, size_t length) {
    bool in_string = false;
    for (size_t index = 0; index < length; ++index) {
        if (!in_string && source[index] == '#') {
            while (index < length && source[index] != '\n') ++index;
            continue;
        }
        if (source[index] == '"') in_string = !in_string;
        if (!in_string && source[index] == '[') return true;
    }
    return false;
}

enum {
    LIST_PROFILE_ABORT_INDEX = 0,
    LIST_PROFILE_TEXT_OUT_INDEX = 1,
    LIST_PROFILE_INT_OUT_INDEX = 2,
    LIST_PROFILE_TEXT_LIST_OUT_INDEX = 3,
    LIST_PROFILE_START_INDEX = 4,
    LIST_PROFILE_ALLOC_INDEX = 5,
    LIST_PROFILE_USER_INDEX_BASE = 6,
    LIST_PROFILE_TYPE_ABORT = 0,
    LIST_PROFILE_TYPE_ONE_VOID = 1,
    LIST_PROFILE_TYPE_ALLOC = 2,
    LIST_PROFILE_TYPE_VOID = 3,
    LIST_PROFILE_TYPE_REF_BASE = 4,
    LIST_PROFILE_TYPE_COUNT = LIST_PROFILE_TYPE_REF_BASE + MAX_PARAMETERS + 1
};

static uint32_t list_profile_i32_scratch(
    const ListProfileParser *parser,
    const ListProfileFunction *fn,
    int expression
) {
    (void)parser;
    return (uint32_t)(fn->local_count + expression);
}

static uint32_t list_profile_i64_scratch(
    const ListProfileParser *parser,
    const ListProfileFunction *fn,
    int expression
) {
    return (uint32_t)(fn->local_count + parser->expression_count + expression);
}

static void list_profile_abort(
    Buffer *body,
    int code,
    int detail_expression,
    uint32_t detail_local
) {
    byte(body, OP_I32_CONST);
    sleb(body, code);
    if (detail_expression < 0) {
        byte(body, OP_I32_CONST);
        sleb(body, 0);
    } else {
        instruction_index(body, OP_LOCAL_GET, detail_local);
        byte(body, 0xa7); /* i32.wrap_i64 */
    }
    instruction_index(body, OP_CALL, LIST_PROFILE_ABORT_INDEX);
    byte(body, OP_UNREACHABLE);
}

static void list_profile_store_header(
    Buffer *body,
    uint32_t reference,
    uint64_t value
) {
    uint8_t header[KOFUN_WASM_OBJECT_HEADER_BYTES];
    kofun_wasm_write_u64_header(header, value);
    for (size_t index = 0; index < KOFUN_WASM_OBJECT_HEADER_BYTES; ++index) {
        instruction_index(body, OP_LOCAL_GET, reference);
        byte(body, OP_I32_CONST);
        sleb(body, header[index]);
        byte(body, 0x3a); /* i32.store8 */
        uleb(body, 0);
        uleb(body, index);
    }
}

static void list_profile_emit_expression(
    const ListProfileParser *parser,
    const ListProfileFunction *fn,
    int expression_index,
    Buffer *body
);

static void list_profile_emit_allocation(
    const ListProfileParser *parser,
    const ListProfileFunction *fn,
    int expression_index,
    size_t object_size,
    Buffer *body
) {
    uint32_t scratch = list_profile_i32_scratch(
        parser, fn, expression_index
    );
    byte(body, OP_I32_CONST);
    sleb(body, (int64_t)object_size);
    byte(body, OP_I32_CONST);
    sleb(body, 8);
    instruction_index(body, OP_CALL, LIST_PROFILE_ALLOC_INDEX);
    instruction_index(body, OP_LOCAL_SET, scratch);
    instruction_index(body, OP_LOCAL_GET, scratch);
    byte(body, OP_I32_EQZ);
    begin_if(body);
    byte(body, OP_I32_CONST);
    sleb(body, 2);
    byte(body, OP_I32_CONST);
    sleb(body, (int64_t)object_size);
    instruction_index(body, OP_CALL, LIST_PROFILE_ABORT_INDEX);
    byte(body, OP_UNREACHABLE);
    byte(body, OP_END);
}

static void list_profile_emit_expression(
    const ListProfileParser *parser,
    const ListProfileFunction *fn,
    int expression_index,
    Buffer *body
) {
    const ListProfileExpression *expression =
        &parser->expressions[expression_index];
    uint32_t scratch_i32 = list_profile_i32_scratch(
        parser, fn, expression_index
    );
    uint32_t scratch_i64 = list_profile_i64_scratch(
        parser, fn, expression_index
    );
    if (expression->kind == LPX_INT) {
        i64_const(body, expression->integer);
        return;
    }
    if (expression->kind == LPX_LOCAL) {
        instruction_index(body, OP_LOCAL_GET, (uint32_t)expression->local);
        return;
    }
    if (expression->kind == LPX_CALL) {
        for (int argument = 0; argument < expression->item_count; ++argument) {
            list_profile_emit_expression(
                parser, fn,
                parser->items[expression->item_start + argument], body
            );
        }
        instruction_index(
            body, OP_CALL,
            (uint32_t)(LIST_PROFILE_USER_INDEX_BASE +
                       expression->callee_index)
        );
        return;
    }
    if (expression->kind == LPX_TEXT) {
        size_t object_size = KOFUN_WASM_OBJECT_HEADER_BYTES +
                             expression->literal_length;
        list_profile_emit_allocation(
            parser, fn, expression_index, object_size, body
        );
        list_profile_store_header(
            body, scratch_i32, (uint64_t)expression->literal_length
        );
        for (size_t index = 0; index < expression->literal_length; ++index) {
            instruction_index(body, OP_LOCAL_GET, scratch_i32);
            byte(body, OP_I32_CONST);
            sleb(body, (uint8_t)expression->literal[index]);
            byte(body, 0x3a); /* i32.store8 */
            uleb(body, 0);
            uleb(body, KOFUN_WASM_OBJECT_HEADER_BYTES + index);
        }
        instruction_index(body, OP_LOCAL_GET, scratch_i32);
        return;
    }
    if (expression->kind == LPX_LIST) {
        size_t stride = expression->type == LP_TYPE_LIST_INT ? 8 : 4;
        size_t count = (size_t)expression->item_count;
        size_t object_size = KOFUN_WASM_OBJECT_HEADER_BYTES + count * stride;
        list_profile_emit_allocation(
            parser, fn, expression_index, object_size, body
        );
        list_profile_store_header(body, scratch_i32, count);
        for (int item = 0; item < expression->item_count; ++item) {
            instruction_index(body, OP_LOCAL_GET, scratch_i32);
            list_profile_emit_expression(
                parser, fn, parser->items[expression->item_start + item], body
            );
            if (expression->type == LP_TYPE_LIST_INT) {
                byte(body, 0x37); /* i64.store */
                uleb(body, 3);
            } else {
                byte(body, 0x36); /* i32.store */
                uleb(body, 2);
            }
            uleb(body, KOFUN_WASM_OBJECT_HEADER_BYTES +
                       (size_t)item * stride);
        }
        instruction_index(body, OP_LOCAL_GET, scratch_i32);
        return;
    }
    if (expression->kind == LPX_LEN) {
        list_profile_emit_expression(
            parser, fn, expression->operand, body
        );
        byte(body, 0x29); /* i64.load */
        uleb(body, 3);
        uleb(body, 0);
        return;
    }
    if (expression->kind == LPX_INDEX) {
        const ListProfileExpression *operand =
            &parser->expressions[expression->operand];
        list_profile_emit_expression(
            parser, fn, expression->operand, body
        );
        instruction_index(body, OP_LOCAL_SET, scratch_i32);
        list_profile_emit_expression(
            parser, fn, expression->index, body
        );
        instruction_index(body, OP_LOCAL_SET, scratch_i64);

        instruction_index(body, OP_LOCAL_GET, scratch_i64);
        i64_const(body, 0);
        byte(body, OP_I64_LT_S);
        begin_if(body);
        list_profile_abort(body, 1, expression->index, scratch_i64);
        byte(body, OP_END);

        instruction_index(body, OP_LOCAL_GET, scratch_i64);
        instruction_index(body, OP_LOCAL_GET, scratch_i32);
        byte(body, 0x29); /* i64.load length */
        uleb(body, 3);
        uleb(body, 0);
        byte(body, 0x5a); /* i64.ge_u */
        begin_if(body);
        list_profile_abort(body, 1, expression->index, scratch_i64);
        byte(body, OP_END);

        instruction_index(body, OP_LOCAL_GET, scratch_i32);
        byte(body, OP_I32_CONST);
        sleb(body, KOFUN_WASM_OBJECT_HEADER_BYTES);
        byte(body, OP_I32_ADD);
        instruction_index(body, OP_LOCAL_GET, scratch_i64);
        byte(body, 0xa7); /* i32.wrap_i64 */
        byte(body, OP_I32_CONST);
        sleb(body, operand->type == LP_TYPE_LIST_INT ? 8 : 4);
        byte(body, 0x6c); /* i32.mul */
        byte(body, OP_I32_ADD);
        if (operand->type == LP_TYPE_LIST_INT) {
            byte(body, 0x29); /* i64.load */
            uleb(body, 3);
        } else {
            byte(body, 0x28); /* i32.load */
            uleb(body, 2);
        }
        uleb(body, 0);
        return;
    }
    fatal("internal unsupported wasm32-hostabi1 List expression");
}

static Buffer list_profile_emit_user_body(
    const ListProfileParser *parser,
    const ListProfileFunction *fn
) {
    Buffer body = {0};
    uint64_t i32_locals =
        (uint64_t)(fn->local_count - fn->parameter_count) +
        parser->expression_count;
    uint64_t i64_locals = parser->expression_count;
    uleb(&body, 2);
    uleb(&body, i32_locals);
    byte(&body, 0x7f);
    uleb(&body, i64_locals);
    byte(&body, 0x7e);
    for (int statement_index = fn->body; statement_index >= 0;
         statement_index = parser->statements[statement_index].next) {
        const ListProfileStatement *statement =
            &parser->statements[statement_index];
        list_profile_emit_expression(
            parser, fn, statement->expression, &body
        );
        if (statement->kind == LPS_BIND) {
            instruction_index(
                &body, OP_LOCAL_SET, (uint32_t)statement->local
            );
        } else if (statement->kind == LPS_PRINT) {
            ListProfileType type =
                parser->expressions[statement->expression].type;
            uint32_t import = type == LP_TYPE_TEXT
                ? LIST_PROFILE_TEXT_OUT_INDEX
                : type == LP_TYPE_LIST_INT
                    ? LIST_PROFILE_INT_OUT_INDEX
                    : LIST_PROFILE_TEXT_LIST_OUT_INDEX;
            instruction_index(&body, OP_CALL, import);
        } else {
            byte(&body, OP_RETURN);
        }
    }
    byte(&body, OP_END);
    return body;
}

static Buffer emit_profile_list_module(const ListProfileParser *parser) {
    Buffer module = {0};
    static const uint8_t header[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00
    };
    bytes(&module, header, sizeof(header));

    Buffer types = {0};
    uleb(&types, LIST_PROFILE_TYPE_COUNT);
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
    static const char *const import_names[] = {
        "abort", "text_out", "list_int_out", "list_text_out"
    };
    uleb(&imports, 4);
    for (size_t index = 0; index < 4; ++index) {
        wasm_string(&imports, "kofun:host-abi-v1");
        wasm_string(&imports, import_names[index]);
        byte(&imports, 0x00);
        uleb(&imports, index == 0
            ? LIST_PROFILE_TYPE_ABORT : LIST_PROFILE_TYPE_ONE_VOID);
    }
    section(&module, 2, &imports);

    Buffer functions = {0};
    uleb(&functions, 2 + parser->function_count);
    uleb(&functions, LIST_PROFILE_TYPE_ONE_VOID);
    uleb(&functions, LIST_PROFILE_TYPE_ALLOC);
    for (size_t index = 0; index < parser->function_count; ++index) {
        const ListProfileFunction *fn = &parser->functions[index];
        uleb(&functions, fn->result == LP_TYPE_VOID
            ? LIST_PROFILE_TYPE_VOID
            : (uint64_t)(LIST_PROFILE_TYPE_REF_BASE + fn->parameter_count));
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
    byte(&exports, 0x00); uleb(&exports, LIST_PROFILE_START_INDEX);
    wasm_string(&exports, "kofun_alloc");
    byte(&exports, 0x00); uleb(&exports, LIST_PROFILE_ALLOC_INDEX);
    section(&module, 7, &exports);

    Buffer code = {0};
    uleb(&code, 2 + parser->function_count);
    Buffer start = {0};
    uleb(&start, 0);
    instruction_index(
        &start, OP_CALL,
        (uint32_t)(LIST_PROFILE_USER_INDEX_BASE + parser->main_index)
    );
    byte(&start, OP_END);
    uleb(&code, start.length); bytes(&code, start.data, start.length);
    Buffer allocator = emit_profile_allocator_body();
    uleb(&code, allocator.length); bytes(&code, allocator.data, allocator.length);
    for (size_t index = 0; index < parser->function_count; ++index) {
        Buffer body = list_profile_emit_user_body(
            parser, &parser->functions[index]
        );
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
