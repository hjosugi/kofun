/*
 * Compiler-native Decimal, slice 2 of #710.
 *
 * The significand is a binary big integer in base 2^32 rather than BCD or a
 * fixed `i128`, which is the choice `docs/DECIMAL.md` records and its reasons:
 * BCD wastes space and gets no help from the CPU, and a fixed width would make
 * ordinary exact expressions fail at an arbitrary magnitude boundary.
 *
 * Only four big-integer primitives are needed, because slice 2 has no
 * arithmetic: multiply by a small factor, add a small term, divide by a small
 * divisor, and compare. Construction is `((0 * 10 + d) * 10 + d) ...`;
 * canonicalization is repeated division by ten. Slices 3 and 4 add the rest.
 */

#include "decimal_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIMB_BITS 32
#define LIMB_BASE (1ULL << LIMB_BITS)

/*
 * The small-value path. Frozen decision 1 allows keeping small significands
 * inline and promoting on overflow, but only as an optimization that cannot be
 * observed. Here "inline" means the magnitude fits two limbs, so the whole
 * significand is one `uint64_t`; nothing about the observable value changes at
 * the boundary, and `inline_storage` exists so a gate can prove that.
 */
#define INLINE_LIMBS 2u

const char *kofun_decimal_status_code(KofunDecimalStatus status) {
    switch (status) {
        case KOFUN_DECIMAL_OK: return "";
        case KOFUN_DECIMAL_DIGIT_LIMIT: return "D001";
        case KOFUN_DECIMAL_SCALE_LIMIT: return "D002";
        case KOFUN_DECIMAL_MALFORMED: return "D003";
        case KOFUN_DECIMAL_MEMORY: return "D004";
    }
    return "";
}

const char *kofun_decimal_status_message(KofunDecimalStatus status) {
    switch (status) {
        case KOFUN_DECIMAL_OK:
            return "";
        case KOFUN_DECIMAL_DIGIT_LIMIT:
            return "error[D001]: Decimal significand exceeds the profile's "
                   "digit limit";
        case KOFUN_DECIMAL_SCALE_LIMIT:
            return "error[D002]: Decimal scale is outside the profile's range";
        case KOFUN_DECIMAL_MALFORMED:
            return "error[D003]: malformed Decimal literal";
        case KOFUN_DECIMAL_MEMORY:
            return "error[D004]: Decimal allocation refused";
    }
    return "";
}

unsigned kofun_decimal_profile_version(void) {
    return KOFUN_DECIMAL_PROFILE_VERSION;
}

void kofun_decimal_init(KofunDecimal *value) {
    if (value == NULL) return;
    value->sign = 0;
    value->scale = 0;
    value->limbs = NULL;
    value->limb_count = 0;
    value->inline_storage = true;
}

void kofun_decimal_free(KofunDecimal *value) {
    if (value == NULL) return;
    free(value->limbs);
    kofun_decimal_init(value);
}

/* --- magnitude primitives ------------------------------------------------ */

typedef struct {
    uint32_t *limbs;
    size_t count;
    size_t capacity;
} Magnitude;

static void magnitude_init(Magnitude *m) {
    m->limbs = NULL;
    m->count = 0;
    m->capacity = 0;
}

static void magnitude_free(Magnitude *m) {
    free(m->limbs);
    magnitude_init(m);
}

static bool magnitude_reserve(Magnitude *m, size_t wanted) {
    if (wanted <= m->capacity) return true;
    size_t capacity = m->capacity == 0 ? 8 : m->capacity;
    while (capacity < wanted) {
        if (capacity > (size_t)-1 / 2) return false;
        capacity *= 2;
    }
    uint32_t *grown = realloc(m->limbs, capacity * sizeof(*grown));
    if (grown == NULL) return false;
    m->limbs = grown;
    m->capacity = capacity;
    return true;
}

static void magnitude_trim(Magnitude *m) {
    while (m->count > 0 && m->limbs[m->count - 1] == 0) --m->count;
}

