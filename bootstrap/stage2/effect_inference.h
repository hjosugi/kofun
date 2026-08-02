#ifndef KOFUN_STAGE2_EFFECT_INFERENCE_H
#define KOFUN_STAGE2_EFFECT_INFERENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * The executable Stage 2 profile admits at most 64 top-level functions.  Keep
 * the effect graph on that same bound so inference needs no allocation and a
 * malformed producer graph fails closed instead of dropping an edge.
 */
#define KOFUN_EFFECT_MAX_FUNCTIONS 64u

typedef enum {
    KOFUN_EFFECT_PURE = 0,
    KOFUN_EFFECT_IO = 1
} KofunEffect;

typedef struct {
    size_t function_count;
    const char *names[KOFUN_EFFECT_MAX_FUNCTIONS];
    bool direct_io[KOFUN_EFFECT_MAX_FUNCTIONS];
    bool calls[KOFUN_EFFECT_MAX_FUNCTIONS][KOFUN_EFFECT_MAX_FUNCTIONS];
} KofunEffectGraph;

typedef struct {
    KofunEffect effects[KOFUN_EFFECT_MAX_FUNCTIONS];
    /* SIZE_MAX means either pure or directly rooted in print. */
    size_t forcing_callee[KOFUN_EFFECT_MAX_FUNCTIONS];
} KofunEffectResult;

static bool kofun_effect_graph_valid(const KofunEffectGraph *graph) {
    size_t left;
    size_t right;
    if (graph == NULL || graph->function_count == 0u ||
        graph->function_count > KOFUN_EFFECT_MAX_FUNCTIONS) {
        return false;
    }
    for (left = 0u; left < graph->function_count; left += 1u) {
        if (graph->names[left] == NULL || graph->names[left][0] == '\0') {
            return false;
        }
        for (right = left + 1u;
             right < graph->function_count;
             right += 1u) {
            if (strcmp(graph->names[left], graph->names[right]) == 0) {
                return false;
            }
        }
    }
    return true;
}

/*
 * Infer the least fixed point of pure < io.  The second pass chooses the
 * lexicographically first immediate impure callee only after the fixed point
 * has converged, so explanations do not depend on declaration or traversal
 * order, including inside recursive SCCs.
 */
static bool kofun_effect_infer(
    const KofunEffectGraph *graph,
    KofunEffectResult *result
) {
    bool changed;
    size_t caller;
    size_t callee;
    if (result == NULL || !kofun_effect_graph_valid(graph)) return false;
    memset(result, 0, sizeof(*result));
    for (caller = 0u; caller < graph->function_count; caller += 1u) {
        result->effects[caller] = graph->direct_io[caller] ?
            KOFUN_EFFECT_IO : KOFUN_EFFECT_PURE;
        result->forcing_callee[caller] = SIZE_MAX;
    }
    do {
        changed = false;
        for (caller = 0u; caller < graph->function_count; caller += 1u) {
            if (result->effects[caller] == KOFUN_EFFECT_IO) continue;
            for (callee = 0u;
                 callee < graph->function_count;
                 callee += 1u) {
                if (graph->calls[caller][callee] &&
                    result->effects[callee] == KOFUN_EFFECT_IO) {
                    result->effects[caller] = KOFUN_EFFECT_IO;
                    changed = true;
                    break;
                }
            }
        }
    } while (changed);

    for (caller = 0u; caller < graph->function_count; caller += 1u) {
        size_t selected = SIZE_MAX;
        if (graph->direct_io[caller] ||
            result->effects[caller] == KOFUN_EFFECT_PURE) {
            continue;
        }
        for (callee = 0u; callee < graph->function_count; callee += 1u) {
            if (!graph->calls[caller][callee] ||
                result->effects[callee] != KOFUN_EFFECT_IO) {
                continue;
            }
            if (selected == SIZE_MAX ||
                strcmp(graph->names[callee], graph->names[selected]) < 0) {
                selected = callee;
            }
        }
        if (selected == SIZE_MAX) return false;
        result->forcing_callee[caller] = selected;
    }
    return true;
}

#endif
