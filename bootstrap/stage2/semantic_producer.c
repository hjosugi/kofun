/*
 * Bounded production adapter from the executable Stage 2 semantic passes to
 * immutable semantic events. This file deliberately reuses the audited Stage
 * 2 lexer/parser/scope/ownership implementation in the same translation unit;
 * it does not scrape command output or expose compiler-private records.
 */
#include "semantic_producer.h"
#include "sha256.h"

#define main kofun_stage2_embedded_seed_main
#include "compiler.c"
#undef main

#include <errno.h>

enum {
    PRODUCER_MAX_NODES = 512,
    PRODUCER_MAX_IDENTITIES = 512,
    PRODUCER_MAX_REFERENCES = 1024,
    PRODUCER_MAX_FACTS = 1024,
    PRODUCER_MAX_DIAGNOSTICS = 128,
    PRODUCER_MAX_FUNCTIONS = 64,
    PRODUCER_MAX_CONSTRUCTORS = 128,
    PRODUCER_MAX_BINDINGS = 256
};

typedef struct {
    KofunSemanticNode value;
    KofunSemanticId diagnostic;
    char name[64];
    char type[64];
    bool is_declaration;
} ProducerNode;

typedef struct {
    KofunSemanticIdentity value;
} ProducerIdentity;

typedef struct {
    KofunSemanticReference value;
    KofunSemanticId diagnostic;
} ProducerReference;

typedef struct {
    KofunSemanticFact value;
    char display[160];
    char reason[160];
    KofunSemanticId diagnostic;
} ProducerFact;

typedef struct {
    KofunSemanticDiagnostic value;
    char code[16];
    char category[32];
    char template_id[64];
    char fallback[160];
    KofunSemanticId affected;
    uint32_t remedy;
} ProducerDiagnostic;

typedef struct {
    char name[64];
    KofunSemanticId node;
    KofunSemanticId symbol;
    int64_t start;
    int64_t body_open;
    int64_t end;
} ProducerFunction;

typedef struct {
    char name[64];
    KofunSemanticId node;
    KofunSemanticId symbol;
} ProducerConstructor;

typedef struct {
    char name[64];
    char type[64];
    char ownership[32];
    KofunSemanticId node;
    KofunSemanticId binding;
    int64_t function_start;
    int64_t declaration_start;
} ProducerBinding;

typedef struct {
    const KofunStage2SemanticInput *input;
    const char *source;
    const char *scope_hir;
    KofunSemanticSource source_record;
    KofunSemanticId value_namespace_id;
    KofunSemanticId type_namespace_id;
    ProducerNode nodes[PRODUCER_MAX_NODES];
    size_t node_count;
    ProducerIdentity identities[PRODUCER_MAX_IDENTITIES];
    size_t identity_count;
    ProducerReference references[PRODUCER_MAX_REFERENCES];
    size_t reference_count;
    ProducerFact facts[PRODUCER_MAX_FACTS];
    size_t fact_count;
    ProducerDiagnostic diagnostics[PRODUCER_MAX_DIAGNOSTICS];
    size_t diagnostic_count;
    ProducerFunction functions[PRODUCER_MAX_FUNCTIONS];
    size_t function_count;
    ProducerConstructor constructors[PRODUCER_MAX_CONSTRUCTORS];
    size_t constructor_count;
    ProducerBinding bindings[PRODUCER_MAX_BINDINGS];
    size_t binding_count;
    bool language_failed;
    bool resource_failed;
} Producer;

static KofunSemanticBytes producer_text(const char *value) {
    KofunSemanticBytes text_value;
    text_value.bytes = (const uint8_t *)value;
    text_value.length = (uint32_t)strlen(value);
    return text_value;
}

static void producer_hash(
    const char *domain,
    const uint8_t *bytes,
    size_t length,
    KofunSemanticId *result
) {
    KofunSha256 sha;
    uint8_t u16[2];
    uint8_t u32[4];
    static const uint8_t prefix[6] = {'K', 'O', 'F', 'U', 'N', 0u};
    size_t domain_length = strlen(domain);
    if (domain_length > UINT16_MAX || length > UINT32_MAX) {
        memset(result, 0, sizeof(*result));
        return;
    }
    u16[0] = (uint8_t)(domain_length >> 8u);
    u16[1] = (uint8_t)domain_length;
    u32[0] = (uint8_t)(length >> 24u);
    u32[1] = (uint8_t)(length >> 16u);
    u32[2] = (uint8_t)(length >> 8u);
    u32[3] = (uint8_t)length;
    kofun_sha256_init(&sha);
    kofun_sha256_update(&sha, prefix, sizeof(prefix));
    kofun_sha256_update(&sha, u16, sizeof(u16));
    kofun_sha256_update(
        &sha,
        (const uint8_t *)domain,
        domain_length
    );
    kofun_sha256_update(&sha, u32, sizeof(u32));
    kofun_sha256_update(&sha, bytes, length);
    kofun_sha256_finish(&sha, result->bytes);
}

static void producer_named_id(
    const Producer *producer,
    const char *domain,
    const char *name,
    KofunSemanticId *result
) {
    uint8_t payload[KOFUN_SEMANTIC_ID_BYTES + 160u];
    size_t name_length = strlen(name);
    if (name_length > 160u) name_length = 160u;
    memcpy(
        payload,
        producer->source_record.file_id.bytes,
        KOFUN_SEMANTIC_ID_BYTES
    );
    memcpy(payload + KOFUN_SEMANTIC_ID_BYTES, name, name_length);
    producer_hash(
        domain,
        payload,
        KOFUN_SEMANTIC_ID_BYTES + name_length,
        result
    );
}

static void producer_write_field(
    uint8_t **cursor,
    uint16_t tag,
    const uint8_t *value,
    size_t length
) {
    uint8_t *at = *cursor;
    at[0] = (uint8_t)(tag >> 8u);
    at[1] = (uint8_t)tag;
    at[2] = (uint8_t)(length >> 24u);
    at[3] = (uint8_t)(length >> 16u);
    at[4] = (uint8_t)(length >> 8u);
    at[5] = (uint8_t)length;
    if (length != 0u) memcpy(at + 6u, value, length);
    *cursor = at + 6u + length;
}

static void producer_namespace_id(
    unsigned tag,
    const char *name,
    KofunSemanticId *result
) {
    char payload[96];
    int length = snprintf(
        payload,
        sizeof(payload),
        "kofun.namespace-id/v1\ntag=%u\nname=%s\n",
        tag,
        name
    );
    if (length < 0 || (size_t)length >= sizeof(payload)) {
        memset(result, 0, sizeof(*result));
        return;
    }
    producer_hash(
        "kofun.id.namespace/v1",
        (const uint8_t *)payload,
        (size_t)length,
        result
    );
}

static bool producer_symbol_id(
    const Producer *producer,
    const KofunSemanticId *namespace_id,
    const char *declaration_kind,
    const char *name,
    KofunSemanticId *result
) {
    size_t kind_length = strlen(declaration_kind);
    size_t name_length = strlen(name);
    size_t payload_length;
    uint8_t *payload;
    uint8_t *cursor;
    if (kind_length > UINT32_MAX || name_length > UINT32_MAX ||
        kind_length > SIZE_MAX - name_length - 88u) {
        return false;
    }
    payload_length = 88u + kind_length + name_length;
    payload = (uint8_t *)malloc(payload_length);
    if (payload == NULL) return false;
    cursor = payload;
    producer_write_field(
        &cursor,
        UINT16_C(0x8001),
        producer->source_record.module_id.bytes,
        KOFUN_SEMANTIC_ID_BYTES
    );
    producer_write_field(
        &cursor,
        UINT16_C(0x8002),
        namespace_id->bytes,
        KOFUN_SEMANTIC_ID_BYTES
    );
    producer_write_field(
        &cursor,
        UINT16_C(0x8003),
        (const uint8_t *)declaration_kind,
        kind_length
    );
    producer_write_field(
        &cursor,
        UINT16_C(0x8004),
        (const uint8_t *)name,
        name_length
    );
    if ((size_t)(cursor - payload) != payload_length) {
        free(payload);
        return false;
    }
    producer_hash(
        "kofun.id.symbol/v1",
        payload,
        payload_length,
        result
    );
    free(payload);
    return true;
}

