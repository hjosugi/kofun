# Implemented status

| Capability | Status | Gate | Claim |
|---|---|---|---|
| `.kofun` source extension | implemented | `make repository-check` | `source-extension` |
| Kofun-written compiler seed | implemented: nested-block Int/Bool Core | `bootstrap/stage1/check.sh` | `compiler-seed` |
| Reproducible bootstrap | implemented | `bootstrap/stage1/check.sh` | `reproducible-bootstrap` |
| arithmetic Core validation/emission | implemented | `tests/cli.sh` | `arithmetic-core` |
| build/run/check/test CLI | Core only | `tests/cli.sh` | `cli-commands` |
| explicit skip reporting and coverage | implemented | `kofun test` | `test-skip-reporting` |
| semantic compiler self-recompile | first runnable compiler generation implemented; three-generation fixed point open | `bootstrap/selfhost/check-compiler-driver.sh` | `self-recompile` |
| Stage 2 lexer, parser, and integer Core lowering | checkpoint implemented | `bootstrap/stage2/check.sh` | `stage2-core-lowering` |
| Stage 2 semantic tooling output | bounded compiler-derived KSE projects one-way into canonical non-authoritative typed-sidecar v1 for explicit single-file `kofun check`; compiler/KIF/cache consumers remain forbidden | `make stage2-events`, `make typed-sidecar-projector` | `stage2-typed-sidecar` |
| qualified module aliases | bounded same-package `import a.b as local`; local-only `AliasBindingId` preserves target identity, with no public/per-name/external aliases or `bin/kofun` routing | `tests/conformance/modules/import-aliases/run.sh`, `make import-aliases` | `module-aliases` |
| C11 user-function calls | bounded Int Core: recursion and forward calls | `bootstrap/stage2/check.sh` | `c11-function-calls` |
| x86-64 native user-function calls | bounded Int Core: six arguments, guarded returns, recursion | `tests/conformance/functions` | `native-x86-64-function-calls` |
| x86-64/AArch64 native Text-returning calls | bounded compiler-shaped profile with parameters, locals, concatenation, forwarding, and direct calls | `bootstrap/native/check.sh` | `native-text-returning-calls` |
| AArch64 native user-function calls | same bounded Int Core lowered to AArch64; executed under `qemu-aarch64` | `tests/conformance/functions`, `bootstrap/native/check.sh` | `native-aarch64-function-calls` |
| x86-64/AArch64 native Int64 values | literal magnitudes through `INT64_MAX`, backward-compatible deterministic encodings, and the complete signed range through checked expressions | `bootstrap/native/check.sh` | `native-int64-values` |
| x86-64/AArch64 native integer division | `//` and `%` use the specified floor semantics; `/` is not defined on `Int` and both targets refuse it with one diagnostic (#687); both guard zero and non-representable quotients with canonical per-operator `R010` diagnostics | `tests/conformance/numeric`, `tests/conformance/functions`, `bootstrap/native/check.sh` | `native-integer-division` |
| self-host success corpus as a native binary | the driver's five-`print` corpus reaches a static ELF on both targets and matches the self-host C11 path exactly | `bootstrap/selfhost/native/check-native-corpus.sh` | `selfhost-native-corpus` |
| x86-64/AArch64 constant-stack returned calls | a `return` of a direct call branches instead of calling, so direct and mutual recursion in that position run in constant stack; proved by executing three million steps under a lowered stack limit | `bootstrap/native/check.sh`, `tests/conformance/functions` | `native-constant-stack-returns` |
| stable diagnostics | canonical registry plus executable family owners; Stage 2 retains 46/46 codes and 3 explicit span debts | `tests/diagnostics/`, `make diagnostics` | `stable-diagnostics` |
| explicit public re-exports | bounded same-package `pub import` / `pub from`; non-widening `ExportBindingId` edges, 64-edge chains, 1,024 bindings/module and 65,536 edges/package, KIF export facts, and facade/canonical tooling paths | `tests/conformance/modules/re-exports/run.sh`, `make re-exports` | `public-re-exports` |
| deterministic compiler fuzzing | versioned oracle/backend observations for arithmetic plus focused grammar, value-if, match-guard, match-value, and enum-match families | `tests/fuzz/`, `make fuzz` | `deterministic-fuzzing` |
| payload-free concrete enum matching | bounded Stage 2 C11 slice with constructor-set exhaustiveness | `tests/conformance/syntax/issues_35_47/run.sh`, `tests/fuzz/enum_match.sh` | `enum-matching` |
| nominal heterogeneous records | bounded typed frontend only: declaration, labelled construction, typed reads, ownership and layout facts, and a scanner that produces and consumes `List[Token]`; no module, generic, or backend lowering | `make records`, `spec/records-v1.md` | `nominal-records` |
| general parser/type checker | open | no active gate | `general-parser-type-checker` |
| borrowed-List Copy/move ownership check | narrow Stage 2 checkpoint | `bootstrap/stage2/check.sh` | `borrowed-list-ownership` |
| general ownership and law checking | open | no active general pass | `general-ownership-checking` |
| ELF64/x86-64 native image writer | checkpoint implemented | `bootstrap/native/check.sh` | `elf64-image-writer` |
| wasm32 Int64 arithmetic Core + lazy browser host | executable checkpoint | `bootstrap/wasm/check.sh`, `tests/conformance/numeric`, `examples/wasm-browser` | `wasm32-arithmetic-core` |
| x86-64/AArch64 List[Int] Core | checkpoint implemented; AArch64 executes under qemu | `bootstrap/native/check.sh`, `tests/conformance/list` | `native-list-int-core` |
| x86-64/AArch64 UTF-8 Text Core | checkpoint implemented; AArch64 executes under qemu | `bootstrap/native/check.sh`, `tests/conformance/text` | `native-utf8-text-core` |
| general native lowering | open | unified types/control flow and additional target profiles | `general-native-lowering` |
| C ABI `extern` / `repr(C)` profile | bounded host-C implementation | `bootstrap/c_abi/check.sh` | `c-abi-profile` |
| vendored Rust crate through C ABI shim | implemented example | `examples/rust-shim/check.sh` | `rust-crate-shim` |
| Linux HTTP/1.1 epoll framework through C ABI | bounded library implementation | `tests/http/check.sh` | `http-framework` |
| Linux x86-64 native CLI application framework | bounded direct-static implementation | `framework/cli/check.sh` | `cli-framework` |
| Linux x86-64 syscall/stdlib API | Kofun source contract | `stdlib/tests/verify.sh` | `syscall-stdlib-api` |
| syscall file round-trip execution | implemented | native ELF success and errno failure gates | `syscall-file-round-trip` |
| Tree-sitter grammar | structural grammar, editor queries, recovery corpus, and repository source gate | `editor/tree-sitter-kofun/` | `tree-sitter-grammar` |
| stdio language server | bounded diagnostics, symbols, completion, hover, definitions, and references | `tests/lsp/check.sh` | `stdio-language-server` |
| formatter and REPL | open | design only | `formatter-and-repl` |
| checked Int64 contract | implemented for Core | numeric conformance corpus | `checked-int64-contract` |

Historical prototypes do not count as active after their source is removed.

The `Claim` column is the stable identity of each row in
[`release/claims.json`](../release/claims.json), which binds it to a positive
gate, a boundary that fails outside it, and a reproduction command.
`make release-claims` fails if a row here gains, loses, or reworders a
capability without the manifest following, so this table and the evidence
cannot drift apart. Status text lives here and is mirrored into the manifest;
do not restate it in the manifest by hand.
