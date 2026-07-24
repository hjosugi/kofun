# Self-host Core native parity

This directory holds evidence that the frozen self-host **Core** reaches a real
native binary through two fully independent backends, tying the self-hosting
track to direct native-binary production.

The frozen program is the canonical single-expression Core:

```kofun
fn main() {
    print((6 + 1) * 6)
}
```

`check-native-corpus.sh` lowers it to a native executable two ways and requires
both to print the pinned golden `corpus_core.stdout` (`42`):

1. **Self-host C11 path.** The compiler built from the frozen `S`
   (`bootstrap/stage1/compiler.kofun`) — call it `A1` — is produced exactly as
   in `../check-compiler-driver.sh`: the trusted Stage 2 seed runs
   `kofun-stage2 --selfhost-compile` to emit the checked-in
   `../driver/S.c`, and the declared host `cc` builds `A1` from it. `A1` then
   compiles the frozen Core to deterministic C11, which `cc` links into a native
   binary.
2. **Direct-native path.** `kofun build ... --target x86_64-linux` and
   `--target aarch64-linux` drive the audited `bootstrap/native/core_compiler.c`
   backend, which writes a statically linked ELF64 image directly, with no
   assembler, linker, or `cc`.

The gate also checks that the direct-native images are:

- **deterministic** — two builds of the same source are byte-identical;
- **path-independent** — the same relative source compiled from two different
  directories produces byte-identical images, so no absolute build path leaks;
- **correctly shaped** — `readelf -h` reports `ELF64` with the expected machine
  (`Advanced Micro Devices X86-64` and `AArch64`).

The x86-64 image is executed and its output is compared both to the pinned
golden and to the self-host C11 path's output, so the two independent backends
must agree on Core behavior. The AArch64 image is executed when a
`qemu-aarch64` runner is available (or `QEMU_AARCH64` is set); otherwise its
ELF64 machine is still verified and execution is reported as skipped.

## Bounded surface, stated honestly

The direct-native Core is a bounded subset: it accepts a single `print`
expression. The gate records this as negative evidence — building the full
five-`print` self-host success corpus (`../driver/corpus_answer.kofun`) through
the native backend is refused with the stable `unsupported Core` diagnostic and
writes no image, while the C11 self-host path accepts that same corpus.

## What this is and is not

- It **is** parity evidence that the self-host Core produces a real native
  binary through two independent backends.
- It is **not** self-application: `A1` compiles an ordinary Core input, exactly
  like the `../driver` gate. It makes no claim that `S` compiles `S`.
- It does **not** add a direct-native dependency to the C11 bootstrap fixed
  point tracked by #271/#272, which stays `cc`-based and native-independent per
  those issues. This gate is a separate artifact.

## Validation

```sh
make selfhost-native
# or
sh bootstrap/selfhost/native/check-native-corpus.sh
```
