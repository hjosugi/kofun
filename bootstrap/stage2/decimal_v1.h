#ifndef KOFUN_STAGE2_DECIMAL_V1_H
#define KOFUN_STAGE2_DECIMAL_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Compiler-native Decimal, slice 2 of #710: the runtime representation, its
 * canonical form, and the versioned resource profile that bounds it.
 *
 * A Decimal is a canonical `(significand, scale)` pair whose significand is an
 * arbitrary-precision binary integer, per #710's frozen decision 1. The value
 * is `significand * 10^-scale`. Scale may be negative, so `1e3` and `1000`
 * are the same canonical value.
 *
 * There is no arithmetic here. Slice 2 constructs, canonicalizes, compares and
 * renders; the operators are slices 3 and 4. `docs/DECIMAL.md` is normative.
 */

/*
 * Resource profile. `docs/DECIMAL.md` says the first concrete thresholds "are
 * deferred, but must be versioned together when introduced" — this is that
 * introduction, so the version and every limit move as one unit.
 *
 * Every backend registered for this profile observes these limits and these
 * diagnostics. Exceeding one fails before an unbounded allocation and never
 * clamps, wraps, rounds, or changes representation.
 */
#define KOFUN_DECIMAL_PROFILE_VERSION 1u
#define KOFUN_DECIMAL_MAX_SIGNIFICAND_DIGITS 4096u
#define KOFUN_DECIMAL_MAX_SCALE 6144L
#define KOFUN_DECIMAL_MIN_SCALE (-6144L)

typedef enum {
    KOFUN_DECIMAL_OK = 0,
    /* More significand digits than the profile admits. */
    KOFUN_DECIMAL_DIGIT_LIMIT = 1,
    /* Scale outside the profile's range, before or after canonicalization. */
    KOFUN_DECIMAL_SCALE_LIMIT = 2,
    /* Not a literal this profile's grammar accepts. */
    KOFUN_DECIMAL_MALFORMED = 3,
    /* Allocation refused. */
    KOFUN_DECIMAL_MEMORY = 4
} KofunDecimalStatus;

/* Stable diagnostic code, `D001`-style, or "" for OK. */
const char *kofun_decimal_status_code(KofunDecimalStatus status);

/* Stable one-line message with no source location, or "" for OK. */
const char *kofun_decimal_status_message(KofunDecimalStatus status);

unsigned kofun_decimal_profile_version(void);

/*
 * A canonical Decimal.
 *
 * `sign` is -1, 0 or +1, and is 0 exactly when the value is zero. A zero
 * carries `scale == 0` and no limbs, so there is one representation of zero.
 *
 * `limbs` is the significand's magnitude in base 2^32, least significant
 * first, with no leading zero limb. Canonical form additionally requires that
 * the magnitude is not divisible by ten: trailing decimal zeros are moved into
 * `scale` instead, which is what makes `1.0`, `1.00` and `1` one value
 * (#710 frozen decision 6).
 *
 * `inline_storage` records whether the significand fits the small-value path.
 * It is diagnostics for the conformance gate only. Frozen decision 1 permits
 * small storage solely as an unobservable optimization, so nothing outside the
 * gate that proves that invisibility may branch on it.
 */
typedef struct {
    int sign;
    int32_t scale;
    uint32_t *limbs;
    size_t limb_count;
    bool inline_storage;
} KofunDecimal;

void kofun_decimal_init(KofunDecimal *value);
void kofun_decimal_free(KofunDecimal *value);

/*
 * Build a canonical Decimal from a literal, which must be the exact bytes the
 * lexer retained: `digits [ "." digits ] [ exponent ]`, `_` only between two
 * digits, no `f64` suffix and no sign.
 *
 * The digits are consumed as written and scaled by the exponent. No host
 * `double` is on this path, which `docs/DECIMAL.md` requires.
 */
KofunDecimalStatus kofun_decimal_from_literal(
    const char *text,
    size_t length,
    KofunDecimal *out
);

/* Two canonical values are equal exactly when their fields match. */
bool kofun_decimal_equal(const KofunDecimal *left, const KofunDecimal *right);

/* -1, 0, +1 by numeric value. */
int kofun_decimal_compare(const KofunDecimal *left, const KofunDecimal *right);

/*
 * `<significand>e<-scale>`, the canonical observation used by the gates: two
 * spellings of one value render identically, and the rendering is exact
 * because it never leaves the integer domain. Caller frees.
 */
