import assert from "node:assert/strict";
import { access, readFile } from "node:fs/promises";
import path from "node:path";
import { docs } from "../app/docs/docs-manifest.ts";

const markdownLink = /!?\[[^\]]*]\(([^)\s]+)(?:\s+["'][^"']*["'])?\)/g;

for (const entry of docs) {
  const sourcePath = path.resolve(entry.source);
  await access(sourcePath);
  const source = await readFile(sourcePath, "utf8");
  const prose = source
    .replace(/```[\s\S]*?```/g, "")
    .replace(/`[^`\n]*`/g, "");

  for (const match of prose.matchAll(markdownLink)) {
    const href = match[1];
    if (/^(?:https?:|mailto:|#)/i.test(href)) continue;

    const rawTarget = decodeURIComponent(href.split("#", 1)[0]);
    if (!rawTarget) continue;

    const target = path.resolve(path.dirname(sourcePath), rawTarget);
    await assert.doesNotReject(
      access(target),
      `${entry.source}: broken local link ${href}`,
    );
  }
}

console.log(`PASS: ${docs.length} rendered documents and their local links`);
