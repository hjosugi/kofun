# wasm32 host ABI v1

Status: accepted. Owner: repository maintainer. Issue: #906. Parent: #26.

This document is the normative host-boundary contract for the `wasm32` target:
one pinned ABI version, one import allowlist, one export set, and the byte
representation of `Text` and `List` as they cross between a Kofun guest module
and its host.

It is the **input** to wasm32 `Text`/`List` lowering, not the lowering. No code
generation changes with it, and the bounded `Int` target described by
`bootstrap/wasm/README.md` is untouched.

Its normative input is `spec/aggregate-layout-v1.md` with
`spec/aggregate-layout-v1/targets/wasm32.json`. Every byte quantity below is
**recomputed** from those files by running
`spec/aggregate-layout-v1/layout.mjs`; nothing here is a size, offset, or
reference width restated by hand. The executable form of this contract is
`spec/wasm-host-abi-v1/hostabi.mjs`, gated by
`sh spec/wasm-host-abi-v1/check.sh`.

## The ABI version, pinned twice

The one version string is **`kofun-wasm-host-abi-v1`**, revision `1`. A module
carries it in two independent places, and a host checks both:

| Pin | Form | Catches |
|---|---|---|
| import namespace | every import comes from the module name `kofun:host-abi-v1` | a guest compiled against another revision, at link time |
| exported global | `kofun_abi_version`, an immutable `i32` whose value is `1` | a guest that imports nothing at all |

Neither pin subsumes the other. A module with no imports has no namespace to
disagree about; a host that only inspected the namespace would accept it. The
global is read out of the module bytes *before* instantiation, because an
engine has no opinion about the value of an exported global and would link the
module happily.

## Imports — the allowlist

Every import comes from `kofun:host-abi-v1` and must be one of these four
fields, with exactly this signature. An import outside the table cannot be
satisfied; an import inside it with another signature is refused.

| Field | Signature | Meaning |
|---|---|---|
| `abort` | `(func (param i32 i32))` | `(code, detail)`: a checked failure. The host traps; control never returns to the guest. |
| `text_out` | `(func (param i32))` | borrow one `Text` object by reference for the duration of the call |
| `list_int_out` | `(func (param i32))` | borrow one `List[Int]` object by reference for the duration of the call |
| `list_text_out` | `(func (param i32))` | borrow one `List[Text]` object by reference for the duration of the call |

Pinned abort codes: `1` is a `List` index outside `0 .. length-1`; `2` is a
`kofun_alloc` request the guest could not satisfy. A host that renders a code
may not invent its own numbering.

The reference host publishes these four as **typed WebAssembly exports**, not
as plain JavaScript functions. That is load-bearing: a JavaScript function is
coerced to whatever signature the importer declared, so no engine can reject a
wrong-signature import against one. A wasm export carries a real function type
and the engine refuses the mismatch itself.

## Exports — the entry points

| Name | Kind | Signature | Meaning |
|---|---|---|---|
| `memory` | memory | `(memory 1)` or wider | the single linear memory every boundary reference addresses |
| `kofun_abi_version` | global | `(global i32 = 1)`, immutable | the revision pin above |
| `kofun_start` | func | `(func (param i32))` | the only entry point; the parameter is a host-owned `List[Text]` reference |
| `kofun_alloc` | func | `(func (param i32 i32) (result i32))` | `(size, align)` to a guest-owned reference, or `0` |

All four are mandatory, and an export present with the wrong kind or signature
is treated as absent: a `kofun_start` that is not `(func (param i32))` is not
the `kofun_start` this contract requires.

**A conforming module has no start section.** All guest execution begins when
the host calls `kofun_start`. That is what makes "no guest code ran" a checkable
claim rather than a hope: a module that declares a start function is refused on
its bytes, and is never handed to an engine at all.

## Text at the boundary

A `Text` **value** is exactly one reference: 4 bytes, 4-byte aligned on
`wasm32`, and it is passed and returned as `i32`. It never converts to or from
`Int`.

A `Text` **object** is `[byte_length: u64][UTF-8 bytes]` — the header at
offset 0, the payload at offset 8, no trailing padding, alignment 8. Zero
length is header-only and is *not* a null reference.

```
Text "ok" at address 1024 on wasm32
  offset 0  [ byte_length: u64 = 2 ]  8 bytes
  offset 8  [ 'o' 'k'              ]  2 bytes
  total 10          image 02000000000000006f6b
```

**UTF-8 only.** The payload is well-formed UTF-8 and nothing else. There is no
UTF-16 form, no code-unit count, and no length in characters: `"a日本語"` is
10 bytes, not 4. A host decodes fatally — ill-formed bytes are refused with
`text-not-utf8`, never replaced with U+FFFD and carried onward as if they had
been text.

