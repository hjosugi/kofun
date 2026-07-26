/*
 * Persisted compiler semantic dependency graph (#301, first slice).
 *
 * This helper is the semantic-invalidation layer that sits above the KIF v1
 * interface digests. It resolves one package inventory, derives each module's
 * public and package-internal semantic digests, persists nodes and edges under
 * a cache directory, and on a later run decides which modules must be executed
 * and which semantic products may be reused.
 *
 * It owns compiler semantics only. It is not Frost's target/action graph, it
 * schedules nothing, and it never claims a timing result. The gate records the
 * exact executed/reused node sets.
 *
 * Reuse is a real skip: a reused module's interface is neither rebuilt nor
 * republished, and its digests are taken from the verified cache entry.
 */

#define _POSIX_C_SOURCE 200809L

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define KOFUN_RE_EXPORTS_NO_MAIN
#include "re_exports.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define INCREMENTAL_GRAPH_SCHEMA "kofun-incremental-graph/v1"
#define INCREMENTAL_REPORT_SCHEMA "kofun-incremental-report/v1"

/*
 * Resource limits. Every one of them is a bounded cache miss when a stored
 * graph exceeds it, never a trusted read and never a crash.
 */
#define INCREMENTAL_MODULE_LIMIT MODULE_LIMIT
#define INCREMENTAL_EDGE_LIMIT 65536u
#define INCREMENTAL_MANIFEST_BYTE_LIMIT (4u * 1024u * 1024u)
#define INCREMENTAL_MANIFEST_LINE_LIMIT (LOGICAL_PATH_LIMIT + 1024u)
#define INCREMENTAL_KIF_BYTE_LIMIT KOFUN_KIF_MAX_ENVELOPE
#define INCREMENTAL_PATH_LIMIT (HOST_PATH_LIMIT + 128u)

typedef enum {
    /* The cache directory holds no manifest at all. */
    CACHE_STATE_COLD = 0,
    /* A manifest was read, validated, and may be consulted. */
    CACHE_STATE_WARM = 1,
    /* A manifest existed but could not be trusted; recompute everything. */
    CACHE_STATE_MISS = 2
} CacheState;

typedef struct {
    uint8_t module_id[32];
    uint8_t source_digest[32];
    uint8_t public_digest[32];
    uint8_t internal_digest[32];
    uint8_t kif_digest[32];
    char logical_path[LOGICAL_PATH_LIMIT + 1u];
    /* Digest over this module's outgoing edge set, in canonical order. */
    uint8_t edge_digest[32];
} CachedModule;

typedef struct {
    CacheState state;
    const char *miss_reason;
    uint8_t package_id[32];
    CachedModule modules[INCREMENTAL_MODULE_LIMIT];
    size_t module_count;
} CachedGraph;

typedef enum {
    NODE_EXECUTED = 0,
    NODE_REUSED = 1
} NodeOutcome;

typedef struct {
    uint8_t source_digest[32];
    uint8_t public_digest[32];
    uint8_t internal_digest[32];
    uint8_t kif_digest[32];
    uint8_t edge_digest[32];
    NodeOutcome outcome;
    const char *reason;
    /* Logical path of the upstream module that forced this execution. */
    const char *reason_source;
    /*
     * Set when an already-decided upstream module invalidated this one. It is
     * deliberately separate from `outcome`, whose zero value is EXECUTED and
     * would otherwise make every undecided module look pre-forced.
     */
    bool forced;
    bool decided;
    bool public_changed;
    bool internal_changed;
    bool digests_known;
} ModuleState;

typedef struct {
    ReExportResolver resolver;
    ModuleState states[INCREMENTAL_MODULE_LIMIT];
    CachedGraph cache;
    /* Canonical order in which modules are decided: dependencies first. */
    size_t order[INCREMENTAL_MODULE_LIMIT];
    size_t order_count;
    char cache_directory[INCREMENTAL_PATH_LIMIT];
    size_t executed_count;
    size_t reused_count;
} IncrementalGraph;

/* ---------------------------------------------------------------- helpers */

static void incremental_note(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}

static bool digests_equal(const uint8_t left[32], const uint8_t right[32]) {
    return memcmp(left, right, 32u) == 0;
}

static bool join_cache_path(
    char *output,
    size_t capacity,
    const char *directory,
    const char *leaf
) {
    int written = snprintf(output, capacity, "%s/%s", directory, leaf);
    return written > 0 && (size_t)written < capacity;
}

static bool module_kif_leaf(const uint8_t module_id[32], char *output) {
    char hex[65];
    bytes_to_hex(module_id, 32u, hex);
    return snprintf(output, 80u, "m-%s.kif", hex) == 70;
}

