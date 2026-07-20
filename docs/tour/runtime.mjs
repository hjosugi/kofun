import { compileKofun } from "./compiler.mjs";

const DIAGNOSTICS = new Map([
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

export class KofunRuntimeError extends Error {
  constructor(code) {
    super(DIAGNOSTICS.get(code) ?? `error[R010]: wasm trap ${code}`);
    this.name = "KofunRuntimeError";
    this.code = code;
  }
}

export async function runKofun(
  source,
  { webAssembly = globalThis.WebAssembly } = {},
) {
  if (webAssembly === undefined) {
    throw new Error("This browser does not provide WebAssembly");
  }
  const moduleBytes = compileKofun(source);
  if (!webAssembly.validate(moduleBytes)) {
    throw new Error("The browser compiler emitted invalid WebAssembly");
  }
  const lines = [];
  const { instance } = await webAssembly.instantiate(moduleBytes, {
    kofun: {
      print_i64(value) {
        lines.push(value.toString());
      },
      panic(code) {
        throw new KofunRuntimeError(code);
      },
    },
  });
  instance.exports.main();
  return { lines, moduleBytes };
}
