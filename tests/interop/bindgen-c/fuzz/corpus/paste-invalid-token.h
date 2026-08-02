/* A token paste whose result is not a valid preprocessing token. clang must
 * refuse this, and the tool must relay the refusal with the cause named and
 * write nothing. */
#ifndef KBFUZZ_PASTE_INVALID_H
#define KBFUZZ_PASTE_INVALID_H

#define KBF_CAT_(a, b) a##b
#define KBF_CAT(a, b) KBF_CAT_(a, b)

/* `+` pasted onto `/` is not a token. */
long kbfuzz_invalid(long KBF_CAT(+, /) value);

#endif
