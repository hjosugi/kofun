import { buildSnapshot } from './extract.mjs'
import { missingLiveClaimNumbers, shouldRetainClosedClaim } from './claim-refresh.mjs'

function equal(actual, expected, name) {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        throw new Error(`${name}: ${JSON.stringify(actual)} != ${JSON.stringify(expected)}`)
    }
}

const prior = {
    issues: [
        { number: 7, claims: [{ agent_id: 'agent-seven', status: 'active' }] },
        { number: 8, claims: [{ agent_id: 'agent-eight', status: 'released' }] },
        { number: 9, claims: [{ agent_id: 'agent-nine', status: 'pr-open' }] },
    ],
}
equal(
    missingLiveClaimNumbers(prior, [{ number: 9 }]),
    [7],
    'only a disappeared live claim seeds a closed-state probe',
)

const closedActive = {
    number: 7,
    title: 'closed while owned',
    state: 'closed',
    labels: [],
    body: '',
    claim_comments: [
        { body: '### agent-claim:v1\n- agent_id: agent-seven\n- status: active' },
    ],
}
if (!shouldRetainClosedClaim(closedActive)) {
    throw new Error('a newly closed live claim was not retained')
}
equal(
    buildSnapshot('hjosugi/kofun', [closedActive]),
    {
        schema: 'kofun.backlog-issue-state/v1',
        repository: 'hjosugi/kofun',
        state_labels: ['ready', 'blocked', 'needs-detail', 'needs-decision', 'deferred', 'in-progress'],
        open_issues: 0,
        issues: [
            {
                number: 7,
                title: 'closed while owned',
                state_labels: [],
                state_line: null,
                blocked_by: [],
                evidence_commits: [],
                claims: [{ agent_id: 'agent-seven', status: 'active' }],
                issue_state: 'closed',
            },
        ],
    },
    'retained closed claim stays visible without inflating open count',
)

const closedReleased = {
    ...closedActive,
    claim_comments: [
        ...closedActive.claim_comments,
        { body: '### agent-claim:v1\n- agent_id: agent-seven\n- status: merged' },
    ],
}
if (shouldRetainClosedClaim(closedReleased)) {
    throw new Error('a merged claim remained in the closed-state carry set')
}

process.stdout.write('PASS: prior live claims make open-to-closed transitions observable\n')
