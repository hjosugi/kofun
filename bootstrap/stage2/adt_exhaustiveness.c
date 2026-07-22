#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SOURCE_LIMIT (1024u * 1024u)
#define ARTIFACT_LIMIT (8u * 1024u * 1024u)
#define NAME_LIMIT 256u
#define PATH_LIMIT 512u
#define ID_LENGTH 64u
#define ADT_LIMIT 64u
#define CONSTRUCTOR_LIMIT 256u
#define MATCH_LIMIT 256u
#define ARM_LIMIT 512u
#define PATTERN_NODE_LIMIT 256u
#define SCOPE_LIMIT 1024u
#define BINDING_LIMIT 1024u
#define USE_LIMIT 2048u
#define PATTERN_BINDING_LIMIT 512u
#define DISPLAY_CAP 8u
#define OPERATION_LIMIT UINT64_C(4096)

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

typedef struct {
    size_t index;
    char id[ID_LENGTH + 1u];
    char path[PATH_LIMIT + 1u];
} ModuleFact;

typedef struct {
    char id[ID_LENGTH + 1u];
    char module_id[ID_LENGTH + 1u];
    char name[NAME_LIMIT + 1u];
    char path[PATH_LIMIT + 1u];
    size_t module_index;
    size_t declaration_start;
    size_t declaration_end;
    size_t name_start;
    size_t name_end;
} AdtFact;

typedef struct {
    char id[ID_LENGTH + 1u];
    char owner[ID_LENGTH + 1u];
    char module_id[ID_LENGTH + 1u];
    char name[NAME_LIMIT + 1u];
    char path[PATH_LIMIT + 1u];
    size_t module_index;
    size_t ordinal;
    unsigned arity;
    size_t declaration_start;
    size_t declaration_end;
    size_t name_start;
    size_t name_end;
} ConstructorFact;

typedef enum {
    NODE_WILDCARD,
    NODE_LITERAL,
    NODE_NAME,
    NODE_CONSTRUCTOR,
    NODE_OR,
    NODE_PARENTHESIZED,
    NODE_ERROR
} NodeKind;

typedef struct {
    int64_t id;
    NodeKind kind;
    size_t start;
    size_t end;
    char name[NAME_LIMIT + 1u];
    size_t name_start;
    size_t name_end;
    size_t open_start;
    size_t open_end;
    size_t close_start;
    size_t close_end;
    size_t child_count;
    int64_t children[8];
} PatternNode;

typedef struct {
    int64_t source_id;
    size_t start;
    size_t open_start;
    size_t open_end;
    size_t close_start;
    size_t close_end;
    size_t declared_arm_count;
} PatternMatch;

typedef struct {
    int64_t match_id;
    size_t index;
    int64_t root;
    size_t start;
    size_t end;
    int64_t arrow_start;
    int64_t arrow_end;
} PatternArm;

typedef struct {
    int64_t id;
    int64_t parent;
    char kind[32];
    size_t open_start;
    size_t close_end;
} ScopeFact;

typedef struct {
    int64_t id;
    int64_t scope_id;
    char name[NAME_LIMIT + 1u];
    char type_name[NAME_LIMIT + 1u];
    size_t start;
    size_t end;
} BindingFact;

typedef struct {
    size_t start;
    size_t end;
    int64_t scope_id;
    int64_t binding_id;
} UseFact;

typedef struct {
    size_t start;
    size_t end;
    int64_t scope_id;
    char name[NAME_LIMIT + 1u];
    char role[16];
} CandidateUse;

typedef enum {
    ARM_CONSTRUCTOR,
    ARM_WILDCARD,
    ARM_BINDING
} ArmRole;

typedef struct {
    size_t match_index;
    size_t arm_index;
    int64_t pattern_root;
    ArmRole role;
    int64_t constructor_index;
    int64_t scope_id;
    int64_t binding_id;
    bool guarded;
    size_t start;
    size_t end;
} TypedArm;

typedef struct {
    size_t id;
    int64_t source_match_id;
    size_t start;
    size_t end;
    int64_t scrutinee_binding_id;
    int64_t scope_id;
    size_t adt_index;
    size_t first_arm;
    size_t arm_count;
} TypedMatch;

typedef struct {
    int64_t binding_id;
    int64_t scope_id;
    size_t match_index;
    size_t arm_index;
    int64_t pattern_node;
    char name[NAME_LIMIT + 1u];
    const char *role;
    size_t start;
    size_t end;
} PatternBinding;

typedef struct {
    char *source;
    size_t source_length;
    char logical_path[PATH_LIMIT + 1u];
    char target_module_id[ID_LENGTH + 1u];
    ModuleFact modules[ADT_LIMIT];
    size_t module_count;
    AdtFact adts[ADT_LIMIT];
    size_t adt_count;
    ConstructorFact constructors[CONSTRUCTOR_LIMIT];
    size_t constructor_count;
    PatternNode nodes[PATTERN_NODE_LIMIT];
    size_t node_count;
    PatternMatch pattern_matches[MATCH_LIMIT];
    size_t pattern_match_count;
    PatternArm pattern_arms[ARM_LIMIT];
    size_t pattern_arm_count;
    ScopeFact scopes[SCOPE_LIMIT];
    size_t scope_count;
    BindingFact bindings[BINDING_LIMIT];
    size_t binding_count;
    UseFact uses[USE_LIMIT];
    size_t use_count;
    CandidateUse candidates[USE_LIMIT];
    size_t candidate_count;
    TypedMatch matches[MATCH_LIMIT];
    size_t match_count;
    TypedArm arms[ARM_LIMIT];
    size_t arm_count;
    PatternBinding pattern_bindings[PATTERN_BINDING_LIMIT];
    size_t pattern_binding_count;
    int64_t next_binding_id;
    char error[16384];
    bool failed;
} Program;

static void buffer_init(Buffer *buffer) {
    buffer->capacity = 512u;
    buffer->length = 0u;
    buffer->data = malloc(buffer->capacity);
    if (buffer->data == NULL) {
        fputs("adt exhaustiveness: allocation failed\n", stderr);
        exit(2);
    }
    buffer->data[0] = '\0';
}

static void buffer_reserve(Buffer *buffer, size_t extra) {
    size_t needed = buffer->length + extra + 1u;
    size_t capacity = buffer->capacity;
    char *resized;
    if (needed <= capacity) return;
    while (capacity < needed) capacity *= 2u;
    resized = realloc(buffer->data, capacity);
    if (resized == NULL) {
        free(buffer->data);
        fputs("adt exhaustiveness: allocation failed\n", stderr);
        exit(2);
    }
    buffer->data = resized;
    buffer->capacity = capacity;
}

static void buffer_append(Buffer *buffer, const char *text) {
    size_t length = strlen(text);
    buffer_reserve(buffer, length);
    memcpy(buffer->data + buffer->length, text, length + 1u);
    buffer->length += length;
}

static void buffer_format(Buffer *buffer, const char *format, ...) {
    va_list arguments;
    va_list copy;
    int needed;
    va_start(arguments, format);
    va_copy(copy, arguments);
    needed = vsnprintf(NULL, 0u, format, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(arguments);
        fputs("adt exhaustiveness: formatting failed\n", stderr);
        exit(2);
    }
    buffer_reserve(buffer, (size_t)needed);
    (void)vsnprintf(
        buffer->data + buffer->length,
        buffer->capacity - buffer->length,
        format,
        arguments
    );
    va_end(arguments);
    buffer->length += (size_t)needed;
}

static void set_error(Program *program, const char *code, const char *format, ...) {
    char detail[15000];
    va_list arguments;
    if (program->failed) return;
    va_start(arguments, format);
    if (vsnprintf(detail, sizeof(detail), format, arguments) < 0) {
        detail[0] = '\0';
    }
    va_end(arguments);
    (void)snprintf(
        program->error,
        sizeof(program->error),
        "error[%s]: %s",
        code,
        detail
    );
    program->failed = true;
}

static char *read_bounded(Program *program, const char *path, size_t limit) {
    FILE *input = fopen(path, "rb");
    long measured;
    size_t length;
    char *value;
    if (input == NULL) {
        set_error(program, "E2S79", "cannot open adapter input `%s`", path);
        return NULL;
    }
    if (fseek(input, 0, SEEK_END) != 0 ||
        (measured = ftell(input)) < 0 ||
        fseek(input, 0, SEEK_SET) != 0) {
        fclose(input);
        set_error(program, "E2S79", "cannot measure adapter input `%s`", path);
        return NULL;
    }
    if ((unsigned long)measured > limit) {
        fclose(input);
        set_error(program, "E2S79", "adapter input `%s` exceeds %zu bytes", path, limit);
        return NULL;
    }
    length = (size_t)measured;
    value = malloc(length + 1u);
    if (value == NULL) {
        fclose(input);
        set_error(program, "E2S79", "allocation failed for adapter input");
        return NULL;
    }
    if (fread(value, 1u, length, input) != length || fclose(input) != 0) {
        free(value);
        set_error(program, "E2S79", "cannot read complete adapter input `%s`", path);
        return NULL;
    }
    value[length] = '\0';
    return value;
}

