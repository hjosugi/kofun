#include "sha256.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void u16be(uint8_t output[2], uint16_t value) {
    output[0] = (uint8_t)(value >> 8u);
    output[1] = (uint8_t)value;
}

static void u32be(uint8_t output[4], uint32_t value) {
    output[0] = (uint8_t)(value >> 24u);
    output[1] = (uint8_t)(value >> 16u);
    output[2] = (uint8_t)(value >> 8u);
    output[3] = (uint8_t)value;
}

static void u64be(uint8_t output[8], uint64_t value) {
    output[0] = (uint8_t)(value >> 56u);
    output[1] = (uint8_t)(value >> 48u);
    output[2] = (uint8_t)(value >> 40u);
    output[3] = (uint8_t)(value >> 32u);
    output[4] = (uint8_t)(value >> 24u);
    output[5] = (uint8_t)(value >> 16u);
    output[6] = (uint8_t)(value >> 8u);
    output[7] = (uint8_t)value;
}

static void field(
    KofunSha256 *context,
    uint16_t tag,
    const uint8_t *value,
    size_t length
) {
    uint8_t tag_bytes[2];
    uint8_t length_bytes[4];
    u16be(tag_bytes, tag);
    u32be(length_bytes, (uint32_t)length);
    kofun_sha256_update(context, tag_bytes, sizeof(tag_bytes));
    kofun_sha256_update(context, length_bytes, sizeof(length_bytes));
    kofun_sha256_update(context, value, length);
}

static int nibble(char byte) {
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    return -1;
}

static int identity(const char *text, uint8_t output[32]) {
    size_t index;
    if (strlen(text) != 64u) return 0;
    for (index = 0u; index < 32u; index += 1u) {
        int high = nibble(text[index * 2u]);
        int low = nibble(text[index * 2u + 1u]);
        if (high < 0 || low < 0) return 0;
        output[index] = (uint8_t)((unsigned)high * 16u + (unsigned)low);
    }
    return 1;
}

static int unsigned_integer(const char *text, uint64_t *output) {
    char *end = NULL;
    unsigned long long value;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return 0;
    *output = (uint64_t)value;
    return 1;
}

int main(int argc, char **argv) {
    static const char domain[] = "kofun.id.alias-binding/v1";
    const uint8_t prefix[6] = { 'K', 'O', 'F', 'U', 'N', 0 };
    uint8_t importer[32];
    uint8_t file[32];
    uint8_t target[32];
    uint8_t start_bytes[8];
    uint8_t end_bytes[8];
    uint8_t digest[32];
    uint8_t domain_length[2];
    uint8_t payload_length[4];
    uint64_t start;
    uint64_t end;
    size_t local_length;
    size_t index;
    KofunSha256 context;
    if (argc != 7 || !identity(argv[1], importer) || !identity(argv[2], file) ||
        !unsigned_integer(argv[3], &start) || !unsigned_integer(argv[4], &end) ||
        !identity(argv[6], target)) return 2;
    local_length = strlen(argv[5]);
    u16be(domain_length, (uint16_t)(sizeof(domain) - 1u));
    u32be(payload_length,
        (uint32_t)(36u + 32u + 32u + 8u + 8u + local_length + 32u));
    u64be(start_bytes, start);
    u64be(end_bytes, end);
    kofun_sha256_init(&context);
    kofun_sha256_update(&context, prefix, sizeof(prefix));
    kofun_sha256_update(&context, domain_length, sizeof(domain_length));
    kofun_sha256_update(&context, (const uint8_t *)domain, sizeof(domain) - 1u);
    kofun_sha256_update(&context, payload_length, sizeof(payload_length));
    field(&context, UINT16_C(0x8001), importer, sizeof(importer));
    field(&context, UINT16_C(0x8002), file, sizeof(file));
    field(&context, UINT16_C(0x8003), start_bytes, sizeof(start_bytes));
    field(&context, UINT16_C(0x8004), end_bytes, sizeof(end_bytes));
    field(&context, UINT16_C(0x8005), (const uint8_t *)argv[5], local_length);
    field(&context, UINT16_C(0x8006), target, sizeof(target));
    kofun_sha256_finish(&context, digest);
    for (index = 0u; index < sizeof(digest); index += 1u) {
        printf("%02x", digest[index]);
    }
    fputc('\n', stdout);
    return 0;
}
