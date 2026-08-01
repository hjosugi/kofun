import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import {
    mkdtempSync,
    readFileSync,
    rmSync,
    writeFileSync,
} from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const testDir = dirname(fileURLToPath(import.meta.url))
const repoDir = resolve(testDir, '../../..')
const domain = [
    { source: '(0.0 - 1.25)', display: '-1.25' },
    { source: '(0.0 - 0.5)', display: '-0.5' },
    { source: '0.0', display: '0.0' },
    { source: '0.5', display: '0.5' },
    { source: '1.25', display: '1.25' },
]

function sha(value) {
    return createHash('sha256').update(value).digest('hex')
}

function canonicalValue(display) {
    return {
        encoding: 'kofun.canonical-value/v1',
        sha256: sha(`kofun.canonical-value/v1\0${display}`),
        display,
    }
}

function commonEvidence({ type, lawName, equation, values, result, cases }) {
    const typeId = sha(`type\0${type}`)
    const equationId = sha(`equation\0${equation}`)
    const implementation = readFileSync(
        resolve(repoDir, 'bootstrap/stage2/decimal_v1.c'), 'utf8')
    const model = {
        domains: [{
            name: 'values',
            type_id: typeId,
            canonical_type: type,
            declaration_order: 0,
            origin: 'explicit-sample',
            canonical_values_sha256: sha(values.join('\0')),
            input_domain: null,
            output_domain: null,
        }],
        equalities: [{
            type_id: typeId,
            canonical_type: type,
            kind: 'compiler-structural',
            equality_id: sha(`equality\0${type}`),
            interface_sha256: sha(`interface\0${type}\0==`),
            body_sha256: sha(`body\0${type}\0==`),
            equivalence_certification_sha256: null,
        }],
        budget: {
            profile: 'kofun.law-eval/standard-v1',
            planned_cases: cases,
            evaluator_steps: cases * 8,
            recursion_depth: 32,
            allocations: 10000,
            live_heap_bytes: 1048576,
            value_bytes: 4096,
            diagnostic_bytes: 4096,
        },
        enumeration_algorithm: 'kofun.law-cartesian/v1',
        shrinking_algorithm: 'kofun.law-structural-shrink/v1',
    }
    const subject = {
        package_id: sha('package\0stdlib.decimal'),
        module_id: sha(`module\0stdlib.decimal.${type.toLowerCase()}`),
        law_declaration_id: sha(`law\0${lawName}\0${equation}`),
        law_name: lawName,
        ground_types: [{ slot: 'Value', type_id: typeId, canonical_type: type }],
        normalized_equations_sha256: equationId,
        implementation: {
            implementation_id: sha(`implementation\0${type}\0${implementation}`),
            trait_implementation_id: null,
            interface_sha256: sha(`interface\0${type}\0addition`),
            body_sha256: sha(implementation),
            transitive_dependency_digests: [],
        },
    }
    const compiler = {
        name: 'Kofun',
        semantic_version: '0.3.38-seed',
        semantics_sha256: sha(implementation),
        evaluator_version: 'stdlib-decimal-laws/v2',
        evidence_abi: 'kofun.law-evidence/v2',
    }
    const cacheKey = sha(JSON.stringify({ compiler, subject, model }))
    const document = {
        schema: 'kofun.law-evidence/v2',
        identities: {
            evaluation_cache_key: {
                domain: 'kofun.cache.law-evaluation/v2',
                algorithm: 'sha256',
                sha256: cacheKey,
            },
            evidence: {
                domain: 'kofun.id.law-evidence/v2',
                algorithm: 'sha256',
                sha256: sha(JSON.stringify({ cacheKey, result })),
            },
        },
        compiler,
        subject,
        model,
        evaluation: {
            cases_planned: cases,
            cases_checked: cases,
            evaluator_steps: cases * 8,
            max_recursion_depth: 1,
            allocations: 0,
            peak_live_heap_bytes: 0,
            max_value_bytes: 64,
            diagnostic_bytes: 0,
            cancellation_check_interval_steps: 1024,
        },
        result,
        policy: {
            requested_assurance: type === 'Decimal' ? 'bounded-exhaustive' : null,
            requirement_met: type === 'Decimal',
        },
        metadata: {
            logical_source_path: type === 'Decimal'
                ? 'stdlib/decimal/tests/generated-addition-associativity.kofun'
                : 'stdlib/decimal/tests/float_counterexample.js',
        },
    }
    return `${JSON.stringify(document, null, 2)}\n`
}

