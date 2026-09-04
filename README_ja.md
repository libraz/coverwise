# coverwise

組み合わせテストのカバレッジエンジンです。既存のテストスイートが何を網羅しているかを計測し、不足分だけを追加し、あるいはカバリング配列をゼロから構築します。TypeScript、Python、C++、コマンドラインのいずれからも利用できます。

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/coverwise/ci.yml?branch=main&label=CI)](https://github.com/libraz/coverwise/actions)
[![npm](https://img.shields.io/npm/v/@libraz/coverwise)](https://www.npmjs.com/package/@libraz/coverwise)
[![PyPI](https://img.shields.io/pypi/v/coverwise)](https://pypi.org/project/coverwise/)
[![codecov](https://codecov.io/gh/libraz/coverwise/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/coverwise)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](https://github.com/libraz/coverwise/blob/main/LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![TypeScript](https://img.shields.io/badge/TypeScript-6-blue?logo=typescript)](https://www.typescriptlang.org/)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20WebAssembly-lightgrey)](https://github.com/libraz/coverwise)

![coverwise のテスト設計ループ。analyze が既存スイートを計測し、extend が不足を埋めるテストを追加し、generate がカバリング配列をゼロから構築します。](docs/images/test-design-loop-ja.svg)

coverwise は、動作がいくつかのパラメータ（OS、ブラウザ、デプロイ先、フィーチャーフラグなど）に左右されるシステムと、それらの相互作用を網羅しているかどうかわからないテストスイートとの間に位置します。パラメータとスイートを渡すとカバレッジレポートが返り、同じエンジンが、そのレポートの指摘した不足行を書きます。

## できること

1 つのモデルに対する 3 つの操作があります。`analyze` は、既存スイートが t-wise の相互作用空間をどれだけ網羅しているかを計測し、欠けている組み合わせをすべて名指しします。`extend` は、その不足を埋めるテストだけを追加し、既存の行は順序ごとそのまま残します。`generate` は、カバリング配列をゼロから構築します。coverwise はこの 3 つを対等な操作として扱います。また計測はジェネレータから独立しているため、coverwise が書いていないスイートも同じように判定できます。

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const parameters = [
  { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
  { name: 'env',     values: ['staging', 'production'] },
];

// A hand-written suite, as a project would already have it:
const myExistingTests = [
  { os: 'Windows', browser: 'Chrome',  env: 'production' },
  { os: 'macOS',   browser: 'Safari',  env: 'production' },
  { os: 'Linux',   browser: 'Firefox', env: 'staging' },
  { os: 'Windows', browser: 'Firefox', env: 'staging' },
];

const report = cw.analyzeCoverage(parameters, myExistingTests);
report.coverageRatio;         // 0.5238095238095238 (11 of the 21 pairs)
report.uncovered.length;      // 10
report.uncovered[0].display;  // 'os=Windows, browser=Safari'

const extended = cw.extendTests(myExistingTests, { parameters });
extended.tests.length;  // 11 — the four rows above, then seven that close the gaps
extended.coverage;      // 1
```

手書きの 4 行は `extended.tests` の先頭 4 行にそのまま現れます。`extend` は渡された行を並べ替えも書き換えもしないため、追加された 7 行だけを切り離してレビューできます。

## ユースケース

| 手元にあるもの | coverwise の役割 | ガイド |
|---|---|---|
| 手書きのスイートはあるが、何を取りこぼしているかは計測していない | t-wise のカバレッジ率を報告し、欠けている組み合わせをすべて名指しします | [既存スイートを監査する](docs/ja/use-cases/audit-an-existing-suite.md) |
| 不足はわかっているが、スイートを書き直したくはない | 不足を埋める行だけを追加し、もう一度計測します | [不足を少しずつ埋める](docs/ja/use-cases/close-the-gaps-incrementally.md) |
| pytest のモジュールが全組み合わせで parametrize されている | 同じパラメータに対するカバリングスイートに置き換えます | [pytest の全組み合わせを置き換える](docs/ja/use-cases/replace-a-cross-product-in-pytest.md) |

## インストール

```bash
npm install @libraz/coverwise
```

Node.js 18 以上が必要で、ESM のみの提供です。ブラウザで既定のエントリポイントを使う場合は WebAssembly のサポートが要りますが、ピュア TypeScript のエントリポイントには何も要りません。

```bash
pip install coverwise
```

Python 3.10 以上が必要です。wheel にはネイティブの `coverwise` 実行ファイルが同梱され、Linux の x86_64 と aarch64（manylinux_2_28 なので glibc 2.28 以上）、および macOS 14 以上の Apple Silicon 向けにビルドされています。npm パッケージは実行ファイルをインストールしません。

Linux x64 向けのアーカイブは各[GitHub Release](https://github.com/libraz/coverwise/releases)に添付されています。ソースからビルドする場合は、浮動小数点の `std::to_chars` を備えた C++17 コンパイラが必要です。GCC 11、Clang 10、AppleClang 14 以降が該当します。

## JavaScript と TypeScript

`generate` は同じパラメータリストを受け取り、有効なペアをすべて網羅するスイートを返します。制約は式の文字列としても、同じ文字列を組み立てる `when` ビルダーとしても書けます。

```typescript
import { Coverwise, when } from '@libraz/coverwise';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'theme',   values: ['light', 'dark'] },
  ],
  constraints: [
    when('os').eq('Windows').then(when('browser').ne('Safari')).toString(),
  ],
});

result.tests.length;  // 9
result.coverage;      // 1
```

`@libraz/coverwise/pure` は、同じエンジンを TypeScript に移植したものです。WebAssembly を読み込めない、あるいは読み込みたくないランタイム向けの入口になります。API は同一で、`Coverwise.create()` は何も読み込まずに解決します。

```typescript
import { Coverwise } from '@libraz/coverwise/pure';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
  ],
});

result.tests.length;  // 9
```

2 つの JavaScript エンジンはペアワイズのモデルではほぼ同等で、強度とタプル数が大きくなるほど WebAssembly 側が優位になります。5 つのサーフェスの比較と選び方は[サーフェスの選び方](docs/ja/choosing-a-surface.md)にあります。

## Python

PyPI パッケージには、ネイティブ実行ファイルと、同じ JSON 契約の上に立つ Python API が含まれます。`generate`、`analyze_coverage`、`extend_tests`、`estimate_model` は素の辞書をやり取りします。

```python
import coverwise

result = coverwise.generate(
    parameters={
        "os": ["Windows", "macOS", "Linux"],
        "browser": ["Chrome", "Firefox", "Safari"],
    },
    constraints=["IF os = Windows THEN browser != Safari"],
)

len(result["tests"])  # 8
result["coverage"]    # 1
```

`coverwise.parametrize` はモデルを pytest のケースに変換し、手書きの全組み合わせをカバリングスイートに置き換えます。

```python
@coverwise.parametrize({"os": ["Windows", "macOS"], "browser": ["Chrome", "Firefox"]})
def test_login(os, browser):
    assert login(os, browser).ok
```

## コマンドライン

`pip install coverwise` がインストールする実行ファイルは、C++ ビルドが生成するものと同じです。ソースからビルドすると `build/bin/coverwise` に出力され、`cmake --install build` で選んだプレフィックス配下の `bin/coverwise` に配置されます。

```bash
# Measure the coverage of an existing suite
coverwise analyze --params params.json --tests tests.json

# Add the tests that close the gaps
coverwise extend --existing tests.json input.json

# Build a covering suite from scratch
coverwise generate input.json > tests.json

# Preview the size of a model before generating
coverwise stats input.json
```

入力パスにはいずれも `-` を指定でき、その場合はその JSON を標準入力から読み込みます。中間ファイルなしでコマンドを繋げられます。

```bash
coverwise generate input.json | coverwise analyze --params input.json --tests -
```

`coverwise --help` は上記の使い方を標準出力に表示し、`0` で終了します。`--version` フラグはありません。

終了コードは、成功が `0`、制約エラーが `1`、カバレッジ不足が `2`、入力不正が `3` です。`3` は使い方の誤りと、標準出力への書き込み失敗も含みます。読み手がパイプを閉じた場合、プロセスの内側からはこの書き込み失敗として見えます。

## 機能

| 機能 | 説明 |
|------|------|
| **カバレッジ分析** | 任意のスイートの t-wise カバレッジを計測し、未網羅の組み合わせをすべて列挙します。 |
| **増分拡張** | 不足を埋めるテストだけを追加し、既存の行はそのまま残します。 |
| **ペアワイズと t-wise** | ペアワイズから任意の強度まで、カバリング配列を生成します。 |
| **制約** | `IF/THEN/ELSE`、`IMPLIES`、`AND/OR/NOT`、関係演算（`<`、`>=`）、`IN`、`LIKE`。 |
| **異常系テスト** | `invalid` を指定した値は、単一障害の異常系テストになります。 |
| **混合強度** | サブモデルで、重要なパラメータ群の強度を上げられます。 |
| **境界値** | 数値の範囲を、端と端付近の値に展開します。 |
| **等価クラス** | 値をクラスにまとめ、クラス単位でカバレッジを追跡します。 |
| **シードテスト** | 必須テストを起点に生成を始められます。 |
| **決定的** | 有効な入力とシードが同じなら、どのサーフェスでも同じスイートになります。 |

WebAssembly は C++ コアをコンパイルしたものなので、この 2 つは構成上一致します。ピュア TypeScript の移植は、[パリティスイート](js/compat.test.ts)によって WebAssembly サーフェスに合わせて検証されています。それぞれが何を保証し、何を保証しないかは[決定性](docs/ja/determinism.md)にまとめています。

## パフォーマンス

生成は貪欲かつ近似的です。最小のスイートではなく t-wise の完全な網羅を目標とするため、テスト数は理論最小値のおおむね 1.5〜2.5 倍になります。コストを決めるのはタプル集合の大きさで、これはパラメータ数よりも強度に対してはるかに速く増えます。

[パフォーマンス](docs/ja/performance.md)には、同梱のジェネレータが各種構成で生成するタプル数とテスト数を掲載しています。それらの数値が手元のモデルについて何を予測し、何を予測しないかもそこで説明しています。

## ドキュメント

組み合わせテストが初めての場合は、[入門](docs/ja/primer/index.md)から読んでください。ドキュメント全体が前提とする語彙をそこで組み立てます。全組み合わせがなぜ実行不能なのか、タプルとカバリング配列とは何か、強度を上げると何が得られて何を払うのか、制約は網羅対象の空間に何をするのか、を扱います。

### まず読む

- [イントロダクション](docs/ja/introduction.md) — coverwise とは何か、ループの全体像、利用者に委ねられること
- [はじめかた](docs/ja/getting-started.md) — インストールと、各サーフェスでの最初のスイート
- [ユースケース](docs/ja/use-cases/index.md) — 手元にあるデータから始まる実例ガイド

### ガイド

- [実例集](docs/ja/examples.md) — 機能ごとの、コピーして使えるレシピ
- [制約構文](docs/ja/constraints.md) — 制約言語のリファレンス
- [サーフェスの選び方](docs/ja/choosing-a-surface.md) — WebAssembly、ピュア TypeScript、ネイティブ C++、CLI、Python
- [決定性](docs/ja/determinism.md) — シードが保証すること、どのエンジン間で成り立つか
- [パフォーマンス](docs/ja/performance.md) — ベンチマーク表と、その読み方
- [入力上限](docs/ja/limits.md) — 入力の上限と、上限に達したときの挙動

### リファレンス

- [JavaScript API](docs/ja/js-api.md)
- [Python API](docs/ja/python-api.md)
- [C++ API](docs/ja/cpp-api.md)
- [CLI リファレンス](docs/ja/cli.md)
- [用語集](docs/ja/glossary.md)
- [FAQ と制限](docs/ja/faq.md)

## coverwise が行わないこと

- **テストの実行はしません**。coverwise が出力するのはパラメータ値の行です。それをテストフレームワークに渡すこと、何をアサーションとするかを決めることは、利用者側に残ります。Python パッケージには pytest 用のヘルパーが付属しますが、それ以外に実行環境やレポート形式、ディレクトリ構成を前提とする箇所はありません。
- **パラメータの意味は解釈しません**。値は不透明なラベルです。どの組み合わせが危険か、どの組み合わせがどの制約にも書かれない理由で起こり得ないか、カバレッジとは無関係に個別のテストを与えるべきか。いずれもモデルが持たないドメイン知識です。
- **ペアワイズで十分かどうかは判断できません**。強度は入力です。エンジンが報告するのは、指定された強度で何を網羅したかであって、その強度が対象システムの実際の欠陥を捉えるかどうかは、カバリング配列ではなくシステムについての判断になります。
- **テストフレームワークについての意見は持ちません**。出力は辞書のリストです。それを parametrize されたケース、フィクスチャ、テーブル駆動テスト、CSV ファイルのいずれにするかは呼び出し側に委ねられ、どのサーフェスも特別扱いされません。

## ビルド

```bash
# Native C++
make build            # Debug build
make release          # Optimized build
make test             # Python wheel, then the full C++ suite
cmake --install build --prefix ./install   # Library, headers, CMake package and CLI

# WebAssembly and JavaScript
make wasm             # Emscripten build of the WASM module
yarn build            # WASM plus the TypeScript wrapper
yarn test             # Vitest, including the WASM and pure TypeScript suites

# Python binding
make python-wheel     # Build the wheel around the native executable
make test-python      # Build the wheel, then run pytest against it

# Checks
make format           # Auto-fix C++, TypeScript and Python formatting
make format-check     # The same checks, writing nothing
make lint             # Static checks that are not formatting
make coverage         # C++ line coverage, written under build/coverage
make preflight        # Every gate CI runs, cheapest first
```

`make test` は C++ スイートを走らせる前に Python の wheel をビルドするため、`rye` がパス上にある必要があります。C++ のテストだけを走らせたい場合は、`make build` のあとに `ctest --test-dir build` を使ってください。

## ライセンス

[coverwise](https://github.com/libraz/coverwise)は[Apache License 2.0](LICENSE)で公開しています。
