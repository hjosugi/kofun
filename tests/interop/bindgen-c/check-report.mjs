#!/usr/bin/env node
// Structural checks over the bindgen-c audit report (#574).
//
//   node check-report.mjs verify REPORT MODULE TRIPLE HEADER_SHA256
//       Verifies the report against the pinned fixture: schema, context,
//       trust marking, layout facts, audit completeness, and internal
//       consistency. Prints one line per failed check and exits 1.
//
//   node check-report.mjs symbols REPORT
//       Prints every bound function's symbol, one per line, for the gate's
//       readelf comparison.
//
// The gate greps for prose too; this file holds the checks that need JSON
// structure rather than a substring: exact sets, cross-references between
// sections, and the no-absolute-path rule over every string in the report.

import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import process from 'node:process';

function readReport(reportPath) {
  let parsed;
  try {
    parsed = JSON.parse(readFileSync(reportPath, 'utf8'));
  } catch (error) {
    process.stderr.write(`check-report: unreadable report: ${error.message}\n`);
    process.exit(1);
  }
  return parsed;
}

const mode = process.argv[2];

if (mode === 'symbols') {
  if (process.argv.length !== 4) {
    process.stderr.write('check-report: usage: symbols REPORT\n');
    process.exit(2);
  }
  const symbolReport = readReport(process.argv[3]);
  for (const fn of symbolReport.layout.functions) {
    process.stdout.write(`${fn.symbol}\n`);
  }
  process.exit(0);
}

if (mode !== 'verify' || process.argv.length !== 7) {
  process.stderr.write(
    'check-report: usage: verify REPORT MODULE TRIPLE HEADER_SHA256 | symbols REPORT\n');
  process.exit(2);
}

const [reportPath, modulePath, triple, headerSha] = process.argv.slice(3);
const report = readReport(reportPath);
const failures = [];
const check = (condition, message) => {
  if (!condition) failures.push(message);
};

// ------------------------------------------------------------ schema/context
check(report.schema === 'kofun.bindgen-c.report/v1',
  `schema is ${String(report.schema)}`);
const context = report.context || {};
check(typeof context.clang_version === 'string' && context.clang_version !== '',
  'context.clang_version is missing');
check(context.target_triple === triple,
  `context.target_triple is ${String(context.target_triple)}, clang says ${triple}`);
check(context.language_standard === 'c11',
  `context.language_standard is ${String(context.language_standard)}`);
check(Array.isArray(context.headers) && context.headers.length === 1,
  'context.headers must list exactly the fixture header');
const header = (context.headers || [])[0] || {};
check(header.path === 'fixture/kbfix.h',
  `recorded header path is ${String(header.path)}`);
check(header.sha256 === headerSha,
  `recorded header sha256 ${String(header.sha256)} differs from sha256sum ${headerSha}`);
check(/^[0-9a-f]{64}$/.test(String(context.context_sha256)),
  'context.context_sha256 is not a sha256');

// ------------------------------------------------------------------- module
const moduleSection = report.module || {};
check(moduleSection.trust === 'raw-trusted-foreign',
  `module.trust is ${String(moduleSection.trust)}`);
check(moduleSection.file === 'kbfix.raw.kofun',
  `module.file is ${String(moduleSection.file)} (the .raw. segment is load-bearing)`);
let moduleBytes = null;
try {
  moduleBytes = readFileSync(modulePath);
} catch (error) {
  failures.push(`module is unreadable: ${error.message}`);
}
if (moduleBytes !== null) {
  const digest = createHash('sha256').update(moduleBytes).digest('hex');
  check(moduleSection.sha256 === digest,
    `module.sha256 ${String(moduleSection.sha256)} does not match the module file ${digest}`);
}

// ------------------------------------------------------------------- layout
const layout = report.layout || {};
const functions = layout.functions || [];
const functionNames = functions.map((fn) => fn.name);
const expectedFunctions = [
  'kbfix_counter_add',
  'kbfix_counter_free',
  'kbfix_counter_new',
  'kbfix_counter_on_change',
  'kbfix_counter_value',
  'kbfix_label_copy',
  'kbfix_library_name',
  'kbfix_name_length',
  'kbfix_stats_scale',
];
check(JSON.stringify(functionNames) === JSON.stringify(expectedFunctions),
  `bound functions are [${functionNames.join(', ')}], expected the pinned nine in sorted order`);
const signatureOf = (name) =>
  (functions.find((fn) => fn.name === name) || {}).kofun_signature;
check(signatureOf('kbfix_counter_new') ===
  'extern "C" fn kbfix_counter_new(initial: CLong) -> CBytes',
  'kbfix_counter_new signature drifted');
check(signatureOf('kbfix_counter_free') ===
  'extern "C" fn kbfix_counter_free(counter: CBytes) -> Unit',
  'kbfix_counter_free signature drifted');
check(signatureOf('kbfix_stats_scale') ===
  'extern "C" fn kbfix_stats_scale(stats: kbfix_stats_t, factor: CLong) -> kbfix_stats_t',
  'kbfix_stats_scale signature drifted');
check(signatureOf('kbfix_label_copy') ===
  'extern "C" fn kbfix_label_copy(counter: CBytes, buffer: CBytes, capacity: CULong, out_length: CBytes) -> CInt',
  'kbfix_label_copy signature drifted');
for (const fn of functions) {
  check(typeof fn.symbol === 'string' && fn.symbol !== '',
    `function ${fn.name} has no symbol`);
}

const records = layout.records || [];
check(records.length === 1 && records[0].name === 'kbfix_stats_t',
  'exactly kbfix_stats_t must be bound as a record');
