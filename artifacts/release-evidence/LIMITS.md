# Release limits

Generated from `release/claims.json` by `make release-evidence`. Do not edit.

## Claims with no executable gate

| Claim | State | What is absent |
|---|---|---|
| `formatter-and-repl` | design | Design only. No formatter or REPL is shipped, and no gate claims one. |
| `general-native-lowering` | open | Unified types, unified control flow, and additional target profiles are absent. Only the enumerated native checkpoints are claimed. |
| `general-ownership-checking` | open | There is no general ownership or law pass. Only the narrow borrowed-`List` checkpoint is claimed. |
| `general-parser-type-checker` | open | The active compiler is not a general parser or type checker. Only the enumerated Stage 2 checkpoints are claimed. |

## Boundaries that fail outside a claim

| Claim | Kind | Evidence | Observation |
|---|---|---|---|
| `arithmetic-core` | rejection | `tests/conformance/numeric/reject_slash_operator.kofun` | `/` on `Int` is refused with one diagnostic instead of being lowered. |
| `borrowed-list-ownership` | rejection | `tests/compile-fail/use_after_take.kofun` | Using a moved binding is refused at compile time. |
| `c-abi-profile` | limit | `tests/conformance/capabilities.tsv` | Backends outside the host-C profile are recorded as unsupported with a stated reason. |
| `c11-function-calls` | rejection | `tests/diagnostics/stage2/e2s10_unsupported_statement.kofun` | Statements outside the lowered slice are refused with `E2S10` rather than mis-lowered. |
| `checked-int64-contract` | trap | `tests/conformance/numeric/reject_slash_operator.kofun` | Operations that cannot be represented are refused or trapped rather than wrapping. |
| `cli-commands` | rejection | `tests/cli_stage2_outcomes.sh` | Non-Core sources produce a refusal outcome instead of a partial build. |
| `cli-framework` | limit | `tests/conformance/capabilities.tsv` | Targets outside Linux x86-64 are recorded as unsupported with a stated reason. |
| `compiler-seed` | rejection | `bootstrap/selfhost/driver/corpus_reject_index_int.kofun` | Indexing an Int receiver exits nonzero and writes no C; thirty additional typed-boundary, block-boundary, loop-boundary, Text-boundary, and index-boundary fixtures are checked by the same gate. |
| `deterministic-fuzzing` | rejection | `tests/fuzz/semantic_differential.sh` | A divergence between the oracle and a backend fails the gate with the differing program. |
| `elf64-image-writer` | limit | `tests/conformance/capabilities.tsv` | Corpora outside the lowered profiles are recorded as unsupported with a stated reason. |
| `enum-matching` | rejection | `tests/conformance/adt/generic_unsupported.kofun` | Enum shapes outside the slice are refused rather than silently accepted. |
| `http-framework` | limit | `tests/http/check.sh` | Requests beyond the bounded state machine are refused rather than buffered without limit. |
| `module-aliases` | rejection | `tests/conformance/capabilities.tsv` | Public, per-name and external aliases are outside the profile and are refused. |
| `native-aarch64-function-calls` | skip | `bootstrap/native/check.sh` | Without `qemu-aarch64` the execution branch is reported as skipped, never as passed. |
| `native-constant-stack-returns` | limit | `bootstrap/native/fixtures/function_all_traps_pressure.kofun` | Calls outside return position remain ordinary calls and are bounded by the stack limit. |
| `native-int64-values` | trap | `tests/conformance/numeric/reject_slash_operator.kofun` | Values outside the representable range are refused or trapped, never wrapped. |
| `native-integer-division` | trap | `tests/conformance/functions/division_floor_signs.kofun` | Division by zero and non-representable quotients trap with a canonical per-operator `R010` diagnostic. |
| `native-list-int-core` | limit | `tests/conformance/capabilities.tsv` | Backends that do not lower `List` values are recorded as unsupported with a stated reason. |
| `native-text-returning-calls` | limit | `tests/conformance/capabilities.tsv` | Backends that do not lower `Text` values are recorded as unsupported with a stated reason. |
| `native-utf8-text-core` | limit | `tests/conformance/capabilities.tsv` | Backends that do not lower `Text` values are recorded as unsupported with a stated reason. |
| `native-x86-64-function-calls` | rejection | `tests/conformance/capabilities.tsv` | Corpora outside the Int Core profile are recorded as unsupported with a stated reason. |
| `nominal-records` | rejection | `tests/conformance/records/generic_record.kofun` | Generic records are refused explicitly instead of being accepted outside the bounded profile. |
| `public-re-exports` | limit | `tests/conformance/modules/re-exports/run.sh` | Chains and counts beyond the stated bounds are refused rather than truncated. |
| `reproducible-bootstrap` | limit | `bootstrap/stage1/SHA256SUMS` | A digest mismatch fails the gate instead of accepting the regenerated artifact. |
| `rust-crate-shim` | skip | `examples/rust-shim/check.sh` | Without Cargo the gate reports a skip rather than a pass. |
| `self-recompile` | rejection | `bootstrap/selfhost/driver/corpus_reject.kofun` | A source outside the profile is refused identically by both seeds and writes no C. |
| `selfhost-native-corpus` | limit | `bootstrap/selfhost/driver/corpus_reject.stdout` | The refusing corpus keeps its pinned output; a change in either path fails the gate. |
| `source-extension` | rejection | `Makefile` | A `.kf`, `.py`, `.pyc` or `.pyo` file, or a `pyproject.toml`, fails `repository-check`. |
| `stable-diagnostics` | rejection | `tests/diagnostics/registry.tsv` | A code without an executable owner, or an owner without a registry row, fails the gate. |
| `stage2-core-lowering` | rejection | `tests/diagnostics/stage2/e2s10_unsupported_statement.kofun` | A statement outside the Core is refused with `E2S10`. |
| `stage2-typed-sidecar` | rejection | `tests/typed-sidecar/authority-boundary.sh` | A compiler, KIF or cache consumer of the sidecar is refused by the authority-boundary gate. |
| `stdio-language-server` | limit | `tests/lsp/performance_test.js` | Exceeding a latency percentile or the resident-set growth ratio fails the gate. |
| `syscall-file-round-trip` | trap | `stdlib/tests/verify.sh` | A failing syscall surfaces its errno instead of returning a success value. |
| `syscall-stdlib-api` | limit | `tests/conformance/capabilities.tsv` | Targets outside Linux x86-64 are recorded as unsupported with a stated reason. |
| `test-skip-reporting` | skip | `tests/cli.sh` | A skipped case is reported as skipped and never folded into the pass count. |
| `tree-sitter-grammar` | limit | `editor/tree-sitter-kofun/grammar.js` | Malformed sources produce error nodes through the recovery corpus rather than aborting the parse. |
| `wasm32-arithmetic-core` | rejection | `bootstrap/wasm/fixtures/unsupported_text.kofun` | `Text` values are refused by the wasm32 backend rather than partially lowered. |
