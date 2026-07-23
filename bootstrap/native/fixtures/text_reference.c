#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static size_t utf8_width(const unsigned char *bytes, size_t remaining) {
    if (remaining == 0) return 0;
    if (bytes[0] < UINT8_C(0x80)) return 1;
    if (bytes[0] < UINT8_C(0xe0)) return remaining >= 2 ? 2 : 0;
    if (bytes[0] < UINT8_C(0xf0)) return remaining >= 3 ? 3 : 0;
    return remaining >= 4 ? 4 : 0;
}

static size_t codepoint_count(const char *value) {
    const unsigned char *bytes = (const unsigned char *)value;
    size_t length = strlen(value);
    size_t offset = 0;
    size_t count = 0;
    while (offset < length) {
        size_t width = utf8_width(bytes + offset, length - offset);
        if (width == 0) return SIZE_MAX;
        offset += width;
        ++count;
    }
    return count;
}

static bool codepoint_at(
    const char *value,
    int64_t index,
    const char **start,
    size_t *width
) {
    size_t count = codepoint_count(value);
    if (count == SIZE_MAX) return false;
    if (index < 0) index += (int64_t)count;
    if (index < 0 || (uint64_t)index >= count) return false;

    size_t offset = 0;
    for (int64_t current = 0; current < index; ++current) {
        offset += utf8_width(
            (const unsigned char *)value + offset,
            strlen(value) - offset
        );
    }
    *start = value + offset;
    *width = utf8_width(
        (const unsigned char *)value + offset,
        strlen(value) - offset
    );
    return *width != 0;
}

static int print_codepoint(const char *value, int64_t index) {
    const char *start = NULL;
    size_t width = 0;
    if (!codepoint_at(value, index, &start, &width)) return 1;
    if (fwrite(start, 1, width, stdout) != width) return 1;
    return putchar('\n') == EOF ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "concat") == 0) {
        return puts("古" "墳") == EOF;
    }
    if (strcmp(argv[1], "equal") == 0) {
        return puts(strcmp("古" "墳", "古墳") == 0 ? "true" : "false")
            == EOF;
    }
    if (strcmp(argv[1], "not-equal") == 0) {
        return puts(strcmp("古墳", "古") != 0 ? "true" : "false")
            == EOF;
    }
    if (strcmp(argv[1], "len") == 0) {
        return printf("%zu\n", codepoint_count("aé古🌍") + 38) < 0;
    }
    if (strcmp(argv[1], "index") == 0) {
        return print_codepoint("aé古墳🌍", 3);
    }
    if (strcmp(argv[1], "negative-index") == 0) {
        return print_codepoint("古墳", -1);
    }
    if (strcmp(argv[1], "chars-index") == 0) {
        return print_codepoint("aé古🌍", -2);
    }
    if (strcmp(argv[1], "empty-chars-len") == 0) {
        return printf("%zu\n", codepoint_count("") + 42) < 0;
    }
    return 2;
}
