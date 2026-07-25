export type PlaygroundExample = {
  id: string;
  name: string;
  description: string;
  source: string;
};

export type PlaygroundResult = {
  output: string;
  durationMs: number;
  tokenCount: number;
  steps: number;
  error?: {
    code: string;
    message: string;
    line: number;
    column: number;
  };
};

type TokenKind = "number" | "string" | "identifier" | "symbol" | "eof";

type Token = {
  kind: TokenKind;
  value: string;
  line: number;
  column: number;
};

type LiteralValue = number | string | boolean | null;

type Expression =
  | { kind: "literal"; value: LiteralValue; token: Token }
  | { kind: "list"; items: Expression[]; token: Token }
  | { kind: "variable"; name: string; token: Token }
  | {
      kind: "unary";
      operator: string;
      value: Expression;
      token: Token;
    }
  | {
      kind: "binary";
      operator: string;
      left: Expression;
      right: Expression;
      token: Token;
    }
  | {
      kind: "call";
      callee: Expression;
      arguments: Expression[];
      token: Token;
    }
  | {
      kind: "lambda";
      parameters: string[];
      body: Expression;
      token: Token;
    }
  | {
      kind: "index";
      target: Expression;
      index: Expression;
      token: Token;
    }
  | {
      kind: "if";
      condition: Expression;
      thenBranch: Statement[];
      elseBranch: Statement[] | Expression;
      token: Token;
    };

type Statement =
  | {
      kind: "let";
      name: string;
      mutable: boolean;
      value: Expression;
      token: Token;
    }
  | { kind: "assign"; name: string; value: Expression; token: Token }
  | { kind: "return"; value: Expression; token: Token }
  | { kind: "expression"; value: Expression; token: Token };

type FunctionDeclaration = {
  name: string;
  parameters: string[];
  body: Statement[];
  token: Token;
};

type Program = {
  functions: FunctionDeclaration[];
};

class KofunError extends Error {
  readonly code: string;
  readonly token: Token;

  constructor(
    code: string,
    message: string,
    token: Token,
  ) {
    super(message);
    this.code = code;
    this.token = token;
    this.name = "KofunError";
  }
}

const isIdentifierStart = (value: string) =>
  value === "_" || /[\p{L}]/u.test(value);

const isIdentifierContinue = (value: string) =>
  value === "_" || /[\p{L}\p{N}]/u.test(value);

function tokenize(source: string): Token[] {
  const tokens: Token[] = [];
  let cursor = 0;
  let line = 1;
  let column = 1;

  const current = () => source[cursor] ?? "";
  const peek = (distance = 1) => source[cursor + distance] ?? "";
  const advance = () => {
    const value = source[cursor++] ?? "";
    if (value === "\n") {
      line += 1;
      column = 1;
    } else {
      column += 1;
    }
    return value;
  };
  const add = (
    kind: TokenKind,
    value: string,
    tokenLine: number,
    tokenColumn: number,
  ) => tokens.push({ kind, value, line: tokenLine, column: tokenColumn });

  while (cursor < source.length) {
    if (/\s/u.test(current())) {
      advance();
      continue;
    }
    if (current() === "#") {
      while (cursor < source.length && current() !== "\n") advance();
      continue;
    }

    const tokenLine = line;
    const tokenColumn = column;

    if (isIdentifierStart(current())) {
      let value = advance();
      while (isIdentifierContinue(current())) value += advance();
      add("identifier", value, tokenLine, tokenColumn);
      continue;
    }

    if (/[0-9]/u.test(current())) {
      let value = advance();
      while (/[0-9_]/u.test(current())) value += advance();
      if (current() === "." && peek() !== "." && /[0-9]/u.test(peek())) {
        value += advance();
        while (/[0-9_]/u.test(current())) value += advance();
      }
      add("number", value.replaceAll("_", ""), tokenLine, tokenColumn);
      continue;
    }

    if (current() === '"') {
      advance();
      let value = "";
      let closed = false;
      while (cursor < source.length) {
        const char = advance();
        if (char === '"') {
          closed = true;
          break;
        }
        if (char === "\n") {
          throw new KofunError(
            "P001",
            "Text literals cannot contain a raw newline",
            { kind: "string", value, line: tokenLine, column: tokenColumn },
          );
        }
        if (char !== "\\") {
          value += char;
          continue;
        }
        const escaped = advance();
        if (escaped === "n") value += "\n";
        else if (escaped === "r") value += "\r";
        else if (escaped === "t") value += "\t";
        else if (escaped === '"') value += '"';
        else if (escaped === "\\") value += "\\";
        else if (escaped === "x") {
          const digits = advance() + advance();
          if (!/^[0-9a-f]{2}$/iu.test(digits)) {
            throw new KofunError(
              "P001",
              "Expected two hexadecimal digits after \\x",
              { kind: "string", value, line: tokenLine, column: tokenColumn },
            );
          }
          value += String.fromCodePoint(Number.parseInt(digits, 16));
        } else if (escaped === "u") {
          let digits = "";
          for (let index = 0; index < 4; index += 1) digits += advance();
          if (!/^[0-9a-f]{4}$/iu.test(digits)) {
            throw new KofunError(
              "P001",
              "Expected four hexadecimal digits after \\u",
              { kind: "string", value, line: tokenLine, column: tokenColumn },
            );
          }
          value += String.fromCodePoint(Number.parseInt(digits, 16));
        } else {
          throw new KofunError(
            "P001",
            `Unsupported Text escape \\${escaped}`,
            { kind: "string", value, line: tokenLine, column: tokenColumn },
          );
        }
      }
      if (!closed) {
        throw new KofunError("P001", "Unterminated Text literal", {
          kind: "string",
          value,
          line: tokenLine,
          column: tokenColumn,
        });
      }
      add("string", value, tokenLine, tokenColumn);
      continue;
    }

    const pair = current() + peek();
    if (
      ["=>", "->", "|>", "==", "!=", "<=", ">=", "??", "..", "//"].includes(
        pair,
      )
    ) {
      advance();
      advance();
      add("symbol", pair, tokenLine, tokenColumn);
      continue;
    }
    if ("(){}[],:=+-*/%<>!".includes(current())) {
      add("symbol", advance(), tokenLine, tokenColumn);
      continue;
    }

    throw new KofunError(
      "P001",
      `Unexpected character ${JSON.stringify(current())}`,
      {
        kind: "symbol",
        value: current(),
        line: tokenLine,
        column: tokenColumn,
      },
    );
  }

  tokens.push({ kind: "eof", value: "", line, column });
  return tokens;
}

