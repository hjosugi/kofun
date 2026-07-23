# Numeric backend conformance corpus

This directory is the shared numeric corpus for every Kofun backend. The
`c11-stage1`, direct-static `native-x86_64`, and `wasm32-node` adapters use
these same files rather than maintaining backend-specific copies.

Each corpus file carries its expected stdout, stderr, and exit status in
`# expect-*` headers. `expectations.kofun` exposes the same observations to
future Kofun-native tooling. `./bin/kofun test tests/conformance/numeric`
dispatches the common runner across every adapter in
`tests/conformance/backends/`.

The runner records one of two states for every declared backend/case pair:

- `ran`: compare stdout, stderr, and exit status byte for byte with the
  manifest;
- `unsupported`: print the backend's explicit compile diagnostic, count the
  case as skipped, and fail because a declared corpus requires full coverage.

A backend that does not yet support the complete corpus omits `numeric` from
its `BACKEND_CORPORA` registration and is reported as corpus-level
`UNSUPPORTED`.

Missing results, crashes, signals, timeouts, and coverage regressions are
failures, never implicit skips. The summary reports `ran/total` coverage for
each backend. Adding a backend therefore requires one adapter file, not a new
test suite.

`make verify` runs this corpus. Runtime failures are captured as ordinary
stdout/stderr/exit observations rather than treated as harness failures.
