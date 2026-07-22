#include "kif_v1.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum {
    HEADER_BYTES = 12,
    TAG_PUBLIC_FACTS = 0x8006,
    TAG_PUBLIC_DIGEST = 0x8008,
    FACT_TAG_NAMESPACE = 0x8001,
    FACT_TAG_SYMBOL = 0x8002,
    FACT_TAG_NAME = 0x8004
};

typedef struct {
    size_t header;
    size_t value;
    size_t length;
} FieldPosition;

static void fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static uint16_t load_u16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
}

static uint32_t load_u32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24u) |
        ((uint32_t)bytes[1] << 16u) |
        ((uint32_t)bytes[2] << 8u) |
        (uint32_t)bytes[3];
}

static void store_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8u);
    bytes[1] = (uint8_t)value;
}

static void store_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24u);
    bytes[1] = (uint8_t)(value >> 16u);
    bytes[2] = (uint8_t)(value >> 8u);
    bytes[3] = (uint8_t)value;
}

static uint8_t *read_file(const char *path, size_t *length_out) {
    FILE *input = fopen(path, "rb");
    long measured;
    uint8_t *bytes;
    size_t length;
    if (input == NULL || fseek(input, 0, SEEK_END) != 0 ||
        (measured = ftell(input)) < 0 || fseek(input, 0, SEEK_SET) != 0) {
        fail("cannot open codec fixture");
    }
    length = (size_t)measured;
    bytes = malloc(length == 0u ? 1u : length);
    if (bytes == NULL || fread(bytes, 1u, length, input) != length || fclose(input) != 0) {
        fail("cannot read codec fixture");
    }
    *length_out = length;
    return bytes;
}

static uint8_t *duplicate_bytes(const uint8_t *bytes, size_t length) {
    uint8_t *copy = malloc(length == 0u ? 1u : length);
    if (copy == NULL) fail("mutation allocation failed");
    memcpy(copy, bytes, length);
    return copy;
}

static bool find_field(
    const uint8_t *bytes,
    size_t start,
    size_t length,
    uint16_t wanted,
    FieldPosition *position
) {
    size_t cursor = start;
    size_t end = start + length;
    while (cursor < end) {
        uint16_t tag;
        uint32_t field_length;
        if (end - cursor < 6u) return false;
        tag = load_u16(bytes + cursor);
        field_length = load_u32(bytes + cursor + 2u);
        if ((size_t)field_length > end - cursor - 6u) return false;
        if (tag == wanted) {
            *position = (FieldPosition){ cursor, cursor + 6u, field_length };
            return true;
        }
        cursor += 6u + field_length;
    }
    return false;
}

static void expect_status_with_limits(
    const uint8_t *bytes,
    size_t length,
    KofunKifLimits limits,
    KofunKifStatus expected,
    const char *label
) {
    KifReadResult result = kofun_kif_read(bytes, length, limits);
    if (result.status != expected) {
        fprintf(stderr, "FAIL: %s returned %s, expected %s\n", label,
            kofun_kif_status_name(result.status), kofun_kif_status_name(expected));
        exit(1);
    }
    if (expected == KOFUN_KIF_OK) {
        if (result.interface == NULL || result.rebuild_required) fail(label);
        kofun_kif_destroy(result.interface);
    } else if (result.interface != NULL || !result.rebuild_required) {
        fail("failed read published a partial interface");
    }
}

static void expect_status(
    const uint8_t *bytes,
    size_t length,
    KofunKifStatus expected,
    const char *label
) {
    expect_status_with_limits(bytes, length, kofun_kif_default_limits(), expected, label);
}

static uint8_t *insert_field(
    const uint8_t *bytes,
    size_t length,
    size_t offset,
    uint16_t tag,
    const uint8_t *value,
    size_t value_length,
    size_t *new_length
) {
    size_t added = 6u + value_length;
    uint8_t *copy = malloc(length + added);
    if (copy == NULL) fail("field insertion allocation failed");
    memcpy(copy, bytes, offset);
    store_u16(copy + offset, tag);
    store_u32(copy + offset + 2u, (uint32_t)value_length);
    memcpy(copy + offset + 6u, value, value_length);
    memcpy(copy + offset + added, bytes + offset, length - offset);
    store_u32(copy + 8u, (uint32_t)(length + added - HEADER_BYTES));
    *new_length = length + added;
    return copy;
}

