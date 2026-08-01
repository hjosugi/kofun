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
- No formatter, rename, or workspace symbols. There is no `kofun fmt`; rename
  without whole-project reference coverage would be unsafe; and the server does
  not read unopened files, so a workspace symbol list would be silently partial.

## 0.3.0

- Diagnostics, go-to-definition, and hover types from one validated in-memory
  typed sidecar, with a visibly labelled syntactic fallback outside the bounded
  producer profile.
- `.kofun` language registration, comments, brackets, indentation, and TextMate
  highlighting.
