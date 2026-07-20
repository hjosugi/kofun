# Deterministic compiler fuzz smoke tests

`grammar.sh` generates bounded random token streams and requires the Stage 2
lexer/parser to terminate with either a valid projection or a normal
diagnostic. A per-case watchdog turns hangs into failures; signals and other
abnormal statuses fail the gate.

`semantic_differential.sh` generates valid arithmetic programs, calculates the
expected result independently, and compares exact stdout/stderr across the C11
reference and direct x86-64 backend. It is deterministic so every CI failure
can be reproduced from its case number and generated source.

Run both:

```sh
make fuzz
```

These are bounded CI smoke budgets, not a replacement for long-running
coverage-guided fuzzing. The semantic gate uses the active C11 reference
because a general Kofun interpreter is not yet part of the Python-free
toolchain.