/* m = m * factor + addend, both small. */
static bool magnitude_mul_add_small(
    Magnitude *m,
    uint32_t factor,
    uint32_t addend
) {
    uint64_t carry = addend;
    for (size_t index = 0; index < m->count; ++index) {
        uint64_t product = (uint64_t)m->limbs[index] * factor + carry;
        m->limbs[index] = (uint32_t)(product & 0xFFFFFFFFu);
        carry = product >> LIMB_BITS;
    }
    while (carry != 0) {
        if (!magnitude_reserve(m, m->count + 1)) return false;
        m->limbs[m->count++] = (uint32_t)(carry & 0xFFFFFFFFu);
        carry >>= LIMB_BITS;
    }
    return true;
}

/* m /= divisor, returning the remainder. `divisor` must be non-zero. */
static uint32_t magnitude_divmod_small(Magnitude *m, uint32_t divisor) {
    uint64_t remainder = 0;
    for (size_t index = m->count; index-- > 0;) {
        uint64_t current = (remainder << LIMB_BITS) | m->limbs[index];
        m->limbs[index] = (uint32_t)(current / divisor);
        remainder = current % divisor;
    }
    magnitude_trim(m);
    return (uint32_t)remainder;
}

/* Remainder of m / divisor without modifying m. */
static uint32_t magnitude_mod_small(const Magnitude *m, uint32_t divisor) {
    uint64_t remainder = 0;
    for (size_t index = m->count; index-- > 0;) {
        remainder = ((remainder << LIMB_BITS) | m->limbs[index]) % divisor;
    }
    return (uint32_t)remainder;
}

static bool magnitude_is_zero(const Magnitude *m) {
    return m->count == 0;
}

static int magnitude_compare(const Magnitude *a, const Magnitude *b) {
    if (a->count != b->count) return a->count < b->count ? -1 : 1;
    for (size_t index = a->count; index-- > 0;) {
        if (a->limbs[index] != b->limbs[index]) {
            return a->limbs[index] < b->limbs[index] ? -1 : 1;
        }
    }
    return 0;
}

/* --- construction -------------------------------------------------------- */

static bool ascii_digit(char symbol) {
    return symbol >= '0' && symbol <= '9';
}

/*
 * Split a literal into its digit sequence and its decimal exponent, without
 * building any number. `digits` receives every significant digit in order and
 * `exponent` the power of ten the digit string must be multiplied by, so the
 * value is `digits * 10^exponent` exactly.
 *
 * Underscores are dropped here rather than rejected: the lexer has already
 * refused any that sit outside a position between two digits (`E2S98`), so
 * anything reaching this function is well formed.
 */
static KofunDecimalStatus split_literal(
    const char *text,
    size_t length,
    char *digits,
    size_t digits_capacity,
    size_t *digit_count,
    long *exponent
) {
    size_t written = 0;
    long fraction_digits = 0;
    long explicit_exponent = 0;
    bool seen_digit = false;
    bool seen_point = false;
    size_t cursor = 0;

    for (; cursor < length; ++cursor) {
        char symbol = text[cursor];
        if (symbol == '_') continue;
        if (symbol == '.') {
            if (seen_point) return KOFUN_DECIMAL_MALFORMED;
            seen_point = true;
            continue;
        }
        if (symbol == 'e' || symbol == 'E') break;
        if (!ascii_digit(symbol)) return KOFUN_DECIMAL_MALFORMED;
        seen_digit = true;
        if (seen_point) ++fraction_digits;
        /* Leading zeros contribute nothing and must not consume the budget. */
        if (written == 0 && symbol == '0' && !seen_point) continue;
        if (written >= digits_capacity) return KOFUN_DECIMAL_DIGIT_LIMIT;
        digits[written++] = symbol;
    }
    if (!seen_digit) return KOFUN_DECIMAL_MALFORMED;

    if (cursor < length && (text[cursor] == 'e' || text[cursor] == 'E')) {
        ++cursor;
        int exponent_sign = 1;
        if (cursor < length && (text[cursor] == '+' || text[cursor] == '-')) {
            if (text[cursor] == '-') exponent_sign = -1;
            ++cursor;
        }
        bool seen_exponent_digit = false;
        for (; cursor < length; ++cursor) {
            if (text[cursor] == '_') continue;
            if (!ascii_digit(text[cursor])) return KOFUN_DECIMAL_MALFORMED;
            seen_exponent_digit = true;
            /* Clamp the accumulator, not the value: anything past the scale
             * limit is refused below, and this only keeps the counter from
             * overflowing on absurd input. */
            if (explicit_exponent < 1000000L) {
                explicit_exponent = explicit_exponent * 10 +
                    (text[cursor] - '0');
            }
        }
        if (!seen_exponent_digit) return KOFUN_DECIMAL_MALFORMED;
        explicit_exponent *= exponent_sign;
    }

    *digit_count = written;
    *exponent = explicit_exponent - fraction_digits;
    return KOFUN_DECIMAL_OK;
}

