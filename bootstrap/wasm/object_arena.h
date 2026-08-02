#ifndef KOFUN_WASM_OBJECT_ARENA_H
#define KOFUN_WASM_OBJECT_ARENA_H

#include <stddef.h>
#include <stdint.h>

/*
 * wasm32-hostabi1 uses one fixed WebAssembly page.  Offset zero stays the
 * null/failure reference and the first KiB is reserved for compiler-owned
 * data.  The host-owned bump arena therefore begins at the address used by
 * the recomputed v1 boundary vectors.
 */
enum {
    KOFUN_WASM_PAGE_BYTES = 65536,
    KOFUN_WASM_ARENA_BASE = 1024,
    KOFUN_WASM_OBJECT_HEADER_BYTES = 8,
    KOFUN_WASM_HOST_ABI_REVISION = 1
};

/* AggregateLayout v1 pins both Text.byte_length and List.length to one
 * little-endian u64 at object offset zero.  Keeping the encoder here gives
 * later Text/List lowering one implementation instead of a second layout
 * rule. */
static inline void kofun_wasm_write_u64_header(
    uint8_t target[KOFUN_WASM_OBJECT_HEADER_BYTES],
    uint64_t value
) {
    for (size_t index = 0; index < KOFUN_WASM_OBJECT_HEADER_BYTES; ++index) {
        target[index] = (uint8_t)(value & UINT64_C(0xff));
        value >>= 8;
    }
}

#endif
