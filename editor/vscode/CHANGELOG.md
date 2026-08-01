# Changelog

All notable changes to the Kofun VS Code extension. Versions track the
repository's `release: prepare` steps rather than a separate cadence.

## 0.4.0

Editor features, all served by the bundled language server and all restricted
to what the validated typed sidecar or the labelled syntactic fallback can
actually support.

### Added

- **Inlay hints.** The callee's parameter name and its `read`/`edit`/`take`
  mode are shown at each call argument, so `process(data)` reads as
  `process(take data:)` without opening the callee. A `let` with no written
  type shows the inferred one. Each of the three is switchable under
  `kofun.inlayHints`.
- **Completion** over the names visible at the cursor, resolved by the same
  rules go-to-definition uses. Items state whether their type came from the
  validated sidecar or the syntactic fallback, and a fact from a document that
  is still failing is marked provisional.
- **Document outline**, nesting each function's parameters and locals under it.
- **Find all references** and **occurrence highlighting**, both shadow-safe.
- **Signature help** with the active parameter tracked across nested calls.
- **Folding ranges** for blocks and for runs of comment lines.
- **Selection ranges** expanding from the token through each enclosing block.
- **Semantic tokens.** Names are coloured by what they resolved to rather than
  by their spelling, so a parameter and a local sharing a name are drawn
  differently, declarations carry a declaration modifier, and builtins carry
  `defaultLibrary`. An unresolved name is left to TextMate rather than coloured
  as a guess.
- **Rename** for locals and parameters, rewriting the declaration and every use
  in one edit. It refuses a name that is a keyword, a builtin, not a valid
  identifier, or already visible where the old one is used — that last case
  would capture a different declaration.
- **Tasks** for `kofun check`, `build`, and `test`.
- **Snippets** for the declaration forms, including one per ownership mode.
- **Commands** to restart the language server and open its output, and a
  status-bar item reporting whether the server is starting, running, or stopped.

### Not included, deliberately

- No code-action provider. No diagnostic in the repository's registry carries a
  remedy today, so the capability would advertise a quick-fix list the server
  can never fill.
- No completion trigger characters. Member and field completion is not
  implemented, so `.` must not promise a list that cannot be produced.
- No problem matcher on the tasks. The CLI reports byte offsets rather than
  line and column, so a matcher would place every problem on the wrong line;
  diagnostics come from the language server, which converts those spans
  correctly against the open document.
- No renaming of functions or types. They can be named from a file this server
  never reads, so a rename would edit some uses and silently leave others
  behind. The request is refused with that reason rather than performed
  partially.
- No formatter and no workspace symbols. There is no `kofun fmt`, and the
  server does not read unopened files, so a workspace symbol list would be
  silently partial.

## 0.3.0

- Diagnostics, go-to-definition, and hover types from one validated in-memory
  typed sidecar, with a visibly labelled syntactic fallback outside the bounded
  producer profile.
- `.kofun` language registration, comments, brackets, indentation, and TextMate
  highlighting.
