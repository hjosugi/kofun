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
2. structural parsing of a compilation unit into textual function and
   payload-free enum IR, including names, constructor tags, arities, byte
   spans, and top-level function visibility metadata;
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
- bounded Int-valued `if` in `let`, `print`, assignment, and `return`, with
  mandatory `else`, nested values, and selected-only evaluation;
- exhaustive statement-position Bool `match` with `true`, `false`, and `_`
  block arms, including nested matches and ordered optional Bool guards;
- bounded Int-valued Bool `match` in `let`, `print`, assignment, and `return`,
  with nested value `if`/`match` and selected-only evaluation;
- top-level, non-generic payload-free enum declarations, explicitly typed
  immutable constructor bindings, and exhaustive statement-position enum
  `match` with ordered guards and `_`;
- `print(Int)` and `return Int`.

The emitted C11 uses checked arithmetic helpers and preserves Kofun floor
division/modulo behavior for negative operands. Assignment evaluates and checks
the replacement value before changing the binding. Conditions evaluate once
and only the selected branch executes. Value `if` requires one final Int
expression in each branch; general typed value blocks, `else if`, general Bool
expressions, and loops remain outside this Core slice. Bool match uses a finite
`{true, false}` coverage check; `E2S25` names missing patterns and `E2S26`
rejects duplicate or unreachable arms. Guards run only after their pattern
matches; false continues to the next arm, and guarded arms do not provide
static coverage. `E2S29` rejects non-Bool guards. `E2S30` rejects bounded value
arms that do not produce Int. Payload-free enum matches apply the same finite
coverage rules to their declared constructor set; `E2S31` rejects malformed or
colliding bounded declarations and `E2S32` rejects unresolved or mismatched
enum uses. Enum bindings remain match-only, including through lexically nested
blocks, so their internal tags cannot escape into Int expressions. Generic or
payload constructors, pattern bindings, nested patterns, and value-producing
enum matches remain outside this slice. The bounded validator permits 256
enum-related identifier occurrences per function and keeps unrelated Int code
on a pre-indexed fast path.
Assignment is currently block-local: changing an outer binding from inside an
`if` or `match` branch is rejected with `E2S22` rather than being silently
miscompiled.
Top-level prototypes make
declaration order irrelevant. The lowerer rejects unknown calls, duplicate
function names, wrong arity, non-`Int` parameters, and non-`Int` helper return
types before invoking the host compiler.

Top-level functions accept an omitted modifier, `private`, `internal`, or
`pub`. Structural IR preserves semantic visibility, implicit versus explicit
origin, the modifier/declaration spans, `file:0`, and a declaration-order
symbol identity. These spellings remain identifier tokens elsewhere. `E2S33`
rejects malformed, duplicate, conflicting, or misplaced basic modifiers;
`E2S34` rejects Java/Rust aliases and deferred `pub(...)` forms. This slice
does not enforce access across files, modules, packages, imports, signatures,
tooling, FFI, or linker symbols.

The main CLI tries this Stage 2 C11 Core first and uses the Stage 1 seed as a
compatibility fallback for inputs outside this slice. Direct-native
user-function lowering is not implemented yet.

`bootstrap/stage2/adt_frontend.c` is a separate typed-only checkpoint for flat
nominal ADTs. It collects non-generic zero/one-`Int`-payload constructors before
resolving bounded constructor-returning functions, then emits token and typed
IR artifacts with nominal IDs and byte spans. It deliberately emits no C,
native, Wasm, layout, allocation, match, or runtime representation. The main
CLI does not route ordinary builds through this helper yet.

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

`tests/conformance/modules/visibility-syntax/run.sh` separately covers all
basic visibility forms, same-file forward calls and execution, contextual
identifier uses, exact modifier diagnostics, artifact absence, and
byte-identical repeated output.

`tests/conformance/adt/run.sh` covers the typed-only MaybeInt checkpoint,
constructor-before-declaration resolution, deterministic IR/token artifacts,
zero/one-payload typing, and exact E2S36–E2S46 diagnostics for invalid or
explicitly deferred ADT forms.

`bootstrap/stage2/visibility_access.c` is the pure access primitive for the
next resolver slice. It compares only schema-tagged 32-byte package, module,
file, and optional type-owner identities; it has no filesystem, name, import,
target, linker, or runtime input. The table-driven
`tests/conformance/modules/visibility-access/run.sh` gate verifies exact
allowed, denied, and unsupported results. General module resolution does not
call it yet.

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
