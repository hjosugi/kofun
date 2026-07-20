# Stage 2 semantic frontend checkpoint

`compiler.kofun` is the canonical implementation. It stays inside the typed
bootstrap subset already exercised by the Stage 1 seed:

- `Int`, `Bool`, `Text`, and `List[Text]`;
- typed functions and direct calls;
- `if`, `while`, and indexed text/character traversal;
- `args`, `read_text`, `write_text`, `chars`, `len`, `text_slice`,
  `contains`, `starts_with`, `is_digit`, `is_space`, and `to_text`.

The frontend performs four concrete operations:

1. lexical scanning that ignores comments and treats escaped strings as single
   tokens, producing a deterministic token-span tape;
2. structural parsing of a compilation unit into a textual function IR,
   including names, arities, and byte spans, with bounded error recovery;
3. an identity source projection gated by successful lexing and parsing.
4. statement and precedence-aware expression parsing for a deliberately small
   typed Core, followed by deterministic standalone C11 lowering.

The identity operation is deliberately conservative. Reapplying it reaches a byte
fixed point, which gives later lowering work a deterministic frontend boundary.
When the output path ends in `.c`, the same frontend instead accepts exactly
one zero-argument `fn main()` and lowers:

- immutable or mutable `let` bindings, with optional `Int` or `Bool`
  annotations, plus type-preserving mutable assignment;
- immutable `List[Int]` bindings from non-empty literals such as
  `[1, 2, 3]`, with an optional `List[Int]` annotation;
- runtime `Int` indexing such as `xs[i]`, lowered with explicit length
  metadata and a checked lookup;
- integer literals, bindings, parentheses, unary `+`/`-`, `+`, `-`, `*`,
  floor `//`, and floor `%`;
- Boolean literals, unary `!`, integer comparisons, same-type equality, and
  short-circuit `&&`/`||`;
- immutable value-position `if condition { expression } else { expression }`
  for unified `Int` or `Bool` branches;
- nested `if`/`else` statements and `while` loops;
- `print(Int | Bool)` and `return Int`.

The emitted C11 uses checked arithmetic helpers and preserves Kofun floor
division/modulo behavior for negative operands. Logical operators use C
short-circuit evaluation, so an unevaluated right-hand expression cannot
trigger a checked arithmetic failure. Value-position `if` uses the C11
conditional operator, which evaluates its condition once and only its selected
branch. A `List[Int]` value carries an Int64
length alongside its Int64 elements. An index below zero or greater than or
equal to that length exits with status 1 and writes exactly
`error[R023]: List index out of bounds` to stderr.

The lowerer carries a lexical binding environment through each block. A child
block can read and mutate eligible outer bindings, but declarations in the
child do not escape it. Unknown or out-of-scope names use `E2S30`, immutable
assignment uses `E2S31`, and Int/Bool misuse uses `E2S32`; diagnostics include a
stable byte offset and fail before a C output artifact is written. List
elements and indexes are checked as `Int`; unknown index bindings retain the
same `E2S30` contract.

This is deliberately the first Issue #8 acceptance slice. Empty and mutable
lists, general native-backend or interpreter parity, and collection operations
such as `map`, `filter`, and `fold` remain open.

The Issue #74 checkpoint accepts exactly one expression in each value branch,
requires `else` with `E2S15`, and requires both branches to produce the same
`Int` or `Bool` type with `E2S32`. General value-producing blocks, declarations
inside value branches, `else if` values, typed IR, and native/interpreter parity
remain open.

The Issue #547 syntax checkpoint recognizes `=>` as a two-character pair token
and preserves expression arrows, parenthesized multi-argument arrows, pipeline
arrows, and trailing-lambda arrows in structural projection. C11 lowering
rejects these forms explicitly with `E2S10`; lambda inference, closure values,
and trailing-lambda execution remain open.

## Parser recovery

On malformed top-level input, the parser collects diagnostics in source order
instead of stopping after the first one. Each record has a stable code and
half-open byte span:

```text
diagnostic|E2S03|120|144|malformed-function
```

Recovery synchronizes only at a column-zero `fn`, a deliberately conservative
boundary that avoids mistaking an indented nested function for a new
compilation-unit declaration. A synchronization scan examines at most 4096
tokens, one parse records at most eight diagnostics, and every recovery branch
advances the cursor. Reports include `truncated|true` when either bound prevents
the parser from consuming the remaining input.

Normal compilation still returns status 1 and writes no source, IR, or token
artifact when diagnostics exist. The explicit recovery form is intended for
editors and diagnostic tooling:

```sh
kofun-stage2 INPUT.kofun UNUSED.kofun OUTPUT.ir OUTPUT.tokens --recover
```

It writes the recovery IR and token tape, preserves any valid functions found
after malformed regions, never writes `UNUSED.kofun`, and still returns status
1 when the report contains diagnostics.

## Verification

Run:

```sh
sh bootstrap/stage2/check.sh
```

The check validates the canonical-source and seed hashes, compiles the audited
C11 seed, round-trips the fixture, current Stage 1 compiler, and Stage 2
compiler byte-for-byte, inspects their function IR, checks token-tape
determinism, and rejects a missing closing brace. It also lowers
`core_fixture.kofun` twice, compares the C/IR/token artifacts, compiles the C11
with warnings as errors, executes it, and compares exact output and status. A
second generated program verifies the division-by-zero status/stderr contract,
and a structurally valid non-Core function verifies explicit lowering
rejection. A nested branch/loop fixture observes mutation, lexical scope,
comparison, Bool printing, and short-circuit behavior in compiled C11.
The value-position-if fixture checks immutable Int/Bool results and places
division by zero in both unselected branches to prove selected-branch-only
evaluation. Negative fixtures compare exact mandatory-else, condition-type,
branch-unification, and unsupported-result-type diagnostics.
The List fixture checks an annotated five-element literal and a variable index,
then compares exact `3` output. Its generated C11 is reproduced twice,
byte-compared, compiled with warnings as errors, and executed; a second fixture
checks the same path with inferred binding types. A bounds fixture compares the
exact `R023` stderr/status contract. Negative fixtures compare exact scope,
mutability, mixed-type, List-element, List-index, and unknown-index diagnostics.
Recovery corpus gates compare exact diagnostic codes/spans/order,
verify later valid function IR, assert the eight-diagnostic cap, and ensure
normal error compilation emits no artifacts. The gate uses only POSIX shell, a
C11 compiler, `sha256sum`, and standard comparison/search tools.

`compiler.c` is an audited executable transliteration of the Kofun source so
this checkpoint can run before Stage 1 accepts all of Stage 2. It is part of the
temporary trusted seed, not evidence that Kofun has completed self-hosting. The
typed Core lowering is real, but Stage 2 still cannot lower its own Text,
List[Text], file-I/O, and control-flow-heavy implementation. Full semantic
self-compilation therefore remains open. The next bootstrap milestone is to
extend the Kofun compiler path until it can rebuild this seed from
`compiler.kofun`, then compare the resulting artifact.

The bounded ownership slice also remains separate from general ownership: it
checks one explicitly typed borrowed List parameter and rejects non-Copy moves,
but does not provide full inference, lifetime analysis, or collection codegen.
