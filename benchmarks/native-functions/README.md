# Direct native function benchmark

This benchmark measures the direct x86-64 backend's bounded user-defined
function profile: the recursive integer shapes that profile actually supports,
compiled to a static ELF by `bootstrap/native/core_compiler.c` and executed on
the host. It is a bounded local measurement of one backend against its own
previous revision, not a language or runtime comparison.

Three workloads cover the shapes the profile lowers:

- `fib35.kofun` — recursion, a comparison guard, and checked arithmetic, with
  one value live across a call;
- `mutual_fib32.kofun` — the same tree-recursive work split across two
  mutually recursive functions;
- `six_argument_fib30.kofun` — the same work driven through a six-argument
  call, so every argument register and the whole call boundary is exercised.

Every workload is compiled by the producer under test, executed, and compared
against its expected output before a single sample is taken, so no timing
number can come from a program that computes the wrong answer.

## Method

`cpu_samples.c` forks the measured command, waits for it, and reports the
`wait4` rusage user plus system microseconds. Process CPU time is used rather
than wall time because these workloads are single-threaded, compute-bound, and
short. Each invocation runs the command once as a warm-up and discards it.

Samples are collected one round at a time — current, then baseline, then the C
reference — so machine drift lands on every variant instead of on whichever
one ran last. The default is 11 samples; the median is the sixth sorted sample
and the recorded lowest and highest samples show the dispersion.

Emitted code size is `p_filesz` of the first `PT_LOAD` program header, which is
the actual code, not the page-aligned file size that every image shares.
Compile time is one producer run over all twelve bounded-corpus sources.

Reproduce, comparing against the revision before the register allocator:

```sh
BASELINE=fdb8e6d258312b9222f4dde4df35badee6423d68 SAMPLES=11 \
    sh benchmarks/native-functions/benchmark.sh
```

`BASELINE` accepts any git revision; the harness builds that revision's
`bootstrap/native/core_compiler.c` as a second producer. With `BASELINE` set,
the recorded budgets are enforced and the script fails when one is missed:
fib(35) must improve by at least 25%, no workload may regress by more than 5%,
and neither emitted code nor corpus compile time may regress by more than 10%.
Without `BASELINE` the script reports the current numbers only. `SAMPLES`,
`CC`, and `REFERENCE_CFLAGS` are the other knobs.

## Recorded measurement

`results.json` holds the raw samples behind these medians, measured on an AMD
Ryzen 3 7330U with GCC 16.1.1, comparing #665 against the revision before it:

| Workload | Before | After | Change |
|---|---:|---:|---:|
| `fib35` | 84,250 us | 39,570 us | −53.03% |
| `mutual_fib32` | 20,168 us | 9,905 us | −50.89% |
| `six_argument_fib30` | 15,272 us | 6,758 us | −55.75% |

Emitted code shrank on all three (−2.19%, −4.33%, −4.21%) and corpus compile
time was unchanged at −0.36%, which is inside the run-to-run spread.

The same `fib(35)` written in C and built with `-O3` has a median of 11,034 us
on this host, so the direct backend went from 7.635x to 3.586x that reference.
The remaining distance is not attributable to value placement alone: the Kofun
program branches to a checked-overflow diagnostic after every arithmetic
operation and the C program does not.
