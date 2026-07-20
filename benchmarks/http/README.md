# HTTP framework benchmark

This benchmark compares the Kofun-configured sample with a minimal direct-C
front end to the same first-party event-loop library. Reusing the library,
compiler flags, route, process model, and client deliberately isolates the
cost of entering and configuring the framework through Kofun's explicit C ABI
profile. It is not a comparison with another production web framework and it
does not measure direct-native Kofun.

Both binaries:

- bind an ephemeral `127.0.0.1` port;
- register only `GET /bench`;
- return the exact 200 response `{"ok":true}`;
- use the same nonblocking epoll implementation and configured limits;
- are built by the same C compiler with `-std=c11 -O2`;
- receive the same requests from the same C client on the same machine.

The client forks the configured number of workers. Each worker uses one
persistent HTTP/1.1 connection and validates every complete response. Process
startup, route registration, and graceful shutdown are outside the timed
region. Client worker creation and connection setup are inside it, identically
for both servers. One 1000-request warmup precedes the measured samples. The
runners are alternated Kofun then minimal for each sample. At least five
elapsed-time samples are collected and the median is reported.

The committed `results.json` measurement observed medians of 200,299,366 ns
(99,850.54 requests/s) for the Kofun-configured server and 203,345,397 ns
(98,354.82 requests/s) for the minimal direct-C front end. That 1.5% spread is
within the raw sample variation; it is recorded as parity, not a performance
win.

Reproduce the measurement:

```sh
sh benchmarks/http/benchmark.sh
```

Defaults are 20,000 total requests per sample, concurrency 4, keep-alive
enabled, and 5 samples. Override them with `REQUESTS`, `CONCURRENCY`, and
`SAMPLES`; `SAMPLES` below 5 is rejected.

`results.json` records the committed measurement, including every raw sample,
median, workload, host, compiler, commands, and implementation commit. Results
are local loopback throughput observations, not general deployment claims.
