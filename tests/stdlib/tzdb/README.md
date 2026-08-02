# Bounded tzdb producer

This directory owns the executable checkpoint for issue #878. It is a small,
offline fixture, not an IANA database implementation or a stable public API.

`tzdb.kofun` receives a fixed-capacity `Bytes20` value. Two adjacent bytes are
packed into each bounded word so the typed semantic event stays under its
16-field limit. Exactly eighteen bytes form the v1 payload; the explicit
length and claimed content digest sit outside those bytes. The producer
validates the bound, byte range, magic, version,
zone, transition count, transition ordering, offset chain, trailing data, and
content digest before constructing the bounded `Tzdb` value.

The fixture uses one synthetic zone:

- offset `0` until UTC instant `1000`;
- offset `+100` until UTC instant `2000`;
- offset `0` afterwards.

That produces a local-time gap `[1000, 1100)` and fold `[2000, 2100)`. The
golden observes one unique value, one nonexistent value, and both UTC choices
for one ambiguous value. `Ambiguous` packs its two bounded instants as
`earlier * 100000 + later`, because the current Stage 2 enum profile carries
one `Int` payload. The gate checks the packed value explicitly.

Every result that leaves the resolver is also projected as a fixed five-field
numeric serialization:

1. zone code;
2. fixture format version;
3. content digest;
4. closed resolution kind;
5. closed resolution payload.

`serialize_resolution` receives the `Tzdb` and local reading together, computes
the resolution, and copies provenance from that same database. A caller never
passes a zone, version, or digest beside an already-computed result. This is the
bounded executable equivalent of the canonical `ResolvedLocal`/Text surface;
it is not a claim that portable Text-returning serialization has landed.

Fixed-offset arithmetic failure keeps `-7` as the closed resolution kind and
carries the offending `LocalDateTime.wall_seconds` as its payload. The kind and
payload therefore answer different questions instead of repeating `-7` twice.

The gate creates a valid drift observation by moving transition 1 from `1000`
to `1001`. That changes only byte 6 of the same v1 format. Normal, gap, and fold
rows each carry the resulting new digest, so an isolated serialized value can
be attributed without relying on a run-level header.

## Explicit local-time policy

Fold policy and gap policy are separate closed types. Their serialized codes
are deliberately disjoint:

- `1` FoldEarlier;
- `2` FoldLater;
- `3` FoldReject;
- `4` GapShiftForward;
- `5` GapReject.

The executable constructors carry one ignored `Int` marker because the current
Stage 2 ADT projection requires one bounded payload. The canonical surface uses
payload-free constructors; the marker is not part of policy semantics.

Applying a policy produces the selected instant or a typed refusal. Fold and
gap refusal codes are `-10` and `-11`, outside the reader-error domain
`-1` through `-9`. An applied row repeats the five resolution fields and adds
policy code, outcome code, and outcome payload. Applying any policy to Unique
keeps its instant while recording the policy; Reject only refuses an actual
fold or gap.

There is no default-policy overload. The focused gate derives a negative source
from the authoritative producer by removing one FoldEarlier argument, then
requires both `check` and `build` to reject it with `E2S17` and no binary.

The digest is a versioned polynomial fixture digest modulo `1000000007`. It is
content identity for this bounded format, not a cryptographic authenticity
claim. No network, host zoneinfo, locale, ambient file, or ambient clock input
is permitted.

Run:

```sh
sh tests/stdlib/tzdb/check.sh
```

The gate checks and executes the Kofun producer with both the reference
executor and emitted C11 backend, compares exact output bytes twice, audits the
typed Stage 2 HIR and emitted C, and names every rejection class.

## What the gate pins beyond the producer

`stdlib/tzdb/tzdb.kofun` is the canonical surface: the `Bytes`, `List`,
multi-payload and `Result` shapes the Core cannot lower yet. It is pinned the
way `stdlib/clock/adapters.kofun` is — its declarations must survive, its
`Ambiguous` and `Nonexistent` must keep carrying both offsets, and it must
still stop at the documented compiler boundary. The executable evidence is the
producer, not that file.

Both edges of the gap and of the fold are read. The resolver gets them right —
`local >= start && local < end` for both — so these are regression guards
rather than fixes, but an off-by-one at an edge is the classic way an hour goes
missing and nothing pinned it before.

`mixed_local_instant.kofun` and `local_wall_seconds_on_instant.kofun` must be
refused by `check` and by `build`. A local reading and an instant are both one
number; keeping them apart is the whole reason the resolution sum has anywhere
to live.

Independence from ambient state is executed, not grepped for: the producer runs
identically under `TZ=Pacific/Kiritimati`, under a `Sao_Paulo` zone with a
`tr_TR` locale, and under `env -i`. The empty environment is there because a
program that needed `TZ` would more likely fall back than fail, and a fallback
would not show up as a difference between two hostile settings.
