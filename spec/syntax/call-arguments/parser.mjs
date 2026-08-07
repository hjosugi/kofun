// The surface parser for `spec/syntax/call-arguments-v1.md`, owned by issue
// #880 — declaration external labels, labelled call arguments, and the single
// canonical trailing-lambda spelling.
//
// This is the parse layer only. It decides *what the source says*, not what it
// means: no type binding, no overload selection, no lowering. `model.mjs`
// already owns binding (`bindCall`) and #881 owns the frontend that will use
// both. The one place the two layers touch is trailing-lambda attachment, and
// the contract is explicit about why: "The grammar never inserts a trailing
// lambda before overload resolution: the callee's single resolved signature
// must establish that the final parameter is functional." So this parser takes
// a signature environment and reads exactly one fact out of it — whether the
// final formal is functional — and refuses to guess when it cannot.
//
// `classifySurface` in model.mjs answers the same question with a regex over
// normalised text. That is enough to keep the decision document honest and it
// is not enough to be a parser: it cannot tell `outer(inner() fn(x) => x)` from
// two sibling calls, it has no notion of an unresolved callee, and it reports a
// kind rather than a tree a formatter can print. This file replaces it for
// every use that needs structure; model.mjs keeps its own copy because the
// decision gate must stay runnable on its own.
//
// Diagnostics here are *categories*, not numbers, because the contract says so:
// "Stable diagnostic categories are required; final numeric codes belong to the
// frontend implementation child."

const OWNERSHIP_MODES = new Set(["read", "edit", "take"]);
const KEYWORDS = new Set(["fn", "let", "return"]);

// Longest first, so `|>` never lexes as `|` and `->` never as `-`.
const PUNCTUATION = [
    "|>", "=>", "->", "==", "!=", "<=", ">=",
    "(", ")", "{", "}", "[", "]", ",", ":", ".",
    "+", "-", "*", "/", "%", "<", ">", "=",
];

export class SurfaceError extends Error {
    constructor(category, message, offset) {
        super(`${category}: ${message} at byte ${offset}`);
        this.name = "SurfaceError";
        this.category = category;
        this.offset = offset;
    }
}

const fail = (category, message, offset) => {
    throw new SurfaceError(category, message, offset);
};

// Every token carries whether a newline or a comment preceded it, because the
// trailing-lambda rule is stated in terms of that trivia and nothing else can
// recover it after lexing.
export function tokenize(source) {
    const tokens = [];
    let index = 0;
    let newlineBefore = false;
    let commentBefore = false;

    const push = (kind, value, offset) => {
        tokens.push({ kind, value, offset, newlineBefore, commentBefore });
        newlineBefore = false;
        commentBefore = false;
    };

    while (index < source.length) {
        const character = source[index];
        if (character === "\n") {
            newlineBefore = true;
            index += 1;
            continue;
        }
        if (character === " " || character === "\t" || character === "\r") {
            index += 1;
            continue;
        }
        if (character === "#") {
            while (index < source.length && source[index] !== "\n") index += 1;
            commentBefore = true;
            continue;
        }
        if (/[A-Za-z_]/.test(character)) {
            const start = index;
            while (index < source.length && /[A-Za-z0-9_]/.test(source[index])) {
                index += 1;
            }
            const value = source.slice(start, index);
            push(KEYWORDS.has(value) ? "keyword" : "identifier", value, start);
            continue;
        }
        if (/[0-9]/.test(character)) {
            const start = index;
            while (index < source.length && /[0-9_.]/.test(source[index])) {
                index += 1;
            }
            push("number", source.slice(start, index), start);
            continue;
        }
        if (character === '"') {
            const start = index;
            index += 1;
            while (index < source.length && source[index] !== '"') {
                if (source[index] === "\\") index += 1;
                index += 1;
            }
            if (index >= source.length) {
                fail("unterminated-string", "string literal has no closing quote", start);
            }
            index += 1;
            push("string", source.slice(start, index), start);
            continue;
        }
        const punctuation = PUNCTUATION.find((candidate) =>
            source.startsWith(candidate, index));
        if (!punctuation) {
            fail("unknown-character", `unknown character ${JSON.stringify(character)}`, index);
        }
        push("punctuation", punctuation, index);
        index += punctuation.length;
    }
    push("end", "", source.length);
    return tokens;
}

