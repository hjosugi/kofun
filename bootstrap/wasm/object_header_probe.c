#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "object_arena.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: object-header-probe U64\n");
        return 2;
    }
    errno = 0;
    char *end = NULL;
    uintmax_t parsed = strtoumax(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' ||
        parsed > UINT64_MAX) {
        fprintf(stderr, "object-header-probe: invalid u64\n");
        return 2;
    }
    uint8_t header[KOFUN_WASM_OBJECT_HEADER_BYTES];
    kofun_wasm_write_u64_header(header, (uint64_t)parsed);
    for (size_t index = 0; index < sizeof(header); ++index) {
        printf("%02x", header[index]);
    }
    putchar('\n');
    return 0;
}
