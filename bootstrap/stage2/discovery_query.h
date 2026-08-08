#ifndef KOFUN_STAGE2_DISCOVERY_QUERY_H
#define KOFUN_STAGE2_DISCOVERY_QUERY_H

#include "discovery_provider.h"
#include "semantic_producer.h"

/*
 * One live analysis transaction.  The semantic snapshot owns every byte used
 * by later queries; the AnalysisKey is derived from that same committed
 * transaction rather than accepted from a client.
 */
typedef struct {
    KofunStage2DiscoverySnapshot semantic;
    KofunDiscoveryAnalysisKey analysis_key;
} KofunStage2DiscoveryAnalysis;

bool kofun_stage2_discovery_analyze(
    const KofunStage2SemanticInput *input,
    const char *interface_set_sha256_hex,
    KofunStage2DiscoveryAnalysis *analysis,
    KofunStage2SemanticResult *result
);

/*
 * Answer canonical request bytes from one committed live analysis.  `source`
 * must be the exact source buffer analyzed above; its SHA-256 is rechecked
 * before offsets are inspected.  Returns canonical result bytes written, or
 * zero for a provider misuse or an output buffer that cannot hold the result.
 */
size_t kofun_stage2_discovery_query(
    const KofunStage2DiscoveryAnalysis *analysis,
    const uint8_t *source,
    size_t source_length,
    const char *request_bytes,
    size_t request_length,
    char *output,
    size_t output_capacity
);

#endif
