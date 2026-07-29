# Release evidence

Generated from `release/claims.json` by `make release-evidence`. Do not edit.

| Claim | Positive gate | Observation |
|---|---|---|
| `arithmetic-core` | `sh tests/cli.sh` | The arithmetic Core validates and emits without a diagnostic. |
| `borrowed-list-ownership` | `sh bootstrap/stage2/check.sh` | The borrowed-List Copy/move checkpoint passes. |
| `c-abi-profile` | `sh bootstrap/c_abi/check.sh` | The C ABI profile builds and round-trips through host C. |
| `c11-function-calls` | `sh bootstrap/stage2/check.sh` | Int Core user-function calls lower and execute through C11. |
| `checked-int64-contract` | `sh tests/conformance/run.sh` | The numeric conformance corpus passes on every supported backend. |
| `cli-commands` | `sh tests/cli.sh` | Each subcommand succeeds on Core sources. |
| `cli-framework` | `sh framework/cli/check.sh` | The declarative CLI example builds statically and runs. |
| `compiler-seed` | `sh bootstrap/stage1/check.sh` | The audited C seed accepts and executes one corpus covering all 15 typed profile builtins, both `len` overloads, argv/file I/O, Text operations, character predicates, Unicode validation, and stdout. All 30 builtin arity/type boundary rows exit nonzero and write no C, while the older corpora remain byte-identical. |
| `deterministic-fuzzing` | `sh tests/fuzz/semantic_differential.sh` | Oracle and backend observations agree for every generated program. |
| `elf64-image-writer` | `sh bootstrap/native/check.sh` | A static ELF64 image is written and executes. |
| `enum-matching` | `sh tests/conformance/syntax/issues_35_47/run.sh` | Payload-free enum matches lower and execute with exhaustiveness enforced. |
| `http-framework` | `sh tests/http/check.sh` | The bounded HTTP/1.1 server accepts and answers gated requests. |
| `module-aliases` | `sh tests/conformance/modules/import-aliases/run.sh` | Same-package aliases resolve while preserving target identity. |
| `native-aarch64-function-calls` | `sh bootstrap/native/check.sh` | AArch64 images are emitted and, with qemu present, execute to the expected values. |
| `native-constant-stack-returns` | `sh bootstrap/native/check.sh` | Three million recursive steps complete under a lowered stack limit. |
| `native-int64-values` | `sh bootstrap/native/check.sh` | Int64 magnitudes and checked expressions emit deterministic encodings on both targets. |
| `native-integer-division` | `sh bootstrap/native/check.sh` | Floor division and remainder agree with the specification on both targets. |
| `native-list-int-core` | `sh bootstrap/native/check.sh` | `List[Int]` programs lower and execute on both targets. |
| `native-text-returning-calls` | `sh bootstrap/native/check.sh` | Text-returning calls lower and execute on both targets. |
| `native-utf8-text-core` | `sh bootstrap/native/check.sh` | UTF-8 `Text` programs lower and execute on both targets. |
| `native-x86-64-function-calls` | `sh tests/conformance/run.sh` | The functions corpus passes on the x86-64 native backend. |
| `nominal-records` | `make records` | The scanner fixture constructs, passes, returns, reads, and lays out nominal records through the bounded typed frontend. |
| `public-re-exports` | `sh tests/conformance/modules/re-exports/run.sh` | Re-export chains resolve within the stated bounds and preserve binding identity. |
| `reproducible-bootstrap` | `sh bootstrap/stage1/check.sh` | Regeneration reproduces the checked-in Stage 1 artifact and its digest. |
| `rust-crate-shim` | `sh examples/rust-shim/check.sh` | The vendored crate builds and answers through the C ABI shim. |
| `self-recompile` | `sh bootstrap/selfhost/check-compiler-driver.sh` | The compiler-produced compiler emits byte-identical C to the audited seed for every accept corpus, including all 15 profile builtins, and agrees on all 30 builtin arity/type refusals. |
| `selfhost-native-corpus` | `sh bootstrap/selfhost/native/check-native-corpus.sh` | The native and C11 self-host paths produce identical output. |
| `source-extension` | `make repository-check` | No Python or `.kf` sources remain and the editor extension registers `.kofun`. |
| `stable-diagnostics` | `sh tests/diagnostics/check.sh` | Every registry code has an owner and every fixture matches exactly. |
| `stage2-core-lowering` | `sh bootstrap/stage2/check.sh` | Integer Core sources lex, parse, lower and execute. |
| `stage2-typed-sidecar` | `make typed-sidecar-projector` | Stage 2 events project into a schema-valid typed-sidecar v1 document. |
| `stdio-language-server` | `sh tests/lsp/check.sh` | Every supported request answers and the measured percentiles stay inside their budgets. |
| `syscall-file-round-trip` | `sh stdlib/tests/verify.sh` | The file round-trip succeeds and returns the written bytes. |
| `syscall-stdlib-api` | `sh stdlib/tests/verify.sh` | The declared syscall and stdlib contracts check and execute. |
| `test-skip-reporting` | `sh tests/cli.sh` | Skipped cases appear in the report with their own count. |
| `tree-sitter-grammar` | `make tree-sitter` | The grammar parses the repository corpus and the recovery cases. |
| `wasm32-arithmetic-core` | `sh bootstrap/wasm/check.sh` | wasm32 modules are emitted and execute to the expected values. |