// A type is functional when it is a callable domain — `Int -> Int` in ordinary
// source, or the bare marker `Function` that model.mjs and the decision corpus
// already use for the same idea. Both spellings resolve to the same fact, so a
// signature written either way answers the trailing question identically.
function typeIsFunctional(type) {
    return type.kind === "function" || (type.kind === "name" && type.name === "Function");
}

export function finalParameterIsFunctional(signature) {
    const parameters = signature.parameters ?? [];
    if (parameters.length === 0) return false;
    return typeIsFunctional(parameters[parameters.length - 1].type);
}

class Parser {
    constructor(source, options = {}) {
        this.source = source;
        this.tokens = tokenize(source);
        this.position = 0;
        // Signatures the caller declares out of band — the standard library, or
        // an earlier module. Declarations found in this source are added as they
        // are parsed, so a call may reference a callee declared above it.
        this.signatures = new Map();
        for (const signature of options.signatures ?? []) {
            this.signatures.set(signature.name, signature);
        }
        // Depth of trailing-lambda bodies currently open. It exists for one
        // case: `outer(0) fn(x) => x fn(y) => y`. The body is the bare name
        // `x`, so the juxtaposition guard in `parsePostfix` sees a non-call
        // followed by `fn(` and would blame the parentheses rule. The contract
        // has a rule that fits better — "A second trailing lambda is always
        // rejected" — and it can only be applied by the enclosing call, which
        // is still on the stack. Inside a trailing body the guard therefore
        // defers and lets that rule report.
        this.trailingDepth = 0;
    }

    peek(ahead = 0) {
        return this.tokens[Math.min(this.position + ahead, this.tokens.length - 1)];
    }

    next() {
        const token = this.peek();
        if (token.kind !== "end") this.position += 1;
        return token;
    }

    at(value) {
        return this.peek().value === value && this.peek().kind !== "end";
    }

    expect(value, category) {
        const token = this.peek();
        if (token.value !== value || token.kind === "end") {
            fail(category, `expected ${JSON.stringify(value)}, found ${JSON.stringify(token.value)}`,
                token.offset);
        }
        return this.next();
    }

    expectIdentifier(category, what) {
        const token = this.peek();
        if (token.kind !== "identifier") {
            fail(category, `expected ${what}, found ${JSON.stringify(token.value)}`, token.offset);
        }
        return this.next();
    }

    // ---- declarations -----------------------------------------------------

    parseProgram() {
        const declarations = [];
        while (this.peek().kind !== "end") {
            if (!this.at("fn")) {
                fail("unexpected-top-level",
                    `expected a function declaration, found ${JSON.stringify(this.peek().value)}`,
                    this.peek().offset);
            }
            declarations.push(this.parseFunctionDeclaration());
        }
        return { kind: "Program", declarations };
    }

    parseFunctionDeclaration() {
        const start = this.expect("fn", "expected-declaration").offset;
        const name = this.expectIdentifier("expected-declaration-name", "a function name").value;
        const parameters = this.parseParameterList();
        let result = null;
        if (this.at("->")) {
            this.next();
            result = this.parseType();
        }
        const signature = { name, parameters, result, offset: start };
        // Registered before the body is parsed so a directly recursive call can
        // still resolve its own trailing position.
        this.signatures.set(name, signature);
        // `spec/grammar.ebnf`: function_tail ends in ( "=", expression | block ).
        // The expression body is `=`, not `=>` — `=>` is the lambda arrow, and
        // a declaration is not a lambda. Both are parsed here because the
        // ambiguity corpus needs a named declaration to follow a call, and the
        // boundary it checks is `fn` plus an identifier, whichever body form
        // comes after it.
        if (this.at("=")) {
            this.next();
            return {
                kind: "Function", name, parameters, result,
                value: this.parseExpression(), offset: start,
            };
        }
        const body = this.parseBlock();
        return { kind: "Function", name, parameters, result, body, offset: start };
    }

