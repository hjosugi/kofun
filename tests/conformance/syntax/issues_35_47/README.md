# Syntax issues #35–#47 conformance checkpoint

This isolated checkpoint connects
`spec/syntax/FOUNDATIONS_AND_CONTROL.md` to the executable bootstrap boundary.
Run it from anywhere with:

```sh
sh tests/conformance/syntax/issues_35_47/run.sh
```

The runner uses only POSIX shell, the checked-in Stage 1/Stage 2 C seeds, and a
C11 compiler. It proves:

- Stage 1 executes an ASCII `fn main()` with newline-terminated immutable
  bindings;
- Stage 2 executes `let mut` declarations, rebinding, statement-position
  `if`, Int-valued `if`, and exhaustive statement-position Bool `match` with
  ordered guards;
- Stage 2 rejects immutable and undeclared assignment targets with exact,
  span-carrying `E2S22` diagnostics;
- Stage 2 names missing Bool patterns with `E2S25` and rejects duplicate or
  unreachable arms with `E2S26`;
- Stage 2 structural IR preserves names, arities, spans, and balanced bodies;
- the current frontends reject Unicode names; and
- Stage 2 C lowering explicitly rejects lambda, owned-binding, `else if`,
  `for`, and `while` fixtures rather than treating them as supported.

Structural round-trip is not semantic support. The unsupported fixtures are
future conformance inputs retained as negative capability checks until their
features are implemented.

The bounded value-position `if` slice accepts Bool literals or Int comparisons,
requires `else`, and requires one final Int expression in each branch. It
works in `let`, `print`, assignment, and `return`, including nested value
conditions. The fixture proves condition-once/selected-only lowering by keeping
checked division-by-zero in unselected branches, then separately proves that a
selected failing branch still reports the stable runtime error. `E2S27` covers
a missing value `else`; `E2S28` covers a branch that cannot produce the bounded
Int result.

The current assignment slice is block-local. A mutable assignment followed by
an `if` in the same function executes, and a binding declared inside an `if` or
`match` branch can be changed inside that branch. Assigning to an outer binding
from inside a branch is rejected with `E2S22` and a correction hint until
lexical scope resolution replaces the bounded declaration scan.

Bool match is deliberately finite and statement-only. The positive fixture
executes explicit constructor coverage, `_`, nested matches, repeated guarded
patterns, and a guarded catch-all. Guards run once, in source order, only after
their pattern matches; a false guard continues to the next arm. Checked
failures in nonmatching or later guards remain unobserved, while a failure in
the selected candidate propagates. Comparison operands execute left to right
and stop after a checked failure. Guarded arms never contribute static coverage,
so an unguarded fallback remains mandatory. Exact negative fixtures cover a
non-Bool scrutinee, each missing Bool constructor, guard-only coverage,
duplicate/unreachable patterns, and an invalid non-Bool guard (`E2S29`).
