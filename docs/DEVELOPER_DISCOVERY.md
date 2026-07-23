# Developer discovery contract

Status: accepted design for the first tooling slice.

Issue: [#635](https://github.com/hjosugi/kofun/issues/635).

## Outcome

Kofun tools must answer two questions at a source position:

1. What static type did the compiler infer for this expression?
2. Which operations are callable here, and why is an expected operation not
   callable?

This is the useful part of Ruby's `Object#class`, `Object#methods`, and Pry
experience, adapted to a statically typed language with free functions, traits,
extension methods, ownership modes, and effects.

The mandatory path is compiler-backed. A REPL, editor, and command-line client
consume the same versioned semantic query. They do not reconstruct lookup from
display text, evaluate the expression, or require reflection metadata in the
program being inspected.

Runtime reflection is optional. If #454 later permits a retained `TypeInfo`
profile, it must adapt to the identities and public facts defined here. It must
not turn this contract into dynamic invocation by string or require a universal
`Object` base class.

## User surfaces

Surface syntax is presentation, not semantics. A REPL may render:

```text
kofun> let languages = ["Ruby", "PHP", "Python"]
kofun> :type languages
List[Text]

kofun> :operations languages
std.list.length(read List[T]) -> Int
std.list.map(read List[T], fn(T) -> U) -> List[U]
std.list.filter(read List[T], fn(read T) -> Bool) -> List[T]
```

An editor may show the same operations after `languages.`. A CLI may query a
file, byte position, and expression span. All three clients must display facts
from the same result schema.

Kofun calls the set `operations`, not `methods`. The set may contain inherent
members, imported extensions, trait operations, and receiver-style views of
ordinary functions. The name avoids promising Ruby's object model.

## Normative query

The transport may be JSON, an in-process value, or an RPC encoding. Its logical
request is:

```json
{
  "schema": "kofun.discovery.request/v1",
  "analysis": {
    "file_id": "package://app/src/main.kofun",
    "source_digest": "sha256:...",
    "interface_digest": "sha256:..."
  },
  "position": {
    "byte_offset": 84,
    "expression_start": 75,
    "expression_end": 84
  },
  "query": {
    "kind": "type-and-operations",
    "include_unavailable": false
  }
}
```

Required fields:

- `schema` selects this contract exactly. Unknown major versions fail.
- `file_id` is a stable package-relative identity, not an ambient absolute
  filesystem path.
- `source_digest` binds the query to the exact source bytes.
- `interface_digest` binds imported public facts to the compiled-interface
  graph.
- offsets are zero-based UTF-8 byte offsets and must fall on code-point
  boundaries.
- `kind` is `type`, `operations`, `type-and-operations`, or
  `explain-operation`.
- `include_unavailable` asks for visible candidates rejected by types,
  ownership, effects, or bounds. It never weakens visibility.

`explain-operation` additionally carries the written or qualified operation
name. A tool should use it when a user asks why a particular candidate is
missing instead of dumping every rejected candidate.

## Normative result

The logical response is:

```json
{
  "schema": "kofun.discovery.result/v1",
  "status": "complete",
  "analysis": {
    "file_id": "package://app/src/main.kofun",
    "source_digest": "sha256:...",
    "interface_digest": "sha256:..."
  },
  "type": {
    "identity": "kofun:type:std.list/List@1",
    "display": "List[Text]",
    "completeness": "complete"
  },
  "operations": [
    {
      "identity": "kofun:symbol:std.list/map@1",
      "display_name": "map",
      "qualified_name": "std.list.map",
      "signature": "fn[T, U](read List[T], fn(T) -> U) -> List[U]",
      "receiver_mode": "read",
      "effects": [],
      "origin": {
        "module": "std.list",
        "kind": "function"
      },
      "visibility": "public",
      "availability": "callable",
      "documentation": "kofun-doc://std.list/map"
    }
  ],
  "omissions": [],
  "diagnostics": []
}
```

### Stable facts

- `identity` is the semantic identity emitted by the compiler/interface
  contract. Clients must not use the display spelling as a cache key.
- `display` and `signature` are canonical Kofun source renderings for the
  active edition.
- `origin.kind` is `member`, `extension`, `trait`, or `function`.
- `receiver_mode` is `read`, `edit`, `take`, or `none`.
- `effects` is the canonical, sorted required effect/capability set.
- `visibility` is the visibility observable to the query's source position.
- `documentation` is optional and must not be dereferenced to answer the
  semantic query.

Results are sorted by `qualified_name`, then canonical signature, then stable
identity. Source order, import order, hash-map order, filesystem order, and
parallel scheduling must not change the bytes of a canonical result.

### Completeness

`status` is:

- `complete`: the type and lookup inputs are complete and current;
- `partial`: useful facts exist, but one or more named inputs are incomplete;
- `stale`: a source or interface digest does not match;
- `unavailable`: the selected compiler profile cannot produce the query;
- `invalid`: the request is malformed.

A partial type uses a compiler-owned placeholder such as `_T1` and reports the
reason in `diagnostics`. It must not invent `Any`, silently erase unsatisfied
bounds, or present a rejected operation as callable.

## Operation-set rules

The compiler computes the callable set at the exact source position, in this
order:

1. infer as much of the receiver/expression type as current facts permit;
2. collect declarations visible through lexical and module rules;
3. collect extension candidates admitted by #293's accepted import/scope rule;
4. collect trait candidates admitted by #316's accepted coherence and
   resolution rule;
5. apply generic constraints and inference;
6. apply receiver ownership requirements;
7. apply effects and capabilities;
8. sort and serialize the accepted set deterministically.

This sequence is a query model, not permission for one compiler phase to bypass
another phase's stable semantic identities.

With `include_unavailable: true`, the result may include visible rejected
candidates with one or more of:

- `type-mismatch`;
- `unsatisfied-bound`;
- `requires-edit`;
- `requires-take`;
- `missing-effect`;
- `ambiguous`;
- `unsupported-in-profile`.

A declaration hidden by package/module visibility is not returned by name.
The response may contain the aggregate omission `hidden-by-visibility` so a
user understands that the result is intentionally filtered without learning a
private API from a compiled dependency.

## Required examples

### Complete concrete type

`List[Text]` returns `List` operations after substituting `T = Text`.
Operations requiring `edit` or `take` are absent for a read-only receiver unless
unavailable results were requested.

### Incomplete generic type

For an unconstrained `_T1`, the result is `partial`. Only operations justified
by known bounds are callable. The tool does not list every operation in the
workspace.

### Imported extension

An extension imported under #293 appears with `origin.kind = extension` and its
defining module. Removing the import removes the operation deterministically.

### Hidden extension

An unimported or private cross-package extension does not leak its name. The
result may report the aggregate omission `hidden-by-visibility`.

### Unsatisfied trait bound

An explicitly requested operation may be returned as unavailable with
`unsatisfied-bound` and the canonical required bound. Candidate ordering must
not change the diagnostic.

### Ownership and effects

If `sort_in_place` requires `edit List[T]`, a read-only receiver omits it by
default and explains `requires-edit` on request. A network operation requiring
`IO`/network authority behaves the same way with `missing-effect`; discovery
never performs the effect.

## Stale data, privacy, and side effects

- A source/interface digest mismatch returns `stale`; clients must not relabel
  old facts as current.
- Partial sidecars retain the disclosure guards defined by #601/#606. This
  contract cannot expand the facts a sidecar is allowed to expose.
- Queries are read-only. They cannot execute user code, macros, build scripts,
  network requests, processes, clocks, random sources, or arbitrary file reads.
- Documentation lookup is a separate explicit action.
- Private bodies, local values outside the queried scope, and compiler-internal
  AST/HIR representations are not part of the result.
- Implementations must bound candidate count, response bytes, and explanation
  work and return a diagnostic on limit exhaustion.

## Runtime metadata boundary

Normal release artifacts retain no additional metadata for this feature. The
tooling path reads current compiler state, KIF, or typed sidecars.

If #454 later accepts runtime `TypeInfo`:

- retention is selected by an explicit build profile;
- the profile and metadata schema are versioned;
- only public, stable facts are exposed by default;
- stripped facts return `unavailable`, not fabricated empty lists;
- string-based invocation and mutation remain separate language decisions;
- disabling retention must leave a bounded release artifact byte-identical.

## Dependencies and implementation split

- #293 owns extension scope and identity.
- #316 owns method/trait candidate resolution.
- #600/#601/#604 own KIF and sidecar wire/storage validity.
- #605/#608 own production of complete and partial semantic events.
- #194 is a presentation consumer, not a second lookup implementation.
- #454 owns optional runtime metadata.

The first implementation child should expose the query over semantic events or
typed sidecars and add deterministic fixtures. REPL rendering follows after
that query is executable.
