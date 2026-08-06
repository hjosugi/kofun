// The focused gate for issue #880: the parser and canonical formatter of
// `spec/syntax/call-arguments-v1.md`, driven by `surface-corpus.json`.
//
// `check.mjs` next door is the *decision* gate — it holds the document to what
// #625 accepted. This one is the *surface* gate: it holds an implementation to
// the grammar, the ambiguity boundary, and the canonical shape. Keeping them
// apart is deliberate. The decision gate must stay runnable when no parser
// exists, which was true until this change and will be true again for the next
// contract that lands ahead of its implementation.
//
// Every assertion names what failed, per #814 and #838: an aborting gate that
// prints nothing costs a contributor the same debugging session every time.

import assert from "node:assert/strict";
import fs from "node:fs";

import {
    SurfaceError,
    classifyFragment,
    parseFragment,
    parseProgram,
    trailingCount,
} from "./parser.mjs";
import {
    formatDeclaration,
    formatFragment,
    formatType,
} from "./format.mjs";

const corpus = JSON.parse(
    fs.readFileSync(new URL("./surface-corpus.json", import.meta.url), "utf8"));
assert.equal(corpus.schema, "kofun.call-arguments-surface/v1",
    `surface corpus declares schema ${JSON.stringify(corpus.schema)}; expected kofun.call-arguments-surface/v1`);

// ---------------------------------------------------------------------------
// The signature environment is itself the first piece of evidence: it is built
// by parsing ordinary Kofun declarations with the parser under test, so if
// declaration external labels did not parse, nothing below could run at all.

const program = parseProgram(corpus.declarations);
const signatures = program.declarations;
assert.ok(signatures.length > 0,
    "surface corpus declared no signatures, so every trailing case would resolve vacuously");

const byName = new Map(signatures.map((signature) => [signature.name, signature]));
const options = { signatures };

for (const expected of corpus.declarationSurface ?? []) {
    const declaration = byName.get(expected.function);
    assert.ok(declaration,
        `declaration surface case ${JSON.stringify(expected.name)} names ${JSON.stringify(expected.function)}, which the corpus declarations do not declare`);
    const actual = declaration.parameters.map((parameter) => ({
        mode: parameter.mode,
        external: parameter.external,
        internal: parameter.internal,
        type: formatType(parameter.type),
    }));
    assert.deepEqual(actual, expected.parameters,
        `declaration surface case ${JSON.stringify(expected.name)}: parsed parameters differ from the contract's reading of ${JSON.stringify(expected.function)}`);
    const signature = formatDeclaration({ ...declaration, body: null });
    assert.equal(signature, expected.canonical,
        `declaration surface case ${JSON.stringify(expected.name)}: canonical signature is ${JSON.stringify(signature)}`);
}
console.log(
    `call-arguments-surface: ${(corpus.declarationSurface ?? []).length} declaration shapes parse with mode, external label, and internal name distinct: PASS`);

// ---------------------------------------------------------------------------
// Accepted surface: the kind is read off the tree, the trailing count is read
// off the tree, and the canonical text is what the formatter prints.

const requiredAcceptFields = ["name", "source", "kind", "trailing"];
for (const testCase of corpus.accept) {
    for (const field of requiredAcceptFields) {
        assert.ok(Object.hasOwn(testCase, field),
            `accept case ${JSON.stringify(testCase.name ?? "<unnamed>")} is missing the ${field} expectation, so it would assert nothing`);
    }

    let fragment;
    try {
        fragment = parseFragment(testCase.source, options);
    } catch (error) {
        assert.fail(
            `accept case ${JSON.stringify(testCase.name)} did not parse: ${error.message}\n  source: ${JSON.stringify(testCase.source)}`);
    }

    const kind = classifyFragment(fragment);
    assert.equal(kind, testCase.kind,
        `accept case ${JSON.stringify(testCase.name)}: parsed as ${kind}, contract says ${testCase.kind}\n  source: ${JSON.stringify(testCase.source)}`);

    const attached = fragment.statements.reduce(
        (total, statement) => total + trailingCount(statement), 0);
    assert.equal(attached, testCase.trailing,
        `accept case ${JSON.stringify(testCase.name)}: ${attached} trailing lambdas attached, contract says ${testCase.trailing}\n  source: ${JSON.stringify(testCase.source)}`);

    if (testCase.format === false) {
        assert.ok((testCase.reason ?? "").length >= 20,
            `accept case ${JSON.stringify(testCase.name)} opts out of formatting without saying why; an unexplained opt-out is how a formatter bug hides`);
        assert.ok(!Object.hasOwn(testCase, "canonical"),
            `accept case ${JSON.stringify(testCase.name)} both opts out of formatting and states a canonical form; one of the two is wrong`);
        continue;
    }

    assert.ok(Object.hasOwn(testCase, "canonical"),
        `accept case ${JSON.stringify(testCase.name)} states no canonical form and does not opt out with "format": false`);
    const formatted = formatFragment(fragment);
    assert.equal(formatted, testCase.canonical,
        `accept case ${JSON.stringify(testCase.name)}: formatter produced\n${formatted}\n  contract says\n${testCase.canonical}`);

    // Canonical output must be a fixed point. A formatter whose output parses
    // to something else, or reformats differently on a second pass, is not
    // canonical — and running it over a repository would keep producing diffs.
    const reparsed = parseFragment(formatted, options);
    assert.equal(formatFragment(reparsed), formatted,
        `accept case ${JSON.stringify(testCase.name)}: formatting is not a fixed point; a second pass changed the text`);
    assert.equal(classifyFragment(reparsed), testCase.kind,
        `accept case ${JSON.stringify(testCase.name)}: the formatted text no longer parses as ${testCase.kind}`);
    assert.equal(
        reparsed.statements.reduce((total, statement) => total + trailingCount(statement), 0),
        testCase.trailing,
        `accept case ${JSON.stringify(testCase.name)}: the formatted text attached a different number of trailing lambdas`);
}
console.log(
    `call-arguments-surface: ${corpus.accept.length} accepted forms parse, classify, and format to a fixed point: PASS`);

