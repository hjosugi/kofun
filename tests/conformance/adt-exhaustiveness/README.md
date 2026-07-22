# Resolved ADT exhaustiveness

This focused gate joins the production top-level declaration table, lossless
Pattern tree, and lexical ScopeId/BindingId projection into one bounded typed
match input. Coverage uses resolved ADT and constructor `SymbolId` values, not
constructor spelling.

The first slice accepts flat nominal ADTs, whole constructor patterns,
wildcards, and binding catch-alls. One-`Int` payload constructors may use `_`
or one binding. Guards are conservative and never remove a constructor from
the uncovered set. Nested payload usefulness and or-pattern expansion remain
explicit follow-ups.

Run it with:

```sh
sh tests/conformance/adt-exhaustiveness/run.sh
```
