# Nested ADT usefulness v1

This focused checkpoint fixes the first nested constructor-specialisation rule
without claiming production source typing or lowering. Its input names resolved
nominal ADT and constructor identities, including the resolved payload-owner
identity for one single-payload outer constructor. Display names are diagnostics
only.

The admitted matrix has one scrutinee column and at most one nested payload
column. A whole outer constructor covers all of its payload constructors; a
nested row covers exactly its resolved outer/inner pair; `_` covers every case
at its admitted column. Missing witnesses are ordered by outer then inner
constructor ordinal. Deeper recursion, multiple payload columns, guards,
bindings, source typing, runtime representation, and executable lowering remain
outside this child.

The model accepts 1..16 ADTs, 1..64 constructors in total, 1..128 rows, and at
most 4096 checked usefulness visits. The 64-constructor total implies the
1024-cell maximum (32 outer constructors by 32 payload constructors), so a
1025th cell is unreachable without first crossing the constructor bound.

Run the standalone C11, identity, limits, transactional, sanitizer, and static
analysis corpus with:

```sh
sh tests/conformance/adt-nested-usefulness/run.sh
```
