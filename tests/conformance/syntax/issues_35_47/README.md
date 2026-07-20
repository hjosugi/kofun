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
- Stage 2 executes the same surface plus `let mut`, assignment, statement
  `if`/`else`, and pre-test `while`;
- Stage 2 structural IR preserves names, arities, spans, and balanced bodies;
- the current frontends reject Unicode names; and
- Stage 2 C lowering explicitly rejects lambda, owned-binding, `for`, and
  `match` fixtures rather than treating them as supported.

Structural round-trip is not semantic support. The unsupported fixtures are
future conformance inputs retained as negative capability checks until their
features are implemented. The positive `if` and `while` fixtures compile to
C11 and execute with exact stdout and status checks.
