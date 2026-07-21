# BinaryHeap checkpoint

This directory defines the first platform-independent standard-library
checkpoint for `BinaryHeap`. It is a bounded reference surface, not completion
of the generated 20-step lifecycle in issue #515.

## BinaryHeap v1 surface

[`binary_heap.kofun`](binary_heap.kofun) exposes a min-heap specialized to
`Int`:

- `binary_heap_int_empty` and `binary_heap_int_from_list` construct heaps;
- `binary_heap_int_length` and `binary_heap_int_is_empty` inspect size;
- `binary_heap_int_peek_min` returns the minimum without changing the heap;
- value-returning `binary_heap_int_push` inserts one value;
- `binary_heap_int_pop_min` returns both the minimum and remaining heap; and
- `binary_heap_int_to_sorted_list` is the deterministic traversal and eager
  pipeline bridge.

Duplicates are preserved. `peek_min` and `pop_min` return the typed
`BinaryHeapEmpty` failure for an empty heap, so no `Int` is reserved as a
sentinel. The binary-tree invariant is `parent <= child`; equal values have no
identity or stability guarantee. Deterministic traversal means ascending
value order, independent of insertion history. The internal level-order List
is intentionally not a public iteration order.

The API treats the `BinaryHeapInt` record and its helper functions as module
private, but the current compiler does not yet enforce visibility. Direct
record construction can violate the invariant and is outside this checkpoint's
supported API.

## Costs and ownership

The sift algorithm performs O(log n) comparisons and swaps for insertion and
removal, and `peek_min`, length, and emptiness are O(1). Construction by
successive insertion and ascending traversal by successive removal perform
O(n log n) comparisons. This correctness-first value API copies or rebuilds a
List before mutation. With the List checkpoint's conservative O(k) `push`
cost, heap insertion performs O(n) total work, removal O(n^2), construction by
insertion O(n^2), and ascending traversal O(n^3). These bounds include copying
and allocation, not only comparisons. The checkpoint does not claim amortized
vector growth, an O(n) Floyd build, structural sharing, or storage identity.

Inputs are read-only and remain unchanged. Internal `edit` helpers receive only
fresh Lists owned by the operation. Version 1 is intentionally specialized to
Copy `Int`, avoiding unsupported claims about borrowing, moving, or comparing
generic and affine elements. Allocation failure policy remains outside the
currently expressible platform-independent collection API.

## Executable boundary

The active compiler cannot yet lower the canonical record, ADT, and general
List-mutation module, so the gate does not claim that `binary_heap.kofun` is
importable today. [`tests/projection.kofun`](tests/projection.kofun) executes
the observable peek, push, pop, duplicate, empty, and ascending-drain rules,
plus the parent/child and sift comparison choices, through both C11 and direct
x86-64 with byte-identical output.

The projection uses two finite bit planes for fixture values `0..7` and checks
parent/child and sift choices separately. That is only an executable semantic
model; the two-occurrence fixture bound, representation, range, and capacity
are not restrictions on the public heap.

## Remaining lifecycle work

Generic comparator constraints, max-heaps and custom ordering, affine
elements, optimized contiguous storage, O(n) heapify, lazy iteration,
decrease-key handles, stable equal-priority ordering, parallel and SIMD
execution, serialization, foreign interoperability, property and fuzz suites,
performance baselines, tooling views, enforced representation privacy, and
release stabilization remain open.

Run the Python-free focused gate with:

```sh
sh stdlib/binary_heap/tests/verify.sh
```