static bool bounded_copy(
    Program *program,
    char *output,
    size_t capacity,
    const char *value,
    const char *label
) {
    size_t length = strlen(value);
    if (length == 0u || length >= capacity) {
        set_error(program, "E2S79", "%s is empty or exceeds its bounded size", label);
        return false;
    }
    memcpy(output, value, length + 1u);
    return true;
}

static bool id_is_valid(const char *value) {
    size_t index;
    if (strlen(value) != ID_LENGTH) return false;
    for (index = 0u; index < ID_LENGTH; index += 1u) {
        unsigned char byte = (unsigned char)value[index];
        if (!isdigit(byte) && !(byte >= 'a' && byte <= 'f')) return false;
    }
    return true;
}

static bool parse_size(const char *value, size_t *output) {
    char *end = NULL;
    uintmax_t parsed;
    if (value[0] == '\0' || value[0] == '-') return false;
    parsed = strtoumax(value, &end, 10);
    if (end == value || *end != '\0' || parsed > SIZE_MAX) return false;
    *output = (size_t)parsed;
    return true;
}

static bool parse_i64(const char *value, int64_t *output) {
    char *end = NULL;
    intmax_t parsed;
    if (value[0] == '\0') return false;
    parsed = strtoimax(value, &end, 10);
    if (end == value || *end != '\0' || parsed < INT64_MIN || parsed > INT64_MAX) {
        return false;
    }
    *output = (int64_t)parsed;
    return true;
}

static bool parse_span(const char *value, size_t *start, size_t *end) {
    const char *dots = strstr(value, "..");
    char left[32];
    size_t length;
    if (dots == NULL || strstr(dots + 2, "..") != NULL) return false;
    length = (size_t)(dots - value);
    if (length == 0u || length >= sizeof(left)) return false;
    memcpy(left, value, length);
    left[length] = '\0';
    return parse_size(left, start) && parse_size(dots + 2, end) && *end >= *start;
}

static size_t split_fields(char *line, char *fields[], size_t capacity) {
    size_t count = 1u;
    char *cursor = line;
    fields[0] = line;
    while (*cursor != '\0') {
        if (*cursor == '|') {
            if (count >= capacity) return capacity + 1u;
            *cursor = '\0';
            fields[count++] = cursor + 1;
        }
        cursor += 1;
    }
    return count;
}

static const char *key_value(char *fields[], size_t count, const char *key) {
    size_t index;
    size_t length = strlen(key);
    for (index = 1u; index < count; index += 1u) {
        if (strncmp(fields[index], key, length) == 0 && fields[index][length] == '=') {
            return fields[index] + length + 1u;
        }
    }
    return NULL;
}

static size_t skip_trivia(const char *source, size_t length, size_t start) {
    size_t cursor = start;
    while (cursor < length) {
        unsigned char byte = (unsigned char)source[cursor];
        if (isspace(byte)) {
            cursor += 1u;
        } else if (source[cursor] == '#') {
            while (cursor < length && source[cursor] != '\n') cursor += 1u;
        } else {
            break;
        }
    }
    return cursor;
}

static bool identifier_start(char byte) {
    unsigned char value = (unsigned char)byte;
    return byte == '_' || isalpha(value) != 0;
}

static bool identifier_continue(char byte) {
    unsigned char value = (unsigned char)byte;
    return identifier_start(byte) || isdigit(value) != 0;
}

static bool pair_token(const char *source, size_t length, size_t start) {
    static const char *pairs[] = {
        "->", "==", "!=", "<=", ">=", "&&", "||", "//", "..",
        "**", "??", "|>", "=>"
    };
    char pair[3];
    size_t index;
    if (start + 1u >= length) return false;
    pair[0] = source[start];
    pair[1] = source[start + 1u];
    pair[2] = '\0';
    for (index = 0u; index < sizeof(pairs) / sizeof(pairs[0]); index += 1u) {
        if (strcmp(pair, pairs[index]) == 0) return true;
    }
    return false;
}

static size_t token_end(const char *source, size_t length, size_t start) {
    size_t cursor;
    if (start >= length) return start;
    cursor = start + 1u;
    if (identifier_start(source[start])) {
        while (cursor < length && identifier_continue(source[cursor])) cursor += 1u;
        return cursor;
    }
    if (isdigit((unsigned char)source[start])) {
        while (cursor < length &&
            (isdigit((unsigned char)source[cursor]) || source[cursor] == '_')) {
            cursor += 1u;
        }
        return cursor;
    }
    if (source[start] == '"') {
        bool escaped = false;
        while (cursor < length) {
            char byte = source[cursor++];
            if (escaped) escaped = false;
            else if (byte == '\\') escaped = true;
            else if (byte == '"') return cursor;
            else if (byte == '\n') return start;
        }
        return start;
    }
    return pair_token(source, length, start) ? cursor + 1u : cursor;
}

static bool source_equals(
    const Program *program,
    size_t start,
    size_t end,
    const char *text
) {
    size_t length = strlen(text);
    return start <= end && end <= program->source_length &&
        end - start == length &&
        memcmp(program->source + start, text, length) == 0;
}

static bool source_token_equals(const Program *program, size_t start, const char *text) {
    size_t end = token_end(program->source, program->source_length, start);
    return source_equals(program, start, end, text);
}

static bool span_in_source(const Program *program, size_t start, size_t end) {
    return start <= end && end <= program->source_length;
}

static ModuleFact *module_by_index(Program *program, size_t index) {
    size_t candidate;
    for (candidate = 0u; candidate < program->module_count; candidate += 1u) {
        if (program->modules[candidate].index == index) return &program->modules[candidate];
    }
    return NULL;
}

