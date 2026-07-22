#include "kif_v1.h"

#define KOFUN_MODULE_SYMBOLS_NO_MAIN
#include "module_symbols.c"

static const char *kif_fact_kind_name(KofunKifFactKind kind) {
    switch (kind) {
        case KOFUN_KIF_FACT_FUNCTION: return "function";
        case KOFUN_KIF_FACT_ADT: return "adt";
        case KOFUN_KIF_FACT_CONSTRUCTOR: return "constructor";
    }
    return "invalid";
}

static const char *kif_visibility_name(KofunKifVisibility visibility) {
    switch (visibility) {
        case KOFUN_KIF_VISIBILITY_INTERNAL: return "internal";
        case KOFUN_KIF_VISIBILITY_PUBLIC: return "pub";
    }
    return "invalid";
}

static KofunKifFactKind map_kind(DeclarationKind kind) {
    switch (kind) {
        case DECLARATION_FUNCTION: return KOFUN_KIF_FACT_FUNCTION;
        case DECLARATION_ADT: return KOFUN_KIF_FACT_ADT;
        case DECLARATION_CONSTRUCTOR: return KOFUN_KIF_FACT_CONSTRUCTOR;
    }
    return 0;
}

static bool declaration_is_interface_fact(const Declaration *declaration) {
    return declaration->visibility == VISIBILITY_PUBLIC ||
        declaration->visibility == VISIBILITY_INTERNAL;
}

static bool find_name_token(
    Program *program,
    const Declaration *declaration,
    size_t *token_index
) {
    Module *module = &program->modules[declaration->module_index];
    size_t index;
    for (index = 0u; index < module->token_count; index += 1u) {
        if (module->tokens[index].start == declaration->name_start &&
            module->tokens[index].end == declaration->name_end) {
            *token_index = index;
            return true;
        }
    }
    set_error(program, "E2S70", "cannot recover signature tokens for `%s`", declaration->name);
    return false;
}

static bool project_signature(
    Program *program,
    const Declaration *declaration,
    KofunKifFact *fact
) {
    Module *module = &program->modules[declaration->module_index];
    size_t name;
    if (!find_name_token(program, declaration, &name)) return false;
    if (declaration->kind == DECLARATION_FUNCTION) {
        size_t open = name + 1u;
        size_t close;
        size_t cursor;
        uint16_t parameter_count = 0u;
        if (open >= module->token_count ||
            !punctuation_equals(module, &module->tokens[open], '(') ||
            !find_closing(program, module, open, '(', ')', &close)) return false;
        cursor = open + 1u;
        while (cursor < close) {
            if (cursor + 2u >= close ||
                module->tokens[cursor].kind != TOKEN_IDENTIFIER ||
                !punctuation_equals(module, &module->tokens[cursor + 1u], ':') ||
                !token_equals(module, &module->tokens[cursor + 2u], "Int")) {
                set_error(program, "E2S70",
                    "KIF v1 function `%s` requires only Int parameter types", declaration->name);
                return false;
            }
            if (parameter_count >= 256u) {
                set_error(program, "E2S70",
                    "KIF v1 function `%s` has too many parameters", declaration->name);
                return false;
            }
            parameter_count += 1u;
            cursor += 3u;
            if (cursor < close) {
                if (!punctuation_equals(module, &module->tokens[cursor], ',')) {
                    set_error(program, "E2S70",
                        "KIF v1 function `%s` has a malformed parameter list", declaration->name);
                    return false;
                }
                cursor += 1u;
            }
        }
        if (close + 2u >= module->token_count ||
            module->tokens[close + 1u].kind != TOKEN_ARROW ||
            !token_equals(module, &module->tokens[close + 2u], "Int")) {
            set_error(program, "E2S70",
                "KIF v1 function `%s` requires an Int result type", declaration->name);
            return false;
        }
        fact->parameter_count = parameter_count;
        fact->result_type = KOFUN_KIF_TYPE_INT;
    } else if (declaration->kind == DECLARATION_CONSTRUCTOR) {
        size_t next = name + 1u;
        if (next < module->token_count && punctuation_equals(module, &module->tokens[next], '(')) {
            size_t close;
            if (!find_closing(program, module, next, '(', ')', &close)) return false;
            if (close != next + 4u ||
                module->tokens[next + 1u].kind != TOKEN_IDENTIFIER ||
                !punctuation_equals(module, &module->tokens[next + 2u], ':') ||
                !token_equals(module, &module->tokens[next + 3u], "Int")) {
                set_error(program, "E2S70",
                    "KIF v1 constructor `%s` requires zero or one Int payload",
                    declaration->name);
                return false;
            }
            fact->constructor_payload_count = 1u;
        }
    }
    return true;
}

