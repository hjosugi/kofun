import { fileURLToPath } from "node:url";
import { dirname, join, resolve } from "node:path";
import { spawnSync } from "node:child_process";

const packageRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const cli = join(packageRoot, "node_modules", ".bin", "tree-sitter");
const fixture = join("test", "fixtures", "queries.kofun");
const queries = ["highlights.scm", "locals.scm", "indents.scm", "folds.scm"];

for (const query of queries) {
  run([
    "query",
    "--quiet",
    "--grammar-path",
    packageRoot,
    join("queries", query),
    fixture,
  ]);
}

run([
  "highlight",
  "--check",
  "--quiet",
  "--grammar-path",
  packageRoot,
  fixture,
]);

console.log(`tree-sitter-kofun: ${queries.length} queries compiled and matched`);

function run(args) {
  const result = spawnSync(cli, args, {
    cwd: packageRoot,
    encoding: "utf8",
  });
  if (result.status !== 0) {
    process.stderr.write(result.stdout);
    process.stderr.write(result.stderr);
    process.exit(result.status ?? 1);
  }
}
