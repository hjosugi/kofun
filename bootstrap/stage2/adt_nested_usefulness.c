#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ID_LENGTH 64u
#define NAME_LIMIT 64u
#define LINE_LIMIT 1024u
#define INPUT_LIMIT (UINT64_C(1024) * UINT64_C(1024))
#define ADT_LIMIT 16u
#define CONSTRUCTOR_LIMIT 64u
#define ROW_LIMIT 128u
#define CELL_LIMIT 1024u
#define DISPLAY_LIMIT 8u
#define OPERATION_LIMIT UINT64_C(4096)

typedef struct {
    char id[ID_LENGTH + 1u];
    char name[NAME_LIMIT + 1u];
} Adt;

typedef struct {
    char id[ID_LENGTH + 1u];
    char owner[ID_LENGTH + 1u];
    char name[NAME_LIMIT + 1u];
    size_t ordinal;
    bool has_payload;
    char payload_owner[ID_LENGTH + 1u];
} Constructor;

typedef struct {
    bool outer_wildcard;
    char outer[ID_LENGTH + 1u];
    bool inner_wildcard;
    bool inner_absent;
    char inner[ID_LENGTH + 1u];
    size_t depth;
    size_t start;
    size_t end;
} Row;

typedef struct {
    size_t outer;
    int64_t inner;
} Cell;

typedef struct {
    Adt adts[ADT_LIMIT];
    size_t adt_count;
    Constructor constructors[CONSTRUCTOR_LIMIT];
    size_t constructor_count;
    Row rows[ROW_LIMIT];
    size_t row_count;
    char target[ID_LENGTH + 1u];
    bool has_target;
    char code[16];
    char message[2048];
} Program;

static bool fail(Program *program, const char *code, const char *message) {
    if (program->code[0] == '\0') {
        (void)snprintf(program->code, sizeof(program->code), "%s", code);
        (void)snprintf(program->message, sizeof(program->message), "%s", message);
    }
    return false;
}

static bool fail_line(
    Program *program,
    const char *code,
    const char *subject,
    size_t line
) {
    char message[256];
    (void)snprintf(message, sizeof(message), "%s at line %zu", subject, line);
    return fail(program, code, message);
}

static bool valid_id(const char *value) {
    size_t index;
    if (strlen(value) != ID_LENGTH) return false;
    for (index = 0u; index < ID_LENGTH; index += 1u) {
        char byte = value[index];
        if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) return false;
    }
    return true;
}