static bool append_projected_fact(
    Program *program,
    Declaration *declaration,
    KofunKifFact *facts,
    size_t *count
) {
    KofunKifFact *fact = &facts[*count];
    memset(fact, 0, sizeof(*fact));
    memcpy(fact->namespace_id, declaration->namespace_id, KOFUN_KIF_ID_BYTES);
    memcpy(fact->symbol_id, declaration->symbol_id, KOFUN_KIF_ID_BYTES);
    fact->kind = map_kind(declaration->kind);
    fact->visibility = declaration->visibility == VISIBILITY_PUBLIC
        ? KOFUN_KIF_VISIBILITY_PUBLIC : KOFUN_KIF_VISIBILITY_INTERNAL;
    fact->name = declaration->name;
    fact->name_length = strlen(declaration->name);
    if (declaration->kind == DECLARATION_CONSTRUCTOR) {
        const Declaration *owner;
        if (!declaration->has_owner || declaration->owner_index >= program->declaration_count) {
            set_error(program, "E2S70", "constructor `%s` has no canonical owner", declaration->name);
            return false;
        }
        owner = &program->declarations[declaration->owner_index];
        memcpy(fact->owner_symbol_id, owner->symbol_id, KOFUN_KIF_ID_BYTES);
        if (declaration->constructor_index > UINT32_MAX) {
            set_error(program, "E2S70", "constructor ordinal exceeds KIF v1");
            return false;
        }
        fact->constructor_ordinal = (uint32_t)declaration->constructor_index;
    }
    if (!project_signature(program, declaration, fact)) return false;
    *count += 1u;
    return true;
}

static bool build_interface(
    Program *program,
    const uint8_t module_id[KOFUN_KIF_ID_BYTES],
    const char *edition,
    KofunKifInterface *interface
) {
    size_t module_index = SIZE_MAX;
    size_t public_count = 0u;
    size_t internal_count = 0u;
    size_t index;
    memset(interface, 0, sizeof(*interface));
    for (index = 0u; index < program->module_count; index += 1u) {
        if (memcmp(program->modules[index].module_id, module_id, KOFUN_KIF_ID_BYTES) == 0) {
            module_index = index;
            break;
        }
    }
    if (module_index == SIZE_MAX) {
        set_error(program, "E2S69", "requested ModuleId is absent from the inventory");
        return false;
    }
    if (strlen(edition) == 0u || strlen(edition) > KOFUN_KIF_MAX_EDITION_BYTES) {
        set_error(program, "E2S69", "edition is outside the KIF v1 bound");
        return false;
    }
    for (index = 0u; index < program->declaration_count; index += 1u) {
        Declaration *declaration = &program->declarations[index];
        if (declaration->module_index != module_index || !declaration_is_interface_fact(declaration)) {
            continue;
        }
        if (declaration->visibility == VISIBILITY_PUBLIC) public_count += 1u;
        else internal_count += 1u;
    }
    if (public_count != 0u) {
        interface->public_facts = calloc(public_count, sizeof(*interface->public_facts));
    }
    if (internal_count != 0u) {
        interface->internal_facts = calloc(internal_count, sizeof(*interface->internal_facts));
    }
    if ((public_count != 0u && interface->public_facts == NULL) ||
        (internal_count != 0u && interface->internal_facts == NULL)) {
        set_error(program, "E2S71", "KIF fact allocation failed");
        return false;
    }
    memcpy(interface->package_id, program->modules[module_index].package_id, KOFUN_KIF_ID_BYTES);
    memcpy(interface->module_id, module_id, KOFUN_KIF_ID_BYTES);
    memcpy(interface->edition, edition, strlen(edition) + 1u);
    for (index = 0u; index < program->declaration_count; index += 1u) {
        Declaration *declaration = &program->declarations[index];
        if (declaration->module_index != module_index || !declaration_is_interface_fact(declaration)) {
            continue;
        }
        if (declaration->visibility == VISIBILITY_PUBLIC) {
            if (!append_projected_fact(program, declaration,
                    interface->public_facts, &interface->public_fact_count)) return false;
        } else if (!append_projected_fact(program, declaration,
                interface->internal_facts, &interface->internal_fact_count)) return false;
    }
    return true;
}

