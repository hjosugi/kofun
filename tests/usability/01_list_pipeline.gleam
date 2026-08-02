//// Comparison for corpus item 1: a pure list pipeline with map, filter,
//// and fold.
////
//// Language: Gleam. Chosen because #624 cites Gleam's pipelines and
//// subject-first APIs (https://tour.gleam.run/functions/pipelines/) as the
//// evidence to evaluate, and this is the row where that evidence applies
//// directly.
////
//// NOT COMPILED BY THE GATE. No `gleam` toolchain is present in this
//// repository's CI image, so tests/usability/check.sh reports this file as
//// skipped by name rather than pretending to have run it. It is written to
//// be read against 01_list_pipeline.kofun.
////
//// What to compare:
////
////   * `|>` threads the list through three stages in the order they run,
////     with no intermediate names invented only to keep the reading order.
////     The Kofun file needs three `let` bindings to get the same order, or
////     one inside-out nested expression to avoid them.
////   * `list.filter`, `list.map`, and `list.fold` are ordinary library
////     functions in `gleam/list`, not compiler builtins. A Gleam user can
////     write a fourth one; a Kofun user cannot (corpus item 5).
////   * Neither language requires parameter type annotations on these
////     lambdas. Kofun's file writes `fn(value: Int) => ...` because the
////     checked-in conformance corpus does, but `fn(value) => value > 6`
////     compiles and runs identically on the native backend. This row is a
////     tie, and the rubric records it as one — the corpus is evidence, so
////     a measure that does not discriminate has to say so.

import gleam/int
import gleam/io
import gleam/list

pub fn main() {
  [3, 12, 7, 5]
  |> list.filter(fn(value) { value > 6 })
  |> list.map(fn(value) { value * 2 })
  |> list.fold(0, fn(sum, value) { sum + value })
  |> int.to_string
  |> io.println
}
