#include "../../bootstrap/stage2/semantic_events.h"
#include "../../bootstrap/stage2/sha256.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            exit(1); \
        } \
    } while (0)

typedef struct {
    uint8_t *source;
    size_t source_length;
    KofunSemanticSource semantic_source;
    KofunSemanticId file_id;
} Fixture;

static KofunSemanticBytes text(const char *value) {
    KofunSemanticBytes result;
    result.bytes = (const uint8_t *)value;
    result.length = (uint32_t)strlen(value);
    return result;
}

static void id_from_text(const char *value, KofunSemanticId *id) {
    kofun_sha256(
        (const uint8_t *)value,
        strlen(value),
        id->bytes
    );
}

static int compare_ids(const void *left, const void *right) {
    return memcmp(
        ((const KofunSemanticId *)left)->bytes,
        ((const KofunSemanticId *)right)->bytes,
        KOFUN_SEMANTIC_ID_BYTES
    );
}

static uint8_t *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *bytes;
    CHECK(file != NULL);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    CHECK(size >= 0);
    CHECK(fseek(file, 0, SEEK_SET) == 0);
    bytes = (uint8_t *)malloc((size_t)size + 1u);
    CHECK(bytes != NULL);
    CHECK(fread(bytes, 1u, (size_t)size, file) == (size_t)size);
    CHECK(fclose(file) == 0);
    bytes[size] = 0u;
    *length = (size_t)size;
    return bytes;
}

static void fixture_init(Fixture *fixture, const char *source_path) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->source = read_file(source_path, &fixture->source_length);
    id_from_text("package:anonymous", &fixture->semantic_source.package_id);
    id_from_text("module:anonymous-main", &fixture->semantic_source.module_id);
    id_from_text("file:src/main.kofun", &fixture->file_id);
    fixture->semantic_source.file_id = fixture->file_id;
    fixture->semantic_source.logical_path = text("src/main.kofun");
    fixture->semantic_source.source_bytes = fixture->source_length;
    kofun_sha256(
        fixture->source,
        fixture->source_length,
        fixture->semantic_source.source_sha256
    );
    fixture->semantic_source.edition = text("2026");
    fixture->semantic_source.semantic_compatibility = text("stage2-v1");
    fixture->semantic_source.caller_generation = UINT64_C(42);
}

static void fixture_destroy(Fixture *fixture) {
    free(fixture->source);
    memset(fixture, 0, sizeof(*fixture));
}

static KofunSemanticSpan token_span(
    const Fixture *fixture,
    const char *needle,
    size_t occurrence
) {
    const char *cursor = (const char *)fixture->source;
    const char *found = NULL;
    size_t index;
    for (index = 0u; index <= occurrence; index += 1u) {
        found = strstr(cursor, needle);
        CHECK(found != NULL);
        cursor = found + strlen(needle);
    }
    {
        KofunSemanticSpan span;
        span.start = (uint32_t)(found - (const char *)fixture->source);
        span.end = span.start + (uint32_t)strlen(needle);
        return span;
    }
}

static KofunSemanticId node_id(
    const Fixture *fixture,
    KofunSemanticNodeKind kind,
    KofunSemanticSpan span,
    uint32_t occurrence
) {
    KofunSemanticId result;
    kofun_semantic_derive_id(
        "kofun.sidecar.node/v1",
        &fixture->file_id,
        kind,
        span,
        occurrence,
        &result
    );
    return result;
}

static void test_node_identity_frame(const Fixture *fixture) {
    static const uint8_t expected[KOFUN_SEMANTIC_ID_BYTES] = {
        0xb9u, 0x21u, 0xedu, 0xc4u, 0x45u, 0xd9u, 0x96u, 0xb7u,
        0x10u, 0x5bu, 0x75u, 0xd1u, 0xb5u, 0xabu, 0x62u, 0xbeu,
        0x36u, 0x4du, 0xc8u, 0x32u, 0x5cu, 0xa0u, 0xedu, 0xb2u,
        0x19u, 0xdcu, 0xf6u, 0x6fu, 0xf2u, 0xe5u, 0xe4u, 0x71u
    };
    KofunSemanticSpan span = {
        0u, (uint32_t)fixture->source_length
    };
    KofunSemanticId actual = node_id(
        fixture,
        KOFUN_SEMANTIC_NODE_MODULE,
        span,
        0u
    );
    CHECK(memcmp(actual.bytes, expected, sizeof(expected)) == 0);
}

static bool emit_node(
    KofunSemanticSink *sink,
    KofunSemanticId id,
    KofunSemanticNodeKind kind,
    KofunSemanticSpan span,
    KofunSemanticStatus status,
    const KofunSemanticId *dependencies,
    uint16_t dependency_count,
    const KofunSemanticId *diagnostics,
    uint16_t diagnostic_count
) {
    KofunSemanticNode node;
    memset(&node, 0, sizeof(node));
    node.node_id = id;
    node.kind = kind;
    node.span = span;
    node.status = status;
    node.dependencies = dependencies;
    node.dependency_count = dependency_count;
    node.diagnostic_ids = diagnostics;
    node.diagnostic_count = diagnostic_count;
    return kofun_semantic_node(sink, &node);
}

static bool emit_identity(
    KofunSemanticSink *sink,
    KofunSemanticId owner,
    KofunSemanticIdentityKind kind,
    const char *label,
    KofunSemanticId *value
) {
    KofunSemanticIdentity identity;
    memset(&identity, 0, sizeof(identity));
    identity.owner_node_id = owner;
    identity.kind = kind;
    id_from_text(label, &identity.value);
    identity.status = KOFUN_SEMANTIC_VALIDATED;
    if (value != NULL) *value = identity.value;
    return kofun_semantic_identity(sink, &identity);
}

