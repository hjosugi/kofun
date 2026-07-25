#include "../../bootstrap/stage2/semantic_events.h"
#include "../../bootstrap/stage2/semantic_producer.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
            exit(1); \
        } \
    } while (0)

enum {
    AUDIT_BEGIN = 1,
    AUDIT_NODE = 2,
    AUDIT_IDENTITY = 3,
    AUDIT_REFERENCE = 4,
    AUDIT_FACT = 5,
    AUDIT_DIAGNOSTIC = 6,
    AUDIT_END = 7,
    AUDIT_CANCEL = 8,
    AUDIT_PHASES = 8
};

typedef struct {
    uint8_t *bytes;
    size_t length;
    size_t capacity;
} Capture;

typedef struct {
    KofunSemanticSink *downstream;
    Capture capture;
    unsigned calls[AUDIT_PHASES];
    unsigned reject_phase;
    unsigned node_kinds[16];
    unsigned identity_kinds[16];
    unsigned reference_target_kinds[16];
    unsigned origin_facts;
    unsigned ownership_facts;
    unsigned diagnostic_related;
    unsigned diagnostic_remedies;
    unsigned dependent_nodes;
    unsigned dependent_facts;
    bool saw_callable_signature;
    bool node_source_order;
    bool reference_source_order;
    bool saw_nonmodule_node;
    bool saw_reference;
    uint32_t last_node_start;
    uint32_t last_reference_start;
} Audit;

static KofunSemanticBytes text(const char *value) {
    KofunSemanticBytes result;
    result.bytes = (const uint8_t *)value;
    result.length = (uint32_t)strlen(value);
    return result;
}

static bool text_is(KofunSemanticBytes value, const char *expected) {
    size_t length = strlen(expected);
    return value.length == length &&
        memcmp(value.bytes, expected, length) == 0;
}

static uint8_t *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    long file_size;
    uint8_t *bytes;
    CHECK(file != NULL);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    file_size = ftell(file);
    CHECK(file_size >= 0);
    CHECK(fseek(file, 0, SEEK_SET) == 0);
    bytes = (uint8_t *)malloc((size_t)file_size + 1u);
    CHECK(bytes != NULL);
    CHECK(fread(bytes, 1u, (size_t)file_size, file) == (size_t)file_size);
    CHECK(fclose(file) == 0);
    bytes[file_size] = 0u;
    *length = (size_t)file_size;
    return bytes;
}

static bool capture_put(Capture *capture, const void *bytes, size_t length) {
    size_t required;
    size_t capacity;
    uint8_t *grown;
    if (length > SIZE_MAX - capture->length) return false;
    required = capture->length + length;
    if (required > capture->capacity) {
        capacity = capture->capacity == 0u ? 256u : capture->capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2u) {
                capacity = required;
                break;
            }
            capacity *= 2u;
        }
        grown = (uint8_t *)realloc(capture->bytes, capacity);
        if (grown == NULL) return false;
        capture->bytes = grown;
        capture->capacity = capacity;
    }
    if (length != 0u) {
        memcpy(capture->bytes + capture->length, bytes, length);
    }
    capture->length = required;
    return true;
}

static bool capture_u8(Capture *capture, uint8_t value) {
    return capture_put(capture, &value, sizeof(value));
}

static bool capture_u16(Capture *capture, uint16_t value) {
    uint8_t bytes[2];
    bytes[0] = (uint8_t)(value >> 8u);
    bytes[1] = (uint8_t)value;
    return capture_put(capture, bytes, sizeof(bytes));
}

static bool capture_u32(Capture *capture, uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value >> 24u);
    bytes[1] = (uint8_t)(value >> 16u);
    bytes[2] = (uint8_t)(value >> 8u);
    bytes[3] = (uint8_t)value;
    return capture_put(capture, bytes, sizeof(bytes));
}

static bool capture_u64(Capture *capture, uint64_t value) {
    uint8_t bytes[8];
    unsigned index;
    for (index = 0u; index < 8u; index += 1u) {
        bytes[index] = (uint8_t)(value >> (56u - index * 8u));
    }
    return capture_put(capture, bytes, sizeof(bytes));
}

