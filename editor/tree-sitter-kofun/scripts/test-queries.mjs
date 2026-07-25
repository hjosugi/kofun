import { spawnSync } from "node:child_process";
import { readdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const packageDirectory = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const fixture = join(packageDirectory, "test", "fixtures", "queries.kofun");
const queryDirectory = join(packageDirectory, "queries");
const queries = readdirSync(queryDirectory)
  .filter((name) => name.endsWith(".scm"))
  .sort();

if (queries.length !== 4) {
  throw new Error(`expected four query files, found ${queries.length}`);
}

for (const query of queries) {
  const result = spawnSync(
    "tree-sitter",
    ["query", join(queryDirectory, query), fixture],
    {
      cwd: packageDirectory,
      encoding: "utf8",
    },
  );

  if (result.status !== 0) {
    process.stderr.write(result.stdout);
    process.stderr.write(result.stderr);
    process.exit(result.status ?? 1);
  }

  if (!result.stdout.trim()) {
    throw new Error(`${query} compiled but produced no captures`);
  }
}

console.log(`compiled and exercised ${queries.length} query files`);
