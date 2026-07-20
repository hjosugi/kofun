# Rendering cost checkpoint

`tests/benchmark.c` renders 180,000 120-column truecolor progress frames in
nine batches and reports the median nanoseconds per frame. It exercises UTF-8
decoding (CJK plus a combining sequence), layout, color lowering, and bounded
buffer output. The gate is a deliberately conservative 2 ms per frame, leaving
14.7 ms of a 60 Hz frame on even a slow acceptance runner.

On 2026-07-21 the repository runner measured **3,276 ns/frame** with the normal
`cc -O2` gate. At 60 frames/second this renderer would consume about 0.02% of
one CPU second; normal build progress renders much less frequently. This is a
renderer-only measurement and excludes terminal write latency, which is why
the interactive session emits one bounded frame and append-only mode emits one
line per update.

The benchmark threshold is a regression guard, not a cross-machine performance
claim. `framework/tui/check.sh` always reruns it and keeps the live result in
`build/tui-framework/benchmark.txt`.