static bool parse_module_symbols(Program *program, char *artifact) {
    char *cursor = artifact;
    size_t line_number = 0u;
    bool header = false;
    while (*cursor != '\0' && !program->failed) {
        char *line = cursor;
        char *fields[32];
        size_t count;
        char *newline = strchr(cursor, '\n');
        if (newline != NULL) {
            *newline = '\0';
            cursor = newline + 1;
        } else {
            cursor += strlen(cursor);
        }
        line_number += 1u;
        if (line[0] == '\0') continue;
        if (!header) {
            if (strcmp(line, "kofun-module-symbols/v1") != 0) {
                set_error(program, "E2S79", "module-symbol artifact header is invalid");
                return false;
            }
            header = true;
            continue;
        }
        count = split_fields(line, fields, sizeof(fields) / sizeof(fields[0]));
        if (count == 0u || count > sizeof(fields) / sizeof(fields[0])) {
            set_error(program, "E2S79", "module-symbol line %zu has too many fields", line_number);
            return false;
        }
        if (strcmp(fields[0], "package") == 0) {
            const char *id = key_value(fields, count, "id");
            if (id == NULL || !id_is_valid(id)) {
                set_error(program, "E2S79", "module-symbol package identity is invalid");
                return false;
            }
        } else if (strcmp(fields[0], "module") == 0) {
            const char *id = key_value(fields, count, "id");
            const char *path = key_value(fields, count, "path");
            ModuleFact *module;
            size_t index;
            if (program->module_count >= ADT_LIMIT || id == NULL || path == NULL ||
                !id_is_valid(id)) {
                set_error(program, "E2S79", "module-symbol module record is invalid or exceeds %u", ADT_LIMIT);
                return false;
            }
            index = program->module_count;
            if (module_by_index(program, index) != NULL) {
                set_error(program, "E2S79", "duplicate module index %zu", index);
                return false;
            }
            module = &program->modules[program->module_count++];
            memset(module, 0, sizeof(*module));
            module->index = index;
            if (!bounded_copy(program, module->id, sizeof(module->id), id, "module id") ||
                !bounded_copy(program, module->path, sizeof(module->path), path, "logical path")) {
                return false;
            }
        } else if (strcmp(fields[0], "decl") == 0) {
            const char *module_text = key_value(fields, count, "module");
            const char *kind = key_value(fields, count, "kind");
            const char *name = key_value(fields, count, "name");
            const char *symbol = key_value(fields, count, "symbol");
            const char *path = key_value(fields, count, "path");
            const char *header_span = key_value(fields, count, "header");
            const char *name_span = key_value(fields, count, "name-span");
            size_t module_index;
            size_t declaration_start;
            size_t declaration_end;
            size_t name_start;
            size_t name_end;
            ModuleFact *module;
            if (module_text == NULL || kind == NULL || name == NULL || symbol == NULL ||
                path == NULL || header_span == NULL || name_span == NULL ||
                !parse_size(module_text, &module_index) ||
                !parse_span(header_span, &declaration_start, &declaration_end) ||
                !parse_span(name_span, &name_start, &name_end) || !id_is_valid(symbol)) {
                set_error(program, "E2S79", "module-symbol declaration line %zu is malformed", line_number);
                return false;
            }
            module = module_by_index(program, module_index);
            if (module == NULL || strcmp(module->path, path) != 0) {
                set_error(program, "E2S79", "declaration line %zu has unknown module/path", line_number);
                return false;
            }
            if (strcmp(kind, "adt") == 0) {
                AdtFact *adt;
                if (program->adt_count >= ADT_LIMIT) {
                    set_error(program, "E2S79", "typed-match adapter exceeds %u ADTs", ADT_LIMIT);
                    return false;
                }
                adt = &program->adts[program->adt_count++];
                memset(adt, 0, sizeof(*adt));
                adt->module_index = module_index;
                adt->declaration_start = declaration_start;
                adt->declaration_end = declaration_end;
                adt->name_start = name_start;
                adt->name_end = name_end;
                if (!bounded_copy(program, adt->id, sizeof(adt->id), symbol, "ADT SymbolId") ||
                    !bounded_copy(program, adt->module_id, sizeof(adt->module_id), module->id, "module id") ||
                    !bounded_copy(program, adt->name, sizeof(adt->name), name, "ADT name") ||
                    !bounded_copy(program, adt->path, sizeof(adt->path), path, "ADT path")) {
                    return false;
                }
            } else if (strcmp(kind, "constructor") == 0) {
                const char *owner = key_value(fields, count, "owner");
                const char *ordinal_text = key_value(fields, count, "constructor-index");
                ConstructorFact *constructor;
                size_t ordinal;
                if (owner == NULL || ordinal_text == NULL || !id_is_valid(owner) ||
                    !parse_size(ordinal_text, &ordinal) ||
                    program->constructor_count >= CONSTRUCTOR_LIMIT) {
                    set_error(program, "E2S79", "constructor declaration line %zu is malformed or exceeds %u", line_number, CONSTRUCTOR_LIMIT);
                    return false;
                }
                constructor = &program->constructors[program->constructor_count++];
                memset(constructor, 0, sizeof(*constructor));
                constructor->module_index = module_index;
                constructor->ordinal = ordinal;
                constructor->declaration_start = declaration_start;
                constructor->declaration_end = declaration_end;
                constructor->name_start = name_start;
                constructor->name_end = name_end;
                if (!bounded_copy(program, constructor->id, sizeof(constructor->id), symbol, "constructor SymbolId") ||
                    !bounded_copy(program, constructor->owner, sizeof(constructor->owner), owner, "constructor owner") ||
                    !bounded_copy(program, constructor->module_id, sizeof(constructor->module_id), module->id, "module id") ||
                    !bounded_copy(program, constructor->name, sizeof(constructor->name), name, "constructor name") ||
                    !bounded_copy(program, constructor->path, sizeof(constructor->path), path, "constructor path")) {
                    return false;
                }
            }
        }
    }
    if (!header || program->module_count == 0u) {
        set_error(program, "E2S79", "module-symbol artifact is incomplete");
        return false;
    }
    return !program->failed;
}

static NodeKind node_kind(Program *program, const char *name) {
    if (strcmp(name, "WildcardPattern") == 0) return NODE_WILDCARD;
    if (strcmp(name, "LiteralPattern") == 0) return NODE_LITERAL;
    if (strcmp(name, "NamePattern") == 0) return NODE_NAME;
    if (strcmp(name, "ConstructorPattern") == 0) return NODE_CONSTRUCTOR;
    if (strcmp(name, "OrPattern") == 0) return NODE_OR;
    if (strcmp(name, "ParenthesizedPattern") == 0) return NODE_PARENTHESIZED;
    if (strcmp(name, "ErrorPattern") == 0) return NODE_ERROR;
    set_error(program, "E2S79", "unknown Pattern node kind `%s`", name);
    return NODE_ERROR;
}

static bool parse_child_ids(Program *program, PatternNode *node, char *value) {
    char *cursor = value;
    size_t count = 0u;
    if (node->child_count == 0u) return value[0] == '\0';
    while (*cursor != '\0') {
        char *comma = strchr(cursor, ',');
        int64_t id;
        if (comma != NULL) *comma = '\0';
        if (!parse_i64(cursor, &id) || count >= sizeof(node->children) / sizeof(node->children[0])) {
            set_error(program, "E2S79", "constructor child id list is malformed");
            return false;
        }
        node->children[count++] = id;
        if (comma == NULL) break;
        cursor = comma + 1;
    }
    if (count != node->child_count) {
        set_error(program, "E2S79", "constructor child count differs from Pattern record");
        return false;
    }
    return true;
}

static bool parse_pattern_artifact(Program *program, char *artifact) {
    char *cursor = artifact;
    bool header = false;
    bool saw_count = false;
    size_t declared_match_count = 0u;
    while (*cursor != '\0' && !program->failed) {
        char *line = cursor;
        char *fields[32];
        size_t count;
        char *newline = strchr(cursor, '\n');
        if (newline != NULL) {
            *newline = '\0';
            cursor = newline + 1;
        } else {
            cursor += strlen(cursor);
        }
        if (line[0] == '\0') continue;
        if (!header) {
            if (strcmp(line, "kofun-pattern-tree/v1") != 0) {
                set_error(program, "E2S79", "Pattern artifact header is invalid");
                return false;
            }
            header = true;
            continue;
        }
        count = split_fields(line, fields, sizeof(fields) / sizeof(fields[0]));
        if (count > sizeof(fields) / sizeof(fields[0])) {
            set_error(program, "E2S79", "Pattern artifact record has too many fields");
            return false;
        }
        if (strcmp(fields[0], "limits") == 0 || strcmp(fields[0], "delimiter") == 0 ||
            strcmp(fields[0], "separator") == 0 || strcmp(fields[0], "pattern-diagnostic") == 0) {
            continue;
        }
        if (strcmp(fields[0], "match") == 0) {
            PatternMatch *match;
            int64_t id;
            if (count != 8u || program->pattern_match_count >= MATCH_LIMIT ||
                !parse_i64(fields[1], &id)) {
                set_error(program, "E2S79", "Pattern match record is malformed or exceeds %u", MATCH_LIMIT);
                return false;
            }
            match = &program->pattern_matches[program->pattern_match_count++];
            memset(match, 0, sizeof(*match));
            match->source_id = id;
            if (!parse_size(fields[2], &match->start) ||
                !parse_size(fields[3], &match->open_start) ||
                !parse_size(fields[4], &match->open_end) ||
                !parse_size(fields[5], &match->close_start) ||
                !parse_size(fields[6], &match->close_end) ||
                !parse_size(fields[7], &match->declared_arm_count)) {
                set_error(program, "E2S79", "Pattern match spans are malformed");
                return false;
            }
        } else if (strcmp(fields[0], "node") == 0) {
            PatternNode *node;
            int64_t id;
            size_t index;
            if (count < 5u || program->node_count >= PATTERN_NODE_LIMIT ||
                !parse_i64(fields[1], &id)) {
                set_error(program, "E2S79", "Pattern node record is malformed or exceeds %u", PATTERN_NODE_LIMIT);
                return false;
            }
            for (index = 0u; index < program->node_count; index += 1u) {
                if (program->nodes[index].id == id) {
                    set_error(program, "E2S79", "duplicate Pattern node id %" PRId64, id);
                    return false;
                }
            }
            node = &program->nodes[program->node_count++];
            memset(node, 0, sizeof(*node));
            node->id = id;
            node->kind = node_kind(program, fields[2]);
            if (!parse_size(fields[3], &node->start) || !parse_size(fields[4], &node->end)) {
                set_error(program, "E2S79", "Pattern node span is malformed");
                return false;
            }
            if (node->kind == NODE_NAME) {
                if (count != 8u || !bounded_copy(program, node->name, sizeof(node->name), fields[5], "Pattern name") ||
                    !parse_size(fields[6], &node->name_start) || !parse_size(fields[7], &node->name_end)) {
                    set_error(program, "E2S79", "NamePattern record is malformed");
                    return false;
                }
            } else if (node->kind == NODE_CONSTRUCTOR) {
                if (count != 14u || !bounded_copy(program, node->name, sizeof(node->name), fields[5], "constructor Pattern name") ||
                    !parse_size(fields[6], &node->name_start) || !parse_size(fields[7], &node->name_end) ||
                    !parse_size(fields[8], &node->open_start) || !parse_size(fields[9], &node->open_end) ||
                    !parse_size(fields[10], &node->close_start) || !parse_size(fields[11], &node->close_end) ||
                    !parse_size(fields[12], &node->child_count) || node->child_count > 8u ||
                    !parse_child_ids(program, node, fields[13])) {
                    if (!program->failed) set_error(program, "E2S79", "ConstructorPattern record is malformed");
                    return false;
                }
            }
        } else if (strcmp(fields[0], "arm") == 0) {
            PatternArm *arm;
            int64_t match_id;
            int64_t root;
            if (count != 8u || program->pattern_arm_count >= ARM_LIMIT ||
                !parse_i64(fields[1], &match_id) || !parse_i64(fields[3], &root)) {
                set_error(program, "E2S79", "Pattern arm record is malformed or exceeds %u", ARM_LIMIT);
                return false;
            }
            arm = &program->pattern_arms[program->pattern_arm_count++];
            memset(arm, 0, sizeof(*arm));
            arm->match_id = match_id;
            arm->root = root;
            if (!parse_size(fields[2], &arm->index) ||
                !parse_size(fields[4], &arm->start) || !parse_size(fields[5], &arm->end) ||
                !parse_i64(fields[6], &arm->arrow_start) || !parse_i64(fields[7], &arm->arrow_end)) {
                set_error(program, "E2S79", "Pattern arm spans are malformed");
                return false;
            }
        } else if (strcmp(fields[0], "match-count") == 0) {
            if (count != 2u || !parse_size(fields[1], &declared_match_count) || saw_count) {
                set_error(program, "E2S79", "Pattern match-count record is malformed or duplicated");
                return false;
            }
            saw_count = true;
        }
    }
    if (!header || !saw_count || declared_match_count != program->pattern_match_count) {
        set_error(program, "E2S79", "Pattern artifact match count is stale or incomplete");
        return false;
    }
    return !program->failed;
}

