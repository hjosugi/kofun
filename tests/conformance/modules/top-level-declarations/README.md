# Top-level declaration-table gate

This focused C11 gate consumes already-validated package/module/file identities
and one source file per module. It collects function and bounded flat-ADT
headers before resolving bodies, assigns the production namespace/symbol
SHA-256 identities from `spec/modules/module-identity.md`, and writes output
only after the whole inventory succeeds.

The pipe-delimited test inventory is an adapter protocol, not Kofun manifest
syntax. Its fields are `PackageId|ModuleId|FileId|logical-path|source-operand`.
The source operand is never serialized; logical provenance is.

This gate exercises the import-free compatibility mode. The same resolver's
qualified-import mode is covered separately by
`../imports-qualified/run.sh`; partial modules, KIF, layout, and general
backend emission remain outside both focused gates. KIF emission remains
tracked by issue #575.
