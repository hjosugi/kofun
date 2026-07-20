import { readFile } from "node:fs/promises";

const diagnostics = new Map([
  [1, "error[R010]: integer overflow in operator `+`"],
  [2, "error[R010]: integer overflow in operator `-`"],
  [3, "error[R010]: integer overflow in operator `*`"],
  [4, "error[R010]: integer overflow in unary operator `-`"],
  [5, "error[R010]: operator `/` failed: division by zero"],
  [6, "error[R010]: integer overflow in operator `/`"],
  [7, "error[R010]: operator `//` failed: division by zero"],
  [8, "error[R010]: integer overflow in operator `//`"],
  [9, "error[R010]: operator `%` failed: division by zero"],
]);

class KofunTrap extends Error {}

if (process.argv.length !== 3) {
  console.error("usage: node run.mjs PROGRAM.wasm");
  process.exit(2);
}

const moduleBytes = await readFile(process.argv[2]);
if (!WebAssembly.validate(moduleBytes)) {
  console.error("kofun wasm32: invalid WebAssembly module");
  process.exit(1);
}

let diagnostic = null;
const imports = {
  kofun: {
    print_i64(value) {
      process.stdout.write(`${value.toString()}\n`);
    },
    panic(code) {
      diagnostic = diagnostics.get(code) ?? `error[R010]: wasm trap ${code}`;
      throw new KofunTrap();
    },
  },
};

try {
  const { instance } = await WebAssembly.instantiate(moduleBytes, imports);
  instance.exports.main();
} catch (error) {
  if (!(error instanceof KofunTrap) || diagnostic === null) throw error;
  process.stderr.write(`${diagnostic}\n`);
  process.exitCode = 1;
}
