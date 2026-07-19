# Language vision

## Mission

Cofnのmissionは、systems safetyとhigh-level productivityの間にある日常的な摩擦を減らすことだ。

Rustは、ownershipとborrowingによってGCなしのmemory safetyを実用化した。一方で、すべてのapplication codeが同じ粒度でlifetime、borrowing、wrapper type、conversionを扱う必要はない。

PythonとJuliaは、探索、科学計算、面接、scriptで非常に速く書ける。一方で、大規模化したときの型・memory・concurrencyの保証は別の仕組みに依存する。

Cofnは次の分離を行う。

- 日常データはGC-managed、immutable-by-default、type-inferred
- resourceはowned、affine、deterministic cleanup
- viewは`read` / `edit`で明示し、通常はnon-escaping
- pure computationは普通の関数として書く
- effectはcompilerが推論し、必要なAPI境界で見える
- performance-critical codeはunboxed、specialized、native compiled

## Product principles

### One day to productive

初日に必要な概念を限定する。

- `fn`
- `let`
- `if` / `else if` / `else`
- `for` / `while`
- List、Map、Set
- `T?`、`null`、`??`
- `Result`
- `|>`
- `read` / `edit` / `take`

advanced generics、effects、metaprogrammingは、必要になるまで見えなくてよい。

### Safety is the default, not ceremony

- implicit unsafe conversionを禁止する
- nullはoptional typeにのみ入る
- owned resourceの二重消費を禁止する
- mutable aliasを禁止する
- data raceを型とruntime contractで防ぐ
- unsupported backend behaviorは明示的にcompile errorにする

### Functional core, practical shell

- dataはimmutable-by-default
- function、closure、pipeline、ADT、pattern matchingを第一級にする
- IOやmutationを完全に隠さず、effectとして追跡する
- interview algorithmでは局所mutationを普通に使える
- pure-onlyを目的化しない

### Performance is designed, then measured

「zero cost」というsloganだけでなく、次を測定する。

- startup latency
- compile time
- steady-state throughput
- tail latency
- allocation count
- GC pause
- memory footprint
- generated code size
- vectorization rate

### One tool, one standard distribution

base installにはcompilerだけでなく、formatter、linter、test runner、documentation、package manager、profiler protocol、scientific coreを含める。

### Error messages are part of the language

syntaxだけでなく、失敗時のexperienceをspec化する。

- stable error code
- exact source span
- why it is unsafe or ambiguous
- direct correction
- `cofn explain CODE`
- IDE fix-it

## Non-goals

- Rust source compatibility
- C preprocessor互換macro
- every feature in the first release
- dynamic typingを静的型のescape hatchとして乱用すること
- implicit numeric narrowing
- hidden network access during builds
- package install時のunrestricted script execution
- syntax noveltyそのもの

## Target users

- Pythonから型安全なnative executionへ進みたい人
- Rustの安全性は欲しいがapplication codeのlifetime ceremonyを減らしたい人
- scientific computingとsystems integrationを一つの言語で扱いたい人
- coding interviewとproduction codeで同じlanguageを使いたい人
- type-level programmingとmetaprogrammingを楽しみたい人

## Success criteria

1. beginnerが一日でbasic programs、collections、file IO、testsを書ける。
2. ownership errorの大半をlifetime annotationなしで説明できる。
3. numeric kernelがC/Rust baselineに対して競争力を持つ。
4. standard toolだけで一般的なCLI、web service、data processing、scientific workloadを構築できる。
5. compiler crash、undefined behavior、silent fallbackをrelease blockerとして扱う。
