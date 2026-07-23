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
payload constructors, pattern bindings, and value-producing enum matches
remain outside this executable slice. Independently, the canonical frontend
appends a versioned `kofun-pattern-tree/v1` syntax section for wildcard,
Bool/null/Int literal, unresolved name, constructor, nested, or-pattern, and
parenthesized forms. Literal records retain `literal_kind`, exact token
spelling, and token span; all other nodes likewise retain their required byte
spans and delimiter/separator spans. Because unary minus is a separate token,
`-42` is not accepted as an Int literal pattern. The tree does no resolution,
typing, arity, exhaustiveness, binding, or lowering.
`E2S58` covers malformed/deferred families and 32-depth/256-node budgets; the
focused `--parse-patterns` mode preserves recovered `ErrorPattern` nodes while
normal compilation remains transactional. Exhausting the node budget emits
one fatal `ErrorPattern` for the failing arm and stops the remaining Pattern
scan, so no later occurrence can reuse its ID or span. After a Pattern, only
`if` or `=>` may continue the arm; other tokens produce `E2S58` without leaking
to name resolution. The first reported Pattern diagnostic is selected by the
smallest source start byte, including across nested matches. The bounded validator permits 256
enum-related identifier occurrences per function and keeps unrelated Int code
on a pre-indexed fast path.
Arm-arrow recovery never crosses a top-level comma; it records the malformed
arm without an arrow and resumes at the following independent arm.
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

The main CLI tries this Stage 2 C11 Core first. Its internal
`--compile-outcome` mode reports `0` for successful C emission, `1` for invalid
source, `2` for usage/infrastructure failure, and `3` for validated source
whose lowering is unsupported. Only status `3` may enter the explicit Stage 1
compatibility path; a language diagnostic is never retried by another
frontend. The Stage 1 seed independently requires its exact line-oriented
`fn main()`/`let`/`print` Core before it can commit C output. Direct-native
user-function lowering is not implemented yet.

`bootstrap/stage2/adt_frontend.c` is a separate typed-only checkpoint for flat
nominal ADTs. It collects non-generic zero/one-`Int`-payload constructors before
resolving bounded constructor-returning functions, then emits token and typed
IR artifacts with nominal IDs and byte spans. It deliberately emits no C,
native, Wasm, layout, allocation, match, or runtime representation. The main
CLI does not route ordinary builds through this helper yet.

`bootstrap/stage2/adt_exhaustiveness.c` is the resolved flat-ADT match
checkpoint. It defensively joins the declaration table, lossless Pattern tree,
and lexical ScopeId/BindingId artifact for one source module, then publishes a
typed match projection only after all identities and spans agree. Constructor
coverage is keyed by the resolved owner `SymbolId`; same-spelled constructors
in another module cannot affect the result. Unguarded whole-constructor arms
remove one constructor, unguarded wildcard or binding arms remove the remaining
set, and guarded arms remove nothing. `E2S25` lists missing witnesses in
declaration order and `E2S26` points to a redundant pattern and its earlier
cover. One-`Int` payload constructors accept `_` or one binding; nested payload
usefulness and or-pattern expansion remain outside this bounded slice. Run the
transactional, sanitizer-backed gate with `make adt-exhaustiveness`.

`bootstrap/stage2/module_symbols.c` is the next resolver-side checkpoint. It
consumes a validated inventory of raw `PackageId`, `ModuleId`, and `FileId`
values with exactly one source per module, collects supported function and
flat-ADT headers before inspecting bodies, and emits
`kofun-module-symbols/v1`. Its `NamespaceId` and `SymbolId` values use the
production framed SHA-256/KIF inputs from the accepted module specifications;
file paths, spans, visibility, bodies, and declaration order are excluded from
symbol identity. The adapter inventory used by its test is not manifest
syntax. Imports, partial modules, KIF emission, layout, and backend lowering
remain outside this helper.

`bootstrap/stage2/imports_qualified.c` is the focused same-package qualified
import checkpoint. Its validated adapter inventory has six pipe-delimited
fields:

```text
PackageId|ModuleId|FileId|declared-module-path|logical-path|host-source
```

The declared path is semantic inventory data, not a path inferred from the
filesystem. An optional source `module` header must match it. A leading
`import a.b` resolves exactly one inventoried `ModuleId` in the current
`PackageId` and introduces only the final-component qualifier `b`; it never
introduces an unqualified member, transitive binding, export, or re-export.
In particular, an ordinary import is a private local binding under the
explicit non-widening re-export contract. `pub import` and `pub from` are
reserved re-export forms and are rejected by this ordinary-import checkpoint
rather than being silently treated as private imports.
All declarations and import edges are collected before function bodies.
Import cycles are reported canonically by shortest edge count, rotation to the
smallest raw `ModuleId`, and then lexicographic raw-`ModuleId` sequence. The
bounded dynamic diagnostic retains every edge span and the closing node; it
never truncates a valid package cycle to the fixed base diagnostic buffer.

