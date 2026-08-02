/* Token-paste and stringize abuse. Pastes build declarator names out of
 * fragments, stringize turns them into string literals, and a paste is
 * nested inside another paste's argument. Every name the tool sees here was
 * synthesized by the preprocessor and never appears literally in the file:
 * a reader grepping for `kbfuzz_paste_ab` finds nothing. */
#ifndef KBFUZZ_TOKEN_PASTE_H
#define KBFUZZ_TOKEN_PASTE_H

#define KBF_CAT_(a, b) a##b
#define KBF_CAT(a, b) KBF_CAT_(a, b)
#define KBF_CAT3(a, b, c) KBF_CAT(KBF_CAT(a, b), c)
#define KBF_STR_(x) #x
#define KBF_STR(x) KBF_STR_(x)

#define KBF_PREFIX kbfuzz_paste
#define KBF_MID _a
#define KBF_SUFFIX b

/* Expands to: long kbfuzz_paste_ab(long); */
long KBF_CAT3(KBF_PREFIX, KBF_MID, KBF_SUFFIX)(long value);

/* Paste inside a paste argument, four levels of indirection deep. */
#define KBF_NAME KBF_CAT(KBF_CAT(kbfuzz, _paste), _nested)
long KBF_NAME(long value);

/* Stringize of a macro that itself expands to a paste. */
#define KBF_LABEL KBF_STR(KBF_CAT3(KBF_PREFIX, KBF_MID, KBF_SUFFIX))

/* Stringize applied to something containing quotes and backslashes. */
#define KBF_QUOTED KBF_STR("a\"b\\c")

/* A paste whose result is a keyword, used as a type. */
#define KBF_LONG KBF_CAT(lo, ng)
KBF_LONG kbfuzz_paste_keyword(KBF_LONG value);

#endif
