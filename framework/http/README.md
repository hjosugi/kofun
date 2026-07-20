# Kofun HTTP/API framework

This directory contains a first-party, reusable Linux HTTP/1.1 event-loop
library and its Kofun-facing API. It is a library above the language core, not
a scheduler or networking runtime built into every Kofun binary.

`examples/api_server.kofun` owns server configuration and route registration:

```kofun
let server = kofun_http_server_new(
    "127.0.0.1", 0, 128, 8192, 4096, 5000, 3000,
)
kofun_http_route(server, "GET", "/hello/:name", 1)
kofun_http_route(server, "POST", "/sum", 2)
return kofun_http_server_run(server)
```

Build and run it from a clean checkout:

```sh
sh framework/http/build.sh examples/api_server.kofun build/api-server
./build/api-server
```

The process prints `READY <port>` after binding. Port `0` selects an ephemeral
localhost port. Send `SIGINT` or `SIGTERM` to stop accepting, drain existing
connections, and exit.

The build helper compiles `src/kofun_http.c` into a static archive, combines
the declarations in `api.kofun` with the application, and invokes the existing
explicit `--backend c --c-abi` profile. The resulting executable is a host-C
binary linked with libc. It is not Kofun's direct-native static backend.

## HTTP and JSON boundary

The v1 parser accepts HTTP/1.1 origin-form requests with CRLF framing and one
decimal `Content-Length`. Transfer coding is rejected. Connections are
keep-alive unless the request says `Connection: close`, a protocol error makes
framing unsafe, or shutdown is draining.

Routes are registered before the event loop starts. A `:name` segment captures
one non-empty path segment, including strict percent decoding. Handler kind 1
JSON-escapes that captured value,
kind 2 strictly decodes a JSON object containing integer `left` and `right`
members and encodes their sum, and kind 3 returns a static benchmark object.
This bounded handler registry is the callback-free v1 surface supported by the
current explicit C ABI profile; arbitrary Kofun callbacks remain open.
Duplicate routes and invalid handler/pattern combinations poison server
configuration; `kofun_http_server_run` then returns 2 before printing `READY`.
Applications cannot silently run with a rejected route even though the current
bounded Kofun C ABI source slice has no conditional statement for inspecting
each registration result.

Defined client errors are:

- `400` for malformed HTTP, unsupported transfer coding, or invalid JSON;
- `404` when no method/pattern pair matches;
- `413` when `Content-Length` exceeds the configured body limit;
- `431` when the header exceeds the configured header limit.

## Trust, limits, and backpressure

The C library and libc are a foreign native-code trust boundary outside
Kofun's memory-safety guarantees. The public handle is a pointer represented
as `CLong`, which is valid only for the supported Linux LP64 C ABI profile.

The sample caps concurrent connections at 128, headers at 8192 bytes, bodies
at 4096 bytes, idle time at 5000 ms, and shutdown drain time at 3000 ms.
Per-connection input and output storage is bounded. Partial writes retain a
bounded response buffer and enable `EPOLLOUT`; the server never grows an
unbounded queue. Idle connections are closed, including slowloris clients
that do not complete headers or bodies before the timeout. When the maximum
connection count is reached, newly accepted sockets are closed.

The event loop uses nonblocking sockets, `epoll`, and `signalfd`. Shutdown
removes and closes the listening socket, marks existing connections to close
after their next response, and enforces the drain deadline. TLS is deliberately
out of scope; terminate TLS at a trusted reverse proxy.

Run the mandatory real-socket acceptance gate with:

```sh
sh tests/http/check.sh
```