/*
 * Move every factor of ten out of the magnitude and into the scale, which is
 * what makes the form canonical: `1.0`, `1.00`, `0.1e1` and `1` all arrive
 * here as different `(magnitude, scale)` pairs and leave as the same one.
 */
static void canonicalize(Magnitude *m, long *scale) {
    if (magnitude_is_zero(m)) {
        *scale = 0;
        return;
    }
    while (magnitude_mod_small(m, 10) == 0) {
        (void)magnitude_divmod_small(m, 10);
        --*scale;
        if (magnitude_is_zero(m)) {
            *scale = 0;
            return;
        }
    }
}

static KofunDecimalStatus build(
    const char *text,
    size_t length,
    Magnitude *magnitude,
    long *scale
) {
    char *digits = malloc(KOFUN_DECIMAL_MAX_SIGNIFICAND_DIGITS);
    if (digits == NULL) return KOFUN_DECIMAL_MEMORY;
    size_t digit_count = 0;
    long exponent = 0;
    KofunDecimalStatus status = split_literal(
        text,
        length,
        digits,
        KOFUN_DECIMAL_MAX_SIGNIFICAND_DIGITS,
        &digit_count,
        &exponent
    );
    if (status != KOFUN_DECIMAL_OK) {
        free(digits);
        return status;
    }

    magnitude_init(magnitude);
    for (size_t index = 0; index < digit_count; ++index) {
        if (!magnitude_mul_add_small(
                magnitude,
                10u,
                (uint32_t)(digits[index] - '0'))) {
            free(digits);
            magnitude_free(magnitude);
            return KOFUN_DECIMAL_MEMORY;
        }
    }
    free(digits);
    magnitude_trim(magnitude);

    /* The stored scale is the negated exponent: value = significand * 10^-scale. */
    *scale = -exponent;
    canonicalize(magnitude, scale);

    if (*scale > KOFUN_DECIMAL_MAX_SCALE || *scale < KOFUN_DECIMAL_MIN_SCALE) {
        magnitude_free(magnitude);
        return KOFUN_DECIMAL_SCALE_LIMIT;
    }
    return KOFUN_DECIMAL_OK;
}

KofunDecimalStatus kofun_decimal_from_literal(
    const char *text,
    size_t length,
    KofunDecimal *out
) {
    if (text == NULL || out == NULL) return KOFUN_DECIMAL_MALFORMED;
    kofun_decimal_init(out);

    Magnitude magnitude;
    long scale = 0;
    KofunDecimalStatus status = build(text, length, &magnitude, &scale);
    if (status != KOFUN_DECIMAL_OK) return status;

    out->limbs = magnitude.limbs;
    out->limb_count = magnitude.count;
    out->scale = (int32_t)scale;
    out->sign = magnitude.count == 0 ? 0 : 1;
    out->inline_storage = magnitude.count <= INLINE_LIMBS;
    if (out->sign == 0) {
        /* One representation of zero: no limbs, scale zero. */
        free(out->limbs);
        out->limbs = NULL;
        out->limb_count = 0;
        out->scale = 0;
        out->inline_storage = true;
    }
    return KOFUN_DECIMAL_OK;
}

/* --- observation --------------------------------------------------------- */

