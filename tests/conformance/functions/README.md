# User-defined function conformance

This corpus is the executable contract for the bounded `Int` function Core.
Every registered backend must compile and run arguments, returned values,
forward references, recursion, mutual recursion, and the six-register argument
boundary with identical stdout, stderr, and exit status.

The C11 backend and the direct x86-64 static ELF backend both execute every
case. The direct AArch64 static ELF backend also executes every case under
`qemu-aarch64`; when the emulator is absent that adapter reports an explicit
`UNSUPPORTED` skip instead of failing. Unsupported parameter or result types
remain explicit compiler errors.
