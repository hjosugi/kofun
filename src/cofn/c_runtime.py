from __future__ import annotations

C_RUNTIME = r'''typedef struct {
    int64_t len;
    const char **items;
} cofn_text_list;

int cofn_runtime_argc = 0;
char **cofn_runtime_argv = NULL;

void cofn_rt_panic(const char *message) {
    fprintf(stderr, "Cofn runtime error: %s\n", message);
    exit(1);
}

void *cofn_rt_alloc(size_t size) {
    void *value = malloc(size == 0 ? 1 : size);
    if (value == NULL) {
        cofn_rt_panic("out of memory");
    }
    return value;
}

char *cofn_rt_copy_n(const char *value, size_t length) {
    char *result = (char *)cofn_rt_alloc(length + 1);
    if (length > 0) {
        memcpy(result, value, length);
    }
    result[length] = '\0';
    return result;
}

char *cofn_rt_text_concat(const char *left, const char *right) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    char *result = (char *)cofn_rt_alloc(left_len + right_len + 1);
    memcpy(result, left, left_len);
    memcpy(result + left_len, right, right_len + 1);
    return result;
}

bool cofn_rt_text_equal(const char *left, const char *right) {
    return strcmp(left, right) == 0;
}

int64_t cofn_rt_text_len(const char *value) {
    return (int64_t)strlen(value);
}

int64_t cofn_rt_text_list_len(cofn_text_list values) {
    return values.len;
}

cofn_text_list cofn_rt_args(void) {
    cofn_text_list result;
    result.len = (int64_t)cofn_runtime_argc;
    result.items = (const char **)cofn_runtime_argv;
    return result;
}

cofn_text_list cofn_rt_chars(const char *value) {
    size_t length = strlen(value);
    const char **items = (const char **)cofn_rt_alloc(sizeof(char *) * (length == 0 ? 1 : length));
    for (size_t index = 0; index < length; ++index) {
        items[index] = cofn_rt_copy_n(value + index, 1);
    }
    cofn_text_list result;
    result.len = (int64_t)length;
    result.items = items;
    return result;
}

const char *cofn_rt_text_list_get(cofn_text_list values, int64_t index) {
    if (index < 0 || index >= values.len) {
        cofn_rt_panic("List[Text] index out of bounds");
    }
    return values.items[index];
}

const char *cofn_rt_text_index(const char *value, int64_t index) {
    int64_t length = (int64_t)strlen(value);
    if (index < 0 || index >= length) {
        cofn_rt_panic("Text index out of bounds");
    }
    return cofn_rt_copy_n(value + index, 1);
}

bool cofn_rt_text_contains(const char *value, const char *needle) {
    return strstr(value, needle) != NULL;
}

int64_t cofn_rt_find(const char *value, const char *needle) {
    const char *found = strstr(value, needle);
    return found == NULL ? INT64_C(-1) : (int64_t)(found - value);
}

char *cofn_rt_text_slice(const char *value, int64_t start, int64_t end) {
    int64_t length = (int64_t)strlen(value);
    if (start < 0) start = 0;
    if (end < start) end = start;
    if (start > length) start = length;
    if (end > length) end = length;
    return cofn_rt_copy_n(value + start, (size_t)(end - start));
}

char *cofn_rt_trim(const char *value) {
    const unsigned char *start = (const unsigned char *)value;
    while (*start != '\0' && isspace(*start)) {
        ++start;
    }
    const unsigned char *end = (const unsigned char *)value + strlen(value);
    while (end > start && isspace(end[-1])) {
        --end;
    }
    return cofn_rt_copy_n((const char *)start, (size_t)(end - start));
}

char *cofn_rt_replace(const char *value, const char *old, const char *replacement) {
    size_t old_len = strlen(old);
    if (old_len == 0) {
        return cofn_rt_copy_n(value, strlen(value));
    }
    size_t replacement_len = strlen(replacement);
    size_t count = 0;
    const char *cursor = value;
    while ((cursor = strstr(cursor, old)) != NULL) {
        ++count;
        cursor += old_len;
    }
    size_t value_len = strlen(value);
    size_t result_len;
    if (replacement_len >= old_len) {
        result_len = value_len + count * (replacement_len - old_len);
    } else {
        result_len = value_len - count * (old_len - replacement_len);
    }
    char *result = (char *)cofn_rt_alloc(result_len + 1);
    char *out = result;
    cursor = value;
    const char *match;
    while ((match = strstr(cursor, old)) != NULL) {
        size_t prefix = (size_t)(match - cursor);
        memcpy(out, cursor, prefix);
        out += prefix;
        memcpy(out, replacement, replacement_len);
        out += replacement_len;
        cursor = match + old_len;
    }
    strcpy(out, cursor);
    return result;
}

bool cofn_rt_starts_with(const char *value, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    return strncmp(value, prefix, prefix_len) == 0;
}

bool cofn_rt_ends_with(const char *value, const char *suffix) {
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    return suffix_len <= value_len && strcmp(value + value_len - suffix_len, suffix) == 0;
}

bool cofn_rt_is_digit(const char *value) {
    return value[0] != '\0' && value[1] == '\0' && isdigit((unsigned char)value[0]) != 0;
}

bool cofn_rt_is_space(const char *value) {
    return value[0] != '\0' && value[1] == '\0' && isspace((unsigned char)value[0]) != 0;
}

int64_t cofn_rt_parse_int(const char *value) {
    char *end = NULL;
    long long result = strtoll(value, &end, 10);
    if (end == value || *end != '\0') {
        cofn_rt_panic("invalid integer text");
    }
    return (int64_t)result;
}

char *cofn_rt_int_to_text(int64_t value) {
    char buffer[64];
    int written = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
    if (written < 0) {
        cofn_rt_panic("integer formatting failed");
    }
    return cofn_rt_copy_n(buffer, (size_t)written);
}

char *cofn_rt_read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        cofn_rt_panic("cannot open input file");
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        cofn_rt_panic("cannot seek input file");
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        cofn_rt_panic("cannot measure input file");
    }
    rewind(file);
    char *result = (char *)cofn_rt_alloc((size_t)size + 1);
    size_t read = fread(result, 1, (size_t)size, file);
    if (read != (size_t)size && ferror(file)) {
        fclose(file);
        cofn_rt_panic("cannot read input file");
    }
    result[read] = '\0';
    fclose(file);
    return result;
}

void cofn_rt_write_text(const char *path, const char *value) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        cofn_rt_panic("cannot open output file");
    }
    size_t length = strlen(value);
    if (fwrite(value, 1, length, file) != length) {
        fclose(file);
        cofn_rt_panic("cannot write output file");
    }
    if (fclose(file) != 0) {
        cofn_rt_panic("cannot close output file");
    }
}'''


def runtime_lines() -> list[str]:
    return C_RUNTIME.splitlines()
