#include "discovery_v1.h"

#include <stdio.h>
#include <string.h>

/*
 * Observation harness for the discovery v1 contract layer (#637).
 *
 * Each mode prints what the module decided, so the golden files record
 * behavior rather than restating the implementation.
 */

static const char *const CANONICAL_REQUEST =
    "{\n"
    "  \"analysis\": {\n"
    "    \"file_id\": "
    "\"1111111111111111111111111111111111111111111111111111111111111111\",\n"
    "    \"generation\": 17,\n"
    "    \"interface_set_sha256\": "
    "\"3333333333333333333333333333333333333333333333333333333333333333\",\n"
    "    \"semantic_compatibility\": \"stage2-v1\",\n"
    "    \"source_sha256\": "
    "\"2222222222222222222222222222222222222222222222222222222222222222\"\n"
    "  },\n"
    "  \"position\": {\n"
    "    \"byte_offset\": 84,\n"
    "    \"expression\": {\n"
    "      \"end\": 84,\n"
    "      \"start\": 75\n"
    "    }\n"
    "  },\n"
    "  \"query\": {\n"
    "    \"include_unavailable\": false,\n"
    "    \"kind\": \"type-and-operations\",\n"
    "    \"spelling\": null\n"
    "  },\n"
    "  \"schema\": \"kofun.discovery.request/v1\"\n"
    "}\n";

/* Replace the first occurrence of `from` with `to`, for mutation cases. */
static int mutate(char *out, size_t capacity, const char *from,
                  const char *to) {
    const char *found = strstr(CANONICAL_REQUEST, from);
    size_t prefix;
    size_t total;
    if (found == NULL) {
        return 0;
    }
    prefix = (size_t)(found - CANONICAL_REQUEST);
    total = prefix + strlen(to) + strlen(found + strlen(from));
    if (total + 1u > capacity) {
        return 0;
    }
    memcpy(out, CANONICAL_REQUEST, prefix);
    memcpy(out + prefix, to, strlen(to));
    strcpy(out + prefix + strlen(to), found + strlen(from));
    return 1;
}

static void report_parse(const char *label, const char *bytes) {
    KofunDiscoveryRequest request;
    KofunDiscoveryReason reason;
    const char *name;
    if (kofun_discovery_request_parse(bytes, strlen(bytes), &request,
                                      &reason)) {
        printf("%s: accepted generation=%lld offset=%u span=%u..%u kind=%d "
               "include_unavailable=%s spelling=%s\n",
               label, (long long)request.analysis.generation,
               request.byte_offset, request.expression_start,
               request.expression_end, (int)request.kind,
               request.include_unavailable ? "true" : "false",
               request.has_spelling ? request.spelling : "(null)");
        return;
    }
    name = kofun_discovery_reason_name(reason);
    printf("%s: rejected %s\n", label, name == NULL ? "(none)" : name);
}

static void report_mutation(const char *label, const char *from,
                            const char *to) {
    char buffer[4096];
    if (!mutate(buffer, sizeof(buffer), from, to)) {
        printf("%s: harness could not build the case\n", label);
        return;
    }
    report_parse(label, buffer);
}

static void report_emit(const char *label, KofunDiscoveryStatus status,
                        KofunDiscoveryReason reason, int with_analysis) {
    KofunDiscoveryAnalysisKey analysis;
    char buffer[4096];
    size_t written;
    memset(&analysis, 0, sizeof(analysis));
    memset(analysis.file_id, '1', KOFUN_DISCOVERY_ID_CHARS);
    memset(analysis.source_sha256, '2', KOFUN_DISCOVERY_ID_CHARS);
    memset(analysis.interface_set_sha256, '3', KOFUN_DISCOVERY_ID_CHARS);
    strcpy(analysis.semantic_compatibility, "stage2-v1");
    analysis.generation = 17;

    written = kofun_discovery_result_emit_factless(
        status, reason, with_analysis ? &analysis : NULL, buffer,
        sizeof(buffer));
    if (written == 0) {
        printf("%s: refused\n", label);
        return;
    }
    printf("%s: %zu bytes\n", label, written);
    fwrite(buffer, 1u, written, stdout);
}

