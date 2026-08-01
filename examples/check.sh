#!/usr/bin/env sh
set -eu

# Binds every example to the check that owns it.
#
# `docs/REPOSITORY_GUIDE.md` says an example is owned by "the example plus its
# nearest check", but eleven files had no check at all: the Tree-sitter corpus
# gate proves a file parses and nothing proved anything else about it. This
# gate reads the table in `examples/README.md` and enforces what each row
# claims, so an example cannot be added without declaring how it is checked and
# an `illustrative` row cannot quietly stay illustrative after the feature it
# waits for lands.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EXAMPLES="$ROOT/examples"
README="$EXAMPLES/README.md"
WORK=${KOFUN_EXAMPLES_WORK:-"$ROOT/build/examples-check"}

# The one law-evidence artifact whose source binding is already broken. It is
# named here, not repaired, because no producer exists to reproduce the result
# it records. See the "Known evidence debt" section of examples/README.md.
EVIDENCE_DEBT="artifacts/optional-bool-monad.evidence.json"

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

command -v node >/dev/null 2>&1 || fail 'node is required'
test -f "$README" || fail "missing $README"

rm -rf "$WORK"
mkdir -p "$WORK"

# ------------------------------------------------------------------- table
# Rows live between the `## Examples` heading and the next heading, so the
# vocabulary table above it is not mistaken for data.
sed -n '/^## Examples$/,/^## /p' "$README" |
    sed -n 's/^| `\([^`]*\)` | \([a-z]*\) | `\([^`]*\)` |$/\1\t\2\t\3/p' \
    >"$WORK/rows.tsv"

test -s "$WORK/rows.tsv" || fail 'no example rows parsed out of README.md'

cut -f1 "$WORK/rows.tsv" | sort >"$WORK/declared.txt"
sort -u "$WORK/declared.txt" >"$WORK/declared.unique.txt"
cmp -s "$WORK/declared.txt" "$WORK/declared.unique.txt" ||
    fail 'an example is listed more than once in README.md'

(cd "$EXAMPLES" && find . -name '*.kofun' |
    sed 's|^\./||' |
    grep -v '^rust-shim/vendor/' |
    sort) >"$WORK/present.txt"

if ! cmp -s "$WORK/declared.txt" "$WORK/present.txt"; then
    printf 'FAIL: examples/README.md and examples/ disagree\n' >&2
    printf '  listed but absent:\n' >&2
    comm -23 "$WORK/declared.txt" "$WORK/present.txt" | sed 's/^/    /' >&2
    printf '  present but unlisted:\n' >&2
    comm -13 "$WORK/declared.txt" "$WORK/present.txt" | sed 's/^/    /' >&2
    exit 1
fi

echo "PASS: every example under examples/ is declared exactly once"

# ------------------------------------------------------------------- rows
owned=0
ran=0
illustrative=0

while IFS="$(printf '\t')" read -r example status evidence; do
    source_path="$EXAMPLES/$example"
    test -f "$source_path" || fail "$example: declared file does not exist"

    case $status in
    owned)
        owner="$ROOT/$evidence"
        test -f "$owner" ||
            fail "$example: owner check $evidence does not exist"
        # The owner has to still name the example. A check that stopped
        # referring to its example is exactly the drift this row asserts
        # against.
        grep -Fq "$(basename "$example")" "$owner" ||
            fail "$example: $evidence no longer names it"
        owned=$((owned + 1))
        ;;
    runs)
        expected="$EXAMPLES/$evidence"
        test -f "$expected" ||
            fail "$example: expected-output file $evidence does not exist"
        binary="$WORK/$(basename "$example" .kofun)"
        "$ROOT/bin/kofun" build "examples/$example" -o "$binary" \
            >"$WORK/build.log" 2>&1 ||
            {
                cat "$WORK/build.log" >&2
                fail "$example: declared as running but does not build"
            }
        "$binary" >"$WORK/actual" 2>"$WORK/actual.stderr" ||
            fail "$example: declared as running but exited non-zero"
        test ! -s "$WORK/actual.stderr" ||
            fail "$example: wrote unexpected stderr"
        cmp -s "$expected" "$WORK/actual" ||
            fail "$example: output differs from $evidence"
        ran=$((ran + 1))
        ;;
    illustrative)
        set +e
        "$ROOT/bin/kofun" check "examples/$example" \
            >"$WORK/check.out" 2>&1
        check_status=$?
        set -e
        # A file that starts checking cleanly is not a failure of the
        # example, it is a signal that the slice grew. Say so, because
        # "expected an error and got success" reads as nonsense otherwise.
        test "$check_status" -ne 0 ||
            fail "$example: now checks cleanly; move it to \`runs\` in examples/README.md and commit its expected output"
        grep -Fq "$evidence" "$WORK/check.out" ||
            fail "$example: expected $evidence, got: $(head -1 "$WORK/check.out")"
        illustrative=$((illustrative + 1))
        ;;
    *)
        fail "$example: unknown status \`$status\`"
        ;;
    esac
done <"$WORK/rows.tsv"

printf 'PASS: %d owned examples are still named by the check that owns them\n' \
    "$owned"
printf 'PASS: %d runnable examples build, run, and match their expected output\n' \
    "$ran"
printf 'PASS: %d illustrative examples still stop at the boundary they name\n' \
    "$illustrative"

# ------------------------------------------------------------ diagnostics
# An example that names a diagnostic code is making a claim about what the
# compiler says. `ownership.kofun` cited `E330` from a retired numbering
# scheme — no `E3xx` code has ever been in the registry — and a comment is
# exactly where that survives unread.
registry="$ROOT/tests/diagnostics/registry.tsv"
test -f "$registry" || fail "missing $registry"
cited=0
for code in $(grep -rhoE '\bE[0-9]{3}\b|\bE2S[0-9]+\b|\bR[0-9]{3}\b' \
    "$EXAMPLES" --include='*.kofun' | sort -u); do
    grep -q "^$code	" "$registry" ||
        fail "an example cites unregistered diagnostic code \`$code\`; see tests/diagnostics/registry.tsv"
    cited=$((cited + 1))
done
printf 'PASS: every diagnostic code cited by an example is registered (%d)\n' "$cited"

# -------------------------------------------------------------- evidence
# An evidence artifact that records `source.sha256` for a file under
# examples/ is claiming a result about exactly those bytes. Nothing compared
# the two before, and one artifact had already drifted.
cd "$ROOT"
node - "$EVIDENCE_DEBT" <<'NODE'
import { readFileSync, readdirSync, statSync } from 'node:fs'
import { createHash } from 'node:crypto'
import { join } from 'node:path'

const debt = process.argv[2]
const collect = (directory) =>
    readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
        const path = join(directory, entry.name)
        if (entry.isDirectory()) return collect(path)
        return entry.name.endsWith('.json') ? [path] : []
    })

const drifted = []
let checked = 0
let carried = 0

for (const path of collect('artifacts')) {
    let evidence
    try {
        evidence = JSON.parse(readFileSync(path, 'utf8'))
    } catch {
        continue
    }
    const source = evidence?.source
    if (!source?.path || !source?.sha256) continue
    if (!source.path.startsWith('examples/')) continue

    statSync(source.path)
    const actual = createHash('sha256')
        .update(readFileSync(source.path))
        .digest('hex')
    if (actual === source.sha256) {
        checked += 1
        continue
    }
    if (path === debt) {
        carried += 1
        console.log(
            `NOTE: ${path} still records a stale hash for ${source.path}; ` +
                'carried as a named debt, see examples/README.md (#864)',
        )
        continue
    }
    drifted.push(`${path}: ${source.path} hashes to ${actual}, not ${source.sha256}`)
}

if (drifted.length > 0) {
    for (const line of drifted) process.stderr.write(`FAIL: ${line}\n`)
    process.exit(1)
}

console.log(
    `PASS: ${checked} example-backed evidence artifacts match their source, ` +
        `${carried} carried as a named debt`,
)
NODE

echo 'examples check passed'