/*
 * Atomic file replacement: write a fresh temporary beside the destination,
 * flush it, then rename. A reader therefore never observes a partially
 * committed manifest or interface.
 */
static bool write_file_atomic(
    const char *path,
    const uint8_t *bytes,
    size_t length
) {
    char temporary[INCREMENTAL_PATH_LIMIT + 16u];
    int descriptor;
    ssize_t written;
    size_t offset = 0u;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) < 0) return false;
    remove(temporary);
    descriptor = open(temporary, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (descriptor < 0) return false;
    while (offset < length) {
        written = write(descriptor, bytes + offset, length - offset);
        if (written < 0) {
            close(descriptor);
            remove(temporary);
            return false;
        }
        offset += (size_t)written;
    }
    if (fsync(descriptor) != 0 || close(descriptor) != 0) {
        remove(temporary);
        return false;
    }
    if (rename(temporary, path) != 0) {
        remove(temporary);
        return false;
    }
    return true;
}

static bool read_file_bounded(
    const char *path,
    size_t limit,
    uint8_t **bytes,
    size_t *length
) {
    FILE *handle = fopen(path, "rb");
    long size;
    uint8_t *buffer;
    *bytes = NULL;
    *length = 0u;
    if (handle == NULL) return false;
    if (fseek(handle, 0, SEEK_END) != 0) {
        fclose(handle);
        return false;
    }
    size = ftell(handle);
    if (size < 0 || (size_t)size > limit || fseek(handle, 0, SEEK_SET) != 0) {
        fclose(handle);
        return false;
    }
    buffer = malloc((size_t)size + 1u);
    if (buffer == NULL) {
        fclose(handle);
        return false;
    }
    if ((size_t)size != 0u && fread(buffer, 1u, (size_t)size, handle) != (size_t)size) {
        free(buffer);
        fclose(handle);
        return false;
    }
    fclose(handle);
    buffer[(size_t)size] = '\0';
    *bytes = buffer;
    *length = (size_t)size;
    return true;
}

static bool ensure_directory(const char *path) {
    struct stat info;
    if (stat(path, &info) == 0) return S_ISDIR(info.st_mode);
    if (mkdir(path, 0700) == 0) return true;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

/*
 * Create every missing component of the cache directory. The cache root is a
 * build output, so the helper owns creating it rather than requiring a caller
 * to pre-make the tree.
 */
static bool ensure_directory_tree(const char *path) {
    char buffer[INCREMENTAL_PATH_LIMIT];
    size_t index;
    size_t length = strlen(path);
    if (length == 0u || length >= sizeof(buffer)) return false;
    memcpy(buffer, path, length + 1u);
    for (index = 1u; index < length; index += 1u) {
        if (buffer[index] != '/') continue;
        buffer[index] = '\0';
        if (!ensure_directory(buffer)) return false;
        buffer[index] = '/';
    }
    return ensure_directory(buffer);
}

/* --------------------------------------------------------- text buffering */

typedef struct {
    char *bytes;
    size_t length;
    size_t capacity;
    bool failed;
} LineBuffer;

static void line_buffer_release(LineBuffer *buffer) {
    free(buffer->bytes);
    memset(buffer, 0, sizeof(*buffer));
}

static void line_buffer_append(LineBuffer *buffer, const char *text) {
    size_t needed = strlen(text);
    if (buffer->failed) return;
    if (buffer->length + needed + 1u > buffer->capacity) {
        size_t capacity = buffer->capacity == 0u ? 4096u : buffer->capacity;
        char *grown;
        while (capacity < buffer->length + needed + 1u) {
            if (capacity > INCREMENTAL_MANIFEST_BYTE_LIMIT) {
                buffer->failed = true;
                return;
            }
            capacity *= 2u;
        }
        grown = realloc(buffer->bytes, capacity);
        if (grown == NULL) {
            buffer->failed = true;
            return;
        }
        buffer->bytes = grown;
        buffer->capacity = capacity;
    }
    if (buffer->bytes == NULL) {
        buffer->failed = true;
        return;
    }
    memcpy(buffer->bytes + buffer->length, text, needed);
    buffer->length += needed;
    buffer->bytes[buffer->length] = '\0';
}

static void line_buffer_appendf(LineBuffer *buffer, const char *format, ...) {
    char line[INCREMENTAL_MANIFEST_LINE_LIMIT];
    va_list arguments;
    int written;
    if (buffer->failed) return;
    va_start(arguments, format);
    written = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= sizeof(line)) {
        buffer->failed = true;
        return;
    }
    line_buffer_append(buffer, line);
}

