// Browser port of bootstrap/wasm/compiler.c's bounded arithmetic Core.
// Keep the emitted module byte-for-byte compatible with the audited C seed.

const MAX_SOURCE_BYTES = 1024 * 1024;
const MAX_BINDINGS = 128;
const MAX_NODES = 1024;
const MAX_STATEMENTS = 256;
const INT64_MAX = 9223372036854775807n;
const INT64_MIN = -9223372036854775808n;
const INT64_MIN_MAGNITUDE = 9223372036854775808n;

const TOKEN = Object.freeze({
  EOF: "eof",
  IDENTIFIER: "identifier",
  INTEGER: "integer",
  LEFT_PAREN: "(",
  RIGHT_PAREN: ")",
  LEFT_BRACE: "{",
  RIGHT_BRACE: "}",
  COLON: ":",
  EQUAL: "=",
  PLUS: "+",
  MINUS: "-",
  STAR: "*",
  SLASH: "/",
  FLOOR_DIV: "//",
  PERCENT: "%",
  COMMA: ",",
});

const NODE = Object.freeze({
  LITERAL: "literal",
  VARIABLE: "variable",
  NEGATE: "negate",
  ADD: "add",
  SUBTRACT: "subtract",
  MULTIPLY: "multiply",
  DIVIDE: "divide",
  FLOOR_DIVIDE: "floor_divide",
  FLOOR_MODULO: "floor_modulo",
});

const OP = Object.freeze({
  UNREACHABLE: 0x00,
  IF: 0x04,
  END: 0x0b,
  CALL: 0x10,
  LOCAL_GET: 0x20,
  LOCAL_SET: 0x21,
  I32_CONST: 0x41,
  I64_CONST: 0x42,
  I32_EQZ: 0x45,
  I32_NE: 0x47,
  I64_EQZ: 0x50,
  I64_EQ: 0x51,
  I64_NE: 0x52,
  I64_LT_S: 0x53,
  I32_AND: 0x71,
  I32_OR: 0x72,
  I64_ADD: 0x7c,
  I64_SUB: 0x7d,
  I64_MUL: 0x7e,
  I64_DIV_S: 0x7f,
  I64_REM_S: 0x81,
  I64_AND: 0x83,
  I64_XOR: 0x85,
});

const ERROR = Object.freeze({
  ADD_OVERFLOW: 1,
  SUBTRACT_OVERFLOW: 2,
  MULTIPLY_OVERFLOW: 3,
  NEGATE_OVERFLOW: 4,
  DIVIDE_ZERO: 5,
  DIVIDE_OVERFLOW: 6,
  FLOOR_DIVIDE_ZERO: 7,
  FLOOR_DIVIDE_OVERFLOW: 8,
  MODULO_ZERO: 9,
});

export class KofunCompileError extends Error {
  constructor(line, detail) {
    super(`kofun wasm32: line ${line}: ${detail}`);
    this.name = "KofunCompileError";
    this.line = line;
    this.detail = detail;
  }
}

class Parser {
  constructor(source) {
    this.source = source;
    this.cursor = 0;
    this.line = 1;
    this.token = null;
    this.error = null;
    this.nodes = [];
    this.bindings = [];
    this.statements = [];
    this.printCount = 0;
  }

  fail(message) {
    if (this.error === null) {
      this.error = new KofunCompileError(this.token?.line ?? this.line, message);
    }
  }

