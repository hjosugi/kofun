# Stable identities and semantic interface digests

Status: accepted normative design for GitHub issue #303.

This document fixes production hashing, canonical interface bytes, semantic
invalidation, target ABI invalidation, compatibility, corruption handling, and
resource limits. It consumes the package, file/module, namespace, and
visibility contracts from #284, #300, #290, and #285 respectively.

The words **must**, **must not**, **should**, and **may** are normative.

The focused Stage 2 declaration collector now executes the production
`NamespaceId` and `SymbolId` framing for bounded functions, ADTs, and
constructors. KIF interface emission and the three semantic/ABI digest views
remain specification-only at this checkpoint.

## Decisions

Kofun makes the following v1 choices:

| Question | Decision |
| --- | --- |
| Compiler-authoritative encoding | versioned length-prefixed binary (`KIF`); JSON is dump-only |
| Hash | SHA-256 with the framed, versioned domain separators below |
| Body inputs | only values/typed bodies explicitly defined as semantic compile-time inputs |
| Nominal layout | opaque unless an explicit representation or foreign-ABI contract exposes it |
| Implementation/law identity | canonical trait, type, binder, constraint, and evidence identities |
| Version policy | compatible optional minor fields may be skipped; unknown required/major data requires rebuild |
| Limits | validate a 16 MiB envelope and bounded counts/depth before allocation |

One digest is not reused for different jobs. A target-neutral public semantic
digest, a target-neutral package-internal semantic digest, and a target ABI
digest are three distinct values with distinct domains and inputs.

## Hash construction

All v1 IDs and digests are 32 raw bytes. Human-readable forms are exactly 64
lowercase hexadecimal digits. The production hash is SHA-256 over this framed
preimage:

~~~text
"KOFUN\0"                         6 bytes
domain-length                     unsigned 16-bit big endian
domain                            domain-length ASCII bytes
payload-length                    unsigned 32-bit big endian
payload                           exact canonical bytes
~~~

The payload limit below ensures the 32-bit length is sufficient. Concatenating
an unframed domain and payload is forbidden. A stored or transmitted digest is
always recomputed before it becomes authoritative.

The v1 domains are:

| Identity/value | Domain |
| --- | --- |
| PackageId | `kofun.id.package/v1` |
| FileId | `kofun.id.file/v1` |
| ModuleId | `kofun.id.module/v1` |
| NamespaceId | `kofun.id.namespace/v1` |
| SymbolId | `kofun.id.symbol/v1` |
| TypeId | `kofun.id.type/v1` |
| ImportBindingId | `kofun.id.import-binding/v1` |
| ExportBindingId | `kofun.id.export-binding/v1` |
| ImplementationId | `kofun.id.implementation/v1` |
| LawEvidenceId | `kofun.id.law-evidence/v1` |
| public semantic digest | `kofun.digest.public-semantic/v1` |
| package-internal semantic digest | `kofun.digest.package-internal/v1` |
| target ABI digest | `kofun.digest.target-abi/v1` |

Domains are protocol constants. They are not user-provided text and cannot be
aliased. Changing field meaning or canonical encoding requires a new domain.

## Identity hierarchy

`PackageId` hashes the exact canonical `PackageIdPayload` from
[`package-roots.md`](package-roots.md). `FileId` and `ModuleId` hash their exact
canonical input payloads from
[`source-file-mapping.md`](source-file-mapping.md). `NamespaceId` hashes the
tag/name payload from [`namespaces.md`](namespaces.md). Those earlier payloads
become production inputs under the hash construction above; their example
fingerprints remain independent regression checks.

The remaining identity payloads are canonical KIF records:

- `SymbolId` contains the raw `ModuleId`, raw `NamespaceId`, stable
  declaration-kind tag, and NFC declared name. The kind distinguishes, for
  example, a value function from a value constructor. FileId, source span,
  visibility, signature, body, and source order are excluded.
- `TypeId` for a nominal declaration contains its `SymbolId`, generic binder
  count, and ordered binder `SymbolId`s. A constructed type is represented by
  a canonical `TypeRef` containing this `TypeId` and canonical arguments; it
  does not mint a source-order ID.
- `ImportBindingId` contains the importing `ModuleId` and `FileId`, local
  namespace/name, target identity, and import-form tag. It is a local binding,
  never a replacement for the target `SymbolId`.
- `ExportBindingId` contains exporting `ModuleId`, exported namespace/name,
  target `SymbolId`, and effective visibility. A re-export therefore changes
  the exporting interface without changing the target.
- `ImplementationId` contains the implementing `PackageId`, trait `SymbolId`
  or intrinsic-trait tag, canonical self `TypeRef`, ordered generic binder
  identities, canonical constraints, and coherence-mode tag. Source location
  and discovery order are excluded.
