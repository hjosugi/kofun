# Token span stage contract

Status: normative contract for the narrow Stage 2 token-span prototype.

This contract documents what the executable frontend does today and separates
that checkpoint from the planned lossless parser and tooling model. It does
not claim error recovery, incremental lexing, retained trivia, source maps for
generated code, or a stable external tooling API.

## Stage boundary

The stage accepts one immutable UTF-8 source byte sequence associated with one
source identity. The current command-line prototype obtains that sequence from
its input path. Its successful lexical output is a UTF-8 text artifact:

```text
kofun-token-tape/v1
KIND|START|END|LINE
...
```

`START` and `END` are unsigned decimal byte offsets into the original source.
The interval is half-open: `START <= byte < END`. `LINE` is the one-based line
number containing `START`. A byte offset, rather than a Unicode scalar or
display column, is the stable coordinate for this version.

The prototype writes the tape only as part of a successful Stage 2 invocation.
The command also structurally parses the compilation unit and writes source
and function-IR outputs. A lexical tape by itself does not prove that the
source is a valid program.

## Token model and ownership

The conceptual data model is:

```text
SourceId  = identity of one immutable source snapshot
ByteSpan  = { source: SourceId, start: UInt, end: UInt }
Token     = { kind: TokenKind, span: ByteSpan, start_line: UInt }
TokenTape = ordered sequence<Token>
```

The textual v1 prototype omits `SourceId` because one artifact describes
exactly one input snapshot. Its owner MUST keep the source bytes alive for at
least as long as consumers dereference spans. Tokens borrow source text by
span; they do not own per-token copies in the conceptual model. A materialized
text tape costs `O(token_count)` records and decimal serialization overhead;
the scanner's source plus tape memory is `O(source_bytes + output_bytes)`.

A token's stable identity within an immutable snapshot is
`(SourceId, start, end, kind)`. Line number is derived metadata and is not part
of identity. Re-lexing byte-identical source MUST produce byte-identical tape.
After any source edit, a new `SourceId` is required; v1 provides no incremental
identity mapping.

## Success invariants

For every successful tape:

1. the header is exactly `kofun-token-tape/v1`;
2. tokens are ordered by strictly increasing `start`;
3. every token has `start < end <= source_byte_length`;
4. token spans do not overlap;
5. slicing the source bytes by a span recovers exactly that token's spelling;
6. whitespace and `#` line comments occupy gaps and have no token records;
7. paired punctuation recognized by the prototype occupies one span;
8. an escaped double-quoted string occupies one span, including its quotes;
9. repeated runs over byte-identical input produce identical tapes.

The current kinds are `keyword`, `identifier`, `integer`, `string`, and
`punctuation`. They describe lexical classification only. In particular, a
future keyword not in the current scanner's fixed list may appear as an
identifier until the implementation is extended.

## Baseline recognized by the prototype

The current executable scanner recognizes:

- Unicode 17 `XID_Start` / `XID_Continue` identifiers plus `_`, with NFC,
  confusable-collision, invalid UTF-8, and bidi-control validation performed
  before tokenization;
- a fixed keyword list used by the bootstrap subset;
- digit/underscore runs classified as integer tokens;
- double-quoted, single-line strings with backslash escaping;
- the paired tokens `->`, `==`, `!=`, `<=`, `>=`, `&&`, `||`, `//`, `..`,
  `**`, `??`, and `|>`;
- every other ASCII non-trivia byte as one punctuation token;
- ASCII/host-C whitespace and `#` line comments as skipped trivia.

This is a feasibility subset, not the full lexical language contract. For
example, it does not validate numeric underscore placement, recognize Float
tokens, classify `null` and `set` as keywords, or retain comments.

## Failure behavior

An unterminated or newline-crossing double-quoted string makes lexical
scanning fail with:

```text
error[E2S01]: unterminated string at byte START
```

Unicode source validation uses `EUNICODE001` through `EUNICODE007`.
Diagnostics report the byte offset together with one-based line and extended
grapheme-cluster column. `KOFUN_DIAGNOSTIC_LOCALE=ja` selects the Japanese
catalog; unsupported locales fall back to English.

The Stage 2 driver reports diagnostics on standard output and exits nonzero.
Structural parsing may subsequently fail with another Stage 2 diagnostic.
The prototype stops; it does not synthesize a token, recover at a newline, or
return a partial tape. Recovery and multi-error reporting therefore remain
open lifecycle work.

Out-of-memory and host file-I/O failures are fatal seed errors. The prototype
does not yet enforce a configurable source-size or token-count budget.

## Consumer rules

A parser consuming v1 MUST:

- treat source bytes as authoritative and token text as a slice;
- reject malformed tape records and out-of-bounds or overlapping spans;
- never infer trivia preservation from gaps unless it also owns the source;
- attach a syntax-node span from the first consumed token start through the
  last consumed token end;
- keep zero-width inserted/recovery tokens outside v1 until recovery semantics
  define their identity.

A diagnostic SHOULD underline the smallest relevant token span. A later
lossless syntax layer MUST represent trivia separately instead of converting
comment gaps into semantic tokens.

## Executable prototype

Run:

```sh
sh tests/conformance/syntax/issues_48_60/run.sh
```

The fixture compiles the audited C11 Stage 2 seed, scans one accepted source,
compares the complete tape with a checked-in golden artifact, checks ordering,
bounds, non-overlap, token slices, trivia gaps, pair-token width, line
numbers, and deterministic replay. It also verifies the exact failure
diagnostic for an unterminated string.

The prototype justifies the first three issue 60 lifecycle items:

1. compiler-stage contract;
2. data structures and ownership;
3. minimal prototype.

It does not justify core completeness, recovery, incremental work,
parallelism, bounded adversarial memory, tooling stability, or release
qualification.
