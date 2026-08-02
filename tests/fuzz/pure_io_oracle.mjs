#!/usr/bin/env node

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";

const model = process.argv[2];
const cases = Number.parseInt(process.env.KOFUN_PURE_IO_CASES ?? "32", 10);
if (!model || !Number.isSafeInteger(cases) || cases < 1 || cases > 4096) {
  process.stderr.write("pure/io fuzz: invalid model or case count\n");
  process.exit(2);
}

let state = 0x556e2026;
function random() {
  state ^= state << 13;
  state ^= state >>> 17;
  state ^= state << 5;
  return state >>> 0;
}

function shuffled(values) {
  const result = [...values];
  for (let index = result.length - 1; index > 0; index -= 1) {
    const other = random() % (index + 1);
    [result[index], result[other]] = [result[other], result[index]];
  }
  return result;
}

function oracle(names, direct, calls) {
  const effects = new Map(names.map((name) => [name, direct.has(name)]));
  let changed;
  do {
    changed = false;
    for (const caller of names) {
      if (effects.get(caller)) continue;
      if ([...(calls.get(caller) ?? [])].some((callee) => effects.get(callee))) {
        effects.set(caller, true);
        changed = true;
      }
    }
  } while (changed);
  return [...names].sort().map((name) => {
    if (!effects.get(name)) return `${name}|pure|-`;
    if (direct.has(name)) return `${name}|io|print`;
    const forcing = [...(calls.get(name) ?? [])]
      .filter((callee) => effects.get(callee)).sort()[0];
    if (!forcing) throw new Error(`oracle has no forcing callee for ${name}`);
    return `${name}|io|${forcing}`;
  }).join("\n") + "\n";
}

const work = fs.mkdtempSync(path.join(os.tmpdir(), "kofun-pure-io-fuzz."));
for (let caseIndex = 0; caseIndex < cases; caseIndex += 1) {
  const count = 1 + (random() % 20);
  const names = Array.from({ length: count }, (_, index) =>
    `f${index.toString().padStart(2, "0")}`);
  const direct = new Set(names.filter(() => random() % 7 === 0));
  const calls = new Map(names.map((name) => [name, new Set()]));
  for (const caller of names) {
    for (const callee of names) {
      if (random() % 9 === 0) calls.get(caller).add(callee);
    }
  }
  if (count > 1 && caseIndex % 3 === 0) {
    calls.get(names[0]).add(names[1]);
    calls.get(names[1]).add(names[0]);
  }
  if (caseIndex % 5 === 0) direct.add(names[count - 1]);

  const declarationOrder = shuffled(names);
  const lines = declarationOrder.map((name) =>
    `fn ${name} ${direct.has(name) ? 1 : 0}`);
  for (const caller of shuffled(names)) {
    for (const callee of shuffled(calls.get(caller))) {
      lines.push(`call ${caller} ${callee}`);
    }
  }
  const input = path.join(work, `case-${caseIndex}.txt`);
  fs.writeFileSync(input, lines.join("\n") + "\n");
  const observed = spawnSync(model, [input], { encoding: "utf8" });
  if (observed.status !== 0) {
    process.stderr.write(
      `pure/io fuzz: model failed in case ${caseIndex}: ${observed.stderr}`,
    );
    process.exit(1);
  }
  const expected = oracle(names, direct, calls);
  if (observed.stdout !== expected) {
    process.stderr.write(
      `pure/io fuzz: mismatch in case ${caseIndex}\n` +
      `expected:\n${expected}observed:\n${observed.stdout}`,
    );
    process.exit(1);
  }
}

process.stdout.write(
  `PASS: independent pure/io oracle agrees on ${cases} bounded call graphs\n`,
);
