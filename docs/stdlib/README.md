# Standard library specifications

The library-wide policy is
[`../STANDARD_LIBRARY_CHARTER.md`](../STANDARD_LIBRARY_CHARTER.md).

Accepted focused contracts:

- [`http-client.md`](http-client.md): URL, HTTP/1.1, transport, redirect,
  cancellation, TLS, pooling, and bounded-body policy;
- [`date-time.md`](date-time.md): duration, clocks, POSIX-like instants, civil
  calendar, RFC 3339, and versioned IANA time-zone data;
- [`benchmark.md`](benchmark.md): benchmark definition, calibration, raw sample
  schema, summaries, anti-elision, and reproducible comparison boundaries.

An accepted specification is `specified`, not `implemented`. Executable status
comes only from a current gate referenced by `stdlib/capabilities.tsv`.
