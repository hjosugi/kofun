/*
 * Kofun bounded native CLI profile compiler.
 *
 * This audited C11 bootstrap parses a declarative Kofun CLI source and emits
 * an executable Linux x86-64 ELF image directly. It does not invoke an
 * assembler, linker, C compiler, shell, or runtime tool while compiling an
 * application.
 */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "runtime_template.inc"

enum {
    MAX_SOURCE = 65536,
    MAX_IDENTIFIER = 63,
    MAX_TEXT = 255,
    MAX_COMMANDS = 8,
    MAX_POSITIONS = 4,
    MAX_OPTIONS = 8,
    MAX_CONFIG = 65536,
    CONFIG_MAGIC = 0x31494c43U,
    HEADER_SIZE = 28,
    COMMAND_SIZE = 32,
    POSITION_SIZE = 8,
    OPTION_SIZE = 20,
    ACTION_GREET = 1,
    ACTION_SUM = 2,
    ACTION_ENV = 3,
    ACTION_STATUS = 4,
    OPTION_BOOL = 1,
    OPTION_TEXT = 2,
};

typedef enum {
    TOKEN_EOF,
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
} TokenKind;

typedef struct {
    TokenKind kind;
    char text[MAX_TEXT + 1];
    size_t line;
    size_t column;
} Token;

typedef struct {
    const char *source;
    size_t length;
    size_t cursor;
    size_t line;
    size_t column;
    Token token;
} Parser;

typedef struct {
    char name[MAX_IDENTIFIER + 1];
    char about[MAX_TEXT + 1];
} Position;

typedef struct {
    char identifier[MAX_IDENTIFIER + 1];
    char long_name[MAX_TEXT + 1];
    char about[MAX_TEXT + 1];
    uint32_t kind;
    char default_value[MAX_TEXT + 1];
    bool has_default;
} Option;

typedef struct {
    char name[MAX_IDENTIFIER + 1];
    char about[MAX_TEXT + 1];
    uint32_t action;
    Position positions[MAX_POSITIONS];
    size_t position_count;
    Option options[MAX_OPTIONS];
    size_t option_count;
    bool has_about;
    bool has_action;
} Command;

typedef struct {
    char type_name[MAX_IDENTIFIER + 1];
    char name[MAX_TEXT + 1];
    char version[MAX_TEXT + 1];
    char about[MAX_TEXT + 1];
    Command commands[MAX_COMMANDS];
    size_t command_count;
    bool has_name;
    bool has_version;
    bool has_about;
} Application;

typedef struct {
    unsigned char bytes[MAX_CONFIG];
    size_t length;
} Config;

static void fail(const char *message) {
    fprintf(stderr, "kofun cli compiler: %s\n", message);
    exit(2);
}

static void source_fail(
    const Parser *parser,
    size_t line,
    size_t column,
    const char *message
) {
    (void)parser;
    fprintf(
        stderr,
        "kofun cli compiler: line %zu:%zu: %s\n",
        line,
        column,
        message
    );
    exit(2);
}

static char *read_source(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "kofun cli compiler: cannot read %s: %s\n",
                path, strerror(errno));
        exit(2);
    }
    if (fseek(file, 0, SEEK_END) != 0) fail("cannot seek source");
    long measured = ftell(file);
    if (measured < 0) fail("cannot measure source");
    if ((unsigned long)measured > MAX_SOURCE) {
        fail("source exceeds 65536-byte profile limit");
    }
    if (fseek(file, 0, SEEK_SET) != 0) fail("cannot rewind source");
    char *source = malloc((size_t)measured + 1);
    if (source == NULL) fail("out of memory");
    size_t read = fread(source, 1, (size_t)measured, file);
    if (read != (size_t)measured || ferror(file)) {
        fail("cannot read complete source");
    }
    if (fclose(file) != 0) fail("cannot close source");
    if (memchr(source, '\0', read) != NULL) {
        fail("source contains a NUL byte");
    }
    source[read] = '\0';
    *length = read;
    return source;
}

