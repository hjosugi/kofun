#define _GNU_SOURCE

#include "kofun_http.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

enum {
    KH_MAX_CONNECTIONS = 256,
    KH_MAX_HEADER_BYTES = 8192,
    KH_MAX_BODY_BYTES = 4096,
    KH_INPUT_CAPACITY = KH_MAX_HEADER_BYTES + KH_MAX_BODY_BYTES + 1,
    KH_OUTPUT_CAPACITY = 8192,
    KH_MAX_ROUTES = 32,
    KH_METHOD_CAPACITY = 8,
    KH_PATTERN_CAPACITY = 128,
    KH_PATH_CAPACITY = 512,
    KH_PARAMETER_CAPACITY = 256,
    KH_MAX_EVENTS = 64,
};

typedef struct {
    char method[KH_METHOD_CAPACITY];
    char pattern[KH_PATTERN_CAPACITY];
    int handler_kind;
} KhRoute;

typedef struct {
    int fd;
    unsigned char input[KH_INPUT_CAPACITY];
    size_t input_length;
    size_t request_length;
    char output[KH_OUTPUT_CAPACITY];
    size_t output_length;
    size_t output_sent;
    uint64_t last_activity_ms;
    bool response_close;
    bool peer_closed;
    bool drain_after_response;
} KhConnection;

typedef struct {
    int listener;
    int epoll;
    int signal_fd;
    int bound_port;
    size_t maximum_connections;
    size_t maximum_header_bytes;
    size_t maximum_body_bytes;
    uint64_t idle_timeout_ms;
    uint64_t drain_timeout_ms;
    uint64_t drain_deadline_ms;
    KhRoute routes[KH_MAX_ROUTES];
    size_t route_count;
    KhConnection *connections;
    size_t active_connections;
    bool configuration_error;
    bool draining;
    bool signal_mask_active;
    sigset_t previous_signal_mask;
} KhServer;

typedef struct {
    const unsigned char *bytes;
    size_t length;
    size_t position;
} JsonCursor;

static uint64_t monotonic_ms(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return (uint64_t)value.tv_sec * UINT64_C(1000) +
        (uint64_t)value.tv_nsec / UINT64_C(1000000);
}

static bool write_all(int fd, const char *bytes, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t result = write(fd, bytes + sent, length - sent);
        if (result > 0) {
            sent += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

static void server_destroy(KhServer *server) {
    if (server == NULL) return;
    if (server->connections != NULL) {
        for (size_t index = 0;
             index < server->maximum_connections;
             ++index) {
            if (server->connections[index].fd >= 0) {
                close(server->connections[index].fd);
            }
        }
    }
    if (server->listener >= 0) close(server->listener);
    if (server->signal_fd >= 0) close(server->signal_fd);
    if (server->epoll >= 0) close(server->epoll);
    if (server->signal_mask_active) {
        (void)sigprocmask(SIG_SETMASK, &server->previous_signal_mask, NULL);
    }
    free(server->connections);
    free(server);
}

static bool add_epoll(KhServer *server, int fd, uint32_t events) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.fd = fd;
    return epoll_ctl(server->epoll, EPOLL_CTL_ADD, fd, &event) == 0;
}

static bool update_connection_events(
    KhServer *server,
    const KhConnection *connection
) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN | EPOLLRDHUP;
    if (connection->output_sent < connection->output_length) {
        event.events |= EPOLLOUT;
    }
    event.data.fd = connection->fd;
    return epoll_ctl(
        server->epoll,
        EPOLL_CTL_MOD,
        connection->fd,
        &event
    ) == 0;
}

static KhConnection *find_connection(KhServer *server, int fd) {
    for (size_t index = 0;
         index < server->maximum_connections;
         ++index) {
        if (server->connections[index].fd == fd) {
            return &server->connections[index];
        }
    }
    return NULL;
}

