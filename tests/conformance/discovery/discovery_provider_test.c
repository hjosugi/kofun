#include "discovery_provider.h"

#include <stdio.h>
#include <string.h>

/*
 * Observation harness for the discovery provider boundary (#637): turning
 * #608's semantic records into contract facts.
 */

static KofunSemanticId id_of(uint8_t digit) {
    KofunSemanticId id;
    memset(id.bytes, digit, KOFUN_SEMANTIC_ID_BYTES);
    return id;
}

static KofunSemanticBytes bytes_of(const char *text) {
    KofunSemanticBytes bytes;
    bytes.bytes = (const uint8_t *)text;
    bytes.length = (uint32_t)strlen(text);
    return bytes;
}

static KofunSemanticSource fixture_source(void) {
    KofunSemanticSource source;
    memset(&source, 0, sizeof(source));
    source.file_id = id_of(0x11);
    memset(source.source_sha256, 0x22, KOFUN_SEMANTIC_ID_BYTES);
    source.semantic_compatibility = bytes_of("stage2-v1");
    source.caller_generation = 17u;
    return source;
}

static const char *const INTERFACE_DIGEST =
    "3333333333333333333333333333333333333333333333333333333333333333";

static KofunDiscoveryRequest request_for(const KofunDiscoveryAnalysisKey *key) {
    KofunDiscoveryRequest request;
    memset(&request, 0, sizeof(request));
    request.analysis = *key;
    request.kind = KOFUN_DISCOVERY_QUERY_TYPE_AND_OPERATIONS;
    return request;
}

static void report_current(const char *label,
                           const KofunDiscoveryRequest *request,
                           const KofunDiscoveryAnalysisKey *live) {
    KofunDiscoveryReason reason;
    const char *name;
    if (kofun_discovery_is_current(request, live, &reason)) {
        printf("%s: current\n", label);
        return;
    }
    name = kofun_discovery_reason_name(reason);
    printf("%s: stale %s\n", label, name == NULL ? "(none)" : name);
}

static KofunSemanticNode node_of(uint8_t digit, KofunSemanticNodeKind kind,
                                 uint32_t start, uint32_t end) {
    KofunSemanticNode node;
    memset(&node, 0, sizeof(node));
    node.node_id = id_of(digit);
    node.kind = kind;
    node.span.start = start;
    node.span.end = end;
    node.status = KOFUN_SEMANTIC_VALIDATED;
    return node;
}

static void report_select(const char *label, const KofunSemanticNode *nodes,
                          size_t count, uint32_t offset, uint32_t start,
                          uint32_t end) {
    KofunDiscoveryRequest request;
    KofunDiscoveryReason reason;
    size_t index = 0;
    memset(&request, 0, sizeof(request));
    request.byte_offset = offset;
    request.expression_start = start;
    request.expression_end = end;

    if (kofun_discovery_select_expression(nodes, count, &request, &index,
                                          &reason)) {
        printf("%s: selected node %02x span %u..%u\n", label,
               nodes[index].node_id.bytes[0], nodes[index].span.start,
               nodes[index].span.end);
        return;
    }
    printf("%s: rejected %s\n", label,
           kofun_discovery_reason_name(reason) == NULL
               ? "(none)"
               : kofun_discovery_reason_name(reason));
}

