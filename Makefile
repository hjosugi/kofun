.PHONY: help compiler test check bootstrap stage2 native stdlib roadmap syntax repository-check verify clean

KOFUN := ./bin/kofun

help:
	@printf '%s\n' \
	  'make compiler         Build the Python-free Kofun compiler seed' \
	  'make test             Exercise build/run/check/test' \
	  'make check            Check canonical bootstrap sources' \
	  'make bootstrap        Verify the Stage 1 seed path' \
	  'make stage2           Verify the Stage 2 semantic frontend checkpoint' \
	  'make native           Build and execute the Kofun-emitted ELF64 fixture' \
	  'make stdlib           Verify the Kofun syscall/stdlib contracts' \
	  'make roadmap          Verify the executable issues 31-34 roadmap' \
	  'make syntax           Verify syntax contracts for issues 35-60' \
	  'make repository-check Require .kofun sources and no Python files' \
	  'make verify           Run every available gate'

compiler:
	@$(KOFUN) --version

test: compiler
	sh tests/cli.sh
	$(KOFUN) test tests/conformance/numeric

check: compiler
	$(KOFUN) check bootstrap/fixtures/answer.kofun

bootstrap:
	sh bootstrap/stage1/check.sh

stage2:
	sh bootstrap/stage2/check.sh

native:
	sh bootstrap/native/check.sh

stdlib:
	sh stdlib/tests/verify.sh

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

verify: test check bootstrap stage2 native stdlib roadmap syntax repository-check
	@sh -n bin/kofun bootstrap/stage1/check.sh bootstrap/stage2/check.sh \
	  bootstrap/native/check.sh stdlib/tests/verify.sh tests/cli.sh \
	  tests/conformance/run.sh tests/conformance/backends/c11-stage1.sh \
	  spec/roadmap-31-34/verify-current-gates.sh \
	  tests/conformance/syntax/issues_35_47/run.sh \
	  tests/conformance/syntax/issues_48_60/run.sh
	@git diff --check

clean:
	rm -rf build .tmp-*