static void parser_advance_character(Parser *parser) {
    if (parser->cursor >= parser->length) return;
    if (parser->source[parser->cursor] == '\n') {
        ++parser->line;
        parser->column = 1;
    } else {
        ++parser->column;
    }
    ++parser->cursor;
}

static void skip_trivia(Parser *parser) {
    for (;;) {
        while (parser->cursor < parser->length &&
               isspace((unsigned char)parser->source[parser->cursor])) {
            parser_advance_character(parser);
        }
        if (parser->cursor >= parser->length ||
            parser->source[parser->cursor] != '#') {
            return;
        }
        while (parser->cursor < parser->length &&
               parser->source[parser->cursor] != '\n') {
            parser_advance_character(parser);
        }
    }
}

static bool identifier_start(char character) {
    return isalpha((unsigned char)character) || character == '_';
}

static bool identifier_continue(char character) {
    return isalnum((unsigned char)character) || character == '_';
}

static void next_token(Parser *parser) {
    skip_trivia(parser);
    Token *token = &parser->token;
    token->text[0] = '\0';
    token->line = parser->line;
    token->column = parser->column;
    if (parser->cursor >= parser->length) {
        token->kind = TOKEN_EOF;
        return;
    }
    char character = parser->source[parser->cursor];
    if (character == '{') {
        token->kind = TOKEN_LEFT_BRACE;
        parser_advance_character(parser);
        return;
    }
    if (character == '}') {
        token->kind = TOKEN_RIGHT_BRACE;
        parser_advance_character(parser);
        return;
    }
    if (identifier_start(character)) {
        size_t length = 0;
        while (parser->cursor < parser->length &&
               identifier_continue(parser->source[parser->cursor])) {
            if (length >= MAX_IDENTIFIER) {
                source_fail(
                    parser,
                    token->line,
                    token->column,
                    "identifier exceeds 63-byte profile limit"
                );
            }
            token->text[length++] = parser->source[parser->cursor];
            parser_advance_character(parser);
        }
        token->text[length] = '\0';
        token->kind = TOKEN_IDENTIFIER;
        return;
    }
    if (character == '"') {
        parser_advance_character(parser);
        size_t length = 0;
        while (parser->cursor < parser->length &&
               parser->source[parser->cursor] != '"') {
            unsigned char value =
                (unsigned char)parser->source[parser->cursor];
            if (value == '\n' || value == '\r' || value < 0x20) {
                source_fail(
                    parser,
                    token->line,
                    token->column,
                    "string literal contains a control character"
                );
            }
            parser_advance_character(parser);
            if (value == '\\') {
                if (parser->cursor >= parser->length) {
                    source_fail(
                        parser,
                        token->line,
                        token->column,
                        "unterminated string escape"
                    );
                }
                char escaped = parser->source[parser->cursor];
                parser_advance_character(parser);
                if (escaped == 'n') {
                    value = '\n';
                } else if (escaped == 't') {
                    value = '\t';
                } else if (escaped == '"' || escaped == '\\') {
                    value = (unsigned char)escaped;
                } else {
                    source_fail(
                        parser,
                        token->line,
                        token->column,
                        "unsupported string escape"
                    );
                }
            }
            if (length >= MAX_TEXT) {
                source_fail(
                    parser,
                    token->line,
                    token->column,
                    "string exceeds 255-byte profile limit"
                );
            }
            token->text[length++] = (char)value;
        }
        if (parser->cursor >= parser->length) {
            source_fail(
                parser,
                token->line,
                token->column,
                "unterminated string literal"
            );
        }
        parser_advance_character(parser);
        token->text[length] = '\0';
        token->kind = TOKEN_STRING;
        return;
    }
    source_fail(
        parser,
        token->line,
        token->column,
        "unexpected character in CLI declaration"
    );
}

static bool token_is(const Parser *parser, const char *text) {
    return parser->token.kind == TOKEN_IDENTIFIER &&
        strcmp(parser->token.text, text) == 0;
}

static void expect_keyword(Parser *parser, const char *keyword) {
    if (!token_is(parser, keyword)) {
        char message[160];
        int written = snprintf(
            message,
            sizeof(message),
            "expected '%s'",
            keyword
        );
        if (written < 0 || (size_t)written >= sizeof(message)) {
            fail("cannot format parser diagnostic");
        }
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            message
        );
    }
    next_token(parser);
}

