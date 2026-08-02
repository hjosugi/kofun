# Call arguments v1

Status: accepted design contract for issue #625. Parser, HIR, checker, KIF,
formatter, and backend support are follow-up implementation work. This document
does not claim that the current compiler accepts the surface below.

The words MUST, MUST NOT, SHOULD, SHOULD NOT, and MAY are normative.

## Decision

Kofun will support declaration-site external labels and exactly one trailing
lambda spelling.

```kofun
fn replace(in text: Text, from old: Text, to replacement: Text) -> Text

replace(in: source, from: "a", to: "b")
items.fold(initial: 0) fn(acc, item) => acc + item
```

An external label is distinct from the internal parameter name. A parameter
without an external label is positional. A parameter with an external label is
labelled and the label is mandatory at every ordinary call site. This differs
from Gleam's optional call-site labels: mandatory labels preserve the reversal
protection that motivated the feature. It also differs from Kotlin's use of the
internal parameter name and its overload filtering by names.

The trailing form reuses Kofun's canonical `fn` lambda syntax. There is no
brace lambda, receiver lambda, implicit `it`, or alternate anonymous-function
form in the trailing position.

### Amendment: ordinary lambda spellings (#943)

The accepted contract originally said there was no second anonymous-function
form. That was too broad: the Stage 2 conformance gate already and deliberately
accepts three spellings in ordinary expression position:

```kofun
fn(value: Int) => value * 2
(left, right) => left + right
value => value * 3
```

Issue #943 chooses to retain and document all three. Removing the two shorthand
forms would break a checked-in capability without a language-level reason, and
encoding the future trailing-call restriction in the general lambda grammar
would put call attachment in the wrong layer; issue #880 owns that parser
context. Call-arguments v1 remains narrower: only the canonical `fn(...)`
spelling may follow a closed ordinary call. No new lambda spelling is
introduced by this amendment.

Primary comparisons:

- Gleam labelled arguments separate the external label from the internal name,
  require positional arguments before labelled arguments, and have no runtime
  dictionary cost: <https://tour.gleam.run/functions/labelled-arguments/>.
- Kotlin permits named arguments and one lambda outside the parentheses when
  the last parameter is functional: <https://kotlinlang.org/docs/functions.html>
  and <https://kotlinlang.org/docs/lambdas.html#passing-trailing-lambdas>.
- Kofun keeps the useful declaration label and single trailing position while
  rejecting optional labels, default arguments, argument-driven overload
  filtering, and alternate lambda syntax in v1.

## Grammar

```text
parameter           = [ ownership-mode ], [ external-label ], internal-name,
                      ":", type
external-label      = identifier
ordinary-call       = callee, "(", [ argument-list ], ")"
argument-list       = positional-argument, { ",", positional-argument },
                      [ ",", labelled-argument,
                        { ",", labelled-argument } ]
                    | labelled-argument, { ",", labelled-argument }
positional-argument = expression
labelled-argument   = identifier, ":", expression
trailing-call       = ordinary-call, lambda-expression
lambda-expression   = "fn", "(", [ lambda-parameters ], ")",
                      ( "=>", expression | block )
```

The parser MUST treat a newline or comment between `)` and `fn` as trivia when
the resolved call still needs its final functional parameter. Otherwise `fn`
begins the next expression or declaration according to the ordinary grammar.
The grammar never inserts a trailing lambda before overload resolution: the
callee's single resolved signature must establish that the final parameter is
functional.

Canonical formatting keeps all ordinary arguments inside parentheses, closes
the parenthesis, writes one space, and then writes the trailing `fn` expression.
Expression lambdas stay on the same line when they fit. Block lambdas use the
existing block formatter and are not rewritten into expression lambdas.

```kofun
items.fold(initial: 0) fn(acc, item) => acc + item

items.visit(order: depth_first) fn(item) {
    audit(item)
    consume(item)
}
```

Parentheses MUST remain even when the lambda is the only argument:

```kofun
transaction() fn(tx) => commit(tx)
```

`transaction fn(...)` is not valid v1 syntax.

## Binding and diagnostics

Declaration order defines parameter and ABI order. At a call:

