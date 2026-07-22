# Explicit non-widening re-exports

Status: accepted normative design for GitHub issue #287.

This document defines how a module forwards an imported module or selected
declaration as part of its public API. It builds on the package/module,
namespace, visibility, and identity contracts in #284, #300, #290, #285, and
#303.

The words **must**, **must not**, **should**, and **may** are normative.

## Decision

Kofun uses the existing contextual `pub` modifier on import declarations:

~~~kofun
pub import collections
pub from collections import Map, Set
~~~

This is Option A from #287. `export import`, manifest export lists, and implicit
re-export of ordinary imports are rejected. One explicit `pub` vocabulary
makes an API edge visible at its source and does not turn implementation
dependencies into compatibility commitments accidentally.

The first slice has no `internal` re-export form. An ordinary `import` remains
a private import binding and is never re-exported implicitly. `pub` on a
re-export requests public reachability; it is not a request to widen the
target.

## Grammar and position

The grammar extends the accepted import forms:

~~~text
re-export-declaration := "pub" qualified-import
                       | "pub" selective-import
qualified-import      := "import" module-path NEWLINE
selective-import      := "from" module-path "import" import-name-list NEWLINE
import-name-list      := identifier ("," identifier)* ","?
~~~

`pub`, `import`, and `from` remain contextual in these declaration positions.
The lexer does not make them hard keywords. The re-export appears in the same
header region as ordinary imports: after the required manifest-source module
header and before ordinary declarations.

`pub import` exports one module-namespace qualifier whose local/exported name
is the final component of `module-path`. `pub from ... import ...` exports the
accessible targets selected under #290: one requested spelling may create at
most one binding in each of the value, type, module, and meta namespaces.

The first slice rejects:

- `internal import`, `private import`, `export import`, and bare `export`;
- wildcard/glob forms;
- module or per-name aliases;
- an empty selected-name list;
- relative, external-package, or manifest-defined forwarding;
- a re-export after an ordinary declaration; and
- a modifier separated from the import declaration by another declaration.

These failures are explicit diagnostics. None is interpreted as an ordinary
private import.

## Binding model

A re-export has three distinct identities:

1. the source import binding, identified by `ImportBindingId` in the facade;
2. the forwarding API edge, identified by `ExportBindingId`;
3. the original target declaration identity, which remains its
   `NamespaceId`/`SymbolId` (and `TypeId` where applicable).

The forwarding edge does not copy, wrap, or rename the target declaration.
Every resolved use through the facade carries both the export path provenance
and original target identity.

For a qualified module re-export, the target has a canonical module
self-symbol so the #303 `ExportBindingId` contract remains uniform:

~~~text
ModuleSelfSymbolId = SymbolId(
    module_id = target ModuleId,
    namespace_id = module NamespaceId,
    declaration_kind = module,
    declared_name = canonical full declared module path
)
~~~

The full declared path is already validated UTF-8/NFC under #300. A module
self-symbol is an identity/protocol record, not a second module declaration
and not a replacement for `ModuleId`. Resolver graph edges retain the target
`ModuleId`; namespace-generic export tables retain the module self-symbol.

For a selected re-export, the target `SymbolId` is the declaration symbol
collected by #111 and later declaration-kind extensions. A same-spelled value
and type produce two export bindings with distinct `NamespaceId` and
`ExportBindingId` values.

## ExportBindingId v1 payload

The production ID uses the #303 framed SHA-256 domain
`kofun.id.export-binding/v1`. Its canonical KIF record has strictly increasing
required tags:

| Tag | Value |
| ---: | --- |
| `0x8001` | raw 32-byte exporting `ModuleId` |
| `0x8002` | raw 32-byte exported `NamespaceId` |
| `0x8003` | exported UTF-8/NFC name |
| `0x8004` | raw 32-byte original target `SymbolId` |
| `0x8005` | one-byte effective visibility tag |

Visibility tags for this schema are `0=private`, `1=restricted`,
`2=internal`, and `3=public`, matching the accepted ordering. A successful v1
source re-export always records `3`; narrower targets fail before an export ID
is committed.

