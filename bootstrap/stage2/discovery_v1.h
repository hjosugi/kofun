#ifndef KOFUN_STAGE2_DISCOVERY_V1_H
#define KOFUN_STAGE2_DISCOVERY_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * `kofun.discovery.request/v1` and `kofun.discovery.result/v1` (#637), as
 * defined by docs/DEVELOPER_DISCOVERY.md.
 *
 * This is the contract layer: it decides whether a request is admissible and
 * emits results whose bytes are canonical. It deliberately holds no compiler
 * state. The projection of types and callable operations plugs in behind
 * `kofun_discovery_result_emit`, so the query semantics stay identical whether
 * the facts arrive from the live in-process analysis or, later, from a
 * validated sidecar (#609).
 *
 * Two rules from the contract shape every signature here:
 *
 *   - `authoritative` is always JSON `false`. A discovery result reports what
 *     the compiler already validated; it never becomes a source of truth, so
 *     there is no way to ask for it to be one.
 *   - The byte caps measure the canonical JSON encoding even when a transport
 *     carries the record some other way, so length checks belong to this module
 *     rather than to a transport.
 */

#define KOFUN_DISCOVERY_REQUEST_SCHEMA "kofun.discovery.request/v1"
#define KOFUN_DISCOVERY_RESULT_SCHEMA "kofun.discovery.result/v1"
#define KOFUN_DISCOVERY_LIMIT_PROFILE "kofun.discovery/default-v1"

/* Exactly 64 lowercase hex characters, per the `Id` scalar. */
#define KOFUN_DISCOVERY_ID_CHARS 64u

#define KOFUN_DISCOVERY_MAX_REQUEST_BYTES (64u * 1024u)
#define KOFUN_DISCOVERY_MAX_RESULT_BYTES (4u * 1024u * 1024u)
#define KOFUN_DISCOVERY_MAX_REQUEST_DEPTH 16u
#define KOFUN_DISCOVERY_MAX_RESULT_DEPTH 64u
#define KOFUN_DISCOVERY_MAX_OPERATIONS 4096u
#define KOFUN_DISCOVERY_MAX_DIAGNOSTICS 256u
#define KOFUN_DISCOVERY_MAX_OMISSIONS 64u
#define KOFUN_DISCOVERY_MAX_SPELLING_BYTES 1024u
#define KOFUN_DISCOVERY_MAX_COMPATIBILITY_BYTES 64u

/* U32 and U53 upper bounds, named so the checks read as the contract does. */
#define KOFUN_DISCOVERY_U32_MAX UINT32_C(4294967295)
#define KOFUN_DISCOVERY_U53_MAX INT64_C(9007199254740991)

typedef enum {
    KOFUN_DISCOVERY_STATUS_COMPLETE = 1,
    KOFUN_DISCOVERY_STATUS_PARTIAL = 2,
    KOFUN_DISCOVERY_STATUS_STALE = 3,
    KOFUN_DISCOVERY_STATUS_UNAVAILABLE = 4,
    KOFUN_DISCOVERY_STATUS_INVALID = 5
} KofunDiscoveryStatus;

/*
 * `ResultReason`, in the contract's order. `KOFUN_DISCOVERY_REASON_NONE`
 * encodes JSON `null`, which `complete` requires and the other statuses forbid.
 */
typedef enum {
    KOFUN_DISCOVERY_REASON_NONE = 0,
    KOFUN_DISCOVERY_REASON_CANCELLED_BEFORE_ANALYSIS = 1,
    KOFUN_DISCOVERY_REASON_INCOMPLETE_CURRENT_FILE_FACTS = 2,
    KOFUN_DISCOVERY_REASON_INVALID_POSITION = 3,
    KOFUN_DISCOVERY_REASON_INVALID_REQUEST = 4,
    KOFUN_DISCOVERY_REASON_LIMIT_EXHAUSTED = 5,
    KOFUN_DISCOVERY_REASON_STALE_GENERATION = 6,
    KOFUN_DISCOVERY_REASON_STALE_INTERFACE_SET = 7,
    KOFUN_DISCOVERY_REASON_STALE_SEMANTIC_COMPATIBILITY = 8,
    KOFUN_DISCOVERY_REASON_STALE_SOURCE = 9,
    KOFUN_DISCOVERY_REASON_UNSUPPORTED_IN_PROFILE = 10,
    KOFUN_DISCOVERY_REASON_WRONG_FILE = 11
} KofunDiscoveryReason;

typedef enum {
    KOFUN_DISCOVERY_QUERY_TYPE = 1,
    KOFUN_DISCOVERY_QUERY_OPERATIONS = 2,
    KOFUN_DISCOVERY_QUERY_TYPE_AND_OPERATIONS = 3,
    KOFUN_DISCOVERY_QUERY_EXPLAIN_OPERATION = 4
} KofunDiscoveryQueryKind;

typedef struct {
    char file_id[KOFUN_DISCOVERY_ID_CHARS + 1u];
    char source_sha256[KOFUN_DISCOVERY_ID_CHARS + 1u];
    char interface_set_sha256[KOFUN_DISCOVERY_ID_CHARS + 1u];
    char semantic_compatibility[KOFUN_DISCOVERY_MAX_COMPATIBILITY_BYTES + 1u];
    /* U53: the contract permits generations past U32. */
    int64_t generation;
} KofunDiscoveryAnalysisKey;

typedef struct {
    KofunDiscoveryAnalysisKey analysis;
    uint32_t byte_offset;
    uint32_t expression_start;
    uint32_t expression_end;
    KofunDiscoveryQueryKind kind;
    bool include_unavailable;
    /* Empty exactly when the request carried JSON `null`. */
    char spelling[KOFUN_DISCOVERY_MAX_SPELLING_BYTES + 1u];
    bool has_spelling;
} KofunDiscoveryRequest;

/*
 * Parse and validate canonical request bytes.
 *
 * Returns true only for a request that is admissible on every axis the schema
 * fixes: exact schema string, every required field present exactly once, no
 * unknown field, ASCII-lexicographic key order, no floating point, no
 * duplicate key, depth and byte caps, scalar ranges, and
 * `expression.start <= byte_offset <= expression.end`.
 *
 * On failure `*reason` is set to the reason the contract names for that
 * rejection — `invalid-position` when the offsets are individually well formed
 * but inconsistent, and `invalid-request` for every structural failure — so a
 * caller can emit the required `invalid` result without re-deriving why.
 *
 * Whether the offsets fall on UTF-8 code-point boundaries *of the analyzed
 * source* is deliberately not decided here: this module never sees the source.
 * `kofun_discovery_offsets_are_boundaries` is provided for the caller that
 * does.
 */
bool kofun_discovery_request_parse(const char *bytes, size_t length,
                                   KofunDiscoveryRequest *out,
                                   KofunDiscoveryReason *reason);

/*
 * True when all three request offsets land on UTF-8 code-point boundaries
 * within `source`, and the expression span lies inside it. The caller supplies
 * the exact source named by the analysis key.
 */
bool kofun_discovery_offsets_are_boundaries(const KofunDiscoveryRequest *request,
                                            const char *source,
                                            size_t source_length);

/*
 * Emit a canonical `kofun.discovery.result/v1` object followed by one LF.
 *
 * `analysis` is echoed when the status permits it and must be NULL when the
 * status is `invalid`. This entry point covers every status whose shape carries
 * no facts — `stale`, `unavailable`, and `invalid` — which the contract defines
 * as having a null type and empty operations, omissions, and diagnostics.
 * Fact-bearing statuses arrive with the projection.
 *
 * Returns the number of bytes written, or 0 if the buffer is too small or the
 * (status, reason, analysis) triple is not one the contract permits.
 */
size_t kofun_discovery_result_emit_factless(KofunDiscoveryStatus status,
                                            KofunDiscoveryReason reason,
                                            const KofunDiscoveryAnalysisKey *analysis,
                                            char *buffer, size_t capacity);

/* Stable spellings, for diagnostics and tests. */
const char *kofun_discovery_status_name(KofunDiscoveryStatus status);
const char *kofun_discovery_reason_name(KofunDiscoveryReason reason);

#endif /* KOFUN_STAGE2_DISCOVERY_V1_H */
