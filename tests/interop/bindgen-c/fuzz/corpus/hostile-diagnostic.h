/* A header whose diagnostic is itself the attack: #error carrying shell
 * metacharacters, a path-shaped string, and an ANSI escape. None of it may
 * reach a shell, and none of it may be echoed unbounded — the tool truncates
 * captured diagnostics and passes clang a structured argv, so this text is
 * data on its way to stderr and nothing else. */
#ifndef KBFUZZ_HOSTILE_DIAGNOSTIC_H
#define KBFUZZ_HOSTILE_DIAGNOSTIC_H

#define KBF_ANGRY "; rm -rf / ; $(id) `id` && echo /etc/passwd"

#error kbfuzz hostile diagnostic ; rm -rf / ; $(id) `id` ../../escape

long kbfuzz_never_reached(long value);

#endif
