# Python-free Stage 1 seed

`compiler.kofun` is the canonical source. `compiler.c` is its checked-in,
auditable bootstrap seed. A host C11 compiler is enough to build and run this
Kofun-written compiler.

```sh
sh bootstrap/stage1/check.sh
```

Stage 1 accepts the documented arithmetic Core:

```text
kofun-stage1 INPUT.kofun OUTPUT.c
```

It does not yet semantically compile its complete own source, so the Stage 2
fixed-point gate remains open.
