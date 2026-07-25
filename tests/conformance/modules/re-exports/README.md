# Explicit public re-exports

This gate exercises the bounded resolver for the accepted
`pub import module.path` and `pub from module.path import Name` forms. It
preserves original `ModuleId`/`NamespaceId`/`SymbolId` values, gives every
facade edge a distinct `ExportBindingId`, rejects visibility widening, and
serializes public export facts into the authoritative KIF v1 interface.

The line-oriented HIR and documentation projection are focused inspection
artifacts. The KIF file is authoritative. The current executable slice covers
same-package modules, bounded `Int` functions, flat ADTs, their value/type
namespace expansion, and chains up to 64 edges. It does not grant linker, FFI,
or runtime forwarding and does not accept aliases, globs, external packages,
or non-public forwarding.

Run:

```sh
sh tests/conformance/modules/re-exports/run.sh
```
