#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    int64_t length;
    int64_t elements[3];
} IntList;

static int64_t list_index(const IntList *list, int64_t index) {
    if (index < 0) index += list->length;
    if (index < 0 || index >= list->length) return INT64_MIN;
    return list->elements[index];
}

int main(void) {
    const IntList values = {
        .length = 3,
        .elements = {10, 20 + 22, 99},
    };
    printf("%" PRId64 "\n", list_index(&values, -2));
    return 0;
}
