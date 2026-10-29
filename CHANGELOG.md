# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Generation is deterministic: the same model and seed produce the same suite.
Entries below call out the releases where that output changed, so a pinned
version can be upgraded knowingly.

## [1.6.2] - 2026-10-30

The engine, the WASM binding, the pure TypeScript port, the CLI and the Python
package now behave as the documentation describes in the places where they had
drifted from it. **Negative rows are chosen by coverage gain, so
`negativeTests` from `generate` and `extend` differ from 1.6.1 for models with
invalid values, and are usually shorter at the same single-fault coverage.
Positive suites are unchanged for valid models and seeds.**

### Added

- `model/options_validation.h` is installed with the other public headers, and
  `core::Extend` takes a defaulted `model::ChargedText` so a caller that already
  counted the row text against the string budget does not have it counted twice.

### Changed

- **C++ API (breaking):** `TestCase::unresolved` is
  `std::vector<std::optional<std::string>>`. `std::nullopt` marks a position the
  row carried no member for, and an empty string is a member the caller wrote as
  `""`. A `GenerateOptions::seed` above 2^32-1 is rejected instead of being
  truncated to its low 32 bits.
- A `LIKE` pattern without a wildcard resolves exactly like `=`: aliases and
  ASCII case are resolved, and a value the parameter does not declare is a
  `CONSTRAINT_ERROR` instead of a test that never matches.
- A string containing an unpaired UTF-16 surrogate is rejected as invalid input
  on every surface instead of being passed through.
- `extend` takes its existing rows and seeds in one step under a single
  100,000-row limit. A preserved row with exactly one invalid value that
  otherwise satisfies the constraints counts toward negative coverage and is not
  generated again, and under boundary expansion a preserved index that names no
  declared value is returned unassigned.
- The JavaScript surface charges numeric and boolean parameter values against
  the string budget as their rendered text, drops the undocumented per-row field
  cap, and accepts any `strength` from 1 to 4294967295 that the model allows.
- Python: `coverwise.parametrize` passes each value to the test function in its
  declared type (`int`, `bool`, `float`) rather than as a string, and keeps an
  explicitly declared alias spelling as written. Both launchers exit `127` with
  a diagnostic when the native executable cannot be started.
- Float boundary values are snapped to the precision of `min`, `max` and `step`
  before de-duplication.
- Seed remapping after boundary expansion resolves each declared value once,
  `FirstUncovered` decodes without per-call allocations, and the JavaScript
  surface builds value resolvers only when there are rows to convert.

### Fixed

- Coverage validation applies the 100,000-row limit, keeps `omitted_uncovered`
  when its search budget runs out, and counts its aggregate budget in 64 bits.
  Class coverage enforces the same row limit as value coverage.
- A weights key spelled as the declared value is accepted wherever it sorts
  among that value's aliases, and the pure TypeScript port reads an empty
  `aliases` or `equivalenceClasses` list as metadata, as the core does.
- `allOf` and `anyOf` build their constraint trees directly, reading each custom
  condition once instead of calling its own `and()` or `or()`.
- `Rng.uniformInt` in the pure TypeScript port handles an interval 2^32 wide.
- `DynamicBitset::NumBlocks` no longer overflows for sizes near `UINT32_MAX`.
- The CLI accepts subnormal numbers and FIFO inputs, prints parse diagnostics as
  printable text, and stops keeping rows past the row limit while it reads a
  document. Its optional unsigned fields reject non-finite numbers.
- Python: `analyze_coverage` sends `strength` through the model as `generate`
  does.

## [1.6.1] - 2026-10-15

Seeds, sub-models and generation options are now accepted the same way on every
surface, and estimating a model no longer builds coverage engines. Generated
suites are unchanged for valid models and seeds.

### Fixed

- A seed whose value index lies past a parameter's declared values is refused
  even when boundary expansion adds enough values for the index to land on a
  generated one.
- The compiled WASM module refuses a sub-model without `strength`, as the
  published types, the JavaScript wrapper and the CLI already do, instead of
  defaulting it.
- The pure TypeScript surface detects boundary parameters by own `type`, `range`
  and `step` fields, as the validator and the WASM binding do, so an inherited
  field no longer expands boundary values on that surface alone.
- The pure TypeScript option validation charges weight names against the string
  budget in the order the core walks them, so an oversized key listed first no
  longer masks the aggregate refusal, and an all-empty alias list reads the same
  during validation as during generation.

### Changed

- `estimateModel` counts tuples without building coverage engines, and a run
  whose global exclusion sweep has spent its search budget fails without
  sweeping each sub-model first.
- The feasibility solver completes assignments in place and shares one solve
  order and frame stack across the satisfiability check, completion and
  suggestions; the CLI resolves a row's members through a name index built once
  per row.
- The workflow toolchain matches the repository pins: Node 22.23.3 and Yarn
  4.18.1.

## [1.6.0] - 2026-09-04

Nothing a caller supplies can leave an entry point as anything but a documented
error, and the rules that decide acceptance — ASCII case folding, value-name
resolution, the byte budgets and the declared limits — have one definition each
instead of a copy per surface. **A row cell or a weights key written in another
ASCII case now names the value it means, so `analyze` and `extend` output can
differ from 1.5.0 for suites carrying such text. Generated suites are otherwise
unchanged for valid models and seeds.**

