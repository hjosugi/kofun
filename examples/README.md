# Examples

[`docs/REPOSITORY_GUIDE.md`](../docs/REPOSITORY_GUIDE.md) says an example is
owned by "the example plus its nearest check". Eleven of these files had no check
of any kind, so nothing but the Tree-sitter corpus gate — which proves only
that a file *parses* — stood between them and silent rot.

This table is that missing binding, and `sh examples/check.sh` (`task
examples`) enforces it. Every `.kofun` file under `examples/` appears here
exactly once and every row names a file that exists, so an example cannot be
added without declaring how it is checked.

## Status vocabulary

| Status | Meaning | What the gate does |
|---|---|---|
| `owned` | another subsystem's check already runs it | asserts the named check exists and still names the example |
| `runs` | compiles and runs on the Stage 2 Core path today | builds it, runs it, and compares stdout to the committed `.expected` file |
| `illustrative` | written in accepted syntax that the Stage 2 Core slice does not lower yet | asserts `bin/kofun check` still refuses it with the stated code |

`illustrative` is a falsifiable claim, not a shrug. The gate fails if such a
file starts checking cleanly, which is the signal to move it to `runs` and give
it an expected output. None of these files uses retired syntax — every
construct in them traces to a current entry in
[`docs/DESIGN_DECISIONS.md`](../docs/DESIGN_DECISIONS.md), and `task
tree-sitter` parses all twenty without an `ERROR` or `MISSING` node.

## Examples

| Example | Status | Evidence |
|---|---|---|
| `api_server.kofun` | owned | `tests/http/check.sh` |
| `broken_list_monad.kofun` | illustrative | `E2S02` |
| `cli_tool.kofun` | owned | `framework/cli/check.sh` |
| `coding_interview.kofun` | illustrative | `E2S15` |
| `fibonacci_native.kofun` | owned | `bootstrap/native/check.sh` |
| `hello.kofun` | runs | `hello.expected` |
| `lambdas.kofun` | owned | `tests/conformance/syntax/issues_35_47/run.sh` |
| `lawful_list_monad.kofun` | illustrative | `E2S02` |
| `native_answer.kofun` | runs | `native_answer.expected` |
| `null_and_else_if.kofun` | illustrative | `E2S35` |
| `ownership.kofun` | illustrative | `E2S35` |
| `pipeline.kofun` | illustrative | `E2S16` |
| `project/src/bench.kofun` | runs | `project/src/bench.expected` |
| `project/src/main.kofun` | owned | `spec/package-roots/check.sh` |
| `proven_optional_bool_monad.kofun` | illustrative | `E2S02` |
| `rust-shim/graphemes.kofun` | owned | `examples/rust-shim/check.sh` |
| `science.kofun` | illustrative | `E2S16` |
| `tui_dashboard.kofun` | owned | `framework/tui/check.sh` |
| `wasm-browser/app.kofun` | owned | `docs/tour/check.sh` |
| `wasm_arithmetic.kofun` | owned | `bootstrap/wasm/check.sh` |

## Why the illustrative files do not run

They are not stale spellings. Each one is blocked on a slice boundary the
Stage 2 Core path states for itself:

| File | Boundary |
|---|---|
| `broken_list_monad.kofun`, `lawful_list_monad.kofun`, `proven_optional_bool_monad.kofun` | `law` declarations (DD-035) are not a Stage 2 Core top-level form |
| `ownership.kofun` | `read`/`take`/`own` parameter modes (DD-005, DD-006) parse, but the general ownership pass is open |
| `coding_interview.kofun` | `List[Int]` parameters are outside the Core parameter set, which `E2S15` states directly in parameter position |
| `null_and_else_if.kofun` | `null` and `T?` are accepted design (DD-002) with no Core lowering |
| `pipeline.kofun` | `|>` is accepted design (DD-011); `map`, `filter`, and `sum` are not Core functions |
| `science.kofun` | `linspace` and the numeric surface it uses are not Core functions |

## Evidence binding

`artifacts/optional-bool-monad.evidence.json` records a `source.sha256` for
`proven_optional_bool_monad.kofun`. It had not matched since 2026-07-30 — the
example was edited and the evidence was not regenerated — and nothing compared
the two, which is how an artifact went on reporting `status: passed`,
`proven-finite`, and 264 of 264 cases for bytes that no longer existed. #875
resolved it by reducing the artifact to what is still true (`unverified`, exact
hash, no result asserted), and `examples/check-law-evidence.sh` holds that
exact shape.

The gate here is the general net beside it: **any** artifact under `artifacts/`
that binds itself by hash to a file under `examples/` must still match, and the
check fails rather than passing quietly if no such artifact exists at all. The
next one to drift is caught without waiting for someone to notice.
