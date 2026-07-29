# Delivery plan

Status: generated read-only planning snapshot. This is a capacity forecast, not
a promise that unresolved, blocked, or deferred work will complete by a date.

Repository: [`hjosugi/kofun`](https://github.com/hjosugi/kofun)

As of: `2026-07-29`

## Capacity and scope

- Maximum four agents: three writer lanes plus one review/integration lane.
- Work in progress is capped at three implementation issues.
- Planning umbrellas are counted but never scheduled as implementation work.
- All `73` open curated issues have rough
  Gantt dates; blocked and deferred dates are explicitly low-confidence.

| Metric | Count |
|---|---:|
| All issues | 654 |
| Open issues | 520 |
| Open curated | 73 |
| Open planning umbrellas | 447 |
| Scheduled curated | 73 |
| Unscheduled curated | 0 |

## Forecast

| Scenario | Finish | Interpretation |
|---|---|---|
| 4-agent capacity plan | 2027-02-17 | Deterministic lane simulation with one serial reviewer. |
| Conservative +25% buffer | 2027-04-09 | Allows for refinement, rework, and integration variance. |
| Observed completion pace, intake frozen | 2026-08-13 | Optimistic extrapolation; it freezes intake and may include retrospective tracker closure. |
| Observed net issue burn | — | No finish date: curated intake equals or exceeds completed work in the observation window. |

Confidence is **low** until missing sizes, refinement
states, external blockers, and future intake are resolved. Over the trailing
`14` days, curated intake was
`70.5`/week and completion was
`33.5`/week; issues closed as
`not_planned` are excluded from completion throughput.

## Parallel workstreams

Issues in the same workstream stay on one primary writer lane so they can share
context and avoid cross-worktree collisions. Different lanes run in parallel.

| Workstream | Primary lane | Issues | Start | Rough finish | Grouped issue numbers |
|---|---|---:|---|---|---|
| Self-host & Bootstrap | `writer-a` | 6 | 2026-07-29 | 2026-10-19 | #751, #618, #622, #271, #272, #274 |
| Decimal | `writer-b` | 5 | 2026-08-04 | 2026-12-29 | #723, #724, #725, #726, #710 |
| Frontend & Types | `writer-b` | 9 | 2026-07-29 | 2026-12-01 | #551, #626, #312, #772, #314, #550, #625, #276, #557 |
| Backend & Runtime | `writer-a` | 12 | 2026-07-31 | 2026-12-21 | #533, #554, #570, #571, #277, #782, #783, #784, #576, #278, #279, #280 |
| Effects & Concurrency | `writer-b` | 8 | 2026-10-15 | 2027-01-20 | #627, #555, #556, #569, #573, #742, #735, #736 |
| Tooling & Diagnostics | `writer-c` | 9 | 2026-07-29 | 2027-01-18 | #583, #740, #584, #741, #606, #609, #637, #574, #572 |
| Stdlib & Frameworks | `writer-c` | 10 | 2026-08-04 | 2026-12-17 | #638, #639, #640, #645, #646, #647, #648, #644, #636, #27 |
| Repository & Docs | `writer-c` | 7 | 2026-08-31 | 2027-01-29 | #768, #767, #769, #585, #757, #558, #424 |
| Science & Future | `writer-a` | 6 | 2026-12-17 | 2027-02-17 | #536, #537, #538, #539, #540, #738 |
| Research & Positioning | `writer-c` | 1 | 2026-12-17 | 2026-12-25 | #281 |

## Critical dependency chains

### Self-host fixed point and independent reproduction

Lane: `writer-a`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#744](https://github.com/hjosugi/kofun/issues/744) | closed | P0 | — | — |
| [#745](https://github.com/hjosugi/kofun/issues/745) | closed | P0 | — | — |
| [#746](https://github.com/hjosugi/kofun/issues/746) | closed | P0 | — | — |
| [#747](https://github.com/hjosugi/kofun/issues/747) | closed | P0 | — | — |
| [#748](https://github.com/hjosugi/kofun/issues/748) | closed | P0 | — | — |
| [#749](https://github.com/hjosugi/kofun/issues/749) | closed | P0 | — | — |
| [#751](https://github.com/hjosugi/kofun/issues/751) | blocked | P0 | 2026-07-29 | 2026-07-31 |
| [#618](https://github.com/hjosugi/kofun/issues/618) | needs-detail | P0 | 2026-08-14 | 2026-08-31 |
| [#622](https://github.com/hjosugi/kofun/issues/622) | blocked | P0 | 2026-09-03 | 2026-09-09 |
| [#271](https://github.com/hjosugi/kofun/issues/271) | blocked | P0 | 2026-09-15 | 2026-09-21 |
| [#272](https://github.com/hjosugi/kofun/issues/272) | blocked | P0 | 2026-09-25 | 2026-10-01 |
| [#274](https://github.com/hjosugi/kofun/issues/274) | blocked | P1 | 2026-10-13 | 2026-10-19 |

### Compiler-native Decimal delivery

Lane: `writer-b`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#721](https://github.com/hjosugi/kofun/issues/721) | closed | P1 | — | — |
| [#722](https://github.com/hjosugi/kofun/issues/722) | closed | P1 | — | — |
| [#723](https://github.com/hjosugi/kofun/issues/723) | ready | P1 | 2026-08-04 | 2026-08-17 |
| [#724](https://github.com/hjosugi/kofun/issues/724) | blocked | P1 | 2026-08-26 | 2026-09-08 |
| [#725](https://github.com/hjosugi/kofun/issues/725) | blocked | P2 | 2026-09-17 | 2026-09-30 |
| [#726](https://github.com/hjosugi/kofun/issues/726) | blocked | P2 | 2026-10-05 | 2026-10-16 |

### Optional frontend, inference, and coalescing

Lane: `writer-b`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#70](https://github.com/hjosugi/kofun/issues/70) | closed | P2 | — | — |
| [#312](https://github.com/hjosugi/kofun/issues/312) | blocked | P1 | 2026-08-20 | 2026-08-26 |
| [#314](https://github.com/hjosugi/kofun/issues/314) | blocked | P2 | 2026-09-11 | 2026-09-17 |

### Lexical and pattern-binding shadowing

Lane: `writer-b`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#112](https://github.com/hjosugi/kofun/issues/112) | closed | P1 | — | — |
| [#772](https://github.com/hjosugi/kofun/issues/772) | blocked | P1 | 2026-09-07 | 2026-09-11 |

### Law declarations and composable sequencing

Lane: `writer-b`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#551](https://github.com/hjosugi/kofun/issues/551) | ready | P1 | 2026-07-29 | 2026-08-04 |
| [#626](https://github.com/hjosugi/kofun/issues/626) | ready | P1 | 2026-08-14 | 2026-08-21 |

### Visibility interfaces, tooling, docs, and LSP

Lane: `writer-c`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#583](https://github.com/hjosugi/kofun/issues/583) | blocked | P1 | 2026-07-29 | 2026-08-05 |
| [#584](https://github.com/hjosugi/kofun/issues/584) | blocked | P2 | 2026-10-19 | 2026-10-23 |
| [#585](https://github.com/hjosugi/kofun/issues/585) | blocked | P3 | 2026-11-05 | 2026-11-11 |
| [#606](https://github.com/hjosugi/kofun/issues/606) | blocked | P1 | 2026-11-17 | 2026-11-23 |

### HTTP client contract and core

Lane: `writer-c`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#638](https://github.com/hjosugi/kofun/issues/638) | in-progress | P2 | 2026-08-04 | 2026-08-10 |
| [#644](https://github.com/hjosugi/kofun/issues/644) | blocked | P2 | 2026-10-23 | 2026-10-29 |

### Date/time contract, core, clocks, and time zones

Lane: `writer-c`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#639](https://github.com/hjosugi/kofun/issues/639) | in-progress | P2 | 2026-08-10 | 2026-08-18 |
| [#645](https://github.com/hjosugi/kofun/issues/645) | needs-detail | P2 | 2026-09-07 | 2026-09-15 |
| [#647](https://github.com/hjosugi/kofun/issues/647) | needs-detail | P2 | 2026-09-23 | 2026-10-02 |
| [#648](https://github.com/hjosugi/kofun/issues/648) | needs-detail | P2 | 2026-10-09 | 2026-10-20 |

### Benchmark contract and report format

Lane: `writer-c`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#640](https://github.com/hjosugi/kofun/issues/640) | in-progress | P2 | 2026-08-14 | 2026-08-24 |
| [#646](https://github.com/hjosugi/kofun/issues/646) | needs-detail | P2 | 2026-09-15 | 2026-09-23 |

### Repository split and documentation cleanup

Lane: `writer-c`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#768](https://github.com/hjosugi/kofun/issues/768) | needs-decision | P2 | 2026-08-31 | 2026-09-10 |
| [#767](https://github.com/hjosugi/kofun/issues/767) | needs-detail | P2 | 2026-10-01 | 2026-10-09 |
| [#769](https://github.com/hjosugi/kofun/issues/769) | needs-decision | P3 | 2026-10-29 | 2026-11-05 |

### Semantic identity and typed upgrade patches

Lane: `writer-c`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#740](https://github.com/hjosugi/kofun/issues/740) | needs-decision | P2 | 2026-08-20 | 2026-09-01 |
| [#741](https://github.com/hjosugi/kofun/issues/741) | deferred | P3 | 2026-11-11 | 2026-11-17 |

## Scheduled curated work

Writer end is implementation complete; delivered is after the single reviewer
lane finishes integration.

| Issue | Workstream | Lane | State | Priority | Size | Start | Writer end | Delivered | Confidence | Title |
|---|---|---|---|---|---|---|---|---|---|---|
| [#751](https://github.com/hjosugi/kofun/issues/751) | Self-host & Bootstrap | `writer-a` | blocked | P0 | S | 2026-07-29 | 2026-07-30 | 2026-07-31 | conditional | selfhost: close the remaining declaration forms so A1 compiles S (#622) |
| [#551](https://github.com/hjosugi/kofun/issues/551) | Frontend & Types | `writer-b` | ready | P1 | M | 2026-07-29 | 2026-08-03 | 2026-08-04 | medium | Law declarations: make Monad a library law, not a compiler keyword |
| [#583](https://github.com/hjosugi/kofun/issues/583) | Tooling & Diagnostics | `writer-c` | blocked | P1 | M | 2026-07-29 | 2026-08-03 | 2026-08-05 | conditional | Visibility interfaces: reject API leaks and filter semantic artifacts |
| [#533](https://github.com/hjosugi/kofun/issues/533) | Backend & Runtime | `writer-a` | needs-detail | P1 | L | 2026-07-31 | 2026-08-13 | 2026-08-20 | low | Show where ownership goes: LSP hover, inlay hints, and diagnostics |
| [#723](https://github.com/hjosugi/kofun/issues/723) | Decimal | `writer-b` | ready | P1 | L | 2026-08-04 | 2026-08-13 | 2026-08-17 | low | Decimal slice 4: exact addition, subtraction, multiplication, and checked exact division on every backend |
| [#638](https://github.com/hjosugi/kofun/issues/638) | Stdlib & Frameworks | `writer-c` | in-progress | P2 | M | 2026-08-04 | 2026-08-07 | 2026-08-10 | medium | HTTP client contract: streaming, redirects, TLS, cancellation, and bounded resources |
| [#639](https://github.com/hjosugi/kofun/issues/639) | Stdlib & Frameworks | `writer-c` | in-progress | P2 | M | 2026-08-10 | 2026-08-13 | 2026-08-18 | medium | Date and time contract: instants, civil calendars, time zones, parsing, and deterministic clocks |
| [#618](https://github.com/hjosugi/kofun/issues/618) | Self-host & Bootstrap | `writer-a` | needs-detail | P0 | L | 2026-08-14 | 2026-08-27 | 2026-08-31 | low | Self-host profile: freeze the smallest honest compiler S and coverage manifest |
| [#626](https://github.com/hjosugi/kofun/issues/626) | Frontend & Types | `writer-b` | ready | P1 | M | 2026-08-14 | 2026-08-19 | 2026-08-21 | medium | Composable sequencing syntax: Result-first propagation and law-generic bind |
| [#640](https://github.com/hjosugi/kofun/issues/640) | Stdlib & Frameworks | `writer-c` | in-progress | P2 | M | 2026-08-14 | 2026-08-19 | 2026-08-24 | medium | Benchmark harness contract: warmup, sampling, allocation metrics, and reproducible reports |
| [#312](https://github.com/hjosugi/kofun/issues/312) | Frontend & Types | `writer-b` | blocked | P1 | M | 2026-08-20 | 2026-08-25 | 2026-08-26 | conditional | Optional inference: narrow direct bindings without leaking refinements |
| [#740](https://github.com/hjosugi/kofun/issues/740) | Tooling & Diagnostics | `writer-c` | needs-decision | P2 | M | 2026-08-20 | 2026-08-28 | 2026-09-01 | low | Term-level semantic identity: evaluate rename-stable hashes, caches, and structural diffs |
| [#724](https://github.com/hjosugi/kofun/issues/724) | Decimal | `writer-b` | blocked | P1 | L | 2026-08-26 | 2026-09-04 | 2026-09-08 | conditional | Decimal slice 5: explicit rounding and formatting, then migrate the bounded stdlib checkpoint |
| [#554](https://github.com/hjosugi/kofun/issues/554) | Backend & Runtime | `writer-a` | needs-detail | P1 | S | 2026-08-28 | 2026-09-02 | 2026-09-03 | low | Backend strategy: reject MLIR, keep the self-hosted backend — with measured costs |
| [#768](https://github.com/hjosugi/kofun/issues/768) | Repository & Docs | `writer-c` | needs-decision | P2 | S | 2026-08-31 | 2026-09-04 | 2026-09-10 | low | Move the generated backlog corpus out: 22.6% of tracked bytes that no gate reads |
| [#622](https://github.com/hjosugi/kofun/issues/622) | Self-host & Bootstrap | `writer-a` | blocked | P0 | M | 2026-09-03 | 2026-09-08 | 2026-09-09 | conditional | Self-host compiler driver: args, file I/O, and compiling S to deterministic C11 |
| [#772](https://github.com/hjosugi/kofun/issues/772) | Frontend & Types | `writer-b` | blocked | P1 | M | 2026-09-07 | 2026-09-10 | 2026-09-11 | conditional | Shadowing over pattern bindings: insert resolved arm bindings into the arm ScopeId |
| [#645](https://github.com/hjosugi/kofun/issues/645) | Stdlib & Frameworks | `writer-c` | needs-detail | P2 | M | 2026-09-07 | 2026-09-14 | 2026-09-15 | low | Date/time core: Gregorian values, checked arithmetic, and strict RFC 3339 |
| [#570](https://github.com/hjosugi/kofun/issues/570) | Backend & Runtime | `writer-a` | needs-detail | P1 | S | 2026-09-09 | 2026-09-14 | 2026-09-16 | low | Austral: propagate ownership kind structurally through generics and ADTs |
| [#314](https://github.com/hjosugi/kofun/issues/314) | Frontend & Types | `writer-b` | blocked | P2 | M | 2026-09-11 | 2026-09-16 | 2026-09-17 | conditional | Null coalescing: execute Optional(Int) ?? Int lazily |
| [#271](https://github.com/hjosugi/kofun/issues/271) | Self-host & Bootstrap | `writer-a` | blocked | P0 | M | 2026-09-15 | 2026-09-18 | 2026-09-21 | conditional | Bootstrap: produce C1/A1 and self-compile S into C2/A2 |
| [#646](https://github.com/hjosugi/kofun/issues/646) | Stdlib & Frameworks | `writer-c` | needs-detail | P2 | M | 2026-09-15 | 2026-09-22 | 2026-09-23 | low | Benchmark report v1: canonical raw-sample codec and deterministic summaries |
| [#725](https://github.com/hjosugi/kofun/issues/725) | Decimal | `writer-b` | blocked | P2 | L | 2026-09-17 | 2026-09-28 | 2026-09-30 | conditional | Decimal slice 6: state scale guarantees truthfully now, and add Fixed[scale] when const generics exist |
| [#571](https://github.com/hjosugi/kofun/issues/571) | Backend & Runtime | `writer-a` | needs-detail | P1 | S | 2026-09-21 | 2026-09-24 | 2026-09-25 | low | Austral/Nim: support zero-copy borrowed results without user-written lifetimes |
| [#647](https://github.com/hjosugi/kofun/issues/647) | Stdlib & Frameworks | `writer-c` | needs-detail | P2 | M | 2026-09-23 | 2026-09-30 | 2026-10-02 | low | Clock adapters: explicit monotonic/system identities, sleep, and deterministic fake time |
| [#272](https://github.com/hjosugi/kofun/issues/272) | Self-host & Bootstrap | `writer-a` | blocked | P0 | M | 2026-09-25 | 2026-09-30 | 2026-10-01 | conditional | Bootstrap fixed point: produce C3/A3, compare three generations, close B4/B5 |
| [#550](https://github.com/hjosugi/kofun/issues/550) | Frontend & Types | `writer-b` | in-progress | P1 | M | 2026-09-29 | 2026-10-02 | 2026-10-05 | medium | Implicit return of the final expression — and the lambda/function inconsistency |
| [#277](https://github.com/hjosugi/kofun/issues/277) | Backend & Runtime | `writer-a` | deferred | P1 | L | 2026-10-01 | 2026-10-12 | 2026-10-14 | low | Freestanding target: run with no operating system |
| [#767](https://github.com/hjosugi/kofun/issues/767) | Repository & Docs | `writer-c` | needs-detail | P2 | M | 2026-10-01 | 2026-10-08 | 2026-10-09 | low | Move the website out: site/, app/, and the Next/Cloudflare/npm manifests |
| [#726](https://github.com/hjosugi/kofun/issues/726) | Decimal | `writer-b` | blocked | P2 | L | 2026-10-05 | 2026-10-14 | 2026-10-16 | conditional | Decimal slice 7: versioned law evidence, the Float associativity counterexample, and backend differential conformance |
| [#648](https://github.com/hjosugi/kofun/issues/648) | Stdlib & Frameworks | `writer-c` | needs-detail | P2 | M | 2026-10-09 | 2026-10-16 | 2026-10-20 | low | Time-zone data v1: versioned tiny tzdb reader with gap and fold resolution |
| [#274](https://github.com/hjosugi/kofun/issues/274) | Self-host & Bootstrap | `writer-a` | blocked | P1 | M | 2026-10-13 | 2026-10-16 | 2026-10-19 | conditional | Reproducible bootstrap B6: independent clean builder reproduces the fixed point |
| [#627](https://github.com/hjosugi/kofun/issues/627) | Effects & Concurrency | `writer-b` | ready | P1 | M | 2026-10-15 | 2026-10-20 | 2026-10-21 | medium | Reactive programming: small Stream/Signal protocol with explicit demand and ownership |
| [#782](https://github.com/hjosugi/kofun/issues/782) | Backend & Runtime | `writer-a` | ready | P2 | M | 2026-10-19 | 2026-10-22 | 2026-10-26 | medium | stage2: construct, match, and bind single-Int-payload ADT constructors (#644) |
| [#584](https://github.com/hjosugi/kofun/issues/584) | Tooling & Diagnostics | `writer-c` | blocked | P2 | M | 2026-10-19 | 2026-10-22 | 2026-10-23 | conditional | Visibility tooling: prevent macro, LSP, and sidecar access bypasses |
| [#625](https://github.com/hjosugi/kofun/issues/625) | Frontend & Types | `writer-b` | needs-decision | P1 | M | 2026-10-21 | 2026-10-29 | 2026-10-30 | low | Function-call ergonomics: labelled arguments and one trailing-lambda rule |
| [#783](https://github.com/hjosugi/kofun/issues/783) | Backend & Runtime | `writer-a` | ready | P2 | M | 2026-10-23 | 2026-10-28 | 2026-11-02 | medium | records: lower nominal record construction and field reads to the Stage 2 C11 slice (#644) |
| [#644](https://github.com/hjosugi/kofun/issues/644) | Stdlib & Frameworks | `writer-c` | blocked | P2 | M | 2026-10-23 | 2026-10-28 | 2026-10-29 | conditional | HTTP/1.1 client core: bounded request and response state machine over scripted transport |
| [#784](https://github.com/hjosugi/kofun/issues/784) | Backend & Runtime | `writer-a` | needs-decision | P2 | M | 2026-10-29 | 2026-11-06 | 2026-11-09 | low | ownership: an affine handle whose owned state cannot be duplicated (#644) |
| [#769](https://github.com/hjosugi/kofun/issues/769) | Repository & Docs | `writer-c` | needs-decision | P3 | S | 2026-10-29 | 2026-11-04 | 2026-11-05 | low | Settle docs/: move the 9 documents no manifest resolves, record why the other 27 stay |
| [#276](https://github.com/hjosugi/kofun/issues/276) | Frontend & Types | `writer-b` | needs-detail | P1 | L | 2026-10-30 | 2026-11-12 | 2026-11-16 | low | Reduce boilerplate: derive, and generated members |
| [#585](https://github.com/hjosugi/kofun/issues/585) | Repository & Docs | `writer-c` | blocked | P3 | M | 2026-11-05 | 2026-11-10 | 2026-11-11 | conditional | Visibility stabilization: document API changes and migration |
| [#576](https://github.com/hjosugi/kofun/issues/576) | Backend & Runtime | `writer-a` | needs-detail | P2 | S | 2026-11-09 | 2026-11-12 | 2026-11-18 | low | Koka: reuse matched ADT constructors in place when storage is unique |
| [#741](https://github.com/hjosugi/kofun/issues/741) | Tooling & Diagnostics | `writer-c` | deferred | P3 | M | 2026-11-11 | 2026-11-16 | 2026-11-17 | low | Typed upgrade patches: track replacements over the semantic dependency graph |
| [#278](https://github.com/hjosugi/kofun/issues/278) | Backend & Runtime | `writer-a` | deferred | P2 | L | 2026-11-13 | 2026-11-24 | 2026-11-27 | low | ARM Cortex-M backend (thumb2) |
| [#555](https://github.com/hjosugi/kofun/issues/555) | Effects & Concurrency | `writer-b` | needs-detail | P1 | S | 2026-11-13 | 2026-11-18 | 2026-11-19 | low | Concurrency: scoped parallelism needs no new machinery; ownership helps here |
| [#606](https://github.com/hjosugi/kofun/issues/606) | Tooling & Diagnostics | `writer-c` | blocked | P1 | M | 2026-11-17 | 2026-11-20 | 2026-11-23 | conditional | LSP semantic adapter: consume typed sidecars with stale and disclosure guards |
| [#556](https://github.com/hjosugi/kofun/issues/556) | Effects & Concurrency | `writer-b` | needs-detail | P1 | S | 2026-11-19 | 2026-11-24 | 2026-11-25 | low | Effect systems: ship pure/impure first; multi-shot continuations are unsound with take |
| [#609](https://github.com/hjosugi/kofun/issues/609) | Tooling & Diagnostics | `writer-c` | in-progress | P1 | M | 2026-11-23 | 2026-11-26 | 2026-11-30 | medium | Stage 2 sidecar projector: map semantic events into atomic CLI output |
| [#279](https://github.com/hjosugi/kofun/issues/279) | Backend & Runtime | `writer-a` | deferred | P2 | L | 2026-11-25 | 2026-12-04 | 2026-12-09 | low | Memory-mapped I/O, volatile access, and interrupt handlers |
| [#557](https://github.com/hjosugi/kofun/issues/557) | Frontend & Types | `writer-b` | needs-detail | P1 | S | 2026-11-25 | 2026-11-30 | 2026-12-01 | low | Reading list for the type checker: what to copy, what to skip |
| [#636](https://github.com/hjosugi/kofun/issues/636) | Stdlib & Frameworks | `writer-c` | in-progress | P1 | M | 2026-11-27 | 2026-12-02 | 2026-12-03 | medium | Standard library charter: batteries-included coverage, tiers, compatibility, and update policy |
| [#569](https://github.com/hjosugi/kofun/issues/569) | Effects & Concurrency | `writer-b` | needs-detail | P1 | S | 2026-12-01 | 2026-12-04 | 2026-12-07 | low | Austral: make authority explicit with affine capability values |
| [#637](https://github.com/hjosugi/kofun/issues/637) | Tooling & Diagnostics | `writer-c` | in-progress | P1 | M | 2026-12-03 | 2026-12-08 | 2026-12-10 | medium | Discovery query v1: project inferred types and callable operations from semantic facts |
| [#280](https://github.com/hjosugi/kofun/issues/280) | Backend & Runtime | `writer-a` | deferred | P2 | L | 2026-12-07 | 2026-12-16 | 2026-12-21 | low | No-heap mode: static allocation only |
| [#573](https://github.com/hjosugi/kofun/issues/573) | Effects & Concurrency | `writer-b` | needs-detail | P1 | S | 2026-12-07 | 2026-12-10 | 2026-12-11 | low | Odin: make allocator choice a scoped, effect-tracked capability |
| [#27](https://github.com/hjosugi/kofun/issues/27) | Stdlib & Frameworks | `writer-c` | needs-detail | P1 | M | 2026-12-09 | 2026-12-16 | 2026-12-17 | low | Sample: end-to-end JSON API service |
| [#710](https://github.com/hjosugi/kofun/issues/710) | Decimal | `writer-b` | needs-detail | P1 | L | 2026-12-11 | 2026-12-24 | 2026-12-29 | low | Compiler-native Decimal: implement the accepted language design across all backends |
| [#536](https://github.com/hjosugi/kofun/issues/536) | Science & Future | `writer-a` | deferred | P2 | L | 2026-12-17 | 2026-12-28 | 2026-12-31 | low | ndarray: n-dimensional arrays with views, strides, and broadcasting |
| [#281](https://github.com/hjosugi/kofun/issues/281) | Research & Positioning | `writer-c` | needs-detail | P1 | M | 2026-12-17 | 2026-12-24 | 2026-12-25 | low | Position paper: what replacing C and Rust actually requires |
| [#742](https://github.com/hjosugi/kofun/issues/742) | Effects & Concurrency | `writer-b` | ready | P2 | M | 2026-12-25 | 2026-12-30 | 2027-01-04 | medium | Validation accumulation: collect independent errors without running dependent checks |
| [#574](https://github.com/hjosugi/kofun/issues/574) | Tooling & Diagnostics | `writer-c` | needs-detail | P1 | S | 2026-12-25 | 2026-12-30 | 2027-01-01 | low | V: generate audited C bindings from Clang AST before attempting source translation |
| [#537](https://github.com/hjosugi/kofun/issues/537) | Science & Future | `writer-a` | deferred | P2 | L | 2026-12-29 | 2027-01-07 | 2027-01-11 | low | Linear algebra: matmul, decompositions, solvers, and BLAS/LAPACK binding |
| [#735](https://github.com/hjosugi/kofun/issues/735) | Effects & Concurrency | `writer-b` | needs-decision | P2 | M | 2026-12-31 | 2027-01-08 | 2027-01-12 | low | One-shot effect handlers: affine resumptions with a runtime double-resume backstop |
| [#757](https://github.com/hjosugi/kofun/issues/757) | Repository & Docs | `writer-c` | ready | P2 | M | 2026-12-31 | 2027-01-05 | 2027-01-06 | medium | Build entry point: replace the Makefile with a Taskfile, keeping every gate byte-identical |
| [#558](https://github.com/hjosugi/kofun/issues/558) | Repository & Docs | `writer-c` | needs-detail | P2 | S | 2027-01-06 | 2027-01-11 | 2027-01-13 | low | What verification actually costs — and why proof-to-code ratios don't measure dependent types |
| [#538](https://github.com/hjosugi/kofun/issues/538) | Science & Future | `writer-a` | deferred | P2 | L | 2027-01-08 | 2027-01-19 | 2027-01-22 | low | Signal and image processing: FFT, convolution, filtering |
| [#736](https://github.com/hjosugi/kofun/issues/736) | Effects & Concurrency | `writer-b` | needs-decision | P2 | M | 2027-01-11 | 2027-01-19 | 2027-01-20 | low | Deterministic structured concurrency: versioned schedule traces, strict replay, and bounded exploration |
| [#572](https://github.com/hjosugi/kofun/issues/572) | Tooling & Diagnostics | `writer-c` | needs-detail | P2 | S | 2027-01-12 | 2027-01-15 | 2027-01-18 | low | Nim: infer last-use moves and add a compile-time move assertion |
| [#424](https://github.com/hjosugi/kofun/issues/424) | Repository & Docs | `writer-c` | deferred | P2 | L | 2027-01-18 | 2027-01-27 | 2027-01-29 | low | Hot reload at function granularity, via micro-partition |
| [#539](https://github.com/hjosugi/kofun/issues/539) | Science & Future | `writer-a` | deferred | P2 | L | 2027-01-20 | 2027-01-29 | 2027-02-02 | low | Statistics: distributions, tests, regression, optimisation, integration |
| [#540](https://github.com/hjosugi/kofun/issues/540) | Science & Future | `writer-a` | deferred | P2 | L | 2027-02-01 | 2027-02-10 | 2027-02-12 | low | Dataframes: typed columnar tables with group-by and join |
| [#738](https://github.com/hjosugi/kofun/issues/738) | Science & Future | `writer-a` | deferred | P3 | M | 2027-02-11 | 2027-02-16 | 2027-02-17 | low | Time-series forecasting research: rolling-origin backtests, uncertainty, and estimator boundaries |

## Weekly calendar

| Week of | Active issues | Delivered | Writer load |
|---|---|---|---:|
| 2026-07-27 | #533, #551, #583, #751 | #751 | 9/15 |
| 2026-08-03 | #533, #551, #583, #638, #723 | #551, #583 | 15/15 |
| 2026-08-10 | #533, #618, #626, #638, #639, #640, #723 | #638 | 15/15 |
| 2026-08-17 | #312, #533, #618, #626, #639, #640, #723, #740 | #533, #626, #639, #723 | 15/15 |
| 2026-08-24 | #312, #554, #618, #640, #724, #740 | #312, #640 | 15/15 |
| 2026-08-31 | #554, #618, #622, #724, #740, #768 | #554, #618, #740 | 15/15 |
| 2026-09-07 | #314, #570, #622, #645, #724, #768, #772 | #622, #724, #768, #772 | 15/15 |
| 2026-09-14 | #271, #314, #570, #645, #646, #725 | #314, #570, #645 | 15/15 |
| 2026-09-21 | #271, #272, #571, #646, #647, #725 | #271, #571, #646 | 15/15 |
| 2026-09-28 | #272, #277, #550, #647, #725, #767 | #272, #647, #725 | 15/15 |
| 2026-10-05 | #277, #550, #648, #726, #767 | #550, #767 | 15/15 |
| 2026-10-12 | #274, #277, #627, #648, #726 | #277, #726 | 15/15 |
| 2026-10-19 | #274, #584, #625, #627, #644, #648, #782, #783 | #274, #584, #627, #648 | 15/15 |
| 2026-10-26 | #276, #625, #644, #769, #782, #783, #784 | #625, #644, #782 | 15/15 |
| 2026-11-02 | #276, #585, #769, #783, #784 | #769, #783 | 15/15 |
| 2026-11-09 | #276, #278, #555, #576, #585, #741, #784 | #585, #784 | 15/15 |
| 2026-11-16 | #276, #278, #555, #556, #576, #606, #741 | #276, #555, #576, #741 | 15/15 |
| 2026-11-23 | #278, #279, #556, #557, #606, #609, #636 | #278, #556, #606 | 15/15 |
| 2026-11-30 | #279, #557, #569, #609, #636, #637 | #557, #609, #636 | 15/15 |
| 2026-12-07 | #27, #279, #280, #569, #573, #637, #710 | #279, #569, #573, #637 | 15/15 |
| 2026-12-14 | #27, #280, #281, #536, #710 | #27 | 15/15 |
| 2026-12-21 | #280, #281, #536, #574, #710, #742 | #280, #281 | 15/15 |
| 2026-12-28 | #536, #537, #574, #710, #735, #742, #757 | #536, #574, #710 | 15/15 |
| 2027-01-04 | #537, #538, #558, #735, #742, #757 | #742, #757 | 15/15 |
| 2027-01-11 | #537, #538, #558, #572, #735, #736 | #537, #558, #735 | 15/15 |
| 2027-01-18 | #424, #538, #539, #572, #736 | #538, #572, #736 | 12/15 |
| 2027-01-25 | #424, #539 | #424 | 8/15 |
| 2027-02-01 | #539, #540 | #539 | 5/15 |
| 2027-02-08 | #540, #738 | #540 | 5/15 |
| 2027-02-15 | #738 | #738 | 2/15 |

## Unscheduled exceptions

Every open curated issue receives a rough date. This table is therefore normally
empty; planning umbrellas remain intentionally outside the execution Gantt.

| Issue | State | Reason |
|---|---|---|
| — | — | — |

## Assumptions

- No new issues enter the scheduled scope.
- Three writer lanes and one review/integration lane are continuously available on business days.
- Known dependency chains are serial; unrelated workstreams run in parallel on stable lanes to reduce file collisions.
- Blocked and deferred items have rough placeholder dates only; their tracker state remains authoritative.
- S/M/L use 2/4/8 writer days; unknown size uses 5; refinement and decision states receive extra time.
- The calendar models weekdays only; public holidays, leave, incidents, and new intake are outside the lane simulation.
- The conservative date adds a 25% business-day buffer after the simulated review bottleneck.

Regenerate this document and
`site/plan-snapshot.json` with `node site/sync-plan.mjs`. Use
`node site/sync-plan.mjs --check` in CI to detect semantic drift without
rewriting files.
