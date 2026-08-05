/* A typedef chain deeper than the 32-link bound the type mapper walks.
 * The header is valid C, so the run succeeds; the declaration that uses
 * the deepest alias must land in the audit with the bound named, never
 * be bound on a guess, and never send the mapper into a loop. */
#ifndef KBFUZZ_DEEP_TYPEDEF_H
#define KBFUZZ_DEEP_TYPEDEF_H

typedef long kbfuzz_t00;
typedef kbfuzz_t00 kbfuzz_t01;
typedef kbfuzz_t01 kbfuzz_t02;
typedef kbfuzz_t02 kbfuzz_t03;
typedef kbfuzz_t03 kbfuzz_t04;
typedef kbfuzz_t04 kbfuzz_t05;
typedef kbfuzz_t05 kbfuzz_t06;
typedef kbfuzz_t06 kbfuzz_t07;
typedef kbfuzz_t07 kbfuzz_t08;
typedef kbfuzz_t08 kbfuzz_t09;
typedef kbfuzz_t09 kbfuzz_t10;
typedef kbfuzz_t10 kbfuzz_t11;
typedef kbfuzz_t11 kbfuzz_t12;
typedef kbfuzz_t12 kbfuzz_t13;
typedef kbfuzz_t13 kbfuzz_t14;
typedef kbfuzz_t14 kbfuzz_t15;
typedef kbfuzz_t15 kbfuzz_t16;
typedef kbfuzz_t16 kbfuzz_t17;
typedef kbfuzz_t17 kbfuzz_t18;
typedef kbfuzz_t18 kbfuzz_t19;
typedef kbfuzz_t19 kbfuzz_t20;
typedef kbfuzz_t20 kbfuzz_t21;
typedef kbfuzz_t21 kbfuzz_t22;
typedef kbfuzz_t22 kbfuzz_t23;
typedef kbfuzz_t23 kbfuzz_t24;
typedef kbfuzz_t24 kbfuzz_t25;
typedef kbfuzz_t25 kbfuzz_t26;
typedef kbfuzz_t26 kbfuzz_t27;
typedef kbfuzz_t27 kbfuzz_t28;
typedef kbfuzz_t28 kbfuzz_t29;
typedef kbfuzz_t29 kbfuzz_t30;
typedef kbfuzz_t30 kbfuzz_t31;
typedef kbfuzz_t31 kbfuzz_t32;
typedef kbfuzz_t32 kbfuzz_t33;
typedef kbfuzz_t33 kbfuzz_t34;
typedef kbfuzz_t34 kbfuzz_t35;
typedef kbfuzz_t35 kbfuzz_t36;
typedef kbfuzz_t36 kbfuzz_t37;
typedef kbfuzz_t37 kbfuzz_t38;
typedef kbfuzz_t38 kbfuzz_t39;
typedef kbfuzz_t39 kbfuzz_t40;
typedef kbfuzz_t40 kbfuzz_t41;
typedef kbfuzz_t41 kbfuzz_t42;
typedef kbfuzz_t42 kbfuzz_t43;
typedef kbfuzz_t43 kbfuzz_t44;
typedef kbfuzz_t44 kbfuzz_t45;
typedef kbfuzz_t45 kbfuzz_t46;
typedef kbfuzz_t46 kbfuzz_t47;
typedef kbfuzz_t47 kbfuzz_t48;

/* Uses the 48th alias: past the 32-link bound. */
kbfuzz_t48 kbfuzz_deep_typedef(kbfuzz_t48 value);

/* Uses a shallow alias: inside the bound, so it stays bindable. */
kbfuzz_t02 kbfuzz_shallow_typedef(kbfuzz_t02 value);

#endif
