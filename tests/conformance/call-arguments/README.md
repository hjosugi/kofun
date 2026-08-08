# Call arguments

This gate owns the accepted declaration/call surface, the checked-HIR fixed
slot binding, and the first executable backend slice.

`source_order_int.kofun` deliberately writes `as_second` before `as_first`.
Each value calls `observe`, so its golden distinguishes source evaluation
order (`2`, then `1`) from declaration/ABI order; the final `12` proves the
ordinary ABI vector is still `(left, right)`. The Stage 2 C11 emitter stores
each expression once in a function-local `int64_t` temporary through the C11
comma operator before making the declaration-order call.

The executable slice is intentionally limited to direct top-level functions
whose parameters and result are all `Int`. Wider carriers, ownership-bearing
values, pipeline subjects, the trailing-lambda form, and direct-native/Wasm
lowering retain E2S158 and remain owned by #882.

`shadowed_callable.kofun` keeps lexical resolution load-bearing: a callable
parameter named like a supported top-level function must not be redirected to
that top-level ABI. The bounded slice refuses it before C emission.

`lifted_lambda_call.kofun` pins a second function-scope boundary. Lifted
lambdas become separate generated C functions, so this slice refuses a
labelled call in their body rather than declaring its temporaries in the
enclosing source function.
