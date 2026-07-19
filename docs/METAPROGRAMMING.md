# Metaprogramming

## Goals

- Lispのquote/unquoteの楽しさ
- Perlのpragmatic code generation
- Rust derive macroの実用性
- TypeScript compiler APIのtype-aware tooling
- build reproducibilityとsecurity

## Non-goals

- textual substitution
- unrestricted compiler-process execution
- order-dependent global mutation
- source locationの消失
- macro expansionでtype safetyを迂回すること

## Levels

### Token macros

small syntax abstraction向け。

```cofn
macro assert_close(actual, expected, tolerance) {
    # token-tree expansion
}
```

### Typed AST macros

declarationやexpressionをtyped nodeとして操作する。

```cofn
meta fn derive_show(info: TypeInfo) -> Decl {
    quote {
        impl Show[$(info.type)] {
            fn show(read value: $(info.type)) -> Text {
                $(build_show_body(info))
            }
        }
    }
}
```

### Compile-time functions

pure、deterministic、boundedな計算。

```cofn
const table = meta generate_lookup_table(256)
```

## Hygiene

- generated identifierはmacro scopeに属する
- caller bindingをcaptureする場合は明示APIを使う
- generated and call-site spanを両方保持する
- diagnosticsはexpansion stackを表示する

## Staging

概念phase:

```text
parse
-> token macro expansion
-> name resolution
-> typed AST macro expansion
-> type re-check
-> const/meta evaluation
-> lowering
```

phase cycleを禁止し、dependency graphをdeterministicにする。

## Sandboxing

macro default capability:

```text
no network
no arbitrary process
no ambient filesystem
no clock
no nondeterministic random
bounded memory
bounded instructions
```

必要なinputはmanifestで宣言する。

```toml
[meta.inputs]
files = ["schema/*.json"]
```

compilerはinput content hashをbuild cache keyに含める。

## Reflection

exposeする情報:

- type name
- fields and constructors
- generic parameters
- visibility
- attributes
- source span
- trait implementations where stable

private bodyやarbitrary compiler internalsはstable APIにしない。

## Expansion inspection

```bash
cofn expand src/main.cofn
cofn expand --macro derive_json src/model.cofn
```

outputにはgenerated source、source mapping、type informationを含める。

## Macro testing

```cofn
meta test "derive JSON" {
    let expanded = expand(quote { @derive(json) type User = { id: Int } })
    expect(expanded).to_match_snapshot()
}
```

compiler conformance suiteではhygiene、determinism、resource limit、error spanを検査する。

## Compatibility

macro APIはcompiler internal ASTを直接公開しない。versioned public meta IRを介する。

- stable schema
- capability declaration
- edition compatibility
- generated code migration
- deprecation diagnostics

## Stage 0

未実装。parserは`meta fn` declaration flagだけ保持できる。実行、quote/unquote、hygiene、sandboxはbacklog対象。
