# Schedule trace v1

Status: accepted normative design for GitHub issue #736.

Contract identifiers:

- trace: `kofun.schedule-trace/v1`
- witness: `kofun.schedule-witness/v1`
- modeled task semantics: `kofun.scoped-concurrency/model-v1`

This contract is deterministic test evidence for the structured-concurrency
direction in #555 and the virtual scheduler in #627. Kofun has no production
task scheduler today. The executable bounded model is neither a language
runtime nor evidence that `spawn`, channels, or handlers are implemented.

The words **must**, **must not**, **should**, and **may** are normative.

## Semantic model

Tasks are confined to lexical scopes. A scope cannot complete while a child is
live. Failure propagates through join or scope exit; cancellation recursively
cancels descendants and runs their cleanup. A task handle is an opaque affine
capability visible only to its creating task. It may be joined or cancelled
once, and may not be returned, stored globally, printed, compared, hashed, sent
through a channel, or placed in a trace.

The bounded model has one operation vocabulary shared by all four policies:
spawn, user `yield`, channel send/receive, join, cancel, ownership acquisition,
stdout append, failure, and completion. Scheduler implementation preemption is
not an event in v1; only explicit user `yield` is trace-visible.

## Stable scopes and task identities

The root scope is `s0` and its root task is `s0.t0`. Each task owns at most one
lexical child scope, named by appending `.s1`. Children are allocated in source
execution order within that scope as `.t1`, `.t2`, and so on. A nested child
therefore has an identity such as `s0.t0.s1.t1.s1.t1`.

IDs are semantic labels, not handles. Allocation depends only on the recorded
spawn operation. Addresses, host thread IDs, random map iteration, target
pointer width, source paths, and wall-clock time must not enter an ID.

## Decision and event records

Every step begins with one decision record:

```json
{"index":0,"runnable":["s0.t0"],"selected":"s0.t0"}
```

`runnable` is the complete ready queue in readiness order. It contains each ID
once. `selected` must be one member. FIFO selects index zero. Seeded scheduling
selects an index using `xorshift32-v1` over the same queue. Replay consumes the
checked-in selection. Exhaustive exploration forks once per runnable ID in that
order. A continuing task returns to the tail; wake and spawn also append to the
tail. Thus FIFO means readiness FIFO, not lexical-ID sorting.

The canonical semantic event kinds are:

| Event | Required meaning |
| --- | --- |
| `spawn` | parent created one child with the next deterministic ID |
| `yield` | selected task explicitly yielded |
| `block` | join or receive could not advance |
| `wake` | a child terminal state or channel send made a task runnable |
| `join` | parent observed a terminal child exactly once |
| `cancel` | target and live descendants entered cancellation |
| `send` / `receive` | one canonical channel value was transferred |
| `ownership` | read/edit/take acquisition or stable conflict observation |
| `stdout` | exact UTF-8 bytes appended by the model program |
| `complete` | task completed after all lexical children terminated |
| `failure` | stable language diagnostic and cancellation propagation began |

Events have monotonically increasing `sequence`, `kind`, `task`, `scope`, and a
kind-specific JSON `detail`. Object keys are lexicographically ordered by the
canonical writer; arrays preserve semantic order. Unknown fields or kinds are
rejected rather than ignored.

## Trace envelope and identity

A trace has exactly these fields: `schema`, `algorithm`, `target_semantics`,
`program_digest`, `seed`, `budgets`, `decisions`, and `events`. The program
digest is lowercase SHA-256 of canonical program JSON. Limits and semantics are
identity inputs: replay refuses any drift even if the next task happens to be
runnable. Canonical bytes are UTF-8 JSON, two-space indentation, one trailing
newline, no insignificant alternate spelling, and no duplicate JSON keys.

Supported algorithm identifiers are `fifo-v1`, `seeded-xorshift32-v1`,
`explicit-replay-v1`, and `exhaustive-dfs-v1`. A seeded witness records its
unsigned 32-bit seed. FIFO, replay, and exhaustive use `null`.

The fixed model maxima are 32 tasks, 256 decisions, 512 steps, and 4096 visited
states. A program declares equal or smaller positive `tasks`, `decisions`,
`steps`, and `states` budgets. Termination reports exactly one of `complete`,
`failure`, `deadlock`, or `budget`, with the reached bound named for `budget`.

## Strict replay

Replay first validates canonical bytes, schema/version, target semantics,
program digest, budgets, algorithm, seed shape, event fields, task-ID grammar,
and absence of authority-like fields. It then executes the same task model.

Stable rejection categories are:

| Code | Refusal |
| --- | --- |
| `ETRACE_PARSE` | malformed, duplicate-key, or non-canonical JSON |
| `ETRACE_VERSION` | unknown schema, algorithm, or target semantics |
| `ETRACE_PROGRAM` | digest mismatch |
| `ETRACE_BUDGET` | limits differ or exceed v1 maxima |
| `ETRACE_AUTHORITY` | handle, token, capability, address, or thread authority serialized |
| `EREPLAY_RUNNABLE` | recorded ready queue differs in membership or order |
| `EREPLAY_TASK` | selected ID is absent or invalid |
| `EREPLAY_EARLY` | decisions end while execution still needs one |
| `EREPLAY_SUFFIX` | terminal execution leaves an unconsumed decision |
| `EREPLAY_EVENT` | events or final observation differ |

There is no permissive replay and no best-effort task remapping. A truncated
trace is early exhaustion; an extra record is an unconsumed suffix. Changed
runnable membership is rejected even when the selected task still exists.

## Observations and witnesses

The portable terminal observation contains exact `stdout`, exact `stderr`, an
`exit_category`, the final `ownership_event` or `null`, and the stable
`diagnostic` or `null`. A failure witness contains the canonical program, seed,
complete trace, observation, and all declared budgets. It contains no live task
handle or capability. `model.mjs replay PROGRAM WITNESS` must reproduce every
observation field byte-for-byte.

External I/O, wall clock, entropy, signals, and host scheduling are forbidden
during exploration. A future runtime differential lane must replace them with
deterministic fixtures before claiming equivalent observations.

## Exhaustive exploration

Depth-first exploration branches on the ready queue and counts decisions,
steps, tasks, and unique semantic states. A state key includes task PCs and
status, readiness order, child/scope state, cancellation, channel contents,
stdout/stderr, joined handles, and all read/edit/take ownership claims. It must
not prune states that differ in authority, cleanup, or capability state.

Exploration stops each branch at its declared task/decision/step bound and
stops the whole search at the state bound. The report names every bound reached,
the explored terminal count, and the first canonical failure witness. Witness
selection is shortest decision count, then lexicographic selected-ID sequence.
This is deterministic shrinking, not a claim of globally minimal source code.

## Required adversaries

The corpus at `tests/concurrency/schedule-replay/` covers cancellation before
and after child completion, child failure propagation, nested lexical scopes,
out-of-order joins, channel block/wake, and sibling `edit`/`take` conflicts.
Its rejection corpus covers every strict-replay code. The authority case proves
that adding a serialized handle fails before execution.

Run `sh spec/concurrency/schedule-trace/check.sh`. It executes FIFO and seeded
runs twice, replays emitted witnesses, explores the bounded race space, proves
an explicit budget outcome, and checks every rejection. It does not invoke a
production scheduler because none exists.