/* ------------------------------------------------------------ edge digest */

/*
 * A module's outgoing edge set is part of its cache key. Removing a selected
 * import or a re-export changes this digest even when the importing module's
 * own interface digests are untouched, so the decision never silently reuses a
 * module whose dependency shape moved.
 *
 * Edges are hashed in canonical order with explicit kind tags and length
 * framing. Source discovery order and host paths are excluded.
 */
static void hash_framed(KofunSha256 *context, const uint8_t *bytes, size_t length) {
    uint8_t header[8];
    size_t index;
    for (index = 0u; index < 8u; index += 1u) {
        header[index] = (uint8_t)((uint64_t)length >> (56u - index * 8u));
    }
    kofun_sha256_update(context, header, sizeof(header));
    kofun_sha256_update(context, bytes, length);
}

typedef struct {
    uint8_t kind;
    uint8_t form_tag;
    uint8_t target_module[32];
    uint8_t binding_id[32];
    uint8_t target_symbol[32];
} OutgoingEdge;

static int compare_outgoing_edges(const void *left, const void *right) {
    const OutgoingEdge *a = left;
    const OutgoingEdge *b = right;
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    if (a->form_tag != b->form_tag) return a->form_tag < b->form_tag ? -1 : 1;
    if (memcmp(a->target_module, b->target_module, 32u) != 0) {
        return memcmp(a->target_module, b->target_module, 32u) < 0 ? -1 : 1;
    }
    if (memcmp(a->binding_id, b->binding_id, 32u) != 0) {
        return memcmp(a->binding_id, b->binding_id, 32u) < 0 ? -1 : 1;
    }
    return memcmp(a->target_symbol, b->target_symbol, 32u);
}

static bool collect_outgoing_edges(
    IncrementalGraph *graph,
    size_t module_index,
    OutgoingEdge **edges,
    size_t *edge_count
) {
    ReExportResolver *resolver = &graph->resolver;
    ImportResolver *qualified = &resolver->imports.qualified;
    Program *program = &qualified->program;
    size_t capacity = 0u;
    size_t count = 0u;
    size_t index;
    OutgoingEdge *buffer;
    *edges = NULL;
    *edge_count = 0u;
    for (index = 0u; index < qualified->import_count; index += 1u) {
        if (qualified->imports[index].importer_index == module_index) capacity += 1u;
    }
    for (index = 0u; index < resolver->export_count; index += 1u) {
        ResolvedExport *item = &resolver->exports[index];
        if (resolver->declarations[item->declaration_index].exporter_index ==
            module_index) {
            capacity += 1u;
        }
    }
    if (capacity > INCREMENTAL_EDGE_LIMIT) return false;
    if (capacity == 0u) return true;
    buffer = calloc(capacity, sizeof(*buffer));
    if (buffer == NULL) return false;
    for (index = 0u; index < qualified->import_count; index += 1u) {
        ImportBinding *binding = &qualified->imports[index];
        OutgoingEdge *edge;
        if (binding->importer_index != module_index) continue;
        edge = &buffer[count++];
        edge->kind = 1u; /* import */
        edge->form_tag = binding->form_tag;
        memcpy(edge->target_module,
            program->modules[binding->target_index].module_id, 32u);
        memcpy(edge->binding_id, binding->binding_id, 32u);
    }
    for (index = 0u; index < resolver->export_count; index += 1u) {
        ResolvedExport *item = &resolver->exports[index];
        OutgoingEdge *edge;
        if (resolver->declarations[item->declaration_index].exporter_index !=
            module_index) {
            continue;
        }
        edge = &buffer[count++];
        edge->kind = 2u; /* re-export */
        edge->form_tag = 0u;
        memcpy(edge->target_module,
            program->modules[item->target_module_index].module_id, 32u);
        memcpy(edge->binding_id, item->export_binding_id, 32u);
        memcpy(edge->target_symbol, item->target_symbol_id, 32u);
    }
    if (count > 1u) qsort(buffer, count, sizeof(*buffer), compare_outgoing_edges);
    *edges = buffer;
    *edge_count = count;
    return true;
}