The helper emits `kofun-imports-qualified/v1`. Import bindings use the
production `kofun.id.import-binding/v1` framed SHA-256 domain over importer
`ModuleId`/`FileId`, the module `NamespaceId`, local qualifier, target
`ModuleId`, and stable numeric tag 1 for the `qualified-module-v1` form.
Qualified-call HIR retains the
binding, target `ModuleId`, target `SymbolId`, component/use spans, validated
signature, and the identity-only access result/proof from
`visibility_access.c`. Private declarations in another file are denied;
`internal` and `pub` declarations are usable inside the package.
The executable qualified-call checkpoint accepts only `Int` parameter and
return types. It validates those tokens before projecting an `Int` signature
or lowering C, so another identifier type fails transactionally with `E2S65`.
This line-oriented artifact is a non-authoritative structural test projection,
not KIF and not the canonical `kofun.typed-sidecar/v1` tooling document. It is
never accepted as compiler input and this transactional helper emits no
partial projection after failure; a future typed-sidecar producer must use the
separate #603 status, disclosure, canonical JSON, and trust boundary.

An optional third output operand emits a bounded reference C lowering for
single-return Int functions. Its linker names are derived from the resolved
target `SymbolId`; the conformance gate compiles and executes the lowering.
This helper and its six-field inventory are not yet routed through `bin/kofun`
or manifest loading. The include boundary around `module_symbols.c` is a
temporary way to reuse production declaration identities; it is deliberately
guarded so the existing standalone collector remains independently buildable.
A later compiler-library extraction can replace that adapter without changing
the HIR schema or identities.

`bootstrap/stage2/imports_selective.c` extends the same resolver boundary with
bounded `from a.b import Name, Other` declarations. Each requested spelling
binds the accessible declaration in every matching semantic namespace, keeps
the target `SymbolId`, and derives a distinct selective `ImportBindingId` using
stable form tag 2. The resolver retains every keyword, path component, name,
comma, declaration, call, and type-reference span in its deterministic test
projection. Qualified and selective bindings may coexist; neither introduces
unlisted, transitive, or re-exported names. Duplicate requests, missing or
inaccessible names, local/import collisions, wrong-namespace uses, aliases,
wildcards, malformed lists, and imports after declarations fail before the HIR
or optional reference C output is committed. The two outputs are installed as
one rollback-capable transaction. Run the gate with `make imports-selective`.

Focused diagnostics are `E2S59` malformed/order/path, `E2S60` missing module,
`E2S61` self import, `E2S62` duplicate import, `E2S63` qualifier collision,
`E2S64` canonical cycle, `E2S65` qualified lookup/signature/arity/lowering,
`E2S66` access denial, `E2S67` bounded-resource exhaustion, and `E2S68`
allocation/invariant/output failure. Semantic failure removes both requested
artifacts.

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

`tests/conformance/modules/top-level-declarations/run.sh` covers production
identity framing, same-module forward/recursive/mutual references, value/type
namespace separation, canonical multi-module inventory order, path and source
order invariance, transaction failure, fixed resource limits, and exact
E2S48–E2S56 inventory. It also runs the collector with C11 warnings as errors,
GCC analyzer when available, AddressSanitizer, and UndefinedBehaviorSanitizer.

`tests/conformance/patterns/run.sh` covers the canonical lossless Pattern tree,
path-independent deterministic goldens, statement/value arm nesting, hard
recovery synchronization, normal-compile transactionality, exact `E2S58`,
and the depth/node boundary plus one-over cases. General Pattern syntax is not
evidence of executable destructuring.

`tests/conformance/modules/imports-qualified/run.sh` covers two-module
qualified resolution, production binding/target identities, absolute source
remapping, no unqualified leakage, missing/self/duplicate/colliding imports,
canonical cycles, visibility enforcement, resource rejection, transaction
failure, and reference-backend execution through the resolved `SymbolId`.

`kif_v1.c` and `kif_v1.h` implement the compiler-authoritative KIF v1 binary
writer/reader for bounded function and flat-ADT facts. Records use explicit
schema tags and big-endian widths, canonical identity ordering, distinct
public and package-internal semantic views, defensive limits, full self-read,
and atomic replacement. `kif_v1_tool.c` projects the committed declaration
table, emits non-authoritative JSON only after validation, and resolves the
qualified-import slice from a validated KIF while the dependency source is
absent. `tests/conformance/modules/kif-v1/run.sh` covers deterministic bytes,
visibility, digests, source-free consumption, corruption mutations, exact
limits, failed publication, C11 warnings, sanitizers, and static analysis.

`bootstrap/stage2/visibility_access.c` is the pure access primitive for the
next resolver slice. It compares only schema-tagged 32-byte package, module,
file, and optional type-owner identities; it has no filesystem, name, import,
target, linker, or runtime input. The table-driven
`tests/conformance/modules/visibility-access/run.sh` gate verifies exact
allowed, denied, and unsupported results. The focused qualified-import
resolver calls it for each cross-file target; the main CLI is not routed
through that resolver yet.

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
