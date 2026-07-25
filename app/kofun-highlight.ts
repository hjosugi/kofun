export type KofunHighlightKind =
  | "plain"
  | "comment"
  | "keyword"
  | "type"
  | "number"
  | "string"
  | "operator"
  | "function";

export type KofunHighlightToken = {
  kind: KofunHighlightKind;
  value: string;
};

const KEYWORDS = new Set([
  "as",
  "break",
  "continue",
  "edit",
  "else",
  "enum",
  "false",
  "fn",
  "for",
  "if",
  "impl",
  "in",
  "law",
  "let",
  "match",
  "mut",
  "null",
  "pure",
  "read",
  "return",
  "take",
  "trait",
  "true",
  "type",
  "unsafe",
  "while",
]);

const TYPE_NAMES = new Set([
  "Any",
  "Bool",
  "Decimal",
  "Float",
  "Int",
  "List",
  "Never",
  "Null",
  "Resource",
  "Text",
  "Tuple",
  "Void",
]);

const OPERATOR_PAIRS = new Set([
  "=>",
  "->",
  "|>",
  "==",
  "!=",
  "<=",
  ">=",
  "??",
  "..",
  "//",
]);

const isIdentifierStart = (value: string) =>
  value === "_" || /[\p{L}]/u.test(value);

const isIdentifierContinue = (value: string) =>
  value === "_" || /[\p{L}\p{N}]/u.test(value);

/**
 * Losslessly classifies source text for the editable playground overlay.
 *
 * This lexer is deliberately tolerant: incomplete strings and partially typed
 * programs still receive highlighting and are never rejected by the editor.
 * Parsing and diagnostics remain the responsibility of the browser runtime.
 */
export function tokenizeKofunForHighlight(
  source: string,
): KofunHighlightToken[] {
  const tokens: KofunHighlightToken[] = [];
  let cursor = 0;
  let expectFunctionName = false;

  const push = (kind: KofunHighlightKind, value: string) => {
    if (!value) return;
    const previous = tokens.at(-1);
    if (previous?.kind === kind) {
      previous.value += value;
      return;
    }
    tokens.push({ kind, value });
  };

  while (cursor < source.length) {
    const current = source[cursor] ?? "";

    if (/\s/u.test(current)) {
      const start = cursor;
      while (cursor < source.length && /\s/u.test(source[cursor] ?? "")) {
        cursor += 1;
      }
      push("plain", source.slice(start, cursor));
      continue;
    }

    if (current === "#") {
      const start = cursor;
      while (cursor < source.length && source[cursor] !== "\n") cursor += 1;
      push("comment", source.slice(start, cursor));
      continue;
    }

    if (current === '"') {
      const start = cursor;
      cursor += 1;
      while (cursor < source.length) {
        if (source[cursor] === "\\") {
          cursor += Math.min(2, source.length - cursor);
          continue;
        }
        const value = source[cursor];
        cursor += 1;
        if (value === '"') break;
      }
      push("string", source.slice(start, cursor));
      expectFunctionName = false;
      continue;
    }

    if (/[0-9]/u.test(current)) {
      const start = cursor;
      cursor += 1;
      while (/[0-9_]/u.test(source[cursor] ?? "")) cursor += 1;
      if (
        source[cursor] === "." &&
        source[cursor + 1] !== "." &&
        /[0-9]/u.test(source[cursor + 1] ?? "")
      ) {
        cursor += 1;
        while (/[0-9_]/u.test(source[cursor] ?? "")) cursor += 1;
      }
      push("number", source.slice(start, cursor));
      expectFunctionName = false;
      continue;
    }

    if (isIdentifierStart(current)) {
      const start = cursor;
      cursor += 1;
      while (isIdentifierContinue(source[cursor] ?? "")) cursor += 1;
      const value = source.slice(start, cursor);
      let next = cursor;
      while (/\s/u.test(source[next] ?? "")) next += 1;

      if (expectFunctionName || source[next] === "(") {
        push("function", value);
      } else if (KEYWORDS.has(value)) {
        push("keyword", value);
      } else if (TYPE_NAMES.has(value) || /^\p{Lu}/u.test(value)) {
        push("type", value);
      } else {
        push("plain", value);
      }

      expectFunctionName = value === "fn";
      continue;
    }

    const pair = source.slice(cursor, cursor + 2);
    if (OPERATOR_PAIRS.has(pair)) {
      push("operator", pair);
      cursor += 2;
      expectFunctionName = false;
      continue;
    }

    if ("=+-*/%<>!".includes(current)) {
      push("operator", current);
      cursor += 1;
      expectFunctionName = false;
      continue;
    }

    push("plain", current);
    cursor += 1;
    expectFunctionName = false;
  }

  return tokens;
}
