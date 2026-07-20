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

The identity operation is deliberately conservative. Reapplying it reaches a byte
fixed point, which gives later lowering work a deterministic frontend boundary.
When the output path ends in `.c`, the same frontend instead accepts exactly
one zero-argument `fn main()` and lowers:

- immutable or mutable `let` bindings, with optional `Int` annotations;
- integer literals, bindings, parentheses, unary `+`/`-`, `+`, `-`, `*`,
  floor `//`, and floor `%`;
- `print(Int)` and `return Int`.

The emitted C11 uses checked arithmetic helpers and preserves Kofun floor
division/modulo behavior for negative operands.

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
rejection. Dedicated positive and negative fixtures exercise the ownership
slice both through the Stage 2 seed and `kofun check`; unrelated structural
programs are explicitly rejected as outside that slice. The gate uses only
POSIX shell, a C11 compiler, `sha256sum`, and standard comparison/search tools.

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