if (records.length === 1) {
  const record = records[0];
  check(record.size === 16 && record.alignment === 8,
    `kbfix_stats_t is ${record.size}/${record.alignment}, LP64 says 16/8`);
  const offsets = (record.fields || []).map((field) => `${field.name}@${field.offset}`);
  check(JSON.stringify(offsets) === JSON.stringify(['total@0', 'events@8', 'flags@12']),
    `kbfix_stats_t field offsets are ${offsets.join(', ')}`);
}

const enums = layout.enums || [];
check(enums.length === 1 && enums[0].name === 'kbfix_status',
  'exactly enum kbfix_status must be recorded');
if (enums.length === 1) {
  const constants = (enums[0].constants || [])
    .map((constant) => `${constant.name}=${constant.value}`);
  check(JSON.stringify(constants) === JSON.stringify(
    ['KBFIX_OK=0', 'KBFIX_ERR_NULL=-1', 'KBFIX_ERR_RANGE=-2', 'KBFIX_ERR_CAPACITY=3']),
    `enum constants are ${constants.join(', ')}`);
}

const opaque = (layout.opaque_handles || []).map((handle) => handle.name);
check(JSON.stringify(opaque) === JSON.stringify(['kbfix_counter']),
  `opaque handles are [${opaque.join(', ')}]`);
const callbacks = (layout.callbacks || []).map((callback) => callback.name);
check(JSON.stringify(callbacks) === JSON.stringify(['kbfix_on_change']),
  `callback typedefs are [${callbacks.join(', ')}]`);

// -------------------------------------------------------------------- audit
const audit = report.audit || [];
for (const entry of audit) {
  check(typeof entry.name === 'string' && entry.name !== '',
    'an audit entry has no name');
  check(typeof entry.reason === 'string' && entry.reason !== '',
    `audit entry ${entry.name} has an empty reason`);
  check(entry.category === 'skipped' || entry.category === 'review',
    `audit entry ${entry.name} has category ${String(entry.category)}`);
}
const hasRow = (name, kind, category, reasonFragment) =>
  audit.some((entry) => entry.name === name && entry.kind === kind &&
    entry.category === category && entry.reason.includes(reasonFragment));
const expectRow = (name, kind, category, reasonFragment) => {
  check(hasRow(name, kind, category, reasonFragment),
    `audit is missing: ${name} (${kind}, ${category}, reason mentioning "${reasonFragment}")`);
};
expectRow('KBFIX_MAX_LABEL', 'macro', 'skipped', 'macro');
expectRow('KBFIX_CLAMP', 'macro', 'skipped', 'function-like macro');
expectRow('kbfix_log', 'variadic-function', 'skipped', 'variadic');
expectRow('kbfix_word', 'union', 'skipped', 'union');
expectRow('kbfix_flags', 'bitfield', 'skipped', 'bitfield');
expectRow('kbfix_message', 'flexible-array-member', 'skipped', 'flexible array');
expectRow('kbfix_double', 'inline-function', 'skipped', 'inline');
expectRow('kbfix_on_change', 'callback-typedef', 'review', 'lifetime');

// Every bound function that traffics in raw pointers must carry an explicit
// ownership review row; "generated" must never read as "reviewed".
for (const fn of functions) {
  const pointerBearing = fn.kofun_signature.includes('CBytes') ||
    fn.result.kofun === 'CStr';
  if (!pointerBearing) continue;
  check(hasRow(fn.name, 'function', 'review', 'ownership-unreviewed'),
    `pointer-bearing ${fn.name} has no ownership-unreviewed review row`);
}

// A name never sits on both sides: skipped means absent from the module.
const boundNames = new Set([...functionNames, ...records.map((record) => record.name)]);
for (const entry of audit) {
  if (entry.category !== 'skipped') continue;
  check(!boundNames.has(entry.name),
    `${entry.name} is both bound and skipped`);
}

// Audit rows are sorted, which is what makes reruns byte-comparable.
for (let index = 1; index < audit.length; index += 1) {
  const previous = audit[index - 1];
  const current = audit[index];
  const before = `${previous.name} ${previous.kind} ${previous.reason}`;
  const after = `${current.name} ${current.kind} ${current.reason}`;
  check(before <= after, `audit is not sorted at ${current.name}`);
}

// ------------------------------------------------------------------- counts
const counts = report.counts || {};
check(counts.bound_functions === functions.length,
  'counts.bound_functions disagrees with layout.functions');
check(counts.audit_skipped ===
  audit.filter((entry) => entry.category === 'skipped').length,
  'counts.audit_skipped disagrees with the audit array');
check(counts.audit_review ===
  audit.filter((entry) => entry.category === 'review').length,
  'counts.audit_review disagrees with the audit array');

// ------------------------------------------------- no absolute paths at all
const walkStrings = (value, where) => {
  if (typeof value === 'string') {
    check(!value.startsWith('/'),
      `absolute path recorded at ${where}: ${value}`);
  } else if (Array.isArray(value)) {
    value.forEach((element, index) => walkStrings(element, `${where}[${index}]`));
  } else if (value !== null && typeof value === 'object') {
    for (const [key, element] of Object.entries(value)) {
      walkStrings(element, `${where}.${key}`);
    }
  }
};
walkStrings(report, 'report');

if (failures.length > 0) {
  for (const failure of failures) {
    process.stderr.write(`check-report: ${failure}\n`);
  }
  process.exit(1);
}
process.stdout.write(`check-report: ${audit.length} audit rows, ` +
  `${functions.length} bound functions verified\n`);
