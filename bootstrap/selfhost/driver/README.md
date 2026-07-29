# Self-host compiler driver evidence

This directory holds the #622 evidence: the trusted Stage 2 seed
compiles the frozen self-host source `S`
(`bootstrap/stage1/compiler.kofun`) as one ordinary input into a
runnable compiler, through the single source-to-C command

```sh
kofun-stage2 --selfhost-compile INPUT.kofun OUTPUT.c SOURCE-SHA256
```

with no hidden Stage 1/2 fallback: the typed `kofun.selfhost-hir/v1`
document is produced and lowered in memory, a rejected source prints
its stable frontend diagnostic and writes nothing, and there is no
filename or hash special case.

- `S.c` — the deterministic C11 emitted for the exact frozen `S`
  digest; `check-compiler-driver.sh` re-derives it byte for byte, twice.
- `corpus_answer.kofun` / `.c` / `.stdout` — the success corpus: the
  compiler built from `S.c` compiles this Core input to C that is
  byte-identical to what the audited Stage 1 seed emits, prints the
  same stdout, and the compiled program reproduces the pinned output.
- `corpus_bool.kofun` / `.c` / `.stdout` — the comparison/Bool corpus:
  all six comparisons, Bool literals and bindings, `!`, `&&` and `||`
  lower identically through both seeds. Its skipped short-circuit operands
  contain division by zero, so successful execution proves they were not
  evaluated.
- `corpus_branch.kofun` / `.c` / `.stdout` — the nested-block corpus: `if`,
  `else if` chains, `else`, blocks nested three deep, and a name rebound after
  the block that first bound it closed all lower identically through both seeds.
  Its skipped `else if` condition and short-circuited `||` operand are both
  `1 // 0`, so successful execution proves neither was evaluated.
- `corpus_loop.kofun` / `.c` / `.stdout` — the loop corpus: `while` and
  `for NAME in A .. B` loops nested in each other and in branches, a range whose
  ends are evaluated once into the enclosing scope, and a bound name rebound
  after its block closed all lower identically through both seeds. The body of a
  false `while` and of an empty range each contain `1 // 0`, so successful
  execution proves neither was entered.
- `corpus_text.kofun` / `.c` / `.stdout` — the Text corpus: escaped literals,
  concatenation, equality/inequality, Text bindings and Text printing lower
  identically through both seeds. Its `(+ || ==)` literal pins that operator
  and parenthesis bytes inside quotes remain literal bytes, and the checked-in
  C shows the allocation-list runtime that is emitted only when Text is used.
- `corpus_text_equality_only.kofun` / `.c` / `.stdout` — compares two Text
  literals without creating a Text-typed local, pinning that the conditional
  runtime is selected from Text operations as well as Text storage.
- `corpus_list_text.kofun` / `.c` / `.stdout` — constructs `List[Text]` with
  `chars`, observes it with `len`, and indexes both List[Text] and Text through
  both compilers. `k字n` pins the profile's byte-oriented result of `5`.
- `corpus_builtins.kofun` / `.c` / `.input` / `.output` / `.stdout` — invokes
  all 15 profile builtins through both seeds, then executes argv decoding,
  whole-file read/write, both `len` overloads, Text search/slice/trim,
  character predicates, Unicode validation, and stdout against pinned bytes.
  Its `is_xid_continue` input is ASCII so the known seed/source deviation is
  preserved rather than silently closed on one side.
- `corpus_builtin_rejects.tsv` — one wrong-arity and one wrong-type source line
  for each of the 15 builtins. The two gates expand all 30 rows into full
  sources, require identical nonzero refusal from both seeds, and require that
  no C artifact is written.
- `corpus_trap_list_index.kofun` / `.stderr` and
  `corpus_trap_text_index.kofun` / `.stderr` — well-typed out-of-bounds
  programs compile identically, then exit 1 with the exact receiver-specific
  `R010` diagnostic and no stdout.
- `corpus_reject.kofun` / `.stdout` — the failure corpus: both
  compilers refuse an out-of-Core source with the same diagnostic bytes
  and write nothing.
- `corpus_reject_bool_*.kofun`, `corpus_reject_logical_int.kofun`,
  `corpus_reject_not_int.kofun`, and `corpus_reject_single_pipe.kofun` — the
  typed boundary: Bool in arithmetic or `print`, explicit annotation mismatch
  or an unsupported annotation, reserved Bool-literal binding names, ordered
  Bool, logical Int, `!Int`, and a lone `|` all exit nonzero and write no C
  through either seed.
- `corpus_reject_branch_*.kofun`, `corpus_reject_else_*.kofun`,
  `corpus_reject_unclosed_block.kofun` and
  `corpus_reject_extra_block_end.kofun` — the block boundary: an Int condition,
  a name read after its block closed, a `let` shadowing an outer binding, an
  `else` with no `if`, a second `else` in one chain, an unclosed block, and a
  `}` that closes nothing are all refused the same way by both seeds.
- `corpus_reject_while_condition.kofun`, `corpus_reject_range_bound.kofun`,
  `corpus_reject_range_separator.kofun`, `corpus_reject_loop_shadow.kofun`,
  `corpus_reject_loop_scope.kofun`, `corpus_reject_loop_assignment.kofun` and
  `corpus_reject_else_after_loop.kofun` — the loop boundary: an Int `while`
  condition, a Bool range end, a range written `0..3` without the spaced
  separator, a bound name shadowing a visible binding, a bound name read after
  its loop closed, an assignment to a bound name, and an `else` attached to a
  loop are all refused by both seeds. Each differs from an accepted source by
  exactly the element it pins. Assignment is not a Core statement yet, so the
  assignment fixture is refused as an unknown structural line; it is checked in
  now so the mutable-local slice cannot make a loop bound assignable silently.
- `corpus_reject_text_*.kofun` — the Text boundary: `Text + Int`,
  `Int + Text`, both operand orders of mixed equality, an unsupported escape,
  and ordered Text comparison all fail identically and write no C.
- `corpus_reject_index_int.kofun` and
  `corpus_reject_index_kind.kofun` — indexing rejects an `Int` receiver and a
  non-`Int` index identically through both seeds and writes no C.

The gate also checks path independence (the same relative input
compiled from two different directories emits identical C), determinism
across repeated runs, and bounded I/O failure (a missing input panics
with the runtime's explicit message, exits 1, and preserves the
previous output bytes).

The host boundary in generated programs is the audited runtime shim documented
in `../c11/README.md`: bounded argument decoding through `kofun_rt_args` (the
program name is excluded, exactly like the trusted seed), whole-file
`read_text`/`write_text` with explicit `R010` failures, and `print` as
line-buffered stdout. Known deliberate deviation from the Stage 1 seed,
unchanged from #620: `is_xid_continue` consults the real Unicode tables rather
than the seed's historical `>= 0x80` approximation, so the corpus differential
stays within ASCII inputs.
