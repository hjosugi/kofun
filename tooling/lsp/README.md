# Kofun language server

`kofun-lsp` is a dependency-free stdio language server for the bootstrap
language. It implements incremental document synchronization, versioned
diagnostics, go-to-definition, and hover.

```sh
tooling/lsp/kofun-lsp
```

The server intentionally exposes only semantics it can derive reliably from an
open document:

- lexical/delimiter diagnostics;
- functions, parameters, `let`/`for` bindings, and planned `type`
  declarations;
- declared types, basic literal types, and parameter modes.

It does not read unopened files, build a project-wide import graph, or claim
full compiler type inference. Unknown types are reported as either an
incomplete edit or unavailable inference, never as `Any`. Range changes update
the in-memory text incrementally; the small semantic index is rebuilt in one
linear pass. The 10,000-declaration test in `tests/lsp` measures that trade-off
against the latency and memory gates in the LSP contract.

All protocol positions are UTF-16, as negotiated by `initialize`. This matches
LSP's default and JavaScript editor strings, including combining characters
and surrogate pairs.
