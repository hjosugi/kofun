# Typed upgrade patches research

Status: bounded research decision and executable model for issue #741. It
depends on the term-identity decision in #740 and does not enable production
workspace writes.

## Decision

Keep a versioned read-only patch schema, relation classifier, dependency-
frontier calculation, and deterministic preview. Defer an opt-in automatic
writer until compiler-produced semantic identity and target-branch validation
exist. Reject arbitrary AST migration, patch-driven code execution, and any
progress metric that omits unresolved dependents.

The patch addresses a declaration by the pair `(SymbolId, term identity)`.
`SymbolId` locates the nominal API; term identity proves that the preview saw
the exact bounded semantic content. Neither value grants write or execution
authority.

Unison's pinned term/type edit and patch encodings motivate explicit
replacement/deprecation data and typed frontiers:
<https://github.com/unisonweb/unison/tree/e1870038739ffcb27b4e3b483dafd2c21f6541b2/unison-hashing-v2>.
Kofun does not adopt its content-addressed codebase or causal branch model.
The Language Server Protocol `WorkspaceEdit` remains a transport/presentation
mechanism, not semantic proof:
<https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#workspaceEdit>.

## Research questions and answers

1. **Identity.** Use `SymbolId + term identity`; bind the preview separately to
   the workspace semantic-graph digest and each edited file digest.
2. **Relations.** Mechanically classify exact-type, safe compatible subtype,
   ownership-changing, effect-changing, ABI-changing, and incompatible.
   Visibility/law obligations are evidence fields and may force manual review.
3. **Automation.** Only an exact-type `rename-callee` with exact source text,
   current digests, precise non-overlapping spans, and successful target-branch
   validation is eligible. Compatible type, effect, ownership, ABI, generic,
   trait/law, re-export, or capability changes are preview-only.
4. **Comparison dimensions.** Parameter/result types are compared with explicit
   variance and a bounded subtype table. Ownership modes, effects, ABI,
   visibility, and law obligations are independent dimensions and never erased.
5. **Frontier.** Traverse the complete reverse semantic dependency graph from
   each old identity. Report direct covered or blocked nodes, transitive escaped
   nodes, already-migrated nodes, and unresolved nodes. Progress is `(migrated +
   safely covered) / complete frontier`; hiding a failure changes the graph
   digest and invalidates the preview rather than improving progress.
6. **Composition.** Disjoint replacements compose. Same-old/different-new,
   cycles, and incompatible schema/provenance conflict. A direct old-to-newer
   supersession may replace an older row only when it names that row and has a
   higher revision. Chains remain explicit and are never silently collapsed.
7. **Source authority.** Git and source files remain authoritative. Preview
   contains source spans, expected text, replacements, and deterministic patch
   text. It is reviewed before any write.
8. **Staleness.** Recompute graph and file digests immediately before apply.
   Mismatch rejects the entire operation with no file mutation.
9. **Limits.** v1 bounds patch rows, graph nodes/edges, files, edits, text bytes,
   path length, and dependency traversal. Unknown schema, duplicate keys in an
   eventual wire decoder, cycles, overlaps, and oversized inputs fail closed.

## Schema and relation evidence

`spec/tooling/upgrade-patch/schema.json` records term/type replacement,
deprecation, relation, provenance, dependencies, conflicts, and limits. The
executable model validates a stricter in-memory form and emits canonical JSON.

Relation precedence is ABI change, ownership change, effect change, exact type,
safe compatible type, then incompatible. “Safe compatible” means old parameter
values remain accepted contravariantly and new results remain usable
covariantly under an explicit subtype table, with identical ownership, effects,
and ABI. It is still review-only because it changes the public type contract.

## Frontier and progress

The model consumes a complete graph whose identity binds sorted nodes, edges,
semantic references, and source spans. Statuses are:

- `already-migrated`: references only the replacement identity;
- `covered`: exact-type direct reference with a safe bounded rewrite;
- `blocked`: direct reference lacking safe rewrite evidence or carrying an
  effect/ownership/ABI/incompatible relation;
- `escaped`: a transitive dependent of blocked or unresolved work;
- `unresolved`: the graph declares that the node's semantic facts are partial.

No node disappears from the denominator. Partial application recomputes the
frontier from a new graph identity and keeps every remaining blocked/escaped
node visible.

## Preview, atomicity, and undo

Preview is read-only and deterministic: edits sort by path then byte span,
include expected old text, and render a reviewable source patch. Paths are
repository-relative and normalized; overlapping edits fail.

The bounded apply model clones the workspace, rechecks graph and file digests,
applies every edit in memory, runs an injected validator, and only then returns
the new workspace plus a complete undo snapshot. Any stale digest, missing
text, overlap, or validation failure throws before the caller replaces its
workspace. `rollback` restores the exact snapshot. Production filesystem writes
remain deferred and would require separate authorization.

## Comparison with existing tools

Ordinary compiler errors identify a broken current program but do not record
the intended old-to-new relation or the remaining transitive frontier. LSP
rename can produce source edits for one nominal symbol but does not classify
type/effect/ownership/ABI changes or bind the edit to term content. This model's
concrete advantage is a complete, reviewable frontier plus stale-safe preview.
It does not replace diagnostics, LSP transport, textual diff, or Git review.

## Consumer decisions

| Capability | Decision |
| --- | --- |
| canonical schema and relation evidence | keep |
| read-only frontier and deterministic preview | keep |
| exact-type rename writer | defer until compiler identity and target-branch validation land |
| type/effect/ownership/ABI automatic migration | reject in v1; review only |
| arbitrary AST rewrite or executable patch | reject |
| Git/codebase replacement | reject |

## Follow-up slices

Follow-up implementation must remain split and ordered:

1. #885: schema validation plus compiler-produced identity/frontier adapter;
2. #884: read-only CLI/LSP preview with source spans and target-branch
   validation;
3. #883: separately authorized, opt-in atomic application for the exact-type
   rename subset, with durable undo and failure-injection tests.

The first two may proceed independently after #740's compiler identity adapter;
the third must not be bundled into a read-only tooling release.
