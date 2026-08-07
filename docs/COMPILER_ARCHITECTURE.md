# Compiler architecture

## Implemented bootstrap

```text
bootstrap/stage1/compiler.kofun
        |
        | audited generated seed
        v
bootstrap/stage1/compiler.c
        |
        | cc -std=c11
        v
kofun-stage1 INPUT.kofun OUTPUT.c
```

The current Stage 1 frontend validates line-oriented `let` statements,
Int- or Text-valued `print(EXPR)` statements, nested
`if`/`else if`/`else` blocks, and `while`/half-open `for` ranges in `fn main()`.
Expressions cover checked Int arithmetic, six Int comparisons, Bool equality,
Bool literals and bindings, `!`, short-circuiting `&&`/`||`, and Text literals,
concatenation, equality, `chars(Text) -> List[Text]`, `len(Text|List[Text])`, and
byte-oriented Text/List indexing. The 15 builtins used by the frozen self-host
source have exact arity and typed lowering to conditional runtime shims,
including argv/file I/O, Text search/slice/trim, character predicates, Unicode
validation, and both Text/List `len`. Existing Text/List programs receive only
the helpers they already used; the extended host surface additionally declares
its audited Unicode include dependency. A block scopes the bindings it
introduces, and typed boundary crossings, non-`Bool` conditions and misplaced
`else` lines are refused before deterministic C11 is emitted. Assignment and
non-main declarations remain later compatibility slices.

Three executable checkpoints extend this path without claiming full
integration:

```text
bootstrap/stage2/compiler.kofun
  -> token-span tape + structural function IR + stable Kofun projection
  -> bounded multi-function Int Core C11 lowering

bootstrap/native/encoder.kofun
  -> ELF64 headers + x86-64 instruction bytes
  -> static Linux executable

bootstrap/wasm/compiler.c
  -> bounded arithmetic Core parser + direct WebAssembly module bytes
  -> engine-validated module exporting main
```

The Stage 2 checkpoint lowers a bounded `Int` Core with parameters, results,
recursion, and forward references. It does not lower its own Text/List/file-I/O
implementation. The native checkpoint is registered for explicit Linux
targets. Its Int function profile executes parameters, results, forward and
mutual recursion, guarded returns, and checked arithmetic directly on both
x86-64 and AArch64 from one shared parsed program; the shared x86-64/AArch64
scalar, List, and Text profiles remain separate
bounded frontends. wasm32 supports a separately registered Int64 arithmetic
Core profile; it does not yet share a general typed IR with the native targets.

## Target pipeline

```text
UTF-8 source
  -> lexer
  -> parser
  -> name resolution
  -> type and ownership checking
  -> typed IR
  -> optimization
  -> native / C11 / wasm backend
```

Future compiler components must be implemented in `.kofun`. Generated
bootstrap artifacts require canonical Kofun source, a reproduction command, and
a recorded digest.

## Backend strategy