static void expect_kind(
    Parser *parser,
    TokenKind kind,
    const char *description
) {
    if (parser->token.kind != kind) {
        char message[160];
        int written = snprintf(
            message,
            sizeof(message),
            "expected %s",
            description
        );
        if (written < 0 || (size_t)written >= sizeof(message)) {
            fail("cannot format parser diagnostic");
        }
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            message
        );
    }
}

static void take_identifier(
    Parser *parser,
    char destination[MAX_IDENTIFIER + 1]
) {
    expect_kind(parser, TOKEN_IDENTIFIER, "an identifier");
    strcpy(destination, parser->token.text);
    next_token(parser);
}

static void take_text(
    Parser *parser,
    char destination[MAX_TEXT + 1],
    bool allow_empty
) {
    expect_kind(parser, TOKEN_STRING, "a string literal");
    if (!allow_empty && parser->token.text[0] == '\0') {
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            "this string must not be empty"
        );
    }
    strcpy(destination, parser->token.text);
    next_token(parser);
}

static void expect_left_brace(Parser *parser) {
    expect_kind(parser, TOKEN_LEFT_BRACE, "'{'");
    next_token(parser);
}

static void reject_duplicate(
    const Parser *parser,
    bool already_set,
    const char *field
) {
    if (already_set) {
        char message[160];
        int written = snprintf(
            message,
            sizeof(message),
            "duplicate %s declaration",
            field
        );
        if (written < 0 || (size_t)written >= sizeof(message)) {
            fail("cannot format parser diagnostic");
        }
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            message
        );
    }
}

static uint32_t parse_action(Parser *parser) {
    expect_kind(parser, TOKEN_IDENTIFIER, "an action identifier");
    uint32_t action = 0;
    if (strcmp(parser->token.text, "greet") == 0) {
        action = ACTION_GREET;
    } else if (strcmp(parser->token.text, "sum") == 0) {
        action = ACTION_SUM;
    } else if (strcmp(parser->token.text, "env") == 0) {
        action = ACTION_ENV;
    } else if (strcmp(parser->token.text, "status") == 0) {
        action = ACTION_STATUS;
    } else {
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            "action must be greet, sum, env, or status"
        );
    }
    next_token(parser);
    return action;
}

static bool valid_long_option(const char *name) {
    if (name[0] != '-' || name[1] != '-' || name[2] == '\0') return false;
    for (size_t index = 2; name[index] != '\0'; ++index) {
        char character = name[index];
        if (!(islower((unsigned char)character) ||
              isdigit((unsigned char)character) ||
              character == '-')) {
            return false;
        }
    }
    return true;
}

static void validate_command(const Parser *parser, const Command *command) {
    if (!command->has_about || !command->has_action) {
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            "each command requires about and action declarations"
        );
    }
    size_t expected_positions =
        command->action == ACTION_SUM ? 2 : 1;
    if (command->position_count != expected_positions) {
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            "action has the wrong number of positional declarations"
        );
    }
    if (command->action != ACTION_GREET && command->option_count != 0) {
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            "only the bounded greet action accepts options"
        );
    }
    if (command->action == ACTION_GREET) {
        bool shout = false;
        bool prefix = false;
        for (size_t index = 0; index < command->option_count; ++index) {
            const Option *option = &command->options[index];
            if (strcmp(option->identifier, "shout") == 0 &&
                option->kind == OPTION_BOOL &&
                !option->has_default) {
                shout = true;
            } else if (strcmp(option->identifier, "prefix") == 0 &&
                       option->kind == OPTION_TEXT) {
                prefix = true;
            } else {
                source_fail(
                    parser,
                    parser->token.line,
                    parser->token.column,
                    "greet options are bool shout and text prefix"
                );
            }
        }
        if (!shout || !prefix || command->option_count != 2) {
            source_fail(
                parser,
                parser->token.line,
                parser->token.column,
                "greet requires bool shout and text prefix options"
            );
        }
    }
}