Source/file order, `FileId`, import/export span, absolute/logical path, chain
length, documentation, signature, body, linker name, and target address are
not ID inputs. They remain provenance or target facts.

Changing the exported name, namespace, target identity, effective visibility,
or exporting module changes `ExportBindingId`. Moving/reformatting the source
without changing those facts does not.

## Effective visibility and non-widening

Visibility is ordered:

~~~text
private < restricted < internal < public
~~~

For re-export edge `R` targeting declaration/module `D`:

~~~text
effective(R) = minimum(
    requested visibility of R,
    effective visibility of D,
    effective visibility of every enclosing declaration/type of D,
    effective visibility of every reachable module/export path segment,
    effective visibility of every signature fact exposed through D
)
~~~

The source form requests `public`. Therefore `effective(R)` must be `public`
or compilation fails. The compiler never silently narrows a `pub` re-export
to `internal`, `restricted`, or `private`.

Consequences:

- a private or internal target cannot be publicly re-exported;
- a public member behind a non-public enclosing type/module cannot be
  publicly re-exported;
- a public function/type whose exposed signature leaks a hidden type, trait,
  effect, ownership contract, field, or constructor fails before export;
- importing a target successfully inside the package does not imply it can be
  re-exported publicly; and
- FFI/linker visibility is not granted by a source re-export.

V1 has no independent visibility modifier on a `module` header. A module path
segment is publicly reachable only through package-root reachability and
committed public export edges. If future syntax adds module visibility, its
effective level joins the same minimum; it does not change this rule.

Access checks use the identity-only #582 result and disclosure policy. A
diagnostic must not reveal a hidden declaration that the caller is not allowed
to inspect.

## Lookup through a facade

An importing consumer resolves a facade path in this order:

1. resolve the facade's local import/module binding;
2. select the exported namespace from syntax under #290;
3. find exactly one `ExportBindingId` for the exported name/namespace;
4. follow the edge to the original target `SymbolId`/`ModuleId`;
5. revalidate compatibility and effective access in the consumer context;
6. put the original target identity plus export-chain provenance in HIR.

There is no fallback to a same-spelled private declaration, another namespace,
an unqualified import, filesystem path, or later source declaration. An export
does not make the target module's other names visible unless they have their
own reachable path.

An ordinary import of a facade can consume its public exports; it does not
receive the facade's private imports or transitive dependencies.

## Duplicates and collisions

The facade may contain at most one binding for a normalized exported name in
each namespace. Re-export bindings participate in the same module-scope
duplicate matrix as local declarations and imports:

- same namespace/name: reject;
- same spelling in different namespaces: allow under #290;
- repeated identical re-export: reject rather than silently coalesce;
- two paths to the same target under the same namespace/name: reject in v1;
- re-export versus local public declaration in the same namespace/name:
  reject; and
- qualified module re-export versus a value/type spelling: coexist according
  to the deterministic dotted-path rule.

Diagnostics show both facade binding spans and stable identities in canonical
identity/provenance order. Source order never chooses a winner.

## Chains and cycles

Following a chain preserves the original target `NamespaceId`, `SymbolId`,
`TypeId`, and declaration visibility. Each facade edge has its own
`ExportBindingId` and provenance.

The canonical chain is the ordered list from the consumer-visible facade edge
to the original target. Equivalent source/import enumeration must serialize
the same list. A chain may contain at most 64 export edges, matching #303.
Exactly 64 succeeds; attempting a 65th edge is a stable error before HIR,
interface, object, executable, or cache success.

Re-export cycles are invalid even if an import graph cycle would otherwise be
accepted in a future edition. The resolver reports one canonical shortest
cycle:

1. choose the cycle with the fewest edges;
2. rotate it to start at the smallest raw exporting `ModuleId`;
3. compare the rotated `(ModuleId, ExportBindingId)` sequence
   lexicographically; and
4. show each edge/import span in that order, closing the path at the start.

