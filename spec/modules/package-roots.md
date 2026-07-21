# Package roots and package identity

Status: normative design target for GitHub issue #284.

This document defines how a Kofun invocation selects a package root and forms
the structured package identity consumed by visibility, module mapping,
imports, compiled interfaces, and caches. It preserves the two build paths
already documented in `docs/BUILD_SYSTEM.md`; it does not claim that the
compiler exposes `PackageId` yet.

The words **must**, **must not**, **should**, and **may** are normative.

## Terms

- A **package root** is the directory containing the selected `kofun.toml`.
- A **manifest package** is the set of sources and targets selected from that
  manifest.
- An **anonymous single-file package** is the non-importable package created
  for one explicit `.kofun` source operand.
- A **logical path** is a `/`-separated, package-relative path used in
  identities and diagnostics.
- `PackageIdPayload` is the versioned structured identity below. The digest
  algorithm used by compiled interfaces is intentionally left to #303.

`PackageIdPayload` is a semantic identity. A canonical or absolute filesystem
path is not a package identity.

## Root selection

Root selection occurs before source discovery and remains fixed for the whole
invocation.

1. `kofun build FILE.kofun` selects an anonymous single-file package
   containing exactly that source. The command must not inspect a neighboring
   or ancestor `kofun.toml`.
2. Argument-free `kofun build` requires `./kofun.toml`. The directory
   containing that exact file is the package root.
3. A future explicit `--manifest-path PATH` selects exactly that manifest
   after containment validation.
4. Kofun v1 performs no nearest-ancestor manifest search.
5. Environment variables, directory names, and nested but unselected
   manifests cannot change the selected root.

The explicit-source rule is already executable and guarded by
`tests/build_system.sh`. The `--manifest-path` form is specified for future
compatibility and is not implemented evidence.

## Logical path contract

Manifest source entries are UTF-8 logical paths relative to the selected
package root. An accepted logical path:

- is non-empty and not absolute;
- uses `/`, never `\`, as its separator;
- has no empty, `.` or `..` component;
- has no NUL scalar, drive prefix, or URI scheme; and
- resolves to a regular declared input inside the selected root.

Before reading an input, the implementation must check that symlink traversal
does not escape the root. It must not rewrite an escaping path back under the
root or trust a lexical prefix check alone.

Logical path comparison is byte-for-byte after the language's accepted Unicode
normalization. Host filesystem case folding never merges package identities.
If two logical paths refer to colliding host entries, compilation fails with
both logical paths rather than selecting one by discovery order.

Diagnostics use logical paths. Absolute host paths may appear only as optional
debug detail and never in a serialized semantic identity or reproducible
artifact.

## Manifest package identity

The v1 canonical payload has this field order:

~~~text
kofun.package-id/v1
kind=manifest
name=<normalized workspace/package name>
version=<declared version or unspecified>
source=<locked source identity or workspace-root>
edition=<declared edition or unspecified>
manifest-schema=<decimal schema version>
~~~

The current manifest uses `[workspace].name`; until a dedicated package table
exists, that value supplies `name`. Existing manifests without version or
edition remain valid and use the literal `unspecified`. The selected root
package uses `workspace-root` until a lock/workspace contract supplies a
stronger source identity.

`unspecified` and `workspace-root` are canonical values, not omitted fields.
This prevents two implementations from hashing different shapes for the same
current manifest. A publishable or imported dependency will require an
accepted, locked source identity; `workspace-root` is not sufficient registry
identity.

The payload excludes:

- absolute and canonical checkout paths;
- source contents and modification times;
- inode/device numbers and usernames;
- environment variables and process identifiers;
- target names, target triples, optimization flags, and output paths; and
- source discovery order.

Target identity and target ABI identity are separate contracts. Multiple
targets in one manifest package share `PackageIdPayload` and therefore share
package-internal visibility.

## Anonymous single-file identity

The v1 canonical payload is:

~~~text
kofun.package-id/v1
kind=anonymous-single-file
logical-source=<validated source filename>
~~~

The logical source is the filename within the invocation's anonymous package,
not its absolute parent path. This identity is scoped to the invocation. An
anonymous package cannot be imported, published, used as a stable dependency,
or merged with another explicit source merely because its payload matches.

The build manifest beside the source has no effect. A future multi-source
direct mode requires a separate contract and cannot silently expand the
anonymous package.

## Workspace, targets, and tests

- One selected manifest root defines one package even when it declares
  multiple build targets.
- Target-specific flags and artifacts do not change `PackageIdPayload`.
- A white-box test explicitly compiled as a target of the package may access
  `internal` declarations.
- A black-box test compiled as a dependent package may access only `pub`
  declarations.
- A nested directory becomes another package only through an explicit
  workspace or dependency selection rule. Directory nesting alone grants no
  visibility.
- Package merging and friend packages are unsupported.

These rules provide the package boundary required by #285. Source-file and
module identities are defined separately by #300 and must consume the selected
package payload rather than rediscovering a root.

## Errors and resource safety

Root and path failures must identify the failed rule, the logical input where
available, and one safe remedy. A diagnostic must not dump unrelated
environment state or credentials.

Manifest bytes, source count, logical path length, path component count, and
symlink traversal depth require deterministic implementation limits. Crossing
a limit is a compile error before an output artifact or reusable cache success
is committed.

Changing the current working directory, target list, checkout parent,
timestamp, or host username must not change the package payload. Copying the
same logical manifest package into two clean absolute directories produces the
same payload.

## Compatibility

The first line is the identity schema and is part of canonical serialization.
A schema change uses a new domain string. Readers must reject an unknown
required schema with a rebuild instruction; they must not guess missing fields.

The SHA-256 values exercised by `spec/package-roots/check.sh` are repository
regression fingerprints for these example payload bytes. They do not choose
the compiled-interface hash algorithm, which remains a #303 decision.

## Non-goals

This contract does not define dependency solving, registry names, source
download, workspace-member syntax, module declarations, imports, compiled
interfaces, cache eviction, or package initialization. Locked native library
artifacts in `kofun.packages.toml` are not Kofun source packages.

## Acceptance examples

The reference gate covers:

- direct-source selection beside a manifest;
- exact current-directory manifest selection;
- path-remapped payload equality;
- target-independent payloads;
- logical path acceptance and traversal/drive/backslash rejection;
- symlink escape detection; and
- stable manifest and anonymous payload fingerprints.

Until an implementation exposes these identities, passing the gate proves the
normative serialization examples and existing CLI dispatch only. It does not
claim multi-file module or dependency support.