    // parameter = [ ownership-mode ], [ external-label ], internal-name, ":", type
    //
    // The three shapes are told apart by counting the identifiers before `:`,
    // which is the only thing the grammar gives us. One identifier is the
    // internal name. Two is a label and a name — unless the first is an
    // ownership mode, in which case the mode wins and the parameter is
    // unlabelled. That collision is real: `take file: File` can be read both
    // ways, and the contract's own example (`take into file: File`) puts the
    // mode outermost. Modes are therefore reserved in the leading position, and
    // spelling an external label `read`/`edit`/`take` is refused by name rather
    // than resolved by preference.
    parseParameterList() {
        this.expect("(", "expected-parameter-list");
        const parameters = [];
        const externals = new Set();
        while (!this.at(")")) {
            const words = [];
            while (this.peek().kind === "identifier") {
                words.push(this.next());
                if (this.at(":")) break;
            }
            const colon = this.peek();
            if (colon.value !== ":") {
                fail("malformed-parameter",
                    `expected ':' after a parameter name, found ${JSON.stringify(colon.value)}`,
                    colon.offset);
            }
            if (words.length === 0) {
                fail("malformed-parameter", "expected a parameter name", colon.offset);
            }
            if (words.length > 3) {
                fail("malformed-parameter",
                    `a parameter has at most an ownership mode, an external label, and a name; found ${words.length} names`,
                    words[0].offset);
            }
            this.next();

            let mode = null;
            let external = null;
            let internal = null;
            if (words.length === 1) {
                internal = words[0];
            } else if (words.length === 2) {
                if (OWNERSHIP_MODES.has(words[0].value)) {
                    [mode, internal] = words;
                } else {
                    [external, internal] = words;
                }
            } else {
                if (!OWNERSHIP_MODES.has(words[0].value)) {
                    fail("malformed-parameter",
                        `a three-word parameter must start with an ownership mode, found ${JSON.stringify(words[0].value)}`,
                        words[0].offset);
                }
                [mode, external, internal] = words;
            }
            if (external && OWNERSHIP_MODES.has(external.value)) {
                fail("mode-spelled-as-label",
                    `${JSON.stringify(external.value)} is an ownership mode and cannot be an external label`,
                    external.offset);
            }
            if (external && externals.has(external.value)) {
                fail("duplicate-external-label",
                    `external label ${JSON.stringify(external.value)} is declared twice`,
                    external.offset);
            }
            if (external) externals.add(external.value);

            parameters.push({
                mode: mode ? mode.value : "read",
                modeWritten: mode !== null,
                external: external ? external.value : null,
                internal: internal.value,
                type: this.parseType(),
                offset: words[0].offset,
            });

            if (this.at(",")) {
                this.next();
                if (this.at(")")) {
                    fail("trailing-comma", "a parameter list may not end with a comma",
                        this.peek().offset);
                }
                continue;
            }
            break;
        }
        this.expect(")", "expected-parameter-list-close");
        return parameters;
    }

