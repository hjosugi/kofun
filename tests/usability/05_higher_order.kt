// Comparison for corpus item 5: a higher-order API with two ordinary
// arguments and one callback.
//
// Language: Kotlin. Chosen because #624 cites Kotlin's higher-order and
// trailing lambdas (https://kotlinlang.org/docs/lambdas.html) as the
// evidence to evaluate, and because this row is exactly the question #625
// and its slices #880/#881/#882 own. #909 does not decide that question;
// this file is the evidence a decision would be made against.
//
// NOT COMPILED BY THE GATE. No `kotlinc` is present in this repository's CI
// image, so tests/usability/check.sh reports this file as skipped by name
// rather than pretending to have run it.
//
// What to compare: the Kofun file does not compile. Not "compiles less
// prettily" — a function-typed parameter cannot be declared at all, on any
// frontend, and the diagnostic does not say so. It says
//
//     error[E2S17]: Core function `accumulate` expects 3 arguments, got -1
//
// which reports an arity it computed from a parameter list it failed to
// parse. That is the worst diagnostic in this corpus.
//
// Two Kotlin spellings are shown below because #624 warns against "multiple
// overlapping spellings" and #625's decision is precisely whether to adopt
// the second one. Kotlin has both; a reader has to know that
// `accumulate(1, 2) { ... }` and `accumulate(1, 2, { ... })` are the same
// call. That cost is real, and it is the argument against trailing lambdas,
// not for them.

fun accumulate(start: Int, step: Int, combine: (Int, Int) -> Int): Int {
    val first = combine(start, step)
    return combine(first, step)
}

fun main() {
    // Ordinary argument position.
    println(accumulate(1, 2, { left, right -> left + right }))

    // Trailing-lambda position. Same call, different spelling.
    println(accumulate(1, 2) { left, right -> left + right })
}
