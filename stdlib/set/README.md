# Set checkpoint

This directory defines the first platform-independent standard-library
checkpoint for `Set`. It is a bounded reference surface, not completion of the
generated 20-step lifecycle in issue #510.

## Set v1 surface

[`set.kofun`](set.kofun) exposes an eager `SetInt` API:

- `set_int_empty`, `set_int_from_list`, and `set_int_to_list`;
- `set_int_length`, `set_int_is_empty`, and `set_int_contains`;
- value-returning `set_int_insert` and `set_int_remove`;
- `set_int_union`, `set_int_intersection`, `set_int_difference`,
  `set_int_is_subset`, and `set_int_equal`; and
- deterministic `set_int_map`, `set_int_filter`, and `set_int_fold` pipelines.

The canonical representation is an ascending List with no duplicates. Public
constructors restore that invariant, duplicate insertion and missing removal
are no-ops by value, and every operation is total for every `Int`. No integer
is reserved as a sentinel and this v1 surface has no invalid-input error case.
`set_int_to_list` and `set_int_fold` expose ascending order so repeated runs and
equivalent construction histories have the same observable traversal.

The API treats the `SetInt` record as opaque, but the current compiler does not
yet enforce module visibility. Direct record construction can violate the
invariant and is outside this checkpoint's supported API.

## Costs and ownership

This deliberately clear reference version uses linear List scans and insertion
into an immutable value. Length and emptiness are O(1); membership, insertion,
removal, snapshot, filtering, and folding perform O(n) element work.
Construction and mapping perform O(n^2), union performs O(m(n + m)), and
intersection, difference, subset, and equality perform O(nm). Under the List
checkpoint's conservative O(k) `push` cost, insertion and removal allocate up
to O(n^2), construction and mapping up to O(n^3), union up to
O(m(n + m)^2), and intersection/difference up to O(nm + n^2). Snapshot and
filter use the List checkpoint's O(n) built-ins. These are correctness-first
bounds, not performance targets. No hash-table, balanced-tree, amortization,
or storage-identity promise is made by this checkpoint.

Inputs are read-only and remain unchanged. `SetInt` is specialized to Copy
`Int`, so element return and callback ownership do not pretend that borrowing
and hashing/equality for generic, non-Copy values are already defined. Returned
sets are independent values under Kofun's collection value model.

## Executable boundary

The active compiler cannot yet lower the canonical record module and its
general List-building code, so the gate does not claim that `set.kofun` is
importable today. [`tests/projection.kofun`](tests/projection.kofun) executes
membership, duplicate insertion, missing removal, union, intersection,
difference, subset, length, algebraic identities, and ascending traversal over
the finite universe `0..7`. It runs through both C11 and direct x86-64 with
byte-identical output.

The fixture's Int bit mask is only an executable semantic projection; it is
not the public representation and does not limit `SetInt` values to `0..7`.
Keeping that distinction explicit avoids claiming backend-native Set storage
before it exists.

## Remaining lifecycle work

Generic equality/hash constraints, non-Copy and affine elements, an optimized
tree or hash representation, edit/capacity APIs, lazy iterators, native Set
lowering, parallel and SIMD execution, serialization, foreign interoperability,
property and fuzz suites, performance baselines, tooling views, allocation
failure policy, enforced representation privacy, and release stabilization
remain open.

Run the Python-free focused gate with:

```sh
sh stdlib/set/tests/verify.sh
```