const PRECEDENCE: Record<string, number> = {
  "??": 1,
  "|>": 2,
  "..": 3,
  "==": 4,
  "!=": 4,
  "<": 5,
  "<=": 5,
  ">": 5,
  ">=": 5,
  "+": 6,
  "-": 6,
  "*": 7,
  "/": 7,
  "//": 7,
  "%": 7,
};

class Parser {
  private cursor = 0;
  private readonly tokens: Token[];

  constructor(tokens: Token[]) {
    this.tokens = tokens;
  }

  parseProgram(): Program {
    const functions: FunctionDeclaration[] = [];
    while (!this.at("eof")) functions.push(this.parseFunction());
    if (!functions.some((declaration) => declaration.name === "main")) {
      throw new KofunError("P002", "Program must declare fn main()", this.peek());
    }
    return { functions };
  }

  private parseFunction(): FunctionDeclaration {
    const token = this.expectValue("fn", "Expected a function declaration");
    const name = this.expect("identifier", "Expected function name").value;
    this.expectValue("(", "Expected ( after function name");
    const parameters = this.parseParameters();
    this.expectValue(")", "Expected ) after function parameters");
    if (this.matchValue("->")) {
      while (!this.atValue("{") && !this.at("eof")) this.advance();
    }
    const body = this.parseBlock();
    return { name, parameters, body, token };
  }

  private parseParameters(): string[] {
    const parameters: string[] = [];
    while (!this.atValue(")") && !this.at("eof")) {
      parameters.push(
        this.expect("identifier", "Expected parameter name").value,
      );
      if (this.matchValue(":")) this.skipType([",", ")"]);
      if (!this.matchValue(",")) break;
    }
    return parameters;
  }

  /**
   * Decides whether the `(` the parser just consumed opens an arrow lambda's
   * parameter list instead of a grouping parenthesis (#547). Scans forward to
   * the matching `)` and reports whether `=>` follows it, so `(a + b) * 2`
   * stays arithmetic while `(x, y) => x + y` becomes a lambda.
   */
  private parenthesisOpensLambda(): boolean {
    let depth = 1;
    let index = this.cursor;
    while (index < this.tokens.length) {
      const token = this.tokens[index];
      if (token.kind === "eof") return false;
      if (token.kind === "symbol") {
        if (token.value === "(" || token.value === "[" || token.value === "{") {
          depth += 1;
        } else if (
          token.value === ")" ||
          token.value === "]" ||
          token.value === "}"
        ) {
          depth -= 1;
          if (depth === 0) {
            return (
              token.value === ")" && this.tokens[index + 1]?.value === "=>"
            );
          }
        }
      }
      index += 1;
    }
    return false;
  }

