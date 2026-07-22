# Executable lexical scopes

`run.sh` fixes the Stage 2 Core lexical-scope contract at an executable
boundary. The positive fixture combines nested `if`, guarded Bool `match`,
payload-free enum `match`, shadowing, outer reads, and assignment to an outer
`let mut`. Its generated C must use `BindingId` storage rather than source
spellings, and its scope HIR has an exact deterministic golden.

Negative fixtures cover direct and nested self-reference, sibling/child escape,
use before declaration, unknown nested names, cross-function isolation, and an
enum-constructor/local-name collision with `E2S35`. Assignment through a child
scope still preserves `E2S22` for an immutable or unknown target.

The bounded resolver accepts at most 32 lexical levels, 256 scopes, 256
bindings, and 256 binding uses per function. `run.sh` exercises every accepted
boundary and its first rejected value. It also recompiles identical source from
a remapped path and requires byte-identical C, token tape, and scope HIR.

Run `sh tests/conformance/modules/lexical-scopes/run.sh`.
