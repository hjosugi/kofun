.PHONY: help compiler test diagnostics fuzz check bootstrap stage2 native wasm tour c-abi rust-shim http cli-framework tui-framework stdlib build-system packages lsp roadmap syntax repository-check verify clean

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
	  'make lsp              Verify the stdio language server and editor client' \
	  'make roadmap          Verify the executable issues 31-34 roadmap' \
	  'make syntax           Verify syntax contracts for issues 35-60' \
	  'make repository-check Require .kofun sources and the Kofun toolchain' \
	  'make verify           Run every available gate'

compiler:
	@$(KOFUN) --version

test: compiler
	sh tests/cli.sh
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

verify: test diagnostics fuzz check bootstrap stage2 native wasm tour c-abi rust-shim http cli-framework tui-framework stdlib build-system packages lsp roadmap syntax repository-check
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
	  stdlib/map/tests/verify.sh stdlib/json/tests/verify.sh \
	  tests/cli.sh tests/build_system.sh \
	  package/manager.sh tests/package_manager.sh \
	  tests/lsp/check.sh tooling/lsp/kofun-lsp \
	  editor/vscode/server/kofun-lsp \
	  tests/conformance/run.sh tests/conformance/backends/c11-stage1.sh \
	  tests/conformance/backends/native-x86_64.sh \
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
