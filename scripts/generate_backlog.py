#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "backlog"


@dataclass(frozen=True)
class Area:
    slug: str
    name: str
    profile: str
    subjects: tuple[str, ...]


AREAS: tuple[Area, ...] = (
    Area("syntax", "Syntax and Grammar", "language", (
        "Unicode identifiers", "keyword minimalism", "automatic statement termination", "function declarations", "lambda expressions",
        "immutable bindings", "mutable bindings", "owned bindings", "if expressions", "else if chains", "match expressions",
        "for loops", "while loops", "range expressions", "pipeline operator", "null literal", "optional type suffix",
        "list literals", "tuple literals", "map literals", "set literals", "string interpolation", "numeric literals",
        "operator precedence", "comment syntax",
    )),
    Area("parser", "Lexer and Parser", "compiler", (
        "UTF-8 decoding", "token spans", "newline insertion", "nested comments", "string escape scanning", "numeric scanning",
        "Pratt expression parsing", "declaration parsing", "block parsing", "generic type parsing", "optional type parsing",
        "lambda parsing", "if expression parsing", "match parsing", "pattern parsing", "error recovery", "incremental parsing",
        "lossless syntax tree", "parser event stream", "syntax tree interning", "source map generation", "macro token trees",
        "documentation comments", "parser performance", "grammar conformance",
    )),
    Area("diagnostics", "Diagnostics", "language", (
        "source snippets", "stable error codes", "multi-span labels", "actionable hints", "ownership errors", "type mismatch errors",
        "nullability errors", "pattern exhaustiveness errors", "module resolution errors", "macro expansion traces",
        "C backend errors", "native backend errors", "runtime stack traces", "Unicode column widths", "machine-readable JSON",
        "diagnostic snapshots", "warning levels", "deny policies", "explain command", "error recovery summaries",
        "fix-it edits", "IDE diagnostic streaming", "localization hooks", "compiler crash reports", "diagnostic style guide",
    )),
    Area("modules", "Name Resolution and Modules", "language", (
        "lexical scopes", "shadowing rules", "top-level declarations", "imports", "selective imports", "module aliases",
        "package roots", "visibility", "re-exports", "prelude", "cyclic imports", "forward references", "namespace separation",
        "trait names", "type names", "macro names", "extension methods", "qualified paths", "glob imports", "unused imports",
        "duplicate definitions", "module initialization", "source file mapping", "incremental dependency graph", "module ABI identity",
    )),
    Area("inference", "Type Inference", "language", (
        "literal inference", "local binding inference", "function return inference", "lambda parameter inference", "generic call inference",
        "bidirectional checking", "numeric promotion", "list element joins", "tuple inference", "optional inference", "null coalescing",
        "if branch joins", "match branch joins", "method resolution", "pipeline inference", "recursive functions", "mutual recursion",
        "value restriction", "polymorphic let", "default type selection", "error type recovery", "constraint simplification",
        "incremental inference", "inference diagnostics", "inference performance",
    )),
    Area("types", "Advanced Type System", "language", (
        "algebraic data types", "generic types", "generic functions", "traits", "trait implementations", "associated types",
        "higher-kinded type encoding", "row-polymorphic records", "effect rows", "union types", "intersection types", "refinement predicates",
        "opaque types", "newtypes", "type aliases", "const generics", "variance", "subtyping boundaries", "existential packages",
        "GADT-style constructors", "type-level functions", "compile-time values", "auto traits", "coherence", "orphan rules",
    )),
    Area("ownership", "Ownership and Borrowing", "runtime", (
        "affine owned values", "read parameters", "edit parameters", "take parameters", "move checking", "use-after-take errors",
        "lexical non-escaping views", "borrow conflict detection", "implicit reborrowing", "owned return values", "owned fields",
        "resource destructors", "scope-based cleanup", "early return cleanup", "panic cleanup", "branch move merging", "loop move analysis",
        "closure captures", "async captures", "shared conversion", "copy types", "clone protocol", "pinning", "FFI ownership",
        "ownership visualization",
    )),
    Area("memory", "GC and Memory Runtime", "runtime", (
        "generational tracing GC", "nursery allocation", "old generation", "write barriers", "precise stack maps", "root enumeration",
        "large object space", "pinned objects", "weak references", "finalization", "cycle handling", "GC safepoints", "pause budgeting",
        "concurrent marking", "parallel collection", "thread-local heaps", "allocation profiling", "heap snapshots", "GC tuning flags",
        "owned-to-GC promotion", "GC-to-owned restrictions", "reference counting option", "arena regions", "embedded no-GC profile",
        "memory sanitizer hooks",
    )),
    Area("effects", "Effects and Error Handling", "language", (
        "Result type", "question propagation operator", "panic effect", "IO effect", "async effect", "state effect", "exception interop",
        "effect inference", "effect polymorphism", "effect handlers", "defer blocks", "resource scopes", "recover blocks", "typed errors",
        "error context", "stack traces", "cancellation", "timeouts", "retry combinators", "validation accumulation", "null errors",
        "FFI error mapping", "no-panic functions", "effect diagnostics", "effect ABI",
    )),
    Area("meta", "Metaprogramming", "language", (
        "quote expressions", "unquote expressions", "token macros", "AST macros", "typed macros", "meta functions", "compile-time evaluation",
        "macro hygiene", "source span preservation", "macro modules", "derive macros", "attribute macros", "pattern macros", "syntax extensions",
        "macro caching", "macro sandboxing", "deterministic expansion", "macro diagnostics", "expansion inspection", "macro testing",
        "reflection metadata", "schema generation", "code generation API", "build-time plugins", "macro compatibility policy",
    )),
    Area("ir", "HIR MIR and IR", "compiler", (
        "AST lowering", "name-resolved HIR", "typed HIR", "ownership annotations", "effect annotations", "control-flow graphs",
        "SSA construction", "phi nodes", "basic block layout", "value numbering", "source locations", "debug scopes", "generic substitution",
        "monomorphization", "closure conversion", "async lowering", "match lowering", "desugaring", "constant representation",
        "aggregate layout", "call ABI", "exception edges", "GC safepoints", "IR serialization", "incremental IR cache",
    )),
    Area("optimizer", "Optimizer", "compiler", (
        "constant folding", "dead code elimination", "copy propagation", "common subexpression elimination", "sparse conditional constants",
        "inlining", "tail-call optimization", "loop invariant motion", "loop unrolling", "bounds-check elimination", "escape analysis",
        "allocation sinking", "scalar replacement", "devirtualization", "specialization", "vectorization", "fusion of map and filter",
        "ownership-based reuse", "GC barrier elimination", "null-check elimination", "branch prediction metadata", "profile-guided optimization",
        "link-time optimization", "optimization remarks", "miscompile verification",
    )),
    Area("codegen", "Native Code Generation", "compiler", (
        "C11 bootstrap backend", "LLVM backend", "Cranelift backend", "x86-64 ABI", "AArch64 ABI", "RISC-V ABI", "Windows calling convention",
        "object file emission", "static linking", "dynamic linking", "position-independent code", "thread-local storage", "debug information",
        "unwind tables", "stack maps", "integer overflow modes", "floating-point modes", "SIMD intrinsics", "atomic instructions",
        "tail calls", "sanitizer instrumentation", "coverage instrumentation", "cross compilation", "reproducible binaries", "backend differential tests",
    )),
    Area("vm", "VM and Reference Interpreter", "compiler", (
        "tree-walk evaluator", "bytecode format", "bytecode verifier", "register VM", "constant pool", "call frames", "closures", "tail calls",
        "garbage collection", "owned resources", "exceptions", "effects", "async scheduler", "debug hooks", "breakpoints", "single stepping",
        "hot reload", "REPL state", "bytecode serialization", "portable snapshots", "sandbox limits", "deterministic mode", "profiling counters",
        "JIT bridge", "interpreter conformance",
    )),
    Area("stdlib", "Core Standard Library", "library", (
        "Bool", "Int", "Float", "Decimal", "Complex", "Text", "Bytes", "Optional", "Result", "Range", "Iterator", "Function",
        "Path", "File", "Directory", "Environment", "Process", "Clock", "Random", "Regex", "JSON", "CSV", "TOML", "logging", "testing API",
    )),
    Area("collections", "Collections and Algorithms", "library", (
        "List", "Vector", "Array", "Slice", "Tuple", "Map", "Set", "MultiMap", "Deque", "Queue", "Stack", "BinaryHeap", "PriorityQueue",
        "BitSet", "LinkedList", "Graph", "Tree", "Trie", "UnionFind", "LRU cache", "sorting", "binary search", "hashing", "persistent collections",
        "parallel collections",
    )),
    Area("science", "Scientific Computing", "library", (
        "N-dimensional arrays", "array shapes", "strides", "slicing", "broadcasting", "elementwise operators", "matrix multiplication",
        "linear algebra", "BLAS interop", "LAPACK interop", "FFT", "statistics", "random distributions", "automatic differentiation",
        "symbolic expressions", "units of measure", "data frames", "missing data", "CSV datasets", "memory mapping", "GPU arrays",
        "SIMD kernels", "plotting protocol", "notebook protocol", "reproducible numerics",
    )),
    Area("concurrency", "Concurrency and Async", "runtime", (
        "structured concurrency", "async functions", "await expressions", "task groups", "cancellation", "channels", "select", "mutex",
        "read-write lock", "semaphore", "barrier", "atomics", "thread spawning", "work-stealing scheduler", "IO reactor", "timers",
        "blocking pool", "send safety", "share safety", "data-race prevention", "actor library", "parallel iterators", "deterministic scheduler",
        "deadlock diagnostics", "distributed task protocol",
    )),
    Area("packages", "Build Package and Registry", "tooling", (
        "frost new", "frost run", "frost check", "frost build", "frost test", "frost fmt", "frost lint", "frost doc", "frost bench",
        "manifest format", "lockfile", "semantic versions", "dependency resolver", "workspace support", "build scripts", "feature flags",
        "binary dependencies", "registry protocol", "package signing", "checksum verification", "offline mode", "vendor mode", "incremental builds",
        "remote cache", "reproducible builds",
    )),
    Area("docs_tools", "Formatter Linter and Documentation", "tooling", (
        "syntax-aware formatter", "comment preservation", "format stability", "import sorting", "unused binding lint", "shadowing lint",
        "complexity lint", "ownership lint", "nullability lint", "performance lint", "security lint", "lint configuration", "automatic fixes",
        "API documentation", "doctests", "documentation search", "source links", "type signatures", "examples", "module graphs",
        "documentation themes", "offline docs", "versioned docs", "style guide", "tool plugin API",
    )),
    Area("developer_tools", "IDE Debugger and Profiler", "tooling", (
        "language server", "completion", "hover", "go to definition", "find references", "rename", "signature help", "semantic tokens",
        "inlay hints", "code actions", "workspace symbols", "call hierarchy", "type hierarchy", "incremental diagnostics", "debug adapter",
        "source breakpoints", "data breakpoints", "expression evaluation", "native debugger integration", "CPU profiler", "allocation profiler",
        "GC profiler", "flame graphs", "benchmark explorer", "VS Code extension",
    )),
    Area("interop", "FFI and Interoperability", "tooling", (
        "C imports", "C exports", "header generation", "ABI-safe types", "ownership annotations", "callback trampolines", "variadic C calls",
        "C++ bridge", "Rust bridge", "Python extension", "Python embedding", "Java JNI", "JavaScript embedding", "WebAssembly host calls",
        "Fortran interop", "BLAS bindings", "system library discovery", "pkg-config integration", "dynamic library loading", "symbol visibility",
        "exception boundaries", "GC handles", "thread attachment", "FFI sanitizers", "binding generator",
    )),
    Area("platforms", "Platforms Wasm and Embedded", "tooling", (
        "Linux x86-64", "Linux AArch64", "macOS x86-64", "macOS arm64", "Windows x86-64", "FreeBSD", "Android", "iOS", "WebAssembly WASI",
        "WebAssembly browser", "embedded ARM", "embedded RISC-V", "no-std profile", "no-GC profile", "real-time profile", "cross toolchains",
        "sysroot packaging", "platform capabilities", "filesystem abstraction", "network abstraction", "clock abstraction", "entropy abstraction",
        "endian portability", "32-bit targets", "continuous platform testing",
    )),
    Area("laws", "Lawful Types and Proof Checking", "language", (
        "law declaration syntax", "law family registry", "Monad left identity", "Monad right identity", "Monad associativity",
        "Functor identity", "Functor composition", "Applicative identity", "Applicative composition", "Applicative homomorphism",
        "Applicative interchange", "Semigroup associativity", "Monoid identity", "Alternative laws", "Foldable laws",
        "Traversable laws", "finite carrier enumeration", "complete function spaces", "bounded exhaustive models", "property-based evidence",
        "proof-term kernel", "SMT proof certificates", "counterexample minimization", "law-aware optimization", "law evidence ABI",
    )),
    Area("bootstrap", "Self Hosting and Bootstrap", "compiler", (
        "Stage 0 reference compiler", "Stage 1 Frost frontend", "Stage 1 Frost type checker", "Stage 1 ownership checker", "Stage 1 law checker",
        "Stage 1 HIR lowering", "Stage 1 C11 backend", "Stage 1 native driver", "Stage 2 self compilation", "Stage 2 artifact comparison",
        "bootstrap fixed point", "reproducible compiler build", "trusted computing base inventory", "bootstrap binary provenance", "diverse double compilation",
        "bootstrap parser parity", "bootstrap type parity", "bootstrap diagnostics parity", "bootstrap runtime parity", "bootstrap law parity",
        "cross architecture bootstrap", "offline bootstrap", "minimal seed distribution", "bootstrap regression corpus", "bootstrap release gate",
    )),
    Area("quality", "Testing Security and Performance", "quality", (
        "unit test harness", "snapshot testing", "property testing", "fuzz testing", "mutation testing", "compiler differential testing",
        "conformance suite", "language corpus", "benchmark suite", "compile-time benchmarks", "runtime benchmarks", "memory benchmarks",
        "security threat model", "supply-chain security", "sandboxing", "reproducibility", "undefined behavior audit", "memory safety audit",
        "concurrency safety audit", "parser hardening", "macro hardening", "registry abuse prevention", "responsible disclosure",
        "release qualification", "performance regression gates",
    )),
    Area("ecosystem", "Governance Ecosystem and Adoption", "quality", (
        "project charter", "decision process", "RFC process", "code of conduct", "maintainer roles", "release team", "security team",
        "language design team", "library team", "compiler team", "tooling team", "compatibility policy", "edition policy", "stability promises",
        "telemetry policy", "privacy policy", "trademark policy", "name collision review", "website", "playground", "package registry operations",
        "education curriculum", "coding interview kit", "scientific community outreach", "production adoption guide",
    )),
)


