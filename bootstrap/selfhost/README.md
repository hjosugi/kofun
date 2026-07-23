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
make selfhost-profile
```

## What the status columns mean

Each profile row has evidence slots for the canonical source, typed frontend,
C11 lowering, compiler-produced compiler, positive test, negative test, and
differential test.

- a repository path means that evidence exists;
- `planned:#NNN` names the issue that must supply the evidence;
- `partial` means at least the frozen source evidence exists but the complete
  self-compile chain does not;
- `complete` is allowed only when every evidence slot is a checked-in path.

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