static bool compute_edge_digest(
    IncrementalGraph *graph,
    size_t module_index,
    uint8_t digest[32]
) {
    OutgoingEdge *edges = NULL;
    size_t edge_count = 0u;
    size_t index;
    KofunSha256 context;
    static const char domain[] = "kofun.incremental.edges/v1";
    if (!collect_outgoing_edges(graph, module_index, &edges, &edge_count)) {
        return false;
    }
    kofun_sha256_init(&context);
    hash_framed(&context, (const uint8_t *)domain, sizeof(domain) - 1u);
    for (index = 0u; index < edge_count; index += 1u) {
        OutgoingEdge *edge = &edges[index];
        uint8_t tags[2];
        tags[0] = edge->kind;
        tags[1] = edge->form_tag;
        hash_framed(&context, tags, sizeof(tags));
        hash_framed(&context, edge->target_module, 32u);
        hash_framed(&context, edge->binding_id, 32u);
        hash_framed(&context, edge->target_symbol, 32u);
    }
    kofun_sha256_finish(&context, digest);
    free(edges);
    return true;
}

/* ------------------------------------------------------------- cache read */

static void cache_miss(CachedGraph *cache, const char *reason) {
    cache->state = CACHE_STATE_MISS;
    cache->miss_reason = reason;
    cache->module_count = 0u;
}

static bool manifest_field(char **cursor, char **field) {
    char *start = *cursor;
    char *space;
    if (start == NULL || *start == '\0') return false;
    space = strchr(start, ' ');
    if (space != NULL) {
        *space = '\0';
        *cursor = space + 1;
    } else {
        *cursor = start + strlen(start);
    }
    *field = start;
    return **field != '\0';
}

/*
 * Read a stored graph. Any unknown schema, malformed record, exceeded limit,
 * or digest mismatch degrades to a bounded cache miss: the caller then treats
 * every module as executed. A corrupt cache is never trusted and never fatal.
 */
static void load_cached_graph(
    CachedGraph *cache,
    const char *directory,
    const uint8_t package_id[32]
) {
    char path[INCREMENTAL_PATH_LIMIT];
    uint8_t *bytes = NULL;
    size_t length = 0u;
    char *cursor;
    char *line;
    bool seen_schema = false;
    bool seen_package = false;
    memset(cache, 0, sizeof(*cache));
    cache->state = CACHE_STATE_COLD;
    if (!join_cache_path(path, sizeof(path), directory, "manifest")) {
        cache_miss(cache, "cache-path-too-long");
        return;
    }
    if (!read_file_bounded(path, INCREMENTAL_MANIFEST_BYTE_LIMIT, &bytes, &length)) {
        struct stat info;
        if (stat(path, &info) == 0) {
            cache_miss(cache, "manifest-unreadable-or-oversized");
        }
        return;
    }
    cursor = (char *)bytes;
    while ((line = cursor) != NULL && *line != '\0') {
        char *newline = strchr(line, '\n');
        char *field;
        char *rest;
        if (newline != NULL) {
            *newline = '\0';
            cursor = newline + 1;
        } else {
            cursor = line + strlen(line);
        }
        if (*line == '\0') continue;
        rest = line;
        if (!manifest_field(&rest, &field)) {
            cache_miss(cache, "manifest-malformed-record");
            break;
        }
        if (strcmp(field, "schema") == 0) {
            if (seen_schema || !manifest_field(&rest, &field) ||
                strcmp(field, INCREMENTAL_GRAPH_SCHEMA) != 0) {
                cache_miss(cache, "manifest-unknown-schema");
                break;
            }
            seen_schema = true;
            continue;
        }
        if (!seen_schema) {
            cache_miss(cache, "manifest-unknown-schema");
            break;
        }
        if (strcmp(field, "package") == 0) {
            uint8_t stored[32];
            if (seen_package || !manifest_field(&rest, &field) ||
                !parse_identity(field, stored)) {
                cache_miss(cache, "manifest-malformed-record");
                break;
            }
            if (!digests_equal(stored, package_id)) {
                cache_miss(cache, "manifest-package-mismatch");
                break;
            }
            seen_package = true;
            continue;
        }
        if (strcmp(field, "module") == 0) {
            CachedModule *entry;
            if (!seen_package) {
                cache_miss(cache, "manifest-malformed-record");
                break;
            }
            if (cache->module_count >= INCREMENTAL_MODULE_LIMIT) {
                cache_miss(cache, "manifest-module-limit");
                break;
            }
            entry = &cache->modules[cache->module_count];
            memset(entry, 0, sizeof(*entry));
            if (!manifest_field(&rest, &field) ||
                !parse_identity(field, entry->module_id) ||
                !manifest_field(&rest, &field) ||
                !parse_identity(field, entry->source_digest) ||
                !manifest_field(&rest, &field) ||
                !parse_identity(field, entry->public_digest) ||
                !manifest_field(&rest, &field) ||
                !parse_identity(field, entry->internal_digest) ||
                !manifest_field(&rest, &field) ||
                !parse_identity(field, entry->edge_digest) ||
                !manifest_field(&rest, &field) ||
                !parse_identity(field, entry->kif_digest)) {
                cache_miss(cache, "manifest-malformed-record");
                break;
            }
            /*
             * The logical path is the final field and is taken verbatim to the
             * end of the record. Splitting it on a space would silently
             * truncate a path that legitimately contains one.
             */
            if (strlen(rest) > LOGICAL_PATH_LIMIT || !logical_path_is_valid(rest)) {
                cache_miss(cache, "manifest-malformed-record");
                break;
            }
            memcpy(entry->logical_path, rest, strlen(rest) + 1u);
            cache->module_count += 1u;
            continue;
        }
        cache_miss(cache, "manifest-unknown-record");
        break;
    }
    free(bytes);
    if (cache->state == CACHE_STATE_MISS) return;
    if (!seen_schema || !seen_package) {
        cache_miss(cache, "manifest-incomplete");
        return;
    }
    memcpy(cache->package_id, package_id, 32u);
    cache->state = CACHE_STATE_WARM;
}

