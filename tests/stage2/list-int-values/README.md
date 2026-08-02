# Stage 2 `List[Int]` values

This focused gate owns issue #919's local-value slice. It proves that both an
explicit `List[Int]` annotation and an inferred integer-list literal reach the
scope HIR, lower to a fixed-capacity C11 value, execute `len`, and perform
positive, negative, and dynamic checked index reads.

The capacity is 64 elements. A larger literal, a non-`Int` element or index,
an out-of-range literal index, a mutable list, and any local list annotation
other than exactly `List[Int]` fail compilation with `E2S148` and no C
artifact. A dynamic out-of-range index exits 1 with `R021` before printing.
`List[Int]` parameters and results remain intentionally outside this slice;
the argument fixture pins that boundary as `E2S15`.

The executable observation is compared with the independent native Core C11
reference, and two different absolute source directories must produce
byte-identical generated C, typed HIR, and token tapes.

Run:

```sh
sh tests/stage2/list-int-values/run.sh
```