PROFILES: dict[str, tuple[tuple[str, str, str, str], ...]] = {
    "language": (
        ("requirements", "Write user stories and non-goals", "User stories, constraints, and non-goals are reviewed and linked to concrete examples.", "review checklist passes"),
        ("prior-art", "Survey prior art and failure modes", "At least three relevant designs and their tradeoffs are recorded without copying accidental complexity.", "design review records alternatives"),
        ("semantics", "Specify normative semantics", "Observable behavior, static rules, and edge cases are normative and unambiguous.", "spec examples have one interpretation"),
        ("surface", "Specify surface syntax and ergonomics", "Canonical syntax, precedence, and one-day-learning guidance are documented.", "parser examples cover valid and invalid forms"),
        ("ast", "Represent in syntax and semantic trees", "AST/HIR nodes preserve spans and all information required by later phases.", "round-trip structural tests pass"),
        ("resolution", "Integrate name and module resolution", "Names resolve deterministically across local, module, and imported scopes.", "resolution tests cover shadowing and ambiguity"),
        ("typing", "Implement static type and safety rules", "The checker accepts sound programs and rejects known unsound counterexamples.", "positive and negative type tests pass"),
        ("ownership", "Integrate ownership and effect analysis", "Moves, views, cleanup, and effects interact without hidden lifetime syntax.", "safety regression suite passes"),
        ("diagnostics", "Add actionable diagnostics", "Errors include stable codes, precise spans, and a practical correction hint.", "diagnostic snapshots are approved"),
        ("interpreter", "Implement reference semantics", "The interpreter executes the specified behavior and exposes no backend-specific shortcuts.", "reference execution tests pass"),
        ("lowering", "Lower to typed IR", "Lowering preserves semantics and attaches types, effects, ownership, and source locations.", "IR golden tests pass"),
        ("native", "Implement native backend support", "Supported constructs compile to native code with behavior matching the interpreter.", "differential native tests pass"),
        ("meta", "Define metaprogramming interaction", "Expansion, hygiene, staging, and generated diagnostics are deterministic.", "macro conformance tests pass"),
        ("ide", "Expose IDE and tooling data", "Language-server consumers receive stable semantic tokens, symbols, and edits.", "LSP integration test passes"),
        ("security", "Complete safety and abuse review", "Threats, resource limits, and unsafe escape hatches are documented and tested.", "security sign-off is recorded"),
        ("fuzz", "Add property and fuzz coverage", "Generators exercise valid and invalid forms without crashes or invariant violations.", "fuzz budget completes cleanly"),
        ("performance", "Establish performance budgets", "Compile-time, runtime, allocation, and code-size budgets are measurable.", "benchmark gate is green"),
        ("docs", "Write tutorial and reference documentation", "A new user can learn the feature in one day and experts can find normative details.", "doctests and link checks pass"),
        ("compat", "Define compatibility and migration", "Edition, deprecation, migration, and tooling behavior are explicit.", "migration fixture passes"),
        ("release", "Complete stabilization and release acceptance", "Open blockers are resolved, conformance is complete, and the feature is release-ready.", "release checklist is signed"),
    ),
    "compiler": (
        ("contract", "Define compiler-stage contract", "Inputs, outputs, invariants, and failure behavior are written for the stage.", "contract review passes"),
        ("model", "Design data structures and ownership", "Data structures have explicit lifetimes, memory costs, and stable identities.", "design invariants are tested"),
        ("prototype", "Build a minimal prototype", "A narrow end-to-end path proves feasibility and records unsupported cases.", "prototype fixture passes"),
        ("correctness", "Implement core correctness path", "The implementation handles all normative baseline cases without silent fallback.", "core conformance tests pass"),
        ("recovery", "Implement error recovery", "Malformed inputs produce bounded diagnostics and allow useful continued analysis.", "recovery corpus does not crash"),
        ("spans", "Preserve source and debug locations", "Every generated node can be traced to user or generated source.", "source-map tests pass"),
        ("incremental", "Add incremental computation", "Changes invalidate only necessary work and cache keys are deterministic.", "incremental edit tests pass"),
        ("parallel", "Add safe parallel execution", "Parallel work is deterministic and free from data races and deadlocks.", "thread sanitizer suite passes"),
        ("memory", "Bound memory consumption", "Peak memory is measured and adversarial inputs respect configured limits.", "memory stress gate passes"),
        ("diagnostics", "Add stage-specific diagnostics", "Failures expose stable codes, spans, context, and remediation.", "diagnostic snapshots pass"),
        ("unit", "Add exhaustive unit coverage", "Boundary conditions and internal invariants have focused unit tests.", "unit coverage threshold passes"),
        ("fuzz", "Add fuzz and differential testing", "Randomized inputs are compared against a trusted model where possible.", "fuzz and differential jobs pass"),
        ("bench", "Create compiler benchmarks", "Throughput, latency, memory, and output quality have tracked baselines.", "benchmark dashboard records baseline"),
        ("opt", "Integrate optimization safely", "Optimizations preserve semantics and are individually disableable for diagnosis.", "optimization differential tests pass"),
        ("platform", "Validate target portability", "Supported operating systems and architectures produce equivalent results.", "target matrix is green"),
        ("security", "Harden against hostile inputs", "Resource exhaustion, malformed data, and unsafe host interaction are mitigated.", "security corpus passes"),
        ("observability", "Add tracing and inspection", "Developers can inspect stage timing, artifacts, caches, and decisions.", "trace schema is stable"),
        ("tooling", "Expose stable tooling APIs", "IDE, formatter, debugger, and external tools consume versioned data.", "tooling compatibility test passes"),
        ("docs", "Document internals and debugging", "Contributor docs explain invariants, workflows, and common failure analysis.", "documentation review passes"),
        ("release", "Qualify for release", "Correctness, fuzzing, performance, portability, and documentation gates all pass.", "release qualification is signed"),
    ),
    "runtime": (
        ("semantics", "Define runtime safety semantics", "Ownership, sharing, cleanup, concurrency, and failure behavior are normative.", "runtime model review passes"),
        ("layout", "Design representation and metadata", "Object layout, tags, headers, and metadata costs are documented.", "layout assertions pass"),
        ("prototype", "Implement single-threaded prototype", "A minimal implementation demonstrates correct lifecycle behavior.", "prototype lifecycle tests pass"),
        ("integration", "Integrate compiler lowering", "Compiler-generated operations map directly to runtime contracts.", "lowering/runtime tests pass"),
        ("cleanup", "Guarantee deterministic cleanup", "Normal, error, panic, cancellation, and early-return paths release resources correctly.", "cleanup matrix passes"),
        ("sharing", "Implement safe sharing", "Shared access preserves race freedom and ownership invariants.", "aliasing tests pass"),
        ("threads", "Implement multi-thread behavior", "Thread transitions and synchronization are deterministic within specified bounds.", "concurrency stress passes"),
        ("safepoints", "Implement safepoints and suspension", "Threads can suspend without exposing invalid roots or resource states.", "safepoint stress passes"),
        ("failure", "Handle allocation and runtime failure", "Out-of-memory, cancellation, and subsystem failure have defined behavior.", "failure injection passes"),
        ("diagnostics", "Add runtime diagnostics", "Crashes and safety violations include actionable traces and state summaries.", "runtime snapshots pass"),
        ("unit", "Add lifecycle unit tests", "Every state transition and invalid transition has direct coverage.", "unit suite passes"),
        ("model", "Add model-based tests", "Random operation sequences agree with an executable reference model.", "model checker passes"),
        ("fuzz", "Fuzz runtime boundaries", "Adversarial allocation, scheduling, and cleanup sequences do not corrupt state.", "fuzz budget passes"),
        ("bench", "Measure latency and throughput", "Fast paths, pauses, contention, and memory overhead have baselines.", "runtime benchmark gate passes"),
        ("tuning", "Expose safe tuning controls", "Configuration has validated ranges, stable defaults, and observability.", "configuration tests pass"),
        ("platform", "Validate platform primitives", "Atomics, TLS, virtual memory, and clocks work on supported targets.", "platform matrix passes"),
        ("security", "Audit runtime attack surface", "Memory corruption, denial of service, side channels, and unsafe FFI are reviewed.", "security audit closes blockers"),
        ("tooling", "Integrate debugger and profiler", "Runtime metadata supports stacks, heaps, tasks, and resource inspection.", "debugger/profiler tests pass"),
        ("docs", "Document operational behavior", "Users can tune, monitor, troubleshoot, and reason about runtime costs.", "operations guide review passes"),
        ("release", "Stabilize runtime contract", "ABI, configuration, safety, and performance gates are accepted for release.", "runtime release sign-off exists"),
    ),
    "library": (
        ("requirements", "Define user-facing requirements", "Core use cases, non-goals, complexity expectations, and error behavior are recorded.", "API review passes"),
        ("api", "Design compact public API", "Names and signatures are learnable in one day and compose with pipelines and inference.", "API ergonomics examples pass"),
        ("types", "Define static typing behavior", "Generic parameters, nullability, ownership, and effects are precise.", "type conformance tests pass"),
        ("reference", "Implement reference version", "A clear implementation establishes correctness before optimization.", "reference tests pass"),
        ("errors", "Define errors and edge cases", "Invalid inputs never rely on undocumented sentinel values.", "negative tests pass"),
        ("iterators", "Integrate iteration and pipelines", "Values compose with map, filter, fold, ranges, and lazy iteration.", "pipeline tests pass"),
        ("ownership", "Integrate ownership and GC behavior", "Copies, views, mutations, sharing, and cleanup have predictable costs.", "ownership tests pass"),
        ("algorithms", "Implement baseline algorithms", "Operations meet documented complexity and stability guarantees.", "algorithm tests pass"),
        ("specialize", "Add numeric and type specialization", "Common concrete types avoid boxing and unnecessary dispatch.", "specialization benchmark passes"),
        ("vectorize", "Add SIMD and bulk execution", "Eligible operations use vectorized kernels without changing semantics.", "SIMD differential tests pass"),
        ("parallel", "Add parallel execution", "Parallel APIs are deterministic where promised and race-safe.", "parallel stress passes"),
        ("interop", "Add ecosystem interoperability", "C, Python, Rust, files, and standard protocols have documented conversions.", "interop fixtures pass"),
        ("unit", "Add unit and example tests", "Public operations and examples have direct deterministic coverage.", "unit suite passes"),
        ("property", "Add algebraic property tests", "Documented laws and invariants hold across generated inputs.", "property suite passes"),
        ("fuzz", "Fuzz parsers and boundaries", "Malformed and adversarial inputs do not crash or exhaust uncontrolled resources.", "fuzz gate passes"),
        ("bench", "Publish performance baselines", "Latency, throughput, allocations, and scaling are compared with alternatives.", "benchmark report is current"),
        ("docs", "Write cookbook and API reference", "Beginners have direct recipes and experts have precise contracts.", "doctests pass"),
        ("tooling", "Integrate docs IDE and debugger", "Types, values, errors, and examples display correctly in tools.", "tooling fixture passes"),
        ("compat", "Define serialization and compatibility", "Stored or exchanged representations have explicit versioning.", "compatibility fixtures pass"),
        ("release", "Complete library stabilization", "API, correctness, performance, security, and docs are release-ready.", "library sign-off exists"),
    ),
    "tooling": (
        ("ux", "Define command and workflow UX", "Common workflows are direct, discoverable, and consistent with the single Frost tool.", "UX review passes"),
        ("contract", "Specify configuration and protocol", "Inputs, outputs, defaults, schemas, and versioning are explicit.", "schema validation passes"),
        ("prototype", "Build end-to-end prototype", "A minimal real workflow completes without manual hidden steps.", "smoke workflow passes"),
        ("core", "Implement core operation", "The tool handles the baseline workflow with deterministic results.", "core integration tests pass"),
        ("errors", "Add actionable failure handling", "Failures state what happened, where, and the next practical action.", "error snapshots pass"),
        ("incremental", "Add incremental and cached operation", "Repeated work is avoided with correct invalidation and reproducible keys.", "cache correctness tests pass"),
        ("parallel", "Add bounded parallelism", "Parallel work improves throughput without nondeterminism or resource spikes.", "parallel stress passes"),
        ("offline", "Support offline and hermetic use", "Documented workflows work without network access when inputs are available.", "offline fixture passes"),
        ("security", "Harden trust boundaries", "Untrusted projects, packages, servers, and generated files are constrained.", "security tests pass"),
        ("privacy", "Define privacy and telemetry", "No data leaves the machine without explicit, documented consent.", "privacy review passes"),
        ("unit", "Add unit coverage", "Parsing, configuration, state transitions, and edge cases have focused tests.", "unit suite passes"),
        ("integration", "Add real-project integration tests", "Representative workspaces complete the workflow on supported platforms.", "integration matrix passes"),
        ("fuzz", "Fuzz external inputs", "Malformed files, protocols, and paths do not crash or escape boundaries.", "fuzz budget passes"),
        ("bench", "Measure workflow performance", "Startup, latency, throughput, cache, and memory have tracked baselines.", "performance gate passes"),
        ("platform", "Validate platform matrix", "Path, shell, process, encoding, and filesystem behavior is portable.", "platform CI is green"),
        ("api", "Expose automation API", "Machines can consume stable JSON or protocol output without scraping text.", "automation contract tests pass"),
        ("ide", "Integrate editor workflow", "Editors can invoke the feature with cancellation and incremental feedback.", "editor integration passes"),
        ("docs", "Write task-oriented documentation", "Users can finish the workflow from a fresh installation.", "documentation smoke test passes"),
        ("migration", "Define upgrades and rollback", "State formats and protocols migrate safely and can recover from failure.", "upgrade/downgrade fixture passes"),
        ("release", "Complete tool release qualification", "Correctness, UX, security, performance, and portability gates pass.", "tool release sign-off exists"),
    ),
    "quality": (
        ("scope", "Define scope metrics and ownership", "Goals, measurable outcomes, owners, and non-goals are recorded.", "charter review passes"),
        ("policy", "Write normative policy", "Decision rights, exceptions, escalation, and auditability are explicit.", "policy review passes"),
        ("baseline", "Measure current baseline", "Current behavior and gaps are captured with reproducible evidence.", "baseline artifact exists"),
        ("automation", "Automate the primary workflow", "The process runs consistently without undocumented manual intervention.", "automation smoke test passes"),
        ("gates", "Define blocking quality gates", "Pass/fail criteria are objective and tied to release or governance decisions.", "gate simulation passes"),
        ("reporting", "Create transparent reporting", "Status, trends, exceptions, and owners are visible in a stable format.", "report validation passes"),
        ("triage", "Implement intake and triage", "New findings are classified, deduplicated, prioritized, and assigned.", "triage drill passes"),
        ("response", "Define incident and failure response", "Detection, containment, communication, and recovery steps are practiced.", "tabletop exercise passes"),
        ("security", "Complete abuse and conflict review", "Incentives, trust boundaries, conflicts, and abuse scenarios have mitigations.", "risk review closes blockers"),
        ("privacy", "Complete privacy review", "Data collection, retention, access, and deletion are minimized and documented.", "privacy sign-off exists"),
        ("tests", "Add process conformance tests", "Automated checks detect violations of the documented contract.", "conformance suite passes"),
        ("sampling", "Add independent sampling audit", "A reproducible sample is independently reviewed for false positives and gaps.", "sampling report is accepted"),
        ("bench", "Track cost and latency", "Human time, compute, delays, and throughput are measured against budgets.", "operational budget gate passes"),
        ("accessibility", "Review accessibility and inclusion", "Workflows and materials are usable across documented accessibility needs.", "accessibility review passes"),
        ("international", "Review international operation", "Localization, time zones, legal boundaries, and regional constraints are mapped.", "international checklist passes"),
        ("docs", "Publish public and contributor documentation", "Users and contributors can understand expectations and decisions.", "docs review passes"),
        ("training", "Create training and rehearsal", "Responsible people can execute the process through realistic exercises.", "training exercise passes"),
        ("migration", "Plan adoption and transition", "Existing users and assets can migrate with rollback and support paths.", "migration rehearsal passes"),
        ("review", "Run stabilization review", "Evidence is reviewed by accountable owners and unresolved blockers are explicit.", "stabilization minutes are recorded"),
        ("release", "Approve production operation", "All gates, owners, monitoring, and response plans are active.", "production approval is signed"),
    ),
}