### Added

- `compareUtf8` orders caller-supplied map keys by code point in the pure
  TypeScript engine, so it refuses a model over the same key the native surface
  would.
- A `preflight` target runs every gate CI runs, including a sanitizer build kept
  separate from the ordinary one.
- The documentation gains a four-page primer, a use-case track starting from
  data a project already holds, and standalone pages for choosing a surface,
  determinism, the declared input limits, performance and the questions the
  reference pages previously answered only in passing — with hand-authored
  figures, in English and Japanese alike.

### Fixed

- A getter, a `Proxy` trap or a computed member that throws while the WASM
  binding reads a caller's model is caught where it runs and reported as
  `INVALID_INPUT`; it would otherwise unwind out through the WebAssembly frames
  past every destructor on the stack.
- The JavaScript entry points convert whatever they throw into a
  `CoverwiseError`, and a `values` element that is not a scalar is refused
  rather than synthesised into `"undefined"`, `"null"` or `"[object Object]"`.
- A class tuple decided on its last representative is reported as decided rather
  than undecided; an undecided verdict dropped the tuple out of the coverage
  universe and hid that no test covered it.
- A suite that covers a class tuple is no longer discarded for a search budget
  it never needed, and representative enumeration is bounded by one aggregate so
  the loop cannot outlast it.
- A tuple mask that does not describe the model excludes every tuple instead of
  returning with nothing filtered, which left a caller holding a tuple set it
  believed had been filtered.
- Boundary expansion runs before the acceptance rules and propagates its failure
  instead of discarding it, and an unsupported `extend` mode is refused instead
  of silently keeping nothing.
- The CLI checks every write to standard output, so a failed write is no longer
  reported as success; a reader that closes the pipe ends the run on the
  documented invalid-input code with a diagnostic rather than on a signal.
  Requested usage goes to standard output and diagnosed usage to standard error.
- A parameter name that cannot be written as one bare token is refused rather
  than emitted in a comparison that reads back differently.
- The Python binding refuses a `set` or `frozenset` wherever a value list
  belongs, accepts a dict view, reports a non-finite number through the
  documented error type before the subprocess starts, describes an executable
  that cannot be run the same way at every entry point, and hands the caller the
  whole diagnostic instead of its first line.
- The pure TypeScript engine reports a tuple count past 2^53 exactly and
  saturates at the same ceiling the core does, instead of rounding and then
  growing without bound.
- A decimal outside double's range parses to the same value on either C++
  parsing backend. The backend used where `std::from_chars` is unavailable for
  floating point read an overflow as the largest representable double and an
  underflow to zero as a parsed zero, rather than as the infinity and the
  rejected literal the other backend produces; both are now decided from the
  decimal itself.
- A boundary parameter whose metadata length disagrees with its value list is
  refused in the order the C++ layer checks; the CLI never reports an unmapped
  failure as success; a negative-coverage warning is raised only when a
  shortfall exists.

### Changed

- **C++ install set (breaking for embedders):** the install prefix carries
  exactly the transitive include closure of `coverwise.h`, the surface the C++
  API reference documents. Internal headers and the tuning budgets are no longer
  installed and are verified unreachable from a real prefix. Install rules run
  only when coverwise is the top-level build, so an embedding parent's prefix
  stays untouched.
- A weights key, a constraint operand and a row cell all resolve a value through
  one entry point per language and fold ASCII case the same way. Two weights
  keys naming one value are refused unless one of them is the spelling the model
  declares, since with neither declared the winner came down to map walk order.
- The byte budgets charge each string once, over exactly the documented set: a
  call now states whether a reader already counted the caller's row values, so a
  model the command line accepts is no longer refused by the library, and text
  under a key naming no parameter is charged rather than ignored.
- The tuple, combination, diagnostic and search-node budgets, and the boundary
  acceptance thresholds and their wording, have one definition per language that
  every surface reads.
- A constraint value renders through one function that lets the position decide
  the quoting, so a number or a boolean on the right of `=` is quoted like any
  other value and cannot read back as a parameter reference.
- Generation and validation do less work per decision: `IN` membership is
  precomputed so evaluation costs the same as every other atom, feasibility
  search draws on one caller-owned frame stack instead of allocating per tuple,
  and overlap diagnostics answer from the engine's own enumeration rather than
  materialising every uncovered tuple.
- The npm tarball ships one readme and declares its subpath types for node10
  resolution, and the browser snippet points at a CDN that serves the package
  verbatim.
- Every published limit, command and example is derived from the shipping binary
  or the constants the code still declares, and a document leaves coverage only
  through an entry that states why.
- The workflow toolchain matches the repository pins: Node 22.23.2, Yarn 4.18.0
  and Python 3.14 wherever a job builds or tests a binding.

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

[1.6.2]: https://github.com/libraz/coverwise/releases/tag/v1.6.2
[1.6.1]: https://github.com/libraz/coverwise/releases/tag/v1.6.1
[1.6.0]: https://github.com/libraz/coverwise/releases/tag/v1.6.0
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