1. zero or more positional arguments bind the leading unlabelled parameters;
2. labelled arguments follow all positional arguments and may appear in any
   source order;
3. each declared external label occurs exactly once;
4. internal names are not accepted as labels unless they are also explicitly
   declared external labels;
5. unknown, duplicate, missing, positional-after-labelled, and label-on-an-
   unlabelled-parameter cases are errors;
6. a trailing lambda binds only the final functional parameter and is an error
   if that parameter was already supplied.

Stable diagnostic categories are required; final numeric codes belong to the
frontend implementation child. A diagnostic MUST point to the call-site label
or argument and the corresponding declaration when one exists.

Labels MUST NOT participate in overload selection. Candidate discovery and
overload selection use the same callable identity and type rules as an ordinary
positional call. Only after one signature is selected does label binding
validate that call. Two declarations that differ only by external labels are a
duplicate API, not overloads.

Default arguments are rejected in v1. This prevents omitted arguments from
changing evaluation or API behavior and keeps the pipeline and trailing rules
independent of default selection.

## Evaluation, ownership, effects, and lowering

Every explicit expression evaluates exactly once, from left to right in source
order. A pipeline subject evaluates first. The trailing lambda value evaluates
after all expressions inside the parentheses. These values are then placed in
declaration-order parameter slots before the call.

For example:

```kofun
replace(to: effect_c(), in: effect_a(), from: effect_b())
```

evaluates `effect_c`, `effect_a`, and `effect_b` in that source order, then
passes their temporaries in `in`, `from`, `to` ABI order. Reordering labels does
not reorder effects.

`source |> replace(from: old, to: new)` evaluates `source` first and binds it to
the first parameter. That parameter MAY have an external label; the synthetic
pipeline binding satisfies it without spelling `in:`. No other label may bind
the first parameter again.

Ownership modes precede the external label in a declaration:

```kofun
fn write(take into file: File, bytes data: Bytes) -> Result[Unit, IoError]
```

`take` is the parameter's ownership mode, `into` is its external label, and
`file` is its internal name. Binding labels never weakens `read`, `edit`, or
`take`. The ownership and effect check is performed on the already-bound
declaration-order arguments, while diagnostics retain source-order spans.

Lowering MUST use ordinary temporaries and fixed parameter slots. It MUST NOT
allocate a dictionary, construct a label table at runtime, pass labels through
the ABI, evaluate an expression twice, or dispatch by string. The KIF/public
interface identity includes each external label or an explicit `unlabelled`
marker in declaration order. Renaming an external public label is an API digest
change; renaming only an internal parameter is not.

## Ambiguity boundary

- `call() fn(x) => x` is one trailing call only when the resolved final formal
  is functional.
- `call()` followed by a newline and a top-level `fn named(...)` is two
  declarations because `fn` is followed by an identifier, not `(`.
- A comment or newline between `)` and `fn(` does not terminate a valid trailing
  call.
- `value |> fold(initial: 0) fn(...)` attaches the lambda to `fold`, then applies
  the pipeline rewrite.
- Nested trailing calls associate with the nearest preceding unresolved call:
  `outer(inner() fn(x) => x) fn(y) => y`.
- A second trailing lambda is always rejected.

The executable decision model in `spec/syntax/call-arguments/` checks these
boundaries, binding failures, evaluation order, fixed ABI slots, public
fingerprints, and the no-runtime-label lowering shape.

## Usability corpus conclusion

Labels are required only when the declaration author identifies semantic risk.
They materially distinguish same-typed arguments such as `from`/`to`, surface a
pipeline subject, and name Boolean or policy values. Short mathematical calls
and ordinary unary functions remain positional. The corpus rejects examples in
which labels merely repeat obvious one-argument names.

The selected rule therefore improves the calls that are hard to review without
taxing every call or admitting optional style drift.

## Implementation slices

The decision deliberately separates follow-up work:

1. #880: parser plus canonical formatter surface and ambiguity corpus;
2. #881: HIR/type checking, binding diagnostics, callable identity, and KIF
   digest;
3. #882: pipeline/trailing lowering plus C11/direct-native differential
   evidence.

Those children must retain this document's unsupported-current-compiler
boundary until their own executable gates land.
