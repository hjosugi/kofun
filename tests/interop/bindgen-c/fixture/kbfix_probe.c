/*
 * kbfix_probe — the pointer-bearing paths the sanitizer gate exercises from
 * the C side (issue #900).
 *
 * The generated boundary is a Kofun module compiled through the checked C
 * ABI profile, and that profile cannot express a writable buffer or a
 * function pointer: `CStr` accepts only a static string and callbacks are
 * outside the slice (bootstrap/c_abi/README.md). The buffer+length and
 * callback contracts are still library entry points the audit report binds,
 * so they are exercised here instead — against the *same* sanitized
 * libkbfix.so the generated driver links, in the same gate run.
 *
 * What this file is not: a second copy of driver.kofun. The paths the
 * generated boundary can cross — opaque handle create/destroy, status
 * returns, the by-value record round trip, and the library-owned string —
 * are crossed by the generated driver and compared against driver.stdout.
 * This file covers only what that driver cannot reach, and the gate asserts
 * every function it calls is a bound symbol in the audit report.
 *
 * Every buffer here is heap-allocated at exactly the size the library is
 * told about, so a one-byte overrun inside libkbfix.so is a
 * heap-buffer-overflow rather than a write into unused slack. Deterministic:
 * no clock, no environment, no input.
 */
#include "kbfix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long callback_invocations = 0;

static void on_change(void *context, long value) {
    long *seen = (long *)context;
    callback_invocations += 1;
    *seen = value;
}

int main(void) {
    kbfix_counter_t *counter;
    unsigned long length = 0;
    unsigned long exact;
    char *buffer;
    long seen = -1;
    enum kbfix_status status;

    counter = kbfix_counter_new(7);
    if (counter == NULL) {
        return 2;
    }

    /* Callback with a declared lifetime: registered, invoked synchronously
     * inside kbfix_counter_add, and not retained past kbfix_counter_free. */
    status = kbfix_counter_on_change(counter, on_change, &seen);
    printf("on_change registered %d\n", (int)status);
    status = kbfix_counter_add(counter, 5);
    printf("add %d value %ld\n", (int)status, kbfix_counter_value(counter));
    printf("callback invocations %ld seen %ld\n", callback_invocations, seen);

    /* Buffer + explicit length, capacity deliberately too small: the library
     * must report the required length and must not write into the buffer. A
     * one-byte allocation makes any write a heap-buffer-overflow. */
    buffer = malloc(1U);
    if (buffer == NULL) {
        kbfix_counter_free(counter);
        return 2;
    }
    buffer[0] = '#';
    status = kbfix_label_copy(counter, buffer, 1UL, &length);
    printf("short copy %d length %lu untouched %d\n",
           (int)status, length, buffer[0] == '#');
    free(buffer);

    /* Buffer + explicit length, capacity exactly what the library asked for.
     * The allocation is sized from the length the library just reported, so
     * an off-by-one in either direction is a sanitizer report. */
    exact = length + 1U;
    buffer = malloc(exact);
    if (buffer == NULL) {
        kbfix_counter_free(counter);
        return 2;
    }
    status = kbfix_label_copy(counter, buffer, exact, &length);
    printf("exact copy %d length %lu text %s\n", (int)status, length, buffer);
    printf("terminated %d\n", (int)strlen(buffer) == (int)length);
    free(buffer);

    /* Refusals: a NULL handle and a NULL out-length must be rejected without
     * dereferencing anything. */
    printf("null handle %d\n", (int)kbfix_label_copy(NULL, NULL, 0UL, &length));
    printf("null length %d\n", (int)kbfix_label_copy(counter, NULL, 0UL, NULL));

    /* Client-owned cleanup. Released exactly once; LeakSanitizer runs at
     * exit and a missed release fails the gate. */
    kbfix_counter_free(counter);

    /* free(NULL) is a documented no-op in the header's contract. */
    kbfix_counter_free(NULL);
    return 0;
}
