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
	$(KOFUN) run examples/hello.kf
	$(KOFUN) run examples/pipeline.kf
	$(KOFUN) run examples/science.kf
	$(KOFUN) run examples/ownership.kf

check:
	$(KOFUN) check examples/hello.kf
	$(KOFUN) check examples/pipeline.kf
	$(KOFUN) check examples/science.kf
	$(KOFUN) check examples/ownership.kf
	$(KOFUN) check examples/lawful_list_monad.kf
	$(KOFUN) check examples/proven_optional_bool_monad.kf

laws:
	$(KOFUN) laws examples/lawful_list_monad.kf
	$(KOFUN) laws examples/proven_optional_bool_monad.kf \
	  --require-assurance proven-finite \
	  --output artifacts/optional-bool-monad.evidence.json
	@! $(KOFUN) laws examples/broken_list_monad.kf >/dev/null 2>&1 || \
	  (printf '%s\n' 'broken Monad fixture unexpectedly passed' >&2; exit 1)

native:
	@printf '%s\n' '--- direct x86-64 backend (no C, no clang, no ld) ---'
	$(KOFUN) build examples/fibonacci_native.kf -o build/fibonacci
	./build/fibonacci
	@file build/fibonacci | grep -q 'statically linked' || \
	  (printf '%s\n' 'native backend produced a non-static binary' >&2; exit 1)
	@printf '%s\n' '--- C11 bootstrap backend ---'
	$(KOFUN) build examples/fibonacci_native.kf --backend c -o build/fibonacci-c
	./build/fibonacci-c

bootstrap:
	PYTHONPATH=src $(PYTHON) bootstrap/check_bootstrap.py

backlog:
	$(PYTHON) scripts/generate_backlog.py

repository-check:
	$(PYTHON) scripts/verify_backlog.py
	$(PYTHON) scripts/verify_repository.py

verify: test check laws native bootstrap repository-check
	$(KOFUN) fmt --check examples/*.kf tests/kofun/*.kf bootstrap/stage1/*.kf bootstrap/fixtures/*.kf

clean:
	rm -rf build .tmp-* .pytest_cache .mypy_cache .ruff_cache
	find . -type d -name __pycache__ -prune -exec rm -rf {} +
	find . -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete
