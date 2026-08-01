#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
WORK=${KOFUN_TASK_HELP_WORK:-"$ROOT/build/task-help"}
ASSERT_CONTEXT='task-help'
. "$ROOT/tests/assertions/assert.sh"

for tool in node task cmp
do
    command -v "$tool" >/dev/null 2>&1 || {
        printf '%s\n' "task-help gate requires $tool" >&2
        exit 1
    }
done

rm -rf "$WORK"
mkdir -p "$WORK"

node --check "$ROOT/tooling/task-help.mjs"
node "$ROOT/tooling/task-help.mjs" --check >"$WORK/check.stdout"
NO_COLOR=1 node "$ROOT/tooling/task-help.mjs" >"$WORK/direct.stdout"
NO_COLOR=1 task --dir "$ROOT" help >"$WORK/help.stdout"
NO_COLOR=1 task --dir "$ROOT" >"$WORK/default.stdout"

assert_grep "classification result" \
    -Eq '^PASS: [0-9]+ documented tasks belong to exactly one help group$' \
    "$WORK/check.stdout"
assert_grep "quick-start group" -Fxq 'Start here' "$WORK/direct.stdout"
assert_grep "compiler group" -Fxq 'Compiler and self-hosting' "$WORK/direct.stdout"
assert_grep "tooling group" -Fxq 'Tooling and developer UX' "$WORK/direct.stdout"
assert_grep "release group" -Fxq 'Repository and release' "$WORK/direct.stdout"
assert_grep "tour row" -Eq '^  task tour +Verify the no-install browser learning tour$' \
    "$WORK/direct.stdout"
assert_grep "full-list hint" -Fq 'task --list' "$WORK/direct.stdout"
assert_not_grep "non-TTY ANSI" -Fq "$(printf '\033')" "$WORK/direct.stdout"

if ! cmp -s "$WORK/direct.stdout" "$WORK/help.stdout"; then
    assert_fail "task help differs from the renderer"
fi
if ! cmp -s "$WORK/direct.stdout" "$WORK/default.stdout"; then
    assert_fail "bare task differs from task help"
fi

printf '%s\n' \
    'PASS: bare task and task help render one grouped, self-documenting guide' \
    'PASS: every visible task is described and assigned to exactly one group'
