# Delivery plan

Status: generated read-only planning snapshot. This is a capacity forecast, not
a promise that unresolved, blocked, or deferred work will complete by a date.

Repository: [`hjosugi/kofun`](https://github.com/hjosugi/kofun)

As of: `2026-07-28`

## Capacity and scope

- Maximum four agents: three writer lanes plus one review/integration lane.
- Work in progress is capped at three implementation issues.
- Planning umbrellas are counted but never scheduled as implementation work.
- All `75` open curated issues have rough
  Gantt dates; blocked and deferred dates are explicitly low-confidence.

| Metric | Count |
|---|---:|
| All issues | 651 |
| Open issues | 522 |
| Open curated | 75 |
| Open planning umbrellas | 447 |
| Scheduled curated | 75 |
| Unscheduled curated | 0 |

## Forecast

| Scenario | Finish | Interpretation |
|---|---|---|
| 4-agent capacity plan | 2027-02-09 | Deterministic lane simulation with one serial reviewer. |
| Conservative +25% buffer | 2027-03-31 | Allows for refinement, rework, and integration variance. |
| Observed completion pace, intake frozen | 2026-08-14 | Optimistic extrapolation; it freezes intake and may include retrospective tracker closure. |
| Observed net issue burn | — | No finish date: curated intake equals or exceeds completed work in the observation window. |

Confidence is **low** until missing sizes, refinement
states, external blockers, and future intake are resolved. Over the trailing
`14` days, curated intake was
`69`/week and completion was
`31`/week; issues closed as
`not_planned` are excluded from completion throughput.

## Parallel workstreams

Issues in the same workstream stay on one primary writer lane so they can share
context and avoid cross-worktree collisions. Different lanes run in parallel.

| Workstream | Primary lane | Issues | Start | Rough finish | Grouped issue numbers |
|---|---|---:|---|---|---|
| Self-host & Bootstrap | `writer-a` | 8 | 2026-07-28 | 2026-11-05 | #748, #749, #751, #618, #622, #271, #272, #274 |
| Decimal | `writer-b` | 5 | 2026-08-03 | 2027-01-06 | #723, #724, #725, #726, #710 |
| Frontend & Types | `writer-b` | 11 | 2026-07-28 | 2026-12-14 | #551, #626, #772, #70, #312, #314, #550, #332, #625, #276, #557 |
| Backend & Runtime | `writer-a` | 10 | 2026-07-30 | 2026-12-10 | #770, #533, #554, #570, #571, #277, #576, #278, #279, #280 |
| Effects & Concurrency | `writer-b` | 8 | 2026-10-26 | 2027-01-29 | #627, #555, #556, #569, #573, #742, #735, #736 |
| Tooling & Diagnostics | `writer-c` | 9 | 2026-07-28 | 2027-01-19 | #583, #740, #584, #741, #606, #609, #637, #574, #572 |
| Stdlib & Frameworks | `writer-c` | 10 | 2026-08-03 | 2026-12-18 | #638, #639, #640, #644, #645, #646, #647, #648, #636, #27 |
| Repository & Docs | `writer-c` | 7 | 2026-08-28 | 2027-02-02 | #768, #767, #769, #585, #757, #558, #424 |
| Science & Future | `writer-a` | 6 | 2026-12-09 | 2027-02-09 | #536, #537, #538, #539, #540, #738 |
| Research & Positioning | `writer-c` | 1 | 2026-12-18 | 2026-12-28 | #281 |

## Critical dependency chains

### Self-host fixed point and independent reproduction

Lane: `writer-a`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#744](https://github.com/hjosugi/kofun/issues/744) | closed | P0 | — | — |
| [#745](https://github.com/hjosugi/kofun/issues/745) | closed | P0 | — | — |
| [#746](https://github.com/hjosugi/kofun/issues/746) | closed | P0 | — | — |
| [#747](https://github.com/hjosugi/kofun/issues/747) | closed | P0 | — | — |
| [#748](https://github.com/hjosugi/kofun/issues/748) | in-progress | P0 | 2026-07-28 | 2026-07-30 |
| [#749](https://github.com/hjosugi/kofun/issues/749) | blocked | P0 | 2026-08-05 | 2026-08-11 |
| [#751](https://github.com/hjosugi/kofun/issues/751) | blocked | P0 | 2026-08-25 | 2026-08-27 |
| [#618](https://github.com/hjosugi/kofun/issues/618) | needs-detail | P0 | 2026-09-02 | 2026-09-17 |
| [#622](https://github.com/hjosugi/kofun/issues/622) | blocked | P0 | 2026-09-22 | 2026-09-28 |
| [#271](https://github.com/hjosugi/kofun/issues/271) | blocked | P0 | 2026-10-02 | 2026-10-08 |
| [#272](https://github.com/hjosugi/kofun/issues/272) | blocked | P0 | 2026-10-20 | 2026-10-26 |
| [#274](https://github.com/hjosugi/kofun/issues/274) | blocked | P1 | 2026-10-30 | 2026-11-05 |

### Compiler-native Decimal delivery

Lane: `writer-b`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#721](https://github.com/hjosugi/kofun/issues/721) | closed | P1 | — | — |
| [#722](https://github.com/hjosugi/kofun/issues/722) | closed | P1 | — | — |
| [#723](https://github.com/hjosugi/kofun/issues/723) | ready | P1 | 2026-08-03 | 2026-08-14 |
| [#724](https://github.com/hjosugi/kofun/issues/724) | blocked | P1 | 2026-08-19 | 2026-09-01 |
| [#725](https://github.com/hjosugi/kofun/issues/725) | blocked | P2 | 2026-09-10 | 2026-09-24 |
| [#726](https://github.com/hjosugi/kofun/issues/726) | blocked | P2 | 2026-09-28 | 2026-10-13 |

### Optional frontend, inference, and coalescing

Lane: `writer-b`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#70](https://github.com/hjosugi/kofun/issues/70) | ready | P2 | 2026-09-04 | 2026-09-10 |
| [#312](https://github.com/hjosugi/kofun/issues/312) | blocked | P1 | 2026-09-22 | 2026-09-29 |
| [#314](https://github.com/hjosugi/kofun/issues/314) | blocked | P2 | 2026-10-08 | 2026-10-14 |

### Lexical and pattern-binding shadowing

Lane: `writer-b`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#112](https://github.com/hjosugi/kofun/issues/112) | closed | P1 | — | — |
| [#772](https://github.com/hjosugi/kofun/issues/772) | blocked | P1 | 2026-08-31 | 2026-09-04 |

### Law declarations and composable sequencing

Lane: `writer-b`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#551](https://github.com/hjosugi/kofun/issues/551) | ready | P1 | 2026-07-28 | 2026-08-03 |
| [#626](https://github.com/hjosugi/kofun/issues/626) | ready | P1 | 2026-08-13 | 2026-08-19 |

### Visibility interfaces, tooling, docs, and LSP

Lane: `writer-c`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#583](https://github.com/hjosugi/kofun/issues/583) | blocked | P1 | 2026-07-28 | 2026-08-04 |
| [#584](https://github.com/hjosugi/kofun/issues/584) | blocked | P2 | 2026-10-26 | 2026-10-30 |
| [#585](https://github.com/hjosugi/kofun/issues/585) | blocked | P3 | 2026-11-06 | 2026-11-12 |
| [#606](https://github.com/hjosugi/kofun/issues/606) | blocked | P1 | 2026-11-18 | 2026-11-24 |

### HTTP client contract and core

Lane: `writer-c`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#638](https://github.com/hjosugi/kofun/issues/638) | in-progress | P2 | 2026-08-03 | 2026-08-07 |
| [#644](https://github.com/hjosugi/kofun/issues/644) | needs-detail | P2 | 2026-09-04 | 2026-09-14 |

### Date/time contract, core, clocks, and time zones

Lane: `writer-c`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#639](https://github.com/hjosugi/kofun/issues/639) | in-progress | P2 | 2026-08-07 | 2026-08-17 |
| [#645](https://github.com/hjosugi/kofun/issues/645) | needs-detail | P2 | 2026-09-14 | 2026-09-22 |
| [#647](https://github.com/hjosugi/kofun/issues/647) | needs-detail | P2 | 2026-09-30 | 2026-10-09 |
| [#648](https://github.com/hjosugi/kofun/issues/648) | needs-detail | P2 | 2026-10-16 | 2026-10-27 |

### Benchmark contract and report format

Lane: `writer-c`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#640](https://github.com/hjosugi/kofun/issues/640) | in-progress | P2 | 2026-08-13 | 2026-08-20 |
| [#646](https://github.com/hjosugi/kofun/issues/646) | needs-detail | P2 | 2026-09-22 | 2026-09-30 |

### Repository split and documentation cleanup

Lane: `writer-c`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#768](https://github.com/hjosugi/kofun/issues/768) | needs-decision | P2 | 2026-08-28 | 2026-09-07 |
| [#767](https://github.com/hjosugi/kofun/issues/767) | needs-detail | P2 | 2026-10-08 | 2026-10-16 |
| [#769](https://github.com/hjosugi/kofun/issues/769) | needs-decision | P3 | 2026-10-30 | 2026-11-06 |

### Semantic identity and typed upgrade patches

Lane: `writer-c`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
| [#740](https://github.com/hjosugi/kofun/issues/740) | needs-decision | P2 | 2026-08-19 | 2026-08-28 |
| [#741](https://github.com/hjosugi/kofun/issues/741) | deferred | P3 | 2026-11-12 | 2026-11-19 |

## Scheduled curated work

Writer end is implementation complete; delivered is after the single reviewer
lane finishes integration.

| Issue | Workstream | Lane | State | Priority | Size | Start | Writer end | Delivered | Confidence | Title |
|---|---|---|---|---|---|---|---|---|---|---|
| [#748](https://github.com/hjosugi/kofun/issues/748) | Self-host & Bootstrap | `writer-a` | in-progress | P0 | S | 2026-07-28 | 2026-07-29 | 2026-07-30 | medium | selfhost: accept List[Text] and indexing in S (#622) |
| [#551](https://github.com/hjosugi/kofun/issues/551) | Frontend & Types | `writer-b` | ready | P1 | M | 2026-07-28 | 2026-07-31 | 2026-08-03 | medium | Law declarations: make Monad a library law, not a compiler keyword |
| [#583](https://github.com/hjosugi/kofun/issues/583) | Tooling & Diagnostics | `writer-c` | blocked | P1 | M | 2026-07-28 | 2026-07-31 | 2026-08-04 | conditional | Visibility interfaces: reject API leaks and filter semantic artifacts |
| [#770](https://github.com/hjosugi/kofun/issues/770) | Backend & Runtime | `writer-a` | ready | P1 | M | 2026-07-30 | 2026-08-04 | 2026-08-05 | medium | Codegen contract: stop re-implementing register allocation and lowering per target |
| [#723](https://github.com/hjosugi/kofun/issues/723) | Decimal | `writer-b` | ready | P1 | L | 2026-08-03 | 2026-08-12 | 2026-08-14 | low | Decimal slice 4: exact addition, subtraction, multiplication, and checked exact division on every backend |
| [#638](https://github.com/hjosugi/kofun/issues/638) | Stdlib & Frameworks | `writer-c` | in-progress | P2 | M | 2026-08-03 | 2026-08-06 | 2026-08-07 | medium | HTTP client contract: streaming, redirects, TLS, cancellation, and bounded resources |
| [#749](https://github.com/hjosugi/kofun/issues/749) | Self-host & Bootstrap | `writer-a` | blocked | P0 | M | 2026-08-05 | 2026-08-10 | 2026-08-11 | conditional | selfhost: accept the 15 profile builtins in S (#622) |
| [#639](https://github.com/hjosugi/kofun/issues/639) | Stdlib & Frameworks | `writer-c` | in-progress | P2 | M | 2026-08-07 | 2026-08-12 | 2026-08-17 | medium | Date and time contract: instants, civil calendars, time zones, parsing, and deterministic clocks |
| [#533](https://github.com/hjosugi/kofun/issues/533) | Backend & Runtime | `writer-a` | needs-detail | P1 | L | 2026-08-11 | 2026-08-24 | 2026-08-26 | low | Show where ownership goes: LSP hover, inlay hints, and diagnostics |
| [#626](https://github.com/hjosugi/kofun/issues/626) | Frontend & Types | `writer-b` | ready | P1 | M | 2026-08-13 | 2026-08-18 | 2026-08-19 | medium | Composable sequencing syntax: Result-first propagation and law-generic bind |
| [#640](https://github.com/hjosugi/kofun/issues/640) | Stdlib & Frameworks | `writer-c` | in-progress | P2 | M | 2026-08-13 | 2026-08-18 | 2026-08-20 | medium | Benchmark harness contract: warmup, sampling, allocation metrics, and reproducible reports |
| [#724](https://github.com/hjosugi/kofun/issues/724) | Decimal | `writer-b` | blocked | P1 | L | 2026-08-19 | 2026-08-28 | 2026-09-01 | conditional | Decimal slice 5: explicit rounding and formatting, then migrate the bounded stdlib checkpoint |
| [#740](https://github.com/hjosugi/kofun/issues/740) | Tooling & Diagnostics | `writer-c` | needs-decision | P2 | M | 2026-08-19 | 2026-08-27 | 2026-08-28 | low | Term-level semantic identity: evaluate rename-stable hashes, caches, and structural diffs |
| [#751](https://github.com/hjosugi/kofun/issues/751) | Self-host & Bootstrap | `writer-a` | blocked | P0 | S | 2026-08-25 | 2026-08-26 | 2026-08-27 | conditional | selfhost: close the remaining declaration forms so A1 compiles S (#622) |
| [#554](https://github.com/hjosugi/kofun/issues/554) | Backend & Runtime | `writer-a` | needs-detail | P1 | S | 2026-08-27 | 2026-09-01 | 2026-09-02 | low | Backend strategy: reject MLIR, keep the self-hosted backend — with measured costs |
| [#768](https://github.com/hjosugi/kofun/issues/768) | Repository & Docs | `writer-c` | needs-decision | P2 | S | 2026-08-28 | 2026-09-03 | 2026-09-07 | low | Move the generated backlog corpus out: 22.6% of tracked bytes that no gate reads |
| [#772](https://github.com/hjosugi/kofun/issues/772) | Frontend & Types | `writer-b` | blocked | P1 | M | 2026-08-31 | 2026-09-03 | 2026-09-04 | conditional | Shadowing over pattern bindings: insert resolved arm bindings into the arm ScopeId |
| [#618](https://github.com/hjosugi/kofun/issues/618) | Self-host & Bootstrap | `writer-a` | needs-detail | P0 | L | 2026-09-02 | 2026-09-15 | 2026-09-17 | low | Self-host profile: freeze the smallest honest compiler S and coverage manifest |
| [#70](https://github.com/hjosugi/kofun/issues/70) | Frontend & Types | `writer-b` | ready | P2 | M | 2026-09-04 | 2026-09-09 | 2026-09-10 | medium | Optional frontend: parse null and T? into typed IR |
| [#644](https://github.com/hjosugi/kofun/issues/644) | Stdlib & Frameworks | `writer-c` | needs-detail | P2 | M | 2026-09-04 | 2026-09-11 | 2026-09-14 | low | HTTP/1.1 client core: bounded request and response state machine over scripted transport |
| [#725](https://github.com/hjosugi/kofun/issues/725) | Decimal | `writer-b` | blocked | P2 | L | 2026-09-10 | 2026-09-21 | 2026-09-24 | conditional | Decimal slice 6: state scale guarantees truthfully now, and add Fixed[scale] when const generics exist |
| [#645](https://github.com/hjosugi/kofun/issues/645) | Stdlib & Frameworks | `writer-c` | needs-detail | P2 | M | 2026-09-14 | 2026-09-21 | 2026-09-22 | low | Date/time core: Gregorian values, checked arithmetic, and strict RFC 3339 |
| [#570](https://github.com/hjosugi/kofun/issues/570) | Backend & Runtime | `writer-a` | needs-detail | P1 | S | 2026-09-16 | 2026-09-21 | 2026-09-25 | low | Austral: propagate ownership kind structurally through generics and ADTs |
| [#622](https://github.com/hjosugi/kofun/issues/622) | Self-host & Bootstrap | `writer-a` | blocked | P0 | M | 2026-09-22 | 2026-09-25 | 2026-09-28 | conditional | Self-host compiler driver: args, file I/O, and compiling S to deterministic C11 |
| [#312](https://github.com/hjosugi/kofun/issues/312) | Frontend & Types | `writer-b` | blocked | P1 | M | 2026-09-22 | 2026-09-25 | 2026-09-29 | conditional | Optional inference: narrow direct bindings without leaking refinements |
| [#646](https://github.com/hjosugi/kofun/issues/646) | Stdlib & Frameworks | `writer-c` | needs-detail | P2 | M | 2026-09-22 | 2026-09-29 | 2026-09-30 | low | Benchmark report v1: canonical raw-sample codec and deterministic summaries |
| [#571](https://github.com/hjosugi/kofun/issues/571) | Backend & Runtime | `writer-a` | needs-detail | P1 | S | 2026-09-28 | 2026-10-01 | 2026-10-02 | low | Austral/Nim: support zero-copy borrowed results without user-written lifetimes |
| [#726](https://github.com/hjosugi/kofun/issues/726) | Decimal | `writer-b` | blocked | P2 | L | 2026-09-28 | 2026-10-07 | 2026-10-13 | conditional | Decimal slice 7: versioned law evidence, the Float associativity counterexample, and backend differential conformance |
| [#647](https://github.com/hjosugi/kofun/issues/647) | Stdlib & Frameworks | `writer-c` | needs-detail | P2 | M | 2026-09-30 | 2026-10-07 | 2026-10-09 | low | Clock adapters: explicit monotonic/system identities, sleep, and deterministic fake time |
| [#271](https://github.com/hjosugi/kofun/issues/271) | Self-host & Bootstrap | `writer-a` | blocked | P0 | M | 2026-10-02 | 2026-10-07 | 2026-10-08 | conditional | Bootstrap: produce C1/A1 and self-compile S into C2/A2 |
| [#277](https://github.com/hjosugi/kofun/issues/277) | Backend & Runtime | `writer-a` | deferred | P1 | L | 2026-10-08 | 2026-10-19 | 2026-10-22 | low | Freestanding target: run with no operating system |
| [#314](https://github.com/hjosugi/kofun/issues/314) | Frontend & Types | `writer-b` | blocked | P2 | M | 2026-10-08 | 2026-10-13 | 2026-10-14 | conditional | Null coalescing: execute Optional(Int) ?? Int lazily |
| [#767](https://github.com/hjosugi/kofun/issues/767) | Repository & Docs | `writer-c` | needs-detail | P2 | M | 2026-10-08 | 2026-10-15 | 2026-10-16 | low | Move the website out: site/, app/, and the Next/Cloudflare/npm manifests |
| [#550](https://github.com/hjosugi/kofun/issues/550) | Frontend & Types | `writer-b` | in-progress | P1 | M | 2026-10-14 | 2026-10-19 | 2026-10-20 | medium | Implicit return of the final expression — and the lambda/function inconsistency |
| [#648](https://github.com/hjosugi/kofun/issues/648) | Stdlib & Frameworks | `writer-c` | needs-detail | P2 | M | 2026-10-16 | 2026-10-23 | 2026-10-27 | low | Time-zone data v1: versioned tiny tzdb reader with gap and fold resolution |
| [#272](https://github.com/hjosugi/kofun/issues/272) | Self-host & Bootstrap | `writer-a` | blocked | P0 | M | 2026-10-20 | 2026-10-23 | 2026-10-26 | conditional | Bootstrap fixed point: produce C3/A3, compare three generations, close B4/B5 |
| [#332](https://github.com/hjosugi/kofun/issues/332) | Frontend & Types | `writer-b` | ready | P1 | M | 2026-10-20 | 2026-10-23 | 2026-10-28 | medium | Traits frontend: resolve coherent concrete implementations |
| [#576](https://github.com/hjosugi/kofun/issues/576) | Backend & Runtime | `writer-a` | needs-detail | P2 | S | 2026-10-26 | 2026-10-29 | 2026-11-03 | low | Koka: reuse matched ADT constructors in place when storage is unique |
| [#627](https://github.com/hjosugi/kofun/issues/627) | Effects & Concurrency | `writer-b` | ready | P1 | M | 2026-10-26 | 2026-10-29 | 2026-11-02 | medium | Reactive programming: small Stream/Signal protocol with explicit demand and ownership |
| [#584](https://github.com/hjosugi/kofun/issues/584) | Tooling & Diagnostics | `writer-c` | blocked | P2 | M | 2026-10-26 | 2026-10-29 | 2026-10-30 | conditional | Visibility tooling: prevent macro, LSP, and sidecar access bypasses |
| [#274](https://github.com/hjosugi/kofun/issues/274) | Self-host & Bootstrap | `writer-a` | blocked | P1 | M | 2026-10-30 | 2026-11-04 | 2026-11-05 | conditional | Reproducible bootstrap B6: independent clean builder reproduces the fixed point |
| [#625](https://github.com/hjosugi/kofun/issues/625) | Frontend & Types | `writer-b` | needs-decision | P1 | M | 2026-10-30 | 2026-11-09 | 2026-11-10 | low | Function-call ergonomics: labelled arguments and one trailing-lambda rule |
| [#769](https://github.com/hjosugi/kofun/issues/769) | Repository & Docs | `writer-c` | needs-decision | P3 | S | 2026-10-30 | 2026-11-05 | 2026-11-06 | low | Settle docs/: move the 9 documents no manifest resolves, record why the other 27 stay |
| [#278](https://github.com/hjosugi/kofun/issues/278) | Backend & Runtime | `writer-a` | deferred | P2 | L | 2026-11-05 | 2026-11-16 | 2026-11-18 | low | ARM Cortex-M backend (thumb2) |
| [#585](https://github.com/hjosugi/kofun/issues/585) | Repository & Docs | `writer-c` | blocked | P3 | M | 2026-11-06 | 2026-11-11 | 2026-11-12 | conditional | Visibility stabilization: document API changes and migration |
| [#276](https://github.com/hjosugi/kofun/issues/276) | Frontend & Types | `writer-b` | needs-detail | P1 | L | 2026-11-10 | 2026-11-23 | 2026-11-26 | low | Reduce boilerplate: derive, and generated members |
| [#741](https://github.com/hjosugi/kofun/issues/741) | Tooling & Diagnostics | `writer-c` | deferred | P3 | M | 2026-11-12 | 2026-11-17 | 2026-11-19 | low | Typed upgrade patches: track replacements over the semantic dependency graph |
| [#279](https://github.com/hjosugi/kofun/issues/279) | Backend & Runtime | `writer-a` | deferred | P2 | L | 2026-11-17 | 2026-11-26 | 2026-11-30 | low | Memory-mapped I/O, volatile access, and interrupt handlers |
| [#606](https://github.com/hjosugi/kofun/issues/606) | Tooling & Diagnostics | `writer-c` | blocked | P1 | M | 2026-11-18 | 2026-11-23 | 2026-11-24 | conditional | LSP semantic adapter: consume typed sidecars with stale and disclosure guards |
| [#555](https://github.com/hjosugi/kofun/issues/555) | Effects & Concurrency | `writer-b` | needs-detail | P1 | S | 2026-11-24 | 2026-11-27 | 2026-12-02 | low | Concurrency: scoped parallelism needs no new machinery; ownership helps here |
| [#609](https://github.com/hjosugi/kofun/issues/609) | Tooling & Diagnostics | `writer-c` | in-progress | P1 | M | 2026-11-24 | 2026-11-27 | 2026-12-01 | medium | Stage 2 sidecar projector: map semantic events into atomic CLI output |
| [#280](https://github.com/hjosugi/kofun/issues/280) | Backend & Runtime | `writer-a` | deferred | P2 | L | 2026-11-27 | 2026-12-08 | 2026-12-10 | low | No-heap mode: static allocation only |
| [#556](https://github.com/hjosugi/kofun/issues/556) | Effects & Concurrency | `writer-b` | needs-detail | P1 | S | 2026-11-30 | 2026-12-03 | 2026-12-07 | low | Effect systems: ship pure/impure first; multi-shot continuations are unsound with take |
| [#636](https://github.com/hjosugi/kofun/issues/636) | Stdlib & Frameworks | `writer-c` | in-progress | P1 | M | 2026-11-30 | 2026-12-03 | 2026-12-04 | medium | Standard library charter: batteries-included coverage, tiers, compatibility, and update policy |
| [#557](https://github.com/hjosugi/kofun/issues/557) | Frontend & Types | `writer-b` | needs-detail | P1 | S | 2026-12-04 | 2026-12-09 | 2026-12-14 | low | Reading list for the type checker: what to copy, what to skip |
| [#637](https://github.com/hjosugi/kofun/issues/637) | Tooling & Diagnostics | `writer-c` | in-progress | P1 | M | 2026-12-04 | 2026-12-09 | 2026-12-11 | medium | Discovery query v1: project inferred types and callable operations from semantic facts |
| [#536](https://github.com/hjosugi/kofun/issues/536) | Science & Future | `writer-a` | deferred | P2 | L | 2026-12-09 | 2026-12-18 | 2026-12-22 | low | ndarray: n-dimensional arrays with views, strides, and broadcasting |
| [#569](https://github.com/hjosugi/kofun/issues/569) | Effects & Concurrency | `writer-b` | needs-detail | P1 | S | 2026-12-10 | 2026-12-15 | 2026-12-16 | low | Austral: make authority explicit with affine capability values |
| [#27](https://github.com/hjosugi/kofun/issues/27) | Stdlib & Frameworks | `writer-c` | needs-detail | P1 | M | 2026-12-10 | 2026-12-17 | 2026-12-18 | low | Sample: end-to-end JSON API service |
| [#573](https://github.com/hjosugi/kofun/issues/573) | Effects & Concurrency | `writer-b` | needs-detail | P1 | S | 2026-12-16 | 2026-12-21 | 2026-12-23 | low | Odin: make allocator choice a scoped, effect-tracked capability |
| [#281](https://github.com/hjosugi/kofun/issues/281) | Research & Positioning | `writer-c` | needs-detail | P1 | M | 2026-12-18 | 2026-12-25 | 2026-12-28 | low | Position paper: what replacing C and Rust actually requires |
| [#537](https://github.com/hjosugi/kofun/issues/537) | Science & Future | `writer-a` | deferred | P2 | L | 2026-12-21 | 2026-12-30 | 2027-01-01 | low | Linear algebra: matmul, decompositions, solvers, and BLAS/LAPACK binding |
| [#710](https://github.com/hjosugi/kofun/issues/710) | Decimal | `writer-b` | needs-detail | P1 | L | 2026-12-22 | 2027-01-04 | 2027-01-06 | low | Compiler-native Decimal: implement the accepted language design across all backends |
| [#574](https://github.com/hjosugi/kofun/issues/574) | Tooling & Diagnostics | `writer-c` | needs-detail | P1 | S | 2026-12-28 | 2026-12-31 | 2027-01-04 | low | V: generate audited C bindings from Clang AST before attempting source translation |
| [#538](https://github.com/hjosugi/kofun/issues/538) | Science & Future | `writer-a` | deferred | P2 | L | 2026-12-31 | 2027-01-11 | 2027-01-13 | low | Signal and image processing: FFT, convolution, filtering |
| [#757](https://github.com/hjosugi/kofun/issues/757) | Repository & Docs | `writer-c` | ready | P2 | M | 2027-01-01 | 2027-01-06 | 2027-01-07 | medium | Build entry point: replace the Makefile with a Taskfile, keeping every gate byte-identical |
| [#742](https://github.com/hjosugi/kofun/issues/742) | Effects & Concurrency | `writer-b` | ready | P2 | M | 2027-01-05 | 2027-01-08 | 2027-01-11 | medium | Validation accumulation: collect independent errors without running dependent checks |
| [#558](https://github.com/hjosugi/kofun/issues/558) | Repository & Docs | `writer-c` | needs-detail | P2 | S | 2027-01-07 | 2027-01-12 | 2027-01-14 | low | What verification actually costs — and why proof-to-code ratios don't measure dependent types |
| [#735](https://github.com/hjosugi/kofun/issues/735) | Effects & Concurrency | `writer-b` | needs-decision | P2 | M | 2027-01-11 | 2027-01-19 | 2027-01-20 | low | One-shot effect handlers: affine resumptions with a runtime double-resume backstop |
| [#539](https://github.com/hjosugi/kofun/issues/539) | Science & Future | `writer-a` | deferred | P2 | L | 2027-01-12 | 2027-01-21 | 2027-01-25 | low | Statistics: distributions, tests, regression, optimisation, integration |
| [#572](https://github.com/hjosugi/kofun/issues/572) | Tooling & Diagnostics | `writer-c` | needs-detail | P2 | S | 2027-01-13 | 2027-01-18 | 2027-01-19 | low | Nim: infer last-use moves and add a compile-time move assertion |
| [#424](https://github.com/hjosugi/kofun/issues/424) | Repository & Docs | `writer-c` | deferred | P2 | L | 2027-01-19 | 2027-01-28 | 2027-02-02 | low | Hot reload at function granularity, via micro-partition |
| [#736](https://github.com/hjosugi/kofun/issues/736) | Effects & Concurrency | `writer-b` | needs-decision | P2 | M | 2027-01-20 | 2027-01-28 | 2027-01-29 | low | Deterministic structured concurrency: versioned schedule traces, strict replay, and bounded exploration |
| [#540](https://github.com/hjosugi/kofun/issues/540) | Science & Future | `writer-a` | deferred | P2 | L | 2027-01-22 | 2027-02-02 | 2027-02-04 | low | Dataframes: typed columnar tables with group-by and join |
| [#738](https://github.com/hjosugi/kofun/issues/738) | Science & Future | `writer-a` | deferred | P3 | M | 2027-02-03 | 2027-02-08 | 2027-02-09 | low | Time-series forecasting research: rolling-origin backtests, uncertainty, and estimator boundaries |

## Weekly calendar

| Week of | Active issues | Delivered | Writer load |
|---|---|---|---:|
| 2026-07-27 | #551, #583, #748, #770 | #748 | 12/15 |
| 2026-08-03 | #551, #583, #638, #639, #723, #749, #770 | #551, #583, #638, #770 | 15/15 |
| 2026-08-10 | #533, #626, #639, #640, #723, #749 | #723, #749 | 15/15 |
| 2026-08-17 | #533, #626, #639, #640, #724, #740 | #626, #639, #640 | 15/15 |
| 2026-08-24 | #533, #554, #724, #740, #751, #768 | #533, #740, #751 | 15/15 |
| 2026-08-31 | #70, #554, #618, #644, #724, #768, #772 | #554, #724, #772 | 15/15 |
| 2026-09-07 | #70, #618, #644, #725, #768 | #70, #768 | 15/15 |
| 2026-09-14 | #570, #618, #644, #645, #725 | #618, #644 | 15/15 |
| 2026-09-21 | #312, #570, #622, #645, #646, #725 | #570, #645, #725 | 15/15 |
| 2026-09-28 | #271, #312, #571, #622, #646, #647, #726 | #312, #571, #622, #646 | 15/15 |
| 2026-10-05 | #271, #277, #314, #647, #726, #767 | #271, #647 | 15/15 |
| 2026-10-12 | #277, #314, #550, #648, #726, #767 | #314, #726, #767 | 15/15 |
| 2026-10-19 | #272, #277, #332, #550, #648 | #277, #550 | 15/15 |
| 2026-10-26 | #272, #274, #332, #576, #584, #625, #627, #648, #769 | #272, #332, #584, #648 | 15/15 |
| 2026-11-02 | #274, #278, #576, #585, #625, #627, #769 | #274, #576, #627, #769 | 15/15 |
| 2026-11-09 | #276, #278, #585, #625, #741 | #585, #625 | 15/15 |
| 2026-11-16 | #276, #278, #279, #606, #741 | #278, #741 | 15/15 |
| 2026-11-23 | #276, #279, #280, #555, #606, #609 | #276, #606 | 15/15 |
| 2026-11-30 | #279, #280, #555, #556, #557, #609, #636, #637 | #279, #555, #609, #636 | 15/15 |
| 2026-12-07 | #27, #280, #536, #556, #557, #569, #637 | #280, #556, #637 | 15/15 |
| 2026-12-14 | #27, #281, #536, #557, #569, #573 | #27, #557, #569 | 15/15 |
| 2026-12-21 | #281, #536, #537, #573, #710 | #536, #573 | 15/15 |
| 2026-12-28 | #281, #537, #538, #574, #710, #757 | #281, #537 | 15/15 |
| 2027-01-04 | #538, #558, #574, #710, #742, #757 | #574, #710, #757 | 15/15 |
| 2027-01-11 | #538, #539, #558, #572, #735, #742 | #538, #558, #742 | 15/15 |
| 2027-01-18 | #424, #539, #540, #572, #735, #736 | #572, #735 | 15/15 |
| 2027-01-25 | #424, #539, #540, #736 | #539, #736 | 13/15 |
| 2027-02-01 | #424, #540, #738 | #424, #540 | 5/15 |
| 2027-02-08 | #738 | #738 | 1/15 |

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
