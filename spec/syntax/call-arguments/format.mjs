// The canonical formatter for the surface `parser.mjs` accepts, owned by issue
// #880 alongside it.
//
// The contract states the shape in one sentence: "Canonical formatting keeps
// all ordinary arguments inside parentheses, closes the parenthesis, writes one
// space, and then writes the trailing `fn` expression. Expression lambdas stay
// on the same line when they fit. Block lambdas use the existing block
// formatter and are not rewritten into expression lambdas."
//
// Two of those clauses are prohibitions and they are the reason this file
// exists rather than a `JSON.stringify`-shaped printer. A formatter that
// rewrote a block lambda into an expression lambda, or that reached for a brace
// lambda or an implicit parameter because they are shorter, would be inventing
// syntax the language does not have — and it would do it silently, on code that
// already parsed. There is no such spelling in the printer at all: `fn` is
// always followed by a parenthesised parameter list, and a block body is always
// printed as a block.
//
// **Comments are not carried.** `parser.mjs` records that a comment preceded a
// token — the trailing-lambda rule is stated in terms of that trivia — but it
// does not keep the comment text, so this printer cannot reproduce one. A
// formatter that silently dropped a comment would be worse than no formatter,
// so the surface corpus never asks this one to format a commented source: those
// entries carry `"format": false` and say why. Attaching comments to the tree
// is real work and it belongs to whoever ships a formatter a contributor runs,
// not to the slice that fixes the canonical shape.

const INDENT = "    ";
const DEFAULT_WIDTH = 80;

export class FormatError extends Error {
    constructor(message) {
        super(message);
        this.name = "FormatError";
    }
}

function pad(depth) {
    return INDENT.repeat(depth);
}

// Type printing is precedence-driven rather than case-driven, and the reason is
// that the case-driven version was wrong twice in the same way.
//
// The first version dropped the parentheses in `(Int -> Int) -> Int`, printing
// a function *returning* a function where the source had one *taking* a
// function. That was fixed with a single special case — parenthesise the domain
// when it is a function. Then `optional` and `moded` were added, which are two
// more ways for a function type to sit inside a node whose kind is not
// `function`, and the same class of bug came straight back:
//
//     (Int?)?                   ->  Int??                    does not reparse
//     (Int -> Int)?             ->  Int -> Int?              different type
//     take (Int -> Int) -> Int  ->  take Int -> Int -> Int   different type
//
// A grammar-restricted generator put 29% of well-formed types through that.
//
// It is not only cosmetic: `typeIsFunctional` decides trailing-lambda
// attachment, so `op: (Int -> Int)?` — which refuses a trailing lambda —
// printed as `op: Int -> Int?`, which accepts one. Running the formatter over a
// declaration changed whether its callers were allowed to write a trailing
// lambda.
//
// So each kind now declares how tightly it binds, and a child is parenthesised
// exactly when it binds looser than its position allows. `spec/grammar.ebnf`
// 67-75 is the source of the ordering: `?` binds tightest, then a mode prefix,
// then `->` (right-associative, so only its domain is restricted).
const TYPE_PRECEDENCE = Object.freeze({
    name: 0, tuple: 0, optional: 1, moded: 2, function: 3,
});

function formatTypeAt(type, looseness) {
    if (type === null) return "";
    const text = renderType(type);
    const bound = TYPE_PRECEDENCE[type.kind] ?? 0;
    return bound > looseness ? `(${text})` : text;
}

function renderType(type) {
    if (type.kind === "function") {
        // The domain may be anything up to a mode prefix; a function there
        // needs parentheses or `->` reassociates.
        return `${formatTypeAt(type.domain, 2)} -> ${formatTypeAt(type.result, 3)}`;
    }
    if (type.kind === "moded") {
        return `${type.mode} ${formatTypeAt(type.inner, 1)}`;
    }
    if (type.kind === "optional") {
        // `?` attaches to a primary type only, so anything looser is wrapped.
        return `${formatTypeAt(type.inner, 0)}?`;
    }
    if (type.kind === "tuple") {
        return `(${type.elements.map((element) => formatTypeAt(element, 3)).join(", ")})`;
    }
    if (type.arguments.length === 0) return type.name;
    return `${type.name}[${type.arguments.map((argument) => formatTypeAt(argument, 3)).join(", ")}]`;
}

