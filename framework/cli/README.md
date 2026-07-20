# Native CLI framework

This directory contains the executable checkpoint for Kofun's bounded native
CLI application profile. It is a real application compiler, but it is not
general Kofun lowering.

```sh
./bin/kofun build examples/cli_tool.kofun \
  --framework cli -o build/kofun-tool
./build/kofun-tool --help
./build/kofun-tool greet Ada --prefix Welcome
```

`compiler.c` parses the declaration, serializes command metadata, and writes a
Linux x86-64 ELF directly. The final application build does not invoke a C
compiler, assembler, linker, or shell. The executable has two `PT_LOAD`
segments, no `PT_INTERP`, no dynamic section, and no runtime dependency.

The checked runtime prefix is generated from the auditable `runtime.c`,
`start.S`, and `runtime.ld` sources. `template_to_inc.c` locates the config
anchor, removes the section table from the product, and produces
`runtime_template.inc`. `SHA256SUMS` binds those sources to the checked prefix.
The acceptance gate rebuilds the prefix twice, compares it byte for byte, and
places failing `cc`, `as`, and `ld` spies in front of the active application
build.

This bootstrap split is intentional:

- the runtime source and linker script are the reviewable reference;
- the checked prefix makes an ordinary application build toolchain-free;
- declaration metadata, rather than a fixture answer, drives help and runtime
  dispatch;
- the C11 declaration compiler is temporary bootstrap machinery, not a claim
  that the general Kofun compiler can lower this API yet.

Run the non-skipping acceptance gate with:

```sh
make cli-framework
```

See [TUTORIAL.md](TUTORIAL.md), [REFERENCE.md](REFERENCE.md), and
[SECURITY.md](SECURITY.md).
