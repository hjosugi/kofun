#include "visibility_access.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_CAPACITY 2048u
#define FIELD_COUNT 15u
#define BOUNDARY_CAPACITY (KOFUN_ACCESS_MAX_ENCLOSING + 1u)

static void die(const char *message, const char *detail) {
    if (detail == NULL) {
        fprintf(stderr, "visibility-access driver: %s\n", message);
    } else {
        fprintf(stderr, "visibility-access driver: %s: %s\n", message, detail);
    }
    exit(2);
}

static uint32_t parse_number(const char *text) {
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        die("invalid decimal number", text);
    }
    return (uint32_t)value;
}

static KofunIdentity make_identity(
    KofunIdentityKind kind,
    uint16_t schema,
    uint32_t seed
) {
    size_t index;
    KofunIdentity identity;

    identity.schema = schema;
    identity.kind = kind;
    for (index = 0; index < KOFUN_IDENTITY_BYTES; index += 1) {
        identity.bytes[index] = (uint8_t)(
            (seed * 31u + (uint32_t)kind * 19u +
                (uint32_t)index * 17u) & 0xffu
        );
    }
    return identity;
}

static bool parse_optional_identity(
    const char *text,
    KofunIdentityKind kind,
    uint16_t schema,
    KofunIdentity *output
) {
    if (strcmp(text, "-") == 0) {
        memset(output, 0, sizeof(*output));
        return false;
    }
    *output = make_identity(kind, schema, parse_number(text));
    return true;
}

static KofunVisibility parse_visibility(const char *text) {
    if (strcmp(text, "private") == 0) return KOFUN_VISIBILITY_PRIVATE;
    if (strcmp(text, "internal") == 0) return KOFUN_VISIBILITY_INTERNAL;
    if (strcmp(text, "pub") == 0) return KOFUN_VISIBILITY_PUBLIC;
    if (strcmp(text, "restricted") == 0) return KOFUN_VISIBILITY_RESTRICTED;
    if (strcmp(text, "unknown") == 0) return KOFUN_VISIBILITY_UNKNOWN;
    die("invalid visibility", text);
    return KOFUN_VISIBILITY_UNKNOWN;
}

static size_t split_fields(char *line, char **fields) {
    size_t count = 0;
    char *token = strtok(line, "|");

    while (token != NULL && count < FIELD_COUNT) {
        fields[count] = token;
        count += 1;
        token = strtok(NULL, "|");
    }
    if (token != NULL) die("too many fields", line);
    return count;
}

static KofunEffectiveBoundary base_boundary(
    const KofunDeclarationAccess *declaration,
    KofunVisibility visibility
) {
    KofunEffectiveBoundary boundary;
    boundary.visibility = visibility;
    boundary.defining_package = declaration->defining_package;
    boundary.defining_module = declaration->defining_module;
    boundary.defining_file = declaration->defining_file;
    boundary.has_defining_owner = declaration->has_defining_owner;
    boundary.defining_owner = declaration->defining_owner;
    return boundary;
}

static size_t build_boundaries(
    const char *shape,
    const KofunDeclarationAccess *declaration,
    KofunEffectiveBoundary *boundaries
) {
    size_t index;

    if (strcmp(shape, "none") == 0 ||
        strcmp(shape, "invalid-use-span") == 0) {
        return 0;
    }
    if (strcmp(shape, "depth64") == 0 || strcmp(shape, "depth65") == 0) {
        size_t count = strcmp(shape, "depth64") == 0
            ? KOFUN_ACCESS_MAX_ENCLOSING
            : BOUNDARY_CAPACITY;
        for (index = 0; index < count; index += 1) {
            boundaries[index] = base_boundary(
                declaration,
                KOFUN_VISIBILITY_PUBLIC
            );
        }
        return count;
    }
    if (strcmp(shape, "public") == 0) {
        boundaries[0] = base_boundary(declaration, KOFUN_VISIBILITY_PUBLIC);
        return 1;
    }
    if (strcmp(shape, "internal-same") == 0) {
        boundaries[0] = base_boundary(declaration, KOFUN_VISIBILITY_INTERNAL);
        return 1;
    }
    if (strcmp(shape, "internal-other") == 0) {
        boundaries[0] = base_boundary(declaration, KOFUN_VISIBILITY_INTERNAL);
        boundaries[0].defining_package = make_identity(
            KOFUN_ID_PACKAGE,
            KOFUN_IDENTITY_SCHEMA_V1,
            99
        );
        return 1;
    }
    if (strcmp(shape, "mixed-order-a") == 0 ||
        strcmp(shape, "mixed-order-b") == 0) {
        KofunEffectiveBoundary internal = base_boundary(
            declaration,
            KOFUN_VISIBILITY_INTERNAL
        );
        KofunEffectiveBoundary private_file = base_boundary(
            declaration,
            KOFUN_VISIBILITY_PRIVATE
        );
        internal.defining_package = make_identity(
            KOFUN_ID_PACKAGE,
            KOFUN_IDENTITY_SCHEMA_V1,
            99
        );
        private_file.has_defining_owner = false;
        private_file.defining_file = make_identity(
            KOFUN_ID_FILE,
            KOFUN_IDENTITY_SCHEMA_V1,
            99
        );
        if (strcmp(shape, "mixed-order-a") == 0) {
            boundaries[0] = internal;
            boundaries[1] = private_file;
        } else {
            boundaries[0] = private_file;
            boundaries[1] = internal;
        }
        return 2;
    }
    if (strcmp(shape, "private-file-same") == 0) {
        boundaries[0] = base_boundary(declaration, KOFUN_VISIBILITY_PRIVATE);
        boundaries[0].has_defining_owner = false;
        return 1;
    }
    if (strcmp(shape, "private-file-other") == 0) {
        boundaries[0] = base_boundary(declaration, KOFUN_VISIBILITY_PRIVATE);
        boundaries[0].has_defining_owner = false;
        boundaries[0].defining_file = make_identity(
            KOFUN_ID_FILE,
            KOFUN_IDENTITY_SCHEMA_V1,
            99
        );
        return 1;
    }
    if (strcmp(shape, "private-owner-same") == 0) {
        boundaries[0] = base_boundary(declaration, KOFUN_VISIBILITY_PRIVATE);
        boundaries[0].has_defining_owner = true;
        if (!declaration->has_defining_owner) {
            boundaries[0].defining_owner = make_identity(
                KOFUN_ID_TYPE,
                KOFUN_IDENTITY_SCHEMA_V1,
                31
            );
        }
        return 1;
    }
    if (strcmp(shape, "private-owner-other") == 0) {
        boundaries[0] = base_boundary(declaration, KOFUN_VISIBILITY_PRIVATE);
        boundaries[0].has_defining_owner = true;
        boundaries[0].defining_owner = make_identity(
            KOFUN_ID_TYPE,
            KOFUN_IDENTITY_SCHEMA_V1,
            99
        );
        return 1;
    }
    if (strcmp(shape, "restricted") == 0) {
        boundaries[0] = base_boundary(
            declaration,
            KOFUN_VISIBILITY_RESTRICTED
        );
        return 1;
    }
    if (strcmp(shape, "invalid-schema") == 0) {
        boundaries[0] = base_boundary(declaration, KOFUN_VISIBILITY_PUBLIC);
        boundaries[0].defining_module.schema = 2;
        return 1;
    }
    die("invalid boundary shape", shape);
    return 0;
}

