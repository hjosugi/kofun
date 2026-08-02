// The rules docs/ISSUE_READINESS.md states, asserted against the committed
// snapshot. Every failure names the issue and both values, because "the
// backlog is inconsistent" is not something anyone can act on.

import { readFileSync } from 'node:fs'

import { CLAIM_STATUSES, STATE_LABELS, latestLiveClaimAgents } from './extract.mjs'

const [snapshotPath, debtPath] = process.argv.slice(2)
if (!snapshotPath || !debtPath) {
    throw new Error('usage: check.mjs SNAPSHOT DEBT')
}

const snapshot = JSON.parse(readFileSync(snapshotPath, 'utf8'))
const failures = []

if (snapshot.schema !== 'kofun.backlog-issue-state/v1') {
    failures.push(`snapshot schema is ${snapshot.schema}, not kofun.backlog-issue-state/v1`)
}

// The vocabulary is carried twice — here as the repository's definition, and in
// the snapshot as the one its writer used. Reading only the snapshot's copy
// would let a stale or hand-edited file widen the vocabulary and then satisfy
// every rule below against its own wider version.
const vocabulary = new Set(STATE_LABELS)
const declared = snapshot.state_labels ?? []
const unexpected = declared.filter((name) => !vocabulary.has(name))
const missing = STATE_LABELS.filter((name) => !declared.includes(name))
if (unexpected.length > 0 || missing.length > 0) {
    failures.push(
        `snapshot declares a different state vocabulary than tests/backlog/extract.mjs` +
            `${unexpected.length > 0 ? `; it adds ${unexpected.join(', ')}` : ''}` +
            `${missing.length > 0 ? `; it omits ${missing.join(', ')}` : ''}` +
            '; regenerate it with tests/backlog/refresh.mjs',
    )
}

const issues = snapshot.issues ?? []

// An empty set satisfies every rule below without checking anything, which is
// how this class of drift stays invisible in the first place. Refuse rather
// than report a vacuous pass.
if (issues.length === 0) {
    failures.push('snapshot holds no open issues; the gate would pass without checking anything')
}
const openIssueCount = issues.filter(
    (issue) => (issue.issue_state ?? 'open') !== 'closed',
).length
if (openIssueCount !== snapshot.open_issues) {
    failures.push(`snapshot says ${snapshot.open_issues} open issues but carries ${openIssueCount}`)
}

const open = new Set(
    issues.filter((issue) => (issue.issue_state ?? 'open') !== 'closed').map((issue) => issue.number),
)
const byNumber = new Map(issues.map((issue) => [issue.number, issue]))

// Recorded debt. Both directions matter: an unlisted problem is new drift, and
// a listed row that no longer applies is a win that was not recorded.
//
// One ledger, two readers. `unverifiable-stamp` needs the git history, so
// tests/backlog/check-stamps.mjs owns it and this reader must not report those
// rows as unused. A kind neither reader claims is a typo that would otherwise
// sit in the file excusing nothing.
const OWNED_KINDS = new Set(['state-disagreement', 'unstamped-ready', 'closed-blockers'])
const KNOWN_KINDS = new Set([...OWNED_KINDS, 'unverifiable-stamp'])

const debt = new Map()
for (const raw of readFileSync(debtPath, 'utf8').split('\n')) {
    const line = raw.trim()
    if (line === '' || line.startsWith('#')) continue
    const [kind, number, detail] = raw.split('\t')
    if (!KNOWN_KINDS.has(kind)) {
        failures.push(`debt row for #${Number(number)} has unknown kind \`${kind}\``)
        continue
    }
    if (!OWNED_KINDS.has(kind)) continue
    const key = `${kind}:${Number(number)}`
    // A duplicate row is a badly edited file, and storing it by key would
    // silently keep the last one — so the ledger would report a smaller count
    // than it holds and a stale copy could outlive the row that replaced it.
    if (debt.has(key)) {
        failures.push(`debt lists #${Number(number)} as ${kind} more than once`)
        continue
    }
    debt.set(key, { kind, number: Number(number), detail })
}
const usedDebt = new Set()

function owedDebt(kind, number, detail) {
    const key = `${kind}:${number}`
    const row = debt.get(key)
    if (row === undefined) return false
    usedDebt.add(key)
    if (row.detail !== detail) {
        failures.push(
            `#${number} is recorded as ${kind} ${row.detail} but is now ${detail}; ` +
                'update tests/backlog/debt.tsv so the row still describes the issue',
        )
    }
    return true
}

let agreeing = 0
let blockedWithNamedBlockers = 0
let stamped = 0
let readyCount = 0
let stated = 0
let claims = 0
let liveClaims = 0

const claimVocabulary = new Set(CLAIM_STATUSES)