static bool parse_scope_artifact(Program *program, char *artifact) {
    char *cursor = artifact;
    bool header = false;
    while (*cursor != '\0' && !program->failed) {
        char *line = cursor;
        char *fields[16];
        size_t count;
        char *newline = strchr(cursor, '\n');
        if (newline != NULL) {
            *newline = '\0';
            cursor = newline + 1;
        } else {
            cursor += strlen(cursor);
        }
        if (line[0] == '\0') continue;
        if (!header) {
            if (strcmp(line, "kofun-scope-hir/v1") != 0) {
                set_error(program, "E2S79", "scope-HIR artifact header is invalid");
                return false;
            }
            header = true;
            continue;
        }
        count = split_fields(line, fields, sizeof(fields) / sizeof(fields[0]));
        if (count > sizeof(fields) / sizeof(fields[0])) {
            set_error(program, "E2S79", "scope-HIR record has too many fields");
            return false;
        }
        if (strcmp(fields[0], "hir-function") == 0) continue;
        if (strcmp(fields[0], "scope") == 0) {
            ScopeFact *scope;
            size_t index;
            if (count != 7u || program->scope_count >= SCOPE_LIMIT) {
                set_error(program, "E2S79", "scope-HIR exceeds %u scopes or is malformed", SCOPE_LIMIT);
                return false;
            }
            scope = &program->scopes[program->scope_count++];
            memset(scope, 0, sizeof(*scope));
            if (!parse_i64(fields[1], &scope->id) || !parse_i64(fields[2], &scope->parent) ||
                !bounded_copy(program, scope->kind, sizeof(scope->kind), fields[3], "scope kind") ||
                !parse_size(fields[4], &scope->open_start) || !parse_size(fields[5], &scope->close_end)) {
                set_error(program, "E2S79", "scope-HIR scope record is malformed");
                return false;
            }
            for (index = 0u; index + 1u < program->scope_count; index += 1u) {
                if (program->scopes[index].id == scope->id) {
                    set_error(program, "E2S79", "duplicate ScopeId %" PRId64, scope->id);
                    return false;
                }
            }
        } else if (strcmp(fields[0], "binding") == 0) {
            BindingFact *binding;
            size_t index;
            if (count != 11u || program->binding_count >= BINDING_LIMIT) {
                set_error(program, "E2S79", "scope-HIR exceeds %u bindings or is malformed", BINDING_LIMIT);
                return false;
            }
            binding = &program->bindings[program->binding_count++];
            memset(binding, 0, sizeof(*binding));
            if (!parse_i64(fields[1], &binding->id) || !parse_i64(fields[2], &binding->scope_id) ||
                !bounded_copy(program, binding->name, sizeof(binding->name), fields[3], "binding name") ||
                !bounded_copy(program, binding->type_name, sizeof(binding->type_name), fields[5], "binding type") ||
                !parse_size(fields[8], &binding->start) || !parse_size(fields[9], &binding->end)) {
                set_error(program, "E2S79", "scope-HIR binding record is malformed");
                return false;
            }
            for (index = 0u; index + 1u < program->binding_count; index += 1u) {
                if (program->bindings[index].id == binding->id) {
                    set_error(program, "E2S79", "duplicate BindingId %" PRId64, binding->id);
                    return false;
                }
            }
            if (binding->id >= program->next_binding_id) program->next_binding_id = binding->id + 1;
        } else if (strcmp(fields[0], "use") == 0) {
            UseFact *use;
            if (count != 6u || program->use_count >= USE_LIMIT) {
                set_error(program, "E2S79", "scope-HIR exceeds %u uses or is malformed", USE_LIMIT);
                return false;
            }
            use = &program->uses[program->use_count++];
            memset(use, 0, sizeof(*use));
            if (!parse_size(fields[1], &use->start) || !parse_size(fields[2], &use->end) ||
                !parse_i64(fields[3], &use->scope_id) || !parse_i64(fields[4], &use->binding_id)) {
                set_error(program, "E2S79", "scope-HIR use record is malformed");
                return false;
            }
        } else if (strcmp(fields[0], "candidate-use") == 0) {
            CandidateUse *use;
            if (count != 6u || program->candidate_count >= USE_LIMIT) {
                set_error(program, "E2S79", "scope-HIR exceeds %u candidate uses or is malformed", USE_LIMIT);
                return false;
            }
            use = &program->candidates[program->candidate_count++];
            memset(use, 0, sizeof(*use));
            if (!parse_size(fields[1], &use->start) || !parse_size(fields[2], &use->end) ||
                !parse_i64(fields[3], &use->scope_id) ||
                !bounded_copy(program, use->name, sizeof(use->name), fields[4], "candidate-use name") ||
                !bounded_copy(program, use->role, sizeof(use->role), fields[5], "candidate-use role")) {
                set_error(program, "E2S79", "scope-HIR candidate-use record is malformed");
                return false;
            }
        }
    }
    if (!header) {
        set_error(program, "E2S79", "scope-HIR artifact is empty");
        return false;
    }
    return !program->failed;
}

static PatternNode *pattern_node(Program *program, int64_t id) {
    size_t index;
    for (index = 0u; index < program->node_count; index += 1u) {
        if (program->nodes[index].id == id) return &program->nodes[index];
    }
    return NULL;
}

static ScopeFact *scope_fact(Program *program, int64_t id) {
    size_t index;
    for (index = 0u; index < program->scope_count; index += 1u) {
        if (program->scopes[index].id == id) return &program->scopes[index];
    }
    return NULL;
}

static BindingFact *binding_fact(Program *program, int64_t id) {
    size_t index;
    for (index = 0u; index < program->binding_count; index += 1u) {
        if (program->bindings[index].id == id) return &program->bindings[index];
    }
    return NULL;
}

static AdtFact *adt_by_id(Program *program, const char *id) {
    size_t index;
    for (index = 0u; index < program->adt_count; index += 1u) {
        if (strcmp(program->adts[index].id, id) == 0) return &program->adts[index];
    }
    return NULL;
}

static ConstructorFact *owned_constructor(
    Program *program,
    const char *owner,
    const char *name
) {
    size_t index;
    for (index = 0u; index < program->constructor_count; index += 1u) {
        ConstructorFact *constructor = &program->constructors[index];
        if (strcmp(constructor->owner, owner) == 0 &&
            strcmp(constructor->name, name) == 0) {
            return constructor;
        }
    }
    return NULL;
}

static bool scope_is_descendant(Program *program, int64_t child, int64_t parent) {
    size_t depth;
    for (depth = 0u; depth <= SCOPE_LIMIT; depth += 1u) {
        ScopeFact *scope;
        if (child == parent) return true;
        scope = scope_fact(program, child);
        if (scope == NULL || scope->parent < 0) return false;
        child = scope->parent;
    }
    return false;
}

