# C++ API

coverwise のエンジンを C++ プログラムに組み込むためのリファレンスです。ライブラリをリンクし、モデルをメモリ上で組み立て、JSON もプロセス境界もスクリプトランタイムも介さずに結果を読み取る用途を扱います。C++ からエンジンを呼ぶ読者に向けたページで、語彙は[タプルとカバレッジ](primer/tuples-and-coverage.md)、[強度](primer/strength.md)、[制約と必要タプル集合](primer/constraints-and-the-universe.md)のものを前提とし、ここでは再説明しません。最初のプログラムのビルドと実行は[はじめかた](getting-started.md)にあります。

すべての公開 API は `coverwise` 名前空間にあります。`coverwise.h` をインクルードすると全 API が利用可能です。

```cpp
#include "coverwise.h"
```

## 公開サーフェス

`coverwise.h` は 13 本のヘッダをインクルードし、その 13 本が公開サーフェスそのものです。インストールされるヘッダ集合が `coverwise.h` の include 閉包と一致することはビルド時に検査されます。つまりこの傘ヘッダから到達できる型はパッケージが提供する型であり、一覧にないヘッダ（ビットセット表現、カバレッジエンジン、ストラテジ層、options 受理のヘルパ）は内部実装であり、インストールもされません。

| ヘッダ | 宣言している内容 |
|---|---|
| `coverwise.h` | 傘ヘッダ。残り 12 本をインクルードします |
| `core/generator.h` | `Generate`、`Extend`、`EstimateModel` |
| `model/generate_options.h` | `GenerateOptions`、`SubModel`、`WeightConfig`、`ExtendMode`、`ModelStats` |
| `model/parameter.h` | `Parameter`、`ResolveValueName`、`HasInvalidValues`、`ValidateParameters` |
| `model/test_case.h` | `TestCase`、`GenerateResult`、`GenerateStats`、`UncoveredTuple`、`NegativeCoverage`、`ClassCoverage`、`Suggestion` |
| `model/boundary.h` | `BoundaryConfig`、`ExpandBoundaryValues` |
| `model/constraint_parser.h` | `ParseResult`、`ParseOptions`、`ParseConstraint`、`AnnotateConstraintError` |
| `model/constraint_ast.h` | `ConstraintResult`、`ConstraintNode`、`Constraint`、各ノード型、`RelOp`、数値キャッシュ |
| `model/error.h` | `Error` と `Error::Code` |
| `model/limits.h` | 宣言済みの入力上限 7 個 |
| `util/string_util.h` | 大小文字の畳み込み、数値パース、JavaScript 互換の数値書式化 |
| `validator/coverage_validator.h` | `CoverageReport`、`ClassCoverageReport`、`ValidateCoverage`、`ComputeClassCoverage`、`AnnotateClassCoverage` |
| `validator/constraint_validator.h` | `ConstraintReport`、`ValidateConstraints` |

## エラー

### `model::Error`

すべてのエントリポイントは、例外を投げるのではなく 1 つの構造化エラーで失敗を報告します。呼び出し側は例外を捕捉するのではなくフィールドを読みます。

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

`ok` は `code == Code::kOk` と等価で、失敗の有無はこれで判定します。`code` を特定の値と比較するのは失敗の種類で分岐するためであり、失敗の検出のためではありません。

| コード | 意味 |
|---|---|
| `kOk` | 失敗していません。結果の他のフィールドはすべて有効です。 |
| `kConstraintError` | 制約式がパースできなかったか、制約集合を満たす割り当てが存在しません。 |
| `kInsufficientCoverage` | 完全なカバレッジに届かずに生成が終了しました。多くは `max_tests` による制限が原因です。 |
| `kInvalidInput` | 受理ゲートがモデルを拒否しました。パラメータの不備、範囲外のオプション、上限の超過などです。 |
| `kTupleExplosion` | 指定した強度とモデルでは、必要タプル集合が列挙できないほど大きすぎます。 |

`message` は失敗を言い表す 1 文です。`detail` は補足情報で、問題のある値、食い違った 2 つの数値、`AnnotateConstraintError` が印を付けた制約式中の位置などが入ります。補足すべきことがなければ空になるため、両方を表示する呼び出し側は `detail` が空でないときだけ連結します。

