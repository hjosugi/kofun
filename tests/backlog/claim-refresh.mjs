import { claimEvents, latestLiveClaimAgents } from './extract.mjs'

// The open-issues endpoint cannot report an issue after it closes. Seed a
// bounded follow-up probe from only the canonical live claims already present
// in the committed snapshot, excluding issue numbers still in the open result.
export function missingLiveClaimNumbers(priorSnapshot, openIssues) {
    const open = new Set(openIssues.map((issue) => issue.number))
    return (priorSnapshot?.issues ?? [])
        .filter((issue) => !open.has(issue.number))
        .filter((issue) => latestLiveClaimAgents(issue.claims).length > 0)
        .map((issue) => issue.number)
        .sort((left, right) => left - right)
}

// Retain only an actually closed row whose current comments still say an agent
// is live. A released/merged event removes the row on the next refresh.
export function shouldRetainClosedClaim(issue) {
    return (
        issue.state === 'closed' &&
        latestLiveClaimAgents(claimEvents(issue.claim_comments)).length > 0
    )
}
