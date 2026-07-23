# Benchmark harness contract

Status: accepted v1 design.

Issue: [#640](https://github.com/hjosugi/kofun/issues/640).

Tier: portable standard-library developer/testing support with target-specific
counter providers.

## Outcome

Kofun provides a first-party way to define, run, and report microbenchmarks
without confusing wall time, process CPU time, monotonic elapsed time,
allocations, runtime counters, or profiler output.

The library defines cases and metrics. `kofun bench` owns process isolation,
configuration, execution, canonical report writing, and comparison. Profilers
and runtime counter providers remain separate subsystems behind an optional
metrics interface.

Every report retains raw samples. The harness never silently removes outliers
or turns a noisy local observation into a general performance claim.

## Public model

Conceptual API:

```text
BenchmarkSuite.new(name: Text) -> BenchmarkSuite

suite.case(
    name: Text,
    setup: fn() -> Result[State, Error],
    measure: fn(edit Bench, edit State) -> Result[Value, Error],
    teardown: fn(take State) -> Result[Void, Error],
)

bench.iter(fn() -> Result[Value, Error])
bench.iter_batched(
    batch_setup: fn() -> Result[Input, Error],
    operation: fn(take Input) -> Result[Value, Error],
)
bench.consume(value: T) -> T
bench.metric(name: Text, value: MetricValue, unit: MetricUnit)
```

`setup` runs once before warmup. `teardown` runs exactly once after successful
setup, including benchmark failure and cancellation. Its failure is reported
without replacing an earlier failure.

`iter` owns the calibrated loop. User code does not inspect or choose the
iteration count from inside the operation.

`iter_batched` excludes per-iteration input construction from the measured
region and prevents state reuse from changing the workload accidentally.

## Required benchmark examples

The syntax is conceptual until the public module naming is finalized. These
placements and metric outcomes are normative.

### Pure computation

```text
suite.case("fib/20", setup: fn() { 20 }, measure: fn(edit bench, edit n) {
    bench.iter(fn() { bench.consume(fib(*n)) })
})
```

The returned value crosses the anti-elision boundary on every iteration. No
allocation, I/O, or clock effect is inferred merely from `consume`.

### Allocation-heavy operation

```text
suite.case("render/1024", setup: fn() { Template.load(fixture) },
    measure: fn(edit bench, edit template) {
        bench.iter(fn() {
            bench.consume(template.render(repeated_values(1024)))
        })
    })
```

The resulting bytes are consumed inside the measured loop. Fixture loading is
outside warmup and measurement. Allocation metrics are recorded only when an
explicit provider supplies them.

### Per-iteration setup outside the measured operation

```text
bench.iter_batched(
    batch_setup: fn() { shuffled_input(read seed_data) },
    operation: fn(take input) { bench.consume(sort(take input)) },
)
```

`batch_setup` runs once per operation but is excluded from elapsed and counter
samples. The affine input prevents an already-sorted value from being reused.

### Parameterized cases

```text
for size in [16, 256, 4096] {
    suite.case(Parameter("lookup", {"size": size}), ...)
}
```

The canonical parameter identity, not presentation order or formatted display
text, participates in the workload identity and report ordering.

### Unavailable counter

```text
allocations: unavailable(
    provider: "runtime.allocations/v1",
    reason: "unsupported-backend",
)
```

This is different from `allocations = 0`. A comparison may compare elapsed
samples while reporting that the allocation dimension is unavailable; it must
not manufacture a zero or silently drop the dimension.

## Runner responsibilities

`kofun bench`:

- discovers named benchmark suites through explicit declarations;
- selects cases and parameter sets;
- builds the requested backend/optimization profile;
- launches a fresh benchmark process unless an explicit in-process diagnostic
  mode is selected;
- passes the exact budget/configuration;
- captures toolchain, source, artifact, target, backend, and host identities;
- writes a canonical versioned report atomically;
- exits nonzero and does not publish a successful report on failure.

The library does not read CLI arguments, environment variables, CPU affinity,
or output paths itself.

## Clock and metric identities

Mandatory timing uses an injected monotonic clock and reports:

```text
elapsed_wall_ns
```

The name means monotonic wall elapsed time for the measured batch. It is never
labeled CPU time.

Optional providers may report:

```text
process_cpu_ns
thread_cpu_ns
allocations
allocated_bytes
peak_live_bytes
gc_cycles
instructions
cycles
cache_misses
custom metrics
```

Every value carries a stable unit and provider identity. Missing counters are
recorded as `unavailable(reason)`, not zero. A provider states whether a metric
is per-process, per-thread, per-batch, or per-operation.

Profiler traces are not benchmark metrics. Starting a profiler changes the run
profile and produces a separately identified diagnostic run.

## Configuration

V1 configuration:

```text
warmup_min_iterations: 3
warmup_min_time: 250 ms
sample_count: 30
target_sample_time: 10 ms
max_iterations_per_sample: 1_000_000_000
max_case_time: 10 s
overhead_samples: 20
timeout: 30 s
```

All values are explicit in the report. The runner may accept overrides, but
rejects zero, negative, overflowed, or inconsistent budgets.

For slow cases, one iteration may exceed `target_sample_time`; it remains a
valid one-iteration sample if within `max_case_time` and `timeout`.

## Calibration

Calibration uses the monotonic clock:

1. execute one untimed correctness iteration after setup;
2. execute warmup batches until both minimum iteration and time budgets are
   reached;
3. start with one measured iteration and double the batch count until elapsed
   time meets `target_sample_time`, the iteration cap is reached, or doubling
   would overflow;
4. keep that fixed iteration count for every reported sample;
5. collect exactly `sample_count` successful samples.

Warmup values are not included in reported samples. Calibration decisions and
observed warmup totals are recorded.

The same fixed synthetic clock stream must produce the same chosen iteration
count and report bytes.

## Measurement region

For each sample:

1. reset optional counters;
2. read the monotonic start;
3. execute exactly the calibrated number of operations;
4. consume every returned value;
5. read the monotonic end;
6. read optional counters;
7. append one raw sample.

Clock reads and provider reads are outside the repeated operation but inside
the sample envelope as specified above. Their measured overhead is calibrated
separately.

The harness does not subtract overhead automatically. Subtraction can create
negative or misleading values for tiny operations. It records the raw overhead
samples and their median so readers can judge whether the workload is too
small. A comparison may reject a case whose median elapsed value is not at
least ten times the median empty-harness overhead.

## Anti-elision

`bench.consume(value)` has ordinary identity semantics: it returns the same
value. During a benchmark build, the compiler treats its input and result as an
opaque observable boundary:

- the value must be computed;
- the operation cannot be deleted solely because the result is unused;
- the call does not imply allocation, I/O, mutation, or an arbitrary
  optimization barrier for unrelated code.

The benchmark profile records the compiler intrinsic/schema version. Normal
application builds may expose `consume` only inside the benchmark module.

The harness cannot prove a workload is representative. Reviewers must still
inspect generated code and workload design when results matter.

## Failures and cancellation

A setup, operation, metric, teardown, clock, provider, or serialization failure
terminates the case. Cancellation is checked between iterations and samples.

The runner:

- exits nonzero;
- atomically withholds the final successful report;
- may write a separately named diagnostic artifact with
  `status = failed|cancelled`;
- never compares a partial case with a completed baseline;
- preserves teardown failures as additional diagnostics;
- kills an unresponsive isolated child at the explicit timeout.

Panics/crashes record process status and captured bounded stderr, with secrets
and arbitrary environment data excluded.

## Raw sample schema

Canonical JSON uses:

```json
{
  "schema": "kofun.benchmark-report/v1",
  "status": "complete",
  "suite": "text",
  "case": "grapheme_count/ascii-1k",
  "workload_identity": "sha256:...",
  "configuration": {
    "warmup_min_iterations": 3,
    "warmup_min_time_ns": 250000000,
    "sample_count": 30,
    "target_sample_time_ns": 10000000,
    "iterations_per_sample": 4096
  },
  "identity": {
    "compiler": "Kofun ...",
    "compiler_digest": "sha256:...",
    "source_digest": "sha256:...",
    "artifact_digest": "sha256:...",
    "target": "x86_64-linux",
    "backend": "native",
    "optimization": "release",
    "host_os": "...",
    "host_arch": "x86_64",
    "cpu": "...",
    "logical_cpu_count": 8
  },
  "providers": [],
  "overhead_samples_ns": [100, 101, 99],
  "samples": [
    {
      "index": 0,
      "iterations": 4096,
      "elapsed_wall_ns": 12000000,
      "metrics": {}
    }
  ],
  "summary": {}
}
```

The real arrays contain the configured counts. Integers are used for time and
count metrics. Floating custom metrics must be finite and have a canonical
JSON representation.

The report records CPU affinity, governor/frequency information, container/VM
status, load, and power mode when explicitly available. Unknown metadata is
`unavailable`; the runner does not require elevated privileges or modify host
settings.

Timestamps are optional metadata and never part of workload identity.

## Workload identity

`workload_identity` hashes:

- canonical suite/case/parameter identity;
- benchmark source and relevant data digests;
- benchmark API/schema version;
- requested backend/target/optimization;
- provider configuration that can change measured behavior.

Host identity is not part of the workload identity. It is a comparison
compatibility dimension.

## Summary rules

All summaries derive from per-operation values:

```text
elapsed_wall_ns / iterations
metric_value / iterations, for metrics declared per-operation
```

V1 reports:

- minimum;
- maximum;
- median;
- p25 and p75 by nearest-rank;
- median absolute deviation (MAD);
- sample count and iterations per sample.

Sorting uses total ordering on finite values. For an even sample count, median
is the checked arithmetic mean of the two central values. Nearest-rank
percentile `p` selects `ceil(p * n)` with one-based indexing.

MAD is the median of absolute deviations from the median. No sample is removed.
Potential outliers may be annotated using an explicit rule, but remain in raw
arrays and all primary summaries.

V1 does not emit a confidence interval or statistical-significance claim.
A later comparison method must name its assumptions and preserve raw inputs.

## Comparison

`kofun bench compare baseline.json candidate.json` first checks:

- complete status;
- matching workload identity or an explicit reviewed mapping;
- compatible target/backend/optimization;
- compatible clock/provider meanings;
- matching iteration semantics and metric units;
- host differences displayed prominently.

It reports absolute and relative median differences plus both raw summaries.
It does not label a regression or improvement unless the caller supplied a
threshold policy, for example:

```text
median regression > 5% and > 5 * max(MAD_baseline, MAD_candidate)
```

That example is not the universal default. Repositories commit the threshold
appropriate to each gate.

## Parameterized and parallel cases

Case names include a canonical parameter identity. Parameters are declared
data, not arbitrary formatted strings.

Parallel benchmarks are a later extension. They must report wall elapsed time
for the entire batch, the exact worker count, scheduling policy, and whether
state is shared. They cannot relabel summed thread CPU time as wall time.

## Deterministic schema fixture

A schema/summarizer gate uses fixed synthetic:

- identity metadata;
- clock values;
- iteration count;
- overhead samples;
- raw elapsed/allocation/custom metrics;
- unavailable provider reason.

It checks byte-exact canonical JSON and hand-computed min/max/median/p25/p75/MAD.
It also covers overflow, NaN/infinity, missing samples, inconsistent iteration
counts, failed status, invalid units, and atomic output replacement.

Live smoke tests only check that valid samples are collected and labeled. They
do not use absolute performance thresholds in portable CI.

## Implementation slices

1. Report schema, canonical codec, and pure synthetic summarizer.
2. Monotonic single-process runner with setup/measure/teardown and anti-elision.
3. Optional allocation and VM/native counter providers from #398/#476.
4. Baseline comparison and repository threshold policies.
5. Parallel benchmark extension.

The first implementation issue is slice 1. It can proceed before every runtime
counter exists.

## Sources

- [Go `testing` benchmark API](https://pkg.go.dev/testing)
- [Kofun HTTP benchmark methodology](../../benchmarks/http/README.md)
