# Visibility syntax conformance

This corpus is the executable Stage 2 frontend slice for issue #578 and the
normative visibility contract in `spec/modules/visibility.md`.

The positive cases prove that an omitted modifier is recorded as implicit
private, while `private`, `internal`, and `pub` are recorded as explicit
visibility with exact modifier and declaration spans. Function records also
carry the deterministic single-input `file:0` identity and declaration-order
`symbol:N` identity used by this bounded frontend. The mixed case proves that
the modifier has no effect on same-file calls, forward references, or runtime
behavior. The contextual-identifier case proves that modifier spellings remain
ordinary identifiers outside a declaration prefix.

The negative cases reserve `E2S33` for malformed, duplicate, conflicting, or
misplaced basic modifiers and `E2S34` for deferred or foreign visibility
forms. Rejection happens during structural parsing, before requested C, IR, or
token artifacts are written.

This slice performs no cross-file, module, package, import, re-export,
signature-leak, tooling, FFI, or linker visibility enforcement. Those checks
remain in #582–#585.

Run:

```sh
sh tests/conformance/modules/visibility-syntax/run.sh
```
