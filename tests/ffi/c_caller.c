#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

typedef struct Pair {
    int64_t left;
    int64_t right;
    int64_t tag;
} Pair;

extern int rust_add(int left, int right);
extern long rust_stack_sum(
    long one,
    long two,
    long three,
    long four,
    long five,
    long six,
    long seven,
    long eight
);
extern Pair rust_transform(Pair value);

int main(void) {
    Pair transformed = rust_transform((Pair){40, 0, 0});
    printf("%d\n", rust_add(20, 22));
    printf("%ld\n", rust_stack_sum(1, 2, 3, 4, 5, 6, 7, 8));
    printf("%" PRId64 "\n", transformed.left);
    printf("%" PRId64 "\n", transformed.right);
    printf("%" PRId64 "\n", transformed.tag);
    return 0;
}
