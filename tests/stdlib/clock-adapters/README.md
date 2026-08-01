# Clock adapter corpus

The executable producer for
[#848](https://github.com/hjosugi/kofun/issues/848), the bounded child of
[#647](https://github.com/hjosugi/kofun/issues/647): explicit monotonic and
system clock identities, a closed clock error, affine clock and sleeper
handles, and a deterministic fake clock with a scripted waiter.

```sh
sh tests/stdlib/clock-adapters/check.sh
```

## What is proved here

[`adapters.kofun`](adapters.kofun) is checked and run on two executors — the
reference interpreter and the C11 backend — and both must produce
[`adapters.stdout`](adapters.stdout) byte for byte. The gate then reads
specific lines of that output, so a change that keeps the program running but
alters a decision fails rather than passing with a new golden.

[`typed_hir.kofun`](typed_hir.kofun) is the typed-HIR half. The Stage 2
semantic sidecar projects a bounded program — 64 functions, 512 nodes — and
the producer is larger than that, so the witness carries the same four value
shapes and the same closed error through `kofun check --emit-typed-sidecar`
and then through the backend. The gate requires a *complete* projection that
names `ClockIdentity`, `MonotonicInstant`, `SystemInstant`, `Duration`, and
`ClockError`; a partial one fails.

| Claim | Evidence |
|---|---|
| monotonic and system time are distinct types | `mixed_instants.kofun` is rejected; the two shapes have different fields |
| a domain tag is not an identity | two monotonic clocks with different serials compare as different clocks |
| ordering rejects different identities | `WrongClockIdentity` carrying the observed serial |
| backwards time is typed, not clamped | `BackwardsTime` carrying the regression in nanoseconds |
| deadline arithmetic is checked | `ClockArithmeticOverflow` at the signed-`Int` limit, never a wrap |
| the clock handle is affine | a retained handle is refused with `StaleClockHandle`, and refusing it leaves the clock and the reading count untouched |
| the sleeper is a separate affine capability | registering, cancelling, and polling take a sleeper handle and hand back the next; a retained one is refused the same way |
| every transition returns one next state | each read, advance, register, cancel, and poll returns the next clock or table, and nothing mutates in place |
| equal deadlines wake in a stable order | two waiters registered with the same deadline wake in registration order, one per poll |
| cancellation is honoured | a cancelled waiter never wakes, and cancelling a woken waiter is refused rather than silently repeated |
| the handle that cancelled is spent | reusing the pre-cancel sleeper handle is refused, and the cancellation it made is still there afterwards |
| the platform-failure path runs without a platform | the fake clock is scripted to fail and returns `PlatformReadFailed` |
| no wall-clock read occurs | the emitted C contains no time header and no time symbol, and two runs are byte-identical |
| the clock types survive to typed HIR | `typed_hir.kofun` emits a complete Stage 2 semantic sidecar naming all five |

## Why this is a projection

The canonical surface is
[`stdlib/clock/adapters.kofun`](../../../stdlib/clock/adapters.kofun): nested
records, ADT payloads of any shape, `Result`, `List`, and `edit` parameters.
The Stage 2 Core that lowers a program to an executable today is smaller —
record fields are `Int` or `Bool`, an ADT constructor carries at most one
`Int`, `let` binds `Int` expressions, and there are no loops — so the producer
spells the same decisions with the vocabulary the Core accepts.

Nothing is weakened by that. The distinct types are still distinct nominal
types, the error domain is still a closed ADT reached by `match`, and the
affine handle is still checked on every transition. What changes is spelling:
an identity is two `Int` fields instead of a nested record, an outcome is a
record carrying a code that maps one-to-one onto a `ClockError` constructor
instead of `Result[T, ClockError]`, and the waiter table is three named slots
instead of a `List`.

The affine rule is a generation token rather than a move checker, because the
Stage 2 Core has no move checking to lean on: a handle names the generation it
was minted at, the owner of the state names the live one, and only a match is
accepted. #784 is the open design issue for a general affine handle; its
decision has not landed, so this projection does not pre-empt it.

The gate pins both files, so the canonical surface cannot drift away from the
projection that proves it.

## Known compiler boundary

`mixed_instants.kofun` and `monotonic_epoch_field.kofun` are rejected — the
toolchain exits non-zero and names `E2S32` — but they are rejected during
lowering, not by `kofun check`, and the message reaches the reader through the
C backend rather than as a frontend diagnostic. The gate asserts the rejection
and the reason, and deliberately does not assert that `check` alone catches
them, because today it does not. That is a Stage 2 diagnostic gap, not a clock
one.