bool kofun_decimal_equal(const KofunDecimal *left, const KofunDecimal *right) {
    if (left == NULL || right == NULL) return false;
    if (left->sign != right->sign) return false;
    if (left->scale != right->scale) return false;
    if (left->limb_count != right->limb_count) return false;
    return memcmp(
        left->limbs,
        right->limbs,
        left->limb_count * sizeof(*left->limbs)
    ) == 0;
}

/*
 * Compare by value. Two canonical values with different scales may still be
 * ordered without arithmetic: scale up the one with the smaller scale by
 * repeated multiplication, on copies, and compare magnitudes.
 */
int kofun_decimal_compare(const KofunDecimal *left, const KofunDecimal *right) {
    if (left->sign != right->sign) return left->sign < right->sign ? -1 : 1;
    if (left->sign == 0) return 0;

    Magnitude a;
    Magnitude b;
    magnitude_init(&a);
    magnitude_init(&b);
    if (!magnitude_reserve(&a, left->limb_count == 0 ? 1 : left->limb_count) ||
        !magnitude_reserve(&b, right->limb_count == 0 ? 1 : right->limb_count)) {
        magnitude_free(&a);
        magnitude_free(&b);
        return 0;
    }
    memcpy(a.limbs, left->limbs, left->limb_count * sizeof(*a.limbs));
    a.count = left->limb_count;
    memcpy(b.limbs, right->limbs, right->limb_count * sizeof(*b.limbs));
    b.count = right->limb_count;

    /* A larger scale means more negative powers of ten, so the other side is
     * the one that must grow. */
    long difference = (long)left->scale - (long)right->scale;
    Magnitude *grow = difference > 0 ? &b : &a;
    long steps = difference > 0 ? difference : -difference;
    for (long step = 0; step < steps; ++step) {
        if (!magnitude_mul_add_small(grow, 10u, 0u)) {
            magnitude_free(&a);
            magnitude_free(&b);
            return 0;
        }
    }

    int order = magnitude_compare(&a, &b);
    magnitude_free(&a);
    magnitude_free(&b);
    return left->sign < 0 ? -order : order;
}

static char *magnitude_to_text(const KofunDecimal *value, bool signed_text) {
    if (value->sign == 0) {
        char *zero = malloc(2);
        if (zero == NULL) return NULL;
        zero[0] = '0';
        zero[1] = '\0';
        return zero;
    }

    Magnitude work;
    magnitude_init(&work);
    if (!magnitude_reserve(&work, value->limb_count)) return NULL;
    memcpy(work.limbs, value->limbs, value->limb_count * sizeof(*work.limbs));
    work.count = value->limb_count;

    /* Ten decimal digits per 32-bit limb is a safe upper bound. */
    size_t capacity = value->limb_count * 10 + 4;
    char *reversed = malloc(capacity);
    if (reversed == NULL) {
        magnitude_free(&work);
        return NULL;
    }
    size_t written = 0;
    while (!magnitude_is_zero(&work)) {
        uint32_t digit = magnitude_divmod_small(&work, 10u);
        reversed[written++] = (char)('0' + digit);
    }
    magnitude_free(&work);

    size_t prefix = (signed_text && value->sign < 0) ? 1u : 0u;
    char *text = malloc(written + prefix + 1);
    if (text == NULL) {
        free(reversed);
        return NULL;
    }
    if (prefix) text[0] = '-';
    for (size_t index = 0; index < written; ++index) {
        text[prefix + index] = reversed[written - 1 - index];
    }
    text[prefix + written] = '\0';
    free(reversed);
    return text;
}

char *kofun_decimal_significand_text(const KofunDecimal *value) {
    if (value == NULL) return NULL;
    return magnitude_to_text(value, true);
}

char *kofun_decimal_to_canonical_text(const KofunDecimal *value) {
    if (value == NULL) return NULL;
    char *significand = magnitude_to_text(value, true);
    if (significand == NULL) return NULL;
    /* `<significand>e<exponent>`, exponent being the negated scale, so the
     * rendering is the value and never a rounded decimal expansion. */
    size_t capacity = strlen(significand) + 16;
    char *text = malloc(capacity);
    if (text == NULL) {
        free(significand);
        return NULL;
    }
    long exponent = -(long)value->scale;
    int written = snprintf(text, capacity, "%se%ld", significand, exponent);
    free(significand);
    if (written < 0 || (size_t)written >= capacity) {
        free(text);
        return NULL;
    }
    return text;
}