static bool emit_fact(
    KofunSemanticSink *sink,
    KofunSemanticId owner,
    KofunSemanticFactKind kind,
    KofunSemanticStatus status,
    const char *display,
    const char *reason,
    const KofunSemanticId *diagnostics,
    uint16_t diagnostic_count
) {
    KofunSemanticFact fact;
    memset(&fact, 0, sizeof(fact));
    fact.owner_node_id = owner;
    fact.kind = kind;
    fact.status = status;
    fact.display = text(display);
    fact.reason = text(reason);
    fact.diagnostic_ids = diagnostics;
    fact.diagnostic_count = diagnostic_count;
    return kofun_semantic_fact(sink, &fact);
}

static bool emit_clean_records(
    KofunSemanticSink *sink,
    const Fixture *fixture
) {
    KofunSemanticSpan spans[12];
    KofunSemanticId nodes[12];
    KofunSemanticId later_symbol;
    KofunSemanticId constructor_symbol;
    KofunSemanticReference reference;
    size_t index;
    spans[0].start = 0u;
    spans[0].end = (uint32_t)fixture->source_length;
    spans[1] = token_span(fixture, "MaybeInt", 0u);
    spans[2] = token_span(fixture, "Present", 0u);
    spans[3] = token_span(fixture, "later", 0u);
    spans[4] = token_span(fixture, "value", 0u);
    spans[5] = token_span(fixture, "copied", 0u);
    spans[6] = token_span(fixture, "main", 0u);
    spans[7] = token_span(fixture, "current", 0u);
    spans[8] = token_span(fixture, "later", 1u);
    spans[9] = token_span(fixture, "Present", 1u);
    spans[10] = token_span(fixture, "if", 0u);
    spans[11] = token_span(fixture, "match", 0u);
    nodes[0] = node_id(fixture, KOFUN_SEMANTIC_NODE_MODULE, spans[0], 0u);
    nodes[1] = node_id(fixture, KOFUN_SEMANTIC_NODE_ADT, spans[1], 0u);
    nodes[2] = node_id(
        fixture,
        KOFUN_SEMANTIC_NODE_CONSTRUCTOR,
        spans[2],
        0u
    );
    nodes[3] = node_id(
        fixture,
        KOFUN_SEMANTIC_NODE_FUNCTION,
        spans[3],
        0u
    );
    nodes[4] = node_id(
        fixture,
        KOFUN_SEMANTIC_NODE_PARAMETER,
        spans[4],
        0u
    );
    nodes[5] = node_id(fixture, KOFUN_SEMANTIC_NODE_LOCAL, spans[5], 0u);
    nodes[6] = node_id(
        fixture,
        KOFUN_SEMANTIC_NODE_FUNCTION,
        spans[6],
        1u
    );
    nodes[7] = node_id(fixture, KOFUN_SEMANTIC_NODE_LOCAL, spans[7], 1u);
    nodes[8] = node_id(fixture, KOFUN_SEMANTIC_NODE_CALL, spans[8], 0u);
    nodes[9] = node_id(fixture, KOFUN_SEMANTIC_NODE_CALL, spans[9], 1u);
    nodes[10] = node_id(fixture, KOFUN_SEMANTIC_NODE_IF, spans[10], 0u);
    nodes[11] = node_id(fixture, KOFUN_SEMANTIC_NODE_MATCH, spans[11], 0u);

    if (!kofun_semantic_begin(sink, &fixture->semantic_source)) return false;
    for (index = 0u; index < 12u; index += 1u) {
        KofunSemanticNodeKind kind = index == 0u ?
            KOFUN_SEMANTIC_NODE_MODULE :
            (index == 1u ? KOFUN_SEMANTIC_NODE_ADT :
             (index == 2u ? KOFUN_SEMANTIC_NODE_CONSTRUCTOR :
              (index == 3u || index == 6u ?
               KOFUN_SEMANTIC_NODE_FUNCTION :
               (index == 4u ? KOFUN_SEMANTIC_NODE_PARAMETER :
                (index == 5u || index == 7u ?
                 KOFUN_SEMANTIC_NODE_LOCAL :
                 (index == 8u || index == 9u ?
                  KOFUN_SEMANTIC_NODE_CALL :
                  (index == 10u ?
                   KOFUN_SEMANTIC_NODE_IF :
                   KOFUN_SEMANTIC_NODE_MATCH)))))));
        if (!emit_node(
                sink,
                nodes[index],
                kind,
                spans[index],
                KOFUN_SEMANTIC_VALIDATED,
                NULL,
                0u,
                NULL,
                0u)) {
            return false;
        }
    }
    if (!emit_identity(
            sink,
            nodes[1],
            KOFUN_SEMANTIC_ID_TYPE,
            "type:MaybeInt",
            NULL) ||
        !emit_identity(
            sink,
            nodes[2],
            KOFUN_SEMANTIC_ID_CONSTRUCTOR,
            "constructor:MaybeInt.Present",
            &constructor_symbol) ||
        !emit_identity(
            sink,
            nodes[3],
            KOFUN_SEMANTIC_ID_SYMBOL,
            "function:later",
            &later_symbol) ||
        !emit_identity(
            sink,
            nodes[4],
            KOFUN_SEMANTIC_ID_BINDING,
            "binding:later.value",
            NULL) ||
        !emit_identity(
            sink,
            nodes[5],
            KOFUN_SEMANTIC_ID_BINDING,
            "binding:later.copied",
            NULL) ||
        !emit_identity(
            sink,
            nodes[6],
            KOFUN_SEMANTIC_ID_SYMBOL,
            "function:main",
            NULL) ||
        !emit_identity(
            sink,
            nodes[7],
            KOFUN_SEMANTIC_ID_BINDING,
            "binding:main.current",
            NULL)) {
        return false;
    }
    memset(&reference, 0, sizeof(reference));
    id_from_text("reference:main.later", &reference.reference_id);
    reference.source_node_id = nodes[8];
    reference.name_space = KOFUN_SEMANTIC_NAMESPACE_VALUE;
    reference.span = spans[8];
    reference.status = KOFUN_SEMANTIC_VALIDATED;
    reference.target_shape = KOFUN_SEMANTIC_TARGET_VISIBLE;
    reference.target_kind = KOFUN_SEMANTIC_ID_SYMBOL;
    reference.target_value = later_symbol;
    if (!kofun_semantic_reference(sink, &reference)) return false;
    id_from_text("reference:main.Present", &reference.reference_id);
    reference.source_node_id = nodes[9];
    reference.span = spans[9];
    reference.name_space = KOFUN_SEMANTIC_NAMESPACE_CONSTRUCTOR;
    reference.target_kind = KOFUN_SEMANTIC_ID_CONSTRUCTOR;
    reference.target_value = constructor_symbol;
    if (!kofun_semantic_reference(sink, &reference)) return false;

    if (!emit_fact(
            sink, nodes[4], KOFUN_SEMANTIC_FACT_TYPE,
            KOFUN_SEMANTIC_VALIDATED, "Int", "", NULL, 0u) ||
        !emit_fact(
            sink, nodes[5], KOFUN_SEMANTIC_FACT_TYPE,
            KOFUN_SEMANTIC_VALIDATED, "Int", "", NULL, 0u) ||
        !emit_fact(
            sink, nodes[7], KOFUN_SEMANTIC_FACT_TYPE,
            KOFUN_SEMANTIC_VALIDATED, "Int", "", NULL, 0u) ||
        !emit_fact(
            sink, nodes[8], KOFUN_SEMANTIC_FACT_TYPE,
            KOFUN_SEMANTIC_VALIDATED, "Int", "", NULL, 0u) ||
        !emit_fact(
            sink, nodes[9], KOFUN_SEMANTIC_FACT_TYPE,
            KOFUN_SEMANTIC_VALIDATED, "MaybeInt", "", NULL, 0u) ||
        !emit_fact(
            sink, nodes[5], KOFUN_SEMANTIC_FACT_OWNERSHIP,
            KOFUN_SEMANTIC_VALIDATED, "copy", "", NULL, 0u) ||
        !emit_fact(
            sink, nodes[7], KOFUN_SEMANTIC_FACT_OWNERSHIP,
            KOFUN_SEMANTIC_VALIDATED, "mutable-local", "", NULL, 0u)) {
        return false;
    }
    return kofun_semantic_end(
        sink,
        KOFUN_SOURCE_CHECKED,
        KOFUN_SEMANTIC_COMPLETE
    );
}

