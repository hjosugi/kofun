# Explicit Clock checkpoint

[`clock.kofun`](clock.kofun) defines clock-tagged instants, non-negative
durations, elapsed-time and deadline arithmetic, and a deterministic manual
clock. [`linux_x86_64.kofun`](linux_x86_64.kofun) is the explicit system-read
adapter.

```kofun
let start = match clock_instant(MonotonicClock, 10, 750000000) {
    Ok(value) => value,
    Err(error) => return Err(error),
}
let mut clock = match clock_manual(start) {
    Ok(value) => value,
    Err(error) => return Err(error),
}
let step = match clock_duration(1, 500000000) {
    Ok(value) => value,
    Err(error) => return Err(error),
}
clock_manual_advance(clock, step)
```

The canonical surface is deliberately small:

| operation | contract | cost |
|---|---|---|
| `clock_instant(clock, seconds, nanoseconds)` | constructs a tagged instant; nanoseconds must be `0 .. 999999999` | O(1) |
| `clock_duration(seconds, nanoseconds)` | constructs a non-negative canonical duration | O(1) |
| `clock_compare(left, right)` | returns `-1`, `0`, or `1`; different clocks are `Err` | O(1) |
| `clock_elapsed(start, end)` | returns a non-negative duration; backwards, mismatched, and overflowing spans are `Err` | O(1) |
| `clock_add(instant, duration)` | computes a deadline with checked carry and signed-`Int` overflow | O(1) |
| `clock_deadline_reached(now, deadline)` | compares same-clock values without reading ambient time | O(1) |
| `clock_manual(start)` / `clock_manual_advance(clock, duration)` | creates and advances caller-owned deterministic clock state | O(1) |
| `clock_read(clock)` | Linux x86-64 `clock_gettime(2)` adapter returning a typed system/read error | one syscall |

`MonotonicClock` is the domain for measuring elapsed time and scheduling
deadlines. `RealtimeClock` is a Unix-era observation and may move because of
clock adjustment; `clock_elapsed` reports `EndBeforeStart` when that happens.
Instants from those domains are never compared or subtracted. Durations cannot
be negative, and neither constructor silently normalizes an out-of-range
nanosecond field. Arithmetic overflow is `ClockArithmeticOverflow`, not a
sentinel or wraparound.

The manual clock performs no ambient read. Its state is mutable and must be
passed with `edit`, so tests and simulations control every transition. The
Linux adapter is the only file in this module that reads system time. It uses
the existing safe syscall wrapper and does not declare a new trusted intrinsic.

## Current boundary

This is an executable checkpoint, not a claim that the full Clock backlog is
complete. The active compiler still rejects the canonical records and ADTs
before code generation. The focused gate therefore runs an audited Int-Core
projection of the same validation, ordering, elapsed-time borrow, addition
carry, overflow, deadline, and manual-advance rules through the C11 backend.
The checked vectors include the signed-`Int` limits.

Record fields cannot yet be made module-private. Callers must use
`clock_instant`, `clock_duration`, and `clock_manual`; fabricating invalid
records is outside the contract until opaque types are implemented.

This checkpoint does not provide sleeping, timers, time zones, calendar
conversion, formatting/parsing, serialization, a parallel scheduler, or
cross-platform adapters. It does not promise realtime monotonicity or compare
values from different clock domains. Those remain later lifecycle work rather
than undocumented behavior.

Run the Python-free focused gate with:

```sh
sh stdlib/clock/tests/verify.sh
```
