#include "sha256.h"
#include "visibility_access.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODULE_LIMIT 256u
#define IMPORT_PER_MODULE_LIMIT 256u
#define IMPORT_TOTAL_LIMIT 65536u
#define MODULE_PATH_COMPONENT_LIMIT 64u
#define MODULE_PATH_LIMIT 4096u
#define SOURCE_LIMIT (1024u * 1024u)
#define TOKEN_LIMIT 262144u
#define IDENTIFIER_LIMIT 256u
#define LOGICAL_PATH_LIMIT 512u
#define HOST_PATH_LIMIT 1024u
#define TOP_LEVEL_LIMIT 4096u
#define CONSTRUCTOR_LIMIT 8192u
#define PARAMETER_LIMIT 256u
#define DELIMITER_DEPTH_LIMIT 256u
#define DECLARATION_TOTAL_LIMIT 65536u
#define CALL_LIMIT 65536u
#define LOOKUP_OPERATION_LIMIT UINT64_C(10000000)
#define GRAPH_OPERATION_LIMIT UINT64_C(20000000)

typedef enum {
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_STRING,
    TOKEN_PUNCTUATION,
    TOKEN_ARROW
} TokenKind;

typedef enum {
    DECLARATION_FUNCTION,
    DECLARATION_ADT,
    DECLARATION_CONSTRUCTOR
} DeclarationKind;

typedef enum {
    VISIBILITY_IMPLICIT_PRIVATE,
    VISIBILITY_PRIVATE,
    VISIBILITY_INTERNAL,
    VISIBILITY_PUBLIC
} Visibility;

typedef struct {
    TokenKind kind;
    size_t start;
    size_t end;
    bool line_break_before;
} Token;

typedef struct {
    uint8_t package_id[32];
    uint8_t module_id[32];
    uint8_t file_id[32];
    char declared_module_path[MODULE_PATH_LIMIT + 1u];
    char logical_path[LOGICAL_PATH_LIMIT + 1u];
    char host_path[HOST_PATH_LIMIT + 1u];
    char *source;
    size_t source_length;
    Token *tokens;
    size_t token_count;
    size_t token_capacity;
    size_t top_level_count;
    size_t constructor_count;
    size_t import_count;
} Module;

typedef struct {
    DeclarationKind kind;
    Visibility visibility;
    size_t module_index;
    char name[IDENTIFIER_LIMIT + 1u];
    unsigned namespace_tag;
    uint8_t namespace_id[32];
    uint8_t symbol_id[32];
    size_t start;
    size_t end;
    size_t name_start;
    size_t name_end;
    size_t signature_start;
    size_t signature_end;
    bool has_owner;
    size_t owner_index;
    size_t constructor_index;
    size_t body_token_start;
    size_t body_token_end;
    size_t parameters_token_start;
    size_t parameters_token_end;
    size_t parameter_count;
    bool backend_signature_supported;
} Declaration;

typedef struct {
    size_t importing_module_index;
    size_t target_module_index;
    char path[MODULE_PATH_LIMIT + 1u];
    char local_name[IDENTIFIER_LIMIT + 1u];
    uint8_t import_binding_id[32];
    size_t start;
    size_t end;
    size_t component_count;
    size_t component_start[MODULE_PATH_COMPONENT_LIMIT];
    size_t component_end[MODULE_PATH_COMPONENT_LIMIT];
    bool resolved;
} Import;

typedef struct {
    size_t caller_index;
    size_t callee_index;
    size_t start;
    size_t end;
    size_t full_start;
    size_t full_end;
    size_t qualifier_start;
    size_t qualifier_end;
    size_t argument_count;
    size_t import_index;
    bool qualified;
    KofunAccessResult access;
} Call;

typedef struct {
    Module modules[MODULE_LIMIT];
    size_t module_count;
    Declaration *declarations;
    size_t declaration_count;
    size_t declaration_capacity;
    Call *calls;
    size_t call_count;
    size_t call_capacity;
    Import *imports;
    size_t import_count;
    size_t import_capacity;
    uint8_t namespace_ids[4][32];
    uint64_t lookup_operations;
    uint64_t graph_operations;
    char error[65536];
    bool failed;
} Program;

static Program *comparison_program;

static void set_error(Program *program, const char *code, const char *format, ...) {
    char detail[65000];
    va_list arguments;
    if (program->failed) return;
    va_start(arguments, format);
    if (vsnprintf(detail, sizeof(detail), format, arguments) < 0) {
        detail[0] = '\0';
    }
    va_end(arguments);
    snprintf(program->error, sizeof(program->error), "error[%s]: %s", code, detail);
    program->failed = true;
}

static void bytes_to_hex(const uint8_t *bytes, size_t length, char *output) {
    static const char digits[] = "0123456789abcdef";
    size_t index;
    for (index = 0; index < length; index += 1) {
        output[index * 2u] = digits[bytes[index] >> 4u];
        output[index * 2u + 1u] = digits[bytes[index] & UINT8_C(0x0f)];
    }
    output[length * 2u] = '\0';
}

static int hex_digit(unsigned char byte) {
    if (byte >= '0' && byte <= '9') return (int)(byte - '0');
    if (byte >= 'a' && byte <= 'f') return (int)(byte - 'a') + 10;
    return -1;
}