  /**
   * Recognises `x: Int => …`, the annotated arrow parameter written without
   * parentheses (#547). The cursor must sit on the `:`. Returns the annotation
   * source so the diagnostic can spell out the parenthesised fix, or null when
   * the `:` belongs to something else.
   */
  private unparenthesisedParameterAnnotation(): string | null {
    const parts: string[] = [];
    let depth = 0;
    let index = this.cursor + 1;
    while (index < this.tokens.length) {
      const token = this.tokens[index];
      if (token.kind === "eof") return null;
      if (token.value === "(" || token.value === "[" || token.value === "{") {
        depth += 1;
      } else if (
        token.value === ")" ||
        token.value === "]" ||
        token.value === "}"
      ) {
        if (depth === 0) return null;
        depth -= 1;
      } else if (depth === 0) {
        if (token.value === "=>") {
          return parts.length > 0 ? parts.join("") : null;
        }
        if (token.value === "," || token.value === "=") return null;
      }
      parts.push(token.value);
      index += 1;
    }
    return null;
  }

  private skipType(stops: string[]) {
    let depth = 0;
    while (!this.at("eof")) {
      const value = this.peek().value;
      if (depth === 0 && stops.includes(value)) return;
      if (value === "[") depth += 1;
      if (value === "]") depth -= 1;
      this.advance();
    }
  }

  private parseBlock(): Statement[] {
    this.expectValue("{", "Expected { to start block");
    const statements: Statement[] = [];
    while (!this.atValue("}") && !this.at("eof")) {
      statements.push(this.parseStatement());
    }
    this.expectValue("}", "Expected } to end block");
    return statements;
  }

  private parseStatement(): Statement {
    if (this.matchValue("let")) {
      const token = this.previous();
      const mutable = this.matchValue("mut");
      const name = this.expect("identifier", "Expected binding name").value;
      if (this.matchValue(":")) this.skipType(["="]);
      this.expectValue("=", "Expected = in binding");
      return {
        kind: "let",
        name,
        mutable,
        value: this.parseExpression(),
        token,
      };
    }
    if (this.matchValue("return")) {
      const token = this.previous();
      return {
        kind: "return",
        value: this.parseExpression(),
        token,
      };
    }
    if (
      this.peek().kind === "identifier" &&
      this.peek(1).value === "="
    ) {
      const token = this.advance();
      const name = token.value;
      this.advance();
      return {
        kind: "assign",
        name,
        value: this.parseExpression(),
        token,
      };
    }
    const value = this.parseExpression();
    return { kind: "expression", value, token: value.token };
  }

  private parseExpression(minimumPrecedence = 0): Expression {
    let left = this.parsePrefix();
    left = this.parsePostfix(left);

    while (true) {
      const operator = this.peek();
      const precedence = PRECEDENCE[operator.value];
      if (precedence === undefined || precedence < minimumPrecedence) break;
      if (operator.value === "/") {
        // `/` is not defined on Int (#687): with no implicit numeric promotion
        // it cannot produce a fractional value from two Int operands. Rejected
        // here so the playground agrees with every compiler backend.
        throw new KofunError(
          "P002",
          "`/` is not defined on Int; use `//` for the integer quotient",
          operator,
        );
      }
      this.advance();
      const right = this.parseExpression(precedence + 1);
      left = {
        kind: "binary",
        operator: operator.value,
        left,
        right,
        token: operator,
      };
    }
    return left;
  }

