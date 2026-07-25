# User-defined function conformance

This corpus is the executable contract for the bounded `Int` function Core.
Every registered backend must compile and run arguments, returned values,
forward references, recursion, mutual recursion, and the six-register argument
boundary with identical stdout, stderr, and exit status.

Three cases exist specifically to pin backend value placement, and any backend
that keeps values in registers has to keep them observationally identical to a
backend that does not:

- `register_pressure` nests further than the direct x86-64 backend has
  allocatable registers, so its deepest operands, its multiply, and its unary
  negation must come from deterministic spill slots;
- `values_across_calls` computes every argument of a six-argument call with a
  call, which leaves earlier arguments live across later ones, and nests calls
  inside call arguments;
- `branch_join` returns from three separate guards plus the fall-through, so
  every return path must agree on what it restores.

Three more pin what a backend may do with a call in a returned position. A
backend is free to lower one as a branch instead of a call, and these cases
make the observable behaviour of that choice identical to a backend that does
not:

- `tail_position_accumulators` threads two accumulators through a returned
  self-call, so `fib_loop(n - 1, b, a + b)` is wrong unless every argument is
  computed before any parameter is overwritten;
- `tail_position_boundary` rotates all six parameters through a returned
  self-call, which is the same requirement at the full argument-register
  boundary, and returns a call whose own argument is a call;
- `tail_position_guards` puts the returned calls inside guards with an ordinary
  fall-through return after them, so a backend that reuses the frame still has
  to leave the non-tail path alone.

The C11 backend and the direct x86-64 static ELF backend both execute every
case. The direct AArch64 static ELF backend also executes every case under
`qemu-aarch64`; when the emulator is absent that adapter reports an explicit
`UNSUPPORTED` skip instead of failing. Unsupported parameter or result types
remain explicit compiler errors.

Constant stack is a stronger claim than identical output, and it is not made
here: it is specific to the direct native backends and is proved by execution
in `bootstrap/native/check.sh`.
