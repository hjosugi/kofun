# Semantic namespaces and lookup

Status: accepted normative design for GitHub issue #290.

This document assigns every named declaration to one of four semantic
namespaces and fixes syntax-directed lookup, duplicate, import, re-export,
diagnostic, and identity behavior. It does not make capitalization semantic.

The words **must**, **must not**, **should**, and **may** are normative.

## Decision

Kofun uses exactly four module-level namespace kinds:

| Stable tag | Name | Purpose |
| ---: | --- | --- |
| 0 | value | runtime values and value-producing declarations |
| 1 | type | types, traits, and type-level parameters |
| 2 | module | semantic modules and imported module qualifiers |
| 3 | meta | explicitly invoked compile-time declarations |

This accepts option C from #290. One unified namespace rejects common
type/value reuse; two namespaces leave modules and macros ad hoc; a namespace
per declaration kind makes lookup and tooling too fragmented. Four
syntax-directed namespaces give every symbol an identity domain while keeping
use-site selection teachable.

The tag order is canonical serialization order, not lookup precedence. A new
namespace kind requires a new schema domain or compatible schema extension; an
implementation cannot assign a local fifth tag.

## Namespace identity

The canonical input is:

~~~text
kofun.namespace-id/v1
tag=<0|1|2|3>
name=<value|type|module|meta>
~~~

Both fields must agree. Unknown tags/names and duplicate fields are errors.
`SymbolId` under #303 includes the complete `NamespaceId`; lowering and tools
never infer a namespace from spelling or declaration capitalization.

## Declaration assignment

Every named declaration maps to exactly one namespace:

| Declaration/binding kind | Namespace | Notes |
| --- | --- | --- |
| local binding, parameter, pattern binding | value | lexical scope under #110/#112 |
| function, constant, immutable/mutable global | value | overload sets are not implied |
| enum/ADT constructor | value | its result names the owning type |
| field, method, associated function/constant | value | inside a member scope keyed by owning type/trait |
| nominal type, enum/ADT type, record/class type | type | nominal identity is not structural equality |
| type alias, type parameter, associated type | type | scoped by its declaration owner |
| trait/interface/protocol | type | methods remain member-scope values |
| module declaration, imported module qualifier | module | a module is not a runtime value |
| macro, meta function, named compile-time declaration | meta | invocation syntax must be explicit |
| named law/proof declaration | meta | evidence identity is refined by its owning contract |

An implementation block is indexed by `ImplementationId` under #403 and is
not a separately lookupable name. Effect labels, lifetime regions, labels, and
unnamed proof terms are not module declarations; their owning specifications
must define bounded local subdomains without adding module-level namespaces.

## Same spelling and duplicates

A scope may contain at most one binding with a given normalized spelling in
each namespace. The same spelling may coexist in different namespaces because
the use site selects a namespace.

| Pair in the same declaration scope | Result |
| --- | --- |
| value/value | reject |
| type/type | reject |
| module/module | reject |
| meta/meta | reject |
| value/type | allow |
| value/module | allow with the dotted-path rule below |
| type/module | allow with the dotted-path rule below |
| value/meta, type/meta, module/meta | allow; meta invocation is explicit |
| constructor/function | reject; both are value |
| trait/type alias | reject; both are type |

Duplicate diagnostics name the namespace and both declarations in canonical
`SymbolId` order. Source order, import order, path spelling, and hash-map order
cannot choose a winner. Lexical shadowing of an ancestor value is #112; it does
not permit two same-scope value bindings and does not hide other namespaces.

## Syntax-directed lookup

Single-segment lookup selects exactly one namespace:

| Syntactic position | Namespace |
| --- | --- |
| expression, callee, value pattern | value |
| type annotation, generic argument/bound, trait position | type |
| import target or module declaration path component | module |
| explicit macro/meta/law invocation | meta |

Constructor patterns search value and then verify that the result is a
constructor of the expected type. Trait bounds search type and then verify the
resolved kind is a trait. Kind validation is not a second namespace lookup.

