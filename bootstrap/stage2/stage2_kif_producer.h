#ifndef KOFUN_STAGE2_KIF_PRODUCER_H
#define KOFUN_STAGE2_KIF_PRODUCER_H

#include "kif_v1.h"
#include "semantic_producer.h"

typedef struct {
    bool published;
    KofunStage2SemanticResult compiler;
    KifWriteResult write;
} KofunStage2KifResult;

bool kofun_stage2_publish_kif(
    const KofunStage2SemanticInput *input,
    KofunSemanticBytes edition,
    const char *destination,
    bool cancellation_observed_after_commit,
    KofunStage2KifResult *result
);

#endif
