# Issue triage and refinement

Status: working repository policy.

This document defines how Kofun's GitHub issues are classified and refined.
It is intended to make the open issue set usable by implementers, not to make
the number of issues itself a progress metric. Live issue counts change and
must not be copied into this document.

## Sources of truth

The issue tracker and the generated backlog serve different purposes:

| Source | Authority |
|---|---|
| Executable gates and current source | What the compiler actually supports |
| [`MVP_IMPLEMENTED.md`](MVP_IMPLEMENTED.md) | Concise status of active, verified capabilities |
| [`README.md`](../README.md) | Current user-facing bootstrap status and commands |
| [`ROADMAP.md`](ROADMAP.md) | Milestone outcomes and exit criteria |
| Design and specification documents under `docs/` and `spec/` | Intended contracts and target design |
| GitHub issues | Work selection, discussion, and completion evidence |
| [`rfcs/index.json`](../rfcs/index.json) | Durable public semantic decisions, and whether each is implemented |
| [`release/claims.json`](../release/claims.json) | Published capability claims joined to their gates |
| [`backlog/`](https://github.com/hjosugi/kofun-site/blob/main/backlog/README.md) in `hjosugi/kofun-site` | Generated long-range work catalogue |

Issues own work state; the RFC ledger owns decisions. An issue can be closed
because the work is done, abandoned, or superseded, so its state says nothing
durable about what the language decided. When a change alters a public semantic
contract, [`RFC_PROCESS.md`](RFC_PROCESS.md) applies and the decision is
recorded in the ledger, which the issue then references. Ordinary work needs no
RFC.

An open issue is not evidence that a feature is missing, and a closed issue is
not evidence that a feature works. Check the current source and an executable
gate before changing an implementation-status claim.

The 13,500 generated backlog rows are not the GitHub work queue. They live in
[`hjosugi/kofun-site`](https://github.com/hjosugi/kofun-site/blob/main/backlog/README.md)
because no gate here reads them; issues stay in this repository. Do not create
one GitHub issue per row by default. When a GitHub issue corresponds to a
generated row, preserve the `KOFUN-XXXXX` ID and fingerprint in the issue body.
Generated area files must not be edited manually. Regeneration remains disabled
until the retired host-language generator has a Kofun replacement, as described
in the backlog README.

## Classification

Classify every open issue on six independent axes. If repository labels are
available, use the names below as labels. Until then, keep the same values in
the issue body. Use one value per axis unless this document says otherwise.

### Tracker role

| Role | Meaning |
|---|---|
| `curated` | One independently reviewable result that can move through refinement, implementation, and verification. |
| `planning` | An umbrella that tracks an outcome through curated child issues. It is never directly implementation-ready. |

All non-planning issues in the active GitHub working set are `curated`,
including bounded research and design decisions. The role is distinct from
kind: a planning issue uses `kind: planning`, while a curated issue uses the
kind of result it produces.

### Workflow state

| State | Meaning | Next action |
|---|---|---|
| `needs-triage` | The issue has not been checked against current source, docs, and possible duplicates. | Establish current behavior and classify it. |
| `needs-detail` | The desired outcome is known, but scope, acceptance criteria, or validation is incomplete. | Refine the body; do not implement yet. |
| `needs-decision` | A language, architecture, security, or compatibility choice is unresolved. | Create or finish a bounded design/research issue. |
| `blocked` | A named hard dependency or external condition prevents useful completion. | Record the blocker and the artifact that will unblock it. |
| `ready` | The definition of ready below is satisfied and hard dependencies are complete. | An implementer may claim it. |
| `in-progress` | An assignee or linked pull request is actively working on the issue. | Keep scope and new blockers visible. |
| `verification-pending` | A linked or merged change appears to satisfy the issue, but required target-branch evidence is not yet confirmed. | Run the named gates on the current target branch, then close or return it to refinement. |
| `deferred` | The issue is valid but intentionally outside the active milestone. | Revisit when its milestone becomes active. |

The normal path for curated work is `needs-triage` to `needs-detail` or
`needs-decision`, then `ready`, `in-progress`, optionally
`verification-pending`, and closed. A fully specified issue with an unfinished
hard dependency is `blocked`, not `ready`. Age alone is not a reason to close a
valid issue.

### Kind

Choose one primary kind:

| Kind | Expected result |
|---|---|
| `bug` | Intended behavior is restored and a regression test demonstrates the former failure. |
| `implementation` | One agreed capability or internal contract is added. |
| `spec-design` | A normative or architectural decision, alternatives, and consequences are recorded. |
| `research` | A bounded question is answered with evidence and a recommendation; production code is not implied. |
| `test-quality` | A test, fuzz, benchmark, audit, diagnostic, or quality gate is added or repaired. |
| `documentation` | A defined audience can complete or understand a task using updated documentation. |
| `maintenance` | Build, dependency, release, CI, or repository maintenance has a verifiable result. |
| `planning` | An outcome and its independently closable child issues are tracked. It is not directly implementable. |

If an issue contains two independently reviewable results, split it rather
than assigning two kinds. For example, settle semantics in `spec-design`, then
implement the accepted contract in `implementation`.

### Area

Choose one primary area from the 27-area table in
[`backlog/README.md`](https://github.com/hjosugi/kofun-site/blob/main/backlog/README.md).
Use the owning subsystem, not every subsystem that may be touched. Mention
secondary areas under **Related**.

For cross-cutting work, ownership follows the artifact being changed:

- a parser change with new diagnostics belongs to Lexer and Parser;
- a diagnostic framework change belongs to Diagnostics;
- a backend-independent conformance gate belongs to Testing, Security, and
  Performance;
- tutorial-only work belongs to Formatter, Linter, and Documentation.

### Milestone

Use exactly one roadmap milestone: `M0-spec`, `M1-bootstrap`, `M2-alpha`,
`M3-beta`, or `M4-1.0`. The milestone identifies the exit criterion served by
the issue; it is not a promised date. If no current roadmap outcome requires
the issue, use the earliest milestone whose deliverable or exit criterion does
and state that connection in **Why now**.

The GitHub tracker currently also contains legacy milestones whose names and
numbers do not match these roadmap meanings. Until those milestones are
explicitly migrated, record the roadmap milestone in the issue body and do not
infer a mapping from a shared number such as “M1”. A milestone migration must
publish the old-to-new mapping before changing issues in bulk.

### Size

Use scope size, not elapsed-time estimates:

| Size | Scope |
|---|---|
| `S` | One narrow artifact or behavior and one direct validation path. |
| `M` | A normal reviewable change, possibly touching several files in one subsystem. |
| `L` | Multiple independently testable outcomes or subsystem contracts. Split before marking `ready`. |

## Priority

Priority records impact and ordering pressure. It is separate from bug
severity, issue size, and workflow state.

| Priority | Use when |
|---|---|
| `P0` | Current behavior threatens safety or correctness, corrupts bootstrap evidence, silently accepts unsupported behavior, or blocks the active milestone with no reasonable workaround. |
| `P1` | The issue is required for an active milestone exit criterion or fixes a serious core-path failure with a limited workaround. |
| `P2` | The issue is a useful scoped capability, quality improvement, or non-blocking milestone deliverable. |
| `P3` | The work is later-stage, exploratory, optional, polish, or ecosystem breadth. |

Do not raise priority because an issue is old, broad, or easy. A planning issue
does not inherit the highest priority of all of its children; prioritize the
specific critical child. Re-evaluate priority when the current milestone or
evidence changes.

When issues have the same priority, order them by:

1. direct contribution to the active roadmap or self-hosting exit criterion;
2. correctness and safety risk;
3. number of ready issues they unblock;
4. availability of deterministic validation;
5. smallest independently valuable slice.

## Definition of ready

An implementation, bug, quality, documentation, or maintenance issue is
`ready` only when all of the following are true:

- [ ] The title names one outcome, not a topic (for example, “Reject duplicate
      module names” rather than “Modules”).
- [ ] Kind, primary area, priority, milestone, and size are assigned.
- [ ] Current behavior is supported by a source path, command output, failing
      test, specification section, or other reproducible evidence.
- [ ] The goal, in-scope behavior, and explicit non-goals fit one reviewable
      change.
- [ ] User-visible and internal contracts have no unresolved design question.
- [ ] Acceptance criteria describe observable results, including relevant
      positive, negative, and unsupported cases.
- [ ] Validation names exact commands, tests, fixtures, benchmarks, reviews, or
      audit artifacts and their expected result.
- [ ] Likely components or interfaces are identified without prescribing an
      unverified implementation.
- [ ] Every hard dependency is linked and complete; related work is not
      misrepresented as a blocker.
- [ ] Documentation and compatibility impact are stated, including “none” with
      a reason when appropriate.
- [ ] Safety claims include a threat model and a negative test; performance
      claims include a reproducible benchmark and budget.

A `spec-design` or `research` issue may be ready without an implementation
contract, but it must instead name the question, constraints, alternatives to
compare, evidence to collect, decision owner, output document, and completion
deadline or milestone. Its acceptance criterion is a reviewable decision or
recommendation, not “investigate”.

## Canonical issue body

Use this body when refining a GitHub issue. Remove instructional comments, but
keep sections that are not applicable and explain why.

```markdown
## Metadata

- State: needs-detail
- Tracker role: curated/planning
- Kind: bug/implementation/spec-design/research/test-quality/documentation/maintenance/planning
- Area: one primary area from the backlog README in hjosugi/kofun-site
- Priority: P0/P1/P2/P3
- Milestone: M0-spec/M1-bootstrap/M2-alpha/M3-beta/M4-1.0
- Size: S/M/L
- Generated backlog ID: KOFUN-XXXXX or none
- Fingerprint: value from generated backlog or none

## Summary

State the single independently reviewable outcome in two or three sentences.

## Current behavior and evidence

Describe what happens now. Link source, docs, an executable gate, a minimal
reproducer, or captured output. For a bug, include expected versus actual
behavior and the smallest reproduction.

## Why now

Name the roadmap deliverable, exit criterion, correctness risk, or ready work
that this issue enables.

## Goal

Describe the observable behavior or artifact after completion.

## Scope

### In scope

- Concrete behavior or artifact.

### Out of scope

- Follow-up behavior that must not expand this issue.

## Contract and constraints

- Inputs, outputs, errors, invariants, compatibility, target profiles, and
  explicit unsupported behavior.
- Link the accepted specification or design decision. Write “unresolved” and
  change the state to `needs-decision` if a choice remains.

## Implementation orientation

- Likely components, interfaces, fixtures, and existing patterns to reuse.
- This is guidance, not a requirement to use an unverified approach.

## Acceptance criteria

- [ ] An observable positive result.
- [ ] A negative or unsupported case fails explicitly.
- [ ] Existing supported behavior remains covered.

## Validation

| Check | Command or artifact | Expected result |
|---|---|---|
| Focused | `exact command` | Exact pass condition |
| Regression | `exact command` | Existing gate remains green |

## Dependencies

- Blocked by: #issue and the required artifact, or none
- Blocks: #issue, or none
- Related: #issue, document, or backlog ID, or none

## Documentation and compatibility

- Documents to update in the same change, or “none” with a reason.
- Compatibility, migration, and release-note impact, or “none” with a reason.

## Risks and open questions

- Risk and mitigation.
- Open questions: none before an implementation issue becomes `ready`.
```

Acceptance criteria state results, while validation explains how the results
are proved. “Tests pass”, “implement support”, and “works correctly” are not
sufficient by themselves.

## Dependencies

Use dependency links narrowly:

- **Blocked by** is a hard prerequisite: this issue cannot satisfy its
  acceptance criteria until a specific artifact from the other issue exists.
- **Blocks** is the inverse of a hard prerequisite.
- **Related** shares context, code, or design but may proceed independently.
- **Supersedes/duplicate of** identifies replacement or duplicate scope and is
  normally followed by closing one issue with an explanatory comment.

Name both the issue and the required artifact, such as “blocked by #123 until
the typed HIR node contract is accepted”. Do not depend on a broad planning
issue when one child or design decision is the real prerequisite. Avoid
dependency cycles; split out the shared contract or decision that breaks the
cycle. An issue can be `ready` only after all **Blocked by** links are complete
and their artifacts are present in the repository.

External blockers must name the condition, who can verify it, and the next
review point. “Waiting for later” is `deferred`, not `blocked`.

## Planning issues

A planning issue tracks an outcome that is intentionally larger than one pull
request. It is an index, not an implementation task, and must not be marked
`ready`.

A planning issue must contain:

- the roadmap outcome and measurable exit condition;
- scope and non-goals;
- a checklist of linked, independently closable child issues;
- critical-path dependencies between children;
- current status derived from child evidence;
- risks, unresolved decisions, and the next triage action.

Create a child issue when work has a different owner, kind, dependency,
acceptance result, or validation command. A child must carry its own complete
body; the planning issue is not a substitute for implementation context.

Do not close a planning issue merely because code landed. Close it only after
all required children and the planning exit condition are verified. Do not use
the planning issue as a hard dependency unless it itself produces a concrete
decision or artifact; link the producing child instead.

## Triage procedure

For each issue:

1. Reproduce or locate the claimed current behavior. Check the current target
   branch, linked merged changes, executable gates, and `MVP_IMPLEMENTED.md`
   before assuming the issue remains unimplemented. An older local checkout is
   not sufficient closure evidence.
2. Search for duplicates, superseding work, and overlapping generated backlog
   IDs. Preserve useful discussion when consolidating.
3. Assign workflow state, kind, area, milestone, priority, and size.
4. Split planning or `L` scope into independent results. Separate unresolved
   design from implementation.
5. Rewrite the body using the canonical template, with exact acceptance and
   validation evidence.
6. Link hard dependencies by artifact, remove cycles, and distinguish related
   work.
7. Mark the issue `ready` only after the checklist passes. Use
   `verification-pending` when implementation appears present but target-branch
   gates still need confirmation. Otherwise leave one explicit next action.

If current source and gates already satisfy an issue, record the verifying
commands and reconcile any stale documentation before closing it. Do not keep
an implementation issue open solely to represent future expansion; create a
bounded follow-up for the remaining behavior.

## Completion and documentation sync

An issue is complete when its acceptance criteria and validation evidence are
present, relevant active gates pass, unsupported behavior still fails
explicitly, and required documentation is synchronized.

Use these update rules:

- Update `docs/MVP_IMPLEMENTED.md` when the verified capability matrix changes.
- Update the root `README.md` when bootstrap status, requirements, public CLI
  examples, supported profiles, or user-visible active gates change.
- Update `docs/ROADMAP.md` only when milestone deliverables, exit criteria, or
  milestone status change—not for routine child-issue progress.
- Update the relevant design/specification document when a `spec-design` issue
  accepts or changes a contract.
- Update user, framework, and test documentation in the same pull request as a
  public behavior, workflow, command, or gate change.
- Update a planning issue's child checklist after evidence lands; do not copy
  volatile issue counts into repository documentation.
- Treat “implemented” as requiring active source plus an executable gate.
  Prototypes, issue closure, and design text alone do not change active status.

Triage-only metadata edits normally require no repository documentation
change. If triage discovers a contradiction between the tracker and a source
of truth above, correct the authoritative document as a separate, reviewable
change and link the evidence.
