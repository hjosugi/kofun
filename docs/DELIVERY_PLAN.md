# Delivery plan

Status: generated read-only planning snapshot. This is a capacity forecast, not
a promise that unresolved or externally blocked work will complete by a date.

Repository: [`hjosugi/kofun`](https://github.com/hjosugi/kofun)

As of: `2026-07-27`

## Capacity and scope

- Maximum four agents: three writer lanes plus one review/integration lane.
- Work in progress is capped at three implementation issues.
- Planning umbrellas are counted but never scheduled as implementation work.
- `50` curated issues are scheduled and
  `24` are deferred or externally blocked.

| Metric | Count |
|---|---:|
| All issues | 645 |
| Open issues | 520 |
| Open curated | 74 |
| Open planning umbrellas | 446 |
| Scheduled curated | 50 |
| Unscheduled curated | 24 |

## Forecast

| Scenario | Finish | Interpretation |
|---|---|---|
| 4-agent capacity plan | 2026-12-09 | Deterministic lane simulation with one serial reviewer. |
| Conservative +25% buffer | 2027-01-13 | Allows for refinement, rework, and integration variance. |
| Observed completion pace, intake frozen | 2026-08-07 | Optimistic extrapolation; it freezes intake and may include retrospective tracker closure. |
| Observed net issue burn | — | No finish date: curated intake equals or exceeds completed work in the observation window. |

Confidence is **low** until missing sizes, refinement
states, external blockers, and future intake are resolved. Over the trailing
`14` days, curated intake was
`66.5`/week and completion was
`29`/week; issues closed as
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
| [#721](https://github.com/hjosugi/kofun/issues/721) | closed | P1 | — | — |
| [#722](https://github.com/hjosugi/kofun/issues/722) | in-progress | P1 | 2026-07-27 | 2026-07-31 |
| [#723](https://github.com/hjosugi/kofun/issues/723) | in-progress | P1 | 2026-08-03 | 2026-08-14 |
| [#724](https://github.com/hjosugi/kofun/issues/724) | blocked | P1 | 2026-08-17 | 2026-08-28 |
| [#725](https://github.com/hjosugi/kofun/issues/725) | blocked | P2 | 2026-08-31 | 2026-09-11 |
| [#726](https://github.com/hjosugi/kofun/issues/726) | blocked | P2 | 2026-09-14 | 2026-09-25 |

## Scheduled curated work

Writer end is implementation complete; delivered is after the single reviewer
lane finishes integration.

| Issue | Lane | State | Priority | Size | Start | Writer end | Delivered | Confidence | Title |
|---|---|---|---|---|---|---|---|---|---|
| [#618](https://github.com/hjosugi/kofun/issues/618) | `writer-a` | needs-detail | P0 | L | 2026-07-27 | 2026-08-07 | 2026-08-11 | low | Self-host profile: freeze the smallest honest compiler S and coverage manifest |
| [#722](https://github.com/hjosugi/kofun/issues/722) | `writer-b` | in-progress | P1 | M | 2026-07-27 | 2026-07-30 | 2026-07-31 | medium | Decimal slice 3: same-type numeric operators and explicit conversions in the type checker |
| [#550](https://github.com/hjosugi/kofun/issues/550) | `writer-c` | in-progress | P1 | M | 2026-07-27 | 2026-07-30 | 2026-08-03 | medium | Implicit return of the final expression — and the lambda/function inconsistency |
| [#609](https://github.com/hjosugi/kofun/issues/609) | `writer-c` | in-progress | P1 | M | 2026-07-31 | 2026-08-05 | 2026-08-06 | medium | Stage 2 sidecar projector: map semantic events into atomic CLI output |
| [#723](https://github.com/hjosugi/kofun/issues/723) | `writer-b` | in-progress | P1 | L | 2026-08-03 | 2026-08-12 | 2026-08-14 | low | Decimal slice 4: exact addition, subtraction, multiplication, and checked exact division on every backend |
| [#636](https://github.com/hjosugi/kofun/issues/636) | `writer-c` | in-progress | P1 | M | 2026-08-06 | 2026-08-11 | 2026-08-12 | medium | Standard library charter: batteries-included coverage, tiers, compatibility, and update policy |
| [#622](https://github.com/hjosugi/kofun/issues/622) | `writer-a` | blocked | P0 | M | 2026-08-12 | 2026-08-17 | 2026-08-18 | conditional | Self-host compiler driver: args, file I/O, and compiling S to deterministic C11 |
| [#637](https://github.com/hjosugi/kofun/issues/637) | `writer-c` | in-progress | P1 | M | 2026-08-12 | 2026-08-17 | 2026-08-19 | medium | Discovery query v1: project inferred types and callable operations from semantic facts |
| [#724](https://github.com/hjosugi/kofun/issues/724) | `writer-b` | blocked | P1 | L | 2026-08-17 | 2026-08-26 | 2026-08-28 | conditional | Decimal slice 5: explicit rounding and formatting, then migrate the bounded stdlib checkpoint |
| [#551](https://github.com/hjosugi/kofun/issues/551) | `writer-c` | ready | P1 | M | 2026-08-18 | 2026-08-21 | 2026-08-24 | medium | Law declarations: make Monad a library law, not a compiler keyword |
| [#271](https://github.com/hjosugi/kofun/issues/271) | `writer-a` | blocked | P0 | M | 2026-08-19 | 2026-08-24 | 2026-08-25 | conditional | Bootstrap: produce C1/A1 and self-compile S into C2/A2 |
| [#626](https://github.com/hjosugi/kofun/issues/626) | `writer-c` | ready | P1 | M | 2026-08-24 | 2026-08-27 | 2026-08-31 | medium | Composable sequencing syntax: Result-first propagation and law-generic bind |
| [#272](https://github.com/hjosugi/kofun/issues/272) | `writer-a` | blocked | P0 | M | 2026-08-26 | 2026-08-31 | 2026-09-01 | conditional | Bootstrap fixed point: produce C3/A3, compare three generations, close B4/B5 |
| [#627](https://github.com/hjosugi/kofun/issues/627) | `writer-c` | ready | P1 | M | 2026-08-28 | 2026-09-02 | 2026-09-03 | medium | Reactive programming: small Stream/Signal protocol with explicit demand and ownership |
| [#725](https://github.com/hjosugi/kofun/issues/725) | `writer-b` | blocked | P2 | L | 2026-08-31 | 2026-09-09 | 2026-09-11 | conditional | Decimal slice 6: state scale guarantees truthfully now, and add Fixed[scale] when const generics exist |
| [#274](https://github.com/hjosugi/kofun/issues/274) | `writer-a` | blocked | P1 | M | 2026-09-02 | 2026-09-07 | 2026-09-08 | conditional | Reproducible bootstrap B6: independent clean builder reproduces the fixed point |
| [#625](https://github.com/hjosugi/kofun/issues/625) | `writer-c` | needs-decision | P1 | M | 2026-09-03 | 2026-09-11 | 2026-09-14 | low | Function-call ergonomics: labelled arguments and one trailing-lambda rule |
| [#27](https://github.com/hjosugi/kofun/issues/27) | `writer-a` | needs-detail | P1 | M | 2026-09-09 | 2026-09-16 | 2026-09-17 | low | Sample: end-to-end JSON API service |
| [#726](https://github.com/hjosugi/kofun/issues/726) | `writer-b` | blocked | P2 | L | 2026-09-14 | 2026-09-23 | 2026-09-25 | conditional | Decimal slice 7: versioned law evidence, the Float associativity counterexample, and backend differential conformance |
| [#112](https://github.com/hjosugi/kofun/issues/112) | `writer-c` | needs-detail | P1 | S | 2026-09-14 | 2026-09-17 | 2026-09-18 | low | Shadowing: allow ancestor bindings and reject same-scope duplicates |
| [#276](https://github.com/hjosugi/kofun/issues/276) | `writer-a` | needs-detail | P1 | L | 2026-09-17 | 2026-09-30 | 2026-10-02 | low | Reduce boilerplate: derive, and generated members |
| [#281](https://github.com/hjosugi/kofun/issues/281) | `writer-c` | needs-detail | P1 | M | 2026-09-18 | 2026-09-25 | 2026-09-28 | low | Position paper: what replacing C and Rust actually requires |
| [#533](https://github.com/hjosugi/kofun/issues/533) | `writer-b` | needs-detail | P1 | L | 2026-09-28 | 2026-10-09 | 2026-10-13 | low | Show where ownership goes: LSP hover, inlay hints, and diagnostics |
| [#554](https://github.com/hjosugi/kofun/issues/554) | `writer-c` | needs-detail | P1 | S | 2026-09-28 | 2026-10-01 | 2026-10-05 | low | Backend strategy: reject MLIR, keep the self-hosted backend — with measured costs |
| [#555](https://github.com/hjosugi/kofun/issues/555) | `writer-a` | needs-detail | P1 | S | 2026-10-01 | 2026-10-06 | 2026-10-07 | low | Concurrency: scoped parallelism needs no new machinery; ownership helps here |
| [#556](https://github.com/hjosugi/kofun/issues/556) | `writer-c` | needs-detail | P1 | S | 2026-10-02 | 2026-10-07 | 2026-10-08 | low | Effect systems: ship pure/impure first; multi-shot continuations are unsound with take |
| [#557](https://github.com/hjosugi/kofun/issues/557) | `writer-a` | needs-detail | P1 | S | 2026-10-07 | 2026-10-12 | 2026-10-14 | low | Reading list for the type checker: what to copy, what to skip |
| [#569](https://github.com/hjosugi/kofun/issues/569) | `writer-c` | needs-detail | P1 | S | 2026-10-08 | 2026-10-13 | 2026-10-15 | low | Austral: make authority explicit with affine capability values |
| [#570](https://github.com/hjosugi/kofun/issues/570) | `writer-b` | needs-detail | P1 | S | 2026-10-12 | 2026-10-15 | 2026-10-16 | low | Austral: propagate ownership kind structurally through generics and ADTs |
| [#571](https://github.com/hjosugi/kofun/issues/571) | `writer-a` | needs-detail | P1 | S | 2026-10-13 | 2026-10-16 | 2026-10-19 | low | Austral/Nim: support zero-copy borrowed results without user-written lifetimes |
| [#573](https://github.com/hjosugi/kofun/issues/573) | `writer-c` | needs-detail | P1 | S | 2026-10-14 | 2026-10-19 | 2026-10-20 | low | Odin: make allocator choice a scoped, effect-tracked capability |
| [#574](https://github.com/hjosugi/kofun/issues/574) | `writer-b` | needs-detail | P1 | S | 2026-10-16 | 2026-10-21 | 2026-10-22 | low | V: generate audited C bindings from Clang AST before attempting source translation |
| [#710](https://github.com/hjosugi/kofun/issues/710) | `writer-a` | needs-detail | P1 | L | 2026-10-19 | 2026-10-30 | 2026-11-03 | low | Compiler-native Decimal: implement the accepted language design across all backends |
| [#638](https://github.com/hjosugi/kofun/issues/638) | `writer-c` | in-progress | P2 | M | 2026-10-20 | 2026-10-23 | 2026-10-26 | medium | HTTP client contract: streaming, redirects, TLS, cancellation, and bounded resources |
| [#639](https://github.com/hjosugi/kofun/issues/639) | `writer-b` | in-progress | P2 | M | 2026-10-22 | 2026-10-27 | 2026-10-28 | medium | Date and time contract: instants, civil calendars, time zones, parsing, and deterministic clocks |
| [#640](https://github.com/hjosugi/kofun/issues/640) | `writer-c` | in-progress | P2 | M | 2026-10-26 | 2026-10-29 | 2026-10-30 | medium | Benchmark harness contract: warmup, sampling, allocation metrics, and reproducible reports |
| [#70](https://github.com/hjosugi/kofun/issues/70) | `writer-b` | ready | P2 | M | 2026-10-28 | 2026-11-02 | 2026-11-04 | medium | Optional frontend: parse null and T? into typed IR |
| [#757](https://github.com/hjosugi/kofun/issues/757) | `writer-c` | ready | P2 | M | 2026-10-30 | 2026-11-04 | 2026-11-05 | medium | Build entry point: replace the Makefile with a Taskfile, keeping every gate byte-identical |
| [#735](https://github.com/hjosugi/kofun/issues/735) | `writer-a` | needs-decision | P2 | M | 2026-11-02 | 2026-11-10 | 2026-11-11 | low | One-shot effect handlers: affine resumptions with a runtime double-resume backstop |
| [#736](https://github.com/hjosugi/kofun/issues/736) | `writer-b` | needs-decision | P2 | M | 2026-11-03 | 2026-11-11 | 2026-11-12 | low | Deterministic structured concurrency: versioned schedule traces, strict replay, and bounded exploration |
| [#740](https://github.com/hjosugi/kofun/issues/740) | `writer-c` | needs-decision | P2 | M | 2026-11-05 | 2026-11-13 | 2026-11-16 | low | Term-level semantic identity: evaluate rename-stable hashes, caches, and structural diffs |
| [#742](https://github.com/hjosugi/kofun/issues/742) | `writer-a` | needs-decision | P2 | M | 2026-11-11 | 2026-11-19 | 2026-11-20 | low | Validation accumulation: collect independent errors without running dependent checks |
| [#558](https://github.com/hjosugi/kofun/issues/558) | `writer-b` | needs-detail | P2 | S | 2026-11-12 | 2026-11-17 | 2026-11-18 | low | What verification actually costs — and why proof-to-code ratios don't measure dependent types |
| [#572](https://github.com/hjosugi/kofun/issues/572) | `writer-c` | needs-detail | P2 | S | 2026-11-16 | 2026-11-19 | 2026-11-23 | low | Nim: infer last-use moves and add a compile-time move assertion |
| [#576](https://github.com/hjosugi/kofun/issues/576) | `writer-b` | needs-detail | P2 | S | 2026-11-18 | 2026-11-23 | 2026-11-24 | low | Koka: reuse matched ADT constructors in place when storage is unique |
| [#644](https://github.com/hjosugi/kofun/issues/644) | `writer-a` | needs-detail | P2 | M | 2026-11-20 | 2026-11-27 | 2026-11-30 | low | HTTP/1.1 client core: bounded request and response state machine over scripted transport |
| [#645](https://github.com/hjosugi/kofun/issues/645) | `writer-c` | needs-detail | P2 | M | 2026-11-20 | 2026-11-27 | 2026-12-01 | low | Date/time core: Gregorian values, checked arithmetic, and strict RFC 3339 |
| [#646](https://github.com/hjosugi/kofun/issues/646) | `writer-b` | needs-detail | P2 | M | 2026-11-24 | 2026-12-01 | 2026-12-02 | low | Benchmark report v1: canonical raw-sample codec and deterministic summaries |
| [#647](https://github.com/hjosugi/kofun/issues/647) | `writer-a` | needs-detail | P2 | M | 2026-11-30 | 2026-12-07 | 2026-12-08 | low | Clock adapters: explicit monotonic/system identities, sleep, and deterministic fake time |
| [#648](https://github.com/hjosugi/kofun/issues/648) | `writer-c` | needs-detail | P2 | M | 2026-11-30 | 2026-12-07 | 2026-12-09 | low | Time-zone data v1: versioned tiny tzdb reader with gap and fold resolution |

## Weekly calendar

| Week of | Active issues | Delivered | Writer load |
|---|---|---|---:|
| 2026-07-27 | #550, #609, #618, #722 | #722 | 14/15 |
| 2026-08-03 | #550, #609, #618, #636, #723 | #550, #609 | 15/15 |
| 2026-08-10 | #618, #622, #636, #637, #723 | #618, #636, #723 | 11/15 |
| 2026-08-17 | #271, #551, #622, #637, #724 | #622, #637 | 14/15 |
| 2026-08-24 | #271, #272, #551, #626, #627, #724 | #271, #551, #724 | 12/15 |
| 2026-08-31 | #272, #274, #625, #626, #627, #725 | #272, #626, #627 | 14/15 |
| 2026-09-07 | #27, #274, #625, #725 | #274, #725 | 12/15 |
| 2026-09-14 | #27, #112, #276, #281, #625, #726 | #27, #112, #625 | 15/15 |
| 2026-09-21 | #276, #281, #726 | #726 | 13/15 |
| 2026-09-28 | #276, #281, #533, #554, #555, #556 | #276, #281 | 15/15 |
| 2026-10-05 | #533, #554, #555, #556, #557, #569 | #554, #555, #556 | 15/15 |
| 2026-10-12 | #533, #557, #569, #570, #571, #573, #574 | #533, #557, #569, #570 | 15/15 |
| 2026-10-19 | #571, #573, #574, #638, #639, #710 | #571, #573, #574 | 15/15 |
| 2026-10-26 | #70, #638, #639, #640, #710, #757 | #638, #639, #640 | 15/15 |
| 2026-11-02 | #70, #710, #735, #736, #740, #757 | #70, #710, #757 | 15/15 |
| 2026-11-09 | #558, #735, #736, #740, #742 | #735, #736 | 15/15 |
| 2026-11-16 | #558, #572, #576, #644, #645, #740, #742 | #558, #740, #742 | 15/15 |
| 2026-11-23 | #572, #576, #644, #645, #646 | #572, #576 | 15/15 |
| 2026-11-30 | #644, #645, #646, #647, #648 | #644, #645, #646 | 12/15 |
| 2026-12-07 | #647, #648 | #647, #648 | 2/15 |

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
| [#738](https://github.com/hjosugi/kofun/issues/738) | deferred | deferred outside the active delivery scope |
| [#741](https://github.com/hjosugi/kofun/issues/741) | deferred | deferred outside the active delivery scope |
| [#746](https://github.com/hjosugi/kofun/issues/746) | blocked | blocked without a dependency represented by a scheduled chain |
| [#747](https://github.com/hjosugi/kofun/issues/747) | blocked | blocked without a dependency represented by a scheduled chain |
| [#748](https://github.com/hjosugi/kofun/issues/748) | blocked | blocked without a dependency represented by a scheduled chain |
| [#749](https://github.com/hjosugi/kofun/issues/749) | blocked | blocked without a dependency represented by a scheduled chain |
| [#751](https://github.com/hjosugi/kofun/issues/751) | blocked | blocked without a dependency represented by a scheduled chain |

## Assumptions

- No new issues enter the scheduled scope.
- Three writer lanes and one review/integration lane are continuously available on business days.
- Known dependency chains are serial and their blocked members remain conditional.
- S/M/L use 2/4/8 writer days; unknown size uses 5; refinement and decision states receive extra time.
- The calendar models weekdays only; public holidays, leave, incidents, and new intake are outside the lane simulation.
- The conservative date adds a 25% business-day buffer after the simulated review bottleneck.

Regenerate this document and
`site/plan-snapshot.json` with `node site/sync-plan.mjs`. Use
`node site/sync-plan.mjs --check` in CI to detect semantic drift without
rewriting files.