Recorded from the survey in
[#554](https://github.com/kofun-lang/kofun/issues/554). This section states what
was decided and the measurements that decided it, so the question is not
reopened from marketing material. It describes direction, not implemented
behavior; the implemented boundary is the section above.

The priorities the survey assumed, in order: fast builds, zero build-time
dependencies, small static binaries, and eventually competitive runtime
performance.

### Decisions

1. **Keep the direct self-hosted backend.** The compile-speed advantage is
   already banked and is larger than any backend swap could return.
2. **Do not adopt MLIR.** It contradicts three of the four priorities and buys
   nothing this project needs.
3. **Do not adopt QBE.** It emits assembly text, which reintroduces an
   assembler and a linker and destroys the direct-ELF property.
4. **Keep codegen behind an interface.** The durable decision is that a second
   backend stays possible, not which one it is.
5. **If runtime performance ever becomes the binding constraint, emit LLVM IR
   as text.** That reaches `-O2`/`-O3` with zero build-time dependency: LLVM
   becomes a tool the user may have installed, not something linked in.

### What a backend swap can actually buy

Codegen is not the compile-time bottleneck, so the ceiling on any
backend-driven build-speed win is small.

| Measurement | Figure | Source |
|---|---|---|
| rustc Cranelift backend: codegen time | ~20% reduction | [Rust project goal 2025H2](https://rust-lang.github.io/rust-project-goals/2025h2/production-ready-cranelift.html) |
| the same, as a share of clean-build time | **~5%** | same |
| backend share of compile time, fast backend vs LLVM | 2% vs 15% | [TPDE, arXiv:2505.22610](https://arxiv.org/abs/2505.22610) |
| end-to-end speedup from that swap | 17% | same |

**15% is the ceiling.** Cranelift is also a Rust library, so adopting it from a
non-Rust compiler means an FFI boundary, and it is still not production ready
after roughly six years — unwinding, complete ABI coverage, and SIMD intrinsics
remain missing.

### The measured cost of MLIR

| Cost | Figure | Source |
|---|---|---|
| install, release build | **> 4 GB** | [xDSL, arXiv:2311.07422](https://arxiv.org/pdf/2311.07422) |
| install, debug build | 26 GB | same |
| rebuild after one dialect change, 16-core desktop | 14 s to 1 min | same |
| the same, on a laptop | up to 10 min | same |
| required toolchain | C++17, CMake ≥ 3.20, Python ≥ 3.8, Ninja | [llvm.org](https://llvm.org/docs/GettingStarted.html) |

What MLIR uniquely provides is multi-level dialects for heterogeneous hardware
— GPU and accelerator lowering through NVVM, ROCDL and SPIR-V. That is real and
hard to replicate, and it is not on this roadmap. What it does not provide is
better scalar CPU codegen: for CPU targets MLIR bottoms out in LLVM IR, which
is reachable by emitting LLVM IR text with none of the build cost above.

Of roughly 45 entries on the [MLIR users page](https://mlir.llvm.org/users/),
the general-purpose languages are Mojo, Verona (research), and Firefly
(activity unverified). The rest are ML frameworks, hardware and HLS flows,
DSLs, or an IR layer added to a language that already existed. Mojo is the
cautionary case: three years and substantial funding, and its own
[roadmap](https://mojolang.org/docs/roadmap/) still lists cross-compilation,
packaging, a testing framework, a debugger and a profiler as not started, with
the compiler closed. MLIR did not make the surrounding toolchain cheap.

### The price of staying self-hosted

Converging figures put a naive backend at roughly **1.4x–1.8x** optimised
LLVM/GCC on typical scalar code — a known and survivable price that Go, Hare
and TCC all ship at.

| Backend | Runtime vs optimised LLVM/GCC | Source |
|---|---|---|
| TPDE, single-pass | 1.54x–1.77x slower than LLVM `-O1` | [arXiv:2505.22610](https://arxiv.org/abs/2505.22610) |
| QBE 1.3 | 63–70% of `gcc -O2` | [QBE 1.3 notes](https://c9x.me/compile/release/qbe-1.3.html) |
| QBE, per Hare | 25–75% of LLVM | [Hare FAQ](https://harelang.org/documentation/faq.html) |
| cproc, on QBE | 74–82% of gcc | [Callahan](https://briancallahan.net/blog/20211010.html) |
| Cranelift, Wasm JIT | ~14% slower than LLVM | [cranelift.dev](https://cranelift.dev/) |

Go's own [FAQ](https://go.dev/doc/faq) records why it declined LLVM: it was
"too large and slow to meet our performance goals", and — more important in
retrospect — starting with LLVM "would have made it harder to introduce some of
the ABI and related changes, such as stack management". The second clause
applies directly: a custom ABI or stack discipline is harder through LLVM, not
easier.

### The finding that matters most: averages hide cliffs

Zig's self-hosted x86 backend on a user's Z80 emulator
([Ziggit](https://ziggit.dev/t/self-hosted-x86-backend-is-now-default-in-debug-mode/10447)):

| Build | Time |
|---|---|
| LLVM ReleaseFast | 0.033 s |
| LLVM Debug | 0.373 s |
| Zig self-hosted | **10.03 s** |

Roughly 27x slower than LLVM's *debug* build, because the backend emitted
linear if-else chains rather than jump tables for `switch`. A naive backend is
not uniformly 1.5x slower; it is about 1.5x on average with a long tail of
catastrophic cases.

So the work that follows from this decision is to fix the cliffs before chasing
the average:

- dense `switch` lowering must use a jump table, asserted by a test;
- a real register allocator, rather than incremental peephole work;
- a benchmark corpus shaped to catch a Zig-style cliff, not only to report
  averages.

QBE's own history shows the return on exactly that kind of work: 40% to 63% of
`gcc -O2` from a modest set of vetted passes, with inlining still
unimplemented.

### Unverified, and deliberately not repeated as fact

- the ~1.7x–2.2x estimate against `-O2`/`-O3`; the published data is against
  `-O1`;
- TCC's "57% larger, 2.2x slower" output claim — widely repeated, no primary
  source found;
- Zig 0.16's "75 s to 20 s" compiler build time;
- Roc's dev-backend/LLVM split, secondary sources only;
- Cranelift's "10x faster, 14% slower" is Wasm JIT data and may not transfer to
  AOT;
- no controlled measurement isolating Go's codegen quality from Go's runtime
  semantics was found. Any specific "Go's backend is N% slower" figure should be
  treated as invented until a primary source appears.
