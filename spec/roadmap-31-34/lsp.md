# Issue 34: language server

## Implemented bootstrap

The VS Code extension bundles a dependency-free stdio language server and a
small client. The server owns versioned open-document text, applies LSP range
changes, and indexes the current bootstrap syntax in a linear pass. It provides
lexical/delimiter diagnostics, same-document definitions, declared and basic
literal hover types, and parameter modes. It explicitly reports unavailable or
incomplete inference rather than claiming compiler inference or using `Any`.

`sh tests/lsp/check.sh` covers fragmented framing, lifecycle, incremental edits,
stale versions, Unicode UTF-16 positions, diagnostics, definition, hover, a
packaged-client smoke test, and the numeric 10,000-declaration gate. Raw
performance and memory samples are written to `build/lsp/performance.json`.

## Protocol baseline

The first server must implement JSON-RPC/LSP framing and these methods:

- `initialize`, `initialized`, `shutdown`, and `exit`;
- `textDocument/didOpen`, `textDocument/didChange`, and
  `textDocument/didClose`;
- `textDocument/publishDiagnostics`;
- `textDocument/definition`; and
- `textDocument/hover`.

The client must negotiate UTF-16 positions unless both sides explicitly select
another standard LSP position encoding. Compiler byte spans must be converted
at the protocol boundary and tested with ASCII, multibyte UTF-8, combining
characters, and non-BMP characters.

Every document state is keyed by canonical URI and monotonically increasing
version. Results computed for an older version must never be published after a
newer change. Closing a document clears its diagnostics. File access must not
escape the workspace roots negotiated at initialization.

## Semantic behavior

Diagnostics must include stable code, severity, exact range, concise message,
and related declaration information when available. Syntax recovery must
preserve diagnostics and semantic results outside the damaged region.

Definition must resolve local bindings, parameters, functions, and types to
their declaration ranges. Ambiguous or unresolved references return no
location plus a normal diagnostic; the server must not guess.

Hover must return the normalized inferred or declared type and the parameter
mode (`read`, `edit`, or `take`) when applicable. It must distinguish an
unknown type caused by an incomplete edit from a valid `Any`.

## Incremental performance gate

Create a deterministic generated `.kofun` benchmark with 10,000 declarations
and references, including symbols near the start and end. After one warm-up,
apply 100 single-range edits through `didChange`, alternating between those
regions.

On the documented reference machine:

- diagnostic publication for the edited version has p95 latency at or below
  100 ms and maximum latency at or below 250 ms;
- definition and hover requests have p95 latency at or below 50 ms;
- no response or diagnostic carries a stale document version; and
- resident memory growth from the first to the hundredth edit is below 10%.

Record raw per-operation timings, compiler revision, machine description, file
digest, and process memory samples. A syntax-only TextMate response or a
benchmark that sends full-document replacement changes does not satisfy this
gate.

## Test layers

1. Framing tests split headers and JSON bodies at every boundary.
2. Position tests cover byte-to-UTF-16 conversion and edit application.
3. Protocol transcript tests cover lifecycle, cancellation, stale results, and
   malformed messages.
4. Semantic fixtures cover diagnostics, definition, hover, shadowing, and
   recovery after incomplete edits.
5. The editor smoke test launches the packaged client against the real server.
6. The incremental benchmark enforces the thresholds above in a dedicated
   performance job.

## Executable close checklist

- [x] VS Code syntax registration and TextMate highlighting exist.
- [x] A `.kofun` language server implements the protocol baseline.
- [x] Incremental document versions and synchronous request handling prevent stale results.
- [x] Inline diagnostics update and clear correctly while typing.
- [x] Definition resolves local bindings, parameters, functions, and types.
- [x] Hover exposes available normalized types and parameter modes.
- [x] Unicode position conversion passes the protocol fixtures.
- [x] The packaged VS Code client starts and stops the real server.
- [x] The recorded 10,000-declaration benchmark meets every threshold.