static void report_type(const char *label,
                        const KofunSemanticIdentity *identities,
                        size_t identity_count, const KofunSemanticFact *facts,
                        size_t fact_count) {
    KofunSemanticId node = id_of(0x77);
    KofunDiscoveryTypeFact type;
    const char *status;
    const char *reason;
    static const char *const statuses[] = {"?", "validated", "provisional",
                                           "error", "unavailable"};

    if (!kofun_discovery_type_from_records(&node, identities, identity_count,
                                           facts, fact_count, &type)) {
        printf("%s: unusable\n", label);
        return;
    }
    status = statuses[(int)type.status];
    switch (type.reason) {
    case KOFUN_DISCOVERY_FACT_REASON_NONE:
        reason = "null";
        break;
    case KOFUN_DISCOVERY_FACT_REASON_INCOMPLETE_ANALYSIS:
        reason = "incomplete-analysis";
        break;
    case KOFUN_DISCOVERY_FACT_REASON_TYPE_NOT_AVAILABLE_IN_CURRENT_SUBSET:
        reason = "type-not-available-in-current-subset";
        break;
    case KOFUN_DISCOVERY_FACT_REASON_UNSUPPORTED_CURRENT_STAGE2_FEATURE:
        reason = "unsupported-current-stage2-feature";
        break;
    case KOFUN_DISCOVERY_FACT_REASON_CANCELLED_BEFORE_ANALYSIS:
        reason = "cancelled-before-analysis";
        break;
    default:
        reason = "(other)";
        break;
    }
    printf("%s: status=%s identity=%s display=%s reason=%s\n", label, status,
           type.identity.kind == KOFUN_DISCOVERY_IDENTITY_TYPE_ID ? "TypeId"
                                                                 : "null",
           type.has_display ? type.display : "(null)", reason);
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "";

    if (strcmp(mode, "analysis-key") == 0) {
        KofunSemanticSource source = fixture_source();
        KofunDiscoveryAnalysisKey key;

        if (!kofun_discovery_analysis_key_from_source(&source,
                                                      INTERFACE_DIGEST, &key)) {
            printf("from-source: refused\n");
            return 1;
        }
        printf("file_id=%s\n", key.file_id);
        printf("source_sha256=%s\n", key.source_sha256);
        printf("interface_set_sha256=%s\n", key.interface_set_sha256);
        printf("semantic_compatibility=%s\n", key.semantic_compatibility);
        printf("generation=%lld\n", (long long)key.generation);

        /* A missing interface digest must fail closed, not match by accident. */
        if (kofun_discovery_analysis_key_from_source(&source, NULL, &key)) {
            printf("absent-interface-digest=%s\n", key.interface_set_sha256);
        }
        /* A malformed digest is refused outright. */
        printf("malformed-interface-digest: %s\n",
               kofun_discovery_analysis_key_from_source(&source, "nope", &key)
                   ? "accepted"
                   : "refused");
        return 0;
    }

    if (strcmp(mode, "staleness") == 0) {
        KofunSemanticSource source = fixture_source();
        KofunDiscoveryAnalysisKey live;
        KofunDiscoveryRequest request;

        kofun_discovery_analysis_key_from_source(&source, INTERFACE_DIGEST,
                                                 &live);
        request = request_for(&live);
        report_current("matching", &request, &live);

        /*
         * Each case below mismatches on one axis, and then the last case
         * mismatches on *all* of them at once. That last one is the point: a
         * stale request usually differs on several axes, and the reported
         * reason must be the most fundamental one, because that is what tells
         * the client whether to re-target, upgrade, or re-analyze.
         */
        request = request_for(&live);
        request.analysis.generation = 16;
        report_current("generation", &request, &live);

        request = request_for(&live);
        memset(request.analysis.interface_set_sha256, '9',
               KOFUN_DISCOVERY_ID_CHARS);
        report_current("interface-set", &request, &live);

        request = request_for(&live);
        memset(request.analysis.source_sha256, '9', KOFUN_DISCOVERY_ID_CHARS);
        report_current("source", &request, &live);

        request = request_for(&live);
        strcpy(request.analysis.semantic_compatibility, "stage2-v0");
        report_current("semantic-compatibility", &request, &live);

        request = request_for(&live);
        memset(request.analysis.file_id, '9', KOFUN_DISCOVERY_ID_CHARS);
        report_current("file", &request, &live);

        request = request_for(&live);
        memset(request.analysis.file_id, '9', KOFUN_DISCOVERY_ID_CHARS);
        memset(request.analysis.source_sha256, '9', KOFUN_DISCOVERY_ID_CHARS);
        memset(request.analysis.interface_set_sha256, '9',
               KOFUN_DISCOVERY_ID_CHARS);
        strcpy(request.analysis.semantic_compatibility, "stage2-v0");
        request.analysis.generation = 1;
        report_current("everything-mismatched", &request, &live);
        return 0;
    }

    if (strcmp(mode, "select") == 0) {
        KofunSemanticNode nodes[4];

        /* A call spanning 10..30 with a narrower reference at 20..25 inside
         * it, plus a function declaration that also contains the offset. */
        nodes[0] = node_of(0xa0, KOFUN_SEMANTIC_NODE_FUNCTION, 0u, 100u);
        nodes[1] = node_of(0xb0, KOFUN_SEMANTIC_NODE_CALL, 10u, 30u);
        nodes[2] = node_of(0xc0, KOFUN_SEMANTIC_NODE_REFERENCE, 20u, 25u);
        nodes[3] = node_of(0xd0, KOFUN_SEMANTIC_NODE_SCOPE, 5u, 40u);

        /* The narrowest *expression* wins; the declaration and scope that also
         * contain the offset are not expressions and must not be chosen. */
        report_select("narrowest", nodes, 4u, 22u, 20u, 25u);

        /* Outside the inner reference, the enclosing call is the answer. */
        report_select("enclosing-call", nodes, 4u, 12u, 10u, 30u);

        /* A span the client supplied that is not the parsed occurrence is
         * refused even though the offset is inside a real expression. */
        report_select("span-mismatch", nodes, 4u, 22u, 21u, 25u);

        /* Offsets outside every expression have no answer. */
        report_select("no-expression", nodes, 4u, 90u, 0u, 100u);

        /* Boundary positions count as inside. */
        report_select("at-span-start", nodes, 4u, 20u, 20u, 25u);
        report_select("at-span-end", nodes, 4u, 25u, 20u, 25u);
        return 0;
    }

    if (strcmp(mode, "type") == 0) {
        KofunSemanticIdentity identities[2];
        KofunSemanticFact facts[2];

        memset(identities, 0, sizeof(identities));
        memset(facts, 0, sizeof(facts));

        identities[0].owner_node_id = id_of(0x77);
        identities[0].kind = KOFUN_SEMANTIC_ID_TYPE;
        identities[0].value = id_of(0xaa);
        identities[0].status = KOFUN_SEMANTIC_VALIDATED;

        facts[0].owner_node_id = id_of(0x77);
        facts[0].kind = KOFUN_SEMANTIC_FACT_TYPE;
        facts[0].status = KOFUN_SEMANTIC_VALIDATED;
        facts[0].display = bytes_of("List[Text]");

        report_type("validated", identities, 1u, facts, 1u);

        /* A validated fact whose identity is missing cannot be disclosed as
         * validated: it is downgraded, not published without identity. */
        report_type("validated-without-identity", NULL, 0u, facts, 1u);

        /* An identity that is itself not validated downgrades too. */
        identities[0].status = KOFUN_SEMANTIC_PROVISIONAL;
        report_type("provisional-identity", identities, 1u, facts, 1u);
        identities[0].status = KOFUN_SEMANTIC_VALIDATED;

        /* No type fact at all: unavailable, with no display and never `Any`. */
        report_type("absent", identities, 1u, NULL, 0u);

        /* A provisional fact keeps its placeholder display and gains a
         * reason. */
        facts[0].status = KOFUN_SEMANTIC_PROVISIONAL;
        facts[0].display = bytes_of("_T1");
        facts[0].reason = bytes_of("");
        report_type("provisional", identities, 1u, facts, 1u);

        /* A producer reason the contract knows is carried across verbatim. */
        facts[0].status = KOFUN_SEMANTIC_UNAVAILABLE;
        facts[0].reason = bytes_of(KOFUN_SEMANTIC_REASON_UNSUPPORTED_STAGE2_FEATURE);
        report_type("unsupported-feature", identities, 1u, facts, 1u);

        /* Facts owned by another node are not borrowed. */
        facts[0].owner_node_id = id_of(0x88);
        facts[0].status = KOFUN_SEMANTIC_VALIDATED;
        facts[0].display = bytes_of("List[Text]");
        report_type("other-node", identities, 1u, facts, 1u);
        return 0;
    }

    fprintf(stderr,
            "usage: discovery-provider-test analysis-key|staleness|select|type\n");
    return 2;
}