static void destroy_writer_interface(KofunKifInterface *interface) {
    free(interface->public_facts);
    free(interface->internal_facts);
    memset(interface, 0, sizeof(*interface));
}

static bool read_kif_file(const char *path, uint8_t **bytes_out, size_t *length_out) {
    FILE *input = fopen(path, "rb");
    long measured;
    uint8_t *bytes;
    size_t length;
    if (input == NULL) return false;
    if (fseek(input, 0, SEEK_END) != 0 || (measured = ftell(input)) < 0 ||
        fseek(input, 0, SEEK_SET) != 0 ||
        (unsigned long)measured > KOFUN_KIF_MAX_ENVELOPE) {
        fclose(input);
        return false;
    }
    length = (size_t)measured;
    bytes = malloc(length == 0u ? 1u : length);
    if (bytes == NULL) {
        fclose(input);
        return false;
    }
    {
        size_t read_count = fread(bytes, 1u, length, input);
        int close_status = fclose(input);
        if (read_count != length || close_status != 0) {
            free(bytes);
            return false;
        }
    }
    *bytes_out = bytes;
    *length_out = length;
    return true;
}

static bool emit_dump(const KofunKifInterface *interface, const char *path) {
    FILE *output = path == NULL ? stdout : fopen(path, "wb");
    char package_hex[65];
    char module_hex[65];
    char public_hex[65];
    char internal_hex[65];
    size_t index;
    if (output == NULL) return false;
    bytes_to_hex(interface->package_id, 32u, package_hex);
    bytes_to_hex(interface->module_id, 32u, module_hex);
    bytes_to_hex(interface->public_semantic_digest, 32u, public_hex);
    bytes_to_hex(interface->package_internal_semantic_digest, 32u, internal_hex);
    fprintf(output,
        "{\n  \"schema\": \"kofun.interface-dump/v1\",\n"
        "  \"authoritative\": false,\n"
        "  \"edition\": \"%s\",\n"
        "  \"package_id\": \"%s\",\n"
        "  \"module_id\": \"%s\",\n"
        "  \"public_semantic_digest\": \"%s\",\n"
        "  \"package_internal_semantic_digest\": \"%s\",\n"
        "  \"facts\": [\n",
        interface->edition, package_hex, module_hex, public_hex, internal_hex);
    for (index = 0u; index < interface->public_fact_count + interface->internal_fact_count; index += 1u) {
        const KofunKifFact *fact = index < interface->public_fact_count
            ? &interface->public_facts[index]
            : &interface->internal_facts[index - interface->public_fact_count];
        char namespace_hex[65];
        char symbol_hex[65];
        bytes_to_hex(fact->namespace_id, 32u, namespace_hex);
        bytes_to_hex(fact->symbol_id, 32u, symbol_hex);
        fprintf(output,
            "    {\"namespace_id\": \"%s\", \"symbol_id\": \"%s\", "
            "\"kind\": \"%s\", \"name\": \"%s\", \"visibility\": \"%s\"",
            namespace_hex, symbol_hex, kif_fact_kind_name(fact->kind), fact->name,
            kif_visibility_name(fact->visibility));
        if (fact->kind == KOFUN_KIF_FACT_FUNCTION) {
            fprintf(output, ", \"parameter_count\": %u, \"result\": \"Int\"",
                (unsigned)fact->parameter_count);
        } else if (fact->kind == KOFUN_KIF_FACT_CONSTRUCTOR) {
            char owner_hex[65];
            bytes_to_hex(fact->owner_symbol_id, 32u, owner_hex);
            fprintf(output, ", \"payload_count\": %u, \"owner_symbol_id\": \"%s\", "
                "\"ordinal\": %u",
                (unsigned)fact->constructor_payload_count, owner_hex,
                (unsigned)fact->constructor_ordinal);
        }
        fprintf(output, "}%s\n",
            index + 1u == interface->public_fact_count + interface->internal_fact_count ? "" : ",");
    }
    fprintf(output, "  ]\n}\n");
    if (ferror(output) != 0) {
        if (path != NULL) {
            (void)fclose(output);
            remove(path);
        }
        return false;
    }
    if (path != NULL && fclose(output) != 0) {
        remove(path);
        return false;
    }
    return true;
}

