# Optional(Int) canonical-pair gate

`compiler.kofun` is the canonical Stage 2 source and `compiler.c` is the
temporary audited C11 seed. Optional(Int) lowering first landed only in the C
surface, while the ordinary round-trip gate kept passing because it checks
that the Kofun source survives parsing, not that both files carry the same
semantic families.

This gate derives every function whose name contains `optional_int` from each
surface and requires the sets to agree. It separately pins the lowering
dispatches that make those helpers load-bearing: pre-output validation,
parameter/result representation, contextual `null`, proved payload projection,
tag-test conditions, whole-value let/return transport, and emitted
AggregateLayout declarations. Minimum call counts prevent a matching dead
helper from satisfying the member check.

One transliteration difference is intentional. C returns an
`OptionalCondition` and writes the binding byte through an out-parameter;
Kofun has no out-parameters, so `optional_int_condition_binding` carries that
second result. The gate excludes only that named shape difference and still
requires both consumers.

The self-test renames one member on each side and disconnects the Kofun
pre-lowering validation call. Each mutation must apply and fail with the named
missing member or dispatch. This is not whole-file textual equality and it does
not replace the executable Optional construction/narrowing corpora.