function executeDecimalLaw() {
    const work = mkdtempSync(join(tmpdir(), 'kofun-decimal-law.'))
    try {
        const observations = []
        for (const left of domain) {
            for (const middle of domain) {
                for (const right of domain) {
                    observations.push(
                        `    if (${left.source} + ${middle.source}) + ${right.source} == ` +
                        `${left.source} + (${middle.source} + ${right.source}) { checked = checked + 1 }`)
                }
            }
        }
        const source = [
            '# Generated bounded library-law fixture; no compiler law keyword.',
            'fn main() {',
            '    let mut checked = 0',
            ...observations,
            '    print(checked)',
            '}',
            '',
        ].join('\n')
        const sourcePath = join(work, 'addition-associativity.kofun')
        writeFileSync(sourcePath, source)
        const output = execFileSync(resolve(repoDir, 'bin/kofun'), ['run', sourcePath], {
            encoding: 'utf8',
        })
        const checked = Number(output.trim())
        if (checked !== observations.length) {
            throw new Error(`Decimal associativity checked ${checked}/${observations.length}`)
        }
        return checked
    } finally {
        rmSync(work, { recursive: true, force: true })
    }
}

function executeFloatCounterexample() {
    const output = execFileSync(
        process.execPath,
        [resolve(testDir, 'float_counterexample.js')],
        { encoding: 'utf8' },
    )
    const values = Object.fromEntries(output.trim().split('\n').map((line) => {
        const split = line.indexOf('=')
        return [line.slice(0, split), line.slice(split + 1)]
    }))
    if (values.equal !== 'false') throw new Error('Float witness unexpectedly passed')
    return values
}

const checked = executeDecimalLaw()
const decimalResult = {
    status: 'passed',
    assurance: 'bounded-exhaustive',
    reusable: true,
    outcome_sha256: sha(`passed\0${checked}`),
    counterexample: null,
    diagnostics: [],
}
const float = executeFloatCounterexample()
const floatEquation = '(a + b) + c == a + (b + c)'
const equationId = sha(`equation\0${floatEquation}`)
const typeId = sha('type\0Float')
const floatResult = {
    status: 'failed',
    assurance: null,
    reusable: false,
    outcome_sha256: sha(`failed\0${float.left}\0${float.right}`),
    counterexample: {
        equation_id: equationId,
        canonical_sha256: sha(float.counterexample),
        bindings: JSON.parse(float.counterexample).map((value, index) => ({
            name: ['a', 'b', 'c'][index],
            type_id: typeId,
            value: canonicalValue(String(value)),
        })),
        left: canonicalValue(float.left),
        right: canonicalValue(float.right),
        shrink_steps: 0,
    },
    diagnostics: [{
        code: 'L001',
        message: 'Float addition associativity failed with the committed counterexample',
    }],
}

const outputs = [
    [resolve(testDir, 'law-evidence.json'), commonEvidence({
        type: 'Decimal',
        lawName: 'DecimalAdditionAssociativity',
        equation: '(a + b) + c == a + (b + c)',
        values: domain.map(({ display }) => display),
        result: decimalResult,
        cases: checked,
    })],
    [resolve(testDir, 'float-associativity-evidence.json'), commonEvidence({
        type: 'Float',
        lawName: 'FloatAdditionAssociativity',
        equation: floatEquation,
        values: JSON.parse(float.counterexample).map(String),
        result: floatResult,
        cases: 1,
    })],
]

if (process.argv.includes('--write')) {
    for (const [path, contents] of outputs) writeFileSync(path, contents)
} else {
    for (const [path, contents] of outputs) {
        if (readFileSync(path, 'utf8') !== contents) {
            throw new Error(`law evidence is stale: ${path}`)
        }
    }
}
