import assert from "node:assert/strict";
import { access, lstat, readFile, readdir, stat } from "node:fs/promises";
import process from "node:process";

const output = new URL("../out/", import.meta.url);
const basePath = process.env.KOFUN_BASE_PATH || "/kofun";
const required = [
  ".nojekyll",
  "404.html",
  "index.html",
  "docs/index.html",
  "docs/issue-progress/index.html",
  "tour/index.html",
  "tour/compiler.mjs",
];

for (const path of required) {
  await assert.doesNotReject(
    access(new URL(path, output)),
    `missing Pages export: ${path}`,
  );
}

async function files(directory) {
  const result = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const url = new URL(
      entry.name + (entry.isDirectory() ? "/" : ""),
      directory,
    );
    const stats = await lstat(url);
    assert.equal(stats.isSymbolicLink(), false, `symlink in export: ${url}`);
    if (entry.isDirectory()) {
      result.push(...(await files(url)));
    } else {
      result.push(url);
    }
  }
  return result;
}

const exportedFiles = await files(output);
const htmlFiles = exportedFiles.filter((url) => url.pathname.endsWith(".html"));
let checkedUrls = 0;

async function assertExportedUrl(value, source) {
  assert.ok(
    value === basePath || value.startsWith(`${basePath}/`),
    `${source.pathname}: URL escapes ${basePath}: ${value}`,
  );

  const pathname = new URL(value, "https://pages.invalid").pathname;
  const relative = pathname.slice(basePath.length).replace(/^\/+/, "");
  const target = new URL(relative, output);
  const metadata = await stat(target);
  if (metadata.isDirectory()) {
    await access(new URL("index.html", target));
  }
}

for (const url of htmlFiles) {
  const html = await readFile(url, "utf8");
  for (const match of html.matchAll(/\b(?:href|src)="(\/[^"]*)"/g)) {
    checkedUrls += 1;
    await assert.doesNotReject(
      assertExportedUrl(match[1], url),
      `${url.pathname}: missing export target ${match[1]}`,
    );
  }
}

assert.ok(checkedUrls > 0, "no root-relative export URLs were checked");
console.log(
  `PASS: ${required.length} Pages artifacts and ${checkedUrls} base-path URLs`,
);
