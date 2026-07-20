# Immutable `List[Int]` reference API

`reference.kofun` is the pure-Kofun correctness implementation for Issue #8.
`ImmutableIntList` is a validated immutable sequence; construction and every
copying operation return a new logical value rather than exposing mutation.

## Layout and errors

The native v1 layout is fixed:

```text
+0   Int64 length
+8   Int64 items[length]
```

There is no public capacity field. Mutable builders are a separate future
concept. Storage occupies `align16(8 + 8 * length)` bytes.
`MAX_INT_LIST_LENGTH` is `1152921504606846973`, the greatest length whose
aligned allocation size fits a positive signed Int64.

All boundaries are zero-based and use typed errors:

- `InvalidLength(length)` rejects negative length arithmetic;
- `IndexOutOfBounds(index, length)` rejects negative indices and
  `index >= length`;
- `SliceOutOfBounds(start, end, length)` rejects negative, reversed, or
  past-end `[start, end)` ranges;
- `LengthOverflow(left, right)` reports concatenated length overflow;
- `AllocationSizeOverflow(length)` reports a length that cannot fit the v1
  aligned allocation.

The native diagnostic for `IndexOutOfBounds` is exactly
`error[R023]: List index out of bounds\n`. The native reference reports length
overflow as `error[R024]: List length overflow\n`.

## Operations

| operation | result | boundary |
|---|---|---|
| `int_list_validate` | validated immutable list | checks representable v1 allocation |
| `int_list_length` | element count | O(1) target contract |
| `int_list_get` | one `Int` | typed bounds error |
| `int_list_slice` | copied `[start, end)` | typed range error |
| `int_list_concat` | concatenated value | checked length sum |
| `int_list_equal` | structural equality | exact Int comparison |
| `int_list_map` | transformed immutable list | preserves length |
| `int_list_filter` | retained immutable list | preserves order |
| `int_list_fold` | accumulated `Int` | left-to-right |

The list-only reference uses repeated immutable concatenation and is therefore
O(n²) for copied operations. A built-in implementation must use linear builders
while preserving the same values, order, layout, and errors.

## Executable gate

The active Stage 1 frontend does not lower records, ADTs, higher-order
functions, or general lists. `tests/list_native.packed.kofun` is therefore an
audited Stage1-Core native reference. Its Kofun-authored ELF validates the v1
layout and executes get, slice, concat/equality, map, filter, and fold over
`[1, 2, 3, 4, 5]`. Exact stdout is:

```text
3
15
20
6
```

Separate executions observe the exact `R023` bounds path and `R024` length
overflow path. The shell verifier only transports Kofun-emitted byte words,
checks the image, executes it, and compares results.

Still open:

- generic `List[T]` built-in storage and compiler wiring;
- general Stage 2/native lowering of List operations and callbacks;
- ownership, mutable builders, heap growth, and reclamation/GC;
- specialized kernels, iterators, SIMD, parallelism, property tests, fuzzing,
  and benchmarks.
