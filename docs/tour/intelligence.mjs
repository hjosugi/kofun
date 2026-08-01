// Hover and completion for the tour editor, over the declarations the bounded
// browser compiler actually accepted. Nothing here invents a name or a type:
// every declaration comes from `analyzeKofun`, which runs the same parser that
// emits the module, so the editor cannot claim more than the compiler does.
//
// The rules deliberately match the language server in tooling/lsp:
// visibility is declaration-before-use, a bounded list is reported incomplete
// rather than silently truncated, and positions that are not references answer
// with nothing instead of a guess.

// The bounded Core's whole vocabulary. It is short because the Core is small,
// and listing it here rather than in the UI keeps one source of truth.
export const VOCABULARY = Object.freeze([
  { label: "let", kind: "keyword", detail: "bind an immutable Int" },
  { label: "print", kind: "builtin", detail: "print(Int) -> Void" },
  { label: "fn", kind: "keyword", detail: "declare a function" },
  { label: "main", kind: "keyword", detail: "the entry point this Core runs" },
  { label: "Int", kind: "type", detail: "64-bit signed integer" },
]);

const MAX_COMPLETION_ITEMS = 50;

function isIdentifierStart(character) {
  return /[A-Za-z_]/u.test(character);
}

function isIdentifierContinue(character) {
  return /[A-Za-z0-9_]/u.test(character);
}

// Comments run from `#` to end of line. The Core has no string literals, so a
// comment is the only place a name is not a name.
export function inComment(source, offset) {
  let lineStart = offset;
  while (lineStart > 0 && source[lineStart - 1] !== "\n") lineStart -= 1;
  for (let at = lineStart; at < offset; at += 1) {
    if (source[at] === "#") return true;
  }
  return false;
}

export function prefixAt(source, offset) {
  let start = offset;
  while (start > 0 && isIdentifierContinue(source[start - 1])) start -= 1;
  if (start < offset && !isIdentifierStart(source[start])) return "";
  return source.slice(start, offset);
}

export function identifierAt(source, offset) {
  if (offset < 0 || offset > source.length) return null;
  let start = offset;
  while (start > 0 && isIdentifierContinue(source[start - 1])) start -= 1;
  let end = offset;
  while (end < source.length && isIdentifierContinue(source[end])) end += 1;
  if (start === end || !isIdentifierStart(source[start])) return null;
  return { name: source.slice(start, end), start, end };
}

function visible(declarations, offset) {
  // Later declarations of one name shadow earlier ones, as findBinding in the
  // compiler resolves them: it scans backwards and takes the last match.
  const byName = new Map();
  for (const declaration of declarations) {
    if (offset < declaration.scopeStart) continue;
    byName.set(declaration.name, declaration);
  }
  return byName;
}

export function completionAt(analysis, source, offset) {
  if (inComment(source, offset)) return { items: [], isIncomplete: false };
  const prefix = prefixAt(source, offset).toLowerCase();
  const matches = (label) => !prefix || label.toLowerCase().startsWith(prefix);

  const items = [];
  let truncated = false;
  for (const declaration of visible(analysis.declarations, offset).values()) {
    if (!matches(declaration.name)) continue;
    if (items.length >= MAX_COMPLETION_ITEMS - VOCABULARY.length) {
      truncated = true;
      break;
    }
    items.push({
      label: declaration.name,
      kind: "binding",
      detail: `${declaration.name}: ${declaration.type}`,
      note: `declared on line ${declaration.line}`,
    });
  }
  const taken = new Set(items.map((item) => item.label));
  for (const entry of VOCABULARY) {
    if (!matches(entry.label) || taken.has(entry.label)) continue;
    items.push({ label: entry.label, kind: entry.kind, detail: entry.detail });
  }
  return { items, isIncomplete: truncated };
}

export function hoverAt(analysis, source, offset) {
  if (inComment(source, offset)) return null;
  const identifier = identifierAt(source, offset);
  if (identifier === null) return null;

  // A use resolves to the declaration in scope at the use; the declaration
  // itself is not yet in scope at its own name, so it is matched directly.
  const declaration = visible(analysis.declarations, identifier.start)
    .get(identifier.name) ??
    analysis.declarations.find((candidate) =>
      candidate.start === identifier.start && candidate.end === identifier.end);
  if (declaration !== undefined) {
    const here = declaration.start === identifier.start;
    return {
      start: identifier.start,
      end: identifier.end,
      title: `${declaration.name}: ${declaration.type}`,
      body: here
        ? "Immutable binding. The browser Core has no reassignment, so this " +
          "name keeps this value for the rest of the program."
        : `Immutable binding declared on line ${declaration.line}.`,
      source: "compiler",
    };
  }

  const entry = VOCABULARY.find((candidate) => candidate.label === identifier.name);
  if (entry !== undefined) {
    return {
      start: identifier.start,
      end: identifier.end,
      title: `${entry.label} — ${entry.detail}`,
      body: entry.kind === "builtin"
        ? "Built into the browser Core."
        : "Part of the Core's fixed vocabulary.",
      source: "vocabulary",
    };
  }

  // An unknown name is reported as unknown. Guessing a type here would be the
  // one thing this tour is trying not to teach.
  return {
    start: identifier.start,
    end: identifier.end,
    title: `${identifier.name}: unknown`,
    body: analysis.error === null
      ? "No declaration of this name is in scope at this point."
      : `The program does not compile yet, so nothing is known about this ` +
        `name. ${analysis.error}`,
    source: "unresolved",
  };
}