static int read_mode(const char *input_path, const char *dump_path) {
    uint8_t *bytes = NULL;
    size_t length = 0u;
    KifReadResult result;
    int status = 1;
    if (dump_path != NULL) remove(dump_path);
    if (!read_kif_file(input_path, &bytes, &length)) {
        printf("error[KIF-IO]: cannot read bounded KIF input\n");
        return 1;
    }
    result = kofun_kif_read(bytes, length, kofun_kif_default_limits());
    free(bytes);
    if (result.status != KOFUN_KIF_OK) {
        printf("error[KIF-%s]: %s\n", kofun_kif_status_name(result.status), result.message);
        return 1;
    }
    if (!emit_dump(result.interface, dump_path)) {
        printf("error[KIF-IO]: cannot write diagnostic dump\n");
    } else {
        status = 0;
    }
    kofun_kif_destroy(result.interface);
    return status;
}

static int write_mode(
    const char *inventory,
    const char *module_text,
    const char *edition,
    const char *output,
    const char *dump_path
) {
    Program program;
    KofunKifInterface interface;
    uint8_t module_id[32];
    KifWriteResult result;
    size_t index;
    int status = 1;
    memset(&program, 0, sizeof(program));
    memset(&interface, 0, sizeof(interface));
    if (dump_path != NULL) remove(dump_path);
    if (!parse_identity(module_text, module_id)) {
        set_error(&program, "E2S69", "ModuleId must be 64 lowercase hexadecimal digits");
        goto done;
    }
    if (!load_inventory(&program, inventory) || !order_and_validate_inventory(&program)) goto done;
    for (index = 0u; index < program.module_count; index += 1u) {
        if (!collect_module(&program, index)) goto done;
    }
    compute_identities(&program);
    if (!validate_duplicates(&program) || !resolve_bodies(&program) ||
        !build_interface(&program, module_id, edition, &interface)) goto done;
    result = kofun_kif_write(&interface, output);
    if (result.status != KOFUN_KIF_OK) {
        printf("error[KIF-%s]: %s\n", kofun_kif_status_name(result.status), result.message);
        goto done;
    }
    if (dump_path != NULL && read_mode(output, dump_path) != 0) goto done;
    status = 0;
done:
    if (program.failed) printf("%s\n", program.error);
    if (status != 0 && dump_path != NULL) remove(dump_path);
    destroy_writer_interface(&interface);
    destroy_program(&program);
    return status;
}

typedef struct {
    const KofunKifFact *fact;
    size_t arity;
    size_t start;
    size_t end;
} KifQualifiedCall;

static bool token_text_equals(
    const Module *module,
    const Token *token,
    const char *text
) {
    size_t length = token->end - token->start;
    return strlen(text) == length &&
        memcmp(module->source + token->start, text, length) == 0;
}

