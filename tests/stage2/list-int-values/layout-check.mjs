#!/usr/bin/env node

import fs from 'node:fs'

function fail(message) {
    throw new Error(message)
}

function requireEqual(label, actual, expected) {
    if (String(actual) !== String(expected)) {
        fail(`${label}: emitted ${actual}, descriptor requires ${expected}`)
    }
}

function constant(source, name) {
    const match = source.match(new RegExp(`\\b${name}\\s*=\\s*([0-9]+)\\b`))
    if (match === null) fail(`emitted C omitted ${name}`)
    return match[1]
}

function asserted(source, expression) {
    const escaped = expression.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
    const match = source.match(new RegExp(`${escaped}\\s*==\\s*([0-9]+)`))
    if (match === null) fail(`emitted C omitted assertion for ${expression}`)
    return match[1]
}

function descriptorFacts(document) {
    const value = document.layouts?.find((entry) => entry.id === 'List[Int]')
    const object = document.objects?.find((entry) => entry.id === 'list-int-three')
    if (value === undefined || object === undefined) {
        fail('AggregateLayout descriptor omitted List[Int] value or object')
    }
    const header = object.header?.[0]
    if (header?.name !== 'length' || header?.type !== 'u64') {
        fail('AggregateLayout List[Int] object omitted its u64 length header')
    }
    if (value.kind !== 'list' || value.drop !== 'managed' ||
        JSON.stringify(value.pointers) !== JSON.stringify(['0'])) {
        fail('AggregateLayout List[Int] value is not one managed reference')
    }
    if (object.kind !== 'list' || object.type !== 'List[Int]' ||
        object.element_type !== 'Int' || object.length !== '3') {
        fail('AggregateLayout list-int-three object identity drifted')
    }
    return {
        referenceSize: value.size,
        referenceAlign: value.align,
        lengthOffset: header.offset,
        headerWidth: header.size,
        payloadOffset: object.payload_offset,
        elementSize: object.element_size,
        elementAlign: object.element_align,
        objectSize: object.size,
    }
}

function check(source, facts) {
    requireEqual(
        'List[Int] reference size',
        asserted(source, 'sizeof(KofunIntList)'),
        facts.referenceSize,
    )
    requireEqual(
        'List[Int] length offset',
        constant(source, 'KOFUN_LIST_INT_LENGTH_OFFSET'),
        facts.lengthOffset,
    )
    requireEqual(
        'List[Int] payload offset',
        constant(source, 'KOFUN_LIST_INT_PAYLOAD_OFFSET'),
        facts.payloadOffset,
    )
    requireEqual(
        'List[Int] element size',
        constant(source, 'KOFUN_LIST_INT_ELEMENT_SIZE'),
        facts.elementSize,
    )
    requireEqual(
        'List[Int] object alignment',
        asserted(source, '_Alignof(KofunIntListLayoutProbe)'),
        facts.elementAlign,
    )
    const literal = source.match(
        /struct \{ uint64_t length; int64_t elements\[3\]; \}\)\{UINT64_C\(3\)/,
    )
    if (literal === null) fail('three-element literal did not use an exact-size object')
    const computedSize = BigInt(facts.payloadOffset) +
        BigInt(facts.elementSize) * 3n
    requireEqual('list-int-three object size', computedSize, facts.objectSize)
    requireEqual('List[Int] header width', facts.headerWidth, '8')
    requireEqual('List[Int] reference alignment', facts.referenceAlign, '8')
}

function main() {
    const [descriptorPath, sourcePath, mode = 'check'] = process.argv.slice(2)
    if (descriptorPath === undefined || sourcePath === undefined) {
        fail('usage: layout-check.mjs DESCRIPTOR GENERATED_C [check|mutate-payload]')
    }
    const descriptor = JSON.parse(fs.readFileSync(descriptorPath, 'utf8'))
    const source = fs.readFileSync(sourcePath, 'utf8')
    const facts = descriptorFacts(descriptor)
    check(source, facts)
    if (mode === 'check') return
    if (mode !== 'mutate-payload') fail(`unknown mode ${mode}`)
    const original = `KOFUN_LIST_INT_PAYLOAD_OFFSET = ${facts.payloadOffset}`
    const drifted = `KOFUN_LIST_INT_PAYLOAD_OFFSET = ${
        BigInt(facts.payloadOffset) + BigInt(facts.elementSize)
    }`
    const first = source.indexOf(original)
    if (first < 0 || source.indexOf(original, first + original.length) >= 0) {
        fail('payload-offset mutation did not have exactly one target')
    }
    process.stdout.write(source.replace(original, drifted))
}

try {
    main()
} catch (error) {
    console.error(`list-int-layout: ${error.message}`)
    process.exit(1)
}
