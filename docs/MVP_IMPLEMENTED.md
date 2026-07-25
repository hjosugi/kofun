# Implemented status

| Capability | Status | Gate |
|---|---|---|
| `.kofun` source extension | implemented | `make repository-check` |
| Kofun-written compiler seed | implemented | `compiler.kofun` |
| Python-free bootstrap | implemented | `bootstrap/stage1/check.sh` |
| arithmetic Core validation/emission | implemented | `tests/cli.sh` |
| build/run/check/test CLI | Core only | `tests/cli.sh` |
| explicit skip reporting and coverage | implemented | `kofun test` |
| semantic compiler self-recompile | first runnable compiler generation implemented; three-generation fixed point open | `bootstrap/selfhost/check-compiler-driver.sh` |
| Stage 2 lexer, parser, and integer Core lowering | checkpoint implemented | `bootstrap/stage2/check.sh` |
| qualified module aliases | bounded same-package `import a.b as local`; local-only `AliasBindingId` preserves target identity, with no public/per-name/external aliases or `bin/kofun` routing | `tests/conformance/modules/import-aliases/run.sh`, `make import-aliases` |
| C11 user-function calls | bounded Int Core: recursion and forward calls | `bootstrap/stage2/check.sh` |
| x86-64 native user-function calls | bounded Int Core: six arguments, guarded returns, recursion | `tests/conformance/functions` |
| x86-64/AArch64 native Text-returning calls | bounded compiler-shaped profile with parameters, locals, concatenation, forwarding, and direct calls | `bootstrap/native/check.sh` |
| AArch64 native user-function calls | same bounded Int Core lowered to AArch64; executed under `qemu-aarch64` | `tests/conformance/functions`, `bootstrap/native/check.sh` |
| x86-64/AArch64 native Int64 values | literal magnitudes through `INT64_MAX`, backward-compatible deterministic encodings, and the complete signed range through checked expressions | `bootstrap/native/check.sh` |
| x86-64/AArch64 native integer division | `//` and `%` use the specified floor semantics; `/` is not defined on `Int` and both targets refuse it with one diagnostic (#687); both guard zero and non-representable quotients with canonical per-operator `R010` diagnostics | `tests/conformance/numeric`, `tests/conformance/functions`, `bootstrap/native/check.sh` |
| self-host success corpus as a native binary | the driver's five-`print` corpus reaches a static ELF on both targets and matches the self-host C11 path exactly | `bootstrap/selfhost/native/check-native-corpus.sh` |
| x86-64/AArch64 constant-stack returned calls | a `return` of a direct call branches instead of calling, so direct and mutual recursion in that position run in constant stack; proved by executing three million steps under a lowered stack limit | `bootstrap/native/check.sh`, `tests/conformance/functions` |
| stable diagnostics | canonical registry plus executable family owners; Stage 2 retains 33/33 codes and 3 explicit span debts | `tests/diagnostics/`, `make diagnostics` |
| explicit public re-exports | bounded same-package `pub import` / `pub from`; non-widening `ExportBindingId` edges, 64-edge chains, 1,024 bindings/module and 65,536 edges/package, KIF export facts, and facade/canonical tooling paths | `tests/conformance/modules/re-exports/run.sh`, `make re-exports` |
| deterministic compiler fuzzing | versioned oracle/backend observations for arithmetic plus focused grammar, value-if, match-guard, match-value, and enum-match families | `tests/fuzz/`, `make fuzz` |
| payload-free concrete enum matching | bounded Stage 2 C11 slice with constructor-set exhaustiveness | `tests/conformance/syntax/issues_35_47/run.sh`, `tests/fuzz/enum_match.sh` |
| general parser/type checker | open | no active gate |
| borrowed-List Copy/move ownership check | narrow Stage 2 checkpoint | `bootstrap/stage2/check.sh` |
| general ownership and law checking | open | no active general pass |
| ELF64/x86-64 native image writer | checkpoint implemented | `bootstrap/native/check.sh` |
| wasm32 Int64 arithmetic Core + lazy browser host | executable checkpoint | `bootstrap/wasm/check.sh`, `tests/conformance/numeric`, `examples/wasm-browser` |
| x86-64/AArch64 List[Int] Core | checkpoint implemented; AArch64 executes under qemu | `bootstrap/native/check.sh`, `tests/conformance/list` |
| x86-64/AArch64 UTF-8 Text Core | checkpoint implemented; AArch64 executes under qemu | `bootstrap/native/check.sh`, `tests/conformance/text` |
| general native lowering | open | unified types/control flow and additional target profiles |
| C ABI `extern` / `repr(C)` profile | bounded host-C implementation | `bootstrap/c_abi/check.sh` |
| vendored Rust crate through C ABI shim | implemented example | `examples/rust-shim/check.sh` |
| Linux HTTP/1.1 epoll framework through C ABI | bounded library implementation | `tests/http/check.sh` |
| Linux x86-64 native CLI application framework | bounded direct-static implementation | `framework/cli/check.sh` |
| Linux x86-64 syscall/stdlib API | Kofun source contract | `stdlib/tests/verify.sh` |
| syscall file round-trip execution | implemented | native ELF success and errno failure gates |
| Tree-sitter grammar | structural grammar, editor queries, recovery corpus, and repository source gate | `editor/tree-sitter-kofun/` |
| stdio language server | bounded diagnostics, symbols, completion, hover, definitions, and references | `tests/lsp/check.sh` |
| formatter and REPL | open | design only |
| checked Int64 contract | implemented for Core | numeric conformance corpus |

Historical prototypes do not count as active after their source is removed.
