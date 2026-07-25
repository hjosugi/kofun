/*
 * Independent C reference for the same recursive integer workload. It exists
 * only as a scale marker for the direct native backend and is not part of any
 * Kofun artifact.
 */
#include <stdio.h>

static long fib(long n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    printf("%ld\n", fib(35));
    return 0;
}