  private parsePrefix(): Expression {
    const token = this.advance();
    if (token.kind === "number") {
      const value = Number(token.value);
      if (!Number.isFinite(value)) {
        throw new KofunError("P002", "Numeric literal is out of range", token);
      }
      return { kind: "literal", value, token };
    }
    if (token.kind === "string") {
      return { kind: "literal", value: token.value, token };
    }
    if (token.value === "true" || token.value === "false") {
      return { kind: "literal", value: token.value === "true", token };
    }
    if (token.value === "null") {
      return { kind: "literal", value: null, token };
    }
    if (token.value === "-" || token.value === "!") {
      return {
        kind: "unary",
        operator: token.value,
        value: this.parseExpression(8),
        token,
      };
    }
    if (token.value === "(") {
      if (this.parenthesisOpensLambda()) {
        return this.parseArrowLambdaAfterParenthesis(token);
      }
      const expression = this.parseExpression();
      this.expectValue(")", "Expected ) after expression");
      return expression;
    }
    if (token.value === "[") {
      const items: Expression[] = [];
      while (!this.atValue("]") && !this.at("eof")) {
        items.push(this.parseExpression());
        if (!this.matchValue(",")) break;
      }
      this.expectValue("]", "Expected ] after List literal");
      return { kind: "list", items, token };
    }
    if (token.value === "fn") {
      this.expectValue("(", "Expected ( after fn in lambda");
      const parameters = this.parseParameters();
      this.expectValue(")", "Expected ) after lambda parameters");
      this.expectValue("=>", "Expected => after lambda parameters");
      return {
        kind: "lambda",
        parameters,
        body: this.parseExpression(),
        token,
      };
    }
    if (token.value === "if") {
      const condition = this.parseExpression();
      const thenBranch = this.parseBlock();
      this.expectValue("else", "Playground if expressions require else");
      const elseBranch = this.matchValue("if")
        ? this.parseIfAfterKeyword(this.previous())
        : this.parseBlock();
      return {
        kind: "if",
        condition,
        thenBranch,
        elseBranch,
        token,
      };
    }
    if (token.kind === "identifier") {
      // `x => x * 2`: one token of lookahead separates a lambda from a plain
      // name. Annotations are deliberately not accepted here (#547).
      //
      // In the full language `Trace => 0` is also a match arm, so an
      // identifier before `=>` is genuinely ambiguous there. This interpreter
      // has no `match`, `enum`, or pattern syntax, so the two forms cannot
      // meet: anything reaching here is expression position. If `match` is
      // ever added to the playground, this branch has to be suppressed while
      // parsing arm patterns.
      if (this.matchValue("=>")) {
        return {
          kind: "lambda",
          parameters: [token.value],
          body: this.parseExpression(),
          token,
        };
      }
      if (this.atValue(":")) {
        const annotation = this.unparenthesisedParameterAnnotation();
        if (annotation !== null) {
          throw new KofunError(
            "P002",
            `A typed lambda parameter needs parentheses: write (${token.value}: ${annotation}) => ... instead of ${token.value}: ${annotation} => ...`,
            this.peek(),
          );
        }
      }
      return { kind: "variable", name: token.value, token };
    }
    throw new KofunError("P002", "Expected expression", token);
  }

  /**
   * Parses `(x, y) => …` and `(x: Int) => …` once the opening `(` has been
   * consumed and the lookahead has confirmed the trailing `=>` (#547).
   */
  private parseArrowLambdaAfterParenthesis(token: Token): Expression {
    const parameters = this.parseParameters();
    this.expectValue(")", "Expected ) after lambda parameters");
    this.expectValue("=>", "Expected => after lambda parameters");
    return {
      kind: "lambda",
      parameters,
      body: this.parseExpression(),
      token,
    };
  }

  private parseIfAfterKeyword(token: Token): Expression {
    const condition = this.parseExpression();
    const thenBranch = this.parseBlock();
    this.expectValue("else", "Playground if expressions require else");
    const elseBranch = this.matchValue("if")
      ? this.parseIfAfterKeyword(this.previous())
      : this.parseBlock();
    return {
      kind: "if",
      condition,
      thenBranch,
      elseBranch,
      token,
    };
  }

  private parsePostfix(initial: Expression): Expression {
    let expression = initial;
    while (true) {
      if (this.matchValue("(")) {
        const token = this.previous();
        const argumentsList: Expression[] = [];
        while (!this.atValue(")") && !this.at("eof")) {
          argumentsList.push(this.parseExpression());
          if (!this.matchValue(",")) break;
        }
        this.expectValue(")", "Expected ) after call arguments");
        expression = {
          kind: "call",
          callee: expression,
          arguments: argumentsList,
          token,
        };
        continue;
      }
      if (this.matchValue("[")) {
        const token = this.previous();
        const index = this.parseExpression();
        this.expectValue("]", "Expected ] after index");
        expression = {
          kind: "index",
          target: expression,
          index,
          token,
        };
        continue;
      }
      break;
    }
    return expression;
  }

  private peek(distance = 0) {
    return this.tokens[Math.min(this.cursor + distance, this.tokens.length - 1)];
  }

  private previous() {
    return this.tokens[Math.max(0, this.cursor - 1)];
  }

  private advance() {
    const token = this.peek();
    if (token.kind !== "eof") this.cursor += 1;
    return token;
  }