static const CachedModule *find_cached_module(
    const CachedGraph *cache,
    const uint8_t module_id[32]
) {
    size_t index;
    if (cache->state != CACHE_STATE_WARM) return NULL;
    for (index = 0u; index < cache->module_count; index += 1u) {
        if (digests_equal(cache->modules[index].module_id, module_id)) {
            return &cache->modules[index];
        }
    }
    return NULL;
}

/*
 * A reused module must still prove its stored interface bytes are intact.
 * A missing, oversized, or mutated blob demotes the module to executed.
 */
static bool cached_kif_is_intact(
    const char *directory,
    const uint8_t module_id[32],
    const uint8_t expected_digest[32]
) {
    char leaf[80];
    char path[INCREMENTAL_PATH_LIMIT];
    uint8_t *bytes = NULL;
    size_t length = 0u;
    uint8_t actual[32];
    bool intact;
    if (!module_kif_leaf(module_id, leaf)) return false;
    if (!join_cache_path(path, sizeof(path), directory, leaf)) return false;
    if (!read_file_bounded(path, INCREMENTAL_KIF_BYTE_LIMIT, &bytes, &length)) {
        return false;
    }
    kofun_sha256(bytes, length, actual);
    intact = digests_equal(actual, expected_digest);
    free(bytes);
    return intact;
}

/* ---------------------------------------------------------- decision pass */

/*
 * Order modules so that every import target is decided before its importer.
 * Import cycles are already rejected upstream, so a stable Kahn ordering over
 * the canonical (logical-path sorted) module list terminates. Any module left
 * over by an unexpected cycle is appended in canonical order rather than
 * dropped, so the pass stays total.
 */
static void order_modules_dependencies_first(IncrementalGraph *graph) {
    ImportResolver *qualified = &graph->resolver.imports.qualified;
    Program *program = &qualified->program;
    bool placed[INCREMENTAL_MODULE_LIMIT];
    size_t index;
    size_t pass;
    memset(placed, 0, sizeof(placed));
    graph->order_count = 0u;
    for (pass = 0u; pass < program->module_count; pass += 1u) {
        bool progressed = false;
        for (index = 0u; index < program->module_count; index += 1u) {
            size_t edge;
            bool ready = true;
            if (placed[index]) continue;
            for (edge = 0u; edge < qualified->import_count; edge += 1u) {
                ImportBinding *binding = &qualified->imports[edge];
                if (binding->importer_index != index) continue;
                if (binding->target_index == index) continue;
                if (!placed[binding->target_index]) {
                    ready = false;
                    break;
                }
            }
            if (!ready) continue;
            graph->order[graph->order_count++] = index;
            placed[index] = true;
            progressed = true;
        }
        if (!progressed) break;
    }
    for (index = 0u; index < program->module_count; index += 1u) {
        if (!placed[index]) graph->order[graph->order_count++] = index;
    }
}

/*
 * Apply this issue's invalidation rules to one decided upstream module.
 *
 * A changed public digest invalidates every consumer. A changed
 * package-internal digest invalidates same-package consumers only; an external
 * consumer reads the public view and stays reusable. Unchanged digests
 * propagate nothing, which is what lets a private body edit stop at its own
 * module.
 */
