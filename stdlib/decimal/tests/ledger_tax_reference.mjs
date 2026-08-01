// Independent scaled-integer reference for the Decimal ledger/tax example.
// This deliberately does not import or execute Kofun's Decimal runtime.

const amounts = [1999n, 575n, 12340n]
const rateNumerator = 825n
const rateDenominator = 10000n

function roundHalfUp(numerator, denominator) {
    const quotient = numerator / denominator
    const remainder = numerator % denominator
    return quotient + (remainder * 2n >= denominator ? 1n : 0n)
}

function cents(value) {
    const sign = value < 0n ? '-' : ''
    const absolute = value < 0n ? -value : value
    return `${sign}${absolute / 100n}.${String(absolute % 100n).padStart(2, '0')}`
}

const taxes = amounts.map((amount) =>
    roundHalfUp(amount * rateNumerator, rateDenominator))
const subtotal = amounts.reduce((sum, amount) => sum + amount, 0n)
const tax = taxes.reduce((sum, amount) => sum + amount, 0n)

process.stdout.write([
    cents(subtotal),
    ...taxes.map(cents),
    cents(tax),
    cents(subtotal + tax),
].join('\n') + '\n')