// The prohibitions are checked over every canonical string the gate produced,
// not just asserted case by case: the formatter must never emit a brace lambda,
// a receiver lambda, or an implicit parameter anywhere.
const emitted = corpus.accept
    .filter((testCase) => testCase.format !== false)
    .map((testCase) => testCase.canonical)
    .concat((corpus.width ?? []).map((testCase) => testCase.canonical))
    .join("\n");
assert.ok(!/\)\s*\{/.test(emitted.replace(/\)\s+fn\([^)]*\)\s\{/g, ")"))
    , "the formatter emitted a brace directly after a call's parentheses, which is the brace-lambda spelling v1 does not have");
assert.ok(!/\bit\b/.test(emitted),
    "the formatter emitted an implicit `it` parameter, which v1 does not have");
// Every `fn` the formatter emits is either a named declaration — `fn next(` —
// or an anonymous lambda, and an anonymous one is always `fn(` with no space.
// `fn {`, `fn =>`, and `fn it` are the spellings this rules out.
for (const match of emitted.matchAll(/\bfn(\s*)(.)/g)) {
    const [, gap, following] = match;
    if (/[A-Za-z_]/.test(following)) {
        assert.equal(gap, " ",
            `the formatter emitted a named declaration as ${JSON.stringify(`fn${gap}${following}`)}; a declaration is \`fn \` and one name`);
        continue;
    }
    assert.equal(following, "(",
        `the formatter emitted \`fn\` followed by ${JSON.stringify(following)}; every canonical lambda is \`fn(\``);
    assert.equal(gap, "",
        "the formatter put a space between `fn` and its parameter list; the canonical anonymous lambda is `fn(`");
}
assert.ok(/\)\sfn\(/.test(emitted),
    "no canonical output contains `) fn(`, so the one-space trailing rule was never exercised");
console.log(
    "call-arguments-surface: canonical output is `) fn(` with one space, and no brace lambda, receiver lambda, or implicit parameter: PASS");

// ---------------------------------------------------------------------------
// Refusals. Each names its category, and a category that no longer fires is a
// silently widened parser.

function expectRejected(testCase, parse) {
    let result;
    try {
        result = parse();
    } catch (error) {
        assert.ok(error instanceof SurfaceError,
            `reject case ${JSON.stringify(testCase.name)} failed with ${error.name}, not a SurfaceError: ${error.message}`);
        assert.equal(error.category, testCase.category,
            `reject case ${JSON.stringify(testCase.name)}: refused as ${error.category}, contract says ${testCase.category}\n  source: ${JSON.stringify(testCase.source)}`);
        assert.ok(Number.isInteger(error.offset) && error.offset >= 0,
            `reject case ${JSON.stringify(testCase.name)}: ${error.category} carries no source offset, so no editor could point at it`);
        return;
    }
    assert.fail(
        `reject case ${JSON.stringify(testCase.name)} was accepted; the contract requires ${testCase.category}\n  source: ${JSON.stringify(testCase.source)}\n  parsed: ${JSON.stringify(result).slice(0, 200)}`);
}

for (const testCase of corpus.reject) {
    expectRejected(testCase, () => parseFragment(testCase.source, options));
}
for (const testCase of corpus.rejectDeclarations) {
    expectRejected(testCase, () => parseProgram(testCase.source, options));
}

const categories = new Set(
    corpus.reject.concat(corpus.rejectDeclarations).map((testCase) => testCase.category));
