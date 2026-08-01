# List conformance corpus

This corpus keeps the Python-free direct x86-64 and AArch64 backends honest
about their shared `List[Int]` execution contract. The generated static ELFs
exercise local List bindings, positive and negative indexing, length, and
canonical `map`, `filter`, and `fold` calls with typed `fn` lambdas. The
pipeline case composes all three operations, so a constant-result or
fixture-specific encoder cannot pass the corpus. Empty inputs, an all-false
filter, and a predicate over negative integers cover loop boundaries and
signed comparisons.

The out-of-range case fixes the runtime exit status and diagnostic. Four
compile-time rejection cases hold the aggregate Core's documented `10..99`
print boundary when a known zero, single digit, value above 99, or negative
value is read through a local List index.
`bootstrap/native/check.sh` separately compares the successful observations
with an independent C11 executable reference and forces allocation failure in
a chained multi-allocation path. That C11 program is the normative
Python-free differential reference for this checkpoint; the gate does not
claim parity with the removed historical Python interpreter. With
`qemu-aarch64`, the AArch64 adapter covers 17/17: 13 execute and four are
refused before execution. Without an emulator the native gate still
cross-builds deterministic AArch64 success images, audits their ELF machine
type, and requires both native targets to refuse all four known out-of-bound
print values without leaving an artifact.
