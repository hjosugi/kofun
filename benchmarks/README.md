# Benchmarks

The current repository contains correctness-oriented smoke benchmarks, not claims of production performance.

- `fibonacci.kofun`: recursive integer workload supported by the C11 backend.
- `vector.kofun`: reference-interpreter vector workload.
- `http/`: reproducible five-sample loopback HTTP comparison between the
  Kofun-configured API sample and a minimal direct-C front end to the same
  event-loop library.
- `native-functions/`: reproducible CPU-time measurement of the direct x86-64
  user-defined function profile, with an optional pre-change baseline built
  from any git revision and recorded runtime, compile-time, and emitted-code
  budgets.

Run the native smoke benchmark with `make native`, the recorded HTTP workload
with `sh benchmarks/http/benchmark.sh`, or the native function workloads with
`sh benchmarks/native-functions/benchmark.sh`. Each result is a bounded local
measurement with raw samples and a median, not a production performance
claim. Broader Rust/C/Python comparisons remain backlog work and must include
compiler versions, flags, warmup, confidence intervals, and machine details.
