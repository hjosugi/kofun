# Executable local shadowing

`run.sh` exercises the Stage 2 shadowing contract on the resolved
`ScopeId`/`BindingId` HIR. A child scope may shadow an ancestor, sibling scopes
may reuse a spelling, and leaving the child restores the ancestor identity.
The positive fixture also shadows a parameter from the function-body scope and
shadows a concrete enum binding with an Int binding without changing the enum.
Nearest-binding assignment is checked independently: an immutable child
binding still rejects assignment even when its mutable ancestor has the same
spelling.

`E2S47` rejects duplicate parameters and duplicate `let` bindings in one
scope. The diagnostic points at the second declaration and includes the first
declaration byte position. `duplicate_after_statements.kofun` separates the
two declarations with other statements, another binding, and a whole child
block, so a resolver that only compared a declaration against the preceding
one would pass every other negative here and still fail this one. General
pattern bindings remain unsupported by the current frontend; this gate does
not infer pattern bindings from constructor or wildcard token text.

Shadowing is not warned about. Any formatter or linter opinion on it is
non-semantic, as `spec/syntax/FOUNDATIONS_AND_CONTROL.md` states under `#41`:
it cannot change resolution and cannot reject a program this gate accepts.

Run `sh tests/conformance/modules/shadowing/run.sh`.