/* --- binary64 ------------------------------------------------------------ */

/*
 * Correctly rounded binary64, computed from the same exact significand rather
 * than through `strtod`. The host's conversion is locale-sensitive and is not
 * guaranteed identical across backends, and #710 requires every backend to
 * observe the same value.
 *
 * The value is `magnitude * 10^exponent`. Both sides are scaled into integers
 * — numerator and denominator — and a long division produces enough bits to
 * round to nearest, ties to even.
 */
KofunDecimalStatus kofun_float_from_literal(
    const char *text,
    size_t length,
    double *out
) {
    if (text == NULL || out == NULL) return KOFUN_DECIMAL_MALFORMED;

    Magnitude magnitude;
    long scale = 0;
    KofunDecimalStatus status = build(text, length, &magnitude, &scale);
    if (status != KOFUN_DECIMAL_OK) return status;

    if (magnitude_is_zero(&magnitude)) {
        magnitude_free(&magnitude);
        *out = 0.0;
        return KOFUN_DECIMAL_OK;
    }

    /*
     * Reduce to `numerator / denominator` with both exact integers:
     * value = magnitude * 10^-scale.
     */
    Magnitude numerator;
    Magnitude denominator;
    magnitude_init(&numerator);
    magnitude_init(&denominator);
    if (!magnitude_reserve(&numerator, magnitude.count) ||
        !magnitude_reserve(&denominator, 1)) {
        magnitude_free(&magnitude);
        magnitude_free(&numerator);
        magnitude_free(&denominator);
        return KOFUN_DECIMAL_MEMORY;
    }
    memcpy(
        numerator.limbs,
        magnitude.limbs,
        magnitude.count * sizeof(*numerator.limbs)
    );
    numerator.count = magnitude.count;
    denominator.limbs[0] = 1u;
    denominator.count = 1;
    magnitude_free(&magnitude);

    for (long step = 0; step < scale; ++step) {
        if (!magnitude_mul_add_small(&denominator, 10u, 0u)) {
            magnitude_free(&numerator);
            magnitude_free(&denominator);
            return KOFUN_DECIMAL_MEMORY;
        }
    }
    for (long step = 0; step > scale; --step) {
        if (!magnitude_mul_add_small(&numerator, 10u, 0u)) {
            magnitude_free(&numerator);
            magnitude_free(&denominator);
            return KOFUN_DECIMAL_MEMORY;
        }
    }

    /*
     * Long division producing 54 significant bits plus a sticky flag: 53 for
     * the mantissa, one to round on, and stickiness to break ties correctly.
     */
    int exponent = 0;
    /* Align so the quotient's leading bit is known: scale the numerator up
     * until it is at least the denominator, and the denominator up while it
     * exceeds the numerator by more than a factor of two. */
    while (magnitude_compare(&numerator, &denominator) < 0) {
        if (!magnitude_mul_add_small(&numerator, 2u, 0u)) {
            magnitude_free(&numerator);
            magnitude_free(&denominator);
            return KOFUN_DECIMAL_MEMORY;
        }
        --exponent;
    }
    for (;;) {
        Magnitude twice;
        magnitude_init(&twice);
        if (!magnitude_reserve(&twice, denominator.count + 1)) {
            magnitude_free(&numerator);
            magnitude_free(&denominator);
            magnitude_free(&twice);
            return KOFUN_DECIMAL_MEMORY;
        }
        memcpy(
            twice.limbs,
            denominator.limbs,
            denominator.count * sizeof(*twice.limbs)
        );
        twice.count = denominator.count;
        if (!magnitude_mul_add_small(&twice, 2u, 0u)) {
            magnitude_free(&numerator);
            magnitude_free(&denominator);
            magnitude_free(&twice);
            return KOFUN_DECIMAL_MEMORY;
        }
        bool needs_shift = magnitude_compare(&numerator, &twice) >= 0;
        magnitude_free(&twice);
        if (!needs_shift) break;
        if (!magnitude_mul_add_small(&denominator, 2u, 0u)) {
            magnitude_free(&numerator);
            magnitude_free(&denominator);
            return KOFUN_DECIMAL_MEMORY;
        }
        ++exponent;
    }

    /*
     * numerator/denominator is now in [1, 2), and the value is that ratio
     * times 2^exponent.
     *
     * How many bits to extract is not a constant, and getting it wrong is how
     * subnormals go bad. A normal result has 53 significand bits wherever it
     * sits, so 54 bits — 53 plus one to round on — is right. A **subnormal**
     * result instead has a fixed ulp of 2^-1074 and *fewer* significand bits
     * the smaller it is, so extracting 54 and scaling down afterwards rounds
     * twice: once to 53 bits, once again on the way into the subnormal range.
     * That double rounding was measured against `strtod` on 3 of 3013 random
     * cases, all below 2^-1022.
     *
     * So the ulp is chosen first and the extraction is sized to it.
     */
    bool subnormal = exponent < -1022;
    int keep_bits = 54;
    if (subnormal) {
        /* Bits available above a fixed ulp of 2^-1074, plus one to round on. */
        keep_bits = (exponent + 1074) + 2;
    }
    if (keep_bits < 1) {
        /* Below half the smallest subnormal. */
        magnitude_free(&numerator);
        magnitude_free(&denominator);
        *out = 0.0;
        return KOFUN_DECIMAL_OK;
    }

    uint64_t mantissa = 0;
    bool sticky = false;
    for (int bit = 0; bit < keep_bits; ++bit) {
        mantissa <<= 1;
        if (magnitude_compare(&numerator, &denominator) >= 0) {
            mantissa |= 1u;
            /* numerator -= denominator, borrow-propagating subtract. */
            int64_t borrow = 0;
            for (size_t index = 0; index < numerator.count; ++index) {
                int64_t limb = (int64_t)numerator.limbs[index] - borrow -
                    (index < denominator.count
                         ? (int64_t)denominator.limbs[index]
                         : 0);
                if (limb < 0) {
                    limb += (int64_t)LIMB_BASE;
                    borrow = 1;
                } else {
                    borrow = 0;
                }
                numerator.limbs[index] = (uint32_t)limb;
            }
            magnitude_trim(&numerator);
        }
        if (!magnitude_mul_add_small(&numerator, 2u, 0u)) {
            magnitude_free(&numerator);
            magnitude_free(&denominator);
            return KOFUN_DECIMAL_MEMORY;
        }
    }
    sticky = !magnitude_is_zero(&numerator);
    magnitude_free(&numerator);
    magnitude_free(&denominator);

    /*
     * Round to nearest, ties to even, on the last extracted bit.
     *
     * The loop leaves `floor(ratio * 2^(keep_bits-1))`, so shifting the guard
     * bit off leaves `floor(ratio * 2^(keep_bits-2))` and the value is
     * `mantissa * 2^shift` for the shift below. For the normal case that is
     * `exponent - 52`; using 53 there halves every result, which is exactly
     * what it did before this was measured.
     */
    int shift = subnormal ? -1074 : exponent - 52;
    uint64_t guard = mantissa & 1u;
    mantissa >>= 1;
    if (guard && (sticky || (mantissa & 1u))) {
        ++mantissa;
        /*
         * Carrying out of 53 bits renormalizes. A subnormal that carries into
         * bit 52 has become the smallest normal, and `mantissa * 2^-1074` is
         * already exactly that, so only the normal case adjusts.
         */
        if (!subnormal && mantissa == (1ULL << 53)) {
            mantissa >>= 1;
            ++shift;
        }
    }

    /*
     * Compose without touching the host's decimal parser. Every intermediate
     * is `mantissa * 2^-k` with `mantissa < 2^53`, and the final value is at
     * least the smallest subnormal, so each halving is exact and no rounding
     * happens here.
     */
    double result = (double)mantissa;
    while (shift > 0) {
        result *= 2.0;
        --shift;
    }
    while (shift < 0) {
        result /= 2.0;
        ++shift;
    }
    *out = result;
    return KOFUN_DECIMAL_OK;
}
