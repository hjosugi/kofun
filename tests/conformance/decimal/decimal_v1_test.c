/*
 * Driver for the Decimal slice 2 runtime representation (#721).
 *
 * Each subcommand prints one deterministic line per case so the shell gate can
 * compare bytes rather than interpret. Nothing here decides pass or fail; the
 * gate does, against goldens.
 */

#include "decimal_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "decimal-test: %s\n", message);
    return 2;
}

/* `construct LITERAL...` — canonical rendering, scale, and significand. */
static int command_construct(int count, char **arguments) {
    for (int index = 0; index < count; ++index) {
        KofunDecimal value;
        KofunDecimalStatus status = kofun_decimal_from_literal(
            arguments[index],
            strlen(arguments[index]),
            &value
        );
        if (status != KOFUN_DECIMAL_OK) {
            printf(
                "%s -> %s\n",
                arguments[index],
                kofun_decimal_status_code(status)
            );
            continue;
        }
        char *canonical = kofun_decimal_to_canonical_text(&value);
        char *significand = kofun_decimal_significand_text(&value);
        if (canonical == NULL || significand == NULL) {
            free(canonical);
            free(significand);
            kofun_decimal_free(&value);
            return fail("out of memory rendering a value");
        }
        printf(
            "%s -> %s significand=%s scale=%d\n",
            arguments[index],
            canonical,
            significand,
            value.scale
        );
        free(canonical);
        free(significand);
        kofun_decimal_free(&value);
    }
    return 0;
}

/* `equal LEFT RIGHT ...` — pairwise structural equality and ordering. */
static int command_equal(int count, char **arguments) {
    if (count % 2 != 0) return fail("equal needs an even number of literals");
    for (int index = 0; index < count; index += 2) {
        KofunDecimal left;
        KofunDecimal right;
        KofunDecimalStatus left_status = kofun_decimal_from_literal(
            arguments[index], strlen(arguments[index]), &left);
        KofunDecimalStatus right_status = kofun_decimal_from_literal(
            arguments[index + 1], strlen(arguments[index + 1]), &right);
        if (left_status != KOFUN_DECIMAL_OK ||
            right_status != KOFUN_DECIMAL_OK) {
            printf("%s %s -> status\n", arguments[index], arguments[index + 1]);
            kofun_decimal_free(&left);
            kofun_decimal_free(&right);
            continue;
        }
        printf(
            "%s %s -> equal=%s compare=%d\n",
            arguments[index],
            arguments[index + 1],
            kofun_decimal_equal(&left, &right) ? "yes" : "no",
            kofun_decimal_compare(&left, &right)
        );
        kofun_decimal_free(&left);
        kofun_decimal_free(&right);
    }
    return 0;
}

/*
 * `storage LITERAL...` — the small-value path taken, beside every public
 * observation. Frozen decision 1 permits inline storage only as an
 * unobservable optimization, so the gate needs both halves on one line: the
 * path differs, the observations must not.
 */
static int command_storage(int count, char **arguments) {
    for (int index = 0; index < count; ++index) {
        KofunDecimal value;
        KofunDecimalStatus status = kofun_decimal_from_literal(
            arguments[index], strlen(arguments[index]), &value);
        if (status != KOFUN_DECIMAL_OK) {
            printf(
                "%s -> %s\n",
                arguments[index],
                kofun_decimal_status_code(status)
            );
            continue;
        }
        char *canonical = kofun_decimal_to_canonical_text(&value);
        if (canonical == NULL) {
            kofun_decimal_free(&value);
            return fail("out of memory rendering a value");
        }
        printf(
            "%s -> inline=%s %s scale=%d limbs=%zu\n",
            arguments[index],
            value.inline_storage ? "yes" : "no",
            canonical,
            value.scale,
            value.limb_count
        );
        free(canonical);
        kofun_decimal_free(&value);
    }
    return 0;
}

/* `float LITERAL...` — binary64 as exact hex, so no decimal rendering of the
 * host's own can hide a difference. */