for (const required of [
    // The six bullets of the contract's "Ambiguity boundary" section that state
    // a refusal, plus the two declaration-shape rules the grammar implies.
    "trailing-lambda-requires-parentheses",
    "trailing-brace-lambda",
    "second-trailing-lambda",
    "trailing-lambda-slot-taken",
    "trailing-lambda-not-functional",
    "trailing-callee-unresolved",
    "positional-after-labelled",
    "duplicate-label",
    "duplicate-external-label",
    "mode-spelled-as-label",
]) {
    assert.ok(categories.has(required),
        `the ambiguity corpus no longer covers ${required}; the contract states that boundary and nothing else checks it`);
}
console.log(
    `call-arguments-surface: ${corpus.reject.length + corpus.rejectDeclarations.length} refusals name a stable category and a source offset: PASS`);

// ---------------------------------------------------------------------------
// The decision gate next door classifies the same surface with a regex over
// normalised text. That is enough for what it checks and it is not a parser, so
// the two can drift: a regex cannot see nesting, and it answers on token shape
// where the grammar answers on a resolved signature. Binding them here means a
// change to either that makes them disagree fails, instead of leaving the
// sibling gate quietly describing a different language.
//
// Every entry is cross-checked; none is skipped. That matters, because a skip
// list is where a disagreement would go to hide.
const decision = JSON.parse(
    fs.readFileSync(new URL("./corpus.json", import.meta.url), "utf8"));
for (const testCase of decision.surface) {
    let fragment;
    try {
        fragment = parseFragment(testCase.source, options);
    } catch (error) {
        assert.fail(
            `decision corpus case ${JSON.stringify(testCase.name)} does not parse, so the two gates describe different languages: ${error.message}\n  source: ${JSON.stringify(testCase.source)}`);
    }
    assert.equal(classifyFragment(fragment), testCase.kind,
        `decision corpus case ${JSON.stringify(testCase.name)}: the parser reads it as ${classifyFragment(fragment)}, classifySurface in model.mjs says ${testCase.kind}`);
}
console.log(
    `call-arguments-surface: all ${decision.surface.length} decision-corpus cases parse and agree with classifySurface: PASS`);

// ---------------------------------------------------------------------------
// Width. "Expression lambdas stay on the same line when they fit" is only a
// rule if something checks what happens when they do not.

for (const testCase of corpus.width) {
    const fragment = parseFragment(testCase.source, options);
    const formatted = formatFragment(fragment, testCase.columns);
    assert.equal(formatted, testCase.canonical,
        `width case ${JSON.stringify(testCase.name)} at ${testCase.columns} columns produced\n${formatted}\n  contract says\n${testCase.canonical}`);
    // What the contract fixes is where the break goes, not that every line
    // fits. Wrapping an over-long *argument list* would be a second formatting
    // decision — one this contract does not state and #880 does not own — so a
    // head that still overruns is correct here and is asserted to stay on one
    // line with its `) fn(` intact. That is the property a reader depends on:
    // the lambda is visibly attached to the call above it.
    const [head] = formatted.split("\n");
    assert.ok(head.includes(") fn("),
        `width case ${JSON.stringify(testCase.name)}: the first line lost \`) fn(\`, so the attachment is no longer visible: ${JSON.stringify(head)}`);
    assert.ok(formatted.includes("\n"),
        `width case ${JSON.stringify(testCase.name)} produced one line at ${testCase.columns} columns; nothing about wrapping was exercised`);
}
console.log(
    `call-arguments-surface: ${corpus.width.length} width cases wrap after \`=>\` and never rewrite a block lambda: PASS`);

// ---------------------------------------------------------------------------
// The contract sentences this gate implements. If one is reworded the gate goes
// red, so the code and the document cannot drift apart quietly.

// Whitespace is normalised first, so a sentence that the document wraps across
// two lines still matches. Without it these assertions would fail on a pure
// reflow, which teaches contributors to delete them rather than to keep them.
const doc = fs
    .readFileSync(new URL("../call-arguments-v1.md", import.meta.url), "utf8")
    .replace(/\s+/g, " ");
for (const required of [
    "The grammar never inserts a trailing lambda before overload resolution",
    "Parentheses MUST remain even when the lambda is the only argument",
    "A second trailing lambda is always rejected",
    "Nested trailing calls associate with the nearest preceding unresolved call",
    "Canonical formatting keeps all ordinary arguments inside parentheses",
    "Block lambdas use the existing block formatter and are not rewritten",
]) {
    assert.ok(doc.includes(required),
        `spec/syntax/call-arguments-v1.md no longer contains ${JSON.stringify(required)}, which this gate implements`);
}
console.log(
    "call-arguments-surface: the six contract sentences this gate implements are still in the document: PASS");
