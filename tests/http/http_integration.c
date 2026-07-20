#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum {
    IO_TIMEOUT_MS = 3000,
    SHUTDOWN_TIMEOUT_MS = 5000,
    RESPONSE_CAPACITY = 8192,
};

typedef struct {
    pid_t pid;
    int output_fd;
    int error_fd;
    uint16_t port;
    bool reaped;
} Server;

static Server *active_server;
static const char *test_step = "startup";

static int64_t now_ms(void) {
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        perror("clock_gettime");
        exit(1);
    }
    return (int64_t)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}

static void emergency_cleanup(void) {
    int status;

    if (active_server == NULL || active_server->pid <= 0 ||
        active_server->reaped) {
        return;
    }
    (void)kill(active_server->pid, SIGKILL);
    while (waitpid(active_server->pid, &status, 0) < 0 && errno == EINTR) {
    }
    active_server->reaped = true;
}

static void fail(const char *format, ...) {
    va_list arguments;

    fprintf(stderr, "http integration [%s]: ", test_step);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    emergency_cleanup();
    exit(1);
}

static short poll_one(int fd, short events, int64_t deadline) {
    struct pollfd descriptor;

    descriptor.fd = fd;
    descriptor.events = events;
    descriptor.revents = 0;
    for (;;) {
        int64_t remaining = deadline - now_ms();
        int result;

        if (remaining <= 0) {
            return 0;
        }
        if (remaining > INT32_MAX) {
            remaining = INT32_MAX;
        }
        result = poll(&descriptor, 1, (int)remaining);
        if (result > 0) {
            return descriptor.revents;
        }
        if (result == 0) {
            return 0;
        }
        if (errno != EINTR) {
            fail("poll failed: %s", strerror(errno));
        }
    }
}

static void read_line(int fd, char *buffer, size_t capacity,
                      const char *description) {
    size_t length = 0;
    int64_t deadline = now_ms() + IO_TIMEOUT_MS;

    if (capacity == 0) {
        fail("internal zero-capacity line buffer");
    }
    for (;;) {
        char byte;
        ssize_t count;
        short ready = poll_one(fd, POLLIN, deadline);

        if (ready == 0) {
            fail("timed out waiting for %s", description);
        }
        if ((ready & (POLLIN | POLLHUP)) == 0) {
            fail("unexpected poll state while waiting for %s", description);
        }
        count = read(fd, &byte, 1);
        if (count == 0) {
            fail("server stdout closed before %s", description);
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail("reading server stdout failed: %s", strerror(errno));
        }
        if (byte == '\n') {
            buffer[length] = '\0';
            return;
        }
        if (byte == '\r' || length + 1 >= capacity) {
            fail("malformed or oversized %s line", description);
        }
        buffer[length++] = byte;
    }
}

static uint16_t parse_ready_line(const char *line) {
    unsigned int port = 0;
    char trailing = '\0';

    if (sscanf(line, "READY %u%c", &port, &trailing) != 1 || port == 0 ||
        port > UINT16_MAX) {
        fail("expected exact READY <port>, got \"%s\"", line);
    }
    return (uint16_t)port;
}

