#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int64_t length;
    int64_t elements[8];
} IntList;

static int64_t list_index(const IntList *list, int64_t index) {
    if (index < 0) index += list->length;
    if (index < 0 || index >= list->length) return INT64_MIN;
    return list->elements[index];
}

static IntList map_times(const IntList *source, int64_t multiplier) {
    IntList result = {.length = source->length};
    for (int64_t index = 0; index < source->length; ++index) {
        result.elements[index] = source->elements[index] * multiplier;
    }
    return result;
}

static IntList filter_greater_than(
    const IntList *source,
    int64_t threshold
) {
    IntList result = {0};
    for (int64_t index = 0; index < source->length; ++index) {
        int64_t value = source->elements[index];
        if (value > threshold) {
            result.elements[result.length++] = value;
        }
    }
    return result;
}

static IntList filter_less_than(
    const IntList *source,
    int64_t threshold
) {
    IntList result = {0};
    for (int64_t index = 0; index < source->length; ++index) {
        int64_t value = source->elements[index];
        if (value < threshold) {
            result.elements[result.length++] = value;
        }
    }
    return result;
}

static int64_t fold_sum(const IntList *source, int64_t initial) {
    int64_t result = initial;
    for (int64_t index = 0; index < source->length; ++index) {
        result += source->elements[index];
    }
    return result;
}

static int64_t observe(const char *mode) {
    if (strcmp(mode, "index-negative") == 0) {
        const IntList values = {
            .length = 3,
            .elements = {10, 20 + 22, 99},
        };
        return list_index(&values, -2);
    }
    if (strcmp(mode, "binding") == 0) {
        const IntList values = {
            .length = 3,
            .elements = {10, 42, 99},
        };
        return list_index(&values, 1);
    }
    if (strcmp(mode, "length") == 0) {
        const IntList values = {
            .length = 3,
            .elements = {7, 8, 9},
        };
        return values.length + 39;
    }
    if (strcmp(mode, "map") == 0) {
        const IntList values = {
            .length = 3,
            .elements = {1, 2, 3},
        };
        IntList mapped = map_times(&values, 21);
        return list_index(&mapped, 2);
    }
    if (strcmp(mode, "filter") == 0) {
        const IntList values = {
            .length = 4,
            .elements = {1, 2, 42, 99},
        };
        IntList filtered = filter_greater_than(&values, 2);
        return list_index(&filtered, 1);
    }
    if (strcmp(mode, "fold") == 0) {
        const IntList values = {
            .length = 3,
            .elements = {10, 20, 3},
        };
        return fold_sum(&values, 8);
    }
    if (strcmp(mode, "pipeline") == 0) {
        const IntList values = {
            .length = 3,
            .elements = {1, 2, 3},
        };
        IntList filtered = filter_greater_than(&values, 1);
        IntList mapped = map_times(&filtered, 10);
        return fold_sum(&mapped, 2);
    }
    if (strcmp(mode, "empty-map") == 0) {
        const IntList values = {0};
        IntList mapped = map_times(&values, 2);
        return mapped.length + 60;
    }
    if (strcmp(mode, "empty-filter") == 0) {
        const IntList values = {0};
        IntList filtered = filter_greater_than(&values, 0);
        return filtered.length + 61;
    }
    if (strcmp(mode, "empty-fold") == 0) {
        const IntList values = {0};
        return fold_sum(&values, 62);
    }
    if (strcmp(mode, "all-false") == 0) {
        const IntList values = {
            .length = 3,
            .elements = {1, 2, 3},
        };
        IntList filtered = filter_less_than(&values, 0);
        return filtered.length + 63;
    }
    if (strcmp(mode, "negative-predicate") == 0) {
        const IntList values = {
            .length = 4,
            .elements = {-2, -1, 0, 1},
        };
        IntList filtered = filter_less_than(&values, 0);
        return filtered.length + 62;
    }
    return INT64_MIN;
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "index-negative";
    int64_t result = observe(mode);
    if (result == INT64_MIN) {
        fprintf(stderr, "unknown list reference mode: %s\n", mode);
        return 2;
    }
    printf("%" PRId64 "\n", result);
    return 0;
}