static void copy_token_text(
    const char *source,
    int64_t start,
    char *output,
    size_t output_size
) {
    int64_t end = token_end(source, start);
    size_t length = end > start ? (size_t)(end - start) : 0u;
    if (length >= output_size) length = output_size - 1u;
    if (length != 0u) memcpy(output, source + start, length);
    output[length] = '\0';
}

static KofunSemanticSpan producer_span(int64_t start, int64_t end) {
    KofunSemanticSpan span;
    span.start = start < 0 ? 0u : (uint32_t)start;
    span.end = end < start ? span.start : (uint32_t)end;
    return span;
}

static ProducerNode *producer_add_node(
    Producer *producer,
    KofunSemanticNodeKind kind,
    int64_t start,
    int64_t end,
    const char *name,
    bool declaration
) {
    ProducerNode *node;
    if (producer->node_count >= PRODUCER_MAX_NODES) {
        producer->resource_failed = true;
        return NULL;
    }
    node = &producer->nodes[producer->node_count];
    memset(node, 0, sizeof(*node));
    node->value.kind = kind;
    node->value.span = producer_span(start, end);
    node->value.status = KOFUN_SEMANTIC_VALIDATED;
    kofun_semantic_derive_id(
        "kofun.sidecar.node/v1",
        &producer->source_record.file_id,
        kind,
        node->value.span,
        (uint32_t)producer->node_count,
        &node->value.node_id
    );
    if (name != NULL) {
        (void)snprintf(node->name, sizeof(node->name), "%s", name);
    }
    node->is_declaration = declaration;
    producer->node_count += 1u;
    return node;
}

static bool producer_add_stable_identity(
    Producer *producer,
    KofunSemanticId owner,
    KofunSemanticIdentityKind kind,
    const KofunSemanticId *value
) {
    ProducerIdentity *identity;
    if (producer->identity_count >= PRODUCER_MAX_IDENTITIES) {
        producer->resource_failed = true;
        return false;
    }
    identity = &producer->identities[producer->identity_count++];
    memset(identity, 0, sizeof(*identity));
    identity->value.owner_node_id = owner;
    identity->value.kind = kind;
    identity->value.status = KOFUN_SEMANTIC_VALIDATED;
    identity->value.value = *value;
    return true;
}

static bool producer_add_identity(
    Producer *producer,
    KofunSemanticId owner,
    KofunSemanticIdentityKind kind,
    const char *domain,
    const char *name,
    KofunSemanticId *result
) {
    KofunSemanticId value;
    producer_named_id(
        producer,
        domain,
        name,
        &value
    );
    if (result != NULL) *result = value;
    return producer_add_stable_identity(producer, owner, kind, &value);
}

static ProducerFact *producer_add_fact(
    Producer *producer,
    KofunSemanticId owner,
    KofunSemanticFactKind kind,
    KofunSemanticStatus status,
    const char *display,
    const char *reason
) {
    ProducerFact *fact;
    if (producer->fact_count >= PRODUCER_MAX_FACTS) {
        producer->resource_failed = true;
        return NULL;
    }
    fact = &producer->facts[producer->fact_count++];
    memset(fact, 0, sizeof(*fact));
    fact->value.owner_node_id = owner;
    fact->value.kind = kind;
    fact->value.status = status;
    (void)snprintf(fact->display, sizeof(fact->display), "%s", display);
    (void)snprintf(fact->reason, sizeof(fact->reason), "%s", reason);
    fact->value.display = producer_text(fact->display);
    fact->value.reason = producer_text(fact->reason);
    return fact;
}

static ProducerDiagnostic *producer_add_diagnostic(
    Producer *producer,
    const char *code,
    const char *category,
    const char *template_id,
    const char *fallback,
    KofunSemanticSpan span,
    KofunSemanticId affected
) {
    ProducerDiagnostic *diagnostic;
    char key[256];
    if (producer->diagnostic_count >= PRODUCER_MAX_DIAGNOSTICS) {
        producer->resource_failed = true;
        return NULL;
    }
    diagnostic = &producer->diagnostics[producer->diagnostic_count++];
    memset(diagnostic, 0, sizeof(*diagnostic));
    (void)snprintf(diagnostic->code, sizeof(diagnostic->code), "%s", code);
    (void)snprintf(
        diagnostic->category,
        sizeof(diagnostic->category),
        "%s",
        category
    );
    (void)snprintf(
        diagnostic->template_id,
        sizeof(diagnostic->template_id),
        "%s",
        template_id
    );
    (void)snprintf(
        diagnostic->fallback,
        sizeof(diagnostic->fallback),
        "%s",
        fallback
    );
    (void)snprintf(
        key,
        sizeof(key),
        "%s:%" PRIu32 ":%" PRIu32,
        code,
        span.start,
        span.end
    );
    producer_named_id(
        producer,
        "kofun.semantic.diagnostic/v1",
        key,
        &diagnostic->value.diagnostic_id
    );
    diagnostic->value.code = producer_text(diagnostic->code);
    diagnostic->value.category = producer_text(diagnostic->category);
    diagnostic->value.severity = KOFUN_SEMANTIC_DIAGNOSTIC_ERROR;
    diagnostic->value.template_id = producer_text(diagnostic->template_id);
    diagnostic->value.primary_file_id = producer->source_record.file_id;
    diagnostic->value.primary_span = span;
    diagnostic->value.fallback_text = producer_text(diagnostic->fallback);
    diagnostic->affected = affected;
    diagnostic->value.affected_ids = &diagnostic->affected;
    diagnostic->value.affected_count = 1u;
    diagnostic->remedy = 1u;
    diagnostic->value.remedy_ids = &diagnostic->remedy;
    diagnostic->value.remedy_count = 1u;
    producer->language_failed = true;
    return diagnostic;
}

static void producer_mark_node_error(
    ProducerNode *node,
    ProducerDiagnostic *diagnostic
) {
    if (node == NULL || diagnostic == NULL) return;
    node->value.status = KOFUN_SEMANTIC_ERROR;
    node->diagnostic = diagnostic->value.diagnostic_id;
    node->value.diagnostic_ids = &node->diagnostic;
    node->value.diagnostic_count = 1u;
}

static ProducerFunction *producer_find_function(
    Producer *producer,
    const char *name
) {
    size_t index;
    for (index = 0u; index < producer->function_count; index += 1u) {
        if (strcmp(producer->functions[index].name, name) == 0) {
            return &producer->functions[index];
        }
    }
    return NULL;
}

static ProducerConstructor *producer_find_constructor(
    Producer *producer,
    const char *name
) {
    size_t index;
    for (index = 0u; index < producer->constructor_count; index += 1u) {
        if (strcmp(producer->constructors[index].name, name) == 0) {
            return &producer->constructors[index];
        }
    }
    return NULL;
}

static ProducerBinding *producer_find_binding(
    Producer *producer,
    int64_t function_start,
    const char *name
) {
    size_t index;
    for (index = producer->binding_count; index > 0u; index -= 1u) {
        ProducerBinding *binding = &producer->bindings[index - 1u];
        if (binding->function_start == function_start &&
            strcmp(binding->name, name) == 0) {
            return binding;
        }
    }
    return NULL;
}

