# Deterministic benchmark-summary fixture

This is the executable summary slice tracked by
[#859](https://github.com/hjosugi/kofun/issues/859), split from #847. It proves
the six v1 summary calculations without claiming that Stage 2 can already
execute the canonical `List[Int]` and `Text` report producer.

The committed raw vector is deliberately unsorted:

```text
41, 7, 19, 3, 23, 11, 29, 17
```

The Kofun producer sorts it with a fixed 19-comparator network. Under the
nearest-rank rule in `docs/stdlib/benchmark.md`, the ascending vector
`3, 7, 11, 17, 19, 23, 29, 41` yields:

| field | value | evidence |
|---|---:|---|
| min | 3 | rank 1 |
| max | 41 | rank 8 |
| median | 17 | rank 4, the lower middle |
| p25 | 7 | rank 2 |
| p75 | 23 | rank 6 |
| MAD | 6 | rank 4 of sorted deviations `0, 2, 6, 6, 10, 12, 14, 24` |

`Samples8` is a Stage 2 evidence record, not the public benchmark API. The
canonical raw-sample codec and byte fixture remain blocked on one executable
profile supporting records, `List[Int]`, and `Text`; #847 and #646 remain open
for that boundary.

Run the focused gate with:

```sh
sh tests/stdlib/benchmark-summary/check.sh
```

The gate checks named golden fields, reference/C11 byte equality, typed-HIR
completeness, repeat determinism, and absence of ambient time, file, network,
or randomness calls in the emitted program.
