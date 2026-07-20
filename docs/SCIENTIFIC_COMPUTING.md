# Scientific computing

## Objective

The goal for Kofun is to keep the exploration speed of Python/NumPy and Julia
while getting native code, static types, and memory safety for production
deployment.

Target areas:

- numerical simulation
- statistics
- machine learning kernels
- signal processing
- data analysis
- optimization
- computational geometry
- scientific services

## Array model

The long-term core type:

```kofun
Array[T, Rank]
```

Example:

```kofun
let vector: Array[Float, 1]
let matrix: Array[Float, 2]
```

The shape does not have to be fully static.

```kofun
Array[Float, rank = 2]
```

When the compiler knows the shape, the design calls for optimizing bounds
checks, allocation, and vectorization. Dynamic shapes stay ordinary, handled
cases.

## Surface syntax

The design prefers intuitions close to Python/NumPy.

```kofun
let c = a + b   # elementwise with broadcasting
let d = a * b   # elementwise
let m = a @ b   # matrix multiplication
```

Scalar broadcasting:

```kofun
let normalized = (x - mean(x)) / std(x)
```

Slicing proposal:

```kofun
let row = matrix[3, 0..]
let block = matrix[0..10, 5..15]
```

Where a view can be returned, the design avoids allocating. Mutation through a
view follows the `edit` contract.

## Broadcasting

Broadcasting is planned to check shape rules in both the type system and at
runtime.

- static error when the compile-time shape is known
- checked runtime error for dynamic shapes
- no silent truncation
- errors show both shapes and the failing dimension

## Missing data

`T?` and `null` are used as they are.

```kofun
let temperature: Float? = row.temperature
let safe = temperature ?? default_temperature
```

For array storage, the plan is to allow selecting between a bitmask, a
sentinel-free nullable layout, and an Arrow-compatible layout.

## Linear algebra

Standard science distribution:

```text
science.array
science.linalg
science.fft
science.stats
science.random
science.optimize
science.autodiff
science.units
science.data
```

The BLAS/LAPACK backend should not depend on runtime discovery alone; the plan
is to make it selectable reproducibly from the manifest.

```toml
[science]
blas = "openblas"
threads = 8
```

## Performance strategy

### Unboxed storage

The intended standard layout for `Array[Float, 2]` is a contiguous `Float64`
buffer, not a List of object pointers.

### Kernel fusion

```kofun
let result = values
    |> map(fn(x) => x * x)
    |> filter(fn(x) => x > threshold)
    |> sum()
```

Where possible, the plan is to fuse this into a single loop rather than
building intermediate Lists.

### SIMD

- alignment metadata
- vector width abstraction
- masked tails
- deterministic fallback
- fast-math as opt-in

### Parallelism

```kofun
let result = values.parallel().map(transform).sum()
```

Parallel execution is meant to stay explicitly visible by default. When a
library uses automatic parallelism, it must document the threshold and the
determinism it provides.

### GPU

Proposed API:

```kofun
let device = gpu.default()?
let result = gpu.on(device) {
    matrix_a @ matrix_b
}
```

The compiler is intended to track host/device ownership transfer, async
completion, and buffer lifetimes.

## Automatic differentiation

Treated as a function transformation.

```kofun
let gradient = grad(loss)
let weights2 = weights - learning_rate * gradient(weights)
```

Requirements:

- reverse and forward mode
- custom derivative
- control flow
- mutation restrictions
- effect restrictions
- compiler IR integration
- numerical checking tool

## Units of measure

Planned:

```kofun
let distance = 120.0[m]
let time = 10.0[s]
let speed = distance / time # Float[m / s]
```

Where unit metadata can be made zero-runtime-cost, it is to stay compile-time
only. Dynamic units are handled as a separate type.

## Notebook and REPL

- incremental definitions
- rich display protocol
- table/array summary
- plot MIME output
- interrupt and cancellation
- deterministic environment snapshot
- package lock integration

To reduce hidden state that only works inside a notebook, the design calls for
a cell dependency graph and an exportable script.

## Interoperability

Priority:

1. C ABI
2. BLAS/LAPACK
3. Python array protocol / buffer protocol
4. Arrow C Data Interface
5. Rust bridge
6. GPU APIs
7. Fortran ABI

Conversions are to make copy, view, and ownership transfer explicit.

## Stage 0

Today there is only a small API built on Python lists.

```text
linspace
zeros
ones
mean
dot
vadd
vsub
vmul
vdiv
```

This is a prototype of the syntax and the workflow. It is not a basis for
performance claims.
