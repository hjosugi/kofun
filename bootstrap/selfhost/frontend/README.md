# Self-host frontend evidence

This directory holds the executable evidence that the Stage 2 frontend can
parse, resolve, and type the frozen self-host source `S`
(`bootstrap/stage1/compiler.kofun`) into one complete
`kofun.selfhost-hir/v1` document, per the contract frozen in
`bootstrap/selfhost/hir-v1.md`.

- `S.hir` — the complete typed document for the exact frozen `S` digest:
  deduplicated type table, scope tree, symbols (functions, the 16 profile
  builtins plus the `len` List[Text] overload, parameters, locals),
  bindings, and per-function pre-order node records with types, ownership
  modes, and exact byte spans.
- `reject_*.kofun` / `reject_*.hir` — compile-fail fixtures: each produces
  exit 1 and its exact rejected document (stable diagnostic code, span, and
  message; never a partial typed document; the out-of-profile `match`
  fixture additionally carries an explicit `unsupported` record).
- `check-frontend.sh` — the gate: rebuilds the Stage 2 seed, re-derives the
  profile digest, re-emits and byte-compares every document, and checks
  determinism across repeated runs. Run it with `make selfhost-profile` or
  directly.

## Honest boundary

The emitter currently lives in the audited C seed
(`bootstrap/stage2/compiler.c`, `--emit-selfhost-hir INPUT OUTPUT DIGEST`).
Per the #618/#619 rules, a C-only checkpoint provides evidence but does not
complete profile rows: the `profile.tsv` frontend cells stay `planned:#619`
until the emitter is ported to the canonical Kofun source
(`bootstrap/stage2/compiler.kofun`) with its seed updated in lockstep. That
port is the remaining #654 work, and `make selfhost-frontend` stays red by
design until it lands.
