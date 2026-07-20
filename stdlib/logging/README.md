# Deterministic structured logging checkpoint

[`logging.kofun`](logging.kofun) defines a small, pure logging core. Callers
construct a `LogRecord`, filter it with an explicit minimum level, and render
it to `Text`. The API does not read a clock, create a global logger, inspect an
environment variable, select stdout/stderr, or perform I/O.

```kofun
let record = match log_record(
    Info,
    "worker",
    "job finished",
    [
        log_int_field("job.id", 42),
        log_bool_field("cached", false),
    ],
) {
    Ok(value) => value,
    Err(error) => return Err(error),
}

match log_render_at(record, Info) {
    null => (),
    line => print(line),
}
```

The five levels are `Trace`, `Debug`, `Info`, `Warn`, and `Error`, in that
severity order. A minimum includes itself and every more severe level.
`log_render_at` returns `null` when a record is filtered; it never uses an
empty string as a sentinel. Records have explicit `target`, `message`, and
ordered fields. Field values are specialized as `Text`, `Int`, or `Bool`
because Kofun does not yet have an honest generic display trait.

## Validation and deterministic output

Field keys use `[A-Za-z_][A-Za-z0-9_.-]*`. Construction rejects an invalid key
with its zero-based index and rejects a duplicate with the key plus both
indices. Validation visits fields from left to right. For each field, an
invalid key wins before duplicate detection, and the earliest equal prior key
is reported. Empty targets, empty messages, and an empty field list are valid.

Text format version 1 is:

```text
level=INFO target="worker" message="job finished" job.id=42 cached=false
```

Target, message, and `LogText` values are always quoted. Backslash, quote, LF,
CR, and tab are escaped as `\\`, `\"`, `\n`, `\r`, and `\t` in one pass.
Integers use canonical decimal `to_text`; booleans are lowercase. Caller field
order is preserved. Rendering has no trailing newline, so the chosen sink owns
framing.

The format prevents a field from injecting a new log line, but it is not a
terminal sanitizer: other Unicode control scalars remain data. A sink that
writes untrusted values to an interactive terminal must apply an appropriate
terminal-safe encoding. Version 1 is a display contract, not a durable storage
or network serialization format; compatibility for exchanged logs remains
future work.

## Costs, ownership, and non-goals

Level checks and scalar field constructors are O(1). Key scanning is linear in
key size. Duplicate validation is O(n²) field comparisons in this clear
reference version. Rendering performs O(m) scalar inspections for total text
size `m`; immutable `Text` concatenation may make allocation cost quadratic.
Records and fields are ordinary GC-managed immutable values and own no file,
descriptor, thread, or buffer resource.

This checkpoint deliberately leaves timestamps, source locations, spans,
context propagation, async or parallel sinks, file rotation, sampling, rate
limiting, ecosystem adapters, and performance baselines to later lifecycle
work. Applications add timestamps as explicit validated fields when they have
a clock policy. Sink errors belong to the selected I/O API rather than being
hidden by this pure layer.

## Current compiler boundary

The canonical API uses records, algebraic data types, nested lists, matching,
and optional text. The active compiler stops at its top-level constant/type
surface and cannot code-generate the module today. The executable fixtures are
therefore audited Int-Core projections of level ordering, the complete
threshold matrix, key classes, validation precedence, the five escape rules,
and filtered rendering. It runs through both the C11 seed and direct x86-64
backend and requires byte-identical output. This gate does **not** claim that
`logging.kofun` is currently importable by an executable application.

Run the Python-free gate with:

```sh
sh stdlib/logging/tests/verify.sh
```