  nextToken() {
    while (this.cursor < this.source.length) {
      const value = this.source[this.cursor];
      if (value === "#") {
        while (
          this.cursor < this.source.length &&
          this.source[this.cursor] !== "\n"
        ) {
          this.cursor += 1;
        }
        continue;
      }
      if (!/\s/u.test(value)) break;
      if (value === "\n") this.line += 1;
      this.cursor += 1;
    }

    if (this.cursor === this.source.length) {
      this.token = { kind: TOKEN.EOF, text: "", line: this.line };
      return;
    }

    const start = this.cursor;
    const line = this.line;
    const value = this.source[this.cursor++];
    if (/[A-Za-z_]/u.test(value)) {
      while (
        this.cursor < this.source.length &&
        /[A-Za-z0-9_]/u.test(this.source[this.cursor])
      ) {
        this.cursor += 1;
      }
      this.token = {
        kind: TOKEN.IDENTIFIER,
        text: this.source.slice(start, this.cursor),
        line,
      };
      return;
    }
    if (/[0-9]/u.test(value)) {
      let magnitude = BigInt(value);
      while (
        this.cursor < this.source.length &&
        /[0-9]/u.test(this.source[this.cursor])
      ) {
        magnitude = magnitude * 10n + BigInt(this.source[this.cursor++]);
        if (magnitude > INT64_MIN_MAGNITUDE) {
          this.token = {
            kind: TOKEN.INTEGER,
            text: this.source.slice(start, this.cursor),
            magnitude,
            line,
          };
          this.fail("integer literal exceeds Int64");
          return;
        }
      }
      this.token = {
        kind: TOKEN.INTEGER,
        text: this.source.slice(start, this.cursor),
        magnitude,
        line,
      };
      return;
    }

    const single = new Map([
      ["(", TOKEN.LEFT_PAREN],
      [")", TOKEN.RIGHT_PAREN],
      ["{", TOKEN.LEFT_BRACE],
      ["}", TOKEN.RIGHT_BRACE],
      [":", TOKEN.COLON],
      ["=", TOKEN.EQUAL],
      ["+", TOKEN.PLUS],
      ["-", TOKEN.MINUS],
      ["*", TOKEN.STAR],
      ["%", TOKEN.PERCENT],
      [",", TOKEN.COMMA],
    ]);
    if (value === "/") {
      if (this.source[this.cursor] === "/") {
        this.cursor += 1;
        this.token = { kind: TOKEN.FLOOR_DIV, text: "//", line };
      } else {
        this.token = { kind: TOKEN.SLASH, text: "/", line };
      }
      return;
    }
    if (single.has(value)) {
      this.token = { kind: single.get(value), text: value, line };
      return;
    }
    this.token = { kind: TOKEN.EOF, text: value, line };
    this.fail("unsupported token in wasm32 arithmetic Core");
  }

  consume(kind) {
    if (this.token.kind !== kind) return false;
    this.nextToken();
    return true;
  }

  tokenIs(word) {
    return this.token.kind === TOKEN.IDENTIFIER && this.token.text === word;
  }

  consumeWord(word) {
    if (!this.tokenIs(word)) return false;
    this.nextToken();
    return true;
  }

  expect(kind, message) {
    if (!this.consume(kind)) {
      this.fail(message);
      return false;
    }
    return true;
  }

  expectWord(word, message) {
    if (!this.consumeWord(word)) {
      this.fail(message);
      return false;
    }
    return true;
  }

  addNode(kind, left = -1, right = -1, binding = -1, value = 0n) {
    if (this.nodes.length === MAX_NODES) {
      this.fail("too many expressions in wasm32 Core");
      return -1;
    }
    this.nodes.push({ kind, left, right, binding, value });
    return this.nodes.length - 1;
  }

  findBinding(name) {
    for (let index = this.bindings.length - 1; index >= 0; index -= 1) {
      if (this.bindings[index] === name) return index;
    }
    return -1;
  }

  parsePrimary() {
    if (this.consume(TOKEN.LEFT_PAREN)) {
      const expression = this.parseExpression();
      this.expect(TOKEN.RIGHT_PAREN, "expected `)` in wasm32 Core expression");
      return expression;
    }
    if (this.token.kind === TOKEN.INTEGER) {
      const magnitude = this.token.magnitude;
      if (magnitude > INT64_MAX) {
        this.fail("positive integer literal exceeds Int64");
        return -1;
      }
      this.nextToken();
      return this.addNode(NODE.LITERAL, -1, -1, -1, magnitude);
    }
    if (this.token.kind === TOKEN.IDENTIFIER) {
      const binding = this.findBinding(this.token.text);
      if (binding < 0) {
        this.fail("unknown binding in wasm32 Core expression");
        return -1;
      }
      this.nextToken();
      return this.addNode(NODE.VARIABLE, -1, -1, binding);
    }
    this.fail("expected Int expression in wasm32 Core");
    return -1;
  }