  private at(kind: TokenKind) {
    return this.peek().kind === kind;
  }

  private atValue(value: string) {
    return this.peek().value === value;
  }

  private matchValue(value: string) {
    if (!this.atValue(value)) return false;
    this.advance();
    return true;
  }

  private expect(kind: TokenKind, message: string) {
    const token = this.peek();
    if (token.kind !== kind) throw new KofunError("P002", message, token);
    return this.advance();
  }

  private expectValue(value: string, message: string) {
    const token = this.peek();
    if (token.value !== value) throw new KofunError("P002", message, token);
    return this.advance();
  }
}

type RuntimeValue =
  | LiteralValue
  | RuntimeValue[]
  | { call: (argumentsList: RuntimeValue[], token: Token) => RuntimeValue };

type Binding = {
  value: RuntimeValue;
  mutable: boolean;
};

class Environment {
  private readonly bindings = new Map<string, Binding>();
  private readonly parent?: Environment;

  constructor(parent?: Environment) {
    this.parent = parent;
  }

  define(name: string, value: RuntimeValue, mutable = false) {
    this.bindings.set(name, { value, mutable });
  }

  get(name: string, token: Token): RuntimeValue {
    const binding = this.bindings.get(name);
    if (binding) return binding.value;
    if (this.parent) return this.parent.get(name, token);
    throw new KofunError("R001", `Unknown name ${name}`, token);
  }

  assign(name: string, value: RuntimeValue, token: Token) {
    const binding = this.bindings.get(name);
    if (binding) {
      if (!binding.mutable) {
        throw new KofunError(
          "R002",
          `Cannot assign to immutable binding ${name}`,
          token,
        );
      }
      binding.value = value;
      return;
    }
    if (this.parent) {
      this.parent.assign(name, value, token);
      return;
    }
    throw new KofunError("R001", `Unknown name ${name}`, token);
  }
}

class ReturnSignal {
  readonly value: RuntimeValue;

  constructor(value: RuntimeValue) {
    this.value = value;
  }
}

class Evaluator {
  private steps = 0;
  private readonly output: string[] = [];
  private readonly maximumSteps = 100_000;
  private readonly maximumListLength = 10_000;

  evaluate(program: Program) {
    const global = new Environment();
    this.installBuiltins(global);

    for (const declaration of program.functions) {
      global.define(declaration.name, {
        call: (argumentsList, token) =>
          this.callFunction(declaration, argumentsList, global, token),
      });
    }

    const main = global.get("main", program.functions[0]?.token ?? eofToken());
    this.call(main, [], program.functions[0]?.token ?? eofToken());
    return {
      output: this.output.join("\n"),
      steps: this.steps,
    };
  }

  private callFunction(
    declaration: FunctionDeclaration,
    argumentsList: RuntimeValue[],
    parent: Environment,
    token: Token,
  ): RuntimeValue {
    if (argumentsList.length !== declaration.parameters.length) {
      throw new KofunError(
        "R003",
        `${declaration.name} expects ${declaration.parameters.length} argument(s), received ${argumentsList.length}`,
        token,
      );
    }
    this.tick(token);
    const environment = new Environment(parent);
    declaration.parameters.forEach((name, index) =>
      environment.define(name, argumentsList[index]),
    );
    try {
      return this.evaluateBlock(declaration.body, environment);
    } catch (signal) {
      if (signal instanceof ReturnSignal) return signal.value;
      throw signal;
    }
  }

  private evaluateBlock(statements: Statement[], environment: Environment) {
    let last: RuntimeValue = null;
    for (const statement of statements) {
      this.tick(statement.token);
      if (statement.kind === "let") {
        environment.define(
          statement.name,
          this.evaluateExpression(statement.value, environment),
          statement.mutable,
        );
      } else if (statement.kind === "assign") {
        environment.assign(
          statement.name,
          this.evaluateExpression(statement.value, environment),
          statement.token,
        );
      } else if (statement.kind === "return") {
        throw new ReturnSignal(
          this.evaluateExpression(statement.value, environment),
        );
      } else {
        last = this.evaluateExpression(statement.value, environment);
      }
    }
    return last;
  }

