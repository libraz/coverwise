# C++ API

Reference for embedding the coverwise engine in a C++ program — linking the library, building a model in memory, and reading a result back without going through JSON, a process boundary or a scripting runtime. It is for a reader calling the engine from C++, and it assumes the vocabulary of [Tuples and coverage](primer/tuples-and-coverage.md), [Strength](primer/strength.md) and [Constraints and the required universe](primer/constraints-and-the-universe.md) rather than re-explaining it. [Getting started](getting-started.md) builds and runs a first program.

All public APIs are in the `coverwise` namespace. Include `coverwise.h` for the full API.

```cpp
#include "coverwise.h"
```

## The public surface

`coverwise.h` includes thirteen headers, and those thirteen are the public surface. A build check holds the installed header set equal to the include closure of `coverwise.h`, so a type reachable from that umbrella header is one the package ships, and a header outside the list — the bitset representation, the coverage engine, the strategy layer, the options-acceptance helpers — is internal and is not installed.

| Header | What it declares |
|---|---|
| `coverwise.h` | Umbrella header; includes the rest |
| `core/generator.h` | `Generate`, `Extend`, `EstimateModel` |
| `model/generate_options.h` | `GenerateOptions`, `SubModel`, `WeightConfig`, `ExtendMode`, `ModelStats` |
| `model/parameter.h` | `Parameter`, `ResolveValueName`, `HasInvalidValues`, `ValidateParameters` |
| `model/test_case.h` | `TestCase`, `GenerateResult`, `GenerateStats`, `UncoveredTuple`, `NegativeCoverage`, `ClassCoverage`, `Suggestion` |
| `model/boundary.h` | `BoundaryConfig`, `ExpandBoundaryValues` |
| `model/constraint_parser.h` | `ParseResult`, `ParseOptions`, `ParseConstraint`, `AnnotateConstraintError` |
| `model/constraint_ast.h` | `ConstraintResult`, `ConstraintNode`, `Constraint`, the node types, `RelOp`, the numeric value cache |
| `model/error.h` | `Error` and `Error::Code` |
| `model/limits.h` | The seven declared input limits |
| `util/string_util.h` | Case folding, numeric parsing, JavaScript-compatible number formatting |
| `validator/coverage_validator.h` | `CoverageReport`, `ClassCoverageReport`, `ValidateCoverage`, `ComputeClassCoverage`, `AnnotateClassCoverage` |
| `validator/constraint_validator.h` | `ConstraintReport`, `ValidateConstraints` |

## Errors

### `model::Error`

Every entry point reports failure through one structured error rather than by throwing, so a caller reads a field instead of catching.

```cpp
namespace coverwise::model {
  struct Error {
    enum class Code {
      kOk = 0,
      kConstraintError = 1,
      kInsufficientCoverage = 2,
      kInvalidInput = 3,
      kTupleExplosion = 4,
    };

    Code code = Code::kOk;
    std::string message;
    std::string detail;

    bool ok() const;
  };
}
```

`ok` is `code == Code::kOk` and is the check to write; comparing `code` against a specific value is for branching on the kind of failure, not for detecting one.

| Code | What it means |
|---|---|
| `kOk` | No failure. Every other field of the result is meaningful. |
| `kConstraintError` | A constraint expression did not parse, or the constraint set has no satisfying assignment. |
| `kInsufficientCoverage` | Generation stopped short of full coverage, usually because `max_tests` bound it. |
| `kInvalidInput` | The model was rejected by the acceptance gate — a malformed parameter, an out-of-domain option, an exceeded limit. |
| `kTupleExplosion` | The required tuple set is too large to enumerate for the requested strength and model. |

`message` is a single sentence naming the failure. `detail` is optional context — the offending value, the two numbers that disagree, the position inside a constraint expression that `AnnotateConstraintError` marked. It is empty when there is nothing further to say, so a caller that renders both joins them only when `detail` is non-empty.