**Pointer and length.** The `(pointer, length)` pair a host works with is
*derived*, never passed: `pointer = reference + 8`, `length` = the `u64`
header. Both are computed from the descriptors, so neither is a constant a host
is entitled to assume.

**Ownership.** The guest owns every object in its linear memory, including the
regions `kofun_alloc` returns. The host never frees, never resizes, and never
writes outside a region it was handed by `kofun_alloc`. Passing a reference to
`text_out` transfers nothing: it is a borrow for the duration of that call.

## List at the boundary

A `List` value is exactly one reference, the same 4 bytes as `Text`. A `List`
object is `[length: u64][elements]`: the header at offset 0, the elements at
offset 8, each element at `8 + index * element_size`.

Element size is the element type's layout on this target, recomputed and not
assumed: `Int` is 8 bytes on every target, and a `Text` element is one 4-byte
reference on `wasm32` where it is 8 bytes on `x86_64-linux`.

```
List[Text] x2 at address 1064 on wasm32
  offset 0  [ length: u64 = 2 ]  8 bytes
  offset 8  [ ref -> 1024     ]  4 bytes
  offset 12 [ ref -> 1040     ]  4 bytes
  total 16          image 02000000000000000004000010040000
```

**Immutable `List` v1 has no capacity field.** The object is a header and
exactly `length` elements. A host may not read a capacity, may not infer one
from the allocation it was handed, and may not treat any byte past the last
element as belonging to the list. If a future mutable `List` needs a capacity,
that is a different object shape under a different ABI revision, not a field
this one forgot to mention.

**Bounds.** The valid indices are `0 .. length-1`. An empty list has none, and
`length` is the whole of the bounds information — there is no sentinel and no
terminator. A guest that reaches outside the range calls `abort(1, index)`
instead of reading; a host that would read outside it refuses with
`boundary-out-of-range` instead of trusting a header. Both directions are
exercised: the fixture guest is called once with a two-element `List[Text]` and
once with an empty one, and the empty call aborts with code `1` before any
value crosses.

## The host-call lifetime rule

**The host may not retain a guest pointer past the documented call.** A
reference, a derived `(pointer, length)` pair, or a view onto guest memory is
valid for the body of the host function it was passed to, and for nothing after
that. A host that wants the bytes copies them while the call is live.

There is no way for a host to make retention safe by inspection. The guest may
reuse, overwrite, or free the region as soon as the call returns, and growing
the memory detaches every existing view of it. Nothing in the object says
which of those happened.

`spec/wasm-host-abi-v1/cases/` has no fixture for this, because it is not an
instantiation failure; the fixture is a *host*, run two ways:

```
$ node spec/wasm-host-abi-v1/hostabi.mjs lifetime conforming
{ "borrowed_during_call": "ok", "held_after_return": "ok", ... }

$ node spec/wasm-host-abi-v1/hostabi.mjs lifetime retained
wasm-host-abi: retained-guest-pointer: the host kept (1032, 2) past text_out
and now reads "!k" where the call lent it "ok"
```

The fixture guest overwrites the first payload byte immediately after
`text_out` returns. The conforming host copied during the call and is
unaffected. The retaining host reads bytes that are no longer the value it was
lent, and the gate fails — including if the drift ever stopped happening,
which would mean the fixture had gone inert and was proving nothing.

## Instantiation

Instantiation is three phases, and every refusal below happens in phase 1 or 2,
before any guest code can run:

1. **Validate the bytes.** The start section, both version pins, the import
   allowlist and signatures, and the required exports are checked against the
   module bytes. No engine is involved.
2. **Link.** The engine binds the allowlisted imports.
3. **Bind.** The host takes the memory export and re-reads `kofun_abi_version`
   from the instance. Still nothing has run: the module has no start section,
   and `kofun_start` has not been called.

The order inside phase 1 is part of the contract, because a module can be wrong
in more than one way and the reader deserves the first cause: start section,
then ABI version, then imports, then exports.

| Diagnostic | Fires when |
|---|---|
| `start-section-forbidden` | the module declares a start function |
| `abi-version-mismatch` | an import namespace other than `kofun:host-abi-v1`, or `kofun_abi_version` absent, mutable, non-`i32`, imported, or not `1` |
| `missing-import` | an import this host cannot supply — a field outside the allowlist, or a non-function import |
| `import-signature-mismatch` | an allowlisted field imported with another signature |
| `missing-export` | a required export absent, or present with the wrong kind or signature |
| `boundary-out-of-range` | a boundary read that would leave the guest's memory |
| `text-not-utf8` | a `Text` payload that is not well-formed UTF-8 |
| `retained-guest-pointer` | the lifetime rule above, violated |
| `vector-drift` | a golden vector that is not what the layout rules compute |

Each name is distinct and each names one cause; the gate asserts that the
instantiation fixtures produce five different names and that every fixture
report shows zero host calls, which is how "no guest execution" is measured
rather than asserted.

