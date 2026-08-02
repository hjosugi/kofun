#ifndef KOFUN_STAGE2_CONFUSABLE_VISIBLE_SET_H
#define KOFUN_STAGE2_CONFUSABLE_VISIBLE_SET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KOFUN_VISIBLE_BINDING_LIMIT 65536u
#define KOFUN_VISIBLE_RELATED_LIMIT 8u
#define KOFUN_VISIBLE_ID_BYTES 32u

typedef enum {
    KOFUN_VISIBLE_SITE_LOCAL = 0,
    KOFUN_VISIBLE_SITE_IMPORT = 1,
    KOFUN_VISIBLE_SITE_RE_EXPORT = 2
} KofunVisibleSiteKind;

/*
 * One already-filtered resolver binding. Callers must omit private,
 * inaccessible, and unselected dependency symbols. effective_spelling is the
 * NFC source/KIF spelling visible in resolving_module_id; target_symbol_id is
 * carried only to bind the cache key and is never rewritten.
 */
typedef struct {
    uint8_t resolving_module_id[KOFUN_VISIBLE_ID_BYTES];
    uint8_t namespace_id[KOFUN_VISIBLE_ID_BYTES];
    uint8_t binding_id[KOFUN_VISIBLE_ID_BYTES];
    uint8_t target_symbol_id[KOFUN_VISIBLE_ID_BYTES];
    const uint8_t *effective_spelling;
    size_t effective_spelling_length;
    KofunVisibleSiteKind site_kind;
    const char *canonical_provenance;
    uint32_t span_start;
    uint32_t span_end;
    bool disclose_location;
} KofunVisibleBinding;

typedef struct {
    size_t primary_binding;
    size_t related_bindings[KOFUN_VISIBLE_RELATED_LIMIT];
    size_t related_count;
    size_t related_omitted;
} KofunVisibleConfusableDiagnostic;

typedef enum {
    KOFUN_VISIBLE_CONFUSABLE_OK = 0,
    KOFUN_VISIBLE_CONFUSABLE_COLLISION = 1,
    KOFUN_VISIBLE_CONFUSABLE_INVALID_INPUT = 2,
    KOFUN_VISIBLE_CONFUSABLE_LIMIT_EXHAUSTED = 3,
    KOFUN_VISIBLE_CONFUSABLE_RESOURCE_FAILURE = 4,
    KOFUN_VISIBLE_CONFUSABLE_OUTPUT_TOO_SMALL = 5
} KofunVisibleConfusableStatus;

typedef struct {
    KofunVisibleConfusableStatus status;
    size_t diagnostic_count;
    uint64_t work;
    uint8_t cache_key[KOFUN_VISIBLE_ID_BYTES];
} KofunVisibleConfusableResult;

/*
 * Emits at most one EUNICODE008 diagnostic per
 * (resolving ModuleId, NamespaceId, skeleton) key. diagnostic_count reports
 * the complete required count even when diagnostic_capacity is too small.
 */
KofunVisibleConfusableResult kofun_check_visible_confusables(
    const KofunVisibleBinding *bindings,
    size_t binding_count,
    KofunVisibleConfusableDiagnostic *diagnostics,
    size_t diagnostic_capacity
);

const char *kofun_visible_confusable_status_name(
    KofunVisibleConfusableStatus status
);

#endif