The numeric values of `Code` are not process exit codes and must not be used as such. The command-line surface maps them: constraint errors exit 1, insufficient coverage exits 2, and both invalid input and tuple explosion exit 3. See the [CLI reference](cli.md) for the exit-code contract in full.

## Generation

`Generate`, `Extend` and `EstimateModel` put their options through the same acceptance gate the CLI and the WebAssembly binding use. A model the C++ entry points accept is one every other surface accepts, and a model any of them rejects is rejected here with the same error — including the boundary expansion that runs before acceptance, so a boundary parameter is judged by the values it expands to rather than by the range it was written as. Embedding the library is not a looser contract than calling `coverwise` from a shell.

One thing is charged rather than declared, and it is what separates a library run from a command-line one. The documented byte budgets count the text a caller supplied, once, wherever it was counted. The gate charges what a `GenerateOptions` carries as text — parameter names and values, aliases, equivalence classes, constraint expressions, sub-model parameter names, and the parameter and value names a weight is keyed by. It charges the text of a row position as well, unless a surface counted the rows before the options were built and said so; the command line and the WebAssembly binding do exactly that, which is why the same row is never charged twice.

Building rows in C++ means supplying value indices rather than value names, and an index is not text. A suite of resolved rows therefore costs nothing here, while the same suite written as JSON is charged for every string value in every row — so a large suite can be accepted by the library and refused by `coverwise` on the command line. That is the only respect in which the two differ. Text placed in a row's `unresolved` entries is charged like any other input string.

### `coverwise::core::Generate`

Generate a covering test suite for the requested model.

```cpp
namespace coverwise::core {
  model::GenerateResult Generate(const model::GenerateOptions& options);
}
```

### `coverwise::core::Extend`

Extend an existing test suite to improve coverage.

```cpp
namespace coverwise::core {
  model::GenerateResult Extend(
    const std::vector<model::TestCase>& existing,
    const model::GenerateOptions& options,
    model::ExtendMode mode = model::ExtendMode::kStrict
  );
}
```

The `existing` rows are seeded ahead of generation and kept verbatim; new rows are appended until coverage is complete. A `max_tests` smaller than `existing.size()` is rejected as invalid input rather than silently dropping rows the caller supplied.

### `coverwise::core::EstimateModel`

Preview model statistics without generation.

```cpp
namespace coverwise::core {
  model::ModelStats EstimateModel(const model::GenerateOptions& options);
}
```

## Validation

The validator layer is independent of the generator. It enumerates tuples itself rather than reading anything the generator built, which is what makes it usable as ground truth for a suite of any origin, including one another tool wrote.

### `coverwise::validator::ValidateCoverage`

Validate t-wise coverage of a test suite. Independent of the generator.

```cpp
namespace coverwise::validator {
  CoverageReport ValidateCoverage(
    const std::vector<model::Parameter>& parameters,
    const std::vector<model::TestCase>& tests,
    uint32_t strength,
    const std::vector<model::Constraint>& constraints = {}
  );
}
```

```cpp
namespace coverwise::validator {
  struct CoverageReport {
    struct InvalidTest { uint32_t test_index; std::string reason; };
    uint64_t total_tuples = 0;
    uint64_t covered_tuples = 0;
    double coverage_ratio = 0.0;
    std::vector<model::UncoveredTuple> uncovered;
    uint64_t uncovered_count = 0;
    uint64_t omitted_uncovered = 0;
    std::vector<InvalidTest> invalid_tests;
    model::Error error;
  };
}
```

Read `error` before any count. On every non-ok exit `coverage_ratio` is left at its `0.0` default, so a zero ratio means "not measured" as often as it means "nothing covered"; the two are told apart only by `error`. The other fields depend on how far the call got:

