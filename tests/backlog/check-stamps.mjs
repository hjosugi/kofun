// Every evidence stamp names a commit reachable from HEAD.
//
// A stamp is what makes a current-behavior claim checkable: re-run the command
// at that commit and see whether the result still holds. A stamp naming a
// commit this history does not contain cannot be re-run, so it reads as
// evidence while proving nothing.

import { readFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'

const [snapshotPath, debtPath] = process.argv.slice(2)
if (!snapshotPath || !debtPath) {
    throw new Error('usage: check-stamps.mjs SNAPSHOT DEBT')
}

const git = (...args) => spawnSync('git', args, { encoding: 'utf8' })

if (git('rev-parse', '--is-shallow-repository').stdout.trim() === 'true') {
    process.stderr.write(
        'FAIL: cannot verify evidence stamps in a shallow clone; ' +
            'fetch full history (actions/checkout with fetch-depth: 0)\n',
    )
    process.exit(1)
}

const snapshot = JSON.parse(readFileSync(snapshotPath, 'utf8'))

const excused = new Map()
for (const raw of readFileSync(debtPath, 'utf8').split('\n')) {
    if (raw.trim() === '' || raw.trimStart().startsWith('#')) continue
    const [kind, number, detail] = raw.split('\t')
    if (kind !== 'unverifiable-stamp') continue
    excused.set(Number(number), detail)
}

const stamps = []
for (const issue of snapshot.issues ?? []) {
    for (const commit of issue.evidence_commits ?? []) {
        stamps.push({ number: issue.number, commit })
    }
}

// No stamps at all means this check passes without looking at anything, which
// is the shape of failure the whole gate exists to remove.
if (stamps.length === 0) {
    process.stderr.write(
        'FAIL: no issue carries an evidence stamp; this check would pass without looking at anything\n',
    )
    process.exit(1)
}

const failures = []
const usedExcuse = new Set()
let reachable = 0

for (const { number, commit } of stamps) {
    const present = git('cat-file', '-e', `${commit}^{commit}`).status === 0
    const ancestor =
        present && git('merge-base', '--is-ancestor', commit, 'HEAD').status === 0

    if (ancestor) {
        reachable += 1
        if (excused.has(number) && excused.get(number) === commit) {
            failures.push(
                `#${number} is recorded as an unverifiable stamp but ${commit} is now reachable; ` +
                    'remove its row from tests/backlog/debt.tsv',
            )
            usedExcuse.add(number)
        }
        continue
    }

    if (excused.get(number) === commit) {
        usedExcuse.add(number)
        continue
    }

    failures.push(
        present
            ? `#${number} names commit ${commit}, which is not reachable from HEAD`
            : `#${number} names commit ${commit}, which is not in this repository`,
    )
}

for (const [number, commit] of excused) {
    if (usedExcuse.has(number)) continue
    failures.push(
        `debt records #${number} as an unverifiable stamp for ${commit}, ` +
            'which the snapshot no longer carries; remove its row',
    )
}

if (failures.length > 0) {
    for (const failure of failures) process.stderr.write(`FAIL: backlog stamps: ${failure}\n`)
    process.exit(1)
}

process.stdout.write(
    `PASS: ${reachable} evidence stamps name commits reachable from HEAD, ` +
        `${excused.size} recorded as unverifiable\n`,
)
