/*
 * kofun-wasm-host-abi-v1 — the pinned contract, expressed once as data.
 *
 * Everything downstream reads these constants: the golden vectors, the guest
 * fixtures, the static validator, and the host itself. A contract stated twice
 * is a contract that can disagree with itself, so the import allowlist, the
 * required exports, and the version pins exist here and nowhere else.
 *
 * Byte quantities that belong to the boundary layout are deliberately absent.
 * They are not restated here; they are recomputed from
 * `spec/aggregate-layout-v1/targets/wasm32.json` by `hostabi.mjs`.
 */

/* The one ABI version string. It is pinned in two independent places in every
 * conforming module — the import module namespace and an exported immutable
 * global — because each one catches a mismatch the other cannot: a module that
 * imports nothing still declares the global, and a module compiled against a
 * later revision still names its own namespace. */
export const ABI_VERSION = "kofun-wasm-host-abi-v1";
export const ABI_REVISION = 1;
export const IMPORT_MODULE = "kofun:host-abi-v1";

export const SCHEMA_VECTORS = "kofun.wasm-host-abi/v1";
export const SCHEMA_CASE = "kofun.wasm-host-abi-case/v1";

/* The complete import allowlist. A conforming module imports a subset of this
 * table and nothing else: an import outside it cannot be satisfied, and an
 * import inside it must carry exactly the signature written here. */
export const IMPORTS = [
  {
    field: "abort",
    params: ["i32", "i32"],
    results: [],
    summary: "checked failure: (code, detail); the host traps and never returns to the guest",
  },
  {
    field: "text_out",
    params: ["i32"],
    results: [],
    summary: "borrow one Text object by reference for the duration of the call",
  },
  {
    field: "list_int_out",
    params: ["i32"],
    results: [],
    summary: "borrow one List[Int] object by reference for the duration of the call",
  },
  {
    field: "list_text_out",
    params: ["i32"],
    results: [],
    summary: "borrow one List[Text] object by reference for the duration of the call",
  },
];

/* The complete required export set. Every entry is mandatory: the host refuses
 * a module that is missing one rather than discovering it at the first call. */
export const EXPORTS = [
  {
    name: "memory",
    kind: "memory",
    minimum_pages: 1,
    summary: "the single linear memory every boundary reference addresses",
  },
  {
    name: "kofun_abi_version",
    kind: "global",
    type: "i32",
    mutable: false,
    value: ABI_REVISION,
    summary: "the ABI revision, read from the module bytes before instantiation",
  },
  {
    name: "kofun_start",
    kind: "func",
    params: ["i32"],
    results: [],
    summary: "the only entry point; the parameter is a host-owned List[Text] reference",
  },
  {
    name: "kofun_alloc",
    kind: "func",
    params: ["i32", "i32"],
    results: ["i32"],
    summary: "(size, align) -> reference, or 0; the returned region is guest-owned",
  },
];

/* Abort codes are part of the contract: a host that renders them must not
 * invent its own numbering. */
export const ABORT_CODES = [
  { name: "bounds", code: 1, summary: "an index outside 0 .. length-1 of a List" },
  { name: "allocation", code: 2, summary: "kofun_alloc could not satisfy a request" },
];

/* Every diagnostic this contract can emit, named once. `check.sh` asserts that
 * each instantiation fixture reports exactly its own name, so two failures can
 * never be reported as one. */
export const DIAGNOSTICS = [
  "abi-version-mismatch",
  "missing-import",
  "import-signature-mismatch",
  "missing-export",
  "start-section-forbidden",
  "retained-guest-pointer",
  "boundary-out-of-range",
  "text-not-utf8",
  "vector-drift",
];

export function signatureText(params, results) {
  const parts = ["func"];
  if (params.length > 0) parts.push(`(param ${params.join(" ")})`);
  if (results.length > 0) parts.push(`(result ${results.join(" ")})`);
  return `(${parts.join(" ")})`;
}

export function importByField(field) {
  return IMPORTS.find((entry) => entry.field === field);
}

export function exportByName(name) {
  return EXPORTS.find((entry) => entry.name === name);
}

export function sameSignature(left, right) {
  return (
    left.params.length === right.params.length &&
    left.results.length === right.results.length &&
    left.params.every((type, index) => type === right.params[index]) &&
    left.results.every((type, index) => type === right.results[index])
  );
}