static void parse_position(Parser *parser, Command *command) {
    if (command->position_count >= MAX_POSITIONS) {
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            "command exceeds four positional arguments"
        );
    }
    Position *position = &command->positions[command->position_count++];
    take_identifier(parser, position->name);
    take_text(parser, position->about, false);
    for (size_t index = 0; index + 1 < command->position_count; ++index) {
        if (strcmp(command->positions[index].name, position->name) == 0) {
            source_fail(
                parser,
                parser->token.line,
                parser->token.column,
                "duplicate positional identifier"
            );
        }
    }
}

static void parse_option(Parser *parser, Command *command) {
    if (command->option_count >= MAX_OPTIONS) {
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            "command exceeds eight options"
        );
    }
    Option *option = &command->options[command->option_count++];
    take_identifier(parser, option->identifier);
    take_text(parser, option->long_name, false);
    if (!valid_long_option(option->long_name)) {
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            "long option must match --[a-z0-9-]+"
        );
    }
    if (token_is(parser, "bool")) {
        option->kind = OPTION_BOOL;
        next_token(parser);
    } else if (token_is(parser, "text")) {
        option->kind = OPTION_TEXT;
        next_token(parser);
    } else {
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            "option kind must be bool or text"
        );
    }
    take_text(parser, option->about, false);
    if (token_is(parser, "default")) {
        if (option->kind != OPTION_TEXT) {
            source_fail(
                parser,
                parser->token.line,
                parser->token.column,
                "only text options may have defaults"
            );
        }
        next_token(parser);
        take_text(parser, option->default_value, true);
        option->has_default = true;
    }
    for (size_t index = 0; index + 1 < command->option_count; ++index) {
        const Option *previous = &command->options[index];
        if (strcmp(previous->identifier, option->identifier) == 0 ||
            strcmp(previous->long_name, option->long_name) == 0) {
            source_fail(
                parser,
                parser->token.line,
                parser->token.column,
                "duplicate option identifier or long name"
            );
        }
    }
}

static void parse_command(Parser *parser, Application *application) {
    if (application->command_count >= MAX_COMMANDS) {
        source_fail(
            parser,
            parser->token.line,
            parser->token.column,
            "application exceeds eight commands"
        );
    }
    Command *command =
        &application->commands[application->command_count++];
    take_identifier(parser, command->name);
    for (size_t index = 0; index + 1 < application->command_count; ++index) {
        if (strcmp(application->commands[index].name, command->name) == 0) {
            source_fail(
                parser,
                parser->token.line,
                parser->token.column,
                "duplicate command name"
            );
        }
    }
    expect_left_brace(parser);
    while (parser->token.kind != TOKEN_RIGHT_BRACE) {
        if (parser->token.kind == TOKEN_EOF) {
            source_fail(
                parser,
                parser->token.line,
                parser->token.column,
                "unterminated command declaration"
            );
        }
        if (token_is(parser, "about")) {
            reject_duplicate(parser, command->has_about, "command about");
            command->has_about = true;
            next_token(parser);
            take_text(parser, command->about, false);
        } else if (token_is(parser, "position")) {
            next_token(parser);
            parse_position(parser, command);
        } else if (token_is(parser, "option")) {
            next_token(parser);
            parse_option(parser, command);
        } else if (token_is(parser, "action")) {
            reject_duplicate(parser, command->has_action, "action");
            command->has_action = true;
            next_token(parser);
            command->action = parse_action(parser);
        } else {
            source_fail(
                parser,
                parser->token.line,
                parser->token.column,
                "expected about, position, option, action, or '}'"
            );
        }
    }
    validate_command(parser, command);
    next_token(parser);
}

