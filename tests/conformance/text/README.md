# Text conformance corpus

This corpus exercises the Python-free direct x86-64 backend. It observes UTF-8
Text concatenation, equality, extended grapheme-cluster length, `chars`,
positive and negative grapheme indexing, explicit `bytes` and `codepoints`
views, and the exact out-of-range failure contract.

The script corpus includes Arabic, Hebrew, Hindi, Thai, Japanese, precomposed
Hangul, conjoining Hangul Jamo, emoji ZWJ sequences, emoji skin-tone modifiers,
and regional-indicator flags. Concatenation is re-segmented across its join, so
`"e" + "́"` has one grapheme rather than two.

The native backend adapter executes every case as a generated static ELF.
`bootstrap/native/check.sh` additionally compares the same operations with an
independent C11 reference and forces the mmap allocation-failure path.
