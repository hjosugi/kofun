#ifndef KOFUN_STAGE2_SEMANTIC_PRODUCER_H
#define KOFUN_STAGE2_SEMANTIC_PRODUCER_H

#include "semantic_events.h"

typedef struct {
    const uint8_t *source;
    size_t source_length;
    KofunSemanticBytes logical_path;
    uint64_t caller_generation;
} KofunStage2SemanticInput;

typedef struct {
    KofunSourceStatus source_status;
    KofunCompleteness completeness;
    bool token_span_committed;
    bool tooling_emission_failed;
} KofunStage2SemanticResult;

bool kofun_stage2_produce_semantic_events(
    const KofunStage2SemanticInput *input,
    KofunSemanticSink *sink,
    bool cancellation_observed_after_commit,
    KofunStage2SemanticResult *result
);

#endif
