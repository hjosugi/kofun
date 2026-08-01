# Kofun VS Code metadata

This extension provides `.kofun` language registration, comments, brackets,
indentation, TextMate highlighting, and snippets, plus a bundled stdio language
server offering inline diagnostics, go-to-definition, hover types, completion,
a document outline, find-all-references, occurrence highlighting, and inlay
hints. The server starts when a Kofun document opens. Everything it reports for
the covered Stage 2 subset consumes one validated in-memory typed sidecar;
sources outside the bounded producer profile use a visibly labelled syntactic
fallback.

## What the editor shows

- **Inlay hints** put the callee's parameter name — and its `read`/`edit`/`take`
  mode — before each call argument, so `process(data)` reads as
  `process(take data:)` without opening the callee. The mode is declared in the
  signature but its consequence lands on the caller, which is why it is shown
  here. A `let` without a written type shows the inferred one; a type the
  server cannot determine is left blank rather than guessed at. Each of the
  three is switchable under `kofun.inlayHints`.
- **Completion** offers the names visible at the cursor under the same
  visibility and shadowing rules go-to-definition resolves by, so the two never
  disagree. Items state whether their type came from the validated sidecar or
  the syntactic fallback.
- **Outline and breadcrumbs** nest each function's parameters and locals under
  it.
- **References and highlights** resolve through the same rule, so a shadowed
  name is not swept up with the one shadowing it.
- **Signature help** tracks which argument the caret is in, across nested
  calls, and shows nothing outside a call rather than the last signature it
  produced.
- **Folding** covers block bodies and runs of comment lines; **selection
  ranges** expand from the token through each enclosing block.
- **Semantic tokens** colour by what a name resolved to, not by its spelling,
  so a parameter and a local sharing a name are drawn differently. An
  unresolved name is left to TextMate rather than coloured as a guess.
- **Rename** covers locals and parameters. Renaming a function or a type is
  refused: they can be named from a file this server never reads, so the rename
  would edit some uses and leave others behind.
- **Tasks** run `kofun check`, `build`, and `test` on the active file.

## What is deliberately absent

There is no code-action provider. No diagnostic in `tests/diagnostics/registry.tsv`
carries a remedy today, so the capability would advertise a quick-fix list the
server can never fill; it belongs here once one does. Completion advertises no
trigger characters for the same reason — member and field completion is not
implemented, so `.` must not promise a list that cannot be produced.

The tasks contribute no problem matcher. The CLI reports byte offsets rather
than line and column, so a matcher would place every problem on the wrong line;
diagnostics come from the language server, which converts those spans correctly
against the open document.

There is no formatter, rename, or workspace symbol list: no `kofun fmt` exists,
rename without whole-project reference coverage would be unsafe, and the server
does not read unopened files, so a workspace symbol list would be silently
partial.

## Commands

- **Kofun: Restart Language Server** — stops and restarts the bundled server.
- **Kofun: Show Language Server Output** — opens the server's output channel,
  which is also what the status-bar item opens when clicked.

A status-bar item reports whether the server is starting, running, or stopped,
because a server that died is otherwise invisible until a request quietly
returns nothing.

Run `npm --prefix editor/vscode run vscode:prepublish` before opening
`editor/vscode` with VS Code's extension development host or packaging a VSIX.
That command builds the host Node-API producer bridge and stages the audited
codec/projector beside the bundled server. A C11 compiler and Node headers are
required; set `NODE_INCLUDE_DIR` only when the headers are outside the Node
installation prefix and standard include locations.

Set `kofun.languageServer.path` only when testing another server build.