static FieldPosition first_record(const uint8_t *bytes, FieldPosition vector) {
    FieldPosition record;
    uint32_t length;
    if (vector.length < 8u || load_u32(bytes + vector.value) < 1u) {
        fail("public vector has no record");
    }
    length = load_u32(bytes + vector.value + 4u);
    if ((size_t)length > vector.length - 8u) fail("public record is truncated");
    record.header = vector.value + 4u;
    record.value = vector.value + 8u;
    record.length = length;
    return record;
}

static FieldPosition second_record(const uint8_t *bytes, FieldPosition vector) {
    FieldPosition first = first_record(bytes, vector);
    FieldPosition second;
    uint32_t length;
    second.header = first.value + first.length;
    if (second.header + 4u > vector.value + vector.length) fail("public vector has one record");
    length = load_u32(bytes + second.header);
    second.value = second.header + 4u;
    second.length = length;
    if (second.value + second.length > vector.value + vector.length) {
        fail("second public record is truncated");
    }
    return second;
}

static void test_structural_mutations(const uint8_t *good, size_t length) {
    uint8_t *copy;
    FieldPosition public_vector;
    FieldPosition digest;
    FieldPosition first;
    FieldPosition second;
    FieldPosition field;
    size_t inserted_length;
    static const uint8_t optional_value[3] = { 'o', 'p', 't' };

    expect_status(good, length, KOFUN_KIF_OK, "valid artifact");
    expect_status(good, 3u, KOFUN_KIF_CORRUPT, "truncated magic");
    expect_status(good, HEADER_BYTES - 1u, KOFUN_KIF_CORRUPT, "truncated header");
    expect_status(good, length - 1u, KOFUN_KIF_CORRUPT, "truncated payload");

    copy = duplicate_bytes(good, length);
    copy[0] = 'B';
    expect_status(copy, length, KOFUN_KIF_CORRUPT, "bad magic");
    free(copy);

    copy = duplicate_bytes(good, length);
    store_u16(copy + 4u, 2u);
    expect_status(copy, length, KOFUN_KIF_UNSUPPORTED_SCHEMA, "unsupported major");
    free(copy);

    copy = duplicate_bytes(good, length);
    store_u32(copy + 8u, UINT32_MAX);
    expect_status(copy, length, KOFUN_KIF_CORRUPT, "payload length overflow");
    free(copy);

    copy = insert_field(good, length, HEADER_BYTES, 1u, optional_value,
        sizeof(optional_value), &inserted_length);
    store_u16(copy + 6u, 1u);
    expect_status(copy, inserted_length, KOFUN_KIF_OK, "optional minor field");
    free(copy);

    copy = insert_field(good, length, length, UINT16_C(0x800a), optional_value,
        sizeof(optional_value), &inserted_length);
    expect_status(copy, inserted_length, KOFUN_KIF_UNSUPPORTED_SCHEMA,
        "unknown required field");
    free(copy);

    copy = duplicate_bytes(good, length);
    store_u16(copy + HEADER_BYTES + 6u + load_u32(copy + HEADER_BYTES + 2u),
        UINT16_C(0x8001));
    expect_status(copy, length, KOFUN_KIF_NONCANONICAL, "duplicate envelope field");
    free(copy);

    if (!find_field(good, HEADER_BYTES, length - HEADER_BYTES,
            TAG_PUBLIC_FACTS, &public_vector) ||
        !find_field(good, HEADER_BYTES, length - HEADER_BYTES,
            TAG_PUBLIC_DIGEST, &digest)) fail("required field lookup failed");

    copy = duplicate_bytes(good, length);
    copy[digest.value] ^= UINT8_C(1);
    expect_status(copy, length, KOFUN_KIF_DIGEST_MISMATCH, "public digest mismatch");
    free(copy);

    copy = duplicate_bytes(good, length);
    store_u32(copy + public_vector.value, KOFUN_KIF_MAX_FACTS + 1u);
    expect_status(copy, length, KOFUN_KIF_LIMIT_EXHAUSTED, "fact count over limit");
    free(copy);

    copy = duplicate_bytes(good, length);
    store_u32(copy + public_vector.value, KOFUN_KIF_MAX_FACTS);
    expect_status(copy, length, KOFUN_KIF_CORRUPT, "fact count boundary fit check");
    free(copy);

    copy = duplicate_bytes(good, length);
    store_u32(copy + public_vector.header + 2u, UINT32_MAX);
    expect_status(copy, length, KOFUN_KIF_CORRUPT, "vector length overflow");
    free(copy);

    first = first_record(good, public_vector);
    second = second_record(good, public_vector);
    copy = duplicate_bytes(good, length);
    {
        size_t first_block = 4u + first.length;
        size_t second_block = 4u + second.length;
        uint8_t *blocks = malloc(first_block + second_block);
        if (blocks == NULL) fail("record swap allocation failed");
        memcpy(blocks, good + second.header, second_block);
        memcpy(blocks + second_block, good + first.header, first_block);
        memcpy(copy + first.header, blocks, first_block + second_block);
        free(blocks);
    }
    expect_status(copy, length, KOFUN_KIF_NONCANONICAL, "unsorted facts");
    free(copy);

    if (!find_field(good, first.value, first.length, FACT_TAG_SYMBOL, &field)) {
        fail("first symbol field missing");
    }
    copy = duplicate_bytes(good, length);
    {
        FieldPosition second_symbol;
        if (!find_field(good, second.value, second.length, FACT_TAG_SYMBOL,
                &second_symbol)) fail("second symbol field missing");
        memcpy(copy + second_symbol.value, good + field.value, KOFUN_KIF_ID_BYTES);
    }
    expect_status(copy, length, KOFUN_KIF_NONCANONICAL, "duplicate fact identity");
    free(copy);

    if (!find_field(good, first.value, first.length, FACT_TAG_NAME, &field)) {
        fail("first name field missing");
    }
    copy = duplicate_bytes(good, length);
    copy[field.value] = UINT8_C(0xff);
    expect_status(copy, length, KOFUN_KIF_NONCANONICAL, "invalid UTF-8 name");
    free(copy);

    if (!find_field(good, first.value, first.length, FACT_TAG_NAMESPACE, &field)) {
        fail("first namespace field missing");
    }
    copy = duplicate_bytes(good, length);
    store_u32(copy + field.header + 2u, 31u);
    {
        KifReadResult malformed = kofun_kif_read(copy, length, kofun_kif_default_limits());
        if (malformed.status == KOFUN_KIF_OK || malformed.interface != NULL) {
            fail("malformed ID width was accepted");
        }
    }
    free(copy);
}

