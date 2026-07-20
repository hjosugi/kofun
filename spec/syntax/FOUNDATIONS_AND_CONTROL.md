# Foundations and control-flow syntax

Status: normative design target for GitHub issues #35 through #47.

This document defines the intended source-level contract for the foundation
and control-flow subjects listed below. It is normative about the language
design, but it is not a claim that every rule is implemented. The
implementation column and each section's status paragraph distinguish
executable behavior from design-only behavior. Until an unsupported rule is
implemented and added to conformance, a compiler must reject it rather than
silently assign different semantics.

The words **must**, **must not**, **should**, and **may** are normative. A
source file is interpreted as Unicode scalar values after UTF-8 decoding.
Examples marked `valid` are canonical Kofun. Examples marked `invalid` must be
rejected by a conforming implementation, even when the current bootstrap
compiler cannot yet produce the final diagnostic.

## Implementation evidence

| Issue | Subject | Current executable evidence | Status |
| --- | --- | --- | --- |
| #35 | keyword minimalism | Stage 2 recognizes the Core keywords used by the executable fixture | partial |
| #36 | Unicode identifiers | Stage 1 and Stage 2 deliberately accept ASCII identifiers only | unsupported |
| #37 | function declarations | Stage 2 records top-level function names, arities, and spans; C lowering accepts only `fn main()` | partial |
| #38 | automatic statement termination | newline-separated Core statements compile and execute | partial |
| #39 | mutable bindings | Stage 2 Core executes mutable `Int` rebinding and rejects immutable or undeclared targets | implemented for Core |
| #40 | lambda expressions | structural projection preserves the tokens; Core lowering rejects the statement | unsupported |
| #41 | immutable bindings | Stage 1 and Stage 2 Core compile and execute integer `let` bindings | implemented for Core |
| #42 | owned bindings | structural projection preserves the tokens; Core lowering rejects the statement | unsupported |
| #43 | if expressions | Stage 2 lowers statement-position and bounded Int-valued `if` with Bool literals or integer comparisons | partial |
| #44 | else-if chains | structural projection preserves the tokens; Core lowering rejects the statement | unsupported |
| #45 | for loops | structural projection preserves the tokens; Core lowering rejects the statement | unsupported |
| #46 | match expressions | Stage 2 executes exhaustive guarded statement-position Bool matches and rejects missing/unreachable arms | partial |
| #47 | while loops | structural projection preserves the tokens; Core lowering rejects the statement | unsupported |

The executable evidence is in
`tests/conformance/syntax/issues_35_47/`. A structural projection proves only
that the bootstrap frontend preserves a balanced function body. It is not
evidence of name resolution, type checking, ownership checking, evaluation,
or native lowering.

## Common lexical rules

Whitespace separates tokens except inside a string. Braces delimit blocks;
indentation never changes program structure. `#` begins a line comment outside
a string. The longest valid token is selected, so `->`, `=>`, `==`, `!=`,
`<=`, `>=`, `&&`, `||`, `//`, `..`, `**`, `??`, and `|>` are each one token.

A block contains zero or more declarations or statements. Unless a section
states otherwise, a block in value position has the value of its final bare
expression. A block with no final bare expression has type `Void`.

## #35 — Keyword minimalism

### User stories and non-goals

- A new user can read a small program after learning a compact, searchable
  vocabulary.
- A tool can distinguish structural words from ordinary names without semantic
  guessing.
- It is not a goal to eliminate every keyword, make punctuation carry ownership
  semantics, or reserve likely future English words.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| Go's small fixed keyword set | easy to memorize and lex | adding syntax later can require a new reserved word |
| Rust's mix of strict and contextual keywords | supports edition migration | contextual interpretation increases parser state |
| Lisp's list structure and ordinary symbol heads | very small lexical core | punctuation and nesting are unfamiliar to many users |

Kofun chooses a small fixed set for control and ownership, and ordinary names
for library operations such as `print`, `map`, and `len`.

### Normative contract

The hard keywords in this scope are:

```text
fn let mut own read edit take return
if else for in while break continue match
true false null
```

`law`, `monad`, and `meta` are also hard keywords in the broader language
grammar. A hard keyword must not be used as an identifier. `else if` is two
keywords, not a compound `elif` keyword. Built-in type names such as `Int`,
`Bool`, and `Text`, and callable names such as `print`, are ordinary
predeclared identifiers. No identifier-escape syntax is defined.