Cycle detection is iterative and operation-bounded. Budget exhaustion is an
error, never an assumption that the graph is acyclic.

## Compiled interfaces and digests

Every successful public re-export is a public semantic fact in the exporting
module's KIF interface. The fact contains:

- `ExportBindingId`, exported namespace/name, and effective visibility;
- original target `SymbolId` (and target `ModuleId` for module exports);
- canonical target signature/type facts required by consumers;
- a bounded canonical chain of export identities; and
- compatibility data required by #303.

Adding, removing, renaming, retargeting, narrowing, or changing the chain of a
public re-export changes the facade's public semantic digest. It changes the
package-internal digest because that view includes the public vector. It
changes a target ABI digest only when the exported linker/layout/ABI surface
changes under the target contract.

A private target body edit that leaves the original public target facts and
all export edges unchanged does not change the facade digest. The compiler
does not duplicate private target implementation facts into the facade KIF.

JSON and typed semantic sidecars may display the facade and canonical paths,
but they remain non-authoritative under #303/#575.

## Documentation and tooling

Documentation presents both:

- the public facade path selected by the author; and
- the canonical declaration path/identity for navigation.

Definition jumps may go directly to the declaration while “show export path”
walks the bounded chain. Rename/refactoring distinguishes changing a facade
name/edge from renaming the original declaration. References are keyed by the
original target identity and annotate the export path used.

Tooling must not infer a re-export by scraping docs, linker symbols, or an
ordinary import that happens to be used in a public signature.

## Diagnostics

Stable diagnostic categories cover:

- malformed or misplaced `pub import`/`pub from` syntax;
- unsupported alias, wildcard, `internal`, `private`, or `export` forms;
- missing or inaccessible target;
- visibility widening and hidden-signature leakage;
- duplicate/colliding export binding;
- chain depth and graph-operation limit;
- canonical re-export cycle;
- incompatible/corrupt target interface; and
- internal invariant failure with no artifact commit.

Each source-driven diagnostic includes the facade module/logical path, primary
re-export/name span, safe target/secondary context, requested/effective
visibility, failed rule, and one actionable remedy. An absolute checkout path
is never part of serialized diagnostics.

## Limits and transaction boundary

V1 uses these fixed or inherited limits:

- at most 256 re-export declarations per module;
- at most 1,024 export bindings per module after namespace expansion;
- at most four namespace bindings for one selective spelling;
- at most 64 path components and 4,096 UTF-8 bytes per module path;
- at most 256 UTF-8 bytes per exported identifier in the bootstrap subset;
- at most 64 edges in one canonical export chain;
- at most 65,536 export edges per package compilation; and
- the 16 MiB KIF envelope/count/depth limits from #303.

Implementations validate counts with checked arithmetic before allocation or
commit. Each exact boundary succeeds where practical and one-over fails.

The complete package import/export graph, target visibility, signatures, and
cycles are validated before any export table, KIF, HIR, object, executable,
documentation index, or cache success is committed. A failure in one facade
suppresses the requested package result; readers never observe a partial
export graph.

## Compatibility

Adding a public re-export is an additive public API change but may create a
source ambiguity for consumers that import multiple facades. Removing,
renaming, retargeting, or narrowing one is a breaking public API change.

The exact grammar, ID domain/fields, visibility tags, module-self-symbol rule,
cycle selection, chain limit, and KIF fact meaning are versioned contracts.
Changing their meaning requires the schema/edition migration and rebuild rules
from #303; a reader never guesses an older export edge.

## Implementation status and non-goals

`spec/re-exports/check.sh` is the executable reference gate for syntax
inventory, effective visibility, production `ExportBindingId` framing,
identity preservation, ordering, chain boundaries, cycle ordering, and
transaction language. General import/re-export resolution is not yet routed
through the active compiler; #113 and #114 provide its prerequisite import
bindings.

This contract does not implement external packages, aliases, wildcard/glob
exports, source-level signature files, linker symbol export, runtime module
initialization, macro-generated exports, or automatic facade synthesis. No v1
design question remains open.