| Exit | `total_tuples`, `covered_tuples`, `uncovered`, `uncovered_count` | `invalid_tests` |
|------|------------------------------------------------------------------|-----------------|
| Strength outside `[1, parameter count]` | Zero | Empty |
| Invalid parameters | Zero | Empty |
| Unsatisfiable constraint model | Zero | Empty |
| Enumeration limit exceeded | Zero | Empty |
| Feasibility budget exhausted part-way through enumeration | Partial counts for the tuples already enumerated | Complete |

The last row is the one to guard against: the call stops in the middle of the tuple loop, so the counts are real but describe a prefix of the universe rather than all of it.

`uncovered` may be shorter than `uncovered_count`, and `omitted_uncovered` records the difference. The two always account for each other: the listed tuples plus the omitted ones equal the count.

### `coverwise::validator::ValidateConstraints`

Check constraint violations in a test suite.

```cpp
namespace coverwise::validator {
  ConstraintReport ValidateConstraints(
    const std::vector<model::TestCase>& tests,
    const std::vector<model::Constraint>& constraints
  );
}
```

```cpp
namespace coverwise::validator {
  struct ConstraintReport {
    uint32_t total_tests = 0;
    uint32_t violations = 0;
    std::vector<uint32_t> violating_indices;
  };
}
```

`total_tests` is the number of rows examined, `violations` the number of rows that broke at least one constraint, and `violating_indices` their positions in the `tests` vector. The semantics are per test, not per constraint: a row is examined against every constraint and counted once, at the first one it violates, so `violations` never exceeds `total_tests` and `violating_indices` holds no duplicates. A row breaking three constraints and a row breaking one contribute equally.

This function exists for auditing a suite that came from somewhere else. No generation path calls it: generation constructs only constraint-satisfying rows, so a suite returned by `Generate` has not been through this check and does not need to be. It is also not reachable from the WebAssembly binding, which exports generation, coverage analysis, extension and estimation only — a constraint-violating row submitted to coverage analysis is reported by the coverage validator's own per-row check instead, and neither report is evidence about the other.

### `coverwise::validator::ComputeClassCoverage`

Measure coverage over equivalence classes rather than over individual values.

```cpp
namespace coverwise::validator {
  struct ClassCoverageReport {
    uint64_t total_class_tuples = 0;
    uint64_t covered_class_tuples = 0;
    double coverage_ratio = 0.0;
    model::Error error;
  };

  ClassCoverageReport ComputeClassCoverage(
    const std::vector<model::Parameter>& parameters,
    const std::vector<model::TestCase>& tests,
    uint32_t strength,
    const std::vector<model::Constraint>& constraints = {}
  );
}
```

For either value or class coverage, a tuple containing an invalid value is excluded. With constraints, a partial tuple is excluded only when it has no completion to a full assignment of valid values satisfying every constraint; it is not enough to test whether the partial tuple itself currently evaluates to false. This lets interacting constraints correctly remove unreachable tuples.

### `coverwise::validator::AnnotateClassCoverage`

Fill in a result's class-coverage fields in place.

```cpp
namespace coverwise::validator {
  void AnnotateClassCoverage(
    model::GenerateResult& result,
    const std::vector<model::Parameter>& params,
    uint32_t strength,
    const std::vector<model::Constraint>& constraints = {}
  );
}
```

It checks whether any parameter declares equivalence classes and, if one does, computes class coverage and sets `result.class_coverage`. When no parameter declares any, the optional is left empty and the result is unchanged. Use it to attach class coverage to a result you assembled yourself; `ComputeClassCoverage` is the same measurement as a standalone value.

## Constraints

### `model::ParseConstraint` and `model::ParseOptions`

Parse the constraint DSL into a `Constraint` AST for use with validation. Name resolution is case-insensitive by default; set `case_sensitive` to require exact parameter and value names.

```cpp
struct ParseResult {
  Constraint constraint;  // nullptr when parsing fails.
  Error error;
};

struct ParseOptions {
  bool case_sensitive = false;
};

ParseResult ParseConstraint(
  const std::string& expression,
  const std::vector<Parameter>& parameters,
  const ParseOptions& options = {}
);

Error AnnotateConstraintError(const std::string& expression, Error error);
```

