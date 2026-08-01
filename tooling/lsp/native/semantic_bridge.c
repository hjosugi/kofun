#include <node_api.h>

#include "semantic_producer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static napi_value bridge_undefined(napi_env env) {
    napi_value value;
    if (napi_get_undefined(env, &value) != napi_ok) return NULL;
    return value;
}

static bool bridge_set(napi_env env, napi_value object, const char *name,
                       napi_value value) {
    return napi_set_named_property(env, object, name, value) == napi_ok;
}

static napi_value bridge_boolean(napi_env env, bool value) {
    napi_value result;
    if (napi_get_boolean(env, value, &result) != napi_ok) return NULL;
    return result;
}

static napi_value bridge_uint32(napi_env env, uint32_t value) {
    napi_value result;
    if (napi_create_uint32(env, value, &result) != napi_ok) return NULL;
    return result;
}

static napi_value bridge_string(napi_env env, const char *value) {
    napi_value result;
    if (napi_create_string_utf8(env, value, NAPI_AUTO_LENGTH, &result) !=
        napi_ok) {
        return NULL;
    }
    return result;
}

static napi_value bridge_throw(napi_env env, const char *message) {
    (void)napi_throw_type_error(env, NULL, message);
    return NULL;
}

static napi_value bridge_result(
    napi_env env,
    bool ok,
    const KofunStage2SemanticResult *producer_result,
    const KofunSemanticError *stream_error,
    const uint8_t *event_bytes,
    size_t event_length
) {
    napi_value result;
    napi_value error;
    napi_value value;
    const char *code = "ETS03";
    const char *detail = "semantic event production failed";
    uint32_t record_index = 0u;
    uint32_t event_kind = 0u;

    if (napi_create_object(env, &result) != napi_ok) return NULL;
    value = bridge_boolean(env, ok);
    if (value == NULL || !bridge_set(env, result, "ok", value)) return NULL;
    value = bridge_uint32(env, producer_result->compiler_exit_class);
    if (value == NULL ||
        !bridge_set(env, result, "compilerExitClass", value)) return NULL;
    value = bridge_uint32(env, producer_result->source_status);
    if (value == NULL || !bridge_set(env, result, "sourceStatus", value)) {
        return NULL;
    }
    value = bridge_uint32(env, producer_result->completeness);
    if (value == NULL || !bridge_set(env, result, "completeness", value)) {
        return NULL;
    }
    value = bridge_boolean(env, producer_result->token_span_committed);
    if (value == NULL ||
        !bridge_set(env, result, "tokenSpanCommitted", value)) return NULL;
    value = bridge_string(env, producer_result->diagnostic_code);
    if (value == NULL || !bridge_set(env, result, "diagnosticCode", value)) {
        return NULL;
    }

    if (ok) {
        void *copied = NULL;
        if (napi_create_buffer_copy(
                env, event_length, event_bytes, &copied, &value) != napi_ok) {
            return NULL;
        }
        (void)copied;
        if (!bridge_set(env, result, "eventBytes", value)) return NULL;
        return result;
    }

    if (stream_error != NULL && stream_error->code[0] != '\0') {
        code = stream_error->code;
        detail = stream_error->detail[0] == '\0' ? detail :
            stream_error->detail;
        record_index = stream_error->record_index;
        event_kind = stream_error->event_kind;
    } else if (producer_result->tooling_error.code[0] != '\0') {
        code = producer_result->tooling_error.code;
        detail = producer_result->tooling_error.detail[0] == '\0' ? detail :
            producer_result->tooling_error.detail;
        record_index = producer_result->tooling_error.record_index;
        event_kind = producer_result->tooling_error.event_kind;
    }
    if (napi_create_object(env, &error) != napi_ok) return NULL;
    value = bridge_string(env, code);
    if (value == NULL || !bridge_set(env, error, "code", value)) return NULL;
    value = bridge_string(env, detail);
    if (value == NULL || !bridge_set(env, error, "detail", value)) return NULL;
    value = bridge_uint32(env, record_index);
    if (value == NULL || !bridge_set(env, error, "recordIndex", value)) {
        return NULL;
    }
    value = bridge_uint32(env, event_kind);
    if (value == NULL || !bridge_set(env, error, "eventKind", value)) {
        return NULL;
    }
    if (!bridge_set(env, result, "error", error)) return NULL;
    return result;
}

