# Selective same-package imports

This gate exercises the bounded `from module.path import Name, Other` resolver.
It keeps value and type namespaces separate, preserves the original `SymbolId`,
and records a local `ImportBindingId` without creating a declaration identity.

The adapter inventory and line-oriented HIR are focused conformance artifacts;
neither is a source manifest, KIF, or an authoritative typed sidecar. The slice
supports local-package functions and flat nominal types only. Aliases, wildcard
imports, re-exports, external packages, and transitive imports are rejected.

Run it with:

```sh
sh tests/conformance/modules/imports-selective/run.sh
```
