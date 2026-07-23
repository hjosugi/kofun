# Unicode runtime contract

Kofun pins source and text behavior to Unicode 17.0.0. The common C runtime is
locale-independent and provides:

- UTF-8 validation and scalar decoding;
- UAX #31 `XID_Start` / `XID_Continue` identifier classification;
- NFC enforcement for identifiers;
- UTS #39 confusable-skeleton collision detection within one source file;
- rejection of bidirectional source controls;
- UAX #29 extended grapheme-cluster boundaries;
- explicit byte and codepoint views; and
- terminal display width, including CJK, Hangul, combining marks, emoji ZWJ
  sequences, flags, and an opt-in East Asian ambiguous-width mode.

`len(Text)`, `chars(Text)`, and `Text[Int]` use extended grapheme clusters.
The native Core exposes the lower-level views as `bytes(Text)` and
`codepoints(Text)`.

The generated identifier and confusable tables are reproduced with:

```sh
unicode/generate_tables.sh \
  DerivedCoreProperties.txt confusables.txt unicode/kofun_unicode_tables.inc
```

The inputs are the Unicode 17.0.0
[`DerivedCoreProperties.txt`](https://www.unicode.org/Public/17.0.0/ucd/DerivedCoreProperties.txt)
and UTS #39
[`confusables.txt`](https://www.unicode.org/Public/security/latest/confusables.txt).
Their download hashes are recorded in `unicode/SOURCE_SHA256SUMS`; the checked
in generated table and vendored snapshot are verified by
`unicode/SHA256SUMS`.

The normalization, grapheme-property, and scalar-width database comes from the
vendored utf8proc 2.11.3 snapshot under `vendor/utf8proc/`.
