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

`tests/conformance/capabilities.tsv` is the sole backend/corpus capability
authority. It contains exactly one `supported` or `unsupported` row for every
discovered adapter and registered corpus. Supported rows name an evidence
path. Unsupported rows carry a stable reason. The executable validator rejects
missing, duplicate, unknown, contradictory, and reasonless rows. Adapters and
corpus expectation files must not carry independent backend lists.

The numeric Core corpus is `tests/conformance/numeric/`. Its Kofun manifest,
`expectations.kofun`, exposes the same observations to future Kofun-native
tooling. Every backend marked supported for numeric consumes the same
`.kofun` programs.

## Unsupported cases and coverage

A backend/corpus pair may be `unsupported` only in the canonical manifest,
with a stable reason. Unsupported policy is corpus-level and is not counted as
executed coverage. Once a pair is `supported`, a case-level skip or adapter
status 125 is a failure, as are a missing observation, zero executed cases,
crash, signal termination, timeout, or empty adapter result.

Target capability is independent from executor availability. An adapter may
define an availability check for an external executor such as
`qemu-aarch64`. The runner reports that host condition as `UNAVAILABLE`
without rewriting the target's manifest state.

Every run prints:

```text
PASSED passed; FAILED failed; 0 explicitly skipped
coverage: EXECUTED/TOTAL cases executed by BACKEND
```

This makes lost coverage a gate failure when an existing supported backend
regresses or a new backend is registered.

The common runner treats missing executables, crashes, signals, and timeouts as
failures. It compares output files with `cmp` so trailing newlines and empty
streams remain observable.

## Runtime failures

Expected runtime failures are ordinary observations. In particular, division
by zero and integer overflow must be allowed to run; they pass only when their
stdout, stderr, and exit status exactly match the manifest. A host signal such
as `SIGFPE`, a C undefined-behavior result, or an interpreter traceback is a
contract failure.
