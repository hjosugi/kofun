# Issue 33: native Stage 1

## Verified starting point

`bootstrap/native/encoder.kofun` owns ELF64 headers, x86-64 instruction bytes,
labels, and fixups for three executable fixtures. The native gate verifies
their hashes, layout, stdout, stderr, and exit status. This is genuine direct
machine-code generation with no assembler or linker.

It is not yet a native compiler build. The active CLI compiles the audited
Stage 1 C11 seed with a host C compiler, and it exposes only the C11 bootstrap
backend. The native fixtures use narrow run-length-encoded bridges because
Stage 1 cannot compile the encoder's lists and general calls.

## Native artifact contract

The first native Stage 1 artifact must:

- be emitted from canonical Kofun compiler source through Kofun-owned ELF and
  x86-64 encoders;
- contain no generated C, assembler, linker input, or host-compiler step;
- support every runtime operation used by the Stage 1 compiler;
- consume a `.kofun` input and deterministically emit the same canonical
  fixture artifact as the reference Stage 1 path;
- report identical status, stdout, stderr, and diagnostics for the bootstrap
  conformance corpus; and
- be reproducible byte-for-byte in two clean directories.

The build record must include source, encoder, runtime, corpus, and output
SHA-256 values plus the target triple and exact command.

## Differential gate

For every supported bootstrap fixture, run the reference and native compiler
artifacts from clean directories and compare:

1. emitted artifact bytes;
2. compiler stdout and stderr;
3. compiler exit status; and
4. execution stdout, stderr, and exit status of successful outputs.

An unsupported feature must be an explicit failure with a stable diagnostic,
not a skip that counts as agreement.

The issue predates the Python-free migration and names a retired host-language
checker. The maintained equivalent is `bootstrap/check_bootstrap.kofun`,
driven by a POSIX shell gate. Reintroducing the retired implementation to
satisfy a stale filename is forbidden. The current Kofun checker already
specifies equality of two nonempty Stage 1 C artifacts, but the native producer
needed for its second input does not exist.

## Required implementation order

1. Connect typed integer Core trees and bindings to the native encoder.
2. Add calls, branches, checked arithmetic, canonical diagnostics, and general
   integer formatting to the native runtime.
3. Add the `Text`, `List[Text]`, allocation, and file operations used by Stage
   1.
4. Replace fixture-specific RLE bridges with compiler-driven native emission.
5. Build the Stage 1 compiler itself and add the differential gate above.

## Executable close checklist

- [x] Kofun-authored deterministic ELF64/x86-64 images execute.
- [x] Absolute and PC-relative fixups execute end to end.
- [x] Raw stdout and exit syscalls execute without a linker or libc.
- [ ] Arbitrary typed bootstrap Core lowers through the native backend.
- [ ] Native runtime covers every operation used by Stage 1.
- [ ] Canonical Stage 1 source produces a native compiler artifact.
- [ ] Reference and native Stage 1 outputs are byte-identical on the corpus.
- [ ] The Python-free bootstrap checker runs as part of the native gate.
- [ ] The manifest records native Stage 1 provenance and hashes.