  parseUnary() {
    if (this.consume(TOKEN.PLUS)) return this.parseUnary();
    if (this.consume(TOKEN.MINUS)) {
      if (this.token.kind === TOKEN.INTEGER) {
        const magnitude = this.token.magnitude;
        this.nextToken();
        const value = magnitude === INT64_MIN_MAGNITUDE ? INT64_MIN : -magnitude;
        return this.addNode(NODE.LITERAL, -1, -1, -1, value);
      }
      const operand = this.parseUnary();
      return this.addNode(NODE.NEGATE, operand);
    }
    return this.parsePrimary();
  }

  parseTerm() {
    let left = this.parseUnary();
    while (this.error === null) {
      let kind = null;
      if (this.consume(TOKEN.STAR)) kind = NODE.MULTIPLY;
      else if (this.consume(TOKEN.SLASH)) kind = NODE.DIVIDE;
      else if (this.consume(TOKEN.FLOOR_DIV)) kind = NODE.FLOOR_DIVIDE;
      else if (this.consume(TOKEN.PERCENT)) kind = NODE.FLOOR_MODULO;
      else break;
      const right = this.parseUnary();
      left = this.addNode(kind, left, right);
    }
    return left;
  }

  parseExpression() {
    let left = this.parseTerm();
    while (this.error === null) {
      let kind = null;
      if (this.consume(TOKEN.PLUS)) kind = NODE.ADD;
      else if (this.consume(TOKEN.MINUS)) kind = NODE.SUBTRACT;
      else break;
      const right = this.parseTerm();
      left = this.addNode(kind, left, right);
    }
    return left;
  }

  addStatement(kind, expression, binding = -1) {
    if (this.statements.length === MAX_STATEMENTS) {
      this.fail("too many statements in wasm32 Core");
      return;
    }
    this.statements.push({ kind, expression, binding });
  }

  parseBinding() {
    if (this.bindings.length === MAX_BINDINGS) {
      this.fail("too many bindings in wasm32 Core");
      return;
    }
    if (this.token.kind !== TOKEN.IDENTIFIER) {
      this.fail("expected binding name after `let`");
      return;
    }
    const name = this.token.text;
    if (name.length >= 64) {
      this.fail("binding name is too long");
      return;
    }
    if (this.findBinding(name) >= 0) {
      this.fail("duplicate binding in wasm32 Core");
      return;
    }
    this.nextToken();
    if (
      this.consume(TOKEN.COLON) &&
      !this.expectWord(
        "Int",
        "wasm32 arithmetic Core supports only Int bindings",
      )
    ) {
      return;
    }
    if (!this.expect(TOKEN.EQUAL, "expected `=` in wasm32 Core binding")) {
      return;
    }
    const expression = this.parseExpression();
    if (this.error !== null) return;
    const binding = this.bindings.length;
    this.bindings.push(name);
    this.addStatement("bind", expression, binding);
  }

  parsePrint() {
    if (!this.expect(TOKEN.LEFT_PAREN, "expected `(` after print in wasm32 Core")) {
      return;
    }
    const expression = this.parseExpression();
    if (!this.expect(TOKEN.RIGHT_PAREN, "expected `)` after print expression")) {
      return;
    }
    this.addStatement("print", expression);
    this.printCount += 1;
  }

  parseProgram() {
    this.nextToken();
    if (
      !this.expectWord("fn", "wasm32 Core requires `fn main()`") ||
      !this.expectWord("main", "wasm32 Core requires `fn main()`") ||
      !this.expect(TOKEN.LEFT_PAREN, "expected `(` after main") ||
      !this.expect(TOKEN.RIGHT_PAREN, "expected `)` after main") ||
      !this.expect(TOKEN.LEFT_BRACE, "expected `{` before main body")
    ) {
      throw this.error;
    }
    while (
      this.error === null &&
      this.token.kind !== TOKEN.RIGHT_BRACE &&
      this.token.kind !== TOKEN.EOF
    ) {
      if (this.consumeWord("let")) this.parseBinding();
      else if (this.consumeWord("print")) this.parsePrint();
      else {
        this.fail("wasm32 Core supports only `let` and `print` statements");
      }
    }
    this.expect(TOKEN.RIGHT_BRACE, "expected `}` after main body");
    if (this.token.kind !== TOKEN.EOF) {
      this.fail("unexpected source after `fn main`");
    }
    if (this.printCount === 0) {
      this.fail("wasm32 Core main must print at least one Int");
    }
    if (this.error !== null) throw this.error;
    return this;
  }
}

