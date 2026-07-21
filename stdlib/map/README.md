# Map checkpoint

This directory defines a bounded, platform-independent reference checkpoint
for `Map`. It does not claim completion of the generated 20-step lifecycle in
issue #509.

## Map v1 surface

[`map.kofun`](map.kofun) exposes an immutable `IntMap` whose keys and values
are both `Int`:

- `map_int_empty`, `map_int_entry`, `map_int_from_entries`, and
  `map_int_to_entries`;
- `map_int_length`, `map_int_is_empty`, `map_int_contains_key`, and
  `map_int_get`;
- value-returning `map_int_insert` and `map_int_remove`;
- deterministic `map_int_keys`, `map_int_values`, `map_int_map_values`,
  `map_int_filter`, and `map_int_fold` pipelines.

The canonical representation is an ascending-key List with exactly one entry
per key. `map_int_insert` is an upsert: an existing key is replaced in place by
key order. `map_int_from_entries` applies entries from first to last, so the
last value for a repeated key wins. Missing removal is a no-op by value.

`map_int_get` returns `Err(MapKeyMissing(key))` when no binding exists. It does
not reserve an `Int` value as a sentinel. Every snapshot and callback traverses
keys in ascending order, so equivalent maps have identical observable order
regardless of insertion history. The API treats `IntMap.entries` as opaque,
although the current compiler does not yet enforce module visibility; direct
record construction that violates the invariant is unsupported.

## Costs and ownership

This correctness-first version uses linear List scans. Length and emptiness are
O(1). Lookup, membership, removal, snapshots, mapping, filtering, and folding
perform O(n) element work. Insertion performs O(n), and construction performs
O(n^2) element work. Under the List checkpoint's conservative O(k) `push`
cost, a value-returning insertion, removal, snapshot, map, or filter may
allocate O(n^2), while construction may allocate O(n^3). No hash table,
balanced tree, amortization, storage identity, or iteration-in-insertion-order
promise is made.

Inputs are read-only and remain unchanged. Specializing both sides to Copy
`Int` keeps returned values and callback ownership precise while generic key
equality/order, hashing, and affine-value behavior remain unspecified.

## Executable boundary

The active compiler cannot yet lower the canonical record and typed-result
module. The focused gate records that boundary instead of claiming that
`map.kofun` is importable today.

[`tests/projection.kofun`](tests/projection.kofun) executes lookup outcomes,
upsert replacement, missing removal, length, filtering, value mapping, and
ascending entry traversal through both C11 and direct x86-64 with byte-identical
output. Its separate presence/value scalars cover only fixture keys `1`, `3`,
and `5`; they are an audited scalar projection, not the public representation
or a restriction on `IntMap` keys and values.

## Remaining lifecycle work

Generic key/value parameters and constraints, non-Copy and affine values, an
optimized tree or hash representation, mutable entry APIs, lazy iterators,
native Map lowering, parallel and SIMD execution, serialization, foreign
interoperability, property and fuzz suites, performance baselines, tooling
views, allocation-failure policy, enforced representation privacy, and release
stabilization remain open.

Run the Python-free focused gate with:

```sh
sh stdlib/map/tests/verify.sh
```