static bool parse_identity(const char *text, uint8_t output[32]) {
    size_t index;
    if (strlen(text) != 64u) return false;
    for (index = 0; index < 32u; index += 1) {
        int high = hex_digit((unsigned char)text[index * 2u]);
        int low = hex_digit((unsigned char)text[index * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        output[index] = (uint8_t)((unsigned)high * 16u + (unsigned)low);
    }
    return true;
}

static bool logical_path_is_valid(const char *path) {
    const char *cursor = path;
    const char *component = path;
    size_t length = strlen(path);
    if (length == 0 || length > LOGICAL_PATH_LIMIT || path[0] == '/' ||
        strchr(path, '\\') != NULL || strchr(path, '|') != NULL) return false;
    while (*cursor != '\0') {
        unsigned char byte = (unsigned char)*cursor;
        if (byte < 0x20u || byte == 0x7fu) return false;
        if (*cursor == '/') {
            size_t component_length = (size_t)(cursor - component);
            if (component_length == 0 ||
                (component_length == 1 && component[0] == '.') ||
                (component_length == 2 && component[0] == '.' && component[1] == '.')) {
                return false;
            }
            component = cursor + 1;
        }
        cursor += 1;
    }
    return cursor != component && strcmp(component, ".") != 0 && strcmp(component, "..") != 0;
}

static bool read_source(Program *program, Module *module) {
    FILE *input = fopen(module->host_path, "rb");
    long measured;
    size_t read_count;
    int close_status;
    if (input == NULL) {
        set_error(program, "E2S48", "cannot read logical source `%s`", module->logical_path);
        return false;
    }
    if (fseek(input, 0, SEEK_END) != 0 || (measured = ftell(input)) < 0 ||
        fseek(input, 0, SEEK_SET) != 0) {
        fclose(input);
        set_error(program, "E2S48", "cannot measure logical source `%s`", module->logical_path);
        return false;
    }
    if ((unsigned long)measured > SOURCE_LIMIT) {
        fclose(input);
        set_error(program, "E2S55", "source `%s` exceeds %u bytes", module->logical_path, SOURCE_LIMIT);
        return false;
    }
    module->source_length = (size_t)measured;
    module->source = malloc(module->source_length + 1u);
    if (module->source == NULL) {
        fclose(input);
        set_error(program, "E2S56", "allocation failed while reading `%s`", module->logical_path);
        return false;
    }
    read_count = fread(module->source, 1, module->source_length, input);
    close_status = fclose(input);
    if (read_count != module->source_length || close_status != 0) {
        free(module->source);
        module->source = NULL;
        module->source_length = 0;
        set_error(program, "E2S48", "cannot read complete logical source `%s`", module->logical_path);
        return false;
    }
    module->source[module->source_length] = '\0';
    return true;
}

static size_t split_inventory_line(char *line, char *fields[5]) {
    size_t count = 1;
    char *cursor = line;
    fields[0] = line;
    while (*cursor != '\0') {
        if (*cursor == '|') {
            if (count >= 5u) return 6u;
            *cursor = '\0';
            fields[count] = cursor + 1;
            count += 1;
        }
        cursor += 1;
    }
    return count;
}

static bool load_inventory(Program *program, const char *path) {
    FILE *input = fopen(path, "rb");
    char line[4096];
    size_t line_number = 0;
    bool have_package = false;
    uint8_t package_id[32];
    if (input == NULL) {
        set_error(program, "E2S48", "cannot open inventory");
        return false;
    }
    while (fgets(line, sizeof(line), input) != NULL) {
        char *fields[5];
        size_t count;
        size_t length;
        Module *module;
        line_number += 1;
        length = strlen(line);
        if (length > 0 && line[length - 1u] == '\n') line[--length] = '\0';
        if (length > 0 && line[length - 1u] == '\r') line[--length] = '\0';
        if (length == 0 || line[0] == '#') continue;
        if (program->module_count >= MODULE_LIMIT) {
            set_error(program, "E2S55", "inventory exceeds %u modules at line %zu", MODULE_LIMIT, line_number);
            break;
        }
        count = split_inventory_line(line, fields);
        if (count != 5u) {
            set_error(program, "E2S48", "inventory line %zu must contain five pipe-delimited fields", line_number);
            break;
        }
        module = &program->modules[program->module_count];
        memset(module, 0, sizeof(*module));
        if (!parse_identity(fields[0], module->package_id) ||
            !parse_identity(fields[1], module->module_id) ||
            !parse_identity(fields[2], module->file_id)) {
            set_error(program, "E2S48", "inventory line %zu contains a non-canonical 32-byte identity", line_number);
            break;
        }
        if (!logical_path_is_valid(fields[3])) {
            set_error(program, "E2S48", "inventory line %zu has invalid logical path", line_number);
            break;
        }
        if (strlen(fields[4]) == 0 || strlen(fields[4]) > HOST_PATH_LIMIT) {
            set_error(program, "E2S48", "inventory line %zu has invalid source operand", line_number);
            break;
        }
        memcpy(module->logical_path, fields[3], strlen(fields[3]) + 1u);
        memcpy(module->host_path, fields[4], strlen(fields[4]) + 1u);
        if (!have_package) {
            memcpy(package_id, module->package_id, sizeof(package_id));
            have_package = true;
        } else if (memcmp(package_id, module->package_id, sizeof(package_id)) != 0) {
            set_error(program, "E2S48", "inventory line %zu belongs to a different PackageId", line_number);
            break;
        }
        if (!read_source(program, module)) break;
        program->module_count += 1;
    }
    if (!program->failed && ferror(input)) {
        set_error(program, "E2S48", "inventory read failed");
    }
    fclose(input);
    if (!program->failed && program->module_count == 0) {
        set_error(program, "E2S48", "inventory contains no modules");
    }
    return !program->failed;
}

static int compare_modules(const void *left, const void *right) {
    const Module *a = left;
    const Module *b = right;
    int result = memcmp(a->module_id, b->module_id, 32u);
    if (result != 0) return result;
    result = memcmp(a->file_id, b->file_id, 32u);
    if (result != 0) return result;
    return strcmp(a->logical_path, b->logical_path);
}

static bool order_and_validate_inventory(Program *program) {
    size_t index;
    qsort(program->modules, program->module_count, sizeof(program->modules[0]), compare_modules);
    for (index = 1; index < program->module_count; index += 1) {
        if (memcmp(program->modules[index - 1u].module_id,
                program->modules[index].module_id, 32u) == 0) {
            char module_hex[65];
            bytes_to_hex(program->modules[index].module_id, 32u, module_hex);
            set_error(
                program,
                "E2S49",
                "duplicate ModuleId %s for `%s` and `%s`; V1 permits exactly one source per module",
                module_hex,
                program->modules[index - 1u].logical_path,
                program->modules[index].logical_path
            );
            return false;
        }
    }
    return true;
}

static bool add_token(
    Program *program,
    Module *module,
    TokenKind kind,
    size_t start,
    size_t end,
    bool line_break_before
) {
    Token *resized;
    if (module->token_count >= TOKEN_LIMIT) {
        set_error(program, "E2S55", "token count in `%s` exceeds %u at bytes %zu..%zu",
            module->logical_path, TOKEN_LIMIT, start, end);
        return false;
    }
    if (module->token_count == module->token_capacity) {
        size_t capacity = module->token_capacity == 0 ? 256u : module->token_capacity * 2u;
        if (capacity > TOKEN_LIMIT) capacity = TOKEN_LIMIT;
        resized = realloc(module->tokens, capacity * sizeof(*resized));
        if (resized == NULL) {
            set_error(program, "E2S56", "token allocation failed for `%s`", module->logical_path);
            return false;
        }
        module->tokens = resized;
        module->token_capacity = capacity;
    }
    module->tokens[module->token_count] = (Token){
        .kind = kind,
        .start = start,
        .end = end,
        .line_break_before = line_break_before
    };
    module->token_count += 1;
    return true;
}

static bool tokenize(Program *program, Module *module) {
    size_t cursor = 0;
    bool line_break = false;
    while (cursor < module->source_length) {
        size_t start;
        unsigned char byte = (unsigned char)module->source[cursor];
        if (isspace(byte)) {
            if (byte == '\n') line_break = true;
            cursor += 1;
            continue;
        }
        if (module->source[cursor] == '#') {
            while (cursor < module->source_length && module->source[cursor] != '\n') cursor += 1;
            continue;
        }
        start = cursor;
        if (byte >= 0x80u) {
            set_error(program, "E2S50", "non-ASCII bootstrap token in `%s` at bytes %zu..%zu",
                module->logical_path, start, start + 1u);
            return false;
        }
        if (isalpha(byte) || byte == '_') {
            cursor += 1;
            while (cursor < module->source_length) {
                byte = (unsigned char)module->source[cursor];
                if (!isalnum(byte) && byte != '_') break;
                cursor += 1;
            }
            if (cursor - start > IDENTIFIER_LIMIT) {
                set_error(program, "E2S55", "identifier in `%s` exceeds %u bytes at bytes %zu..%zu",
                    module->logical_path, IDENTIFIER_LIMIT, start, cursor);
                return false;
            }
            if (!add_token(program, module, TOKEN_IDENTIFIER, start, cursor, line_break)) return false;
            line_break = false;
            continue;
        }
        if (isdigit(byte)) {
            cursor += 1;
            while (cursor < module->source_length && isdigit((unsigned char)module->source[cursor])) cursor += 1;
            if (!add_token(program, module, TOKEN_INTEGER, start, cursor, line_break)) return false;
            line_break = false;
            continue;
        }
        if (byte == '"') {
            bool escaped = false;
            cursor += 1;
            while (cursor < module->source_length) {
                char current = module->source[cursor];
                if (!escaped && current == '"') {
                    cursor += 1;
                    break;
                }
                if (!escaped && current == '\n') break;
                if (!escaped && current == '\\') escaped = true;
                else escaped = false;
                cursor += 1;
            }
            if (cursor > module->source_length || cursor == 0 || module->source[cursor - 1u] != '"') {
                set_error(program, "E2S50", "unterminated text literal in `%s` at bytes %zu..%zu",
                    module->logical_path, start, cursor);
                return false;
            }
            if (!add_token(program, module, TOKEN_STRING, start, cursor, line_break)) return false;
            line_break = false;
            continue;
        }
        if (byte == '-' && cursor + 1u < module->source_length && module->source[cursor + 1u] == '>') {
            cursor += 2u;
            if (!add_token(program, module, TOKEN_ARROW, start, cursor, line_break)) return false;
            line_break = false;
            continue;
        }
        if (strchr("(){}[],:.=|+-*/<>!", (int)byte) != NULL) {
            cursor += 1;
            if (!add_token(program, module, TOKEN_PUNCTUATION, start, cursor, line_break)) return false;
            line_break = false;
            continue;
        }
        set_error(program, "E2S50", "unsupported byte in `%s` at bytes %zu..%zu",
            module->logical_path, start, start + 1u);
        return false;
    }
    return true;
}

static bool token_equals(const Module *module, const Token *token, const char *text) {
    size_t length = strlen(text);
    return token->end - token->start == length &&
        memcmp(module->source + token->start, text, length) == 0;
}

static bool punctuation_equals(const Module *module, const Token *token, char punctuation) {
    return token->kind == TOKEN_PUNCTUATION && token->end == token->start + 1u &&
        module->source[token->start] == punctuation;
}

static bool copy_identifier(
    Program *program,
    const Module *module,
    const Token *token,
    char output[IDENTIFIER_LIMIT + 1u]
) {
    size_t length;
    if (token->kind != TOKEN_IDENTIFIER) {
        set_error(program, "E2S50", "expected identifier in `%s` at bytes %zu..%zu",
            module->logical_path, token->start, token->end);
        return false;
    }
    length = token->end - token->start;
    if (length == 0 || length > IDENTIFIER_LIMIT) {
        set_error(program, "E2S55", "identifier in `%s` exceeds the bootstrap limit at bytes %zu..%zu",
            module->logical_path, token->start, token->end);
        return false;
    }
    memcpy(output, module->source + token->start, length);
    output[length] = '\0';
    return true;
}

static bool reserve_declaration(Program *program) {
    Declaration *resized;
    if (program->declaration_count >= DECLARATION_TOTAL_LIMIT) {
        set_error(program, "E2S55", "inventory exceeds %u declarations", DECLARATION_TOTAL_LIMIT);
        return false;
    }
    if (program->declaration_count < program->declaration_capacity) return true;
    {
        size_t capacity = program->declaration_capacity == 0 ? 256u : program->declaration_capacity * 2u;
        if (capacity > DECLARATION_TOTAL_LIMIT) capacity = DECLARATION_TOTAL_LIMIT;
        resized = realloc(program->declarations, capacity * sizeof(*resized));
        if (resized == NULL) {
            set_error(program, "E2S56", "declaration allocation failed");
            return false;
        }
        program->declarations = resized;
        program->declaration_capacity = capacity;
    }
    return true;
}

static bool add_declaration(
    Program *program,
    size_t module_index,
    DeclarationKind kind,
    Visibility visibility,
    const Token *name_token,
    size_t start,
    size_t end,
    size_t signature_start,
    size_t signature_end,
    size_t *output_index
) {
    Module *module = &program->modules[module_index];
    Declaration *declaration;
    if (kind == DECLARATION_CONSTRUCTOR) {
        if (module->constructor_count >= CONSTRUCTOR_LIMIT) {
            set_error(program, "E2S55", "module `%s` exceeds %u constructors",
                module->logical_path, CONSTRUCTOR_LIMIT);
            return false;
        }
        module->constructor_count += 1;
    } else {
        if (module->top_level_count >= TOP_LEVEL_LIMIT) {
            set_error(program, "E2S55", "module `%s` exceeds %u top-level declarations",
                module->logical_path, TOP_LEVEL_LIMIT);
            return false;
        }
        module->top_level_count += 1;
    }
    if (!reserve_declaration(program)) return false;
    declaration = &program->declarations[program->declaration_count];
    memset(declaration, 0, sizeof(*declaration));
    declaration->kind = kind;
    declaration->visibility = visibility;
    declaration->module_index = module_index;
    declaration->namespace_tag = kind == DECLARATION_ADT ? 1u : 0u;
    declaration->start = start;
    declaration->end = end;
    declaration->name_start = name_token->start;
    declaration->name_end = name_token->end;
    declaration->signature_start = signature_start;
    declaration->signature_end = signature_end;
    if (!copy_identifier(program, module, name_token, declaration->name)) return false;
    *output_index = program->declaration_count;
    program->declaration_count += 1;
    return true;
}

static bool find_closing(
    Program *program,
    const Module *module,
    size_t opening,
    char open,
    char close,
    size_t *closing
) {
    size_t cursor;
    size_t depth = 0;
    for (cursor = opening; cursor < module->token_count; cursor += 1) {
        if (punctuation_equals(module, &module->tokens[cursor], open)) {
            depth += 1;
            if (depth > DELIMITER_DEPTH_LIMIT) {
                set_error(program, "E2S55", "delimiter depth in `%s` exceeds %u at bytes %zu..%zu",
                    module->logical_path, DELIMITER_DEPTH_LIMIT,
                    module->tokens[cursor].start, module->tokens[cursor].end);
                return false;
            }
        } else if (punctuation_equals(module, &module->tokens[cursor], close)) {
            if (depth == 0) break;
            depth -= 1;
            if (depth == 0) {
                *closing = cursor;
                return true;
            }
        }
    }
    set_error(program, "E2S50", "unclosed `%c` in `%s` at bytes %zu..%zu",
        open, module->logical_path, module->tokens[opening].start, module->tokens[opening].end);
    return false;
}

static bool parse_dotted_line(
    Program *program,
    Module *module,
    size_t first,
    char output[MODULE_PATH_LIMIT + 1u],
    size_t component_start[MODULE_PATH_COMPONENT_LIMIT],
    size_t component_end[MODULE_PATH_COMPONENT_LIMIT],
    size_t *component_count,
    size_t *next,
    const char *code,
    const char *description
) {
    size_t current = first;
    size_t output_length = 0;
    size_t count = 0;
    bool expect_identifier = true;
    if (first >= module->token_count || module->tokens[first].line_break_before) {
        size_t position = first == 0 ? 0 : module->tokens[first - 1u].end;
        set_error(program, code, "%s is missing a path in `%s` at bytes %zu..%zu",
            description, module->logical_path, position, position);
        return false;
    }
    while (current < module->token_count &&
           (current == first || !module->tokens[current].line_break_before)) {
        Token *token = &module->tokens[current];
        if (expect_identifier) {
            size_t length;
            if (token->kind != TOKEN_IDENTIFIER) {
                set_error(program, code, "malformed %s in `%s` at bytes %zu..%zu; expected an identifier component",
                    description, module->logical_path, token->start, token->end);
                return false;
            }
            if (count >= MODULE_PATH_COMPONENT_LIMIT) {
                set_error(program, "E2S55", "%s in `%s` exceeds %u components at bytes %zu..%zu",
                    description, module->logical_path, MODULE_PATH_COMPONENT_LIMIT,
                    module->tokens[first].start, token->end);
                return false;
            }
            length = token->end - token->start;
            if (output_length != 0) {
                if (output_length >= MODULE_PATH_LIMIT) {
                    set_error(program, "E2S55", "%s in `%s` exceeds %u bytes at bytes %zu..%zu",
                        description, module->logical_path, MODULE_PATH_LIMIT,
                        module->tokens[first].start, token->end);
                    return false;
                }
                output[output_length++] = '.';
            }
            if (length > MODULE_PATH_LIMIT - output_length) {
                set_error(program, "E2S55", "%s in `%s` exceeds %u bytes at bytes %zu..%zu",
                    description, module->logical_path, MODULE_PATH_LIMIT,
                    module->tokens[first].start, token->end);
                return false;
            }
            memcpy(output + output_length, module->source + token->start, length);
            output_length += length;
            if (component_start != NULL) component_start[count] = token->start;
            if (component_end != NULL) component_end[count] = token->end;
            count += 1u;
        } else if (!punctuation_equals(module, token, '.')) {
            set_error(program, code, "malformed %s in `%s` at bytes %zu..%zu; only dotted identifiers are supported",
                description, module->logical_path, token->start, token->end);
            return false;
        }
        expect_identifier = !expect_identifier;
        current += 1u;
    }
    if (count == 0 || expect_identifier) {
        size_t end = current == first ? module->tokens[first - 1u].end : module->tokens[current - 1u].end;
        set_error(program, code, "malformed %s in `%s` at bytes %zu..%zu; path cannot end with `.`",
            description, module->logical_path, module->tokens[first].start, end);
        return false;
    }
    output[output_length] = '\0';
    *component_count = count;
    *next = current;
    return true;
}

static bool parse_module_header(Program *program, Module *module, size_t *cursor) {
    size_t ignored_start[MODULE_PATH_COMPONENT_LIMIT];
    size_t ignored_end[MODULE_PATH_COMPONENT_LIMIT];
    size_t component_count;
    if (module->token_count == 0 || !token_equals(module, &module->tokens[0], "module")) {
        *cursor = 0;
        return true;
    }
    if (!parse_dotted_line(
            program, module, 1u, module->declared_module_path,
            ignored_start, ignored_end, &component_count, cursor,
            "E2S50", "module header"
        )) return false;
    return true;
}

static bool reserve_import(Program *program, Module *module) {
    Import *resized;
    if (module->import_count >= IMPORT_PER_MODULE_LIMIT) {
        set_error(program, "E2S55", "module `%s` exceeds %u imports",
            module->logical_path, IMPORT_PER_MODULE_LIMIT);
        return false;
    }
    if (program->import_count >= IMPORT_TOTAL_LIMIT) {
        set_error(program, "E2S55", "inventory exceeds %u import edges", IMPORT_TOTAL_LIMIT);
        return false;
    }
    if (program->import_count < program->import_capacity) return true;
    {
        size_t capacity = program->import_capacity == 0 ? 256u : program->import_capacity * 2u;
        if (capacity > IMPORT_TOTAL_LIMIT) capacity = IMPORT_TOTAL_LIMIT;
        resized = realloc(program->imports, capacity * sizeof(*resized));
        if (resized == NULL) {
            set_error(program, "E2S56", "import allocation failed");
            return false;
        }
        program->imports = resized;
        program->import_capacity = capacity;
    }
    return true;
}

static bool parse_import(
    Program *program,
    size_t module_index,
    size_t *cursor
) {
    Module *module = &program->modules[module_index];
    Import *binding;
    size_t next;
    size_t component_count;
    if (!reserve_import(program, module)) return false;
    binding = &program->imports[program->import_count];
    memset(binding, 0, sizeof(*binding));
    binding->importing_module_index = module_index;
    binding->target_module_index = SIZE_MAX;
    binding->start = module->tokens[*cursor].start;
    if (!parse_dotted_line(
            program, module, *cursor + 1u, binding->path,
            binding->component_start, binding->component_end,
            &component_count, &next, "E2S57", "qualified import"
        )) return false;
    binding->component_count = component_count;
    binding->end = binding->component_end[component_count - 1u];
    {
        size_t name_start = binding->component_start[component_count - 1u];
        size_t name_length = binding->component_end[component_count - 1u] - name_start;
        memcpy(binding->local_name, module->source + name_start, name_length);
        binding->local_name[name_length] = '\0';
    }
    module->import_count += 1u;
    program->import_count += 1u;
    *cursor = next;
    return true;
}

static bool parse_function(
    Program *program,
    size_t module_index,
    size_t declaration_start,
    Visibility visibility,
    size_t *cursor
) {
    Module *module = &program->modules[module_index];
    size_t current = *cursor + 1u;
    size_t name_index;
    size_t parameters_open;
    size_t parameters_close;
    size_t body_open;
    size_t body_close;
    size_t parameter_count = 0;
    size_t declaration_index;
    bool backend_signature_supported = true;
    if (current >= module->token_count || module->tokens[current].kind != TOKEN_IDENTIFIER) {
        set_error(program, "E2S52", "function name is missing in `%s` at bytes %zu..%zu",
            module->logical_path, module->tokens[*cursor].start, module->tokens[*cursor].end);
        return false;
    }
    name_index = current++;
    if (current >= module->token_count || !punctuation_equals(module, &module->tokens[current], '(')) {
        set_error(program, "E2S52", "function `%.*s` is missing `(` in `%s` at bytes %zu..%zu",
            (int)(module->tokens[name_index].end - module->tokens[name_index].start),
            module->source + module->tokens[name_index].start,
            module->logical_path, module->tokens[name_index].start, module->tokens[name_index].end);
        return false;
    }
    parameters_open = current;
    if (!find_closing(program, module, current, '(', ')', &parameters_close)) return false;
    if (parameters_close > current + 1u) {
        size_t scan = current + 1u;
        while (scan < parameters_close) {
            if (module->tokens[scan].kind != TOKEN_IDENTIFIER ||
                scan + 2u >= parameters_close ||
                !punctuation_equals(module, &module->tokens[scan + 1u], ':') ||
                module->tokens[scan + 2u].kind != TOKEN_IDENTIFIER) {
                set_error(program, "E2S52",
                    "malformed parameter in `%s` at bytes %zu..%zu",
                    module->logical_path, module->tokens[scan].start,
                    module->tokens[scan].end);
                return false;
            }
            if (!token_equals(module, &module->tokens[scan + 2u], "Int")) {
                backend_signature_supported = false;
            }
            parameter_count += 1u;
            if (parameter_count > PARAMETER_LIMIT) {
                set_error(program, "E2S55", "function in `%s` exceeds %u parameters at bytes %zu..%zu",
                    module->logical_path, PARAMETER_LIMIT,
                    module->tokens[current].start, module->tokens[parameters_close].end);
                return false;
            }
            scan += 3u;
            if (scan < parameters_close) {
                if (!punctuation_equals(module, &module->tokens[scan], ',')) {
                    set_error(program, "E2S52",
                        "parameters in `%s` require `,` at bytes %zu..%zu",
                        module->logical_path, module->tokens[scan].start,
                        module->tokens[scan].end);
                    return false;
                }
                scan += 1u;
                if (scan == parameters_close) {
                    set_error(program, "E2S52",
                        "trailing parameter comma is outside #111 in `%s` at bytes %zu..%zu",
                        module->logical_path, module->tokens[scan - 1u].start,
                        module->tokens[scan - 1u].end);
                    return false;
                }
            }
        }
    }
    current = parameters_close + 1u;
    if (current >= module->token_count || module->tokens[current].kind != TOKEN_ARROW) {
        set_error(program, "E2S52", "function signature in `%s` is missing `->` at bytes %zu..%zu",
            module->logical_path, module->tokens[name_index].start, module->tokens[name_index].end);
        return false;
    }
    current += 1;
    if (current >= module->token_count || module->tokens[current].kind != TOKEN_IDENTIFIER) {
        set_error(program, "E2S52", "function return type is missing in `%s` at bytes %zu..%zu",
            module->logical_path, module->tokens[name_index].start, module->tokens[name_index].end);
        return false;
    }
    if (!token_equals(module, &module->tokens[current], "Int")) {
        backend_signature_supported = false;
    }
    current += 1;
    if (current >= module->token_count || !punctuation_equals(module, &module->tokens[current], '{')) {
        set_error(program, "E2S52", "function body is missing in `%s` at bytes %zu..%zu",
            module->logical_path, module->tokens[name_index].start, module->tokens[name_index].end);
        return false;
    }
    body_open = current;
    if (!find_closing(program, module, body_open, '{', '}', &body_close)) return false;
    if (!add_declaration(
            program, module_index, DECLARATION_FUNCTION, visibility,
            &module->tokens[name_index], declaration_start,
            module->tokens[body_close].end, declaration_start,
            module->tokens[body_open].start, &declaration_index
        )) return false;
    program->declarations[declaration_index].body_token_start = body_open + 1u;
    program->declarations[declaration_index].body_token_end = body_close;
    program->declarations[declaration_index].parameters_token_start = parameters_open + 1u;
    program->declarations[declaration_index].parameters_token_end = parameters_close;
    program->declarations[declaration_index].parameter_count = parameter_count;
    program->declarations[declaration_index].backend_signature_supported = backend_signature_supported;
    *cursor = body_close + 1u;
    return true;
}

static bool parse_adt(
    Program *program,
    size_t module_index,
    size_t declaration_start,
    Visibility visibility,
    size_t *cursor
) {
    Module *module = &program->modules[module_index];
    size_t current = *cursor + 1u;
    size_t name_index;
    size_t adt_index;
    size_t local_index = 0;
    size_t last_end;
    if (current >= module->token_count || module->tokens[current].kind != TOKEN_IDENTIFIER) {
        set_error(program, "E2S50", "type name is missing in `%s` at bytes %zu..%zu",
            module->logical_path, module->tokens[*cursor].start, module->tokens[*cursor].end);
        return false;
    }
    name_index = current++;
    if (current >= module->token_count || !punctuation_equals(module, &module->tokens[current], '=')) {
        set_error(program, "E2S50", "type declaration in `%s` is missing `=` at bytes %zu..%zu",
            module->logical_path, module->tokens[name_index].start, module->tokens[name_index].end);
        return false;
    }
    current += 1;
    if (current >= module->token_count || !punctuation_equals(module, &module->tokens[current], '|')) {
        set_error(program, "E2S50", "type declaration in `%s` requires a leading `|` at bytes %zu..%zu",
            module->logical_path, module->tokens[name_index].start, module->tokens[name_index].end);
        return false;
    }
    if (!add_declaration(
            program, module_index, DECLARATION_ADT, visibility,
            &module->tokens[name_index], declaration_start,
            module->tokens[name_index].end, declaration_start,
            module->tokens[name_index].end, &adt_index
        )) return false;
    last_end = module->tokens[name_index].end;
    while (current < module->token_count && punctuation_equals(module, &module->tokens[current], '|')) {
        size_t constructor_name;
        size_t constructor_index;
        size_t constructor_end;
        size_t payload_count = 0;
        current += 1;
        if (current >= module->token_count || module->tokens[current].kind != TOKEN_IDENTIFIER) {
            set_error(program, "E2S50", "constructor name is missing in `%s` at bytes %zu..%zu",
                module->logical_path, module->tokens[current - 1u].start, module->tokens[current - 1u].end);
            return false;
        }
        constructor_name = current++;
        constructor_end = module->tokens[constructor_name].end;
        if (current < module->token_count && punctuation_equals(module, &module->tokens[current], '(')) {
            size_t close;
            if (!find_closing(program, module, current, '(', ')', &close)) return false;
            if (close != current + 4u ||
                module->tokens[current + 1u].kind != TOKEN_IDENTIFIER ||
                !punctuation_equals(module, &module->tokens[current + 2u], ':') ||
                !token_equals(module, &module->tokens[current + 3u], "Int")) {
                set_error(program, "E2S50", "constructor payload in `%s` must be exactly `(name: Int)` at bytes %zu..%zu",
                    module->logical_path, module->tokens[current].start, module->tokens[close].end);
                return false;
            }
            payload_count = 1u;
            constructor_end = module->tokens[close].end;
            current = close + 1u;
        }
        if (!add_declaration(
                program, module_index, DECLARATION_CONSTRUCTOR, visibility,
                &module->tokens[constructor_name], module->tokens[constructor_name].start,
                constructor_end, module->tokens[constructor_name].start,
                constructor_end, &constructor_index
            )) return false;
        program->declarations[constructor_index].has_owner = true;
        program->declarations[constructor_index].owner_index = adt_index;
        program->declarations[constructor_index].constructor_index = local_index;
        program->declarations[constructor_index].parameter_count = payload_count;
        local_index += 1u;
        last_end = constructor_end;
    }
    if (local_index < 2u) {
        set_error(program, "E2S50", "type `%s` in `%s` requires at least two constructors at bytes %zu..%zu",
            program->declarations[adt_index].name, module->logical_path,
            module->tokens[name_index].start, last_end);
        return false;
    }
    program->declarations[adt_index].end = last_end;
    program->declarations[adt_index].signature_end = last_end;
    *cursor = current;
    return true;
}

static bool collect_module(Program *program, size_t module_index) {
    Module *module = &program->modules[module_index];
    size_t cursor;
    if (!tokenize(program, module) || !parse_module_header(program, module, &cursor)) return false;
    while (cursor < module->token_count && token_equals(module, &module->tokens[cursor], "import")) {
        if (!parse_import(program, module_index, &cursor)) return false;
    }
    while (cursor < module->token_count) {
        size_t declaration_start = module->tokens[cursor].start;
        Visibility visibility = VISIBILITY_IMPLICIT_PRIVATE;
        if (token_equals(module, &module->tokens[cursor], "pub") ||
            token_equals(module, &module->tokens[cursor], "internal") ||
            token_equals(module, &module->tokens[cursor], "private")) {
            if (token_equals(module, &module->tokens[cursor], "pub")) visibility = VISIBILITY_PUBLIC;
            else if (token_equals(module, &module->tokens[cursor], "internal")) visibility = VISIBILITY_INTERNAL;
            else visibility = VISIBILITY_PRIVATE;
            cursor += 1;
            if (cursor >= module->token_count) {
                set_error(program, "E2S50", "visibility modifier without declaration in `%s` at bytes %zu..%zu",
                    module->logical_path, declaration_start, module->source_length);
                return false;
            }
            if (punctuation_equals(module, &module->tokens[cursor], '(')) {
                set_error(program, "E2S54", "restricted visibility is outside #111 in `%s` at bytes %zu..%zu",
                    module->logical_path, declaration_start, module->tokens[cursor].end);
                return false;
            }
        }
        if (token_equals(module, &module->tokens[cursor], "fn")) {
            if (!parse_function(program, module_index, declaration_start, visibility, &cursor)) return false;
        } else if (token_equals(module, &module->tokens[cursor], "type")) {
            if (!parse_adt(program, module_index, declaration_start, visibility, &cursor)) return false;
        } else if (token_equals(module, &module->tokens[cursor], "import")) {
            set_error(program, "E2S58", "import in `%s` must precede every ordinary declaration at bytes %zu..%zu; move it below the module header",
                module->logical_path, module->tokens[cursor].start, module->tokens[cursor].end);
            return false;
        } else if (token_equals(module, &module->tokens[cursor], "from") ||
            token_equals(module, &module->tokens[cursor], "use")) {
            set_error(program, "E2S57", "unsupported import form in `%s` at bytes %zu..%zu; #113 accepts only `import a.b`",
                module->logical_path, module->tokens[cursor].start, module->tokens[cursor].end);
            return false;
        } else {
            set_error(program, "E2S50", "unsupported top-level declaration in `%s` at bytes %zu..%zu",
                module->logical_path, module->tokens[cursor].start, module->tokens[cursor].end);
            return false;
        }
    }
    return true;
}

static void store_u16be(uint8_t output[2], uint16_t value) {
    output[0] = (uint8_t)(value >> 8u);
    output[1] = (uint8_t)value;
}

static void store_u32be(uint8_t output[4], uint32_t value) {
    output[0] = (uint8_t)(value >> 24u);
    output[1] = (uint8_t)(value >> 16u);
    output[2] = (uint8_t)(value >> 8u);
    output[3] = (uint8_t)value;
}

static void framed_hash(
    const char *domain,
    const uint8_t *payload,
    size_t payload_length,
    uint8_t digest[32]
) {
    KofunSha256 context;
    uint8_t u16[2];
    uint8_t u32[4];
    static const uint8_t prefix[6] = { 'K', 'O', 'F', 'U', 'N', 0 };
    size_t domain_length = strlen(domain);
    store_u16be(u16, (uint16_t)domain_length);
    store_u32be(u32, (uint32_t)payload_length);
    kofun_sha256_init(&context);
    kofun_sha256_update(&context, prefix, sizeof(prefix));
    kofun_sha256_update(&context, u16, sizeof(u16));
    kofun_sha256_update(&context, (const uint8_t *)domain, domain_length);
    kofun_sha256_update(&context, u32, sizeof(u32));
    kofun_sha256_update(&context, payload, payload_length);
    kofun_sha256_finish(&context, digest);
}

static void hash_field(
    KofunSha256 *context,
    uint16_t tag,
    const uint8_t *value,
    size_t length
) {
    uint8_t u16[2];
    uint8_t u32[4];
    store_u16be(u16, tag);
    store_u32be(u32, (uint32_t)length);
    kofun_sha256_update(context, u16, sizeof(u16));
    kofun_sha256_update(context, u32, sizeof(u32));
    kofun_sha256_update(context, value, length);
}

static const char *kind_name(DeclarationKind kind) {
    switch (kind) {
        case DECLARATION_FUNCTION: return "function";
        case DECLARATION_ADT: return "adt";
        case DECLARATION_CONSTRUCTOR: return "constructor";
    }
    return "invalid";
}

static const char *namespace_name(unsigned tag) {
    switch (tag) {
        case 0: return "value";
        case 1: return "type";
        case 2: return "module";
        case 3: return "meta";
    }
    return "invalid";
}

static const char *visibility_name(Visibility visibility) {
    switch (visibility) {
        case VISIBILITY_IMPLICIT_PRIVATE: return "private(implicit)";
        case VISIBILITY_PRIVATE: return "private(explicit)";
        case VISIBILITY_INTERNAL: return "internal";
        case VISIBILITY_PUBLIC: return "pub";
    }
    return "invalid";
}

static void compute_symbol_hash(
    const uint8_t module_id[32],
    const uint8_t namespace_id[32],
    const char *kind,
    const char *name,
    uint8_t digest[32]
) {
    const char *domain = "kofun.id.symbol/v1";
    size_t domain_length = strlen(domain);
    size_t kind_length = strlen(kind);
    size_t name_length = strlen(name);
    size_t payload_length = 88u + kind_length + name_length;
    uint8_t prefix[6] = { 'K', 'O', 'F', 'U', 'N', 0 };
    uint8_t u16[2];
    uint8_t u32[4];
    KofunSha256 context;
    store_u16be(u16, (uint16_t)domain_length);
    store_u32be(u32, (uint32_t)payload_length);
    kofun_sha256_init(&context);
    kofun_sha256_update(&context, prefix, sizeof(prefix));
    kofun_sha256_update(&context, u16, sizeof(u16));
    kofun_sha256_update(&context, (const uint8_t *)domain, domain_length);
    kofun_sha256_update(&context, u32, sizeof(u32));
    hash_field(&context, UINT16_C(0x8001), module_id, 32u);
    hash_field(&context, UINT16_C(0x8002), namespace_id, 32u);
    hash_field(&context, UINT16_C(0x8003), (const uint8_t *)kind, kind_length);
    hash_field(&context, UINT16_C(0x8004), (const uint8_t *)name, name_length);
    kofun_sha256_finish(&context, digest);
}

static void compute_identities(Program *program) {
    static const char *payloads[4] = {
        "kofun.namespace-id/v1\ntag=0\nname=value\n",
        "kofun.namespace-id/v1\ntag=1\nname=type\n",
        "kofun.namespace-id/v1\ntag=2\nname=module\n",
        "kofun.namespace-id/v1\ntag=3\nname=meta\n"
    };
    size_t index;
    for (index = 0; index < 4u; index += 1) {
        framed_hash("kofun.id.namespace/v1", (const uint8_t *)payloads[index],
            strlen(payloads[index]), program->namespace_ids[index]);
    }
    for (index = 0; index < program->declaration_count; index += 1) {
        Declaration *declaration = &program->declarations[index];
        Module *module = &program->modules[declaration->module_index];
        memcpy(declaration->namespace_id, program->namespace_ids[declaration->namespace_tag], 32u);
        compute_symbol_hash(module->module_id, declaration->namespace_id,
            kind_name(declaration->kind), declaration->name, declaration->symbol_id);
    }
}

static void compute_import_hash(
    const Program *program,
    Import *binding
) {
    const char *domain = "kofun.id.import-binding/v1";
    const char *form = "qualified-module-v1";
    const Module *importer = &program->modules[binding->importing_module_index];
    const Module *target = &program->modules[binding->target_module_index];
    size_t domain_length = strlen(domain);
    size_t name_length = strlen(binding->local_name);
    size_t form_length = strlen(form);
    size_t payload_length = 36u + 32u + 32u + 32u + name_length + 32u + form_length;
    uint8_t prefix[6] = { 'K', 'O', 'F', 'U', 'N', 0 };
    uint8_t u16[2];
    uint8_t u32[4];
    KofunSha256 context;
    store_u16be(u16, (uint16_t)domain_length);
    store_u32be(u32, (uint32_t)payload_length);
    kofun_sha256_init(&context);
    kofun_sha256_update(&context, prefix, sizeof(prefix));
    kofun_sha256_update(&context, u16, sizeof(u16));
    kofun_sha256_update(&context, (const uint8_t *)domain, domain_length);
    kofun_sha256_update(&context, u32, sizeof(u32));
    hash_field(&context, UINT16_C(0x8001), importer->module_id, 32u);
    hash_field(&context, UINT16_C(0x8002), importer->file_id, 32u);
    hash_field(&context, UINT16_C(0x8003), program->namespace_ids[2], 32u);
    hash_field(&context, UINT16_C(0x8004), (const uint8_t *)binding->local_name, name_length);
    hash_field(&context, UINT16_C(0x8005), target->module_id, 32u);
    hash_field(&context, UINT16_C(0x8006), (const uint8_t *)form, form_length);
    kofun_sha256_finish(&context, binding->import_binding_id);
}

static int compare_module_path_indices(const void *left, const void *right) {
    size_t a = *(const size_t *)left;
    size_t b = *(const size_t *)right;
    int result = strcmp(
        comparison_program->modules[a].declared_module_path,
        comparison_program->modules[b].declared_module_path
    );
    if (result != 0) return result;
    result = memcmp(comparison_program->modules[a].module_id,
        comparison_program->modules[b].module_id, 32u);
    if (result != 0) return result;
    return a < b ? -1 : a != b;
}

static bool resolve_imports(Program *program) {
    size_t module_path_indices[MODULE_LIMIT];
    size_t module_index;
    size_t import_index;
    if (program->import_count == 0) return true;
    for (module_index = 0; module_index < program->module_count; module_index += 1) {
        Module *module = &program->modules[module_index];
        if (module->declared_module_path[0] == '\0') {
            set_error(program, "E2S57", "module `%s` has imports but no explicit module header",
                module->logical_path);
            return false;
        }
        module_path_indices[module_index] = module_index;
    }
    comparison_program = program;
    qsort(module_path_indices, program->module_count,
        sizeof(module_path_indices[0]), compare_module_path_indices);
    for (module_index = 1; module_index < program->module_count; module_index += 1u) {
        Module *left_module = &program->modules[module_path_indices[module_index - 1u]];
        Module *right_module = &program->modules[module_path_indices[module_index]];
        if (strcmp(left_module->declared_module_path, right_module->declared_module_path) == 0) {
            char left[65];
            char right[65];
            bytes_to_hex(left_module->module_id, 32u, left);
            bytes_to_hex(right_module->module_id, 32u, right);
            set_error(program, "E2S57", "declared module path `%s` maps to both ModuleId %s and %s",
                left_module->declared_module_path, left, right);
            return false;
        }
    }
    for (import_index = 0; import_index < program->import_count; import_index += 1u) {
        Import *binding = &program->imports[import_index];
        Module *importer = &program->modules[binding->importing_module_index];
        size_t target_index = SIZE_MAX;
        size_t low = 0;
        size_t high = program->module_count;
        size_t prior;
        while (low < high) {
            size_t middle = low + (high - low) / 2u;
            size_t candidate = module_path_indices[middle];
            int order;
            program->lookup_operations += 1u;
            if (program->lookup_operations > LOOKUP_OPERATION_LIMIT) {
                set_error(program, "E2S55", "module-path lookup exceeds %llu operations",
                    (unsigned long long)LOOKUP_OPERATION_LIMIT);
                return false;
            }
            order = strcmp(binding->path, program->modules[candidate].declared_module_path);
            if (order == 0) {
                target_index = candidate;
                break;
            }
            if (order < 0) high = middle;
            else low = middle + 1u;
        }
        if (target_index == SIZE_MAX) {
            set_error(program, "E2S59", "local module `%s` imported by `%s` does not exist at bytes %zu..%zu; add it to this PackageModuleIndex",
                binding->path, importer->logical_path, binding->start, binding->end);
            return false;
        }
        if (target_index == binding->importing_module_index) {
            set_error(program, "E2S59", "module `%s` imports itself at bytes %zu..%zu; remove the self import",
                binding->path, binding->start, binding->end);
            return false;
        }
        binding->target_module_index = target_index;
        binding->resolved = true;
        compute_import_hash(program, binding);
        for (prior = 0; prior < import_index; prior += 1u) {
            Import *earlier = &program->imports[prior];
            if (earlier->importing_module_index != binding->importing_module_index) continue;
            if (earlier->target_module_index == binding->target_module_index) {
                set_error(program, "E2S60", "duplicate import `%s` in `%s` at bytes %zu..%zu; first import is bytes %zu..%zu",
                    binding->path, importer->logical_path, binding->start, binding->end,
                    earlier->start, earlier->end);
                return false;
            }
            if (strcmp(earlier->local_name, binding->local_name) == 0) {
                set_error(program, "E2S60", "module qualifier `%s` in `%s` is ambiguous between `%s` at bytes %zu..%zu and `%s` at bytes %zu..%zu; aliases are tracked by #283",
                    binding->local_name, importer->logical_path,
                    earlier->path, earlier->start, earlier->end,
                    binding->path, binding->start, binding->end);
                return false;
            }
        }
    }
    return true;
}

static int compare_import_indices(const void *left, const void *right) {
    size_t a_index = *(const size_t *)left;
    size_t b_index = *(const size_t *)right;
    const Import *a = &comparison_program->imports[a_index];
    const Import *b = &comparison_program->imports[b_index];
    const Module *a_importer = &comparison_program->modules[a->importing_module_index];
    const Module *b_importer = &comparison_program->modules[b->importing_module_index];
    int result = memcmp(a_importer->module_id, b_importer->module_id, 32u);
    if (result != 0) return result;
    result = memcmp(comparison_program->modules[a->target_module_index].module_id,
        comparison_program->modules[b->target_module_index].module_id, 32u);
    if (result != 0) return result;
    result = memcmp(a->import_binding_id, b->import_binding_id, 32u);
    if (result != 0) return result;
    if (a->start != b->start) return a->start < b->start ? -1 : 1;
    return a_index < b_index ? -1 : a_index != b_index;
}

static bool cycle_sequence_less(
    const size_t *left,
    const size_t *right,
    size_t length
) {
    size_t index;
    for (index = 0; index < length; index += 1u) {
        int order = memcmp(
            comparison_program->modules[left[index]].module_id,
            comparison_program->modules[right[index]].module_id,
            32u
        );
        if (order != 0) return order < 0;
    }
    return false;
}

static bool reject_import_cycles(Program *program) {
    size_t *ordered_imports;
    size_t *edge_matrix;
    size_t offsets[MODULE_LIMIT + 1u];
    size_t best_cycle[MODULE_LIMIT];
    size_t best_length = SIZE_MAX;
    size_t start;
    if (program->import_count == 0) return true;
    ordered_imports = malloc(program->import_count * sizeof(*ordered_imports));
    edge_matrix = malloc(program->module_count * program->module_count * sizeof(*edge_matrix));
    if (ordered_imports == NULL || edge_matrix == NULL) {
        free(ordered_imports);
        free(edge_matrix);
        set_error(program, "E2S56", "cycle adjacency allocation failed");
        return false;
    }
    for (start = 0; start < program->module_count * program->module_count; start += 1u) {
        edge_matrix[start] = SIZE_MAX;
    }
    for (start = 0; start < program->import_count; start += 1u) ordered_imports[start] = start;
    comparison_program = program;
    qsort(ordered_imports, program->import_count, sizeof(*ordered_imports), compare_import_indices);
    memset(offsets, 0, sizeof(offsets));
    for (start = 0; start < program->import_count; start += 1u) {
        Import *edge = &program->imports[ordered_imports[start]];
        offsets[edge->importing_module_index + 1u] += 1u;
        edge_matrix[edge->importing_module_index * program->module_count +
            edge->target_module_index] = ordered_imports[start];
    }
    for (start = 1; start <= program->module_count; start += 1u) {
        offsets[start] += offsets[start - 1u];
    }
    for (start = 0; start < program->module_count; start += 1u) {
        size_t distance[MODULE_LIMIT];
        size_t parent[MODULE_LIMIT];
        size_t queue[MODULE_LIMIT];
        size_t head = 0;
        size_t tail = 0;
        size_t node;
        size_t index;
        for (node = 0; node < program->module_count; node += 1u) {
            distance[node] = SIZE_MAX;
            parent[node] = SIZE_MAX;
        }
        distance[start] = 0;
        queue[tail++] = start;
        while (head < tail) {
            size_t from = queue[head++];
            for (index = offsets[from]; index < offsets[from + 1u]; index += 1u) {
                const Import *edge = &program->imports[ordered_imports[index]];
                size_t to;
                program->graph_operations += 1u;
                if (program->graph_operations > GRAPH_OPERATION_LIMIT) {
                    free(ordered_imports);
                    free(edge_matrix);
                    set_error(program, "E2S55", "import graph exceeds %llu checked operations",
                        (unsigned long long)GRAPH_OPERATION_LIMIT);
                    return false;
                }
                if (edge->importing_module_index != from) continue;
                to = edge->target_module_index;
                if (to < start || to == start || distance[to] != SIZE_MAX) continue;
                distance[to] = distance[from] + 1u;
                parent[to] = from;
                queue[tail++] = to;
            }
        }
        for (node = start + 1u; node < program->module_count; node += 1u) {
            size_t candidate[MODULE_LIMIT];
            size_t cursor;
            size_t length;
            if (distance[node] == SIZE_MAX ||
                edge_matrix[node * program->module_count + start] == SIZE_MAX) continue;
            length = distance[node] + 1u;
            candidate[length - 1u] = node;
            cursor = node;
            while (cursor != start) {
                size_t position = distance[cursor] - 1u;
                cursor = parent[cursor];
                candidate[position] = cursor;
            }
            if (length < best_length || (length == best_length &&
                    cycle_sequence_less(candidate, best_cycle, length))) {
                memcpy(best_cycle, candidate, length * sizeof(candidate[0]));
                best_length = length;
            }
        }
    }
    free(ordered_imports);
    if (best_length != SIZE_MAX) {
        char detail[64000];
        size_t used = 0;
        size_t index;
        used += (size_t)snprintf(detail + used, sizeof(detail) - used,
            "canonical shortest import cycle (%zu edges): ", best_length);
        for (index = 0; index < best_length; index += 1u) {
            size_t from = best_cycle[index];
            size_t to = best_cycle[(index + 1u) % best_length];
            size_t edge_index = edge_matrix[from * program->module_count + to];
            char module_hex[65];
            const Import *edge = &program->imports[edge_index];
            bytes_to_hex(program->modules[from].module_id, 32u, module_hex);
            if (used < sizeof(detail)) {
                int written = snprintf(detail + used, sizeof(detail) - used,
                    "%s%s via `%s` bytes %zu..%zu",
                    index == 0 ? "" : " -> ", module_hex,
                    edge->path, edge->start, edge->end);
                if (written > 0) used += (size_t)written;
            }
        }
        if (used < sizeof(detail)) {
            char start_hex[65];
            bytes_to_hex(program->modules[best_cycle[0]].module_id, 32u, start_hex);
            (void)snprintf(detail + used, sizeof(detail) - used,
                " -> %s; remove one participating import", start_hex);
        }
        set_error(program, "E2S61", "%s", detail);
        free(edge_matrix);
        return false;
    }
    free(edge_matrix);
    return true;
}

static int compare_duplicate_indices(const void *left, const void *right) {
    size_t a_index = *(const size_t *)left;
    size_t b_index = *(const size_t *)right;
    const Declaration *a = &comparison_program->declarations[a_index];
    const Declaration *b = &comparison_program->declarations[b_index];
    const Module *a_module = &comparison_program->modules[a->module_index];
    const Module *b_module = &comparison_program->modules[b->module_index];
    int result = memcmp(a_module->module_id, b_module->module_id, 32u);
    if (result != 0) return result;
    if (a->namespace_tag != b->namespace_tag) return a->namespace_tag < b->namespace_tag ? -1 : 1;
    result = strcmp(a->name, b->name);
    if (result != 0) return result;
    result = memcmp(a->symbol_id, b->symbol_id, 32u);
    if (result != 0) return result;
    result = strcmp(a_module->logical_path, b_module->logical_path);
    if (result != 0) return result;
    if (a->name_start != b->name_start) return a->name_start < b->name_start ? -1 : 1;
    return a_index < b_index ? -1 : a_index != b_index;
}

static bool validate_duplicates(Program *program) {
    size_t *indices;
    size_t index;
    indices = malloc(program->declaration_count * sizeof(*indices));
    if (indices == NULL && program->declaration_count != 0) {
        set_error(program, "E2S56", "duplicate-index allocation failed");
        return false;
    }
    for (index = 0; index < program->declaration_count; index += 1) indices[index] = index;
    comparison_program = program;
    qsort(indices, program->declaration_count, sizeof(*indices), compare_duplicate_indices);
    for (index = 1; index < program->declaration_count; index += 1) {
        Declaration *first = &program->declarations[indices[index - 1u]];
        Declaration *second = &program->declarations[indices[index]];
        Module *first_module = &program->modules[first->module_index];
        Module *second_module = &program->modules[second->module_index];
        if (memcmp(first_module->module_id, second_module->module_id, 32u) == 0 &&
            first->namespace_tag == second->namespace_tag && strcmp(first->name, second->name) == 0) {
            Declaration *left = first;
            Declaration *right = second;
            Module *left_module = first_module;
            Module *right_module = second_module;
            char left_id[65];
            char right_id[65];
            int identity_order = memcmp(left->symbol_id, right->symbol_id, 32u);
            if (identity_order > 0 || (identity_order == 0 &&
                (strcmp(left_module->logical_path, right_module->logical_path) > 0 ||
                (strcmp(left_module->logical_path, right_module->logical_path) == 0 &&
                 left->name_start > right->name_start)))) {
                left = second;
                right = first;
                left_module = second_module;
                right_module = first_module;
            }
            bytes_to_hex(left->symbol_id, 32u, left_id);
            bytes_to_hex(right->symbol_id, 32u, right_id);
            set_error(program, "E2S51",
                "duplicate %s name `%s`: %s `%s` bytes %zu..%zu symbol %s; %s `%s` bytes %zu..%zu symbol %s",
                namespace_name(left->namespace_tag), left->name,
                kind_name(left->kind), left_module->logical_path, left->name_start, left->name_end, left_id,
                kind_name(right->kind), right_module->logical_path, right->name_start, right->name_end, right_id);
            free(indices);
            return false;
        }
    }
    free(indices);
    return true;
}

static bool reserve_call(Program *program) {
    Call *resized;
    if (program->call_count >= CALL_LIMIT) {
        set_error(program, "E2S55", "inventory exceeds %u resolved calls", CALL_LIMIT);
        return false;
    }
    if (program->call_count < program->call_capacity) return true;
    {
        size_t capacity = program->call_capacity == 0 ? 128u : program->call_capacity * 2u;
        if (capacity > CALL_LIMIT) capacity = CALL_LIMIT;
        resized = realloc(program->calls, capacity * sizeof(*resized));
        if (resized == NULL) {
            set_error(program, "E2S56", "call allocation failed");
            return false;
        }
        program->calls = resized;
        program->call_capacity = capacity;
    }
    return true;
}

static bool declaration_name_equals_token(
    const Declaration *declaration,
    const Module *module,
    const Token *token
) {
    size_t length = token->end - token->start;
    return strlen(declaration->name) == length &&
        memcmp(declaration->name, module->source + token->start, length) == 0;
}

static bool keyword_call(const Module *module, const Token *token) {
    static const char *keywords[] = { "if", "while", "match", "return", "let", "take" };
    size_t index;
    for (index = 0; index < sizeof(keywords) / sizeof(keywords[0]); index += 1) {
        if (token_equals(module, token, keywords[index])) return true;
    }
    return false;
}

static size_t argument_count_between(
    const Module *module,
    size_t opening,
    size_t closing
) {
    size_t cursor;
    size_t depth = 0;
    size_t count;
    if (closing == opening + 1u) return 0;
    count = 1u;
    for (cursor = opening + 1u; cursor < closing; cursor += 1u) {
        if (punctuation_equals(module, &module->tokens[cursor], '(')) depth += 1u;
        else if (punctuation_equals(module, &module->tokens[cursor], ')') && depth > 0) depth -= 1u;
        else if (punctuation_equals(module, &module->tokens[cursor], ',') && depth == 0) count += 1u;
    }
    return count;
}

static size_t import_for_qualifier(
    const Program *program,
    size_t module_index,
    const Module *source,
    const Token *qualifier
) {
    size_t index;
    size_t length = qualifier->end - qualifier->start;
    for (index = 0; index < program->import_count; index += 1u) {
        const Import *binding = &program->imports[index];
        if (binding->importing_module_index == module_index &&
            strlen(binding->local_name) == length &&
            memcmp(binding->local_name, source->source + qualifier->start, length) == 0) return index;
    }
    return SIZE_MAX;
}

static KofunIdentity access_identity(
    KofunIdentityKind kind,
    const uint8_t bytes[32]
) {
    KofunIdentity identity;
    identity.schema = KOFUN_IDENTITY_SCHEMA_V1;
    identity.kind = kind;
    memcpy(identity.bytes, bytes, 32u);
    return identity;
}

static KofunAccessResult decide_call_access(
    const Program *program,
    const Declaration *caller,
    const Declaration *callee,
    size_t use_start,
    size_t use_end
) {
    const Module *caller_module = &program->modules[caller->module_index];
    const Module *callee_module = &program->modules[callee->module_index];
    KofunAccessContext context;
    KofunDeclarationAccess declaration;
    memset(&context, 0, sizeof(context));
    memset(&declaration, 0, sizeof(declaration));
    context.caller_package = access_identity(KOFUN_ID_PACKAGE, caller_module->package_id);
    context.caller_module = access_identity(KOFUN_ID_MODULE, caller_module->module_id);
    context.caller_file = access_identity(KOFUN_ID_FILE, caller_module->file_id);
    context.use_span = (KofunSpan){ .start = (uint32_t)use_start, .end = (uint32_t)use_end };
    declaration.defining_package = access_identity(KOFUN_ID_PACKAGE, callee_module->package_id);
    declaration.defining_module = access_identity(KOFUN_ID_MODULE, callee_module->module_id);
    declaration.defining_file = access_identity(KOFUN_ID_FILE, callee_module->file_id);
    declaration.declaration_span = (KofunSpan){
        .start = (uint32_t)callee->start,
        .end = (uint32_t)callee->end
    };
    switch (callee->visibility) {
        case VISIBILITY_IMPLICIT_PRIVATE:
        case VISIBILITY_PRIVATE:
            declaration.declared_visibility = KOFUN_VISIBILITY_PRIVATE;
            break;
        case VISIBILITY_INTERNAL:
            declaration.declared_visibility = KOFUN_VISIBILITY_INTERNAL;
            break;
        case VISIBILITY_PUBLIC:
            declaration.declared_visibility = KOFUN_VISIBILITY_PUBLIC;
            break;
    }
    return kofun_decide_access(&context, &declaration);
}

static bool record_call(
    Program *program,
    size_t caller_index,
    size_t callee_index,
    size_t import_index,
    size_t qualifier_start,
    size_t qualifier_end,
    size_t member_start,
    size_t member_end,
    size_t full_start,
    size_t full_end,
    size_t argument_count,
    bool qualified,
    KofunAccessResult access
) {
    Declaration *callee = &program->declarations[callee_index];
    Module *module = &program->modules[program->declarations[caller_index].module_index];
    if (argument_count != callee->parameter_count) {
        set_error(program, "E2S64", "call to `%s` in `%s` has %zu arguments but requires %zu at bytes %zu..%zu",
            callee->name, module->logical_path, argument_count, callee->parameter_count,
            full_start, full_end);
        return false;
    }
    if (!reserve_call(program)) return false;
    program->calls[program->call_count] = (Call){
        .caller_index = caller_index,
        .callee_index = callee_index,
        .start = member_start,
        .end = member_end,
        .full_start = full_start,
        .full_end = full_end,
        .qualifier_start = qualifier_start,
        .qualifier_end = qualifier_end,
        .argument_count = argument_count,
        .import_index = import_index,
        .qualified = qualified,
        .access = access
    };
    program->call_count += 1u;
    return true;
}

static bool resolve_bodies(Program *program) {
    size_t declaration_index;
    for (declaration_index = 0; declaration_index < program->declaration_count; declaration_index += 1) {
        Declaration *caller = &program->declarations[declaration_index];
        Module *module;
        size_t cursor;
        if (caller->kind != DECLARATION_FUNCTION) continue;
        module = &program->modules[caller->module_index];
        for (cursor = caller->body_token_start; cursor < caller->body_token_end; cursor += 1) {
            Token *token = &module->tokens[cursor];
            size_t candidate;
            size_t found = SIZE_MAX;
            bool found_other_module = false;
            if (token_equals(module, token, "fn")) {
                set_error(program, "E2S50", "body-local function is not a top-level declaration in `%s` at bytes %zu..%zu",
                    module->logical_path, token->start, token->end);
                return false;
            }
            if (token->kind != TOKEN_IDENTIFIER) {
                continue;
            }
            if (cursor + 3u < caller->body_token_end &&
                punctuation_equals(module, &module->tokens[cursor + 1u], '.') &&
                module->tokens[cursor + 2u].kind == TOKEN_IDENTIFIER &&
                punctuation_equals(module, &module->tokens[cursor + 3u], '(')) {
                size_t import_index = import_for_qualifier(program, caller->module_index, module, token);
                Token *member = &module->tokens[cursor + 2u];
                size_t close;
                KofunAccessResult access;
                if (import_index == SIZE_MAX) {
                    set_error(program, "E2S62", "unknown module qualifier `%.*s` in `%s` at bytes %zu..%zu; add an explicit `import a.%.*s`",
                        (int)(token->end - token->start), module->source + token->start,
                        module->logical_path, token->start, token->end,
                        (int)(token->end - token->start), module->source + token->start);
                    return false;
                }
                if (!find_closing(program, module, cursor + 3u, '(', ')', &close)) return false;
                for (candidate = 0; candidate < program->declaration_count; candidate += 1u) {
                    Declaration *target = &program->declarations[candidate];
                    program->lookup_operations += 1u;
                    if (program->lookup_operations > LOOKUP_OPERATION_LIMIT) {
                        set_error(program, "E2S55", "qualified lookup exceeds %llu operations",
                            (unsigned long long)LOOKUP_OPERATION_LIMIT);
                        return false;
                    }
                    if (target->module_index == program->imports[import_index].target_module_index &&
                        target->namespace_tag == 0u && target->kind == DECLARATION_FUNCTION &&
                        declaration_name_equals_token(target, module, member)) {
                        found = candidate;
                        break;
                    }
                }
                if (found == SIZE_MAX) {
                    set_error(program, "E2S62", "module `%s` has no callable value `%.*s` required by `%s` at bytes %zu..%zu; qualified lookup does not fall back",
                        program->imports[import_index].path,
                        (int)(member->end - member->start), module->source + member->start,
                        module->logical_path, member->start, member->end);
                    return false;
                }
                access = decide_call_access(program, caller, &program->declarations[found],
                    token->start, module->tokens[close].end);
                if (!access.usable_reference) {
                    set_error(program, "E2S63", "call through `%s` in `%s` cannot access `%.*s` at bytes %zu..%zu: %s; change target visibility to `internal` or `pub`",
                        program->imports[import_index].local_name, module->logical_path,
                        (int)(member->end - member->start), module->source + member->start,
                        token->start, module->tokens[close].end,
                        kofun_access_reason_name(access.reason));
                    return false;
                }
                if (!record_call(program, declaration_index, found, import_index,
                        token->start, token->end, member->start, member->end,
                        token->start, module->tokens[close].end,
                        argument_count_between(module, cursor + 3u, close), true, access)) return false;
                continue;
            }
            if (cursor > caller->body_token_start && punctuation_equals(module, &module->tokens[cursor - 1u], '.')) continue;
            if (cursor + 1u >= caller->body_token_end ||
                !punctuation_equals(module, &module->tokens[cursor + 1u], '(') || keyword_call(module, token)) continue;
            for (candidate = 0; candidate < program->declaration_count; candidate += 1) {
                Declaration *target = &program->declarations[candidate];
                program->lookup_operations += 1u;
                if (program->lookup_operations > LOOKUP_OPERATION_LIMIT) {
                    set_error(program, "E2S55", "body lookup exceeds %llu operations",
                        (unsigned long long)LOOKUP_OPERATION_LIMIT);
                    return false;
                }
                if (target->namespace_tag != 0u || !declaration_name_equals_token(target, module, token)) continue;
                if (target->module_index == caller->module_index) found = candidate;
                else found_other_module = true;
            }
            if (found == SIZE_MAX) {
                set_error(program, found_other_module ? "E2S62" : "E2S53",
                    found_other_module
                        ? "unqualified value `%.*s` in `%s` does not import names at bytes %zu..%zu; use an explicit module qualifier"
                        : "unknown same-module value `%.*s` in `%s` at bytes %zu..%zu",
                    (int)(token->end - token->start), module->source + token->start,
                    module->logical_path, token->start, token->end);
                return false;
            }
            {
                size_t close;
                KofunAccessResult access;
                if (!find_closing(program, module, cursor + 1u, '(', ')', &close)) return false;
                access = decide_call_access(program, caller, &program->declarations[found],
                    token->start, module->tokens[close].end);
                if (!record_call(program, declaration_index, found, SIZE_MAX,
                        token->start, token->end, token->start, token->end,
                        token->start, module->tokens[close].end,
                        argument_count_between(module, cursor + 1u, close), false, access)) return false;
            }
        }
    }
    return true;
}

static int compare_output_indices(const void *left, const void *right) {
    size_t a_index = *(const size_t *)left;
    size_t b_index = *(const size_t *)right;
    const Declaration *a = &comparison_program->declarations[a_index];
    const Declaration *b = &comparison_program->declarations[b_index];
    const Module *a_module = &comparison_program->modules[a->module_index];
    const Module *b_module = &comparison_program->modules[b->module_index];
    if (a->module_index != b->module_index) return a->module_index < b->module_index ? -1 : 1;
    if (a->namespace_tag != b->namespace_tag) return a->namespace_tag < b->namespace_tag ? -1 : 1;
    {
        int result = memcmp(a->symbol_id, b->symbol_id, 32u);
        if (result != 0) return result;
        result = strcmp(a_module->logical_path, b_module->logical_path);
        if (result != 0) return result;
    }
    if (a->start != b->start) return a->start < b->start ? -1 : 1;
    return a_index < b_index ? -1 : a_index != b_index;
}

static int compare_calls(const void *left, const void *right) {
    const Call *a = left;
    const Call *b = right;
    const Declaration *a_caller = &comparison_program->declarations[a->caller_index];
    const Declaration *b_caller = &comparison_program->declarations[b->caller_index];
    int result = memcmp(a_caller->symbol_id, b_caller->symbol_id, 32u);
    if (result != 0) return result;
    if (a->full_start != b->full_start) return a->full_start < b->full_start ? -1 : 1;
    return 0;
}

static int compare_import_output_indices(const void *left, const void *right) {
    size_t a_index = *(const size_t *)left;
    size_t b_index = *(const size_t *)right;
    const Import *a = &comparison_program->imports[a_index];
    const Import *b = &comparison_program->imports[b_index];
    int result = memcmp(a->import_binding_id, b->import_binding_id, 32u);
    if (result != 0) return result;
    result = memcmp(
        comparison_program->modules[a->importing_module_index].module_id,
        comparison_program->modules[b->importing_module_index].module_id,
        32u
    );
    if (result != 0) return result;
    if (a->start != b->start) return a->start < b->start ? -1 : 1;
    return a_index < b_index ? -1 : a_index != b_index;
}

static bool emit_output(Program *program, const char *path) {
    FILE *output;
    size_t *indices;
    size_t *import_indices;
    size_t index;
    char package_hex[65];
    indices = malloc(program->declaration_count * sizeof(*indices));
    if (indices == NULL && program->declaration_count != 0) {
        set_error(program, "E2S56", "output-index allocation failed");
        return false;
    }
    import_indices = malloc(program->import_count * sizeof(*import_indices));
    if (import_indices == NULL && program->import_count != 0) {
        free(indices);
        set_error(program, "E2S56", "import-output-index allocation failed");
        return false;
    }
    for (index = 0; index < program->declaration_count; index += 1) indices[index] = index;
    for (index = 0; index < program->import_count; index += 1) import_indices[index] = index;
    comparison_program = program;
    qsort(indices, program->declaration_count, sizeof(*indices), compare_output_indices);
    qsort(import_indices, program->import_count, sizeof(*import_indices), compare_import_output_indices);
    qsort(program->calls, program->call_count, sizeof(program->calls[0]), compare_calls);
    output = fopen(path, "wb");
    if (output == NULL) {
        free(indices);
        free(import_indices);
        set_error(program, "E2S56", "cannot create output artifact");
        return false;
    }
    bytes_to_hex(program->modules[0].package_id, 32u, package_hex);
    fprintf(output, "%s\npackage|id=%s\n",
        program->import_count == 0 ? "kofun-module-symbols/v1" : "kofun-imports-qualified/v1",
        package_hex);
    for (index = 0; index < program->module_count; index += 1) {
        char module_hex[65];
        char file_hex[65];
        bytes_to_hex(program->modules[index].module_id, 32u, module_hex);
        bytes_to_hex(program->modules[index].file_id, 32u, file_hex);
        fprintf(output, "module|id=%s|file=%s|path=%s",
            module_hex, file_hex, program->modules[index].logical_path);
        if (program->import_count != 0) {
            fprintf(output, "|declared=%s", program->modules[index].declared_module_path);
        }
        fputc('\n', output);
    }
    for (index = 0; index < program->import_count; index += 1u) {
        Import *binding = &program->imports[import_indices[index]];
        Module *importer = &program->modules[binding->importing_module_index];
        Module *target = &program->modules[binding->target_module_index];
        char importer_hex[65];
        char file_hex[65];
        char target_hex[65];
        char binding_hex[65];
        char module_namespace_hex[65];
        size_t component;
        bytes_to_hex(importer->module_id, 32u, importer_hex);
        bytes_to_hex(importer->file_id, 32u, file_hex);
        bytes_to_hex(target->module_id, 32u, target_hex);
        bytes_to_hex(binding->import_binding_id, 32u, binding_hex);
        bytes_to_hex(program->namespace_ids[2], 32u, module_namespace_hex);
        fprintf(output,
            "import|caller-module=%s|caller-file=%s|local-ns=2:module|local-namespace=%s|local=%s|target-module=%s|binding=%s|form=qualified-module-v1|path=%s|span=%zu..%zu|components=",
            importer_hex, file_hex, module_namespace_hex, binding->local_name,
            target_hex, binding_hex, binding->path, binding->start, binding->end);
        for (component = 0; component < binding->component_count; component += 1u) {
            fprintf(output, "%s%zu..%zu", component == 0 ? "" : ",",
                binding->component_start[component], binding->component_end[component]);
        }
        fputc('\n', output);
    }
    for (index = 0; index < program->declaration_count; index += 1) {
        Declaration *declaration = &program->declarations[indices[index]];
        Module *module = &program->modules[declaration->module_index];
        char symbol_hex[65];
        bytes_to_hex(declaration->symbol_id, 32u, symbol_hex);
        fprintf(output,
            "decl|module=%zu|ns=%u:%s|kind=%s|name=%s|symbol=%s|visibility=%s|path=%s|header=%zu..%zu|name-span=%zu..%zu|signature=%zu..%zu",
            declaration->module_index, declaration->namespace_tag,
            namespace_name(declaration->namespace_tag), kind_name(declaration->kind),
            declaration->name, symbol_hex, visibility_name(declaration->visibility),
            module->logical_path, declaration->start, declaration->end,
            declaration->name_start, declaration->name_end,
            declaration->signature_start, declaration->signature_end);
        if (declaration->has_owner) {
            char owner_hex[65];
            bytes_to_hex(program->declarations[declaration->owner_index].symbol_id, 32u, owner_hex);
            fprintf(output, "|owner=%s|constructor-index=%zu", owner_hex, declaration->constructor_index);
        }
        fputc('\n', output);
    }
    for (index = 0; index < program->call_count; index += 1) {
        Call *call = &program->calls[index];
        Declaration *caller = &program->declarations[call->caller_index];
        Declaration *callee = &program->declarations[call->callee_index];
        char caller_hex[65];
        char callee_hex[65];
        bytes_to_hex(caller->symbol_id, 32u, caller_hex);
        bytes_to_hex(callee->symbol_id, 32u, callee_hex);
        if (!call->qualified) {
            fprintf(output, "call|caller=%s|callee=%s|name=%s|span=%zu..%zu\n",
                caller_hex, callee_hex, callee->name, call->start, call->end);
        } else {
            Import *binding = &program->imports[call->import_index];
            char binding_hex[65];
            char target_module_hex[65];
            char namespace_hex[65];
            bytes_to_hex(binding->import_binding_id, 32u, binding_hex);
            bytes_to_hex(program->modules[binding->target_module_index].module_id, 32u,
                target_module_hex);
            bytes_to_hex(callee->namespace_id, 32u, namespace_hex);
            fprintf(output,
                "qualified-call|caller=%s|binding=%s|target-module=%s|target-ns=%s|target=%s|name=%s|qualifier=%zu..%zu|member=%zu..%zu|full=%zu..%zu|access=%s|reason=%s|proof=0x%02x|arity=%zu\n",
                caller_hex, binding_hex, target_module_hex, namespace_hex, callee_hex,
                callee->name, call->qualifier_start, call->qualifier_end,
                call->start, call->end, call->full_start, call->full_end,
                kofun_access_kind_name(call->access.kind),
                kofun_access_reason_name(call->access.reason),
                call->access.proof, call->argument_count);
        }
    }
    free(indices);
    free(import_indices);
    if (fclose(output) != 0) {
        remove(path);
        set_error(program, "E2S56", "cannot commit output artifact");
        return false;
    }
    return true;
}

static size_t parameter_index_for_token(
    const Program *program,
    const Declaration *declaration,
    const Token *token
) {
    const Module *module = &program->modules[declaration->module_index];
    size_t cursor = declaration->parameters_token_start;
    size_t index = 0;
    while (cursor < declaration->parameters_token_end) {
        if (module->tokens[cursor].kind == TOKEN_IDENTIFIER) {
            size_t left_length = module->tokens[cursor].end - module->tokens[cursor].start;
            size_t right_length = token->end - token->start;
            if (left_length == right_length && memcmp(
                    module->source + module->tokens[cursor].start,
                    module->source + token->start,
                    left_length
                ) == 0) return index;
            index += 1u;
        }
        cursor += 1u;
        while (cursor < declaration->parameters_token_end &&
               !punctuation_equals(module, &module->tokens[cursor], ',')) cursor += 1u;
        if (cursor < declaration->parameters_token_end) cursor += 1u;
    }
    return SIZE_MAX;
}

static const Call *call_at(
    const Program *program,
    size_t caller_index,
    size_t start
) {
    size_t index;
    for (index = 0; index < program->call_count; index += 1u) {
        const Call *call = &program->calls[index];
        if (call->caller_index == caller_index && call->full_start == start) return call;
    }
    return NULL;
}

static bool emit_backend_expression(
    Program *program,
    FILE *output,
    size_t declaration_index,
    size_t first,
    size_t end
) {
    Declaration *declaration = &program->declarations[declaration_index];
    Module *module = &program->modules[declaration->module_index];
    size_t cursor;
    for (cursor = first; cursor < end; cursor += 1u) {
        Token *token = &module->tokens[cursor];
        if (token->kind == TOKEN_INTEGER) {
            fprintf(output, "%.*s", (int)(token->end - token->start), module->source + token->start);
        } else if (token->kind == TOKEN_IDENTIFIER) {
            const Call *call = call_at(program, declaration_index, token->start);
            if (call != NULL) {
                char callee_hex[65];
                if (program->declarations[call->callee_index].kind != DECLARATION_FUNCTION) {
                    set_error(program, "E2S65", "reference backend supports function calls only in `%s` at bytes %zu..%zu",
                        module->logical_path, call->full_start, call->full_end);
                    return false;
                }
                bytes_to_hex(program->declarations[call->callee_index].symbol_id, 32u, callee_hex);
                fprintf(output, "kofun_fn_%s", callee_hex);
                if (call->qualified) cursor += 2u;
            } else {
                size_t parameter_index = parameter_index_for_token(program, declaration, token);
                if (parameter_index == SIZE_MAX) {
                    set_error(program, "E2S65", "reference backend cannot lower identifier `%.*s` in `%s` at bytes %zu..%zu",
                        (int)(token->end - token->start), module->source + token->start,
                        module->logical_path, token->start, token->end);
                    return false;
                }
                fprintf(output, "kofun_p%zu", parameter_index);
            }
        } else if (token->kind == TOKEN_PUNCTUATION &&
            strchr("()+-*/,<>", module->source[token->start]) != NULL) {
            fputc(module->source[token->start], output);
        } else {
            set_error(program, "E2S65", "reference backend cannot lower token in `%s` at bytes %zu..%zu",
                module->logical_path, token->start, token->end);
            return false;
        }
        fputc(' ', output);
    }
    return true;
}

static bool emit_reference_backend(Program *program, const char *path) {
    size_t *indices;
    size_t index;
    size_t entry = SIZE_MAX;
    FILE *output;
    indices = malloc(program->declaration_count * sizeof(*indices));
    if (indices == NULL && program->declaration_count != 0) {
        set_error(program, "E2S56", "backend-index allocation failed");
        return false;
    }
    for (index = 0; index < program->declaration_count; index += 1u) indices[index] = index;
    comparison_program = program;
    qsort(indices, program->declaration_count, sizeof(*indices), compare_output_indices);
    for (index = 0; index < program->declaration_count; index += 1u) {
        Declaration *declaration = &program->declarations[index];
        if (declaration->kind != DECLARATION_FUNCTION) continue;
        if (!declaration->backend_signature_supported) {
            Module *module = &program->modules[declaration->module_index];
            free(indices);
            set_error(program, "E2S65", "reference backend requires Int parameters/results for `%s` in `%s`",
                declaration->name, module->logical_path);
            return false;
        }
        if (strcmp(declaration->name, "main") == 0) {
            if (entry != SIZE_MAX || declaration->parameter_count != 0) {
                free(indices);
                set_error(program, "E2S65", "reference backend requires exactly one zero-argument `main`");
                return false;
            }
            entry = index;
        }
    }
    if (entry == SIZE_MAX) {
        free(indices);
        set_error(program, "E2S65", "reference backend requires exactly one zero-argument `main`");
        return false;
    }
    output = fopen(path, "wb");
    if (output == NULL) {
        free(indices);
        set_error(program, "E2S56", "cannot create reference-backend artifact");
        return false;
    }
    fputs("#include <stdint.h>\n\n", output);
    for (index = 0; index < program->declaration_count; index += 1u) {
        Declaration *declaration = &program->declarations[indices[index]];
        char symbol_hex[65];
        size_t parameter;
        if (declaration->kind != DECLARATION_FUNCTION) continue;
        bytes_to_hex(declaration->symbol_id, 32u, symbol_hex);
        fprintf(output, "int64_t kofun_fn_%s(", symbol_hex);
        if (declaration->parameter_count == 0) fputs("void", output);
        for (parameter = 0; parameter < declaration->parameter_count; parameter += 1u) {
            fprintf(output, "%sint64_t kofun_p%zu", parameter == 0 ? "" : ", ", parameter);
        }
        fputs(");\n", output);
    }
    fputc('\n', output);
    for (index = 0; index < program->declaration_count && !program->failed; index += 1u) {
        size_t declaration_index = indices[index];
        Declaration *declaration = &program->declarations[declaration_index];
        Module *module = &program->modules[declaration->module_index];
        char symbol_hex[65];
        size_t parameter;
        size_t expression_start;
        if (declaration->kind != DECLARATION_FUNCTION) continue;
        if (declaration->body_token_start >= declaration->body_token_end ||
            !token_equals(module, &module->tokens[declaration->body_token_start], "return") ||
            declaration->body_token_start + 1u >= declaration->body_token_end) {
            set_error(program, "E2S65", "reference backend requires a single return expression in `%s` at bytes %zu..%zu",
                module->logical_path, declaration->start, declaration->end);
            break;
        }
        expression_start = declaration->body_token_start + 1u;
        bytes_to_hex(declaration->symbol_id, 32u, symbol_hex);
        fprintf(output, "int64_t kofun_fn_%s(", symbol_hex);
        if (declaration->parameter_count == 0) fputs("void", output);
        for (parameter = 0; parameter < declaration->parameter_count; parameter += 1u) {
            fprintf(output, "%sint64_t kofun_p%zu", parameter == 0 ? "" : ", ", parameter);
        }
        fputs(") {\n", output);
        for (parameter = 0; parameter < declaration->parameter_count; parameter += 1u) {
            fprintf(output, "    (void)kofun_p%zu;\n", parameter);
        }
        fputs("    return ", output);
        if (!emit_backend_expression(program, output, declaration_index,
                expression_start, declaration->body_token_end)) break;
        fputs(";\n}\n\n", output);
    }
    if (!program->failed) {
        char entry_hex[65];
        bytes_to_hex(program->declarations[entry].symbol_id, 32u, entry_hex);
        fprintf(output, "int main(void) { return (int)kofun_fn_%s(); }\n", entry_hex);
    }
    {
        bool output_failed = ferror(output) != 0;
        int close_status = fclose(output);
        free(indices);
        if (program->failed || output_failed || close_status != 0) {
            remove(path);
            if (!program->failed) set_error(program, "E2S56", "cannot commit reference-backend artifact");
            return false;
        }
    }
    return true;
}

static void destroy_program(Program *program) {
    size_t index;
    for (index = 0; index < program->module_count; index += 1) {
        free(program->modules[index].source);
        free(program->modules[index].tokens);
    }
    free(program->declarations);
    free(program->calls);
    free(program->imports);
}

int main(int argc, char **argv) {
    Program program;
    size_t index;
    int status = 1;
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s INVENTORY OUTPUT [REFERENCE_C_OUTPUT]\n", argv[0]);
        return 2;
    }
    memset(&program, 0, sizeof(program));
    remove(argv[2]);
    if (argc == 4) remove(argv[3]);
    if (!load_inventory(&program, argv[1]) || !order_and_validate_inventory(&program)) goto done;
    for (index = 0; index < program.module_count; index += 1) {
        if (!collect_module(&program, index)) goto done;
    }
    compute_identities(&program);
    if (!validate_duplicates(&program) || !resolve_imports(&program) ||
        !reject_import_cycles(&program) || !resolve_bodies(&program) ||
        !emit_output(&program, argv[2]) ||
        (argc == 4 && !emit_reference_backend(&program, argv[3]))) goto done;
    status = 0;
done:
    if (program.failed) printf("%s\n", program.error);
    if (status != 0) {
        remove(argv[2]);
        if (argc == 4) remove(argv[3]);
    }
    destroy_program(&program);
    return status;
}