class Buffer {
  constructor(initial = []) {
    this.data = [...initial];
  }

  byte(value) {
    this.data.push(value & 0xff);
  }

  bytes(values) {
    this.data.push(...values);
  }

  uleb(value) {
    let remaining = BigInt(value);
    do {
      let part = Number(remaining & 0x7fn);
      remaining >>= 7n;
      if (remaining !== 0n) part |= 0x80;
      this.byte(part);
    } while (remaining !== 0n);
  }

  sleb(value) {
    let remaining = BigInt(value);
    for (;;) {
      let part = Number(remaining & 0x7fn);
      const sign = (part & 0x40) !== 0;
      const next =
        remaining >= 0n
          ? remaining / 128n
          : -1n - (-1n - remaining) / 128n;
      const done = (next === 0n && !sign) || (next === -1n && sign);
      if (!done) part |= 0x80;
      this.byte(part);
      if (done) return;
      remaining = next;
    }
  }

  string(value) {
    const encoded = new TextEncoder().encode(value);
    this.uleb(encoded.length);
    this.bytes(encoded);
  }

  section(identifier, payload) {
    this.byte(identifier);
    this.uleb(payload.data.length);
    this.bytes(payload.data);
  }

  toUint8Array() {
    return Uint8Array.from(this.data);
  }
}

function nodeLocal(parser, node) {
  return parser.bindings.length + node * 3;
}

function nodeAux(parser, node, offset) {
  return nodeLocal(parser, node) + offset;
}

function instructionIndex(body, opcode, index) {
  body.byte(opcode);
  body.uleb(index);
}

function i64Const(body, value) {
  body.byte(OP.I64_CONST);
  body.sleb(value);
}

function panicWith(body, code) {
  body.byte(OP.I32_CONST);
  body.sleb(code);
  instructionIndex(body, OP.CALL, 1);
  body.byte(OP.UNREACHABLE);
}

function beginIf(body) {
  body.byte(OP.IF);
  body.byte(0x40);
}

function checkDivisionPair(body, left, right, zeroCode, overflowCode) {
  instructionIndex(body, OP.LOCAL_GET, right);
  body.byte(OP.I64_EQZ);
  beginIf(body);
  panicWith(body, zeroCode);
  body.byte(OP.END);

  instructionIndex(body, OP.LOCAL_GET, left);
  i64Const(body, INT64_MIN);
  body.byte(OP.I64_EQ);
  instructionIndex(body, OP.LOCAL_GET, right);
  i64Const(body, -1n);
  body.byte(OP.I64_EQ);
  body.byte(OP.I32_AND);
  beginIf(body);
  panicWith(body, overflowCode);
  body.byte(OP.END);
}

