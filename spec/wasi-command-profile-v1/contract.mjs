/*
 * Kofun WASI command capability profile v1, stated once as data.
 *
 * This is an implementation input, not a backend. The shipped compiler does
 * not select PROFILE_TARGET. A future backend may do so only if the module it
 * emits satisfies this closed surface and its host receives an explicit
 * capability manifest.
 */

export const PROFILE_TARGET = "wasm32-wasi-command1";
export const PROFILE_VERSION = 1;
export const IMPORT_MODULE = "wasi_snapshot_preview1";
export const VERSION_EXPORT = "kofun_wasi_command_version";
export const START_EXPORT = "_start";

export const CAPABILITIES = Object.freeze([
  "arguments",
  "environment",
  "stdin",
  "stdout",
  "stderr",
  "monotonic-clock",
  "random",
  "preopen-read",
  "exit",
]);

/* A module imports any subset, but every imported function must occur here
 * with this exact core-Wasm signature and its authority must be granted by
 * the host manifest before instantiation. */
export const IMPORTS = Object.freeze([
  { field: "args_get", params: ["i32", "i32"], results: ["i32"], capability: "arguments" },
  { field: "args_sizes_get", params: ["i32", "i32"], results: ["i32"], capability: "arguments" },
  { field: "environ_get", params: ["i32", "i32"], results: ["i32"], capability: "environment" },
  { field: "environ_sizes_get", params: ["i32", "i32"], results: ["i32"], capability: "environment" },
  { field: "fd_read", params: ["i32", "i32", "i32", "i32"], results: ["i32"], capability: "stdin|preopen-read" },
  { field: "fd_write", params: ["i32", "i32", "i32", "i32"], results: ["i32"], capability: "stdout|stderr" },
  { field: "fd_close", params: ["i32"], results: ["i32"], capability: "preopen-read" },
  { field: "fd_prestat_get", params: ["i32", "i32"], results: ["i32"], capability: "preopen-read" },
  { field: "fd_prestat_dir_name", params: ["i32", "i32", "i32"], results: ["i32"], capability: "preopen-read" },
  {
    field: "path_open",
    params: ["i32", "i32", "i32", "i32", "i32", "i64", "i64", "i32", "i32"],
    results: ["i32"],
    capability: "preopen-read",
  },
  { field: "clock_time_get", params: ["i32", "i64", "i32"], results: ["i32"], capability: "monotonic-clock" },
  { field: "random_get", params: ["i32", "i32"], results: ["i32"], capability: "random" },
  { field: "proc_exit", params: ["i32"], results: [], capability: "exit" },
]);

export const DIAGNOSTICS = Object.freeze([
  "profile-version-mismatch",
  "invalid-module",
  "unknown-import-module",
  "unknown-import",
  "duplicate-import",
  "import-signature-mismatch",
  "non-function-import",
  "missing-export",
  "start-section-forbidden",
  "undeclared-capability",
  "boundary-out-of-range",
  "retained-guest-pointer",
  "vector-drift",
]);

export function importByField(field) {
  return IMPORTS.find((entry) => entry.field === field);
}

export function sameSignature(left, right) {
  return (
    left.params.length === right.params.length &&
    left.results.length === right.results.length &&
    left.params.every((value, index) => value === right.params[index]) &&
    left.results.every((value, index) => value === right.results[index])
  );
}