Adding a hard keyword is an edition-level compatibility change. A future
feature should prefer a contextual word only when its position is already
unambiguous and treating it as contextual cannot change the tokenization of an
existing program.

### Canonical and rejected forms

```kofun
# valid: library behavior remains an ordinary call
fn main() {
    let answer = 42
    print(answer)
}
```

```kofun
# invalid: a hard keyword cannot be a binding name
let while = 42
```

**Implementation status:** the bootstrap token tape classifies a subset of
these words. It does not yet enforce the complete reserved-word contract, and
no keyword migration tool or security sign-off exists.

## #36 — Unicode identifiers

### User stories and non-goals

- Authors can use names from their writing system without transliteration.
- Formatters, linkers, and editors agree on whether two spellings are the same
  name.
- It is not a goal to accept arbitrary emoji, invisible controls, or Unicode
  escapes inside identifiers.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| Python's identifier normalization | canonically equivalent-looking names resolve together | compatibility normalization can merge surprising spellings |
| Rust's Unicode XID rules | well-defined language-neutral start/continue sets | visual confusables remain possible |
| ECMAScript identifier escapes | ASCII-only files can name Unicode symbols | escaped and literal spellings complicate source review |

Kofun uses literal, normalized Unicode with no identifier escapes.

### Normative contract

This draft uses the Unicode 15.1.0 `XID_Start` and `XID_Continue` derived
properties. An identifier must:

1. be valid UTF-8 and already be in NFC;
2. start with `_` or an `XID_Start` scalar;
3. continue with zero or more `_` or `XID_Continue` scalars;
4. not equal `_`, which is reserved for the discard pattern; and
5. not equal a hard keyword after NFC checking.

Name equality is scalar-for-scalar equality of the required NFC spelling. The
compiler must reject non-NFC source rather than silently rewrite it, and the
diagnostic should show the NFC replacement. Formatters must preserve accepted
identifier spelling. Confusable detection may warn, but must not change name
resolution.

```kofun
# valid
fn 面積(幅: Int, 高さ: Int) -> Int {
    return 幅 * 高さ
}
```

```kofun
# invalid: emoji is not XID_Start
let 🚀 = 1
```

**Implementation status:** both executable bootstrap frontends are ASCII-only.
The Unicode example is a design target and is explicitly rejected by the
current conformance runner. Unicode tables, normalization, confusable
diagnostics, and cross-module resolution tests remain open.

## #37 — Function declarations

### User stories and non-goals

- A reader can identify a function's name, parameters, ownership modes, and
  result type from one header.
- Recursion and forward calls do not depend on textual declaration order.
- It is not a goal to encode overloading, implicit currying, or effects in
  punctuation.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| Rust `fn name(args) -> T` | explicit modes and result type | headers can become verbose |
| ML curried declarations | concise higher-order composition | arity and tuple intent can be less obvious |
| Go function declarations | readable, fixed arity, simple returns | return types use a different visual convention |

### Normative contract

The canonical forms are:

```text
function       := "fn" IDENT "(" parameters? ")" return-type? function-body
parameters     := parameter ("," parameter)* ","?
parameter      := ownership-mode? IDENT (":" type)?
ownership-mode := "read" | "edit" | "take"
return-type    := "->" type
function-body  := block | "=" expression
```

A declaration introduces its name in its enclosing declaration scope.
Function headers in one scope are collected before bodies are resolved, so
recursion and forward calls are deterministic. Duplicate names with the same
scope are an error; this draft defines no overload set.

Arguments are evaluated left to right exactly once. Parameters without an
ownership mode use value mode. An omitted parameter type must be inferred
monomorphically or rejected as ambiguous. An omitted return type is inferred
from every reachable return and the body value. `return` exits the innermost
function only. An expression body is equivalent to a block whose final
expression is that expression.

```kofun
# valid
fn twice(value: Int) -> Int = value * 2

fn answer() -> Int {
    return twice(21)
}
```

```kofun
# invalid: duplicate parameter name
fn add(value: Int, value: Int) -> Int = value + value
```

**Implementation status:** Stage 2 structural IR records top-level function
names, arities, and byte spans, including forward declarations. It does not
resolve calls or infer types. Its executable C Core requires exactly one
zero-argument `fn main()` and does not lower general functions.

## #38 — Automatic statement termination

### User stories and non-goals

