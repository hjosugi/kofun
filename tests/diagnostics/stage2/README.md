# Stage 2 diagnostic corpus

Each rejected `.kofun` file declares its compiler mode, stable error code, and
expected source span. The paired `.stderr` file is the exact byte-for-byte
public diagnostic. The runner also requires exit status 1, empty internal
stderr, and no generated C artifact.

Run the corpus:

```sh
sh tests/diagnostics/stage2/run.sh
```

After an intentional wording change, regenerate all golden files in one
command and review the diff:

```sh
sh tests/diagnostics/stage2/bless.sh
```

`codes.txt` is the executable inventory for the active Stage 2 frontend,
lowerer, and ownership-slice diagnostic codes. The runner fails if the
inventory and cases diverge.

The current suite records three existing diagnostics without precise positions
as `expect-span: none`: `E2S04`, `E2S20`, and `E2S21`. They are visible span
debt, not silently counted as precise diagnostics. Runtime `R010`, C ABI,
native-backend, and host-I/O diagnostics require separate suites because their
execution and location models differ.

Visibility syntax uses `E2S33` for malformed/conflicting basic modifiers and
`E2S34` for unsupported aliases or deferred restricted forms. Both diagnostics
report the rejected modifier/form byte range and are emitted before output
artifacts are written.

Lexical resolution uses `E2S35` for unknown, uninitialized, and escaped local
bindings as well as deterministic scope/binding/use budget failures. Where a
declaration exists, the diagnostic reports both the use and declaration byte
positions.
