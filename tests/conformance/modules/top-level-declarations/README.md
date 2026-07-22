# Top-level declaration-table gate

This focused C11 gate consumes already-validated package/module/file identities
and one source file per module. It collects function and bounded flat-ADT
headers before resolving bodies, assigns the production namespace/symbol
SHA-256 identities from `spec/modules/module-identity.md`, and writes output
only after the whole inventory succeeds.

The pipe-delimited test inventory is an adapter protocol, not Kofun manifest
syntax. Its fields are `PackageId|ModuleId|FileId|logical-path|source-operand`.
The source operand is never serialized; logical provenance is.

The gate intentionally does not implement imports, partial modules, KIF,
layout, or backend emission. Cross-module lookup remains issue #113, while KIF
emission remains issue #575.