static bool emit_partial_records(
    KofunSemanticSink *sink,
    const Fixture *fixture
) {
    KofunSemanticSpan module_span = {
        0u, (uint32_t)fixture->source_length
    };
    KofunSemanticSpan function_span = token_span(fixture, "later", 0u);
    KofunSemanticSpan local_span = token_span(fixture, "copied", 0u);
    KofunSemanticSpan call_span = token_span(fixture, "later", 1u);
    KofunSemanticSpan recovery_span = token_span(fixture, "match", 0u);
    KofunSemanticId module = node_id(
        fixture, KOFUN_SEMANTIC_NODE_MODULE, module_span, 0u
    );
    KofunSemanticId function = node_id(
        fixture, KOFUN_SEMANTIC_NODE_FUNCTION, function_span, 0u
    );
    KofunSemanticId local = node_id(
        fixture, KOFUN_SEMANTIC_NODE_LOCAL, local_span, 0u
    );
    KofunSemanticId call = node_id(
        fixture, KOFUN_SEMANTIC_NODE_CALL, call_span, 0u
    );
    KofunSemanticId recovery = node_id(
        fixture, KOFUN_SEMANTIC_NODE_ERROR_PATTERN, recovery_span, 0u
    );
    KofunSemanticId diagnostic_id;
    KofunSemanticReference reference;
    KofunSemanticDiagnostic diagnostic;
    uint32_t remedy = 7u;
    id_from_text("diagnostic:E2S16:unknown-call", &diagnostic_id);
    if (!kofun_semantic_begin(sink, &fixture->semantic_source) ||
        !emit_node(
            sink, module, KOFUN_SEMANTIC_NODE_MODULE, module_span,
            KOFUN_SEMANTIC_VALIDATED, NULL, 0u, NULL, 0u) ||
        !emit_node(
            sink, function, KOFUN_SEMANTIC_NODE_FUNCTION, function_span,
            KOFUN_SEMANTIC_VALIDATED, NULL, 0u, NULL, 0u) ||
        !emit_node(
            sink, local, KOFUN_SEMANTIC_NODE_LOCAL, local_span,
            KOFUN_SEMANTIC_VALIDATED, NULL, 0u, NULL, 0u) ||
        !emit_node(
            sink, call, KOFUN_SEMANTIC_NODE_CALL, call_span,
            KOFUN_SEMANTIC_ERROR, NULL, 0u, &diagnostic_id, 1u) ||
        !emit_node(
            sink, recovery, KOFUN_SEMANTIC_NODE_ERROR_PATTERN, recovery_span,
            KOFUN_SEMANTIC_ERROR, NULL, 0u, &diagnostic_id, 1u) ||
        !emit_identity(
            sink, function, KOFUN_SEMANTIC_ID_SYMBOL,
            "function:later", NULL) ||
        !emit_identity(
            sink, local, KOFUN_SEMANTIC_ID_BINDING,
            "binding:later.copied", NULL)) {
        return false;
    }
    memset(&reference, 0, sizeof(reference));
    id_from_text("reference:unknown-call", &reference.reference_id);
    reference.source_node_id = call;
    reference.name_space = KOFUN_SEMANTIC_NAMESPACE_VALUE;
    reference.span = call_span;
    reference.status = KOFUN_SEMANTIC_ERROR;
    reference.target_shape = KOFUN_SEMANTIC_TARGET_UNAVAILABLE;
    reference.target_kind = KOFUN_SEMANTIC_ID_SYMBOL;
    reference.hidden_reason = text("unresolved-current-core-call");
    reference.diagnostic_ids = &diagnostic_id;
    reference.diagnostic_count = 1u;
    if (!kofun_semantic_reference(sink, &reference) ||
        !emit_fact(
            sink, function, KOFUN_SEMANTIC_FACT_TYPE,
            KOFUN_SEMANTIC_VALIDATED, "Int -> Int", "", NULL, 0u) ||
        !emit_fact(
            sink, local, KOFUN_SEMANTIC_FACT_OWNERSHIP,
            KOFUN_SEMANTIC_ERROR, "", "move-after-borrow",
            &diagnostic_id, 1u)) {
        return false;
    }
    memset(&diagnostic, 0, sizeof(diagnostic));
    diagnostic.diagnostic_id = diagnostic_id;
    diagnostic.code = text("E2S16");
    diagnostic.category = text("name-resolution");
    diagnostic.severity = KOFUN_SEMANTIC_DIAGNOSTIC_ERROR;
    diagnostic.template_id = text("unknown-function");
    diagnostic.primary_file_id = fixture->file_id;
    diagnostic.primary_span = call_span;
    diagnostic.fallback_text = text("unknown function");
    diagnostic.affected_ids = &call;
    diagnostic.affected_count = 1u;
    diagnostic.remedy_ids = &remedy;
    diagnostic.remedy_count = 1u;
    if (!kofun_semantic_diagnostic(sink, &diagnostic)) return false;
    return kofun_semantic_end(
        sink,
        KOFUN_SOURCE_FAILED,
        KOFUN_SEMANTIC_PARTIAL
    );
}

