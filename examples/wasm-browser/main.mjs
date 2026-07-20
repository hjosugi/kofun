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

export function createKofunImports(appendLine) {
  return {
    kofun: {
      print_i64(value) {
        appendLine(value.toString());
      },
      panic(code) {
        throw new KofunRuntimeError(code);
      },
    },
  };
}

export async function instantiateKofun(
  wasmUrl,
  {
    appendLine = () => {},
    fetchImpl = globalThis.fetch,
    webAssembly = globalThis.WebAssembly,
  } = {},
) {
  if (typeof fetchImpl !== "function") {
    throw new Error("Kofun wasm32 host requires fetch");
  }
  if (webAssembly === undefined) {
    throw new Error("Kofun wasm32 host requires WebAssembly");
  }

  const response = await fetchImpl(wasmUrl);
  if (!response.ok) {
    throw new Error(
      `cannot fetch Kofun WebAssembly ${wasmUrl}: HTTP ${response.status}`,
    );
  }

  const bytes = await response.arrayBuffer();
  if (!webAssembly.validate(bytes)) {
    throw new Error(`invalid Kofun WebAssembly module: ${wasmUrl}`);
  }

  const { instance } = await webAssembly.instantiate(
    bytes,
    createKofunImports(appendLine),
  );
  if (typeof instance.exports.main !== "function") {
    throw new Error("Kofun WebAssembly module does not export main()");
  }
  return instance;
}

export async function renderKofunProgram(
  root,
  dependencies = {},
) {
  const wasmUrl = root.dataset.kofunWasm;
  const status = root.querySelector("[data-kofun-status]");
  const output = root.querySelector("[data-kofun-output]");
  if (!wasmUrl || status === null || output === null) {
    throw new Error("invalid data-kofun-wasm host element");
  }

  root.dataset.kofunState = "loading";
  root.setAttribute("aria-busy", "true");
  status.textContent = "Loading Kofun WebAssembly…";
  output.textContent = "";

  try {
    const instance = await instantiateKofun(wasmUrl, {
      ...dependencies,
      appendLine(line) {
        output.textContent += `${line}\n`;
      },
    });
    instance.exports.main();
    status.textContent = "Executed by the browser WebAssembly runtime.";
    root.dataset.kofunState = "complete";
  } catch (error) {
    status.textContent =
      error instanceof Error ? error.message : "Kofun WebAssembly failed";
    root.dataset.kofunState = "error";
    throw error;
  } finally {
    root.setAttribute("aria-busy", "false");
  }
}

export function mountKofunPrograms({
  documentObject = globalThis.document,
  Observer = globalThis.IntersectionObserver,
  ...dependencies
} = {}) {
  if (documentObject === undefined) {
    return { programs: [], load() {}, loadFor() {} };
  }

  const programs = Array.from(
    documentObject.querySelectorAll("[data-kofun-wasm]"),
  );
  const loads = new Map();
  const load = (root) => {
    if (!loads.has(root)) {
      loads.set(root, renderKofunProgram(root, dependencies));
    }
    return loads.get(root);
  };

  if (typeof Observer === "function") {
    const observer = new Observer((entries) => {
      for (const entry of entries) {
        if (entry.isIntersecting) {
          observer.unobserve(entry.target);
          void load(entry.target);
        }
      }
    });
    for (const root of programs) observer.observe(root);
  } else {
    for (const root of programs) void load(root);
  }

  return {
    programs,
    load,
    loadFor(root) {
      return loads.get(root);
    },
  };
}

if (globalThis.document !== undefined) {
  const mount = () => {
    mountKofunPrograms();
  };
  if (globalThis.document.readyState === "loading") {
    globalThis.document.addEventListener("DOMContentLoaded", mount, {
      once: true,
    });
  } else {
    mount();
  }
}
