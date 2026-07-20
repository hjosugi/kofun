#ifndef KOFUN_UNICODE_SHIM_H
#define KOFUN_UNICODE_SHIM_H

#include <stddef.h>

#define KOFUN_UNICODE_OK 0
#define KOFUN_UNICODE_INVALID_UTF8 1
#define KOFUN_UNICODE_PANIC 2
#define KOFUN_UNICODE_NULL_BUFFER 3

typedef struct KofunGraphemeResult {
    int status;
    size_t count;
    size_t error_offset;
} KofunGraphemeResult;

/*
 * `bytes` is borrowed for this call only. It must be readable for `length`
 * bytes. The shim never retains, modifies, or frees it.
 */
KofunGraphemeResult kofun_unicode_grapheme_count(
    const void *bytes,
    size_t length
);

/* Acceptance-only proof that a Rust panic is caught before returning to C. */
int kofun_unicode_panic_probe(void);

#endif
