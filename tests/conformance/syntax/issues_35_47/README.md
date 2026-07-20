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
- Stage 2 executes `let mut` declarations, rebinding, and statement-position
  `if` for Core `Int` values;
- Stage 2 rejects immutable and undeclared assignment targets with exact,
  span-carrying `E2S22` diagnostics;
- Stage 2 structural IR preserves names, arities, spans, and balanced bodies;
- the current frontends reject Unicode names; and
- Stage 2 C lowering explicitly rejects lambda, owned-binding, `else if`,
  `for`, `match`, and `while` fixtures rather than treating them as supported.

Structural round-trip is not semantic support. The unsupported fixtures are
future conformance inputs retained as negative capability checks until their
features are implemented.

The current assignment slice is block-local. A mutable assignment followed by
an `if` in the same function executes, and a binding declared inside an `if`
branch can be changed inside that branch. Assigning to an outer binding from
inside a branch is rejected with `E2S22` and a correction hint until lexical
scope resolution replaces the bounded declaration scan.