static int command_float(int count, char **arguments) {
    for (int index = 0; index < count; ++index) {
        double result = 0.0;
        KofunDecimalStatus status = kofun_float_from_literal(
            arguments[index], strlen(arguments[index]), &result);
        if (status != KOFUN_DECIMAL_OK) {
            printf(
                "%s -> %s\n",
                arguments[index],
                kofun_decimal_status_code(status)
            );
            continue;
        }
        uint64_t bits = 0;
        memcpy(&bits, &result, sizeof(bits));
        printf("%s -> %016llx\n", arguments[index],
               (unsigned long long)bits);
    }
    return 0;
}

/* `profile` — the versioned limits, as one record. */
static int command_profile(void) {
    printf("profile-version=%u\n", kofun_decimal_profile_version());
    printf("max-significand-digits=%u\n",
           (unsigned)KOFUN_DECIMAL_MAX_SIGNIFICAND_DIGITS);
    printf("max-scale=%ld\n", (long)KOFUN_DECIMAL_MAX_SCALE);
    printf("min-scale=%ld\n", (long)KOFUN_DECIMAL_MIN_SCALE);
    for (int status = 0; status <= 7; ++status) {
        printf(
            "status=%s message=%s\n",
            kofun_decimal_status_code((KofunDecimalStatus)status),
            kofun_decimal_status_message((KofunDecimalStatus)status)
        );
    }
    return 0;
}

static bool parse_rounding(
    const char *name,
    KofunDecimalRounding *mode
) {
    if (strcmp(name, "HalfUp") == 0) {
        *mode = KOFUN_DECIMAL_HALF_UP;
    } else if (strcmp(name, "HalfEven") == 0) {
        *mode = KOFUN_DECIMAL_HALF_EVEN;
    } else if (strcmp(name, "TowardZero") == 0) {
        *mode = KOFUN_DECIMAL_TOWARD_ZERO;
    } else if (strcmp(name, "Floor") == 0) {
        *mode = KOFUN_DECIMAL_FLOOR;
    } else if (strcmp(name, "Ceiling") == 0) {
        *mode = KOFUN_DECIMAL_CEILING;
    } else {
        return false;
    }
    return true;
}

/* `round VALUE SCALE MODE ...` — one explicit rounding boundary per triple. */
static int command_round(int count, char **arguments) {
    if (count % 3 != 0) return fail("round needs VALUE SCALE MODE triples");
    for (int index = 0; index < count; index += 3) {
        KofunDecimal source;
        KofunDecimal result;
        kofun_decimal_init(&source);
        kofun_decimal_init(&result);
        KofunDecimalRounding mode;
        if (!parse_rounding(arguments[index + 2], &mode) ||
            kofun_decimal_parse(
                arguments[index], strlen(arguments[index]), &source) !=
                KOFUN_DECIMAL_OK) {
            kofun_decimal_free(&source);
            return fail("invalid round case");
        }
        long scale = strtol(arguments[index + 1], NULL, 10);
        KofunDecimalStatus status = kofun_decimal_round(
            &source, scale, mode, &result);
        if (status != KOFUN_DECIMAL_OK) {
            printf("%s scale=%ld %s -> %s\n", arguments[index], scale,
                   arguments[index + 2], kofun_decimal_status_code(status));
        } else {
            char *canonical = kofun_decimal_to_canonical_text(&result);
            char *significand = kofun_decimal_significand_text(&result);
            if (canonical == NULL || significand == NULL) {
                free(canonical);
                free(significand);
                kofun_decimal_free(&source);
                kofun_decimal_free(&result);
                return fail("out of memory rendering a rounded value");
            }
            printf(
                "%s scale=%ld %s -> %s significand=%s scale=%d\n",
                arguments[index], scale, arguments[index + 2], canonical,
                significand, result.scale);
            free(canonical);
            free(significand);
        }
        kofun_decimal_free(&source);
        kofun_decimal_free(&result);
    }
    return 0;
}

