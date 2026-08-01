# Affine resumptions v1

Status: accepted normative design for GitHub issue #735.

Contract identifier: `kofun.affine-resumption/v1`.

This document fixes the contract for a possible future Kofun effect-handler
implementation. The compiler, interpreter, and native backends do **not**
currently implement general handlers. The executable model beside this file
checks this contract only; passing it is not evidence that handlers ship.

The words **must**, **must not**, **should**, and **may** are normative.

## Decision

A captured resumption is an affine value. Its initial state is `available`.
Exactly one of these terminal actions may occur on every executed path:

- `resume(k, value)` consumes `k` and continues the suspended computation;
- `drop_resume(k)` consumes `k` without continuing it and transfers cleanup to
  the handler scope named at capture; or
- unwinding the handler scope performs the same drop action.

At most once, rather than exactly once, is the safety property. Aborting an
operation, cancellation, and failure may deliberately drop the resumption.
Ordinary RC/GC reclamation is not cleanup: the handler scope is the cleanup and
cancellation owner and must run destructors for authority captured by `k`.

V1 exposes no `once`/`multi` surface mode. Every handler that a later proposal
may add is one-shot. A mode field, `multi` keyword, continuation clone, or an
encoding that erases the affine type is rejected. Adding multi-shot behavior
requires a separate research issue with a formal ownership argument; it cannot
amend this identifier in place.

## Flow rule

The checker assigns one budget token to the clause's `Resume[A, B]` binding.
`available` owns the token; `resumed` and `dropped` do not.

- Sequential expressions thread the same state. A second terminal action is
  `EAF001` and reports the first terminal span plus the attempted-use span.
- Alternatives inspect the same incoming state independently. Each reachable
  arm may consume or drop once. The merged value is terminal only when every
  continuing arm is terminal; an arm that aborts has cleanup owned by unwind.
- V1 recognizes alternatives only for one syntactic conditional or a match of
  one immutable scrutinee. Correlated tests expressed by separate conditionals
  are conservatively rejected if their sequential budgets could both consume.
- Loops cannot consume a resumption. A future checker may accept a proved
  zero-or-one loop only under a new, reviewed rule.

Deliberate non-use is spelled `drop_resume(k)`. An implicit drop exists only at
handler-scope unwind; normal fallthrough must be explicit so cleanup review
cannot miss it.

## Transfer rule

The only transfer is a move to a statically known, non-recursive local helper
whose parameter has the exact `Resume[A, B]` type. The helper body is checked
with the caller's token, and the caller loses the binding at the call span.
The helper must finish the token by resume or drop before returning.

The following are always `EAF002` escape errors:

- return or yield of the resumption;
- closure, quote, async task, or deferred-action capture;
- storage in a record, variant, list, cell, global, or heap container;
- transfer through an unknown, generic, interface-dispatched, foreign, or
  dynamically selected parameter;
- recursive or mutually recursive helper transfer; and
- conversion to `Any`, bytes, an integer, a raw pointer, or another type that
  could hide and later reconstruct the token.

These rules intentionally reject safe higher-order programs. The local
decision is preferable to accepting resumption laundering.

## Captured ownership

Capture does not change the mode of an outer value:

- captured `read` remains shared and is released at resume or drop;
- captured `edit` remains exclusive and cannot overlap a sibling edit/take;
- captured `take` is owned by the suspended continuation. Neither the handler
  clause nor another resumption may observe or move it.

A `take` capture therefore requires an explicit cleanup owner in the capture
record. Missing ownership is `EAF008`. Resume transfers the owned value back
into the continuation exactly once. Drop transfers it to the handler-scope
cleanup action exactly once. The adversarial model rejects closure capture,
storage, recursion, and sequential double resume before execution; its forged
runtime case proves that even a checker bypass cannot re-enter the `take`.

## Stable diagnostics

Diagnostics are backend-independent structured observations:

| Code | Meaning | Primary span | Related span |
| --- | --- | --- | --- |
| `EAF001` | use after resume/drop | attempted use | first terminal action |
| `EAF002` | resumption escape or laundering | escape operation | capture |
| `EAF003` | transfer target is not a known affine local | call | capture |
| `EAF004` | recursive transfer | recursive call | capture |
| `EAF005` | loop may consume more than once | consume in loop | capture |
| `EAF006` | fallthrough leaves cleanup implicit | clause end | capture |
| `EAF007` | multi-shot spelling or clone | spelling/clone | capture |
| `EAF008` | captured authority has no cleanup owner | capture | authority move |

Every source span is a half-open byte range in a stable source identity. Text
rendering may add context, but the code, primary span, and related span must be
identical across supported backends.

## Runtime backstop

Every runtime resumption object has an atomic `available`/`consumed` bit. The
first resume or drop changes it to `consumed` before invoking user code or
cleanup. A later resume or drop fails with `EAFR01` and the fixed observation:

```text
stdout: bytes already produced before the invalid attempt
stderr: EAFR01: affine resumption already consumed\n
exit-category: language-runtime-error
cleanup-owner: handler-scope, invoked exactly once
```

The bit is a backstop, not an excuse to weaken static checks. Runtime failure
must not enter the continuation, duplicate a captured `take`, run cleanup
twice, or expose a host assertion, signal, or backend-specific exception.

## Encoding boundary

Typed IR may represent a resumption only as an opaque affine value carrying
its capture span and cleanup owner. Serialization, reflection, equality,
hashing, pointer observation, and FFI export are unavailable. Backend lowering
may add private state, but no mode field is part of the source or interface
contract. Thus a later operation-mode proposal can add a distinct version
without silently turning v1 values into multi-shot values.

## Executable evidence and limits

`sh spec/effects/affine-resumption/check.sh` runs the bounded checker over the
checked-in positive and negative corpus and runs the forged double-resume
backstop through two model backend projections. It limits programs to 64
operations, eight branch levels, eight captures, and eight local transfers.
The model is deliberately not a parser, ownership pass, handler runtime, or
backend implementation.
