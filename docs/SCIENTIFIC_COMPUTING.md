# Scientific computing

## Objective

Kofunは、Python/NumPyとJuliaが持つ探索速度を保ちつつ、production deploymentではnative code、static types、memory safetyを得ることを目標にする。

対象:

- numerical simulation
- statistics
- machine learning kernels
- signal processing
- data analysis
- optimization
- computational geometry
- scientific services

## Array model

long-term core type:

```kofun
Array[T, Rank]
```

例:

```kofun
let vector: Array[Float, 1]
let matrix: Array[Float, 2]
```

shapeを完全にstaticにする必要はない。

```kofun
Array[Float, rank = 2]
```

compilerがshapeを知る場合はbounds check、allocation、vectorizationを最適化する。dynamic shapeも普通に扱える。

## Surface syntax

Python/NumPyに近い直感を優先する。

```kofun
let c = a + b   # elementwise with broadcasting
let d = a * b   # elementwise
let m = a @ b   # matrix multiplication
```

scalar broadcasting:

```kofun
let normalized = (x - mean(x)) / std(x)
```

slicing proposal:

```kofun
let row = matrix[3, 0..]
let block = matrix[0..10, 5..15]
```

viewを返せる場合はallocationしない。viewのmutationは`edit` contractに従う。

## Broadcasting

broadcastingはshape ruleを型・runtime両方で検査する。

- compile-time shapeが分かる場合はstatic error
- dynamic shapeではchecked runtime error
- silent truncationを禁止
- errorには両shapeとfailing dimensionを表示

## Missing data

`T?`と`null`をそのまま使う。

```kofun
let temperature: Float? = row.temperature
let safe = temperature ?? default_temperature
```

array storageではbitmask、sentinel-free nullable layout、Arrow-compatible layoutを選択できる。

## Linear algebra

standard science distribution:

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

BLAS/LAPACK backendはruntime discoveryだけに依存せず、manifestでreproducibleに選択できるようにする。

```toml
[science]
blas = "openblas"
threads = 8
```

## Performance strategy

### Unboxed storage

`Array[Float, 2]`はobject pointerのListではなく、contiguous `Float64` bufferを標準layoutにする。

### Kernel fusion

```kofun
let result = values
    |> map(fn(x) => x * x)
    |> filter(fn(x) => x > threshold)
    |> sum()
```

可能な場合、intermediate Listを作らず一つのloopへfusionする。

### SIMD

- alignment metadata
- vector width abstraction
- masked tails
- deterministic fallback
- fast-mathをopt-in

### Parallelism

```kofun
let result = values.parallel().map(transform).sum()
```

parallel executionはexplicitに見える形を基本とする。libraryがautomatic parallelismを使う場合はthresholdとdeterminismをdocumentする。

### GPU

proposed API:

```kofun
let device = gpu.default()?
let result = gpu.on(device) {
    matrix_a @ matrix_b
}
```

compilerはhost/device ownership transfer、async completion、buffer lifetimeを追跡する。

## Automatic differentiation

function transformationとして扱う。

```kofun
let gradient = grad(loss)
let weights2 = weights - learning_rate * gradient(weights)
```

requirements:

- reverse and forward mode
- custom derivative
- control flow
- mutation restrictions
- effect restrictions
- compiler IR integration
- numerical checking tool

## Units of measure

planned:

```kofun
let distance = 120.0[m]
let time = 10.0[s]
let speed = distance / time # Float[m / s]
```

unit metadataをzero-runtime-costにできる場合はcompile-time onlyにする。dynamic unitsもseparate typeで扱う。

## Notebook and REPL

- incremental definitions
- rich display protocol
- table/array summary
- plot MIME output
- interrupt and cancellation
- deterministic environment snapshot
- package lock integration

notebookだけで動くhidden stateを減らすため、cell dependency graphとexportable scriptを提供する。

## Interoperability

priority:

1. C ABI
2. BLAS/LAPACK
3. Python array protocol / buffer protocol
4. Arrow C Data Interface
5. Rust bridge
6. GPU APIs
7. Fortran ABI

conversionはcopy/view/ownership transferを明示する。

## Stage 0

現在はPython Listを使う小さなAPIのみ。

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

これはsyntaxとworkflowのprototypeであり、performance claimの対象ではない。
