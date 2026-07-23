# tree-sitter-kofun

Incremental parser and editor queries for Kofun. The grammar follows
`spec/grammar.ebnf` and also names the bounded import, C ABI, and native CLI
declarations used by accepted repository examples.

The newline scanner preserves Kofun's statement boundaries:

- newlines terminate statements at the source-file and `{ ... }` levels;
- newlines are ignored while `(` or `[` is open;
- a line beginning with `|>` continues the preceding pipeline expression.

## Development

Install the exact CLI version recorded in `package-lock.json`, then run every
grammar gate:

```sh
npm ci
npm test
npm run test:repository-corpus
npm run test:queries
```

The repository-corpus gate parses every `.kofun` file below `examples/` and
`tests/kofun/` with zero error or missing nodes. Recovery behavior for
truncated declarations, expressions, delimiters, and significant-newline
boundaries is pinned separately in `test/corpus/recovery.txt`.

## Editor queries

`queries/highlights.scm` and `queries/locals.scm` use Tree-sitter's standard
captures. `queries/indents.scm` uses the `@indent` and `@outdent` captures
consumed by Helix, and `queries/folds.scm` uses the `@fold` capture consumed by
Neovim and Helix.

Until `tree-sitter-kofun` has its own registry release, point an editor parser
configuration at this directory and compile `src/parser.c` together with
`src/scanner.c`. Keep the existing TextMate grammar enabled as a fallback.

## Scope

This package provides syntax trees and syntax-driven editor behavior. It does
not perform name resolution, type-aware highlighting, or replace the Kofun
language server. Publishing to third-party parser registries is intentionally
outside the initial integration gate.
