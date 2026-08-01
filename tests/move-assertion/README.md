# Move-assertion corpus

The bounded first artifact for
[#572](https://github.com/hjosugi/kofun/issues/572): the written distinction
between semantic `take` and optimization-only managed moves
(docs/MEMORY_MODEL.md §14), and the unstable compile-time assertion
`compiler.ensure_move(value)` with its explained failures.

```sh
sh tests/move-assertion/check.sh
```

## What is proved here

**The assertion is compile-time only and erased.**
[`zero_footprint_with.kofun`](zero_footprint_with.kofun) and
[`zero_footprint_without.kofun`](zero_footprint_without.kofun) differ by
exactly the assertion line — the gate checks that textually — and must
produce identical stdout on the reference executor and the C11 backend.
The gate then compares the two emitted C translation units byte for byte
and requires them identical, and requires that neither names the
assertion. There is no counter, no helper call, no reordering: the
assertion compiles to nothing.

**Provable last uses compile.** [`last_use.kofun`](last_use.kofun) is the
straight-line case: earlier reads that are arguments to Copy-result calls
(`print`, `len`, an Int-returning function), then the assertion as the
final use. [`scoped_last_use.kofun`](scoped_last_use.kofun) proves a
binding declared and asserted inside the same conditional arm — the
narrow rule is scope-local, not top-level-only.

**Every implemented failure reason rejects with its explanation**, as
diagnostic `E2S146`, checked against the exact public line and — separately
— against the reason word #572 requires:

| Fixture | Reason |
|---|---|
| [`later_use.kofun`](later_use.kofun) | later use — read again after the assertion |
| [`loop_repeat.kofun`](loop_repeat.kofun) | later use — assertion inside a loop the binding outlives |
| [`possible_alias.kofun`](possible_alias.kofun) | possible alias — an earlier read is a whole `let` initializer |
| [`borrowed_parameter.kofun`](borrowed_parameter.kofun) | possible alias — parameters are borrowed views |
| [`branch_mismatch.kofun`](branch_mismatch.kofun) | branch mismatch — one arm asserts, the other reads |
| [`escaping_capture.kofun`](escaping_capture.kofun) | escaping capture — a lambda captures the binding |
| [`copy_value.kofun`](copy_value.kofun) | backend limitation — Copy values have no managed storage |
| [`no_storage_identity.kofun`](no_storage_identity.kofun) | backend limitation — a function name is not a storage identity |
| [`no_value.kofun`](no_value.kofun) | usage — the assertion has no value |
| [`malformed_argument.kofun`](malformed_argument.kofun) | usage — exactly one bare binding name |

## The narrow rule, stated

The assertion compiles only when the Stage 2 slice can prove, cheaply and
soundly, that the argument is a managed local binding at its last use:

- an immutable local binding of managed type (`Text` or `List`), named
  directly;
- asserted in the binding's own scope — conditional arms and loops the
  binding outlives are conservatively rejected, not analysed;
- no use at any later byte, in any scope;
- no use inside any lambda, before or after the assertion;
- every earlier read provably alias-free: an `==`/`!=` operand, or an
  argument to a call whose result is a Copy value or no value. With no
  globals and no escaping views in this slice, a call's only channel out
  is its result, so a Copy result cannot carry the storage.

A failed assertion never downgrades and never falls back: a source file
that combines the assertion with syntax outside the Stage 2 slice is
rejected by the Stage 1 seed compiler, which does not accept
`compiler.ensure_move` at all. The gate's negative cases run through
`./bin/kofun check` to prove the refusals users actually see.

## Unstable, and deliberately so

The assertion is compiler/stdlib-facing and gated as unstable: every
`E2S146` diagnostic says `unstable`, docs/MEMORY_MODEL.md §14 declares the
name, code, and rule all subject to change, and `compiler.ensure_move` is
reserved while that holds. It is not language surface, and everyday code
should never contain it.

## Out of scope, recorded

Deliberately not in this artifact, per the bounded first slice of #572:

- general last-use inference over branches, loops, and aggregates —
  the analysis here proves straight-line, same-scope uses only and
  rejects everything else with a reason;
- allocation counters and the builder/concatenation workload evidence;
- optimization remarks for successful or missed moves;
- the `unknown foreign call` reason — reserved in the diagnostic
  vocabulary, unreachable while the slice can express no foreign call;
- `take`-parameter handoff — `take` is not yet expressible in the Stage 2
  slice, so the borrowed-parameter rejection stands in its place;
- [#576](https://github.com/hjosugi/kofun/issues/576)'s constructor reuse
  analysis, which builds on this assertion once it exists.