static void test_limits(const uint8_t *good, size_t length) {
    KifReadResult result = kofun_kif_read(good, length, kofun_kif_default_limits());
    KofunKifLimits limits = kofun_kif_default_limits();
    size_t total;
    uint8_t *boundary;
    if (result.status != KOFUN_KIF_OK) fail("cannot inspect valid limits fixture");
    total = result.interface->public_fact_count + result.interface->internal_fact_count;
    limits.max_envelope_bytes = length;
    expect_status_with_limits(good, length, limits, KOFUN_KIF_OK, "exact envelope limit");
    limits.max_envelope_bytes = length - 1u;
    expect_status_with_limits(good, length, limits, KOFUN_KIF_LIMIT_EXHAUSTED,
        "envelope one over configured limit");
    limits = kofun_kif_default_limits();
    limits.max_record_fields = 9u;
    expect_status_with_limits(good, length, limits, KOFUN_KIF_OK, "exact envelope field count");
    limits.max_record_fields = 8u;
    expect_status_with_limits(good, length, limits, KOFUN_KIF_LIMIT_EXHAUSTED,
        "envelope field count over limit");
    limits = kofun_kif_default_limits();
    limits.max_facts = total;
    expect_status_with_limits(good, length, limits, KOFUN_KIF_OK, "exact total fact limit");
    limits.max_facts = total - 1u;
    expect_status_with_limits(good, length, limits, KOFUN_KIF_LIMIT_EXHAUSTED,
        "total fact count over limit");
    limits = kofun_kif_default_limits();
    limits.max_depth = 2u;
    expect_status_with_limits(good, length, limits, KOFUN_KIF_OK, "exact v1 nesting depth");
    limits.max_depth = 1u;
    expect_status_with_limits(good, length, limits, KOFUN_KIF_LIMIT_EXHAUSTED,
        "nesting depth over limit");
    kofun_kif_destroy(result.interface);

    boundary = calloc(KOFUN_KIF_MAX_ENVELOPE, 1u);
    if (boundary == NULL) fail("16 MiB boundary allocation failed");
    memcpy(boundary, "KIF\0", 4u);
    store_u16(boundary + 4u, 1u);
    store_u32(boundary + 8u, KOFUN_KIF_MAX_ENVELOPE - HEADER_BYTES);
    expect_status(boundary, KOFUN_KIF_MAX_ENVELOPE, KOFUN_KIF_NONCANONICAL,
        "exact 16 MiB envelope admitted to structural validation");
    expect_status(boundary, KOFUN_KIF_MAX_ENVELOPE + 1u, KOFUN_KIF_LIMIT_EXHAUSTED,
        "16 MiB envelope one over limit");
    free(boundary);
}

