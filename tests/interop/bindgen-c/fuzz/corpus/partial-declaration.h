/* A macro that expands to a partial, unbalanced declaration: the opening
 * brace of a struct with no closing brace, and a parameter list that is
 * never closed. clang must refuse the translation unit; the tool must relay
 * the refusal, name clang as the cause, and leave no output directory. */
#ifndef KBFUZZ_PARTIAL_DECLARATION_H
#define KBFUZZ_PARTIAL_DECLARATION_H

#define KBF_OPEN_RECORD struct kbfuzz_partial {
#define KBF_OPEN_PARAMS long kbfuzz_unclosed(long a,

KBF_OPEN_RECORD
    long field;

KBF_OPEN_PARAMS

#endif
