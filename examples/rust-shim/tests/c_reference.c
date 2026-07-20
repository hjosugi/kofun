#include "kofun_unicode_shim.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned char borrowed[] = {0x65, 0xCC, 0x81};
    const unsigned char original[] = {0x65, 0xCC, 0x81};
    const unsigned char invalid[] = {0xFF};
    KofunGraphemeResult valid;
    KofunGraphemeResult invalid_result;
    KofunGraphemeResult repeated;
    KofunGraphemeResult null_result;

    valid = kofun_unicode_grapheme_count(borrowed, sizeof(borrowed));
    invalid_result = kofun_unicode_grapheme_count(
        invalid,
        sizeof(invalid)
    );
    repeated = kofun_unicode_grapheme_count(borrowed, sizeof(borrowed));
    null_result = kofun_unicode_grapheme_count(NULL, 1);

    if (memcmp(borrowed, original, sizeof(borrowed)) != 0) {
        fputs("borrowed input was modified\n", stderr);
        return 1;
    }
    if (null_result.status != KOFUN_UNICODE_NULL_BUFFER) {
        fputs("null buffer did not map to status\n", stderr);
        return 1;
    }

    printf("%d\n", valid.status);
    printf("%zu\n", valid.count);
    printf("%d\n", invalid_result.status);
    printf("%zu\n", invalid_result.error_offset);
    printf("%zu\n", repeated.count);
    printf("%d\n", kofun_unicode_panic_probe());
    return 0;
}
