// The rules docs/ISSUE_READINESS.md states, asserted against the committed
// snapshot. Every failure names the issue and both values, because "the
// backlog is inconsistent" is not something anyone can act on.

import { readFileSync } from 'node:fs'

const [snapshotPath, debtPath] = process.argv.slice(2)
if (!snapshotPath || !debtPath) {
    throw new Error('usage: check.mjs SNAPSHOT DEBT')
}

const snapshot = JSON.parse(readFileSync(snapshotPath, 'utf8'))
const failures = []

if (snapshot.schema !== 'kofun.backlog-issue-state/v1') {
    failures.push(`snapshot schema is ${snapshot.schema}, not kofun.backlog-issue-state/v1`)
}

const issues = snapshot.issues ?? []

// An empty set satisfies every rule below without checking anything, which is
// how this class of drift stays invisible in the first place. Refuse rather
// than report a vacuous pass.
if (issues.length === 0) {
    failures.push('snapshot holds no open issues; the gate would pass without checking anything')
}
if (issues.length !== snapshot.open_issues) {
    failures.push(`snapshot says ${snapshot.open_issues} open issues but carries ${issues.length}`)
}

const open = new Set(issues.map((issue) => issue.number))
const byNumber = new Map(issues.map((issue) => [issue.number, issue]))

// Recorded debt. Both directions matter: an unlisted problem is new drift, and
// a listed row that no longer applies is a win that was not recorded.
//
// One ledger, two readers. `unverifiable-stamp` needs the git history, so
// tests/backlog/check-stamps.mjs owns it and this reader must not report those
// rows as unused. A kind neither reader claims is a typo that would otherwise
// sit in the file excusing nothing.
const OWNED_KINDS = new Set(['state-disagreement', 'unstamped-ready'])
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
    debt.set(`${kind}:${Number(number)}`, { kind, number: Number(number), detail })
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
let stamped = 0
let readyCount = 0

for (const issue of issues) {
    const where = `#${issue.number}`

    // 1. At most one state. Two state labels is not a stricter claim, it is an
    //    unreadable one.
    if (issue.state_labels.length > 1) {
        failures.push(`${where} carries ${issue.state_labels.length} state labels: ${issue.state_labels.join(', ')}`)
        continue
    }

    const label = issue.state_labels[0] ?? null
    const line = issue.state_line ?? null

    // 2. Label and body agree when both are present. An issue with neither is
    //    untriaged, which is a different problem and not this gate's.
    if (label !== null && line !== null) {
        if (label === line) {
            agreeing += 1
        } else if (!owedDebt('state-disagreement', issue.number, `${label}/${line}`)) {
            failures.push(`${where} label says \`${label}\` and its State line says \`${line}\``)
            continue
        }
    }

    if (label !== 'ready') continue
    readyCount += 1

    // 3. A `ready` issue naming an open blocker is advertised as startable
    //    while its own body says it is not. This one has no debt escape: it is
    //    the drift that costs a contributor their afternoon.
    const openBlockers = issue.blocked_by.filter((number) => open.has(number))
    if (openBlockers.length > 0) {
        const detail = openBlockers
            .map((number) => `#${number} (${byNumber.get(number)?.state_labels[0] ?? 'no state label'})`)
            .join(', ')
        failures.push(`${where} is ready but names open blockers: ${detail}`)
    }

    // 4. Definition of Ready rule 2: a current-behavior claim names the commit
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
    `PASS: ${agreeing} issues agree between their state label and their State line\n` +
        `PASS: no ready issue names an open blocker (${readyCount} ready)\n` +
        `PASS: ${stamped} ready issues carry an evidence stamp\n` +
        `PASS: ${debt.size} recorded debt rows all still describe the issue they name\n`,
)
