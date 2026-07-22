#ifndef KOFUN_STAGE2_SHA256_H
#define KOFUN_STAGE2_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_length;
} KofunSha256;

void kofun_sha256_init(KofunSha256 *context);
void kofun_sha256_update(
    KofunSha256 *context,
    const uint8_t *bytes,
    size_t length
);
void kofun_sha256_finish(KofunSha256 *context, uint8_t digest[32]);
void kofun_sha256(
    const uint8_t *bytes,
    size_t length,
    uint8_t digest[32]
);

#endif