export function formatType(type) {
    return type === null ? "" : formatTypeAt(type, 3);
}

// `take into file: File` — mode, then external label, then internal name, in
// the declaration order the contract fixes.
//
// `read` is the default, and the source may write it anyway. Deciding on the
// resolved mode — "print it unless it is `read`" — deleted an explicitly
// written `read` while `edit` and `take` survived, so the printer silently
// edited the declaration it was asked to format. `modeWritten` is what the
// parser records for exactly this, because the resolved mode cannot tell an
// omitted modifier from a written one.
function formatParameter(parameter) {
    const words = [];
    if (parameter.mode && parameter.modeWritten !== false) words.push(parameter.mode);
    if (parameter.external) words.push(parameter.external);
    words.push(parameter.internal);
    return `${words.join(" ")}: ${formatType(parameter.type)}`;
}

function formatLambdaParameters(parameters) {
    return parameters
        .map((parameter) =>
            parameter.type === null
                ? parameter.name
                : `${parameter.name}: ${formatType(parameter.type)}`)
        .join(", ");
}

// Renders a node on one line. Block bodies have no one-line form, so this
// throws rather than returning something a caller might print: a block lambda
// squeezed onto one line is exactly the rewrite the contract forbids.
function inline(node) {
    switch (node.kind) {
        case "Name":
            return node.name;
        case "Literal":
            return node.value;
        case "Group":
            return `(${inline(node.expression)})`;
        case "Member":
            return `${inline(node.subject)}.${node.member}`;
        case "Binary":
            return `${inline(node.left)} ${node.operator} ${inline(node.right)}`;
        case "Pipeline":
            return `${inline(node.subject)} |> ${inline(node.call)}`;
        case "Lambda":
            if (node.body === "block") {
                throw new FormatError("a block lambda has no single-line form");
            }
            return `fn(${formatLambdaParameters(node.parameters)}) => ${inline(node.expression)}`;
        case "Call": {
            const args = node.arguments
                .map((argument) =>
                    argument.label === null
                        ? inline(argument.expression)
                        : `${argument.label}: ${inline(argument.expression)}`)
                .join(", ");
            const head = `${inline(node.callee)}(${args})`;
            // The one space between `)` and `fn` is the canonical form. It is
            // written here and nowhere else, so there is a single place for it
            // to be wrong.
            return node.trailing === null ? head : `${head} ${inline(node.trailing)}`;
        }
        default:
            throw new FormatError(`no inline form for ${node.kind}`);
    }
}

export function formatExpression(node, depth = 0, width = DEFAULT_WIDTH) {
    // A block lambda anywhere in the expression forces the multi-line path,
    // whatever the width says.
    if (!hasBlockLambda(node)) {
        const single = inline(node);
        if (pad(depth).length + single.length <= width) return single;
    }
    return multiline(node, depth, width);
}

function hasBlockLambda(node) {
    if (node === null || typeof node !== "object") return false;
    if (node.kind === "Lambda" && node.body === "block") return true;
    for (const value of Object.values(node)) {
        if (Array.isArray(value)) {
            if (value.some(hasBlockLambda)) return true;
        } else if (value !== null && typeof value === "object") {
            if (hasBlockLambda(value)) return true;
        }
    }
    return false;
}

