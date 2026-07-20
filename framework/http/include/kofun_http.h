#ifndef KOFUN_HTTP_H
#define KOFUN_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    KOFUN_HTTP_HANDLER_PATH_JSON = 1,
    KOFUN_HTTP_HANDLER_SUM_JSON = 2,
    KOFUN_HTTP_HANDLER_STATIC_JSON = 3,
};

/*
 * The explicit C ABI profile represents the opaque server pointer as CLong on
 * its supported Linux LP64 target. A successful run consumes the handle.
 */
long kofun_http_server_new(
    const char *bind_address,
    int port,
    long maximum_connections,
    long maximum_header_bytes,
    long maximum_body_bytes,
    long idle_timeout_ms,
    long drain_timeout_ms
);

int kofun_http_route(
    long server_handle,
    const char *method,
    const char *pattern,
    int handler_kind
);

int kofun_http_server_run(long server_handle);

#ifdef __cplusplus
}
#endif

#endif
