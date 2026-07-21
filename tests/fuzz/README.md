# Deterministic compiler fuzz smoke tests

`grammar.sh` generates bounded random token streams and requires the Stage 2
lexer/parser to terminate with either a valid projection or a normal
diagnostic. A per-case watchdog turns hangs into failures; signals and other
abnormal statuses fail the gate.

`semantic_differential.sh` generates valid arithmetic programs, calculates the
expected result independently, and compares exact stdout/stderr across the C11
reference and direct x86-64 backend. It is deterministic so every CI failure
can be reproduced from its case number and generated source.

`value_if.sh` generates valid bounded Int-valued `if` programs for Stage 2,
calculates the selected result independently, and places checked
division-by-zero in every unselected branch. This catches eager branch
evaluation as well as wrong comparison or result lowering. The regular and
ASAN/UBSAN compiler builds must also emit byte-identical C, IR, and token
artifacts for every generated case.

`match_guard.sh` generates 32 valid guarded Bool `match` programs for Stage 2.
Each program independently expects two ordered guard probes followed by the
selected arm value. A division-by-zero guard is placed behind a nonmatching
pattern and another behind an already-selected matching pattern, so eager or
out-of-order guard evaluation fails at runtime. Unguarded `true` and `false`
fallbacks provide static coverage without affecting the expected output. The
regular and ASAN/UBSAN compiler builds must emit byte-identical C, IR, and token
artifacts for every case.

`match_value.sh` generates 32 Int-valued Bool `match` programs with alternating
scrutinees. Each case proves ordered guard probes, false-guard fallthrough,
nested value `if`, and selected-only arm evaluation while division-by-zero is
hidden behind nonmatching, unselected, and post-selection paths. Normal and
ASAN/UBSAN compiler builds must emit byte-identical C, IR, and token artifacts.

`match_value_invalid.sh` generates 32 invalid value matches across missing and
guard-only coverage (`E2S25`), unreachable arms (`E2S26`), invalid guards
(`E2S29`), and Void, empty, or multi-value arms (`E2S30`). Normal and
ASAN/UBSAN compilers must agree on status, diagnostic code, IR, and token tape,
write no internal stderr, and emit no C artifact.

`enum_match.sh` generates 32 valid and 32 invalid payload-free enum programs.
The valid side checks constructor selection, ordered guards, catch-all
fallbacks, and selected-only execution. The invalid side covers missing and
unreachable constructors (`E2S25`/`E2S26`), malformed or colliding declarations
(`E2S31`), and unknown or mismatched enum uses (`E2S32`). Normal and ASAN/UBSAN
compiler builds must agree on diagnostics and emitted artifacts, and both the
normal and sanitized generated C programs must produce the expected output.
The invalid corpus also crosses the 256-occurrence per-function enum-use bound
and requires `E2S32` without a C artifact.

Run all fuzz smoke gates:

```sh
make fuzz
```

These are bounded CI smoke budgets, not a replacement for long-running
coverage-guided fuzzing. The semantic gate uses the active C11 reference
because a general Kofun interpreter is not yet part of the Python-free
toolchain.
