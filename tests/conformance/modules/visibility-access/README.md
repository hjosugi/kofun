# Identity-only visibility access gate

This gate implements issue #582 independently of import parsing. The C11
decision engine accepts already-resolved, schema-tagged 32-byte package,
module, file, and optional owner identities. It has no filesystem, import,
source-name, target, linker, environment, or runtime access.

`cases.tsv` is a table of synthetic canonical identities plus display-only
path/name fields. The driver converts the numeric seeds to bounded 32-byte test
IDs, invokes `kofun_decide_access`, and compares every structured result with
`expected.txt`. Display fields are deliberately not passed to the engine.

The corpus covers same-file and same-owner private access, same-package
internal access, public and enclosing reachability, cross-package safe
disclosure, unsupported restricted visibility, identity schema mismatch, the
64-boundary limit, alias provenance, and path/order permutations. Denied or
unsupported decisions always carry `usable=no`; no runtime check is emitted.

Run it with:

```sh
sh tests/conformance/modules/visibility-access/run.sh
```

This engine becomes a resolver primitive for #111/#113. It does not discover
declarations, parse imports, or claim that cross-file modules are executable.
