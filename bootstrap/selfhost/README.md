# First self-host profile

This directory freezes the smallest honest compiler source profile for the
first semantic self-recompile. The canonical source `S` is the existing,
reviewed seed:

```text
bootstrap/stage1/compiler.kofun
```

It is deliberately reused rather than copied into a second self-host tree.
`profile.meta` pins its digest, `profile.tsv` records every language and host
feature used by that source, and `check-profile.sh` rejects an unreviewed
source/profile drift.

Run the gate with:

```sh
task selfhost-profile
```

Run the actual compiler self-compile slice with:

```sh
task selfhost-self-compile
```

That gate builds `A1`, has it compile the exact canonical `S` bytes from
different directories and source names, and requires the two nonempty `C2`
outputs to be byte-identical and valid strict C11. On hosts exposing
`ulimit -v`, each compilation also runs under a 1.5 GiB address-space ceiling.
This closes the first `A1(S)` step; it does not claim the three-generation
fixed point below.

## Typed HIR contract and phase gates

`hir-v1.md` freezes the versioned `kofun.selfhost-hir/v1` schema: the typed
HIR that the #619 frontend must produce and that #620–#622 consume without
reparsing source text. Phase completion gates check evidence cells owned by
one implementation step:

```sh
sh bootstrap/selfhost/check-profile.sh --phase frontend
sh bootstrap/selfhost/check-profile.sh --phase c11-text
sh bootstrap/selfhost/check-profile.sh --phase c11-control
```

The frontend phase gate fails whenever any profile row's frontend cell
lacks checked-in evidence, listing each pending cell explicitly; it is the
#619 acceptance check. Since #654 landed the canonical-source port of the
typed-HIR emitter and per-family fixtures, all 46 frontend cells carry
evidence in `frontend/` and the gate runs green inside `task verify`.
The c11-text and c11-control gates are the matching #620/#621
completion checks for the c11 cells owned by the Text/function slice
and the mutation/loop/List slice. #622 completed the remaining host
cells and every other evidence column, and
`check-compiler-driver.sh` proves the trusted seed compiles the frozen
`S` into a runnable compiler whose Core-corpus behavior matches the
audited Stage 1 seed byte for byte (`driver/`).

## What the status columns mean

Each profile row has evidence slots for the canonical source, typed frontend,
C11 lowering, four separated compiler-evidence classes, positive test,
negative test, and differential test.

- a repository path means that evidence exists;
- `planned:#NNN` names the issue that must supply the evidence;
- `partial` means at least the frozen source evidence exists but the complete
  self-compile chain does not;
- `complete` requires every evidence class below to be **executed**, not
  merely present.

### The four compiler-evidence classes

`kofun.selfhost-profile/v2` replaces the single `self_compiler` column, which
held one path — `driver/S.c` — on all 46 rows and was checked by testing that
the file existed. One path cannot distinguish four different facts, and file
existence proves none of them. Each row now declares a prover per class, and
`check-profile.sh` runs it:

| Class | Cell | What runs |
|---|---|---|
| `used_by_s` | `inventory:S` | the feature must appear in the inventory the gate derives from the pinned canonical source |
| `accepted_by_a1` | `a1-accept:<corpus>` | A1 compiles `driver/<corpus>.kofun`, exits zero, and writes no diagnostic |
| `lowered_by_a1` | `a1-lower:<corpus>` | A1's emitted C is byte-identical to the reviewed `driver/<corpus>.c` |
| `self_application` | `gate:selfhost-self-compile` | A1 compiles the canonical source into a nonempty `C2`, and the named task target runs `check-compiler-driver.sh` inside the aggregate verification |

A row may not claim a corpus that does not use its feature: the gate derives
the corpus's own inventory with the same detector it applies to `S` and
requires the row's key to be in it. `A1` itself is built once from the reviewed
`driver/S.c`; that file's correspondence to `S` is the self-compile gate's
property, so this gate reads it rather than re-deriving it. Set
`KOFUN_SELFHOST_A1` to reuse an already-built binary.

The full self-compile proof — determinism, path independence, the audited
hand-port differential, and the strict-C11 host boundary — stays in
`check-compiler-driver.sh` and is not re-implemented here.

41 of the 46 rows are `complete`. Five are `partial`: `builtin|fail`,
`control|return-void`, `statement|assignment`, `statement|mutable-local`, and
`syntax|function-parameter` are used by `S` but appear in no driver corpus, so
A1 has never been run against a fixture containing them.
[#947](https://github.com/hjosugi/kofun/issues/947) owns that corpus gap; until
it lands, those rows claim `used_by_s` and `self_application` only.

The profile gate derives built-in calls and the bounded syntax/type inventory
from `S`, then compares it with the manifest. Changing `S` therefore requires
an explicit review of both its SHA-256 and coverage rows.

## Fixed-point boundary

The first fixed point permits generated deterministic C11 and one normalized,
declared host C compiler:

```text
S --trusted seed--> C1 --host cc--> A1
A1(S)------------> C2 --host cc--> A2
A2(S)------------> C3 --host cc--> A3
```

Success requires byte-identical `C1/C2/C3` and byte-identical `A1/A2/A3`.
Direct-native compiler reproduction is a separate strengthening track; it does
not block this first C11 fixed point.

The implementation order is
[#619](https://github.com/hjosugi/kofun/issues/619) through
[#622](https://github.com/hjosugi/kofun/issues/622), followed by the executable
generation gates in
[#271](https://github.com/hjosugi/kofun/issues/271) and
[#272](https://github.com/hjosugi/kofun/issues/272).
