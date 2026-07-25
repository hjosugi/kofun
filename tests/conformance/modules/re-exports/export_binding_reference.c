#include "sha256.h"

#include <stdint.h>
#include <stdio.h>
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

int main(int argc, char **argv) {
    static const char domain[] = "kofun.id.export-binding/v1";
    static const uint8_t prefix[6] = { 'K', 'O', 'F', 'U', 'N', 0 };
    static const uint8_t visibility = 3u;
    uint8_t exporter[32];
    uint8_t name_space[32];
    uint8_t target[32];
    uint8_t domain_length[2];
    uint8_t payload_length[4];
    uint8_t digest[32];
    size_t name_length;
    size_t index;
    KofunSha256 context;
    if (argc != 5 ||
        !identity(argv[1], exporter) ||
        !identity(argv[2], name_space) ||
        !identity(argv[4], target)) {
        return 2;
    }
    name_length = strlen(argv[3]);
    u16be(domain_length, (uint16_t)(sizeof(domain) - 1u));
    u32be(
        payload_length,
        (uint32_t)(30u + sizeof(exporter) + sizeof(name_space) +
            name_length + sizeof(target) + sizeof(visibility))
    );
    kofun_sha256_init(&context);
    kofun_sha256_update(&context, prefix, sizeof(prefix));
    kofun_sha256_update(&context, domain_length, sizeof(domain_length));
    kofun_sha256_update(
        &context,
        (const uint8_t *)domain,
        sizeof(domain) - 1u
    );
    kofun_sha256_update(&context, payload_length, sizeof(payload_length));
    field(&context, UINT16_C(0x8001), exporter, sizeof(exporter));
    field(&context, UINT16_C(0x8002), name_space, sizeof(name_space));
    field(
        &context,
        UINT16_C(0x8003),
        (const uint8_t *)argv[3],
        name_length
    );
    field(&context, UINT16_C(0x8004), target, sizeof(target));
    field(
        &context,
        UINT16_C(0x8005),
        &visibility,
        sizeof(visibility)
    );
    kofun_sha256_finish(&context, digest);
    for (index = 0u; index < sizeof(digest); index += 1u) {
        printf("%02x", digest[index]);
    }
    fputc('\n', stdout);
    return 0;
}
