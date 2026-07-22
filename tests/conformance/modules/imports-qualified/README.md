# Qualified local imports

This focused Stage 2 gate implements the first language-level module import:
`import a.b` binds only module qualifier `b`; calls remain qualified. The
resolver consumes the same validated five-field package inventory as the
top-level declaration collector. Host source operands are adapter inputs, not
semantic identities or filesystem discovery.

The gate covers complete-header collection, production `ImportBindingId`
framing, target `SymbolId` retention, the identity-only visibility primitive,
canonical shortest-cycle selection, source-order and checkout remapping,
bounded failure, and optional C11 execution with SymbolId-derived linker names.
The C output accepts only the explicitly diagnosed Int return-expression
subset; it is not general module initialization or a new source-level ABI.

The declared maximum of 256 imports per module is syntactically enforced.
With the same-package limit of 256 modules, self-import and duplicate-edge
rejection make 255 distinct successful outgoing edges the reachable maximum.
Similarly, the 65,536 total-edge guard is retained even though a valid
256-node simple directed graph has at most 65,280 non-self edges.

Run `sh tests/conformance/modules/imports-qualified/run.sh` or
`make imports-qualified`.