static void report_boundaries(const char *label, const char *source,
                              uint32_t start, uint32_t offset, uint32_t end) {
    KofunDiscoveryRequest request;
    memset(&request, 0, sizeof(request));
    request.expression_start = start;
    request.byte_offset = offset;
    request.expression_end = end;
    printf("%s: %s\n", label,
           kofun_discovery_offsets_are_boundaries(&request, source,
                                                  strlen(source))
               ? "boundaries"
               : "rejected");
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "";

    if (strcmp(mode, "parse") == 0) {
        report_parse("canonical", CANONICAL_REQUEST);

        /* Key order, indentation, and unknown fields are all one check. */
        report_mutation("reordered-keys",
                        "\"byte_offset\": 84,\n    \"expression\"",
                        "\"expression\": 0,\n    \"byte_offset\"");
        report_mutation("unknown-field", "  \"schema\":",
                        "  \"extra\": 1,\n  \"schema\":");
        report_mutation("duplicate-key", "  \"schema\":",
                        "  \"query\": {},\n  \"schema\":");
        report_mutation("reindented", "\n  \"position\"", "\n \"position\"");
        report_mutation("trailing-data", "\"kofun.discovery.request/v1\"\n}\n",
                        "\"kofun.discovery.request/v1\"\n}\n{}\n");

        /* Scalars. */
        report_mutation("float-generation", "\"generation\": 17",
                        "\"generation\": 17.0");
        report_mutation("exponent-generation", "\"generation\": 17",
                        "\"generation\": 1e2");
        report_mutation("signed-generation", "\"generation\": 17",
                        "\"generation\": -17");
        report_mutation("leading-zero", "\"generation\": 17",
                        "\"generation\": 017");
        report_mutation("zero-generation", "\"generation\": 17",
                        "\"generation\": 0");
        report_mutation("u53-overflow", "\"generation\": 17",
                        "\"generation\": 9007199254740992");
        report_mutation("u32-overflow", "\"byte_offset\": 84",
                        "\"byte_offset\": 4294967296");
        report_mutation("uppercase-id",
                        "\"1111111111111111111111111111111111111111111111111111"
                        "111111111111\"",
                        "\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                        "AAAAAAAAAAAA\"");
        report_mutation("short-id",
                        "\"1111111111111111111111111111111111111111111111111111"
                        "111111111111\"",
                        "\"1111\"");
        report_mutation("empty-compatibility",
                        "\"semantic_compatibility\": \"stage2-v1\"",
                        "\"semantic_compatibility\": \"\"");

        /* Position consistency is `invalid-position`, not `invalid-request`. */
        report_mutation("offset-before-span", "\"byte_offset\": 84",
                        "\"byte_offset\": 4");
        report_mutation("inverted-span", "\"end\": 84", "\"end\": 4");

        /* `spelling` is required in exactly one direction per query kind. */
        report_mutation("spelling-without-explain", "\"spelling\": null",
                        "\"spelling\": \"map\"");
        report_mutation("explain-without-spelling",
                        "\"kind\": \"type-and-operations\"",
                        "\"kind\": \"explain-operation\"");
        report_mutation("explain-with-spelling",
                        "\"kind\": \"type-and-operations\",\n    "
                        "\"spelling\": null",
                        "\"kind\": \"explain-operation\",\n    "
                        "\"spelling\": \"map\"");
        report_mutation("empty-spelling",
                        "\"kind\": \"type-and-operations\",\n    "
                        "\"spelling\": null",
                        "\"kind\": \"explain-operation\",\n    "
                        "\"spelling\": \"\"");
        report_mutation("unknown-kind", "\"kind\": \"type-and-operations\"",
                        "\"kind\": \"everything\"");
        report_mutation("unknown-schema", "kofun.discovery.request/v1",
                        "kofun.discovery.request/v2");
        report_mutation("control-character",
                        "\"semantic_compatibility\": \"stage2-v1\"",
                        "\"semantic_compatibility\": \"stage2\tv1\"");
        return 0;
    }

    if (strcmp(mode, "emit") == 0) {
        report_emit("invalid", KOFUN_DISCOVERY_STATUS_INVALID,
                    KOFUN_DISCOVERY_REASON_INVALID_REQUEST, 0);
        report_emit("stale-wrong-file", KOFUN_DISCOVERY_STATUS_STALE,
                    KOFUN_DISCOVERY_REASON_WRONG_FILE, 1);
        report_emit("unavailable", KOFUN_DISCOVERY_STATUS_UNAVAILABLE,
                    KOFUN_DISCOVERY_REASON_UNSUPPORTED_IN_PROFILE, 1);

        /* Shapes the contract forbids must be unrepresentable, not merely
         * undocumented. */
        report_emit("invalid-with-analysis", KOFUN_DISCOVERY_STATUS_INVALID,
                    KOFUN_DISCOVERY_REASON_INVALID_REQUEST, 1);
        report_emit("stale-without-analysis", KOFUN_DISCOVERY_STATUS_STALE,
                    KOFUN_DISCOVERY_REASON_WRONG_FILE, 0);
        report_emit("stale-with-wrong-reason", KOFUN_DISCOVERY_STATUS_STALE,
                    KOFUN_DISCOVERY_REASON_LIMIT_EXHAUSTED, 1);
        report_emit("unavailable-without-reason",
                    KOFUN_DISCOVERY_STATUS_UNAVAILABLE,
                    KOFUN_DISCOVERY_REASON_NONE, 1);
        report_emit("complete-is-not-factless", KOFUN_DISCOVERY_STATUS_COMPLETE,
                    KOFUN_DISCOVERY_REASON_NONE, 1);
        return 0;
    }

    if (strcmp(mode, "boundaries") == 0) {
        /* "aé" — the second code point occupies two bytes. */
        report_boundaries("ascii", "let x = 1", 4u, 4u, 5u);
        report_boundaries("end-of-source", "let x", 0u, 5u, 5u);
        report_boundaries("mid-code-point", "a\xc3\xa9z", 0u, 2u, 3u);
        report_boundaries("after-code-point", "a\xc3\xa9z", 0u, 3u, 4u);
        report_boundaries("past-end", "abc", 0u, 2u, 9u);
        return 0;
    }

    fprintf(stderr, "usage: discovery-test parse|emit|boundaries\n");
    return 2;
}