typedef struct {
    int reject;
    unsigned calls[7];
} RejectingSink;

static bool reject_call(RejectingSink *sink, int kind) {
    sink->calls[kind - 1] += 1u;
    return sink->reject != kind;
}

static bool reject_begin(void *context, const KofunSemanticSource *source) {
    (void)source;
    return reject_call((RejectingSink *)context, 1);
}

static bool reject_node(void *context, const KofunSemanticNode *node) {
    (void)node;
    return reject_call((RejectingSink *)context, 2);
}

static bool reject_identity(
    void *context,
    const KofunSemanticIdentity *identity
) {
    (void)identity;
    return reject_call((RejectingSink *)context, 3);
}

static bool reject_fact(void *context, const KofunSemanticFact *fact) {
    (void)fact;
    return reject_call((RejectingSink *)context, 4);
}

static bool reject_reference(
    void *context,
    const KofunSemanticReference *reference
) {
    (void)reference;
    return reject_call((RejectingSink *)context, 5);
}

static bool reject_diagnostic(
    void *context,
    const KofunSemanticDiagnostic *diagnostic
) {
    (void)diagnostic;
    return reject_call((RejectingSink *)context, 6);
}

static bool reject_end(
    void *context,
    KofunSourceStatus status,
    KofunCompleteness completeness
) {
    (void)status;
    (void)completeness;
    return reject_call((RejectingSink *)context, 7);
}

static KofunSemanticSink rejecting_sink(RejectingSink *context) {
    KofunSemanticSink sink;
    sink.context = context;
    sink.begin = reject_begin;
    sink.node = reject_node;
    sink.identity = reject_identity;
    sink.fact = reject_fact;
    sink.reference = reject_reference;
    sink.diagnostic = reject_diagnostic;
    sink.cancellation_observed = NULL;
    sink.end = reject_end;
    return sink;
}

