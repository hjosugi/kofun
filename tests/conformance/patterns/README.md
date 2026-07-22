# General Pattern syntax tree checkpoint

`run.sh` exercises the syntax-only `kofun-pattern-tree/v1` section emitted by
the canonical Stage 2 frontend and its focused `--parse-patterns` mode. The
tree preserves wildcard, Bool/null/Int literal, unresolved name, constructor,
or-pattern, and parenthesized nodes; constructor delimiters, payload commas,
or separators, and every node span remain byte based and source ordered. A
`LiteralPattern` record stores both `literal_kind` and the exact token spelling,
so `true`/`false` and distinct accepted Int tokens such as `0`, `42`, and `4_2`
remain distinguishable from the tree alone. Expression syntax defines `-42` as
unary minus plus an Int token, so it is explicitly rejected as a Pattern token
rather than being mislabeled as one literal.

This checkpoint performs no name resolution, arity/type checking,
exhaustiveness, binding analysis, or runtime lowering. `E2S58` creates an
`ErrorPattern` and synchronizes at `=>`, comma, `)`, or `}`. Focused mode keeps
the recovered tree so a later arm is observable; normal compilation remains
transactional and leaves no requested artifacts after a pattern syntax error.
After a complete Pattern, only the arm arrow or an `if` guard may follow;
adjacent atomic tokens roll the arm back to one `ErrorPattern` instead of being
silently skipped. When nested traversal makes textual records differ from
source order, the reported first diagnostic is the smallest source start byte
(stable record order breaks ties).
An arm-level recovery comma is never crossed while searching for `=>`; the
malformed arm records `arrow=-1`, consumes that comma, and resumes at the next
independent arm.

The parser accepts a maximum tree depth of 32 and 256 Pattern nodes per
compilation. The gate executes each boundary and its one-over rejection. A
node-budget failure is fatal for the remaining Pattern scan: the failing arm
retains its one `ErrorPattern`, while later arms/matches are not emitted and
cannot reuse that occurrence ID with an unrelated span.
Zero-field constructors use the bare `NamePattern` spelling; `Missing()` is
rejected to stay aligned with the #328 constructor syntax.
