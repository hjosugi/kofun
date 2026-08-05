import { claimEvents } from './extract.mjs'

function equal(actual, expected, name) {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        throw new Error(`${name}: ${JSON.stringify(actual)} != ${JSON.stringify(expected)}`)
    }
}

equal(
    claimEvents([
        {
            body:
                '### agent-claim:v1\n' +
                '- agent_id: codex-one\n' +
                '- status: active\n' +
                '- baseline: abc123\n',
        },
        { body: '### agent-claim:v1\n- agent_id: codex-one\n- status: pr-open\n' },
    ]),
    [
        { agent_id: 'codex-one', status: 'active' },
        { agent_id: 'codex-one', status: 'pr-open' },
    ],
    'canonical claim extraction',
)

equal(
    claimEvents([
        { body: '<!--\n### agent-claim:v1\n- agent_id: hidden\n- status: active\n-->' },
        { body: '```markdown\n### agent-claim:v1\n- agent_id: example\n- status: active\n```' },
        { body: 'agent-claim:v1\nagent_id: legacy\nstatus: active' },
    ]),
    [],
    'noncanonical claims stay inert',
)

equal(
    claimEvents([
        { body: '### agent-claim:v1\n- agent_id: duplicate\n- agent_id: other\n- status: active' },
    ]),
    [{ agent_id: null, status: 'active' }],
    'duplicate load-bearing key',
)

process.stdout.write('PASS: canonical claim comments extract once and legacy wrappers stay inert\n')