static bool capture_id(Capture *capture, const KofunSemanticId *id) {
    return capture_put(capture, id->bytes, sizeof(id->bytes));
}

static bool capture_span(Capture *capture, KofunSemanticSpan span) {
    return capture_u32(capture, span.start) &&
        capture_u32(capture, span.end);
}

static bool capture_text(Capture *capture, KofunSemanticBytes value) {
    return capture_u32(capture, value.length) &&
        capture_put(capture, value.bytes, value.length);
}

static bool capture_ids(
    Capture *capture,
    const KofunSemanticId *ids,
    uint16_t count
) {
    uint16_t index;
    if (!capture_u16(capture, count)) return false;
    for (index = 0u; index < count; index += 1u) {
        if (!capture_id(capture, &ids[index])) return false;
    }
    return true;
}

static bool capture_u32s(
    Capture *capture,
    const uint32_t *values,
    uint16_t count
) {
    uint16_t index;
    if (!capture_u16(capture, count)) return false;
    for (index = 0u; index < count; index += 1u) {
        if (!capture_u32(capture, values[index])) return false;
    }
    return true;
}

static bool capture_related(
    Capture *capture,
    const KofunSemanticRelated *related,
    uint16_t count
) {
    uint16_t index;
    if (!capture_u16(capture, count)) return false;
    for (index = 0u; index < count; index += 1u) {
        if (!capture_id(capture, &related[index].file_id) ||
            !capture_span(capture, related[index].span) ||
            !capture_text(capture, related[index].label)) {
            return false;
        }
    }
    return true;
}

static bool capture_edits(
    Capture *capture,
    const KofunSemanticEdit *edits,
    uint16_t count
) {
    uint16_t index;
    if (!capture_u16(capture, count)) return false;
    for (index = 0u; index < count; index += 1u) {
        if (!capture_u32(capture, edits[index].remedy_id) ||
            !capture_id(capture, &edits[index].file_id) ||
            !capture_span(capture, edits[index].span) ||
            !capture_text(capture, edits[index].replacement)) {
            return false;
        }
    }
    return true;
}

static bool audit_enter(Audit *audit, unsigned phase) {
    audit->calls[phase - 1u] += 1u;
    return audit->reject_phase != phase;
}

static bool audit_begin(void *context, const KofunSemanticSource *source) {
    Audit *audit = (Audit *)context;
    if (!audit_enter(audit, AUDIT_BEGIN)) return false;
    if (audit->downstream != NULL &&
        !kofun_semantic_begin(audit->downstream, source)) {
        return false;
    }
    return capture_u8(&audit->capture, AUDIT_BEGIN) &&
        capture_id(&audit->capture, &source->package_id) &&
        capture_id(&audit->capture, &source->module_id) &&
        capture_id(&audit->capture, &source->file_id) &&
        capture_text(&audit->capture, source->logical_path) &&
        capture_u64(&audit->capture, source->source_bytes) &&
        capture_put(
            &audit->capture,
            source->source_sha256,
            sizeof(source->source_sha256)
        ) &&
        capture_text(&audit->capture, source->edition) &&
        capture_text(&audit->capture, source->semantic_compatibility) &&
        capture_u64(&audit->capture, source->caller_generation) &&
        capture_u8(&audit->capture, source->compiler_exit_class);
}

static bool audit_node(void *context, const KofunSemanticNode *node) {
    Audit *audit = (Audit *)context;
    if (!audit_enter(audit, AUDIT_NODE)) return false;
    if (audit->downstream != NULL &&
        !kofun_semantic_node(audit->downstream, node)) {
        return false;
    }
    if ((unsigned)node->kind <
        sizeof(audit->node_kinds) / sizeof(audit->node_kinds[0])) {
        audit->node_kinds[node->kind] += 1u;
    }
    if (node->kind != KOFUN_SEMANTIC_NODE_MODULE) {
        if (audit->saw_nonmodule_node &&
            node->span.start < audit->last_node_start) {
            audit->node_source_order = false;
        }
        audit->saw_nonmodule_node = true;
        audit->last_node_start = node->span.start;
    }
    if (node->dependency_count != 0u) audit->dependent_nodes += 1u;
    return capture_u8(&audit->capture, AUDIT_NODE) &&
        capture_id(&audit->capture, &node->node_id) &&
        capture_u8(&audit->capture, (uint8_t)node->kind) &&
        capture_span(&audit->capture, node->span) &&
        capture_u8(&audit->capture, (uint8_t)node->status) &&
        capture_ids(
            &audit->capture, node->dependencies, node->dependency_count
        ) &&
        capture_ids(
            &audit->capture, node->diagnostic_ids, node->diagnostic_count
        );
}