static Server start_server(const char *executable) {
    int output_pipe[2];
    char error_path[] = "/tmp/kofun-http-stderr-XXXXXX";
    int error_fd;
    pid_t pid;
    Server server;
    char ready[64];

    if (pipe(output_pipe) != 0) {
        fail("pipe failed: %s", strerror(errno));
    }
    error_fd = mkstemp(error_path);
    if (error_fd < 0) {
        fail("mkstemp failed: %s", strerror(errno));
    }
    if (unlink(error_path) != 0) {
        fail("unlinking temporary stderr file failed: %s", strerror(errno));
    }

    pid = fork();
    if (pid < 0) {
        fail("fork failed: %s", strerror(errno));
    }
    if (pid == 0) {
        int null_fd;

        close(output_pipe[0]);
        null_fd = open("/dev/null", O_RDONLY);
        if (null_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0 ||
            dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(error_fd, STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(null_fd);
        close(output_pipe[1]);
        close(error_fd);
        execl(executable, executable, (char *)NULL);
        _exit(127);
    }

    close(output_pipe[1]);
    server.pid = pid;
    server.output_fd = output_pipe[0];
    server.error_fd = error_fd;
    server.port = 0;
    server.reaped = false;
    active_server = &server;
    read_line(server.output_fd, ready, sizeof(ready), "READY");
    server.port = parse_ready_line(ready);
    active_server = NULL;
    return server;
}

static int connect_server(uint16_t port, bool required) {
    struct sockaddr_in address;
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (fd < 0) {
        fail("socket failed: %s", strerror(errno));
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        int saved_errno = errno;

        close(fd);
        if (required) {
            fail("connecting to 127.0.0.1:%u failed: %s", (unsigned int)port,
                 strerror(saved_errno));
        }
        errno = saved_errno;
        return -1;
    }
    return fd;
}

static void write_all(int fd, const char *bytes, size_t length) {
    size_t written = 0;
    int64_t deadline = now_ms() + IO_TIMEOUT_MS;

    while (written < length) {
        short ready = poll_one(fd, POLLOUT, deadline);
        ssize_t count;

        if (ready == 0) {
            fail("timed out writing an HTTP request");
        }
        if ((ready & POLLOUT) == 0) {
            fail("connection closed while writing an HTTP request");
        }
        count = send(fd, bytes + written, length - written, MSG_NOSIGNAL);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail("writing an HTTP request failed: %s", strerror(errno));
        }
        written += (size_t)count;
    }
}

static void write_one_call(int fd, const char *bytes, size_t length) {
    ssize_t count;

    do {
        count = send(fd, bytes, length, MSG_NOSIGNAL);
    } while (count < 0 && errno == EINTR);
    if (count < 0) {
        fail("single-call pipelined write failed: %s", strerror(errno));
    }
    if ((size_t)count != length) {
        fail("single-call pipelined write was unexpectedly partial");
    }
}

static void read_exact(int fd, char *bytes, size_t length) {
    size_t received = 0;
    int64_t deadline = now_ms() + IO_TIMEOUT_MS;

    while (received < length) {
        short ready = poll_one(fd, POLLIN, deadline);
        ssize_t count;

        if (ready == 0) {
            fail("timed out reading an HTTP response");
        }
        if ((ready & (POLLIN | POLLHUP)) == 0) {
            fail("unexpected socket state while reading an HTTP response");
        }
        count = read(fd, bytes + received, length - received);
        if (count == 0) {
            fail("connection closed in the middle of an HTTP response");
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail("reading an HTTP response failed: %s", strerror(errno));
        }
        received += (size_t)count;
    }
}

static void expect_response(int fd, const char *status, const char *connection,
                            const char *body) {
    char expected[RESPONSE_CAPACITY];
    char actual[RESPONSE_CAPACITY];
    int length = snprintf(
        expected, sizeof(expected),
        "HTTP/1.1 %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "\r\n"
        "%s",
        status, strlen(body), connection, body);

    if (length < 0 || (size_t)length >= sizeof(expected)) {
        fail("internal expected response overflow");
    }
    read_exact(fd, actual, (size_t)length);
    if (memcmp(actual, expected, (size_t)length) != 0) {
        fail("response bytes did not match the exact HTTP contract");
    }
}

static void expect_no_response_yet(int fd) {
    short ready = poll_one(fd, POLLIN, now_ms() + 150);

    if ((ready & (POLLIN | POLLHUP | POLLERR)) != 0) {
        fail("server responded before the declared request body was complete");
    }
}

static void expect_eof(int fd) {
    char byte;
    int64_t deadline = now_ms() + IO_TIMEOUT_MS;

    for (;;) {
        short ready = poll_one(fd, POLLIN, deadline);
        ssize_t count;

        if (ready == 0) {
            fail("timed out waiting for the server to close a connection");
        }
        if ((ready & (POLLIN | POLLHUP)) == 0) {
            fail("unexpected socket state while waiting for EOF");
        }
        count = read(fd, &byte, 1);
        if (count == 0) {
            return;
        }
        if (count > 0) {
            fail("unexpected bytes followed the exact HTTP response");
        }
        if (errno != EINTR) {
            fail("reading connection EOF failed: %s", strerror(errno));
        }
    }
}

static void wait_clean_exit(Server *server) {
    int status = 0;
    int64_t deadline = now_ms() + SHUTDOWN_TIMEOUT_MS;

    active_server = server;
    for (;;) {
        pid_t result = waitpid(server->pid, &status, WNOHANG);

        if (result == server->pid) {
            server->reaped = true;
            active_server = NULL;
            break;
        }
        if (result < 0 && errno != EINTR) {
            fail("waitpid failed: %s", strerror(errno));
        }
        if (now_ms() >= deadline) {
            fail("server did not exit before the shutdown deadline");
        }
        (void)poll(NULL, 0, 20);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fail("server exited abnormally (status=%d)", status);
    }
}

static void expect_empty_stderr(Server *server) {
    char byte;
    ssize_t count;

    if (lseek(server->error_fd, 0, SEEK_SET) < 0) {
        fail("seeking captured stderr failed: %s", strerror(errno));
    }
    do {
        count = read(server->error_fd, &byte, 1);
    } while (count < 0 && errno == EINTR);
    if (count < 0) {
        fail("reading captured stderr failed: %s", strerror(errno));
    }
    if (count != 0) {
        fail("server wrote unexpected data to stderr");
    }
}

static void expect_stdout_eof(Server *server) {
    char byte;
    ssize_t count;

    do {
        count = read(server->output_fd, &byte, 1);
    } while (count < 0 && errno == EINTR);
    if (count < 0) {
        fail("reading final server stdout failed: %s", strerror(errno));
    }
    if (count != 0) {
        fail("server wrote unexpected stdout after DRAINING");
    }
}

static void finish_server(Server *server) {
    wait_clean_exit(server);
    expect_stdout_eof(server);
    expect_empty_stderr(server);
    close(server->output_fd);
    close(server->error_fd);
}

static void expect_draining(Server *server) {
    char line[64];

    read_line(server->output_fd, line, sizeof(line), "DRAINING");
    if (strcmp(line, "DRAINING") != 0) {
        fail("expected exact DRAINING, got \"%s\"", line);
    }
}

static void test_http_and_sigterm(const char *executable) {
    static const char pipelined_requests[] =
        "GET /hello/kofun HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "GET /bench HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    static const char hello_request[] =
        "GET /hello/kofun HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    static const char escaped_path_request[] =
        "GET /hello/a%22b%5Cc HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    static const char sum_header[] =
        "POST /sum HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 22\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    static const char sum_body[] = "{\"left\":20,\"right\":22}";
    static const char missing_request[] =
        "GET /unknown HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    static const char invalid_header[] =
        "POST /sum HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 1\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    static const char oversized_request[] =
        "POST /sum HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 4097\r\n"
        "Connection: close\r\n"
        "\r\n";
    static const char transfer_encoding_request[] =
        "POST /sum HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n";
    static const char duplicate_length_request[] =
        "POST /sum HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 1\r\n"
        "Content-Length: 2\r\n"
        "Connection: close\r\n"
        "\r\n";
    Server server;
    int connection;
    int oversized_connection;
    int transfer_connection;
    int duplicate_connection;
    int header_connection;
    int rejected_connection;
    char oversized_header[9000];
    int header_prefix;

    test_step = "SIGTERM server startup";
    server = start_server(executable);
    active_server = &server;
    test_step = "initial keep-alive connection";
    connection = connect_server(server.port, true);

    test_step = "single-write pipelined GET requests";
    write_one_call(
        connection,
        pipelined_requests,
        strlen(pipelined_requests)
    );
    expect_response(connection, "200 OK", "keep-alive",
                    "{\"name\":\"kofun\"}");
    expect_response(connection, "200 OK", "keep-alive", "{\"ok\":true}");

    test_step = "percent-decoded path parameter JSON escaping";
    write_all(
        connection,
        escaped_path_request,
        strlen(escaped_path_request)
    );
    expect_response(
        connection,
        "200 OK",
        "keep-alive",
        "{\"name\":\"a\\\"b\\\\c\"}"
    );

    test_step = "fragmented POST /sum";
    if (strlen(sum_body) != 22) {
        fail("internal sum body length mismatch");
    }
    write_all(connection, sum_header, strlen(sum_header));
    write_all(connection, sum_body, 7);
    expect_no_response_yet(connection);
    write_all(connection, sum_body + 7, strlen(sum_body) - 7);
    expect_response(connection, "200 OK", "keep-alive", "{\"sum\":42}");

    test_step = "GET unknown route";
    write_all(connection, missing_request, strlen(missing_request));
    expect_response(connection, "404 Not Found", "keep-alive",
                    "{\"error\":\"not found\"}");

    test_step = "POST /sum invalid JSON";
    write_all(connection, invalid_header, strlen(invalid_header));
    write_all(connection, "{", 1);
    expect_response(connection, "400 Bad Request", "keep-alive",
                    "{\"error\":\"invalid json\"}");

    test_step = "oversized request connection";
    oversized_connection = connect_server(server.port, true);
    write_all(oversized_connection, oversized_request,
              strlen(oversized_request));
    expect_response(oversized_connection, "413 Payload Too Large", "close",
                    "{\"error\":\"payload too large\"}");
    expect_eof(oversized_connection);
    close(oversized_connection);

    test_step = "Transfer-Encoding rejection";
    transfer_connection = connect_server(server.port, true);
    write_all(
        transfer_connection,
        transfer_encoding_request,
        strlen(transfer_encoding_request)
    );
    expect_response(
        transfer_connection,
        "400 Bad Request",
        "close",
        "{\"error\":\"bad request\"}"
    );
    expect_eof(transfer_connection);
    close(transfer_connection);

    test_step = "conflicting Content-Length rejection";
    duplicate_connection = connect_server(server.port, true);
    write_all(
        duplicate_connection,
        duplicate_length_request,
        strlen(duplicate_length_request)
    );
    expect_response(
        duplicate_connection,
        "400 Bad Request",
        "close",
        "{\"error\":\"bad request\"}"
    );
    expect_eof(duplicate_connection);
    close(duplicate_connection);

    test_step = "header limit 431";
    header_prefix = snprintf(
        oversized_header,
        sizeof(oversized_header),
        "GET /bench HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "X-Fill: "
    );
    if (header_prefix < 0 ||
        (size_t)header_prefix + 8300 + 4 >= sizeof(oversized_header)) {
        fail("internal oversized-header fixture overflow");
    }
    memset(oversized_header + header_prefix, 'a', 8300);
    memcpy(oversized_header + header_prefix + 8300, "\r\n\r\n", 4);
    header_connection = connect_server(server.port, true);
    write_all(
        header_connection,
        oversized_header,
        (size_t)header_prefix + 8300 + 4
    );
    expect_response(
        header_connection,
        "431 Request Header Fields Too Large",
        "close",
        "{\"error\":\"headers too large\"}"
    );
    expect_eof(header_connection);
    close(header_connection);

    test_step = "SIGTERM drain announcement";
    if (kill(server.pid, SIGTERM) != 0) {
        fail("SIGTERM failed: %s", strerror(errno));
    }
    expect_draining(&server);

    test_step = "new connection rejection during drain";
    rejected_connection = connect_server(server.port, false);
    if (rejected_connection >= 0) {
        close(rejected_connection);
        fail("new connection was accepted after DRAINING");
    }

    test_step = "final request during drain";
    write_all(connection, hello_request, strlen(hello_request));
    expect_response(connection, "200 OK", "close", "{\"name\":\"kofun\"}");
    expect_eof(connection);
    close(connection);

    test_step = "SIGTERM server clean exit";
    finish_server(&server);
    active_server = NULL;
}

static void test_sigint_without_clients(const char *executable) {
    Server server;

    test_step = "SIGINT server startup";
    server = start_server(executable);
    active_server = &server;
    test_step = "SIGINT drain announcement";
    if (kill(server.pid, SIGINT) != 0) {
        fail("SIGINT failed: %s", strerror(errno));
    }
    expect_draining(&server);
    test_step = "SIGINT server clean exit";
    finish_server(&server);
    active_server = NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s SERVER_EXECUTABLE\n", argv[0]);
        return 2;
    }
    test_http_and_sigterm(argv[1]);
    test_sigint_without_clients(argv[1]);
    puts("http integration: PASS");
    return 0;
}