static void producer_copy_type(
    const char *source,
    int64_t start,
    char output[64]
) {
    int64_t cursor = start;
    size_t length = 0u;
    int depth = 0;
    output[0] = '\0';
    while (source[cursor] != '\0') {
        int64_t end;
        if ((token_equal(source, cursor, ",") ||
             token_equal(source, cursor, ")") ||
             token_equal(source, cursor, "=") ||
             token_equal(source, cursor, "{")) &&
            depth == 0) {
            break;
        }
        if (token_equal(source, cursor, "[")) depth += 1;
        if (token_equal(source, cursor, "]")) depth -= 1;
        end = token_end(source, cursor);
        if (end <= cursor) break;
        if (length + (size_t)(end - cursor) >= 63u) break;
        memcpy(output + length, source + cursor, (size_t)(end - cursor));
        length += (size_t)(end - cursor);
        output[length] = '\0';
        cursor = skip_trivia(source, end);
    }
    if (output[0] == '\0') {
        (void)snprintf(output, 64u, "%s", "unavailable");
    }
}

static ProducerBinding *producer_add_binding(
    Producer *producer,
    ProducerFunction *function,
    const char *name,
    const char *type_name,
    const char *ownership,
    int64_t start,
    int64_t end,
    KofunSemanticNodeKind node_kind
) {
    ProducerBinding *binding;
    ProducerNode *node;
    char key[160];
    char *hir_binding_id;
    if (producer->binding_count >= PRODUCER_MAX_BINDINGS) {
        producer->resource_failed = true;
        return NULL;
    }
    node = producer_add_node(
        producer,
        node_kind,
        start,
        end,
        name,
        true
    );
    if (node == NULL) return NULL;
    binding = &producer->bindings[producer->binding_count++];
    memset(binding, 0, sizeof(*binding));
    (void)snprintf(binding->name, sizeof(binding->name), "%s", name);
    (void)snprintf(binding->type, sizeof(binding->type), "%s", type_name);
    (void)snprintf(
        binding->ownership,
        sizeof(binding->ownership),
        "%s",
        ownership
    );
    binding->node = node->value.node_id;
    binding->function_start = function->start;
    binding->declaration_start = start;
    hir_binding_id = hir_definition_id_at(producer->scope_hir, start);
    /*
     * The current scope-HIR keys read/edit/take parameters at the ownership
     * token while ordinary parameters and locals are keyed at the declared
     * name. Preserve that executable identity convention at this adapter
     * boundary rather than minting a second binding number.
     */
    if ((hir_binding_id[0] == '\0' ||
         strcmp(hir_binding_id, "-1") == 0) &&
        (strcmp(ownership, "read") == 0 ||
         strcmp(ownership, "edit") == 0 ||
         strcmp(ownership, "take") == 0)) {
        int64_t ownership_start = start;
        free(hir_binding_id);
        while (ownership_start > function->start &&
               isspace((unsigned char)producer->source[
                   ownership_start - 1])) {
            ownership_start -= 1;
        }
        ownership_start -= (int64_t)strlen(ownership);
        hir_binding_id = hir_definition_id_at(
            producer->scope_hir,
            ownership_start
        );
    }
    if (hir_binding_id[0] == '\0' ||
        strcmp(hir_binding_id, "-1") == 0) {
        free(hir_binding_id);
        return NULL;
    }
    (void)snprintf(
        key,
        sizeof(key),
        "hir-binding:%s",
        hir_binding_id
    );
    free(hir_binding_id);
    if (!producer_add_identity(
            producer,
            binding->node,
            KOFUN_SEMANTIC_ID_BINDING,
            "kofun.stage2.binding/v1",
            key,
            &binding->binding)) {
        return NULL;
    }
    if (producer_add_fact(
            producer,
            binding->node,
            KOFUN_SEMANTIC_FACT_TYPE,
            strcmp(type_name, "unavailable") == 0 ?
                KOFUN_SEMANTIC_UNAVAILABLE :
                KOFUN_SEMANTIC_VALIDATED,
            strcmp(type_name, "unavailable") == 0 ? "" : type_name,
            strcmp(type_name, "unavailable") == 0 ?
                "type-not-available-in-current-subset" : "") == NULL ||
        producer_add_fact(
            producer,
            binding->node,
            KOFUN_SEMANTIC_FACT_OWNERSHIP,
            KOFUN_SEMANTIC_VALIDATED,
            ownership,
            "") == NULL) {
        return NULL;
    }
    return binding;
}

static bool producer_collect_types(Producer *producer) {
    int64_t cursor = skip_trivia(producer->source, 0);
    int64_t length = (int64_t)producer->input->source_length;
    while (cursor < length) {
        if (token_equal(producer->source, cursor, "type")) {
            int64_t name_at = skip_trivia(
                producer->source,
                token_end(producer->source, cursor)
            );
            int64_t declaration_end = type_declaration_end(
                producer->source,
                cursor
            );
            char name[64];
            ProducerNode *type_node;
            KofunSemanticId type_symbol;
            int64_t walk;
            copy_token_text(producer->source, name_at, name, sizeof(name));
            type_node = producer_add_node(
                producer,
                KOFUN_SEMANTIC_NODE_ADT,
                name_at,
                token_end(producer->source, name_at),
                name,
                true
            );
            if (type_node == NULL) return false;
            if (!producer_symbol_id(
                    producer,
                    &producer->type_namespace_id,
                    "adt",
                    name,
                    &type_symbol) ||
                !producer_add_stable_identity(
                    producer,
                    type_node->value.node_id,
                    KOFUN_SEMANTIC_ID_SYMBOL,
                    &type_symbol)) {
                return false;
            }
            walk = skip_trivia(
                producer->source,
                token_end(producer->source, name_at)
            );
            while (walk < declaration_end) {
                if (token_equal(producer->source, walk, "|")) {
                    int64_t constructor_at = skip_trivia(
                        producer->source,
                        token_end(producer->source, walk)
                    );
                    char constructor_name[64];
                    ProducerNode *constructor_node;
                    ProducerConstructor *constructor;
                    KofunSemanticId constructor_symbol;
                    copy_token_text(
                        producer->source,
                        constructor_at,
                        constructor_name,
                        sizeof(constructor_name)
                    );
                    if (producer->constructor_count >=
                        PRODUCER_MAX_CONSTRUCTORS) {
                        producer->resource_failed = true;
                        return false;
                    }
                    constructor_node = producer_add_node(
                        producer,
                        KOFUN_SEMANTIC_NODE_CONSTRUCTOR,
                        constructor_at,
                        token_end(producer->source, constructor_at),
                        constructor_name,
                        true
                    );
                    if (constructor_node == NULL) return false;
                    constructor = &producer->constructors[
                        producer->constructor_count++
                    ];
                    memset(constructor, 0, sizeof(*constructor));
                    (void)snprintf(
                        constructor->name,
                        sizeof(constructor->name),
                        "%s",
                        constructor_name
                    );
                    constructor->node = constructor_node->value.node_id;
                    if (!producer_symbol_id(
                            producer,
                            &producer->value_namespace_id,
                            "constructor",
                            constructor_name,
                            &constructor_symbol) ||
                        !producer_add_stable_identity(
                            producer,
                            constructor->node,
                            KOFUN_SEMANTIC_ID_SYMBOL,
                            &constructor_symbol)) {
                        return false;
                    }
                    constructor->symbol = constructor_symbol;
                    walk = token_end(producer->source, constructor_at);
                    continue;
                }
                walk = skip_trivia(
                    producer->source,
                    token_end(producer->source, walk)
                );
            }
            cursor = declaration_end;
            continue;
        }
        cursor = skip_trivia(
            producer->source,
            token_end(producer->source, cursor)
        );
    }
    return true;
}