static napi_value bridge_produce(napi_env env, napi_callback_info info) {
    napi_value arguments[5];
    size_t argument_count = sizeof(arguments) / sizeof(arguments[0]);
    uint8_t *source = NULL;
    size_t source_length = 0u;
    size_t logical_length = 0u;
    char logical_path[KOFUN_SEMANTIC_MAX_TEXT_BYTES + 1u];
    double generation_value;
    uint64_t generation;
    int32_t authority;
    bool cancellation_after_commit;
    KofunStage2SemanticInput input;
    KofunStage2SemanticResult producer_result;
    KofunSemanticStream *stream;
    KofunSemanticSink sink;
    const KofunSemanticError *stream_error;
    const uint8_t *event_bytes = NULL;
    size_t event_length = 0u;
    bool ok;

    if (napi_get_cb_info(
            env, info, &argument_count, arguments, NULL, NULL) != napi_ok ||
        argument_count != 5u) {
        return bridge_throw(
            env,
            "produce(source, logicalPath, generation, authority, cancelled) expected"
        );
    }
    if (napi_get_buffer_info(
            env, arguments[0], (void **)&source, &source_length) != napi_ok) {
        return bridge_throw(env, "source must be a Buffer");
    }
    if (napi_get_value_string_utf8(
            env, arguments[1], NULL, 0u, &logical_length) != napi_ok ||
        logical_length == 0u ||
        logical_length > KOFUN_SEMANTIC_MAX_TEXT_BYTES ||
        napi_get_value_string_utf8(
            env, arguments[1], logical_path, sizeof(logical_path),
            &logical_length) != napi_ok) {
        return bridge_throw(env, "logicalPath is outside the bounded profile");
    }
    if (napi_get_value_double(env, arguments[2], &generation_value) != napi_ok ||
        generation_value < 0.0 ||
        generation_value > 9007199254740991.0) {
        return bridge_throw(env, "generation must be a safe unsigned integer");
    }
    generation = (uint64_t)generation_value;
    if ((double)generation != generation_value) {
        return bridge_throw(env, "generation must be a safe unsigned integer");
    }
    if (napi_get_value_int32(env, arguments[3], &authority) != napi_ok ||
        (authority != (int32_t)KOFUN_STAGE2_SEMANTIC_COMPILE &&
         authority != (int32_t)KOFUN_STAGE2_SEMANTIC_OWNERSHIP)) {
        return bridge_throw(env, "authority must be compile or ownership");
    }
    if (napi_get_value_bool(
            env, arguments[4], &cancellation_after_commit) != napi_ok) {
        return bridge_throw(env, "cancelled must be boolean");
    }

    memset(&input, 0, sizeof(input));
    input.source = source;
    input.source_length = source_length;
    input.logical_path.bytes = (const uint8_t *)logical_path;
    input.logical_path.length = (uint32_t)logical_length;
    input.caller_generation = generation;
    input.authority = (KofunStage2SemanticAuthority)authority;

    stream = kofun_semantic_stream_create();
    if (stream == NULL) {
        return bridge_throw(env, "semantic stream allocation failed");
    }
    sink = kofun_semantic_stream_sink(stream);
    ok = kofun_stage2_produce_semantic_events(
        &input, &sink, cancellation_after_commit, &producer_result
    );
    if (ok) {
        ok = kofun_semantic_stream_bytes(
            stream, &event_bytes, &event_length
        ) && event_length != 0u;
    }
    stream_error = kofun_semantic_stream_error(stream);
    {
        napi_value result = bridge_result(
            env,
            ok,
            &producer_result,
            stream_error,
            event_bytes,
            event_length
        );
        kofun_semantic_stream_destroy(stream);
        return result;
    }
}

static napi_value bridge_init(napi_env env, napi_value exports) {
    napi_value function;
    if (napi_create_function(
            env, "produce", NAPI_AUTO_LENGTH, bridge_produce, NULL,
            &function) != napi_ok ||
        !bridge_set(env, exports, "produce", function)) {
        return bridge_undefined(env);
    }
    return exports;
}

NAPI_MODULE(semantic_bridge, bridge_init)
