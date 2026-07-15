# C++ API

All public APIs are in the `coverwise` namespace. Include `coverwise.h` for the full API.

```cpp
#include "coverwise.h"
```

## Generation

### `coverwise::core::Generate`

Generate a minimal covering array.

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
```

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

struct UncoveredTuple {
  std::vector<std::string> tuple;
  std::vector<std::string> params;
  std::string reason;
  std::string ToString() const;
};
```

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

## Example

```cpp
#include "coverwise.h"
#include <iostream>

int main() {
  using namespace coverwise;

  // Build parameters.
  model::GenerateOptions opts;
  opts.parameters = {
    {"os",      {"Windows", "macOS", "Linux"}},
    {"browser", {"Chrome", "Firefox", "Safari"}},
    {"theme",   {"light", "dark"}},
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
      std::cout << result.parameters[i].name << "="
                << result.parameters[i].values[tc.values[i]] << " ";
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
  auto report = validator::ValidateCoverage(
    result.parameters, result.tests, opts.strength, constraints);
  std::cout << "Validated: " << report.coverage_ratio * 100 << "%\n";

  return report.error.ok() && report.coverage_ratio == 1.0 ? 0 : 1;
}
```

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
- Tested with GCC 9+, Clang 10+, AppleClang 14+
