#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
    SAMPLE_COUNT = 11,
    MEDIAN_INDEX = SAMPLE_COUNT / 2,
};

static int compare_u64(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static uint64_t micros(struct timeval value) {
    return (uint64_t)value.tv_sec * UINT64_C(1000000) +
        (uint64_t)value.tv_usec;
}

static int run_once(char **command, uint64_t *cpu_us) {
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return 1;
    }
    if (child == 0) {
        int null_output = open("/dev/null", O_WRONLY);
        if (null_output < 0 || dup2(null_output, STDOUT_FILENO) < 0) {
            perror("/dev/null");
            _exit(126);
        }
        close(null_output);
        execvp(command[0], command);
        perror(command[0]);
        _exit(127);
    }

    int status = 0;
    struct rusage usage;
    while (wait4(child, &status, 0, &usage) < 0) {
        if (errno == EINTR) continue;
        perror("wait4");
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "measured command failed\n");
        return 1;
    }
    *cpu_us = micros(usage.ru_utime) + micros(usage.ru_stime);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: process_cpu_time COMMAND [ARG...]\n");
        return 2;
    }

    uint64_t samples[SAMPLE_COUNT];
    for (size_t index = 0; index < SAMPLE_COUNT; ++index) {
        if (run_once(&argv[1], &samples[index]) != 0) return 1;
    }
    qsort(samples, SAMPLE_COUNT, sizeof(samples[0]), compare_u64);
    printf("%" PRIu64 "\n", samples[MEDIAN_INDEX]);
    return 0;
}
