# Cofn implementation backlog

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
| [issues-01-syntax.md](issues-01-syntax.md) | Syntax and Grammar | 500 | COFN-00001–COFN-00500 |
| [issues-02-parser.md](issues-02-parser.md) | Lexer and Parser | 500 | COFN-00501–COFN-01000 |
| [issues-03-diagnostics.md](issues-03-diagnostics.md) | Diagnostics | 500 | COFN-01001–COFN-01500 |
| [issues-04-modules.md](issues-04-modules.md) | Name Resolution and Modules | 500 | COFN-01501–COFN-02000 |
| [issues-05-inference.md](issues-05-inference.md) | Type Inference | 500 | COFN-02001–COFN-02500 |
| [issues-06-types.md](issues-06-types.md) | Advanced Type System | 500 | COFN-02501–COFN-03000 |
| [issues-07-ownership.md](issues-07-ownership.md) | Ownership and Borrowing | 500 | COFN-03001–COFN-03500 |
| [issues-08-memory.md](issues-08-memory.md) | GC and Memory Runtime | 500 | COFN-03501–COFN-04000 |
| [issues-09-effects.md](issues-09-effects.md) | Effects and Error Handling | 500 | COFN-04001–COFN-04500 |
| [issues-10-meta.md](issues-10-meta.md) | Metaprogramming | 500 | COFN-04501–COFN-05000 |
| [issues-11-ir.md](issues-11-ir.md) | HIR MIR and IR | 500 | COFN-05001–COFN-05500 |
| [issues-12-optimizer.md](issues-12-optimizer.md) | Optimizer | 500 | COFN-05501–COFN-06000 |
| [issues-13-codegen.md](issues-13-codegen.md) | Native Code Generation | 500 | COFN-06001–COFN-06500 |
| [issues-14-vm.md](issues-14-vm.md) | VM and Reference Interpreter | 500 | COFN-06501–COFN-07000 |
| [issues-15-stdlib.md](issues-15-stdlib.md) | Core Standard Library | 500 | COFN-07001–COFN-07500 |
| [issues-16-collections.md](issues-16-collections.md) | Collections and Algorithms | 500 | COFN-07501–COFN-08000 |
| [issues-17-science.md](issues-17-science.md) | Scientific Computing | 500 | COFN-08001–COFN-08500 |
| [issues-18-concurrency.md](issues-18-concurrency.md) | Concurrency and Async | 500 | COFN-08501–COFN-09000 |
| [issues-19-packages.md](issues-19-packages.md) | Build Package and Registry | 500 | COFN-09001–COFN-09500 |
| [issues-20-docs_tools.md](issues-20-docs_tools.md) | Formatter Linter and Documentation | 500 | COFN-09501–COFN-10000 |
| [issues-21-developer_tools.md](issues-21-developer_tools.md) | IDE Debugger and Profiler | 500 | COFN-10001–COFN-10500 |
| [issues-22-interop.md](issues-22-interop.md) | FFI and Interoperability | 500 | COFN-10501–COFN-11000 |
| [issues-23-platforms.md](issues-23-platforms.md) | Platforms Wasm and Embedded | 500 | COFN-11001–COFN-11500 |
| [issues-24-laws.md](issues-24-laws.md) | Lawful Types and Proof Checking | 500 | COFN-11501–COFN-12000 |
| [issues-25-bootstrap.md](issues-25-bootstrap.md) | Self Hosting and Bootstrap | 500 | COFN-12001–COFN-12500 |
| [issues-26-quality.md](issues-26-quality.md) | Testing Security and Performance | 500 | COFN-12501–COFN-13000 |
| [issues-27-ecosystem.md](issues-27-ecosystem.md) | Governance Ecosystem and Adoption | 500 | COFN-13001–COFN-13500 |

## Generation and verification

```bash
python3 scripts/generate_backlog.py
python3 scripts/verify_backlog.py
```

Do not edit generated area files manually. Change the generator, regenerate, and review the diff.
