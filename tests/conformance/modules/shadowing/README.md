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
one would pass every other negative here and still fail this one.

Single-`Int`-payload constructor patterns bind, so pattern bindings are now
part of the same contract rather than an excluded case. The positive fixture
binds `carried` in two separate match arms, reusing the spelling of an
enclosing `let`, and reads that `let` afterwards to show the ancestor was
restored. `pattern_binding_duplicate.kofun` collides an arm-body `let` with
its own arm's pattern binding, and `pattern_binding_immutable.kofun` assigns
to an immutable pattern binding while a **mutable** ancestor shares the
spelling — that one fails only if assignment resolves the nearest
`BindingId` rather than the name.

Executable multi-field forms are still refused by `E2S24`, but the lossless
Pattern tree now rejects repeated and nested repeated binding names with
`E2S47` before that lowering boundary. Or-pattern alternatives remain resolved
by the ADT projector, where sibling alternatives publish one `BindingId`, so
this gate leaves alternative binding-set equality to that projector. The
duplicate fixtures inspect resolved Pattern records; they do not infer bindings
from constructor or wildcard token text.

Shadowing is not warned about. Any formatter or linter opinion on it is
non-semantic, as `spec/syntax/FOUNDATIONS_AND_CONTROL.md` states under `#41`:
it cannot change resolution and cannot reject a program this gate accepts.

Run `sh tests/conformance/modules/shadowing/run.sh`.