static void run_case(char **field) {
    uint16_t caller_schema = (uint16_t)parse_number(field[5]);
    uint16_t declaration_schema = (uint16_t)parse_number(field[11]);
    KofunAccessContext context;
    KofunDeclarationAccess declaration;
    KofunEffectiveBoundary boundaries[BOUNDARY_CAPACITY];
    KofunAccessResult result;

    context.caller_package = make_identity(
        KOFUN_ID_PACKAGE,
        caller_schema,
        parse_number(field[1])
    );
    context.caller_module = make_identity(
        KOFUN_ID_MODULE,
        caller_schema,
        parse_number(field[2])
    );
    context.caller_file = make_identity(
        KOFUN_ID_FILE,
        caller_schema,
        parse_number(field[3])
    );
    context.has_caller_owner = parse_optional_identity(
        field[4],
        KOFUN_ID_TYPE,
        caller_schema,
        &context.caller_owner
    );
    context.use_span = (KofunSpan){ .start = 100, .end = 110 };

    declaration.declared_visibility = parse_visibility(field[6]);
    declaration.defining_package = make_identity(
        KOFUN_ID_PACKAGE,
        declaration_schema,
        parse_number(field[7])
    );
    declaration.defining_module = make_identity(
        KOFUN_ID_MODULE,
        declaration_schema,
        parse_number(field[8])
    );
    declaration.defining_file = make_identity(
        KOFUN_ID_FILE,
        declaration_schema,
        parse_number(field[9])
    );
    declaration.has_defining_owner = parse_optional_identity(
        field[10],
        KOFUN_ID_TYPE,
        declaration_schema,
        &declaration.defining_owner
    );
    declaration.enclosing_count = build_boundaries(
        field[12],
        &declaration,
        boundaries
    );
    declaration.enclosing_chain = declaration.enclosing_count == 0
        ? NULL
        : boundaries;
    declaration.declaration_span = (KofunSpan){ .start = 20, .end = 40 };
    if (strcmp(field[12], "invalid-use-span") == 0) {
        context.use_span = (KofunSpan){ .start = 110, .end = 100 };
    }

    result = kofun_decide_access(&context, &declaration);
    printf(
        "%s|%s|%s|effective=%s|disclosure=%s|remedy=%s|proof=0x%02x|usable=%s\n",
        field[0],
        kofun_access_kind_name(result.kind),
        kofun_access_reason_name(result.reason),
        kofun_visibility_name(result.effective_visibility),
        kofun_safe_disclosure_name(result.safe_disclosure),
        kofun_access_remedy_name(result.remedy),
        result.proof,
        result.usable_reference ? "yes" : "no"
    );

    (void)field[13];
    (void)field[14];
}

int main(int argc, char **argv) {
    FILE *input;
    char line[LINE_CAPACITY];
    size_t line_number = 0;

    if (argc != 2) die("usage: visibility-access-driver CASES", NULL);
    input = fopen(argv[1], "rb");
    if (input == NULL) die("cannot open cases", argv[1]);

    while (fgets(line, sizeof(line), input) != NULL) {
        char *fields[FIELD_COUNT];
        size_t length;
        size_t count;

        line_number += 1;
        length = strlen(line);
        if (length == 0 || line[length - 1] != '\n') {
            die("line is too long or lacks final newline", argv[1]);
        }
        line[length - 1] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;
        count = split_fields(line, fields);
        if (count != FIELD_COUNT) {
            fprintf(
                stderr,
                "visibility-access driver: line %zu has %zu fields, expected %u\n",
                line_number,
                count,
                FIELD_COUNT
            );
            fclose(input);
            return 2;
        }
        run_case(fields);
    }
    if (ferror(input)) die("failed while reading cases", argv[1]);
    if (fclose(input) != 0) die("failed to close cases", argv[1]);
    return 0;
}
