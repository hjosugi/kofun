# Bounded trait declaration and implementation frontend

Executable evidence for #332. `bootstrap/stage2/traits_frontend.c` parses
one-method traits with one type parameter, concrete implementations, and
generic functions carrying exactly one explicit bound; it assigns
TraitId/MethodId/ImplementationId identities and emits typed IR.

Run:

```sh
sh tests/conformance/traits/run.sh
```

## Frontend-only boundary

This slice lowers nothing. No dictionary is elaborated, nothing is
monomorphised, and no runtime search is emitted or implied — the gate asserts
the IR names no dictionary, monomorphisation, or vtable. #471 carries the
dictionary elaboration that follows.

## What `foreign` means here

Cross-package loading is out of scope, so `foreign` is the synthetic stand-in
the issue calls for: it marks a trait or type declaration as belonging to
another package. It exists to make the #403 orphan rule testable in a single
file, and it is not proposed surface syntax.

`type A = B` is an alias. It is transparent for typing *and* for ownership, so
aliasing a foreign type never makes it local and never makes an implementation
admissible — `orphan_alias_ownership` pins that, and its diagnostic names the
type the alias resolves to rather than the alias.

## Identities

- `TraitId` carries the package provenance and the declaration name.
- `MethodId` is the TraitId plus the declaration-order slot.
- `ImplementationId` carries the ABI schema version, the package, the TraitId,
  the normalized concrete type arguments, the outer nominal self-type identity,
  and the implementation declaration.

## Resolution admits exactly one candidate

Overlap is refused where implementations are *declared*, not where they are
used, so no candidate set is ever ordered at a use site.
`order_independence.kofun` is the positive program with every implementation
declared in the opposite order; the gate compares the selected trait and
self-type of every call against the original and requires them to match, while
the declaration ordinals the identities carry do move. That is what makes
"import or source order never selects between candidates" an assertion rather
than a claim.

## Refusals

Every fixture with a `.stderr` golden is a refusal, and the gate asserts its
own list is the same size as the glob, so a fixture added without a gate entry
stops the build (DD-022).

| Fixture | Code | Refuses |
|---|---|---|
| `blanket_implementation` | E2S132 | a generic or blanket implementation |
| `default_method` | E2S132 | a default method body in a trait |
| `duplicate_trait` | E2S127 | two traits with the same name |
| `method_arity_mismatch` | E2S128 | an implementation with the wrong parameter count |
| `method_name_mismatch` | E2S127 | an implementation of a method the trait does not declare |
| `method_parameter_mismatch` | E2S128 | a parameter type that differs after substitution |
| `method_result_mismatch` | E2S128 | a result type that differs after substitution |
| `missing_implementation` | E2S129 | a bound with no candidate |
| `multiple_bounds` | E2S132 | a second bound |
| `orphan_alias_ownership` | E2S131 | an alias used to claim ownership |
| `orphan_both_foreign` | E2S131 | a foreign trait for a foreign type |
| `overlapping_implementation` | E2S130 | two implementations for one trait and self-type |
| `recursive_bound` | E2S132 | a bound whose argument is not the bounded parameter |
| `trait_arity_mismatch` | E2S127 | the wrong number of trait type arguments |
| `two_type_parameter_trait` | E2S132 | a trait with more than one type parameter |
| `unbounded_method_call` | E2S129 | a trait method called without a bound providing it |

The frontend owns `E2S127`–`E2S133`.