static bool audit_identity(
    void *context,
    const KofunSemanticIdentity *identity
) {
    Audit *audit = (Audit *)context;
    if (!audit_enter(audit, AUDIT_IDENTITY)) return false;
    if (audit->downstream != NULL &&
        !kofun_semantic_identity(audit->downstream, identity)) {
        return false;
    }
    if ((unsigned)identity->kind <
        sizeof(audit->identity_kinds) /
            sizeof(audit->identity_kinds[0])) {
        audit->identity_kinds[identity->kind] += 1u;
    }
    return capture_u8(&audit->capture, AUDIT_IDENTITY) &&
        capture_id(&audit->capture, &identity->owner_node_id) &&
        capture_u8(&audit->capture, (uint8_t)identity->kind) &&
        capture_id(&audit->capture, &identity->value) &&
        capture_u8(&audit->capture, (uint8_t)identity->status);
}

static bool audit_reference(
    void *context,
    const KofunSemanticReference *reference
) {
    Audit *audit = (Audit *)context;
    if (!audit_enter(audit, AUDIT_REFERENCE)) return false;
    if (audit->downstream != NULL &&
        !kofun_semantic_reference(audit->downstream, reference)) {
        return false;
    }
    if ((unsigned)reference->target_kind <
        sizeof(audit->reference_target_kinds) /
            sizeof(audit->reference_target_kinds[0])) {
        audit->reference_target_kinds[reference->target_kind] += 1u;
    }
    if (audit->saw_reference &&
        reference->span.start < audit->last_reference_start) {
        audit->reference_source_order = false;
    }
    audit->saw_reference = true;
    audit->last_reference_start = reference->span.start;
    return capture_u8(&audit->capture, AUDIT_REFERENCE) &&
        capture_id(&audit->capture, &reference->reference_id) &&
        capture_id(&audit->capture, &reference->source_node_id) &&
        capture_u8(&audit->capture, (uint8_t)reference->name_space) &&
        capture_span(&audit->capture, reference->span) &&
        capture_u8(&audit->capture, (uint8_t)reference->status) &&
        capture_u8(&audit->capture, (uint8_t)reference->target_shape) &&
        capture_u8(&audit->capture, (uint8_t)reference->target_kind) &&
        capture_id(&audit->capture, &reference->target_value) &&
        capture_text(&audit->capture, reference->hidden_reason) &&
        capture_ids(
            &audit->capture,
            reference->diagnostic_ids,
            reference->diagnostic_count
        );
}

static bool audit_fact(void *context, const KofunSemanticFact *fact) {
    Audit *audit = (Audit *)context;
    if (!audit_enter(audit, AUDIT_FACT)) return false;
    if (audit->downstream != NULL &&
        !kofun_semantic_fact(audit->downstream, fact)) {
        return false;
    }
    if (fact->kind == KOFUN_SEMANTIC_FACT_ORIGIN) {
        audit->origin_facts += 1u;
    }
    if (fact->kind == KOFUN_SEMANTIC_FACT_OWNERSHIP) {
        audit->ownership_facts += 1u;
    }
    if (fact->dependency_count != 0u) audit->dependent_facts += 1u;
    if (fact->kind == KOFUN_SEMANTIC_FACT_TYPE &&
        text_is(fact->display, "Int -> Int")) {
        audit->saw_callable_signature = true;
    }
    return capture_u8(&audit->capture, AUDIT_FACT) &&
        capture_id(&audit->capture, &fact->owner_node_id) &&
        capture_u8(&audit->capture, (uint8_t)fact->kind) &&
        capture_u8(&audit->capture, (uint8_t)fact->status) &&
        capture_text(&audit->capture, fact->display) &&
        capture_text(&audit->capture, fact->reason) &&
        capture_ids(
            &audit->capture, fact->dependencies, fact->dependency_count
        ) &&
        capture_ids(
            &audit->capture, fact->diagnostic_ids, fact->diagnostic_count
        );
}

