# KIF and typed-sidecar release qualification

`kif-sidecar-v1.json` is the checked release matrix for the two existing
artifact families. It does not merge their schemas or their trust models: KIF
remains compiler authority, while a typed sidecar remains disposable tooling
data and cannot affect compilation, packages, linking, build caches, or KIF.

Each row records the versions a reader accepts, the migrations it implements,
the bounded treatment of future versions, production byte/depth limits,
measured budgets, executable evidence, and every integration gate that must be
green. V1 deliberately has no migration: version 1 is accepted and an unknown
future version is rejected before it can publish trusted facts.

Run `task artifact-qualification`. The gate validates the matrix against the
production constants and Taskfile, runs negative mutations, creates two
byte-identical cold/warm artifacts, and writes raw latency, byte, environment,
and peak-RSS samples to
`build/artifact-qualification/measurements.json`. The raw file is a CI artifact,
not a second checked-in source of budgets.

The negative suite proves that a release cannot pass after evidence is removed,
an authority boundary is widened, a future version is accepted, or a production
or measurement limit is relaxed. Existing focused gates still exercise corrupt,
malformed, oversized, deeply nested, stale, interrupted-write, path-remapped,
source-reordered, and disclosure-hostile inputs; this aggregate gate checks that
none of those facts silently falls out of the release matrix.