- `LawEvidenceId` contains the law declaration `SymbolId`, subject
  `ImplementationId`, evidence-contract version, canonical quantified type
  arguments, and the semantic evidence digest. Evaluator trace, timing, and
  diagnostic presentation are excluded.

Declaration-kind, import-form, visibility, coherence, type, constraint,
effect, ownership, and evidence tags are stable unsigned integers owned by
their versioned schemas. Implementations must not hash debug enum names or
memory representations. A schema cannot encode pointers, process-local table
indices, target addresses, timestamps, inodes, absolute paths, or iteration
order.

## Canonical KIF binary

The compiler-authoritative compiled-interface envelope is:

~~~text
4b 49 46 00              magic "KIF\0"
00 01                    unsigned 16-bit major version 1
00 00                    unsigned 16-bit minor version 0
pp pp pp pp              unsigned 32-bit payload byte length
payload                  ordered fields
~~~

Every field is `tag:u16be`, `length:u32be`, then exactly `length` bytes. Tags
must be strictly increasing; duplicate or out-of-order tags are invalid. A tag
with bit `0x8000` set is required. An unknown required tag rejects the artifact
with a rebuild instruction. An unknown optional tag may be skipped only when
the major version is supported and its declared bytes fit inside the envelope.

V1 requires these fields:

| Tag | Value |
| ---: | --- |
| `0x8001` | UTF-8 text `kofun.interface/v1` |
| `0x8002` | normalized language edition |
| `0x8003` | semantic compatibility version |
| `0x8004` | raw 32-byte PackageId |
| `0x8005` | raw 32-byte ModuleId |
| `0x8006` | public semantic fact vector |
| `0x8007` | package-internal-only semantic fact vector |
| `0x8008` | claimed raw public semantic digest |
| `0x8009` | claimed raw package-internal semantic digest |

A vector is `count:u32be` followed by `count` length-prefixed records. Record
fields use the same strictly increasing TLV rule. Sets/maps are sorted by raw
stable identity bytes and then canonical record bytes. Lists are ordered only
when language semantics make order observable; otherwise they are encoded as
sets. Text is valid UTF-8 already in NFC. Booleans and enums use one-byte
numeric tags. Integers use the smallest schema-mandated fixed-width big-endian
form. Floats use their language-level canonical bit contract, including the
accepted NaN normalization. No host endianness, locale, map order, padding, or
JSON number/string convention may affect the bytes.

The claimed digests are not inputs to themselves. A reader reconstructs the
views below, recomputes both values, and rejects either mismatch before
publishing symbols or cache success. The target ABI digest belongs to a target
artifact/action record rather than this target-neutral KIF envelope.

Canonical JSON may be emitted for diagnostics, review, and test diffs. It must
label itself `authoritative: false`, render IDs as lowercase hex, and use
stable key ordering. A compiler or cache must never accept JSON as an
authoritative interface or calculate production digests from it.

## Semantic fact coverage

Each declaration fact includes its namespace, SymbolId, declaration kind,
normalized exported name, effective visibility, canonical signature/type,
generic binders and constraints, trait/implementation identities, effect row,
ownership modes, and semantic constant/default values required to type-check a
consumer. Export facts include `ExportBindingId`, exported name and namespace,
target identity, effective visibility, and canonical re-export chain.

The public vector contains only facts observable by an external package. The
internal-only vector contains additional `internal` facts required by other
modules of the same PackageId. `private` and restricted file/module-local facts
are absent unless they are represented indirectly inside an explicitly exposed
semantic value. Source paths/spans may live in a separate typed sidecar but are
not authoritative interface facts.

Public nominal records expose field names/types and other source-level facts
needed for type checking when their language visibility allows use. They do
not expose byte offsets, padding, alignment, discriminants, niche choices, or
backend layout by default. An explicit stable representation/foreign-ABI
contract adds its promised representation facts to the semantic interface and
to the relevant target ABI view.

Ordinary function bodies, optimizer decisions, inlining choices, local
inference variables, diagnostics, comments, formatting, and evaluator traces
are excluded. A `const` value, default expression, normalized typed body, or
law result is included only when its owning language contract explicitly makes
that value/body available to consumer type checking or compile-time
evaluation. A future semantic-inline feature must use a new required field or
schema domain; ordinary optimization cannot silently make bodies semantic.

## Three digest views

All views use canonical KIF records and the hash construction above.

### Public semantic digest

The public payload contains interface schema, language edition, semantic
compatibility version, PackageId, ModuleId, and the public fact vector. It is
target neutral. External packages key semantic dependency edges by this value.

### Package-internal semantic digest

The internal payload contains the same header and complete public vector plus
the internal-only vector. Other modules with the same PackageId key semantic
dependency edges by this value. It is not the hash of the internal-only vector
and cannot omit public changes.

### Target ABI digest

