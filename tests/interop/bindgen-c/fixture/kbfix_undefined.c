/*
 * UndefinedBehaviorSanitizer arm probe (#992).
 *
 * LONG_MAX multiplied by two overflows inside kbfix_stats_scale in the
 * sanitized fixture library. There is no memory fault in this program, so an
 * AddressSanitizer report cannot accidentally satisfy the UBSan assertion.
 */
#include "kbfix.h"

#include <limits.h>

int main(void) {
    kbfix_stats_t stats;
    kbfix_stats_t scaled;
    stats.total = LONG_MAX;
    stats.events = 1;
    stats.flags = 0;
    scaled = kbfix_stats_scale(stats, 2);
    (void)scaled;
    return 0;
}