static void test_sink_rejection(const Fixture *fixture) {
    int reject;
    KofunSemanticNode node;
    KofunSemanticIdentity identity;
    KofunSemanticFact fact;
    KofunSemanticReference reference;
    KofunSemanticDiagnostic diagnostic;
    memset(&node, 0, sizeof(node));
    memset(&identity, 0, sizeof(identity));
    memset(&fact, 0, sizeof(fact));
    memset(&reference, 0, sizeof(reference));
    memset(&diagnostic, 0, sizeof(diagnostic));
    for (reject = 1; reject <= 7; reject += 1) {
        RejectingSink context;
        KofunSemanticSink sink;
        bool accepted;
        memset(&context, 0, sizeof(context));
        context.reject = reject;
        sink = rejecting_sink(&context);
        switch (reject) {
            case 1:
                accepted = kofun_semantic_begin(
                    &sink, &fixture->semantic_source
                );
                break;
            case 2:
                accepted = kofun_semantic_node(&sink, &node);
                break;
            case 3:
                accepted = kofun_semantic_identity(&sink, &identity);
                break;
            case 4:
                accepted = kofun_semantic_fact(&sink, &fact);
                break;
            case 5:
                accepted = kofun_semantic_reference(&sink, &reference);
                break;
            case 6:
                accepted = kofun_semantic_diagnostic(&sink, &diagnostic);
                break;
            default:
                accepted = kofun_semantic_end(
                    &sink,
                    KOFUN_SOURCE_CHECKED,
                    KOFUN_SEMANTIC_COMPLETE
                );
                break;
        }
        CHECK(!accepted);
        CHECK(context.calls[reject - 1] == 1u);
    }
}

static void test_early_and_cancellation(const Fixture *fixture) {
    KofunSemanticStream *stream = kofun_semantic_stream_create();
    KofunSemanticSink sink;
    KofunSemanticSpan span = {0u, 0u};
    KofunSemanticId id = node_id(
        fixture, KOFUN_SEMANTIC_NODE_MODULE, span, 0u
    );
    const uint8_t *bytes;
    size_t length;
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    CHECK(!emit_node(
        &sink, id, KOFUN_SEMANTIC_NODE_MODULE, span,
        KOFUN_SEMANTIC_VALIDATED, NULL, 0u, NULL, 0u
    ));
    CHECK(!kofun_semantic_stream_bytes(stream, &bytes, &length));
    CHECK(strcmp(kofun_semantic_stream_error(stream)->code, "ETS03") == 0);
    kofun_semantic_stream_destroy(stream);

    stream = kofun_semantic_stream_create();
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    kofun_semantic_stream_observe_cancellation(stream);
    CHECK(!kofun_semantic_begin(&sink, &fixture->semantic_source));
    CHECK(!kofun_semantic_stream_bytes(stream, &bytes, &length));
    kofun_semantic_stream_destroy(stream);

    stream = kofun_semantic_stream_create();
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    CHECK(kofun_semantic_begin(&sink, &fixture->semantic_source));
    CHECK(emit_node(
        &sink, id, KOFUN_SEMANTIC_NODE_MODULE, span,
        KOFUN_SEMANTIC_VALIDATED, NULL, 0u, NULL, 0u
    ));
    kofun_semantic_stream_observe_cancellation(stream);
    CHECK(kofun_semantic_end(
        &sink, KOFUN_SOURCE_CANCELLED, KOFUN_SEMANTIC_PARTIAL
    ));
    CHECK(kofun_semantic_stream_bytes(stream, &bytes, &length));
    CHECK(kofun_semantic_validate_stream(bytes, length, NULL));
    kofun_semantic_stream_destroy(stream);
}

static void test_relation_limits(const Fixture *fixture) {
    KofunSemanticStream *stream = kofun_semantic_stream_create();
    KofunSemanticSink sink;
    KofunSemanticId dependencies[KOFUN_SEMANTIC_MAX_RELATIONS + 1u];
    KofunSemanticId owner;
    KofunSemanticSpan span = {0u, 0u};
    size_t index;
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    CHECK(kofun_semantic_begin(&sink, &fixture->semantic_source));
    for (index = 0u; index < KOFUN_SEMANTIC_MAX_RELATIONS; index += 1u) {
        char label[32];
        (void)snprintf(label, sizeof(label), "dependency:%zu", index);
        id_from_text(label, &dependencies[index]);
    }
    qsort(
        dependencies,
        KOFUN_SEMANTIC_MAX_RELATIONS,
        sizeof(dependencies[0]),
        compare_ids
    );
    for (index = 0u; index < KOFUN_SEMANTIC_MAX_RELATIONS; index += 1u) {
        CHECK(emit_node(
            &sink,
            dependencies[index],
            KOFUN_SEMANTIC_NODE_LOCAL,
            span,
            KOFUN_SEMANTIC_VALIDATED,
            NULL,
            0u,
            NULL,
            0u
        ));
    }
    id_from_text("dependency-owner", &owner);
    CHECK(emit_node(
        &sink,
        owner,
        KOFUN_SEMANTIC_NODE_FUNCTION,
        span,
        KOFUN_SEMANTIC_VALIDATED,
        dependencies,
        KOFUN_SEMANTIC_MAX_RELATIONS,
        NULL,
        0u
    ));
    CHECK(kofun_semantic_end(
        &sink, KOFUN_SOURCE_CHECKED, KOFUN_SEMANTIC_COMPLETE
    ));
    kofun_semantic_stream_destroy(stream);

    stream = kofun_semantic_stream_create();
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    CHECK(kofun_semantic_begin(&sink, &fixture->semantic_source));
    for (index = 0u; index < KOFUN_SEMANTIC_MAX_RELATIONS + 1u; index += 1u) {
        char label[32];
        (void)snprintf(label, sizeof(label), "over-dependency:%zu", index);
        id_from_text(label, &dependencies[index]);
    }
    qsort(
        dependencies,
        KOFUN_SEMANTIC_MAX_RELATIONS + 1u,
        sizeof(dependencies[0]),
        compare_ids
    );
    id_from_text("over-dependency-owner", &owner);
    CHECK(!emit_node(
        &sink,
        owner,
        KOFUN_SEMANTIC_NODE_FUNCTION,
        span,
        KOFUN_SEMANTIC_VALIDATED,
        dependencies,
        KOFUN_SEMANTIC_MAX_RELATIONS + 1u,
        NULL,
        0u
    ));
    CHECK(strcmp(kofun_semantic_stream_error(stream)->code, "ETS04") == 0);
    kofun_semantic_stream_destroy(stream);
}