static bool parse_dependency_import(
    Program *program,
    Module *module,
    const char *dependency_path,
    char qualifier[IDENTIFIER_LIMIT + 1u]
) {
    size_t cursor;
    bool found = false;
    for (cursor = 0u; cursor < module->token_count; cursor += 1u) {
        char path[HOST_PATH_LIMIT + 1u];
        size_t path_length = 0u;
        size_t component_start = 0u;
        size_t current;
        bool expect_identifier = true;
        if (!token_equals(module, &module->tokens[cursor], "import")) continue;
        current = cursor + 1u;
        while (current < module->token_count &&
            !module->tokens[current].line_break_before) {
            Token *token = &module->tokens[current];
            if (expect_identifier) {
                size_t length;
                if (token->kind != TOKEN_IDENTIFIER) break;
                length = token->end - token->start;
                if (path_length != 0u) path[path_length++] = '.';
                if (path_length + length > HOST_PATH_LIMIT) break;
                component_start = path_length;
                memcpy(path + path_length, module->source + token->start, length);
                path_length += length;
            } else if (!punctuation_equals(module, token, '.')) {
                break;
            }
            expect_identifier = !expect_identifier;
            current += 1u;
        }
        if (path_length == 0u || expect_identifier ||
            (current < module->token_count && !module->tokens[current].line_break_before)) {
            set_error(program, "E2S59", "malformed qualified import in KIF consumer");
            return false;
        }
        path[path_length] = '\0';
        if (strcmp(path, dependency_path) != 0) {
            set_error(program, "E2S60",
                "KIF consumer import `%s` has no supplied compiled interface", path);
            return false;
        }
        if (found) {
            set_error(program, "E2S61", "duplicate KIF dependency import `%s`", path);
            return false;
        }
        memcpy(qualifier, path + component_start, path_length - component_start);
        qualifier[path_length - component_start] = '\0';
        found = true;
        cursor = current - 1u;
    }
    if (!found) set_error(program, "E2S60", "consumer does not import `%s`", dependency_path);
    return found;
}

static const KofunKifFact *find_kif_function(
    const KofunKifInterface *interface,
    bool package_internal,
    const Module *module,
    const Token *name
) {
    size_t index;
    for (index = 0u; index < interface->public_fact_count; index += 1u) {
        const KofunKifFact *fact = &interface->public_facts[index];
        if (fact->kind == KOFUN_KIF_FACT_FUNCTION &&
            token_text_equals(module, name, fact->name)) return fact;
    }
    if (!package_internal) return NULL;
    for (index = 0u; index < interface->internal_fact_count; index += 1u) {
        const KofunKifFact *fact = &interface->internal_facts[index];
        if (fact->kind == KOFUN_KIF_FACT_FUNCTION &&
            token_text_equals(module, name, fact->name)) return fact;
    }
    return NULL;
}

static bool measure_call(
    Program *program,
    Module *module,
    size_t open,
    size_t *arity_out,
    size_t *close_out
) {
    size_t cursor;
    size_t depth = 0u;
    size_t commas = 0u;
    for (cursor = open; cursor < module->token_count; cursor += 1u) {
        Token *token = &module->tokens[cursor];
        if (punctuation_equals(module, token, '(')) {
            depth += 1u;
        } else if (punctuation_equals(module, token, ')')) {
            if (depth == 0u) break;
            depth -= 1u;
            if (depth == 0u) {
                if (cursor > open + 1u &&
                    punctuation_equals(module, &module->tokens[cursor - 1u], ',')) break;
                *arity_out = cursor == open + 1u ? 0u : commas + 1u;
                *close_out = cursor;
                return true;
            }
        } else if (depth == 1u && punctuation_equals(module, token, ',')) {
            if (cursor == open + 1u ||
                punctuation_equals(module, &module->tokens[cursor - 1u], ',')) break;
            commas += 1u;
        }
    }
    set_error(program, "E2S65", "malformed qualified call in KIF consumer");
    return false;
}

