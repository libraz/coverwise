# C++ API

すべての公開 API は `coverwise` 名前空間にあります。`coverwise.h` をインクルードすると全 API が利用可能です。

```cpp
#include "coverwise.h"
```

## 生成

`Generate`・`Extend`・`EstimateModel` は、CLI と WebAssembly バインディングが使うのと
同一の受理ゲートに options を通します。C++ のエントリポイントが受理するモデルは他の全ての面
でも受理され、どれか一つが拒否するモデルはここでも同じエラーで拒否されます。受理の前に走る
境界展開も同じなので、境界パラメータは書かれた range ではなく展開後の値によって判定されます。
ライブラリを組み込んだからといって、シェルから `coverwise` を呼ぶ場合より緩い契約になることは
ありません。

ひとつだけ、宣言ではなく課金によって決まるものがあります。ライブラリでの実行とコマンドライン
での実行を比べる前に知っておく価値があります。文書化されたバイト予算が数えるのは、呼び出し側
が渡したテキストを、どこで数えられたかによらず一度だけ、です。ゲートが課金するのは
`GenerateOptions` がテキストとして持つもの — パラメータ名と値、エイリアス、等価クラス名、
制約式、サブモデルのパラメータ名、そして重みのキーであるパラメータ名と値名 — です。行の位置が
持つテキストも課金しますが、options を組み立てる前に行を数え、そう申告した面がある場合は
除きます。コマンドラインと WebAssembly バインディングはまさにそれを行うので、同じ行が二重に
課金されることはありません。

C++ で行を組み立てるということは、値名ではなく値インデックスを与えるということであり、
インデックスはテキストではありません。したがって解決済みの行だけからなるスイートはここでは
費用がかかりませんが、同じスイートを JSON で書けば各行の文字列値はすべて課金されます。大きな
スイートがライブラリでは受理され、コマンドラインの `coverwise` では拒否されることがあるのは
このためです。両者が異なるのはこの点だけです。行の `unresolved` に自分で入れたテキストは、
他の入力文字列と同じように課金されます。

### `coverwise::core::Generate`

指定したモデルのカバリングテストスイートを生成します。

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

どのカウントよりも先に `error` を読んでください。ok でない終了経路では `coverage_ratio` は
既定値の `0.0` のままです。したがって比率 0 は「何もカバーされていない」ことも「そもそも
測っていない」ことも意味し、両者を区別できるのは `error` だけです。他のフィールドは
呼び出しがどこまで進んだかで変わります。

| 終了経路 | `total_tuples`・`covered_tuples`・`uncovered`・`uncovered_count` | `invalid_tests` |
|----------|------------------------------------------------------------------|-----------------|
| strength が `[1, パラメータ数]` の外 | ゼロ | 空 |
| パラメータが不正 | ゼロ | 空 |
| 制約モデルが充足不能 | ゼロ | 空 |
| 列挙上限を超過 | ゼロ | 空 |
| 列挙の途中で実行可能性の予算が尽きた | そこまでに列挙したタプルぶんの部分カウント | 完全 |

注意が要るのは最後の行です。タプルのループの途中で停止するため、カウントは実数ですが、
対象範囲の全体ではなくその先頭部分だけを表しています。

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

### `model::ParseConstraint` と `model::ParseOptions`

検証で使うため、制約 DSL を `Constraint` AST にパースします。名前解決はデフォルトで
大文字小文字を区別しません。完全一致を必須にするには `case_sensitive` を設定します。

```cpp
struct ParseResult {
  Constraint constraint;  // パース失敗時は nullptr。
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

`Constraint` は `ConstraintNode` を所有するムーブ専用ポインタです。パーサーは
`IF`/`THEN`/`ELSE`、`IMPLIES`、`AND`、`OR`、`NOT`、`IN`、`LIKE`、`=`、`!=`、数値比較、
パラメータ間比較をサポートします。

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

  Parameter() = default;
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
  bool has_invalid_values() const;

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
    bool case_sensitive
  ) const;  // 見つからない場合はUINT32_MAX。
};

uint32_t ResolveValueName(
  const Parameter& parameter,
  const std::string& name
);  // 見つからない場合はUINT32_MAX。

bool HasInvalidValues(const std::vector<Parameter>& parameters);
Error ValidateParameters(const std::vector<Parameter>& parameters);
```

`invalid`、エイリアス、同値クラスのベクタは値ごとのメタデータです。存在する場合は
`values` と同じ長さでなければなりません。どちらの検索もエイリアスを対象に含みます。

`ResolveValueName` は、呼び出し側が書いた値名 — シード、`tests` や `existing` の行、
重みのキー、制約式のオペランド — を解決する入口です。ASCII の大小文字を畳み込むため、
どの経路から届いた名前も同じ規則で解決されます。`find_value_index` はその下位の
プリミティブで、照合方針の既定値を持ちません。バイト一致が必要な呼び出し側は明示的に
指定します。

`ValidateParameters` は空または重複したパラメータ名、ASCII の大小文字だけが異なる
パラメータ名、値を持たないパラメータ、同一パラメータ内の重複値、ASCII の大小文字を
畳み込むと曖昧になる値・エイリアス、不正なメタデータを拒否します。大小文字に関する
2 つの規則は、大文字小文字を区別しない解決に由来します。`ResolveValueName` と
制約パーサーは、いずれもある名前に対して一意の答えを返せなければなりません。

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

境界値展開は、元の値に対して整数では `min-1`、`min`、`min+1`、`max-1`、`max`、`max+1`
を、浮動小数点では `1` の代わりに `step` を使って追加し、重複を除去して数値順に並べます。
生成結果は展開後のパラメータを返すため、`TestCase` の値インデックスは
`GenerateResult::parameters` を基準に表示してください。

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

`tests` にはポジティブな行だけが入り、`negative_tests` には無効値をちょうど1つ含む行が
入ります。無効値が設定されている場合、`negative_coverage` は実行可能な単一障害タプルを
数えます。`max_tests` はポジティブとネガティブを合わせたスイートに適用されるため、ネガ
ティブカバレッジは未完了になることがあります。その場合は `omitted_tuples` と警告を確認
してください。サブモデルがある場合、`coverage` はグローバルおよび各サブモデルエンジン
の最小比率であり、合否判定に使う値です。`warnings` は致命的でない診断、`error` は早期
失敗時だけ非 OK になります。

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

`total_tuples` は制約除外前の、グローバルとサブモデルを合わせた生の上限です。
`estimated_tests` は、最大値数・強度・パラメータ数から求めて `total_tuples` で
頭打ちにした、大まかな見積もりです。上限でも下限でもなく、生成されるスイートは
これを下回ることも、上回ることもあります。`EstimateModel` は推定を返す前に制約と
参照を検証します。

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

値レベルでもクラスレベルでも、無効値を含むタプルは除外されます。制約がある場合、部分
タプルが除外されるのは、すべての制約を満たす有効値の完全な割り当てへ補完できない場合
だけです。部分タプル単体の評価が `false` かどうかだけでは判定しません。これにより、
相互に作用する制約でも到達不能なタプルを正しく除外できます。

## 使用例

```cpp
#include <coverwise.h>

#include <iostream>
#include <utility>
#include <vector>

int main() {
  using namespace coverwise;

  // パラメータを構築。
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

  // 生成。
  auto result = core::Generate(opts);
  if (!result.error.ok()) {
    std::cerr << result.error.message << "\n";
    return 1;
  }

  std::cout << "Tests: " << result.tests.size() << "\n";
  std::cout << "Coverage: " << result.coverage * 100 << "%\n";

  // テストケースを出力。
  for (const auto& tc : result.tests) {
    for (size_t i = 0; i < result.parameters.size(); ++i) {
      std::cout << result.parameters[i].name << "=" << result.parameters[i].values[tc.values[i]]
                << " ";
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
  auto report =
      validator::ValidateCoverage(result.parameters, result.tests, opts.strength, constraints);
  std::cout << "Validated: " << report.coverage_ratio * 100 << "%\n";
  return report.error.ok() && report.coverage_ratio == 1.0 ? 0 : 1;
}
```

このプログラムは、`Tests:` にスイートの件数、`Coverage:` に生成が報告するカバレッジ比を出力し、
続いて各テストケースを `名前=値` の組で 1 行ずつ、最後に `Validated:` として同じスイートを
独立したバリデータが測ったカバレッジ比を出力します。これは CMake フィクスチャがインストール済み
パッケージに対してコンパイルするプログラムであり、上記のテキストがそのままビルドできます。

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
- 浮動小数点版 `std::to_chars` を持つ標準ライブラリ。これが下限を決めます。
  GCC 11+（libstdc++ 11+）、libstdc++ 11+ または libc++ 14+ に対してビルドした
  Clang 10+、macOS 13.3 以降をデプロイターゲットとする AppleClang 14+

数値の書式化は往復可能な最短桁を `std::to_chars(double)` から得ており、それを再現できる
代替手段はありません。したがって古い標準ライブラリでは、出力が変わるのではなくビルドが
失敗します。macOS 13.3 より古いデプロイターゲットを指す Apple のツールチェインは、
デプロイターゲットを名指しする診断とともに拒否されます。
