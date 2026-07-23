# Vendored utf8proc

This directory contains the unmodified `utf8proc.c`, `utf8proc.h`,
`utf8proc_data.c`, and `LICENSE.md` files from utf8proc v2.11.3.

- Upstream: <https://github.com/JuliaStrings/utf8proc>
- Tag: `v2.11.3`
- Archive SHA-256:
  `abfed50b6d4da51345713661370290f4f4747263ee73dc90356299dfc7990c78`
- License: MIT/Expat; the generated Unicode data is covered by the Unicode
  data license reproduced in `LICENSE.md`.

Kofun uses this snapshot for NFC normalization, Unicode scalar properties,
extended grapheme breaking, and character width. It is compiled directly into
the bootstrap seed, so no system locale or shared Unicode library is required.
