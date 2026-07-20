# Deterministic testing API checkpoint

[`testing.kofun`](testing.kofun) defines a small, pure testing surface. Each
assertion has a case name and returns `TestResult`; it does not abort, print,
read a clock, use randomness, or mutate global state. A runner can therefore
retain all failures in input order and choose its own output format.

The initial public operations are:

- `expect_true` and `expect_false`;
- equality checks specialized for `Int`, `Bool`, and `Text`;
- `test_passed` / `test_failed` predicates for `filter`;
- `test_summary_add` for `fold`, plus `test_summarize` as the direct helper;
- `test_exit_code`, which returns 0 only when no assertion failed.

Failure values retain the case name and both operands. For the three mismatch
constructors, fields are `(name, expected, actual)`. Empty and duplicate case
names are valid data: this pure layer does not invent an undocumented sentinel
or silently discard a result. Naming policy belongs to a future test discovery
and reporting layer.

Integer and boolean checks and summary updates are O(1). Text equality is O(n)
in the compared UTF-8 content, and summarizing `n` results is O(n). Results and
summaries are ordinary GC-managed values; this checkpoint owns no affine
resource and retains only the values present in the returned result list.

The API uses explicit type specializations because Kofun does not yet have the
trait constraint needed for an honest generic equality assertion. Approximate
numeric comparison, fixtures, mocks, snapshots, property generation, fuzzing,
parallel scheduling, discovery, and console formatting are non-goals for this
checkpoint. Those features should build on `TestResult` rather than changing
its deterministic pass/fail semantics.

## Current compiler boundary

The current executable Stage 2 frontend stops at the first top-level type
declaration; it cannot yet lower the API's records or algebraic data types. The
executable fixture is consequently a named Int-Core projection of the same
pass/fail and summary rules. The gate runs that projection through both the C11
seed and direct x86-64 backend and requires byte-identical output; it does not
claim that `testing.kofun` is currently importable by an executable program.

Run the Python-free gate with:

```sh
sh stdlib/testing/tests/verify.sh
```
