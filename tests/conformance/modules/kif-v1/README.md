# KIF v2 executable checkpoint

This gate exercises the first compiler-authoritative KIF writer/reader slice
for bounded `Int` functions and flat zero/one-`Int`-payload ADTs. It uses the
production PackageId, ModuleId, NamespaceId, and SymbolId domains plus the v2
public and package-internal semantic digest domains. Function signatures pair
each parameter type with a canonical external-label or explicit-unlabelled
entry; internal parameter names are absent.

The binary is authoritative. The JSON file is emitted only after a complete
binary read and says `"authoritative": false`; no compiler path accepts it as
input. Private declarations, bodies, spans, source paths, and declaration order
are excluded from KIF. Public and internal-only facts are encoded separately.

The gate also writes public function/module export facts. It validates every
ExportBindingId independently from the original target, rejects repeated IDs
inside an ordered export chain, and recomputes a qualified module target's
ModuleSelfSymbolId from its bounded canonical module path. A source-only
qualified consumer then calls the exported function through the facade KIF
with both same-package and external public views. Its HIR retains the facade
binding, original target, and complete ordered chain. Failed, aliased, and
injected pre-rename writes preserve the prior output.

Run:

```sh
sh tests/conformance/modules/kif-v1/run.sh
```
