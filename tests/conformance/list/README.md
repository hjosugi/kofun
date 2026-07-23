# List conformance corpus

This corpus keeps the Python-free direct x86-64 and AArch64 backends honest
about their shared `List[Int]` execution contract. The generated static ELFs
exercise local List bindings, positive and negative indexing, length, and
canonical `map`, `filter`, and `fold` calls with typed `fn` lambdas. The
pipeline case composes all three operations, so a constant-result or
fixture-specific encoder cannot pass the corpus. Empty inputs, an all-false
filter, and a predicate over negative integers cover loop boundaries and
signed comparisons.

The out-of-range case fixes the runtime exit status and diagnostic.
`bootstrap/native/check.sh` separately compares the successful observations
with an independent C11 executable reference and forces allocation failure in
a chained multi-allocation path. That C11 program is the normative
Python-free differential reference for this checkpoint; the gate does not
claim parity with the removed historical Python interpreter. The AArch64
adapter runs 13/13 under `qemu-aarch64`; without an emulator the native gate
still cross-builds deterministic AArch64 success and bounds-failure images and
audits their ELF machine type.
