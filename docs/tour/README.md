# Kofun browser tour

This directory is a dependency-free static learning surface. `compiler.mjs` is
a browser port of the bounded compiler in `bootstrap/wasm/compiler.c`; its
output is checked byte-for-byte against that audited C seed. The resulting
module is instantiated in memory, so editing, compiling, running, and URL
sharing do not need an application server or remote sandbox.

The editor's hover and completion come from that same compiler.
`analyzeKofun` runs the parser that emits the module and reports the bindings
it accepted, with their source spans; `intelligence.mjs` answers from those
declarations and nothing else. A name the compiler did not accept is reported
as unknown rather than given a plausible type, and a binding is not offered
inside its own initializer, because that is the point at which the parser
makes it visible. The rules match the language server in `tooling/lsp`:
declaration-before-use visibility, prefix narrowing, a bounded list reported as
incomplete, and no answer at all inside a comment.

Run the deterministic gate:

```sh
task tour
```

Preview the static files with the repository's no-package development server:

```sh
node examples/wasm-browser/serve.mjs docs/tour
# open http://127.0.0.1:8080/
```

A static host can publish `docs/tour/` as-is. Snippets use a UTF-8 base64url
fragment such as `#step=names&code=...`; fragments are not sent in HTTP
requests. Shared source is capped at 16 KiB.

## Current boundary

The executable browser profile accepts exactly one zero-argument `fn main`, up
to 128 immutable Int bindings, `print`, Int64 literals and variables,
parentheses, unary `+`/`-`, and checked `+`, `-`, `*`, `/`, `//`, and `%`. It
shares the native seed's 1 MiB source, 1,024-expression, and 256-statement
limits.

Editor intelligence is bounded by the same profile. Every binding it knows is
an `Int`, because that is the only binding this Core accepts; there are no
fields or members to complete, and no ownership mode to report, since the Core
does not parse one. Hover over a name the compiler rejected says so rather
than inventing a type. Pointer position is resolved against a hidden mirror
element that carries the same text and metrics as the editor, so hover lands on
the name under the pointer rather than on a guess from the caret.

It does not implement Text, List, records, general functions, control flow,
linear-memory objects, DOM declarations, WASI, packages, debugging metadata,
an optimizer, or `read`/`edit`/`take` checking. The ownership lesson therefore
teaches the double-sale bug and intended access model, then labels its runnable
arithmetic example as a model rather than an ownership-checker demonstration.

The CSS mascot is only a tour guide. `kofun-hub` is not in this repository and
cannot be integrated by this slice.

## Reading paths

- [Coming from Python](guides/python.md)
- [Coming from TypeScript](guides/typescript.md)
- [Coming from Go](guides/go.md)
- [Coming from Rust](guides/rust.md)
