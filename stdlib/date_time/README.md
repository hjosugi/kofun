# Bounded date/time producer

This directory is the executable Stage 2 Core producer owned by issue #846.
It proves the portable value and failure shapes required by
[`docs/stdlib/date-time.md`](../../docs/stdlib/date-time.md) without reading the
host clock, calling a host calendar library, or substituting a host parser.

`date_time.kofun` contains seven nominal value shapes, a closed
`DateTimeOutcome`, preflight-checked addition, exact conversion, duration
normalization, Gregorian leap-day validation, and a recursive Text parser for
the canonical RFC 3339 UTC form:

```text
YYYY-MM-DDTHH:MM:SS[.1-9 digits]Z
```

The parser reports a zero-based byte position and accumulates fractional
seconds as nanoseconds. This is deliberately a bounded producer, not the full
public date/time library: non-UTC offsets, formatting, clock adapters, and time
zones remain owned by the wider contract issues.

Run the focused gate with:

```sh
sh stdlib/date_time/tests/verify.sh
```

The gate compiles the Kofun source through scope HIR and the Stage 2 C11
backend, compiles the generated C with warnings as errors, executes the exact
stdout/stderr goldens, and names every structural observation it checks.
