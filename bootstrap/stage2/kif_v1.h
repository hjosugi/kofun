#ifndef KOFUN_STAGE2_KIF_V1_H
#define KOFUN_STAGE2_KIF_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KOFUN_KIF_ID_BYTES 32u
#define KOFUN_KIF_MAX_ENVELOPE (16u * 1024u * 1024u)
#define KOFUN_KIF_MAX_RECORD_FIELDS 256u
#define KOFUN_KIF_MAX_FACTS 65536u
#define KOFUN_KIF_MAX_DEPTH 128u
#define KOFUN_KIF_MAX_FIELD_BYTES (1024u * 1024u)
#define KOFUN_KIF_MAX_NAME_BYTES 256u
#define KOFUN_KIF_MAX_EDITION_BYTES 64u

typedef enum {
    KOFUN_KIF_OK = 0,
    KOFUN_KIF_UNSUPPORTED_SCHEMA = 1,
    KOFUN_KIF_CORRUPT = 2,
    KOFUN_KIF_NONCANONICAL = 3,
    KOFUN_KIF_LIMIT_EXHAUSTED = 4,
    KOFUN_KIF_DIGEST_MISMATCH = 5,
    KOFUN_KIF_IO_FAILURE = 6,
    KOFUN_KIF_INTERNAL_INVARIANT = 7,
    KOFUN_KIF_VISIBILITY_LEAK = 8
} KofunKifStatus;

typedef enum {
    KOFUN_KIF_FACT_FUNCTION = 1,
    KOFUN_KIF_FACT_ADT = 2,
    KOFUN_KIF_FACT_CONSTRUCTOR = 3
} KofunKifFactKind;

typedef enum {
    KOFUN_KIF_VISIBILITY_INTERNAL = 1,
    KOFUN_KIF_VISIBILITY_PUBLIC = 2
} KofunKifVisibility;

typedef enum {
    KOFUN_KIF_TYPE_INT = 1
} KofunKifTypeTag;

typedef struct {
    uint8_t namespace_id[KOFUN_KIF_ID_BYTES];
    uint8_t symbol_id[KOFUN_KIF_ID_BYTES];
    KofunKifFactKind kind;
    KofunKifVisibility visibility;
    char *name;
    size_t name_length;

    /* Function facts: every parameter and the result are Int in v1. */
    uint16_t parameter_count;
    KofunKifTypeTag result_type;

    /* Constructor facts: zero or one Int payload, with a nominal owner. */
    uint8_t constructor_payload_count;
    uint8_t owner_symbol_id[KOFUN_KIF_ID_BYTES];
    uint32_t constructor_ordinal;
} KofunKifFact;

typedef struct {
    uint8_t package_id[KOFUN_KIF_ID_BYTES];
    uint8_t module_id[KOFUN_KIF_ID_BYTES];
    char edition[KOFUN_KIF_MAX_EDITION_BYTES + 1u];

    KofunKifFact *public_facts;
    size_t public_fact_count;
    KofunKifFact *internal_facts;
    size_t internal_fact_count;

    uint8_t public_semantic_digest[KOFUN_KIF_ID_BYTES];
    uint8_t package_internal_semantic_digest[KOFUN_KIF_ID_BYTES];

    /* Set by the reader. Callers constructing writer input leave it false. */
    bool owns_storage;
} KofunKifInterface;

typedef struct {
    size_t max_envelope_bytes;
    size_t max_record_fields;
    size_t max_facts;
    size_t max_depth;
    size_t max_field_bytes;
} KofunKifLimits;

typedef struct {
    KofunKifStatus status;
    const char *message;
    uint8_t public_semantic_digest[KOFUN_KIF_ID_BYTES];
    uint8_t package_internal_semantic_digest[KOFUN_KIF_ID_BYTES];
} KifWriteResult;

typedef struct {
    KofunKifStatus status;
    const char *message;
    bool rebuild_required;
    KofunKifInterface *interface;
} KifReadResult;

KofunKifLimits kofun_kif_default_limits(void);

KifWriteResult kofun_kif_write(
    const KofunKifInterface *interface,
    const char *destination
);

KifReadResult kofun_kif_read(
    const uint8_t *bytes,
    size_t length,
    KofunKifLimits limits
);

void kofun_kif_destroy(KofunKifInterface *interface);

const char *kofun_kif_status_name(KofunKifStatus status);

#endif