static bool audit_diagnostic(
    void *context,
    const KofunSemanticDiagnostic *diagnostic
) {
    Audit *audit = (Audit *)context;
    if (!audit_enter(audit, AUDIT_DIAGNOSTIC)) return false;
    if (audit->downstream != NULL &&
        !kofun_semantic_diagnostic(audit->downstream, diagnostic)) {
        return false;
    }
    audit->diagnostic_related += diagnostic->related_count;
    audit->diagnostic_remedies += diagnostic->remedy_count;
    return capture_u8(&audit->capture, AUDIT_DIAGNOSTIC) &&
        capture_id(&audit->capture, &diagnostic->diagnostic_id) &&
        capture_text(&audit->capture, diagnostic->code) &&
        capture_text(&audit->capture, diagnostic->category) &&
        capture_u8(&audit->capture, (uint8_t)diagnostic->severity) &&
        capture_text(&audit->capture, diagnostic->template_id) &&
        capture_id(&audit->capture, &diagnostic->primary_file_id) &&
        capture_span(&audit->capture, diagnostic->primary_span) &&
        capture_text(&audit->capture, diagnostic->fallback_text) &&
        capture_ids(
            &audit->capture,
            diagnostic->affected_ids,
            diagnostic->affected_count
        ) &&
        capture_u32s(
            &audit->capture,
            diagnostic->remedy_ids,
            diagnostic->remedy_count
        ) &&
        capture_u8(&audit->capture, diagnostic->truncated ? 1u : 0u) &&
        capture_related(
            &audit->capture,
            diagnostic->related,
            diagnostic->related_count
        ) &&
        capture_edits(
            &audit->capture,
            diagnostic->edits,
            diagnostic->edit_count
        );
}

static void audit_cancellation(void *context) {
    Audit *audit = (Audit *)context;
    audit->calls[AUDIT_CANCEL - 1u] += 1u;
    if (audit->downstream != NULL) {
        kofun_semantic_cancellation_observed(audit->downstream);
    }
    (void)capture_u8(&audit->capture, AUDIT_CANCEL);
}

static bool audit_end(
    void *context,
    KofunSourceStatus status,
    KofunCompleteness completeness
) {
    Audit *audit = (Audit *)context;
    if (!audit_enter(audit, AUDIT_END)) return false;
    if (audit->downstream != NULL &&
        !kofun_semantic_end(audit->downstream, status, completeness)) {
        return false;
    }
    return capture_u8(&audit->capture, AUDIT_END) &&
        capture_u8(&audit->capture, (uint8_t)status) &&
        capture_u8(&audit->capture, (uint8_t)completeness);
}

static KofunSemanticSink audit_sink(Audit *audit) {
    KofunSemanticSink sink;
    memset(&sink, 0, sizeof(sink));
    audit->node_source_order = true;
    audit->reference_source_order = true;
    sink.context = audit;
    sink.begin = audit_begin;
    sink.node = audit_node;
    sink.identity = audit_identity;
    sink.reference = audit_reference;
    sink.fact = audit_fact;
    sink.diagnostic = audit_diagnostic;
    sink.cancellation_observed = audit_cancellation;
    sink.end = audit_end;
    return sink;
}

static void audit_destroy(Audit *audit) {
    free(audit->capture.bytes);
    memset(audit, 0, sizeof(*audit));
}

static bool same_language_result(
    const KofunStage2SemanticResult *left,
    const KofunStage2SemanticResult *right
) {
    return left->compiler_exit_class == right->compiler_exit_class &&
        left->token_span_committed == right->token_span_committed &&
        left->has_source_diagnostic == right->has_source_diagnostic &&
        left->diagnostic_has_byte_span == right->diagnostic_has_byte_span &&
        left->diagnostic_truncated == right->diagnostic_truncated &&
        left->diagnostic_span.start == right->diagnostic_span.start &&
        left->diagnostic_span.end == right->diagnostic_span.end &&
        strcmp(left->diagnostic_code, right->diagnostic_code) == 0 &&
        strcmp(left->diagnostic_category, right->diagnostic_category) == 0 &&
        strcmp(left->diagnostic_template_id, right->diagnostic_template_id) == 0 &&
        strcmp(left->diagnostic_fallback, right->diagnostic_fallback) == 0;
}

