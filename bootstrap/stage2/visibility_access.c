#include "visibility_access.h"

#include <string.h>

static bool identity_is_valid(
    const KofunIdentity *identity,
    KofunIdentityKind expected_kind
) {
    size_t index;
    bool has_nonzero_byte = false;

    if (identity == NULL || identity->schema != KOFUN_IDENTITY_SCHEMA_V1 ||
        identity->kind != expected_kind) {
        return false;
    }
    for (index = 0; index < KOFUN_IDENTITY_BYTES; index += 1) {
        has_nonzero_byte = has_nonzero_byte || identity->bytes[index] != 0;
    }
    return has_nonzero_byte;
}

static bool identity_equal(
    const KofunIdentity *left,
    const KofunIdentity *right
) {
    return left->schema == right->schema && left->kind == right->kind &&
        memcmp(left->bytes, right->bytes, KOFUN_IDENTITY_BYTES) == 0;
}

static bool span_is_valid(KofunSpan span) {
    return span.start <= span.end;
}

static bool visibility_is_basic(KofunVisibility visibility) {
    return visibility == KOFUN_VISIBILITY_PRIVATE ||
        visibility == KOFUN_VISIBILITY_INTERNAL ||
        visibility == KOFUN_VISIBILITY_PUBLIC;
}

static KofunVisibility minimum_visibility(
    KofunVisibility left,
    KofunVisibility right
) {
    return left < right ? left : right;
}

static uint32_t base_proof(
    const KofunAccessContext *context,
    const KofunDeclarationAccess *declaration
) {
    uint32_t proof = 0;

    if (identity_equal(
            &context->caller_package,
            &declaration->defining_package
        )) {
        proof |= KOFUN_ACCESS_PROOF_SAME_PACKAGE;
    }
    if (identity_equal(
            &context->caller_module,
            &declaration->defining_module
        )) {
        proof |= KOFUN_ACCESS_PROOF_SAME_MODULE;
    }
    if (identity_equal(&context->caller_file, &declaration->defining_file)) {
        proof |= KOFUN_ACCESS_PROOF_SAME_FILE;
    }
    if (context->has_caller_owner && declaration->has_defining_owner &&
        identity_equal(
            &context->caller_owner,
            &declaration->defining_owner
        )) {
        proof |= KOFUN_ACCESS_PROOF_SAME_OWNER;
    }
    return proof;
}

static bool context_identities_are_valid(const KofunAccessContext *context) {
    return identity_is_valid(&context->caller_package, KOFUN_ID_PACKAGE) &&
        identity_is_valid(&context->caller_module, KOFUN_ID_MODULE) &&
        identity_is_valid(&context->caller_file, KOFUN_ID_FILE) &&
        (!context->has_caller_owner || identity_is_valid(
            &context->caller_owner,
            KOFUN_ID_TYPE
        )) && span_is_valid(context->use_span);
}

static bool declaration_identities_are_valid(
    const KofunDeclarationAccess *declaration
) {
    return identity_is_valid(
            &declaration->defining_package,
            KOFUN_ID_PACKAGE
        ) && identity_is_valid(
            &declaration->defining_module,
            KOFUN_ID_MODULE
        ) && identity_is_valid(
            &declaration->defining_file,
            KOFUN_ID_FILE
        ) && (!declaration->has_defining_owner || identity_is_valid(
            &declaration->defining_owner,
            KOFUN_ID_TYPE
        )) && span_is_valid(declaration->declaration_span);
}

static bool boundary_identities_are_valid(
    const KofunEffectiveBoundary *boundary
) {
    return identity_is_valid(
            &boundary->defining_package,
            KOFUN_ID_PACKAGE
        ) && identity_is_valid(
            &boundary->defining_module,
            KOFUN_ID_MODULE
        ) && identity_is_valid(
            &boundary->defining_file,
            KOFUN_ID_FILE
        ) && (!boundary->has_defining_owner || identity_is_valid(
            &boundary->defining_owner,
            KOFUN_ID_TYPE
        ));
}

