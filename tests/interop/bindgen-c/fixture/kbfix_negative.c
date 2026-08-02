/*
 * kbfix_negative — the one negative fixture of the sanitizer gate (#900).
 *
 * This program MUST fail. It exists to prove the sanitizer gate is armed:
 * if it ever exits zero, the instrumentation on libkbfix.so is not doing
 * anything and every green run of the positive paths above it means nothing.
 *
 * The fault is deliberately placed *inside the library*: the caller
 * allocates four bytes and then lies to kbfix_label_copy about the capacity,
 * so the library's own memcpy writes past the end of the allocation. The
 * AddressSanitizer report must therefore name kbfix.c — which is what proves
 * the library side is instrumented, not just the caller side.
 *
 * It links the same sanitized libkbfix.so as the positive programs and is
 * never linked into them, so it cannot weaken the passing path.
 */
#include "kbfix.h"

#include <stdlib.h>

int main(void) {
    kbfix_counter_t *counter = kbfix_counter_new(1);
    char *small = malloc(4U);
    unsigned long length = 0;
    if (counter == NULL || small == NULL) {
        return 2;
    }
    /* The counter's label is "counter=1", ten bytes with its terminator.
     * Claiming 64 bytes of capacity for a 4-byte allocation makes the
     * library's memcpy run off the end of the heap block. */
    (void)kbfix_label_copy(counter, small, 64UL, &length);
    free(small);
    kbfix_counter_free(counter);
    return 0;
}
