# Stage 0 semantic contract

Status: normative for the bootstrap implementation.

## Values

Stage 0 has `Int`, `Float`, `Bool`, `Text`, `Null`, `List[T]`, `Tuple[...]`, functions, and an opaque `Resource` value. `T?` is represented by `Null` or a `T` value.

## Bindings

`let` creates an immutable binding. `let mut` creates a mutable binding. Assignment to an immutable binding is a compile-time error. `let own` marks an affine resource binding. After `take`, any later read is a compile-time error when detected by the local analysis and a runtime error otherwise.

## Numeric operators

`/` returns `Float`. `//` performs floor division in the reference interpreter. The current C11 backend accepts only a restricted numeric subset and documents backend-specific gaps rather than silently changing unsupported semantics.

## Control flow

`if` is an expression when both branches produce values. In statement position it produces `Void`. `for` iterates a `List`; `start .. end` is an end-exclusive integer range.

## Functions

Top-level function headers are collected before bodies are checked, enabling recursion and forward calls. Parameters default to value mode. `read`, `edit`, and `take` are ownership modes; Stage 0 enforces a local affine approximation rather than the full planned borrow model.

## Law declarations

`law monad` is compile-time-only. It cannot execute ordinary I/O. The compiler checks left identity, right identity, and associativity over the declared finite model. `bounded-exhaustive` is not a universal proof. `proven-finite` is emitted only for compiler-certified complete finite carriers and complete total-function spaces.

Law evidence may be serialized as `kofun.law-evidence/v1`. The artifact binds results to the source SHA-256 and compiler version. A caller may require a minimum assurance; a declared law below that level is rejected with `L200`.

## Backends

The interpreter defines the broadest Stage 0 behavior. The C11 backend is intentionally strict and rejects unsupported constructs with an error. It includes a narrow `List[Text]` and text/file runtime for the native Stage 1 seed. No backend may silently reinterpret a construct with different semantics.
