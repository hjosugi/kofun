# Bytes reference API

`reference.kofun` is the pure-Kofun correctness implementation for Issue #485.
`BytesValue` is an immutable validated sequence of integers in `0..255`.
Construction never truncates or wraps an invalid element.

## API and boundaries

| operation | result | boundary |
|---|---|---|
| `bytes_from_list` | validated `BytesValue` | rejects the first value outside `0..255` |
| `bytes_length` | byte count | O(1) |
| `bytes_get` | one `Int` byte | zero-based; negative and `index >= length` are errors |
| `bytes_slice` | copied `[start, end)` | negative, reversed, or past-end ranges are errors |
| `bytes_concat` | concatenated value | returns typed length overflow |
| `bytes_equal` | exact byte equality | no coercion or timing guarantee |
| `bytes_read_uint_le` | unsigned little-endian `Int` | widths `1..7` only |
| `bytes_write_uint_le` | immutable updated value | bounds and unsigned range checked |
| `bytes_read_i64_le` | signed two's-complement `Int64` | exactly 8 bytes |
| `bytes_write_i64_le` | immutable updated value | exactly 8 bytes |

Unsigned width 8 is not exposed because Kofun's current `Int` is signed Int64
and cannot represent `2^63..2^64-1`. The full-width API therefore uses signed
two's-complement Int64. A future `UInt64` or big integer type may add an
unsigned eight-byte result without changing these operations.

Signed writes use Kofun's specified floor `//` and non-negative floor `%`;
repeated division by 256 therefore emits the exact two's-complement bytes for
negative Int64 values without overflowing through an unsigned intermediate.

All offsets and slice ends are checked before arithmetic. Span checks use
`width <= length - offset`, avoiding `offset + width` overflow. Unsigned writes
return `UnsignedOutOfRange(value, width)` instead of truncating high bits.
Concatenation returns `LengthOverflow(left, right)` rather than overflowing its
length. `bytes_checked_length_sum` exposes that arithmetic contract directly so
the overflow boundary is testable without allocating an impossible list.

`BytesValue`'s constructor and backing list are module-private API. Immutable
reads may share storage; slice, concat, and write operations return new values.
The list-only reference uses repeated immutable concatenation and is therefore
O(n²) for copied operations. The built-in implementation must provide linear
copying while preserving these results and errors.

## Executable gate

The active Stage 1 frontend does not lower records, ADTs, or lists.
`tests/bytes_native.packed.kofun` is an audited Stage1-Core native lowering. Its
Kofun-authored ELF validates integer byte lists and executes length/get, slice,
concat, equality, unsigned little-endian read/write, and negative bounds/range
cases. `tests/verify.sh` reconstructs and executes that image; the Bytes gate is
not source-only.

Still open from Issue #485:

- built-in `Bytes` storage and Stage 2 wiring;
- zero-copy views and iterator/pipeline adapters;
- mutable builder and specialized fixed-width kernels;
- SIMD, parallel, interop, property, fuzz, benchmark, and tooling lifecycle
  work.