static void test_text_limits(const Fixture *fixture) {
    static const char *invalid_paths[] = {
        "src//main.kofun",
        "src/./main.kofun",
        "src/../main.kofun",
        "src/main.kofun/",
        "src\\main.kofun",
        "file://main.kofun"
    };
    KofunSemanticStream *stream;
    KofunSemanticSink sink;
    KofunSemanticSource source = fixture->semantic_source;
    uint8_t *path = (uint8_t *)malloc(KOFUN_SEMANTIC_MAX_TEXT_BYTES + 1u);
    CHECK(path != NULL);
    memset(path, 'a', KOFUN_SEMANTIC_MAX_TEXT_BYTES + 1u);
    source.logical_path.bytes = path;
    source.logical_path.length = KOFUN_SEMANTIC_MAX_TEXT_BYTES;
    stream = kofun_semantic_stream_create();
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    CHECK(kofun_semantic_begin(&sink, &source));
    CHECK(kofun_semantic_end(
        &sink, KOFUN_SOURCE_CHECKED, KOFUN_SEMANTIC_COMPLETE
    ));
    kofun_semantic_stream_destroy(stream);

    source.logical_path.length = KOFUN_SEMANTIC_MAX_TEXT_BYTES + 1u;
    stream = kofun_semantic_stream_create();
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    CHECK(!kofun_semantic_begin(&sink, &source));
    kofun_semantic_stream_destroy(stream);
    {
        size_t index;
        for (index = 0u;
             index < sizeof(invalid_paths) / sizeof(invalid_paths[0]);
             index += 1u) {
            source.logical_path = text(invalid_paths[index]);
            stream = kofun_semantic_stream_create();
            CHECK(stream != NULL);
            sink = kofun_semantic_stream_sink(stream);
            CHECK(!kofun_semantic_begin(&sink, &source));
            CHECK(strcmp(
                kofun_semantic_stream_error(stream)->code,
                "ETS03"
            ) == 0);
            kofun_semantic_stream_destroy(stream);
        }
    }
    free(path);
}

static void test_event_count_limits(const Fixture *fixture) {
    KofunSemanticStream *stream = kofun_semantic_stream_create();
    KofunSemanticSink sink;
    KofunSemanticSpan span = {0u, 0u};
    uint32_t index;
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    CHECK(kofun_semantic_begin(&sink, &fixture->semantic_source));
    for (index = 0u; index < KOFUN_SEMANTIC_MAX_EVENTS - 2u; index += 1u) {
        char label[40];
        KofunSemanticId id;
        (void)snprintf(label, sizeof(label), "limit-node:%u", index);
        id_from_text(label, &id);
        CHECK(emit_node(
            &sink, id, KOFUN_SEMANTIC_NODE_LOCAL, span,
            KOFUN_SEMANTIC_VALIDATED, NULL, 0u, NULL, 0u
        ));
    }
    CHECK(kofun_semantic_end(
        &sink, KOFUN_SOURCE_CHECKED, KOFUN_SEMANTIC_COMPLETE
    ));
    kofun_semantic_stream_destroy(stream);

    stream = kofun_semantic_stream_create();
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    CHECK(kofun_semantic_begin(&sink, &fixture->semantic_source));
    for (index = 0u; index < KOFUN_SEMANTIC_MAX_EVENTS - 2u; index += 1u) {
        char label[40];
        KofunSemanticId id;
        (void)snprintf(label, sizeof(label), "over-node:%u", index);
        id_from_text(label, &id);
        CHECK(emit_node(
            &sink, id, KOFUN_SEMANTIC_NODE_LOCAL, span,
            KOFUN_SEMANTIC_VALIDATED, NULL, 0u, NULL, 0u
        ));
    }
    {
        KofunSemanticId over;
        id_from_text("over-node:final", &over);
        CHECK(!emit_node(
            &sink, over, KOFUN_SEMANTIC_NODE_LOCAL, span,
            KOFUN_SEMANTIC_VALIDATED, NULL, 0u, NULL, 0u
        ));
        CHECK(strcmp(kofun_semantic_stream_error(stream)->code, "ETS04") == 0);
    }
    kofun_semantic_stream_destroy(stream);
}

static uint8_t *copy_stream(
    KofunSemanticStream *stream,
    size_t *length
) {
    const uint8_t *bytes;
    uint8_t *copy;
    CHECK(kofun_semantic_stream_bytes(stream, &bytes, length));
    copy = (uint8_t *)malloc(*length);
    CHECK(copy != NULL);
    memcpy(copy, bytes, *length);
    return copy;
}

static bool contains_bytes(
    const uint8_t *haystack,
    size_t haystack_length,
    const char *needle
) {
    size_t needle_length = strlen(needle);
    size_t index;
    if (needle_length > haystack_length) return false;
    for (index = 0u; index + needle_length <= haystack_length; index += 1u) {
        if (memcmp(haystack + index, needle, needle_length) == 0) return true;
    }
    return false;
}

static KofunSemanticStream *make_clean_stream(const Fixture *fixture) {
    KofunSemanticStream *stream = kofun_semantic_stream_create();
    KofunSemanticSink sink;
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    CHECK(emit_clean_records(&sink, fixture));
    return stream;
}