static bool producer_collect_functions(Producer *producer) {
    int64_t cursor = next_function_start(producer->source, 0);
    int64_t length = (int64_t)producer->input->source_length;
    while (cursor < length) {
        ProducerFunction *function;
        ProducerNode *function_node;
        char *allocated_name;
        int64_t parameters;
        int64_t parameters_end;
        int64_t body_open;
        int64_t end;
        char return_type[64] = "Void";
        if (producer->function_count >= PRODUCER_MAX_FUNCTIONS) {
            producer->resource_failed = true;
            return false;
        }
        allocated_name = function_name(producer->source, cursor);
        parameters = parameter_open(producer->source, cursor);
        parameters_end = balanced_end(
            producer->source,
            parameters,
            "(",
            ")"
        );
        end = function_end(producer->source, cursor);
        if (parameters < 0 || parameters_end < 0 || end <= cursor) {
            free(allocated_name);
            return false;
        }
        body_open = skip_trivia(producer->source, parameters_end);
        if (token_equal(producer->source, body_open, "->")) {
            int64_t type_at = skip_trivia(
                producer->source,
                token_end(producer->source, body_open)
            );
            producer_copy_type(producer->source, type_at, return_type);
            body_open = skip_trivia(
                producer->source,
                token_end(producer->source, type_at)
            );
            while (body_open < end &&
                   !token_equal(producer->source, body_open, "{")) {
                body_open = skip_trivia(
                    producer->source,
                    token_end(producer->source, body_open)
                );
            }
        }
        function_node = producer_add_node(
            producer,
            KOFUN_SEMANTIC_NODE_FUNCTION,
            cursor,
            token_end(
                producer->source,
                skip_trivia(
                    producer->source,
                    token_end(producer->source, cursor)
                )
            ),
            allocated_name,
            true
        );
        if (function_node == NULL) {
            free(allocated_name);
            return false;
        }
        function = &producer->functions[producer->function_count++];
        memset(function, 0, sizeof(*function));
        (void)snprintf(
            function->name,
            sizeof(function->name),
            "%s",
            allocated_name
        );
        function->node = function_node->value.node_id;
        function->start = cursor;
        function->body_open = body_open;
        function->end = end;
        if (!producer_symbol_id(
                producer,
                &producer->value_namespace_id,
                "function",
                function->name,
                &function->symbol) ||
            !producer_add_stable_identity(
                producer,
                function->node,
                KOFUN_SEMANTIC_ID_SYMBOL,
                &function->symbol) ||
            producer_add_fact(
                producer,
                function->node,
                KOFUN_SEMANTIC_FACT_TYPE,
                KOFUN_SEMANTIC_VALIDATED,
                return_type,
                "") == NULL) {
            free(allocated_name);
            return false;
        }
        free(allocated_name);
        cursor = next_function_start(producer->source, end);
    }
    return true;
}

static ProducerNode *producer_find_node(
    Producer *producer,
    KofunSemanticId id
) {
    size_t index;
    for (index = 0u; index < producer->node_count; index += 1u) {
        if (memcmp(
                producer->nodes[index].value.node_id.bytes,
                id.bytes,
                KOFUN_SEMANTIC_ID_BYTES) == 0) {
            return &producer->nodes[index];
        }
    }
    return NULL;
}

static ProducerFact *producer_find_fact(
    Producer *producer,
    KofunSemanticId owner,
    KofunSemanticFactKind kind
) {
    size_t index;
    for (index = 0u; index < producer->fact_count; index += 1u) {
        ProducerFact *fact = &producer->facts[index];
        if (fact->value.kind == kind &&
            memcmp(
                fact->value.owner_node_id.bytes,
                owner.bytes,
                KOFUN_SEMANTIC_ID_BYTES) == 0) {
            return fact;
        }
    }
    return NULL;
}

static int64_t producer_type_end(const char *source, int64_t start) {
    int64_t cursor = start;
    int depth = 0;
    while (source[cursor] != '\0') {
        if ((token_equal(source, cursor, ",") ||
             token_equal(source, cursor, ")") ||
             token_equal(source, cursor, "=") ||
             token_equal(source, cursor, "{")) &&
            depth == 0) {
            return cursor;
        }
        if (token_equal(source, cursor, "[")) depth += 1;
        if (token_equal(source, cursor, "]")) depth -= 1;
        cursor = skip_trivia(source, token_end(source, cursor));
    }
    return cursor;
}