static bool produce_authority(
    const uint8_t *source,
    size_t source_length,
    const char *logical_path,
    KofunStage2SemanticAuthority authority,
    Audit *audit,
    KofunStage2SemanticResult *result
) {
    KofunStage2SemanticInput input;
    KofunSemanticSink sink = audit_sink(audit);
    memset(&input, 0, sizeof(input));
    input.source = source;
    input.source_length = source_length;
    input.logical_path = text(logical_path);
    input.caller_generation = UINT64_C(90210);
    input.authority = authority;
    return kofun_stage2_produce_semantic_events(
        &input, &sink, false, result
    );
}

static bool produce(
    const uint8_t *source,
    size_t source_length,
    const char *logical_path,
    Audit *audit,
    KofunStage2SemanticResult *result
) {
    return produce_authority(
        source,
        source_length,
        logical_path,
        KOFUN_STAGE2_SEMANTIC_COMPILE,
        audit,
        result
    );
}

static void test_invalid_paths_emit_nothing(
    const uint8_t *source,
    size_t source_length
) {
    static const uint8_t decomposed_path[] = {
        's', 'r', 'c', '/', 'e', 0xcc, 0x81,
        '.', 'k', 'o', 'f', 'u', 'n'
    };
    static const char *invalid_text_paths[] = {
        "/abs/main.kofun",
        "C:/src/main.kofun",
        "file://src/main.kofun",
        "../src/main.kofun",
        "src/../main.kofun",
        "src/\nprivate.kofun"
    };
    size_t index;
    for (index = 0u;
         index <= sizeof(invalid_text_paths) / sizeof(invalid_text_paths[0]);
         index += 1u) {
        Audit audit;
        KofunSemanticSink sink;
        KofunStage2SemanticInput input;
        KofunStage2SemanticResult result;
        KofunSemanticBytes logical_path;
        unsigned phase;
        memset(&audit, 0, sizeof(audit));
        memset(&input, 0, sizeof(input));
        sink = audit_sink(&audit);
        if (index <
            sizeof(invalid_text_paths) / sizeof(invalid_text_paths[0])) {
            logical_path = text(invalid_text_paths[index]);
        } else {
            logical_path.bytes = decomposed_path;
            logical_path.length = sizeof(decomposed_path);
        }
        CHECK(!kofun_semantic_validate_logical_path(logical_path));
        input.source = source;
        input.source_length = source_length;
        input.logical_path = logical_path;
        input.caller_generation = UINT64_C(90210);
        input.authority = KOFUN_STAGE2_SEMANTIC_COMPILE;
        CHECK(!kofun_stage2_produce_semantic_events(
            &input, &sink, false, &result
        ));
        CHECK(result.tooling_emission_failed);
        CHECK(strcmp(result.tooling_error.code, "ETS04") == 0);
        CHECK(result.tooling_error.record_index == 0u);
        CHECK(result.tooling_error.event_kind == 0u);
        for (phase = 0u; phase < AUDIT_PHASES; phase += 1u) {
            CHECK(audit.calls[phase] == 0u);
        }
        audit_destroy(&audit);
    }
}

