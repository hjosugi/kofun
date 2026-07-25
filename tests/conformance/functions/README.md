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

The C11 backend and the direct x86-64 static ELF backend both execute every
case. The direct AArch64 static ELF backend also executes every case under
`qemu-aarch64`; when the emulator is absent that adapter reports an explicit
`UNSUPPORTED` skip instead of failing. Unsupported parameter or result types
remain explicit compiler errors.
