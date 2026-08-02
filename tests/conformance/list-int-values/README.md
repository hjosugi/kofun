# Bounded `List[Int]` values — corpus (#919)

Gate: `sh tests/conformance/list-int-values/run.sh`, also `task
list-int-values`.

This corpus proves increment 2 of #868: a bounded `List[Int]` reaches the
Stage 2 type namespace, binds as a local, and has its length and its elements
read through checked source → typed HIR → executable C11.

Before this, none of the three ways a program reached that boundary said what
the boundary was. #868 measured all three; this corpus pins each one.

| Source | Reported before | Now |
|---|---|---|
| `let v: List[Int] = [1, 2]` | `E2S35` unknown lexical binding `Int` | binds and runs (`locals.kofun`) |
| `let v = [1, 2]` then `len(v)` | `E2S15` `len` expects `TextOrList`, got `Int` | binds and runs (`inferred.kofun`) |
| `f([1, 2, 3])` | `E2S17` arity `got -1`, then C that did not compile | `E2S148`, naming the boundary (`literal_argument.kofun`) |

The third row is worth stating plainly. After #959 made the literal spannable,
the arity stopped being `-1` — and the call then lowered to `kofun_fn_f()`
against a one-parameter prototype, which is not valid C. A list literal in
argument position is now a refusal with no artifact.

## The representation, and where it comes from

`spec/aggregate-layout-v1/examples/core.x86_64-linux.json` carries the
accepted `List[Int]` layout and its object vectors:

| field | value | source |
|---|---|---|
| `kind` | `list` | `List[Int]` layout |
| `size` / `align` | 8 / 8 | `List[Int]` layout |
| `pointers` | `["0"]` | `List[Int]` layout |
| `length` header offset / size | 0 / 8 (`u64`) | `list-int-three` object |
| `payload_offset` | 8 | `list-int-three` object |
| `element_size` / `element_align` | 8 / 8 | `list-int-three` object |
| `size` for three elements | 32 | `list-int-three` object |

So the value is one pointer, and the pointee is a length header followed by a
dense run of 8-byte elements. Stage 2 emits exactly that:

```c
#define KOFUN_LIST_INT_CAPACITY 64
typedef struct {
    uint64_t length;
    int64_t elements[KOFUN_LIST_INT_CAPACITY];
} KofunListIntObject;
typedef struct {
    const KofunListIntObject *object;
} KofunListInt;
```

The emitted translation unit carries nine `_Static_assert`s pinning the header
offset and size, the payload offset, the element size and alignment, the
object's total size, the pointer offset, and the value's size and alignment.

The gate does not read those numbers from a checked-in copy. It recomputes the
descriptor with `spec/aggregate-layout-v1/layout.mjs` and compares the emitted
C against the result, then compiles the emitted representation on its own and
measures the geometry at run time. A drift on either side — a backend that
narrows the header, or a contract change that moves the payload — fails here
rather than passing quietly.

### Storage and the capacity bound

`KOFUN_LIST_INT_CAPACITY` is the explicit capacity bound scope item 2 asks
for, and it is a quantity of the emitted type rather than a comment: the
object carries its elements inline, so no allocator exists to grow. A literal
of 64 elements binds (`capacity.kofun`); a literal of 65 is refused before any
C exists (`oversized.kofun`).

The object has automatic storage in the declaring frame. The layout entry
records `drop: managed`, which is a statement about the ABI's ownership model;
this slice never runs a drop because it never heap-allocates, and it never
lets the value outlive its frame because it gives the value no way out of one.
Parameters, results, and record fields are increments 3–5 of #868 and are
refused where they are written.

## Bounds

The bound an index is checked against is the declaring literal's own element
count, and the check runs during lowering:

- an index outside the range is refused, with no artifact
  (`index_above_range`, `index_below_range`, `index_empty_list`);
- an index this slice cannot check — anything but an integer literal — is
  refused for that reason (`index_not_literal`).

That is a deliberate reading of #919's acceptance criterion, which asks for
out-of-range indexes to be "refused with stable diagnostics and no partial C
artifact". A runtime trap would have to emit the artifact first. A dynamic
index therefore has no checked form here and says so; it arrives with the
increment that gives a list a way to reach a loop it did not declare.

`len` is not folded from the source: it reads the object's own `u64` header,
which is why the gate refuses a `strlen` on a list value.

## Positives

| Fixture | What it observes |
|---|---|
| `locals.kofun` | empty, one-element and four-element annotated locals; every index of the four; an index read inside arithmetic; negative elements |
| `inferred.kofun` | an unannotated literal binding as `List[Int]`, counted and indexed; elements that are expressions over another binding |
| `capacity.kofun` | the capacity bound is reachable: 64 elements, counted, first and last read |

Every positive is compiled twice and from a second directory to prove the C is
byte-identical, compiled under `-std=c11 -Wall -Wextra -Werror -pedantic`, run
twice, and compared against `bin/kofun run` — the reference executor — byte
for byte.

## Refusals

| Fixture | Code | Rule |
|---|---|---|
| `oversized.kofun` | `E2S148` | 65 elements exceeds the capacity bound |
| `index_above_range.kofun` | `E2S148` | `values[3]` on three elements |
| `index_below_range.kofun` | `E2S148` | `values[-1]` |
| `index_empty_list.kofun` | `E2S148` | `values[0]` on an empty list |
| `index_not_literal.kofun` | `E2S148` | an index this slice cannot check |
| `text_element.kofun` | `E2S148` | a `Text` in a `List[Int]` literal |
| `text_annotation.kofun` | `E2S148` | `List[Text]`, an element type this slice does not carry |
| `literal_argument.kofun` | `E2S148` | a literal in argument position |
| `mutable_binding.kofun` | `E2S148` | `let mut` on a list |
| `whole_value_use.kofun` | `E2S148` | a list read as a whole value |
| `list_parameter.kofun` | `E2S15` | `List[Int]` in parameter position |
| `list_result.kofun` | `E2S15` | `List[Int]` in result position |

Every refusal exits 1, writes nothing to internal stderr, and leaves no `.c`
behind.

The last two carry `E2S15` rather than `E2S148` on purpose. They are increment
3 of #868, and each already states its own boundary truthfully in the position
it stands at — the same `E2S15` `examples/coding_interview.kofun` is pinned
to. They are in this corpus so that increment 3 moving them is visible.

## What stays out

`List[Int]` in parameter and result position (increment 3), `Text` record
fields (increment 4), `List[Int]` record fields (increment 5), `List[Text]`,
lists of records, and any nested aggregate. #868 stays open until all five
increments land.
