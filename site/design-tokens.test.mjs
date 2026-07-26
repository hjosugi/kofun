import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const css = await readFile(new URL("../app/globals.css", import.meta.url), "utf8");
const fontSizeRules = css
  .split("\n")
  .map((line, index) => ({ line: line.trim(), number: index + 1 }))
  .filter(({ line }) => line.startsWith("font-size:"));
const rawFontSizeRules = fontSizeRules.filter(
  ({ line }) => !line.startsWith("font-size: var(--font-size-"),
);

assert.ok(fontSizeRules.length > 0, "expected typography rules to be present");
assert.deepEqual(
  rawFontSizeRules,
  [],
  `font-size declarations must use the shared type scale:\n${rawFontSizeRules
    .map(({ line, number }) => `  ${number}: ${line}`)
    .join("\n")}`,
);

for (const token of [
  "--font-size-micro",
  "--font-size-caption",
  "--font-size-ui",
  "--font-size-small",
  "--font-size-body",
  "--font-size-lead",
  "--font-size-title",
  "--font-size-subheading",
  "--font-size-hero",
  "--font-size-section",
  "--font-size-doc-hero",
  "--font-size-doc-title",
]) {
  assert.match(css, new RegExp(`${token}:`), `missing design token ${token}`);
}

console.log("design token checks passed");