static KofunSemanticStream *make_partial_stream(const Fixture *fixture) {
    KofunSemanticStream *stream = kofun_semantic_stream_create();
    KofunSemanticSink sink;
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    CHECK(emit_partial_records(&sink, fixture));
    return stream;
}

static void test_complete_partial(
    const Fixture *fixture,
    const char *work
) {
    KofunSemanticStream *first = make_clean_stream(fixture);
    KofunSemanticStream *second = make_clean_stream(fixture);
    KofunSemanticStream *partial = make_partial_stream(fixture);
    uint8_t *first_bytes;
    uint8_t *second_bytes;
    uint8_t *partial_bytes;
    size_t first_length;
    size_t second_length;
    size_t partial_length;
    char destination[1024];
    first_bytes = copy_stream(first, &first_length);
    second_bytes = copy_stream(second, &second_length);
    partial_bytes = copy_stream(partial, &partial_length);
    CHECK(first_length == second_length);
    CHECK(memcmp(first_bytes, second_bytes, first_length) == 0);
    CHECK(kofun_semantic_validate_stream(
        first_bytes, first_length, NULL
    ));
    CHECK(kofun_semantic_validate_stream(
        partial_bytes, partial_length, NULL
    ));
    CHECK(!contains_bytes(
        partial_bytes, partial_length, "PrivateSecret"
    ));
    (void)snprintf(destination, sizeof(destination), "%s/complete.kse", work);
    CHECK(kofun_semantic_stream_commit(first, destination));
    (void)snprintf(destination, sizeof(destination), "%s/partial.kse", work);
    CHECK(kofun_semantic_stream_commit(partial, destination));
    free(first_bytes);
    free(second_bytes);
    free(partial_bytes);
    kofun_semantic_stream_destroy(first);
    kofun_semantic_stream_destroy(second);
    kofun_semantic_stream_destroy(partial);
}

static uint32_t test_load_u32be(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24u) |
        ((uint32_t)bytes[1] << 16u) |
        ((uint32_t)bytes[2] << 8u) |
        bytes[3];
}

static void test_store_u32be(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24u);
    bytes[1] = (uint8_t)(value >> 16u);
    bytes[2] = (uint8_t)(value >> 8u);
    bytes[3] = (uint8_t)value;
}

static void resign(uint8_t *bytes, size_t length) {
    uint32_t payload = test_load_u32be(bytes + 12u);
    uint8_t digest[32];
    CHECK((uint64_t)payload + 48u == length);
    kofun_sha256(bytes, 16u + payload, digest);
    memcpy(bytes + 16u + payload, digest, sizeof(digest));
}

static uint8_t *fresh_copy(
    const uint8_t *bytes,
    size_t length
) {
    uint8_t *copy = (uint8_t *)malloc(length);
    CHECK(copy != NULL);
    memcpy(copy, bytes, length);
    return copy;
}

static size_t find_bytes(
    const uint8_t *bytes,
    size_t length,
    const char *wanted
) {
    size_t wanted_length = strlen(wanted);
    size_t index;
    for (index = 0u; index + wanted_length <= length; index += 1u) {
        if (memcmp(bytes + index, wanted, wanted_length) == 0) return index;
    }
    CHECK(false);
    return 0u;
}

static void expect_invalid(
    uint8_t *bytes,
    size_t length,
    const char *code
) {
    KofunSemanticError error;
    CHECK(!kofun_semantic_validate_stream(bytes, length, &error));
    CHECK(strcmp(error.code, code) == 0);
    free(bytes);
}

static void test_corruption(const Fixture *fixture) {
    KofunSemanticStream *stream = make_clean_stream(fixture);
    uint8_t *canonical;
    uint8_t *copy;
    size_t length;
    size_t path_at;
    canonical = copy_stream(stream, &length);
    CHECK(kofun_semantic_validate_stream(canonical, length, NULL));

    copy = fresh_copy(canonical, length);
    copy[20] ^= 1u;
    expect_invalid(copy, length, "ETS03");

    copy = fresh_copy(canonical, length);
    copy[17] = 1u;
    resign(copy, length);
    expect_invalid(copy, length, "ETS03");

    copy = fresh_copy(canonical, length);
    copy[16] = 99u;
    resign(copy, length);
    expect_invalid(copy, length, "ETS03");

    copy = fresh_copy(canonical, length);
    copy[24] = 99u;
    resign(copy, length);
    expect_invalid(copy, length, "ETS03");

    copy = fresh_copy(canonical, length);
    copy[25] = 99u;
    resign(copy, length);
    expect_invalid(copy, length, "ETS03");

    copy = fresh_copy(canonical, length);
    copy[26] = 1u;
    resign(copy, length);
    expect_invalid(copy, length, "ETS03");

    copy = fresh_copy(canonical, length);
    test_store_u32be(copy + 28u, 31u);
    resign(copy, length);
    expect_invalid(copy, length, "ETS04");

    copy = fresh_copy(canonical, length);
    test_store_u32be(
        copy + 8u,
        test_load_u32be(copy + 8u) + 1u
    );
    resign(copy, length);
    expect_invalid(copy, length, "ETS03");

    path_at = find_bytes(canonical, length, "src/main.kofun");
    copy = fresh_copy(canonical, length);
    copy[path_at] = UINT8_C(0xff);
    resign(copy, length);
    expect_invalid(copy, length, "ETS04");

    copy = fresh_copy(canonical, length);
    expect_invalid(copy, length - 1u, "ETS04");

    free(canonical);
    kofun_semantic_stream_destroy(stream);
}

