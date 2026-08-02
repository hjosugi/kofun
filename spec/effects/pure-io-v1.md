# Bounded `pure` / `io` inference v1

Stage 2 infers one effect summary for every top-level function in a successful
bounded compilation unit. The lattice is exactly:

```text
pure < io
```

`print` is the only direct `io` root in this profile. A caller is `io` when it
can reach that root through the compiler's resolved top-level function-call
observations; otherwise it is `pure`. The monotone analysis computes the least
fixed point, so self-recursive and mutually recursive components without a
root stay `pure`, while a root makes every reaching caller `io`. Divergence and
panic remain `pure`: this summary does not claim termination or totality.

The result is emitted as the existing typed-sidecar `effect` fact on each
function declaration. Its display is exactly `pure` or `io`. Direct roots use
the public reason `effect-io-root-print`. Transitive facts use
`effect-io-callee` and depend on the lexicographically first immediate resolved
`io` callee's function node, which names the explanation without copying a
possibly private identifier into a free-form reason string. That choice is
made after convergence, making it independent of declaration and traversal
order.

Inference runs only after Stage 2 reports a successful compilation. Unknown
calls therefore keep their existing compiler diagnostic and are never
optimistically classified. Failed or cancelled partial semantic-event streams
do not fabricate effect facts.

This slice has no source effect annotations, effect rows or row variables,
subtyping, polymorphism, handlers, resumptions, capability checking, runtime
change, or optimization promise. The representation can be widened by a later
version, but `pure` and `io` are the complete executable set in v1.