The parser supports `IF`/`THEN`/`ELSE`, `IMPLIES`, `AND`, `OR`, `NOT`, `IN`, `LIKE`, `=`, `!=`, numeric comparisons, and parameter-to-parameter comparisons. [Constraint syntax](constraints.md) is the language definition; this page covers only the C++ types it produces.

`AnnotateConstraintError` takes the expression and the error a parse of it returned, and gives back the same error with the offending position marked in `detail`. It is what turns "unexpected token" into something a user can act on.

### `model::ConstraintNode` and `model::ConstraintResult`

A parsed constraint is an AST, and `Evaluate` on its root is the only public way to ask what that constraint says about an assignment.

```cpp
namespace coverwise::model {
  enum class ConstraintResult {
    kTrue,
    kFalse,
    kUnknown,
  };

  class ConstraintNode {
   public:
    virtual ~ConstraintNode() = default;
    virtual ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const = 0;
  };

  using Constraint = std::unique_ptr<ConstraintNode>;

  constexpr uint32_t kUnassigned = UINT32_MAX;
}
```

`assignment` holds one value index per parameter, positionally, in the same order as the `parameters` vector the constraint was parsed against. An entry of `kUnassigned` means that parameter has no value yet, which is what makes the assignment partial and the evaluation three-valued.

`kUnknown` is the third value. It means the expression reads a parameter the assignment has not fixed, so the constraint can be neither satisfied nor violated yet — not that the constraint is unsatisfiable, and not that it is vacuously true. A caller filtering candidate rows treats `kFalse` as a rejection and `kUnknown` as "still open", which is what lets a partially built row be pruned as soon as it is decidably wrong and no earlier. Evaluation short-circuits, so an `AND` whose left operand is `kFalse` is `kFalse` without reading the right one, and an `IF` whose condition is `kUnknown` is `kUnknown` unless both branches agree on the same answer.

`Constraint` is an owning pointer to a `ConstraintNode` and is move-only. A `std::vector<Constraint>` is therefore moved into a validator call, not copied, and destroying the vector destroys the ASTs.

### Node types

The concrete node types are public so an embedder can inspect a parsed AST. Evaluation goes through the base class; the derived types add readers, not behaviour.

```cpp
namespace coverwise::model {
  class EqualsNode;          // param_index(), value_index()
  class NotEqualsNode;       // param_index(), value_index()
  class AndNode;
  class OrNode;
  class NotNode;
  class ImpliesNode;
  class IfThenElseNode;
  class RelationalNode;
  class InNode;
  class LikeNode;
  class ParamEqualsNode;
  class ParamNotEqualsNode;

  enum class RelOp { kLess, kLessEqual, kGreater, kGreaterEqual };

  struct NumericValueCache {
    std::vector<double> numeric;
    std::vector<uint8_t> valid;
  };

  using NumericValueCachePtr = std::shared_ptr<const NumericValueCache>;

  NumericValueCachePtr BuildNumericValueCache(const std::vector<std::string>& values);
}
```

`RelOp` names the four numeric comparisons a `RelationalNode` carries; `=` and `!=` are separate node types because they compare strings rather than numbers. A `RelationalNode` compares a parameter against a numeric literal or against another parameter, and a value that does not parse as a number compares `kFalse` rather than raising.

`NumericValueCache` is the parsed form of one parameter's value strings: `numeric[i]` is the value at index `i` as a double and `valid[i]` records whether it was numeric at all. `BuildNumericValueCache` builds one, and the cached constructors of `RelationalNode` take it, so a model with many relational constraints over the same parameter parses each value string once instead of once per node. `NumericValueCachePtr` is a shared pointer to a const cache, so sharing one across nodes is safe and copying it is cheap.

## Data types

### `model::GenerateOptions`

