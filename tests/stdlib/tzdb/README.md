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