`Code` の数値はプロセスの終了コードではなく、そのように使ってはいけません。コマンドラインサーフェスは終了コードへ写像します。制約エラーは 1、カバレッジ不足は 2、不正入力とタプル爆発はどちらも 3 です。終了コードの契約全体は[CLI リファレンス](cli.md)にあります。

## 生成

`Generate`・`Extend`・`EstimateModel` は、CLI と WebAssembly バインディングが使うのと同一の受理ゲートに options を通します。C++ のエントリポイントが受理するモデルは他のすべてのサーフェスでも受理され、どれか 1 つが拒否するモデルはここでも同じエラーで拒否されます。受理の前に走る境界展開も同じです。境界パラメータは書かれた range ではなく、展開後の値によって判定されます。ライブラリを組み込んだからといって、シェルから `coverwise` を呼ぶ場合より緩い契約になることはありません。

ひとつだけ、宣言ではなく計上によって決まるものがあります。ライブラリでの実行とコマンドラインでの実行を分けるのはこの点です。文書化されたバイト予算が数えるのは、呼び出し側が渡したテキストを、どこで数えられたかによらず一度だけ、です。ゲートが計上するのは `GenerateOptions` がテキストとして持つもの、すなわちパラメータ名と値、エイリアス、等価クラス名、制約式、サブモデルのパラメータ名、そして重みのキーであるパラメータ名と値名です。行の位置が持つテキストも計上しますが、options を組み立てる前に行を数え、そう申告したサーフェスがある場合は除きます。コマンドラインと WebAssembly バインディングはまさにそれを行うので、同じ行が二重に計上されることはありません。

C++ で行を組み立てるということは、値名ではなく値インデックスを与えるということであり、インデックスはテキストではありません。したがって解決済みの行だけからなるスイートはここでは予算を消費しませんが、同じスイートを JSON で書けば各行の文字列値はすべて計上されます。大きなスイートがライブラリでは受理され、コマンドラインの `coverwise` では拒否されることがあるのはこのためです。両者が異なるのはこの点だけです。行の `unresolved` に入れたテキストは、他の入力文字列と同じように計上されます。

### `coverwise::core::Generate`

指定したモデルのカバリング配列となるテストスイートを生成します。

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

`existing` の行は生成に先立ってシードとして投入され、そのままの形で保持されます。カバレッジが完全になるまで、新しい行が後ろに追加されます。`existing.size()` より小さい `max_tests` は、呼び出し側が与えた行を黙って捨てるのではなく、不正入力として拒否されます。

### `coverwise::core::EstimateModel`

生成を実行せずにモデル統計をプレビューします。

```cpp
namespace coverwise::core {
  model::ModelStats EstimateModel(const model::GenerateOptions& options);
}
```

## バリデーション

バリデータ層はジェネレータから独立しています。ジェネレータが構築したものを読むのではなく、自分でタプルを列挙します。これにより、どこから来たスイートでも真値として判定できます。coverwise が生成したもの、稼働中のシステムから記録したもの、別のツールが書いたもの、いずれも同じように扱えます。

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

どのカウントよりも先に `error` を読んでください。ok でない終了経路では `coverage_ratio` は既定値の `0.0` のままです。したがって比率 0 は「何もカバーされていない」ことも「そもそも測っていない」ことも意味し、両者を区別できるのは `error` だけです。他のフィールドは呼び出しがどこまで進んだかで変わります。

| 終了経路 | `total_tuples`・`covered_tuples`・`uncovered`・`uncovered_count` | `invalid_tests` |
|----------|------------------------------------------------------------------|-----------------|
| strength が `[1, パラメータ数]` の外 | ゼロ | 空 |
| パラメータが不正 | ゼロ | 空 |
| 制約モデルが充足不能 | ゼロ | 空 |
| 列挙上限を超過 | ゼロ | 空 |
| 列挙の途中で実行可能性の予算が尽きた | そこまでに列挙したタプルぶんの部分カウント | 完全 |

注意が要るのは最後の行です。タプルのループの途中で停止するため、カウントは実数ですが、対象範囲の全体ではなくその先頭部分だけを表しています。