static bool validate_artifacts(Program *program) {
    size_t index;
    size_t target_count = 0u;
    for (index = 0u; index < program->module_count; index += 1u) {
        ModuleFact *module = &program->modules[index];
        size_t other;
        if (strcmp(module->path, program->logical_path) == 0) {
            target_count += 1u;
            memcpy(program->target_module_id, module->id, sizeof(module->id));
        }
        for (other = 0u; other < index; other += 1u) {
            if (strcmp(program->modules[other].id, module->id) == 0 ||
                strcmp(program->modules[other].path, module->path) == 0) {
                set_error(program, "E2S79", "duplicate module identity or logical path");
                return false;
            }
        }
    }
    if (target_count != 1u) {
        set_error(program, "E2S79", "logical path %s does not identify exactly one module", program->logical_path);
        return false;
    }
    for (index = 0u; index < program->adt_count; index += 1u) {
        AdtFact *adt = &program->adts[index];
        ModuleFact *module = module_by_index(program, adt->module_index);
        bool target = module != NULL && strcmp(module->id, program->target_module_id) == 0;
        size_t other;
        if (module == NULL || strcmp(module->id, adt->module_id) != 0 ||
            (target && (strcmp(adt->path, program->logical_path) != 0 ||
             !span_in_source(program, adt->declaration_start, adt->declaration_end) ||
             !source_equals(program, adt->name_start, adt->name_end, adt->name)))) {
            set_error(program, "E2S79", "stale ADT declaration %s in module-symbol artifact", adt->name);
            return false;
        }
        for (other = 0u; other < index; other += 1u) {
            if (strcmp(program->adts[other].id, adt->id) == 0) {
                set_error(program, "E2S79", "duplicate ADT SymbolId %s", adt->id);
                return false;
            }
        }
    }
    for (index = 0u; index < program->constructor_count; index += 1u) {
        ConstructorFact *constructor = &program->constructors[index];
        AdtFact *owner = adt_by_id(program, constructor->owner);
        bool target = owner != NULL && strcmp(owner->module_id, program->target_module_id) == 0;
        size_t after;
        size_t other;
        if (owner == NULL || owner->module_index != constructor->module_index ||
            strcmp(owner->module_id, constructor->module_id) != 0 ||
            (target && (strcmp(constructor->path, program->logical_path) != 0 ||
             !span_in_source(program, constructor->declaration_start, constructor->declaration_end) ||
             !source_equals(program, constructor->name_start, constructor->name_end, constructor->name)))) {
            set_error(program, "E2S79", "stale or unknown owner for constructor %s", constructor->name);
            return false;
        }
        if (target) {
            after = skip_trivia(program->source, program->source_length, constructor->name_end);
            constructor->arity = after < constructor->declaration_end &&
                source_token_equals(program, after, "(") ? 1u : 0u;
        } else {
            constructor->arity = constructor->declaration_end > constructor->name_end ? 1u : 0u;
        }
        for (other = 0u; other < index; other += 1u) {
            ConstructorFact *previous = &program->constructors[other];
            if (strcmp(previous->id, constructor->id) == 0 ||
                (strcmp(previous->owner, constructor->owner) == 0 &&
                 (previous->ordinal == constructor->ordinal ||
                  strcmp(previous->name, constructor->name) == 0))) {
                set_error(program, "E2S79", "duplicate constructor identity, ordinal, or name");
                return false;
            }
        }
    }
    for (index = 0u; index < program->adt_count; index += 1u) {
        size_t ordinal;
        size_t count = 0u;
        for (ordinal = 0u; ordinal < program->constructor_count; ordinal += 1u) {
            if (strcmp(program->constructors[ordinal].owner, program->adts[index].id) == 0) count += 1u;
        }
        if (count < 2u || count > ADT_LIMIT) {
            set_error(program, "E2S79", "ADT %s must have 2..%u constructors", program->adts[index].name, ADT_LIMIT);
            return false;
        }
        for (ordinal = 0u; ordinal < count; ordinal += 1u) {
            size_t candidate;
            bool found = false;
            for (candidate = 0u; candidate < program->constructor_count; candidate += 1u) {
                if (strcmp(program->constructors[candidate].owner, program->adts[index].id) == 0 &&
                    program->constructors[candidate].ordinal == ordinal) found = true;
            }
            if (!found) {
                set_error(program, "E2S79", "constructor ordinals for ADT %s are not contiguous", program->adts[index].name);
                return false;
            }
        }
    }
    for (index = 0u; index < program->node_count; index += 1u) {
        PatternNode *node = &program->nodes[index];
        size_t child;
        if (!span_in_source(program, node->start, node->end)) {
            set_error(program, "E2S79", "Pattern node %" PRId64 " has a stale span", node->id);
            return false;
        }
        if (node->kind == NODE_WILDCARD && !source_equals(program, node->start, node->end, "_")) {
            set_error(program, "E2S79", "WildcardPattern span is stale");
            return false;
        }
        if ((node->kind == NODE_NAME || node->kind == NODE_CONSTRUCTOR) &&
            (!source_equals(program, node->name_start, node->name_end, node->name) ||
             node->start != node->name_start)) {
            set_error(program, "E2S79", "named Pattern node %" PRId64 " is stale", node->id);
            return false;
        }
        if (node->kind == NODE_CONSTRUCTOR &&
            (!source_equals(program, node->open_start, node->open_end, "(") ||
             !source_equals(program, node->close_start, node->close_end, ")"))) {
            set_error(program, "E2S79", "ConstructorPattern delimiters are stale");
            return false;
        }
        for (child = 0u; child < node->child_count; child += 1u) {
            if (pattern_node(program, node->children[child]) == NULL) {
                set_error(program, "E2S79", "ConstructorPattern references unknown child %" PRId64, node->children[child]);
                return false;
            }
        }
    }
    for (index = 0u; index < program->pattern_match_count; index += 1u) {
        PatternMatch *match = &program->pattern_matches[index];
        size_t arm_count = 0u;
        size_t arm_index;
        size_t other;
        if (!source_token_equals(program, match->start, "match") ||
            !source_equals(program, match->open_start, match->open_end, "{") ||
            !source_equals(program, match->close_start, match->close_end, "}")) {
            set_error(program, "E2S79", "Pattern match %" PRId64 " has stale delimiters", match->source_id);
            return false;
        }
        for (other = 0u; other < index; other += 1u) {
            if (program->pattern_matches[other].source_id == match->source_id ||
                program->pattern_matches[other].start == match->start) {
                set_error(program, "E2S79", "duplicate Pattern match identity/span");
                return false;
            }
        }
        for (arm_index = 0u; arm_index < program->pattern_arm_count; arm_index += 1u) {
            PatternArm *arm = &program->pattern_arms[arm_index];
            if (arm->match_id != match->source_id) continue;
            arm_count += 1u;
            if (pattern_node(program, arm->root) == NULL || arm->arrow_start < 0 || arm->arrow_end < 0 ||
                !source_equals(program, (size_t)arm->arrow_start, (size_t)arm->arrow_end, "=>")) {
                set_error(program, "E2S79", "Pattern arm has unknown root or stale arrow");
                return false;
            }
        }
        if (arm_count != match->declared_arm_count) {
            set_error(program, "E2S79", "Pattern match arm count is stale");
            return false;
        }
        for (arm_index = 0u; arm_index < arm_count; arm_index += 1u) {
            size_t candidate;
            size_t occurrences = 0u;
            for (candidate = 0u; candidate < program->pattern_arm_count; candidate += 1u) {
                if (program->pattern_arms[candidate].match_id == match->source_id &&
                    program->pattern_arms[candidate].index == arm_index) occurrences += 1u;
            }
            if (occurrences != 1u) {
                set_error(program, "E2S79", "Pattern arm indices are duplicated or non-contiguous");
                return false;
            }
        }
    }
    for (index = 0u; index < program->scope_count; index += 1u) {
        ScopeFact *scope = &program->scopes[index];
        if (!span_in_source(program, scope->open_start, scope->close_end) ||
            (scope->parent >= 0 && scope_fact(program, scope->parent) == NULL)) {
            set_error(program, "E2S79", "ScopeId %" PRId64 " has stale spans or parent", scope->id);
            return false;
        }
        if (strcmp(scope->kind, "match-arm") == 0 &&
            (!source_token_equals(program, scope->open_start, "{") ||
             scope->close_end == 0u || program->source[scope->close_end - 1u] != '}')) {
            set_error(program, "E2S79", "match-arm ScopeId %" PRId64 " has stale delimiters", scope->id);
            return false;
        }
    }
    for (index = 0u; index < program->binding_count; index += 1u) {
        BindingFact *binding = &program->bindings[index];
        if (scope_fact(program, binding->scope_id) == NULL ||
            !source_equals(program, binding->start, binding->end, binding->name)) {
            set_error(program, "E2S79", "BindingId %" PRId64 " has stale identity/span", binding->id);
            return false;
        }
    }
    for (index = 0u; index < program->use_count; index += 1u) {
        UseFact *use = &program->uses[index];
        BindingFact *binding = binding_fact(program, use->binding_id);
        if (scope_fact(program, use->scope_id) == NULL || binding == NULL ||
            !source_equals(program, use->start, use->end, binding->name)) {
            set_error(program, "E2S79", "scope-HIR use at byte %zu is stale", use->start);
            return false;
        }
    }
    for (index = 0u; index < program->candidate_count; index += 1u) {
        CandidateUse *use = &program->candidates[index];
        if (scope_fact(program, use->scope_id) == NULL ||
            !source_equals(program, use->start, use->end, use->name) ||
            (strcmp(use->role, "read") != 0 && strcmp(use->role, "assign") != 0)) {
            set_error(program, "E2S79", "scope-HIR candidate use at byte %zu is stale", use->start);
            return false;
        }
    }
    return true;
}

