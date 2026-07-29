# Delivery plan

Status: generated read-only planning snapshot. This is a capacity forecast, not
a promise that unresolved or externally blocked work will complete by a date.

Repository: [`hjosugi/kofun`](https://github.com/hjosugi/kofun)

As of: `2026-07-29`

## Capacity and scope

- Maximum four agents: three writer lanes plus one review/integration lane.
- Work in progress is capped at three implementation issues.
- Planning umbrellas are counted but never scheduled as implementation work.
- `53` curated issues are scheduled and
  `20` are deferred or externally blocked.

| Metric | Count |
|---|---:|
| All issues | 654 |
| Open issues | 520 |
| Open curated | 73 |
| Open planning umbrellas | 447 |
| Scheduled curated | 53 |
| Unscheduled curated | 20 |

## Forecast

| Scenario | Finish | Interpretation |
|---|---|---|
| 4-agent capacity plan | 2026-12-16 | Deterministic lane simulation with one serial reviewer. |
| Conservative +25% buffer | 2027-01-21 | Allows for refinement, rework, and integration variance. |
| Observed completion pace, intake frozen | 2026-08-10 | Optimistic extrapolation; it freezes intake and may include retrospective tracker closure. |
| Observed net issue burn | — | No finish date: curated intake equals or exceeds completed work in the observation window. |

Confidence is **low** until missing sizes, refinement
states, external blockers, and future intake are resolved. Over the trailing
`14` days, curated intake was
`70.5`/week and completion was
`33.5`/week; issues closed as
`not_planned` are excluded from completion throughput.

## Critical dependency chains

### Self-host fixed point and independent reproduction

Lane: `writer-a`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#618](https://github.com/hjosugi/kofun/issues/618) | needs-detail | P0 | 2026-07-29 | 2026-08-13 |
| [#622](https://github.com/hjosugi/kofun/issues/622) | blocked | P0 | 2026-08-14 | 2026-08-20 |
| [#271](https://github.com/hjosugi/kofun/issues/271) | blocked | P0 | 2026-08-21 | 2026-08-27 |
| [#272](https://github.com/hjosugi/kofun/issues/272) | blocked | P0 | 2026-08-28 | 2026-09-03 |
| [#274](https://github.com/hjosugi/kofun/issues/274) | blocked | P1 | 2026-09-04 | 2026-09-10 |

### Compiler-native Decimal delivery

Lane: `writer-b`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#721](https://github.com/hjosugi/kofun/issues/721) | closed | P1 | — | — |
| [#722](https://github.com/hjosugi/kofun/issues/722) | closed | P1 | — | — |
| [#723](https://github.com/hjosugi/kofun/issues/723) | ready | P1 | 2026-07-29 | 2026-08-11 |
| [#724](https://github.com/hjosugi/kofun/issues/724) | blocked | P1 | 2026-08-12 | 2026-08-25 |
| [#725](https://github.com/hjosugi/kofun/issues/725) | blocked | P2 | 2026-08-26 | 2026-09-08 |
| [#726](https://github.com/hjosugi/kofun/issues/726) | blocked | P2 | 2026-09-09 | 2026-09-22 |

## Scheduled curated work

Writer end is implementation complete; delivered is after the single reviewer
lane finishes integration.

| Issue | Lane | State | Priority | Size | Start | Writer end | Delivered | Confidence | Title |
|---|---|---|---|---|---|---|---|---|---|
| [#618](https://github.com/hjosugi/kofun/issues/618) | `writer-a` | needs-detail | P0 | L | 2026-07-29 | 2026-08-11 | 2026-08-13 | low | Self-host profile: freeze the smallest honest compiler S and coverage manifest |
| [#723](https://github.com/hjosugi/kofun/issues/723) | `writer-b` | ready | P1 | L | 2026-07-29 | 2026-08-07 | 2026-08-11 | low | Decimal slice 4: exact addition, subtraction, multiplication, and checked exact division on every backend |
| [#751](https://github.com/hjosugi/kofun/issues/751) | `writer-c` | in-progress | P0 | S | 2026-07-29 | 2026-07-30 | 2026-07-31 | medium | selfhost: close the remaining declaration forms so A1 compiles S (#622) |
| [#550](https://github.com/hjosugi/kofun/issues/550) | `writer-c` | in-progress | P1 | M | 2026-07-31 | 2026-08-05 | 2026-08-06 | medium | Implicit return of the final expression — and the lambda/function inconsistency |
| [#609](https://github.com/hjosugi/kofun/issues/609) | `writer-c` | in-progress | P1 | M | 2026-08-06 | 2026-08-11 | 2026-08-14 | medium | Stage 2 sidecar projector: map semantic events into atomic CLI output |
| [#724](https://github.com/hjosugi/kofun/issues/724) | `writer-b` | blocked | P1 | L | 2026-08-12 | 2026-08-21 | 2026-08-25 | conditional | Decimal slice 5: explicit rounding and formatting, then migrate the bounded stdlib checkpoint |
| [#636](https://github.com/hjosugi/kofun/issues/636) | `writer-c` | in-progress | P1 | M | 2026-08-12 | 2026-08-17 | 2026-08-18 | medium | Standard library charter: batteries-included coverage, tiers, compatibility, and update policy |
| [#622](https://github.com/hjosugi/kofun/issues/622) | `writer-a` | blocked | P0 | M | 2026-08-14 | 2026-08-19 | 2026-08-20 | conditional | Self-host compiler driver: args, file I/O, and compiling S to deterministic C11 |
| [#637](https://github.com/hjosugi/kofun/issues/637) | `writer-c` | in-progress | P1 | M | 2026-08-18 | 2026-08-21 | 2026-08-26 | medium | Discovery query v1: project inferred types and callable operations from semantic facts |
| [#271](https://github.com/hjosugi/kofun/issues/271) | `writer-a` | blocked | P0 | M | 2026-08-21 | 2026-08-26 | 2026-08-27 | conditional | Bootstrap: produce C1/A1 and self-compile S into C2/A2 |
| [#551](https://github.com/hjosugi/kofun/issues/551) | `writer-c` | ready | P1 | M | 2026-08-24 | 2026-08-27 | 2026-08-28 | medium | Law declarations: make Monad a library law, not a compiler keyword |
| [#725](https://github.com/hjosugi/kofun/issues/725) | `writer-b` | blocked | P2 | L | 2026-08-26 | 2026-09-04 | 2026-09-08 | conditional | Decimal slice 6: state scale guarantees truthfully now, and add Fixed[scale] when const generics exist |
| [#272](https://github.com/hjosugi/kofun/issues/272) | `writer-a` | blocked | P0 | M | 2026-08-28 | 2026-09-02 | 2026-09-03 | conditional | Bootstrap fixed point: produce C3/A3, compare three generations, close B4/B5 |
| [#626](https://github.com/hjosugi/kofun/issues/626) | `writer-c` | ready | P1 | M | 2026-08-28 | 2026-09-02 | 2026-09-04 | medium | Composable sequencing syntax: Result-first propagation and law-generic bind |
| [#627](https://github.com/hjosugi/kofun/issues/627) | `writer-c` | ready | P1 | M | 2026-09-03 | 2026-09-08 | 2026-09-09 | medium | Reactive programming: small Stream/Signal protocol with explicit demand and ownership |
| [#274](https://github.com/hjosugi/kofun/issues/274) | `writer-a` | blocked | P1 | M | 2026-09-04 | 2026-09-09 | 2026-09-10 | conditional | Reproducible bootstrap B6: independent clean builder reproduces the fixed point |
| [#726](https://github.com/hjosugi/kofun/issues/726) | `writer-b` | blocked | P2 | L | 2026-09-09 | 2026-09-18 | 2026-09-22 | conditional | Decimal slice 7: versioned law evidence, the Float associativity counterexample, and backend differential conformance |
| [#625](https://github.com/hjosugi/kofun/issues/625) | `writer-c` | needs-decision | P1 | M | 2026-09-09 | 2026-09-17 | 2026-09-18 | low | Function-call ergonomics: labelled arguments and one trailing-lambda rule |
| [#27](https://github.com/hjosugi/kofun/issues/27) | `writer-a` | needs-detail | P1 | M | 2026-09-11 | 2026-09-18 | 2026-09-23 | low | Sample: end-to-end JSON API service |
| [#276](https://github.com/hjosugi/kofun/issues/276) | `writer-c` | needs-detail | P1 | L | 2026-09-18 | 2026-10-01 | 2026-10-05 | low | Reduce boilerplate: derive, and generated members |
| [#281](https://github.com/hjosugi/kofun/issues/281) | `writer-a` | needs-detail | P1 | M | 2026-09-21 | 2026-09-28 | 2026-09-29 | low | Position paper: what replacing C and Rust actually requires |
| [#533](https://github.com/hjosugi/kofun/issues/533) | `writer-b` | needs-detail | P1 | L | 2026-09-23 | 2026-10-06 | 2026-10-08 | low | Show where ownership goes: LSP hover, inlay hints, and diagnostics |
| [#554](https://github.com/hjosugi/kofun/issues/554) | `writer-a` | needs-detail | P1 | S | 2026-09-29 | 2026-10-02 | 2026-10-06 | low | Backend strategy: reject MLIR, keep the self-hosted backend — with measured costs |
| [#555](https://github.com/hjosugi/kofun/issues/555) | `writer-c` | needs-detail | P1 | S | 2026-10-02 | 2026-10-07 | 2026-10-09 | low | Concurrency: scoped parallelism needs no new machinery; ownership helps here |
| [#556](https://github.com/hjosugi/kofun/issues/556) | `writer-a` | needs-detail | P1 | S | 2026-10-05 | 2026-10-08 | 2026-10-12 | low | Effect systems: ship pure/impure first; multi-shot continuations are unsound with take |
| [#557](https://github.com/hjosugi/kofun/issues/557) | `writer-b` | needs-detail | P1 | S | 2026-10-07 | 2026-10-12 | 2026-10-13 | low | Reading list for the type checker: what to copy, what to skip |
| [#569](https://github.com/hjosugi/kofun/issues/569) | `writer-c` | needs-detail | P1 | S | 2026-10-08 | 2026-10-13 | 2026-10-14 | low | Austral: make authority explicit with affine capability values |
| [#570](https://github.com/hjosugi/kofun/issues/570) | `writer-a` | needs-detail | P1 | S | 2026-10-09 | 2026-10-14 | 2026-10-15 | low | Austral: propagate ownership kind structurally through generics and ADTs |
| [#571](https://github.com/hjosugi/kofun/issues/571) | `writer-b` | needs-detail | P1 | S | 2026-10-13 | 2026-10-16 | 2026-10-19 | low | Austral/Nim: support zero-copy borrowed results without user-written lifetimes |
| [#573](https://github.com/hjosugi/kofun/issues/573) | `writer-c` | needs-detail | P1 | S | 2026-10-14 | 2026-10-19 | 2026-10-20 | low | Odin: make allocator choice a scoped, effect-tracked capability |
| [#574](https://github.com/hjosugi/kofun/issues/574) | `writer-a` | needs-detail | P1 | S | 2026-10-15 | 2026-10-20 | 2026-10-21 | low | V: generate audited C bindings from Clang AST before attempting source translation |
| [#710](https://github.com/hjosugi/kofun/issues/710) | `writer-b` | needs-detail | P1 | L | 2026-10-19 | 2026-10-30 | 2026-11-03 | low | Compiler-native Decimal: implement the accepted language design across all backends |
| [#638](https://github.com/hjosugi/kofun/issues/638) | `writer-c` | in-progress | P2 | M | 2026-10-20 | 2026-10-23 | 2026-10-26 | medium | HTTP client contract: streaming, redirects, TLS, cancellation, and bounded resources |
| [#639](https://github.com/hjosugi/kofun/issues/639) | `writer-a` | in-progress | P2 | M | 2026-10-21 | 2026-10-26 | 2026-10-27 | medium | Date and time contract: instants, civil calendars, time zones, parsing, and deterministic clocks |
| [#640](https://github.com/hjosugi/kofun/issues/640) | `writer-c` | in-progress | P2 | M | 2026-10-26 | 2026-10-29 | 2026-10-30 | medium | Benchmark harness contract: warmup, sampling, allocation metrics, and reproducible reports |
| [#742](https://github.com/hjosugi/kofun/issues/742) | `writer-a` | ready | P2 | M | 2026-10-27 | 2026-10-30 | 2026-11-04 | medium | Validation accumulation: collect independent errors without running dependent checks |
| [#757](https://github.com/hjosugi/kofun/issues/757) | `writer-c` | ready | P2 | M | 2026-10-30 | 2026-11-04 | 2026-11-05 | medium | Build entry point: replace the Makefile with a Taskfile, keeping every gate byte-identical |
| [#782](https://github.com/hjosugi/kofun/issues/782) | `writer-a` | ready | P2 | M | 2026-11-02 | 2026-11-05 | 2026-11-06 | medium | stage2: construct, match, and bind single-Int-payload ADT constructors (#644) |
| [#783](https://github.com/hjosugi/kofun/issues/783) | `writer-b` | ready | P2 | M | 2026-11-02 | 2026-11-05 | 2026-11-09 | medium | records: lower nominal record construction and field reads to the Stage 2 C11 slice (#644) |
| [#735](https://github.com/hjosugi/kofun/issues/735) | `writer-c` | needs-decision | P2 | M | 2026-11-05 | 2026-11-13 | 2026-11-16 | low | One-shot effect handlers: affine resumptions with a runtime double-resume backstop |
| [#736](https://github.com/hjosugi/kofun/issues/736) | `writer-a` | needs-decision | P2 | M | 2026-11-06 | 2026-11-16 | 2026-11-17 | low | Deterministic structured concurrency: versioned schedule traces, strict replay, and bounded exploration |
| [#740](https://github.com/hjosugi/kofun/issues/740) | `writer-b` | needs-decision | P2 | M | 2026-11-06 | 2026-11-16 | 2026-11-18 | low | Term-level semantic identity: evaluate rename-stable hashes, caches, and structural diffs |
| [#768](https://github.com/hjosugi/kofun/issues/768) | `writer-c` | needs-decision | P2 | S | 2026-11-16 | 2026-11-20 | 2026-11-23 | low | Move the generated backlog corpus out: 22.6% of tracked bytes that no gate reads |
| [#784](https://github.com/hjosugi/kofun/issues/784) | `writer-a` | needs-decision | P2 | M | 2026-11-17 | 2026-11-25 | 2026-11-26 | low | ownership: an affine handle whose owned state cannot be duplicated (#644) |
| [#558](https://github.com/hjosugi/kofun/issues/558) | `writer-b` | needs-detail | P2 | S | 2026-11-17 | 2026-11-20 | 2026-11-24 | low | What verification actually costs — and why proof-to-code ratios don't measure dependent types |
| [#572](https://github.com/hjosugi/kofun/issues/572) | `writer-b` | needs-detail | P2 | S | 2026-11-23 | 2026-11-26 | 2026-11-27 | low | Nim: infer last-use moves and add a compile-time move assertion |
| [#576](https://github.com/hjosugi/kofun/issues/576) | `writer-c` | needs-detail | P2 | S | 2026-11-23 | 2026-11-26 | 2026-11-30 | low | Koka: reuse matched ADT constructors in place when storage is unique |
| [#645](https://github.com/hjosugi/kofun/issues/645) | `writer-a` | needs-detail | P2 | M | 2026-11-26 | 2026-12-03 | 2026-12-04 | low | Date/time core: Gregorian values, checked arithmetic, and strict RFC 3339 |
| [#646](https://github.com/hjosugi/kofun/issues/646) | `writer-b` | needs-detail | P2 | M | 2026-11-27 | 2026-12-04 | 2026-12-07 | low | Benchmark report v1: canonical raw-sample codec and deterministic summaries |
| [#647](https://github.com/hjosugi/kofun/issues/647) | `writer-c` | needs-detail | P2 | M | 2026-11-27 | 2026-12-04 | 2026-12-08 | low | Clock adapters: explicit monotonic/system identities, sleep, and deterministic fake time |
| [#648](https://github.com/hjosugi/kofun/issues/648) | `writer-a` | needs-detail | P2 | M | 2026-12-04 | 2026-12-11 | 2026-12-14 | low | Time-zone data v1: versioned tiny tzdb reader with gap and fold resolution |
| [#767](https://github.com/hjosugi/kofun/issues/767) | `writer-b` | needs-detail | P2 | M | 2026-12-07 | 2026-12-14 | 2026-12-15 | low | Move the website out: site/, app/, and the Next/Cloudflare/npm manifests |
| [#769](https://github.com/hjosugi/kofun/issues/769) | `writer-c` | needs-decision | P3 | S | 2026-12-07 | 2026-12-11 | 2026-12-16 | low | Settle docs/: move the 9 documents no manifest resolves, record why the other 27 stay |

## Weekly calendar

| Week of | Active issues | Delivered | Writer load |
|---|---|---|---:|
| 2026-07-27 | #550, #618, #723, #751 | #751 | 9/15 |
| 2026-08-03 | #550, #609, #618, #723 | #550 | 15/15 |
| 2026-08-10 | #609, #618, #622, #636, #723, #724 | #609, #618, #723 | 11/15 |
| 2026-08-17 | #271, #622, #636, #637, #724 | #622, #636 | 14/15 |
| 2026-08-24 | #271, #272, #551, #626, #637, #724, #725 | #271, #551, #637, #724 | 12/15 |
| 2026-08-31 | #272, #274, #626, #627, #725 | #272, #626 | 14/15 |
| 2026-09-07 | #27, #274, #625, #627, #725, #726 | #274, #627, #725 | 12/15 |
| 2026-09-14 | #27, #276, #625, #726 | #625 | 15/15 |
| 2026-09-21 | #27, #276, #281, #533, #726 | #27, #726 | 13/15 |
| 2026-09-28 | #276, #281, #533, #554, #555 | #281 | 15/15 |
| 2026-10-05 | #276, #533, #554, #555, #556, #557, #569, #570 | #276, #533, #554, #555 | 15/15 |
| 2026-10-12 | #556, #557, #569, #570, #571, #573, #574 | #556, #557, #569, #570 | 15/15 |
| 2026-10-19 | #571, #573, #574, #638, #639, #710 | #571, #573, #574 | 15/15 |
| 2026-10-26 | #638, #639, #640, #710, #742, #757 | #638, #639, #640 | 15/15 |
| 2026-11-02 | #710, #735, #736, #740, #742, #757, #782, #783 | #710, #742, #757, #782 | 15/15 |
| 2026-11-09 | #735, #736, #740, #783 | #783 | 15/15 |
| 2026-11-16 | #558, #735, #736, #740, #768, #784 | #735, #736, #740 | 15/15 |
| 2026-11-23 | #558, #572, #576, #645, #646, #647, #768, #784 | #558, #572, #768, #784 | 15/15 |
| 2026-11-30 | #576, #645, #646, #647, #648 | #576, #645 | 15/15 |
| 2026-12-07 | #646, #647, #648, #767, #769 | #646, #647 | 15/15 |
| 2026-12-14 | #648, #767, #769 | #648, #767, #769 | 1/15 |

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
| [#644](https://github.com/hjosugi/kofun/issues/644) | blocked | blocked without a dependency represented by a scheduled chain |
| [#738](https://github.com/hjosugi/kofun/issues/738) | deferred | deferred outside the active delivery scope |
| [#741](https://github.com/hjosugi/kofun/issues/741) | deferred | deferred outside the active delivery scope |
| [#772](https://github.com/hjosugi/kofun/issues/772) | blocked | blocked without a dependency represented by a scheduled chain |

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
