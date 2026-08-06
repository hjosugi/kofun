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

export function formatType(type) {
    if (type === null) return "";
    if (type.kind === "function") {
        return `${formatType(type.domain)} -> ${formatType(type.result)}`;
    }
    if (type.arguments.length === 0) return type.name;
    return `${type.name}[${type.arguments.map(formatType).join(", ")}]`;
}

// `take into file: File` — mode, then external label, then internal name, in
// the declaration order the contract fixes. `read` is the default and is
// printed only when the source wrote it, which the parser records by keeping
// the token rather than the resolved mode.
function formatParameter(parameter) {
    const words = [];
    if (parameter.mode && parameter.mode !== "read") words.push(parameter.mode);
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
    if (node.kind !== "Call") return inline(node);

    const args = node.arguments
        .map((argument) =>
            argument.label === null
                ? inline(argument.expression)
                : `${argument.label}: ${inline(argument.expression)}`)
        .join(", ");
    const head = `${inline(node.callee)}(${args})`;
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