The ABI payload contains ABI schema, normalized target triple, ABI-affecting
profile features, calling-convention version, runtime ABI version, linker
names, and canonical target layouts/representations for every public or
internal fact that crosses the selected target's link boundary. It includes
edition/schema identity needed to interpret those facts but excludes source
paths and target-neutral private bodies.

The ABI digest is not `hash(public-digest || internal-digest || target)`. It is
derived from the narrower target ABI fact view so a target-neutral semantic
change that cannot affect linking/layout need not invalidate target artifacts.
Conversely, a target/layout change invalidates it without renaming any
PackageId, ModuleId, SymbolId, TypeId, or semantic digest.

## Required invalidation matrix

| Change | Public | Internal | Target ABI |
| --- | --- | --- | --- |
| private body only | unchanged | unchanged | unchanged |
| private declaration rename | unchanged | unchanged | unchanged |
| internal signature used across package modules | unchanged | changed | changed for affected target |
| public signature/type/effect/ownership | changed | changed | changed when ABI-facing |
| public re-export path/name | changed | changed | changed when linker/interface surface changes |
| comment, formatting, source reorder, absolute path remap | unchanged | unchanged | unchanged |
| target triple/layout only | unchanged | unchanged | changed |
| compiler compatible patch only | unchanged | unchanged | unchanged |
| incompatible interface schema or language edition | mismatch/new value | mismatch/new value | mismatch/new value |

The reference gate has one golden transition for every row. An implementation
may rebuild more local optimization work, but it must not publish a changed
semantic digest for excluded data or reuse an artifact whose required digest
changed.

## Consumers and transaction boundary

#575's compiled interface is the canonical KIF envelope. Its typed sidecar may
add source spans, logical paths, docs, and local inference data, but sidecar
bytes and digest claims are non-authoritative. Losing a sidecar affects tooling
quality, not semantic correctness.

#301 records an edge to the exact view consumed: external semantic edges use
the public digest, same-package semantic edges use the internal digest, and
target/link edges use the ABI digest. A consumer records referenced identities
as a precision aid, but cannot substitute source mtimes or path hashes for a
required view digest.

The compiler validates the whole envelope, canonical order, identity graph,
visibility closure, digest claims, and limits before exposing a module table,
object, executable, typed sidecar, cache hit, or dependency-graph success.
Failure leaves no partially reusable authoritative interface.

## Corruption, collision, and limits

Malformed magic, truncated lengths, trailing bytes, duplicate/out-of-order
fields, invalid UTF-8/NFC, unknown required tags, unsupported major versions,
wrong fixed widths, impossible counts, dangling identities, visibility leaks,
and digest mismatches are bounded fatal interface diagnostics with a rebuild
remedy. They are never repaired from source order or textual names.

If two different canonical preimages produce one digest during an operation
or when admitting a cache/interface entry, compilation fails with a collision
diagnostic. The implementation compares bounded canonical bytes whenever one
digest is associated with competing content. It must not choose by path,
arrival order, or a secondary unversioned hash. Tests may inject a fake hash to
exercise this path; production remains SHA-256.

V1 limits, checked before allocation and with overflow-safe arithmetic, are:

| Resource | Limit |
| --- | ---: |
| complete KIF envelope | 16 MiB |
| fields in one record | 256 |
| declarations plus export bindings per module | 65,536 |
| implementations or law-evidence records per module | 65,536 |
| generic binders on one declaration | 256 |
| canonical type/constraint nesting depth | 128 |
| canonical re-export depth | 64 |
| one text/bytes field | 1 MiB, subject to smaller language limits |

Compressed authoritative KIF is unsupported in v1, so limits apply to the
exact bytes being parsed. Counts, lengths, and nesting budgets are validated
against remaining envelope bytes before allocating collections or descending.

## Versioning and compatibility

Major version, a changed domain, changed tag meaning, changed normalization,
or an unknown required field is rebuild-required. A compatible minor version
may add optional presentation/optimization metadata only; old readers skip it
by length, and it cannot affect semantic or ABI meaning. Any new semantic input
is required and therefore needs a reader that understands it, normally with a
new digest domain.

A compiler patch that preserves all schemas, edition semantics, canonical
facts, runtime ABI, and target layout versions does not change these IDs or
digests. Migration tooling may read an explicitly supported old major only to
rebuild from source; it cannot relabel old bytes as current. Cache misses and
rebuild instructions are safe compatibility behavior.

## Implementation status and non-goals

`spec/module-identity/check.sh` exercises framed production hashes, binary KIF
ordering, path/source-order stability, all invalidation rows, structural
corruption, claimed-digest rejection, collision handling, and resource limits.
It is reference decision evidence. The active compiler does not yet emit a
general compiled interface or semantic module graph.

This contract does not define import syntax, dependency solving, cache
eviction, optimizer invalidation below semantic interfaces, linker format,
concrete target layout algorithms, or a public stable ABI annotation syntax.
No identity, encoding, digest, body/layout, evidence, version, or limit question
remains open after this decision.
