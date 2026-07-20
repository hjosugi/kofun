# Issues 31-34: executable roadmap

This directory records the reviewable boundary between implemented bootstrap
capabilities and the acceptance criteria of issues 31-34. It is intentionally
not an implementation claim. An unchecked item remains open even when a nearby
checkpoint is executable.

| Issue | Acceptance item | Current evidence | Status |
|---|---|---|---|
| #31 | Generic functions and types with trait bounds type-check | Syntax and type-system design documents only | open |
| #31 | Strategy selected with measured justification | A baseline and measurement protocol are specified in `generics-and-traits.md`; no compiler measurements exist | open |
| #31 | Laws can be stated over generic types | Proposed syntax exists, but no generic proof kernel exists | open |
| #32 | Stage 1 compiles its own source | Stage 1 accepts only the arithmetic Core | open |
| #32 | Stage 2 is byte-identical to Stage 1 | Source/token/IR projection is deterministic, but no self-produced compiler exists | open |
| #32 | Manifest closes the gate and records hashes | The manifest truthfully records both self-recompile gates as open | open |
| #33 | Stage 1 builds through the native backend | Three Kofun-authored ELF fixtures execute; Stage 1 still builds through C11 | open |
| #33 | Interpreted and native Stage 1 outputs match | The Kofun checker exists, but no native Stage 1 artifact exists to supply its second input | open |
| #33 | Bootstrap gate verifies the native path | Native fixtures and Stage 1 are separate gates | open |
| #34 | Inline diagnostics | Versioned diagnostics update and clear through the packaged stdio server | implemented |
| #34 | Definition and hover | Same-document bootstrap symbols and available types/modes are indexed | implemented |
| #34 | Interactive incremental performance | The deterministic 10,000-declaration, 100-range-edit gate enforces the contract thresholds | implemented |

## Current executable evidence

Run the isolated probe:

```sh
sh spec/roadmap-31-34/verify-current-gates.sh
```

It compiles the audited Stage 2 C11 checkpoint, uses that checkpoint to lower
`current-core-probe.kofun`, compiles and executes the result, and checks the
observable floor-arithmetic contract. It also confirms that the self-recompile
manifest entries remain open, then runs the packaged LSP protocol, editor smoke,
and performance gates.

To rerun the repository's complete Stage 2 and native fixture gates before the
probe:

```sh
sh spec/roadmap-31-34/verify-current-gates.sh --full
```

The probe demonstrates real integer Core lowering and the scoped bootstrap LSP.
It does not demonstrate generic type checking, compiler self-reproduction,
native Stage 1, project-wide editor indexing, or full compiler type inference.

## Close policy

An issue may be closed only after every acceptance item in its issue-specific
document has:

1. an executable test or reproducible measurement;
2. an artifact produced from canonical `.kofun` source;
3. an exact command recorded in the repository; and
4. no contradiction with `bootstrap/manifest.json`.

Generated bootstrap artifacts may be checked in when their canonical source,
reproduction command, and digest are recorded. Python is not part of any plan
or validation command in this directory.
