// A JSON Schema subset interpreter, shared by the repository's registry gates.
//
// The point is that a schema file stays authoritative rather than being
// restated in JavaScript, where the two would drift. So this interprets the
// keywords those schemas actually use, and an unknown keyword is a hard error:
// silently ignoring one would let a constraint be written into a schema and
// never enforced, which is the exact failure these gates exist to prevent.
//
// Adding a keyword here is deliberate. Add it to KNOWN_KEYWORDS only together
// with the code that enforces it.

const IGNORED_KEYWORDS = new Set(['$schema', '$id', 'title', 'description', '$defs'])
const KNOWN_KEYWORDS = new Set([
    '$ref', 'type', 'required', 'properties', 'additionalProperties', 'const',
    'enum', 'minimum', 'minItems', 'uniqueItems', 'items', 'minLength',
    'maxLength', 'pattern', 'maximum', 'maxItems', 'oneOf', 'allOf', 'if',
    'then', 'not',
])

function resolveRef(schema, ref) {
    if (!ref.startsWith('#/')) throw new Error(`unsupported $ref: ${ref}`)
    let node = schema
    for (const segment of ref.slice(2).split('/')) {
        node = node?.[segment]
        if (node === undefined) throw new Error(`unresolvable $ref: ${ref}`)
    }
    return node
}

export function typeOf(value) {
    if (Array.isArray(value)) return 'array'
    if (value === null) return 'null'
    if (Number.isInteger(value)) return 'integer'
    return typeof value
}

export function validateAgainstSchema(root, node, value, path, errors) {
    for (const keyword of Object.keys(node)) {
        if (IGNORED_KEYWORDS.has(keyword) || KNOWN_KEYWORDS.has(keyword)) continue
        throw new Error(`schema uses unsupported keyword \`${keyword}\` at ${path}`)
    }

    if (node.$ref !== undefined) {
        validateAgainstSchema(root, resolveRef(root, node.$ref), value, path, errors)
    }

    const schemaMatches = (candidate) => {
        const candidateErrors = []
        validateAgainstSchema(root, candidate, value, path, candidateErrors)
        return candidateErrors.length === 0
    }

    if (node.not !== undefined && schemaMatches(node.not)) {
        errors.push(`${path}: value matches a forbidden schema`)
    }
    if (node.oneOf !== undefined) {
        const matches = node.oneOf.filter(schemaMatches).length
        if (matches !== 1) {
            errors.push(`${path}: expected exactly one oneOf branch, found ${matches}`)
        }
    }
    for (const candidate of node.allOf ?? []) {
        validateAgainstSchema(root, candidate, value, path, errors)
    }
    if (node.if !== undefined && schemaMatches(node.if) && node.then !== undefined) {
        validateAgainstSchema(root, node.then, value, path, errors)
    }

    const actual = typeOf(value)
    const expectedTypes = Array.isArray(node.type) ? node.type : [node.type]
    const typeMatches = expectedTypes.some((expected) =>
        expected === actual || (expected === 'number' && actual === 'integer'))
    if (node.type !== undefined && !typeMatches) {
        errors.push(`${path}: expected ${expectedTypes.join(' or ')}, found ${actual}`)
        return
    }
    if (node.const !== undefined && value !== node.const) {
        errors.push(`${path}: expected the constant ${JSON.stringify(node.const)}`)
    }
    if (node.enum !== undefined && !node.enum.includes(value)) {
        errors.push(`${path}: ${JSON.stringify(value)} is not one of ${node.enum.join(', ')}`)
    }
    if (node.minimum !== undefined && value < node.minimum) {
        errors.push(`${path}: must be at least ${node.minimum}`)
    }
    if (node.maximum !== undefined && value > node.maximum) {
        errors.push(`${path}: must be at most ${node.maximum}`)
    }

    if (actual === 'string') {
        if (node.minLength !== undefined && value.length < node.minLength) {
            errors.push(`${path}: must be at least ${node.minLength} characters`)
        }
        if (node.maxLength !== undefined && value.length > node.maxLength) {
            errors.push(`${path}: must be at most ${node.maxLength} characters`)
        }
        if (node.pattern !== undefined && !new RegExp(node.pattern).test(value)) {
            errors.push(`${path}: does not match ${node.pattern}`)
        }
    }

    if (actual === 'array') {
        if (node.minItems !== undefined && value.length < node.minItems) {
            errors.push(`${path}: must contain at least ${node.minItems} items`)
        }
        if (node.uniqueItems === true) {
            const seen = new Set(value.map((item) => JSON.stringify(item)))
            if (seen.size !== value.length) errors.push(`${path}: items must be unique`)
        }
        if (node.maxItems !== undefined && value.length > node.maxItems) {
            errors.push(`${path}: must contain at most ${node.maxItems} items`)
        }
        if (node.items !== undefined) {
            value.forEach((item, index) => {
                validateAgainstSchema(root, node.items, item, `${path}[${index}]`, errors)
            })
        }
    }

    if (actual === 'object') {
        for (const key of node.required ?? []) {
            if (!Object.hasOwn(value, key)) errors.push(`${path}: missing required \`${key}\``)
        }
        const properties = node.properties ?? {}
        if (node.additionalProperties === false) {
            for (const key of Object.keys(value)) {
                if (!Object.hasOwn(properties, key)) {
                    errors.push(`${path}: unknown property \`${key}\``)
                }
            }
        }
        for (const [key, subschema] of Object.entries(properties)) {
            if (Object.hasOwn(value, key)) {
                validateAgainstSchema(root, subschema, value[key], `${path}.${key}`, errors)
            }
        }
    }
}
