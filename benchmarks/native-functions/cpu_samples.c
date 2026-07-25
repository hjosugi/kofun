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

/*
 * Runs one command repeatedly and prints the process CPU microseconds of every
 * measured run, one sample per line. The first run is a discarded warm-up.
 * Only measurement lives here: the benchmark script owns the median, the
 * dispersion, and every recorded budget.
 */

enum {
    MAX_SAMPLES = 999,
};

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
        fprintf(stderr, "cpu_samples: measured command failed\n");
        return 1;
    }
    *cpu_us =
        (uint64_t)usage.ru_utime.tv_sec * UINT64_C(1000000) +
        (uint64_t)usage.ru_utime.tv_usec +
        (uint64_t)usage.ru_stime.tv_sec * UINT64_C(1000000) +
        (uint64_t)usage.ru_stime.tv_usec;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: cpu_samples SAMPLES COMMAND [ARG...]\n");
        return 2;
    }

    char *end = NULL;
    long samples = strtol(argv[1], &end, 10);
    if (end == NULL || *end != '\0' || samples < 1 || samples > MAX_SAMPLES) {
        fprintf(
            stderr,
            "cpu_samples: SAMPLES must be 1..%d\n",
            (int)MAX_SAMPLES
        );
        return 2;
    }

    uint64_t warmup = 0;
    if (run_once(&argv[2], &warmup) != 0) return 1;
    for (long index = 0; index < samples; ++index) {
        uint64_t cpu_us = 0;
        if (run_once(&argv[2], &cpu_us) != 0) return 1;
        printf("%" PRIu64 "\n", cpu_us);
    }
    return 0;
}