static bool collect_kif_calls(
    Program *program,
    Module *module,
    const KofunKifInterface *interface,
    bool package_internal,
    const char *qualifier,
    KifQualifiedCall **calls_out,
    size_t *count_out
) {
    KifQualifiedCall *calls = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    size_t cursor;
    for (cursor = 0u; cursor + 3u < module->token_count; cursor += 1u) {
        const KofunKifFact *fact;
        size_t arity;
        size_t close;
        if (module->tokens[cursor].kind != TOKEN_IDENTIFIER ||
            !token_text_equals(module, &module->tokens[cursor], qualifier) ||
            !punctuation_equals(module, &module->tokens[cursor + 1u], '.') ||
            module->tokens[cursor + 2u].kind != TOKEN_IDENTIFIER ||
            !punctuation_equals(module, &module->tokens[cursor + 3u], '(')) continue;
        fact = find_kif_function(interface, package_internal, module,
            &module->tokens[cursor + 2u]);
        if (fact == NULL) {
            set_error(program, "E2S65", "compiled interface has no accessible function `%.*s`",
                (int)(module->tokens[cursor + 2u].end - module->tokens[cursor + 2u].start),
                module->source + module->tokens[cursor + 2u].start);
            free(calls);
            return false;
        }
        if (!measure_call(program, module, cursor + 3u, &arity, &close)) {
            free(calls);
            return false;
        }
        if (arity != fact->parameter_count) {
            set_error(program, "E2S65",
                "compiled function `%s` expects %u arguments but got %zu",
                fact->name, (unsigned)fact->parameter_count, arity);
            free(calls);
            return false;
        }
        if (count == capacity) {
            size_t next = capacity == 0u ? 16u : capacity * 2u;
            KifQualifiedCall *resized;
            if (next > CALL_LIMIT) next = CALL_LIMIT;
            if (count == CALL_LIMIT ||
                (resized = realloc(calls, next * sizeof(*resized))) == NULL) {
                free(calls);
                set_error(program, "E2S68", "KIF qualified-call allocation failed");
                return false;
            }
            calls = resized;
            capacity = next;
        }
        calls[count++] = (KifQualifiedCall){
            .fact = fact,
            .arity = arity,
            .start = module->tokens[cursor].start,
            .end = module->tokens[close].end
        };
    }
    if (count == 0u) {
        free(calls);
        set_error(program, "E2S65", "consumer has no qualified call through `%s`", qualifier);
        return false;
    }
    *calls_out = calls;
    *count_out = count;
    return true;
}

static bool emit_kif_resolution(
    const KofunKifInterface *interface,
    bool package_internal,
    const char *dependency_path,
    const char *qualifier,
    const KifQualifiedCall *calls,
    size_t count,
    const char *output_path
) {
    FILE *output = fopen(output_path, "wb");
    char module_hex[65];
    char digest_hex[65];
    size_t index;
    if (output == NULL) return false;
    bytes_to_hex(interface->module_id, KOFUN_KIF_ID_BYTES, module_hex);
    bytes_to_hex(package_internal ? interface->package_internal_semantic_digest
            : interface->public_semantic_digest,
        KOFUN_KIF_ID_BYTES, digest_hex);
    fprintf(output,
        "kofun-imports-qualified/v1\n"
        "compiled-interface|path=%s|module=%s|view=%s|digest=%s\n",
        dependency_path, module_hex,
        package_internal ? "package-internal" : "public", digest_hex);
    for (index = 0u; index < count; index += 1u) {
        char symbol_hex[65];
        bytes_to_hex(calls[index].fact->symbol_id, KOFUN_KIF_ID_BYTES, symbol_hex);
        fprintf(output,
            "qualified-call|qualifier=%s|name=%s|target-module=%s|"
            "target-symbol=%s|arity=%zu|signature=fn(%u:Int)->Int|span=%zu..%zu\n",
            qualifier, calls[index].fact->name, module_hex, symbol_hex,
            calls[index].arity, (unsigned)calls[index].fact->parameter_count,
            calls[index].start, calls[index].end);
    }
    {
        bool write_failed = ferror(output) != 0;
        int close_status = fclose(output);
        if (!write_failed && close_status == 0) return true;
        remove(output_path);
        return false;
    }
}

