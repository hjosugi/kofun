import { readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join, relative, resolve } from "node:path";
import { spawnSync } from "node:child_process";

const packageRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const repositoryRoot = resolve(packageRoot, "..", "..");
const roots = [
  join(repositoryRoot, "examples"),
  join(repositoryRoot, "tests", "kofun"),
];

const sources = roots.flatMap(walk).sort();
if (sources.length === 0) {
  throw new Error("repository corpus did not contain any .kofun files");
}

const result = spawnSync(
  join(packageRoot, "node_modules", ".bin", "tree-sitter"),
  ["parse", "--quiet", "--grammar-path", packageRoot, ...sources],
  {
    cwd: packageRoot,
    encoding: "utf8",
  },
);

if (result.status !== 0) {
  process.stderr.write(result.stdout);
  process.stderr.write(result.stderr);
  process.exit(result.status ?? 1);
}

console.log(
  `tree-sitter-kofun: ${sources.length} repository sources parsed without errors`,
);

function walk(directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) {
      return walk(path);
    }
    if (entry.isFile() && entry.name.endsWith(".kofun")) {
      return [relative(packageRoot, path)];
    }
    return [];
  });
}
