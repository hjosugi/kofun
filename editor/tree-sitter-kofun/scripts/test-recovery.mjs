import { spawnSync } from "node:child_process";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const packageDirectory = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const recoveryDirectory = join(packageDirectory, "test", "recovery");

const cases = [
  {
    name: "truncated declaration",
    file: "truncated-declaration.kofun",
    required: [/\(function_declaration\b/, /\(block\b/, /\bMISSING\b/],
  },
  {
    name: "truncated expression",
    file: "truncated-expression.kofun",
    required: [/\(function_declaration\b/, /\(let_statement\b/, /\bMISSING\b/],
  },
  {
    name: "broken delimiter",
    file: "broken-delimiter.kofun",
    required: [/\(MISSING "]"/, /\(string\)/],
  },
];

for (const recoveryCase of cases) {
  const result = spawnSync(
    "tree-sitter",
    [
      "parse",
      "--no-ranges",
      join(recoveryDirectory, recoveryCase.file),
    ],
    {
      cwd: packageDirectory,
      encoding: "utf8",
    },
  );
  const output = `${result.stdout}\n${result.stderr}`;

  for (const pattern of recoveryCase.required) {
    if (!pattern.test(output)) {
      process.stderr.write(output);
      throw new Error(
        `${recoveryCase.name} did not retain expected recovery structure: ${pattern}`,
      );
    }
  }
}

console.log(`verified useful recovery for ${cases.length} truncated inputs`);
