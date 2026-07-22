# Typed-sidecar tooling codec

`codec.mjs` is the production tooling-only reader, encoder, replacement
decision, and atomic writer for `kofun.typed-sidecar/v1`.

It exports:

~~~js
readTypedSidecar(bytes)
encodeTypedSidecar(document)
canReplaceTypedSidecar(oldDocument, newDocument, currentSourceDigest)
writeTypedSidecarAtomic(path, document, { currentSourceDigest, signal })
~~~

Read and encode return tagged `{ ok: true, ... }` or
`{ ok: false, error }` records. Read documents and result records are
recursively immutable. Replacement decisions are `{ allow, reason }` with
the stable reasons `allow`, `invalid-old`, `invalid-new`, `wrong-file`,
`stale-sequence`, and `source-mismatch`.

The writer validates and encodes before taking a destination lock. While the
lock is held it validates the old artifact, writes and flushes a mode-0600
temporary regular file in the destination directory, re-reads the current
destination, repeats the generation/source decision, and atomically renames.
Failure before rename preserves the previous bytes and removes only temporary
files whose device/inode still match those created by this writer.

Errors use bounded codes:

| Code | Class |
| --- | --- |
| `TS001` | invalid UTF-8/BOM/JSON/duplicate key/trailing data |
| `TS002` | schema or canonical form mismatch |
| `TS003` | semantic ID/path/span/order/relation/status violation |
| `TS004` | byte/count/depth/text/edit limit |
| `TS005` | invalid-old or denied replacement |
| `TS006` | cancellation, destination safety, lock, or atomic I/O failure |

This module is non-authoritative by construction. Compiler, KIF, build,
package, and linker paths cannot import it; the focused authority gate checks
that boundary. A typed sidecar can answer tooling queries but can never create
compiler or cache success.

Run the focused gates with:

~~~sh
make typed-sidecar-codec
~~~
