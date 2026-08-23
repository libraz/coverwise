# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Generation is deterministic: the same model and seed produce the same suite.
Entries below call out the releases where that output changed, so a pinned
version can be upgraded knowingly.

## [1.5.0] - 2026-08-24

Every surface — CLI, WASM, JavaScript, pure TypeScript and Python — is held to
one input-acceptance contract and one error shape, and the paths that decide
them are now single implementations rather than per-surface copies. **Generated
suites are unchanged for valid models and seeds.**

### Added

- The documented input limits have one origin and are enforced identically on
  every surface.
- Type-level contract files pin what each JavaScript entry point exports and
  with what visibility, and the Python package ships a typing marker.
- A security policy.

### Fixed

- `LIKE` honours case sensitivity, the last value-matching operator that
  ignored it.
- Subnormal decimals parse identically on every platform and build backend.
- A zero strength returns an empty universe instead of dividing by zero, and an
  exhausted budget no longer reports a covered tuple as a constraint error.
- Every CLI subcommand exits the same way on a malformed constraint and names
  the expression that failed; `analyze` honours the model's strength and
  rejects a `--constraints` document that is not a constraint list rather than
  silently measuring an unconstrained universe.
- A row preserved from an existing suite keeps the text the caller supplied, so
  a warning names the value instead of an internal sentinel.
- The JavaScript entry points raise a structured error for `null` or
  `undefined` in a values array, and quote an equality value so one matching a
  parameter name stays a value.
- The Python API returns the report the executable wrote instead of discarding
  it, raises `TypeError` for a scalar where a value list belongs, and reports a
  crashed executable as a process failure rather than a model error.
- Source maps pointing outside the published tarball are no longer shipped.

### Changed

- **C++ API (breaking):** `CheckedBinomial` takes a `util::BinomialLimit`
  instead of a `uint64_t` budget, bounding it to the range over which the C++
  and TypeScript engines reach the same verdict. A literal budget needs a `u`
  suffix.
- `stats` reads the same model document as `generate`, so a `seeds` array
  `generate` rejects no longer passes the preflight.
- A row that drifts from the model is preserved with a warning naming the
  offending value, instead of being dropped — **`extend` output can differ from
  1.4.0 for suites containing such rows.**
- `estimatedTests` is documented as a coarse sizing heuristic rather than an
  upper bound. No reported value changed.
- Generation and coverage validation are substantially faster on large and
  constrained models: pass state is reused, a parameter is scored in one batch,
  and a tuple already witnessed by a valid test skips the feasibility search.
- The published benchmark table, CLI output blocks and constraint examples are
  regenerated from the shipping binary and pinned by tests. Per-configuration
  run times are no longer published, since nothing in the repository can
  re-derive them.

## [1.4.0] - 2026-07-25

A Python binding that ships the native CLI, plus stdin input for the CLI itself.

### Added

- Python binding distributing the native CLI, with a model API and pytest
  integration.
- CLI reads JSON input from standard input.
- Documentation for the Python API, pytest integration and stdin input.

### Fixed

- Decimal parsing no longer depends on libc++ floating-point `from_chars`.
- The Python wheel's platform tag matches what it actually ships.

### Changed

- Binding sources reorganized under `src/binding/`.
- The release pipeline builds and tests the Python wheel.

## [1.3.1] - 2026-07-24

### Added

- Negative coverage is reported, with tuple indices and boundary typing
  documented.

### Fixed

- Full coverage is guaranteed and the constraint search is bounded — **generated
  suites can differ from 1.3.0 for models with constraints.**
- Validation hardened across the core, model, validator and JS surfaces.

### Changed

- The workflow diagram is a hand-authored SVG rather than Mermaid.
- API reference expanded and a documentation index added.

## [1.3.0] - 2026-07-15

### Added

- Constraint solver.
- Options validation.
- Installable C++ package.

### Changed

- Actions bumped to versions defaulting to the Node 24 runtime.

## [1.2.0] - 2026-06-26

Boundary expansion, equivalence classes and value aliases reach every surface —
C++, WASM, JS and CLI — and the surfaces agree with each other.

### Added

- Boundary expansion, equivalence classes and value aliases reachable from the
  CLI, WASM and JS entry points, not only the core.
- Typed errors unified with input validation across the CLI and WASM.

### Fixed

- Cross-surface inconsistencies in the WASM bindings, CLI, parser and validator.
- The CLI exit-code contract and coverage-oracle parity are enforced.

### Changed

- RNG, seed domain and string grammar unified across surfaces — **a given seed
  can produce a different suite than it did in 1.1.1.**
- Number-to-string conversion canonicalized across all surfaces.

## [1.1.1] - 2026-05-09

### Added

- Seed validation and stricter input checks.

### Fixed

- Invalid-flag handling behaves the same on every surface.

## [1.1.0] - 2026-04-07

### Added

- `analyzeCoverage` takes an optional constraints parameter.

## [1.0.2] - 2026-04-02

### Fixed

- The constraint parser accepts quoted strings, and UTF-8 is handled correctly
  in the parser and the WASM bindings.

## [1.0.1] - 2026-03-25

### Added

- t-wise strength support.

### Fixed

- Constraint handling and coverage engine corrections.

## [1.0.0] - 2026-03-25

First release.

### Added

- Combinatorial test generation engine in C++17: pairwise and t-wise coverage,
  constraints, sub-models for mixed strength, and negative testing.
- Boundary value expansion and equivalence class coverage.
- Value aliasing, weight-based priority and preview statistics.
- WASM bindings with a TypeScript wrapper, and a pure TypeScript engine
  mirroring the C++ architecture.
- Bilingual documentation and the publish workflow.

[1.5.0]: https://github.com/libraz/coverwise/releases/tag/v1.5.0
[1.4.0]: https://github.com/libraz/coverwise/releases/tag/v1.4.0
[1.3.1]: https://github.com/libraz/coverwise/releases/tag/v1.3.1
[1.3.0]: https://github.com/libraz/coverwise/releases/tag/v1.3.0
[1.2.0]: https://github.com/libraz/coverwise/releases/tag/v1.2.0
[1.1.1]: https://github.com/libraz/coverwise/releases/tag/v1.1.1
[1.1.0]: https://github.com/libraz/coverwise/releases/tag/v1.1.0
[1.0.2]: https://github.com/libraz/coverwise/releases/tag/v1.0.2
[1.0.1]: https://github.com/libraz/coverwise/releases/tag/v1.0.1
[1.0.0]: https://github.com/libraz/coverwise/releases/tag/v1.0.0
