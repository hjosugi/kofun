import { spawnSync } from "node:child_process";
import { readdirSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const packageDirectory = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const repositoryRoot = resolve(packageDirectory, "..", "..");
const corpusRoots = [
  join(repositoryRoot, "examples"),
  join(repositoryRoot, "tests", "kofun"),
];

const files = corpusRoots
  .flatMap((root) => collectKofunFiles(root))
  .sort();

if (files.length === 0) {
  throw new Error("repository corpus is empty");
}

const result = spawnSync(
  "tree-sitter",
  ["parse", "--quiet", ...files],
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

if (/\b(?:ERROR|MISSING)\b/.test(result.stdout)) {
  process.stderr.write(result.stdout);
  throw new Error("repository corpus contains parse errors");
}

console.log(`parsed ${files.length} repository Kofun files without errors`);

function collectKofunFiles(directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) {
      return collectKofunFiles(path);
    }
    return entry.isFile() && entry.name.endsWith(".kofun")
      ? [path]
      : [];
  });
}
