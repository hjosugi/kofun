/*
 * kbfix — implementation of the pinned bindgen-c fixture library.
 *
 * Compiled by the gate into libkbfix.so with clang; no other build reaches
 * this file. Behavior is deterministic: no clock, no environment, no I/O
 * except the variadic kbfix_log, which the gate never calls.
 */
#include "kbfix.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KBFIX_COUNTER_LIMIT 1000000L

struct kbfix_counter {
    long value;
    kbfix_on_change on_change;
    void *on_change_context;
};

kbfix_counter_t *kbfix_counter_new(long initial) {
    kbfix_counter_t *counter;
    if (initial < 0 || initial > KBFIX_COUNTER_LIMIT) {
        return NULL;
    }
    counter = calloc(1U, sizeof(*counter));
    if (counter == NULL) {
        return NULL;
    }
    counter->value = initial;
    return counter;
}

void kbfix_counter_free(kbfix_counter_t *counter) {
    free(counter);
}

enum kbfix_status kbfix_counter_add(kbfix_counter_t *counter, long amount) {
    long next;
    if (counter == NULL) {
        return KBFIX_ERR_NULL;
    }
    next = counter->value + amount;
    if (amount > KBFIX_COUNTER_LIMIT || amount < -KBFIX_COUNTER_LIMIT ||
        next < 0 || next > KBFIX_COUNTER_LIMIT) {
        return KBFIX_ERR_RANGE;
    }
    counter->value = next;
    if (counter->on_change != NULL) {
        counter->on_change(counter->on_change_context, next);
    }
    return KBFIX_OK;
}

long kbfix_counter_value(const kbfix_counter_t *counter) {
    if (counter == NULL) {
        return -1;
    }
    return counter->value;
}

kbfix_stats_t kbfix_stats_scale(kbfix_stats_t stats, long factor) {
    kbfix_stats_t scaled;
    scaled.total = stats.total * factor;
    scaled.events = stats.events * (int)factor;
    scaled.flags = stats.flags;
    return scaled;
}

enum kbfix_status kbfix_label_copy(const kbfix_counter_t *counter,
                                  char *buffer,
                                  unsigned long capacity,
                                  unsigned long *out_length) {
    char rendered[64];
    unsigned long length;
    int written;
    if (counter == NULL || buffer == NULL || out_length == NULL) {
        return KBFIX_ERR_NULL;
    }
    written = snprintf(rendered, sizeof(rendered), "counter=%ld", counter->value);
    if (written < 0) {
        return KBFIX_ERR_RANGE;
    }
    length = (unsigned long)written;
    *out_length = length;
    if (length + 1U > capacity) {
        return KBFIX_ERR_CAPACITY;
    }
    memcpy(buffer, rendered, length + 1U);
    return KBFIX_OK;
}

enum kbfix_status kbfix_counter_on_change(kbfix_counter_t *counter,
                                          kbfix_on_change callback,
                                          void *context) {
    if (counter == NULL) {
        return KBFIX_ERR_NULL;
    }
    counter->on_change = callback;
    counter->on_change_context = context;
    return KBFIX_OK;
}

const char *kbfix_library_name(void) {
    return "kbfix";
}

long kbfix_name_length(const char *name) {
    if (name == NULL) {
        return -1;
    }
    return (long)strlen(name);
}

int kbfix_log(const char *format, ...) {
    va_list arguments;
    int written;
    va_start(arguments, format);
    written = vfprintf(stderr, format, arguments);
    va_end(arguments);
    return written;
}

/* Defined unconditionally so one library serves both preprocessor contexts;
 * without -DKBFIX_EXTRA the declaration, and therefore the binding, is
 * absent. */
long kbfix_extra_probe(long value) {
    return value + 1000;
}
