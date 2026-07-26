# Naming

## Working title

This prototype uses `Kofun` as a working title.

Intent:

- Start from frustration with Rust
- Easy to associate with functional + Rust
- Short CLI name
- Easy to use with the `.kofun` extension

## Collision risk

The name `kofun` is already used as a crate name in the Rust ecosystem. The same idea has also been published before as shorthand for "functional Rust".

The following are therefore mandatory before any public launch.

1. Search crates.io, PyPI, npm, GitHub, GitLab, and the major Linux distributions.
2. Investigate trademarks for programming languages, compilers, databases, and developer tools.
3. Check domains, social handles, and package namespaces.
4. Consult a legal professional, covering Japan, the United States, and the EU at minimum.
5. Evaluate the project name, CLI name, package prefix, and file extension separately.

## Candidate direction

For the final name, prefer a short coined word that satisfies the following.

- 4 to 7 characters
- Easy to pronounce for both Japanese and English speakers
- Evokes the lightness of `fn` and pipelines
- Not so close as to be mistaken for a Rust-derived language
- Highly searchable
- Package namespace can be secured

This ZIP does not fix the name, and is set up so that namespace changes inside the compiler can be applied in bulk.

## Callable type notation

Callable types use `->` everywhere:

```kofun
Int -> Text
(Int, Text) -> Bool
() -> Int
```

The notation describes a fixed, exact arity. `(A, B) -> R` takes two
arguments, while `Tuple[A, B] -> R` takes one tuple argument. `A -> (B -> R)`
takes one argument and returns another callable; calls are never implicitly
curried, uncurried, partially applied, or converted to and from tuple
arguments.

The historical `Fn[...]` spelling is removed, not retained as an alias.
Migration tooling must give a targeted replacement from `Fn[A, R]` to
`A -> R` and from a historical multi-argument `Fn[A, B, R]` to
`(A, B) -> R`. After migration, `Fn` is an ordinary available identifier.
Stage 2 emits that replacement as `E2S97`, so the rewrite is a diagnostic and
not only a documented intention.

### Why this, and what was rejected

The decision that mattered was currying, not punctuation. Kofun does not
curry, so `add(1)` on a two-argument function is an arity error rather than a
partially applied function. That settles the rest: an arrow only earns its
keep over brackets when the function type is *itself* an arrow chain, which
is a curried language's shape.

Two alternatives were rejected.

- **`Fn[A, B]` in types with `->` in declarations** — the state this replaced.
  It spelled one concept two ways, which is the actual defect; whether `->` is
  worth two characters was never the question.
- **Go-style bare result types** (`fn f(a: Int) Int`) — consistent with
  brackets, but it would have removed the marker that says where a result type
  begins, and Kofun already allows omitting the result type.

There is **no empirical answer** to whether `fn f(a: Int) -> Int` reads better
than `func f(a int) int`. No controlled study measures it, and none is cited
here, because constructing a justification after the fact would be worse than
saying so. What decided it was consistency with the no-currying rule above:
one notation, used in both positions, that does not imply a language feature
Kofun does not have.

Function declarations keep the same arrow before their result:

```kofun
fn render(value: Input) -> Text
```

A Go-style bare result type is not an alternative spelling. Keeping `->` in
declarations and callable types gives one notation without introducing
implicit currying.

## Trait terminology

The public abstraction keyword is `trait`, and an implementation is written
with `impl`. This terminology is fixed independently of the project's working
name.

`trait` means a statically resolved contract whose implementation is selected
at compile time and lowered through a dictionary. It does not imply runtime
interface lookup, dynamic message dispatch, or import-order selection.

The alternatives are rejected for the M2-alpha language:

- `protocol` would rename the existing syntax and documentation without
  changing the semantics, and can suggest runtime protocol dispatch;
- `interface` commonly suggests declaration-site conformance or dynamic
  dispatch, while Kofun deliberately permits coherent retroactive
  implementations when either the trait or the outer nominal type is local;
  and
- unrestricted retroactive conformance is not assigned another keyword. It is
  rejected by the orphan and overlap rules in
  [`TYPE_SYSTEM.md`](TYPE_SYSTEM.md).

Changing `trait` or `impl` is an edition-level language migration. A
documentation, editor, or formatter surface must not use `protocol` or
`interface` as an alias.