static bool valid_name(const char *value) {
    size_t index;
    size_t length = strlen(value);
    if (length == 0u || length > NAME_LIMIT ||
        !((value[0] >= 'A' && value[0] <= 'Z') ||
          (value[0] >= 'a' && value[0] <= 'z') || value[0] == '_')) {
        return false;
    }
    for (index = 1u; index < length; index += 1u) {
        char byte = value[index];
        if (!((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
              (byte >= '0' && byte <= '9') || byte == '_')) {
            return false;
        }
    }
    return true;
}

static bool parse_size(const char *value, size_t *result) {
    char *end = NULL;
    uintmax_t parsed;
    if (value[0] == '\0' || value[0] == '-') return false;
    errno = 0;
    parsed = strtoumax(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed > SIZE_MAX) return false;
    *result = (size_t)parsed;
    return true;
}

static bool parse_span(const char *value, size_t *start, size_t *end) {
    const char *separator = strstr(value, "..");
    char left[32];
    size_t length;
    if (separator == NULL || strstr(separator + 2, "..") != NULL) return false;
    length = (size_t)(separator - value);
    if (length == 0u || length >= sizeof(left)) return false;
    memcpy(left, value, length);
    left[length] = '\0';
    if (!parse_size(left, start) || !parse_size(separator + 2, end)) return false;
    return *start < *end;
}

static size_t split_fields(char *line, char *fields[], size_t capacity) {
    size_t count = 1u;
    char *cursor;
    fields[0] = line;
    for (cursor = line; *cursor != '\0'; cursor += 1) {
        if (*cursor != '|') continue;
        *cursor = '\0';
        if (count >= capacity) return capacity + 1u;
        fields[count++] = cursor + 1;
    }
    return count;
}

static const char *ordered_value(const char *field, const char *key) {
    size_t length = strlen(key);
    if (strncmp(field, key, length) != 0 || field[length] != '=') return NULL;
    return field + length + 1u;
}

static bool copy_id(char output[ID_LENGTH + 1u], const char *value) {
    if (!valid_id(value)) return false;
    memcpy(output, value, ID_LENGTH + 1u);
    return true;
}

static bool copy_name(char output[NAME_LIMIT + 1u], const char *value) {
    size_t length;
    if (!valid_name(value)) return false;
    length = strlen(value);
    memcpy(output, value, length + 1u);
    return true;
}

static bool parse_adt(Program *program, char *fields[], size_t count, size_t line) {
    const char *id;
    const char *name;
    Adt *adt;
    if (count != 3u || (id = ordered_value(fields[1], "id")) == NULL ||
        (name = ordered_value(fields[2], "name")) == NULL) {
        return fail_line(program, "E2S110", "malformed ADT record", line);
    }
    if (program->adt_count >= ADT_LIMIT) {
        return fail(program, "E2S110", "nested usefulness input exceeds 16 ADTs");
    }
    adt = &program->adts[program->adt_count++];
    memset(adt, 0, sizeof(*adt));
    if (!copy_id(adt->id, id) || !copy_name(adt->name, name)) {
        return fail_line(program, "E2S110", "invalid ADT identity or name", line);
    }
    return true;
}

static bool parse_constructor(
    Program *program,
    char *fields[],
    size_t count,
    size_t line
) {
    const char *id;
    const char *owner;
    const char *ordinal;
    const char *name;
    const char *payload_owner;
    Constructor *constructor;
    if (count != 6u || (id = ordered_value(fields[1], "id")) == NULL ||
        (owner = ordered_value(fields[2], "owner")) == NULL ||
        (ordinal = ordered_value(fields[3], "ordinal")) == NULL ||
        (name = ordered_value(fields[4], "name")) == NULL ||
        (payload_owner = ordered_value(fields[5], "payload-owner")) == NULL) {
        return fail_line(program, "E2S110", "malformed constructor record", line);
    }
    if (program->constructor_count >= CONSTRUCTOR_LIMIT) {
        return fail(program, "E2S110", "nested usefulness input exceeds 64 constructors");
    }
    constructor = &program->constructors[program->constructor_count++];
    memset(constructor, 0, sizeof(*constructor));
    if (!copy_id(constructor->id, id) || !copy_id(constructor->owner, owner) ||
        !parse_size(ordinal, &constructor->ordinal) ||
        !copy_name(constructor->name, name)) {
        return fail_line(program, "E2S110", "invalid constructor identity, ordinal, or name", line);
    }
    if (strcmp(payload_owner, "-") != 0) {
        if (!copy_id(constructor->payload_owner, payload_owner)) {
            return fail_line(program, "E2S110", "invalid payload-owner identity", line);
        }
        constructor->has_payload = true;
    }
    return true;
}

static bool parse_target(Program *program, char *fields[], size_t count, size_t line) {
    const char *adt;
    if (count != 2u || (adt = ordered_value(fields[1], "adt")) == NULL ||
        !copy_id(program->target, adt) || program->has_target) {
        return fail_line(program, "E2S110", "malformed or duplicate target record", line);
    }
    program->has_target = true;
    return true;
}

static bool parse_row(Program *program, char *fields[], size_t count, size_t line) {
    const char *outer;
    const char *inner;
    const char *depth;
    const char *span;
    Row *row;
    if (count != 5u || (outer = ordered_value(fields[1], "outer")) == NULL ||
        (inner = ordered_value(fields[2], "inner")) == NULL ||
        (depth = ordered_value(fields[3], "depth")) == NULL ||
        (span = ordered_value(fields[4], "span")) == NULL) {
        return fail_line(program, "E2S110", "malformed row record", line);
    }
    if (program->row_count >= ROW_LIMIT) {
        return fail(program, "E2S110", "nested usefulness input exceeds 128 rows");
    }
    row = &program->rows[program->row_count++];
    memset(row, 0, sizeof(*row));
    if (strcmp(outer, "*") == 0) row->outer_wildcard = true;
    else if (!copy_id(row->outer, outer)) {
        return fail_line(program, "E2S110", "invalid outer constructor identity", line);
    }
    if (strcmp(inner, "*") == 0) row->inner_wildcard = true;
    else if (strcmp(inner, "-") == 0) row->inner_absent = true;
    else if (!copy_id(row->inner, inner)) {
        return fail_line(program, "E2S110", "invalid inner constructor identity", line);
    }
    if (!parse_size(depth, &row->depth) || !parse_span(span, &row->start, &row->end)) {
        return fail_line(program, "E2S110", "invalid row depth or span", line);
    }
    return true;
}

static bool read_input(Program *program, const char *path) {
    FILE *input;
    char line[LINE_LIMIT + 2u];
    size_t line_number = 0u;
    uint64_t bytes = 0u;
    bool header = false;
    input = fopen(path, "rb");
    if (input == NULL) return fail(program, "E2S110", "cannot open nested usefulness input");
    while (fgets(line, sizeof(line), input) != NULL) {
        char *fields[8];
        size_t length = strlen(line);
        size_t count;
        line_number += 1u;
        bytes += (uint64_t)length;
        if (bytes > INPUT_LIMIT) {
            (void)fclose(input);
            return fail(program, "E2S110", "nested usefulness input exceeds 1048576 bytes");
        }
        if (length == 0u || (line[length - 1u] != '\n' && !feof(input))) {
            (void)fclose(input);
            return fail_line(program, "E2S110", "input line exceeds 1024 bytes", line_number);
        }
        while (length > 0u && (line[length - 1u] == '\n' || line[length - 1u] == '\r')) {
            line[--length] = '\0';
        }
        if (!header) {
            if (strcmp(line, "kofun-adt-nested-usefulness/v1") != 0) {
                (void)fclose(input);
                return fail_line(program, "E2S110", "invalid nested usefulness header", line_number);
            }
            header = true;
            continue;
        }
        if (line[0] == '\0') continue;
        count = split_fields(line, fields, sizeof(fields) / sizeof(fields[0]));
        if (count > sizeof(fields) / sizeof(fields[0])) {
            (void)fclose(input);
            return fail_line(program, "E2S110", "record has too many fields", line_number);
        }
        if (strcmp(fields[0], "adt") == 0) {
            if (!parse_adt(program, fields, count, line_number)) {
                (void)fclose(input);
                return false;
            }
        } else if (strcmp(fields[0], "constructor") == 0) {
            if (!parse_constructor(program, fields, count, line_number)) {
                (void)fclose(input);
                return false;
            }
        } else if (strcmp(fields[0], "target") == 0) {
            if (!parse_target(program, fields, count, line_number)) {
                (void)fclose(input);
                return false;
            }
        } else if (strcmp(fields[0], "row") == 0) {
            if (!parse_row(program, fields, count, line_number)) {
                (void)fclose(input);
                return false;
            }
        } else {
            (void)fclose(input);
            return fail_line(program, "E2S110", "unknown nested usefulness record", line_number);
        }
    }
    {
        bool read_failed = ferror(input) != 0;
        bool close_failed = fclose(input) != 0;
        if (read_failed || close_failed) {
            return fail(program, "E2S110", "cannot read complete nested usefulness input");
        }
    }
    if (!header || program->adt_count == 0u || program->constructor_count == 0u ||
        program->row_count == 0u || !program->has_target) {
        return fail(program, "E2S110", "nested usefulness input is incomplete");
    }
    return true;
}

static int64_t adt_index(const Program *program, const char *id) {
    size_t index;
    for (index = 0u; index < program->adt_count; index += 1u) {
        if (strcmp(program->adts[index].id, id) == 0) return (int64_t)index;
    }
    return -1;
}

static int64_t constructor_index(const Program *program, const char *id) {
    size_t index;
    for (index = 0u; index < program->constructor_count; index += 1u) {
        if (strcmp(program->constructors[index].id, id) == 0) return (int64_t)index;
    }
    return -1;
}

static size_t owner_constructor_count(const Program *program, const char *owner) {
    size_t index;
    size_t count = 0u;
    for (index = 0u; index < program->constructor_count; index += 1u) {
        if (strcmp(program->constructors[index].owner, owner) == 0) count += 1u;
    }
    return count;
}

static int64_t constructor_at_ordinal(
    const Program *program,
    const char *owner,
    size_t ordinal
) {
    size_t index;
    for (index = 0u; index < program->constructor_count; index += 1u) {
        const Constructor *constructor = &program->constructors[index];
        if (strcmp(constructor->owner, owner) == 0 && constructor->ordinal == ordinal) {
            return (int64_t)index;
        }
    }
    return -1;
}

static bool validate_model(Program *program) {
    size_t index;
    int64_t target = adt_index(program, program->target);
    if (target < 0) return fail(program, "E2S110", "target ADT identity is absent");
    for (index = 0u; index < program->adt_count; index += 1u) {
        size_t other;
        size_t count = owner_constructor_count(program, program->adts[index].id);
        if (count < 1u) return fail(program, "E2S110", "each ADT requires at least one constructor");
        for (other = 0u; other < index; other += 1u) {
            if (strcmp(program->adts[index].id, program->adts[other].id) == 0) {
                return fail(program, "E2S110", "duplicate ADT identity");
            }
        }
        for (other = 0u; other < count; other += 1u) {
            if (constructor_at_ordinal(program, program->adts[index].id, other) < 0) {
                return fail(program, "E2S110", "constructor ordinals are not contiguous");
            }
        }
    }
    for (index = 0u; index < program->constructor_count; index += 1u) {
        const Constructor *constructor = &program->constructors[index];
        size_t other;
        if (adt_index(program, constructor->owner) < 0) {
            return fail(program, "E2S110", "constructor owner identity is absent");
        }
        if (constructor->has_payload &&
            (adt_index(program, constructor->payload_owner) < 0 ||
             strcmp(constructor->owner, constructor->payload_owner) == 0)) {
            return fail(program, "E2S110", "payload owner is absent or recursively self-owned");
        }
        for (other = 0u; other < index; other += 1u) {
            const Constructor *previous = &program->constructors[other];
            if (strcmp(constructor->id, previous->id) == 0 ||
                (strcmp(constructor->owner, previous->owner) == 0 &&
                 (constructor->ordinal == previous->ordinal ||
                  strcmp(constructor->name, previous->name) == 0))) {
                return fail(program, "E2S110", "duplicate constructor identity, ordinal, or owner-local name");
            }
        }
    }
    for (index = 0u; index < program->row_count; index += 1u) {
        Row *row = &program->rows[index];
        int64_t outer_index;
        if (row->depth > 1u) {
            return fail(program, "E2S110", "nested usefulness row exceeds depth 1");
        }
        if (row->outer_wildcard) {
            if (!row->inner_absent || row->depth != 0u) {
                return fail(program, "E2S110", "outer wildcard row must use inner=- and depth=0");
            }
            continue;
        }
        outer_index = constructor_index(program, row->outer);
        if (outer_index < 0 ||
            strcmp(program->constructors[(size_t)outer_index].owner, program->target) != 0) {
            return fail(program, "E2S110", "row outer constructor is not owned by target ADT");
        }
        {
            Constructor *outer = &program->constructors[(size_t)outer_index];
            if (!outer->has_payload) {
                if (!row->inner_absent || row->depth != 0u) {
                    return fail(program, "E2S110", "payload-free outer row must use inner=- and depth=0");
                }
            } else if (row->inner_absent || row->depth != 1u) {
                return fail(program, "E2S110", "payload outer row requires depth=1 and an inner constructor or wildcard");
            } else if (!row->inner_wildcard) {
                int64_t inner_index = constructor_index(program, row->inner);
                if (inner_index < 0 ||
                    strcmp(program->constructors[(size_t)inner_index].owner,
                        outer->payload_owner) != 0) {
                    return fail(program, "E2S110", "nested constructor is not owned by resolved payload ADT");
                }
            }
        }
    }
    return true;
}

static bool add_cell(Program *program, Cell cells[], size_t *count, size_t outer, int64_t inner) {
    if (*count >= CELL_LIMIT) return fail(program, "E2S110", "nested usefulness matrix exceeds 1024 cells");
    cells[*count].outer = outer;
    cells[*count].inner = inner;
    *count += 1u;
    return true;
}

static bool build_cells(Program *program, Cell cells[], size_t *count) {
    size_t ordinal;
    size_t outer_count = owner_constructor_count(program, program->target);
    *count = 0u;
    for (ordinal = 0u; ordinal < outer_count; ordinal += 1u) {
        int64_t outer_index = constructor_at_ordinal(program, program->target, ordinal);
        Constructor *outer;
        if (outer_index < 0) return fail(program, "E2S110", "target constructor ordinal is absent");
        outer = &program->constructors[(size_t)outer_index];
        if (!outer->has_payload) {
            if (!add_cell(program, cells, count, (size_t)outer_index, -1)) return false;
        } else {
            size_t inner_ordinal;
            size_t inner_count = owner_constructor_count(program, outer->payload_owner);
            for (inner_ordinal = 0u; inner_ordinal < inner_count; inner_ordinal += 1u) {
                int64_t inner_index = constructor_at_ordinal(
                    program, outer->payload_owner, inner_ordinal);
                if (inner_index < 0 ||
                    !add_cell(program, cells, count, (size_t)outer_index, inner_index)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool row_covers(const Program *program, const Row *row, const Cell *cell) {
    const Constructor *outer = &program->constructors[cell->outer];
    if (row->outer_wildcard) return true;
    if (strcmp(row->outer, outer->id) != 0) return false;
    if (cell->inner < 0) return row->inner_absent;
    return row->inner_wildcard ||
        strcmp(row->inner, program->constructors[(size_t)cell->inner].id) == 0;
}

static void witness(
    const Program *program,
    const Cell *cell,
    char *output,
    size_t capacity
) {
    const Constructor *outer = &program->constructors[cell->outer];
    if (cell->inner < 0) {
        (void)snprintf(output, capacity, "%s", outer->name);
    } else {
        const Constructor *inner = &program->constructors[(size_t)cell->inner];
        (void)snprintf(output, capacity, "%s(%s)", outer->name, inner->name);
    }
}

static bool analysis_step(Program *program, uint64_t *operations) {
    *operations += UINT64_C(1);
    if (*operations <= OPERATION_LIMIT) return true;
    return fail(program, "E2S110", "nested usefulness analysis exceeds 4096 operations");
}

static bool paths_alias(const char *input, const char *output) {
    struct stat input_stat;
    struct stat output_stat;
    if (strcmp(input, output) == 0) return true;
    if (stat(input, &input_stat) != 0 || stat(output, &output_stat) != 0) {
        return false;
    }
    return input_stat.st_dev == output_stat.st_dev &&
        input_stat.st_ino == output_stat.st_ino;
}

static bool analyze(
    Program *program,
    const Cell cells[],
    size_t cell_count,
    uint64_t *operations
) {
    bool covered[CELL_LIMIT] = { false };
    int64_t covering[CELL_LIMIT];
    size_t row_index;
    size_t index;
    for (index = 0u; index < cell_count; index += 1u) covering[index] = -1;
    *operations = 0u;
    for (row_index = 0u; row_index < program->row_count; row_index += 1u) {
        Row *row = &program->rows[row_index];
        size_t denoted = 0u;
        size_t novel = 0u;
        int64_t first_cover = -1;
        size_t single_cell = 0u;
        for (index = 0u; index < cell_count; index += 1u) {
            if (!analysis_step(program, operations)) return false;
            if (!row_covers(program, row, &cells[index])) continue;
            denoted += 1u;
            single_cell = index;
            if (!covered[index]) novel += 1u;
            else if (first_cover < 0) first_cover = covering[index];
        }
        if (denoted == 0u) return fail(program, "E2S110", "nested usefulness row denotes no case");
        if (novel == 0u) {
            char current[2u * NAME_LIMIT + 4u];
            char message[512];
            witness(program, &cells[single_cell], current, sizeof(current));
            if (denoted == 1u && first_cover >= 0) {
                Row *earlier = &program->rows[(size_t)first_cover];
                bool whole = earlier->outer_wildcard || earlier->inner_wildcard;
                (void)snprintf(message, sizeof(message),
                    "redundant nested usefulness row at bytes %zu..%zu: case `%s` is already covered by the earlier %s at bytes %zu..%zu; hint: remove the redundant row",
                    row->start, row->end, current,
                    whole ? "whole-constructor row" : "nested row",
                    earlier->start, earlier->end);
            } else {
                (void)snprintf(message, sizeof(message),
                    "redundant nested usefulness row at bytes %zu..%zu: all %zu denoted cases are already covered by earlier rows; hint: remove the redundant row",
                    row->start, row->end, denoted);
            }
            return fail(program, "E2S26", message);
        }
        for (index = 0u; index < cell_count; index += 1u) {
            if (!analysis_step(program, operations)) return false;
            if (row_covers(program, row, &cells[index]) && !covered[index]) {
                covered[index] = true;
                covering[index] = (int64_t)row_index;
            }
        }
    }
    {
        size_t missing_count = 0u;
        char displayed[1200] = "";
        size_t used = 0u;
        for (index = 0u; index < cell_count; index += 1u) {
            if (!analysis_step(program, operations)) return false;
            if (!covered[index]) {
                char item[2u * NAME_LIMIT + 4u];
                missing_count += 1u;
                if (missing_count > DISPLAY_LIMIT) continue;
                witness(program, &cells[index], item, sizeof(item));
                used += (size_t)snprintf(displayed + used, sizeof(displayed) - used,
                    "%s`%s`", used == 0u ? "" : ", ", item);
            }
        }
        if (missing_count != 0u) {
            char message[1536];
            const Adt *target = &program->adts[(size_t)adt_index(program, program->target)];
            if (missing_count > DISPLAY_LIMIT) {
                (void)snprintf(displayed + used, sizeof(displayed) - used,
                    ", and %zu more", missing_count - DISPLAY_LIMIT);
            }
            (void)snprintf(message, sizeof(message),
                "non-exhaustive nested ADT matrix for `%s`: missing %s; hint: add the missing cases or a wildcard row",
                target->name, displayed);
            return fail(program, "E2S25", message);
        }
    }
    return true;
}

static bool publish(
    Program *program,
    const char *path,
    const Cell cells[],
    size_t cell_count,
    uint64_t operations
) {
    char temporary[4096];
    FILE *output;
    size_t index;
    const Adt *target = &program->adts[(size_t)adt_index(program, program->target)];
    if (strlen(path) + 5u >= sizeof(temporary)) {
        return fail(program, "E2S110", "nested usefulness output path is too long");
    }
    (void)snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    (void)remove(temporary);
    output = fopen(temporary, "wb");
    if (output == NULL) return fail(program, "E2S110", "cannot create nested usefulness transaction");
    fprintf(output, "kofun-adt-nested-usefulness-result/v1\n");
    fprintf(output, "target|adt=%s|name=%s\n", target->id, target->name);
    for (index = 0u; index < cell_count; index += 1u) {
        char item[2u * NAME_LIMIT + 4u];
        witness(program, &cells[index], item, sizeof(item));
        fprintf(output, "cell|index=%zu|witness=%s|outer=%s|inner=%s\n",
            index, item, program->constructors[cells[index].outer].id,
            cells[index].inner < 0 ? "-" : program->constructors[(size_t)cells[index].inner].id);
    }
    fprintf(output, "complete|rows=%zu|cells=%zu|operations=%" PRIu64 "\n",
        program->row_count, cell_count, operations);
    if (fclose(output) != 0 || rename(temporary, path) != 0) {
        (void)remove(temporary);
        (void)remove(path);
        return fail(program, "E2S110", "cannot commit nested usefulness output");
    }
    return true;
}

int main(int argc, char **argv) {
    Program program;
    Cell cells[CELL_LIMIT];
    char temporary[4096];
    size_t cell_count = 0u;
    uint64_t operations = 0u;
    memset(&program, 0, sizeof(program));
    if (argc != 3) {
        fprintf(stderr, "usage: %s INPUT.matrix OUTPUT.result\n", argc > 0 ? argv[0] : "adt-nested-usefulness");
        return 2;
    }
    if (strlen(argv[2]) + 5u >= sizeof(temporary)) {
        fprintf(stderr, "error[E2S110]: nested usefulness output path is too long\n");
        return 1;
    }
    (void)snprintf(temporary, sizeof(temporary), "%s.tmp", argv[2]);
    if (paths_alias(argv[1], argv[2]) || paths_alias(argv[1], temporary)) {
        fprintf(stderr, "error[E2S110]: input and output paths must differ\n");
        return 1;
    }
    (void)remove(argv[2]);
    if (!read_input(&program, argv[1]) || !validate_model(&program) ||
        !build_cells(&program, cells, &cell_count) ||
        !analyze(&program, cells, cell_count, &operations) ||
        !publish(&program, argv[2], cells, cell_count, operations)) {
        (void)remove(argv[2]);
        fprintf(stderr, "error[%s]: %s\n", program.code, program.message);
        return 1;
    }
    return 0;
}