/* `rounded-divide LEFT RIGHT SCALE MODE ...` — explicit rounded division. */
static int command_rounded_divide(int count, char **arguments) {
    if (count % 4 != 0) {
        return fail("rounded-divide needs LEFT RIGHT SCALE MODE groups");
    }
    for (int index = 0; index < count; index += 4) {
        KofunDecimal left;
        KofunDecimal right;
        KofunDecimal result;
        kofun_decimal_init(&left);
        kofun_decimal_init(&right);
        kofun_decimal_init(&result);
        KofunDecimalRounding mode;
        if (!parse_rounding(arguments[index + 3], &mode) ||
            kofun_decimal_parse(
                arguments[index], strlen(arguments[index]), &left) !=
                KOFUN_DECIMAL_OK ||
            kofun_decimal_parse(
                arguments[index + 1], strlen(arguments[index + 1]), &right) !=
                KOFUN_DECIMAL_OK) {
            kofun_decimal_free(&left);
            kofun_decimal_free(&right);
            return fail("invalid rounded-divide case");
        }
        long scale = strtol(arguments[index + 2], NULL, 10);
        KofunDecimalStatus status = kofun_decimal_divide_rounded(
            &left, &right, scale, mode, &result);
        if (status != KOFUN_DECIMAL_OK) {
            printf("%s / %s scale=%ld %s -> %s\n", arguments[index],
                   arguments[index + 1], scale, arguments[index + 3],
                   kofun_decimal_status_code(status));
        } else {
            char *canonical = kofun_decimal_to_canonical_text(&result);
            if (canonical == NULL) {
                kofun_decimal_free(&left);
                kofun_decimal_free(&right);
                kofun_decimal_free(&result);
                return fail("out of memory rendering a quotient");
            }
            printf("%s / %s scale=%ld %s -> %s\n", arguments[index],
                   arguments[index + 1], scale, arguments[index + 3],
                   canonical);
            free(canonical);
        }
        kofun_decimal_free(&left);
        kofun_decimal_free(&right);
        kofun_decimal_free(&result);
    }
    return 0;
}

/* `format-parse VALUE SCALE ...` — display text and canonical round trip. */
static int command_format_parse(int count, char **arguments) {
    if (count % 2 != 0) return fail("format-parse needs VALUE SCALE pairs");
    for (int index = 0; index < count; index += 2) {
        KofunDecimal source;
        KofunDecimal parsed;
        kofun_decimal_init(&source);
        kofun_decimal_init(&parsed);
        if (kofun_decimal_parse(
                arguments[index], strlen(arguments[index]), &source) !=
                KOFUN_DECIMAL_OK) {
            return fail("invalid format source");
        }
        long scale = strtol(arguments[index + 1], NULL, 10);
        char *formatted = NULL;
        KofunDecimalStatus status = kofun_decimal_format(
            &source, scale, &formatted);
        if (status != KOFUN_DECIMAL_OK) {
            printf("%s scale=%ld -> %s\n", arguments[index], scale,
                   kofun_decimal_status_code(status));
        } else {
            KofunDecimalStatus parse_status = kofun_decimal_parse(
                formatted, strlen(formatted), &parsed);
            printf("%s scale=%ld -> %s roundtrip=%s\n", arguments[index],
                   scale, formatted,
                   parse_status == KOFUN_DECIMAL_OK &&
                           kofun_decimal_equal(&source, &parsed)
                       ? "equal"
                       : "different");
        }
        free(formatted);
        kofun_decimal_free(&source);
        kofun_decimal_free(&parsed);
    }
    return 0;
}

/*
 * `limit KIND` — a generated one-over case, so the limits are exercised
 * without a multi-kilobyte fixture in the repository.
 */