## Golden vectors

`spec/wasm-host-abi-v1/vectors/boundary.wasm32.json` holds the boundary
descriptors, arena placement, and complete byte images for six objects. It is
**recomputed** by `check.sh` and compared byte for byte, exactly as
`spec/aggregate-layout-v1/check.sh` recomputes its own descriptors instead of
reading them.

The chain is: `spec/aggregate-layout-v1/layout.mjs` is run over
`targets/wasm32.json` and `spec/wasm-host-abi-v1/boundary.json`; the resulting
descriptors supply every offset, stride, alignment, and object size; addresses
come from aligning each object into its arena; images are assembled from the
header the descriptor names and the payload the input document declares.
`layout.mjs` is executed rather than reimplemented — a second copy of the
layout rules would be a second place for them to be wrong.

Two independent checks keep that honest:

- The same computer is run over `spec/aggregate-layout-v1/vectors/core.json`
  and the result must equal the published
  `spec/aggregate-layout-v1/examples/core.wasm32.json` byte for byte; then
  every type and object this document shares with that example must agree on
  kind, size, alignment, pointer bitmap, payload offset, and payload size. A
  boundary value that contradicted the layout contract would be a second,
  quieter layout contract.
- The vectors are recomputed against `decoy-target.json`, a target identical to
  `wasm32` except for 8-byte references, and must **differ**. Vectors that came
  out the same either way would not be derived from the target at all.

Editing a vector by hand, without changing the rule that produces it, is
rejected — the gate mutates a copy of the golden file and requires the
comparison to refuse it with `vector-drift`.

## What this document does not claim

- **No code generation.** Nothing here lowers Kofun `Text` or `List` to wasm.
  The fixture module in `spec/wasm-host-abi-v1/wasm.mjs` is a test artifact
  assembled from the recomputed vectors, not a backend, and the compiler's
  wasm output is unchanged.
- **No WASI.** No command, file, clock, or process capability is described.
- **No engine matrix.** The reference host runs under one engine: the
  WebAssembly implementation in the Node build that runs the gate. Every
  outcome recorded in `cases/` was executed there rather than modelled,
  including the `LinkError` for a missing import and for a wrong-signature
  import, and the `TypeError` for an absent import namespace. Where an engine
  has no opinion — the value of an exported global, an absent export — the
  fixture records `"engine": "accepted"` and the contract refuses the module
  itself. Browser, standalone-runtime, and version coverage is a separate
  concern and is not claimed here.
- **No general value conversion.** `Int`, `Text`, and `List` cross this
  boundary. DOM surfaces, closures, records, ADTs, and floating point do not.
- **No threads, SIMD, GC types, or component-model packaging.**
- **No allocator contract.** `kofun_alloc` is an entry point with an ownership
  rule, not a specified allocator; the fixture's is a fixed arena, which is
  enough to state who owns what and nothing more.

## Relationship to the shipped wasm32 target

`bootstrap/wasm/` already emits standard modules that export `main(): void` and
import `kofun.print_i64` and `kofun.panic`. That is the bounded `Int` target,
and it is a different, earlier host binding: a different module name, a
different export, and no linear-memory objects at all.

**This is a comparison, not a compatibility requirement.** The bounded target
is evidence about what already runs; it does not constrain this contract, and
this contract does not retroactively re-specify it. A module cannot be both:
adopting the host ABI is a versioned change to that target, recorded as one,
and `sh bootstrap/wasm/check.sh` passes unchanged today because nothing in this
document touched it.

## Activation

That versioned change is now recorded. `spec/wasm-host-profile-v1.md` (#1000)
decides that the host ABI is part of the target name: this contract is
activated by **`--target wasm32-hostabi1`**, and bare `--target wasm32` stays
on the bounded numeric binding, which is not deprecated. No backend emits the
profile yet, so the name is refused with `kofun: unsupported target:` and no
artifact — the correct answer from a toolchain that cannot produce it.

Nothing in this document changes with that decision. It fixes the boundary; the
profile document fixes which builds arrive at the boundary, what the older
binding's support state is, and which oracles measure a lowering against this
contract once one exists.

Identifying the binding needs no engine and no guest execution. The phase 1
check above, run over bytes assembled elsewhere, is
`node spec/wasm-host-abi-v1/hostabi.mjs module MODULE.wasm`: it decodes,
applies the same rules in the same order, and answers with either an accepted
verdict or one of the diagnostics named above. It links nothing and instantiates
nothing, which is what makes the verdict usable *before* a host commits to
supplying imports.

## Consumers

wasm32 `Text`/`List` lowering depends on this artifact rather than on prose
assumptions. #26 tracks that lowering; #221 (WASI profile) and #238 (support
matrix) are unowned and out of scope here. Production lowering is a later
issue; this document and its checker are the contract it will be measured
against.
