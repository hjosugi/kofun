# Backend differential contract

Status: normative for every registered Kofun backend.

Contract identifier: `kofun.backend-differential/v1`.

## Oracle and observations

The specification and conformance manifest define the expected behavior. Every
case must provide an explicit expectation; there is no host-language oracle.

For each source program, a backend produces one observation:

```text
stdout: exact bytes
stderr: exact bytes
exit:   process exit status
```

The runner compares all three fields. It does not combine output streams,
discard diagnostics, compare only successful programs, or normalize
backend-specific text.

## Corpus and backend registration

Each corpus program owns its exact expected stdout, stderr, and exit status in
`# expect-*` headers. Backend adapters live in
`tests/conformance/backends/`. Adding a backend means adding one adapter that
defines its name and compile command; the common runner discovers it
automatically. A backend must not copy, filter, or replace the corpus with a
private suite.

The numeric Core corpus is `tests/conformance/numeric/`. Its Kofun manifest,
`expectations.kofun`, exposes the same observations to future Kofun-native
tooling. The active numeric adapters are `c11-stage1` and `wasm32-node`.
Future native adapters must consume the same `.kofun` programs.

## Unsupported cases and coverage

A backend may mark a case `unsupported` only after compilation returns an
explicit unsupported-feature diagnostic. Unsupported cases are skips, not
passes. A missing observation, crash, signal termination, timeout, or empty
adapter result is a failure.

Every run prints:

```text
PASSED passed; FAILED failed; SKIPPED explicitly skipped
coverage: EXECUTED/TOTAL cases executed by BACKEND
```

The detailed report lists every skipped case and its compile diagnostic.
This makes lost coverage visible when an existing backend regresses or a new
backend is registered.

The common runner treats missing executables, crashes, signals, and timeouts as
failures. It compares output files with `cmp` so trailing newlines and empty
streams remain observable.

## Runtime failures

Expected runtime failures are ordinary observations. In particular, division
by zero and integer overflow must be allowed to run; they pass only when their
stdout, stderr, and exit status exactly match the manifest. A host signal such
as `SIGFPE`, a C undefined-behavior result, or an interpreter traceback is a
contract failure.
