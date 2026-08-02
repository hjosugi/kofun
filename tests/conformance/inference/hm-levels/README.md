# Bounded Algorithm J with levels

This gate owns the independent implementation slice from issue #557. It is a
typed frontend only: it does not lower a backend artifact and does not modify
the Stage 2 compiler transliteration pair.

The accepted wrapper is one `fn main() { ... }` containing immutable local
`let` bindings and a final value. Expressions are `Int`, `Bool`, and `Text`
literals, local variables, unary non-recursive lambdas, unary direct
application, and parentheses. `let` bindings and lambda parameters may carry
`Int`, `Bool`, `Text`, or unary function annotations.

The implementation uses mutable metavariables with `(id, level, link)`.
Unification performs the occurs check and lowers levels in the same traversal.
Only an immutable `let` whose initializer is syntactically a lambda is
generalized. Application and control-flow results therefore remain
monomorphic. Instantiation replaces a binding's quantified variables with
fresh metavariables at the use level.

`kofun-hm-levels-ir/v1` records:

- every local and parameter binding with a shadow-safe `BindingId` and
  canonical type scheme;
- every variable use with that binding identity and its instantiated monotype;
- byte spans and the final block type, but no absolute path or ambient value.

The fixture set proves independent `Int`/`Bool`/`Text` instantiations, captured
outer metavariables, stable shadowing identities, annotation checking,
alpha-normalized schemes, an explicit alpha-renamed source pair, source-order
independence for unrelated bindings, path independence, the conservative value
restriction, occurs checking, and level-escape refusal. Recursion,
named-function inference, traits, rows,
records, `match`, effects, ownership modes, mutable locals, and backend
lowering are explicit refusals with no partial artifact.

`HML001`-`HML007` are registered repository-wide with this focused gate as
their executable owner. Syntax, unification, occurs-check, resolution,
recursion, unsupported-feature, and resource-limit families all preserve the
transactional no-artifact rule.

Implementation limits are fixed: 65,536 source bytes, 4,096 tokens, 8,192
type nodes, 1,024 bindings, 4,096 uses, 64 generalized variables per binding,
63-byte identifiers, and 128 parser nesting levels. Limit or refusal failures
exit 1 and leave neither typed IR nor token artifacts. Before any source or
output access, the frontend also proves that the source, both outputs, and both
derived temporary paths are pairwise distinct (including existing-file
identity); unsafe aliases exit 2 without changing the source or prior output
artifacts. The gates only recursively replace the exact generated
`build/hm-levels[.SAFE_SUFFIX]` and `build/hm-levels-fuzz[.SAFE_SUFFIX]`
namespaces.

Run:

```sh
sh tests/conformance/inference/hm-levels/run.sh
KOFUN_HM_LEVELS_CASES=128 sh tests/fuzz/hm_levels.sh
# or run both through the repository task
task hm-levels
```
