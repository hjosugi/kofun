# Stage 2 semantic frontend checkpoint

`compiler.kofun` is the canonical implementation. It stays inside the typed
bootstrap subset already exercised by the Stage 1 seed:

- `Int`, `Bool`, `Text`, and `List[Text]`;
- typed functions and direct calls;
- `if`, `while`, and indexed text/character traversal;
- `args`, `read_text`, `write_text`, `chars`, `len`, `text_slice`,
  `contains`, `starts_with`, `is_digit`, `is_space`, and `to_text`.

The frontend performs five concrete operations:

1. lexical scanning that ignores comments and treats escaped strings as single
   tokens, producing a deterministic token-span tape;
2. structural parsing of a compilation unit into a textual function IR,
   including names, arities, and byte spans;
3. an identity source projection gated by successful lexing and parsing.
4. statement and precedence-aware expression parsing for a deliberately small
   integer Core, followed by deterministic standalone C11 lowering.
5. a type-directed ownership slice for explicitly typed borrowed Lists:
   returning an `Int`, `Float`, `Bool`, or `Unit` iteration element is Copy,
   while moving a non-Copy element such as `Text` is rejected with `E007`.

The identity operation is deliberately conservative. Reapplying it reaches a
byte fixed point, which gives later lowering work a deterministic frontend
boundary. When the output path ends in `.c`, the same frontend instead accepts
one zero-argument `fn main()` plus zero or more `Int` Core functions and lowers:

- immutable or mutable `let` bindings, with optional `Int` annotations;
- assignment to declared mutable `Int` bindings, with immutable and unknown
  targets rejected before C emission;
- integer literals, bindings, parentheses, unary `+`/`-`, `+`, `-`, `*`,
  floor `//`, and floor `%`;
- `Int` parameters and returns;
- direct calls in value or statement position, including forward references
  and recursion;
- statement-position `if` with mandatory braces, optional `else`, nesting,
  Bool literals, and integer `==`, `!=`, `<`, `<=`, `>`, `>=` conditions;
- `print(Int)` and `return Int`.

The emitted C11 uses checked arithmetic helpers and preserves Kofun floor
division/modulo behavior for negative operands. Assignment evaluates and checks
the replacement value before changing the binding. Conditions evaluate once
and only the selected branch executes. Value-producing `if`, `else if`,
general Bool expressions, and loops remain outside this Core slice.
Assignment is currently block-local: changing an outer binding from inside an
`if` branch is rejected with `E2S22` rather than being silently miscompiled.
Top-level prototypes make
declaration order irrelevant. The lowerer rejects unknown calls, duplicate
function names, wrong arity, non-`Int` parameters, and non-`Int` helper return
types before invoking the host compiler.

The main CLI tries this Stage 2 C11 Core first and uses the Stage 1 seed as a
compatibility fallback for inputs outside this slice. Direct-native
user-function lowering is not implemented yet.

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
and `functions_fixture.kofun` proves arguments, results, recursion, an ignored
zero-argument call, and a forward reference through both the seed and the main
CLI. Exact golden diagnostics cover unknown functions and arity mismatch. A
structurally valid non-Core function verifies explicit lowering rejection.
Dedicated positive and negative fixtures exercise the ownership slice both
through the Stage 2 seed and `kofun check`; unrelated structural programs are
explicitly rejected as outside that slice. The gate uses only POSIX shell, a
C11 compiler, `sha256sum`, and standard comparison/search tools.

`compiler.c` is an audited executable transliteration of the Kofun source so
this checkpoint can run before Stage 1 accepts all of Stage 2. It is part of the
temporary trusted seed, not evidence that Kofun has completed self-hosting. The
integer Core lowering is real, but Stage 2 still cannot lower its own Text,
List[Text], file-I/O, and control-flow-heavy implementation. Full semantic
self-compilation therefore remains open. The next bootstrap milestone is to
extend the Kofun compiler path until it can rebuild this seed from
`compiler.kofun`, then compare the resulting artifact.

The Copy/borrow checker is likewise intentionally bounded. It recognizes one
explicit `read List[T]` parameter per function, a named `for` iteration, and a
same-line return that contains the element. It does not claim full inference,
borrow lifetimes, `take` call resolution, or collection code generation.
