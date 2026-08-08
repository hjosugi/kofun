#include "discovery_query.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const EMPTY_INTERFACE_SET =
    "80d1c79a9cc431fc7b585d15fcf9a21b31e2daa8002300b1b95cda519d163804";
static const char *const LOGICAL_PATH =
    "tests/conformance/discovery/live_nominal_type.kofun";

static void fail(const char *detail) {
    fprintf(stderr, "nominal-typeid: %s\n", detail);
    exit(1);
}

static uint8_t *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *bytes;
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) return NULL;
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)size + 1u);
    if (bytes == NULL ||
        fread(bytes, 1u, (size_t)size, file) != (size_t)size) {
        free(bytes);
        (void)fclose(file);
        return NULL;
    }
    (void)fclose(file);
    bytes[size] = 0u;
    *length = (size_t)size;
    return bytes;
}

static size_t reference_start(const uint8_t *source) {
    const char *match = strstr((const char *)source, "return value");
    if (match == NULL) fail("fixture has no nominal reference");
    return (size_t)(match - (const char *)source) + strlen("return ");
}

static bool contains_bytes(
    const char *bytes,
    size_t length,
    const char *needle
) {
    size_t needle_length = strlen(needle);
    size_t index;
    if (needle_length > length) return false;
    for (index = 0u; index + needle_length <= length; index += 1u) {
        if (memcmp(bytes + index, needle, needle_length) == 0) return true;
    }
    return false;
}

static KofunStage2DiscoveryExpression *expression_at(
    KofunStage2DiscoveryAnalysis *analysis,
    size_t start
) {
    size_t index;
    for (index = 0u;
         index < analysis->semantic.expression_count;
         index += 1u) {
        KofunStage2DiscoveryExpression *expression =
            &analysis->semantic.expressions[index];
        if (expression->node.span.start == start &&
            expression->node.span.end == start + strlen("value")) {
            return expression;
        }
    }
    return NULL;
}

static void analyze(
    const uint8_t *source,
    size_t source_length,
    KofunStage2DiscoveryAnalysis *analysis
) {
    KofunStage2SemanticInput input;
    KofunStage2SemanticResult result;
    memset(&input, 0, sizeof(input));
    memset(analysis, 0, sizeof(*analysis));
    input.source = source;
    input.source_length = source_length;
    input.logical_path.bytes = (const uint8_t *)LOGICAL_PATH;
    input.logical_path.length = (uint32_t)strlen(LOGICAL_PATH);
    input.caller_generation = 23u;
    input.authority = KOFUN_STAGE2_SEMANTIC_COMPILE;
    if (!kofun_stage2_discovery_analyze(
            &input, EMPTY_INTERFACE_SET, analysis, &result)) {
        fprintf(
            stderr,
            "nominal-typeid: compiler-exit=%u diagnostic=%s fallback=%s tooling=%s\n",
            (unsigned)result.compiler_exit_class,
            result.diagnostic_code,
            result.diagnostic_fallback,
            result.tooling_error.detail
        );
        fail("Stage 2 discovery analysis did not commit");
    }
}

static void require_validated_nominal(
    const KofunStage2DiscoveryExpression *expression
) {
    if (expression == NULL || !expression->has_type_identity ||
        expression->type_identity.kind != KOFUN_SEMANTIC_ID_TYPE ||
        expression->type_identity.status != KOFUN_SEMANTIC_VALIDATED ||
        !expression->has_type_fact ||
        expression->type_status != KOFUN_SEMANTIC_VALIDATED ||
        strcmp(expression->type_display, "Choice") != 0 ||
        expression->type_reason[0] != '\0') {
        fail("resolved nominal reference lacks validated TypeId and type fact");
    }
}

static size_t build_request(
    char *request,
    size_t capacity,
    const KofunStage2DiscoveryAnalysis *analysis,
    size_t start
) {
    int written = snprintf(
        request,
        capacity,
        "{\n"
        "  \"analysis\": {\n"
        "    \"file_id\": \"%s\",\n"
        "    \"generation\": %lld,\n"
        "    \"interface_set_sha256\": \"%s\",\n"
        "    \"semantic_compatibility\": \"%s\",\n"
        "    \"source_sha256\": \"%s\"\n"
        "  },\n"
        "  \"position\": {\n"
        "    \"byte_offset\": %zu,\n"
        "    \"expression\": {\n"
        "      \"end\": %zu,\n"
        "      \"start\": %zu\n"
        "    }\n"
        "  },\n"
        "  \"query\": {\n"
        "    \"include_unavailable\": true,\n"
        "    \"kind\": \"type\",\n"
        "    \"spelling\": null\n"
        "  },\n"
        "  \"schema\": \"kofun.discovery.request/v1\"\n"
        "}\n",
        analysis->analysis_key.file_id,
        (long long)analysis->analysis_key.generation,
        analysis->analysis_key.interface_set_sha256,
        analysis->analysis_key.semantic_compatibility,
        analysis->analysis_key.source_sha256,
        start,
        start + strlen("value"),
        start
    );
    if (written < 0 || (size_t)written >= capacity) {
        fail("request buffer exhausted");
    }
    return (size_t)written;
}

