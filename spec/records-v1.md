# Nominal heterogeneous records v1

Status: accepted normative design for GitHub issue #546. The bounded frontend
under `tests/conformance/records/` implements and gates this document; no
production compiler path, module system, or backend lowers records yet.

Decision owner: the repository maintainer. A later change to the accepted
declaration form, construction form, ownership boundary, or layout authority
requires a separately reviewed, versioned specification change.

The words **must**, **must not**, **should**, and **may** are normative.

## Decision

Kofun v1 records use exactly these three forms:

~~~kofun
type Token = {
    kind: TokenKind,
    text: Text,
    start: Int,
}

let token = Token(kind: Identifier, text: "name", start: 0)
let text = token.text
~~~

- `type Name = { ... }` is the only nominal record declaration. `record Name
  { ... }` is **not** added as a second declaration family or keyword.
- `Name(field: value, ...)` is the only direct construction spelling.
  `Name { ... }` is rejected.
- `value.field` is the only field read.

The abandoned `record`/brace-construction experiment described in #546 is prior
art only and creates no compatibility obligation.

### Why the call form

Parenthesized labelled construction is unambiguous beside blocks, map
literals, ADT declarations, control-flow conditions, and loop iterables, and it
needs no parser-context suppression:

| Source | Meaning |
|---|---|
| `if flag { ... }` | `if` condition followed by a block |
| `while index < limit { ... }` | `while` condition followed by a block |
| `for item in items { ... }` | loop iterable followed by a block |
| `{ "a": 1 }` | reserved for the map literal decision in #52/#624 |
| `Present(42)` | flat ADT constructor application |
| `Token(kind: k, text: t, start: 0)` | record construction |

A `{` therefore never introduces record construction, and an expression never
begins with `{`. This rule is what keeps the map-literal question open without
blocking records: whatever #52/#624 selects for maps and tuples, it cannot
collide with a construction form that has no braces.

Implementations **must** reject `Name { ... }` in expression position with a
diagnostic that names the accepted form rather than a generic parse error.

## Declaration

A record declaration binds one nominal type name in the type namespace and
declares one or more fields in source order.

- A record **must** declare at least one field.
- Field names **must** be unique within the declaration.
- Trailing commas are accepted; the formatter is canonical for delimiter and
  spacing choices and **must not** reorder fields.
- Declaration order is the record's field order for layout, drop order, and
  every generated listing. It is part of the declared contract, so reordering
  fields is a semantic change even though it does not change the field set.
- Generic record declarations (`type Box[T] = { ... }`) are **not** accepted in
  v1 and **must** be rejected explicitly rather than silently ignored.
- A record **must not** be structurally recursive in v1, directly or through
  another record, because v1 has no indirection form to break the cycle.

## Construction

Construction is a call on the record's type name with every argument labelled
by a declared field name.

- Every declared field **must** be supplied exactly once. There are no default
  values, no inference of a missing field, and no partial construction.
- Field arguments **may** be written in any order.
- Argument expressions **must** evaluate exactly once, left to right in
  *written* order. Storage follows *declaration* order. A construction written
  out of order therefore produces the same value shape as one written in
  order, and the evaluation order remains the visible source order.
- Mixing labelled and positional arguments is rejected, and a fully positional
  record construction is rejected. Labels are what distinguish construction
  from an ordinary function call, so a labelled argument to a function is also
  rejected.
- Direct construction **must** be forbidden outside the visibility boundary of
  any required field that is not accessible there. A public factory function is
  the remedy; v1 has no privileged construction escape hatch.

## Field access

`value.field` resolves against the declared nominal type of `value` and always
has the declared field type. There is no dynamic field lookup, no structural
fallback, and no field access on a non-record value.

## Identity and visibility

- Record identity is **nominal** and includes package, module, and declaration
  identity. Two records with equal field names and types are different types.
- Nominal identity **must not** depend on declaration order within a file, so
  a record may be used before its declaration is read.
- Visibility follows `modules/visibility.md` independently for the type and for
  each field. A `pub` record does not publish its fields.
- Field access **must not** bypass field visibility, and neither may any
  future pattern destructuring form.

## Ownership, mutation, and drop

- Fields are **immutable** in v1. Field assignment, `edit` access to a record,
  spread/update sugar, and in-place record mutation are unsupported and
  **must** be rejected explicitly rather than accepted and ignored.
- `read record.field` borrows the selected non-`Copy` field for the enclosing
  non-escaping view. Selecting a `Copy` field produces its value under the
  ordinary `Copy` rule.
- `take record` transfers the whole record. Partial moves such as
  `take record.field` are **rejected** in v1, so no partially initialized or
  partially dropped record state exists.
- A record containing an affine owned field is itself affine. Dropping a whole
  record drops its owned fields in reverse declaration order. Ordinary managed
  fields keep managed-value semantics.
- Pattern destructuring waits for a separate ownership-aware design.