static void propagate_invalidation(
    IncrementalGraph *graph,
    size_t upstream_index
) {
    ImportResolver *qualified = &graph->resolver.imports.qualified;
    Program *program = &qualified->program;
    ModuleState *upstream = &graph->states[upstream_index];
    size_t edge;
    if (!upstream->public_changed && !upstream->internal_changed) return;
    for (edge = 0u; edge < qualified->import_count; edge += 1u) {
        ImportBinding *binding = &qualified->imports[edge];
        ModuleState *consumer;
        bool same_package;
        if (binding->target_index != upstream_index) continue;
        if (binding->importer_index == upstream_index) continue;
        consumer = &graph->states[binding->importer_index];
        /*
         * Topological order guarantees consumers are still undecided here.
         * Keeping the first recorded cause makes the reported reason stable.
         */
        if (consumer->decided || consumer->forced) continue;
        same_package = digests_equal(
            program->modules[binding->importer_index].package_id,
            program->modules[upstream_index].package_id);
        if (upstream->public_changed) {
            consumer->reason = "public-digest-changed";
        } else if (upstream->internal_changed && same_package) {
            consumer->reason = "internal-digest-changed";
        } else {
            continue;
        }
        consumer->forced = true;
        consumer->reason_source =
            qualified->modules[upstream_index].declared_path;
    }
}

/*
 * Derive one module's interface and publish it into the cache. This is the
 * work a reused module skips.
 */
static bool execute_module(
    IncrementalGraph *graph,
    size_t module_index,
    ModuleState *state
) {
    ReExportResolver *resolver = &graph->resolver;
    Program *program = &resolver->imports.qualified.program;
    KofunKifInterface interface;
    KifWriteResult result;
    char leaf[80];
    char path[INCREMENTAL_PATH_LIMIT];
    uint8_t *bytes = NULL;
    size_t length = 0u;
    memset(&interface, 0, sizeof(interface));
    if (!module_kif_leaf(program->modules[module_index].module_id, leaf) ||
        !join_cache_path(path, sizeof(path), graph->cache_directory, leaf)) {
        set_error(program, "E2S94", "incremental cache path exceeds its limit");
        return false;
    }
    if (!build_facade_interface(resolver, module_index, &interface)) {
        destroy_facade_interface(&interface);
        return false;
    }
    result = kofun_kif_write(&interface, path);
    destroy_facade_interface(&interface);
    if (result.status != KOFUN_KIF_OK) {
        set_error(program,
            result.status == KOFUN_KIF_IO_FAILURE ? "E2S92" : "E2S91",
            "incremental interface transaction failed: %s", result.message);
        return false;
    }
    memcpy(state->public_digest, result.public_semantic_digest, 32u);
    memcpy(state->internal_digest, result.package_internal_semantic_digest, 32u);
    if (!read_file_bounded(path, INCREMENTAL_KIF_BYTE_LIMIT, &bytes, &length)) {
        set_error(program, "E2S92",
            "incremental interface could not be read back for hashing");
        return false;
    }
    kofun_sha256(bytes, length, state->kif_digest);
    free(bytes);
    state->digests_known = true;
    return true;
}

static bool decide_modules(IncrementalGraph *graph) {
    ReExportResolver *resolver = &graph->resolver;
    Program *program = &resolver->imports.qualified.program;
    size_t position;
    for (position = 0u; position < graph->order_count; position += 1u) {
        size_t module_index = graph->order[position];
        Module *module = &program->modules[module_index];
        ModuleState *state = &graph->states[module_index];
        const CachedModule *cached;
        kofun_sha256((const uint8_t *)module->source, module->source_length,
            state->source_digest);
        if (!compute_edge_digest(graph, module_index, state->edge_digest)) {
            set_error(program, "E2S94",
                "incremental edge set for `%s` exceeds its limit",
                module->logical_path);
            return false;
        }
        cached = find_cached_module(&graph->cache, module->module_id);
        if (graph->cache.state == CACHE_STATE_COLD) {
            state->outcome = NODE_EXECUTED;
            state->reason = "cold-cache";
        } else if (graph->cache.state == CACHE_STATE_MISS) {
            state->outcome = NODE_EXECUTED;
            state->reason = graph->cache.miss_reason;
        } else if (state->forced) {
            /* An upstream module already invalidated this one. */
            state->outcome = NODE_EXECUTED;
        } else if (cached == NULL) {
            state->outcome = NODE_EXECUTED;
            state->reason = "module-not-cached";
        } else if (!digests_equal(state->source_digest, cached->source_digest)) {
            state->outcome = NODE_EXECUTED;
            state->reason = "source-changed";
        } else if (!digests_equal(state->edge_digest, cached->edge_digest)) {
            state->outcome = NODE_EXECUTED;
            state->reason = "edges-changed";
        } else if (!cached_kif_is_intact(graph->cache_directory,
                       module->module_id, cached->kif_digest)) {
            state->outcome = NODE_EXECUTED;
            state->reason = "cached-interface-unusable";
        } else {
            state->outcome = NODE_REUSED;
            state->reason = "unchanged";
            memcpy(state->public_digest, cached->public_digest, 32u);
            memcpy(state->internal_digest, cached->internal_digest, 32u);
            memcpy(state->kif_digest, cached->kif_digest, 32u);
            state->digests_known = true;
        }
        state->decided = true;
        if (state->outcome == NODE_EXECUTED) {
            if (!execute_module(graph, module_index, state)) return false;
            if (cached != NULL) {
                state->public_changed =
                    !digests_equal(state->public_digest, cached->public_digest);
                state->internal_changed =
                    !digests_equal(state->internal_digest, cached->internal_digest);
            } else {
                /* Never cached: assume the worst rather than reuse a consumer. */
                state->public_changed = true;
                state->internal_changed = true;
            }
            /*
             * A cold or missed cache already executes every module, so
             * propagating from it would only overwrite the true reason.
             */
            if (graph->cache.state == CACHE_STATE_WARM) {
                propagate_invalidation(graph, module_index);
            }
            graph->executed_count += 1u;
        } else {
            graph->reused_count += 1u;
        }
    }
    return true;
}

