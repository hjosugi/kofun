// Writes the committed snapshot that `tests/backlog/check.sh` reads.
//
// Issue state lives on GitHub and `task verify` is hermetic, so the gate
// cannot fetch. This is the split `artifacts/release-evidence/` already uses:
// a command with network regenerates a committed artifact, and CI proves the
// committed copy still matches by regenerating and diffing it.
//
// Usage:
//
//   node tests/backlog/refresh.mjs
//       read open issues from the API; needs GITHUB_TOKEN or GH_TOKEN
//
//   node tests/backlog/refresh.mjs --from-json PAYLOAD
//       build the same snapshot from a captured `GET /issues` payload, for an
//       environment that can reach the API only through a tool
//
// Both paths run the same extraction, so a snapshot produced one way is
// reproducible the other way.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { buildSnapshot } from './extract.mjs'

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..')
const OUTPUT = resolve(ROOT, 'artifacts/backlog/issue-state.json')

const OWNER = process.env.KOFUN_BACKLOG_OWNER ?? 'hjosugi'
const REPO = process.env.KOFUN_BACKLOG_REPO ?? 'kofun'

async function fetchOpenIssues() {
    const token = process.env.GITHUB_TOKEN ?? process.env.GH_TOKEN
    if (!token) {
        throw new Error('reading issue state needs GITHUB_TOKEN or GH_TOKEN')
    }
    const issues = []
    for (let page = 1; page <= 20; page += 1) {
        const url =
            `https://api.github.com/repos/${OWNER}/${REPO}/issues` +
            `?state=open&per_page=100&page=${page}`
        const response = await fetch(url, {
            headers: {
                accept: 'application/vnd.github+json',
                authorization: `Bearer ${token}`,
                'user-agent': 'kofun-backlog-refresh',
            },
        })
        if (!response.ok) {
            throw new Error(
                `GitHub returned ${response.status} for page ${page}: ` +
                    `${(await response.text()).slice(0, 200)}`,
            )
        }
        const batch = await response.json()
        if (batch.length === 0) return issues
        issues.push(...batch)
    }
    throw new Error('more than 2000 open issues; raise the page bound deliberately')
}

// A captured payload is either the bare array the REST endpoint returns or an
// object carrying it, depending on which tool captured it. Accept both rather
// than making the caller reshape evidence by hand.
function readCapturedIssues(path) {
    const payload = JSON.parse(readFileSync(path, 'utf8'))
    if (Array.isArray(payload)) return payload
    if (Array.isArray(payload.issues)) return payload.issues
    if (Array.isArray(payload.items)) return payload.items
    throw new Error(`${path} holds no issue array`)
}

const fromJsonIndex = process.argv.indexOf('--from-json')
const issues =
    fromJsonIndex === -1
        ? await fetchOpenIssues()
        : readCapturedIssues(process.argv[fromJsonIndex + 1])

const snapshot = buildSnapshot(`${OWNER}/${REPO}`, issues)

mkdirSync(dirname(OUTPUT), { recursive: true })
writeFileSync(OUTPUT, `${JSON.stringify(snapshot, null, 2)}\n`)

process.stdout.write(
    `PASS: backlog snapshot written for ${snapshot.open_issues} open issues\n`,
)
