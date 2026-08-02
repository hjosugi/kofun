#include "../../bootstrap/stage2/effect_inference.h"

#include <stdio.h>
#include <stdlib.h>

static size_t find_name(const KofunEffectGraph *graph, const char *name) {
    size_t index;
    for (index = 0u; index < graph->function_count; index += 1u) {
        if (strcmp(graph->names[index], name) == 0) return index;
    }
    return SIZE_MAX;
}

static int name_index_compare(const void *left, const void *right, void *data) {
    const KofunEffectGraph *graph = (const KofunEffectGraph *)data;
    size_t left_index = *(const size_t *)left;
    size_t right_index = *(const size_t *)right;
    return strcmp(graph->names[left_index], graph->names[right_index]);
}

int main(int argc, char **argv) {
    FILE *input;
    KofunEffectGraph graph;
    KofunEffectResult result;
    char names[KOFUN_EFFECT_MAX_FUNCTIONS][64];
    char kind[16];
    char left[64];
    char right[64];
    size_t order[KOFUN_EFFECT_MAX_FUNCTIONS];
    size_t index;
    if (argc != 2) return 2;
    input = fopen(argv[1], "rb");
    if (input == NULL) return 2;
    memset(&graph, 0, sizeof(graph));
    while (fscanf(input, "%15s", kind) == 1) {
        if (strcmp(kind, "fn") == 0) {
            unsigned direct;
            if (graph.function_count >= KOFUN_EFFECT_MAX_FUNCTIONS ||
                fscanf(input, "%63s %u", left, &direct) != 2 || direct > 1u) {
                (void)fclose(input);
                return 2;
            }
            (void)snprintf(
                names[graph.function_count],
                sizeof(names[graph.function_count]),
                "%s",
                left
            );
            graph.names[graph.function_count] = names[graph.function_count];
            graph.direct_io[graph.function_count] = direct == 1u;
            graph.function_count += 1u;
        } else if (strcmp(kind, "call") == 0) {
            size_t caller;
            size_t callee;
            if (fscanf(input, "%63s %63s", left, right) != 2) {
                (void)fclose(input);
                return 2;
            }
            caller = find_name(&graph, left);
            callee = find_name(&graph, right);
            if (caller == SIZE_MAX || callee == SIZE_MAX) {
                (void)fclose(input);
                return 2;
            }
            graph.calls[caller][callee] = true;
        } else {
            (void)fclose(input);
            return 2;
        }
    }
    if (fclose(input) != 0 || !kofun_effect_infer(&graph, &result)) return 1;
    for (index = 0u; index < graph.function_count; index += 1u) {
        order[index] = index;
    }
    /* The bounded insertion sort avoids non-portable qsort_r variants. */
    for (index = 1u; index < graph.function_count; index += 1u) {
        size_t cursor = index;
        while (cursor > 0u &&
               name_index_compare(
                   &order[cursor - 1u], &order[cursor], &graph) > 0) {
            size_t temporary = order[cursor - 1u];
            order[cursor - 1u] = order[cursor];
            order[cursor] = temporary;
            cursor -= 1u;
        }
    }
    for (index = 0u; index < graph.function_count; index += 1u) {
        size_t function = order[index];
        if (result.effects[function] == KOFUN_EFFECT_PURE) {
            (void)printf("%s|pure|-\n", graph.names[function]);
        } else if (graph.direct_io[function]) {
            (void)printf("%s|io|print\n", graph.names[function]);
        } else {
            size_t callee = result.forcing_callee[function];
            if (callee >= graph.function_count) return 1;
            (void)printf(
                "%s|io|%s\n",
                graph.names[function],
                graph.names[callee]
            );
        }
    }
    return 0;
}
