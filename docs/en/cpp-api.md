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
    uint32_t strength
  );
}
```

### `coverwise::validator::ValidateConstraints`

Check constraint violations in a test suite.

```cpp
namespace coverwise::validator {
  ConstraintReport ValidateConstraints(
    const std::vector<model::Parameter>& parameters,
    const std::vector<model::TestCase>& tests,
    const std::vector<std::shared_ptr<model::ConstraintNode>>& constraints
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
  uint32_t seed = 0;
  uint32_t max_tests = 0;           // 0 = no limit.
  std::vector<TestCase> seeds;       // Existing test cases to keep.
  std::vector<SubModel> sub_models;
  WeightConfig weights;
  std::vector<BoundaryConfig> boundary_configs;
};
```

### `model::Parameter`

```cpp
class Parameter {
public:
  Parameter(const std::string& name, const std::vector<std::string>& values);

  const std::string& name() const;
  size_t size() const;                     // Number of values.
  const std::string& value(size_t i) const;

  // Invalid values (for negative testing).
  void set_invalid(size_t index, bool invalid);
  bool is_invalid(size_t index) const;
  size_t valid_count() const;
  size_t invalid_count() const;

  // Aliases.
  void set_aliases(size_t index, const std::vector<std::string>& aliases);
  const std::vector<std::string>& aliases(size_t index) const;
  bool has_aliases() const;
  std::string display_name(size_t index, size_t rotation = 0) const;

  // Equivalence classes.
  void set_equivalence_class(size_t index, const std::string& cls);
  const std::string& equivalence_class(size_t index) const;
  bool has_equivalence_classes() const;
  std::vector<std::string> unique_classes() const;

  // Lookup.
  std::optional<size_t> find_value_index(
    const std::string& name,
    bool case_sensitive = true
  ) const;
};
```

### `model::TestCase`

```cpp
struct TestCase {
  std::vector<uint32_t> values;  // One index per parameter. UINT32_MAX = unassigned.

  static TestCase WithSize(size_t n);  // Create unassigned test case.
};
```

### `model::GenerateResult`

```cpp
struct GenerateResult {
  std::vector<TestCase> tests;
  std::vector<TestCase> negative_tests;
  double coverage = 0.0;
  std::vector<UncoveredTuple> uncovered;
  GenerateStats stats;
  std::vector<Suggestion> suggestions;
  std::vector<std::string> warnings;
  uint32_t strength = 2;
  std::optional<ClassCoverage> class_coverage;
};

struct GenerateStats {
  size_t total_tuples = 0;
  size_t covered_tuples = 0;
  size_t test_count = 0;
};

struct UncoveredTuple {
  std::vector<std::string> tuple;
  std::vector<std::string> params;
  std::string reason;
  std::string display;
};
```

### `model::SubModel`

```cpp
struct SubModel {
  std::vector<std::string> parameter_names;
  uint32_t strength = 0;
};
```

### `model::WeightConfig`

```cpp
struct WeightConfig {
  struct Entry {
    std::string parameter_name;
    std::vector<std::pair<std::string, double>> value_weights;
  };
  std::vector<Entry> entries;
};
```

### `validator::CoverageReport`

```cpp
struct CoverageReport {
  size_t total_tuples = 0;
  size_t covered_tuples = 0;
  double coverage_ratio = 0.0;
  std::vector<model::UncoveredTuple> uncovered;
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

  std::cout << "Tests: " << result.tests.size() << "\n";
  std::cout << "Coverage: " << result.coverage * 100 << "%\n";

  // Print test cases.
  for (const auto& tc : result.tests) {
    for (size_t i = 0; i < opts.parameters.size(); ++i) {
      std::cout << opts.parameters[i].name() << "="
                << opts.parameters[i].value(tc.values[i]) << " ";
    }
    std::cout << "\n";
  }

  // Validate independently.
  auto report = validator::ValidateCoverage(opts.parameters, result.tests, 2);
  std::cout << "Validated: " << report.coverage_ratio * 100 << "%\n";

  return 0;
}
```

## Build Integration

### CMake

```cmake
add_subdirectory(coverwise)
target_link_libraries(your_target PRIVATE coverwise_lib)
```

### Compiler Requirements

- C++17 or later
- Tested with GCC 9+, Clang 10+, AppleClang 14+
