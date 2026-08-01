# Bounded tzdb producer

The executable artifact for [#878](https://github.com/hjosugi/kofun/issues/878):
time-zone rules read from bytes a caller injects, a bounded transition lookup,
and explicit gap and fold results carrying the provenance of the rules that
produced them.

`check.sh` is the gate. `task tzdb` runs it.

## What is here

| file | what it is |
|---|---|
| `tzdb.kofun` | the executable Stage 2 producer |
| `tzdb.stdout` | its recorded output, 117 lines |
| `typed_hir.kofun` | a witness small enough for the semantic sidecar to project |
| `typed_hir.stdout` | its recorded output |
| `mixed_local_instant.kofun` | an instant handed to the local resolver; must be refused |
| `local_epoch_field.kofun` | an epoch read off a local reading; must be refused |

The canonical surface is `stdlib/tzdb/tzdb.kofun`. It is ahead of the compiler
and the gate pins that: it must still fail `kofun check` with
`error[E2S02]: expected top-level \`fn\` or \`type\``. The executable evidence is
`tzdb.kofun`, not the canonical file.

## The fixture

17 bytes, committed inside the producer rather than read from anywhere:

```
0..3    magic 'K' 'T' 'Z' '1'
4       format version
5       zone code
6       transition count
7       reserved, must be zero
8..9    transition 0 instant, seconds, big-endian
10      transition 0 offset before, minutes, biased by 128
11      transition 0 offset after,  minutes, biased by 128
12..13  transition 1 instant, seconds, big-endian
14      transition 1 offset before, minutes, biased by 128
15      transition 1 offset after,  minutes, biased by 128
16      content digest: the sum of bytes 0..15, modulo 251
```

251 is the largest prime below 256, so every single-byte edit changes the
digest. The digest and the version travel into every serialized result: an
answer produced from different rules cannot be mistaken for one produced from
these.

Transition 0 springs forward one hour at 10000 seconds, opening a gap over
local `[10000, 13600)`. Transition 1 falls back one hour at 20000 seconds,
opening a fold over local `[20000, 23600)`. Both intervals are half-open at the
top, and the gate reads both ends of both — an off-by-one there is the classic
way an hour goes missing.

## The lookup

`interval_rank` is a sum of four comparisons against the four local-time
boundaries the two transitions create. The comparison count and the control
flow are the same for every input, so a replay cannot take a different path
through the table; there is only one path. The issue permits a binary search
over the same boundaries, and this is strictly more bounded than one.

No branch in the rank selects an offset. The rank does not know what an offset
is: the direction of a jump is read from the transition, so a gap and a fold
cannot be swapped by editing the boundary order.

## Two spellings differ from the canonical surface

The Stage 2 Core is small — record fields are `Int`, an ADT constructor carries
at most one `Int`, and there are no loops. Exactly two things are spelled
differently because of it, and the producer says so at the top of the file:

* `Bytes` is projected as a fixed-capacity record of byte slots plus a length.
  The reader still reads through one accessor and still refuses a length it did
  not expect, so truncation and trailing bytes are observed rather than assumed
  away.
* A resolution payload of two offsets is spelled as a record beside a one-`Int`
  constructor. The sum is still closed and is still reached by `match`.

## What is deliberately absent

No IANA database, no zoneinfo discovery, no `TZ`, no download, no leap seconds,
no locale, and no ambient file access. The only rules this program knows are
the bytes written into it, which is what makes replaying it a complete test
rather than a hopeful one. The gate asserts this against the emitted C rather
than trusting the source: `tzdb.c` is searched for the symbols that would reach
host time or host time-zone state.

The assertions that search for those symbols read the source with comment lines
stripped. Both files explain at length what they do not reach for, and a grep
over the whole text cannot tell the comment saying "does not consult TZ" from a
line that consults it.
