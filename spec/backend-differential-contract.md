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
tooling. The active numeric adapters are `c11-stage1`, direct-static
`native-x86_64`, and `wasm32-node`. They consume the same `.kofun` programs.

## Unsupported cases and coverage

A backend omits a corpus from `BACKEND_CORPORA` when the complete corpus is not
supported. Once an adapter declares a corpus, every case is required.
Compilation may still mark a case `unsupported` only after an explicit
unsupported-feature diagnostic; the runner reports that case as a skip for
diagnostic clarity and fails the backend run. A missing observation, crash,
signal termination, timeout, or empty adapter result is also a failure.

Every run prints:

```text
PASSED passed; FAILED failed; SKIPPED explicitly skipped
coverage: EXECUTED/TOTAL cases executed by BACKEND
```

The detailed report lists every skipped case and its compile diagnostic.
Any skipped case in a declared corpus makes the run fail, so supported coverage
cannot silently regress.

The common runner treats missing executables, crashes, signals, and timeouts as
failures. It compares output files with `cmp` so trailing newlines and empty
streams remain observable.

## Runtime failures

Expected runtime failures are ordinary observations. In particular, division
by zero and integer overflow must be allowed to run; they pass only when their
stdout, stderr, and exit status exactly match the manifest. A host signal such
as `SIGFPE`, a C undefined-behavior result, or an interpreter traceback is a
contract failure.