def priority(stage: int) -> str:
    if stage <= 5:
        return "P0"
    if stage <= 10:
        return "P1"
    if stage <= 16:
        return "P2"
    return "P3"


def milestone(stage: int) -> str:
    if stage <= 3:
        return "M0-spec"
    if stage <= 8:
        return "M1-bootstrap"
    if stage <= 13:
        return "M2-alpha"
    if stage <= 17:
        return "M3-beta"
    return "M4-1.0"


def escape(value: str) -> str:
    return value.replace("|", "\\|").replace("\n", " ").strip()


def stable_fingerprint(parts: tuple[str, ...]) -> str:
    return hashlib.sha256("\x1f".join(parts).encode("utf-8")).hexdigest()[:12]


def generate() -> dict[str, object]:
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("issues-*.md"):
        old.unlink()

    all_rows: list[dict[str, str]] = []
    next_id = 1
    index_rows: list[tuple[str, str, int, int, int]] = []

    for area_index, area in enumerate(AREAS, start=1):
        tasks = PROFILES[area.profile]
        if len(area.subjects) != 25 or len(tasks) != 20:
            raise RuntimeError(f"{area.name}: expected 25 subjects and 20 tasks")
        start_id = next_id
        rows: list[dict[str, str]] = []
        for subject_index, subject in enumerate(area.subjects, start=1):
            previous = "-"
            for stage, (kind, action, acceptance, validation) in enumerate(tasks, start=1):
                issue_id = f"FROST-{next_id:05d}"
                title = f"{action}: {subject}"
                concrete_acceptance = f"For {subject}: {acceptance}"
                row = {
                    "id": issue_id,
                    "state": "planned",
                    "priority": priority(stage),
                    "milestone": milestone(stage),
                    "kind": kind,
                    "area": area.name,
                    "title": title,
                    "acceptance": concrete_acceptance,
                    "validation": validation,
                    "depends": previous,
                    "fingerprint": stable_fingerprint((area.slug, subject, kind, title)),
                }
                rows.append(row)
                all_rows.append(row)
                previous = issue_id
                next_id += 1
        end_id = next_id - 1
        filename = f"issues-{area_index:02d}-{area.slug}.md"
        path = OUT / filename
        lines = [
            f"# {area.name} backlog",
            "",
            f"- Issues: {len(rows)}",
            f"- Range: FROST-{start_id:05d} through FROST-{end_id:05d}",
            f"- Profile: `{area.profile}`",
            "",
            "| ID | State | Priority | Milestone | Kind | Title | Acceptance criteria | Validation | Depends on | Fingerprint |",
            "|---|---|---|---|---|---|---|---|---|---|",
        ]
        for row in rows:
            lines.append(
                "| {id} | {state} | {priority} | {milestone} | {kind} | {title} | {acceptance} | {validation} | {depends} | `{fingerprint}` |".format(
                    **{key: escape(value) for key, value in row.items()}
                )
            )
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        index_rows.append((filename, area.name, len(rows), start_id, end_id))

    total = len(all_rows)
    by_priority: dict[str, int] = {}
    by_milestone: dict[str, int] = {}
    for row in all_rows:
        by_priority[row["priority"]] = by_priority.get(row["priority"], 0) + 1
        by_milestone[row["milestone"]] = by_milestone.get(row["milestone"], 0) + 1

    index = [
        "# Frost implementation backlog",
        "",
        f"This directory contains **{total:,} discrete implementation issues**.",
        "Each area has 25 concrete subjects. Each subject has a 20-step lifecycle from requirements to release acceptance.",
        "Every row has a stable ID, priority, milestone, acceptance criterion, validation method, dependency, and content fingerprint.",
        "",
        "The generated backlog is intentionally broader than the bootstrap implementation. `docs/MVP_IMPLEMENTED.md` records what currently works.",
        "",
        "## Counts",
        "",
        f"- Total: {total:,}",
        f"- Areas: {len(AREAS)}",
        "- Issues per area: 500",
        f"- P0: {by_priority.get('P0', 0):,}",
        f"- P1: {by_priority.get('P1', 0):,}",
        f"- P2: {by_priority.get('P2', 0):,}",
        f"- P3: {by_priority.get('P3', 0):,}",
        "",
        "## Area files",
        "",
        "| File | Area | Count | ID range |",
        "|---|---|---:|---|",
    ]
    for filename, name, count, start, end in index_rows:
        index.append(f"| [{filename}]({filename}) | {escape(name)} | {count} | FROST-{start:05d}–FROST-{end:05d} |")
    index += [
        "",
        "## Generation and verification",
        "",
        "```bash",
        "python3 scripts/generate_backlog.py",
        "python3 scripts/verify_backlog.py",
        "```",
        "",
        "Do not edit generated area files manually. Change the generator, regenerate, and review the diff.",
    ]
    (OUT / "README.md").write_text("\n".join(index) + "\n", encoding="utf-8")

    summary = {
        "schema": 1,
        "total": total,
        "areas": len(AREAS),
        "issues_per_area": 500,
        "by_priority": by_priority,
        "by_milestone": by_milestone,
        "first_id": all_rows[0]["id"],
        "last_id": all_rows[-1]["id"],
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    (OUT / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return summary


if __name__ == "__main__":
    result = generate()
    print(json.dumps(result, indent=2, sort_keys=True))