static bool producer_collect_parameters_and_bindings(Producer *producer) {
    size_t function_index;
    for (function_index = 0u;
         function_index < producer->function_count;
         function_index += 1u) {
        ProducerFunction *function = &producer->functions[function_index];
        int64_t parameters = parameter_open(
            producer->source,
            function->start
        );
        int64_t parameters_end = balanced_end(
            producer->source,
            parameters,
            "(",
            ")"
        );
        int64_t cursor = skip_trivia(
            producer->source,
            token_end(producer->source, parameters)
        );
        ProducerNode *scope_node = producer_add_node(
            producer,
            KOFUN_SEMANTIC_NODE_SCOPE,
            function->body_open,
            function->end,
            "function-body",
            true
        );
        char scope_key[96];
        char *hir_scope_id;
        if (scope_node == NULL) return false;
        hir_scope_id = hir_scope_id_for_open(
            producer->scope_hir,
            function->body_open
        );
        if (hir_scope_id[0] == '\0' ||
            strcmp(hir_scope_id, "-1") == 0) {
            free(hir_scope_id);
            return false;
        }
        (void)snprintf(
            scope_key,
            sizeof(scope_key),
            "hir-scope:%s",
            hir_scope_id
        );
        free(hir_scope_id);
        if (!producer_add_identity(
                producer,
                scope_node->value.node_id,
                KOFUN_SEMANTIC_ID_SCOPE,
                "kofun.stage2.scope/v1",
                scope_key,
                NULL)) {
            return false;
        }
        while (cursor < parameters_end &&
               !token_equal(producer->source, cursor, ")")) {
            char ownership[32] = "value";
            char name[64];
            char type_name[64];
            int64_t name_at = cursor;
            int64_t colon;
            int64_t type_at;
            int64_t type_end;
            if (token_equal(producer->source, cursor, "read") ||
                token_equal(producer->source, cursor, "edit") ||
                token_equal(producer->source, cursor, "take")) {
                copy_token_text(
                    producer->source,
                    cursor,
                    ownership,
                    sizeof(ownership)
                );
                name_at = skip_trivia(
                    producer->source,
                    token_end(producer->source, cursor)
                );
            }
            copy_token_text(
                producer->source,
                name_at,
                name,
                sizeof(name)
            );
            colon = skip_trivia(
                producer->source,
                token_end(producer->source, name_at)
            );
            if (!token_equal(producer->source, colon, ":")) return false;
            type_at = skip_trivia(
                producer->source,
                token_end(producer->source, colon)
            );
            producer_copy_type(producer->source, type_at, type_name);
            type_end = producer_type_end(producer->source, type_at);
            if (producer_add_binding(
                    producer,
                    function,
                    name,
                    type_name,
                    ownership,
                    name_at,
                    token_end(producer->source, name_at),
                    KOFUN_SEMANTIC_NODE_PARAMETER) == NULL) {
                return false;
            }
            if (token_equal(producer->source, type_end, ",")) {
                cursor = skip_trivia(
                    producer->source,
                    token_end(producer->source, type_end)
                );
            } else {
                cursor = type_end;
            }
        }
        cursor = skip_trivia(
            producer->source,
            token_end(producer->source, function->body_open)
        );
        while (cursor < function->end) {
            if (token_equal(producer->source, cursor, "let")) {
                int64_t name_at = skip_trivia(
                    producer->source,
                    token_end(producer->source, cursor)
                );
                char ownership[32] = "immutable-local";
                char name[64];
                char type_name[64] = "unavailable";
                int64_t after_name;
                int64_t initializer;
                bool annotated = false;
                if (token_equal(producer->source, name_at, "mut")) {
                    (void)snprintf(
                        ownership,
                        sizeof(ownership),
                        "%s",
                        "mutable-local"
                    );
                    name_at = skip_trivia(
                        producer->source,
                        token_end(producer->source, name_at)
                    );
                }
                copy_token_text(
                    producer->source,
                    name_at,
                    name,
                    sizeof(name)
                );
                after_name = skip_trivia(
                    producer->source,
                    token_end(producer->source, name_at)
                );
                if (token_equal(producer->source, after_name, ":")) {
                    int64_t type_at = skip_trivia(
                        producer->source,
                        token_end(producer->source, after_name)
                    );
                    int64_t type_end;
                    annotated = true;
                    producer_copy_type(
                        producer->source,
                        type_at,
                        type_name
                    );
                    type_end = producer_type_end(producer->source, type_at);
                    after_name = type_end;
                }
                initializer = skip_trivia(
                    producer->source,
                    token_end(producer->source, after_name)
                );
                if (!annotated) {
                    const char *kind = token_kind(
                        producer->source,
                        initializer
                    );
                    if (strcmp(kind, "string") == 0) {
                        (void)snprintf(type_name, sizeof(type_name), "Text");
                    } else if (strcmp(kind, "integer") == 0) {
                        (void)snprintf(type_name, sizeof(type_name), "Int");
                    } else if (token_equal(
                            producer->source,
                            initializer,
                            "true") ||
                        token_equal(
                            producer->source,
                            initializer,
                            "false")) {
                        (void)snprintf(type_name, sizeof(type_name), "Bool");
                    } else if (strcmp(kind, "identifier") == 0) {
                        char value_name[64];
                        ProducerBinding *source_binding;
                        ProducerFunction *source_function;
                        ProducerConstructor *constructor;
                        copy_token_text(
                            producer->source,
                            initializer,
                            value_name,
                            sizeof(value_name)
                        );
                        source_binding = producer_find_binding(
                            producer,
                            function->start,
                            value_name
                        );
                        source_function = producer_find_function(
                            producer,
                            value_name
                        );
                        constructor = producer_find_constructor(
                            producer,
                            value_name
                        );
                        if (source_binding != NULL) {
                            (void)snprintf(
                                type_name,
                                sizeof(type_name),
                                "%s",
                                source_binding->type
                            );
                        } else if (source_function != NULL) {
                            char *return_type = function_return_type(
                                producer->source,
                                value_name
                            );
                            (void)snprintf(
                                type_name,
                                sizeof(type_name),
                                "%s",
                                return_type
                            );
                            free(return_type);
                        } else if (constructor != NULL) {
                            (void)snprintf(
                                type_name,
                                sizeof(type_name),
                                "%s",
                                "flat-adt"
                            );
                        }
                    }
                }
                {
                    ProducerBinding *binding = producer_add_binding(
                        producer,
                        function,
                        name,
                        type_name,
                        ownership,
                        name_at,
                        token_end(producer->source, name_at),
                        KOFUN_SEMANTIC_NODE_LOCAL
                    );
                    if (binding == NULL) return false;
                    if (annotated) {
                        const char *kind = token_kind(
                            producer->source,
                            initializer
                        );
                        bool mismatch =
                            (strcmp(type_name, "Int") == 0 &&
                             strcmp(kind, "string") == 0) ||
                            (strcmp(type_name, "Text") == 0 &&
                             strcmp(kind, "integer") == 0) ||
                            (strcmp(type_name, "Bool") == 0 &&
                             strcmp(kind, "integer") == 0);
                        if (mismatch) {
                            ProducerNode *node = producer_find_node(
                                producer,
                                binding->node
                            );
                            ProducerDiagnostic *diagnostic =
                                producer_add_diagnostic(
                                    producer,
                                    "E2S15",
                                    "type",
                                    "binding-type-mismatch",
                                    "binding initializer has the wrong type",
                                    producer_span(
                                        initializer,
                                        token_end(
                                            producer->source,
                                            initializer
                                        )
                                    ),
                                    binding->node
                                );
                            ProducerFact *fact = producer_find_fact(
                                producer,
                                binding->node,
                                KOFUN_SEMANTIC_FACT_TYPE
                            );
                            producer_mark_node_error(node, diagnostic);
                            if (fact != NULL && diagnostic != NULL) {
                                fact->value.status = KOFUN_SEMANTIC_ERROR;
                                fact->display[0] = '\0';
                                (void)snprintf(
                                    fact->reason,
                                    sizeof(fact->reason),
                                    "%s",
                                    "binding-type-mismatch"
                                );
                                fact->value.display =
                                    producer_text(fact->display);
                                fact->value.reason =
                                    producer_text(fact->reason);
                                fact->diagnostic =
                                    diagnostic->value.diagnostic_id;
                                fact->value.diagnostic_ids =
                                    &fact->diagnostic;
                                fact->value.diagnostic_count = 1u;
                            }
                        }
                    }
                }
            } else if (token_equal(producer->source, cursor, "for")) {
                int64_t name_at = skip_trivia(
                    producer->source,
                    token_end(producer->source, cursor)
                );
                int64_t in_at = skip_trivia(
                    producer->source,
                    token_end(producer->source, name_at)
                );
                int64_t collection_at = skip_trivia(
                    producer->source,
                    token_end(producer->source, in_at)
                );
                char name[64];
                char collection_name[64];
                const char *element_type = "unavailable";
                ProducerBinding *collection;
                copy_token_text(
                    producer->source,
                    name_at,
                    name,
                    sizeof(name)
                );
                copy_token_text(
                    producer->source,
                    collection_at,
                    collection_name,
                    sizeof(collection_name)
                );
                collection = producer_find_binding(
                    producer,
                    function->start,
                    collection_name
                );
                if (collection != NULL) {
                    if (strcmp(collection->type, "List[Text]") == 0) {
                        element_type = "Text";
                    } else if (strcmp(
                            collection->type,
                            "List[Int]") == 0) {
                        element_type = "Int";
                    }
                }
                if (producer_add_binding(
                        producer,
                        function,
                        name,
                        element_type,
                        "borrowed-element",
                        name_at,
                        token_end(producer->source, name_at),
                        KOFUN_SEMANTIC_NODE_LOCAL) == NULL) {
                    return false;
                }
            }
            cursor = skip_trivia(
                producer->source,
                token_end(producer->source, cursor)
            );
        }
    }
    return true;
}

static ProducerReference *producer_add_reference(
    Producer *producer,
    ProducerNode *source_node,
    KofunSemanticNamespace name_space,
    KofunSemanticSpan span,
    KofunSemanticIdentityKind target_kind,
    const KofunSemanticId *target
) {
    ProducerReference *reference;
    char key[96];
    if (producer->reference_count >= PRODUCER_MAX_REFERENCES) {
        producer->resource_failed = true;
        return NULL;
    }
    reference = &producer->references[producer->reference_count++];
    memset(reference, 0, sizeof(*reference));
    (void)snprintf(
        key,
        sizeof(key),
        "%" PRIu32 ":%" PRIu32 ":%u",
        span.start,
        span.end,
        (unsigned)producer->reference_count
    );
    producer_named_id(
        producer,
        "kofun.semantic.reference/v1",
        key,
        &reference->value.reference_id
    );
    reference->value.source_node_id = source_node->value.node_id;
    reference->value.name_space = name_space;
    reference->value.span = span;
    reference->value.status = target == NULL ?
        KOFUN_SEMANTIC_ERROR : KOFUN_SEMANTIC_VALIDATED;
    reference->value.target_shape = target == NULL ?
        KOFUN_SEMANTIC_TARGET_UNAVAILABLE :
        KOFUN_SEMANTIC_TARGET_VISIBLE;
    reference->value.target_kind = target_kind;
    if (target == NULL) {
        reference->value.hidden_reason =
            producer_text("unresolved-current-stage2-reference");
    } else {
        reference->value.target_value = *target;
    }
    return reference;
}

static bool producer_builtin_name(const char *name) {
    return strcmp(name, "print") == 0 ||
        builtin_arity(name) >= 0;
}