char *kofun_decimal_to_canonical_text(const KofunDecimal *value);

/* Signed decimal digits of the significand. Caller frees. */
char *kofun_decimal_significand_text(const KofunDecimal *value);

/*
 * Binary64 for the `f64` suffix, correctly rounded to nearest-even from the
 * exact decimal value.
 *
 * `literal` is the literal without its `f64` suffix. This deliberately does
 * not go through `strtod`: the conversion must be identical on every backend
 * and independent of the host locale, so it is computed from the same
 * arbitrary-precision significand Decimal uses.
 */
KofunDecimalStatus kofun_float_from_literal(
    const char *text,
    size_t length,
    double *out
);

/* --- exact arithmetic (slice 4 of #710, issue #723) ----------------------- */

/*
 * Addition, subtraction and multiplication are exact: the result's scale
 * follows from the operands and no digit is discarded (frozen decision 5).
 * `+` and `-` align the two scales with an exact power of ten and take the
 * larger; `*` adds the scales. The result is canonicalized, so trailing
 * decimal zeros produced by the operation move into the scale and
 * `0.1 + 0.2` and `0.3` are one value.
 *
 * The only failures are resource failures: a result may exceed the profile's
 * digit or scale limit, and then it fails rather than rounding. There is no
 * rounding mode here because these operations never round.
 *
 * `out` is initialized by the callee and owned by the caller on success; on
 * failure it is left as a valid empty value that `kofun_decimal_free` accepts.
 */
KofunDecimalStatus kofun_decimal_add(
    const KofunDecimal *left,
    const KofunDecimal *right,
    KofunDecimal *out
);
KofunDecimalStatus kofun_decimal_subtract(
    const KofunDecimal *left,
    const KofunDecimal *right,
    KofunDecimal *out
);
KofunDecimalStatus kofun_decimal_multiply(
    const KofunDecimal *left,
    const KofunDecimal *right,
    KofunDecimal *out
);

/*
 * The outcome of an exact division, which is a fact about the two values
 * rather than a resource failure — so it is a separate enum from
 * `KofunDecimalStatus`. Conflating them would let a caller treat "this
 * quotient needs more digits than the profile allows" and "this quotient does
 * not terminate at all" as the same thing; only the first would be fixed by a
 * larger profile.
 *
 * There are exactly three outcomes and no fourth. In particular there is no
 * "rounded" outcome: rounded division is a different operation that requires a
 * destination scale and a mode, and it is slice 5.
 */
typedef enum {
    KOFUN_DECIMAL_DIVISION_EXACT = 0,
    KOFUN_DECIMAL_DIVISION_INEXACT = 1,
    KOFUN_DECIMAL_DIVISION_BY_ZERO = 2
} KofunDecimalDivision;

/* Stable spelling of an outcome, matching `docs/DECIMAL.md`. */
const char *kofun_decimal_division_name(KofunDecimalDivision outcome);

/*
 * Exact division. `out` receives the quotient only when `*outcome` is
 * `KOFUN_DECIMAL_DIVISION_EXACT`; otherwise it is a valid empty value.
 *
 * A quotient terminates exactly when, after reducing, the denominator has no
 * prime factor other than two and five. This is decided rather than
 * approximated: the divisor's factors of two and five are removed, and the
 * quotient is exact precisely when what remains divides the dividend.
 */
KofunDecimalStatus kofun_decimal_divide_exact(
    const KofunDecimal *left,
    const KofunDecimal *right,
    KofunDecimal *out,
    KofunDecimalDivision *outcome
);

/*
 * The same four operations on `Float`, where they are binary64 and therefore
 * *not* exact. They exist here, beside the exact ones, because keeping the two
 * types apart is only meaningful if both are implemented and the difference is
 * observable — `0.1 + 0.2` is `0.3` in one and `0.30000000000000004` in the
 * other, and a corpus that showed only the Decimal side would not prove the
 * types are distinct.
 *
 * These are thin wrappers on the host's binary64 operations. That is the
 * point: `Float` is IEEE 754 binary64 with its ordinary behavior, including
 * infinities from division by zero rather than the checked outcome Decimal
 * gives. There is no rounding-mode argument because binary64 rounding is
 * round-to-nearest-even and is a property of the type, not a choice.
 */
double kofun_float_add(double left, double right);
double kofun_float_subtract(double left, double right);
double kofun_float_multiply(double left, double right);
double kofun_float_divide(double left, double right);

#endif
