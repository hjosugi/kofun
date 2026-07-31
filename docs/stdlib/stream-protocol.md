# Stream protocol contract

## Status

This document is the accepted contract for GitHub issue
[#627](https://github.com/hjosugi/kofun/issues/627). No stream library is
implemented; acceptance fixes the protocol — `Stream`, `Subscription`,
demand, termination, and overflow semantics — so that implementation
children (protocol/types, deterministic test scheduler, core operators, I/O
adapters, native optimization) can be reviewed independently. Reactive
support is not a prerequisite for the self-host fixed point #618–#622.

Under the standard-library charter the protocol is a **portable
interface**; schedulers and I/O sources are platform adapters or
independently versioned modules.

The words **must**, **must not**, and **may** are normative.

## Decision summary

Kofun stays a small, orthogonal language: there is no `observable`,
`reactive`, scheduler, or coroutine keyword, and event composition is a
typed library contract that reads as an ordinary pipeline:

```kofun
events
|> stream.map(parse)
|> stream.filter_map(result.ok)
|> stream.take(100)
|> stream.subscribe(render)
```

Everything desugars to ordinary typed calls; syntax is reconsidered only if
a library prototype proves a repeated ambiguity or safety hole that
functions cannot solve.

## The protocol

### Types

- `Stream[T, E]` — a discrete sequence delivering `Next(T)` zero or more
  times, terminated by exactly one of `Error(E)` or `Complete`.
- `Subscription` — an affine (`take`) handle: cancelling it is
  deterministic, idempotent, promptly visible upstream, and releases owned
  resources on every terminal path. Dropping a `Subscription` cancels; the
  type system makes a leaked live subscription a visible fact, consistent
  with `read` / `edit` / `take`.

### Demand

- Downstream signals demand as an explicit typed credit: `request(n)`.
  A publisher must never emit more `Next` signals than requested, so every
  queue in a pipeline has a known bound. Signals to one subscriber are
  serial; after a terminal signal nothing further is delivered.

### Cold, hot, and overflow

- Sources are **cold by default**: each subscription starts its own work.
- A hot source (sensor, input events, fan-out) is an explicit `broadcast`
  adapter that requires a buffer policy at construction; nothing infers
  hotness. Replay is a separate explicit adapter with a bounded capacity.
- Buffer policy is a closed enum, always bounded: `wait` (apply
  backpressure), `drop_oldest`, `drop_newest`, `coalesce(fn)`, or `fail`.
  There is no unbounded default anywhere in the protocol.

### Schedulers

- Synchronous finite streams need no scheduler: subscribe, request, and
  deliver on the caller. A `Scheduler` appears only at an actual async
  boundary (#117/#555) and is always an explicit argument, never ambient.
- A deterministic virtual scheduler drives all conformance tests; no
  conformance fixture depends on wall-clock timing.

### Operators and laws

- v1 operator set: `map`, `filter`, `filter_map`, `scan`, `take`,
  `concat`, `merge`, `flat_map`, `subscribe`, plus channel adapters.
  `combine_latest`, `zip`, and `switch_latest` are follow-ups; copying the
  ReactiveX catalog is a non-goal. `Channel[T]` and `Stream[T, E]` remain
  distinct abstractions with an explicit adapter between them.
- `map` admits ordinary Functor laws under value observation.
  `flat_map` is **not** advertised as a lawful Monad: the equality model
  must state whether timing, scheduler choice, cancellation, demand, and
  error order are observable before any such claim, because those
  observations can invalidate naive associativity. Law claims, when made,
  go through the #551 law machinery with an explicitly stated observation
  set.
- Operators must not hide blocking I/O, thread creation, unbounded
  allocation, or scheduler changes. Fusion is permitted only with
  differential tests preserving values, errors, order, demand, and
  cancellation, with x86-64/AArch64 parity measured before performance
  claims.

### `Signal`

- v1 has **no** `Signal[T]`. State-over-time is a stream plus an explicit
  held current value (`stream.hold(initial)` returning a readable cell).
  A dedicated `Signal` is added only if the UI corpus proves the held-cell
  shape unclear — the burden of proof sits with the new type, not with the
  small core.

## Alternatives considered

**Language-level reactivity (keywords, compiler-known observables).**
Merits: potentially better diagnostics and syntax. Demerits: grows the
language for one domain, contradicts the Go-like small-core goal, and
freezes semantics before library experience exists. Rejected.

**Unbounded channels/callbacks as the composition story.** Merits: already
familiar; no new protocol. Demerits: no demand contract means every queue
is a latent memory leak and slow subscribers are unprotected; Go's own
pipeline pattern documents the manual shutdown burden. Rejected as the
public contract; channels remain with an explicit adapter.

**Full ReactiveX catalog.** Merits: familiarity for RX users. Demerits:
dozens of operators with subtle timing semantics, many unlawful under
honest observation; a small audited core composes into the rest. Rejected.

**Continuous-time FRP signals.** Merits: elegant for animation. Demerits:
conflates discrete events with continuous behaviors, requires a sampling
story the runtime does not have, and is severable later. Rejected for v1.

## Non-goals

Copying ReactiveX wholesale, continuous-time FRP, implicit global
schedulers or hidden threads, unbounded queues anywhere, and blocking the
first self-hosted compiler on any of this.

## Validation

| Check | Artifact | Expected result |
|---|---|---|
| Contract review | this document | protocol, demand, termination, overflow fixed |
| Conformance suite | deterministic virtual-scheduler corpus | over-production, terminal races, cancellation, bounded buffers covered |
| Required corpus | issue #627 items 1–7 | each case has exactly one interpretation |
| Charter matrix | `sh stdlib/check-capabilities.sh` | stream row cites this contract |
