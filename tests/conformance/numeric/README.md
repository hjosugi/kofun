# Numeric backend conformance corpus

This directory is the shared numeric corpus for every Kofun backend. Backends
must use these same files rather than creating backend-specific copies.

Each corpus file carries its expected stdout, stderr, and exit status in
`# expect-*` headers. `expectations.kofun` exposes the same observations to
future Kofun-native tooling. Backend support is declared only in
`tests/conformance/capabilities.tsv`; neither an adapter nor an expectations
file carries a second backend list. `./bin/kofun test
tests/conformance/numeric` dispatches the common runner across every adapter in
`tests/conformance/backends/`.

The capability manifest records one of two states for every backend/corpus
pair:

- `supported`: the adapter must execute at least one case, and every case must
  compare stdout, stderr, and exit status byte for byte;
- `unsupported`: the manifest carries a stable reason and the adapter is not
  invoked for that corpus.

An executor can be unavailable on the current host without changing target
capability. In particular, AArch64 support stays fixed while the adapter
separately reports whether `qemu-aarch64` exists. A supported adapter returning
the conformance skip status is a failure. Missing results, zero executed cases,
crashes, signals, and timeouts are also failures. The summary reports
`executed/total` coverage for each available supported backend.

`tests/conformance/capabilities_test.sh` rejects incomplete matrices,
duplicate, unknown, contradictory, or reasonless rows, disappearing adapters,
attempts to restore an independent adapter or expectations-file policy,
partially skipped supported corpora, and drift between each expectations
manifest's `expected_cases` count and the files the runner discovers. `make
verify` runs that gate and this corpus. Runtime failures are captured as
ordinary stdout/stderr/exit observations rather than treated as harness
failures.