    // type = name [ "[" type { "," type } "]" ] [ "->" type ]
    // A parenthesised head is three different things, and reading it as only
    // the middle one made two normative spellings unparseable:
    //
    //     () -> Int            an empty callable domain
    //     (Int, Int) -> Int    a multi-argument callable domain
    //     (Int -> Int) -> Int  an ordinary grouping around a function type
    //
    // `spec/grammar.ebnf` derives all three, and the first two are live in the
    // repository today — `tests/conformance/syntax/issues_35_47/
    // lambda_argument.kofun` compiles both through Stage 2. Missing them was
    // not cosmetic: `finalParameterIsFunctional` reads the final formal's type
    // to decide whether a trailing lambda may attach, so an unparseable
    // functional type is a signature the trailing rule cannot be applied to.
    // It showed up in this slice's own corpus, which had to declare `fold`
    // with a *unary* combiner while its headline case passes a two-parameter
    // lambda.
    //
    // The grouping case keeps no marker: `formatType` recovers the parentheses
    // structurally, because a function type in domain position always needs
    // them and never needs them anywhere else.
    parseType() {
        let left;
        if (this.at("(")) {
            this.next();
            if (this.at(")")) {
                this.next();
                left = { kind: "tuple", elements: [] };
            } else {
                const first = this.parseType();
                if (this.at(",")) {
                    const elements = [first];
                    while (this.at(",")) {
                        this.next();
                        elements.push(this.parseType());
                    }
                    this.expect(")", "expected-type-close");
                    left = { kind: "tuple", elements };
                } else {
                    this.expect(")", "expected-type-close");
                    left = first;
                }
            }
        } else {
            const name = this.expectIdentifier("expected-type", "a type name");
            left = { kind: "name", name: name.value, arguments: [] };
            if (this.at("[")) {
                this.next();
                for (;;) {
                    left.arguments.push(this.parseType());
                    if (this.at(",")) {
                        this.next();
                        continue;
                    }
                    break;
                }
                this.expect("]", "expected-type-argument-close");
            }
        }
        if (this.at("->")) {
            this.next();
            return { kind: "function", domain: left, result: this.parseType() };
        }
        return left;
    }

    // ---- statements -------------------------------------------------------

    parseBlock() {
        const open = this.expect("{", "expected-block").offset;
        const statements = [];
        while (!this.at("}")) {
            if (this.peek().kind === "end") {
                fail("unterminated-block", "block has no closing '}'", open);
            }
            statements.push(this.parseStatement());
        }
        this.expect("}", "expected-block-close");
        return { kind: "Block", statements, offset: open };
    }

    parseStatement() {
        const token = this.peek();
        if (token.value === "let") {
            this.next();
            const name = this.expectIdentifier("expected-binding-name", "a binding name").value;
            this.expect(":", "expected-binding-type");
            const type = this.parseType();
            this.expect("=", "expected-binding-value");
            return {
                kind: "Let", name, type,
                value: this.parseExpression(),
                offset: token.offset,
            };
        }
        if (token.value === "return") {
            this.next();
            return { kind: "Return", value: this.parseExpression(), offset: token.offset };
        }
        const expression = this.parseExpression();
        // Statements terminate automatically at a newline. Without this the
        // `separate-declaration` boundary would be unreachable: `consume(value)`
        // followed by a newline and `fn next(...)` has to end here, and a parser
        // that kept going would read `fn` as part of the same expression.
        const following = this.peek();
        if (
            following.kind !== "end" &&
            following.value !== "}" &&
            !following.newlineBefore
        ) {
            fail("missing-termination",
                `expected a newline after a statement, found ${JSON.stringify(following.value)}`,
                following.offset);
        }
        return { kind: "Expression", expression, offset: token.offset };
    }

    // ---- expressions ------------------------------------------------------

    // Pipeline is lowest, then comparison, then additive, then multiplicative.
    // The precedence is ordinary; the only thing this file needs from it is
    // that `items |> fold(initial: 0) fn(...)` parses the trailing lambda into
    // `fold` before the pipeline node is built.
    parseExpression() {
        return this.parsePipeline();
    }

    parsePipeline() {
        let left = this.parseComparison();
        while (this.at("|>")) {
            const offset = this.next().offset;
            const right = this.parseComparison();
            left = { kind: "Pipeline", subject: left, call: right, offset };
        }
        return left;
    }

    parseBinary(operators, parseOperand) {
        let left = parseOperand.call(this);
        while (operators.includes(this.peek().value) && this.peek().kind === "punctuation") {
            const operator = this.next();
            left = {
                kind: "Binary",
                operator: operator.value,
                left,
                right: parseOperand.call(this),
                offset: operator.offset,
            };
        }
        return left;
    }

    parseComparison() {
        return this.parseBinary(["==", "!=", "<", ">", "<=", ">="], this.parseAdditive);
    }

