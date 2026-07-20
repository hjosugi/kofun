# Issue 34: language server

## Verified starting point

The VS Code extension registers `.kofun`, brackets, comments, indentation, and
a TextMate grammar. It declares no language client or server. Stage 2 can
produce lexical token byte spans and top-level function byte spans, but the
active compiler does not expose resolved definitions, inferred hover types, or
an incremental document API.

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
- [ ] A `.kofun` language server implements the protocol baseline.
- [ ] Incremental document versions and cancellation prevent stale results.
- [ ] Inline diagnostics update and clear correctly while typing.
- [ ] Definition resolves all required declaration categories.
- [ ] Hover exposes normalized types and parameter modes.
- [ ] Unicode position conversion passes the protocol fixtures.
- [ ] The packaged VS Code client starts and stops the real server.
- [ ] The recorded 10,000-declaration benchmark meets every threshold.
