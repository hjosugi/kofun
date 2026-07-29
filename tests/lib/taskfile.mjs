// The task set the repository's manifests resolve their commands against.
//
// `release/claims.json` and `rfcs/index.json` both name the gate that proves a
// claim or an implementation, and both must reject a command naming a task
// that does not exist. They carried an identical copy of this reader while the
// entry point was a Makefile; identical copies cannot disagree, so no gate
// could catch a drift between them (DD-022). One reader, imported twice.
//
// The Taskfile this parses is generated to a fixed shape: task names are the
// only two-space-indented `name:` keys under the top-level `tasks:` mapping.
// Parsing that shape directly keeps the validators dependency-free, which is
// the same reason `json-schema.mjs` exists beside this file.

import { readFileSync } from 'node:fs'

const TASKFILE = 'Taskfile.yml'

export function taskfileTasks(repositoryRoot) {
    const source = readFileSync(`${repositoryRoot}/${TASKFILE}`, 'utf8')
    const tasks = new Set()
    let inTasks = false
    for (const line of source.split('\n')) {
        if (/^tasks:\s*$/.test(line)) {
            inTasks = true
            continue
        }
        if (!inTasks) continue
        // A non-indented, non-blank line ends the mapping.
        if (line.trim() !== '' && !/^\s/.test(line)) break
        const match = /^ {2}([A-Za-z][A-Za-z0-9_-]*):\s*$/.exec(line)
        if (match) tasks.add(match[1])
    }
    if (tasks.size === 0) {
        throw new Error(`${TASKFILE} declares no tasks; the parser or the file is wrong`)
    }
    return tasks
}

export const TASKFILE_PATH = TASKFILE
