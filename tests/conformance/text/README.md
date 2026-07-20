# Text conformance corpus

This corpus exercises the Python-free direct x86-64 backend. It observes UTF-8
Text concatenation, equality, codepoint length, `chars`, positive and negative
codepoint indexing, and the exact out-of-range failure contract.

The native backend adapter executes every case as a generated static ELF.
`bootstrap/native/check.sh` additionally compares the same operations with an
independent C11 reference and forces the mmap allocation-failure path.