/* --------------------------------------------------------------- outputs */

static bool commit_manifest(IncrementalGraph *graph) {
    Program *program = &graph->resolver.imports.qualified.program;
    LineBuffer buffer;
    char path[INCREMENTAL_PATH_LIMIT];
    char package_hex[65];
    size_t index;
    bool committed;
    memset(&buffer, 0, sizeof(buffer));
    if (!join_cache_path(path, sizeof(path), graph->cache_directory, "manifest")) {
        set_error(program, "E2S94", "incremental manifest path exceeds its limit");
        return false;
    }
    bytes_to_hex(program->modules[0].package_id, 32u, package_hex);
    line_buffer_appendf(&buffer, "schema %s\n", INCREMENTAL_GRAPH_SCHEMA);
    line_buffer_appendf(&buffer, "package %s\n", package_hex);
    for (index = 0u; index < program->module_count; index += 1u) {
        Module *module = &program->modules[index];
        ModuleState *state = &graph->states[index];
        char module_hex[65];
        char source_hex[65];
        char public_hex[65];
        char internal_hex[65];
        char edge_hex[65];
        char kif_hex[65];
        bytes_to_hex(module->module_id, 32u, module_hex);
        bytes_to_hex(state->source_digest, 32u, source_hex);
        bytes_to_hex(state->public_digest, 32u, public_hex);
        bytes_to_hex(state->internal_digest, 32u, internal_hex);
        bytes_to_hex(state->edge_digest, 32u, edge_hex);
        bytes_to_hex(state->kif_digest, 32u, kif_hex);
        line_buffer_appendf(&buffer, "module %s %s %s %s %s %s %s\n",
            module_hex, source_hex, public_hex, internal_hex, edge_hex,
            kif_hex, module->logical_path);
    }
    if (buffer.failed) {
        line_buffer_release(&buffer);
        set_error(program, "E2S94", "incremental manifest exceeds its byte limit");
        return false;
    }
    committed = write_file_atomic(path, (const uint8_t *)buffer.bytes,
        buffer.length);
    line_buffer_release(&buffer);
    if (!committed) {
        set_error(program, "E2S92", "incremental manifest could not be committed");
        return false;
    }
    return true;
}

/*
 * The report is the gate's evidence. It names the exact executed and reused
 * node sets in canonical module order and exposes each module's public digest
 * so an external, source-free consumer's reuse decision is checkable too.
 *
 * Every record puts its fixed-width fields first and the logical path last,
 * because a logical path may legitimately contain a space. A reader therefore
 * takes the path as the remainder of the record rather than as one field.
 */