```cpp
struct GenerateOptions {
  std::vector<Parameter> parameters;
  std::vector<std::string> constraint_expressions;
  uint32_t strength = 2;
  uint64_t seed = 0;                // Valid domain: 0 through 2^32 - 1.
  uint32_t max_tests = 0;           // 0 = no limit.
  std::vector<TestCase> seeds;       // Existing test cases to keep.
  std::vector<SubModel> sub_models;
  WeightConfig weights;
  std::map<std::string, BoundaryConfig> boundary_configs;
};
```

`seed` is declared as `uint64_t` but its domain is 0 through 2^32 - 1, and the acceptance gate enforces that: a larger value is rejected as invalid input with a message naming the domain, rather than being truncated to its low 32 bits. The wider declared type is there so a surface that reads a seed from JSON can carry an out-of-range number as far as the gate and get one rejection, instead of wrapping it into a different valid seed and generating a suite nobody asked for. Within the domain, the same seed and the same accepted model produce the same suite on every surface; see [Determinism](determinism.md).

`strength` must be between 1 and the parameter count. `max_tests` bounds the combined positive and negative suite; a bound that stops generation short leaves `error` at `kInsufficientCoverage`.

### `model::ExtendMode`

```cpp
namespace coverwise::model {
  enum class ExtendMode : uint32_t {
    kStrict,  // Keep existing tests exactly as-is.
  };
}
```

`kStrict` is the only mode today, and it is the default argument of `Extend`. The enumeration exists so that a future mode is a new enumerator rather than a new overload.

The underlying type is fixed, which makes a value outside the enumerator list a representable value rather than undefined behaviour, so `static_cast<ExtendMode>(7)` is a legal thing for a caller to form — a binding forwarding an integer from another language does exactly that. `Extend` switches on the mode explicitly and answers such a value with an invalid-input error naming the mode, instead of falling through to strict behaviour the caller did not ask for.

### `model::Parameter`

```cpp
struct Parameter {
  std::string name;
  std::vector<std::string> values;

  Parameter() = default;
  Parameter(std::string name, std::vector<std::string> values);
  Parameter(std::string name, std::vector<std::string> values,
            std::vector<bool> invalid);

  uint32_t size() const;

  // Invalid values (for negative testing).
  void set_invalid(std::vector<bool> invalid);
  const std::vector<bool>& invalid() const;
  bool is_invalid(uint32_t index) const;
  uint32_t valid_count() const;
  uint32_t invalid_count() const;
  bool has_invalid_values() const;

  // Aliases.
  void set_aliases(std::vector<std::vector<std::string>> aliases);
  const std::vector<std::string>& aliases(uint32_t index) const;
  const std::vector<std::vector<std::string>>& all_aliases() const;
  bool has_aliases() const;
  const std::string& display_name(uint32_t index, uint32_t rotation) const;

  // Equivalence classes.
  void set_equivalence_classes(std::vector<std::string> classes);
  const std::string& equivalence_class(uint32_t index) const;
  const std::vector<std::string>& equivalence_classes() const;
  bool has_equivalence_classes() const;
  std::vector<std::string> unique_classes() const;

  // Lookup.
  uint32_t find_value_index(
    const std::string& name,
    bool case_sensitive
  ) const;  // UINT32_MAX when not found.
};

uint32_t ResolveValueName(
  const Parameter& parameter,
  const std::string& name
);  // UINT32_MAX when not found.

bool HasInvalidValues(const std::vector<Parameter>& parameters);
Error ValidateParameters(const std::vector<Parameter>& parameters);
```

The `invalid`, alias, and equivalence-class vectors are per-value metadata: when present, each must have the same length as `values`. Both lookups also search aliases.

`ResolveValueName` is the entry point for a value name a caller wrote — a seed, a `tests` or `existing` row, a weights key, a constraint operand. It folds ASCII case, so a name is resolved the same way whichever of those it arrived through. `find_value_index` is the primitive underneath it and takes no default match policy, so a caller that wants byte equality has to ask for it.

