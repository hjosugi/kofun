# Optional(Int) lazy coalescing (#314)

Gate: `task optional-coalescing`.

This corpus owns the bounded executable form `Optional(Int) ?? Int -> Int` in
the Stage 2 C11 backend. It proves that the left is evaluated exactly once and
that the fallback is evaluated only when the left is absent. Marker prints and
checked overflow make both properties observable rather than inferred.

`behavior.kofun` covers let, print, return, and function-argument position;
present negative and zero payloads; an absent value; a bare contextual `null`;
arithmetic in the fallback; and an unselected fallback that would fail if it
ran. `left_checked_error.kofun` and `selected_fallback_error.kofun` pin checked
failure propagation from each selected side.

`parenthesized_left.kofun` proves that balanced primary-expression parentheses
are transparent around each exact accepted left shape: an `Int?` binding, a
call returning `Int?`, and contextual `null`. `comparison_precedence.kofun`
pins `(optional ?? Int) == Int`: fallback type analysis ends before the
comparison, while the complete condition still types as Bool.

The six compile-time refusals pin the slice boundary: the left must be exactly
`Int?`; a plain non-Int fallback and an `Optional(Int)` fallback are distinct
rejections; `Optional(Text)` is unsupported; chaining is not accepted; and a
missing fallback is malformed. Each refusal
uses registered diagnostic E2S147 and leaves no C/backend artifact.

Stage 2's CLI intentionally writes the successfully parsed IR and token
checkpoints before C lowering. A lowering refusal therefore retains `.ir` and
`.tokens`; the dedicated gate asserts both are nonempty while the `.c` output
is absent. Here, “no artifact” means no runnable/backend artifact, not no
diagnostic checkpoint files. The shared backend adapter removes those
checkpoints before returning to the differential harness, whose artifact scan
likewise concerns `program`/`program.*` runnable backend outputs.

`sh tests/conformance/run.sh tests/conformance/optional-coalescing` runs the
same observation headers through the shared backend-differential harness. The
capability matrix advertises only `c11-stage2` as supported; every other
registered backend reports an explicit unsupported reason before compiling.

The implementation uses one function-local `KofunOptionalInt` carrier per
operator source byte. A comma expression assigns the left once, and C11's
conditional operator tests its explicit AggregateLayout v1 tag before choosing
the payload or fallback. There is no unwrap or extraction surface. Optional
matching, `?` propagation, safe navigation, generic optional payloads, and
chaining remain outside this slice.
