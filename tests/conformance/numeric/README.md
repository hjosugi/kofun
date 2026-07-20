# Numeric backend conformance corpus

This directory is the shared numeric corpus for every Kofun backend. The active
backend is `c11-stage1`; future native or WebAssembly backends must use these
same files rather than creating backend-specific copies.

Each corpus file carries its expected stdout, stderr, and exit status in
`# expect-*` headers. `expectations.kofun` exposes the same observations to
future Kofun-native tooling. `./bin/kofun test tests/conformance/numeric`
dispatches the common runner across every adapter in
`tests/conformance/backends/`.

The runner records one of two states for every backend/case pair:

- `ran`: compare stdout, stderr, and exit status byte for byte with the
  manifest;
- `unsupported`: print the backend's explicit compile diagnostic and count the
  case as skipped.

Missing results, crashes, signals, and timeouts are failures, never implicit
skips. The summary reports `ran/total` coverage for each backend. Adding a
backend therefore requires one adapter file, not a new test suite.

`make verify` runs this corpus. Runtime failures are captured as ordinary
stdout/stderr/exit observations rather than treated as harness failures.
