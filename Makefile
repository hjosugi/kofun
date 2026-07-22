.PHONY: help compiler test diagnostics fuzz check bootstrap stage2 patterns adt adt-exhaustiveness module-symbols imports-qualified imports-selective kif-v1 native wasm tour c-abi rust-shim http cli-framework tui-framework stdlib build-system packages package-roots source-file-mapping namespaces module-identity visibility-spec visibility-syntax visibility-access re-exports-spec typed-sidecar-spec typed-sidecar-codec lsp roadmap syntax repository-check verify clean

KOFUN := ./bin/kofun

help:
	@printf '%s\n' \
	  'make compiler         Build the Python-free Kofun compiler seed' \
	  'make test             Exercise build/run/check/test' \
	  'make diagnostics      Verify must-fail diagnostic goldens' \
	  'make fuzz             Run deterministic grammar and semantic fuzz smoke tests' \
	  'make check            Check canonical bootstrap sources' \
	  'make bootstrap        Verify the Stage 1 seed path' \
	  'make stage2           Verify the Stage 2 semantic frontend checkpoint' \
	  'make patterns         Verify lossless general Pattern syntax trees' \
	  'make adt              Verify bounded nominal ADT typed frontend' \
	  'make adt-exhaustiveness Verify resolved flat-ADT match diagnostics' \
	  'make module-symbols   Verify stable top-level declaration collection' \
	  'make imports-qualified Verify qualified same-package module imports' \
	  'make imports-selective Verify selective same-package name imports' \
	  'make kif-v1           Verify authoritative compiled interfaces' \
	  'make native           Build and execute the Kofun-emitted ELF64 fixture' \
	  'make wasm             Build and execute the wasm32 arithmetic Core' \
	  'make tour             Verify the no-install browser learning tour' \
	  'make c-abi            Verify explicit dynamic C ABI interoperability' \
	  'make rust-shim        Verify the vendored Rust crate shim offline' \
	  'make http             Verify the first-party HTTP/API framework' \
	  'make cli-framework    Verify the direct-static native CLI framework' \
	  'make tui-framework    Verify the shared terminal UI framework' \
	  'make stdlib           Verify the Kofun syscall/stdlib contracts' \
	  'make build-system     Verify direct and Frost-integrated build paths' \
	  'make packages         Verify locked package fetch and offline use' \
	  'make package-roots    Verify package-root specification examples' \
	  'make source-file-mapping Verify source/module identity examples' \
	  'make namespaces       Verify semantic namespace and lookup examples' \
	  'make module-identity  Verify stable IDs and interface digest examples' \
	  'make visibility-spec  Verify declaration-visibility specification examples' \
	  'make visibility-syntax Verify executable function visibility syntax' \
	  'make visibility-access Verify identity-only visibility enforcement' \
	  'make re-exports-spec  Verify explicit non-widening re-export design' \
	  'make typed-sidecar-spec Verify bounded complete/partial tooling artifacts' \
	  'make typed-sidecar-codec Verify production reader/writer and atomic replacement' \
	  'make lsp              Verify the stdio language server and editor client' \
	  'make roadmap          Verify the executable issues 31-34 roadmap' \
	  'make syntax           Verify syntax contracts for issues 35-60' \
	  'make repository-check Require .kofun sources and the Kofun toolchain' \
	  'make verify           Run every available gate'

compiler:
	@$(KOFUN) --version

test: compiler
	sh tests/cli.sh
	sh tests/conformance/modules/lexical-scopes/run.sh
	sh tests/conformance/modules/shadowing/run.sh
	$(KOFUN) test tests/conformance/numeric
	$(KOFUN) test tests/conformance/functions
	$(KOFUN) test tests/conformance/list
	$(KOFUN) test tests/conformance/text

diagnostics:
	sh tests/diagnostics/stage2/run.sh

fuzz:
	sh tests/fuzz/grammar.sh
	sh tests/fuzz/semantic_differential.sh
	sh tests/fuzz/value_if.sh
	sh tests/fuzz/match_guard.sh
	sh tests/fuzz/match_value.sh
	sh tests/fuzz/match_value_invalid.sh
	sh tests/fuzz/enum_match.sh

check: compiler
	$(KOFUN) check bootstrap/fixtures/answer.kofun

bootstrap:
	sh bootstrap/stage1/check.sh

stage2:
	sh bootstrap/stage2/check.sh

patterns:
	sh tests/conformance/patterns/run.sh

adt:
	sh tests/conformance/adt/run.sh

adt-exhaustiveness:
	sh tests/conformance/adt-exhaustiveness/run.sh

module-symbols:
	sh tests/conformance/modules/top-level-declarations/run.sh

imports-qualified:
	sh tests/conformance/modules/imports-qualified/run.sh

imports-selective:
	sh tests/conformance/modules/imports-selective/run.sh

kif-v1:
	sh tests/conformance/modules/kif-v1/run.sh

native:
	sh bootstrap/native/check.sh

wasm:
	sh bootstrap/wasm/check.sh

tour:
	sh docs/tour/check.sh

c-abi:
	sh bootstrap/c_abi/check.sh

rust-shim:
	sh examples/rust-shim/check.sh

http:
	sh tests/http/check.sh

cli-framework:
	sh framework/cli/check.sh

tui-framework:
	sh framework/tui/check.sh

stdlib:
	sh stdlib/tests/verify.sh

build-system:
	sh tests/build_system.sh

packages:
	sh tests/package_manager.sh

package-roots:
	sh spec/package-roots/check.sh