static bool producer_collect_references(Producer *producer) {
    size_t function_index;
    for (function_index = 0u;
         function_index < producer->function_count;
         function_index += 1u) {
        ProducerFunction *function = &producer->functions[function_index];
        int64_t cursor = skip_trivia(
            producer->source,
            token_end(producer->source, function->body_open)
        );
        while (cursor < function->end) {
            if (token_equal(producer->source, cursor, "if")) {
                if (producer_add_node(
                        producer,
                        KOFUN_SEMANTIC_NODE_IF,
                        cursor,
                        token_end(producer->source, cursor),
                        "if",
                        false) == NULL) {
                    return false;
                }
            } else if (token_equal(producer->source, cursor, "match")) {
                if (producer_add_node(
                        producer,
                        KOFUN_SEMANTIC_NODE_MATCH,
                        cursor,
                        token_end(producer->source, cursor),
                        "match",
                        false) == NULL) {
                    return false;
                }
            }
            if (strcmp(token_kind(producer->source, cursor), "identifier") ==
                0) {
                char name[64];
                int64_t after = skip_trivia(
                    producer->source,
                    token_end(producer->source, cursor)
                );
                ProducerBinding *binding;
                copy_token_text(
                    producer->source,
                    cursor,
                    name,
                    sizeof(name)
                );
                binding = producer_find_binding(
                    producer,
                    function->start,
                    name
                );
                if (token_equal(producer->source, after, "(")) {
                    ProducerFunction *target_function =
                        producer_find_function(producer, name);
                    ProducerConstructor *target_constructor =
                        producer_find_constructor(producer, name);
                    if (!producer_builtin_name(name)) {
                        ProducerNode *call = producer_add_node(
                            producer,
                            KOFUN_SEMANTIC_NODE_CALL,
                            cursor,
                            token_end(producer->source, cursor),
                            name,
                            false
                        );
                        const KofunSemanticId *target = NULL;
                        KofunSemanticIdentityKind target_kind =
                            KOFUN_SEMANTIC_ID_SYMBOL;
                        ProducerReference *reference;
                        if (call == NULL) return false;
                        if (target_function != NULL) {
                            target = &target_function->symbol;
                        } else if (target_constructor != NULL) {
                            target = &target_constructor->symbol;
                            target_kind = KOFUN_SEMANTIC_ID_CONSTRUCTOR;
                        }
                        reference = producer_add_reference(
                            producer,
                            call,
                            target_constructor == NULL ?
                                KOFUN_SEMANTIC_NAMESPACE_VALUE :
                                KOFUN_SEMANTIC_NAMESPACE_CONSTRUCTOR,
                            call->value.span,
                            target_kind,
                            target
                        );
                        if (reference == NULL) return false;
                        if (target == NULL) {
                            ProducerDiagnostic *diagnostic =
                                producer_add_diagnostic(
                                    producer,
                                    "E2S16",
                                    "name-resolution",
                                    "unknown-function",
                                    "unknown current-Core function",
                                    call->value.span,
                                    call->value.node_id
                                );
                            producer_mark_node_error(call, diagnostic);
                            if (diagnostic == NULL) return false;
                            reference->diagnostic =
                                diagnostic->value.diagnostic_id;
                            reference->value.diagnostic_ids =
                                &reference->diagnostic;
                            reference->value.diagnostic_count = 1u;
                        } else {
                            const char *result_type =
                                target_constructor == NULL ?
                                    "Int" : "flat-adt";
                            if (producer_add_fact(
                                    producer,
                                    call->value.node_id,
                                    KOFUN_SEMANTIC_FACT_TYPE,
                                    KOFUN_SEMANTIC_VALIDATED,
                                    result_type,
                                    "") == NULL) {
                                return false;
                            }
                        }
                    }
                } else if (binding != NULL &&
                           binding->declaration_start != cursor) {
                    ProducerNode *use = producer_add_node(
                        producer,
                        KOFUN_SEMANTIC_NODE_REFERENCE,
                        cursor,
                        token_end(producer->source, cursor),
                        name,
                        false
                    );
                    if (use == NULL ||
                        producer_add_reference(
                            producer,
                            use,
                            KOFUN_SEMANTIC_NAMESPACE_VALUE,
                            use->value.span,
                            KOFUN_SEMANTIC_ID_BINDING,
                            &binding->binding) == NULL) {
                        return false;
                    }
                    if (producer_add_fact(
                            producer,
                            use->value.node_id,
                            KOFUN_SEMANTIC_FACT_TYPE,
                            KOFUN_SEMANTIC_VALIDATED,
                            binding->type,
                            "") == NULL) {
                        return false;
                    }
                }
            }
            cursor = skip_trivia(
                producer->source,
                token_end(producer->source, cursor)
            );
        }
    }
    return true;
}

static bool producer_apply_ownership(Producer *producer) {
    size_t binding_index;
    for (binding_index = 0u;
         binding_index < producer->binding_count;
         binding_index += 1u) {
        ProducerBinding *binding = &producer->bindings[binding_index];
        int64_t cursor;
        ProducerFunction *function = NULL;
        size_t function_index;
        if (strcmp(binding->ownership, "borrowed-element") != 0 ||
            strcmp(binding->type, "Text") != 0) {
            continue;
        }
        for (function_index = 0u;
             function_index < producer->function_count;
             function_index += 1u) {
            if (producer->functions[function_index].start ==
                binding->function_start) {
                function = &producer->functions[function_index];
                break;
            }
        }
        if (function == NULL) continue;
        cursor = skip_trivia(
            producer->source,
            token_end(producer->source, function->body_open)
        );
        while (cursor < function->end) {
            if (token_equal(producer->source, cursor, "return")) {
                int64_t value_at = skip_trivia(
                    producer->source,
                    token_end(producer->source, cursor)
                );
                char name[64];
                copy_token_text(
                    producer->source,
                    value_at,
                    name,
                    sizeof(name)
                );
                if (strcmp(name, binding->name) == 0) {
                    ProducerNode *use = NULL;
                    ProducerReference *reference = NULL;
                    ProducerDiagnostic *diagnostic;
                    ProducerFact *fact;
                    size_t node_index;
                    size_t reference_index;
                    for (node_index = 0u;
                         node_index < producer->node_count;
                         node_index += 1u) {
                        if (producer->nodes[node_index].value.span.start ==
                                (uint32_t)value_at &&
                            producer->nodes[node_index].value.kind ==
                                KOFUN_SEMANTIC_NODE_REFERENCE) {
                            use = &producer->nodes[node_index];
                            break;
                        }
                    }
                    if (use == NULL) return false;
                    for (reference_index = 0u;
                         reference_index < producer->reference_count;
                         reference_index += 1u) {
                        if (memcmp(
                                producer->references[reference_index]
                                    .value.source_node_id.bytes,
                                use->value.node_id.bytes,
                                KOFUN_SEMANTIC_ID_BYTES) == 0) {
                            reference =
                                &producer->references[reference_index];
                            break;
                        }
                    }
                    diagnostic = producer_add_diagnostic(
                        producer,
                        "E007",
                        "ownership",
                        "borrowed-element-move",
                        "cannot move non-Copy element out of borrowed collection",
                        use->value.span,
                        use->value.node_id
                    );
                    if (diagnostic == NULL) return false;
                    producer_mark_node_error(use, diagnostic);
                    if (reference != NULL) {
                        reference->value.status = KOFUN_SEMANTIC_ERROR;
                        reference->diagnostic =
                            diagnostic->value.diagnostic_id;
                        reference->value.diagnostic_ids =
                            &reference->diagnostic;
                        reference->value.diagnostic_count = 1u;
                    }
                    fact = producer_find_fact(
                        producer,
                        binding->node,
                        KOFUN_SEMANTIC_FACT_OWNERSHIP
                    );
                    if (fact != NULL) {
                        fact->value.status = KOFUN_SEMANTIC_ERROR;
                        fact->display[0] = '\0';
                        (void)snprintf(
                            fact->reason,
                            sizeof(fact->reason),
                            "%s",
                            "move-from-borrowed-collection"
                        );
                        fact->value.display = producer_text(fact->display);
                        fact->value.reason = producer_text(fact->reason);
                        fact->diagnostic =
                            diagnostic->value.diagnostic_id;
                        fact->value.diagnostic_ids = &fact->diagnostic;
                        fact->value.diagnostic_count = 1u;
                    }
                    break;
                }
            }
            cursor = skip_trivia(
                producer->source,
                token_end(producer->source, cursor)
            );
        }
    }
    return true;
}