static KofunAccessReason boundary_denial_reason(
    const KofunAccessContext *context,
    KofunVisibility visibility,
    const KofunIdentity *defining_package,
    const KofunIdentity *defining_file,
    bool has_defining_owner,
    const KofunIdentity *defining_owner
) {
    if (visibility == KOFUN_VISIBILITY_PUBLIC) {
        return KOFUN_ACCESS_REASON_ALLOWED;
    }
    if (visibility == KOFUN_VISIBILITY_INTERNAL) {
        return identity_equal(&context->caller_package, defining_package)
            ? KOFUN_ACCESS_REASON_ALLOWED
            : KOFUN_ACCESS_REASON_INTERNAL_PACKAGE_BOUNDARY;
    }
    if (has_defining_owner) {
        return context->has_caller_owner && identity_equal(
            &context->caller_owner,
            defining_owner
        ) ? KOFUN_ACCESS_REASON_ALLOWED
          : KOFUN_ACCESS_REASON_PRIVATE_OWNER_BOUNDARY;
    }
    return identity_equal(&context->caller_file, defining_file)
        ? KOFUN_ACCESS_REASON_ALLOWED
        : KOFUN_ACCESS_REASON_PRIVATE_FILE_BOUNDARY;
}

static KofunSafeDisclosure denial_disclosure(
    const KofunAccessContext *context,
    const KofunIdentity *defining_package
) {
    return identity_equal(&context->caller_package, defining_package)
        ? KOFUN_DISCLOSE_DECLARATION
        : KOFUN_DISCLOSE_USE_ONLY;
}

static KofunAccessRemedy remedy_for_reason(KofunAccessReason reason) {
    switch (reason) {
        case KOFUN_ACCESS_REASON_PRIVATE_FILE_BOUNDARY:
            return KOFUN_REMEDY_USE_SAME_FILE;
        case KOFUN_ACCESS_REASON_PRIVATE_OWNER_BOUNDARY:
            return KOFUN_REMEDY_USE_SAME_OWNER;
        case KOFUN_ACCESS_REASON_INTERNAL_PACKAGE_BOUNDARY:
            return KOFUN_REMEDY_EXPOSE_PUBLIC_ABSTRACTION;
        case KOFUN_ACCESS_REASON_INACCESSIBLE_ENCLOSING_BOUNDARY:
        case KOFUN_ACCESS_REASON_UNSUPPORTED_RESTRICTED_VISIBILITY:
            return KOFUN_REMEDY_CHANGE_VISIBILITY;
        case KOFUN_ACCESS_REASON_IDENTITY_SCHEMA_MISMATCH:
            return KOFUN_REMEDY_UPDATE_SCHEMA;
        case KOFUN_ACCESS_REASON_TRAVERSAL_LIMIT_EXCEEDED:
            return KOFUN_REMEDY_REDUCE_NESTING;
        case KOFUN_ACCESS_REASON_ALLOWED:
            return KOFUN_REMEDY_NONE;
    }
    return KOFUN_REMEDY_UPDATE_SCHEMA;
}

static KofunAccessResult result_for(
    KofunAccessKind kind,
    KofunAccessReason reason,
    KofunVisibility effective_visibility,
    KofunSafeDisclosure disclosure,
    uint32_t proof
) {
    KofunAccessResult result;
    result.kind = kind;
    result.reason = reason;
    result.effective_visibility = effective_visibility;
    result.safe_disclosure = disclosure;
    result.remedy = remedy_for_reason(reason);
    result.proof = proof;
    result.usable_reference = kind == KOFUN_ACCESS_ALLOWED;
    return result;
}