static void test_dual_sink(
    const uint8_t *source,
    size_t source_length,
    const char *logical_path
) {
    Audit direct;
    Audit decoded;
    Audit rejected;
    KofunStage2SemanticResult direct_result;
    KofunStage2SemanticResult kse_result;
    KofunStage2SemanticInput input;
    KofunSemanticStream *stream = kofun_semantic_stream_create();
    KofunSemanticSink kse_sink;
    KofunSemanticSink decoded_sink;
    KofunSemanticSink rejected_sink;
    KofunSemanticError replay_error;
    const uint8_t *bytes;
    uint8_t *corrupt;
    size_t length;
    unsigned phase;
    memset(&direct, 0, sizeof(direct));
    memset(&decoded, 0, sizeof(decoded));
    memset(&rejected, 0, sizeof(rejected));
    memset(&input, 0, sizeof(input));
    CHECK(stream != NULL);
    kse_sink = kofun_semantic_stream_sink(stream);
    CHECK(produce(
        source, source_length, logical_path, &direct, &direct_result
    ));
    input.source = source;
    input.source_length = source_length;
    input.logical_path = text(logical_path);
    input.caller_generation = UINT64_C(90210);
    input.authority = KOFUN_STAGE2_SEMANTIC_COMPILE;
    CHECK(kofun_stage2_produce_semantic_events(
        &input, &kse_sink, false, &kse_result
    ));
    CHECK(!direct_result.tooling_emission_failed);
    CHECK(!kse_result.tooling_emission_failed);
    CHECK(same_language_result(&direct_result, &kse_result));
    if (direct_result.compiler_exit_class == 0u) {
        CHECK(direct.node_kinds[KOFUN_SEMANTIC_NODE_CALL] != 0u);
        CHECK(direct.node_kinds[KOFUN_SEMANTIC_NODE_IF] != 0u);
        CHECK(direct.node_kinds[KOFUN_SEMANTIC_NODE_MATCH] != 0u);
        CHECK(direct.identity_kinds[KOFUN_SEMANTIC_ID_TYPE] != 0u);
        CHECK(
            direct.identity_kinds[KOFUN_SEMANTIC_ID_CONSTRUCTOR] >= 2u
        );
        CHECK(
            direct.reference_target_kinds[
                KOFUN_SEMANTIC_ID_CONSTRUCTOR] != 0u
        );
        CHECK(direct.saw_callable_signature);
        CHECK(direct.dependent_nodes != 0u);
        CHECK(direct.dependent_facts != 0u);
        CHECK(
            direct.origin_facts ==
                direct.calls[AUDIT_NODE - 1u]
        );
        CHECK(direct.node_source_order);
        CHECK(direct.reference_source_order);
    }
    CHECK(direct_result.source_status == kse_result.source_status);
    CHECK(direct_result.completeness == kse_result.completeness);
    CHECK(kofun_semantic_stream_bytes(stream, &bytes, &length));
    decoded_sink = audit_sink(&decoded);
    CHECK(kofun_semantic_replay_stream(
        bytes, length, &decoded_sink, &replay_error
    ));
    CHECK(direct.capture.length == decoded.capture.length);
    CHECK(memcmp(
        direct.capture.bytes,
        decoded.capture.bytes,
        direct.capture.length
    ) == 0);

    rejected.reject_phase = AUDIT_NODE;
    rejected_sink = audit_sink(&rejected);
    CHECK(!kofun_semantic_replay_stream(
        bytes, length, &rejected_sink, &replay_error
    ));
    CHECK(strcmp(replay_error.code, "ETS03") == 0);
    CHECK(strstr(replay_error.detail, "rejected") != NULL);

    corrupt = (uint8_t *)malloc(length);
    CHECK(corrupt != NULL);
    memcpy(corrupt, bytes, length);
    corrupt[length - 1u] ^= UINT8_C(1);
    audit_destroy(&rejected);
    memset(&rejected, 0, sizeof(rejected));
    rejected_sink = audit_sink(&rejected);
    CHECK(!kofun_semantic_replay_stream(
        corrupt, length, &rejected_sink, &replay_error
    ));
    for (phase = 0u; phase < AUDIT_PHASES; phase += 1u) {
        CHECK(rejected.calls[phase] == 0u);
    }
    free(corrupt);
    audit_destroy(&direct);
    audit_destroy(&decoded);
    audit_destroy(&rejected);
    kofun_semantic_stream_destroy(stream);
}

static void reject_phase_preserves_language(
    const uint8_t *source,
    size_t source_length,
    const char *logical_path,
    unsigned phase
) {
    Audit baseline;
    Audit rejected;
    KofunStage2SemanticResult baseline_result;
    KofunStage2SemanticResult rejected_result;
    bool baseline_ok;
    memset(&baseline, 0, sizeof(baseline));
    memset(&rejected, 0, sizeof(rejected));
    baseline_ok = produce(
        source,
        source_length,
        logical_path,
        &baseline,
        &baseline_result
    );
    CHECK(baseline_ok);
    CHECK(baseline.calls[phase - 1u] != 0u);
    rejected.reject_phase = phase;
    CHECK(!produce(
        source,
        source_length,
        logical_path,
        &rejected,
        &rejected_result
    ));
    CHECK(rejected_result.tooling_emission_failed);
    CHECK(strcmp(rejected_result.tooling_error.code, "ETS03") == 0);
    CHECK(rejected_result.tooling_error.event_kind == phase);
    CHECK(strstr(rejected_result.tooling_error.detail, "rejected") != NULL);
    CHECK(same_language_result(&baseline_result, &rejected_result));
    audit_destroy(&baseline);
    audit_destroy(&rejected);
}