`uncovered` は `uncovered_count` より短いことがあり、その差は `omitted_uncovered` に記録されます。両者は必ず辻褄が合います。列挙された未網羅タプル数と省略された数の合計が、カウントに一致します。

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

```cpp
namespace coverwise::validator {
  struct ConstraintReport {
    uint32_t total_tests = 0;
    uint32_t violations = 0;
    std::vector<uint32_t> violating_indices;
  };
}
```

`total_tests` は検査した行数、`violations` は 1 つ以上の制約に違反した行数、`violating_indices` はそれらの `tests` ベクタ上の位置です。意味論は制約単位ではなくテスト単位です。各行はすべての制約に照らして検査されますが、最初に違反した制約のところで 1 回だけ数えられます。したがって `violations` が `total_tests` を超えることはなく、`violating_indices` に重複は現れません。3 つの制約に違反した行と 1 つだけ違反した行の寄与は同じです。

この関数は、外部から来たテストスイートを監査するために存在します。生成経路のどこからも呼ばれません。生成は制約を満たす行だけを構築するため、`Generate` が返したスイートはこの検査を通っておらず、通す必要もありません。WebAssembly バインディングからも到達できません。バインディングが公開するのは生成・カバレッジ分析・拡張・見積もりだけです。制約に違反する行をカバレッジ分析に渡した場合は、カバレッジバリデータ自身の行ごとの検査によって報告されます。2 つの報告は互いの根拠にはなりません。

### `coverwise::validator::ComputeClassCoverage`

個々の値ではなく等価クラスを単位としてカバレッジを測定します。

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

値レベルでもクラスレベルでも、無効値を含むタプルは除外されます。制約がある場合、部分タプルが除外されるのは、すべての制約を満たす有効値の完全な割り当てへ補完できない場合だけです。部分タプル単体の評価が `false` かどうかだけでは判定しません。これにより、相互に作用する制約でも到達不能なタプルを正しく除外できます。

### `coverwise::validator::AnnotateClassCoverage`

結果のクラスカバレッジ関連フィールドをその場で埋めます。

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

いずれかのパラメータが等価クラスを宣言しているかを調べ、宣言していればクラスカバレッジを計算して `result.class_coverage` に設定します。どのパラメータも宣言していない場合は optional が空のままとなり、結果は変わりません。自分で組み立てた結果にクラスカバレッジを付与したいときに使います。同じ測定を単体の値として得たい場合は `ComputeClassCoverage` を使います。

## 制約

### `model::ParseConstraint` と `model::ParseOptions`

検証で使うため、制約 DSL を `Constraint` AST にパースします。名前解決はデフォルトで大文字小文字を区別しません。完全一致を必須にするには `case_sensitive` を設定します。

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

パーサーは `IF`/`THEN`/`ELSE`、`IMPLIES`、`AND`、`OR`、`NOT`、`IN`、`LIKE`、`=`、`!=`、数値比較、パラメータ間比較をサポートします。言語の定義は[制約構文](constraints.md)にあります。このページが扱うのは、それが生成する C++ の型だけです。

`AnnotateConstraintError` は、式とそのパースが返したエラーを受け取り、問題の位置を `detail` に印付けした同じエラーを返します。「予期しないトークン」を、利用者が対処できる情報に変えるのがこの関数です。

### `model::ConstraintNode` と `model::ConstraintResult`

パース済みの制約は AST であり、その根に対する `Evaluate` が、その制約が割り当てについて何を言っているかを問い合わせる唯一の公開手段です。

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

`assignment` はパラメータごとの値インデックスを 1 つずつ、制約をパースしたときの `parameters` ベクタと同じ順序で位置的に保持します。要素が `kUnassigned` であれば、そのパラメータにはまだ値が定まっていません。これが割り当てを部分的なものにし、評価を 3 値にしています。

3 番目の値である `kUnknown` が重要です。これは、式が読むパラメータのうち割り当てが確定していないものがあり、制約を満たしているとも違反しているとも判定できない、という意味です。制約が充足不能だという意味でも、無条件に真だという意味でもありません。候補行を絞り込む呼び出し側は、`kFalse` を却下、`kUnknown` を「まだ未確定」として扱います。これにより、構築途中の行を、確実に誤りだと判定できた時点で、しかしそれより早くはならないように枝刈りできます。評価は短絡します。左オペランドが `kFalse` の `AND` は右を読まずに `kFalse` となり、条件が `kUnknown` の `IF` は、両方の分岐が同じ答えで一致しない限り `kUnknown` になります。