static void test_hidden_disclosure(const Fixture *fixture) {
    KofunSemanticStream *stream = kofun_semantic_stream_create();
    KofunSemanticSink sink;
    KofunSemanticSpan span = token_span(fixture, "later", 1u);
    KofunSemanticId node = node_id(
        fixture, KOFUN_SEMANTIC_NODE_CALL, span, 0u
    );
    KofunSemanticReference reference;
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    CHECK(kofun_semantic_begin(&sink, &fixture->semantic_source));
    CHECK(emit_node(
        &sink, node, KOFUN_SEMANTIC_NODE_CALL, span,
        KOFUN_SEMANTIC_VALIDATED, NULL, 0u, NULL, 0u
    ));
    memset(&reference, 0, sizeof(reference));
    id_from_text("hidden-reference", &reference.reference_id);
    reference.source_node_id = node;
    reference.name_space = KOFUN_SEMANTIC_NAMESPACE_VALUE;
    reference.span = span;
    reference.status = KOFUN_SEMANTIC_UNAVAILABLE;
    reference.target_shape = KOFUN_SEMANTIC_TARGET_HIDDEN;
    reference.target_kind = KOFUN_SEMANTIC_ID_SYMBOL;
    id_from_text("PrivateSecret", &reference.target_value);
    reference.hidden_reason = text("inaccessible");
    CHECK(!kofun_semantic_reference(&sink, &reference));
    {
        const uint8_t *bytes;
        size_t length;
        CHECK(!kofun_semantic_stream_bytes(stream, &bytes, &length));
    }
    kofun_semantic_stream_destroy(stream);
}

static void test_nfc_rejection(const Fixture *fixture) {
    static const uint8_t decomposed[] = {'e', 0xcc, 0x81};
    KofunSemanticStream *stream = kofun_semantic_stream_create();
    KofunSemanticSink sink;
    KofunSemanticSpan span = {0u, 0u};
    KofunSemanticId node;
    KofunSemanticFact fact;
    id_from_text("nfc-node", &node);
    CHECK(stream != NULL);
    sink = kofun_semantic_stream_sink(stream);
    CHECK(kofun_semantic_begin(&sink, &fixture->semantic_source));
    CHECK(emit_node(
        &sink, node, KOFUN_SEMANTIC_NODE_LOCAL, span,
        KOFUN_SEMANTIC_VALIDATED, NULL, 0u, NULL, 0u
    ));
    memset(&fact, 0, sizeof(fact));
    fact.owner_node_id = node;
    fact.kind = KOFUN_SEMANTIC_FACT_TYPE;
    fact.status = KOFUN_SEMANTIC_VALIDATED;
    fact.display.bytes = decomposed;
    fact.display.length = sizeof(decomposed);
    fact.reason = text("");
    CHECK(!kofun_semantic_fact(&sink, &fact));
    CHECK(strcmp(
        kofun_semantic_stream_error(stream)->code,
        "ETS04"
    ) == 0);
    kofun_semantic_stream_destroy(stream);
}

static void test_independent_sink(const Fixture *fixture) {
    RejectingSink capture;
    KofunSemanticSink sink;
    memset(&capture, 0, sizeof(capture));
    sink = rejecting_sink(&capture);
    CHECK(emit_clean_records(&sink, fixture));
    CHECK(capture.calls[0] == 1u);
    CHECK(capture.calls[1] == 12u);
    CHECK(capture.calls[2] == 7u);
    CHECK(capture.calls[3] == 7u);
    CHECK(capture.calls[4] == 2u);
    CHECK(capture.calls[5] == 0u);
    CHECK(capture.calls[6] == 1u);
}

static void test_atomic_failure_preserves(
    const Fixture *fixture,
    const char *work
) {
    KofunSemanticStream *stream = make_clean_stream(fixture);
    char directory[1024];
    struct stat status;
    (void)snprintf(directory, sizeof(directory), "%s/not-a-file", work);
    if (mkdir(directory, 0700) != 0) CHECK(errno == EEXIST);
    CHECK(!kofun_semantic_stream_commit(stream, directory));
    CHECK(stat(directory, &status) == 0);
    CHECK(S_ISDIR(status.st_mode));
    kofun_semantic_stream_destroy(stream);
}

int main(int argc, char **argv) {
    Fixture fixture;
    CHECK(argc == 4);
    if (mkdir(argv[2], 0700) != 0) CHECK(errno == EEXIST);
    fixture_init(&fixture, argv[3]);
    test_node_identity_frame(&fixture);
    if (strcmp(argv[1], "events") == 0) {
        test_complete_partial(&fixture, argv[2]);
        test_sink_rejection(&fixture);
        test_early_and_cancellation(&fixture);
        test_hidden_disclosure(&fixture);
        test_nfc_rejection(&fixture);
        test_independent_sink(&fixture);
        test_relation_limits(&fixture);
        test_text_limits(&fixture);
        test_event_count_limits(&fixture);
        puts("PASS: Stage 2 semantic complete/partial/cancelled event transactions");
    } else if (strcmp(argv[1], "stream") == 0) {
        test_corruption(&fixture);
        test_atomic_failure_preserves(&fixture, argv[2]);
        puts("PASS: Stage 2 KSE v1 framing, corruption, limits, and atomic commit");
    } else {
        CHECK(false);
    }
    fixture_destroy(&fixture);
    return 0;
}
