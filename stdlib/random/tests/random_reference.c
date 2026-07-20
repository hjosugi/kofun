#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static int64_t normalize_seed(int64_t seed) {
    int64_t normalized = seed % INT64_C(2147483647);
    if (normalized < 0) normalized += INT64_C(2147483647);
    return normalized == 0 ? 1 : normalized;
}

static int64_t next_state(int64_t state) {
    int64_t high = state / INT64_C(127773);
    int64_t low = state % INT64_C(127773);
    int64_t candidate = INT64_C(16807) * low - INT64_C(2836) * high;
    return candidate <= 0 ? candidate + INT64_C(2147483647) : candidate;
}

static int64_t accepted_state_small(int64_t state, int64_t upper) {
    int64_t limit = INT64_C(2147483646) - INT64_C(2147483646) % upper;
    do {
        state = next_state(state);
    } while (state - 1 >= limit);
    return state;
}

static int64_t accepted_steps_small(int64_t state, int64_t upper) {
    int64_t steps = 0;
    int64_t limit = INT64_C(2147483646) - INT64_C(2147483646) % upper;
    do {
        state = next_state(state);
        ++steps;
    } while (state - 1 >= limit);
    return steps;
}

static int64_t below_small(int64_t state, int64_t upper) {
    return (accepted_state_small(state, upper) - 1) % upper;
}

static int64_t bound_status(int64_t upper) {
    if (upper <= 0) return 0;
    if (upper > INT64_C(2147483646)) return 2;
    return 1;
}

static int64_t sample_status(int64_t count, int64_t available) {
    return count >= 0 && count <= available;
}

static int64_t toy_check_bound(void) {
    int64_t checks = 0;
    for (int64_t upper = 1; upper <= 6; ++upper) {
        int64_t limit = 6 - 6 % upper;
        for (int64_t residue = 0; residue < upper; ++residue) {
            int64_t count = 0;
            for (int64_t candidate = 0; candidate < limit; ++candidate) {
                if (candidate % upper == residue) ++count;
            }
            if (count != limit / upper) return -1000;
            ++checks;
        }
    }
    return checks;
}

static int64_t pow10(int exponent) {
    int64_t value = 1;
    while (exponent-- > 0) value *= 10;
    return value;
}

static int64_t packed_digit(int64_t value, int index, int length) {
    return (value / pow10(length - index - 1)) % 10;
}

static int64_t replace_packed_digit(
    int64_t value,
    int index,
    int length,
    int64_t replacement
) {
    int64_t place = pow10(length - index - 1);
    return value + (replacement - packed_digit(value, index, length)) * place;
}

static int64_t swap_packed_digits(
    int64_t value,
    int left,
    int right,
    int length
) {
    int64_t left_value = packed_digit(value, left, length);
    int64_t right_value = packed_digit(value, right, length);
    value = replace_packed_digit(value, left, length, right_value);
    return replace_packed_digit(value, right, length, left_value);
}

static int64_t shuffle_packed(int64_t state, int64_t packed) {
    for (int64_t remaining = 5; remaining > 1; --remaining) {
        state = accepted_state_small(state, remaining);
        int64_t selected = (state - 1) % remaining;
        packed = swap_packed_digits(packed, (int)(remaining - 1),
                                    (int)selected, 5);
    }
    return packed;
}

static int64_t sample_packed(int64_t state, int64_t packed, int64_t count) {
    for (int64_t index = 0; index < count; ++index) {
        int64_t upper = 5 - index;
        state = accepted_state_small(state, upper);
        int64_t selected = index + (state - 1) % upper;
        packed = swap_packed_digits(packed, (int)index, (int)selected, 5);
    }
    return packed_digit(packed, 0, 5) * 100 +
           packed_digit(packed, 1, 5) * 10 +
           packed_digit(packed, 2, 5);
}

static int64_t fixed_work_steps(int64_t state, int remaining, int64_t upper) {
    int64_t total = 0;
    while (remaining-- > 0) {
        total += accepted_steps_small(state, upper);
        state = accepted_state_small(state, upper);
    }
    return total;
}

static int64_t shuffle_work_steps(int64_t state, int64_t remaining) {
    int64_t total = 0;
    while (remaining > 1) {
        total += accepted_steps_small(state, remaining);
        state = accepted_state_small(state, remaining);
        --remaining;
    }
    return total;
}

int main(void) {
    printf("%" PRId64 "\n", normalize_seed(0));
    printf("%" PRId64 "\n", normalize_seed(INT64_C(2147483647)));
    printf("%" PRId64 "\n", normalize_seed(-1));

    int64_t state = normalize_seed(1);
    for (int count = 0; count < 10; ++count) {
        state = next_state(state);
        printf("%" PRId64 "\n", state - 1);
    }

    state = normalize_seed(1);
    for (int count = 0; count < 8; ++count) {
        state = accepted_state_small(state, 10);
        printf("%" PRId64 "\n", (state - 1) % 10);
    }

    printf("%" PRId64 "\n",
           accepted_steps_small(normalize_seed(100000), INT64_C(1073741824)));
    printf("%" PRId64 "\n",
           below_small(normalize_seed(100000), INT64_C(1073741824)));
    printf("%" PRId64 "\n", toy_check_bound());
    printf("%" PRId64 "\n", bound_status(0));
    printf("%" PRId64 "\n", bound_status(INT64_C(2147483646)));
    printf("%" PRId64 "\n", bound_status(INT64_C(2147483647)));
    printf("%" PRId64 "\n", sample_status(-1, 5));
    printf("%" PRId64 "\n", sample_status(3, 5));
    printf("%" PRId64 "\n", sample_status(6, 5));

    state = normalize_seed(1);
    for (int count = 0; count < 6; ++count) {
        state = accepted_state_small(state, 256);
        printf("%" PRId64 "\n", (state - 1) % 256);
    }

    printf("%" PRId64 "\n", shuffle_packed(normalize_seed(42), 12345));
    printf("%" PRId64 "\n", sample_packed(normalize_seed(42), 12345, 3));
    printf("%" PRId64 "\n", fixed_work_steps(normalize_seed(1), 1000, 10));
    printf("%" PRId64 "\n", fixed_work_steps(normalize_seed(1), 256, 256));
    printf("%" PRId64 "\n", shuffle_work_steps(normalize_seed(1), 128));
    printf("%" PRId64 "\n",
           fixed_work_steps(normalize_seed(1), 1000, INT64_C(1073741824)));
    return 0;
}
