# Resolved ADT exhaustiveness

This focused gate joins the production top-level declaration table, lossless
Pattern tree, and lexical ScopeId/BindingId projection into one bounded typed
match input. Coverage uses resolved ADT and constructor `SymbolId` values, not
constructor spelling.

The first slice accepts flat nominal ADTs, whole constructor patterns,
wildcards, and binding catch-alls. One-`Int` payload constructors may use `_`
or one binding. Guards are conservative and never remove a constructor from
the uncovered set. Nested payload usefulness remains an explicit follow-up.

Or-patterns expand into the alternatives one arm tests left to right, and
grouping parentheses carry no coverage meaning. Every alternative of one arm
must bind the same names with the same payload roles (`E2S105`) so the arm body
sees one `BindingId`. An alternative that repeats an already-matched
constructor is `E2S26`, including inside a guarded arm, because both
alternatives select the same constructor and the guard cannot give the later
one a case the earlier one did not already test. The emitted
projection publishes one `typed-alternative` row per tested alternative, and
one arm accepts at most 64 of them.

Run it with:

```sh
sh tests/conformance/adt-exhaustiveness/run.sh
```
