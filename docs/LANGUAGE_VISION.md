# Language vision

## Mission

Kofun's mission is to reduce the everyday friction between systems safety and
high-level productivity.

Its distinguishing product position is: **the language where you state an
algebraic law and the compiler hands you a counterexample.** The measured
implementation status for that position and for the wider systems-language
goals lives in the
[README checklist](../README.md#measured-project-status). Target design in this
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
  optional second backend; do not adopt MLIR ([#554](https://github.com/hjosugi/kofun/issues/554))
- keep `read` / `edit` / `take`; if concurrency is introduced, begin with
  scoped parallelism that reuses ownership exclusivity
  ([#555](https://github.com/hjosugi/kofun/issues/555))
- introduce a pure/impure boundary before considering effect rows or handlers
  ([#556](https://github.com/hjosugi/kofun/issues/556))
- reject full dependent types and investigate refinement types only after the
  ordinary type checker is complete
  ([#557](https://github.com/hjosugi/kofun/issues/557),
  [#558](https://github.com/hjosugi/kofun/issues/558))

These decisions are subordinate to the current compiler path. The bounded
user-defined call slice now runs under C11 and direct x86-64
([#549](https://github.com/hjosugi/kofun/issues/549)); heterogeneous records
remain the next blocker
([#546](https://github.com/hjosugi/kofun/issues/546)).

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
- prevent data races with types and runtime contracts
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