- Ordinary code needs neither semicolons nor indentation-sensitive grammar.
- A multiline expression remains one expression when its syntax visibly
  continues.
- It is not a goal to recover every malformed line break or copy JavaScript's
  unrestricted automatic semicolon insertion.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| Go's lexical semicolon insertion | deterministic and simple | a line break before `.` or `{` can surprise users |
| JavaScript ASI | tolerates semicolon-free code | restricted productions and prefix operators create hazards |
| Python's newline plus open-delimiter continuation | predictable multiline expressions | indentation participates in block syntax |

Kofun keeps braced blocks and uses a conservative newline separator.

### Normative contract

`NEWLINE` terminates a statement only when all of these are true:

1. the lexer is not inside `()`, `[]`, or a string;
2. the preceding token can end an expression or is `break`, `continue`, or
   `return`;
3. the next non-comment token can begin a declaration or statement, is `}`,
   or is end of file; and
4. the next token is not `else`, and the preceding token is not an infix
   operator, prefix operator, `.`, `,`, `:`, `=`, `->`, or `=>`.

A semicolon is an explicit statement separator wherever such a newline would
be valid. Empty separators have no effect. A newline after bare `return`
terminates it; returning a value on another line requires parentheses.

```kofun
# valid
let answer =
    (20 +
     22)
print(answer)
```

```kofun
# invalid: newline terminates return
fn answer() -> Int {
    return
    42
}
```

**Implementation status:** newline-separated Stage 1 and Stage 2 integer Core
statements execute. The bootstrap frontends currently treat broad whitespace
as trivia rather than implementing the complete insertion algorithm; the
operator, `else`, semicolon, and bare-return boundaries need parser
conformance tests.

## #39 — Mutable bindings

### User stories and non-goals

- An algorithm can update a clearly marked local variable.
- A reader can distinguish rebinding from ownership transfer or mutation
  through an exclusive view.
- It is not a goal to make every value mutable or to infer mutability from
  assignment.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| Rust `let mut` | mutability is local and visible at declaration | repeated annotation adds ceremony |
| JavaScript `let`/`const` | rebinding distinction is familiar | object mutation is independent and often confusing |
| ML reference cells | mutation is explicit in the value type | dereference syntax obscures simple loop variables |

### Normative contract

`let mut name: T = expression` evaluates `expression` once, creates a binding
of type `T`, and permits assignment to that binding. The annotation may be
omitted when `T` is inferred. `name = replacement` evaluates `replacement`
before changing `name`; `replacement` must have type `T`. Assignment has type
`Void` and is not an expression.

`mut` changes only whether the binding may be reassigned. Mutation of an owned
resource or through a borrowed view is governed by `edit`, not by `mut`.
Reading a mutable binding has the same value semantics as reading an immutable
one. Capturing mutable bindings in closures is not implicitly allowed by this
draft; a future capture-mode design must make exclusive access explicit.

```kofun
# valid
let mut remaining = 3
remaining = remaining - 1
```

```kofun
# invalid: `mut` follows `let`, not the name or type
let remaining mut: Int = 3
```

**Implementation status:** Stage 2 Core accepts and executes mutable `Int`
declarations and assignment. It evaluates checked replacement expressions
before changing storage, and emits span-carrying `E2S22` diagnostics with
correction hints for immutable or undeclared targets. Assignment through
members/indexes, non-`Int` type checking, and closure capture checking remain
open. Until lexical scope resolution replaces the bounded declaration scan,
assignment to an outer binding from inside an `if` branch is explicitly
rejected with `E2S22`; same-block assignment followed by `if` executes.

## #40 — Lambda expressions

### User stories and non-goals

- A higher-order call can receive short behavior without a separately named
  function.
- Lambda parameter syntax matches named-function parameter syntax.
- It is not a goal to add a second arrow convention, implicit tuple
  destructuring, or invisible ownership captures.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| JavaScript arrow functions | concise expression closures | lexical `this` and several parameter forms add special cases |
| Rust `|args| expression` | visually compact and capture-aware | parameter syntax differs from named functions |
| ML `fn pattern => expression` | one uniform functional form | repeated arms and currying are unfamiliar to many users |

Kofun reuses `fn` and named-function parameters.

### Normative contract

```text
lambda := "fn" "(" parameters? ")" ("=>" expression | block)
```

