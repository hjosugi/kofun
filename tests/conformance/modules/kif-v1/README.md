# KIF v1 executable checkpoint

This gate exercises the first compiler-authoritative KIF writer/reader slice
for bounded `Int` functions and flat zero/one-`Int`-payload ADTs. It uses the
production PackageId, ModuleId, NamespaceId, SymbolId, public semantic digest,
and package-internal semantic digest domains.

The binary is authoritative. The JSON file is emitted only after a complete
binary read and says `"authoritative": false`; no compiler path accepts it as
input. Private declarations, bodies, spans, source paths, and declaration order
are excluded from KIF. Public and internal-only facts are encoded separately.

Run:

```sh
sh tests/conformance/modules/kif-v1/run.sh
```
