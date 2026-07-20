# Issue 32: Stage 2 fixed point

## Verified starting point

The repository has three distinct working checkpoints:

1. the audited Stage 1 C11 seed builds a Kofun-written arithmetic-Core
   compiler;
2. the Stage 2 frontend produces stable source, token-tape, and structural IR
   projections and lowers a small integer `main` to C11; and
3. the native checkpoint executes deterministic Kofun-authored ELF fixtures.

The Stage 2 source projection reaches a byte fixed point, but this is not a
compiler fixed point. Stage 1 cannot parse and lower its own `Text`,
`List[Text]`, direct-call, loop, file-I/O, and string-building implementation.

## Artifact contract

The fixed-point gate uses these names conceptually:

```text
S   canonical compiler source
A1  executable compiler produced from the audited trusted seed and S
A2  executable compiler produced by running A1 on S
A3  executable compiler produced by running A2 on S
```

All build inputs, target triples, layout rules, and reproducibility settings
must be identical. Success requires:

```text
sha256(A1) == sha256(A2) == sha256(A3)
cmp A1 A2
cmp A2 A3
```

Equality of generated C text, token tapes, structural IR, stdout, or behavior
alone is useful differential evidence but cannot replace executable byte
equality for this issue.

The manifest entry that closes the gate must record at least:

- canonical source path and SHA-256;
- trusted seed path and SHA-256;
- target triple and artifact format;
- exact reproduction command;
- SHA-256 for `A1`, `A2`, and `A3`;
- compiler version and source revision; and
- successful semantic conformance corpus digest.

The gate must rebuild in two clean temporary directories and reject an
undeclared environment-dependent difference.

## Required implementation order

1. Represent every construct used by canonical compiler source in parsed and
   typed IR.
2. Lower `Text`, `List[Text]`, calls, returns, branches, loops, mutation, and
   bootstrap file operations.
3. Compile the compiler source and run the resulting artifact on the existing
   arithmetic and error corpus.
4. Produce `A2` and `A3` through the artifact chain above.
5. Compare exact bytes and update the manifest only in the same verified
   change.

Each language expansion must add a focused positive fixture, at least one
compile-fail fixture, and a differential execution case before it is used by
the compiler source.

## Executable close checklist

- [x] Canonical Stage 1 and Stage 2 source and audited seed hashes are checked.
- [x] Stage 2 source/token/IR projection is deterministic.
- [x] A deliberately small integer Core lowers and executes deterministically.
- [ ] Stage 1 accepts every construct in its canonical source.
- [ ] `A1` successfully compiles canonical compiler source.
- [ ] `A2` successfully compiles canonical compiler source.
- [ ] `A1`, `A2`, and `A3` are byte-identical executables.
- [ ] The bootstrap corpus has identical values, statuses, and diagnostics.
- [ ] `bootstrap/manifest.json` closes both self-recompile gates and records
      the required hashes.
