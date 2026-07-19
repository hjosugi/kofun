.PHONY: help test demo check laws native bootstrap backlog repository-check verify clean

PYTHON ?= python3
FROST := PYTHONPATH=src $(PYTHON) -m frost.cli

help:
	@printf '%s\n' \
	  'make test             Run Python and Frost tests' \
	  'make demo             Run interpreter examples' \
	  'make laws             Verify passing and failing Monad-law fixtures' \
	  'make native           Build native Fibonacci demo' \
	  'make bootstrap        Verify the Frost-written Stage 1 seed' \
	  'make backlog          Regenerate 13,500 issues' \
	  'make repository-check Validate links, versions, manifests, and generated files' \
	  'make verify           Run all repository checks'

test:
	PYTHONPATH=src $(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v
	$(FROST) test tests/frost

demo:
	$(FROST) run examples/hello.frost
	$(FROST) run examples/pipeline.frost
	$(FROST) run examples/science.frost
	$(FROST) run examples/ownership.frost

check:
	$(FROST) check examples/hello.frost
	$(FROST) check examples/pipeline.frost
	$(FROST) check examples/science.frost
	$(FROST) check examples/ownership.frost
	$(FROST) check examples/lawful_list_monad.frost
	$(FROST) check examples/proven_optional_bool_monad.frost

laws:
	$(FROST) laws examples/lawful_list_monad.frost
	$(FROST) laws examples/proven_optional_bool_monad.frost \
	  --require-assurance proven-finite \
	  --output artifacts/optional-bool-monad.evidence.json
	@! $(FROST) laws examples/broken_list_monad.frost >/dev/null 2>&1 || \
	  (printf '%s\n' 'broken Monad fixture unexpectedly passed' >&2; exit 1)

native:
	@printf '%s\n' '--- direct x86-64 backend (no C, no clang, no ld) ---'
	$(FROST) build examples/fibonacci_native.frost -o build/fibonacci
	./build/fibonacci
	@file build/fibonacci | grep -q 'statically linked' || \
	  (printf '%s\n' 'native backend produced a non-static binary' >&2; exit 1)
	@printf '%s\n' '--- C11 bootstrap backend ---'
	$(FROST) build examples/fibonacci_native.frost --backend c -o build/fibonacci-c
	./build/fibonacci-c

bootstrap:
	PYTHONPATH=src $(PYTHON) bootstrap/check_bootstrap.py

backlog:
	$(PYTHON) scripts/generate_backlog.py

repository-check:
	$(PYTHON) scripts/verify_backlog.py
	$(PYTHON) scripts/verify_repository.py

verify: test check laws native bootstrap repository-check
	$(FROST) fmt --check examples/*.frost tests/frost/*.frost bootstrap/stage1/*.frost bootstrap/fixtures/*.frost

clean:
	rm -rf build .tmp-* .pytest_cache .mypy_cache .ruff_cache
	find . -type d -name __pycache__ -prune -exec rm -rf {} +
	find . -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete
