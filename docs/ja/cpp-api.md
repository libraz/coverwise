# C++ API

すべての公開 API は `coverwise` 名前空間にあります。`coverwise.h` をインクルードすると全 API が利用可能です。

```cpp
#include "coverwise.h"
```

## 生成

### `coverwise::core::Generate`

最小カバリング配列を生成します。

```cpp
namespace coverwise::core {
  model::GenerateResult Generate(const model::GenerateOptions& options);
}
```

### `coverwise::core::Extend`

既存テストスイートを拡張してカバレッジを改善します。

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

生成を実行せずにモデル統計をプレビューします。

```cpp
namespace coverwise::core {
  model::ModelStats EstimateModel(const model::GenerateOptions& options);
}
```

## バリデーション

### `coverwise::validator::ValidateCoverage`

テストスイートの t-wise カバレッジを検証します。ジェネレータとは独立しています。

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

テストスイートの制約違反をチェックします。

```cpp
namespace coverwise::validator {
  ConstraintReport ValidateConstraints(
    const std::vector<model::TestCase>& tests,
    const std::vector<model::Constraint>& constraints
  );
}
```

## データ型

### `model::GenerateOptions`

```cpp
struct GenerateOptions {
  std::vector<Parameter> parameters;
  std::vector<std::string> constraint_expressions;
  uint32_t strength = 2;
  uint64_t seed = 0;                // 有効範囲: 0〜2^32 - 1。
  uint32_t max_tests = 0;           // 0 = 無制限。
  std::vector<TestCase> seeds;       // 保持する既存テストケース。
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

  // 無効値（ネガティブテスト用）。
  void set_invalid(std::vector<bool> invalid);
  const std::vector<bool>& invalid() const;
  bool is_invalid(uint32_t index) const;
  uint32_t valid_count() const;
  uint32_t invalid_count() const;

  // エイリアス。
  void set_aliases(std::vector<std::vector<std::string>> aliases);
  const std::vector<std::string>& aliases(uint32_t index) const;
  const std::vector<std::vector<std::string>>& all_aliases() const;
  bool has_aliases() const;
  const std::string& display_name(uint32_t index, uint32_t rotation) const;

  // 同値クラス。
  void set_equivalence_classes(std::vector<std::string> classes);
  const std::string& equivalence_class(uint32_t index) const;
  const std::vector<std::string>& equivalence_classes() const;
  bool has_equivalence_classes() const;
  std::vector<std::string> unique_classes() const;

  // 検索。
  uint32_t find_value_index(
    const std::string& name,
    bool case_sensitive = true
  ) const;  // 見つからない場合はUINT32_MAX。
};
```

### `model::TestCase`

```cpp
struct TestCase {
  std::vector<uint32_t> values;  // パラメータごとの値インデックス。UINT32_MAX = 未割当。
};
```

### `model::GenerateResult`

```cpp
struct GenerateResult {
  std::vector<Parameter> parameters;  // boundary展開後の有効な値空間。
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

struct UncoveredTuple {
  std::vector<std::string> tuple;
  std::vector<std::string> params;
  std::vector<std::pair<uint32_t, uint32_t>> indices;
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

## 使用例

```cpp
#include "coverwise.h"
#include <iostream>

int main() {
  using namespace coverwise;

  // パラメータを構築。
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

  // 生成。
  auto result = core::Generate(opts);

  if (!result.error.ok()) {
    std::cerr << result.error.message << "\n";
    return 1;
  }

  std::cout << "テスト数: " << result.tests.size() << "\n";
  std::cout << "カバレッジ: " << result.coverage * 100 << "%\n";

  // テストケースを出力。
  for (const auto& tc : result.tests) {
    for (size_t i = 0; i < result.parameters.size(); ++i) {
      std::cout << result.parameters[i].name << "="
                << result.parameters[i].values[tc.values[i]] << " ";
    }
    std::cout << "\n";
  }

  // 独立して検証。
  std::vector<model::Constraint> constraints;
  for (const auto& expression : opts.constraint_expressions) {
    auto parsed = model::ParseConstraint(expression, result.parameters);
    if (!parsed.error.ok()) return 1;
    constraints.push_back(std::move(parsed.constraint));
  }
  auto report = validator::ValidateCoverage(
    result.parameters, result.tests, opts.strength, constraints);
  std::cout << "検証結果: " << report.coverage_ratio * 100 << "%\n";

  return report.error.ok() && report.coverage_ratio == 1.0 ? 0 : 1;
}
```

## ビルド統合

### CMake

```cmake
add_subdirectory(coverwise)
target_link_libraries(your_target PRIVATE coverwise::coverwise)
```

インストール済みpackageを利用する場合：

```cmake
find_package(coverwise 1.2 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE coverwise::coverwise)
```

### コンパイラ要件

- C++17 以降
- GCC 9+、Clang 10+、AppleClang 14+ で動作確認済み
