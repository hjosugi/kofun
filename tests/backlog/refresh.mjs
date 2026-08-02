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
//       build the same snapshot from a captured payload whose issue objects
//       carry `claim_comments`, or whose top level carries
//       `comments_by_issue: { "123": [...] }`
//
// Both paths run the same extraction, so a snapshot produced one way is
// reproducible the other way.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { buildSnapshot } from './extract.mjs'
import { missingLiveClaimNumbers, shouldRetainClosedClaim } from './claim-refresh.mjs'

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..')
const OUTPUT = resolve(ROOT, 'artifacts/backlog/issue-state.json')

const OWNER = process.env.KOFUN_BACKLOG_OWNER ?? 'hjosugi'
const REPO = process.env.KOFUN_BACKLOG_REPO ?? 'kofun'

function headers(token) {
    return {
        accept: 'application/vnd.github+json',
        authorization: `Bearer ${token}`,
        'user-agent': 'kofun-backlog-refresh',
    }
}

async function fetchClaimComments(issue, token) {
    if ((issue.comments ?? 0) > 100) {
        throw new Error(
            `#${issue.number} has ${issue.comments} comments; the bounded one-call ` +
                'claim refresh supports at most 100',
        )
    }
    const response = await fetch(`${issue.comments_url}?per_page=100`, {
        headers: headers(token),
    })
    if (!response.ok) {
        throw new Error(
            `GitHub returned ${response.status} for #${issue.number} comments: ` +
                `${(await response.text()).slice(0, 200)}`,
        )
    }
    return { ...issue, claim_comments: await response.json() }
}

async function fetchIssue(number, token) {
    const url = `https://api.github.com/repos/${OWNER}/${REPO}/issues/${number}`
    const response = await fetch(url, { headers: headers(token) })
    if (!response.ok) {
        throw new Error(
            `GitHub returned ${response.status} for claimed issue #${number}: ` +
                `${(await response.text()).slice(0, 200)}`,
        )
    }
    return response.json()
}

async function fetchOpenIssues(priorSnapshot) {
    const token = process.env.GITHUB_TOKEN ?? process.env.GH_TOKEN
    if (!token) {
        throw new Error('reading issue state needs GITHUB_TOKEN or GH_TOKEN')
    }
    const issues = []
    for (let page = 1; page <= 20; page += 1) {
        const url =
            `https://api.github.com/repos/${OWNER}/${REPO}/issues` +
            `?state=open&per_page=100&page=${page}`
        const response = await fetch(url, { headers: headers(token) })
        if (!response.ok) {
            throw new Error(
                `GitHub returned ${response.status} for page ${page}: ` +
                    `${(await response.text()).slice(0, 200)}`,
            )
        }
        const batch = await response.json()
        if (batch.length === 0) {
            const withComments = []
            // Intentionally sequential: one bounded request per open issue is
            // cheap at this backlog size and avoids secondary-rate-limit bursts.
            for (const issue of issues) {
                withComments.push(await fetchClaimComments(issue, token))
            }
            for (const number of missingLiveClaimNumbers(priorSnapshot, withComments)) {
                const claimed = await fetchClaimComments(
                    await fetchIssue(number, token),
                    token,
                )
                if (shouldRetainClosedClaim(claimed)) withComments.push(claimed)
            }
            return withComments
        }
        issues.push(...batch.filter((issue) => issue.pull_request === undefined))
    }
    throw new Error('more than 2000 open issues; raise the page bound deliberately')
}

// A captured payload is either the bare array the REST endpoint returns or an
// object carrying it, depending on which tool captured it. Accept both rather
// than making the caller reshape evidence by hand.
function readCapturedIssues(path) {
    const payload = JSON.parse(readFileSync(path, 'utf8'))
    const issues = Array.isArray(payload)
        ? payload
        : Array.isArray(payload.issues)
          ? payload.issues
          : Array.isArray(payload.items)
            ? payload.items
            : null
    if (issues === null) throw new Error(`${path} holds no issue array`)

    const commentsByIssue = payload.comments_by_issue ?? {}
    return issues.map((issue) => {
        const claimComments =
            issue.claim_comments ??
            (Array.isArray(issue.comments) ? issue.comments : commentsByIssue[issue.number])
        if (claimComments === undefined && Number(issue.comments ?? 0) > 0) {
            throw new Error(
                `${path} has no captured comments for #${issue.number}; add ` +
                    '`claim_comments` or top-level `comments_by_issue`',
            )
        }
        return { ...issue, claim_comments: claimComments ?? [] }
    })
}

const fromJsonIndex = process.argv.indexOf('--from-json')
const priorSnapshot = JSON.parse(readFileSync(OUTPUT, 'utf8'))
const issues =
    fromJsonIndex === -1
        ? await fetchOpenIssues(priorSnapshot)
        : readCapturedIssues(process.argv[fromJsonIndex + 1])

const snapshot = buildSnapshot(`${OWNER}/${REPO}`, issues)

mkdirSync(dirname(OUTPUT), { recursive: true })
writeFileSync(OUTPUT, `${JSON.stringify(snapshot, null, 2)}\n`)

process.stdout.write(
    `PASS: backlog snapshot written for ${snapshot.open_issues} open issues\n`,
)