static bool commit_report(IncrementalGraph *graph, const char *path) {
    Program *program = &graph->resolver.imports.qualified.program;
    LineBuffer buffer;
    size_t index;
    bool committed;
    const char *cache_state = "cold";
    memset(&buffer, 0, sizeof(buffer));
    if (graph->cache.state == CACHE_STATE_WARM) cache_state = "warm";
    if (graph->cache.state == CACHE_STATE_MISS) cache_state = "miss";
    line_buffer_appendf(&buffer, "schema %s\n", INCREMENTAL_REPORT_SCHEMA);
    line_buffer_appendf(&buffer, "cache %s\n", cache_state);
    if (graph->cache.state == CACHE_STATE_MISS) {
        line_buffer_appendf(&buffer, "cache-miss-reason %s\n",
            graph->cache.miss_reason);
    }
    for (index = 0u; index < program->module_count; index += 1u) {
        ModuleState *state = &graph->states[index];
        char public_hex[65];
        char internal_hex[65];
        bytes_to_hex(state->public_digest, 32u, public_hex);
        bytes_to_hex(state->internal_digest, 32u, internal_hex);
        line_buffer_appendf(&buffer, "module %s %s %s\n",
            state->outcome == NODE_EXECUTED ? "executed" : "reused",
            state->reason != NULL ? state->reason : "unchanged",
            program->modules[index].logical_path);
        if (state->reason_source != NULL) {
            line_buffer_appendf(&buffer, "cause %s %s\n",
                state->reason_source, program->modules[index].logical_path);
        }
        line_buffer_appendf(&buffer, "public %s %s\n",
            public_hex, program->modules[index].logical_path);
        line_buffer_appendf(&buffer, "internal %s %s\n",
            internal_hex, program->modules[index].logical_path);
    }
    line_buffer_appendf(&buffer, "summary executed=%zu reused=%zu\n",
        graph->executed_count, graph->reused_count);
    if (buffer.failed) {
        line_buffer_release(&buffer);
        set_error(program, "E2S94", "incremental report exceeds its byte limit");
        return false;
    }
    committed = write_file_atomic(path, (const uint8_t *)buffer.bytes,
        buffer.length);
    line_buffer_release(&buffer);
    if (!committed) {
        set_error(program, "E2S92", "incremental report could not be committed");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ entry */

int main(int argc, char **argv) {
    static IncrementalGraph graph;
    ReExportResolver *resolver = &graph.resolver;
    ImportResolver *qualified = &resolver->imports.qualified;
    Program *program = &qualified->program;
    size_t index;
    int status = 1;
    if (argc != 4) {
        fprintf(stderr, "usage: %s INVENTORY CACHE_DIRECTORY REPORT\n", argv[0]);
        return 2;
    }
    memset(&graph, 0, sizeof(graph));
    qualified->extension_context = &resolver->imports;
    if (strlen(argv[2]) >= sizeof(graph.cache_directory)) {
        fprintf(stderr, "error: cache directory path exceeds %u bytes\n",
            (unsigned)sizeof(graph.cache_directory) - 1u);
        return 2;
    }
    memcpy(graph.cache_directory, argv[2], strlen(argv[2]) + 1u);
    if (!ensure_directory_tree(graph.cache_directory)) {
        fprintf(stderr, "error: cannot create cache directory `%s`\n", argv[2]);
        return 2;
    }
    if (!load_qualified_inventory(qualified, argv[1]) ||
        !order_and_validate_inventory(program) ||
        !attach_declared_paths(qualified)) {
        goto done;
    }
    for (index = 0u; index < program->module_count; index += 1u) {
        if (!collect_re_export_module(resolver, index)) goto done;
    }
    compute_identities(program);
    if (!validate_duplicates(program)) goto done;
    if (!resolve_imports(qualified)) {
        add_self_re_export_secondary_span(resolver);
        remap_public_import_error(program);
        goto done;
    }
    if (!validate_ordinary_import_cycles(resolver)) goto done;
    canonicalize_import_graph_edges(&resolver->imports);
    if (!resolve_selective_bindings(&resolver->imports)) goto done;
    if (!build_re_export_requests(resolver) || !resolve_re_exports(resolver)) {
        goto done;
    }
    if (program->module_count == 0u) {
        set_error(program, "E2S49", "inventory declares no module");
        goto done;
    }
    load_cached_graph(&graph.cache, graph.cache_directory,
        program->modules[0].package_id);
    if (graph.cache.state == CACHE_STATE_MISS) {
        incremental_note("note: incremental cache miss (%s); recomputing",
            graph.cache.miss_reason);
    }
    order_modules_dependencies_first(&graph);
    if (!decide_modules(&graph)) goto done;
    /*
     * A failed decision never reaches this point, so a failure is never
     * committed as a reusable success.
     */
    if (!commit_manifest(&graph) || !commit_report(&graph, argv[3])) goto done;
    status = 0;
done:
    if (program->failed) {
        printf("%s\n", qualified->expanded_error != NULL
            ? qualified->expanded_error : program->error);
    }
    destroy_re_export_resolver(resolver);
    return status;
}