`ValidateParameters` rejects empty or duplicate parameter names, parameter names that differ only by ASCII case, parameters with no values, duplicate values within a parameter, values or aliases of one parameter that are ambiguous once ASCII case is folded, and malformed metadata. The two case-folding rules follow from case-insensitive lookup: `ResolveValueName` and the constraint parser must each have a single answer for a given name.

### `model::BoundaryConfig`

```cpp
struct BoundaryConfig {
  enum class Type { kInteger, kFloat };
  Type type = Type::kInteger;
  double min_value = 0;
  double max_value = 0;
  double step = 1.0;
};

Parameter ExpandBoundaryValues(const Parameter&, const BoundaryConfig&);
Parameter ExpandBoundaryValues(const Parameter&, const BoundaryConfig&, Error* error);
```

Boundary expansion merges, de-duplicates, and numerically sorts the original values with `min-1`, `min`, `min+1`, `max-1`, `max`, and `max+1` for integers; for floats it uses `step` in place of `1`. Generation returns the expanded parameters, so render `TestCase` value indices against `GenerateResult::parameters`.

### `model::TestCase`

```cpp
struct TestCase {
  std::vector<uint32_t> values;  // One index per parameter. UINT32_MAX = unassigned.
};
```

### `model::GenerateResult`

```cpp
struct GenerateResult {
  std::vector<Parameter> parameters;  // Effective values after boundary expansion.
  std::vector<TestCase> tests;
  std::vector<TestCase> negative_tests;
  std::optional<NegativeCoverage> negative_coverage;
  double coverage = 0.0;
  std::vector<UncoveredTuple> uncovered;
  uint64_t uncovered_count = 0;
  uint64_t omitted_uncovered = 0;
  GenerateStats stats;
  std::vector<Suggestion> suggestions;
  std::vector<std::string> warnings;
  std::optional<ClassCoverage> class_coverage;
  Error error;
};

struct GenerateStats {
  uint64_t total_tuples = 0;
  uint64_t covered_tuples = 0;
  uint32_t test_count = 0;
};

struct NegativeCoverage {
  uint64_t total_tuples = 0;
  uint64_t covered_tuples = 0;
  uint64_t omitted_tuples = 0;
  double coverage_ratio = 1.0;
};

struct ClassCoverage {
  uint64_t total_class_tuples = 0;
  uint64_t covered_class_tuples = 0;
  double class_coverage_ratio = 0.0;
};

struct Suggestion {
  std::string description;
  TestCase test_case;
};

struct UncoveredTuple {
  std::vector<std::string> tuple;
  std::vector<std::string> params;
  std::vector<std::pair<uint32_t, uint32_t>> indices;
  std::string reason;
  std::string ToString() const;
};
```

`tests` contains only positive rows; `negative_tests` contains rows with exactly one invalid value. When invalid values are configured, `negative_coverage` counts feasible single-fault tuples. A `max_tests` limit applies to the combined positive and negative suite, so negative coverage can be incomplete and the result records `omitted_tuples` and warnings. `coverage` is the minimum ratio across the global and any sub-model engines; it is the pass/fail metric when sub-models are in use. `warnings` carries non-fatal diagnostics, while `error` is non-OK only for an early failure.

### `model::SubModel`

```cpp
struct SubModel {
  std::vector<std::string> parameter_names;
  uint32_t strength = 2;
};
```

### `model::WeightConfig`

```cpp
struct WeightConfig {
  std::map<std::string, std::map<std::string, double>> entries;
  double GetWeight(const std::string& parameter, const std::string& value) const;
  bool empty() const;
};
```

### `model::ModelStats`