for (const issue of issues) {
    const where = `#${issue.number}`

    // Claims are append-only events. Validate every event, then let the latest
    // valid event for one agent decide whether that agent still owns the issue.
    for (const claim of issue.claims ?? []) {
        claims += 1
        if (claim.agent_id === null || claim.agent_id === undefined || claim.agent_id === '') {
            failures.push(`${where} has an agent-claim:v1 without exactly one \`agent_id\``)
            continue
        }
        if (claim.status === null || claim.status === undefined || claim.status === '') {
            failures.push(`${where} claim for \`${claim.agent_id}\` has no single \`status\``)
            continue
        }
        if (!claimVocabulary.has(claim.status)) {
            failures.push(
                `${where} claim for \`${claim.agent_id}\` has status \`${claim.status}\`, ` +
                    `not one of ${CLAIM_STATUSES.join(', ')}`,
            )
            continue
        }
    }
    const liveAgents = latestLiveClaimAgents(issue.claims)
    liveClaims += liveAgents.length
    if (liveAgents.length > 1) {
        failures.push(`${where} has ${liveAgents.length} live claims: ${liveAgents.join(', ')}`)
    }
    if ((issue.issue_state ?? 'open') === 'closed' && liveAgents.length > 0) {
        failures.push(`${where} is closed but still has live claims: ${liveAgents.join(', ')}`)
    }
    if ((issue.issue_state ?? 'open') === 'closed') continue

    // 1. At most one state. Two state labels is not a stricter claim, it is an
    //    unreadable one.
    if (issue.state_labels.length > 1) {
        failures.push(`${where} carries ${issue.state_labels.length} state labels: ${issue.state_labels.join(', ')}`)
        continue
    }

    const label = issue.state_labels[0] ?? null
    const line = issue.state_line ?? null

    // 2. A State line names a state. The label side cannot go wrong — the
    //    extraction only keeps labels drawn from the vocabulary — but the line
    //    is free text, so an issue can invent a word. Rule 3 below never sees
    //    it: an invented word on an issue with no label has nothing to
    //    disagree with, so the gate reported agreement it had not checked.
    //    #998 carried `State: planning` this way.
    if (line !== null && !vocabulary.has(line)) {
        failures.push(
            `${where} has \`State: ${line}\`, which is not one of ${STATE_LABELS.join(', ')}; ` +
                'use a state docs/ISSUE_READINESS.md defines, or widen the vocabulary there first',
        )
        continue
    }
    if (line !== null) stated += 1

    // 3. Label and body agree when both are present. An issue with neither is
    //    untriaged, which is a different problem and not this gate's.
    if (label !== null && line !== null) {
        if (label === line) {
            agreeing += 1
        } else if (!owedDebt('state-disagreement', issue.number, `${label}/${line}`)) {
            failures.push(`${where} label says \`${label}\` and its State line says \`${line}\``)
            continue
        }
    }

    // 4. The mirror of a ready issue naming an open blocker: a blocked issue
    //    whose nonempty dependency list is entirely closed is advertised as
    //    unstartable after its own stated reason has disappeared. Issues with
    //    no named blockers are deliberately outside this rule; they may be
    //    waiting on a decision or another condition the extractor cannot see.
    if (label === 'blocked' && issue.blocked_by.length > 0) {
        blockedWithNamedBlockers += 1
        const openBlockers = issue.blocked_by.filter((number) => open.has(number))
        if (openBlockers.length === 0) {
            const detail = issue.blocked_by.map((number) => `#${number}`).join(', ')
            if (!owedDebt('closed-blockers', issue.number, detail)) {
                failures.push(`${where} is blocked but all named blockers are closed: ${detail}`)
            }
        }
    }

    if (label !== 'ready') continue
    readyCount += 1

    // 5. A `ready` issue naming an open blocker is advertised as startable
    //    while its own body says it is not. This one has no debt escape: it is
    //    the drift that costs a contributor their afternoon.
    const openBlockers = issue.blocked_by.filter((number) => open.has(number))
    if (openBlockers.length > 0) {
        const detail = openBlockers
            .map((number) => `#${number} (${byNumber.get(number)?.state_labels[0] ?? 'no state label'})`)
            .join(', ')
        failures.push(`${where} is ready but names open blockers: ${detail}`)
    }

    // 6. Definition of Ready rule 2: a current-behavior claim names the commit
    //    it was measured on. Without a stamp nobody can tell whether the
    //    premise still holds without re-deriving it, so nobody does.
    if (issue.evidence_commits.length === 0) {
        if (!owedDebt('unstamped-ready', issue.number, '-')) {
            failures.push(
                `${where} is ready with no evidence stamp; measure its current behavior and name the ` +
                    'commit, or record it in tests/backlog/debt.tsv',
            )
        }
    } else {
        stamped += 1
    }
}

// A debt row nobody reached is either fixed or stale. Both mean the row must
// go, and leaving it would let a future issue inherit the exemption unseen.
for (const [key, row] of debt) {
    if (usedDebt.has(key)) continue
    const issue = byNumber.get(row.number)
    if (issue === undefined) {
        failures.push(`debt names #${row.number} (${row.kind}), which is not an open issue; remove its row`)
    } else {
        failures.push(
            `debt names #${row.number} as ${row.kind}, which no longer applies; ` +
                'remove its row so the improvement is recorded',
        )
    }
}

if (failures.length > 0) {
    for (const failure of failures) process.stderr.write(`FAIL: backlog: ${failure}\n`)
    process.exit(1)
}

process.stdout.write(
    `PASS: ${claims} canonical claim events use the closed status vocabulary\n` +
        `PASS: ${liveClaims} live claims are unique per open issue and absent from closed issues\n` +
        `PASS: ${stated} State lines name a state in the vocabulary\n` +
        `PASS: ${agreeing} issues agree between their state label and their State line\n` +
        `PASS: ${blockedWithNamedBlockers} blocked issues with named blockers still have an open blocker or recorded debt\n` +
        `PASS: no ready issue names an open blocker (${readyCount} ready)\n` +
        `PASS: ${stamped} ready issues carry an evidence stamp\n` +
        `PASS: ${debt.size} recorded debt rows all still describe the issue they name\n`,
)