static int resolve_mode(
    const char *input_path,
    const char *consumer_package_text,
    const char *dependency_path,
    const char *consumer_path,
    const char *output_path
) {
    uint8_t *bytes = NULL;
    size_t length = 0u;
    uint8_t consumer_package[32];
    KifReadResult result;
    Program program;
    Module *module = &program.modules[0];
    char qualifier[IDENTIFIER_LIMIT + 1u];
    KifQualifiedCall *calls = NULL;
    size_t call_count = 0u;
    bool package_internal;
    int status = 1;
    memset(&program, 0, sizeof(program));
    program.module_count = 1u;
    remove(output_path);
    if (!parse_identity(consumer_package_text, consumer_package)) {
        printf("error[KIF-resolve]: consumer PackageId must be 64 lowercase hexadecimal digits\n");
        return 1;
    }
    if (!read_kif_file(input_path, &bytes, &length)) {
        printf("error[KIF-io-commit-failure]: cannot read bounded KIF input\n");
        return 1;
    }
    result = kofun_kif_read(bytes, length, kofun_kif_default_limits());
    free(bytes);
    if (result.status != KOFUN_KIF_OK) {
        printf("error[KIF-%s]: %s\n", kofun_kif_status_name(result.status), result.message);
        return 1;
    }
    package_internal = memcmp(consumer_package, result.interface->package_id,
        KOFUN_KIF_ID_BYTES) == 0;
    memcpy(module->logical_path, "kif-consumer.kofun", sizeof("kif-consumer.kofun"));
    if (strlen(consumer_path) > HOST_PATH_LIMIT) {
        set_error(&program, "E2S48", "KIF consumer path exceeds adapter limit");
        goto done;
    }
    memcpy(module->host_path, consumer_path, strlen(consumer_path) + 1u);
    if (!read_source(&program, module) || !tokenize(&program, module) ||
        !parse_dependency_import(&program, module, dependency_path, qualifier) ||
        !collect_kif_calls(&program, module, result.interface, package_internal,
            qualifier, &calls, &call_count)) goto done;
    if (!emit_kif_resolution(result.interface, package_internal, dependency_path,
            qualifier, calls, call_count, output_path)) {
        set_error(&program, "E2S68", "cannot commit KIF qualified-import HIR");
        goto done;
    }
    status = 0;
done:
    if (program.failed) printf("%s\n", program.error);
    if (status != 0) remove(output_path);
    free(calls);
    destroy_program(&program);
    kofun_kif_destroy(result.interface);
    return status;
}

int main(int argc, char **argv) {
    /* Keep the included collector's standalone projection compiled and audited. */
    (void)emit_output;
    if (argc >= 2 && strcmp(argv[1], "write") == 0) {
        if (argc != 6 && argc != 7) {
            fprintf(stderr, "usage: %s write INVENTORY MODULE_ID EDITION OUTPUT [DUMP]\n", argv[0]);
            return 2;
        }
        return write_mode(argv[2], argv[3], argv[4], argv[5], argc == 7 ? argv[6] : NULL);
    }
    if (argc >= 2 && strcmp(argv[1], "read") == 0) {
        if (argc != 3 && argc != 4) {
            fprintf(stderr, "usage: %s read INPUT [DUMP]\n", argv[0]);
            return 2;
        }
        return read_mode(argv[2], argc == 4 ? argv[3] : NULL);
    }
    if (argc >= 2 && strcmp(argv[1], "resolve") == 0) {
        if (argc != 7) {
            fprintf(stderr,
                "usage: %s resolve KIF CONSUMER_PACKAGE_ID DEPENDENCY_PATH CONSUMER OUTPUT_HIR\n",
                argv[0]);
            return 2;
        }
        return resolve_mode(argv[2], argv[3], argv[4], argv[5], argv[6]);
    }
    fprintf(stderr,
        "usage: %s write INVENTORY MODULE_ID EDITION OUTPUT [DUMP]\n"
        "       %s read INPUT [DUMP]\n"
        "       %s resolve KIF CONSUMER_PACKAGE_ID DEPENDENCY_PATH CONSUMER OUTPUT_HIR\n",
        argv[0], argv[0], argv[0]);
    return 2;
}