static int command_limit(const char *kind) {
    size_t length = 0;
    char *text = NULL;
    if (strcmp(kind, "digits-at") == 0 || strcmp(kind, "digits-over") == 0) {
        size_t digits = KOFUN_DECIMAL_MAX_SIGNIFICAND_DIGITS;
        if (strcmp(kind, "digits-over") == 0) ++digits;
        text = malloc(digits + 1);
        if (text == NULL) return fail("out of memory building a limit case");
        /* A leading 1 then zeros would canonicalize away; use 9s so every
         * digit survives into the significand. */
        memset(text, '9', digits);
        text[digits] = '\0';
        length = digits;
    } else if (strcmp(kind, "scale-at") == 0 ||
               strcmp(kind, "scale-over") == 0) {
        long scale = KOFUN_DECIMAL_MAX_SCALE;
        if (strcmp(kind, "scale-over") == 0) ++scale;
        text = malloc(64);
        if (text == NULL) return fail("out of memory building a limit case");
        snprintf(text, 64, "1e-%ld", scale);
        length = strlen(text);
    } else {
        return fail("unknown limit kind");
    }

    KofunDecimal value;
    KofunDecimalStatus status = kofun_decimal_from_literal(
        text, length, &value);
    if (status == KOFUN_DECIMAL_OK) {
        printf("%s -> ok scale=%d limbs=%zu\n", kind, value.scale,
               value.limb_count);
    } else {
        printf("%s -> %s\n", kind, kofun_decimal_status_code(status));
    }
    kofun_decimal_free(&value);
    free(text);
    return 0;
}

/*
 * `arith OP LEFT RIGHT ...` — one exact operation per operand pair.
 *
 * The result is printed as canonical text *and* as significand/scale, because
 * the two catch different mistakes: canonical text would hide a result that
 * carried the right value with an uncanonical scale, and significand/scale
 * alone would not show that `0.1 + 0.2` and `0.3` render identically.
 */
static int command_arith(const char *op, int count, char **arguments) {
    for (int index = 0; index + 1 < count; index += 2) {
        KofunDecimal left;
        KofunDecimal right;
        KofunDecimal result;
        kofun_decimal_init(&left);
        kofun_decimal_init(&right);
        kofun_decimal_init(&result);
        if (kofun_decimal_from_literal(
                arguments[index], strlen(arguments[index]), &left) !=
                KOFUN_DECIMAL_OK ||
            kofun_decimal_from_literal(
                arguments[index + 1], strlen(arguments[index + 1]), &right) !=
                KOFUN_DECIMAL_OK) {
            kofun_decimal_free(&left);
            kofun_decimal_free(&right);
            return fail("an operand is not a valid literal");
        }
        KofunDecimalStatus status;
        KofunDecimalDivision outcome = KOFUN_DECIMAL_DIVISION_EXACT;
        if (strcmp(op, "add") == 0) {
            status = kofun_decimal_add(&left, &right, &result);
        } else if (strcmp(op, "sub") == 0) {
            status = kofun_decimal_subtract(&left, &right, &result);
        } else if (strcmp(op, "mul") == 0) {
            status = kofun_decimal_multiply(&left, &right, &result);
        } else {
            status = kofun_decimal_divide_exact(
                &left, &right, &result, &outcome);
        }
        if (status != KOFUN_DECIMAL_OK) {
            printf(
                "%s %s %s -> %s\n",
                arguments[index],
                op,
                arguments[index + 1],
                kofun_decimal_status_code(status)
            );
        } else if (outcome != KOFUN_DECIMAL_DIVISION_EXACT) {
            printf(
                "%s %s %s -> %s\n",
                arguments[index],
                op,
                arguments[index + 1],
                kofun_decimal_division_name(outcome)
            );
        } else {
            char *canonical = kofun_decimal_to_canonical_text(&result);
            char *significand = kofun_decimal_significand_text(&result);
            if (canonical == NULL || significand == NULL) {
                free(canonical);
                free(significand);
                kofun_decimal_free(&left);
                kofun_decimal_free(&right);
                kofun_decimal_free(&result);
                return fail("out of memory rendering a result");
            }
            printf(
                "%s %s %s -> %s significand=%s scale=%d\n",
                arguments[index],
                op,
                arguments[index + 1],
                canonical,
                significand,
                result.scale
            );
            free(canonical);
            free(significand);
        }
        kofun_decimal_free(&left);
        kofun_decimal_free(&right);
        kofun_decimal_free(&result);
    }
    return 0;
}

