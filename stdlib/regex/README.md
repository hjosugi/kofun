# Regex checkpoint

This directory defines the first platform-independent Kofun regular-expression
API. It is a deliberately small reference checkpoint, not a claim that the
generated Regex lifecycle issue is complete.

## Regex v1 surface

`regex_compile` accepts Unicode-scalar literals, `.` for any scalar, postfix
`*`, `^` only at the start, and `$` only at the end. A backslash quotes the next
scalar. The other familiar operators (`+`, `?`, groups, alternation, classes,
and counted repetition) return `UnsupportedOperator`; malformed uses of the v1
operators return offset-bearing typed errors.

`regex_find` uses leftmost-first search. At a fixed start, `*` is greedy.
`RegexMatch.start` and `.end` are zero-based Unicode-scalar offsets and `end` is
exclusive. `regex_find_from` makes repeated scanning explicit and rejects start
offsets outside `0..len(chars(input))` with `null`. Matching is pure and has no
locale, encoding, clock, filesystem, or process-global dependency.

## Costs and ownership

Compilation takes `O(m)` time and owns a token list of `O(m)` scalars. The
readable reference matcher uses recursive backtracking: with `q` repetitions it
can take `O(m * (n + 1)^q)` time in the worst case and `O(m + n)` stack space.
That bound is part of this checkpoint; it must not be presented as a
linear-time engine. Compiled values and returned spans are immutable values.

The executable tests are audited Int-Core projections because the active
compiler cannot yet lower the canonical records, ADTs, nullable results, lists,
or Unicode text operations. The full C11 projection covers literals, wildcard,
repetition, anchors, leftmost/greedy spans, and distinct malformed-pattern
statuses. A compact projection covers the core matching rules differentially
through C11 and direct x86-64; it remains below that backend's current one-page
function-Core image boundary.

## Remaining lifecycle work

Character classes, alternation, groups and captures, bounded and non-greedy
quantifiers, a linear-time NFA engine, replacement/splitting, iterators,
resource limits, Unicode property tables, fuzz/property suites, benchmarks,
foreign-runtime compatibility, IDE/debugger views, and stabilization remain
open. The v1 serialized representation is only its source pattern; no compiled
form is stable or portable.
