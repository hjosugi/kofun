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
  `if`, and exhaustive statement-position Bool `match`;
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

The current assignment slice is block-local. A mutable assignment followed by
an `if` in the same function executes, and a binding declared inside an `if` or
`match` branch can be changed inside that branch. Assigning to an outer binding
from inside a branch is rejected with `E2S22` and a correction hint until
lexical scope resolution replaces the bounded declaration scan.

Bool match is deliberately finite and statement-only. The positive fixture
executes both explicit constructor coverage and `_`, proves unselected arms are
not evaluated, and nests one match in another. Exact negative fixtures cover a
non-Bool scrutinee, each missing Bool constructor, duplicate `true`, an arm
after `_`, an unreachable `_` after complete coverage, and the explicit
unsupported-guard boundary.
