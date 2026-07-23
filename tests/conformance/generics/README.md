# Explicit generic function frontend

This focused Stage 2 checkpoint parses and type-checks unbounded generic
functions with explicit type applications. It collects all signatures before
checking bodies, gives each declaration-scoped parameter a distinct
`TypeParameterId`, substitutes explicit type arguments before checking value
arguments, and preserves declaration/use spans in `kofun-generics-ir/v1`.

The positive fixture covers `Int`, `Bool`, and `Text` instantiations, two
type parameters, two declarations that independently name `T`, two
instantiations of one declaration, and a forward generic call. The negative
fixtures freeze duplicate, unknown, runtime-value, missing/incorrect arity,
substitution mismatch, annotation mismatch, bounds, direct and mutual
recursion, unconstrained, and one-over-limit diagnostics.

This is typed-only evidence. It performs no inference, monomorphization,
dictionary selection, C/native/Wasm lowering, layout, or execution.

Run `sh tests/conformance/generics/run.sh`.