A lambda evaluates to a closure. Its parameter and result inference follows a
named function. Immutable GC-managed bindings referenced by the body are
captured by value. An owned binding, an `edit` view, or a mutable binding must
not be captured implicitly. `return` inside a block-bodied lambda returns from
that lambda, never from the enclosing function.

The expression body is preferred for one expression. The block body is used
for local bindings or early return. `=>` has lower precedence than every
operator inside its body and is right-associative only as part of nested
lambda syntax.

```kofun
# valid
let double = fn(value: Int) => value * 2
```

```kofun
# invalid: named functions use a name and no arrow
fn double(value: Int) => value * 2
```

**Implementation status:** the active draft grammar contains this surface, and
the Stage 2 structural projection preserves it. No executable frontend
constructs closure types, performs capture analysis, interprets a lambda, or
lowers one.

## #41 — Immutable bindings

### User stories and non-goals

- Local names are stable unless mutation is explicitly requested.
- Initializers run once in obvious left-to-right statement order.
- It is not a goal for binding immutability to silently deep-freeze an opaque
  resource or bypass that resource's API.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| Rust immutable-by-default `let` | local reasoning and optimizer freedom | newcomers must learn `mut` for loops |
| JavaScript `const` | prevents rebinding | referenced objects may still mutate |
| Haskell bindings | referential transparency by default | effectful algorithms require a different model |

### Normative contract

`let name: T = expression` evaluates `expression` once and binds the resulting
value for the remainder of the lexical scope. The annotation may be omitted
when `T` can be inferred. The name is not visible in its own initializer.
Assignment to the binding is a static error.

A binding may shadow a name from an outer scope, but the same scope must not
declare the same name twice. Binding immutability prohibits rebinding; the
operations permitted on the value are determined independently by its type
and ownership mode.

```kofun
# valid
let width: Int = 6
let area = width * 7
```

```kofun
# invalid: immutable binding cannot be reassigned
let answer = 41
answer = 42
```

**Implementation status:** integer immutable declarations execute in Stage 1
and Stage 2 Core. The Core lowerers do not yet diagnose later assignment
through a complete semantic checker, and shadowing/type behavior outside the
integer subset remains open.

## #42 — Owned bindings

### User stories and non-goals

- Files, sockets, locks, and similar resources have deterministic cleanup.
- Ownership transfer is visible at the declaration and transfer sites without
  lifetime punctuation.
- It is not a goal to require deterministic ownership for ordinary
  GC-managed values or to hide reference counting behind `own`.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| Rust ownership and RAII | static use-after-move prevention | lifetimes and borrow syntax have a steep learning curve |
| C++ RAII | deterministic destruction with familiar scopes | copies and moves can be implicit |
| Swift consuming parameters | ownership can be expressed at API boundaries | defaults and optimizer conventions are less explicit locally |

### Normative contract

`let own name: T = expression` requires an owned-resource type and creates one
affine binding. The initializer transfers its result into `name`. On every
normal or exceptional exit from the scope, the implementation must run
`T`'s cleanup exactly once unless ownership has already been transferred with
`take name`.

After `take name`, every path that reads, edits, takes, or implicitly captures
`name` is a static error. `read name` creates a non-escaping read-only view;
`edit name` creates a non-escaping exclusive view. A view must not outlive the
statement-delimited call that receives it in this language version.

`let mut own`, `let own mut`, copying an owned binding, and implicit closure
capture of an owned binding are invalid. Shared ownership requires an explicit
library operation such as `share(value)` and produces a different type.

```kofun
# valid
let own socket = Socket.connect("example.com:443")
send(take socket)
```

```kofun
# invalid: use after ownership transfer
let own socket = Socket.connect("example.com:443")
send(take socket)
print(socket.peer())
```

**Implementation status:** the bootstrap structural projection preserves this
spelling, but the executable Core rejects it. Resource typing, path-sensitive
affine analysis, cleanup insertion, unwinding behavior, and backend tests
remain open.

## #43 — If expressions

### User stories and non-goals

- The same construct handles conditional values and conditional effects.
- Only the selected branch runs, and value-producing branches have one type.
- It is not a goal to allow truthiness, omit braces, or use a separate ternary
  operator.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| Rust value-producing `if` | one construct for values and statements | branch types must unify |
| C statement `if` plus `?:` | familiar imperative control | duplicates conditional syntax and precedence rules |
| Python statement `if` plus conditional expression | readable prose-like forms | two forms must be learned |

