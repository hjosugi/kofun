#!/usr/bin/env node

import fs from "node:fs";

function sorted(value) {
  if (Array.isArray(value)) return value.map(sorted);
  if (value !== null && typeof value === "object") {
    return Object.fromEntries(Object.keys(value).sort().map((key) => [key, sorted(value[key])]));
  }
  return value;
}

const [kind, input, output] = process.argv.slice(2);
if (!kind || !input || !output) throw new Error("usage: make-invalid.mjs CASE INPUT OUTPUT");
const document = JSON.parse(fs.readFileSync(input, "utf8"));

switch (kind) {
  case "bom":
    fs.writeFileSync(output, Buffer.concat([
      Buffer.from([0xef, 0xbb, 0xbf]),
      fs.readFileSync(input),
    ]));
    process.exit(0);
  case "invalid-utf8":
    fs.writeFileSync(output, Buffer.from([0xc3, 0x28]));
    process.exit(0);
  case "prototype-key":
    fs.writeFileSync(output, '{"__proto__":{"authoritative":false}}\n');
    process.exit(0);
  case "depth-overflow":
    fs.writeFileSync(output, `${"[".repeat(129)}0${"]".repeat(129)}\n`);
    process.exit(0);
  case "remapped":
    document.file.path_remap_root_id = "0000000000000000000000000000000000000000000000000000000000000000";
    break;
  case "authoritative":
    document.authoritative = true;
    break;
  case "wrong-schema":
    document.schema = "kofun.typed-sidecar/v2";
    break;
  case "invalid-id":
    document.file.file_id = "NOT-A-LOWERCASE-HEX-ID";
    break;
  case "count-overflow":
    document.nodes = Array(65_537).fill(null);
    break;
  case "bad-pair":
    document.completeness = "partial";
    break;
  case "absolute-path":
    document.file.logical_path = "/checkout/src/main.kofun";
    break;
  case "span-overflow":
    document.nodes[0].span.end = document.file.byte_length + 1;
    break;
  case "non-validated-complete":
    document.nodes[0].status = "provisional";
    break;
  case "duplicate-id":
    document.references[0].id = document.nodes[1].id;
    break;
  case "dangling-diagnostic":
    document.nodes[1].diagnostic_ids = ["8888888888888888888888888888888888888888888888888888888888888888"];
    break;
  case "validated-dependency":
    document.nodes[0].depends_on = [document.nodes[1].id];
    break;
  case "hidden-leak":
    document.references[0].status = "unavailable";
    document.references[0].target = {
      disclosure: "hidden",
      identity: {
        kind: "SymbolId",
        value: "8888888888888888888888888888888888888888888888888888888888888888",
      },
    };
    break;
  case "node-order":
    document.nodes.reverse();
    break;
  case "text-overflow":
    document.diagnostics[0].fallback_text = "x".repeat(65_537);
    break;
  case "noncanonical":
    fs.writeFileSync(output, `${JSON.stringify(document)}\n`);
    process.exit(0);
  default:
    throw new Error(`unknown invalid case: ${kind}`);
}

fs.writeFileSync(output, `${JSON.stringify(sorted(document), null, 2)}\n`);
