# Frost implementation backlog

This directory contains **13,500 generated planning rows**.
Each area has 25 concrete subjects. Each subject has a 20-step lifecycle from requirements to release acceptance.
Every row has a stable ID, priority, milestone, acceptance criterion, validation method, dependency, and content fingerprint.

The generated backlog is intentionally broader than the bootstrap implementation. `docs/MVP_IMPLEMENTED.md` records what currently works.

## これはGitHub issuesではありません

**この13,500行は意図的にファイルとして保持しています。** GitHub issuesには登録しません。

理由は3つあります。

1. **中身がテンプレートです。** 27エリア × 25トピック × 同一の20ステップを機械的に組み合わせたもので、
   「Define contract: X」「Design data structures: X」のようにトピック名を差し替えただけの文面です。
   計画の網羅性を示す資料としては有用ですが、着手できる単位の作業記述ではありません。
2. **GitHubのcontent作成上限は約500件/時です。** 13,500件の登録には27時間以上かかり、
   スパム判定によるアカウント制限のリスクが高くなります。
3. **登録すると検索も巡回も実質不可能になります。** 同一文面が数千件並ぶissue trackerは、
   実際のバグや設計判断を埋もれさせます。

実際に着手できる作業は、**GitHub issuesに34件を登録済み**です。
そちらは実測値・該当ファイル・行番号・受け入れ条件を伴う、粒度のある課題です。

https://github.com/hjosugi/frost-lang/issues

この生成バックログは、その34件が全体計画のどこに位置するかを示す地図として使ってください。

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
| [issues-01-syntax.md](issues-01-syntax.md) | Syntax and Grammar | 500 | FROST-00001–FROST-00500 |
| [issues-02-parser.md](issues-02-parser.md) | Lexer and Parser | 500 | FROST-00501–FROST-01000 |
| [issues-03-diagnostics.md](issues-03-diagnostics.md) | Diagnostics | 500 | FROST-01001–FROST-01500 |
| [issues-04-modules.md](issues-04-modules.md) | Name Resolution and Modules | 500 | FROST-01501–FROST-02000 |
| [issues-05-inference.md](issues-05-inference.md) | Type Inference | 500 | FROST-02001–FROST-02500 |
| [issues-06-types.md](issues-06-types.md) | Advanced Type System | 500 | FROST-02501–FROST-03000 |
| [issues-07-ownership.md](issues-07-ownership.md) | Ownership and Borrowing | 500 | FROST-03001–FROST-03500 |
| [issues-08-memory.md](issues-08-memory.md) | GC and Memory Runtime | 500 | FROST-03501–FROST-04000 |
| [issues-09-effects.md](issues-09-effects.md) | Effects and Error Handling | 500 | FROST-04001–FROST-04500 |
| [issues-10-meta.md](issues-10-meta.md) | Metaprogramming | 500 | FROST-04501–FROST-05000 |
| [issues-11-ir.md](issues-11-ir.md) | HIR MIR and IR | 500 | FROST-05001–FROST-05500 |
| [issues-12-optimizer.md](issues-12-optimizer.md) | Optimizer | 500 | FROST-05501–FROST-06000 |
| [issues-13-codegen.md](issues-13-codegen.md) | Native Code Generation | 500 | FROST-06001–FROST-06500 |
| [issues-14-vm.md](issues-14-vm.md) | VM and Reference Interpreter | 500 | FROST-06501–FROST-07000 |
| [issues-15-stdlib.md](issues-15-stdlib.md) | Core Standard Library | 500 | FROST-07001–FROST-07500 |
| [issues-16-collections.md](issues-16-collections.md) | Collections and Algorithms | 500 | FROST-07501–FROST-08000 |
| [issues-17-science.md](issues-17-science.md) | Scientific Computing | 500 | FROST-08001–FROST-08500 |
| [issues-18-concurrency.md](issues-18-concurrency.md) | Concurrency and Async | 500 | FROST-08501–FROST-09000 |
| [issues-19-packages.md](issues-19-packages.md) | Build Package and Registry | 500 | FROST-09001–FROST-09500 |
| [issues-20-docs_tools.md](issues-20-docs_tools.md) | Formatter Linter and Documentation | 500 | FROST-09501–FROST-10000 |
| [issues-21-developer_tools.md](issues-21-developer_tools.md) | IDE Debugger and Profiler | 500 | FROST-10001–FROST-10500 |
| [issues-22-interop.md](issues-22-interop.md) | FFI and Interoperability | 500 | FROST-10501–FROST-11000 |
| [issues-23-platforms.md](issues-23-platforms.md) | Platforms Wasm and Embedded | 500 | FROST-11001–FROST-11500 |
| [issues-24-laws.md](issues-24-laws.md) | Lawful Types and Proof Checking | 500 | FROST-11501–FROST-12000 |
| [issues-25-bootstrap.md](issues-25-bootstrap.md) | Self Hosting and Bootstrap | 500 | FROST-12001–FROST-12500 |
| [issues-26-quality.md](issues-26-quality.md) | Testing Security and Performance | 500 | FROST-12501–FROST-13000 |
| [issues-27-ecosystem.md](issues-27-ecosystem.md) | Governance Ecosystem and Adoption | 500 | FROST-13001–FROST-13500 |

## Generation and verification

```bash
python3 scripts/generate_backlog.py
python3 scripts/verify_backlog.py
```

Do not edit generated area files manually. Change the generator, regenerate, and review the diff.
