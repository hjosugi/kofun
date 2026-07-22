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

static void field(KofunSha256 *context, uint16_t tag, const uint8_t *value, size_t length) {
    uint8_t tag_bytes[2];
    uint8_t length_bytes[4];
    u16be(tag_bytes, tag);
    u32be(length_bytes, (uint32_t)length);
    kofun_sha256_update(context, tag_bytes, sizeof(tag_bytes));
    kofun_sha256_update(context, length_bytes, sizeof(length_bytes));
    kofun_sha256_update(context, value, length);
}

static void framed(
    const char *domain,
    const uint8_t *payload,
    size_t payload_length,
    uint8_t output[32]
) {
    static const uint8_t prefix[6] = { 'K', 'O', 'F', 'U', 'N', 0 };
    uint8_t domain_length[2];
    uint8_t length[4];
    KofunSha256 context;
    u16be(domain_length, (uint16_t)strlen(domain));
    u32be(length, (uint32_t)payload_length);
    kofun_sha256_init(&context);
    kofun_sha256_update(&context, prefix, sizeof(prefix));
    kofun_sha256_update(&context, domain_length, sizeof(domain_length));
    kofun_sha256_update(&context, (const uint8_t *)domain, strlen(domain));
    kofun_sha256_update(&context, length, sizeof(length));
    kofun_sha256_update(&context, payload, payload_length);
    kofun_sha256_finish(&context, output);
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
    static const char namespace_payload[] =
        "kofun.namespace-id/v1\ntag=2\nname=module\n";
    static const char domain[] = "kofun.id.import-binding/v1";
    const uint8_t form_tag = UINT8_C(1);
    uint8_t importer[32];
    uint8_t file[32];
    uint8_t target[32];
    uint8_t namespace_id[32];
    uint8_t digest[32];
    uint8_t prefix[6] = { 'K', 'O', 'F', 'U', 'N', 0 };
    uint8_t domain_length[2];
    uint8_t payload_length[4];
    size_t local_length;
    size_t index;
    KofunSha256 context;
    if (argc != 5 || !identity(argv[1], importer) || !identity(argv[2], file) ||
        !identity(argv[4], target)) return 2;
    local_length = strlen(argv[3]);
    framed("kofun.id.namespace/v1", (const uint8_t *)namespace_payload,
        sizeof(namespace_payload) - 1u, namespace_id);
    u16be(domain_length, (uint16_t)(sizeof(domain) - 1u));
    u32be(payload_length, (uint32_t)(36u + 32u + 32u + 32u + local_length + 32u + 1u));
    kofun_sha256_init(&context);
    kofun_sha256_update(&context, prefix, sizeof(prefix));
    kofun_sha256_update(&context, domain_length, sizeof(domain_length));
    kofun_sha256_update(&context, (const uint8_t *)domain, sizeof(domain) - 1u);
    kofun_sha256_update(&context, payload_length, sizeof(payload_length));
    field(&context, UINT16_C(0x8001), importer, sizeof(importer));
    field(&context, UINT16_C(0x8002), file, sizeof(file));
    field(&context, UINT16_C(0x8003), namespace_id, sizeof(namespace_id));
    field(&context, UINT16_C(0x8004), (const uint8_t *)argv[3], local_length);
    field(&context, UINT16_C(0x8005), target, sizeof(target));
    field(&context, UINT16_C(0x8006), &form_tag, 1u);
    kofun_sha256_finish(&context, digest);
    for (index = 0u; index < sizeof(digest); index += 1u) printf("%02x", digest[index]);
    fputc('\n', stdout);
    return 0;
}