static AdtFact *target_adt_by_name(Program *program, const char *name) {
    size_t index;
    for (index = 0u; index < program->adt_count; index += 1u) {
        AdtFact *adt = &program->adts[index];
        if (strcmp(adt->module_id, program->target_module_id) == 0 &&
            strcmp(adt->name, name) == 0) {
            return adt;
        }
    }
    return NULL;
}

static size_t adt_index_of(const Program *program, const AdtFact *adt) {
    return (size_t)(adt - program->adts);
}

static size_t constructor_index_of(
    const Program *program,
    const ConstructorFact *constructor
) {
    return (size_t)(constructor - program->constructors);
}

static ConstructorFact *constructor_for_ordinal(
    Program *program,
    const AdtFact *adt,
    size_t ordinal
) {
    size_t index;
    for (index = 0u; index < program->constructor_count; index += 1u) {
        ConstructorFact *constructor = &program->constructors[index];
        if (strcmp(constructor->owner, adt->id) == 0 &&
            constructor->ordinal == ordinal) {
            return constructor;
        }
    }
    return NULL;
}

static size_t adt_constructor_count(Program *program, const AdtFact *adt) {
    size_t index;
    size_t count = 0u;
    for (index = 0u; index < program->constructor_count; index += 1u) {
        if (strcmp(program->constructors[index].owner, adt->id) == 0) count += 1u;
    }
    return count;
}

static PatternArm *pattern_arm_for(
    Program *program,
    int64_t match_id,
    size_t arm_index
) {
    size_t index;
    for (index = 0u; index < program->pattern_arm_count; index += 1u) {
        PatternArm *arm = &program->pattern_arms[index];
        if (arm->match_id == match_id && arm->index == arm_index) return arm;
    }
    return NULL;
}

static ScopeFact *scope_opening_at(
    Program *program,
    size_t start,
    const char *kind
) {
    size_t index;
    for (index = 0u; index < program->scope_count; index += 1u) {
        ScopeFact *scope = &program->scopes[index];
        if (scope->open_start == start && strcmp(scope->kind, kind) == 0) return scope;
    }
    return NULL;
}

static UseFact *use_at(Program *program, size_t start, size_t end) {
    size_t index;
    for (index = 0u; index < program->use_count; index += 1u) {
        UseFact *use = &program->uses[index];
        if (use->start == start && use->end == end) return use;
    }
    return NULL;
}

static bool analysis_step(Program *program, uint64_t *operations) {
    *operations += UINT64_C(1);
    if (*operations <= OPERATION_LIMIT) return true;
    set_error(program, "E2S79",
        "ADT match analysis exceeds %" PRIu64 " operations; hint: split the match or ADT",
        OPERATION_LIMIT);
    return false;
}

static bool add_pattern_binding(
    Program *program,
    const TypedMatch *match,
    const TypedArm *arm,
    const PatternNode *node,
    const char *role,
    int64_t *binding_id
) {
    size_t index;
    PatternBinding *binding;
    if (program->pattern_binding_count >= PATTERN_BINDING_LIMIT ||
        program->next_binding_id == INT64_MAX) {
        set_error(program, "E2S79", "pattern binding limit is %u", PATTERN_BINDING_LIMIT);
        return false;
    }
    for (index = 0u; index < program->binding_count; index += 1u) {
        BindingFact *existing = &program->bindings[index];
        if (existing->scope_id == arm->scope_id && strcmp(existing->name, node->name) == 0) {
            set_error(program, "E2S79",
                "pattern binding `%s` at bytes %zu..%zu collides with BindingId %" PRId64
                " at bytes %zu..%zu",
                node->name, node->name_start, node->name_end, existing->id,
                existing->start, existing->end);
            return false;
        }
    }
    for (index = 0u; index < program->pattern_binding_count; index += 1u) {
        PatternBinding *existing = &program->pattern_bindings[index];
        if (existing->scope_id == arm->scope_id && strcmp(existing->name, node->name) == 0) {
            set_error(program, "E2S79",
                "duplicate pattern binding `%s` at bytes %zu..%zu; first binding is bytes %zu..%zu",
                node->name, node->name_start, node->name_end,
                existing->start, existing->end);
            return false;
        }
    }
    binding = &program->pattern_bindings[program->pattern_binding_count++];
    memset(binding, 0, sizeof(*binding));
    binding->binding_id = program->next_binding_id++;
    binding->scope_id = arm->scope_id;
    binding->match_index = match->id;
    binding->arm_index = arm->arm_index;
    binding->pattern_node = node->id;
    memcpy(binding->name, node->name, strlen(node->name) + 1u);
    binding->role = role;
    binding->start = node->name_start;
    binding->end = node->name_end;
    *binding_id = binding->binding_id;
    return true;
}

static bool arm_is_guarded(Program *program, const PatternArm *arm) {
    size_t cursor = skip_trivia(program->source, program->source_length, arm->end);
    return cursor < (size_t)arm->arrow_start && source_token_equals(program, cursor, "if");
}

static bool build_typed_arms(
    Program *program,
    TypedMatch *match,
    PatternMatch *source_match,
    const AdtFact *adt,
    uint64_t *operations
) {
    size_t arm_index;
    match->first_arm = program->arm_count;
    for (arm_index = 0u; arm_index < source_match->declared_arm_count; arm_index += 1u) {
        PatternArm *source_arm = pattern_arm_for(program, source_match->source_id, arm_index);
        PatternNode *root;
        ScopeFact *arm_scope;
        TypedArm *arm;
        ConstructorFact *constructor = NULL;
        size_t body_open;
        if (!analysis_step(program, operations)) return false;
        if (source_arm == NULL || source_arm->arrow_start < 0 || source_arm->arrow_end < 0) {
            set_error(program, "E2S79", "typed match has a missing Pattern arm %zu", arm_index);
            return false;
        }
        root = pattern_node(program, source_arm->root);
        body_open = skip_trivia(
            program->source,
            program->source_length,
            (size_t)source_arm->arrow_end
        );
        arm_scope = scope_opening_at(program, body_open, "match-arm");
        if (root == NULL || arm_scope == NULL || program->arm_count >= ARM_LIMIT) {
            set_error(program, "E2S79",
                "Pattern arm %zu cannot be joined to its match-arm ScopeId", arm_index);
            return false;
        }
        arm = &program->arms[program->arm_count++];
        memset(arm, 0, sizeof(*arm));
        arm->match_index = match->id;
        arm->arm_index = arm_index;
        arm->pattern_root = root->id;
        arm->constructor_index = -1;
        arm->scope_id = arm_scope->id;
        arm->binding_id = -1;
        arm->guarded = arm_is_guarded(program, source_arm);
        arm->start = source_arm->start;
        arm->end = source_arm->end;

        if (root->kind == NODE_WILDCARD) {
            arm->role = ARM_WILDCARD;
        } else if (root->kind == NODE_NAME) {
            constructor = owned_constructor(program, adt->id, root->name);
            if (constructor != NULL) {
                if (constructor->arity != 0u) {
                    set_error(program, "E2S79",
                        "payload constructor `%s` requires a whole `%s(_)` pattern at bytes %zu..%zu",
                        constructor->name, constructor->name, root->start, root->end);
                    return false;
                }
                arm->role = ARM_CONSTRUCTOR;
                arm->constructor_index = (int64_t)constructor_index_of(program, constructor);
            } else {
                arm->role = ARM_BINDING;
                if (!add_pattern_binding(program, match, arm, root, "catchall", &arm->binding_id)) {
                    return false;
                }
            }
        } else if (root->kind == NODE_CONSTRUCTOR) {
            PatternNode *child = NULL;
            constructor = owned_constructor(program, adt->id, root->name);
            if (constructor == NULL) {
                set_error(program, "E2S79",
                    "constructor `%s` at bytes %zu..%zu does not belong to ADT `%s`",
                    root->name, root->name_start, root->name_end, adt->name);
                return false;
            }
            if (constructor->arity != root->child_count || constructor->arity > 1u) {
                set_error(program, "E2S79",
                    "constructor pattern `%s` has %zu payload patterns but resolved arity is %u",
                    root->name, root->child_count, constructor->arity);
                return false;
            }
            if (root->child_count == 1u) child = pattern_node(program, root->children[0]);
            if (child != NULL && child->kind != NODE_WILDCARD && child->kind != NODE_NAME) {
                set_error(program, "E2S79",
                    "nested payload usefulness is unsupported at bytes %zu..%zu; hint: use `%s(_)` or one binding",
                    child->start, child->end, root->name);
                return false;
            }
            if (root->child_count == 1u && child == NULL) {
                set_error(program, "E2S79", "constructor pattern references an unknown payload node");
                return false;
            }
            arm->role = ARM_CONSTRUCTOR;
            arm->constructor_index = (int64_t)constructor_index_of(program, constructor);
            if (child != NULL && child->kind == NODE_NAME &&
                !add_pattern_binding(program, match, arm, child, "payload", &arm->binding_id)) {
                return false;
            }
        } else {
            set_error(program, "E2S79",
                "pattern form at bytes %zu..%zu is outside the bounded ADT exhaustiveness slice",
                root->start, root->end);
            return false;
        }
        match->arm_count += 1u;
    }
    return true;
}