  private evaluateExpression(
    expression: Expression,
    environment: Environment,
  ): RuntimeValue {
    this.tick(expression.token);
    if (expression.kind === "literal") return expression.value;
    if (expression.kind === "list") {
      const values = expression.items.map((item) =>
        this.evaluateExpression(item, environment),
      );
      this.checkListLength(values.length, expression.token);
      return values;
    }
    if (expression.kind === "variable") {
      return environment.get(expression.name, expression.token);
    }
    if (expression.kind === "unary") {
      const value = this.evaluateExpression(expression.value, environment);
      if (expression.operator === "-") {
        return -this.number(value, expression.token);
      }
      return !this.truthy(value);
    }
    if (expression.kind === "binary") {
      if (expression.operator === "|>") {
        const piped = this.evaluateExpression(expression.left, environment);
        if (expression.right.kind === "call") {
          const callee = this.evaluateExpression(
            expression.right.callee,
            environment,
          );
          const argumentsList = expression.right.arguments.map((argument) =>
            this.evaluateExpression(argument, environment),
          );
          return this.call(callee, [piped, ...argumentsList], expression.token);
        }
        return this.call(
          this.evaluateExpression(expression.right, environment),
          [piped],
          expression.token,
        );
      }
      if (expression.operator === "??") {
        const left = this.evaluateExpression(expression.left, environment);
        return left === null
          ? this.evaluateExpression(expression.right, environment)
          : left;
      }
      const left = this.evaluateExpression(expression.left, environment);
      const right = this.evaluateExpression(expression.right, environment);
      return this.binary(
        expression.operator,
        left,
        right,
        expression.token,
      );
    }
    if (expression.kind === "call") {
      const callee = this.evaluateExpression(expression.callee, environment);
      const argumentsList = expression.arguments.map((argument) =>
        this.evaluateExpression(argument, environment),
      );
      return this.call(callee, argumentsList, expression.token);
    }
    if (expression.kind === "lambda") {
      return {
        call: (argumentsList, token) => {
          if (argumentsList.length !== expression.parameters.length) {
            throw new KofunError(
              "R003",
              `Lambda expects ${expression.parameters.length} argument(s), received ${argumentsList.length}`,
              token,
            );
          }
          const lambdaEnvironment = new Environment(environment);
          expression.parameters.forEach((name, index) =>
            lambdaEnvironment.define(name, argumentsList[index]),
          );
          return this.evaluateExpression(expression.body, lambdaEnvironment);
        },
      };
    }
    if (expression.kind === "index") {
      const target = this.evaluateExpression(expression.target, environment);
      const rawIndex = this.integer(
        this.evaluateExpression(expression.index, environment),
        expression.token,
      );
      if (!Array.isArray(target) && typeof target !== "string") {
        throw new KofunError(
          "R004",
          "Indexing requires List or Text",
          expression.token,
        );
      }
      const values = typeof target === "string" ? Array.from(target) : target;
      const index = rawIndex < 0 ? values.length + rawIndex : rawIndex;
      if (index < 0 || index >= values.length) {
        throw new KofunError(
          "R004",
          "Index is outside the value",
          expression.token,
        );
      }
      return values[index];
    }
    const condition = this.evaluateExpression(
      expression.condition,
      environment,
    );
    if (this.truthy(condition)) {
      return this.evaluateBlock(
        expression.thenBranch,
        new Environment(environment),
      );
    }
    if (Array.isArray(expression.elseBranch)) {
      return this.evaluateBlock(
        expression.elseBranch,
        new Environment(environment),
      );
    }
    return this.evaluateExpression(expression.elseBranch, environment);
  }

  private binary(
    operator: string,
    left: RuntimeValue,
    right: RuntimeValue,
    token: Token,
  ): RuntimeValue {
    if (operator === "==") return this.equal(left, right);
    if (operator === "!=") return !this.equal(left, right);
    if (operator === "+") {
      if (typeof left === "string" && typeof right === "string") {
        return left + right;
      }
      return this.checkedNumber(
        this.number(left, token) + this.number(right, token),
        token,
      );
    }
    if (operator === "-") {
      return this.checkedNumber(
        this.number(left, token) - this.number(right, token),
        token,
      );
    }
    if (operator === "*") {
      return this.checkedNumber(
        this.number(left, token) * this.number(right, token),
        token,
      );
    }
    if (operator === "//" || operator === "%") {
      const divisor = this.number(right, token);
      if (divisor === 0) {
        throw new KofunError("R005", "Division by zero", token);
      }
      const dividend = this.number(left, token);
      if (operator === "//") return Math.floor(dividend / divisor);
      return dividend - Math.floor(dividend / divisor) * divisor;
    }
    if (["<", "<=", ">", ">="].includes(operator)) {
      const first = this.number(left, token);
      const second = this.number(right, token);
      if (operator === "<") return first < second;
      if (operator === "<=") return first <= second;
      if (operator === ">") return first > second;
      return first >= second;
    }
    if (operator === "..") {
      const start = this.integer(left, token);
      const end = this.integer(right, token);
      const length = Math.abs(end - start);
      this.checkListLength(length, token);
      return Array.from(
        { length },
        (_, index) => start + index * (end >= start ? 1 : -1),
      );
    }
    throw new KofunError("R006", `Unsupported operator ${operator}`, token);
  }

