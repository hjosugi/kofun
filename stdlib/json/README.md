# JSON value codec checkpoint

[`json.kofun`](json.kofun) defines a deterministic, platform-independent JSON
value codec. Its compact public surface is `json_parse(Text)`,
`json_render(JsonValue)`, and the checked `json_number(Text)` constructor.

```kofun
let payload = match json_parse("{\"ok\":true,\"count\":1e3}") {
    Ok(value) => value,
    Err(error) => return Err(error),
}
let encoded = json_render(payload)
```

`JsonValue` represents null, booleans, number lexemes, text, arrays, and
ordered object members. Numbers remain validated `Text` lexemes, so parsing
does not silently round a decimal, truncate an exponent, or impose the current
`Int`/`Float` range. Rendering preserves the accepted number spelling and
object member order. Whitespace is normalized away and separators are emitted
without optional spaces.

## Version 1 behavior

- The accepted number grammar is JSON's
  `-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?` shape.
- Object names must be unique. Parsing reports the duplicate name and source
  offset; rendering also rejects programmatically constructed duplicates.
- Strings accept direct Unicode scalar values and the JSON short escapes for
  quote, reverse solidus, solidus, backspace, form feed, newline, carriage
  return, and tab.
- Raw U+0000 through U+001F controls are rejected. A renderer uses short
  escapes when one exists and reports other C0 controls rather than emitting
  invalid JSON.
- Arrays and objects may nest to 64 levels. The fixed limit makes adversarial
  resource use explicit and returns `NestingLimitExceeded` before descending.
- Error offsets count Unicode scalars, matching `chars`, rather than UTF-8
  bytes. No malformed input is represented by an empty document or sentinel
  `JsonValue`.

Parsing performs O(n) scalar inspection aside from immutable text/list
construction costs. Duplicate-name checks are O(m^2) for an object with `m`
members in this clear reference version. Rendering follows the same duplicate
check and otherwise visits every value and emitted scalar once. The API owns
no file, stream, or external resource; parsed values are newly constructed and
rendering only reads its input.

## Honest boundary

This first profile does **not** decode `\uXXXX` escapes. It returns the typed
`UnsupportedUnicodeEscape` error, including for surrogate pairs. Direct UTF-8
decoded Unicode text round-trips, but inputs that rely on Unicode escape
notation require a future scalar-construction primitive. This means the
checkpoint is a strict useful subset of RFC 8259, not a claim of full JSON
conformance.

The active compiler cannot yet lower the canonical recursive ADT/record/list
implementation. The focused gate checks that boundary and executes an audited
Int-Core projection of container grammar, whitespace, JSON number states,
short escapes, malformed separators, trailing input, and nesting exhaustion.
It does not claim the canonical `JsonValue` API is code-generated today.

Streaming parsing, byte/UTF-8 adapters, JSON Pointer/Patch, typed derivation,
schema validation, `\uXXXX` decoding, fuzzing, SIMD/parallel parsing,
interoperability adapters, and performance reports remain later lifecycle work
from issue #499.

Run the Python-free focused gate with:

```sh
sh stdlib/json/tests/verify.sh
```