`Constraint` は `ConstraintNode` を所有するムーブ専用ポインタです。したがって `std::vector<Constraint>` はバリデータ呼び出しへコピーではなくムーブで渡され、ベクタを破棄すると AST も破棄されます。

### ノード型

具象ノード型は公開されており、パース済みの AST を組み込み側が検査できます。評価は基底クラス経由で行い、派生型が追加するのは読み取り手段であって振る舞いではありません。

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

`RelOp` は `RelationalNode` が持つ 4 種類の数値比較を表します。`=` と `!=` が別のノード型なのは、これらが数値ではなく文字列を比較するためです。`RelationalNode` はパラメータを数値リテラルまたは別のパラメータと比較し、数値としてパースできない値は例外ではなく `kFalse` として比較されます。

`NumericValueCache` は、1 つのパラメータの値文字列をパース済みの形にしたものです。`numeric[i]` はインデックス `i` の値を double にしたもの、`valid[i]` はそれが数値だったかどうかを記録します。`BuildNumericValueCache` がこれを構築し、`RelationalNode` のキャッシュ受け取り版コンストラクタが受け取ります。これにより、同じパラメータに対する関係比較の制約が多数あるモデルでも、各値文字列のパースはノードごとではなく 1 回で済みます。`NumericValueCachePtr` は const なキャッシュを指す共有ポインタなので、複数のノードで共有しても安全で、コピーも安価です。

## データ型

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

`seed` の宣言型は `uint64_t` ですが、有効域は 0 から 2^32 - 1 までであり、受理ゲートがそれを強制します。これを超える値は下位 32 ビットに切り詰められるのではなく、有効域を明示したメッセージとともに不正入力として拒否されます。宣言型が広いのは、JSON から seed を読むサーフェスが範囲外の数値をゲートまでそのまま運び、拒否を 1 回だけ受け取れるようにするためです。そうしないと、値が別の有効な seed に折り返され、誰も要求していないスイートが生成されてしまいます。有効域の内側であれば、同じ seed と同じ受理済みモデルはどのサーフェスでも同じスイートを生成します。[決定性](determinism.md)を参照してください。

`strength` は 1 以上、パラメータ数以下でなければなりません。`max_tests` はポジティブとネガティブを合わせたスイートを制限します。この制限で生成が途中で止まった場合、`error` は `kInsufficientCoverage` になります。

### `model::ExtendMode`

```cpp
namespace coverwise::model {
  enum class ExtendMode : uint32_t {
    kStrict,  // Keep existing tests exactly as-is.
  };
}
```

現在のモードは `kStrict` のみで、これが `Extend` の既定引数です。将来モードを追加するときに、オーバーロードではなく列挙子の追加で済むように enum として定義されています。

基底型が固定されているのは意図的です。固定しておくことで、列挙子リストにない値が未定義動作ではなく表現可能な値になります。つまり `static_cast<ExtendMode>(7)` は呼び出し側が正当に作れる値であり、他言語から整数を転送するバインディングは実際にそれを行います。`Extend` はモードで明示的に分岐し、そうした値に対しては、呼び出し側が求めていない strict の挙動へ流れ込むのではなく、モードを名指しした不正入力エラーを返します。

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

`invalid`、エイリアス、等価クラスのベクタは値ごとのメタデータです。存在する場合は `values` と同じ長さでなければなりません。どちらの検索もエイリアスを対象に含みます。

`ResolveValueName` は、呼び出し側が書いた値名（シード、`tests` や `existing` の行、重みのキー、制約式のオペランド）を解決する入口です。ASCII の大小文字を畳み込むため、どの経路から届いた名前も同じ規則で解決されます。`find_value_index` はその下位のプリミティブで、照合方針の既定値を持ちません。バイト一致が必要な呼び出し側は明示的に指定します。

