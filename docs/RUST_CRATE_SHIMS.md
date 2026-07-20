# Reusing Rust crates through C ABI shims

Kofun can reuse a Rust crate when a small Rust library translates the crate's
Rust API into the explicit C ABI profile introduced by issue #21. This is an
implemented, bounded interoperability path, not direct Rust-ABI support.

The worked example uses the real
[`unicode-segmentation`](https://crates.io/crates/unicode-segmentation/1.13.3)
crate to count Unicode extended grapheme clusters. This is useful beyond
Kofun's current native Text checkpoint: the bytes `65 CC 81` encode `e`
followed by U+0301, which is two Unicode scalar values but one user-perceived
grapheme cluster.

## Reproducible worked example

The dependency is exact-pinned to version 1.13.3 in
`examples/rust-shim/Cargo.toml`, locked by `Cargo.lock`, and fully vendored.
The registry checksum is:

```text
c6f5d3c3b1bf09027a88a6bc961fc00497d651009560b5463668dc81b0fa87a8
```

Its upstream MIT and Apache-2.0 licenses and copyright notice are checked in
beside the source. See `examples/rust-shim/THIRD_PARTY.md`.

Build and run without registry or network access:

```sh
cd examples/rust-shim
CARGO_NET_OFFLINE=true cargo build --offline --locked --release --lib
cd ../..

./bin/kofun build examples/rust-shim/graphemes.kofun \
  --backend c --c-abi \
  --link-library examples/rust-shim/target/release/libkofun_unicode_shim.so \
  -o build/graphemes
./build/graphemes
```

The active acceptance gate uses an empty Cargo home and a clean target
directory, so the build succeeds from the checked-in vendor alone:

```sh
sh examples/rust-shim/check.sh
```

`cargo`, `rustc`, a C11 compiler, and `readelf` are required. Missing tools are
failures, not skips.

## The boundary

The Kofun declaration, generated C, checked-in C header, and Rust export agree
on this logical ABI:

| Value | C ABI representation | Rule |
|---|---|---|
| input bytes | `const void *` / Kofun `CBytes` | borrowed, readable only during the call |
| input length | `size_t` / Kofun `CSize` | exact byte count; no NUL scan |
| result | 24-byte `repr(C)` struct | status, grapheme count, UTF-8 error offset |
| status | C `int` | `0` success, `1` invalid UTF-8, `2` caught panic, `3` invalid null buffer |

The shim constructs a Rust `&[u8]` only for the duration of the call. It does
not retain, mutate, or free the caller's buffer. It validates UTF-8 before
constructing `&str`. The C acceptance caller passes a stack buffer, calls the
shim twice, and verifies that every byte remains unchanged.

Invalid UTF-8 is ordinary data failure, not a Rust `Result` crossing the ABI.
The shim returns status 1 and `Utf8Error::valid_up_to()` in `error_offset`.
Unexpected Rust panics are caught inside the exported function and mapped to
status 2. The acceptance-only panic probe deliberately panics, then proves
that both the Kofun and C processes continue and exit successfully. Foreign
exceptions or unwinding are never allowed to cross the C frame.

The ABI still relies on the caller honoring pointer validity and length.
Passing a non-null invalid pointer or an oversized length is undefined
behavior, as it is for an equivalent C function.

## Why Rust types do not cross directly

Rust's native ABI is not Kofun's interoperability contract. The following
types require a deliberate shim:

| Rust surface | Why it cannot cross directly | Typical shim |
|---|---|---|
| generic function | each concrete use is monomorphized; there is no one stable external signature | export one C symbol per supported concrete type |
| trait method | dispatch and symbol shape use Rust ABI details | export a free `extern "C"` function for the required operation |
| `String` | Rust-owned pointer/length/capacity layout and allocator/drop contract are not a C ABI | borrow bytes plus length, or provide explicit create/destroy handles |
| `Vec<T>` | element layout, capacity, allocator, and destructor belong to Rust | caller-provided buffer, two-call size/query API, or opaque owned handle |
| `Result<T, E>` | enum layout and niche optimizations are not stable C ABI | integer status plus `repr(C)` output/error records |
| trait object | fat pointer and Rust vtable layout are not stable C ABI | opaque handle plus a fixed C function table owned by the shim |
| closure | captured environment and call ABI are compiler-defined | context pointer plus an `extern "C"` callback, with explicit lifetime |
| panic/unwind | unwinding through foreign frames is unsafe and may abort | catch inside Rust and return a status; use `panic=abort` only when abort is intended |

Opaque handles additionally need paired destruction functions from the same
Rust library. A C or Kofun caller must never free Rust allocation with a
different allocator. This example avoids that problem entirely by borrowing
input and returning only scalars in a `repr(C)` value.

## Build-path and trust boundary

Building the shim uses Cargo, rustc, LLVM, and the vendored crate. Kofun does
not invoke Cargo when `--link-library` points at an already built `.so` or
`.a`; it only emits C and invokes the host C compiler/linker. The prebuilt
artifact therefore separates Rust compilation cost from repeated Kofun
rebuild/link cost.

This remains the host-C dynamic profile:

```text
vendored Rust crate -> rustc cdylib -> fixed C ABI -> Kofun --backend c --c-abi
```

It does not change or weaken the direct-native `--target` profile. Direct
native output remains static and does not use Cargo, the host C compiler,
libc, a system linker, or this Rust library. Loading the shim trusts its native
code and transitive supply chain.

## Measurement

`examples/rust-shim/benchmark.sh` measures two deliberately different costs:

1. **clean Rust shim build**: remove the Cargo target directory, then build the
   release cdylib from the vendored source with `--offline --locked`;
2. **prebuilt-artifact Kofun rebuild/relink**: keep that `.so` unchanged and
   time the complete single-file Kofun C-ABI path, including C emission, host-C
   compilation, and dynamic linking.

Cleanup and one Kofun warmup are outside the timed intervals. Five wall-clock
samples are recorded and the median is reported. Kofun's single-file path has
no incremental object cache, so the second number is described as a repeated
full rebuild/relink, not as a cache hit.

The checked machine-readable result is
`artifacts/rust-shim-build-cost.json`. It records the exact source commit,
commands, sample arrays, medians, machine, compiler versions, and artifact
sizes. These are observations from one machine, not universal performance
claims.