    parseAdditive() {
        return this.parseBinary(["+", "-"], this.parseMultiplicative);
    }

    parseMultiplicative() {
        return this.parseBinary(["*", "/", "%"], this.parsePostfix);
    }

    parsePostfix() {
        let node = this.parsePrimary();
        for (;;) {
            if (this.at(".")) {
                this.next();
                const member = this.expectIdentifier("expected-member", "a member name");
                node = { kind: "Member", subject: node, member: member.value, offset: member.offset };
                continue;
            }
            if (this.at("(") && !this.peek().newlineBefore) {
                node = this.parseCall(node);
                continue;
            }
            break;
        }
        // `transaction fn(tx) => commit(tx)` — juxtaposing a lambda onto
        // something that is not a closed call. The contract names this
        // explicitly: "Parentheses MUST remain even when the lambda is the only
        // argument"; `transaction fn(...)` is not valid v1 syntax. Refusing it
        // here, rather than letting the statement layer report a stray `fn`,
        // is the difference between telling the author the rule and telling
        // them a token was unexpected.
        if (
            node.kind !== "Call" &&
            this.trailingDepth === 0 &&
            this.at("fn") &&
            this.peek(1).value === "(" &&
            !this.peek().newlineBefore
        ) {
            fail("trailing-lambda-requires-parentheses",
                "a trailing lambda attaches to a call; write `callee() fn(...)`",
                this.peek().offset);
        }
        return node;
    }

    parsePrimary() {
        const token = this.peek();
        if (token.value === "fn" && this.peek(1).value === "(") return this.parseLambda();
        if (token.kind === "identifier") {
            this.next();
            return { kind: "Name", name: token.value, offset: token.offset };
        }
        if (token.kind === "number" || token.kind === "string") {
            this.next();
            return { kind: "Literal", value: token.value, literal: token.kind, offset: token.offset };
        }
        if (token.value === "(") {
            this.next();
            const inner = this.parseExpression();
            this.expect(")", "expected-group-close");
            return { kind: "Group", expression: inner, offset: token.offset };
        }
        return fail("expected-expression",
            `expected an expression, found ${JSON.stringify(token.value)}`, token.offset);
    }

    // lambda-expression = "fn", "(", [ lambda-parameters ], ")",
    //                     ( "=>", expression | block )
    //
    // Lambda parameters may be bare names or annotated, which is what the
    // corpus writes (`fn(acc, item)`) and what #943 retained. They carry no
    // external labels: a label is a property of a declaration's public
    // interface and a lambda has none.
    parseLambda() {
        const start = this.expect("fn", "expected-lambda").offset;
        this.expect("(", "expected-lambda-parameters");
        const parameters = [];
        while (!this.at(")")) {
            const name = this.expectIdentifier("expected-lambda-parameter", "a lambda parameter name");
            let type = null;
            if (this.at(":")) {
                this.next();
                type = this.parseType();
            }
            parameters.push({ name: name.value, type, offset: name.offset });
            if (this.at(",")) {
                this.next();
                continue;
            }
            break;
        }
        this.expect(")", "expected-lambda-parameters-close");
        if (this.at("=>")) {
            this.next();
            return {
                kind: "Lambda", body: "expression", parameters,
                expression: this.parseExpression(), offset: start,
            };
        }
        if (this.at("{")) {
            return {
                kind: "Lambda", body: "block", parameters,
                block: this.parseBlock(), offset: start,
            };
        }
        return fail("expected-lambda-body",
            `a lambda body is '=>' or a block, found ${JSON.stringify(this.peek().value)}`,
            this.peek().offset);
    }