KofunAccessResult kofun_decide_access(
    const KofunAccessContext *context,
    const KofunDeclarationAccess *declaration
) {
    size_t index;
    uint32_t proof;
    KofunVisibility effective;
    KofunAccessReason denial;
    KofunSafeDisclosure enclosing_disclosure = KOFUN_DISCLOSE_DECLARATION;
    bool enclosing_denied = false;

    if (context == NULL || declaration == NULL) {
        return result_for(
            KOFUN_ACCESS_UNSUPPORTED,
            KOFUN_ACCESS_REASON_IDENTITY_SCHEMA_MISMATCH,
            KOFUN_VISIBILITY_UNKNOWN,
            KOFUN_DISCLOSE_USE_ONLY,
            0
        );
    }
    if (!context_identities_are_valid(context) ||
        !declaration_identities_are_valid(declaration)) {
        return result_for(
            KOFUN_ACCESS_UNSUPPORTED,
            KOFUN_ACCESS_REASON_IDENTITY_SCHEMA_MISMATCH,
            KOFUN_VISIBILITY_UNKNOWN,
            KOFUN_DISCLOSE_USE_ONLY,
            0
        );
    }
    if (declaration->declared_visibility == KOFUN_VISIBILITY_RESTRICTED) {
        return result_for(
            KOFUN_ACCESS_UNSUPPORTED,
            KOFUN_ACCESS_REASON_UNSUPPORTED_RESTRICTED_VISIBILITY,
            KOFUN_VISIBILITY_RESTRICTED,
            denial_disclosure(context, &declaration->defining_package),
            0
        );
    }
    if (!visibility_is_basic(declaration->declared_visibility)) {
        return result_for(
            KOFUN_ACCESS_UNSUPPORTED,
            KOFUN_ACCESS_REASON_IDENTITY_SCHEMA_MISMATCH,
            KOFUN_VISIBILITY_UNKNOWN,
            KOFUN_DISCLOSE_USE_ONLY,
            0
        );
    }
    if (declaration->enclosing_count > KOFUN_ACCESS_MAX_ENCLOSING) {
        return result_for(
            KOFUN_ACCESS_DENIED,
            KOFUN_ACCESS_REASON_TRAVERSAL_LIMIT_EXCEEDED,
            declaration->declared_visibility,
            KOFUN_DISCLOSE_USE_ONLY,
            0
        );
    }
    if (declaration->enclosing_count > 0 &&
        declaration->enclosing_chain == NULL) {
        return result_for(
            KOFUN_ACCESS_UNSUPPORTED,
            KOFUN_ACCESS_REASON_IDENTITY_SCHEMA_MISMATCH,
            KOFUN_VISIBILITY_UNKNOWN,
            KOFUN_DISCLOSE_USE_ONLY,
            0
        );
    }

    proof = base_proof(context, declaration);
    effective = declaration->declared_visibility;
    denial = boundary_denial_reason(
        context,
        declaration->declared_visibility,
        &declaration->defining_package,
        &declaration->defining_file,
        declaration->has_defining_owner,
        &declaration->defining_owner
    );
    if (denial != KOFUN_ACCESS_REASON_ALLOWED) {
        return result_for(
            KOFUN_ACCESS_DENIED,
            denial,
            effective,
            denial_disclosure(context, &declaration->defining_package),
            proof
        );
    }

    for (index = 0; index < declaration->enclosing_count; index += 1) {
        const KofunEffectiveBoundary *boundary =
            &declaration->enclosing_chain[index];
        if (!boundary_identities_are_valid(boundary)) {
            return result_for(
                KOFUN_ACCESS_UNSUPPORTED,
                KOFUN_ACCESS_REASON_IDENTITY_SCHEMA_MISMATCH,
                KOFUN_VISIBILITY_UNKNOWN,
                KOFUN_DISCLOSE_USE_ONLY,
                proof
            );
        }
        if (boundary->visibility == KOFUN_VISIBILITY_RESTRICTED) {
            return result_for(
                KOFUN_ACCESS_UNSUPPORTED,
                KOFUN_ACCESS_REASON_UNSUPPORTED_RESTRICTED_VISIBILITY,
                KOFUN_VISIBILITY_RESTRICTED,
                denial_disclosure(context, &boundary->defining_package),
                proof
            );
        }
        if (!visibility_is_basic(boundary->visibility)) {
            return result_for(
                KOFUN_ACCESS_UNSUPPORTED,
                KOFUN_ACCESS_REASON_IDENTITY_SCHEMA_MISMATCH,
                KOFUN_VISIBILITY_UNKNOWN,
                KOFUN_DISCLOSE_USE_ONLY,
                proof
            );
        }
        effective = minimum_visibility(effective, boundary->visibility);
    }

    for (index = 0; index < declaration->enclosing_count; index += 1) {
        const KofunEffectiveBoundary *boundary =
            &declaration->enclosing_chain[index];
        denial = boundary_denial_reason(
            context,
            boundary->visibility,
            &boundary->defining_package,
            &boundary->defining_file,
            boundary->has_defining_owner,
            &boundary->defining_owner
        );
        if (denial != KOFUN_ACCESS_REASON_ALLOWED) {
            enclosing_denied = true;
            if (denial_disclosure(
                    context,
                    &boundary->defining_package
                ) == KOFUN_DISCLOSE_USE_ONLY) {
                enclosing_disclosure = KOFUN_DISCLOSE_USE_ONLY;
            }
        }
    }
    if (enclosing_denied) {
        return result_for(
            KOFUN_ACCESS_DENIED,
            KOFUN_ACCESS_REASON_INACCESSIBLE_ENCLOSING_BOUNDARY,
            effective,
            enclosing_disclosure,
            proof
        );
    }

    proof |= KOFUN_ACCESS_PROOF_ENCLOSING_REACHABLE;
    return result_for(
        KOFUN_ACCESS_ALLOWED,
        KOFUN_ACCESS_REASON_ALLOWED,
        effective,
        KOFUN_DISCLOSE_DECLARATION,
        proof
    );
}