```cpp
struct ModelStats {
  uint32_t parameter_count = 0;
  uint32_t total_values = 0;
  uint32_t strength = 0;
  uint64_t total_tuples = 0;
  uint32_t estimated_tests = 0;
  uint32_t sub_model_count = 0;
  uint32_t constraint_count = 0;
  struct ParamDetail {
    std::string name;
    uint32_t value_count = 0;
    uint32_t invalid_count = 0;
  };
  std::vector<ParamDetail> parameters;
  Error error;
};
```

`total_tuples` is a raw global-plus-sub-model upper bound before constraint exclusion. `estimated_tests` is a coarse sizing heuristic derived from the largest value count, the strength and the parameter count, capped at `total_tuples`; it is not a bound in either direction, and a generated suite may be smaller or larger. `EstimateModel` validates constraints and references before returning the estimate.

## Input limits

The limits the acceptance gate applies are declared as public constants in `model/limits.h`, which `coverwise.h` includes. Each is an `inline constexpr size_t` in the `coverwise::model` namespace, so an embedder reads them instead of hardcoding the numbers, and a program that sizes a buffer or pre-checks a model against them stays correct if a limit ever moves.

| Constant | What it bounds |
|---|---|
| `kMaxParameters` | Parameters one model may declare |
| `kMaxValuesPerParameter` | Values one parameter may declare |
| `kMaxTests` | Rows in a `tests`, `seeds` or `existing` array |
| `kMaxConstraints` | Constraint expressions in one model |
| `kMaxStringBytes` | UTF-8 bytes in any single input string |
| `kMaxAggregateStringBytes` | UTF-8 bytes across all of one input's strings |
| `kMaxDocumentBytes` | Raw bytes of one JSON document a surface reads |

The values, what a caller sees when each is exceeded, and which of them binds first are in [Input limits](limits.md). `kMaxDocumentBytes` is a memory guard on reading a document rather than part of the acceptance contract, so a program that builds a `GenerateOptions` in memory never meets it.

## String utilities

```cpp
namespace coverwise::util {
  std::string FoldAsciiString(const std::string& value);
  bool CaseInsensitiveEqual(const std::string& a, const std::string& b);
  bool IsNumeric(const std::string& s);
  double ToDouble(const std::string& s);
  bool TryParseFiniteDouble(const std::string& s, double* out);
  std::string JsNumberToString(double value);
}
```

`FoldAsciiString` is the one place the case fold is defined, and every case-insensitive decision on every surface goes through it or through `CaseInsensitiveEqual`, which shares its byte range. Only ASCII letters are affected; every other byte, including every byte of a multi-byte UTF-8 sequence, is copied verbatim, which is safe precisely because no such byte falls in the ASCII letter range.

`IsNumeric` reports whether a string parses as a double. `ToDouble` parses one and is undefined on input `IsNumeric` rejects. `TryParseFiniteDouble` validates first and is the entry point for arbitrary text: subnormals are representable and accepted, while a decimal that overflows to infinity or underflows to zero is rejected.

`JsNumberToString` is the shared algorithm that keeps numeric output byte-identical across the CLI, the WebAssembly build, the npm package and the pure-TypeScript port. It produces the shortest decimal string that round-trips to the same double, following the ECMAScript Number-to-String algorithm, which is what `String(value)` produces in JavaScript. An embedder that renders a numeric parameter value with `std::to_string` or an `ostream` will not match the other surfaces; calling this function will. It formats `3.14` as `3.14`, `1.0/3.0` as `0.3333333333333333`, `-0.0` as `0`, `100.0` as `100`, `1e-7` as `1e-7` and `1e21` as `1e+21`.

## Example