    // argument-list = positional { "," positional } [ "," labelled { "," labelled } ]
    //               | labelled { "," labelled }
    parseCall(callee) {
        const open = this.expect("(", "expected-call-open").offset;
        const args = [];
        const labels = new Set();
        let sawLabel = false;
        while (!this.at(")")) {
            const token = this.peek();
            const labelled = token.kind === "identifier" && this.peek(1).value === ":";
            if (labelled) {
                this.next();
                this.next();
                if (labels.has(token.value)) {
                    fail("duplicate-label",
                        `label ${JSON.stringify(token.value)} appears twice in one call`,
                        token.offset);
                }
                labels.add(token.value);
                sawLabel = true;
                args.push({
                    label: token.value,
                    expression: this.parseExpression(),
                    offset: token.offset,
                });
            } else {
                if (sawLabel) {
                    fail("positional-after-labelled",
                        "a positional argument may not follow a labelled argument",
                        token.offset);
                }
                args.push({ label: null, expression: this.parseExpression(), offset: token.offset });
            }
            if (this.at(",")) {
                this.next();
                if (this.at(")")) {
                    fail("trailing-comma", "an argument list may not end with a comma",
                        this.peek().offset);
                }
                continue;
            }
            break;
        }
        this.expect(")", "expected-call-close");
        const call = {
            kind: "Call",
            callee,
            arguments: args,
            trailing: null,
            offset: open,
        };
        this.attachTrailingLambda(call);
        return call;
    }

    // The callee's *name* is what resolves a signature. A method call
    // (`items.fold(...)`) resolves on the member, a plain call on the name.
    // Anything else — a call on a computed callee — has no name to resolve and
    // therefore cannot take a trailing lambda, because the grammar refuses to
    // insert one before the signature is known.
    calleeName(callee) {
        if (callee.kind === "Name") return callee.name;
        if (callee.kind === "Member") return callee.member;
        return null;
    }

    attachTrailingLambda(call) {
        const token = this.peek();
        if (token.value !== "fn" || token.kind === "end") {
            // A brace directly after a closed call, on the same line, is the
            // one alternate spelling the contract names and rejects: "There is
            // no brace lambda, receiver lambda, implicit `it`, or alternate
            // anonymous-function form in the trailing position."
            if (token.value === "{" && !token.newlineBefore) {
                fail("trailing-brace-lambda",
                    "there is no brace lambda in v1; the trailing form is `) fn(...)`",
                    token.offset);
            }
            return;
        }
        const after = this.peek(1);
        // `call()` then a newline then `fn named(...)` is two declarations,
        // because `fn` is followed by an identifier rather than `(`. This is
        // decided before the signature is consulted: a named declaration is
        // never a trailing lambda regardless of what the callee needs.
        if (after.kind === "identifier") return;
        if (after.value !== "(") {
            fail("lambda-missing-parameter-list",
                `a lambda is 'fn(' ...; found ${JSON.stringify(after.value)} after 'fn'`,
                after.offset);
        }

        const name = this.calleeName(call.callee);
        const signature = name === null ? null : this.signatures.get(name);
        if (!signature) {
            fail("trailing-callee-unresolved",
                `cannot attach a trailing lambda: the signature of ${name === null ? "this callee" : JSON.stringify(name)} is not known here`,
                token.offset);
        }
        if (!finalParameterIsFunctional(signature)) {
            // The final formal is not functional, so this `fn` is not part of
            // the call. When trivia separates them it begins the next
            // expression or declaration and we simply stop. When nothing
            // separates them the source cannot mean anything else, and saying
            // which rule refused it beats a stray-token report.
            if (token.newlineBefore || token.commentBefore) return;
            return fail("trailing-lambda-not-functional",
                `${JSON.stringify(signature.name)} does not end in a functional parameter, so it takes no trailing lambda`,
                token.offset);
        }
        const finalIndex = signature.parameters.length - 1;
        const finalParameter = signature.parameters[finalIndex];
        const alreadySupplied =
            call.arguments.some((argument) =>
                argument.label !== null && argument.label === finalParameter.external) ||
            // Positional arguments fill the leading slots in order, so the final
            // slot is taken exactly when the positional count reaches it.
            call.arguments.filter((argument) => argument.label === null).length > finalIndex;
        if (alreadySupplied) {
            fail("trailing-lambda-slot-taken",
                `the final parameter of ${JSON.stringify(signature.name)} is already supplied inside the parentheses`,
                token.offset);
        }

        this.trailingDepth += 1;
        call.trailing = this.parseLambda();
        this.trailingDepth -= 1;
        call.signature = signature.name;

        // A second `fn(` is only a second *trailing lambda* when it could have
        // attached to this call. Once the first one is bound the final formal
        // is supplied, so the contract's other clause takes over: "Otherwise
        // `fn` begins the next expression or declaration according to the
        // ordinary grammar." The guard below therefore honours the same trivia
        // rule its sibling does — without it,
        //
        //     outer(0) fn(x) => x
        //     fn(y) => y
        //
        // was refused as a second trailing lambda when it is two statements,
        // which is the shape the corpus already pins as `statement-sequence`.
        const second = this.peek();
        if (
            second.value === "fn" &&
            this.peek(1).value === "(" &&
            !second.newlineBefore &&
            !second.commentBefore
        ) {
            fail("second-trailing-lambda",
                "a call takes at most one trailing lambda",
                second.offset);
        }
    }
}

