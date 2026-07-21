# Declaration visibility

Status: normative design target for GitHub issue #285.

Kofun uses default-private declarations and explicit public API. This document
defines declared and effective visibility for top-level declarations, nominal
type members, imports, re-exports, entry points, and interface construction.
The active Stage 2 compiler implements the basic modifiers on top-level
functions as the bounded #578 syntax/HIR slice. Resolver enforcement and other
declaration kinds remain follow-up work.

The words **must**, **must not**, **should**, and **may** are normative.

## Visibility table

| Source form | Meaning |
| --- | --- |
| no modifier | private |
| `private` | explicit private at the nearest privacy boundary |
| `pub(to ancestor.path)` | the named ancestor module and its descendants |
| `internal` | every module in the current package |
| `pub` | importing packages, subject to enclosing visibility |

`public`, `protected`, `pub(crate)`, `pub(super)`, and `pub(in path)` are not
aliases. Capitalization never changes visibility.

The common forms, from narrowest to widest, are private, restricted ancestor,
internal package, and public. A restricted path is a specialized subset of the
current package rather than arbitrary friendship.

## Identities and boundaries

- **Package** and package identity follow
  [`package-roots.md`](package-roots.md). `internal` means that package/build
  unit, not a textual module, directory, or neighboring checkout.
- **Module** is a semantic namespace inside one package. #300 owns the mapping
  from source files to `ModuleId`.
- **Source file** is one declared UTF-8 source input with a stable `FileId`.
- **Nominal type** is one resolved `TypeId`; a type alias or equal spelling
  does not confer private access.

Visibility is a compile-time name-resolution and interface rule. It is not
runtime authorization, a security sandbox, or linker symbol visibility.

## Default and explicit private

Every declaration and member without a visibility modifier is private.
Writing `private` is allowed and has identical access semantics. Syntax/HIR
must retain whether private was implicit or explicit for formatting,
diagnostics, and source-preserving tools.

Private uses the nearest enclosing privacy boundary:

- a top-level declaration is visible only within its source file;
- a field, constructor, or callable member of a nominal type is visible only
  within that nominal type's implementation scope; and
- a nested declaration is visible only within its immediately enclosing
  declaration scope.

An extension does not gain private access merely by sharing a package or type
spelling. Any future multi-file inherent implementation rule must prove the
same resolved `TypeId` under an accepted source-mapping contract.

## Internal

`internal` makes a declaration visible to all source files and modules with
the same `PackageIdPayload`. Code in another package cannot access it, even if
it uses the same directory, repository, or textual module path.

A white-box test explicitly compiled as a target of the package may access
internal declarations. A black-box test compiled as a dependent package may
not. Friend packages, package merging, and manifest access allowlists are
unsupported.

## Public and enclosing reachability

`pub` makes a declaration eligible for external use. Eligibility is not the
same as reachability. Effective visibility is the intersection of:

1. the declaration's own visibility;
2. every enclosing type/declaration boundary;
3. every module/path segment used to reach it;
4. the importing or re-exporting binding; and
5. every type, trait, effect, ownership contract, constructor payload, or
   constant that appears in its exposed signature.

A public member inside an inaccessible type or module is not externally
reachable. The compiler must not silently promote an enclosing boundary.

An import never changes target visibility. A re-export may preserve or narrow
visibility, but it may not widen it. If source requests a wider re-export than
the target's effective visibility, compilation fails instead of silently
reducing the advertised API. #287 defines forwarding syntax and provenance.

## Restricted public

`pub(to path)` names one ancestor module in the same package. The declaration
is visible from that module and its descendants.

The target must:

- resolve in the declaring package;
- be the declaring module itself or one of its ancestors;
- use its canonical module path, not an alias; and
- remain within deterministic path/depth limits.

A sibling, descendant, missing module, other package, alias-only path,
multiple target list, or friend list is rejected. Restricting targets to
ancestors keeps access monotonic in the module tree.

This form is normative but deliberately excluded from the first executable
slice. Until its dedicated implementation lands, the compiler must reject it
with a stable unsupported-feature diagnostic. It must not treat it as `pub` or
`internal`.

## Types, fields, constructors, and patterns

Visibility is attached independently to a type, every field, every
constructor, and every callable member. Publishing a type does not publish its
representation.

~~~kofun
pub type User = {
    private id: UserId,
    pub name: Text,
}
~~~

Outside a field's effective visibility, code cannot select, construct, update,
or destructure that field. Reflection, metaprogramming, generated code, and
pattern matching receive no privileged bypass.

