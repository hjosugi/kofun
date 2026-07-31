# Language vision

## Mission

Kofun's mission is to reduce the everyday friction between systems safety and
high-level productivity.

Its distinguishing product position is: **the language where you state an
algebraic law and the compiler hands you a counterexample.** The measured
implementation status for that position and for the wider systems-language
goals lives in the
[implemented-status matrix](MVP_IMPLEMENTED.md). Target design in this
document must not be read as implemented behavior.

Rust made memory safety without a GC practical, through ownership and
borrowing. At the same time, not all application code needs to handle
lifetimes, borrowing, wrapper types, and conversions at the same granularity.

Python and Julia are very fast to write in for exploration, scientific
computing, interviews, and scripts. At the same time, once a program grows,
their type, memory, and concurrency guarantees rely on separate mechanisms.

The long-term design for Kofun draws the following separation.

- everyday data is GC-managed, immutable by default, and type-inferred
- resources are owned, affine, and cleaned up deterministically
- views are stated explicitly with `read` / `edit`, and are normally
  non-escaping
- pure computation is written as ordinary functions
- effects are inferred by the compiler and visible at the API boundaries that
  need them
- performance-critical code is unboxed, specialized, and natively compiled

## Frontier decisions

Kofun is not scheduled as five independent language-research programmes. The
current decisions are:

- keep the direct, self-hosted native backend and preserve an interface for an
  optional second backend; do not adopt MLIR ([#554](https://github.com/hjosugi/kofun/issues/554)).
  The measured costs behind that are recorded in
  [Compiler architecture](COMPILER_ARCHITECTURE.md#backend-strategy)
- keep `read` / `edit` / `take`; if concurrency is introduced, begin with
  scoped parallelism that reuses ownership exclusivity
  ([#555](https://github.com/hjosugi/kofun/issues/555)). What that promises,
  and what it deliberately does not, is recorded in
  [Memory model](MEMORY_MODEL.md#12-concurrency-stance)
- introduce a pure/impure boundary before considering effect rows or handlers
  ([#556](https://github.com/hjosugi/kofun/issues/556)); see below
- reject full dependent types and investigate refinement types only after the
  ordinary type checker is complete
  ([#557](https://github.com/hjosugi/kofun/issues/557),
  [#558](https://github.com/hjosugi/kofun/issues/558)). The measured
  verification costs behind that, and the tenfold-inflated `$87M` figure the
  authors themselves retracted, are recorded in
  [Law system](LAW_SYSTEM.md#what-the-level-above-proven-finite-would-cost)

These decisions are subordinate to the current compiler path. The bounded
user-defined call slice now runs under C11 and direct x86-64/AArch64
([#549](https://github.com/hjosugi/kofun/issues/549)). The first self-hosting
profile deliberately keeps its current string-scanning representation, so
heterogeneous records ([#546](https://github.com/hjosugi/kofun/issues/546))
remain important but do not block the C11 fixed point. Syntax usability and
lawful composition are reviewed in
[#624](https://github.com/hjosugi/kofun/issues/624) through
[#626](https://github.com/hjosugi/kofun/issues/626). Reactive programming stays
a small typed `Stream`/`Signal` library protocol with explicit demand and
ownership ([#627](https://github.com/hjosugi/kofun/issues/627)), not a new
syntax family. None of this expands the P0 compiler profile.

### Effects: a two-point lattice first, and one-shot continuations

Recorded from [#556](https://github.com/hjosugi/kofun/issues/556). The roadmap
lists effects after the bootstrap milestones; this states which design that
means and how it interacts with ownership, so the two are not decided
separately.

**Build a two-point `pure`/`io` split, inferred rather than annotated.** It is
the highest value-to-complexity ratio available: Koka's own case study found
almost all functions inferred total, with only a handful of driver functions
performing side effects, so most of the benefit is reachable from the cheapest
possible lattice. It needs no rows, no unification changes, and no
polymorphism, and it buys purity guarantees, safe memoisation and common
subexpression elimination, and a compiler fast path. Design the lattice as a
degenerate row so it can widen later without breaking source. The bottom
element is named `pure` and folds divergence and panics into it; `total` is not
promised, because deciding termination is undecidable and even Koka's `pure`
permits divergence and exceptions.

**Continuations are one-shot. This is a soundness requirement, not a
performance choice.** Resuming a continuation twice duplicates everything it
captured, including moved-in owned resources, which is a double-free or a
double-use of a value that `take` has already transferred.
[Soundly Handling Linearity](https://arxiv.org/abs/2307.09383) (POPL 2024)
states the conflict directly — linear type systems assume a continuation is
invoked exactly once, handlers allow it to be discarded or invoked more than
once, and the mismatch produced a real soundness bug in Links.
[Affect](https://iris-project.org/pdfs/2025-popl-affect.pdf) (POPL 2025)
attacks the same problem from the affine side and is a verified calculus, not a
shipping language. No production language combines Rust-style ownership with a
full algebraic effect system, and both recent papers were motivated by
soundness failures. Multi-shot continuations plus ownership is an open research
problem and is not to be attempted here.

**If handlers are ever pursued, take Effekt's design rather than Koka's.**
Effekt avoids parametric effect polymorphism by treating all functions as
second-class, so effect types express which capabilities a computation requires
from its context. That is the discipline this language already implements:
second-class references and non-escaping blocks are the same idea, so effect
safety may come nearly free from machinery that already exists, without a row
system. The cost is documented and accepted: blocks cannot be returned or
stored, so no breadth-first parsers whose continuations must be queued, awkward
curried application, and no automatic differentiation.

The reason to avoid the row-based route is inference, not runtime. Runtime is
cheap — OCaml measures 1% mean overhead for code that does not use effects, and
handler setup 10x a plain call against 67x for a concurrency monad on the same
benchmarks. Inference is where the danger is: effect subtyping combined with
polymorphism can make inference undecidable, set-union constraints admit
non-unique solutions, and naive rows need lacks-constraints or
presence/absence flags. Effekt's own paper is candid that effect polymorphism
is "particularly difficult to understand and reason about" and that attempts to
hide it behind syntactic sugar break down and leak "to the startled user". For
a young language, confusing diagnostics in higher-order library code is the
cost it can least afford.

## Product principles

### One day to productive

Limit the concepts needed on the first day.

- `fn`
- `let`
- `if` / `else if` / `else`
- `for` / `while`
- List, Map, Set
- `T?`, `null`, `??`
- `Result`
- `|>`
- `read` / `edit` / `take`

Advanced generics, effects, and metaprogramming can stay out of sight until
they are needed.

### Safety is the default, not ceremony

- reject implicit unsafe conversions
- allow `null` only in optional types
- reject double consumption of an owned resource
- reject mutable aliases
- prevent data races with types and runtime contracts — data-race freedom, not
  race-condition freedom ([Memory model §12](MEMORY_MODEL.md#12-concurrency-stance))
- turn unsupported backend behavior into an explicit compile error

### Functional core, practical shell

- data is immutable by default
- functions, closures, pipelines, ADTs, and pattern matching are first class
- IO and mutation are not hidden entirely; they are tracked as effects
- local mutation is ordinary in interview algorithms
- purity is not a goal in itself

### Performance is designed, then measured

Rather than relying on a "zero cost" slogan, measure the following.

- startup latency
- compile time
- steady-state throughput
- tail latency
- allocation count
- GC pause
- memory footprint
- generated code size
- vectorization rate

### One tool, one standard distribution

The base install is intended to contain not only the compiler but also a
formatter, linter, test runner, documentation, package manager, profiler
protocol, and scientific core.

### Error messages are part of the language

Specify not only the syntax but the experience on failure.

- stable error code
- exact source span
- why it is unsafe or ambiguous
- direct correction
- `kofun explain CODE`
- IDE fix-it

## Non-goals

- Rust source compatibility
- C preprocessor-compatible macros
- every feature in the first release
- abusing dynamic typing as an escape hatch from static types
- implicit numeric narrowing
- hidden network access during builds
- unrestricted script execution at package install time
- syntax novelty for its own sake

## Target users

- people who want to move from Python to type-safe native execution
- people who want Rust's safety but less lifetime ceremony in application code
- people who want to cover scientific computing and systems integration in one
  language
- people who want to use the same language for coding interviews and production
  code
- people who enjoy type-level programming and metaprogramming

## Success criteria

1. A beginner can write basic programs, collections, file IO, and tests in one
   day.
2. Most ownership errors can be explained without lifetime annotations.
3. Numeric kernels are competitive against a C/Rust baseline.
4. Common CLIs, web services, data processing, and scientific workloads can be
   built with the standard tools alone.
5. Compiler crashes, undefined behavior, and silent fallbacks are treated as
   release blockers.