static void close_connection(
    KhServer *server,
    KhConnection *connection
) {
    if (connection == NULL || connection->fd < 0) return;
    (void)epoll_ctl(
        server->epoll,
        EPOLL_CTL_DEL,
        connection->fd,
        NULL
    );
    close(connection->fd);
    memset(connection, 0, sizeof(*connection));
    connection->fd = -1;
    if (server->active_connections > 0) {
        --server->active_connections;
    }
}

static KhConnection *new_connection(KhServer *server, int fd) {
    for (size_t index = 0;
         index < server->maximum_connections;
         ++index) {
        KhConnection *connection = &server->connections[index];
        if (connection->fd >= 0) continue;
        memset(connection, 0, sizeof(*connection));
        connection->fd = fd;
        connection->last_activity_ms = monotonic_ms();
        ++server->active_connections;
        return connection;
    }
    return NULL;
}

static const char *status_reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        default: return "Internal Server Error";
    }
}

static bool prepare_response(
    KhConnection *connection,
    int status,
    const char *body,
    bool close_after
) {
    size_t body_length = strlen(body);
    const char *connection_value =
        close_after ? "close" : "keep-alive";
    int header_length = snprintf(
        connection->output,
        sizeof(connection->output),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "\r\n",
        status,
        status_reason(status),
        body_length,
        connection_value
    );
    if (header_length < 0 ||
        (size_t)header_length >= sizeof(connection->output) ||
        body_length >
            sizeof(connection->output) - (size_t)header_length) {
        return false;
    }
    memcpy(
        connection->output + (size_t)header_length,
        body,
        body_length
    );
    connection->output_length = (size_t)header_length + body_length;
    connection->output_sent = 0;
    connection->response_close = close_after;
    return true;
}

static size_t find_header_end(
    const unsigned char *bytes,
    size_t length
) {
    if (length < 4) return 0;
    for (size_t index = 0; index + 3 < length; ++index) {
        if (bytes[index] == '\r' &&
            bytes[index + 1] == '\n' &&
            bytes[index + 2] == '\r' &&
            bytes[index + 3] == '\n') {
            return index + 4;
        }
    }
    return 0;
}