### Normative contract

The condition is evaluated exactly once and must have type `Bool`. Only the
selected branch is evaluated. In value position, `else` is mandatory and both
branch block values must unify to one type. In statement position, `else` may
be omitted and the entire expression has type `Void`.

Braces are mandatory. The condition needs no parentheses. The final bare
expression of a branch is its value; `return`, `break`, `continue`, or a
diverging expression contributes the bottom type for branch unification.

```kofun
# valid
let label = if score >= 90 {
    "excellent"
} else {
    "keep going"
}
```

```kofun
# invalid: value position requires `else`
let label = if score >= 90 {
    "excellent"
}
```

**Implementation status:** the public Stage 2 C Core lowers statement-position
`if` with optional `else` and bounded Int-valued `if` with mandatory `else`.
Both forms require braces, accept Bool literals or integer comparisons, support
nesting, evaluate the condition once, and evaluate only the selected branch.
The value slice accepts one final Int expression per branch in `let`, `print`,
assignment, or `return`; `E2S27` rejects a missing `else`, and `E2S28` rejects
a branch outside that bounded Int type. General Bool expressions, multi-
statement value blocks, general branch type unification, `else if`,
interpreter/native differential coverage, and full static typing remain open.
Mutable assignment is supported within the current branch block, while
assignment from a branch to an outer binding is explicitly rejected as
described in #39.

## #44 — Else-if chains

### User stories and non-goals

- Multiway decisions read top to bottom without deep visual nesting.
- Evaluation order and attachment of `else` are unambiguous.
- It is not a goal to add `elif`, fallthrough, or implicit pattern matching.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| C and Go `else if` | composition of existing tokens | without mandatory braces, dangling-else rules matter |
| Python `elif` | compact and visually distinct | adds another keyword |
| ML nested `if ... else if` | expression semantics are direct | long chains can obscure exhaustive alternatives |

### Normative contract

`else if` is parsed as an `else` branch containing another `if` expression.
Conditions are evaluated left to right until the first true condition; later
conditions and branches are not evaluated. A final `else` is required when the
chain appears in value position.

Because every branch uses braces, an `else` attaches to the immediately
preceding unmatched `if`. A newline or comment between `}` and `else` does not
terminate the chain.

```kofun
# valid
if score >= 90 {
    print("excellent")
} else if score >= 70 {
    print("good")
} else {
    print("keep going")
}
```

```kofun
# invalid: `elif` is not a Kofun keyword
if ready {
    start()
} elif waiting {
    retry()
}
```

**Implementation status:** the surface is in the active grammar and preserved
by structural projection, but public Core lowering rejects the first `if`.
Short-circuit evaluation and value-type unification are not executable yet.

## #45 — For loops

### User stories and non-goals

- Collection and range iteration use one readable form.
- The iterable is evaluated once and the loop variable has a clear lexical
  scope.
- It is not a goal to provide C's three-clause loop header, implicit numeric
  coercion, or value-producing loops in this version.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| Python `for name in iterable` | concise and broadly familiar | mutation during iteration is library-dependent |
| Rust `for pattern in IntoIterator` | static protocol and patterns | desugaring and ownership can be complex |
| C three-clause `for` | precise index control | verbose and easy to misuse for collections |

### Normative contract

`for name in iterable { body }` evaluates `iterable` once, obtains its iterator,
and repeatedly requests the next element. `name` is a fresh immutable binding
scoped to `body`. The built-in `List[T]` iterator visits elements from index
zero upward. `start .. end` produces ascending integers from `start` inclusive
to `end` exclusive; if `start >= end`, it is empty.

`continue` advances to the next request. `break` exits the innermost loop.
Neither accepts a value in this language version. A `for` expression has type
`Void`. Iterator invalidation and ownership transfer must be specified by the
iterable type; the loop must not silently clone owned elements.

```kofun
# valid
for index in 0 .. 3 {
    print(index)
}
```

```kofun
# invalid: C-style header is not supported
for (let index = 0; index < 3; index = index + 1) {
    print(index)
}
```

**Implementation status:** the Kofun-written seeds rely on range `for`
internally, and the structural frontend preserves the surface. Public Core
lowering has no iterator protocol or loop lowering and explicitly rejects it.

## #46 — Match expressions

### User stories and non-goals

