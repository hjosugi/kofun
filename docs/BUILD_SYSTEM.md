# Integrated build system

Kofun keeps two intentionally separate build paths behind one CLI command.

## Single-file fast path

```sh
kofun build main.kofun -o main
```

A source argument selects the direct compiler path before any manifest check.
It does not parse `kofun.toml`, start frost-build, inspect an incremental cache,
or contact a daemon. A manifest beside the source does not change this rule.
The C11 bootstrap backend may invoke the configured C compiler; “no external
build tool” means that a standalone file does not require frost-build or
another project build orchestrator.

Foreign linking is a separate, explicit single-file profile:

```sh
kofun build ffi.kofun --backend c --c-abi \
  --link-library /absolute/path/to/libffi_example.so -o ffi
```

This path invokes the host C compiler and system linker. It is never selected
implicitly, accepts library files rather than raw linker flags, and does not
change the static direct-native `--target` path. Its bounded language and ABI
contract are documented in `bootstrap/c_abi/README.md`.

`tests/build_system.sh` installs a failing Frost spy next to a manifest and
proves that the source form never invokes it.

The issue #19 latency threshold measures compiler-internal, non-Python work,
not end-to-end CLI wall time. After a separate warmup, a C11 harness measures
the child CPU usage of eleven Stage 1 compiler processes, including executable
startup and the CPU spent on source/output I/O, and requires their median to
remain below 5 ms. CPU usage prevents unrelated shared-host scheduler waits
from being mislabeled as compiler work. The gate deliberately excludes shell
CLI dispatch and the external C compiler/link step, so the number must not be
reported as total build latency.

## Manifest upgrade

```sh
cd project-with-kofun-toml
kofun build
kofun build app --explain
```

When there is no source argument and `./kofun.toml` exists, Kofun hands the
build to the real frost-build engine. `KOFUN_FROST=/path/to/frost` selects an
explicit engine; otherwise `frost` is resolved from `PATH`.

The manifest uses frost-build's Kofun adapter:

```toml
[workspace]
default_targets = ["app"]

[toolchain]
kofunc = "kofun"

[target.app]
kind = "kofun_binary"
srcs = ["src/main.kofun"]
```

Frost currently names its native manifest `frost.toml`. Kofun preserves
`kofun.toml` as the user-facing source of truth by maintaining a generated
engine workspace under `.kofun/frost-workspace`. Project entries are mirrored
there, the manifest is exposed to Frost under its native name, and Frost owns
the persistent graph, journal, outputs, and content-addressed cache below:

```text
.kofun/frost-workspace/.frost/
├── bin/debug/<target>
└── obj/debug/<target>/kofun.c
```

The generated workspace is ignored by Git. Kofun refuses symlinked `.kofun`
directories and conflicting generated entries instead of deleting or
overwriting paths it does not own.

The executable cross-repository gate uses frost-build main containing adapter
commit `7df61e3` or newer. It verifies:

- an initial two-target build;
- a no-op build that executes no actions;
- one changed source rebuilding only its owning target;
- removal and restoration of both declared artifacts from Frost's action
  cache without reinvoking Kofun.

If the frost-build checkout is not adjacent, set
`KOFUN_FROST_TEST_BIN=/path/to/frost` when running the gate.
