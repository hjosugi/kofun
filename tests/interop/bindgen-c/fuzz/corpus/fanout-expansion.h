/* High fan-out expansion. Each level repeats the level below it eight times,
 * so one use of KBF_F4 expands to 8^5 = 32768 tokens from three characters of
 * source. The expansion is *used*, as an enum constant's value, so the volume
 * travels through the preprocessor, the AST, and the report rather than
 * sitting in an unreferenced #define.
 *
 * The tool must bound the captured output rather than discover its size by
 * running out of memory, and must still finish inside its wall-clock bound. */
#ifndef KBFUZZ_FANOUT_H
#define KBFUZZ_FANOUT_H

#define KBF_F0(x) x + x + x + x + x + x + x + x
#define KBF_F1(x) KBF_F0(x) + KBF_F0(x) + KBF_F0(x) + KBF_F0(x) + KBF_F0(x) + KBF_F0(x) + KBF_F0(x) + KBF_F0(x)
#define KBF_F2(x) KBF_F1(x) + KBF_F1(x) + KBF_F1(x) + KBF_F1(x) + KBF_F1(x) + KBF_F1(x) + KBF_F1(x) + KBF_F1(x)
#define KBF_F3(x) KBF_F2(x) + KBF_F2(x) + KBF_F2(x) + KBF_F2(x) + KBF_F2(x) + KBF_F2(x) + KBF_F2(x) + KBF_F2(x)
#define KBF_F4(x) KBF_F3(x) + KBF_F3(x) + KBF_F3(x) + KBF_F3(x) + KBF_F3(x) + KBF_F3(x) + KBF_F3(x) + KBF_F3(x)

/* 32768 additions of 1, folded by clang into a single enum constant the
 * report has to carry. A generator that stopped bounding its input would
 * either hang here or record a value it never computed. */
enum kbfuzz_fanout {
    KBF_FANOUT_ZERO = 0,
    KBF_FANOUT_BIG = KBF_F4(1)
};

long kbfuzz_fanout_probe(long value);

#endif
