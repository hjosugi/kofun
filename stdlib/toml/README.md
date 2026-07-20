# TOML flat-document codec checkpoint

[`toml.kofun`](toml.kofun) defines a small, deterministic TOML 1.0-compatible
profile. It parses a complete `Text` value into an ordered `TomlDocument` and
renders that value to canonical TOML text.

```kofun
let configuration = match toml_parse(
    "title = \"Kofun\"\nworkers = 4\nstrict = true",
) {
    Ok(document) => document,
    Err(error) => return Err(error),
}
let canonical = toml_render(configuration)
```

The checkpoint accepts one flat key/value assignment per physical line. Keys
may be bare or single-line basic/literal quoted keys. Values are single-line
basic/literal `Text`, signed decimal `Int`, or `Bool`. Blank lines, comments,
spaces and tabs around syntax, LF, and CRLF are supported.

## Contracts and errors

- Basic strings implement `\b`, `\t`, `\n`, `\f`, `\r`, `\"`, and `\\`.
  Unknown escapes and currently unsupported `\u`/`\U` escapes are distinct
  errors. Literal strings do not process escapes.
- Decimal integers accept an optional sign and underscores only between
  digits. Leading zeros other than `0` are rejected. Values are checked against
  signed 64-bit `Int`, including the asymmetric `-9223372036854775808` limit,
  before conversion.
- Duplicate decoded keys are rejected. An assignment never silently replaces
  an earlier value.
- Error offsets are zero-based Unicode-scalar positions. A bare CR, missing
  syntax, forbidden control, unterminated quote, trailing data, invalid integer,
  and unsupported feature have typed, non-sentinel errors.
- Parsing has no ambient file or environment effects. It owns newly built keys,
  values, and the ordered entry list. Rendering only reads the document and
  rejects duplicate programmatic entries.

Parsing and rendering take O(n) scalar inspections for source/output size `n`,
plus O(e²) decoded-key comparisons for `e` entries. The reference uses immutable
`Text` concatenation and list `push`, so allocation cost may also be quadratic.
No zero-copy, streaming, hash-table, or performance claim is made.

## Compatibility profile and non-goals

`TOML_FLAT_PROFILE_VERSION = 1` is a strict subset profile: accepted documents
use TOML 1.0 spellings and canonical output is valid TOML 1.0, but many valid
TOML documents are deliberately rejected. The following remain future work:

- dotted keys, table headers, arrays of tables, arrays, and inline tables;
- floats, offset/local date-times, local dates, and local times;
- multiline strings, Unicode escapes, and line-ending string folding;
- schema binding, file/byte adapters, mutation APIs, streaming, SIMD, parallel
  parsing, interoperability adapters, fuzzing, and performance baselines.

Those features return a typed unsupported error where the profile can identify
them. They will require either a new profile version or a broader API; this
checkpoint will not silently reinterpret existing accepted input.

## Current compiler boundary

The canonical implementation uses records, algebraic data types, nested values,
and mutable text/list construction. The active compiler does not yet lower that
combination. The deterministic gate therefore executes an audited Int-Core
projection of the same line grammar, duplicate-key rule, four scalar kinds,
basic-string escape states, decimal separator/leading-zero rules, signed Int
range decisions, and LF/CRLF handling. It does **not** claim that the canonical
record API is code-generated today.

Run the Python-free gate with:

```sh
sh stdlib/toml/tests/verify.sh
```