static bool producer_prepare_source(Producer *producer) {
    static const char package_prefix[] =
        "kofun.package-id/v1\n"
        "kind=anonymous-single-file\n"
        "logical-source=";
    static const char file_prefix[] =
        "kofun.file-id-input/v1\n"
        "package-payload-begin\n";
    static const char file_middle[] =
        "package-payload-end\n"
        "logical-path=";
    static const char file_suffix[] =
        "\nsource-role=authored\n"
        "provenance=explicit-source\n";
    static const char module_prefix[] =
        "kofun.module-id-input/v1\n"
        "package-payload-begin\n";
    static const char module_suffix[] =
        "package-payload-end\n"
        "kind=synthetic-root\n";
    ProducerNode *module;
    uint8_t *package_payload;
    uint8_t *file_payload;
    uint8_t *module_payload;
    uint8_t *cursor;
    size_t path_length;
    size_t package_length;
    size_t file_length;
    size_t module_length;
    if (producer->input->source == NULL ||
        producer->input->source_length > UINT32_MAX ||
        producer->input->logical_path.bytes == NULL ||
        producer->input->logical_path.length == 0u) {
        return false;
    }
    path_length = producer->input->logical_path.length;
    if (path_length > SIZE_MAX - sizeof(package_prefix) - 1u) return false;
    package_length = sizeof(package_prefix) - 1u + path_length + 1u;
    if (package_length >
            SIZE_MAX - (sizeof(file_prefix) - 1u) -
                (sizeof(file_middle) - 1u) -
                path_length - (sizeof(file_suffix) - 1u) ||
        package_length >
            SIZE_MAX - (sizeof(module_prefix) - 1u) -
                (sizeof(module_suffix) - 1u)) {
        return false;
    }
    file_length =
        sizeof(file_prefix) - 1u +
        package_length +
        sizeof(file_middle) - 1u +
        path_length +
        sizeof(file_suffix) - 1u;
    module_length =
        sizeof(module_prefix) - 1u +
        package_length +
        sizeof(module_suffix) - 1u;
    if (package_length > UINT32_MAX ||
        file_length > UINT32_MAX ||
        module_length > UINT32_MAX) {
        return false;
    }
    package_payload = (uint8_t *)malloc(package_length);
    file_payload = (uint8_t *)malloc(file_length);
    module_payload = (uint8_t *)malloc(module_length);
    if (package_payload == NULL ||
        file_payload == NULL ||
        module_payload == NULL) {
        free(package_payload);
        free(file_payload);
        free(module_payload);
        return false;
    }
    cursor = package_payload;
    memcpy(cursor, package_prefix, sizeof(package_prefix) - 1u);
    cursor += sizeof(package_prefix) - 1u;
    memcpy(cursor, producer->input->logical_path.bytes, path_length);
    cursor += path_length;
    *cursor++ = '\n';
    if ((size_t)(cursor - package_payload) != package_length) {
        free(package_payload);
        free(file_payload);
        free(module_payload);
        return false;
    }
    producer_hash(
        "kofun.id.package/v1",
        package_payload,
        package_length,
        &producer->source_record.package_id
    );
    cursor = file_payload;
    memcpy(cursor, file_prefix, sizeof(file_prefix) - 1u);
    cursor += sizeof(file_prefix) - 1u;
    memcpy(cursor, package_payload, package_length);
    cursor += package_length;
    memcpy(cursor, file_middle, sizeof(file_middle) - 1u);
    cursor += sizeof(file_middle) - 1u;
    memcpy(cursor, producer->input->logical_path.bytes, path_length);
    cursor += path_length;
    memcpy(cursor, file_suffix, sizeof(file_suffix) - 1u);
    cursor += sizeof(file_suffix) - 1u;
    if ((size_t)(cursor - file_payload) != file_length) {
        free(package_payload);
        free(file_payload);
        free(module_payload);
        return false;
    }
    producer_hash(
        "kofun.id.file/v1",
        file_payload,
        file_length,
        &producer->source_record.file_id
    );
    cursor = module_payload;
    memcpy(cursor, module_prefix, sizeof(module_prefix) - 1u);
    cursor += sizeof(module_prefix) - 1u;
    memcpy(cursor, package_payload, package_length);
    cursor += package_length;
    memcpy(cursor, module_suffix, sizeof(module_suffix) - 1u);
    cursor += sizeof(module_suffix) - 1u;
    free(package_payload);
    free(file_payload);
    if ((size_t)(cursor - module_payload) != module_length) {
        free(module_payload);
        return false;
    }
    producer_hash(
        "kofun.id.module/v1",
        module_payload,
        module_length,
        &producer->source_record.module_id
    );
    free(module_payload);
    producer_namespace_id(
        0u,
        "value",
        &producer->value_namespace_id
    );
    producer_namespace_id(
        1u,
        "type",
        &producer->type_namespace_id
    );
    producer->source_record.logical_path = producer->input->logical_path;
    producer->source_record.source_bytes = producer->input->source_length;
    /* The source field carries the exact content SHA-256, not its identity
     * domain hash. */
    kofun_sha256(
        producer->input->source,
        producer->input->source_length,
        producer->source_record.source_sha256
    );
    producer->source_record.edition = producer_text("2026");
    producer->source_record.semantic_compatibility =
        producer_text("stage2-semantic-v1");
    producer->source_record.caller_generation =
        producer->input->caller_generation;
    module = producer_add_node(
        producer,
        KOFUN_SEMANTIC_NODE_MODULE,
        0,
        (int64_t)producer->input->source_length,
        "anonymous-module",
        true
    );
    if (module == NULL) return false;
    return producer_add_stable_identity(
            producer,
            module->value.node_id,
            KOFUN_SEMANTIC_ID_PACKAGE,
            &producer->source_record.package_id) &&
        producer_add_stable_identity(
            producer,
            module->value.node_id,
            KOFUN_SEMANTIC_ID_FILE,
            &producer->source_record.file_id) &&
        producer_add_stable_identity(
            producer,
            module->value.node_id,
            KOFUN_SEMANTIC_ID_MODULE,
            &producer->source_record.module_id);
}

static bool producer_add_parser_failure(Producer *producer) {
    int64_t length = (int64_t)producer->input->source_length;
    int64_t start = length == 0 ? 0 : length - 1;
    ProducerNode *error_node = producer_add_node(
        producer,
        KOFUN_SEMANTIC_NODE_ERROR_PATTERN,
        start,
        length,
        "parser-recovery",
        false
    );
    ProducerDiagnostic *diagnostic;
    if (error_node == NULL) return false;
    diagnostic = producer_add_diagnostic(
        producer,
        "E2S03",
        "parser",
        "incomplete-top-level-declaration",
        "incomplete Stage 2 declaration after committed token spans",
        error_node->value.span,
        error_node->value.node_id
    );
    if (diagnostic == NULL) return false;
    producer_mark_node_error(error_node, diagnostic);
    return true;
}