/*
 * `identity LEFT RIGHT SUM ...` — the headline acceptance criterion of #710,
 * as an equality rather than a rendering: `0.1 + 0.2 == 0.3`.
 *
 * This is deliberately not "does it print 0.3". A backend that rendered
 * correctly while comparing unequal would pass a rendering check and fail
 * every program a user writes.
 */
static int command_identity(int count, char **arguments) {
    for (int index = 0; index + 2 < count; index += 3) {
        KofunDecimal left;
        KofunDecimal right;
        KofunDecimal expected;
        KofunDecimal sum;
        kofun_decimal_init(&left);
        kofun_decimal_init(&right);
        kofun_decimal_init(&expected);
        kofun_decimal_init(&sum);
        if (kofun_decimal_from_literal(
                arguments[index], strlen(arguments[index]), &left) !=
                KOFUN_DECIMAL_OK ||
            kofun_decimal_from_literal(
                arguments[index + 1], strlen(arguments[index + 1]), &right) !=
                KOFUN_DECIMAL_OK ||
            kofun_decimal_from_literal(
                arguments[index + 2], strlen(arguments[index + 2]),
                &expected) != KOFUN_DECIMAL_OK) {
            kofun_decimal_free(&left);
            kofun_decimal_free(&right);
            kofun_decimal_free(&expected);
            return fail("an operand is not a valid literal");
        }
        KofunDecimalStatus status = kofun_decimal_add(&left, &right, &sum);
        if (status != KOFUN_DECIMAL_OK) {
            printf(
                "%s + %s == %s -> %s\n",
                arguments[index],
                arguments[index + 1],
                arguments[index + 2],
                kofun_decimal_status_code(status)
            );
        } else {
            /*
             * The binary64 answer is printed beside the decimal one. That is
             * the whole point of keeping the two types apart: this line is
             * where `0.30000000000000004` shows up, and it is evidence that
             * the decimal path is not quietly going through a double.
             */
            double binary = strtod(arguments[index], NULL) +
                            strtod(arguments[index + 1], NULL);
            printf(
                "%s + %s == %s -> %s binary64=%.17g\n",
                arguments[index],
                arguments[index + 1],
                arguments[index + 2],
                kofun_decimal_equal(&sum, &expected) ? "true" : "false",
                binary
            );
        }
        kofun_decimal_free(&left);
        kofun_decimal_free(&right);
        kofun_decimal_free(&expected);
        kofun_decimal_free(&sum);
    }
    return 0;
}

/*
 * `contrast OP LEFT RIGHT ...` — the same operation on both types, side by
 * side.
 *
 * This is the corpus that proves `Decimal` and `Float` cannot be conflated.
 * Each line carries the exact decimal answer and the binary64 one, so a
 * backend that implemented one type by delegating to the other would produce
 * two identical columns and fail every line where they must differ.
 *
 * Division is where the two differ in kind rather than in digits: Decimal
 * reports `DivisionByZero` and yields no value, while binary64 yields an
 * infinity and carries on.
 */