static Application parse_application(const char *source, size_t length) {
    Parser parser = {
        .source = source,
        .length = length,
        .cursor = 0,
        .line = 1,
        .column = 1,
    };
    Application application;
    memset(&application, 0, sizeof(application));
    next_token(&parser);
    expect_keyword(&parser, "cli");
    take_identifier(&parser, application.type_name);
    expect_left_brace(&parser);
    while (parser.token.kind != TOKEN_RIGHT_BRACE) {
        if (parser.token.kind == TOKEN_EOF) {
            source_fail(
                &parser,
                parser.token.line,
                parser.token.column,
                "unterminated CLI application declaration"
            );
        }
        if (token_is(&parser, "name")) {
            reject_duplicate(&parser, application.has_name, "application name");
            application.has_name = true;
            next_token(&parser);
            take_text(&parser, application.name, false);
        } else if (token_is(&parser, "version")) {
            reject_duplicate(
                &parser,
                application.has_version,
                "application version"
            );
            application.has_version = true;
            next_token(&parser);
            take_text(&parser, application.version, false);
        } else if (token_is(&parser, "about")) {
            reject_duplicate(
                &parser,
                application.has_about,
                "application about"
            );
            application.has_about = true;
            next_token(&parser);
            take_text(&parser, application.about, false);
        } else if (token_is(&parser, "command")) {
            next_token(&parser);
            parse_command(&parser, &application);
        } else {
            source_fail(
                &parser,
                parser.token.line,
                parser.token.column,
                "expected name, version, about, command, or '}'"
            );
        }
    }
    next_token(&parser);
    if (parser.token.kind != TOKEN_EOF) {
        source_fail(
            &parser,
            parser.token.line,
            parser.token.column,
            "trailing tokens after CLI declaration"
        );
    }
    if (!application.has_name ||
        !application.has_version ||
        !application.has_about) {
        source_fail(
            &parser,
            1,
            1,
            "application requires name, version, and about declarations"
        );
    }
    if (application.command_count < 2) {
        source_fail(
            &parser,
            1,
            1,
            "application requires at least two commands"
        );
    }
    return application;
}

static void config_reserve(Config *config, size_t amount) {
    if (amount > MAX_CONFIG - config->length) {
        fail("serialized declaration exceeds 65536-byte profile limit");
    }
    memset(config->bytes + config->length, 0, amount);
    config->length += amount;
}