  private installBuiltins(environment: Environment) {
    const builtin = (
      name: string,
      callback: (argumentsList: RuntimeValue[], token: Token) => RuntimeValue,
    ) => environment.define(name, { call: callback });

    builtin("print", (argumentsList, token) => {
      this.requireArity("print", argumentsList, 1, token);
      this.output.push(this.format(argumentsList[0]));
      return null;
    });
    builtin("len", (argumentsList, token) => {
      this.requireArity("len", argumentsList, 1, token);
      const value = argumentsList[0];
      if (Array.isArray(value)) return value.length;
      if (typeof value === "string") return Array.from(value).length;
      throw new KofunError("R003", "len expects List or Text", token);
    });
    builtin("map", (argumentsList, token) => {
      this.requireArity("map", argumentsList, 2, token);
      const values = this.list(argumentsList[0], token);
      return values.map((value) =>
        this.call(argumentsList[1], [value], token),
      );
    });
    builtin("filter", (argumentsList, token) => {
      this.requireArity("filter", argumentsList, 2, token);
      const values = this.list(argumentsList[0], token);
      return values.filter((value) =>
        this.truthy(this.call(argumentsList[1], [value], token)),
      );
    });
    builtin("fold", (argumentsList, token) => {
      this.requireArity("fold", argumentsList, 3, token);
      const values = this.list(argumentsList[0], token);
      let accumulator = argumentsList[1];
      for (const value of values) {
        accumulator = this.call(
          argumentsList[2],
          [accumulator, value],
          token,
        );
      }
      return accumulator;
    });
    builtin("sum", (argumentsList, token) => {
      this.requireArity("sum", argumentsList, 1, token);
      return this.list(argumentsList[0], token).reduce<number>(
        (total, value) => total + this.number(value, token),
        0,
      );
    });
    builtin("mean", (argumentsList, token) => {
      this.requireArity("mean", argumentsList, 1, token);
      const values = this.list(argumentsList[0], token);
      if (values.length === 0) {
        throw new KofunError("R003", "mean requires a non-empty List", token);
      }
      return (
        values.reduce<number>(
          (total, value) => total + this.number(value, token),
          0,
        ) / values.length
      );
    });
    builtin("linspace", (argumentsList, token) => {
      this.requireArity("linspace", argumentsList, 3, token);
      const start = this.number(argumentsList[0], token);
      const end = this.number(argumentsList[1], token);
      const count = this.integer(argumentsList[2], token);
      if (count <= 0) {
        throw new KofunError("R003", "linspace count must be positive", token);
      }
      this.checkListLength(count, token);
      if (count === 1) return [start];
      return Array.from(
        { length: count },
        (_, index) => start + ((end - start) * index) / (count - 1),
      );
    });
    builtin("vmul", (argumentsList, token) => {
      this.requireArity("vmul", argumentsList, 2, token);
      const left = this.list(argumentsList[0], token);
      const right = this.list(argumentsList[1], token);
      if (left.length !== right.length) {
        throw new KofunError("R003", "vmul Lists must have equal length", token);
      }
      return left.map(
        (value, index) =>
          this.number(value, token) * this.number(right[index], token),
      );
    });
    builtin("dot", (argumentsList, token) => {
      this.requireArity("dot", argumentsList, 2, token);
      const left = this.list(argumentsList[0], token);
      const right = this.list(argumentsList[1], token);
      if (left.length !== right.length) {
        throw new KofunError("R003", "dot Lists must have equal length", token);
      }
      return left.reduce<number>(
        (total, value, index) =>
          total +
          this.number(value, token) * this.number(right[index], token),
        0,
      );
    });
  }

  private call(value: RuntimeValue, argumentsList: RuntimeValue[], token: Token) {
    if (
      value === null ||
      typeof value !== "object" ||
      Array.isArray(value) ||
      !("call" in value)
    ) {
      throw new KofunError("R003", "Value is not callable", token);
    }
    return value.call(argumentsList, token);
  }