static void render_id(
    const KofunSemanticId *identity,
    char output[KOFUN_SEMANTIC_ID_BYTES * 2u + 1u]
) {
    static const char digits[] = "0123456789abcdef";
    size_t index;
    for (index = 0u; index < KOFUN_SEMANTIC_ID_BYTES; index += 1u) {
        output[index * 2u] =
            digits[(identity->bytes[index] >> 4u) & 0x0fu];
        output[index * 2u + 1u] = digits[identity->bytes[index] & 0x0fu];
    }
    output[KOFUN_SEMANTIC_ID_BYTES * 2u] = '\0';
}

int main(int argc, char **argv) {
    uint8_t *source;
    uint8_t *shifted;
    uint8_t *unrelated_edit;
    size_t source_length = 0u;
    size_t unrelated_edit_length = 0u;
    size_t start;
    size_t unrelated_edit_start;
    KofunStage2DiscoveryAnalysis analysis;
    KofunStage2DiscoveryExpression *expression;
    KofunSemanticId expected;
    char expected_hex[KOFUN_SEMANTIC_ID_BYTES * 2u + 1u];
    char expected_value[96];
    char request[4096];
    int expected_value_length;
    size_t request_length;
    char *output;
    size_t output_length;

    if (argc != 3) {
        fail("usage: nominal-typeid-test FIXTURE UNRELATED-EDIT-FIXTURE");
    }
    source = read_file(argv[1], &source_length);
    if (source == NULL) fail("could not read fixture");
    start = reference_start(source);
    analyze(source, source_length, &analysis);
    expression = expression_at(&analysis, start);
    require_validated_nominal(expression);
    expected = expression->type_identity.value;
    render_id(&expected, expected_hex);
    expected_value_length = snprintf(
        expected_value,
        sizeof(expected_value),
        "\"value\": \"%s\"",
        expected_hex
    );
    if (expected_value_length < 0 ||
        (size_t)expected_value_length >= sizeof(expected_value)) {
        free(source);
        fail("expected identity value buffer exhausted");
    }

    request_length = build_request(
        request, sizeof(request), &analysis, start);
    output = malloc(KOFUN_DISCOVERY_MAX_RESULT_BYTES);
    if (output == NULL) fail("result allocation failed");
    output_length = kofun_stage2_discovery_query(
        &analysis,
        source,
        source_length,
        request,
        request_length,
        output,
        KOFUN_DISCOVERY_MAX_RESULT_BYTES
    );
    if (output_length == 0u ||
        !contains_bytes(
            output,
            output_length,
            "\"display\": \"Choice\",\n    \"identity\": {\n      \"kind\": \"TypeId\"") ||
        !contains_bytes(output, output_length, expected_value) ||
        !contains_bytes(
            output,
            output_length,
            "\"reason\": null,\n    \"status\": \"validated\"")) {
        free(output);
        free(source);
        fail("query did not emit the validated nominal TypeId");
    }

    shifted = malloc(source_length + 2u);
    if (shifted == NULL) {
        free(output);
        free(source);
        fail("shifted source allocation failed");
    }
    shifted[0] = '\n';
    memcpy(shifted + 1u, source, source_length + 1u);
    analyze(shifted, source_length + 1u, &analysis);
    expression = expression_at(&analysis, start + 1u);
    require_validated_nominal(expression);
    if (memcmp(
            expression->type_identity.value.bytes,
            expected.bytes,
            KOFUN_SEMANTIC_ID_BYTES) != 0) {
        free(shifted);
        free(output);
        free(source);
        fail("source-span shift changed nominal TypeId");
    }

    unrelated_edit = read_file(argv[2], &unrelated_edit_length);
    if (unrelated_edit == NULL) {
        free(shifted);
        free(output);
        free(source);
        fail("could not read unrelated-edit fixture");
    }
    unrelated_edit_start = reference_start(unrelated_edit);
    analyze(unrelated_edit, unrelated_edit_length, &analysis);
    expression = expression_at(&analysis, unrelated_edit_start);
    require_validated_nominal(expression);
    if (memcmp(
            expression->type_identity.value.bytes,
            expected.bytes,
            KOFUN_SEMANTIC_ID_BYTES) != 0) {
        free(unrelated_edit);
        free(shifted);
        free(output);
        free(source);
        fail("unrelated declaration rename/reorder changed nominal TypeId");
    }

    printf("nominal-type-id=%s\n", expected_hex);
    printf("type=validated display=Choice identity=TypeId\n");
    printf("source-span-shift=identical\n");
    printf("unrelated-rename-reorder=identical\n");
    printf("query=validated-type exact-value=present\n");

    free(unrelated_edit);
    free(shifted);
    free(output);
    free(source);
    return 0;
}