static void store_u32(unsigned char *destination, uint32_t value) {
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static void config_u32(Config *config, size_t offset, uint32_t value) {
    if (offset > config->length || config->length - offset < 4) {
        fail("internal declaration patch is outside buffer");
    }
    store_u32(config->bytes + offset, value);
}

static uint32_t config_text(Config *config, const char *text) {
    size_t length = strlen(text) + 1;
    if (config->length > UINT32_MAX) fail("config offset overflow");
    uint32_t offset = (uint32_t)config->length;
    config_reserve(config, length);
    memcpy(config->bytes + offset, text, length);
    return offset;
}

static Config serialize_application(const Application *application) {
    Config config;
    memset(&config, 0, sizeof(config));
    config_reserve(
        &config,
        HEADER_SIZE + application->command_count * COMMAND_SIZE
    );
    config_u32(&config, 0, CONFIG_MAGIC);
    config_u32(&config, 20, (uint32_t)application->command_count);
    config_u32(&config, 24, HEADER_SIZE);
    for (size_t command_index = 0;
         command_index < application->command_count;
         ++command_index) {
        const Command *command = &application->commands[command_index];
        size_t command_offset =
            HEADER_SIZE + command_index * COMMAND_SIZE;
        config_u32(&config, command_offset + 8, command->action);
        config_u32(
            &config,
            command_offset + 12,
            (uint32_t)command->position_count
        );
        config_u32(
            &config,
            command_offset + 16,
            (uint32_t)config.length
        );
        config_reserve(
            &config,
            command->position_count * POSITION_SIZE
        );
        config_u32(
            &config,
            command_offset + 20,
            (uint32_t)command->option_count
        );
        config_u32(
            &config,
            command_offset + 24,
            (uint32_t)config.length
        );
        config_reserve(&config, command->option_count * OPTION_SIZE);
    }
    config_u32(&config, 8, config_text(&config, application->name));
    config_u32(&config, 12, config_text(&config, application->version));
    config_u32(&config, 16, config_text(&config, application->about));
    for (size_t command_index = 0;
         command_index < application->command_count;
         ++command_index) {
        const Command *command = &application->commands[command_index];
        size_t command_offset =
            HEADER_SIZE + command_index * COMMAND_SIZE;
        uint32_t position_offset =
            (uint32_t)config.bytes[command_offset + 16] |
            ((uint32_t)config.bytes[command_offset + 17] << 8) |
            ((uint32_t)config.bytes[command_offset + 18] << 16) |
            ((uint32_t)config.bytes[command_offset + 19] << 24);
        uint32_t option_offset =
            (uint32_t)config.bytes[command_offset + 24] |
            ((uint32_t)config.bytes[command_offset + 25] << 8) |
            ((uint32_t)config.bytes[command_offset + 26] << 16) |
            ((uint32_t)config.bytes[command_offset + 27] << 24);
        config_u32(
            &config,
            command_offset,
            config_text(&config, command->name)
        );
        config_u32(
            &config,
            command_offset + 4,
            config_text(&config, command->about)
        );
        for (size_t index = 0; index < command->position_count; ++index) {
            size_t offset = position_offset + index * POSITION_SIZE;
            config_u32(
                &config,
                offset,
                config_text(&config, command->positions[index].name)
            );
            config_u32(
                &config,
                offset + 4,
                config_text(&config, command->positions[index].about)
            );
        }
        for (size_t index = 0; index < command->option_count; ++index) {
            const Option *option = &command->options[index];
            size_t offset = option_offset + index * OPTION_SIZE;
            config_u32(
                &config,
                offset,
                config_text(&config, option->identifier)
            );
            config_u32(
                &config,
                offset + 4,
                config_text(&config, option->long_name)
            );
            config_u32(
                &config,
                offset + 8,
                config_text(&config, option->about)
            );
            config_u32(&config, offset + 12, option->kind);
            if (option->has_default) {
                config_u32(
                    &config,
                    offset + 16,
                    config_text(&config, option->default_value)
                );
            }
        }
    }
    config_u32(&config, 4, (uint32_t)config.length);
    return config;
}

static void patch_u64(
    unsigned char *bytes,
    size_t length,
    size_t offset,
    uint64_t value
) {
    if (offset > length || length - offset < 8) {
        fail("runtime program header patch is outside prefix");
    }
    for (unsigned index = 0; index < 8; ++index) {
        bytes[offset + index] =
            (unsigned char)(value >> (index * 8));
    }
}

static void write_application(const char *path, const Config *config) {
    if (sizeof(CLI_RUNTIME_PREFIX) != CLI_CONFIG_FILE_OFFSET) {
        fail("runtime prefix/config offset mismatch");
    }
    unsigned char *prefix = malloc(sizeof(CLI_RUNTIME_PREFIX));
    if (prefix == NULL) fail("out of memory");
    memcpy(prefix, CLI_RUNTIME_PREFIX, sizeof(CLI_RUNTIME_PREFIX));
    patch_u64(
        prefix,
        sizeof(CLI_RUNTIME_PREFIX),
        CLI_CONFIG_PHDR_OFFSET + 32,
        (uint64_t)config->length
    );
    patch_u64(
        prefix,
        sizeof(CLI_RUNTIME_PREFIX),
        CLI_CONFIG_PHDR_OFFSET + 40,
        (uint64_t)config->length
    );
    FILE *output = fopen(path, "wb");
    if (output == NULL) {
        fprintf(stderr, "kofun cli compiler: cannot create %s: %s\n",
                path, strerror(errno));
        exit(2);
    }
    bool ok =
        fwrite(prefix, 1, sizeof(CLI_RUNTIME_PREFIX), output) ==
            sizeof(CLI_RUNTIME_PREFIX) &&
        fwrite(config->bytes, 1, config->length, output) ==
            config->length;
    if (fclose(output) != 0 || !ok) {
        fail("cannot write complete output ELF");
    }
    free(prefix);
    if (chmod(path, 0755) != 0) {
        fprintf(stderr, "kofun cli compiler: cannot chmod %s: %s\n",
                path, strerror(errno));
        exit(2);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fail("usage: compiler INPUT.kofun OUTPUT");
    }
    size_t source_length = 0;
    char *source = read_source(argv[1], &source_length);
    Application application = parse_application(source, source_length);
    Config config = serialize_application(&application);
    write_application(argv[2], &config);
    free(source);
    return 0;
}