A type with hidden construction state must expose an accessible constructor
or factory if callers are expected to create values. ADT constructor
visibility follows the same independent rule as record fields.

## Public API leakage

A declaration cannot expose a signature component that is less visible than
the declaration's requested effective API. Invalid examples include:

- a public function parameter or result containing an internal/private type;
- a public field whose type is less visible;
- a public trait bound, effect, or constructor payload naming a hidden
  declaration; and
- a public alias revealing hidden representation.

The diagnostic identifies the exported use and hidden declaration, reports
both effective boundaries, and suggests either reducing the API visibility or
exposing an appropriate public abstraction. It must not reveal private details
from an inaccessible dependency in its message.

An explicitly opaque type may hide representation only through a separately
specified opaque-interface rule. Opacity is never inferred to make an invalid
public signature pass.

## Entry points, FFI, and runtime

- A build manifest may select a private `fn main()`. Being an executable entry
  point does not make a source declaration public.
- `pub` does not create a C, Rust, ELF, or WebAssembly export symbol.
- FFI annotations and native symbol visibility remain #208/interoperability
  concerns.
- Visibility checks finish during resolution and compiled-interface creation;
  successful execution has no visibility check at runtime.

Public semantic, package-internal, and target ABI artifacts are separate under
#303. Private declarations are omitted from public and package-internal
interfaces unless a future accepted semantic-input rule explicitly requires a
private fact without exposing its source representation.

## Lexical status and grammar

`pub`, `internal`, and `private` are contextual declaration modifiers. The
lexer continues to emit ordinary identifier tokens outside a position where a
declaration or member may begin. They are not added to the hard-keyword list.

The basic grammar target is:

~~~text
visibility := "pub" | "internal" | "private"
declaration := visibility? declaration-body
~~~

At most one visibility modifier is accepted. Duplicate, conflicting,
misplaced, malformed, and unsupported restricted forms are diagnostics at the
modifier/form span. Changing these spellings into hard keywords is an
edition-level compatibility change.

The first executable slice #578 applies only to supported top-level function
declarations and preserves implicit versus explicit private in syntax/HIR. Its
structural function record appends, in order, semantic level, origin, modifier
start/end (`-1|-1` when omitted), declaration start/end, the bounded
single-input `file:0` identity, and a declaration-order `symbol:N` identity.
It does not claim cross-file or package access enforcement.

## Canonical examples

~~~kofun
pub type User = {
    private id: UserId,
    pub name: Text,
}

internal type UserRecord = {
    id: Text,
    name: Text,
}

pub fn create_user(name: Text) -> User {
    validate_name(name)
    return make_user(generate_id(), name)
}

fn validate_name(name: Text) {
    # file-private by default
}

internal fn generate_id() -> UserId {
    # package-visible
}
~~~

A dependent package can reach `User`, `User.name`, and `create_user` through a
public module path. It cannot reach `User.id`, `UserRecord`, `validate_name`,
or `generate_id`.

~~~kofun
module user.service.validation

pub(to user.service) fn validate(user: User) {
}
~~~

The restricted function is visible from `user.service` and descendant modules
but not a sibling module or another package.

## Diagnostics and limits

Access diagnostics carry the use span, declaration span when accessible,
requested visibility, effective visibility, caller `PackageId`/`ModuleId`, and
one safe remedy. Stable limits apply to enclosing depth, restricted path
components, re-export depth, and leaked signature traversal.

Crossing a limit fails before a compiled interface or output artifact is
committed. Imports, generated declarations, macros, and sidecar readers cannot
bypass access by constructing an ID directly.

## Compatibility and implementation status

Changing an omitted declaration from private to public is an intentional API
addition. Removing or narrowing a reachable public declaration is a public
interface change. Internal-only changes affect package consumers but not
external semantic digests. Private implementation edits do not affect
dependent package interfaces.

The reference gate `spec/visibility/check.sh` verifies the normative tables,
basic effective-visibility ordering, public-leak rejection, and ancestor-path
examples. The executable
`tests/conformance/modules/visibility-syntax/run.sh` gate verifies the bounded
top-level function parser/IR slice, contextual identifier behavior, exact
diagnostics, artifact absence, deterministic output, and unchanged runtime
semantics. Neither gate claims resolver enforcement; compilers must continue
to reject unsupported visibility syntax explicitly.

## Non-goals

Protected inheritance access, Java package-private defaults, Kotlin default
public, Go capitalization visibility, arbitrary friend graphs, runtime access
checks, implicit API export, and linker export inference from `pub` are not
part of Kofun's visibility model.
