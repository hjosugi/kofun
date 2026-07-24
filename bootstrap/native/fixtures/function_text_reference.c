#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char *join3(
    const char *left,
    const char *middle,
    const char *right
) {
    size_t left_length = strlen(left);
    size_t middle_length = strlen(middle);
    size_t right_length = strlen(right);
    if (left_length > SIZE_MAX - middle_length ||
        left_length + middle_length > SIZE_MAX - right_length ||
        left_length + middle_length + right_length == SIZE_MAX) {
        fputs("reference: Text length overflow\n", stderr);
        exit(1);
    }
    size_t length = left_length + middle_length + right_length;
    char *result = malloc(length + 1);
    if (result == NULL) {
        fputs("reference: out of memory\n", stderr);
        exit(1);
    }
    memcpy(result, left, left_length);
    memcpy(result + left_length, middle, middle_length);
    memcpy(
        result + left_length + middle_length,
        right,
        right_length + 1
    );
    return result;
}

static char *declaration_label(const char *kind, const char *name) {
    return join3(kind, " ", name);
}

static char *greeting(const char *prefix, const char *name) {
    return join3(prefix, "、", name);
}

int main(int argc, char **argv) {
    char *result;
    if (argc == 2 && strcmp(argv[1], "utf8") == 0) {
        result = greeting("こんにちは", "古墳");
    } else {
        result = declaration_label("fn", "main");
    }
    puts(result);
    free(result);
    return 0;
}
