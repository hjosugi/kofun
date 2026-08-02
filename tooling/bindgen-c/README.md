# kofun bindgen-c

Stage 1 of [#574](https://github.com/hjosugi/kofun/issues/574): generate
audited, raw C bindings from the Clang AST — before, and separate from, any
C-to-Kofun source translation.

```sh
kofun bindgen-c HEADER.h --out-dir DIR [--module NAME]
    [-D NAME[=VALUE]]... [-I DIR]... [--std STD]
    [--target TRIPLE] [--sysroot DIR] [--clang PATH]
```

`bindgen-c.mjs` runs under node with no dependencies beyond the node
standard library. clang is invoked through a structured argv
(`execFileSync`), never through a shell-interpolated string, and the header,
the AST, and the preprocessor output are treated as untrusted inputs:
parsing is bounded, nothing is evaluated, and no content from the header
reaches a shell. The gate for all of this is
[`tests/interop/bindgen-c/check.sh`](../../tests/interop/bindgen-c/check.sh).

## Outputs

Two files in `--out-dir`, both deterministic — the same inputs produce
byte-identical outputs (declarations sorted by name, no timestamps, paths
under the working directory recorded relative to it):

- **`NAME.raw.kofun`** — extern declarations in the checked C ABI profile
  surface that [`bootstrap/c_abi/`](../../bootstrap/c_abi/) compiles:
  `repr(C) struct` records and `extern "C" fn` functions. The file opens
  with a raw-trust banner, `trust: raw-trusted-foreign`, and the full
  interpretation context, so it cannot be mistaken for a safe façade and a
  stale artifact cannot be mistaken for a regenerated one. The `.raw.`
  segment is part of the contract.
- **`NAME.bindgen.json`** — schema `kofun.bindgen-c.report/v1`: the
  interpretation context (clang version, effective target triple, language
  standard, defines, include paths, sysroot, sha256 of the input header,
  and a digest over all of it), per-declaration layout facts (sizes,
  alignments, field offsets, enum constants, symbol names, a structured
  calling-convention classification derived from Clang's function type and
  the effective target, and the original C type of every parameter and result), and
  the audit — every skipped or review-required declaration with a reason.

## Type mapping

The target is the pinned 64-bit Linux LP64 profile, the same one
`bootstrap/c_abi/compiler.c` checks and `_Static_assert`s.

| C | Kofun | notes |
|---|---|---|
| `void` | `Unit` | result position only |
| `_Bool` | `Bool` | |
| `char`, `signed char` / `unsigned char` | `I8` / `U8` | `char` is signed on the pinned target |
| `short` / `unsigned short` | `I16` / `U16` | |
| `int` / `unsigned int` | `CInt` / `CUInt` | |
| `long` / `unsigned long` | `CLong` / `CULong` | LP64 |
| `long long` / `unsigned long long` | `I64` / `U64` | |
| `float` / `double` | `F32` / `F64` | |
| `const char *` | `CStr` | read-only NUL-terminated view |
| any other pointer, incl. `void *`, `T *`, function pointers | `CBytes` | untyped; every such lowering is a review row in the audit |
| `enum E` | `CInt` | constants recorded in the report, values must fit `int` |
| complete `struct S` (scalar fields) | `repr(C) struct` | layout recorded and independently re-derived by the c_abi compiler |
| opaque `struct S` (declared, undefined) | `CBytes` at use | listed under `layout.opaque_handles` |
| typedefs | resolved (bounded depth) | the spelled C type is preserved in the report |

Skipped, with an audit row each, never silently: macros (object- and
function-like), variadic functions, unions, bitfields, flexible array
members, `static`/`inline` functions, global variables, functions without
prototypes, nested declarators (function-pointer results), records with
pointer or array fields, enums that do not fit `int`, attribute-carrying
records, unsupported function attributes, non-default calling conventions,
and anything past the checked
profile's capacity limits (16 structs, 16 fields, 64 functions, 16
parameters).

For the pinned x86_64 Linux profile, an attribute-free Clang function type
is recorded as `sysv-x86_64` with source `target-default`. An explicit
`sysv_abi` spelling resolves to the same supported convention. Other
conventions are skipped with `reason_code: unsupported-calling-convention`
and their Clang attribute and effective convention remain in the audit row.
The gate turns every accepted report row back into an explicitly attributed
C function type and asks Clang to prove it compatible with the real header;
missing, unknown, or contradictory convention data is a hard failure.

## What generated code does not claim

Generated bindings are **raw and trusted, not safe**. Headers do not encode
pointer ownership, lifetime, thread affinity, callback duration, or error
conventions, so the generator cannot check them; it records them as
`review` rows (`ownership-unreviewed`, `opaque-handle-pointer`,
`untyped-pointer`, `callback-parameter`) instead of guessing. A safe façade
over a raw module is hand-written, reviewed work. `kofun migrate-c`
(translating C function bodies) is deliberately out of scope for stage 1
and stays deferred until bindgen is stable, per #574.

## Regeneration policy

Generated files are immutable: edit the header or the invocation and
regenerate, never the output. The module embeds `context-sha256`, and the
report embeds the module's sha256, so a hand-edited or stale artifact is
detectable mechanically.
