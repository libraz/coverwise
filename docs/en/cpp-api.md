# C++ API

All public APIs are in the `coverwise` namespace. Include `coverwise.h` for the full API.

```cpp
#include "coverwise.h"
```

## Generation

`Generate`, `Extend` and `EstimateModel` put their options through the same
acceptance gate the CLI and the WebAssembly binding use. A model the C++ entry
points accept is one every other surface accepts, and a model any of them
rejects is rejected here with the same error — including the boundary expansion
that runs before acceptance, so a boundary parameter is judged by the values it
expands to rather than by the range it was written as. Embedding the library
does not put you on a looser contract than calling `coverwise` from a shell.

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

### `coverwise::core::EstimateModel`

Preview model statistics without generation.

```cpp
namespace coverwise::core {
  model::ModelStats EstimateModel(const model::GenerateOptions& options);
}
```

## Validation

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

Read `error` before any count. On every non-ok exit `coverage_ratio` is left at
its `0.0` default, so a zero ratio means "not measured" as often as it means
"nothing covered"; the two are told apart only by `error`. The other fields
depend on how far the call got:

| Exit | `total_tuples`, `covered_tuples`, `uncovered`, `uncovered_count` | `invalid_tests` |
|------|------------------------------------------------------------------|-----------------|
| Strength outside `[1, parameter count]` | Zero | Empty |
| Invalid parameters | Zero | Empty |
| Unsatisfiable constraint model | Zero | Empty |
| Enumeration limit exceeded | Zero | Empty |
| Feasibility budget exhausted part-way through enumeration | Partial counts for the tuples already enumerated | Complete |

The last row is the one to guard against: the call stops in the middle of the
tuple loop, so the counts are real but describe a prefix of the universe rather
than all of it.

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

## Data Types

### `model::ParseConstraint` and `model::ParseOptions`

Parse the constraint DSL into a `Constraint` AST for use with validation. Name
resolution is case-insensitive by default; set `case_sensitive` to require exact
parameter and value names.

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

`Constraint` is an owning pointer to a `ConstraintNode`; it is move-only. The
parser supports `IF`/`THEN`/`ELSE`, `IMPLIES`, `AND`, `OR`, `NOT`, `IN`, `LIKE`,
`=`, `!=`, numeric comparisons, and parameter-to-parameter comparisons.

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
    bool case_sensitive = true
  ) const;  // UINT32_MAX when not found.
};

bool HasInvalidValues(const std::vector<Parameter>& parameters);
Error ValidateParameters(const std::vector<Parameter>& parameters);
```

The `invalid`, alias, and equivalence-class vectors are per-value metadata:
when present, each must have the same length as `values`. `find_value_index`
also searches aliases. `ValidateParameters` rejects empty or duplicate parameter
names, parameter names that differ only by ASCII case, parameters with no values,
duplicate values within a parameter, values or aliases of one parameter that are
ambiguous once ASCII case is folded, and malformed metadata. The two case-folding
rules follow from case-insensitive lookup: `find_value_index` with
`case_sensitive = false` and the constraint parser must each have a single answer
for a given name.

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

Boundary expansion merges, de-duplicates, and numerically sorts the original
values with `min-1`, `min`, `min+1`, `max-1`, `max`, and `max+1` for integers;
for floats it uses `step` in place of `1`. Generation returns the expanded
parameters, so render `TestCase` value indices against `GenerateResult::parameters`.

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

`tests` contains only positive rows; `negative_tests` contains rows with exactly
one invalid value. When invalid values are configured, `negative_coverage`
counts feasible single-fault tuples. A `max_tests` limit applies to the combined
positive and negative suite, so negative coverage can be incomplete and the
result records `omitted_tuples` and warnings. `coverage` is the minimum ratio
across the global and any sub-model engines; it is the pass/fail metric when
sub-models are in use. `warnings` carries non-fatal diagnostics, while `error`
is non-OK only for an early failure.

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

`total_tuples` is a raw global-plus-sub-model upper bound before constraint
exclusion. `estimated_tests` is a coarse sizing heuristic derived from the
largest value count, the strength and the parameter count, capped at
`total_tuples`; it is not a bound in either direction, and a generated suite may
be smaller or larger. `EstimateModel` validates constraints and references before
returning the estimate.

### `validator::CoverageReport`

```cpp
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
```

### `validator::ClassCoverageReport`

```cpp
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
```

For either value or class coverage, a tuple containing an invalid value is
excluded. With constraints, a partial tuple is excluded only when it has no
completion to a full assignment of valid values satisfying every constraint;
it is not enough to test whether the partial tuple itself currently evaluates
to false. This lets interacting constraints correctly remove unreachable tuples.

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

It prints `Tests:` with the suite size, `Coverage:` with the ratio generation
reports, then one line per test case as `name=value` pairs, and finally
`Validated:` with the ratio the independent validator measures for the same
suite. This is the program a CMake fixture compiles against the installed
package, so the text above is the text that builds.

## Build Integration

### CMake

```cmake
add_subdirectory(coverwise)
target_link_libraries(your_target PRIVATE coverwise::coverwise)
```

For an installed package:

```cmake
find_package(coverwise 1.2 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE coverwise::coverwise)
```

### Compiler Requirements

- C++17 or later
- A standard library with floating-point `std::to_chars`, which is what sets the
  floor: GCC 11+ (libstdc++ 11+), Clang 10+ built against libstdc++ 11+ or
  libc++ 14+, AppleClang 14+ with a macOS 13.3 or newer deployment target

Number formatting takes its shortest round-trip digits from
`std::to_chars(double)`, and there is no fallback that reproduces them, so an
older standard library fails to build rather than producing different output. An
Apple toolchain aimed below macOS 13.3 is rejected with a diagnostic naming the
deployment target.
