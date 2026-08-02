# Documentation index

The documentation index is a bounded, non-authoritative view for documentation
renderers and local developer tools. It joins two independently validated
inputs:

- the compiler-authoritative KIF binary, decoded by the bounded KIF v1 reader;
- the matching non-authoritative typed sidecar, used only to establish whether
  each stable declaration identity was validated in the current check.

The result uses the schema marker `kofun.documentation-index/v1` and always
contains `"authoritative": false`. It can improve documentation tooling, but
it cannot make a compile, dependency, cache, package, or linker decision
succeed.

## Trust and visibility boundary

The KIF binary owns declaration names, stable identities, canonical signatures,
effective visibility, and the public/package-internal semantic digests. The
typed sidecar does not own any of those facts. The projector accepts KIF facts
only through `kif_v1_tool read`, after the binary reader has checked the
envelope, canonical ordering, digests, identity graph, and visibility closure.
The emitted JSON dump is still labelled non-authoritative: it is a bounded
projection of already validated KIF bytes, never an interface that a compiler
may consume.

The two supported views are deliberately different:

| View | Required authority | Included declarations | Output label |
| --- | --- | --- | --- |
| `public` | validated public KIF view | `pub` only | `public` |
| `package-internal` | exact declaring `PackageId` | `pub` and `internal` | `public` or `package-internal` per entry |

A package-internal request without the exact declaring `PackageId` fails. A
public result never contains internal or private names or identities. Private
declarations are absent from both KIF views and therefore cannot be recovered
from sidecar display text. The projector also never copies sidecar paths,
spans, diagnostics, inferred display strings, or path-remap roots. Errors are
bounded generic messages and do not echo rejected input, so a malicious
sidecar cannot use a failure path to disclose a private name or filesystem
location.

The join is exact on `(identity kind, stable identity)` and declaration kind.
A complete sidecar missing a visible KIF declaration fails closed. A partial
sidecar emits only the validated matching prefix. Package/module disagreement,
duplicate identities, unsupported KIF facts, and malformed inputs produce no
new index.

Every projection repeats that join against the KIF visibility projection and
caller package supplied for the current request. A previously public sidecar
identity therefore disappears from the public projection when the live KIF
makes it internal or removes it. A conflicting replacement identity fails the
complete join. Replaying an old sidecar, forging a caller/package/symbol
identity, or changing a path-remap root cannot restore access or expose sidecar
paths, source content, or spans. Failed requests have no index to publish, so
an already committed safe result remains unchanged.

## Operating procedure

Build or obtain the repository's bounded KIF v1 reader, then supply a KIF and
typed sidecar produced for the same package, module, and source generation.
For a public view:

```sh
node tooling/typed-sidecar/documentation-index-cli.mjs \
  --sidecar build/api.kofun-semantic.json \
  --kif build/api.kif \
  --kif-reader build/kofun-kif-v1 \
  --output build/api.documentation-index.json \
  --view public \
  --current-source "$CURRENT_SOURCE_SHA256" \
  --current-generation 12
```

For a local internal view, change `--view` to `package-internal` and pass the
declaring KIF identity with `--requesting-package PACKAGE_ID`. Do not infer that
identity from a directory, checkout location, source path, or display name.

The consumer must inspect all of these fields before rendering:

- `authoritative` remains `false` in every result;
- `current` says whether source digest and requested generation matched;
- `status` is `complete`, `partial`, `stale`, or `cancelled`;
- `visibility_scope` records the requested disclosure boundary;
- `visibility_digest` pins the exact KIF view used for the projection;
- `reason` explains the bounded trust state without exposing rejected data.

Only a current complete index is a complete documentation snapshot. A current
partial index is a best-effort validated prefix and should be visibly marked
in UI. Stale and cancelled projections contain no entries. None of these
states upgrades the result into compiler authority.

Run the focused repository gate with:

```sh
task documentation-index
```

That gate compiles the actual Stage 2 KIF producer and reader, creates a real
KIF fixture, exercises public/internal projection and atomic publication, and
checks this operating contract.

## Limits and failure behavior

The reviewed default maxima are 16 MiB for the KIF dump input, 4 MiB for the
output document, 4,096 facts/entries, 256 UTF-8 bytes per NFC declaration name,
and 256 function parameters. Callers may lower but not raise these limits.
Unknown fields or schemas, non-canonical identities, malformed JSON, invalid
UTF-8/NFC, duplicate visible identities, and oversized inputs fail closed.

Failures use stable classes:

| Code | Meaning |
| --- | --- |
| `TDI01` | invalid request, view, generation, or limits |
| `TDI02` | package/module/identity/trust boundary mismatch |
| `TDI03` | invalid sidecar, KIF, projection, or join |
| `TDI04` | reviewed byte/count limit exceeded |
| `TDI05` | invalid destination or denied replacement |
| `TDI06` | cancellation, lock, destination safety, or atomic I/O failure |

Publication uses a mode-0600 temporary regular file and destination lock in the
target directory, re-reads the destination before rename, and replaces it
atomically. A generation must increase. A partial index cannot replace a
current complete index, and stale/cancelled indexes cannot be published at all.
Malformed old files, symlink/directory destinations, races, cancellation, or
any failure before rename preserve the old bytes and clean up only files whose
device/inode still match those created by this writer.

This slice intentionally supports the facts present in bounded KIF v1.1:
functions, flat ADTs, zero/one-payload constructors, and re-export bindings with
`Int` or flat nominal type references. It does not publish documentation
comments, records, generics, effects, ownership annotations, arbitrary source
locations, or a site search format. Those require canonical KIF facts and a
separately reviewed schema change; sidecar display text is never a fallback.
