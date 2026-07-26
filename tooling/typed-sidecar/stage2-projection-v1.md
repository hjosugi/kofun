# Stage 2 semantic-event projection v1

`from-stage2.mjs` is the one-way bridge from the internal
`kofun-stage2-semantic-events/v1` transaction to the non-authoritative
`kofun.typed-sidecar/v1` document. It does not derive a fact from source text,
read a sidecar into the compiler, or publish KIF/cache authority.

The KSE input boundary remains exactly 4,096 events, 4 MiB of framed payload,
4,096 bytes per text/nested field, and 64 relations per record. The projector
does not widen those bounds to the larger typed-sidecar storage profile.

## Exact field mapping

Every current KSE v1 field is either mapped by this table or makes the whole
projection fail. Transport headers, field tags/wires, counts, lengths, and the
KSE digest are validated and consumed by the reader; they are not copied into
the public document.

| KSE event/tag | Typed-sidecar v1 destination |
|---|---|
| source 1/2/3 | `file.package_id` / `file.module_id` / `file.file_id` |
| source 4/5/6 | `file.logical_path` / `file.byte_length` / `file.content_sha256` |
| source 7 | `compiler.edition`; producer value `2026` maps to schema name `kofun-2026` |
| source 8 | `compiler.semantic_compatibility` |
| source 9 | `generation.sequence` |
| source 10 | frozen projector result `compiler_exit_class`; checked against end status, never serialized as authority |
| node 1/2/3/4 | `nodes[].id` / closed textual `kind` / `span` / `status` |
| node 5/6 | `nodes[].depends_on` / `diagnostic_ids` |
| identity 1 | selects the owning `nodes[]` record |
| identity 2/3 | `nodes[].identities[].kind/value`; constructor identity maps to value-namespace `SymbolId` |
| identity 4 | must be `validated`; v1 has no identity-level status field, so any other status rejects |
| reference 1/2/4/5 | `references[].id/from_node/span/status` |
| reference 3 | `references[].namespace`; constructor namespace maps to v1 `value` |
| reference 6 | `target.disclosure` under the status/disclosure pairing |
| reference 7/8 | resolved `target.identity.kind/value` and identity-owner `declaration_node`; hidden may retain only safe `identity_kind`; an unavailable/provisional safe kind is revalidated and then omitted because v1 permits it only on hidden targets |
| reference 9 | hidden/provisional/unavailable public `target.reason` |
| reference 10 | `references[].diagnostic_ids` |
| fact 1/2/3 | owning node / nested `type`, `effect`, `ownership`, or `origin` / nested `status` |
| fact 4/5 | nested `display` / public `reason` |
| fact 6/7 | fact-local dependency/diagnostic closure; then unioned into the owning node's `depends_on` / `diagnostic_ids` and canonicalized. An error fact must carry its own diagnostic ID; an owner-only link does not satisfy it |
| diagnostic 1/2/3/4 | `diagnostics[].id/code/category/severity` |
| diagnostic 5 | `template_id`; Stage 2 separators are deterministically mapped to schema-name `-` separators |
| diagnostic 6/7/8 | `primary.file_id` / `primary.span` / `fallback_text` |
| diagnostic 9 | `affected_ids` |
| diagnostic 10 | `remedies[].id` as stable `stage2-remedy-<decimal>`, then canonical string-ID order (`10` therefore sorts before `2`) |
| diagnostic 11 | `truncated` |
| diagnostic 12 | `related[]`; label becomes the stable schema `relation`, FileId/span become `location` |
| diagnostic 13 | matching remedy `span/replacement`; the single-file FileId is revalidated and omitted because v1 remedy edits are root-file relative |
| end 1/2 | `source_status` / `completeness` |

The reader independently checks KSE magic/version/count/payload/digest, exact
tag/wire schemas, fixed-width values, UTF-8/NFC, logical path, phase order, and
nested list framing before publishing recursively frozen records. Projection
then rechecks IDs, spans, unique canonical sets, direct error-record diagnostic
links, dependency and diagnostic closure, identity ownership,
namespace/target-kind compatibility, safe
disclosure, public reason allowlists, and end/exit pairing. The result is
accepted only after `encodeTypedSidecar` and `readTypedSidecar` agree.

## APIs and transaction

The destination-free read/project seam is:

```js
readStage2SemanticEvents(bytes) -> EventReadResult
projectStage2SemanticEvents(events) -> ProjectionResult
```

Both successful values are recursively immutable. `emitStage2TypedSidecar`
adds a current-source re-digest and the existing atomic replacement writer:

```js
emitStage2TypedSidecar(eventBytes, destination, {
  sourcePath | currentSourceBytes,
  signal
}) -> Promise<WriteResult>
```

All data failures are bounded tagged `ETS03`–`ETS06` results. Programmer API
misuse may throw `TypeError`.

`kofun check INPUT.kofun --emit-typed-sidecar OUTPUT --generation N` applies
the same compile, Stage 1 compatibility, and ownership-check orchestration as
plain `kofun check`. It freezes the resulting stdout/stderr/exit bytes before
the output-only emitter sees the selected workspace-local KSE,
re-digests the exact current source immediately before the atomic writer, and
then applies FileId/generation/source-digest replacement. A failed source keeps
its language exit status and diagnostic channel even if tooling emission also
fails; a clean source with tooling failure exits 3. Ownership-only accepted
sources emit from the ownership authority rather than a narrower compile
attempt. No-flag check never starts this path. Sources over the producer's
64-function profile return bounded `ETS04` without entering the oversized
observer transaction.

## Consumer boundary

The C function `kofun_stage2_produce_semantic_events` already accepts an exact
in-memory source snapshot, logical path, generation, authority selection, and
sink, and returns a frozen compiler outcome without a destination. The JS
projector likewise returns a destination-free immutable document from KSE.

There is not yet a self-contained packaged JS binding that joins those two
in-process for the VS Code server. A later #606 adapter must package that
producer boundary and may not spawn or scrape `kofun check`, rely on a
repository-relative helper, read a sidecar as compiler input, or expand the
4,096-event/4-MiB KSE profile. Inputs larger than this bounded semantic profile
remain an explicit `ETS04` result for that consumer to handle with its own
bounded syntactic fallback; they are not retried into fabricated facts.
