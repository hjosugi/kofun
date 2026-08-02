// What the snapshot is allowed to say about an issue.
//
// Kept apart from the fetching so the same extraction runs whether the issues
// came from the API or from a captured payload. A bootstrap that derived the
// fields differently from the refresh would produce a snapshot CI could never
// reproduce, and the `git diff --exit-code` proof would fail for a reason that
// has nothing to do with the backlog.
//
// Every rule here is stated in docs/ISSUE_READINESS.md.

// The state vocabulary that document defines. What matters to the gate is that
// at most one applies to an issue.
export const STATE_LABELS = Object.freeze([
    'ready',
    'blocked',
    'needs-detail',
    'needs-decision',
    'deferred',
    'in-progress',
])

// A claim is an append-only event log. `active` and `pr-open` still own the
// issue; `released` and `merged` release it. Keeping this vocabulary here, next
// to the state vocabulary, gives the hermetic checker one closed authority.
export const CLAIM_STATUSES = Object.freeze([
    'active',
    'pr-open',
    'released',
    'merged',
])

export const LIVE_CLAIM_STATUSES = Object.freeze(['active', 'pr-open'])

// The first `- State: x` line of the Metadata block. Only the first, because a
// body that quotes an earlier state — a refinement note saying what the issue
// used to be — must not read as a second declaration.
//
// Only the leading word is the state. Issues qualify it in prose — `State:
// blocked (on #904)` — and reading the qualifier as part of the name would
// report a disagreement between `blocked` and `blocked`.
export function stateLine(body) {
    const match = /^[-*]\s*State:\s*([A-Za-z][A-Za-z-]*)/m.exec(body ?? '')
    return match === null ? null : match[1].toLowerCase()
}

// Issue numbers on a `Blocked by:` line. "Blocked by: none" yields none, so an
// absent line and an explicit none are the same list: an issue that says
// nothing about blockers claims none.
export function blockedBy(body) {
    const numbers = new Set()
    for (const line of (body ?? '').split('\n')) {
        if (!/blocked by:/i.test(line)) continue
        for (const match of line.matchAll(/#(\d+)/g)) {
            numbers.add(Number(match[1]))
        }
    }
    return [...numbers].sort((left, right) => left - right)
}

// Full 40-hex object names. Rule 2 of the Definition of Ready asks a
// current-behavior claim to name the commit it was measured on; this is how
// the gate sees whether one is there at all.
export function evidenceCommits(body) {
    const found = new Set()
    for (const match of (body ?? '').matchAll(/\b[0-9a-f]{40}\b/g)) {
        found.add(match[0])
    }
    return [...found].sort()
}

// Canonical claims are deliberately distinguishable from the three historical
// dialects: a visible level-three Markdown heading is the wrapper, and the two
// load-bearing keys are exact list items. Fenced examples and HTML comments are
// removed before matching so documentation and invisible legacy claims cannot
// acquire ownership by accident.
export function claimEvents(comments) {
    const events = []
    for (const comment of comments ?? []) {
        const body = (comment.body ?? '')
            .replace(/<!--[^]*?-->/g, '')
            .replace(/^(```|~~~)[^\n]*\n[^]*?^\1\s*$/gm, '')
        const lines = body.split('\n')
        for (let index = 0; index < lines.length; index += 1) {
            if (lines[index].trimEnd() !== '### agent-claim:v1') continue

            const values = new Map()
            const duplicates = new Set()
            for (index += 1; index < lines.length; index += 1) {
                const line = lines[index]
                if (/^#{1,6}\s/.test(line) || line.trim() === '') break
                const match = /^- (agent_id|status):\s*(\S(?:.*\S)?)\s*$/.exec(line)
                if (match === null) continue
                if (values.has(match[1])) duplicates.add(match[1])
                values.set(match[1], match[2])
            }
            events.push({
                agent_id: duplicates.has('agent_id') ? null : (values.get('agent_id') ?? null),
                status: duplicates.has('status') ? null : (values.get('status') ?? null),
            })
        }
    }
    return events
}

export function snapshotIssue(issue) {
    const labels = (issue.labels ?? []).map((label) =>
        typeof label === 'string' ? label : label.name,
    )
    const row = {
        number: issue.number,
        title: issue.title,
        state_labels: STATE_LABELS.filter((name) => labels.includes(name)),
        state_line: stateLine(issue.body),
        blocked_by: blockedBy(issue.body),
        evidence_commits: evidenceCommits(issue.body),
    }
    const claims = claimEvents(issue.claim_comments)
    if (claims.length > 0) row.claims = claims
    if (issue.state === 'closed') row.issue_state = 'closed'
    return row
}

// The snapshot is a pure function of the issues. It carries no timestamp and
// no generator version on purpose: a field that changed on every run would
// make CI's `git diff --exit-code` fail for reasons unrelated to the backlog,
// and the freshness signal would stop meaning anything.
export function buildSnapshot(repository, issues) {
    const rows = issues
        .filter((issue) => issue.pull_request === undefined)
        .map(snapshotIssue)
        .sort((left, right) => left.number - right.number)
    return {
        schema: 'kofun.backlog-issue-state/v1',
        repository,
        state_labels: [...STATE_LABELS],
        open_issues: rows.length,
        issues: rows,
    }
}
