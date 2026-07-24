export const STEPS = Object.freeze([
  {
    id: "first-result",
    eyebrow: "1 · First five minutes",
    title: "Make Kofun-kun move",
    intro:
      "Press Run. The two printed integers become a colour and a finish line, so your first program changes something you can see.",
    exercise:
      "Change colour to 320 and distance to 84, then run it again. Any whole numbers work.",
    source: `# First output: a colour and a place to move to.
fn main() {
    let colour: Int = 260 - 20
    let distance: Int = 40 + 28
    print(colour)
    print(distance)
}`,
    expected: ["240", "68"],
  },
  {
    id: "names",
    eyebrow: "2 · One idea",
    title: "Give a number a useful name",
    intro:
      "A let binding names a value. Kofun works out the Int type here; the explicit : Int form is also accepted.",
    exercise:
      "Make laps equal 4 and points_per_lap equal 25. Can you print 100 without typing 100 in print?",
    source: `fn main() {
    let laps = 3
    let points_per_lap = 20
    print(laps * points_per_lap)
}`,
    expected: ["60"],
  },
  {
    id: "ownership-bug",
    eyebrow: "3 · The bug before the rules",
    title: "Stop two buyers taking one ticket",
    intro:
      "Imagine two buyers both edit the last ticket. Shared mutation can let both succeed. Full Kofun prevents the second edit because only one edit permission can exist; this arithmetic Core models the safe outcome without pretending to check ownership yet.",
    exercise:
      "There was one ticket: print one winner and one refusal. Edit the two named values so the total handled requests stays 2.",
    source: `# Full Kofun's edit permission prevents a second winner.
# This browser Core can run only the resulting arithmetic model.
fn main() {
    let winners = 1
    let refused = 1
    print(winners)
    print(refused)
    print(winners + refused)
}`,
    expected: ["1", "1", "2"],
    ownership: {
      bug: "Without exclusive access: buyer A sees 1, buyer B sees 1, both sell it.",
      prevention:
        "With edit: buyer A receives the sole edit permission. Buyer B cannot edit until it returns.",
      rules: [
        "read borrows a value without changing it",
        "edit grants one temporary, exclusive mutation",
        "take transfers ownership; the old name can no longer be used",
      ],
    },
  },
  {
    id: "checked-math",
    eyebrow: "4 · Useful failure",
    title: "Let a failure explain itself",
    intro:
      "Arithmetic is checked at runtime. Try the program, then repair the zero divisor. The diagnostic is output too.",
    exercise:
      "Change divisor so the program prints -4. Kofun's // is floor division, including for negative numbers.",
    source: `fn main() {
    let divisor = 3 - 3
    print(-7 // divisor)
}`,
    expectedError: "error[R010]: operator `//` failed: division by zero",
  },
]);
export const GUIDES = Object.freeze([
  {
    id: "python",
    name: "Python",
    transfers: "Readable expressions, # comments, // floor division, and % signs follow familiar rules.",
    surprise: "Bindings are statically typed and future full Kofun makes access mode explicit with read/edit/take.",
    worse: "Kofun currently has a tiny ecosystem, and this browser target cannot run strings, lists, loops, classes, or Python packages.",
    from: "total = laps * 20\nprint(total)",
    to: "let total = laps * 20\nprint(total)",
  },
  {
    id: "typescript",
    name: "TypeScript",
    transfers: "let bindings, static checks, and a compile step will feel familiar.",
    surprise: "Int is an exact signed 64-bit integer here, not JavaScript's Number; ownership is a language check rather than a lint convention.",
    worse: "There is no browser/DOM standard library yet. The tour host, not Kofun code, turns printed integers into motion.",
    from: "const total: number = laps * 20;\nconsole.log(total);",
    to: "let total: Int = laps * 20\nprint(total)",
  },
  {
    id: "go",
    name: "Go",
    transfers: "Small complete programs, explicit main, simple tooling, and direct compilation are shared priorities.",
    surprise: "Kofun's planned ownership vocabulary tracks access explicitly; // means floor division while / truncates in this bounded Core.",
    worse: "Kofun has no production goroutines, standard library, package ecosystem, or compatibility promise today.",
    from: "total := laps * 20\nfmt.Println(total)",
    to: "let total = laps * 20\nprint(total)",
  },
  {
    id: "rust",
    name: "Rust",
    transfers: "Checked arithmetic goals, affine ownership, static native binaries, and explicit mutation have common ground.",
    surprise: "Kofun says read/edit/take at boundaries instead of exposing Rust's borrow/lifetime syntax.",
    worse: "The implemented checker is not yet a Rust alternative: browser ownership, Text, records, traits, generics, and reclamation are missing.",
    from: "let total: i64 = laps * 20;\nprintln!(\"{total}\");",
    to: "let total: Int = laps * 20\nprint(total)",
  },
]);