`ValidateParameters` は空または重複したパラメータ名、ASCII の大小文字だけが異なるパラメータ名、値を持たないパラメータ、同一パラメータ内の重複値、ASCII の大小文字を畳み込むと曖昧になる値・エイリアス、不正なメタデータを拒否します。大小文字に関する 2 つの規則は、大文字小文字を区別しない解決に由来します。`ResolveValueName` と制約パーサーは、いずれもある名前に対して一意の答えを返せなければなりません。

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

境界値展開は、元の値に対して整数では `min-1`、`min`、`min+1`、`max-1`、`max`、`max+1` を、浮動小数点では `1` の代わりに `step` を使って追加し、重複を除去して数値順に並べます。生成結果は展開後のパラメータを返すため、`TestCase` の値インデックスは `GenerateResult::parameters` を基準に表示してください。

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

`tests` にはポジティブな行だけが入り、`negative_tests` には無効値をちょうど 1 つ含む行が入ります。無効値が設定されている場合、`negative_coverage` は実行可能な単一障害タプルを数えます。`max_tests` はポジティブとネガティブを合わせたスイートに適用されるため、ネガティブカバレッジは未完了になることがあります。その場合は `omitted_tuples` と警告を確認してください。サブモデルがある場合、`coverage` はグローバルおよび各サブモデルエンジンの最小比率であり、合否判定に使う値です。`warnings` は致命的でない診断、`error` は早期失敗時だけ非 OK になります。

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

`total_tuples` は制約除外前の、グローバルとサブモデルを合わせた生の上限です。`estimated_tests` は、最大値数・強度・パラメータ数から求めて `total_tuples` で頭打ちにした、大まかな見積もりです。上限でも下限でもなく、生成されるスイートはこれを下回ることも、上回ることもあります。`EstimateModel` は推定を返す前に制約と参照を検証します。

## 入力上限

受理ゲートが適用する上限は、`coverwise.h` がインクルードする `model/limits.h` に公開定数として宣言されています。いずれも `coverwise::model` 名前空間の `inline constexpr size_t` です。組み込み側は数値をハードコードせずにこれらを読めますし、バッファのサイズを決めたりモデルを事前検査したりするプログラムは、上限が将来変わっても正しいままです。

| 定数 | 制限する対象 |
|---|---|
| `kMaxParameters` | 1 モデルが宣言できるパラメータ数 |
| `kMaxValuesPerParameter` | 1 パラメータが宣言できる値の個数 |
| `kMaxTests` | `tests`・`seeds`・`existing` 配列の行数 |
| `kMaxConstraints` | 1 モデルの制約式の個数 |
| `kMaxStringBytes` | 単一の入力文字列の UTF-8 バイト数 |
| `kMaxAggregateStringBytes` | 1 入力に含まれる全文字列の合計 UTF-8 バイト数 |
| `kMaxDocumentBytes` | サーフェスが読み込む 1 つの JSON ドキュメントの生バイト数 |

具体的な値、超過したときに呼び出し側に見えるもの、どれが先に効くかは[入力上限](limits.md)にまとめてあります。`kMaxDocumentBytes` は受理契約の一部ではなくドキュメント読み込み時のメモリガードなので、`GenerateOptions` をメモリ上で組み立てるプログラムがこれに当たることはありません。

## 文字列ユーティリティ

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

`FoldAsciiString` は大小文字の畳み込みを定義する唯一の場所であり、すべてのサーフェスにおける大文字小文字を区別しない判定は、この関数か、同じバイト範囲を共有する `CaseInsensitiveEqual` を通ります。影響を受けるのは ASCII の英字だけで、それ以外のバイトは、マルチバイト UTF-8 シーケンスを構成するバイトも含めてそのまま複写されます。UTF-8 でこれが安全なのは、そうしたバイトが ASCII 英字の範囲に入らないからです。

`IsNumeric` は文字列が double としてパースできるかを返します。`ToDouble` は実際にパースしますが、`IsNumeric` が拒否する入力に対する動作は未定義です。`TryParseFiniteDouble` は先に検証するため、任意のテキストを受け取る場合の入口になります。非正規化数は表現可能なので受理され、無限大へオーバーフローする、あるいはゼロへアンダーフローする小数は拒否されます。

