# Release claims

Generated from `release/claims.json` by `make release-evidence`. Do not edit.

| Claim | State | Area | Capability |
|---|---|---|---|
| `arithmetic-core` | implemented | frontend | The arithmetic Core is validated and emitted for the checked Int64 operator set; `/` is deliberately not part of it. |
| `borrowed-list-ownership` | checkpoint | frontend | Stage 2 rejects use-after-move for borrowed `List` bindings only. It is not a general ownership pass. |
| `c-abi-profile` | checkpoint | interop | `extern` declarations and `repr(C)` layouts are honoured for the host C compiler only; no cross-ABI or cross-target promise is made. |
| `c11-function-calls` | checkpoint | c-backend | User functions lower to C11 for the Int Core, including recursion and forward references. Other value types are outside the slice. |
| `checked-int64-contract` | implemented | frontend | Every Core `Int` operation is checked: overflow and non-representable results are refused or trapped, never wrapped. |
| `cli-commands` | checkpoint | tooling | `build`, `run`, `check` and `test` accept Core sources. Sources outside the Core are refused by the driver, not partially compiled. |
| `cli-framework` | checkpoint | frameworks | The CLI framework produces a direct static Linux x86-64 binary for the declarative command profile. No dynamic linking or other host is covered. |
| `compiler-seed` | implemented | bootstrap | The Stage 1 seed is written in Kofun and kept beside a hand-audited C transliteration; the pair is the bootstrap root of trust. |
| `deterministic-fuzzing` | implemented | quality | Fuzzing is seeded and reproducible: the same seed yields the same programs and the same oracle/backend observations. |
| `elf64-image-writer` | checkpoint | native | The backend writes a static ELF64 image directly, without an external assembler or linker, for the covered Core profiles. |
| `enum-matching` | checkpoint | frontend | Payload-free concrete enums match exhaustively over their constructor set. Payload-carrying and generic enums are outside the slice. |
| `formatter-and-repl` | design | tooling | Design only. No formatter or REPL is shipped, and no gate claims one. |
| `general-native-lowering` | open | native | Unified types, unified control flow, and additional target profiles are absent. Only the enumerated native checkpoints are claimed. |
| `general-ownership-checking` | open | frontend | There is no general ownership or law pass. Only the narrow borrowed-`List` checkpoint is claimed. |
| `general-parser-type-checker` | open | frontend | The active compiler is not a general parser or type checker. Only the enumerated Stage 2 checkpoints are claimed. |
| `http-framework` | checkpoint | frameworks | A bounded HTTP/1.1 server library over epoll, reached through the C ABI profile. TLS, HTTP/2 and client redirects are outside it. |
| `module-aliases` | checkpoint | frontend | Same-package `import a.b as local` only. The alias is local, never re-exported, and does not route the driver. |
| `native-aarch64-function-calls` | checkpoint | native | The same bounded Int Core lowers to AArch64. Execution evidence requires `qemu-aarch64`; without it the gate reports a skip rather than a pass. |
| `native-constant-stack-returns` | checkpoint | native | A `return` of a direct call becomes a branch, so recursion in tail position runs in constant stack. Calls in non-return position still consume stack. |
| `native-int64-values` | checkpoint | native | Literals reach `INT64_MAX` and checked expressions reach the complete signed range, with deterministic encodings on both targets. |
| `native-integer-division` | checkpoint | native | `//` and `%` follow floor semantics on both targets. `/` is refused on `Int`, and zero or non-representable quotients trap with a canonical per-operator `R010`. |
| `native-list-int-core` | checkpoint | native | `List[Int]` values lower natively on both targets for the Core operation set. AArch64 execution evidence requires qemu. |
| `native-text-returning-calls` | checkpoint | native | Functions returning `Text` lower on both targets for the compiler-shaped profile: parameters, locals, concatenation, forwarding and direct calls. |
| `native-utf8-text-core` | checkpoint | native | UTF-8 `Text` values lower natively on both targets for the Core operation set, with codepoint and grapheme behaviour gated against Unicode data. |
| `native-x86-64-function-calls` | checkpoint | native | Up to six integer arguments, guarded returns and recursion, for the Int Core only. |
| `nominal-records` | checkpoint | frontend | The dedicated Stage 2 record frontend accepts nominal declarations, labelled construction, typed field reads, whole-record moves, and declaration-order layout facts. Module visibility, generic records, and backend lowering remain outside the checkpoint. |
| `public-re-exports` | checkpoint | frontend | Same-package `pub import` / `pub from` with non-widening edges and explicit numeric bounds: 64-edge chains, 1,024 bindings per module, 65,536 edges per package. |
| `reproducible-bootstrap` | implemented | bootstrap | Regenerating Stage 1 from its seed reproduces the checked-in artifact byte for byte on the supported host toolchain. |
| `rust-crate-shim` | checkpoint | interop | One vendored Rust crate is reached through an audited C ABI shim, as a worked example. It is not a general Rust interop story. |
| `self-recompile` | checkpoint | self-hosting | The frozen profile reaches a runnable compiler-produced compiler. The three-generation semantic fixed point is not reached and is not claimed. |
| `selfhost-native-corpus` | checkpoint | self-hosting | The driver's five-`print` success corpus reaches a static ELF on both native targets and its output matches the self-host C11 path exactly. |
| `source-extension` | implemented | tooling | `.kofun` is the only source extension in the repository, and the editor integration registers exactly that extension. |
| `stable-diagnostics` | implemented | quality | Every diagnostic code has a canonical registry row and an executable family owner. Stage 2 retains 46 of 46 codes, with 3 span debts recorded rather than hidden. |
| `stage2-core-lowering` | checkpoint | frontend | Stage 2 lexes, parses and lowers the integer Core. Sources outside that Core are refused with a diagnostic. |
| `stage2-typed-sidecar` | checkpoint | tooling | Stage 2 projects compiler-derived semantic events one-way into typed-sidecar v1, for explicit single-file `kofun check` only. The sidecar is non-authoritative and compiler, KIF and cache consumers remain forbidden. |
| `stdio-language-server` | checkpoint | tooling | A stdio LSP server providing diagnostics, symbols, completion, hover, definitions and references, within measured latency budgets. |
| `syscall-file-round-trip` | implemented | frameworks | A native ELF program opens, writes, reads back and closes a file through direct syscalls, and reports errno failures explicitly. |
| `syscall-stdlib-api` | checkpoint | frameworks | The syscall and standard-library surface is a Kofun source contract for Linux x86-64. It is not a portable standard library. |
| `test-skip-reporting` | implemented | tooling | `kofun test` reports skipped cases explicitly and counts them separately from passes. |
| `tree-sitter-grammar` | implemented | tooling | A structural Tree-sitter grammar with editor queries and a recovery corpus, gated against the repository's own sources. |
| `wasm32-arithmetic-core` | checkpoint | webassembly | Checked Int64 arithmetic lowers directly to wasm32 and runs in a lazily initialised browser host. Text and List values are outside the slice. |
