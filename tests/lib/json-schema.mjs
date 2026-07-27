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
    'maxLength', 'pattern',
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

    const actual = typeOf(value)
    if (node.type !== undefined && actual !== node.type) {
        errors.push(`${path}: expected ${node.type}, found ${actual}`)
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
