.PHONY: help test demo check laws native bootstrap backlog repository-check verify clean

PYTHON ?= python3
KOFUN := PYTHONPATH=src $(PYTHON) -m kofun.cli

help:
	@printf '%s\n' \
	  'make test             Run Python and Kofun tests' \
	  'make demo             Run interpreter examples' \
	  'make laws             Verify passing and failing Monad-law fixtures' \
	  'make native           Build native Fibonacci demo' \
	  'make bootstrap        Verify the Kofun-written Stage 1 seed' \
	  'make backlog          Regenerate 13,500 issues' \
	  'make repository-check Validate links, versions, manifests, and generated files' \
	  'make verify           Run all repository checks'

test:
	PYTHONPATH=src $(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v
	$(KOFUN) test tests/kofun

demo:
	$(KOFUN) run examples/hello.kofun
	$(KOFUN) run examples/pipeline.kofun
	$(KOFUN) run examples/science.kofun
	$(KOFUN) run examples/ownership.kofun

check:
	$(KOFUN) check examples/hello.kofun
	$(KOFUN) check examples/pipeline.kofun
	$(KOFUN) check examples/science.kofun
	$(KOFUN) check examples/ownership.kofun
	$(KOFUN) check examples/lawful_list_monad.kofun
	$(KOFUN) check examples/proven_optional_bool_monad.kofun

laws:
	$(KOFUN) laws examples/lawful_list_monad.kofun
	$(KOFUN) laws examples/proven_optional_bool_monad.kofun \
	  --require-assurance proven-finite \
	  --output artifacts/optional-bool-monad.evidence.json
	@! $(KOFUN) laws examples/broken_list_monad.kofun >/dev/null 2>&1 || \
	  (printf '%s\n' 'broken Monad fixture unexpectedly passed' >&2; exit 1)

native:
	@printf '%s\n' '--- direct x86-64 backend (no C, no clang, no ld) ---'
	$(KOFUN) build examples/fibonacci_native.kofun -o build/fibonacci
	./build/fibonacci
	@file build/fibonacci | grep -q 'statically linked' || \
	  (printf '%s\n' 'native backend produced a non-static binary' >&2; exit 1)
	@printf '%s\n' '--- C11 bootstrap backend ---'
	$(KOFUN) build examples/fibonacci_native.kofun --backend c -o build/fibonacci-c
	./build/fibonacci-c

bootstrap:
	PYTHONPATH=src $(PYTHON) bootstrap/check_bootstrap.py

backlog:
	$(PYTHON) scripts/generate_backlog.py

repository-check:
	$(PYTHON) scripts/verify_backlog.py
	$(PYTHON) scripts/verify_repository.py

verify: test check laws native bootstrap repository-check
	$(KOFUN) fmt --check examples/*.kofun tests/kofun/*.kofun bootstrap/stage1/*.kofun bootstrap/fixtures/*.kofun

clean:
	rm -rf build .tmp-* .pytest_cache .mypy_cache .ruff_cache
	find . -type d -name __pycache__ -prune -exec rm -rf {} +
	find . -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete
