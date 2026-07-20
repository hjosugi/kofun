import { createReadStream } from "node:fs";
import { stat } from "node:fs/promises";
import { createServer } from "node:http";
import { extname, resolve, sep } from "node:path";

const root = resolve(process.argv[2] ?? "build/wasm-browser");
const port = Number.parseInt(process.env.PORT ?? "8080", 10);
const contentTypes = new Map([
  [".html", "text/html; charset=utf-8"],
  [".mjs", "text/javascript; charset=utf-8"],
  [".wasm", "application/wasm"],
]);

if (!Number.isInteger(port) || port < 1 || port > 65535) {
  console.error("PORT must be an integer from 1 through 65535");
  process.exit(2);
}

createServer(async (request, response) => {
  try {
    const requestUrl = new URL(request.url ?? "/", "http://localhost");
    const relativePath =
      requestUrl.pathname === "/" ? "index.html" : requestUrl.pathname.slice(1);
    const path = resolve(root, relativePath);
    if (path !== root && !path.startsWith(`${root}${sep}`)) {
      response.writeHead(403).end("forbidden\n");
      return;
    }
    const metadata = await stat(path);
    if (!metadata.isFile()) throw new Error("not a file");
    response.writeHead(200, {
      "Content-Type":
        contentTypes.get(extname(path)) ?? "application/octet-stream",
      "Content-Length": metadata.size,
      "Cache-Control": "no-store",
      "X-Content-Type-Options": "nosniff",
    });
    createReadStream(path).pipe(response);
  } catch {
    response.writeHead(404, {
      "Content-Type": "text/plain; charset=utf-8",
    }).end("not found\n");
  }
}).listen(port, "127.0.0.1", () => {
  console.log(`Kofun wasm32 sample: http://127.0.0.1:${port}/`);
});
