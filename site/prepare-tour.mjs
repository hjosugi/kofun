import { cp, rm } from "node:fs/promises";

const source = new URL("../docs/tour/", import.meta.url);
const target = new URL("../public/tour/", import.meta.url);

await rm(target, { force: true, recursive: true });
await cp(source, target, { recursive: true });

console.log("PREPARED: docs/tour -> public/tour");