## Representation

Typed IR records nominal identity, declaration-order fields, field types,
visibility, ownership/drop class, and source spans. It **must not** hard-code
backend offsets.

The untagged byte layout is computed only through the target-parameterized
aggregate layout contract selected in #120. Layout evidence **must** name the
target data-layout inputs it used; a layout claim without them is not evidence.

Normative layout facts for v1:

- a record value carries **no tag, header, or discriminant**: its bytes are its
  fields plus alignment padding only;
- fields are placed in declaration order, each at the next offset that
  satisfies its alignment;
- the record's alignment is the maximum field alignment, minimum 1, and its
  size is the field extent rounded up to that alignment;
- the layout is a pure function of the field types and the target data layout,
  so it is deterministic and reproducible.

There is **no** stable public or FFI ABI promise in v1. `repr(C)` and the C ABI
boundary remain owned by their own contracts.

For the LP64 profile shared by `x86_64-linux` and `aarch64-linux` — `Int` 8/8,
pointer 8/8, `Bool` 1/1, payload-free enum tag 1/1, and `Text`/`List` as a
(pointer, length) pair — the Token record above is:

| Field | Type | Offset | Size | Align |
|---|---|---|---|---|
| `kind` | `TokenKind` | 0 | 1 | 1 |
| `text` | `Text` | 8 | 16 | 8 |
| `start` | `Int` | 24 | 8 | 8 |

with size 32 and alignment 8 on both targets. The two targets agree here
because their named data-layout inputs agree, not by assumption: the gate
recomputes the comparison from the emitted per-target rows.

## Grammar

~~~ebnf
type_decl        = "type", identifier, "=", record_body ;
record_body      = "{", separators,
                   record_field, { separators, ",", separators, record_field },
                   [ separators, "," ], separators, "}" ;
record_field     = identifier, ":", type_ref ;
record_construct = nominal_type, "(", labelled_arguments, ")" ;
labelled_arguments = [ labelled_argument,
                     { separators, ",", separators, labelled_argument },
                     [ separators, "," ] ] ;
labelled_argument = identifier, ":", expression ;
~~~

`record_construct` is a `call` whose arguments are all labelled; the parser
does not need a separate production to disambiguate it, and no expression
production begins with `{`.

## Diagnostics

Each condition below is a distinct diagnostic. An implementation **must not**
merge them into one generic error.

| Condition | Bounded gate code |
|---|---|
| duplicate type or function name | `E2S107` |
| duplicate field in a declaration | `E2S108` |
| unknown field, parameter, or result type | `E2S109` |
| generic record declaration | `E2S110` |
| recursive record declaration | `E2S111` |
| unknown name or record type | `E2S112` |
| duplicate field in a construction | `E2S113` |
| missing field in a construction | `E2S114` |
| unknown field in a construction | `E2S115` |
| wrong field type in a construction | `E2S116` |
| positional construction, or labels on a call | `E2S117` |
| `Name { ... }` brace construction | `E2S118` |
| unknown field read, or a read on a non-record | `E2S119` |
| field assignment or `edit` access | `E2S120` |
| partial move | `E2S121` |
| use after `take` | `E2S122` |
| other type or arity mismatch | `E2S123` |
| `{` in expression position, including a map literal | `E2S124` |

The codes above are the bounded frontend's stable identifiers, registered in
`tests/diagnostics/registry.tsv`. A production frontend inherits the
*conditions*, not necessarily the code numbers.

Inaccessible-field construction is specified above but has no code yet: the
bounded gate is single-file and has no module or visibility boundary to cross.
It is owned by the integration of this contract with `modules/visibility.md`.

## Non-goals for v1

Tuples (#52), derive, structural rows, default field values, methods, generic
records, field mutation, spread/update sugar, pattern destructuring, a stable
public ABI, and map/set literals are follow-ups. None of them may be assumed by
code written against this document.

This contract is **not** a prerequisite for the record-free first
string-scanning fixed point tracked by #618–#622; that profile stays record
free unless the frozen compiler profile explicitly adopts records.

## Executable evidence

`tests/conformance/records/run.sh`, wired as `make records`, gates this
document. It proves, on the current target branch:

- a Token-shaped record with `TokenKind`, `Text`, and `Int` fields constructs,
  passes, returns, and reads correctly, driven by a working scanner that
  produces and consumes a `List[Token]`;
- written field order is free while storage follows declaration order;
- nominal record and field identities are independent of declaration order;
- layout is untagged, declaration ordered, and identical on the two named LP64
  target profiles, recomputed from per-target rows;
- blocks, conditions, loop iterables, list literals, and flat constructors stay
  separable from record construction; and
- each diagnostic above fires with an exact message, span, and exit status,
  and no artifact survives a rejected source.

It deliberately does not prove: module-level visibility, generic records,
native or C11 lowering of record values, or an ABI. Those remain open.
