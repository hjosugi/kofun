/* Deeply nested object-like macros: each level names the one below it, so
 * the final use expands through 64 definitions before it becomes a token.
 * Must terminate, must be bounded, and must produce the same bytes twice. */
#ifndef KBFUZZ_DEEP_OBJECT_H
#define KBFUZZ_DEEP_OBJECT_H

#define KBF_D00 long
#define KBF_D01 KBF_D00
#define KBF_D02 KBF_D01
#define KBF_D03 KBF_D02
#define KBF_D04 KBF_D03
#define KBF_D05 KBF_D04
#define KBF_D06 KBF_D05
#define KBF_D07 KBF_D06
#define KBF_D08 KBF_D07
#define KBF_D09 KBF_D08
#define KBF_D10 KBF_D09
#define KBF_D11 KBF_D10
#define KBF_D12 KBF_D11
#define KBF_D13 KBF_D12
#define KBF_D14 KBF_D13
#define KBF_D15 KBF_D14
#define KBF_D16 KBF_D15
#define KBF_D17 KBF_D16
#define KBF_D18 KBF_D17
#define KBF_D19 KBF_D18
#define KBF_D20 KBF_D19
#define KBF_D21 KBF_D20
#define KBF_D22 KBF_D21
#define KBF_D23 KBF_D22
#define KBF_D24 KBF_D23
#define KBF_D25 KBF_D24
#define KBF_D26 KBF_D25
#define KBF_D27 KBF_D26
#define KBF_D28 KBF_D27
#define KBF_D29 KBF_D28
#define KBF_D30 KBF_D29
#define KBF_D31 KBF_D30
#define KBF_D32 KBF_D31
#define KBF_D33 KBF_D32
#define KBF_D34 KBF_D33
#define KBF_D35 KBF_D34
#define KBF_D36 KBF_D35
#define KBF_D37 KBF_D36
#define KBF_D38 KBF_D37
#define KBF_D39 KBF_D38
#define KBF_D40 KBF_D39
#define KBF_D41 KBF_D40
#define KBF_D42 KBF_D41
#define KBF_D43 KBF_D42
#define KBF_D44 KBF_D43
#define KBF_D45 KBF_D44
#define KBF_D46 KBF_D45
#define KBF_D47 KBF_D46
#define KBF_D48 KBF_D47
#define KBF_D49 KBF_D48
#define KBF_D50 KBF_D49
#define KBF_D51 KBF_D50
#define KBF_D52 KBF_D51
#define KBF_D53 KBF_D52
#define KBF_D54 KBF_D53
#define KBF_D55 KBF_D54
#define KBF_D56 KBF_D55
#define KBF_D57 KBF_D56
#define KBF_D58 KBF_D57
#define KBF_D59 KBF_D58
#define KBF_D60 KBF_D59
#define KBF_D61 KBF_D60
#define KBF_D62 KBF_D61
#define KBF_D63 KBF_D62

KBF_D63 kbfuzz_deep(KBF_D63 value);

#endif
