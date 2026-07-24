import { spawn } from "node:child_process";
import { rm, writeFile } from "node:fs/promises";
import process from "node:process";
import { fileURLToPath } from "node:url";

const output = new URL("../out/", import.meta.url);
const nextBin = fileURLToPath(
  new URL("../node_modules/next/dist/bin/next", import.meta.url),
);
const basePath = process.env.KOFUN_BASE_PATH || "/kofun";

await rm(output, { force: true, recursive: true });

const exitCode = await new Promise((resolve, reject) => {
  const child = spawn(process.execPath, [nextBin, "build"], {
    env: {
      ...process.env,
      KOFUN_BASE_PATH: basePath,
      KOFUN_STATIC_EXPORT: "1",
    },
    stdio: "inherit",
  });
  child.once("error", reject);
  child.once("exit", (code, signal) => {
    if (signal) {
      reject(new Error(`Next.js build terminated by ${signal}`));
      return;
    }
    resolve(code);
  });
});

if (exitCode !== 0) {
  process.exitCode = exitCode;
} else {
  await writeFile(new URL(".nojekyll", output), "");
  console.log(`EXPORTED: out/ for ${basePath || "/"}`);
}
