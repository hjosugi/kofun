# Kofun language specification draft

This directory separates normative language contracts from the smaller
Python-free bootstrap implementation.

- `grammar.ebnf` is the full-language grammar draft. The active Stage 1 and
  Stage 2 checkpoints intentionally accept smaller subsets.
- `semantics.md` records the semantic contract that every executable backend
  must preserve for the constructs it accepts.
- `backend-differential-contract.md` defines exact cross-backend observations
  and unsupported-feature accounting.
- `roadmap-31-34/` defines executable acceptance gates for generics, compiler
  fixed points, native Stage 1, and the language server.
- `syntax/FOUNDATIONS_AND_CONTROL.md` specifies issues 35 through 47 and links
  each claim to explicit bootstrap capability evidence.
- `syntax/EXPRESSIONS_AND_LITERALS.md` specifies issues 48 through 59 without
  treating planned syntax as implemented behavior.
- `parser/TOKEN_SPANS.md` defines the current Stage 2 byte-span prototype and
  the work still required for a lossless parser.
- `modules/package-roots.md` defines deterministic manifest and anonymous
  single-file package roots and the versioned `PackageIdPayload` contract.
- `law-evidence.schema.json` defines the machine-readable
  `kofun.law-evidence/v1` artifact.

Design-only material in `docs/` is not normative until it is promoted here
with conformance evidence. The specification is versioned independently from
the implementation; the current draft is `0.3-bootstrap`.
