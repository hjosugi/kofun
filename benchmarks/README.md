# Benchmarks

The current repository contains correctness-oriented smoke benchmarks, not claims of production performance.

- `fibonacci.cofn`: recursive integer workload supported by the C11 backend.
- `vector.cofn`: reference-interpreter vector workload.

Run the native smoke benchmark with `make native`. Production benchmark methodology and Rust/C/Python comparisons are backlog work and must include compiler versions, flags, warmup, confidence intervals, and machine details.