HIR references contain `NamespaceId` and `SymbolId`. A failed lookup reports
the requested namespace; it must not silently retry another namespace because
a same-spelled declaration exists there.

## Dotted paths and member access

Kofun uses `.` for both module qualification and member access. Resolution is
deterministic:

1. for an identifier-led dotted chain, look for the first segment in the
   module namespace of the current file;
2. if found, the chain is module-qualified and intermediate segments resolve
   only as modules; the final segment uses the surrounding value/type/meta
   position;
3. if no module binding exists, resolve the first segment in the surrounding
   value or type namespace and continue as member/associated lookup;
4. no candidate causes a retry after a module binding was selected.

Thus a module qualifier and a value/type may share a bare spelling, but the
module interpretation owns an identifier-led dotted chain. Tooling must warn
when that coexistence makes member syntax surprising and offer a qualified or
renamed import; it must not change resolution. #283 may add an explicit module
alias. No casing convention or filesystem path breaks a tie.

## Qualified lookup

Once a module chain resolves, the final segment searches only the namespace
implied by its syntactic position. For example, `math.Vector` in a type
annotation searches the type exports of `math`, while `math.Vector(...)` in an
expression searches its value exports. A module may export a type and value
with the same name; both uses remain deterministic.

Visibility is checked on the selected target before the HIR reference becomes
usable. An inaccessible value does not fall back to a same-spelled type, module,
or meta declaration. Imports and aliases retain the target `NamespaceId`.

## Selective imports

A namespace-polymorphic selective request such as:

~~~kofun
from result import Result
~~~

binds every accessible exported target named `Result`, with at most one target
per namespace. If the module exports a value and type named `Result`, the
request creates two distinct `ImportBindingId` records. Expression and type
use sites select them without an extra selector.

Zero matching accessible targets is an error. Two candidates in one namespace
are an invalid exporting interface and fail; source/import order cannot choose
one. Module and meta targets follow the same rule when they are exportable.
Tooling displays the complete matched namespace set and offers namespace-aware
completion. A future explicit selector may narrow a request but cannot change
the meaning of this unqualified form.

## Re-exports

A re-export preserves each target `NamespaceId`, `SymbolId`, visibility, and
provenance. Re-exporting the selective request above creates one export binding
per matched namespace. It cannot merge identities, widen visibility, or
replace a collision in the same namespace. #287 owns forwarding syntax and
effective visibility; this document fixes only namespace behavior.

## Diagnostics and tooling

Every lookup, duplicate, import, and re-export diagnostic includes:

- requested or colliding namespace name;
- use span and accessible declaration spans;
- stable declaration identities where safe to disclose;
- the syntactic position that selected the namespace; and
- one namespace-aware remedy.

A diagnostic may mention that another namespace contains the spelling, for
example “type `Result` exists, but a value was required.” It must not accept it
or reveal an inaccessible dependency declaration. LSP definition, references,
rename, hover, and completion key data by `(NamespaceId, SymbolId)`, never by
text alone. Renaming a type does not rename a same-spelled value unless both
identities are explicitly selected.

## Ordering, limits, and compatibility

Namespace-bearing maps and sets serialize by namespace tag and then stable
identity bytes. A selective import can create at most four bindings because
there are four namespace kinds; broader source/import budgets belong to #114.
Unknown namespace schemas fail before an interface, HIR, object, executable,
or cache success is committed.

Changing a declaration to a different namespace is a remove-plus-add semantic
API change, even when its spelling is unchanged. Changing capitalization alone
has only the ordinary identifier effect and never moves a namespace.

## Implementation status and non-goals

`spec/namespaces/check.sh` verifies the canonical namespace identities,
declaration mapping, duplicate matrix, syntax-directed lookup, dotted-path
precedence, selective value/type bindings, and stable ordering. The active
compiler does not yet implement general module imports or all declaration
kinds, so the gate is decision evidence rather than resolver evidence.

This contract does not define overload resolution, member lookup details,
extension precedence, import/re-export syntax, visibility, lexical shadowing,
macro hygiene, or the production `SymbolId` digest. No namespace-separation
question remains open after this decision.
