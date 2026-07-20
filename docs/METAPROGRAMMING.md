# Metaprogramming

## Goals

- the fun of Lisp quote/unquote
- Perl's pragmatic code generation
- the practicality of Rust derive macros
- the type-aware tooling of the TypeScript compiler API
- build reproducibility and security

## Non-goals

- textual substitution
- unrestricted compiler-process execution
- order-dependent global mutation
- loss of source locations
- bypassing type safety through macro expansion

## Levels

### Token macros

For small syntax abstractions.

```kofun
macro assert_close(actual, expected, tolerance) {
    # token-tree expansion
}
```

### Typed AST macros

Operate on declarations and expressions as typed nodes.

```kofun
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

Pure, deterministic, bounded computation.

```kofun
const table = meta generate_lookup_table(256)
```

## Hygiene

- a generated identifier belongs to the macro scope
- capturing a caller binding requires an explicit API
- both the generated span and the call-site span are retained
- diagnostics show the expansion stack

## Staging

Conceptual phases:

```text
parse
-> token macro expansion
-> name resolution
-> typed AST macro expansion
-> type re-check
-> const/meta evaluation
-> lowering
```

Phase cycles are forbidden, which keeps the dependency graph deterministic.

## Sandboxing

Default macro capabilities:

```text
no network
no arbitrary process
no ambient filesystem
no clock
no nondeterministic random
bounded memory
bounded instructions
```

Required inputs are declared in the manifest.

```toml
[meta.inputs]
files = ["schema/*.json"]
```

The compiler includes the input content hash in the build cache key.

## Reflection

Information exposed:

- type name
- fields and constructors
- generic parameters
- visibility
- attributes
- source span
- trait implementations where stable

Private bodies and arbitrary compiler internals are not part of the stable API.

## Expansion inspection

```bash
kofun expand src/main.kofun
kofun expand --macro derive_json src/model.kofun
```

The output includes generated source, source mapping, and type information.

## Macro testing

```kofun
meta test "derive JSON" {
    let expanded = expand(quote { @derive(json) type User = { id: Int } })
    expect(expanded).to_match_snapshot()
}
```

The compiler conformance suite checks hygiene, determinism, resource limits, and error spans.

## Compatibility

The macro API does not expose the compiler's internal AST directly. It goes through a versioned public meta IR.

- stable schema
- capability declaration
- edition compatibility
- generated code migration
- deprecation diagnostics

## Stage 0

Unimplemented. The parser can hold nothing more than a `meta fn` declaration flag. Execution, quote/unquote, hygiene, and sandboxing are all backlog items.
