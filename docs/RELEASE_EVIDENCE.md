# Release evidence

Kofun publishes bounded capability claims. This document describes the one
machine-checked join between those claims and the evidence that bounds them.

The manifest does not replace [`docs/MVP_IMPLEMENTED.md`](MVP_IMPLEMENTED.md),
`tests/conformance/capabilities.tsv`, the diagnostic registry, or any other
subsystem artifact. Each of those remains authoritative for its own domain. The
manifest connects them, and fails when published wording outruns executable
evidence.

## The files

| File | Role |
|---|---|
| [`spec/release-claim.schema.json`](../spec/release-claim.schema.json) | Versioned schema. Constrains shape. |
| [`release/claims.json`](../release/claims.json) | Canonical manifest. The only editable source. |
| [`tests/release/validate-claims.mjs`](../tests/release/validate-claims.mjs) | Checker and pack generator. |
| [`tests/release/make-invalid.mjs`](../tests/release/make-invalid.mjs) | Negative-mutation fixtures. |
| [`tests/release/check-claims.sh`](../tests/release/check-claims.sh) | The gate. |
| `artifacts/release-evidence/` | Generated pack. Committed, never hand-edited. |

## The two lanes

**Fast structural lane — `task release-claims`, inside `task verify`.** Reads
the repository and runs no gate, so it costs a second. It answers: does every
published capability still join exactly one claim, does every claim still name
evidence that exists, and does the committed pack still match the manifest?

**Release lane — `task release-evidence`.** Regenerates
`artifacts/release-evidence/` from the manifest. CI runs this from a clean
checkout and requires `git diff --exit-code` to be empty, which is what makes
the pack reproducible rather than a snapshot of one machine.

The generated pack is `CLAIMS.md`, `EVIDENCE.md`, `LIMITS.md`, `REPRO.md` and
`index.json`. Editing any of them fails the structural lane, so the generated
Markdown cannot quietly become a second source.

## States

| State | Meaning | Positive gate | Negative boundary |
|---|---|---|---|
| `implemented` | General implementation. | required | required |
| `checkpoint` | Bounded executable slice. | required | required |
| `design` | Accepted design, nothing shipped. | forbidden | optional |
| `open` | Acknowledged absent. | forbidden | optional |
| `unsupported` | An explicitly refused boundary. | forbidden | rejection or skip |

A `design` or `open` claim may not carry a positive gate. This is the rule that
stops a design document, or a closed issue, from reading as an implementation.

## What the checker refuses

Every rule below is proved by a fixture in `make-invalid.mjs` that must fail.
A checker that stopped enforcing one would still report PASS on the honest
manifest, so the rules are tested rather than trusted.

- a published capability with no claim, or with more than one;
- a claim whose wording no longer appears in any public source;
- a claim id, or status text, that has drifted from the published table;
- a duplicate claim id, unknown state, unknown area, or unknown target;
- an evidence path that is missing, untracked, unnormalized, or a directory;
- a gate or reproduction command naming a task that does not exist;
- an `implemented` or `checkpoint` claim with no boundary that fails outside it;
- an `unsupported` claim without an explicit rejection or skip observation;
- a `design` or `open` claim asserting a positive gate;
- a safety claim without a threat model and a negative test;
- a budgeted performance claim without a benchmark, environment, and budget;
- an unknown field anywhere in the manifest; and
- a stale or hand-edited evidence pack.

## Adding or changing a claim

1. Change the published wording in `docs/MVP_IMPLEMENTED.md` if that is what
   moved, including its `Claim` cell.
2. Edit `release/claims.json`. Keep `claims` sorted by `id`.
3. Run `task release-claims`. Each refusal names the row and the repair.
4. Run `task release-evidence` and commit the regenerated pack.

Evidence must bind a repository input. A timestamp is not freshness: the
generated `index.json` records digests of the manifest, the schema, and every
referenced evidence file, and records no wall-clock time at all.

## Deliberate boundaries

- Prerequisites are declared per claim. A gate whose prerequisite is missing
  must report a skip; reporting a pass it did not observe is a defect, and
  `REPRO.md` lists which claims need which tool.
- The manifest owns the capability table in `docs/MVP_IMPLEMENTED.md`. README
  prose is checked only for its area names, because prose is not a claim
  boundary.
- Issue state never appears here. An open or closed issue is not evidence.