  private requireArity(
    name: string,
    argumentsList: RuntimeValue[],
    expected: number,
    token: Token,
  ) {
    if (argumentsList.length !== expected) {
      throw new KofunError(
        "R003",
        `${name} expects ${expected} argument(s), received ${argumentsList.length}`,
        token,
      );
    }
  }

  private number(value: RuntimeValue, token: Token) {
    if (typeof value !== "number") {
      throw new KofunError("R003", "Expected a number", token);
    }
    return value;
  }

  private integer(value: RuntimeValue, token: Token) {
    const result = this.number(value, token);
    if (!Number.isSafeInteger(result)) {
      throw new KofunError("R003", "Expected a safe Int", token);
    }
    return result;
  }

  private checkedNumber(value: number, token: Token) {
    if (!Number.isFinite(value)) {
      throw new KofunError("R007", "Numeric result is out of range", token);
    }
    return value;
  }

  private list(value: RuntimeValue, token: Token): RuntimeValue[] {
    if (!Array.isArray(value)) {
      throw new KofunError("R003", "Expected List", token);
    }
    return value;
  }

  private checkListLength(length: number, token: Token) {
    if (length > this.maximumListLength) {
      throw new KofunError(
        "R008",
        `Playground List limit is ${this.maximumListLength}`,
        token,
      );
    }
  }

  private truthy(value: RuntimeValue) {
    return value !== false && value !== null && value !== 0;
  }

  private equal(left: RuntimeValue, right: RuntimeValue): boolean {
    if (Array.isArray(left) && Array.isArray(right)) {
      return (
        left.length === right.length &&
        left.every((value, index) => this.equal(value, right[index]))
      );
    }
    return left === right;
  }

  private format(value: RuntimeValue): string {
    if (value === null) return "null";
    if (Array.isArray(value)) {
      return `[${value.map((item) => this.format(item)).join(", ")}]`;
    }
    if (typeof value === "object") return "<function>";
    return String(value);
  }

  private tick(token: Token) {
    this.steps += 1;
    if (this.steps > this.maximumSteps) {
      throw new KofunError(
        "R009",
        `Playground step limit is ${this.maximumSteps}`,
        token,
      );
    }
  }
}

const eofToken = (): Token => ({
  kind: "eof",
  value: "",
  line: 1,
  column: 1,
});

export function runKofun(source: string): PlaygroundResult {
  const started = performance.now();
  let tokenCount = 0;
  try {
    const tokens = tokenize(source);
    tokenCount = tokens.length - 1;
    const program = new Parser(tokens).parseProgram();
    const result = new Evaluator().evaluate(program);
    return {
      output: result.output,
      durationMs: performance.now() - started,
      tokenCount,
      steps: result.steps,
    };
  } catch (error) {
    const diagnostic =
      error instanceof KofunError
        ? error
        : new KofunError(
            "R999",
            error instanceof Error ? error.message : "Unknown runtime failure",
            eofToken(),
          );
    return {
      output: "",
      durationMs: performance.now() - started,
      tokenCount,
      steps: 0,
      error: {
        code: diagnostic.code,
        message: diagnostic.message,
        line: diagnostic.token.line,
        column: diagnostic.token.column,
      },
    };
  }
}

export const PLAYGROUND_EXAMPLES: PlaygroundExample[] = [
  {
    id: "pipeline",
    name: "Pipeline",
    description: "Immutable Lists, lambdas, map, filter, and sum.",
    source: `fn main() {
    let values = [1, 2, 3, 4, 5, 6]
    let answer = values
        |> map(fn(x: Int) => x * x)
        |> filter(fn(x: Int) => x % 2 == 0)
        |> sum()

    print(answer)
}`,
  },
  {
    id: "branches",
    name: "Functions",
    description: "Typed parameters and expression-oriented branching.",
    source: `fn classify(score: Int) -> Text {
    return if score >= 90 {
        "excellent"
    } else if score >= 70 {
        "good"
    } else {
        "keep going"
    }
}

fn main() {
    print(classify(82))
}`,
  },
  {
    id: "science",
    name: "Science",
    description: "A small vector vocabulary for exploratory numerical work.",
    source: `fn main() {
    let x = linspace(0.0, 1.0, 5)
    let squared = vmul(x, x)

    print(mean(squared))
    print(dot(x, squared))
}`,
  },
  {
    id: "fold",
    name: "Fold",
    description: "Runtime accumulation over a List[Int].",
    source: `fn main() {
    let values = 1 .. 7
    let product = values
        |> fold(1, fn(total: Int, value: Int) => total * value)

    print(product)
}`,
  },
];