`JsNumberToString` は、CLI・WebAssembly ビルド・npm パッケージ・ピュア TypeScript 移植の数値出力をバイト単位で同一に保つ共有アルゴリズムです。同じ double へ往復できる最短の 10 進文字列を、ECMAScript の Number-to-String アルゴリズムに従って生成します。これは JavaScript の `String(value)` が返すものと同じです。数値のパラメータ値を `std::to_string` や `ostream` で出力する組み込みは他のサーフェスと一致しませんが、この関数を呼べば一致します。`3.14` は `3.14`、`1.0/3.0` は `0.3333333333333333`、`-0.0` は `0`、`100.0` は `100`、`1e-7` は `1e-7`、`1e21` は `1e+21` になります。

## 使用例

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

このプログラムは、`Tests:` にスイートの件数、`Coverage:` に生成が報告するカバレッジ率を出力し、続いて各テストケースを `名前=値` の組で 1 行ずつ、最後に `Validated:` として同じスイートを独立したバリデータが測ったカバレッジ率を出力します。これは CMake フィクスチャがインストール済みパッケージに対してコンパイルするプログラムであり、上記のテキストがそのままビルドできます。

## ビルド統合

### リリース済みビルドの利用

各 GitHub リリースに添付されている Linux x64 アーカイブは、コマンドラインバイナリだけではなく、完全なインストールツリーです。プロジェクトをステージング用の prefix にインストールして作られているため、静的ライブラリ、`include/coverwise/` 以下の公開ヘッダ 13 本、`lib/cmake/coverwise/` 以下の CMake パッケージファイル、`coverwise` 実行ファイル、ライセンスがすべて入っています。展開して `CMAKE_PREFIX_PATH` を展開先ディレクトリに向けるのが、`find_package` を動かすいちばん短い経路です。

```bash
tar -xzf coverwise-<version>-linux-x64.tar.gz
cmake -S . -B build -DCMAKE_PREFIX_PATH="$PWD/coverwise-<version>-linux-x64"
```

他のプラットフォームではソースからビルドします。`cmake --install` が同じツリーをインストールします。

### CMake

```cmake
add_subdirectory(coverwise)
target_link_libraries(your_target PRIVATE coverwise::coverwise)
```

インストール済みパッケージを利用する場合は次のようにします。

```cmake
find_package(coverwise CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE coverwise::coverwise)
```

生成される config は `SameMajorVersion` 互換なので、バージョン指定はメジャーバージョンが同じインストール済みリリースであれば満たされます。バージョンを指定しなければインストールされているものをそのまま受け入れます。現行のメジャー系列を追いかけるプロジェクトにはこれが適しています。

インストールが有効になるのは、トップレベルかつ WebAssembly でないビルドのときだけです。`add_subdirectory` や `FetchContent` で coverwise を取り込む親プロジェクトは、ターゲットを直接リンクし、coverwise 自身のインストールは行いません。

### コンパイラ要件

- C++17 以降
- 浮動小数点版 `std::to_chars` を持つ標準ライブラリ。これが下限を決めます。GCC 11+（libstdc++ 11+）、libstdc++ 11+ または libc++ 14+ に対してビルドした Clang 10+、macOS 13.3 以降をデプロイターゲットとする AppleClang 14+

数値の書式化は往復可能な最短桁を `std::to_chars(double)` から得ており、それを再現できる代替手段はありません。したがって古い標準ライブラリでは、出力が変わるのではなくビルドが失敗します。macOS 13.3 より古いデプロイターゲットを指す Apple のツールチェインは、デプロイターゲットを名指しする診断とともに拒否されます。

## 次に読むもの

- [入力上限](limits.md) — 上記の定数が表す具体的な値と、超過したときに呼び出し側に見えるもの
- [制約構文](constraints.md) — `ParseConstraint` が受け付ける言語
- [サーフェスの選び方](choosing-a-surface.md) — C++ ライブラリの組み込みが適する場面と、適さない場面
- [決定性](determinism.md) — シードが保証するものと、その保証が及ぶサーフェスの範囲
- [CLI リファレンス](cli.md) — コマンドの背後にある同じエンジンと、終了コードの契約
- [用語集](glossary.md) — このページが定義せずに使っている語彙
