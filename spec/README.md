# Frost language specification draft

This directory separates the implemented Stage 0 grammar from future language design.

- `grammar.ebnf` is the grammar accepted by the Stage 0 parser, except that automatic newline-to-semicolon insertion is described lexically.
- `semantics.md` records the executable semantic contract for the current prototype.
- `law-evidence.schema.json` defines the machine-readable `frost.law-evidence/v1` artifact emitted by `frost laws`.
- Design-only features in `docs/` are not normative until they are added here and covered by conformance tests.

The specification is intentionally versioned independently from the implementation. The current draft is `0.2-bootstrap`.