static int command_contrast(int count, char **arguments) {
    for (int index = 0; index + 2 < count; index += 3) {
        const char *op = arguments[index];
        const char *left_text = arguments[index + 1];
        const char *right_text = arguments[index + 2];

        KofunDecimal left;
        KofunDecimal right;
        KofunDecimal result;
        kofun_decimal_init(&left);
        kofun_decimal_init(&right);
        kofun_decimal_init(&result);
        double left_binary = 0.0;
        double right_binary = 0.0;
        if (kofun_decimal_from_literal(
                left_text, strlen(left_text), &left) != KOFUN_DECIMAL_OK ||
            kofun_decimal_from_literal(
                right_text, strlen(right_text), &right) != KOFUN_DECIMAL_OK ||
            kofun_float_from_literal(
                left_text, strlen(left_text), &left_binary) !=
                KOFUN_DECIMAL_OK ||
            kofun_float_from_literal(
                right_text, strlen(right_text), &right_binary) !=
                KOFUN_DECIMAL_OK) {
            kofun_decimal_free(&left);
            kofun_decimal_free(&right);
            return fail("an operand is not a valid literal");
        }

        KofunDecimalStatus status;
        KofunDecimalDivision outcome = KOFUN_DECIMAL_DIVISION_EXACT;
        double binary;
        if (strcmp(op, "add") == 0) {
            status = kofun_decimal_add(&left, &right, &result);
            binary = kofun_float_add(left_binary, right_binary);
        } else if (strcmp(op, "sub") == 0) {
            status = kofun_decimal_subtract(&left, &right, &result);
            binary = kofun_float_subtract(left_binary, right_binary);
        } else if (strcmp(op, "mul") == 0) {
            status = kofun_decimal_multiply(&left, &right, &result);
            binary = kofun_float_multiply(left_binary, right_binary);
        } else {
            status = kofun_decimal_divide_exact(
                &left, &right, &result, &outcome);
            binary = kofun_float_divide(left_binary, right_binary);
        }

        char decimal_text[64];
        if (status != KOFUN_DECIMAL_OK) {
            snprintf(decimal_text, sizeof decimal_text, "%s",
                     kofun_decimal_status_code(status));
        } else if (outcome != KOFUN_DECIMAL_DIVISION_EXACT) {
            snprintf(decimal_text, sizeof decimal_text, "%s",
                     kofun_decimal_division_name(outcome));
        } else {
            char *canonical = kofun_decimal_to_canonical_text(&result);
            if (canonical == NULL) {
                kofun_decimal_free(&left);
                kofun_decimal_free(&right);
                kofun_decimal_free(&result);
                return fail("out of memory rendering a result");
            }
            snprintf(decimal_text, sizeof decimal_text, "%s", canonical);
            free(canonical);
        }
        printf(
            "%s %s %s -> Decimal=%s Float=%.17g\n",
            left_text,
            op,
            right_text,
            decimal_text,
            binary
        );
        kofun_decimal_free(&left);
        kofun_decimal_free(&right);
        kofun_decimal_free(&result);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return fail("usage: decimal-test COMMAND [ARGS...]");
    const char *command = argv[1];
    if (strcmp(command, "add") == 0 || strcmp(command, "sub") == 0 ||
        strcmp(command, "mul") == 0 || strcmp(command, "div") == 0) {
        return command_arith(command, argc - 2, argv + 2);
    }
    if (strcmp(command, "contrast") == 0) {
        return command_contrast(argc - 2, argv + 2);
    }
    if (strcmp(command, "identity") == 0) {
        return command_identity(argc - 2, argv + 2);
    }
    if (strcmp(command, "round") == 0) {
        return command_round(argc - 2, argv + 2);
    }
    if (strcmp(command, "rounded-divide") == 0) {
        return command_rounded_divide(argc - 2, argv + 2);
    }
    if (strcmp(command, "format-parse") == 0) {
        return command_format_parse(argc - 2, argv + 2);
    }
    if (strcmp(command, "construct") == 0) {
        return command_construct(argc - 2, argv + 2);
    }
    if (strcmp(command, "equal") == 0) return command_equal(argc - 2, argv + 2);
    if (strcmp(command, "storage") == 0) {
        return command_storage(argc - 2, argv + 2);
    }
    if (strcmp(command, "float") == 0) return command_float(argc - 2, argv + 2);
    if (strcmp(command, "profile") == 0) return command_profile();
    if (strcmp(command, "limit") == 0) {
        if (argc < 3) return fail("limit needs a kind");
        return command_limit(argv[2]);
    }
    return fail("unknown command");
}