- Algebraic data and finite alternatives can be handled exhaustively.
- Pattern testing, binding, and optional guards have deterministic order.
- It is not a goal to provide implicit fallthrough, regex patterns, or an
  unchecked switch statement.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| Rust `match` | exhaustive typed patterns and guards | ownership effects of patterns require care |
| ML `case`/`match` | compact algebraic decomposition | syntax differs across the ML family |
| Scala `match` | rich extractors and guards | user-defined extraction can hide cost and effects |

### Normative contract

```text
match-expression := "match" expression "{"
                    match-arm ("," match-arm)* ","? "}"
match-arm        := pattern ("if" expression)? "=>"
                    (expression | block)
```

This draft's patterns are `_`, a new binding name, a literal, or an algebraic
variant constructor whose fields contain patterns. The scrutinee is evaluated
once. Arms are tested in source order. A guard runs only after its pattern
matches and must produce `Bool`; a false guard continues with the next arm.
The selected arm alone is evaluated.

A value-producing match must be exhaustive and all reachable arm values must
unify. Unreachable arms are a static error. Pattern matching borrows the
scrutinee for inspection and does not implicitly take an owned field; a future
move-pattern syntax must be explicit.

```kofun
# valid
let text = match result {
    Ok(value) => value,
    Error(code) if code == 404 => "missing",
    Error(_) => "failed",
}
```

```kofun
# invalid: non-exhaustive Bool match
let text = match flag {
    true => "yes",
}
```

**Implementation status:** Stage 2 classifies `match` as a keyword and lowers a
bounded statement-position Bool slice. Bool literals and integer comparisons
may be matched by `true`, `false`, and `_` block arms. The compiler evaluates
the scrutinee once and checks arms in source order. Optional Bool guards run
only after their pattern matches; false continues to the next arm, and no
later guard runs after selection. Guarded arms do not provide static coverage,
so an unguarded fallback remains mandatory. The compiler accepts complete
unguarded `true`/`false` coverage without a catch-all and statically rejects
missing, duplicate, unreachable, or non-Bool-guard cases. The exact finite-set
algorithm and diagnostics are specified in
`spec/bool-match-exhaustiveness.md`.

Value-producing match, arm type unification, bindings, ADTs, payload and nested
patterns, or-patterns, and ownership-aware destructuring remain open.

## #47 — While loops

### User stories and non-goals

- State-driven algorithms have a direct loop whose continuation test is
  explicit.
- The condition is checked before every iteration and must be Boolean.
- It is not a goal to support truthiness, `while ... else`, or value-producing
  breaks in this version.

### Prior designs and tradeoffs

| Design | Useful property | Failure mode avoided or accepted |
| --- | --- | --- |
| C `while` | simple pre-test operational model | integer/pointer truthiness hides type errors |
| Rust `while` | Boolean-only condition and expression-oriented blocks | the loop itself is not normally a value |
| Python `while ... else` | expresses normal completion separately | `else` meaning is frequently misunderstood |

### Normative contract

`while condition { body }` evaluates `condition` before each possible
iteration. It must produce `Bool`. If false, execution continues after the
loop; if true, `body` runs and control returns to the condition.

`continue` transfers directly to the next condition evaluation. `break` exits
the innermost loop. Neither accepts a value. A while expression has type
`Void`. A condition that is initially false executes the body zero times.
Braces are mandatory and there is no `else` clause.

```kofun
# valid
let mut remaining = 3
while remaining > 0 {
    print(remaining)
    remaining = remaining - 1
}
```

```kofun
# invalid: conditions are not truthy
while remaining {
    remaining = remaining - 1
}
```

**Implementation status:** `while` is in the active grammar and is used by the
Kofun-written bootstrap source. Public Core lowering rejects it, and no
executable checker currently enforces Boolean conditions, assignment in the
body, `break`/`continue`, or zero-iteration behavior.

## Conformance requirements

A lifecycle checkbox should be considered satisfied only by evidence matching
its validation line. This document directly supports requirements, prior-art,
normative-semantics, surface-syntax, and reference-documentation review for all
13 issues. The executable fixture supplies partial parser/lowering evidence for
the implementation statuses above.

This document alone does not satisfy AST/HIR completeness, name resolution,
typing, ownership analysis, stable diagnostic snapshots, interpreter
semantics, typed IR, native parity, macro behavior, LSP integration, security
sign-off, fuzzing, performance budgets, migration tooling, or final release
acceptance.
