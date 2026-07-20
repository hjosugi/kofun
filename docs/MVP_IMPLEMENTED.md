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
| Stage 2 lexer and structural function parser | checkpoint implemented | `bootstrap/stage2/check.sh` |
| general parser/type checker | open | no active gate |
| ownership and law checking | open | specification only |
| ELF64/x86-64 native image writer | checkpoint implemented | `bootstrap/native/check.sh` |
| general native lowering | open | integer exit fixture only |
| Linux x86-64 syscall/stdlib API | Kofun source contract | `stdlib/tests/verify.sh` |
| syscall file round-trip execution | open | explicit runner skip |
| formatter, REPL, LSP | open | design only |
| checked Int64 contract | implemented for Core | numeric conformance corpus |

Historical prototypes do not count as active after their source is removed.
