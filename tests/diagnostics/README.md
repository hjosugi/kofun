# Stable diagnostic registry

`registry.tsv` is the canonical, bytewise-sorted declaration of active stable
diagnostic identities. Each row records:

1. code, family, and phase;
2. active emitter or adapter;
3. public channel and exact exit status;
4. primary and secondary span policy;
5. output-artifact policy;
6. one executable fixture owner, fixture, and exact or inline golden; and
7. the adapter that reports the observation.

Span policy is `required`, `not-applicable`, or a named `debt(...)`. The three
pre-existing Stage 2 omissions remain
`debt(stage2-no-source-position)` and therefore cannot be counted as precise.
`file:` evidence names a checked-in fixture or golden. `inline:` evidence
means the named runner constructs or asserts it deterministically.

Run the cheap declaration/report consistency gate and its negative self-tests:

```sh
sh tests/diagnostics/check.sh
sh tests/diagnostics/self-test.sh
```

Run every registered executable owner, then validate the observations:

```sh
sh tests/diagnostics/run.sh
```

The dispatcher appends an adapter report only after that adapter succeeds.
Family runners remain responsible for exact public messages, status, channel,
span, and artifact checks appropriate to their execution model.

## Evidence gaps

Rows whose final field is `gap(reason)` remain canonical identities, but are
deliberately excluded from the passing executable-coverage numerator. This is
used only for active allocation, invariant, budget, or transaction paths that
currently lack a deterministic fault-injection fixture. The checker reports
their count instead of silently treating an owner script as executable
evidence. Adding a real fixture means replacing the gap with an adapter report.

## Deterministic bless workflow

Regenerate or verify all registered golden owners with:

```sh
sh tests/diagnostics/bless.sh
```

File-backed owners invoke their bless script. Inline owners rerun their exact
assertions and write nothing. The dispatcher validates registry/report
consistency before and after the operation, uses `LC_ALL=C`, and ends by
requesting review of the resulting diff. A clean second run must produce no
diff.

## Negative contract

`self-test.sh` creates temporary mutations and proves deterministic rejection
of a duplicate code, an unknown observed code, a missing active fixture, a
public routing mismatch, and a forbidden partial artifact. It never edits the
checked-in registry or goldens.
