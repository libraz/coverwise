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
    uint32_t strength
  );
}
```

### `coverwise::validator::ValidateConstraints`

テストスイートの制約違反をチェックします。

```cpp
namespace coverwise::validator {
  ConstraintReport ValidateConstraints(
    const std::vector<model::Parameter>& parameters,
    const std::vector<model::TestCase>& tests,
    const std::vector<std::shared_ptr<model::ConstraintNode>>& constraints
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
  uint32_t seed = 0;
  uint32_t max_tests = 0;           // 0 = 無制限。
  std::vector<TestCase> seeds;       // 保持する既存テストケース。
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
  size_t size() const;                     // 値の数。
  const std::string& value(size_t i) const;

  // 無効値（ネガティブテスト用）。
  void set_invalid(size_t index, bool invalid);
  bool is_invalid(size_t index) const;
  size_t valid_count() const;
  size_t invalid_count() const;

  // エイリアス。
  void set_aliases(size_t index, const std::vector<std::string>& aliases);
  const std::vector<std::string>& aliases(size_t index) const;
  bool has_aliases() const;
  std::string display_name(size_t index, size_t rotation = 0) const;

  // 同値クラス。
  void set_equivalence_class(size_t index, const std::string& cls);
  const std::string& equivalence_class(size_t index) const;
  bool has_equivalence_classes() const;
  std::vector<std::string> unique_classes() const;

  // 検索。
  std::optional<size_t> find_value_index(
    const std::string& name,
    bool case_sensitive = true
  ) const;
};
```

### `model::TestCase`

```cpp
struct TestCase {
  std::vector<uint32_t> values;  // パラメータごとの値インデックス。UINT32_MAX = 未割当。

  static TestCase WithSize(size_t n);  // 未割当テストケースを作成。
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

  std::cout << "テスト数: " << result.tests.size() << "\n";
  std::cout << "カバレッジ: " << result.coverage * 100 << "%\n";

  // テストケースを出力。
  for (const auto& tc : result.tests) {
    for (size_t i = 0; i < opts.parameters.size(); ++i) {
      std::cout << opts.parameters[i].name() << "="
                << opts.parameters[i].value(tc.values[i]) << " ";
    }
    std::cout << "\n";
  }

  // 独立して検証。
  auto report = validator::ValidateCoverage(opts.parameters, result.tests, 2);
  std::cout << "検証結果: " << report.coverage_ratio * 100 << "%\n";

  return 0;
}
```

## ビルド統合

### CMake

```cmake
add_subdirectory(coverwise)
target_link_libraries(your_target PRIVATE coverwise_lib)
```

### コンパイラ要件

- C++17 以降
- GCC 9+、Clang 10+、AppleClang 14+ で動作確認済み
