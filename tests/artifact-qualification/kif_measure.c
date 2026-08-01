#include "kif_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/resource.h>
#include <time.h>

#define WARM_SAMPLES 31u

static void fail(const char *message) {
    fprintf(stderr, "artifact-qualification: KIF measurement: %s\n", message);
    exit(1);
}

static uint8_t *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    long end;
    uint8_t *bytes;
    if (file == NULL) fail("cannot open input");
    if (fseek(file, 0, SEEK_END) != 0) fail("cannot seek input");
    end = ftell(file);
    if (end <= 0 || (unsigned long)end > KOFUN_KIF_MAX_ENVELOPE) {
        fail("input is empty or exceeds the production envelope limit");
    }
    if (fseek(file, 0, SEEK_SET) != 0) fail("cannot rewind input");
    *length = (size_t)end;
    bytes = (uint8_t *)malloc(*length);
    if (bytes == NULL) fail("cannot allocate input buffer");
    if (fread(bytes, 1u, *length, file) != *length) fail("cannot read input");
    if (fclose(file) != 0) fail("cannot close input");
    return bytes;
}

static uint64_t now_ns(void) {
    struct timespec value;
    if (timespec_get(&value, TIME_UTC) != TIME_UTC) fail("clock unavailable");
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static uint64_t measured_read(const uint8_t *bytes, size_t length) {
    uint64_t start = now_ns();
    KifReadResult result = kofun_kif_read(bytes, length, kofun_kif_default_limits());
    uint64_t elapsed = now_ns() - start;
    if (result.status != KOFUN_KIF_OK || result.interface == NULL ||
        result.rebuild_required) {
        fail("production reader rejected the measured fixture");
    }
    kofun_kif_destroy(result.interface);
    return elapsed;
}

int main(int argc, char **argv) {
    uint8_t *bytes;
    size_t length;
    size_t index;
    uint64_t cold;
    struct rusage usage;
    if (argc != 2) fail("usage: kif-measure INPUT.kif");
    bytes = read_file(argv[1], &length);
    cold = measured_read(bytes, length);
    printf("{\"artifact_bytes\":%lu,\"cold_decode_ns\":%llu,\"warm_decode_ns\":[",
        (unsigned long)length, (unsigned long long)cold);
    for (index = 0u; index < WARM_SAMPLES; index += 1u) {
        if (index > 0u) putchar(',');
        printf("%llu", (unsigned long long)measured_read(bytes, length));
    }
    if (getrusage(RUSAGE_SELF, &usage) != 0) fail("getrusage failed");
    printf("],\"peak_rss_kib\":%ld}\n", usage.ru_maxrss);
    free(bytes);
    return 0;
}