const char *kofun_access_kind_name(KofunAccessKind kind) {
    switch (kind) {
        case KOFUN_ACCESS_ALLOWED: return "Allowed";
        case KOFUN_ACCESS_DENIED: return "Denied";
        case KOFUN_ACCESS_UNSUPPORTED: return "Unsupported";
    }
    return "UnknownAccessKind";
}

const char *kofun_access_reason_name(KofunAccessReason reason) {
    switch (reason) {
        case KOFUN_ACCESS_REASON_ALLOWED: return "Allowed";
        case KOFUN_ACCESS_REASON_PRIVATE_FILE_BOUNDARY:
            return "PrivateFileBoundary";
        case KOFUN_ACCESS_REASON_PRIVATE_OWNER_BOUNDARY:
            return "PrivateOwnerBoundary";
        case KOFUN_ACCESS_REASON_INTERNAL_PACKAGE_BOUNDARY:
            return "InternalPackageBoundary";
        case KOFUN_ACCESS_REASON_INACCESSIBLE_ENCLOSING_BOUNDARY:
            return "InaccessibleEnclosingBoundary";
        case KOFUN_ACCESS_REASON_UNSUPPORTED_RESTRICTED_VISIBILITY:
            return "UnsupportedRestrictedVisibility";
        case KOFUN_ACCESS_REASON_IDENTITY_SCHEMA_MISMATCH:
            return "IdentitySchemaMismatch";
        case KOFUN_ACCESS_REASON_TRAVERSAL_LIMIT_EXCEEDED:
            return "TraversalLimitExceeded";
    }
    return "UnknownAccessReason";
}

const char *kofun_visibility_name(KofunVisibility visibility) {
    switch (visibility) {
        case KOFUN_VISIBILITY_PRIVATE: return "private";
        case KOFUN_VISIBILITY_INTERNAL: return "internal";
        case KOFUN_VISIBILITY_PUBLIC: return "pub";
        case KOFUN_VISIBILITY_RESTRICTED: return "restricted";
        case KOFUN_VISIBILITY_UNKNOWN: return "unknown";
    }
    return "unknown";
}

const char *kofun_safe_disclosure_name(KofunSafeDisclosure disclosure) {
    switch (disclosure) {
        case KOFUN_DISCLOSE_DECLARATION: return "declaration";
        case KOFUN_DISCLOSE_USE_ONLY: return "use-only";
    }
    return "use-only";
}

const char *kofun_access_remedy_name(KofunAccessRemedy remedy) {
    switch (remedy) {
        case KOFUN_REMEDY_NONE: return "none";
        case KOFUN_REMEDY_USE_SAME_FILE: return "use-same-file";
        case KOFUN_REMEDY_USE_SAME_OWNER: return "use-same-owner";
        case KOFUN_REMEDY_EXPOSE_PUBLIC_ABSTRACTION:
            return "expose-public-abstraction";
        case KOFUN_REMEDY_CHANGE_VISIBILITY: return "change-visibility";
        case KOFUN_REMEDY_UPDATE_SCHEMA: return "update-schema";
        case KOFUN_REMEDY_REDUCE_NESTING: return "reduce-nesting";
    }
    return "update-schema";
}