export function parseProgram(source, options = {}) {
    return new Parser(source, options).parseProgram();
}

// Parses a single expression, which is what the ambiguity corpus is written in.
// The whole input must be consumed: a corpus entry that parses a prefix and
// leaves a tail would otherwise pass while describing something the contract
// never allowed.
export function parseExpression(source, options = {}) {
    const parser = new Parser(source, options);
    const expression = parser.parseExpression();
    const token = parser.peek();
    if (token.kind !== "end") {
        fail("trailing-input",
            `expression ends here but ${JSON.stringify(token.value)} follows`, token.offset);
    }
    return expression;
}

// Parses corpus text that may be either one expression or a sequence of
// statements, and reports which. `consume(value)` followed by `fn next(...)` is
// the case that needs it: it is not one expression, and calling it one would
// erase the boundary the corpus exists to check.
export function parseFragment(source, options = {}) {
    const parser = new Parser(source, options);
    const statements = [];
    while (parser.peek().kind !== "end") {
        if (parser.at("fn") && parser.peek(1).kind === "identifier") {
            statements.push(parser.parseFunctionDeclaration());
            continue;
        }
        statements.push(parser.parseStatement());
    }
    return { kind: "Fragment", statements };
}

// The surface kind a fragment describes, in the vocabulary `corpus.json`
// already uses. Derived from the tree rather than from the text, which is the
// whole point of having a parser: `classifySurface` answers this from a regex
// and cannot see nesting.
export function classifyFragment(fragment) {
    if (fragment.statements.some((statement) => statement.kind === "Function")) {
        return "separate-declaration";
    }
    // Two or more statements is the automatic-termination outcome: a newline
    // ended the first one. Naming it separately from `separate-declaration`
    // matters, because the two boundaries are decided by different rules — one
    // by `fn` being followed by an identifier, the other by the newline alone.
    if (fragment.statements.length !== 1) return "statement-sequence";
    const only = fragment.statements[0];
    const expression = only.kind === "Expression" ? only.expression
        : only.kind === "Let" || only.kind === "Return" ? only.value
            : null;
    if (expression === null) return "ordinary-call";
    if (expression.kind === "Pipeline") {
        return trailingCount(expression) > 0 ? "pipeline-trailing-call" : "pipeline-call";
    }
    return trailingCount(expression) > 0 ? "trailing-call" : "ordinary-call";
}

// Counts attached trailing lambdas anywhere in the tree. A nested trailing call
// contributes, which is what makes `outer(inner() fn(x) => x) fn(y) => y` a
// trailing call by structure rather than by counting `fn(` in the text.
export function trailingCount(node) {
    if (node === null || typeof node !== "object") return 0;
    let total = node.kind === "Call" && node.trailing !== null ? 1 : 0;
    for (const value of Object.values(node)) {
        if (Array.isArray(value)) {
            for (const item of value) total += trailingCount(item);
        } else if (value !== null && typeof value === "object") {
            total += trailingCount(value);
        }
    }
    return total;
}