static void test_writer_failures(const uint8_t *good, size_t length, const char *work) {
    KifReadResult read = kofun_kif_read(good, length, kofun_kif_default_limits());
    KifWriteResult write;
    const KofunKifFact *internal_owner = NULL;
    KofunKifFact *public_constructor = NULL;
    char path[1024];
    size_t index;
    if (read.status != KOFUN_KIF_OK) fail("writer failure fixture did not read");

    snprintf(path, sizeof(path), "%s/missing/output.kif", work);
    write = kofun_kif_write(read.interface, path);
    if (write.status != KOFUN_KIF_IO_FAILURE) fail("missing parent did not fail atomically");

    snprintf(path, sizeof(path), "%s/rename-target", work);
    if (mkdir(path, 0700) != 0 && errno != EEXIST) fail("cannot create rename failure target");
    write = kofun_kif_write(read.interface, path);
    if (write.status != KOFUN_KIF_IO_FAILURE) fail("rename failure did not report I/O");

    for (index = 0u; index < read.interface->internal_fact_count; index += 1u) {
        if (read.interface->internal_facts[index].kind == KOFUN_KIF_FACT_ADT) {
            internal_owner = &read.interface->internal_facts[index];
            break;
        }
    }
    for (index = 0u; index < read.interface->public_fact_count; index += 1u) {
        if (read.interface->public_facts[index].kind == KOFUN_KIF_FACT_CONSTRUCTOR) {
            public_constructor = &read.interface->public_facts[index];
            break;
        }
    }
    if (internal_owner == NULL || public_constructor == NULL) fail("visibility fixture is incomplete");
    memcpy(public_constructor->owner_symbol_id, internal_owner->symbol_id, KOFUN_KIF_ID_BYTES);
    snprintf(path, sizeof(path), "%s/visibility-leak.kif", work);
    remove(path);
    write = kofun_kif_write(read.interface, path);
    if (write.status != KOFUN_KIF_VISIBILITY_LEAK) fail("public hidden owner was accepted");
    {
        FILE *unexpected = fopen(path, "rb");
        if (unexpected != NULL) {
            fclose(unexpected);
            fail("rejected writer published an artifact");
        }
    }
    kofun_kif_destroy(read.interface);
}

int main(int argc, char **argv) {
    uint8_t *good;
    size_t length;
    if (argc != 3) {
        fprintf(stderr, "usage: %s VALID_KIF WORK_DIRECTORY\n", argv[0]);
        return 2;
    }
    good = read_file(argv[1], &length);
    test_structural_mutations(good, length);
    test_limits(good, length);
    test_writer_failures(good, length, argv[2]);
    free(good);
    puts("PASS: KIF v1 mutation, limit, publication, and writer transaction matrix");
    return 0;
}