function emitExpression(parser, index, body) {
  const node = parser.nodes[index];
  const target = nodeLocal(parser, index);
  if (node.kind === NODE.LITERAL) {
    i64Const(body, node.value);
    instructionIndex(body, OP.LOCAL_SET, target);
    return;
  }
  if (node.kind === NODE.VARIABLE) {
    instructionIndex(body, OP.LOCAL_GET, node.binding);
    instructionIndex(body, OP.LOCAL_SET, target);
    return;
  }

  emitExpression(parser, node.left, body);
  const left = nodeLocal(parser, node.left);
  if (node.kind === NODE.NEGATE) {
    instructionIndex(body, OP.LOCAL_GET, left);
    i64Const(body, INT64_MIN);
    body.byte(OP.I64_EQ);
    beginIf(body);
    panicWith(body, ERROR.NEGATE_OVERFLOW);
    body.byte(OP.END);
    i64Const(body, 0n);
    instructionIndex(body, OP.LOCAL_GET, left);
    body.byte(OP.I64_SUB);
    instructionIndex(body, OP.LOCAL_SET, target);
    return;
  }

  emitExpression(parser, node.right, body);
  const right = nodeLocal(parser, node.right);
  instructionIndex(body, OP.LOCAL_GET, left);
  instructionIndex(body, OP.LOCAL_GET, right);

  if (node.kind === NODE.ADD) {
    body.byte(OP.I64_ADD);
    instructionIndex(body, OP.LOCAL_SET, target);
    instructionIndex(body, OP.LOCAL_GET, left);
    instructionIndex(body, OP.LOCAL_GET, target);
    body.byte(OP.I64_XOR);
    instructionIndex(body, OP.LOCAL_GET, right);
    instructionIndex(body, OP.LOCAL_GET, target);
    body.byte(OP.I64_XOR);
    body.byte(OP.I64_AND);
    i64Const(body, 0n);
    body.byte(OP.I64_LT_S);
    beginIf(body);
    panicWith(body, ERROR.ADD_OVERFLOW);
    body.byte(OP.END);
    return;
  }
  if (node.kind === NODE.SUBTRACT) {
    body.byte(OP.I64_SUB);
    instructionIndex(body, OP.LOCAL_SET, target);
    instructionIndex(body, OP.LOCAL_GET, left);
    instructionIndex(body, OP.LOCAL_GET, right);
    body.byte(OP.I64_XOR);
    instructionIndex(body, OP.LOCAL_GET, left);
    instructionIndex(body, OP.LOCAL_GET, target);
    body.byte(OP.I64_XOR);
    body.byte(OP.I64_AND);
    i64Const(body, 0n);
    body.byte(OP.I64_LT_S);
    beginIf(body);
    panicWith(body, ERROR.SUBTRACT_OVERFLOW);
    body.byte(OP.END);
    return;
  }
  if (node.kind === NODE.MULTIPLY) {
    body.byte(OP.I64_MUL);
    instructionIndex(body, OP.LOCAL_SET, target);

    instructionIndex(body, OP.LOCAL_GET, left);
    i64Const(body, -1n);
    body.byte(OP.I64_EQ);
    instructionIndex(body, OP.LOCAL_GET, right);
    i64Const(body, INT64_MIN);
    body.byte(OP.I64_EQ);
    body.byte(OP.I32_AND);
    instructionIndex(body, OP.LOCAL_GET, right);
    i64Const(body, -1n);
    body.byte(OP.I64_EQ);
    instructionIndex(body, OP.LOCAL_GET, left);
    i64Const(body, INT64_MIN);
    body.byte(OP.I64_EQ);
    body.byte(OP.I32_AND);
    body.byte(OP.I32_OR);
    beginIf(body);
    panicWith(body, ERROR.MULTIPLY_OVERFLOW);
    body.byte(OP.END);

    instructionIndex(body, OP.LOCAL_GET, right);
    body.byte(OP.I64_EQZ);
    body.byte(OP.I32_EQZ);
    beginIf(body);
    instructionIndex(body, OP.LOCAL_GET, target);
    instructionIndex(body, OP.LOCAL_GET, right);
    body.byte(OP.I64_DIV_S);
    instructionIndex(body, OP.LOCAL_GET, left);
    body.byte(OP.I64_NE);
    beginIf(body);
    panicWith(body, ERROR.MULTIPLY_OVERFLOW);
    body.byte(OP.END);
    body.byte(OP.END);
    return;
  }
  if (node.kind === NODE.DIVIDE) {
    checkDivisionPair(
      body,
      left,
      right,
      ERROR.DIVIDE_ZERO,
      ERROR.DIVIDE_OVERFLOW,
    );
    body.byte(OP.I64_DIV_S);
    instructionIndex(body, OP.LOCAL_SET, target);
    return;
  }
  if (node.kind === NODE.FLOOR_DIVIDE) {
    checkDivisionPair(
      body,
      left,
      right,
      ERROR.FLOOR_DIVIDE_ZERO,
      ERROR.FLOOR_DIVIDE_OVERFLOW,
    );
    body.byte(OP.I64_DIV_S);
    instructionIndex(body, OP.LOCAL_SET, target);
    instructionIndex(body, OP.LOCAL_GET, left);
    instructionIndex(body, OP.LOCAL_GET, right);
    body.byte(OP.I64_REM_S);
    instructionIndex(body, OP.LOCAL_SET, nodeAux(parser, index, 1));

    instructionIndex(body, OP.LOCAL_GET, nodeAux(parser, index, 1));
    i64Const(body, 0n);
    body.byte(OP.I64_NE);
    instructionIndex(body, OP.LOCAL_GET, nodeAux(parser, index, 1));
    i64Const(body, 0n);
    body.byte(OP.I64_LT_S);
    instructionIndex(body, OP.LOCAL_GET, right);
    i64Const(body, 0n);
    body.byte(OP.I64_LT_S);
    body.byte(OP.I32_NE);
    body.byte(OP.I32_AND);
    beginIf(body);
    instructionIndex(body, OP.LOCAL_GET, target);
    i64Const(body, 1n);
    body.byte(OP.I64_SUB);
    instructionIndex(body, OP.LOCAL_SET, target);
    body.byte(OP.END);
    return;
  }
  if (node.kind === NODE.FLOOR_MODULO) {
    instructionIndex(body, OP.LOCAL_GET, right);
    body.byte(OP.I64_EQZ);
    beginIf(body);
    panicWith(body, ERROR.MODULO_ZERO);
    body.byte(OP.END);
    body.byte(OP.I64_REM_S);
    instructionIndex(body, OP.LOCAL_SET, target);

    instructionIndex(body, OP.LOCAL_GET, target);
    i64Const(body, 0n);
    body.byte(OP.I64_NE);
    instructionIndex(body, OP.LOCAL_GET, target);
    i64Const(body, 0n);
    body.byte(OP.I64_LT_S);
    instructionIndex(body, OP.LOCAL_GET, right);
    i64Const(body, 0n);
    body.byte(OP.I64_LT_S);
    body.byte(OP.I32_NE);
    body.byte(OP.I32_AND);
    beginIf(body);
    instructionIndex(body, OP.LOCAL_GET, target);
    instructionIndex(body, OP.LOCAL_GET, right);
    body.byte(OP.I64_ADD);
    instructionIndex(body, OP.LOCAL_SET, target);
    body.byte(OP.END);
  }
}