static bool parse_decimal_size(const char *value, size_t *result) {
    size_t parsed = 0;
    if (*value == '\0') return false;
    for (const unsigned char *cursor =
             (const unsigned char *)value;
         *cursor != '\0';
         ++cursor) {
        if (!isdigit(*cursor)) return false;
        unsigned digit = (unsigned)(*cursor - '0');
        if (parsed > (SIZE_MAX - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    *result = parsed;
    return true;
}

static char *trim_header_value(char *value) {
    char *end;
    while (*value == ' ' || *value == '\t') ++value;
    end = value + strlen(value);
    while (end > value &&
           (end[-1] == ' ' || end[-1] == '\t')) {
        *--end = '\0';
    }
    return value;
}

static bool request_path(
    const char *target,
    char path[KH_PATH_CAPACITY]
) {
    const char *query;
    size_t length;
    if (target[0] != '/') return false;
    query = strchr(target, '?');
    length = query == NULL
        ? strlen(target)
        : (size_t)(query - target);
    if (length == 0 || length >= KH_PATH_CAPACITY) return false;
    for (size_t index = 0; index < length; ++index) {
        unsigned char value = (unsigned char)target[index];
        if (value <= 0x20 || value == 0x7f) return false;
    }
    memcpy(path, target, length);
    path[length] = '\0';
    return true;
}

static int hex_value(unsigned char value) {
    if (value >= '0' && value <= '9') return (int)(value - '0');
    if (value >= 'a' && value <= 'f') return (int)(value - 'a') + 10;
    if (value >= 'A' && value <= 'F') return (int)(value - 'A') + 10;
    return -1;
}

static bool decode_path_parameter(
    const char *source,
    size_t length,
    char destination[KH_PARAMETER_CAPACITY]
) {
    size_t used = 0;
    for (size_t index = 0; index < length; ++index) {
        unsigned char value = (unsigned char)source[index];
        if (value == '%') {
            if (index + 2 >= length) return false;
            int high = hex_value((unsigned char)source[index + 1]);
            int low = hex_value((unsigned char)source[index + 2]);
            if (high < 0 || low < 0) return false;
            value = (unsigned char)((high << 4) | low);
            index += 2;
        }
        if (value == '\0' || used + 1 >= KH_PARAMETER_CAPACITY) {
            return false;
        }
        destination[used++] = (char)value;
    }
    destination[used] = '\0';
    return used > 0;
}

static bool route_match(
    const char *pattern,
    const char *path,
    char parameter[KH_PARAMETER_CAPACITY]
) {
    const char *pattern_at = pattern;
    const char *path_at = path;
    parameter[0] = '\0';
    while (*pattern_at != '\0' || *path_at != '\0') {
        const char *pattern_end;
        const char *path_end;
        size_t pattern_length;
        size_t path_length;
        if (*pattern_at != '/' || *path_at != '/') return false;
        ++pattern_at;
        ++path_at;
        pattern_end = strchr(pattern_at, '/');
        path_end = strchr(path_at, '/');
        if (pattern_end == NULL) pattern_end = pattern_at + strlen(pattern_at);
        if (path_end == NULL) path_end = path_at + strlen(path_at);
        pattern_length = (size_t)(pattern_end - pattern_at);
        path_length = (size_t)(path_end - path_at);
        if (pattern_length > 1 && pattern_at[0] == ':') {
            if (path_length == 0 ||
                parameter[0] != '\0') {
                return false;
            }
            if (!decode_path_parameter(path_at, path_length, parameter)) {
                return false;
            }
        } else if (pattern_length != path_length ||
                   memcmp(pattern_at, path_at, path_length) != 0) {
            return false;
        }
        pattern_at = pattern_end;
        path_at = path_end;
    }
    return true;
}

static bool json_escape(
    const char *source,
    char *destination,
    size_t capacity
) {
    static const char hex[] = "0123456789abcdef";
    size_t used = 0;
    for (const unsigned char *cursor =
             (const unsigned char *)source;
         *cursor != '\0';
         ++cursor) {
        unsigned char value = *cursor;
        if (value == '"' || value == '\\') {
            if (capacity - used <= 2) return false;
            destination[used++] = '\\';
            destination[used++] = (char)value;
        } else if (value < 0x20) {
            if (capacity - used <= 6) return false;
            destination[used++] = '\\';
            destination[used++] = 'u';
            destination[used++] = '0';
            destination[used++] = '0';
            destination[used++] = hex[value >> 4];
            destination[used++] = hex[value & 0x0f];
        } else {
            if (capacity - used <= 1) return false;
            destination[used++] = (char)value;
        }
    }
    if (used >= capacity) return false;
    destination[used] = '\0';
    return true;
}

static void json_skip_space(JsonCursor *cursor) {
    while (cursor->position < cursor->length &&
           isspace(cursor->bytes[cursor->position])) {
        ++cursor->position;
    }
}

static bool json_take(JsonCursor *cursor, unsigned char expected) {
    json_skip_space(cursor);
    if (cursor->position >= cursor->length ||
        cursor->bytes[cursor->position] != expected) {
        return false;
    }
    ++cursor->position;
    return true;
}

static bool json_key(
    JsonCursor *cursor,
    char *key,
    size_t capacity
) {
    size_t used = 0;
    if (!json_take(cursor, '"')) return false;
    while (cursor->position < cursor->length) {
        unsigned char value = cursor->bytes[cursor->position++];
        if (value == '"') {
            if (used >= capacity) return false;
            key[used] = '\0';
            return true;
        }
        if (value == '\\' || value < 0x20 || used + 1 >= capacity) {
            return false;
        }
        key[used++] = (char)value;
    }
    return false;
}

static bool json_integer(JsonCursor *cursor, int64_t *result) {
    bool negative = false;
    uint64_t magnitude = 0;
    uint64_t limit;
    size_t digits = 0;
    json_skip_space(cursor);
    if (cursor->position < cursor->length &&
        cursor->bytes[cursor->position] == '-') {
        negative = true;
        ++cursor->position;
    }
    limit = negative
        ? (uint64_t)INT64_MAX + 1U
        : (uint64_t)INT64_MAX;
    while (cursor->position < cursor->length &&
           isdigit(cursor->bytes[cursor->position])) {
        unsigned digit =
            (unsigned)(cursor->bytes[cursor->position++] - '0');
        if (magnitude > (limit - digit) / 10) return false;
        magnitude = magnitude * 10 + digit;
        ++digits;
    }
    if (digits == 0) return false;
    if (negative && magnitude == (uint64_t)INT64_MAX + 1U) {
        *result = INT64_MIN;
    } else {
        *result = negative
            ? -(int64_t)magnitude
            : (int64_t)magnitude;
    }
    return true;
}

static bool decode_sum_json(
    const unsigned char *bytes,
    size_t length,
    int64_t *sum
) {
    JsonCursor cursor = {
        .bytes = bytes,
        .length = length,
        .position = 0,
    };
    bool left_seen = false;
    bool right_seen = false;
    int64_t left = 0;
    int64_t right = 0;
    if (!json_take(&cursor, '{')) return false;
    for (;;) {
        char key[16];
        int64_t value;
        json_skip_space(&cursor);
        if (cursor.position < cursor.length &&
            cursor.bytes[cursor.position] == '}') {
            ++cursor.position;
            break;
        }
        if (!json_key(&cursor, key, sizeof(key)) ||
            !json_take(&cursor, ':') ||
            !json_integer(&cursor, &value)) {
            return false;
        }
        if (strcmp(key, "left") == 0 && !left_seen) {
            left = value;
            left_seen = true;
        } else if (strcmp(key, "right") == 0 && !right_seen) {
            right = value;
            right_seen = true;
        } else {
            return false;
        }
        json_skip_space(&cursor);
        if (cursor.position < cursor.length &&
            cursor.bytes[cursor.position] == ',') {
            ++cursor.position;
            continue;
        }
        if (cursor.position < cursor.length &&
            cursor.bytes[cursor.position] == '}') {
            ++cursor.position;
            break;
        }
        return false;
    }
    json_skip_space(&cursor);
    if (cursor.position != cursor.length ||
        !left_seen ||
        !right_seen) {
        return false;
    }
    if ((right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right)) {
        return false;
    }
    *sum = left + right;
    return true;
}

static bool handle_route(
    KhServer *server,
    KhConnection *connection,
    const char *method,
    const char *path,
    const unsigned char *body,
    size_t body_length,
    bool close_after
) {
    for (size_t index = 0; index < server->route_count; ++index) {
        KhRoute *route = &server->routes[index];
        char parameter[KH_PARAMETER_CAPACITY];
        char response[2048];
        if (strcmp(route->method, method) != 0 ||
            !route_match(route->pattern, path, parameter)) {
            continue;
        }
        if (route->handler_kind == KOFUN_HTTP_HANDLER_PATH_JSON) {
            char escaped[KH_PARAMETER_CAPACITY * 2];
            if (!json_escape(parameter, escaped, sizeof(escaped))) {
                return prepare_response(
                    connection,
                    400,
                    "{\"error\":\"invalid path parameter\"}",
                    close_after
                );
            }
            int length = snprintf(
                response,
                sizeof(response),
                "{\"name\":\"%s\"}",
                escaped
            );
            if (length < 0 || (size_t)length >= sizeof(response)) {
                return false;
            }
            return prepare_response(
                connection,
                200,
                response,
                close_after
            );
        }
        if (route->handler_kind == KOFUN_HTTP_HANDLER_SUM_JSON) {
            int64_t sum;
            if (!decode_sum_json(body, body_length, &sum)) {
                return prepare_response(
                    connection,
                    400,
                    "{\"error\":\"invalid json\"}",
                    close_after
                );
            }
            int length = snprintf(
                response,
                sizeof(response),
                "{\"sum\":%" PRId64 "}",
                sum
            );
            if (length < 0 || (size_t)length >= sizeof(response)) {
                return false;
            }
            return prepare_response(
                connection,
                200,
                response,
                close_after
            );
        }
        if (route->handler_kind == KOFUN_HTTP_HANDLER_STATIC_JSON) {
            return prepare_response(
                connection,
                200,
                "{\"ok\":true}",
                close_after
            );
        }
        return false;
    }
    return prepare_response(
        connection,
        404,
        "{\"error\":\"not found\"}",
        close_after
    );
}

/*
 * Return 1 when a response was prepared, 0 when more bytes are needed, and -1
 * when the connection must be closed because even the bounded response failed.
 */
static int process_request(
    KhServer *server,
    KhConnection *connection
) {
    size_t header_end = find_header_end(
        connection->input,
        connection->input_length
    );
    if (header_end == 0) {
        if (connection->input_length >= server->maximum_header_bytes) {
            connection->request_length = connection->input_length;
            return prepare_response(
                connection,
                431,
                "{\"error\":\"headers too large\"}",
                true
            ) ? 1 : -1;
        }
        return 0;
    }
    if (header_end > server->maximum_header_bytes) {
        connection->request_length = header_end;
        return prepare_response(
            connection,
            431,
            "{\"error\":\"headers too large\"}",
            true
        ) ? 1 : -1;
    }
    for (size_t index = 0; index < header_end; ++index) {
        if (connection->input[index] == '\0') {
            connection->request_length = header_end;
            return prepare_response(
                connection,
                400,
                "{\"error\":\"bad request\"}",
                true
            ) ? 1 : -1;
        }
    }

    char header[KH_MAX_HEADER_BYTES + 1];
    memcpy(header, connection->input, header_end);
    header[header_end] = '\0';
    char *line_end = strstr(header, "\r\n");
    if (line_end == NULL) return -1;
    *line_end = '\0';
    char *method = header;
    char *first_space = strchr(method, ' ');
    if (first_space == NULL) goto bad_request;
    *first_space = '\0';
    char *target = first_space + 1;
    char *second_space = strchr(target, ' ');
    if (second_space == NULL) goto bad_request;
    *second_space = '\0';
    char *version = second_space + 1;
    if (strchr(version, ' ') != NULL ||
        strcmp(version, "HTTP/1.1") != 0 ||
        method[0] == '\0' ||
        target[0] == '\0') {
        goto bad_request;
    }

    bool content_length_seen = false;
    bool connection_close = false;
    size_t content_length = 0;
    char *line = line_end + 2;
    while (line[0] != '\r' || line[1] != '\n') {
        char *end = strstr(line, "\r\n");
        char *colon;
        char *value;
        if (end == NULL) goto bad_request;
        *end = '\0';
        if (*line == ' ' || *line == '\t') goto bad_request;
        colon = strchr(line, ':');
        if (colon == NULL || colon == line) goto bad_request;
        *colon = '\0';
        value = trim_header_value(colon + 1);
        if (strcasecmp(line, "Content-Length") == 0) {
            if (content_length_seen ||
                !parse_decimal_size(value, &content_length)) {
                goto bad_request;
            }
            content_length_seen = true;
        } else if (strcasecmp(line, "Connection") == 0) {
            if (strcasecmp(value, "close") == 0) {
                connection_close = true;
            }
        } else if (strcasecmp(line, "Transfer-Encoding") == 0) {
            goto bad_request;
        }
        line = end + 2;
    }

    if (content_length > server->maximum_body_bytes) {
        connection->request_length = header_end;
        return prepare_response(
            connection,
            413,
            "{\"error\":\"payload too large\"}",
            true
        ) ? 1 : -1;
    }
    if (header_end > SIZE_MAX - content_length ||
        connection->input_length < header_end + content_length) {
        return 0;
    }

    char path[KH_PATH_CAPACITY];
    if (!request_path(target, path)) goto bad_request;
    connection->request_length = header_end + content_length;
    bool close_after =
        connection_close ||
        connection->drain_after_response ||
        server->draining;
    if (!handle_route(
            server,
            connection,
            method,
            path,
            connection->input + header_end,
            content_length,
            close_after)) {
        return -1;
    }
    return 1;

bad_request:
    connection->request_length = header_end;
    return prepare_response(
        connection,
        400,
        "{\"error\":\"bad request\"}",
        true
    ) ? 1 : -1;
}

static bool consume_request(KhConnection *connection) {
    if (connection->request_length > connection->input_length) return false;
    size_t remaining =
        connection->input_length - connection->request_length;
    memmove(
        connection->input,
        connection->input + connection->request_length,
        remaining
    );
    connection->input_length = remaining;
    connection->request_length = 0;
    connection->output_length = 0;
    connection->output_sent = 0;
    return true;
}

static bool drive_requests(
    KhServer *server,
    KhConnection *connection
) {
    while (connection->output_length == 0) {
        int result = process_request(server, connection);
        if (result < 0) return false;
        if (result == 0) return true;
    }
    return update_connection_events(server, connection);
}

static bool read_connection(
    KhServer *server,
    KhConnection *connection
) {
    for (;;) {
        if (connection->input_length >= sizeof(connection->input)) {
            if (!prepare_response(
                    connection,
                    413,
                    "{\"error\":\"payload too large\"}",
                    true)) {
                return false;
            }
            return update_connection_events(server, connection);
        }
        ssize_t count = recv(
            connection->fd,
            connection->input + connection->input_length,
            sizeof(connection->input) - connection->input_length,
            0
        );
        if (count > 0) {
            connection->input_length += (size_t)count;
            connection->last_activity_ms = monotonic_ms();
            continue;
        }
        if (count == 0) {
            connection->peer_closed = true;
            break;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        return false;
    }
    if (connection->output_length == 0 &&
        !drive_requests(server, connection)) {
        return false;
    }
    if (connection->peer_closed &&
        connection->output_length == 0) {
        return false;
    }
    return true;
}

static bool flush_connection(
    KhServer *server,
    KhConnection *connection
) {
    while (connection->output_sent < connection->output_length) {
        ssize_t count = send(
            connection->fd,
            connection->output + connection->output_sent,
            connection->output_length - connection->output_sent,
            MSG_NOSIGNAL
        );
        if (count > 0) {
            connection->output_sent += (size_t)count;
            connection->last_activity_ms = monotonic_ms();
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return update_connection_events(server, connection);
        }
        return false;
    }
    if (connection->response_close) return false;
    if (!consume_request(connection)) return false;
    if (!drive_requests(server, connection)) return false;
    if (connection->peer_closed &&
        connection->output_length == 0) {
        return false;
    }
    return update_connection_events(server, connection);
}

static bool accept_connections(KhServer *server) {
    for (;;) {
        int fd = accept4(
            server->listener,
            NULL,
            NULL,
            SOCK_NONBLOCK | SOCK_CLOEXEC
        );
        if (fd < 0) {
            if (errno == EINTR) continue;
            return errno == EAGAIN || errno == EWOULDBLOCK;
        }
        KhConnection *connection = new_connection(server, fd);
        if (connection == NULL ||
            !add_epoll(server, fd, EPOLLIN | EPOLLRDHUP)) {
            if (connection != NULL) {
                close_connection(server, connection);
            } else {
                close(fd);
            }
        }
    }
}

static void begin_drain(KhServer *server) {
    static const char message[] = "DRAINING\n";
    if (server->draining) return;
    server->draining = true;
    server->drain_deadline_ms =
        monotonic_ms() + server->drain_timeout_ms;
    if (server->listener >= 0) {
        (void)epoll_ctl(
            server->epoll,
            EPOLL_CTL_DEL,
            server->listener,
            NULL
        );
        close(server->listener);
        server->listener = -1;
    }
    for (size_t index = 0;
         index < server->maximum_connections;
         ++index) {
        if (server->connections[index].fd >= 0) {
            server->connections[index].drain_after_response = true;
        }
    }
    (void)write_all(STDOUT_FILENO, message, sizeof(message) - 1);
}

static bool consume_signals(KhServer *server) {
    for (;;) {
        struct signalfd_siginfo info;
        ssize_t count = read(server->signal_fd, &info, sizeof(info));
        if (count == (ssize_t)sizeof(info)) {
            if (info.ssi_signo == SIGINT || info.ssi_signo == SIGTERM) {
                begin_drain(server);
            }
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        return false;
    }
}

static void expire_connections(KhServer *server) {
    uint64_t now = monotonic_ms();
    for (size_t index = 0;
         index < server->maximum_connections;
         ++index) {
        KhConnection *connection = &server->connections[index];
        if (connection->fd < 0) continue;
        bool idle =
            now >= connection->last_activity_ms &&
            now - connection->last_activity_ms >=
                server->idle_timeout_ms;
        bool drain_expired =
            server->draining &&
            now >= server->drain_deadline_ms;
        if (idle || drain_expired) {
            close_connection(server, connection);
        }
    }
}

static bool setup_event_loop(KhServer *server) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(
            SIG_BLOCK,
            &mask,
            &server->previous_signal_mask) != 0) {
        return false;
    }
    server->signal_mask_active = true;
    server->signal_fd = signalfd(
        -1,
        &mask,
        SFD_NONBLOCK | SFD_CLOEXEC
    );
    if (server->signal_fd < 0) return false;
    server->epoll = epoll_create1(EPOLL_CLOEXEC);
    if (server->epoll < 0) return false;
    if (!add_epoll(server, server->listener, EPOLLIN) ||
        !add_epoll(server, server->signal_fd, EPOLLIN)) {
        return false;
    }
    return true;
}

long kofun_http_server_new(
    const char *bind_address,
    int port,
    long maximum_connections,
    long maximum_header_bytes,
    long maximum_body_bytes,
    long idle_timeout_ms,
    long drain_timeout_ms
) {
    if (bind_address == NULL ||
        port < 0 || port > 65535 ||
        maximum_connections <= 0 ||
        maximum_connections > KH_MAX_CONNECTIONS ||
        maximum_header_bytes < 256 ||
        maximum_header_bytes > KH_MAX_HEADER_BYTES ||
        maximum_body_bytes < 0 ||
        maximum_body_bytes > KH_MAX_BODY_BYTES ||
        idle_timeout_ms <= 0 ||
        drain_timeout_ms <= 0) {
        return 0;
    }

    KhServer *server = calloc(1, sizeof(*server));
    if (server == NULL) return 0;
    server->listener = -1;
    server->epoll = -1;
    server->signal_fd = -1;
    server->maximum_connections = (size_t)maximum_connections;
    server->maximum_header_bytes = (size_t)maximum_header_bytes;
    server->maximum_body_bytes = (size_t)maximum_body_bytes;
    server->idle_timeout_ms = (uint64_t)idle_timeout_ms;
    server->drain_timeout_ms = (uint64_t)drain_timeout_ms;
    server->connections = calloc(
        server->maximum_connections,
        sizeof(*server->connections)
    );
    if (server->connections == NULL) {
        server_destroy(server);
        return 0;
    }
    for (size_t index = 0;
         index < server->maximum_connections;
         ++index) {
        server->connections[index].fd = -1;
    }

    server->listener = socket(
        AF_INET,
        SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
        0
    );
    if (server->listener < 0) {
        server_destroy(server);
        return 0;
    }
    int enabled = 1;
    if (setsockopt(
            server->listener,
            SOL_SOCKET,
            SO_REUSEADDR,
            &enabled,
            sizeof(enabled)) != 0) {
        server_destroy(server);
        return 0;
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    if (inet_pton(
            AF_INET,
            bind_address,
            &address.sin_addr) != 1 ||
        bind(
            server->listener,
            (const struct sockaddr *)&address,
            sizeof(address)) != 0 ||
        listen(server->listener, 128) != 0) {
        server_destroy(server);
        return 0;
    }
    socklen_t address_length = sizeof(address);
    if (getsockname(
            server->listener,
            (struct sockaddr *)&address,
            &address_length) != 0) {
        server_destroy(server);
        return 0;
    }
    server->bound_port = (int)ntohs(address.sin_port);
    return (long)(intptr_t)server;
}

int kofun_http_route(
    long server_handle,
    const char *method,
    const char *pattern,
    int handler_kind
) {
    KhServer *server = (KhServer *)(intptr_t)server_handle;
    if (server == NULL ||
        method == NULL ||
        pattern == NULL ||
        server->route_count >= KH_MAX_ROUTES ||
        strlen(method) >= KH_METHOD_CAPACITY ||
        strlen(pattern) >= KH_PATTERN_CAPACITY ||
        pattern[0] != '/' ||
        (handler_kind != KOFUN_HTTP_HANDLER_PATH_JSON &&
         handler_kind != KOFUN_HTTP_HANDLER_SUM_JSON &&
         handler_kind != KOFUN_HTTP_HANDLER_STATIC_JSON)) {
        if (server != NULL) server->configuration_error = true;
        return -1;
    }
    for (const unsigned char *cursor =
             (const unsigned char *)method;
         *cursor != '\0';
         ++cursor) {
        if (!isupper(*cursor)) {
            server->configuration_error = true;
            return -1;
        }
    }
    if (handler_kind == KOFUN_HTTP_HANDLER_PATH_JSON &&
        strchr(pattern, ':') == NULL) {
        server->configuration_error = true;
        return -1;
    }
    for (size_t index = 0; index < server->route_count; ++index) {
        if (strcmp(server->routes[index].method, method) == 0 &&
            strcmp(server->routes[index].pattern, pattern) == 0) {
            server->configuration_error = true;
            return -1;
        }
    }
    KhRoute *route = &server->routes[server->route_count++];
    strcpy(route->method, method);
    strcpy(route->pattern, pattern);
    route->handler_kind = handler_kind;
    return 0;
}

int kofun_http_server_run(long server_handle) {
    KhServer *server = (KhServer *)(intptr_t)server_handle;
    struct epoll_event events[KH_MAX_EVENTS];
    int status = 0;
    if (server == NULL) return 2;
    if (server->configuration_error ||
        server->route_count == 0 ||
        !setup_event_loop(server)) {
        server_destroy(server);
        return 2;
    }
    char ready[64];
    int ready_length = snprintf(
        ready,
        sizeof(ready),
        "READY %d\n",
        server->bound_port
    );
    if (ready_length < 0 ||
        (size_t)ready_length >= sizeof(ready) ||
        !write_all(STDOUT_FILENO, ready, (size_t)ready_length)) {
        server_destroy(server);
        return 2;
    }

    for (;;) {
        if (server->draining && server->active_connections == 0) break;
        int count = epoll_wait(
            server->epoll,
            events,
            KH_MAX_EVENTS,
            100
        );
        if (count < 0) {
            if (errno == EINTR) continue;
            status = 2;
            break;
        }
        for (int index = 0; index < count; ++index) {
            int fd = events[index].data.fd;
            uint32_t observed = events[index].events;
            if (fd == server->listener) {
                if (!server->draining && !accept_connections(server)) {
                    status = 2;
                    begin_drain(server);
                }
                continue;
            }
            if (fd == server->signal_fd) {
                if (!consume_signals(server)) {
                    status = 2;
                    begin_drain(server);
                }
                continue;
            }
            KhConnection *connection = find_connection(server, fd);
            if (connection == NULL) continue;
            bool keep = true;
            if ((observed & EPOLLIN) != 0) {
                keep = read_connection(server, connection);
            }
            if (keep && (observed & EPOLLOUT) != 0) {
                keep = flush_connection(server, connection);
            }
            if (keep &&
                (observed & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0 &&
                connection->output_length == 0) {
                keep = false;
            }
            if (!keep) close_connection(server, connection);
        }
        expire_connections(server);
    }
    server_destroy(server);
    return status;
}
