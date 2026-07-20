# Kofun implementation backlog

This directory contains **13,500 discrete implementation issues**.
Each area has 25 concrete subjects. Each subject has a 20-step lifecycle from requirements to release acceptance.
Every row has a stable ID, priority, milestone, acceptance criterion, validation method, dependency, and content fingerprint.

The generated backlog is intentionally broader than the bootstrap implementation. `docs/MVP_IMPLEMENTED.md` records what currently works.

## Counts

- Total: 13,500
- Areas: 27
- Issues per area: 500
- P0: 3,375
- P1: 3,375
- P2: 4,050
- P3: 2,700

## Area files

| File | Area | Count | ID range |
|---|---|---:|---|
| [issues-01-syntax.md](issues-01-syntax.md) | Syntax and Grammar | 500 | KOFUN-00001–KOFUN-00500 |
| [issues-02-parser.md](issues-02-parser.md) | Lexer and Parser | 500 | KOFUN-00501–KOFUN-01000 |
| [issues-03-diagnostics.md](issues-03-diagnostics.md) | Diagnostics | 500 | KOFUN-01001–KOFUN-01500 |
| [issues-04-modules.md](issues-04-modules.md) | Name Resolution and Modules | 500 | KOFUN-01501–KOFUN-02000 |
| [issues-05-inference.md](issues-05-inference.md) | Type Inference | 500 | KOFUN-02001–KOFUN-02500 |
| [issues-06-types.md](issues-06-types.md) | Advanced Type System | 500 | KOFUN-02501–KOFUN-03000 |
| [issues-07-ownership.md](issues-07-ownership.md) | Ownership and Borrowing | 500 | KOFUN-03001–KOFUN-03500 |
| [issues-08-memory.md](issues-08-memory.md) | GC and Memory Runtime | 500 | KOFUN-03501–KOFUN-04000 |
| [issues-09-effects.md](issues-09-effects.md) | Effects and Error Handling | 500 | KOFUN-04001–KOFUN-04500 |
| [issues-10-meta.md](issues-10-meta.md) | Metaprogramming | 500 | KOFUN-04501–KOFUN-05000 |
| [issues-11-ir.md](issues-11-ir.md) | HIR MIR and IR | 500 | KOFUN-05001–KOFUN-05500 |
| [issues-12-optimizer.md](issues-12-optimizer.md) | Optimizer | 500 | KOFUN-05501–KOFUN-06000 |
| [issues-13-codegen.md](issues-13-codegen.md) | Native Code Generation | 500 | KOFUN-06001–KOFUN-06500 |
| [issues-14-vm.md](issues-14-vm.md) | VM and Reference Interpreter | 500 | KOFUN-06501–KOFUN-07000 |
| [issues-15-stdlib.md](issues-15-stdlib.md) | Core Standard Library | 500 | KOFUN-07001–KOFUN-07500 |
| [issues-16-collections.md](issues-16-collections.md) | Collections and Algorithms | 500 | KOFUN-07501–KOFUN-08000 |
| [issues-17-science.md](issues-17-science.md) | Scientific Computing | 500 | KOFUN-08001–KOFUN-08500 |
| [issues-18-concurrency.md](issues-18-concurrency.md) | Concurrency and Async | 500 | KOFUN-08501–KOFUN-09000 |
| [issues-19-packages.md](issues-19-packages.md) | Build Package and Registry | 500 | KOFUN-09001–KOFUN-09500 |
| [issues-20-docs_tools.md](issues-20-docs_tools.md) | Formatter Linter and Documentation | 500 | KOFUN-09501–KOFUN-10000 |
| [issues-21-developer_tools.md](issues-21-developer_tools.md) | IDE Debugger and Profiler | 500 | KOFUN-10001–KOFUN-10500 |
| [issues-22-interop.md](issues-22-interop.md) | FFI and Interoperability | 500 | KOFUN-10501–KOFUN-11000 |
| [issues-23-platforms.md](issues-23-platforms.md) | Platforms Wasm and Embedded | 500 | KOFUN-11001–KOFUN-11500 |
| [issues-24-laws.md](issues-24-laws.md) | Lawful Types and Proof Checking | 500 | KOFUN-11501–KOFUN-12000 |
| [issues-25-bootstrap.md](issues-25-bootstrap.md) | Self Hosting and Bootstrap | 500 | KOFUN-12001–KOFUN-12500 |
| [issues-26-quality.md](issues-26-quality.md) | Testing Security and Performance | 500 | KOFUN-12501–KOFUN-13000 |
| [issues-27-ecosystem.md](issues-27-ecosystem.md) | Governance Ecosystem and Adoption | 500 | KOFUN-13001–KOFUN-13500 |

## Verification

```bash
make verify
```

The checked-in verifier sources are Kofun. Executing their full
Text/List/file-I/O workload becomes a gate when Stage 2 can compile that subset;
until then, `make verify` runs the available structural repository gates.
Regeneration is intentionally disabled until a Kofun generator replaces the
retired host-language generator. Do not edit the generated area files manually.