function emitModule(parser) {
  const module = new Buffer([0x00, 0x61, 0x73, 0x6d, 0x01, 0, 0, 0]);

  const types = new Buffer();
  types.uleb(3);
  types.byte(0x60);
  types.uleb(1);
  types.byte(0x7e);
  types.uleb(0);
  types.byte(0x60);
  types.uleb(1);
  types.byte(0x7f);
  types.uleb(0);
  types.byte(0x60);
  types.uleb(0);
  types.uleb(0);
  module.section(1, types);

  const imports = new Buffer();
  imports.uleb(2);
  imports.string("kofun");
  imports.string("print_i64");
  imports.byte(0x00);
  imports.uleb(0);
  imports.string("kofun");
  imports.string("panic");
  imports.byte(0x00);
  imports.uleb(1);
  module.section(2, imports);

  const functions = new Buffer();
  functions.uleb(1);
  functions.uleb(2);
  module.section(3, functions);

  const exports = new Buffer();
  exports.uleb(1);
  exports.string("main");
  exports.byte(0x00);
  exports.uleb(2);
  module.section(7, exports);

  const body = new Buffer();
  const localCount = parser.bindings.length + parser.nodes.length * 3;
  if (localCount === 0) {
    body.uleb(0);
  } else {
    body.uleb(1);
    body.uleb(localCount);
    body.byte(0x7e);
  }
  for (const statement of parser.statements) {
    emitExpression(parser, statement.expression, body);
    const value = nodeLocal(parser, statement.expression);
    instructionIndex(body, OP.LOCAL_GET, value);
    if (statement.kind === "bind") {
      instructionIndex(body, OP.LOCAL_SET, statement.binding);
    } else {
      instructionIndex(body, OP.CALL, 0);
    }
  }
  body.byte(OP.END);

  const code = new Buffer();
  code.uleb(1);
  code.uleb(body.data.length);
  code.bytes(body.data);
  module.section(10, code);
  return module.toUint8Array();
}

export function compileKofun(source) {
  if (typeof source !== "string") {
    throw new TypeError("Kofun source must be a string");
  }
  if (new TextEncoder().encode(source).length > MAX_SOURCE_BYTES) {
    throw new KofunCompileError(1, "source exceeds 1 MiB wasm32 Core limit");
  }
  const parser = new Parser(source).parseProgram();
  return emitModule(parser);
}
