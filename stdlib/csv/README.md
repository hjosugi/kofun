# CSV text codec checkpoint

[`csv.kofun`](csv.kofun) defines a compact, deterministic CSV codec. It parses
a complete `Text` value into `CsvDocument { rows: List[List[Text]] }` and
renders that value back to text. File opening, byte decoding, and schema/type
inference are separate concerns and are deliberately outside this API.

```kofun
let dialect = csv_default_dialect()
let document = match csv_parse("name,note\r\nKofun,\"exact, small\"", dialect) {
    Ok(value) => value,
    Err(error) => return Err(error),
}
let encoded = csv_render(document, dialect)
```

The default dialect uses comma, double quote, and CRLF output. A custom
delimiter and quote must each contain exactly one Unicode scalar, must differ,
and cannot be CR or LF. Parsing accepts both LF and CRLF record separators so
Unix and RFC-style inputs compose without an ambient platform setting.

## Defined behavior

- Delimiters, CR, and LF are data inside quoted fields.
- Two adjacent quote markers inside a quoted field decode to one quote.
- A quote in an unquoted field, data after a closing quote, a bare CR, and EOF
  inside a quoted field are distinct typed errors.
- Error offsets are zero-based Unicode-scalar positions, matching `chars`, not
  UTF-8 byte positions.
- Empty input has zero records. A blank line has one record with one empty
  field. A trailing record separator does not create another record.
- Rows may have different field counts. Rectangular validation belongs to a
  future schema/table layer and is not silently imposed by this text codec.
- Rendering quotes a field exactly when it contains the delimiter, quote, CR,
  or LF, and doubles embedded quotes. It does not append a trailing record
  separator.

Parsing and rendering take O(n) scalar inspections for input/output size `n`.
The current immutable `Text` concatenation and `push` primitives may make the
reference implementation's allocation cost quadratic; this checkpoint does
not claim a zero-copy or streaming implementation.

## Ownership, compatibility, and limits

`csv_parse` owns no external resource and returns newly constructed text and
lists. `csv_render` only reads its document. There are no borrowed views whose
lifetime can escape the call.

This is a source-text codec, not a character-set detector. Callers must decode
bytes before parsing and choose an encoding when writing. NUL and all Unicode
scalars other than the configured syntax markers are ordinary field data.
Formula-injection policy is application-specific and is not altered by the
renderer.

The accepted grammar and escaping rules form compatibility version 1 of this
checkpoint. Future streaming or byte-oriented APIs must produce the same rows
and errors for the same scalar stream; adding permissive recovery requires an
explicit option rather than changing strict parsing in place.

## Current compiler boundary

The canonical implementation uses records, algebraic data types, nested lists,
and mutable text/list construction. The active compiler does not yet lower
that combination. The deterministic gate therefore executes an audited
Int-Core projection of the same four-state parser and quote-length rules. It
tests ordinary, blank, trailing-empty, CRLF, quoted-delimiter, doubled-quote,
multiline, and every defined malformed boundary. It does **not** claim that the
canonical record API is code-generated today.

Streaming/lazy rows, file adapters, header lookup, schema inference, SIMD,
parallel parsing, interoperability adapters, fuzzing, and performance reports
remain future lifecycle work from issue #500.

Run the Python-free gate with:

```sh
sh stdlib/csv/tests/verify.sh
```
