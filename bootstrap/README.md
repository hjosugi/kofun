# Kofun bootstrap

- `manifest.json`: machine-readable stage status
- `stage1/compiler.kf`: compiler seed written in Kofun
- `fixtures/answer.kf`: Kofun Core input used by the bootstrap test
- `check_bootstrap.py`: interpreted/native Stage 1 differential verification

Run:

```bash
PYTHONPATH=src python3 bootstrap/check_bootstrap.py
```

The check performs both paths:

```text
Stage 0 interpreter -> Stage 1 -> fixture C11
Stage 0 C11 backend -> native Stage 1 -> fixture C11
```

The two C11 artifacts must be byte-identical, then compile and print `42`. The native Stage 1 seed is working, but the Stage 2 self-recompile gate is still open. See `docs/SELF_HOSTING.md`.
