#define _GNU_SOURCE

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>

static long raw_syscall2(long number, long first, long second) {
    register long result __asm__("rax") = number;
    register long argument1 __asm__("rdi") = first;
    register long argument2 __asm__("rsi") = second;
    __asm__ volatile(
        "syscall"
        : "+a"(result)
        : "D"(argument1), "S"(argument2)
        : "rcx", "r11", "memory"
    );
    return result;
}

static int nanoseconds_are_canonical(const struct timespec *value) {
    return value->tv_nsec >= 0 && value->tv_nsec < 1000000000L;
}

static int fail(const char *message) {
    fprintf(stderr, "clock Linux x86-64 integration: FAIL: %s\n", message);
    return 1;
}

int main(void) {
    struct timespec monotonic = {0, 0};
    struct timespec realtime = {0, 0};
    struct timespec zero = {0, 0};
    struct timespec invalid = {0, 1000000000L};

    if (CLOCK_REALTIME != 0 || CLOCK_MONOTONIC != 1) {
        return fail("Linux clock IDs no longer match the adapter constants");
    }
    if (raw_syscall2(SYS_clock_gettime, CLOCK_MONOTONIC,
            (long)(uintptr_t)&monotonic) != 0 ||
        !nanoseconds_are_canonical(&monotonic)) {
        return fail("CLOCK_MONOTONIC did not return seconds plus canonical nanoseconds");
    }
    puts("clock_gettime monotonic: seconds plus nanoseconds in 0..999999999");

    if (raw_syscall2(SYS_clock_gettime, CLOCK_REALTIME,
            (long)(uintptr_t)&realtime) != 0 ||
        !nanoseconds_are_canonical(&realtime)) {
        return fail("CLOCK_REALTIME did not return seconds plus canonical nanoseconds");
    }
    puts("clock_gettime realtime: seconds plus nanoseconds in 0..999999999");

    if (raw_syscall2(SYS_clock_gettime, -1,
            (long)(uintptr_t)&realtime) != -EINVAL) {
        return fail("invalid clock ID did not return raw -EINVAL");
    }
    puts("clock_gettime invalid ID: raw -EINVAL");

    if (raw_syscall2(SYS_nanosleep, (long)(uintptr_t)&invalid, 0) != -EINVAL) {
        return fail("out-of-range nanosleep request did not return raw -EINVAL");
    }
    puts("nanosleep invalid nanoseconds: raw -EINVAL");

    if (raw_syscall2(SYS_nanosleep, (long)(uintptr_t)&zero, 0) != 0) {
        return fail("zero-duration nanosleep failed");
    }
    puts("nanosleep zero duration: success");
    return 0;
}
