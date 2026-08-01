import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { validateAgainstSchema } from '../../../tests/lib/json-schema.mjs'

const [schemaPath, ...evidencePaths] = process.argv.slice(2)
if (!schemaPath || evidencePaths.length === 0) {
    throw new Error('usage: validate_law_evidence.mjs SCHEMA EVIDENCE...')
}

const schema = JSON.parse(readFileSync(resolve(schemaPath), 'utf8'))
for (const evidencePath of evidencePaths) {
    const evidence = JSON.parse(readFileSync(resolve(evidencePath), 'utf8'))
    const errors = []
    validateAgainstSchema(schema, schema, evidence, evidencePath, errors)
    if (errors.length > 0) {
        throw new Error(`invalid law evidence ${evidencePath}:\n${errors.join('\n')}`)
    }
}
