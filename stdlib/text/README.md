# Text reference API

`utf8.kofun` is the pure-Kofun reference implementation for Issue #484. It
defines the correctness contract that the built-in `Text` representation and
optimized kernels must preserve.

## Public model

`Utf8Text` is an immutable, validated sequence of UTF-8 bytes. Construction is
only through `text_from_utf8`, which returns `Result[Utf8Text, Utf8Error]`.
There is no replacement-character fallback and no negative sentinel.

| operation | result | complexity |
|---|---|---|
| `text_byte_length` | encoded byte count | O(1) |
| `text_length` | Unicode scalar/code-point count | O(1) |
| `text_at` | code point at a code-point index | O(n) |
| `text_chars` | code points with byte offsets and widths | O(n²) reference |
| `text_concat` | validated concatenation | O(left bytes + right bytes) |
| `text_equal` | canonical byte equality | O(n), early exit |
| `text_utf8_bytes` | immutable encoded bytes | O(1) shared view |

An index is a zero-based Unicode scalar index, never a byte offset. `text_at`
returns `Err(IndexOutOfBounds(index, length))` for a negative index or for
`index >= text_length(value)`.

`text_length` does not count grapheme clusters, terminal cells, UTF-16 code
units, or normalized characters. For example Hindi `नमस्ते` has six Unicode
scalars even though user-perceived character segmentation is different.
Normalization and locale-sensitive comparison are deliberately outside this
reference layer. Equality compares the exact validated UTF-8 representation.
The current list-only reference builds `text_chars` without a mutable builder,
so that operation is intentionally O(n²); the built-in implementation must use
a linear builder while preserving the same output.

## Validation

The decoder accepts only Unicode scalar values encoded in shortest-form UTF-8:

- ASCII `00..7f`;
- two-byte sequences with leads `c2..df`;
- three-byte sequences excluding overlong forms and `U+d800..U+dfff`;
- four-byte sequences through `U+10ffff`.

It rejects bytes outside `0..255`, isolated continuation bytes, illegal lead
bytes, missing continuation bytes, overlong encodings, surrogate scalars, and
values above `U+10ffff`. Every error records the source byte offset.

Input and stored lists are immutable managed values. Construction may share the
list without violating observable behavior. `text_concat` allocates one new
list. `CodePoint` values are copy values. The `Utf8Text` record constructor and
its fields are module-private API; exposing opaque constructors is a required
part of wiring this reference to built-in `Text`.

## Current integration boundary

The source uses records, ADTs, lists, and matching that the integer-only Stage 1
frontend does not yet lower. `tests/text_utf8_native.packed.kofun` is an audited
Stage1-Core lowering of the strict scanner. The repository verifier compiles
that Kofun source, reconstructs its ELF bytes, executes validation over ASCII,
Japanese, Arabic, Hindi, emoji, and malformed fixtures, and checks the observed
counts. It is an executable gate, not a source-only assertion.

Still open from Issue #484:

- wiring `Utf8Text` to the compiler's built-in `Text` storage;
- grapheme segmentation, normalization, case mapping, search, and slicing;
- iterator/pipeline adapters;
- SIMD, parallel, interop, fuzz, benchmark, and tooling lifecycle items.
