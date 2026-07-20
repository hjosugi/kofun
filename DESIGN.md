# Kofun design status

## Active implementation

The active toolchain is the Kofun-written arithmetic compiler at
`bootstrap/stage1/compiler.kofun`. Its audited C11 seed starts the compiler
without a Python runtime. `bootstrap/stage1/check.sh` is the executable gate.

## Target language

The target design includes static typing, `read` / `edit` / `take` ownership,
ADTs, exhaustive matching, closures, collections, law checking, and native and
WebAssembly backends. These remain design work unless
`docs/MVP_IMPLEMENTED.md` and an executable Kofun test say otherwise.

Kofun source uses `.kofun`; legacy `.kf` files are rejected.

The semantic authority is `spec/semantics.md` plus the conformance corpus.
Unsupported behavior must be diagnosed or explicitly skipped, never silently
counted as passing.
