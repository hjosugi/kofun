# Syntax issues 48 through 60

`surface-cases.tsv` inventories valid and invalid source snippets for the
normative designs in `spec/syntax/EXPRESSIONS_AND_LITERALS.md`. They are
parser-facing specification examples, not claims of current Stage 2 support.

`token-spans.kofun` and `token-spans.tokens` are an executable golden pair for
the narrow `kofun-token-tape/v1` prototype. `run.sh` compiles the audited
Stage 2 C11 seed and checks exact output plus span invariants. It uses POSIX
shell, a C11 compiler, `awk`, `cmp`, `cut`, `dd`, `grep`, and standard text
utilities. It does not use Python.

Run:

```sh
sh tests/conformance/syntax/issues_48_60/run.sh
```

The surface corpus uses three tab-separated fields:

1. issue number;
2. expected classification, `valid` or `invalid`;
3. one single-line source example.

The corpus deliberately remains data until the full parser supports each
feature. Moving a case into executable acceptance/rejection tests requires a
real parser diagnostic and must not be simulated by this harness.
