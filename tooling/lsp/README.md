# Kofun language server

`kofun-lsp` is a dependency-free stdio language server for the bootstrap
language. It implements incremental document synchronization, versioned
diagnostics, go-to-definition, hover, completion, a document outline,
find-all-references, occurrence highlighting, and inlay hints.

```sh
tooling/lsp/kofun-lsp
```

The server intentionally exposes only semantics it can derive reliably from an
open document:

- lexical/delimiter diagnostics;
- functions, parameters, `let`/`for` bindings, and planned `type`
  declarations;
- declared types, basic literal types, and parameter modes;
- completion over the names in scope at the cursor, plus the builtin and
  keyword vocabulary.

Completion answers with the declarations that are visible at the requested
position, resolved by the same rules go-to-definition uses, so the two never
name different declarations. Each item reports whether its type came from the
validated typed sidecar or from the labelled syntactic fallback. Members and
fields are not completed and no trigger character is advertised for them, so
`.` never opens a list this server cannot fill. Long lists are filtered by the
identifier already typed and then bounded at 200 items, and a bounded list is
returned as incomplete so the editor asks again as the prefix narrows.

It does not read unopened files, build a project-wide import graph, or claim
full compiler type inference. Unknown types are reported as either an
incomplete edit or unavailable inference, never as `Any`. Range changes update
the in-memory text incrementally; the small semantic index is rebuilt in one
linear pass. The 10,000-declaration test in `tests/lsp` measures that trade-off
against the latency and memory gates in the LSP contract.

All protocol positions are UTF-16, as negotiated by `initialize`. This matches
LSP's default and JavaScript editor strings, including combining characters
and surrogate pairs.

For resource safety, framing headers are limited to 8 KiB and message bodies to
32 MiB. Oversized or unterminated headers receive a parse error and terminate
the connection. Delimiter nesting uses linear storage and constant-time
enclosing-block lookup.
