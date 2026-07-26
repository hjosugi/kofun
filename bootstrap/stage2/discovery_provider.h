#ifndef KOFUN_STAGE2_DISCOVERY_PROVIDER_H
#define KOFUN_STAGE2_DISCOVERY_PROVIDER_H

#include "discovery_v1.h"
#include "semantic_events.h"

/*
 * The provider boundary for developer discovery (#637).
 *
 * `discovery_v1` owns the wire contract and holds no compiler state; this file
 * is the other half — it turns #608's semantic records into the contract's
 * facts. Keeping the two apart is what the issue means by "keep JSON sidecar
 * ingestion behind the same logical provider boundary so #609 can be added
 * later without changing query semantics": a sidecar transport arrives as the
 * same `KofunSemantic*` records, so it enters through these functions and the
 * query semantics cannot diverge from the in-process path.
 *
 * Nothing here evaluates user code, reads the filesystem, or consults a clock.
 * Every function is a pure projection of records the caller already holds.
 */

/*
 * Hex-encode the live analysis identity into the contract's `AnalysisKey`.
 *
 * The interface-set digest is not part of a `KofunSemanticSource` record, so
 * the caller supplies it as 64 lowercase hex characters; passing NULL yields a
 * key whose interface digest is all zeros, which will not match any real
 * request and therefore fails closed as `stale-interface-set` rather than
 * matching by accident.
 */
bool kofun_discovery_analysis_key_from_source(
    const KofunSemanticSource *source, const char *interface_set_sha256_hex,
    KofunDiscoveryAnalysisKey *out);

/*
 * Compare a request's analysis key against the live one.
 *
 * Returns true when the request is current. Otherwise `*reason` carries the
 * stale reason, checked in the order the contract fixes: the `FileId` first,
 * because "filesystem path equality is not a substitute" and a request aimed
 * at another file must not be answered with this file's facts; then semantic
 * compatibility, source digest, interface set, and generation.
 *
 * The order matters for more than tidiness. A stale request usually mismatches
 * on several axes at once, and reporting the *most fundamental* one tells the
 * client what to actually do: re-target the file, upgrade the client, or
 * re-run analysis.
 */
bool kofun_discovery_is_current(const KofunDiscoveryRequest *request,
                                const KofunDiscoveryAnalysisKey *live,
                                KofunDiscoveryReason *reason);

/*
 * Select the expression node the request names.
 *
 * Chooses the narrowest node whose span contains `byte_offset`, considering
 * only nodes that can be expressions, then requires that node's span to equal
 * the request's `expression` span exactly. The contract is explicit that a
 * provider "rejects a client-supplied expression span that does not match its
 * current parsed occurrence": a client that computed a span against edited or
 * differently-parsed source must be refused rather than answered about a
 * different expression that happens to overlap.
 *
 * Returns true and sets `*out_index` on success; on failure `*reason` is
 * `invalid-position`.
 */
bool kofun_discovery_select_expression(const KofunSemanticNode *nodes,
                                       size_t node_count,
                                       const KofunDiscoveryRequest *request,
                                       size_t *out_index,
                                       KofunDiscoveryReason *reason);

/*
 * Project the type of a selected node from its identity and fact records.
 *
 * Consumes the `TypeId` identity and the `KOFUN_SEMANTIC_FACT_TYPE` fact owned
 * by `node_id`. A validated type needs both; anything less becomes a
 * provisional, error, or unavailable fact carrying a reason, and never an
 * invented `Any`.
 *
 * Returns false only when the inputs are unusable (null arguments); an absent
 * or non-validated type is a successful projection of an honest non-validated
 * fact.
 */
bool kofun_discovery_type_from_records(
    const KofunSemanticId *node_id, const KofunSemanticIdentity *identities,
    size_t identity_count, const KofunSemanticFact *facts, size_t fact_count,
    KofunDiscoveryTypeFact *out);

#endif /* KOFUN_STAGE2_DISCOVERY_PROVIDER_H */
