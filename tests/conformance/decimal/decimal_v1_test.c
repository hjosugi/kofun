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
    for (int status = 0; status <= 4; ++status) {
        printf(
            "status=%s message=%s\n",
            kofun_decimal_status_code((KofunDecimalStatus)status),
            kofun_decimal_status_message((KofunDecimalStatus)status)
        );
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

int main(int argc, char **argv) {
    if (argc < 2) return fail("usage: decimal-test COMMAND [ARGS...]");
    const char *command = argv[1];
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
