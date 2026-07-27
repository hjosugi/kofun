# Reproducing the release evidence

Generated from `release/claims.json` by `make release-evidence`. Do not edit.

From a clean checkout:

```sh
make verify
make release-evidence
```

## External prerequisites

A gate whose prerequisite is missing must report a skip. It must never
report a pass it did not observe.

| Prerequisite | Claims that need it |
|---|---|
| `cargo` | `rust-crate-shim` |
| `cc` | `c-abi-profile`, `c11-function-calls`, `compiler-seed`, `deterministic-fuzzing`, `enum-matching`, `http-framework`, `nominal-records`, `reproducible-bootstrap`, `self-recompile`, `stage2-core-lowering` |
| `node` | `stage2-typed-sidecar`, `stdio-language-server`, `tree-sitter-grammar`, `wasm32-arithmetic-core` |
| `qemu-aarch64` | `native-aarch64-function-calls`, `native-list-int-core`, `native-text-returning-calls`, `native-utf8-text-core`, `selfhost-native-corpus` |
| `sha256sum` | `compiler-seed`, `reproducible-bootstrap` |

## Per-claim reproduction

| Claim | Command |
|---|---|
| `arithmetic-core` | `make test` |
| `borrowed-list-ownership` | `make stage2` |
| `c-abi-profile` | `make c-abi` |
| `c11-function-calls` | `make stage2` |
| `checked-int64-contract` | `make check` |
| `cli-commands` | `make test` |
| `cli-framework` | `make cli-framework` |
| `compiler-seed` | `make bootstrap` |
| `deterministic-fuzzing` | `make fuzz` |
| `elf64-image-writer` | `make native` |
| `enum-matching` | `make syntax` |
| `formatter-and-repl` | `make repository-check` |
| `general-native-lowering` | `make native` |
| `general-ownership-checking` | `make stage2` |
| `general-parser-type-checker` | `make stage2` |
| `http-framework` | `make http` |
| `module-aliases` | `make import-aliases` |
| `native-aarch64-function-calls` | `make native` |
| `native-constant-stack-returns` | `make native` |
| `native-int64-values` | `make native` |
| `native-integer-division` | `make native` |
| `native-list-int-core` | `make native` |
| `native-text-returning-calls` | `make native` |
| `native-utf8-text-core` | `make native` |
| `native-x86-64-function-calls` | `make check` |
| `nominal-records` | `make records` |
| `public-re-exports` | `make re-exports` |
| `reproducible-bootstrap` | `make bootstrap` |
| `rust-crate-shim` | `make rust-shim` |
| `self-recompile` | `make selfhost-profile` |
| `selfhost-native-corpus` | `make selfhost-native` |
| `source-extension` | `make repository-check` |
| `stable-diagnostics` | `make diagnostics` |
| `stage2-core-lowering` | `make stage2` |
| `stage2-typed-sidecar` | `make typed-sidecar-projector` |
| `stdio-language-server` | `make lsp` |
| `syscall-file-round-trip` | `make stdlib` |
| `syscall-stdlib-api` | `make stdlib` |
| `test-skip-reporting` | `make test` |
| `tree-sitter-grammar` | `make tree-sitter` |
| `wasm32-arithmetic-core` | `make wasm` |
