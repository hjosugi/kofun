# Same-package module aliases

This gate exercises the bounded `import module.path as local` form in the
Stage 2 qualified-import checkpoint.

An alias creates one file-local module qualifier. The HIR retains a distinct
`AliasBindingId`, its identifier span, the importing `ModuleId`/`FileId`, and
the unchanged target `ModuleId`. Qualified calls continue to retain the
original target `SymbolId`; changing only the local alias cannot rename the
target declaration. The final path component is not also introduced.

The v1 slice rejects duplicate dependency edges, qualifier collisions,
keywords and malformed aliases, re-export aliases, selective/per-symbol
aliases, alias chains, relative paths, and external package aliases. Every
failure is transactional.

Run:

```sh
sh tests/conformance/modules/import-aliases/run.sh
```
