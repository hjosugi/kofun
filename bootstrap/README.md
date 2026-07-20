# Kofun bootstrap

- `stage1/compiler.kofun`: canonical Kofun compiler source
- `stage1/compiler.c`: audited C11 bootstrap artifact
- `stage1/SHA256SUMS`: source and seed digests
- `stage1/check.sh`: Python-free build and fixture gate
- `stage2/compiler.kofun`: Kofun lexer, structural parser, IR, and stable emitter
- `stage2/compiler.c`: audited executable checkpoint seed
- `stage2/check.sh`: deterministic source/token/IR round-trip gate
- `native/encoder.kofun`: direct ELF64/x86-64 encoder
- `native/check.sh`: Kofun Core to executable Linux image gate
- `fixtures/answer.kofun`: arithmetic Core fixture

Run all three checkpoints:

```sh
sh bootstrap/stage1/check.sh
sh bootstrap/stage2/check.sh
sh bootstrap/native/check.sh
```

Stage 1 builds the Kofun-written compiler seed and compiles the arithmetic
fixture. Stage 2 validates a deterministic semantic-frontend boundary. Native
builds and executes a static ELF64 fixture. Semantic self-recompilation and a
general native compiler remain open.
