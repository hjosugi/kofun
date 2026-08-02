// Comparison for corpus item 4: an ADT, a record, and an exhaustive match.
//
// Language: Kotlin. Chosen because #624's ergonomics goal is stated as "easy
// like Kotlin/Gleam", and this row is where Kotlin's sealed hierarchies and
// data classes line up feature-for-feature with Kofun's ADTs and records.
//
// NOT COMPILED BY THE GATE. No `kotlinc` is present in this repository's CI
// image, so tests/usability/check.sh reports this file as skipped by name
// rather than pretending to have run it. Read it against
// 04_adt_record_match.kofun; both print 10, 7, 0.
//
// What to compare:
//
//   * Exhaustiveness costs more to obtain here. `when` is only checked for
//     exhaustiveness when it is used as an expression or when the subject is
//     a sealed type used in an expression position; a statement `when` over
//     the same type compiles with a missing branch and does nothing at
//     runtime. Kofun's `match` is always checked. That is a real point in
//     Kofun's favour and rubric.md scores it under "hidden control flow".
//   * Kotlin needs three modifiers Kofun does not have a counterpart for:
//     `sealed`, `data`, and `object`. Kofun writes `type Shape = | ... ` and
//     `type Extent = { ... }`, which is less to learn.
//   * Kotlin infers the constructor's type at the use site. Kofun needs
//     `let circle: Shape = Circle(5)`; without the annotation the binding is
//     refused (E2S32).
//   * Kotlin's `Extent(width = 10, filled = true)` named arguments are
//     optional. Kofun's labelled construction is mandatory, which is one
//     canonical spelling instead of two — a guardrail #624 asks for.

sealed interface Shape

data class Circle(val radius: Int) : Shape

data class Square(val side: Int) : Shape

data object Empty : Shape

data class Extent(val width: Int, val filled: Boolean)

// `when` as an expression, so the compiler requires every branch.
fun extentOf(shape: Shape): Int = when (shape) {
    is Circle -> shape.radius * 2
    is Square -> shape.side
    Empty -> 0
}

fun describe(extent: Extent): Int = if (extent.filled) extent.width else 0

fun main() {
    println(describe(Extent(width = extentOf(Circle(5)), filled = true)))
    println(describe(Extent(width = extentOf(Square(7)), filled = true)))
    println(describe(Extent(width = extentOf(Empty), filled = false)))
}