source-file-mapping:
	sh spec/source-file-mapping/check.sh

namespaces:
	sh spec/namespaces/check.sh

module-identity:
	sh spec/module-identity/check.sh

visibility-spec:
	sh spec/visibility/check.sh

visibility-syntax:
	sh tests/conformance/modules/visibility-syntax/run.sh

visibility-access:
	sh tests/conformance/modules/visibility-access/run.sh

re-exports-spec:
	sh spec/re-exports/check.sh

typed-sidecar-spec:
	sh spec/typed-sidecar/check.sh

typed-sidecar-codec:
	sh tests/typed-sidecar/codec.sh
	sh tests/typed-sidecar/atomic-write.sh
	sh tests/typed-sidecar/authority-boundary.sh

lsp:
	sh tests/lsp/check.sh

roadmap:
	sh spec/roadmap-31-34/verify-current-gates.sh

syntax:
	sh tests/conformance/syntax/issues_35_47/run.sh
	sh tests/conformance/syntax/issues_48_60/run.sh

repository-check:
	@! find . -path './.git' -prune -o -path './build' -prune -o \
	  -type f \( -name '*.py' -o -name '*.pyc' -o -name '*.pyo' \) -print | grep -q .
	@! find . -path './.git' -prune -o -path './build' -prune -o \
	  -type f -name '*.kf' -print | grep -q .
	@test ! -e pyproject.toml
	@grep -q '"extensions": \[".kofun"\]' editor/vscode/package.json
	@printf '%s\n' 'PASS: .kofun sources only; no Python implementation'

verify: test diagnostics fuzz check bootstrap stage2 patterns adt adt-exhaustiveness module-symbols imports-qualified imports-selective kif-v1 native wasm tour c-abi rust-shim http cli-framework tui-framework stdlib build-system packages package-roots source-file-mapping namespaces module-identity visibility-spec visibility-syntax visibility-access re-exports-spec typed-sidecar-spec typed-sidecar-codec lsp roadmap syntax repository-check
	@sh -n bin/kofun bootstrap/stage1/check.sh bootstrap/stage2/check.sh \
	  bootstrap/native/check.sh bootstrap/native/emit-fixture.sh \
	  bootstrap/wasm/check.sh \
	  examples/wasm-browser/build.sh \
	  docs/tour/check.sh \
	  bootstrap/c_abi/check.sh \
	  examples/rust-shim/check.sh examples/rust-shim/benchmark.sh \
	  framework/http/build.sh tests/http/check.sh \
	  framework/cli/check.sh \
	  framework/tui/build.sh framework/tui/check.sh \
	  benchmarks/http/benchmark.sh \
	  stdlib/tests/verify.sh stdlib/testing/tests/verify.sh \
	  stdlib/logging/tests/verify.sh stdlib/regex/tests/verify.sh \
	  stdlib/clock/tests/verify.sh stdlib/list/tests/verify.sh \
	  stdlib/vector/tests/verify.sh stdlib/array/tests/verify.sh \
	  stdlib/tuple/tests/verify.sh stdlib/set/tests/verify.sh \
	  stdlib/map/tests/verify.sh stdlib/binary_heap/tests/verify.sh \
	  stdlib/json/tests/verify.sh \
	  tests/cli.sh tests/build_system.sh \
	  package/manager.sh tests/package_manager.sh \
	  spec/package-roots/check.sh \
	  spec/source-file-mapping/check.sh \
	  spec/namespaces/check.sh \
	  spec/module-identity/check.sh \
	  spec/visibility/check.sh \
	  spec/re-exports/check.sh \
	  spec/typed-sidecar/check.sh \
	  tests/typed-sidecar/codec.sh \
	  tests/typed-sidecar/atomic-write.sh \
	  tests/typed-sidecar/authority-boundary.sh \
	  tests/conformance/modules/visibility-syntax/run.sh \
	  tests/conformance/modules/visibility-access/run.sh \
	  tests/conformance/adt/run.sh \
	  tests/conformance/adt-exhaustiveness/run.sh \
	  tests/conformance/modules/top-level-declarations/run.sh \
	  tests/conformance/patterns/run.sh \
	  tests/conformance/modules/imports-qualified/run.sh \
	  tests/conformance/modules/imports-selective/run.sh \
	  tests/conformance/modules/kif-v1/run.sh \
	  tests/lsp/check.sh tooling/lsp/kofun-lsp \
	  editor/vscode/server/kofun-lsp \
	  tests/conformance/run.sh tests/conformance/backends/c11-stage1.sh \
	  tests/conformance/backends/native-x86_64.sh \
	  tests/conformance/modules/lexical-scopes/run.sh \
	  tests/conformance/modules/shadowing/run.sh \
	  tests/diagnostics/stage2/run.sh \
	  tests/diagnostics/stage2/bless.sh \
	  tests/fuzz/grammar.sh tests/fuzz/semantic_differential.sh \
	  tests/fuzz/value_if.sh tests/fuzz/match_guard.sh \
	  tests/fuzz/match_value.sh tests/fuzz/match_value_invalid.sh \
	  tests/fuzz/enum_match.sh \
	  tests/conformance/backends/wasm32-node.sh \
	  spec/roadmap-31-34/verify-current-gates.sh \
	  tests/conformance/syntax/issues_35_47/run.sh \
	  tests/conformance/syntax/issues_48_60/run.sh
	@$(CC) -std=c11 -fsyntax-only -Wall -Wextra -Werror \
	  tests/process_cpu_time.c
	@git diff --check

clean:
	rm -rf build .tmp-*
