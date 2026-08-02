/* A macro flood that expands to more top-level declarations than the
 * generator will walk. The refusal must name the bound, and must leave
 * no output directory behind. */
#ifndef KBFUZZ_DECLARATION_FLOOD_H
#define KBFUZZ_DECLARATION_FLOOD_H

#define KBF_CAT_(a, b) a##b
#define KBF_CAT(a, b) KBF_CAT_(a, b)

#define KBF_D16(p) long KBF_CAT(p, 0)(long); long KBF_CAT(p, 1)(long); long KBF_CAT(p, 2)(long); long KBF_CAT(p, 3)(long); long KBF_CAT(p, 4)(long); long KBF_CAT(p, 5)(long); long KBF_CAT(p, 6)(long); long KBF_CAT(p, 7)(long); long KBF_CAT(p, 8)(long); long KBF_CAT(p, 9)(long); long KBF_CAT(p, a)(long); long KBF_CAT(p, b)(long); long KBF_CAT(p, c)(long); long KBF_CAT(p, d)(long); long KBF_CAT(p, e)(long); long KBF_CAT(p, f)(long);
#define KBF_D256(p) KBF_D16(KBF_CAT(p, 0)) KBF_D16(KBF_CAT(p, 1)) KBF_D16(KBF_CAT(p, 2)) KBF_D16(KBF_CAT(p, 3)) KBF_D16(KBF_CAT(p, 4)) KBF_D16(KBF_CAT(p, 5)) KBF_D16(KBF_CAT(p, 6)) KBF_D16(KBF_CAT(p, 7)) KBF_D16(KBF_CAT(p, 8)) KBF_D16(KBF_CAT(p, 9)) KBF_D16(KBF_CAT(p, a)) KBF_D16(KBF_CAT(p, b)) KBF_D16(KBF_CAT(p, c)) KBF_D16(KBF_CAT(p, d)) KBF_D16(KBF_CAT(p, e)) KBF_D16(KBF_CAT(p, f))

/* 17 * 256 = 4352 declarations, past the 4096 walk bound. */
KBF_D256(KBF_CAT(kbfuzz_flood_0, _))
KBF_D256(KBF_CAT(kbfuzz_flood_1, _))
KBF_D256(KBF_CAT(kbfuzz_flood_2, _))
KBF_D256(KBF_CAT(kbfuzz_flood_3, _))
KBF_D256(KBF_CAT(kbfuzz_flood_4, _))
KBF_D256(KBF_CAT(kbfuzz_flood_5, _))
KBF_D256(KBF_CAT(kbfuzz_flood_6, _))
KBF_D256(KBF_CAT(kbfuzz_flood_7, _))
KBF_D256(KBF_CAT(kbfuzz_flood_8, _))
KBF_D256(KBF_CAT(kbfuzz_flood_9, _))
KBF_D256(KBF_CAT(kbfuzz_flood_a, _))
KBF_D256(KBF_CAT(kbfuzz_flood_b, _))
KBF_D256(KBF_CAT(kbfuzz_flood_c, _))
KBF_D256(KBF_CAT(kbfuzz_flood_d, _))
KBF_D256(KBF_CAT(kbfuzz_flood_e, _))
KBF_D256(KBF_CAT(kbfuzz_flood_f, _))
KBF_D256(KBF_CAT(kbfuzz_flood_undefined, _))

#endif
