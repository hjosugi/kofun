# Numeric backend conformance corpus

This directory is the shared numeric corpus for the reference interpreter,
C11 backend, and direct native backend. Programs are Kofun source; no
backend-specific copy of a case is permitted.

`expectations.kofun` is the executable manifest. It registers all required
backends and returns the expected stdout, stderr, and exit status for each
case. A runner executes the manifest with the reference interpreter, then
executes every listed case with every registered backend.

The runner records one of two states for every backend/case pair:

- `ran`: compare stdout, stderr, and exit status byte for byte with the
  manifest;
- `unsupported`: print the backend's explicit compile diagnostic and count the
  case as skipped.

Missing results are failures, never implicit skips. The summary reports
`ran/total` coverage for each backend. Adding a backend therefore requires one
entry in `backend_names()` and a command registration in the runner, not a new
test suite.

The corpus intentionally remains outside the legacy `tests/kofun` golden
runner until the Kofun-native differential runner lands. Running error cases
with the stdout-only legacy runner would turn a required runtime failure into
an unhelpful harness failure.