// The wrapped forms. Only the constructs this surface owns get one; anything
// else that overruns falls back to its single line, because inventing a wrap
// for arbitrary expressions is a separate decision and not this issue's.
function multiline(node, depth, width) {
    if (node.kind === "Pipeline") {
        return `${formatExpression(node.subject, depth, width)} |> ${formatExpression(node.call, depth, width)}`;
    }
    // A block lambda that is not in the trailing position still has to print.
    // The parser accepts one anywhere an expression may appear — `outer(0,
    // fn(x) { consume(x) })`, `let r: Int = fn(x) { consume(x) }` — and
    // `inline` throws on it by design, so routing arguments through `inline`
    // here turned every such program into an uncaught `FormatError`. That is
    // not a `SurfaceError`, so it would kill the gate with a stack trace
    // instead of a named assertion, which is exactly what this gate says it
    // exists to prevent.
    if (node.kind === "Lambda") {
        // The block case was handled here; the expression case was not, so an
        // expression lambda whose *body* holds a block lambda fell through to
        // `inline` and raised. 789 of 21209 generated expressions hit it.
        return node.body === "block"
            ? `fn(${formatLambdaParameters(node.parameters)}) ${formatBlock(node.block, depth, width)}`
            : `fn(${formatLambdaParameters(node.parameters)}) => ${formatExpression(node.expression, depth, width)}`;
    }
    // Every remaining kind has to recurse through `formatExpression`, not
    // `inline`. A first attempt added only the `Lambda` branch above and left
    // this line as `return inline(node)`, which meant a block lambda inside a
    // group, a binary operand, a member subject, or a callee still reached the
    // raise it was supposed to remove — including `let r: Int = (fn(x) { ... })`,
    // one paren away from a shape the fix claimed to handle. A generative
    // round-trip over 16000 format attempts hit it 1772 times.
    if (node.kind === "Group") {
        return `(${formatExpression(node.expression, depth, width)})`;
    }
    if (node.kind === "Binary") {
        return `${formatExpression(node.left, depth, width)} ${node.operator} ${formatExpression(node.right, depth, width)}`;
    }
    if (node.kind === "Member") {
        return `${formatExpression(node.subject, depth, width)}.${node.member}`;
    }
    if (node.kind !== "Call") return inline(node);

    // Arguments go through `formatExpression`, not `inline`, so an argument
    // that has no single-line form gets its multi-line one rather than raising.
    const args = node.arguments
        .map((argument) =>
            argument.label === null
                ? formatExpression(argument.expression, depth, width)
                : `${argument.label}: ${formatExpression(argument.expression, depth, width)}`)
        .join(", ");
    // The callee too: a block lambda can sit in callee position, as
    // `fn(x) { consume(x) }(1)`.
    const head = `${formatExpression(node.callee, depth, width)}(${args})`;
    if (node.trailing === null) return head;

    const lambda = node.trailing;
    const parameters = `fn(${formatLambdaParameters(lambda.parameters)})`;
    if (lambda.body === "block") {
        // `) fn(...) {` then the block, at the caller's depth. The arguments
        // stay inside the parentheses even here: the trailing lambda is the
        // only thing that leaves them.
        return `${head} ${parameters} ${formatBlock(lambda.block, depth, width)}`;
    }
    // An expression lambda that does not fit breaks after `=>`, so `) fn(...)`
    // survives on the first line and the reader still sees the attachment.
    return `${head} ${parameters} =>\n${pad(depth + 1)}${formatExpression(lambda.expression, depth + 1, width)}`;
}

export function formatBlock(block, depth = 0, width = DEFAULT_WIDTH) {
    if (block.statements.length === 0) return "{}";
    const lines = block.statements.map((statement) =>
        `${pad(depth + 1)}${formatStatement(statement, depth + 1, width)}`);
    return `{\n${lines.join("\n")}\n${pad(depth)}}`;
}

export function formatStatement(statement, depth = 0, width = DEFAULT_WIDTH) {
    switch (statement.kind) {
        case "Let":
            return `let ${statement.name}: ${formatType(statement.type)} = ${formatExpression(statement.value, depth, width)}`;
        case "Return":
            return `return ${formatExpression(statement.value, depth, width)}`;
        case "Expression":
            return formatExpression(statement.expression, depth, width);
        case "Function":
            return formatDeclaration(statement, depth, width);
        default:
            throw new FormatError(`no statement form for ${statement.kind}`);
    }
}

export function formatDeclaration(declaration, depth = 0, width = DEFAULT_WIDTH) {
    const parameters = declaration.parameters.map(formatParameter).join(", ");
    const result = declaration.result === null ? "" : ` -> ${formatType(declaration.result)}`;
    const signature = `fn ${declaration.name}(${parameters})${result}`;
    if (declaration.value !== undefined && declaration.value !== null) {
        return `${signature} = ${formatExpression(declaration.value, depth, width)}`;
    }
    if (declaration.body === undefined || declaration.body === null) return signature;
    return `${signature} ${formatBlock(declaration.body, depth, width)}`;
}

export function formatProgram(program, width = DEFAULT_WIDTH) {
    return `${program.declarations
        .map((declaration) => formatDeclaration(declaration, 0, width))
        .join("\n\n")}\n`;
}

export function formatFragment(fragment, width = DEFAULT_WIDTH) {
    return fragment.statements
        .map((statement) => formatStatement(statement, 0, width))
        .join("\n");
}