static bool build_typed_matches(Program *program, uint64_t *operations) {
    size_t index;
    for (index = 0u; index < program->pattern_match_count; index += 1u) {
        PatternMatch *source_match = &program->pattern_matches[index];
        size_t scrutinee_start = skip_trivia(
            program->source,
            program->source_length,
            token_end(program->source, program->source_length, source_match->start)
        );
        size_t scrutinee_end = token_end(program->source, program->source_length, scrutinee_start);
        UseFact *scrutinee_use = use_at(program, scrutinee_start, scrutinee_end);
        BindingFact *scrutinee;
        AdtFact *adt;
        ScopeFact *match_scope;
        TypedMatch *match;
        if (!analysis_step(program, operations)) return false;
        if (scrutinee_use == NULL || scrutinee_start >= scrutinee_end ||
            !identifier_start(program->source[scrutinee_start])) {
            set_error(program, "E2S79",
                "match at byte %zu must use one resolved binding as its scrutinee",
                source_match->start);
            return false;
        }
        scrutinee = binding_fact(program, scrutinee_use->binding_id);
        adt = scrutinee == NULL ? NULL : target_adt_by_name(program, scrutinee->type_name);
        match_scope = scope_opening_at(program, source_match->open_start, "block");
        if (scrutinee == NULL || adt == NULL || match_scope == NULL ||
            program->match_count >= MATCH_LIMIT) {
            set_error(program, "E2S79",
                "match at byte %zu lacks a resolved nominal scrutinee or match ScopeId",
                source_match->start);
            return false;
        }
        match = &program->matches[program->match_count];
        memset(match, 0, sizeof(*match));
        match->id = program->match_count++;
        match->source_match_id = source_match->source_id;
        match->start = source_match->start;
        match->end = source_match->close_end;
        match->scrutinee_binding_id = scrutinee->id;
        match->scope_id = match_scope->id;
        match->adt_index = adt_index_of(program, adt);
        if (!build_typed_arms(program, match, source_match, adt, operations)) return false;
    }
    return true;
}

static PatternBinding *candidate_binding(
    Program *program,
    const CandidateUse *candidate
) {
    PatternBinding *result = NULL;
    size_t index;
    for (index = 0u; index < program->pattern_binding_count; index += 1u) {
        PatternBinding *binding = &program->pattern_bindings[index];
        ScopeFact *scope = scope_fact(program, binding->scope_id);
        if (scope != NULL && strcmp(binding->name, candidate->name) == 0 &&
            scope_is_descendant(program, candidate->scope_id, binding->scope_id) &&
            candidate->start >= scope->open_start && candidate->end <= scope->close_end) {
            if (result != NULL) {
                set_error(program, "E2S79",
                    "candidate use `%s` at byte %zu resolves to multiple pattern bindings",
                    candidate->name, candidate->start);
                return NULL;
            }
            result = binding;
        }
    }
    return result;
}

static bool resolve_pattern_uses(Program *program, uint64_t *operations) {
    size_t index;
    for (index = 0u; index < program->candidate_count; index += 1u) {
        CandidateUse *candidate = &program->candidates[index];
        PatternBinding *binding;
        if (!analysis_step(program, operations)) return false;
        binding = candidate_binding(program, candidate);
        if (binding == NULL) {
            if (!program->failed) {
                set_error(program, "E2S79",
                    "unresolved candidate use `%s` at bytes %zu..%zu",
                    candidate->name, candidate->start, candidate->end);
            }
            return false;
        }
    }
    return true;
}

static const char *arm_role_name(ArmRole role) {
    if (role == ARM_CONSTRUCTOR) return "constructor";
    if (role == ARM_WILDCARD) return "wildcard";
    return "binding";
}

static bool all_covered(const bool covered[], size_t count) {
    size_t index;
    for (index = 0u; index < count; index += 1u) {
        if (!covered[index]) return false;
    }
    return true;
}

static TypedArm *typed_arm_at(Program *program, const TypedMatch *match, size_t index) {
    if (index >= match->arm_count) return NULL;
    return &program->arms[match->first_arm + index];
}

static bool report_redundant(
    Program *program,
    const TypedArm *arm,
    const TypedArm *covering,
    const char *subject
) {
    set_error(program, "E2S26",
        "redundant ADT match arm at bytes %zu..%zu: %s is already covered by the arm at bytes %zu..%zu; hint: remove or reorder the redundant arm",
        arm->start, arm->end, subject, covering->start, covering->end);
    return false;
}

static bool analyze_match(
    Program *program,
    TypedMatch *match,
    uint64_t *operations
) {
    AdtFact *adt = &program->adts[match->adt_index];
    size_t constructor_count = adt_constructor_count(program, adt);
    bool covered[ADT_LIMIT] = { false };
    int64_t covering_arm[ADT_LIMIT];
    size_t arm_index;
    size_t index;
    for (index = 0u; index < ADT_LIMIT; index += 1u) covering_arm[index] = -1;
    for (arm_index = 0u; arm_index < match->arm_count; arm_index += 1u) {
        TypedArm *arm = typed_arm_at(program, match, arm_index);
        if (!analysis_step(program, operations) || arm == NULL) return false;
        if (arm->role == ARM_CONSTRUCTOR) {
            ConstructorFact *constructor = &program->constructors[(size_t)arm->constructor_index];
            size_t ordinal = constructor->ordinal;
            if (ordinal >= constructor_count) {
                set_error(program, "E2S79", "constructor ordinal is outside its ADT");
                return false;
            }
            if (covered[ordinal]) {
                int64_t covering_index = covering_arm[ordinal];
                TypedArm *covering = typed_arm_at(program, match, (size_t)covering_index);
                char subject[NAME_LIMIT + 32u];
                (void)snprintf(subject, sizeof(subject), "constructor `%s`", constructor->name);
                return report_redundant(program, arm, covering, subject);
            }
            if (!arm->guarded) {
                covered[ordinal] = true;
                covering_arm[ordinal] = (int64_t)arm_index;
            }
        } else {
            if (all_covered(covered, constructor_count)) {
                int64_t first = -1;
                for (index = 0u; index < constructor_count; index += 1u) {
                    if (covering_arm[index] >= 0 &&
                        (first < 0 || covering_arm[index] < first)) first = covering_arm[index];
                }
                return report_redundant(
                    program,
                    arm,
                    typed_arm_at(program, match, (size_t)first),
                    "the complete constructor set"
                );
            }
            if (!arm->guarded) {
                for (index = 0u; index < constructor_count; index += 1u) {
                    if (!analysis_step(program, operations)) return false;
                    if (!covered[index]) covering_arm[index] = (int64_t)arm_index;
                    covered[index] = true;
                }
            }
        }
    }
    if (!all_covered(covered, constructor_count)) {
        Buffer missing;
        size_t missing_count = 0u;
        size_t displayed = 0u;
        buffer_init(&missing);
        for (index = 0u; index < constructor_count; index += 1u) {
            ConstructorFact *constructor;
            if (!analysis_step(program, operations)) {
                free(missing.data);
                return false;
            }
            if (covered[index]) continue;
            constructor = constructor_for_ordinal(program, adt, index);
            missing_count += 1u;
            if (displayed < DISPLAY_CAP && constructor != NULL) {
                if (displayed != 0u) buffer_append(&missing, ", ");
                buffer_format(&missing, "%s%s", constructor->name,
                    constructor->arity == 0u ? "" : "(_)");
                displayed += 1u;
            }
        }
        if (missing_count > displayed) {
            buffer_format(&missing, ", and %zu more", missing_count - displayed);
        }
        set_error(program, "E2S25",
            "non-exhaustive match on ADT `%s` at bytes %zu..%zu; missing: %s; ADT declaration is bytes %zu..%zu; hint: add the missing constructor arms or an unguarded catch-all",
            adt->name, match->start, match->end, missing.data,
            adt->declaration_start, adt->declaration_end);
        free(missing.data);
        return false;
    }
    return true;
}

