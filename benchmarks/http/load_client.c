#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char request[] =
    "GET /bench HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "Connection: keep-alive\r\n"
    "\r\n";

static const char response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 11\r\n"
    "Connection: keep-alive\r\n"
    "\r\n"
    "{\"ok\":true}";

static uint64_t monotonic_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        perror("clock_gettime");
        exit(2);
    }
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
        (uint64_t)value.tv_nsec;
}

static bool parse_unsigned(const char *text, unsigned long *result) {
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0) {
        return false;
    }
    *result = value;
    return true;
}

static int connect_server(unsigned long port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct timeval timeout = {
        .tv_sec = 5,
        .tv_usec = 0,
    };
    (void)setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    );
    (void)setsockopt(
        fd,
        SOL_SOCKET,
        SO_SNDTIMEO,
        &timeout,
        sizeof(timeout)
    );
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(
            fd,
            (const struct sockaddr *)&address,
            sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool write_exact(int fd, const char *bytes, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t count = send(
            fd,
            bytes + sent,
            length - sent,
            MSG_NOSIGNAL
        );
        if (count > 0) {
            sent += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

static bool read_response(int fd) {
    char actual[sizeof(response) - 1];
    size_t received = 0;
    while (received < sizeof(actual)) {
        ssize_t count = read(
            fd,
            actual + received,
            sizeof(actual) - received
        );
        if (count > 0) {
            received += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        return false;
    }
    return memcmp(actual, response, sizeof(actual)) == 0;
}

static int worker(unsigned long port, unsigned long requests) {
    int fd = connect_server(port);
    if (fd < 0) return 1;
    for (unsigned long index = 0; index < requests; ++index) {
        if (!write_exact(fd, request, sizeof(request) - 1) ||
            !read_response(fd)) {
            close(fd);
            return 1;
        }
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    unsigned long port;
    unsigned long total_requests;
    unsigned long concurrency;
    if (argc != 4 ||
        !parse_unsigned(argv[1], &port) ||
        port > UINT16_MAX ||
        !parse_unsigned(argv[2], &total_requests) ||
        !parse_unsigned(argv[3], &concurrency) ||
        concurrency > 128 ||
        total_requests < concurrency) {
        fprintf(
            stderr,
            "usage: %s PORT REQUESTS CONCURRENCY\n",
            argv[0]
        );
        return 2;
    }

    uint64_t started = monotonic_ns();
    unsigned long assigned = 0;
    for (unsigned long index = 0; index < concurrency; ++index) {
        unsigned long count =
            total_requests / concurrency +
            (index < total_requests % concurrency ? 1UL : 0UL);
        pid_t child = fork();
        if (child < 0) {
            perror("fork");
            return 2;
        }
        if (child == 0) {
            _exit(worker(port, count));
        }
        ++assigned;
    }

    int failed = 0;
    for (unsigned long index = 0; index < assigned; ++index) {
        int status = 0;
        pid_t child;
        do {
            child = wait(&status);
        } while (child < 0 && errno == EINTR);
        if (child < 0 ||
            !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            failed = 1;
        }
    }
    if (failed) {
        fputs("load client worker failed\n", stderr);
        return 1;
    }
    uint64_t elapsed = monotonic_ns() - started;
    printf("%" PRIu64 "\n", elapsed);
    return 0;
}
