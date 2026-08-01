#!/usr/bin/env node
// ABI probe generator for the bindgen-c gate (#574).
//
// Reads a kofun.bindgen-c.report/v1 audit report and writes two files:
//
//   PROBE.c     a C program that includes the real fixture header and prints
//               the host C compiler's sizeof/alignof/offsetof/enum-constant
//               facts for every type the report claims to know;
//   EXPECTED    the same lines derived from the report's recorded layout.
//
// The gate compiles PROBE.c with clang, runs it, and compares its output to
// EXPECTED byte for byte. A generator that recorded a wrong size, offset,
// alignment, or enum value therefore fails against the C compiler itself,
// not against a copy of its own arithmetic.
//
// The report is treated as untrusted input: every name interpolated into C
// source must match a strict identifier shape first.

import { readFileSync, writeFileSync } from 'node:fs';
import process from 'node:process';

const IDENTIFIER = /^[A-Za-z_][A-Za-z0-9_]*$/;
const TAG_TYPE = /^(struct|enum) [A-Za-z_][A-Za-z0-9_]*$/;

function fail(message) {
  process.stderr.write(`make-abi-probe: error: ${message}\n`);
  process.exit(2);
}

if (process.argv.length !== 6) {
  fail('usage: node make-abi-probe.mjs REPORT.json HEADER_INCLUDE PROBE.c EXPECTED.txt');
}
const [reportPath, headerInclude, probePath, expectedPath] = process.argv.slice(2);
if (headerInclude.includes('"') || headerInclude.includes('\\') || headerInclude.includes('\n')) {
  fail('header include path contains characters that cannot be quoted into C');
}

let reportText;
try {
  reportText = readFileSync(reportPath, 'utf8');
} catch (error) {
  fail(`cannot read report: ${error.message}`);
}
let report;
try {
  report = JSON.parse(reportText);
} catch (error) {
  fail(`report is not parseable JSON: ${error.message}`);
}
if (report.schema !== 'kofun.bindgen-c.report/v1') {
  fail(`unexpected report schema: ${String(report.schema)}`);
}
const layout = report.layout;
if (!layout || !Array.isArray(layout.records) || !Array.isArray(layout.enums)) {
  fail('report carries no layout section');
}

const requireIdentifier = (value, role) => {
  if (typeof value !== 'string' || !IDENTIFIER.test(value)) {
    fail(`${role} is not a plain C identifier: ${String(value)}`);
  }
  return value;
};
const requireTagType = (value, role) => {
  if (typeof value !== 'string' || !TAG_TYPE.test(value)) {
    fail(`${role} is not a struct/enum tag type: ${String(value)}`);
  }
  return value;
};
const requireNumber = (value, role) => {
  if (typeof value !== 'number' || !Number.isSafeInteger(value)) {
    fail(`${role} is not an integer: ${String(value)}`);
  }
  return value;
};

const probe = [];
const expected = [];
probe.push('/* GENERATED ABI probe; compares clang reality with the bindgen report. */');
probe.push(`#include "${headerInclude}"`);
probe.push('#include <stddef.h>');
probe.push('#include <stdio.h>');
probe.push('');
probe.push('int main(void) {');

const emitFormatted = (format, cArguments, expectedLine) => {
  probe.push(`    printf("${format}\\n", ${cArguments});`);
  expected.push(expectedLine);
};

emitFormatted(
  'pointer size %zu alignment %zu',
  'sizeof(void *), _Alignof(void *)',
  `pointer size ${requireNumber(layout.pointer.size, 'pointer size')} ` +
  `alignment ${requireNumber(layout.pointer.alignment, 'pointer alignment')}`);

for (const record of layout.records) {
  const cType = requireTagType(record.c_type, 'record c_type');
  const size = requireNumber(record.size, `${cType} size`);
  const alignment = requireNumber(record.alignment, `${cType} alignment`);
  emitFormatted(
    `record ${cType} size %zu alignment %zu`,
    `sizeof(${cType}), _Alignof(${cType})`,
    `record ${cType} size ${size} alignment ${alignment}`);
  for (const field of record.fields) {
    const fieldName = requireIdentifier(field.name, `${cType} field name`);
    const offset = requireNumber(field.offset, `${cType}.${fieldName} offset`);
    const fieldSize = requireNumber(field.size, `${cType}.${fieldName} size`);
    emitFormatted(
      `record ${cType} field ${fieldName} offset %zu size %zu`,
      `offsetof(${cType}, ${fieldName}), sizeof(((${cType} *)0)->${fieldName})`,
      `record ${cType} field ${fieldName} offset ${offset} size ${fieldSize}`);
  }
}

for (const enumeration of layout.enums) {
  const cType = requireTagType(enumeration.c_type, 'enum c_type');
  const size = requireNumber(enumeration.size, `${cType} size`);
  const alignment = requireNumber(enumeration.alignment, `${cType} alignment`);
  emitFormatted(
    `enum ${cType.slice('enum '.length)} size %zu alignment %zu`,
    `sizeof(${cType}), _Alignof(${cType})`,
    `enum ${cType.slice('enum '.length)} size ${size} alignment ${alignment}`);
  for (const constant of enumeration.constants) {
    const constantName = requireIdentifier(constant.name, `${cType} constant name`);
    const value = requireNumber(constant.value, `${cType}.${constantName} value`);
    emitFormatted(
      `enum ${cType.slice('enum '.length)} constant ${constantName} %ld`,
      `(long)${constantName}`,
      `enum ${cType.slice('enum '.length)} constant ${constantName} ${value}`);
  }
}

probe.push('    return 0;');
probe.push('}');
probe.push('');

writeFileSync(probePath, probe.join('\n'), 'utf8');
writeFileSync(expectedPath, `${expected.join('\n')}\n`, 'utf8');
process.stdout.write(`${expected.length} probed layout facts\n`);
