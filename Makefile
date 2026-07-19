.PHONY: help test demo check laws native bootstrap backlog repository-check verify clean

PYTHON ?= python3
COFN := PYTHONPATH=src $(PYTHON) -m cofn.cli

help:
	@printf '%s\n' \
	  'make test             Run Python and Cofn tests' \
	  'make demo             Run interpreter examples' \
	  'make laws             Verify passing and failing Monad-law fixtures' \
	  'make native           Build native Fibonacci demo' \
	  'make bootstrap        Verify the Cofn-written Stage 1 seed' \
	  'make backlog          Regenerate 13,500 issues' \
	  'make repository-check Validate links, versions, manifests, and generated files' \
	  'make verify           Run all repository checks'

test:
	PYTHONPATH=src $(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v
	$(COFN) test tests/cofn

demo:
	$(COFN) run examples/hello.cofn
	$(COFN) run examples/pipeline.cofn
	$(COFN) run examples/science.cofn
	$(COFN) run examples/ownership.cofn

check:
	$(COFN) check examples/hello.cofn
	$(COFN) check examples/pipeline.cofn
	$(COFN) check examples/science.cofn
	$(COFN) check examples/ownership.cofn
	$(COFN) check examples/lawful_list_monad.cofn
	$(COFN) check examples/proven_optional_bool_monad.cofn

laws:
	$(COFN) laws examples/lawful_list_monad.cofn
	$(COFN) laws examples/proven_optional_bool_monad.cofn \
	  --require-assurance proven-finite \
	  --output artifacts/optional-bool-monad.evidence.json
	@! $(COFN) laws examples/broken_list_monad.cofn >/dev/null 2>&1 || \
	  (printf '%s\n' 'broken Monad fixture unexpectedly passed' >&2; exit 1)

native:
	@printf '%s\n' '--- direct x86-64 backend (no C, no clang, no ld) ---'
	$(COFN) build examples/fibonacci_native.cofn -o build/fibonacci
	./build/fibonacci
	@file build/fibonacci | grep -q 'statically linked' || \
	  (printf '%s\n' 'native backend produced a non-static binary' >&2; exit 1)
	@printf '%s\n' '--- C11 bootstrap backend ---'
	$(COFN) build examples/fibonacci_native.cofn --backend c -o build/fibonacci-c
	./build/fibonacci-c

bootstrap:
	PYTHONPATH=src $(PYTHON) bootstrap/check_bootstrap.py

backlog:
	$(PYTHON) scripts/generate_backlog.py

repository-check:
	$(PYTHON) scripts/verify_backlog.py
	$(PYTHON) scripts/verify_repository.py

verify: test check laws native bootstrap repository-check
	$(COFN) fmt --check examples/*.cofn tests/cofn/*.cofn bootstrap/stage1/*.cofn bootstrap/fixtures/*.cofn

clean:
	rm -rf build .tmp-* .pytest_cache .mypy_cache .ruff_cache
	find . -type d -name __pycache__ -prune -exec rm -rf {} +
	find . -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete
