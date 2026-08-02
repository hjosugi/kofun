/*
 * LeakSanitizer arm probe (#992).
 *
 * This program deliberately violates the fixture header's client-owned
 * handle contract. The allocation happens inside libkbfix.so and the helper
 * returns before process exit, so the leaked pointer is no longer an active
 * stack root when LeakSanitizer performs its final scan.
 */
#include "kbfix.h"

#include <stddef.h>

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static int leak_one_counter(void) {
    kbfix_counter_t *counter = kbfix_counter_new(7);
    if (counter == NULL) {
        return 2;
    }
    return kbfix_counter_value(counter) == 7 ? 0 : 3;
}

int main(void) {
    return leak_one_counter();
}
