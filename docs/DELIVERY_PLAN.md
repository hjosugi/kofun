# Delivery plan

Status: generated read-only planning snapshot. This is a capacity forecast, not
a promise that unresolved or externally blocked work will complete by a date.

Repository: [`hjosugi/kofun`](https://github.com/hjosugi/kofun)

As of: `2026-07-26`

## Capacity and scope

- Maximum four agents: three writer lanes plus one review/integration lane.
- Work in progress is capped at three implementation issues.
- Planning umbrellas are counted but never scheduled as implementation work.
- `50` curated issues are scheduled and
  `17` are deferred or externally blocked.

| Metric | Count |
|---|---:|
| All issues | 628 |
| Open issues | 513 |
| Open curated | 67 |
| Open planning umbrellas | 446 |
| Scheduled curated | 50 |
| Unscheduled curated | 17 |

## Forecast

| Scenario | Finish | Interpretation |
|---|---|---|
| 4-agent capacity plan | 2026-12-11 | Deterministic lane simulation with one serial reviewer. |
| Conservative +25% buffer | 2027-01-15 | Allows for refinement, rework, and integration variance. |
| Observed completion pace, intake frozen | 2026-08-11 | Optimistic extrapolation; it freezes intake and may include retrospective tracker closure. |
| Observed net issue burn | — | No finish date: curated intake equals or exceeds completed work in the observation window. |

Confidence is **low** until missing sizes, refinement
states, external blockers, and future intake are resolved. Over the trailing
`14` days, curated intake was
`58`/week and completion was
`24`/week; issues closed as
`not_planned` are excluded from completion throughput.

## Critical dependency chains

### Self-host fixed point and independent reproduction

Lane: `writer-a`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#618](https://github.com/hjosugi/kofun/issues/618) | needs-detail | P0 | 2026-07-27 | 2026-08-11 |
| [#622](https://github.com/hjosugi/kofun/issues/622) | blocked | P0 | 2026-08-12 | 2026-08-18 |
| [#271](https://github.com/hjosugi/kofun/issues/271) | blocked | P0 | 2026-08-19 | 2026-08-25 |
| [#272](https://github.com/hjosugi/kofun/issues/272) | blocked | P0 | 2026-08-26 | 2026-09-01 |
| [#274](https://github.com/hjosugi/kofun/issues/274) | blocked | P1 | 2026-09-02 | 2026-09-08 |

### Compiler-native Decimal delivery

Lane: `writer-b`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#721](https://github.com/hjosugi/kofun/issues/721) | unclassified | P1 | 2026-07-27 | 2026-08-03 |
| [#722](https://github.com/hjosugi/kofun/issues/722) | unclassified | P1 | 2026-08-04 | 2026-08-12 |
| [#723](https://github.com/hjosugi/kofun/issues/723) | unclassified | P1 | 2026-08-13 | 2026-08-20 |
| [#724](https://github.com/hjosugi/kofun/issues/724) | unclassified | P1 | 2026-08-21 | 2026-08-28 |
| [#725](https://github.com/hjosugi/kofun/issues/725) | blocked | P2 | 2026-08-31 | 2026-09-07 |
| [#726](https://github.com/hjosugi/kofun/issues/726) | blocked | P2 | 2026-09-08 | 2026-09-15 |

## Scheduled curated work

Writer end is implementation complete; delivered is after the single reviewer
lane finishes integration.

| Issue | Lane | State | Priority | Size | Start | Writer end | Delivered | Confidence | Title |
|---|---|---|---|---|---|---|---|---|---|
| [#618](https://github.com/hjosugi/kofun/issues/618) | `writer-a` | needs-detail | P0 | L | 2026-07-27 | 2026-08-07 | 2026-08-11 | low | Self-host profile: freeze the smallest honest compiler S and coverage manifest |
| [#721](https://github.com/hjosugi/kofun/issues/721) | `writer-b` | unclassified | P1 | — | 2026-07-27 | 2026-07-31 | 2026-08-03 | low | Decimal slice 2: arbitrary-precision runtime representation, canonicalization, and the versioned resource profile |
| [#550](https://github.com/hjosugi/kofun/issues/550) | `writer-c` | in-progress | P1 | M | 2026-07-27 | 2026-07-30 | 2026-07-31 | medium | Implicit return of the final expression — and the lambda/function inconsistency |
| [#609](https://github.com/hjosugi/kofun/issues/609) | `writer-c` | in-progress | P1 | M | 2026-07-31 | 2026-08-05 | 2026-08-06 | medium | Stage 2 sidecar projector: map semantic events into atomic CLI output |
| [#722](https://github.com/hjosugi/kofun/issues/722) | `writer-b` | unclassified | P1 | — | 2026-08-04 | 2026-08-10 | 2026-08-12 | low | Decimal slice 3: same-type numeric operators and explicit conversions in the type checker |
| [#636](https://github.com/hjosugi/kofun/issues/636) | `writer-c` | in-progress | P1 | M | 2026-08-06 | 2026-08-11 | 2026-08-13 | medium | Standard library charter: batteries-included coverage, tiers, compatibility, and update policy |
| [#622](https://github.com/hjosugi/kofun/issues/622) | `writer-a` | blocked | P0 | M | 2026-08-12 | 2026-08-17 | 2026-08-18 | conditional | Self-host compiler driver: args, file I/O, and compiling S to deterministic C11 |
| [#120](https://github.com/hjosugi/kofun/issues/120) | `writer-c` | ready | P1 | M | 2026-08-12 | 2026-08-17 | 2026-08-19 | medium | Define AggregateLayout v1 for native64 and wasm32 |
| [#723](https://github.com/hjosugi/kofun/issues/723) | `writer-b` | unclassified | P1 | — | 2026-08-13 | 2026-08-19 | 2026-08-20 | low | Decimal slice 4: exact addition, subtraction, multiplication, and checked exact division on every backend |
| [#403](https://github.com/hjosugi/kofun/issues/403) | `writer-c` | ready | P1 | S | 2026-08-18 | 2026-08-19 | 2026-08-21 | medium | Decide retroactive conformance, orphan rules, and the public keyword |
| [#271](https://github.com/hjosugi/kofun/issues/271) | `writer-a` | blocked | P0 | M | 2026-08-19 | 2026-08-24 | 2026-08-25 | conditional | Bootstrap: produce C1/A1 and self-compile S into C2/A2 |
| [#546](https://github.com/hjosugi/kofun/issues/546) | `writer-c` | ready | P1 | M | 2026-08-20 | 2026-08-25 | 2026-08-26 | medium | Nominal heterogeneous records for structured compiler and application data |
| [#724](https://github.com/hjosugi/kofun/issues/724) | `writer-b` | unclassified | P1 | — | 2026-08-21 | 2026-08-27 | 2026-08-28 | low | Decimal slice 5: explicit rounding and formatting, then migrate the bounded stdlib checkpoint |
| [#272](https://github.com/hjosugi/kofun/issues/272) | `writer-a` | blocked | P0 | M | 2026-08-26 | 2026-08-31 | 2026-09-01 | conditional | Bootstrap fixed point: produce C3/A3, compare three generations, close B4/B5 |
| [#551](https://github.com/hjosugi/kofun/issues/551) | `writer-c` | ready | P1 | M | 2026-08-26 | 2026-08-31 | 2026-09-02 | medium | Law declarations: make Monad a library law, not a compiler keyword |
| [#725](https://github.com/hjosugi/kofun/issues/725) | `writer-b` | blocked | P2 | — | 2026-08-31 | 2026-09-04 | 2026-09-07 | conditional | Decimal slice 6: state scale guarantees truthfully now, and add Fixed[scale] when const generics exist |
| [#626](https://github.com/hjosugi/kofun/issues/626) | `writer-c` | ready | P1 | M | 2026-09-01 | 2026-09-04 | 2026-09-09 | medium | Composable sequencing syntax: Result-first propagation and law-generic bind |
| [#274](https://github.com/hjosugi/kofun/issues/274) | `writer-a` | blocked | P1 | M | 2026-09-02 | 2026-09-07 | 2026-09-08 | conditional | Reproducible bootstrap B6: independent clean builder reproduces the fixed point |
| [#627](https://github.com/hjosugi/kofun/issues/627) | `writer-c` | ready | P1 | M | 2026-09-07 | 2026-09-10 | 2026-09-11 | medium | Reactive programming: small Stream/Signal protocol with explicit demand and ownership |
| [#726](https://github.com/hjosugi/kofun/issues/726) | `writer-b` | blocked | P2 | — | 2026-09-08 | 2026-09-14 | 2026-09-15 | conditional | Decimal slice 7: versioned law evidence, the Float associativity counterexample, and backend differential conformance |
| [#637](https://github.com/hjosugi/kofun/issues/637) | `writer-a` | ready | P1 | M | 2026-09-09 | 2026-09-14 | 2026-09-16 | medium | Discovery query v1: project inferred types and callable operations from semantic facts |
| [#657](https://github.com/hjosugi/kofun/issues/657) | `writer-c` | ready | P1 | M | 2026-09-11 | 2026-09-16 | 2026-09-17 | medium | TypeScript's lesson: keep type-level programming readable — named, bounded, and debuggable |
| [#625](https://github.com/hjosugi/kofun/issues/625) | `writer-a` | needs-decision | P1 | M | 2026-09-15 | 2026-09-23 | 2026-09-24 | low | Function-call ergonomics: labelled arguments and one trailing-lambda rule |
| [#27](https://github.com/hjosugi/kofun/issues/27) | `writer-b` | needs-detail | P1 | — | 2026-09-16 | 2026-09-24 | 2026-09-25 | low | Sample: end-to-end JSON API service |
| [#112](https://github.com/hjosugi/kofun/issues/112) | `writer-c` | needs-detail | P1 | S | 2026-09-17 | 2026-09-22 | 2026-09-23 | low | Shadowing: allow ancestor bindings and reject same-scope duplicates |
| [#276](https://github.com/hjosugi/kofun/issues/276) | `writer-c` | needs-detail | P1 | — | 2026-09-23 | 2026-10-01 | 2026-10-02 | low | Reduce boilerplate: derive, and generated members |
| [#281](https://github.com/hjosugi/kofun/issues/281) | `writer-a` | needs-detail | P1 | — | 2026-09-24 | 2026-10-02 | 2026-10-05 | low | Position paper: what replacing C and Rust actually requires |
| [#533](https://github.com/hjosugi/kofun/issues/533) | `writer-b` | needs-detail | P1 | — | 2026-09-25 | 2026-10-05 | 2026-10-06 | low | Show where ownership goes: LSP hover, inlay hints, and diagnostics |
| [#554](https://github.com/hjosugi/kofun/issues/554) | `writer-c` | needs-detail | P1 | — | 2026-10-02 | 2026-10-12 | 2026-10-13 | low | Backend strategy: reject MLIR, keep the self-hosted backend — with measured costs |
| [#555](https://github.com/hjosugi/kofun/issues/555) | `writer-a` | needs-detail | P1 | — | 2026-10-05 | 2026-10-13 | 2026-10-14 | low | Concurrency: scoped parallelism needs no new machinery; ownership helps here |
| [#556](https://github.com/hjosugi/kofun/issues/556) | `writer-b` | needs-detail | P1 | — | 2026-10-06 | 2026-10-14 | 2026-10-15 | low | Effect systems: ship pure/impure first; multi-shot continuations are unsound with take |
| [#557](https://github.com/hjosugi/kofun/issues/557) | `writer-c` | needs-detail | P1 | — | 2026-10-13 | 2026-10-21 | 2026-10-22 | low | Reading list for the type checker: what to copy, what to skip |
| [#569](https://github.com/hjosugi/kofun/issues/569) | `writer-a` | needs-detail | P1 | — | 2026-10-14 | 2026-10-22 | 2026-10-23 | low | Austral: make authority explicit with affine capability values |
| [#570](https://github.com/hjosugi/kofun/issues/570) | `writer-b` | needs-detail | P1 | — | 2026-10-15 | 2026-10-23 | 2026-10-26 | low | Austral: propagate ownership kind structurally through generics and ADTs |
| [#571](https://github.com/hjosugi/kofun/issues/571) | `writer-c` | needs-detail | P1 | — | 2026-10-22 | 2026-10-30 | 2026-11-02 | low | Austral/Nim: support zero-copy borrowed results without user-written lifetimes |
| [#573](https://github.com/hjosugi/kofun/issues/573) | `writer-a` | needs-detail | P1 | — | 2026-10-23 | 2026-11-02 | 2026-11-03 | low | Odin: make allocator choice a scoped, effect-tracked capability |
| [#574](https://github.com/hjosugi/kofun/issues/574) | `writer-b` | needs-detail | P1 | — | 2026-10-26 | 2026-11-03 | 2026-11-04 | low | V: generate audited C bindings from Clang AST before attempting source translation |
| [#710](https://github.com/hjosugi/kofun/issues/710) | `writer-c` | needs-detail | P1 | L | 2026-11-02 | 2026-11-13 | 2026-11-17 | low | Compiler-native Decimal: implement the accepted language design across all backends |
| [#638](https://github.com/hjosugi/kofun/issues/638) | `writer-a` | in-progress | P2 | M | 2026-11-03 | 2026-11-06 | 2026-11-09 | medium | HTTP client contract: streaming, redirects, TLS, cancellation, and bounded resources |
| [#639](https://github.com/hjosugi/kofun/issues/639) | `writer-b` | in-progress | P2 | M | 2026-11-04 | 2026-11-09 | 2026-11-10 | medium | Date and time contract: instants, civil calendars, time zones, parsing, and deterministic clocks |
| [#640](https://github.com/hjosugi/kofun/issues/640) | `writer-a` | in-progress | P2 | M | 2026-11-09 | 2026-11-12 | 2026-11-13 | medium | Benchmark harness contract: warmup, sampling, allocation metrics, and reproducible reports |
| [#70](https://github.com/hjosugi/kofun/issues/70) | `writer-b` | ready | P2 | M | 2026-11-10 | 2026-11-13 | 2026-11-18 | medium | Optional frontend: parse null and T? into typed IR |
| [#558](https://github.com/hjosugi/kofun/issues/558) | `writer-a` | needs-detail | P2 | — | 2026-11-13 | 2026-11-23 | 2026-11-24 | low | What verification actually costs — and why proof-to-code ratios don't measure dependent types |
| [#572](https://github.com/hjosugi/kofun/issues/572) | `writer-b` | needs-detail | P2 | — | 2026-11-16 | 2026-11-24 | 2026-11-25 | low | Nim: infer last-use moves and add a compile-time move assertion |
| [#576](https://github.com/hjosugi/kofun/issues/576) | `writer-c` | needs-detail | P2 | — | 2026-11-16 | 2026-11-24 | 2026-11-26 | low | Koka: reuse matched ADT constructors in place when storage is unique |
| [#644](https://github.com/hjosugi/kofun/issues/644) | `writer-a` | needs-detail | P2 | M | 2026-11-24 | 2026-12-01 | 2026-12-02 | low | HTTP/1.1 client core: bounded request and response state machine over scripted transport |
| [#645](https://github.com/hjosugi/kofun/issues/645) | `writer-b` | needs-detail | P2 | M | 2026-11-25 | 2026-12-02 | 2026-12-03 | low | Date/time core: Gregorian values, checked arithmetic, and strict RFC 3339 |
| [#646](https://github.com/hjosugi/kofun/issues/646) | `writer-c` | needs-detail | P2 | M | 2026-11-25 | 2026-12-02 | 2026-12-04 | low | Benchmark report v1: canonical raw-sample codec and deterministic summaries |
| [#647](https://github.com/hjosugi/kofun/issues/647) | `writer-a` | needs-detail | P2 | M | 2026-12-02 | 2026-12-09 | 2026-12-10 | low | Clock adapters: explicit monotonic/system identities, sleep, and deterministic fake time |
| [#648](https://github.com/hjosugi/kofun/issues/648) | `writer-b` | needs-detail | P2 | M | 2026-12-03 | 2026-12-10 | 2026-12-11 | low | Time-zone data v1: versioned tiny tzdb reader with gap and fold resolution |

## Weekly calendar

| Week of | Active issues | Delivered | Writer load |
|---|---|---|---:|
| 2026-07-27 | #550, #609, #618, #721 | #550 | 15/15 |
| 2026-08-03 | #609, #618, #636, #721, #722 | #609, #721 | 14/15 |
| 2026-08-10 | #120, #618, #622, #636, #722, #723 | #618, #636, #722 | 11/15 |
| 2026-08-17 | #120, #271, #403, #546, #622, #723, #724 | #120, #403, #622, #723 | 13/15 |
| 2026-08-24 | #271, #272, #546, #551, #724 | #271, #546, #724 | 13/15 |
| 2026-08-31 | #272, #274, #551, #626, #725 | #272, #551 | 14/15 |
| 2026-09-07 | #274, #626, #627, #637, #657, #725, #726 | #274, #626, #627, #725 | 13/15 |
| 2026-09-14 | #27, #112, #625, #637, #657, #726 | #637, #657, #726 | 14/15 |
| 2026-09-21 | #27, #112, #276, #281, #533, #625 | #27, #112, #625 | 15/15 |
| 2026-09-28 | #276, #281, #533, #554 | #276 | 15/15 |
| 2026-10-05 | #281, #533, #554, #555, #556 | #281, #533 | 15/15 |
| 2026-10-12 | #554, #555, #556, #557, #569, #570 | #554, #555, #556 | 15/15 |
| 2026-10-19 | #557, #569, #570, #571, #573 | #557, #569 | 15/15 |
| 2026-10-26 | #570, #571, #573, #574 | #570 | 15/15 |
| 2026-11-02 | #571, #573, #574, #638, #639, #710 | #571, #573, #574 | 15/15 |
| 2026-11-09 | #70, #558, #638, #639, #640, #710 | #638, #639, #640 | 15/15 |
| 2026-11-16 | #70, #558, #572, #576, #710 | #70, #710 | 15/15 |
| 2026-11-23 | #558, #572, #576, #644, #645, #646 | #558, #572, #576 | 15/15 |
| 2026-11-30 | #644, #645, #646, #647, #648 | #644, #645, #646 | 13/15 |
| 2026-12-07 | #647, #648 | #647, #648 | 7/15 |

## Not scheduled

No finish date is assigned when the tracker itself says the work is deferred or
has an external blocker outside the represented serial chains.

| Issue | State | Reason |
|---|---|---|
| [#277](https://github.com/hjosugi/kofun/issues/277) | deferred | deferred outside the active delivery scope |
| [#278](https://github.com/hjosugi/kofun/issues/278) | deferred | deferred outside the active delivery scope |
| [#279](https://github.com/hjosugi/kofun/issues/279) | deferred | deferred outside the active delivery scope |
| [#280](https://github.com/hjosugi/kofun/issues/280) | deferred | deferred outside the active delivery scope |
| [#312](https://github.com/hjosugi/kofun/issues/312) | blocked | blocked without a dependency represented by a scheduled chain |
| [#314](https://github.com/hjosugi/kofun/issues/314) | blocked | blocked without a dependency represented by a scheduled chain |
| [#332](https://github.com/hjosugi/kofun/issues/332) | blocked | blocked without a dependency represented by a scheduled chain |
| [#424](https://github.com/hjosugi/kofun/issues/424) | deferred | deferred outside the active delivery scope |
| [#536](https://github.com/hjosugi/kofun/issues/536) | deferred | deferred outside the active delivery scope |
| [#537](https://github.com/hjosugi/kofun/issues/537) | deferred | deferred outside the active delivery scope |
| [#538](https://github.com/hjosugi/kofun/issues/538) | deferred | deferred outside the active delivery scope |
| [#539](https://github.com/hjosugi/kofun/issues/539) | deferred | deferred outside the active delivery scope |
| [#540](https://github.com/hjosugi/kofun/issues/540) | deferred | deferred outside the active delivery scope |
| [#583](https://github.com/hjosugi/kofun/issues/583) | blocked | blocked without a dependency represented by a scheduled chain |
| [#584](https://github.com/hjosugi/kofun/issues/584) | blocked | blocked without a dependency represented by a scheduled chain |
| [#585](https://github.com/hjosugi/kofun/issues/585) | blocked | blocked without a dependency represented by a scheduled chain |
| [#606](https://github.com/hjosugi/kofun/issues/606) | blocked | blocked without a dependency represented by a scheduled chain |

## Assumptions

- No new issues enter the scheduled scope.
- Three writer lanes and one review/integration lane are continuously available on business days.
- Known dependency chains are serial and their blocked members remain conditional.
- S/M/L use 2/4/8 writer days; unknown size uses 5; refinement and decision states receive extra time.
- The calendar models weekdays only; public holidays, leave, incidents, and new intake are outside the lane simulation.
- The conservative date adds a 25% business-day buffer after the simulated review bottleneck.

Regenerate this document and
`app/roadmap/plan-snapshot.json` with `node site/sync-plan.mjs`. Use
`node site/sync-plan.mjs --check` in CI to detect semantic drift without
rewriting files.
