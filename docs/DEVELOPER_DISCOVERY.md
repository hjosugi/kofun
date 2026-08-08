# Developer discovery contract

Status: accepted design for the first tooling slice. No discovery query,
command, editor integration, REPL integration, or runtime reflection profile
is implemented.

Issue: [#635](https://github.com/kofun-lang/kofun/issues/635).

The words **must**, **must not**, **should**, and **may** are normative.

## Decision

Kofun selects compiler-backed discovery as the mandatory first path. A REPL,
editor, and command-line client ask one semantic service:

1. What static type did the compiler infer for this expression?
2. Which operations are callable at this exact source position?
3. Why is an expected operation unavailable?

This preserves the useful interactive part of Ruby's `Object#class` and
`Object#methods` experience without adopting a universal `Object` hierarchy,
runtime dispatch by string, or ambient reflection metadata.

Of #635's decision options, this selects option 3: tooling-backed discovery is
mandatory, while a runtime adapter is optional and remains separately gated.

All clients use the `kofun.discovery.request/v1` and
`kofun.discovery.result/v1` logical records below. Transport framing may wrap
these records, but it must not add semantic fields or reinterpret them.

The query needs current-file facts from either a live compiler analysis or a
validated, current
[`kofun.typed-sidecar/v1`](../spec/tooling/typed-sidecar.md) projection.
A validated KIF may supplement that analysis with imported declaration facts.
KIF alone cannot recover an expression type, local scope, ownership state, or
meaning of a byte position, and therefore cannot answer this query.

## Trust boundary

Every result contains `"authoritative": false`. A discovery result is
tooling presentation data. It is forbidden as:

- compiler name, type, trait, ownership, effect, or visibility authority;
- KIF input or a replacement for KIF validation;
- build, package, linker, runtime, or release success input; or
- compiler or semantic-cache success input.

A client may cache a result only as a UI response keyed by the complete
analysis key and canonical request bytes. Such a cache never upgrades the
result's authority. A query provider may consume a validated authoritative
KIF internally, but the discovery result remains non-authoritative.

The query never evaluates the expression. It cannot execute user code, macros,
build scripts, network requests, processes, clocks, random sources, or ambient
file reads.

## User surfaces

Command spelling and layout are presentation choices. A REPL may render:

```text
kofun> let languages = ["Ruby", "PHP", "Python"]
kofun> :type languages
List[Text]

kofun> :operations languages
std.list.length(read List[T]) -> Int
std.list.map(read List[T], T -> U) -> List[U]
std.list.filter(read List[T], read T -> Bool) -> List[T]
```

An editor may display the same rows after `languages.`. A CLI may query a file,
generation, and UTF-8 byte position. None performs a second name, extension,
trait, ownership, effect, or visibility lookup.

Kofun calls the set `operations`, not `methods`. A future complete set may
contain inherent members, imported extensions, trait operations, and
receiver-style views of ordinary functions. This name does not promise Ruby's
object model.

## V1 scalar types and limits

V1 has one fixed limit profile: `kofun.discovery/default-v1`.

| Type | Exact rule |
| --- | --- |
| `Bool` | JSON `true` or `false` |
| `U32` | JSON integer from 0 through 4,294,967,295 |
| `U53` | JSON integer from 0 through 9,007,199,254,740,991 |
| `Id` | exactly 64 lowercase hexadecimal ASCII characters |
| `Name` | NFC UTF-8, 1–256 bytes, no control character |
| `QualifiedName` | NFC UTF-8, 1–4,096 bytes, no control character |
| `Display` | NFC UTF-8, 0–4,096 bytes, no control character |
| `Signature` | NFC UTF-8, 1–16,384 bytes, no control character |
| `Spelling` | NFC UTF-8, 1–1,024 bytes, no control character |
| `Span` | object with exactly required `start: U32` and `end: U32`; `start <= end` |
| `Identity` | object with exactly required `kind` and `value: Id` |

`Identity.kind` is exactly one of the stable kinds already accepted by the
typed-sidecar contract:

```text
BindingId ExportBindingId FileId ImplementationId ImportBindingId
LawEvidenceId ModuleId NamespaceId PackageId ScopeId SymbolId TypeId
```

The request is at most 64 KiB and depth 16. The result is at most 4 MiB and
depth 64. Both byte caps measure the canonical JSON encoding defined below,
even when an in-process or RPC transport carries the logical records in
another encoding. A result contains at most 4,096 operations, 256 diagnostics,
64 omissions, 64 dependencies per fact, 64 generic requirements per
operation, and 64 effects/capabilities per operation. One query performs at
most 1,000,000 candidate/filter steps.

All counts and byte lengths are checked before allocation. An over-limit
request returns `invalid`. Limit exhaustion after useful current facts commit
returns `partial` with `truncated: true`; exhaustion before any useful fact
returns `unavailable`. A `complete` result always has `truncated: false`.

## Analysis key and interface-set digest

`AnalysisKey` is an object with exactly these required fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `file_id` | `Id` | compiler-issued `FileId` for the current file |
| `source_sha256` | `Id` | SHA-256 of the exact current source bytes |
| `interface_set_sha256` | `Id` | framed digest defined below |
| `generation` | `U53` | caller-owned monotonic document generation |
| `semantic_compatibility` | `Name` | producer semantic compatibility version |

Absolute paths, URIs, inodes, timestamps, process IDs, pointers, and editor
buffer names are not analysis-key inputs.

The compiler/workspace owner computes `interface_set_sha256`; a client never
constructs it from display names. It is SHA-256 over this exact #303-style
frame:

```text
"KOFUN\0"
|| domain_length:u16be
|| "kofun.discovery.interface-set/v1"
|| payload_length:u32be
|| payload
```

`domain_length` is 32, the byte length of the ASCII domain. `payload` is:

```text
member_count:u32be
|| repeated member_count times {
     PackageId:32 raw bytes
     ModuleId:32 raw bytes
     authorized_view:u8
     semantic_digest:32 raw bytes
   }
```

`authorized_view` is `1` for the public KIF view and `2` for the
package-internal KIF view actually visible to this query. Exactly one member
is emitted for each imported `(PackageId, ModuleId)`: use the internal view
when the query is authorized for it, otherwise use the public view. Emitting
both views for one module is invalid. Members are sorted by raw
`(PackageId, ModuleId, authorized_view, semantic_digest)` bytes. At most 4,096
members are permitted. Two members with the same `(PackageId, ModuleId)` are
invalid regardless of view or semantic digest. The empty set is encoded with
`member_count = 0`. No padding, length prefix inside a member, path, name,
declaration order, or unvalidated interface is included. `payload_length` is
exactly `4 + member_count * 97`.

A query provider records every validated authorized imported KIF view in the
candidate environment before lookup and filtering. This includes a view that
yields zero candidates for the requested spelling or type, so adding a symbol
or changing a negative lookup necessarily changes the digest. A provider must
not digest only the winning or nonempty candidate views. The current file's
source digest and generation remain separate fields; the interface-set digest
cannot replace them.

## Request schema

`DiscoveryRequestV1` is an object with exactly these required fields:

| Field | Type |
| --- | --- |
| `schema` | exact string `kofun.discovery.request/v1` |
| `analysis` | `AnalysisKey` |
| `position` | `QueryPosition` |
| `query` | `Query` |

`QueryPosition` is an object with exactly these required fields:

| Field | Type | Rule |
| --- | --- | --- |
| `byte_offset` | `U32` | code-point boundary at the point of use |
| `expression` | `Span` | current-file expression selected by compiler analysis |

Before examining the position, the provider compares the request `FileId`
byte-for-byte with the live analysis or current-file sidecar `FileId`.
Filesystem path equality is not a substitute. A mismatch returns `stale` with
reason `wrong-file` and no facts. Then
`expression.start <= byte_offset <= expression.end`, and all three offsets
must be UTF-8 code-point boundaries inside the exact source named by the
analysis key. A provider rejects a client-supplied expression span that does
not match its current parsed occurrence.

`Query` is an object with exactly these required fields:

| Field | Type | Rule |
| --- | --- | --- |
| `kind` | enum | `type`, `operations`, `type-and-operations`, or `explain-operation` |
| `include_unavailable` | `Bool` | includes only visible rejected candidates |
| `spelling` | `Spelling` or `null` | required non-null only for `explain-operation`; otherwise required null |

`include_unavailable` never weakens visibility. `spelling` is the spelling
already supplied by the user. Echoing it does not authorize disclosure of a
hidden declaration.

Example:

```json
{
  "analysis": {
    "file_id": "1111111111111111111111111111111111111111111111111111111111111111",
    "generation": 17,
    "interface_set_sha256": "3333333333333333333333333333333333333333333333333333333333333333",
    "semantic_compatibility": "stage2-v1",
    "source_sha256": "2222222222222222222222222222222222222222222222222222222222222222"
  },
  "position": {
    "byte_offset": 84,
    "expression": {
      "end": 84,
      "start": 75
    }
  },
  "query": {
    "include_unavailable": false,
    "kind": "type-and-operations",
    "spelling": null
  },
  "schema": "kofun.discovery.request/v1"
}
```

## Result schema

`DiscoveryResultV1` is an object with exactly these required fields:

| Field | Type |
| --- | --- |
| `schema` | exact string `kofun.discovery.result/v1` |
| `authoritative` | exact JSON boolean `false` |
| `limit_profile` | exact string `kofun.discovery/default-v1` |
| `status` | `complete`, `partial`, `stale`, `unavailable`, or `invalid` |
| `analysis` | `AnalysisKey` or `null` |
| `type` | `TypeFact` or `null` |
| `operations` | array of `OperationFact` |
| `omissions` | array of `Omission` |
| `diagnostics` | array of `DiagnosticSummary` |
| `reason` | `ResultReason` or `null` |
| `truncated` | `Bool` |

The status fixes the permitted shape:

| Status | Exact rule |
| --- | --- |
| `complete` | `analysis` echoes the request; `type` is validated; every operation is validated; `reason` is null; `truncated` is false |
| `partial` | `analysis` echoes a current request; at least one useful fact exists; a nonvalidated fact, diagnostic, omission, or truncation explains incompleteness |
| `stale` | `analysis` echoes the parseable request; `type` is null; operations and omissions are empty; `reason` is `wrong-file` or a `stale-*` reason; `truncated` is false |
| `unavailable` | `analysis` echoes a valid request; `type` is null; operations are empty; `reason` explains why no useful fact exists |
| `invalid` | `analysis` and `type` are null; operations and omissions are empty; `reason` explains request/schema failure |

`ResultReason` is exactly one of:

```text
cancelled-before-analysis
incomplete-current-file-facts
invalid-position
invalid-request
limit-exhausted
stale-generation
stale-interface-set
stale-semantic-compatibility
stale-source
unsupported-in-profile
wrong-file
```

A `complete` result for `type` has an empty operations array. A complete
`operations` or `type-and-operations` result includes the validated type used
for lookup. An `explain-operation` result includes at most one operation row;
if no visible row may be disclosed, it uses an omission.

## Dependency and fact-status closure

`FactStatus` is exactly `validated`, `provisional`, `error`, or `unavailable`.

`DependencyRef` is an object with exactly these required fields:

| Field | Type | Rule |
| --- | --- | --- |
| `source` | enum | `live`, `sidecar`, or `kif` |
| `kind` | enum | `NodeId` or an `Identity.kind` value |
| `value` | `Id` | compiler-issued key |
| `status` | `FactStatus` | status validated by the provider |

`sidecar` dependencies with `kind = NodeId` preserve the sidecar's status and
dependency closure. `kif` dependencies use a stable identity kind and may be
`validated` only after the exact authorized KIF view validates. A live
compiler dependency uses `NodeId` or a stable identity already committed by
that analysis. No display name or rendered diagnostic is a dependency key.

Dependency arrays are unique by `(source, kind, value)` and sorted by that raw
tuple. The required trust order is:

```text
validated < provisional < unavailable < error
```

A fact's status must be at least as restrictive as every direct and transitive
dependency. In particular:

- `validated` requires the complete transitive dependency closure to be
  validated;
- a provisional, unavailable, or error dependency can never satisfy a
  validated fact;
- an `error` fact has at least one diagnostic ID;
- an unavailable fact has a public reason and no fabricated value; and
- only a validated operation with `availability = callable` may be presented
  as callable.

A top-level `partial` result may contain an independently validated callable
row, but it must never upgrade a provisional, error, or unavailable row or
dependency. Clients preserve row status; they do not infer validation from the
presence of a signature.

## Type fact

`TypeFact` is an object with exactly these required fields:

| Field | Type |
| --- | --- |
| `status` | `FactStatus` |
| `identity` | `Identity` or `null` |
| `display` | `Display` or `null` |
| `dependencies` | array of `DependencyRef` |
| `diagnostic_ids` | array of `Id` |
| `reason` | `FactReason` or `null` |

For a validated type, `identity.kind` is `TypeId`, `display` is non-null,
`reason` is null, and every dependency is validated. A provisional type has no
identity, has a non-null reason, and may use a compiler-owned placeholder such
as `_T1`. Error and unavailable types have no identity and a non-null reason;
an error type uses `rejected-by-diagnostic` and has at least one diagnostic
ID, while an unavailable type also has no display. Every diagnostic ID must
name exactly one result diagnostic.

`FactReason` is exactly one of:

```text
cancelled-before-analysis
incomplete-analysis
limit-exhausted
rejected-by-diagnostic
type-not-available-in-current-subset
unsupported-current-stage2-feature
```

The service never invents `Any`.

## Operation fact

`OperationFact` is an object with exactly these required fields:

| Field | Type |
| --- | --- |
| `status` | `FactStatus` |
| `identity` | `Identity` |
| `display_name` | `Name` |
| `qualified_name` | `QualifiedName` |
| `signature` | `Signature` or `null` |
| `generic_requirements` | array of `GenericRequirement` |
| `receiver_mode` | `read`, `edit`, `take`, `none`, or `null` |
| `effects` | array of `EffectRequirement` |
| `origin` | `OperationOrigin` |
| `visibility` | `private`, `internal`, `pub`, or `restricted` |
| `availability` | `callable` or `unavailable` |
| `rejection_reasons` | array of `RejectionReason` |
| `source` | `Location` or `null` |
| `documentation` | canonical documentation link or `null` |
| `dependencies` | array of `DependencyRef` |
| `diagnostic_ids` | array of `Id` |

`identity.kind` is `SymbolId`. Only candidates whose name and identity are
safe to disclose become operation rows. Hidden candidates never become rows.

A canonical documentation link is exactly
`kofun-doc:` followed by one 64-lowercase-hex attachment ID. It is an identity
for a separate explicit documentation lookup, not embedded documentation and
not permission for network access. `source` and `documentation` are non-null
only when the provider validated that their disclosure is allowed.

A validated callable row has a non-null signature and receiver mode, an empty
rejection list, a fully validated dependency closure, validated origin, and
only validated generic requirements and effects. A definitive, visible
rejection may itself be a validated row with
`availability = unavailable` and at least one rejection reason. Every row
with `availability = unavailable`, regardless of fact status, has at least one
rejection reason. Any provisional, error, or unavailable row must also have
`availability = unavailable`; an error row additionally has at least one
diagnostic ID. A null signature or receiver mode means that fact was not
safely available; clients do not reconstruct it.

`GenericRequirement` is an object with exactly required `kind`, `identity`,
`display`, and `status`:

- `kind` is `trait-bound`, `equality`, or `inference`;
- `identity` is an `Identity` or null;
- `display` is `Display`; and
- `status` is `FactStatus`.

`EffectRequirement` is an object with exactly required `identity`, `display`,
and `status`. Its identity kind is `SymbolId` or `TypeId`. Its status follows
the same dependency rule; discovery never performs the effect.

Every dependency needed by a nested generic requirement, effect, origin,
source, or documentation attachment is included in its operation row's
`dependencies`. The operation row's status must be at least as restrictive as
every nested status; independently validated nested facts may remain
validated inside a provisional row.

`OperationOrigin` is an object with exactly these required fields:

| Field | Type | Rule |
| --- | --- | --- |
| `status` | `FactStatus` | closure status for the complete origin record |
| `kind` | enum | `function`, `member`, `extension`, or `trait` |
| `module` | `QualifiedName` | safe defining-module display |
| `module_identity` | `Identity` | kind must be `ModuleId` |
| `implementation_identity` | `Identity` or null | non-null, kind `ImplementationId`, only for an accepted extension/trait implementation |
| `trait` | `Name` or null | non-null only for trait origin |
| `trait_identity` | `Identity` or null | non-null only for trait origin |

For `function` and `member`, all implementation/trait fields are null. For
`extension`, `implementation_identity` is non-null and both trait fields are
null. For `trait`, both trait fields are non-null; a selected concrete
implementation has a non-null `ImplementationId`, while a generic
trait-bound candidate has null. `trait_identity.kind` is `SymbolId` or
`TypeId`. A callable row requires `origin.status = validated`; a nonvalidated
origin forces the row to a nonvalidated, unavailable state.

`RejectionReason` is exactly one of:

```text
ambiguous
incomplete-analysis
limit-exhausted
missing-effect
requires-edit
requires-read
requires-take
type-mismatch
unsatisfied-bound
unsupported-in-profile
```

Rejection reasons are a duplicate-free ASCII-lexicographically sorted set of
at most ten entries.

Generic requirements and effects are unique by identity when an identity is
present. They are sorted with non-null compiler identity `(kind, value)` first,
then null identities, then `kind`/display bytes. Operation rows are unique by
the tuple `(SymbolId, origin.implementation_identity-or-null)` and sorted first
by raw SymbolId, then the optional raw `ImplementationId`, then qualified-name
and signature bytes. Canonical result order is therefore compiler-key order.
A UI may alphabetize a copy for display; it must not rewrite canonical bytes
or use display order as identity.

## Omissions and diagnostics

`Omission` is an object with exactly required `reason` and
`requested_spelling`:

- `reason` is `hidden-by-visibility`, `not-imported`,
  `unsupported-in-profile`, `incomplete-analysis`, or `limit-exhausted`; and
- `requested_spelling` is a `Spelling` or null.

Only `explain-operation` may set `requested_spelling`, and it must exactly echo
the request. A general query uses null. An omission contains no hidden count,
identity, name, signature, origin, path, span, or documentation. Omission rows
are unique and sorted by `(reason, requested_spelling)`.

`DiagnosticSummary` is an object with exactly these required fields:

| Field | Type |
| --- | --- |
| `id` | `Id` |
| `code` | `Name` |
| `severity` | `error`, `warning`, `information`, or `hint` |
| `category` | `Name` |
| `template_id` | `Name` |
| `primary` | `Location` or `null` |
| `fallback_text` | `Display` |

`Location` has exactly required `file_id: Id` and `span: Span`. Locations are
limited to facts already safe for this query. Fallback text is presentation,
not identity or lookup input. Diagnostics are unique and sorted by raw
diagnostic ID first, then code. Every fact diagnostic ID must resolve to one
row; unreferenced rows are permitted only for a root request/status failure.
Every `diagnostic_ids` array is a duplicate-free set of at most 256 IDs sorted
by raw decoded ID bytes.

## Canonical JSON

When encoded as JSON, request and result canonical bytes are:

- UTF-8 without BOM, exactly one JSON object followed by one LF;
- no comments, trailing data, floating-point values, or exponent spelling;
- every object key present as required by its record and serialized in ASCII
  lexicographic order;
- unknown fields rejected;
- duplicate keys rejected before conversion to an object map;
- NFC strings with control characters rejected; `"` and `\` use only `\"`
  and `\\`, while all other permitted Unicode is emitted literally;
- integers serialized in base ten with no sign or leading zero except `0`;
- lowercase `true`, `false`, and `null`;
- empty arrays/objects emitted as `[]`/`{}`; nonempty arrays/objects use
  two-space indentation with one field/item per line, `,` followed by LF, the
  closing token at its parent's indentation, and exactly one ASCII space after
  `:`; and
- arrays ordered by the record-specific rules above.

Identity arrays, dependencies, operations, generic requirements, effects, and
diagnostics sort compiler-issued identity/key bytes before any display text.
Arrays described as sets reject duplicates. Source order, import order,
hash-table order, filesystem order, checkout location, and parallel scheduling
must not change canonical bytes.

An unknown schema major, enum, field, identity kind, status, reason, or limit
profile rejects. A provider may expose a separate best-effort debug dump, but
it is not a v1 discovery result.

## Lookup and safe-disclosure rules

The provider computes the result at the exact source position:

1. validate the request, source identity, current-file facts, and analysis key;
2. infer as much of the selected expression type as committed facts permit;
3. collect declarations admitted by current lexical/module/visibility rules;
4. supplement imports only from the authorized validated KIF views in the
   interface-set digest;
5. conditionally apply future accepted extension and trait/member-resolution
   rules;
6. apply generic constraints and inference;
7. apply receiver ownership requirements;
8. apply effects and capabilities; and
9. close fact status/dependencies, filter disclosure, bound work, and
   serialize in canonical identity order.

[#293](https://github.com/kofun-lang/kofun/issues/293) and
[#316](https://github.com/kofun-lang/kofun/issues/316) remain planning issues.
This document does not select their extension scope, coherence, or
method-resolution semantics. Until those rules are accepted and executable, a
provider reports the corresponding work as `unsupported-in-profile`; it must
not fabricate a complete extension or trait candidate set.

For a user-supplied explanation spelling, the service may report
`not-imported` or `hidden-by-visibility`. It cannot return the hidden
declaration's identity, defining path, signature, documentation, source
location, or even a hidden-candidate count.

## Required acceptance examples

These are fixture requirements for the future implementation, not claims
about the active compiler.

### Complete `List[Text]`

A complete `List[Text]` receiver returns visible `List` operations after
substituting `T = Text`. Full signatures retain canonical generic
requirements. Operations requiring `edit` or `take` are absent for a
read-only receiver unless unavailable candidates were requested.

### Incomplete generic value

For an unconstrained `_T1`, the result is `partial`. Only independently
validated operations justified by known bounds may be callable. The service
does not list every operation in the workspace.

### Imported extension

After #293 selects and implements an import/scope rule, an imported extension
fixture must appear with `origin.kind = extension`, its defining module,
implementation identity, and unchanged operation `SymbolId`. Removing the
import must remove it deterministically. Before then the result is
`unsupported-in-profile`.

### Hidden extension

An unimported or private cross-package extension does not leak its name or
identity. A general query may contain an aggregate
`hidden-by-visibility` omission. An explanation may echo only the spelling
already supplied by the user.

### Unsatisfied trait bound

After #316 selects and implements trait candidate resolution, a visible
operation rejected by a public bound may be returned as a validated
unavailable row with `unsatisfied-bound` and the canonical public
requirement. Before then it is `unsupported-in-profile`.

### `read`, `edit`, and `take`

If `sort_in_place` requires `edit List[T]`, a `read List[T]` receiver omits it
by default and explains `requires-edit` on request. An operation requiring
`take List[T]` is filtered independently. Satisfying `edit` does not imply
`take`; the compiler's ownership result remains authoritative.

### Effects and capabilities

An operation requiring network authority is unavailable when the source
position lacks that capability. An explanation reports `missing-effect` and
only a public validated requirement. Discovery never performs the effect.

## Staleness, privacy, and current implementation boundary

- A FileId, source digest, interface-set digest, generation, or semantic
  compatibility mismatch returns `stale`; no old fact is relabelled as
  current.
- Invalid or noncanonical sidecars are rejected by their codec. Discovery
  cannot upgrade a non-authoritative sidecar into compiler authority.
- Identity-only visibility and disclosure remain authoritative. Private
  bodies, out-of-scope locals, hidden names/IDs/paths/spans, and
  compiler-internal AST/HIR representations are not result fields.
- Documentation lookup is a separate explicit client action and is not part
  of v1.

The executable #608 event producer is a bounded input, not a discovery
implementation. Its current profile intentionally omits imports, generics,
traits, extensions, macros, and general effects. Its internal KSE stream is
not a public client transport. A projector or query provider must preserve
those unavailable boundaries instead of fabricating a complete operation set.

## Runtime metadata and release cost

The mandatory tooling path reads live analysis plus validated current-file
facts and, when needed, validated KIF imports. With discovery disabled, a
normal release artifact must be byte-identical to the same build without
discovery support. The implementation gate must compare those bytes.

Runtime reflection remains blocked on
[#454](https://github.com/kofun-lang/kofun/issues/454). If that issue later accepts
runtime `TypeInfo`:

- retention is selected by an explicit build profile;
- the profile and metadata schema are versioned;
- only public stable facts are exposed by default;
- stripped facts return `unavailable`, not fabricated empty lists;
- dynamic invocation by string and mutation remain separate decisions; and
- disabling retention still produces the byte-identical normal artifact.

No runtime adapter is authorized by this document.

## Issue ownership and reverse links

- [#194](https://github.com/kofun-lang/kofun/issues/194) owns REPL presentation,
  not a second semantic lookup.
- [#293](https://github.com/kofun-lang/kofun/issues/293) owns the future extension
  scope decision.
- [#316](https://github.com/kofun-lang/kofun/issues/316) owns the future
  method/trait candidate-resolution decision.
- [#454](https://github.com/kofun-lang/kofun/issues/454) owns optional runtime
  metadata.
- [#605](https://github.com/kofun-lang/kofun/issues/605) and
  [#608](https://github.com/kofun-lang/kofun/issues/608) own semantic-fact
  production.
- [#600](https://github.com/kofun-lang/kofun/issues/600),
  [#601](https://github.com/kofun-lang/kofun/issues/601), and
  [#604](https://github.com/kofun-lang/kofun/issues/604) own KIF and sidecar
  validity/storage boundaries.

After this document lands on `main`, #194, #293, #316, #454, #605, and #608
must link back to its stable repository URL. #635 is not complete until those
reverse links exist; issue descriptions must not define incompatible
discovery schemas.

The first implementation child must add deterministic complete, partial,
stale, invalid, hidden, ownership, effect, canonical-order, limit, and
release-cost fixtures. Until those executable gates exist, this document is
design evidence only.
