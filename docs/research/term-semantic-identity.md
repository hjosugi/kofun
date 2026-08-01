# Term-level semantic identity research

Status: decision and bounded executable model for issue #740. This is not a
production compiler cache and it does not change the identities accepted by
#301 or #303.

## Decision

Keep a versioned, purpose-limited identity of validated typed Core for two
consumers: content-keyed test/evaluation results and structural semantic diffs.
Defer a production incremental body cache until the compiler exposes the same
canonical Core and its complete semantic dependency set. Reject content-
addressed source storage, package identity, executable lookup, and authority
decisions.

The identity means “the same bounded normalized representation under the same
declared semantic inputs.” It is not a proof of observational equivalence and
must never replace source, Git, `SymbolId`, interface digests, target ABI
digests, artifact hashes, or law evidence.

Unison demonstrates name-independent term hashing, recursive component
hashing, and content-keyed test results in its pinned hashing implementation:
<https://github.com/unisonweb/unison/tree/e1870038739ffcb27b4e3b483dafd2c21f6541b2/unison-hashing-v2>.
Its content-addressed codebase is not adopted. Kofun keeps ordinary files and
Git authoritative and retains only the bounded consumer value.

## Research questions and answers

1. **Representation.** Hash validated typed Core after name resolution and
   ownership/effect elaboration, before target lowering. Parsed or resolved AST
   is too syntactic; target IR makes identity target-specific.
2. **Invariants.** Ignore formatting, comments, source spans, import aliases,
   inferred temporary numbers, local binder names, source order of independent
   declarations, and source order inside a recursive component. Do not ignore
   public nominal identity when mapping a result back to source.
3. **Semantic inputs.** Include the normalized body, types, effects, ownership
   modes, semantic dependency identities, macro-expanded Core, language
   edition, compiler semantic version, numeric policy, and Unicode version.
4. **Recursion.** Compute strongly connected components. For a component of at
   most six definitions, encode every member permutation with local recursive
   references replaced by permutation indexes and select the lexicographically
   smallest bytes. Equal minima mark a symmetric component ambiguous. It still
   has a stable component identity, but no member identity may escape it.
5. **Scope.** The term identity is content-scoped. A cache key wraps it in a
   separate domain with trust/configuration/target/test inputs. `SymbolId`
   separately provides nominal source identity.
6. **Consumers.** Keep bounded test/evaluation cache keys and structural diffs;
   defer compiler incremental caching; reject package addressing, executable
   retrieval, runtime reflection, and authority checks.
7. **Failure behavior.** Reject unknown versions, unknown semantic fields,
   duplicate keys in an eventual wire decoder, corrupt digests, oversized
   input, excessive depth/node/SCC size, ambiguous cross-component references,
   and cache-key mismatch. If one identity is observed with different canonical
   bytes, quarantine both as a collision; never select either object. A miss
   causes recomputation and never turns stored data into code.
8. **Purpose separation.** `SymbolId` names a declaration; public/internal
   digests invalidate dependents; target ABI digests describe a target
   contract; artifact hashes bind bytes; law-evidence IDs bind claims; this
   identity names normalized term content only.
9. **User value with files and Git.** Reuse exact test results safely and show
   “local rename only” versus a typed/effect/ownership change while retaining
   source spans for review. No new source database is needed.

## Canonical model

`spec/semantic-identity/model.mjs` accepts a deliberately small structured Core:
literals, variables, binary operations, conditionals, lets, internal calls, and
external semantic references. Parameters carry type and ownership mode;
definitions carry result type and an ordered effect set. The model:

- alpha-normalizes local variables to de Bruijn indexes;
- orders effects and independent component hashes canonically;
- canonicalizes recursive SCCs under explicit limits;
- hashes with SHA-256 and distinct `component`, `member`, `program`, `cache-key`,
  and `observation` domain prefixes;
- preserves nominal symbols and source spans only in the presentation map;
- refuses ambiguous member export rather than selecting an accidental name or
  source order.

The primary and independent encoders use separate implementations and must emit
identical canonical bytes and hashes for all vectors.

Limits in the executable v1 model are 1 MiB input, 10,000 Core nodes, depth 128,
1,024 definitions, and six members per recursive SCC. A production revision may
lower these values but must change the schema/domain if normalization changes.

## Cache and corruption boundary

A cache key binds term identity, semantic dependency identities, test identity,
compiler semantic version, edition, target semantics, configuration digest, and
fixture/environment digest. A stored entry contains only a canonical
observation: stdout, stderr, exit category, and ownership event. It cannot name
a command, executable, token, capability, or filesystem path.

Unknown schema, key mismatch, digest corruption, or an authority-shaped field
fails closed. Staleness is a cache miss, not a partial hit. Cache content never
authorizes execution; the current compiler and caller decide whether to run.

## Structural diff boundary

The prototype joins definitions by nominal symbol and compares their bounded
term identities. An unchanged identity with a display-name or span change is a
source-only/rename presentation change. A changed identity reports normalized
type/effect/ownership/body categories and attaches the new source span. It does
not synthesize source edits or assert behavioral equivalence.

## Measurements

`check.mjs` runs a representative 256-definition test corpus twice: the cold
path canonicalizes and hashes each term; the hit path validates and reads the
same number of cache observations. It writes machine-local JSON under `build/`
with elapsed nanoseconds, canonical byte counts, hit count, and speed ratio.
The measurement is evidence of model overhead only, not a Kofun compiler speed
claim. The gate requires all 256 hits and reports the observed values each run.

## Consumer decision matrix

| Consumer | Decision | Reason |
| --- | --- | --- |
| test/evaluation cache | keep, bounded | exact inputs and observations can be validated without granting authority |
| structural semantic diff | keep, read-only | adds rename-insensitive classification while source spans remain reviewable |
| compiler incremental body cache | defer | production Core and complete dependency capture are not yet one canonical API |
| provenance display | defer | nominal `SymbolId` and artifact evidence are clearer today |
| package/source content store | reject | files and Git remain authoritative; no product need justifies a new codebase model |
| executable retrieval or authority | reject | a content hash is neither trusted code nor permission |

## Follow-up boundary

If the bounded model remains useful, follow-up work may separately propose:

1. a compiler-produced typed-Core projection with differential encoder vectors;
2. a read-only test-cache adapter with measured invalidation behavior;
3. a structural-diff UI over existing `SymbolId` and source maps.

No production cache/store child should start until item 1 proves that all
semantic inputs are present.
