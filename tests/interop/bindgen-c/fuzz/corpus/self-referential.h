/* Self-referential and mutually recursive macros, object-like and
 * function-like. C's "blue paint" rule stops the expansion; the point of the
 * case is that the tool relies on that rather than on its own recursion
 * guard, and still terminates, bounds its output, and reports every macro. */
#ifndef KBFUZZ_SELF_REF_H
#define KBFUZZ_SELF_REF_H

/* Directly self-referential object-like macro. */
#define KBF_SELF KBF_SELF long

/* Mutually recursive object-like macros. */
#define KBF_PING KBF_PONG
#define KBF_PONG KBF_PING

/* Directly self-referential function-like macro. */
#define KBF_LOOP(x) KBF_LOOP(x)

/* Mutually recursive function-like macros. */
#define KBF_EVEN(x) KBF_ODD(x)
#define KBF_ODD(x) KBF_EVEN(x)

/* A self-referential macro used in a real declarator position: the painted
 * name survives as an identifier, so this is a declaration of a function
 * called KBF_SELF returning long — parseable, and audited, not bound. */
long kbfuzz_self_ref(long value);

#endif