```cpp
#include <coverwise.h>

#include <iostream>
#include <utility>
#include <vector>

int main() {
  using namespace coverwise;

  // Build parameters.
  model::GenerateOptions opts;
  opts.parameters = {
      {"os", {"Windows", "macOS", "Linux"}},
      {"browser", {"Chrome", "Firefox", "Safari"}},
      {"theme", {"light", "dark"}},
  };
  opts.constraint_expressions = {
      "IF os = Windows THEN browser != Safari",
  };
  opts.strength = 2;
  opts.seed = 42;

  // Generate.
  auto result = core::Generate(opts);
  if (!result.error.ok()) {
    std::cerr << result.error.message << "\n";
    return 1;
  }

  std::cout << "Tests: " << result.tests.size() << "\n";
  std::cout << "Coverage: " << result.coverage * 100 << "%\n";

  // Print test cases.
  for (const auto& tc : result.tests) {
    for (size_t i = 0; i < result.parameters.size(); ++i) {
      std::cout << result.parameters[i].name << "=" << result.parameters[i].values[tc.values[i]]
                << " ";
    }
    std::cout << "\n";
  }

  // Validate independently.
  std::vector<model::Constraint> constraints;
  for (const auto& expression : opts.constraint_expressions) {
    auto parsed = model::ParseConstraint(expression, result.parameters);
    if (!parsed.error.ok()) return 1;
    constraints.push_back(std::move(parsed.constraint));
  }
  auto report =
      validator::ValidateCoverage(result.parameters, result.tests, opts.strength, constraints);
  std::cout << "Validated: " << report.coverage_ratio * 100 << "%\n";
  return report.error.ok() && report.coverage_ratio == 1.0 ? 0 : 1;
}
```

It prints `Tests:` with the suite size, `Coverage:` with the ratio generation reports, then one line per test case as `name=value` pairs, and finally `Validated:` with the ratio the independent validator measures for the same suite. This is the program a CMake fixture compiles against the installed package, so the text above is the text that builds.

## Build integration

### A released build

The Linux x64 archive attached to each GitHub release is a complete install tree, not the command-line binary on its own. It is produced by installing the project to a staging prefix, so it carries the static library, all thirteen public headers under `include/coverwise/`, the CMake package files under `lib/cmake/coverwise/`, the `coverwise` executable and the license. Unpacking it and pointing `CMAKE_PREFIX_PATH` at the unpacked directory is the shortest path to a working `find_package`.

```bash
tar -xzf coverwise-<version>-linux-x64.tar.gz
cmake -S . -B build -DCMAKE_PREFIX_PATH="$PWD/coverwise-<version>-linux-x64"
```

Other platforms build from source; the project installs the same tree from `cmake --install`.

### CMake

```cmake
add_subdirectory(coverwise)
target_link_libraries(your_target PRIVATE coverwise::coverwise)
```

For an installed package:

```cmake
find_package(coverwise CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE coverwise::coverwise)
```

The generated config is `SameMajorVersion`-compatible, so a version request is satisfied by any installed release sharing its major version. Requesting no version accepts whatever is installed, which is what a project tracking the current major line wants.

Installation is enabled only for a top-level, non-WebAssembly build. A parent project that embeds coverwise through `add_subdirectory` or `FetchContent` links the target directly and installs nothing of coverwise's own.

### Compiler requirements

- C++17 or later
- A standard library with floating-point `std::to_chars`, which is what sets the floor: GCC 11+ (libstdc++ 11+), Clang 10+ built against libstdc++ 11+ or libc++ 14+, AppleClang 14+ with a macOS 13.3 or newer deployment target

Number formatting takes its shortest round-trip digits from `std::to_chars(double)`, and there is no fallback that reproduces them, so an older standard library fails to build rather than producing different output. An Apple toolchain aimed below macOS 13.3 is rejected with a diagnostic naming the deployment target.

## Where to go next

- [Input limits](limits.md) — the values behind the constants above, and what a caller sees at each
- [Constraint syntax](constraints.md) — the language `ParseConstraint` accepts
- [Choosing a surface](choosing-a-surface.md) — when embedding the C++ library is the right call and when it is not
- [Determinism](determinism.md) — what the seed guarantees, and across which surfaces
- [CLI reference](cli.md) — the same engine behind a command, including the exit-code contract
- [Glossary](glossary.md) — the vocabulary this page uses without defining