static bool analyze_matches(Program *program, uint64_t *operations) {
    size_t index;
    for (index = 0u; index < program->match_count; index += 1u) {
        if (!analyze_match(program, &program->matches[index], operations)) return false;
    }
    return true;
}

static void emit_typed_hir(Program *program, Buffer *output) {
    size_t index;
    buffer_append(output, "kofun-typed-adt-match/v1\n");
    buffer_format(output,
        "limits|operations=%" PRIu64 "|display-cap=%u|pattern-bindings=%u\n",
        OPERATION_LIMIT, DISPLAY_CAP, PATTERN_BINDING_LIMIT);
    for (index = 0u; index < program->match_count; index += 1u) {
        TypedMatch *match = &program->matches[index];
        AdtFact *adt = &program->adts[match->adt_index];
        buffer_format(output,
            "typed-match|id=%zu|source-id=%" PRId64 "|span=%zu..%zu|scope=%" PRId64
            "|scrutinee-binding=%" PRId64 "|adt=%s|adt-symbol=%s|module=%s\n",
            match->id, match->source_match_id, match->start, match->end,
            match->scope_id, match->scrutinee_binding_id, adt->name, adt->id,
            adt->module_id);
    }
    for (index = 0u; index < program->arm_count; index += 1u) {
        TypedArm *arm = &program->arms[index];
        const char *constructor_id = "-";
        const char *constructor_name = "-";
        if (arm->constructor_index >= 0) {
            ConstructorFact *constructor = &program->constructors[(size_t)arm->constructor_index];
            constructor_id = constructor->id;
            constructor_name = constructor->name;
        }
        buffer_format(output,
            "typed-arm|match=%zu|index=%zu|root=%" PRId64 "|role=%s|constructor=%s"
            "|constructor-symbol=%s|scope=%" PRId64 "|binding=%" PRId64
            "|guarded=%s|span=%zu..%zu\n",
            arm->match_index, arm->arm_index, arm->pattern_root,
            arm_role_name(arm->role), constructor_name, constructor_id,
            arm->scope_id, arm->binding_id, arm->guarded ? "yes" : "no",
            arm->start, arm->end);
    }
    for (index = 0u; index < program->pattern_binding_count; index += 1u) {
        PatternBinding *binding = &program->pattern_bindings[index];
        buffer_format(output,
            "pattern-binding|id=%" PRId64 "|scope=%" PRId64 "|match=%zu|arm=%zu"
            "|node=%" PRId64 "|name=%s|role=%s|span=%zu..%zu\n",
            binding->binding_id, binding->scope_id, binding->match_index,
            binding->arm_index, binding->pattern_node, binding->name,
            binding->role, binding->start, binding->end);
    }
    for (index = 0u; index < program->candidate_count; index += 1u) {
        CandidateUse *candidate = &program->candidates[index];
        PatternBinding *binding = candidate_binding(program, candidate);
        if (binding == NULL) continue;
        buffer_format(output,
            "pattern-use|span=%zu..%zu|scope=%" PRId64 "|binding=%" PRId64
            "|name=%s|role=%s\n",
            candidate->start, candidate->end, candidate->scope_id,
            binding->binding_id, candidate->name, candidate->role);
    }
}

static bool same_file(const char *left, const char *right) {
    struct stat left_status;
    struct stat right_status;
    if (strcmp(left, right) == 0) return true;
    return stat(left, &left_status) == 0 && stat(right, &right_status) == 0 &&
        left_status.st_dev == right_status.st_dev && left_status.st_ino == right_status.st_ino;
}

static bool write_transactional(Program *program, const char *path, const char *value) {
    size_t path_length = strlen(path);
    char *temporary = malloc(path_length + 48u);
    int descriptor = -1;
    FILE *output;
    unsigned attempt;
    bool write_ok;
    bool flush_ok;
    bool sync_ok;
    bool close_ok;
    if (temporary == NULL) {
        set_error(program, "E2S79", "output transaction allocation failed");
        return false;
    }
    for (attempt = 0u; attempt < 100u; attempt += 1u) {
        (void)snprintf(temporary, path_length + 48u, "%s.adt-tmp-%ld-%u",
            path, (long)getpid(), attempt);
        descriptor = open(temporary, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (descriptor >= 0) break;
        if (errno != EEXIST) break;
    }
    if (descriptor < 0) {
        free(temporary);
        set_error(program, "E2S79", "cannot create typed-match transaction output");
        return false;
    }
    output = fdopen(descriptor, "wb");
    if (output == NULL) {
        close(descriptor);
        (void)remove(temporary);
        free(temporary);
        set_error(program, "E2S79", "cannot open typed-match transaction stream");
        return false;
    }
    write_ok = fwrite(value, 1u, strlen(value), output) == strlen(value);
    flush_ok = fflush(output) == 0;
    sync_ok = fsync(descriptor) == 0;
    close_ok = fclose(output) == 0;
    if (!write_ok || !flush_ok || !sync_ok || !close_ok || rename(temporary, path) != 0) {
        (void)remove(temporary);
        free(temporary);
        set_error(program, "E2S79", "cannot commit typed-match output");
        return false;
    }
    free(temporary);
    return true;
}

static void remove_output(const char *path) {
    if (path != NULL) (void)remove(path);
}

int main(int argc, char **argv) {
    Program program;
    char *symbols = NULL;
    char *patterns = NULL;
    char *scopes = NULL;
    Buffer output;
    bool output_initialized = false;
    uint64_t operations = 0u;
    int status = 1;
    if (argc != 7) {
        fprintf(stderr,
            "usage: %s SOURCE LOGICAL_PATH MODULE_SYMBOLS PATTERN_TREE SCOPE_HIR OUTPUT\n",
            argv[0]);
        return 2;
    }
    memset(&program, 0, sizeof(program));
    if (same_file(argv[1], argv[6]) || same_file(argv[3], argv[6]) ||
        same_file(argv[4], argv[6]) || same_file(argv[5], argv[6])) {
        set_error(&program, "E2S79", "typed-match output must not alias an input artifact");
        goto done;
    }
    remove_output(argv[6]);
    if (!bounded_copy(&program, program.logical_path, sizeof(program.logical_path),
            argv[2], "logical path")) goto done;
    program.source = read_bounded(&program, argv[1], SOURCE_LIMIT);
    symbols = read_bounded(&program, argv[3], ARTIFACT_LIMIT);
    patterns = read_bounded(&program, argv[4], ARTIFACT_LIMIT);
    scopes = read_bounded(&program, argv[5], ARTIFACT_LIMIT);
    if (program.source == NULL || symbols == NULL || patterns == NULL || scopes == NULL) goto done;
    program.source_length = strlen(program.source);
    if (!parse_module_symbols(&program, symbols) ||
        !parse_pattern_artifact(&program, patterns) ||
        !parse_scope_artifact(&program, scopes) ||
        !validate_artifacts(&program) ||
        !build_typed_matches(&program, &operations) ||
        !resolve_pattern_uses(&program, &operations) ||
        !analyze_matches(&program, &operations)) goto done;
    buffer_init(&output);
    output_initialized = true;
    emit_typed_hir(&program, &output);
    if (program.failed || !write_transactional(&program, argv[6], output.data)) goto done;
    status = 0;
done:
    if (program.failed) printf("%s\n", program.error);
    if (status != 0) remove_output(argv[6]);
    if (output_initialized) free(output.data);
    free(scopes);
    free(patterns);
    free(symbols);
    free(program.source);
    return status;
}