static bool producer_emit(
    Producer *producer,
    KofunSemanticSink *sink,
    bool cancellation_observed_after_commit,
    KofunStage2SemanticResult *result
) {
    size_t index;
    unsigned fact_kind;
    KofunSourceStatus source_status;
    KofunCompleteness completeness;
    if (!kofun_semantic_begin(sink, &producer->source_record)) {
        result->tooling_emission_failed = true;
        return false;
    }
    for (index = 0u; index < producer->node_count; index += 1u) {
        if (!kofun_semantic_node(sink, &producer->nodes[index].value)) {
            result->tooling_emission_failed = true;
            return false;
        }
    }
    for (index = 0u; index < producer->identity_count; index += 1u) {
        if (!kofun_semantic_identity(
                sink,
                &producer->identities[index].value)) {
            result->tooling_emission_failed = true;
            return false;
        }
    }
    for (index = 0u; index < producer->reference_count; index += 1u) {
        if (!kofun_semantic_reference(
                sink,
                &producer->references[index].value)) {
            result->tooling_emission_failed = true;
            return false;
        }
    }
    for (fact_kind = KOFUN_SEMANTIC_FACT_TYPE;
         fact_kind <= KOFUN_SEMANTIC_FACT_ORIGIN;
         fact_kind += 1u) {
        for (index = 0u; index < producer->fact_count; index += 1u) {
            if ((unsigned)producer->facts[index].value.kind != fact_kind) {
                continue;
            }
            if (!kofun_semantic_fact(sink, &producer->facts[index].value)) {
                result->tooling_emission_failed = true;
                return false;
            }
        }
    }
    for (index = 0u; index < producer->diagnostic_count; index += 1u) {
        if (!kofun_semantic_diagnostic(
                sink,
                &producer->diagnostics[index].value)) {
            result->tooling_emission_failed = true;
            return false;
        }
    }
    if (cancellation_observed_after_commit && !producer->language_failed) {
        kofun_semantic_cancellation_observed(sink);
        source_status = KOFUN_SOURCE_CANCELLED;
        completeness = KOFUN_SEMANTIC_PARTIAL;
    } else if (producer->language_failed) {
        source_status = KOFUN_SOURCE_FAILED;
        completeness = KOFUN_SEMANTIC_PARTIAL;
    } else {
        source_status = KOFUN_SOURCE_CHECKED;
        completeness = KOFUN_SEMANTIC_COMPLETE;
    }
    if (!kofun_semantic_end(sink, source_status, completeness)) {
        result->tooling_emission_failed = true;
        return false;
    }
    result->source_status = source_status;
    result->completeness = completeness;
    return true;
}

bool kofun_stage2_produce_semantic_events(
    const KofunStage2SemanticInput *input,
    KofunSemanticSink *sink,
    bool cancellation_observed_after_commit,
    KofunStage2SemanticResult *result
) {
    Producer producer;
    char *owned_source;
    char *tokens;
    char *program;
    char *scope_hir;
    char *ownership;
    bool parsed;
    if (result == NULL) return false;
    memset(result, 0, sizeof(*result));
    result->source_status = KOFUN_SOURCE_FAILED;
    result->completeness = KOFUN_SEMANTIC_PARTIAL;
    if (input == NULL || sink == NULL ||
        input->source == NULL ||
        input->source_length > SIZE_MAX - 1u) {
        result->tooling_emission_failed = true;
        return false;
    }
    if (memchr(input->source, 0, input->source_length) != NULL) {
        return false;
    }
    owned_source = (char *)malloc(input->source_length + 1u);
    if (owned_source == NULL) {
        result->tooling_emission_failed = true;
        return false;
    }
    memcpy(owned_source, input->source, input->source_length);
    owned_source[input->source_length] = '\0';
    memset(&producer, 0, sizeof(producer));
    producer.input = input;
    producer.source = owned_source;

    tokens = lex_source(owned_source);
    if (strncmp(tokens, "kofun-token-tape/v1\n", 20u) != 0) {
        free(tokens);
        free(owned_source);
        return false;
    }
    result->token_span_committed = true;
    free(tokens);
    if (!producer_prepare_source(&producer)) {
        free(owned_source);
        result->tooling_emission_failed = true;
        return false;
    }
    program = parse_program(owned_source);
    parsed = strncmp(
        program,
        "kofun-stage2-ir/v1\n",
        strlen("kofun-stage2-ir/v1\n")
    ) == 0;
    free(program);
    if (!parsed) {
        if (!producer_add_parser_failure(&producer)) {
            free(owned_source);
            result->tooling_emission_failed = true;
            return false;
        }
    } else {
        scope_hir = build_scope_hir_mode(owned_source, true);
        if (strncmp(
                scope_hir,
                "kofun-scope-hir/v1\n",
                strlen("kofun-scope-hir/v1\n")) != 0) {
            free(scope_hir);
            if (!producer_add_parser_failure(&producer)) {
                free(owned_source);
                result->tooling_emission_failed = true;
                return false;
            }
        } else {
            producer.scope_hir = scope_hir;
            if (!producer_collect_types(&producer) ||
                !producer_collect_functions(&producer) ||
                !producer_collect_parameters_and_bindings(&producer) ||
                !producer_collect_references(&producer)) {
                free(scope_hir);
                free(owned_source);
                result->tooling_emission_failed = true;
                return false;
            }
            free(scope_hir);
            producer.scope_hir = NULL;
            ownership = borrowed_collection_check(owned_source);
            free(ownership);
            if (!producer_apply_ownership(&producer)) {
                free(owned_source);
                result->tooling_emission_failed = true;
                return false;
            }
        }
    }
    if (producer.resource_failed) {
        free(owned_source);
        result->tooling_emission_failed = true;
        return false;
    }
    if (!producer_emit(
            &producer,
            sink,
            cancellation_observed_after_commit,
            result)) {
        free(owned_source);
        return false;
    }
    free(owned_source);
    return true;
}

#ifndef KOFUN_STAGE2_SEMANTIC_PRODUCER_LIBRARY
static uint8_t *producer_read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *bytes;
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)size + 1u);
    if (bytes == NULL) {
        (void)fclose(file);
        return NULL;
    }
    if (fread(bytes, 1u, (size_t)size, file) != (size_t)size) {
        (void)fclose(file);
        free(bytes);
        return NULL;
    }
    if (fclose(file) != 0) {
        free(bytes);
        return NULL;
    }
    bytes[size] = 0u;
    *length = (size_t)size;
    return bytes;
}

int main(int argc, char **argv) {
    bool cancel = false;
    int offset = 1;
    uint8_t *source;
    size_t source_length;
    char *generation_end = NULL;
    unsigned long long generation;
    KofunStage2SemanticInput input;
    KofunStage2SemanticResult result;
    KofunSemanticStream *stream;
    KofunSemanticSink sink;
    const uint8_t *event_bytes;
    size_t event_length;
    if (argc == 6 && strcmp(argv[1], "--cancel-after-commit") == 0) {
        cancel = true;
        offset = 2;
    } else if (argc != 5) {
        fputs(
            "usage: kofun-stage2-semantic-events "
            "[--cancel-after-commit] INPUT LOGICAL-PATH OUTPUT GENERATION\n",
            stderr
        );
        return 2;
    }
    errno = 0;
    generation = strtoull(argv[offset + 3], &generation_end, 10);
    if (errno != 0 || generation_end == argv[offset + 3] ||
        *generation_end != '\0') {
        fputs("semantic events: invalid generation\n", stderr);
        return 2;
    }
    source = producer_read_file(argv[offset], &source_length);
    if (source == NULL) {
        fputs("semantic events: cannot read source\n", stderr);
        return 3;
    }
    memset(&input, 0, sizeof(input));
    input.source = source;
    input.source_length = source_length;
    input.logical_path = producer_text(argv[offset + 1]);
    input.caller_generation = (uint64_t)generation;
    stream = kofun_semantic_stream_create();
    if (stream == NULL) {
        free(source);
        return 3;
    }
    sink = kofun_semantic_stream_sink(stream);
    if (!kofun_stage2_produce_semantic_events(
            &input,
            &sink,
            cancel,
            &result)) {
        free(source);
        kofun_semantic_stream_destroy(stream);
        return result.tooling_emission_failed ? 3 : 1;
    }
    if (!kofun_semantic_stream_bytes(stream, &event_bytes, &event_length) ||
        event_length == 0u ||
        !kofun_semantic_stream_commit(stream, argv[offset + 2])) {
        free(source);
        kofun_semantic_stream_destroy(stream);
        return 3;
    }
    free(source);
    kofun_semantic_stream_destroy(stream);
    return result.source_status == KOFUN_SOURCE_CHECKED ? 0 : 1;
}
#endif
