# Issue 34: language server

## Implemented bootstrap

The VS Code extension bundles a dependency-free stdio language server and a
small client. The server owns versioned open-document text, applies LSP range
changes, and indexes the current bootstrap syntax in a linear pass. It provides
lexical/delimiter diagnostics, same-document definitions, declared and basic
literal hover types, parameter modes, and scope-accurate completion. It
explicitly reports unavailable or incomplete inference rather than claiming
compiler inference or using `Any`.

`sh tests/lsp/check.sh` covers fragmented framing, lifecycle, incremental edits,
stale versions, Unicode UTF-16 positions, diagnostics, definition, hover,
completion scope/prefix/provenance, a packaged-client smoke test, and the
numeric 10,000-declaration gate. Raw performance and memory samples are written
to `build/lsp/performance.json`.

## Protocol baseline

The first server must implement JSON-RPC/LSP framing and these methods:

- `initialize`, `initialized`, `shutdown`, and `exit`;
- `textDocument/didOpen`, `textDocument/didChange`, and
  `textDocument/didClose`;
- `textDocument/publishDiagnostics`;
- `textDocument/definition`;
- `textDocument/hover`;
- `textDocument/completion`;
- `textDocument/documentSymbol`;
- `textDocument/references`;
- `textDocument/documentHighlight`;
- `textDocument/inlayHint`;
- `textDocument/signatureHelp`;
- `textDocument/foldingRange`;
- `textDocument/selectionRange`;
- `textDocument/semanticTokens/full`; and
- `textDocument/prepareRename` and `textDocument/rename`.

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

Completion must offer exactly the names visible at the requested position,
under the same visibility and shadowing rules definition resolves by, so the
two can never disagree about which declaration a name refers to. A local is
not offered before its own declaration, and a parameter is not offered outside
its function body. Each item carries the checked type — and the ownership mode
when the declaration has one — taken from the validated sidecar when one
exists, and states which of the two analyses produced it; a fact from a
document that is still failing is marked provisional, as hover marks it.
Positions inside comments and string literals return no items. Member and
field completion is not implemented, so no trigger character may be advertised
for it. A list bounded for size must be returned as incomplete rather than
silently truncated.

The outline, references, occurrence highlights, and inlay hints resolve names
by the same rule definition does, so none of them may report a declaration
definition would not. The outline nests a function's parameters and locals
under it. Inlay hints put the callee's parameter name and its ownership mode at
the call argument, because the mode is declared in the callee's signature while
its consequence lands on the caller; a `let` without a written type shows the
inferred one, and a type the server cannot determine produces no hint rather
than a guess. A range request must not answer outside the requested range.

A capability may not be advertised for a result the server cannot produce. No
`codeActionProvider` is offered while no registered diagnostic carries a
remedy, for the same reason completion advertises no trigger characters.

Signature help reports the active argument and returns nothing outside a call
rather than the last signature it produced. Folding covers block bodies and
runs of comment lines, and never emits a range that ends before it begins.
Selection ranges expand from the token through each enclosing block to the
document, and every parent strictly contains its child.

The editor client is part of the contract: every capability the server
advertises must have a registered provider, every contributed command must have
a handler, and every contributed setting must be read. A gate asserts all three
against the manifest, because a capability with no provider is invisible and a
setting nothing reads is a promise the extension does not keep.

## Incremental performance gate

Create a deterministic generated `.kofun` benchmark with 10,000 declarations
and references, including symbols near the start and end. After one warm-up,
apply 100 single-range edits through `didChange`, alternating between those
regions.

On the documented reference machine:

- diagnostic publication for the edited version has p95 latency at or below
  100 ms and maximum latency at or below 250 ms;
- definition, hover, and completion requests have p95 latency at or below
  50 ms, with completion measured both on the first request after an edit,
  which rebuilds the lexical index, and on a cached repeat;
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
4. Semantic fixtures cover diagnostics, definition, hover, completion,
   shadowing, and recovery after incomplete edits.
5. The editor smoke test launches the packaged client against the real server,
   covering the LSP-to-VS Code enumeration conversions, the contributed
   commands, tasks, and the status item.
6. The manifest gate checks the released artifact: marketplace metadata, a
   changelog entry for the shipped version, capability/provider and
   command/handler and setting/reader agreement, and the files the VSIX must
   and must not carry.
7. The incremental benchmark enforces the thresholds above in a dedicated
   performance job.

## Executable close checklist

- [x] VS Code syntax registration and TextMate highlighting exist.
- [x] A `.kofun` language server implements the protocol baseline.
- [x] Incremental document versions and synchronous request handling prevent stale results.
- [x] Inline diagnostics update and clear correctly while typing.
- [x] Definition resolves local bindings, parameters, functions, and types.
- [x] Hover exposes available normalized types and parameter modes.
- [x] Completion offers the names visible at the position, with checked types
      and stated provenance.
- [x] Unicode position conversion passes the protocol fixtures.
- [x] The packaged VS Code client starts and stops the real server.
- [x] The recorded 10,000-declaration benchmark meets every threshold.
