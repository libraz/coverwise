.PHONY: all build release test clean rebuild format format-check lint lint-validator-boundary wasm \
	coverage coverage-build coverage-clean python-wheel test-python check-version \
	test-sanitize preflight

BUILD_DIR := build
# Kept separate from $(BUILD_DIR) so a sanitizer build never overwrites the ordinary one.
SANITIZE_BUILD_DIR := build-sanitize

# CI pins ASAN_OPTIONS=detect_leaks=1:halt_on_error=1, which is Linux-specific in two
# ways: macOS has no leak detector (passing detect_leaks aborts every test process),
# and it defaults alloc_dealloc_mismatch to false where Linux defaults it to true.
# Taking CI's string verbatim would therefore both break the run and silently drop a
# check, so each platform gets options at least as strict as CI's.
ifeq ($(shell uname -s),Darwin)
SANITIZE_ASAN_OPTIONS := halt_on_error=1:alloc_dealloc_mismatch=1
else
SANITIZE_ASAN_OPTIONS := detect_leaks=1:halt_on_error=1:alloc_dealloc_mismatch=1
endif
SANITIZE_UBSAN_OPTIONS := halt_on_error=1:print_stacktrace=1

all: build

build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) -j

release:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j

wasm:
	emcmake cmake -B build-wasm -DBUILD_WASM=ON -DCMAKE_BUILD_TYPE=Release
	cmake --build build-wasm -j

test: build test-python
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	rm -rf $(BUILD_DIR) $(SANITIZE_BUILD_DIR)

rebuild: clean build

format:
	find src tests tools -name '*.cpp' -o -name '*.h' | xargs clang-format -i
	# TypeScript auto-fix: js/, tests/wasm/ and src/ts/.
	yarn lint:fix
	rye sync --pyproject bindings/python/pyproject.toml
	rye run --pyproject bindings/python/pyproject.toml ruff format bindings/python/src bindings/python/tests
	rye run --pyproject bindings/python/pyproject.toml ruff check --fix bindings/python/src bindings/python/tests

format-check:
	find src tests tools -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
	yarn lint
	yarn format:check
	rye sync --pyproject bindings/python/pyproject.toml
	rye run --pyproject bindings/python/pyproject.toml ruff format --check bindings/python/src bindings/python/tests
	rye run --pyproject bindings/python/pyproject.toml ruff check bindings/python/src bindings/python/tests

# Static checks that are not formatting; add a new one as another prerequisite here.
lint: lint-validator-boundary

# The validators are ground truth precisely because they enumerate tuples themselves,
# so neither the C++ nor the TypeScript port may reach into core/.
lint-validator-boundary:
	@if git grep -n "core/" -- src/validator src/ts/validator; then \
		echo "Validators must not depend on generator/core internals" >&2; \
		exit 1; \
	fi

python-wheel:
	rye sync --pyproject bindings/python/pyproject.toml
	PYTHON="$$(rye run --pyproject bindings/python/pyproject.toml python -c 'import sys; print(sys.executable)')" bindings/python/scripts/build_wheel.sh

test-python: python-wheel
	rye run --pyproject bindings/python/pyproject.toml python -m pytest bindings/python/tests -v

check-version:
	python3 bindings/python/scripts/check_version.py

test-sanitize:
	cmake -B $(SANITIZE_BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DENABLE_SANITIZERS=ON
	cmake --build $(SANITIZE_BUILD_DIR) -j
	ASAN_OPTIONS=$(SANITIZE_ASAN_OPTIONS) UBSAN_OPTIONS=$(SANITIZE_UBSAN_OPTIONS) \
		ctest --test-dir $(SANITIZE_BUILD_DIR) --output-on-failure --parallel

# Every gate CI runs, cheapest first, so a green run means CI has nothing new to say.
# Steps are recursive make calls rather than prerequisites to keep them ordered under -j.
preflight:
	$(MAKE) check-version
	$(MAKE) lint
	yarn install --immutable
	$(MAKE) format-check
	$(MAKE) build
	ctest --test-dir $(BUILD_DIR) --output-on-failure --parallel
	$(MAKE) test-sanitize
	yarn build
	yarn test:coverage
	$(MAKE) test-python
	@echo ""
	@echo "preflight: all CI gates passed"

coverage-build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
	cmake --build $(BUILD_DIR) -j

LCOV_IGNORE := --ignore-errors inconsistent,inconsistent,format,format,unused,unused,category,category,unsupported,unsupported

coverage: coverage-build
	@mkdir -p $(BUILD_DIR)/coverage
	@cd $(BUILD_DIR) && lcov --directory . --zerocounters $(LCOV_IGNORE) >/dev/null 2>&1
	-cd $(BUILD_DIR) && ctest --output-on-failure --parallel
	@cd $(BUILD_DIR) && lcov --directory . --capture --output-file coverage/coverage.info $(LCOV_IGNORE) >/dev/null 2>&1
	@cd $(BUILD_DIR) && lcov --extract coverage/coverage.info '$(CURDIR)/src/*' --output-file coverage/coverage_filtered.info $(LCOV_IGNORE) >/dev/null 2>&1
	@cd $(BUILD_DIR) && genhtml coverage/coverage_filtered.info --output-directory coverage/html $(LCOV_IGNORE) >/dev/null 2>&1
	@echo ""
	@echo "Coverage report: $(BUILD_DIR)/coverage/html/index.html"
	@cd $(BUILD_DIR) && lcov --summary coverage/coverage_filtered.info $(LCOV_IGNORE) 2>&1 | grep -E 'lines|functions|source'

coverage-clean:
	find $(BUILD_DIR) -name '*.gcda' -delete 2>/dev/null || true
	rm -rf $(BUILD_DIR)/coverage
