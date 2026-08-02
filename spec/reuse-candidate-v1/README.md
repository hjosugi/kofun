# ReuseCandidate v1 gate

The normative contract is [`../reuse-candidate-v1.md`](../reuse-candidate-v1.md).
This directory is its executable half.

```sh
sh spec/reuse-candidate-v1/check.sh
```

## What is here

| Path | Role |
| --- | --- |
| `validate.mjs` | The validator. `validate.mjs RECORD.json` writes the canonical record to stdout, or exits 1 with one named error on stderr and no partial output. |
| `check.mjs` | In-process closure checks over the state, reason, provenance, observer, ordering, and remark vocabularies, plus mutations that must fail closed. |
| `check.sh` | The gate. Runs every vector twice from clean work directories, refuses hand-edited goldens, and runs the AggregateLayout v1 gate. |
| `backends.json` | The committed backend support table. A record's backend claim is checked against it. |
| `valid/` | Records the validator MUST accept. Each is byte-identical to its own canonical validated form. |
| `invalid/` | Records the validator MUST reject, with `invalid/expected.tsv` pinning each one's error identity. |

## The checker recomputes

`validate.mjs` does not read a stored verdict. Layout evidence is recomputed by
joining `layout_id` and constructor name into
`spec/aggregate-layout-v1/examples/core.<target>.json` and comparing the
descriptor's own facts against the copied ones; backend support is recomputed
from `backends.json`; the structural reason is recomputed from the two layout
sides; and the remark is recomputed from the state and reason. A golden is
accepted only when it equals the canonical output of validating it, so editing a
golden by hand cannot make it pass — the gate proves that on every run by
editing one and requiring the rejection.

## Adding a vector

1. Write the record with every v1 field, in the field order of the normative
   document.
2. A **valid** vector is stored as its canonical form: run
   `node spec/reuse-candidate-v1/validate.mjs FILE` and replace the file with
   that output — two-space JSON, one trailing newline.
3. An **invalid** vector is stored compact, on one line. It is an input, never a
   canonical record, and the difference in shape says so at a glance.
4. For an invalid vector, add a row to `invalid/expected.tsv` with the code and
   name it must produce. The gate requires the error identities to be distinct,
   so a new invalid vector must exercise a rejection no other vector does.
5. Update the vector counts asserted in `check.sh`.

## What this gate does not prove

No constructor storage is reused anywhere in this repository. The gate proves
that the record and its refusals are well defined and enforced — not that any
optimization exists, and not that any backend could run one. Every registered
backend in `backends.json` is `unsupported`, which is why a record claiming a
guarantee is honoured is refused.
