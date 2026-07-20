#include "kofun_http.h"

#include <stdint.h>

int main(void) {
    long server = kofun_http_server_new(
        "127.0.0.1",
        0,
        128,
        8192,
        4096,
        5000,
        3000
    );
    if (server == 0) return 2;
    if (kofun_http_route(
            server,
            "GET",
            "/bench",
            KOFUN_HTTP_HANDLER_STATIC_JSON) != 0) {
        return 2;
    }
    return kofun_http_server_run(server);
}
