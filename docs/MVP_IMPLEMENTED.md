# Implemented status

| Capability | Status | Gate |
|---|---|---|
| `.kofun` source extension | implemented | `make repository-check` |
| Kofun-written compiler seed | implemented | `compiler.kofun` |
| Python-free bootstrap | implemented | `bootstrap/stage1/check.sh` |
| arithmetic Core validation/emission | implemented | `tests/cli.sh` |
| build/run/check/test CLI | Core only | `tests/cli.sh` |
| explicit skip reporting and coverage | implemented | `kofun test` |
| semantic compiler self-recompile | open | bootstrap manifest |
| Stage 2 lexer, parser, and integer Core lowering | checkpoint implemented | `bootstrap/stage2/check.sh` |
| C11 user-function calls | bounded Int Core: recursion and forward calls | `bootstrap/stage2/check.sh` |
| x86-64 native user-function calls | bounded Int Core: six arguments, guarded returns, recursion | `tests/conformance/functions` |
| general parser/type checker | open | no active gate |
| borrowed-List Copy/move ownership check | narrow Stage 2 checkpoint | `bootstrap/stage2/check.sh` |
| general ownership and law checking | open | no active general pass |
| ELF64/x86-64 native image writer | checkpoint implemented | `bootstrap/native/check.sh` |
| x86-64 List[Int] and UTF-8 Text Core | checkpoint implemented | `bootstrap/native/check.sh`, `tests/conformance/list`, `tests/conformance/text` |
| general native lowering | open | unified types/control flow and additional target profiles |
| C ABI `extern` / `repr(C)` profile | bounded host-C implementation | `bootstrap/c_abi/check.sh` |
| vendored Rust crate through C ABI shim | implemented example | `examples/rust-shim/check.sh` |
| Linux HTTP/1.1 epoll framework through C ABI | bounded library implementation | `tests/http/check.sh` |
| Linux x86-64 native CLI application framework | bounded direct-static implementation | `framework/cli/check.sh` |
| Linux x86-64 syscall/stdlib API | Kofun source contract | `stdlib/tests/verify.sh` |
| syscall file round-trip execution | implemented | native ELF success and errno failure gates |
| formatter, REPL, LSP | open | design only |
| checked Int64 contract | implemented for Core | numeric conformance corpus |

Historical prototypes do not count as active after their source is removed.
