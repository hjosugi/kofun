#include "../../bootstrap/stage2/semantic_events.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    FILE *file;
    long size;
    uint8_t *bytes;
    KofunSemanticError error;
    if (argc != 2) return 2;
    file = fopen(argv[1], "rb");
    if (file == NULL) return 2;
    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        return 2;
    }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return 2;
    }
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL) {
        (void)fclose(file);
        return 2;
    }
    if (fread(bytes, 1u, (size_t)size, file) != (size_t)size) {
        (void)fclose(file);
        free(bytes);
        return 2;
    }
    if (fclose(file) != 0) {
        free(bytes);
        return 2;
    }
    if (!kofun_semantic_validate_stream(
            bytes,
            (size_t)size,
            &error)) {
        fprintf(
            stderr,
            "%s record=%u kind=%u %s\n",
            error.code,
            error.record_index,
            error.event_kind,
            error.detail
        );
        free(bytes);
        return 1;
    }
    free(bytes);
    return 0;
}
