#ifndef KOFUN_STAGE2_VISIBILITY_ACCESS_H
#define KOFUN_STAGE2_VISIBILITY_ACCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KOFUN_IDENTITY_SCHEMA_V1 1u
#define KOFUN_IDENTITY_BYTES 32u
#define KOFUN_ACCESS_MAX_ENCLOSING 64u

typedef enum {
    KOFUN_ID_PACKAGE = 1,
    KOFUN_ID_MODULE = 2,
    KOFUN_ID_FILE = 3,
    KOFUN_ID_TYPE = 4
} KofunIdentityKind;

typedef struct {
    uint16_t schema;
    KofunIdentityKind kind;
    uint8_t bytes[KOFUN_IDENTITY_BYTES];
} KofunIdentity;

typedef enum {
    KOFUN_VISIBILITY_PRIVATE = 0,
    KOFUN_VISIBILITY_INTERNAL = 1,
    KOFUN_VISIBILITY_PUBLIC = 2,
    KOFUN_VISIBILITY_RESTRICTED = 3,
    KOFUN_VISIBILITY_UNKNOWN = 255
} KofunVisibility;

typedef struct {
    uint32_t start;
    uint32_t end;
} KofunSpan;

typedef struct {
    KofunIdentity caller_package;
    KofunIdentity caller_module;
    KofunIdentity caller_file;
    bool has_caller_owner;
    KofunIdentity caller_owner;
    KofunSpan use_span;
} KofunAccessContext;

typedef struct {
    KofunVisibility visibility;
    KofunIdentity defining_package;
    KofunIdentity defining_module;
    KofunIdentity defining_file;
    bool has_defining_owner;
    KofunIdentity defining_owner;
} KofunEffectiveBoundary;

typedef struct {
    KofunVisibility declared_visibility;
    KofunIdentity defining_package;
    KofunIdentity defining_module;
    KofunIdentity defining_file;
    bool has_defining_owner;
    KofunIdentity defining_owner;
    const KofunEffectiveBoundary *enclosing_chain;
    size_t enclosing_count;
    KofunSpan declaration_span;
} KofunDeclarationAccess;

typedef enum {
    KOFUN_ACCESS_ALLOWED = 0,
    KOFUN_ACCESS_DENIED = 1,
    KOFUN_ACCESS_UNSUPPORTED = 2
} KofunAccessKind;

typedef enum {
    KOFUN_ACCESS_REASON_ALLOWED = 0,
    KOFUN_ACCESS_REASON_PRIVATE_FILE_BOUNDARY = 1,
    KOFUN_ACCESS_REASON_PRIVATE_OWNER_BOUNDARY = 2,
    KOFUN_ACCESS_REASON_INTERNAL_PACKAGE_BOUNDARY = 3,
    KOFUN_ACCESS_REASON_INACCESSIBLE_ENCLOSING_BOUNDARY = 4,
    KOFUN_ACCESS_REASON_UNSUPPORTED_RESTRICTED_VISIBILITY = 5,
    KOFUN_ACCESS_REASON_IDENTITY_SCHEMA_MISMATCH = 6,
    KOFUN_ACCESS_REASON_TRAVERSAL_LIMIT_EXCEEDED = 7
} KofunAccessReason;

typedef enum {
    KOFUN_DISCLOSE_DECLARATION = 0,
    KOFUN_DISCLOSE_USE_ONLY = 1
} KofunSafeDisclosure;

typedef enum {
    KOFUN_REMEDY_NONE = 0,
    KOFUN_REMEDY_USE_SAME_FILE = 1,
    KOFUN_REMEDY_USE_SAME_OWNER = 2,
    KOFUN_REMEDY_EXPOSE_PUBLIC_ABSTRACTION = 3,
    KOFUN_REMEDY_CHANGE_VISIBILITY = 4,
    KOFUN_REMEDY_UPDATE_SCHEMA = 5,
    KOFUN_REMEDY_REDUCE_NESTING = 6
} KofunAccessRemedy;

enum {
    KOFUN_ACCESS_PROOF_SAME_PACKAGE = 1u << 0,
    KOFUN_ACCESS_PROOF_SAME_MODULE = 1u << 1,
    KOFUN_ACCESS_PROOF_SAME_FILE = 1u << 2,
    KOFUN_ACCESS_PROOF_SAME_OWNER = 1u << 3,
    KOFUN_ACCESS_PROOF_ENCLOSING_REACHABLE = 1u << 4
};

typedef struct {
    KofunAccessKind kind;
    KofunAccessReason reason;
    KofunVisibility effective_visibility;
    KofunSafeDisclosure safe_disclosure;
    KofunAccessRemedy remedy;
    uint32_t proof;
    bool usable_reference;
} KofunAccessResult;

KofunAccessResult kofun_decide_access(
    const KofunAccessContext *context,
    const KofunDeclarationAccess *declaration
);

const char *kofun_access_kind_name(KofunAccessKind kind);
const char *kofun_access_reason_name(KofunAccessReason reason);
const char *kofun_visibility_name(KofunVisibility visibility);
const char *kofun_safe_disclosure_name(KofunSafeDisclosure disclosure);
const char *kofun_access_remedy_name(KofunAccessRemedy remedy);

#endif