static void test_failed_prefix_content(
    const char *parse_path,
    const char *scope_path,
    const char *ownership_path
) {
    uint8_t *source;
    size_t length;
    Audit audit;
    KofunStage2SemanticResult result;

    source = read_file(parse_path, &length);
    memset(&audit, 0, sizeof(audit));
    CHECK(produce(
        source, length, "src/parse-prefix.kofun", &audit, &result
    ));
    CHECK(result.compiler_exit_class != 0u);
    CHECK(audit.node_kinds[KOFUN_SEMANTIC_NODE_FUNCTION] != 0u);
    CHECK(audit.identity_kinds[KOFUN_SEMANTIC_ID_SYMBOL] != 0u);
    audit_destroy(&audit);
    free(source);

    source = read_file(scope_path, &length);
    memset(&audit, 0, sizeof(audit));
    CHECK(produce(
        source, length, "src/scope-prefix.kofun", &audit, &result
    ));
    CHECK(strcmp(result.diagnostic_code, "E2S47") == 0);
    CHECK(audit.node_kinds[KOFUN_SEMANTIC_NODE_SCOPE] >= 2u);
    CHECK(audit.node_kinds[KOFUN_SEMANTIC_NODE_LOCAL] >= 2u);
    CHECK(audit.diagnostic_related != 0u);
    CHECK(audit.diagnostic_remedies != 0u);
    audit_destroy(&audit);
    free(source);

    source = read_file(ownership_path, &length);
    memset(&audit, 0, sizeof(audit));
    CHECK(produce_authority(
        source,
        length,
        "src/ownership-prefix.kofun",
        KOFUN_STAGE2_SEMANTIC_OWNERSHIP,
        &audit,
        &result
    ));
    CHECK(strcmp(result.diagnostic_code, "E007") == 0);
    CHECK(audit.node_kinds[KOFUN_SEMANTIC_NODE_SCOPE] >= 2u);
    CHECK(audit.node_kinds[KOFUN_SEMANTIC_NODE_PARAMETER] != 0u);
    CHECK(audit.node_kinds[KOFUN_SEMANTIC_NODE_LOCAL] != 0u);
    CHECK(audit.node_kinds[KOFUN_SEMANTIC_NODE_REFERENCE] != 0u);
    CHECK(audit.ownership_facts != 0u);
    CHECK(audit.diagnostic_remedies != 0u);
    audit_destroy(&audit);
    free(source);
}

int main(int argc, char **argv) {
    uint8_t *valid_source;
    uint8_t *invalid_source;
    size_t valid_length;
    size_t invalid_length;
    unsigned phase;
    CHECK(argc == 6);
    valid_source = read_file(argv[1], &valid_length);
    invalid_source = read_file(argv[2], &invalid_length);

    test_invalid_paths_emit_nothing(valid_source, valid_length);
    test_dual_sink(valid_source, valid_length, "src/main.kofun");
    test_dual_sink(
        invalid_source, invalid_length, "src/invalid.kofun"
    );
    for (phase = AUDIT_BEGIN; phase <= AUDIT_END; phase += 1u) {
        if (phase == AUDIT_DIAGNOSTIC) continue;
        reject_phase_preserves_language(
            valid_source,
            valid_length,
            "src/main.kofun",
            phase
        );
    }
    reject_phase_preserves_language(
        invalid_source,
        invalid_length,
        "src/invalid.kofun",
        AUDIT_DIAGNOSTIC
    );
    test_failed_prefix_content(argv[3], argv[4], argv[5]);

    free(valid_source);
    free(invalid_source);
    puts("PASS: Stage 2 producer dual-sink records and rejection preservation");
    return 0;
}
