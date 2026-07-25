#ifndef KOFUN_STAGE2_SEMANTIC_PRODUCER_H
#define KOFUN_STAGE2_SEMANTIC_PRODUCER_H

#include "semantic_events.h"

typedef enum {
    KOFUN_STAGE2_SEMANTIC_COMPILE = 0,
    KOFUN_STAGE2_SEMANTIC_OWNERSHIP = 1
} KofunStage2SemanticAuthority;

typedef struct {
    const uint8_t *source;
    size_t source_length;
    KofunSemanticBytes logical_path;
    uint64_t caller_generation;
    KofunStage2SemanticAuthority authority;
} KofunStage2SemanticInput;

typedef struct {
    KofunSourceStatus source_status;
    KofunCompleteness completeness;
    bool token_span_committed;
    bool tooling_emission_failed;
    uint8_t compiler_exit_class;
    bool has_source_diagnostic;
    bool diagnostic_has_byte_span;
    bool diagnostic_truncated;
    char diagnostic_code[16];
    char diagnostic_category[32];
    char diagnostic_template_id[64];
    KofunSemanticSpan diagnostic_span;
    char diagnostic_fallback[KOFUN_SEMANTIC_ERROR_DETAIL_BYTES];
    KofunSemanticError tooling_error;
} KofunStage2SemanticResult;

bool kofun_stage2_produce_semantic_events(
    const KofunStage2SemanticInput *input,
    KofunSemanticSink *sink,
    bool cancellation_observed_after_commit,
    KofunStage2SemanticResult *result
);

#endif
