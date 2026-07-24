# Self-host compiler driver evidence

This directory holds the #622 evidence: the trusted Stage 2 seed
compiles the frozen self-host source `S`
(`bootstrap/stage1/compiler.kofun`) as one ordinary input into a
runnable compiler, through the single source-to-C command

```sh
kofun-stage2 --selfhost-compile INPUT.kofun OUTPUT.c SOURCE-SHA256
```

with no hidden Stage 1/2 fallback: the typed `kofun.selfhost-hir/v1`
document is produced and lowered in memory, a rejected source prints
its stable frontend diagnostic and writes nothing, and there is no
filename or hash special case.

- `S.c` — the deterministic C11 emitted for the exact frozen `S`
  digest; `check-compiler-driver.sh` re-derives it byte for byte, twice.
- `corpus_answer.kofun` / `.c` / `.stdout` — the success corpus: the
  compiler built from `S.c` compiles this Core input to C that is
  byte-identical to what the audited Stage 1 seed emits, prints the
  same stdout, and the compiled program reproduces the pinned output.
- `corpus_reject.kofun` / `.stdout` — the failure corpus: both
  compilers refuse an out-of-Core source with the same diagnostic bytes
  and write nothing.

The gate also checks path independence (the same relative input
compiled from two different directories emits identical C), determinism
across repeated runs, and bounded I/O failure (a missing input panics
with the runtime's explicit message, exits 1, and preserves the
previous output bytes).

The host boundary in generated programs is the audited runtime shim
documented in `../c11/README.md`: bounded argument decoding through
`kofun_rt_args` (the program name is excluded, exactly like the trusted
seed), whole-file `read_text`/`write_text` with explicit panics, and
`print` as line-buffered stdout. Known deliberate deviation from the
Stage 1 seed, unchanged from #620: `is_xid_continue` consults the real
Unicode 17 tables rather than the seed's historical `>= 0x80`
approximation, so the corpus differential stays within ASCII inputs.
