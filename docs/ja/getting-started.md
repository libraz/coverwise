# はじめかた

このページでは、coverwise を各サーフェスにインストールし、完全なスイートを 1 つ生成し、返ってきたものが何なのかを説明します。ここで使う語彙（パラメータ、値、強度、タプル、カバレッジ）は[イントロダクション](introduction.md)で導入し、[入門](primer/index.md)で詳しく扱います。

## インストール

### JavaScript / TypeScript

```bash
npm install @libraz/coverwise
# or
yarn add @libraz/coverwise
```

### Python

```bash
pip install coverwise
```

API、pytest 用ヘルパー、エラー型は[Python API](python-api.md)にあります。

### C++

```bash
git clone https://github.com/libraz/coverwise.git
cd coverwise
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel
cmake --install build --prefix ./install
```

ヘッダー、リンク方法、API の全体は[C++ API](cpp-api.md)にあります。

### CLI

同じ PyPI パッケージがネイティブバイナリを同梱しています。

```bash
pip install coverwise
```

Linux x64 のアーカイブは[GitHub Releases](https://github.com/libraz/coverwise/releases)でも配布しています。npm パッケージにネイティブ CLI は含まれません。ソースからビルドする場合は次のとおりです。

```bash
make build
# Binary at build/bin/coverwise
cmake --install build --prefix ./install
# Installed binary at install/bin/coverwise
```

コマンド、JSON スキーマ、終了コードは[CLI リファレンス](cli.md)にあります。

## 実行環境の前提

- **JavaScript 系サーフェス** — Node.js 18 以上、かつ ESM のみです。パッケージは `"type": "module"` を宣言しており、以下の例はすべてトップレベル `await` を使うため、CommonJS のファイルでは構文解析に失敗します。CommonJS のプロジェクトでは `.mjs` ファイルを使うか、自分の `package.json` に `"type": "module"` を設定してください。
- **Python** — Python 3.10 以上です。Linux ホイールは manylinux_2_28 でビルドしているため glibc 2.28 以上が必要で、macOS ホイールは macOS 14 以降の Apple Silicon 向けです。
- **C++** — 浮動小数点の `std::to_chars` が使える C++17 コンパイラ、すなわち GCC 11 以上、Clang 10 以上、AppleClang 14 以上です。

## 最初のスイート

どのサーフェスでも同じモデルを使います。3 種類の OS、3 種類のブラウザ、2 種類のテーマを、既定の強度 2 で生成します。

### JavaScript

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'theme',   values: ['light', 'dark'] },
  ],
});

result.tests.length;         // 10
result.stats.totalTuples;    // 21
result.coverage;             // 1

for (const test of result.tests) {
  console.log(test);
}
// { os: 'macOS', browser: 'Firefox', theme: 'dark' }
// { os: 'Linux', browser: 'Firefox', theme: 'light' }
// { os: 'Linux', browser: 'Safari', theme: 'dark' }
// { os: 'Linux', browser: 'Chrome', theme: 'dark' }
// { os: 'macOS', browser: 'Chrome', theme: 'light' }
// { os: 'Windows', browser: 'Firefox', theme: 'light' }
// { os: 'Windows', browser: 'Safari', theme: 'dark' }
// { os: 'Linux', browser: 'Safari', theme: 'light' }
// { os: 'macOS', browser: 'Safari', theme: 'light' }
// { os: 'Windows', browser: 'Chrome', theme: 'dark' }
```

10 行です。組み合わせをすべて実行すると 3 × 3 × 2 = 18 行になります。この差は、1 行が同時に 3 つのペア（パラメータ対ごとに 1 つ）を網羅することから生まれます。10 行で 30 個のペア枠が得られ、網羅すべき相異なるペアは 21 個だからです。この 21 は `3 × 3` の os-browser ペア、`3 × 2` の os-theme ペア、`3 × 2` の browser-theme ペアの合計で、`result.stats.totalTuples` が報告する値です。

10 行は下限にも近い値です。os-browser のペアだけで 9 通りあるため、このモデルのスイートは 9 行を下回りません。coverwise はそれより 1 行多く使っています。この下限を決める規則と、実測値との対比は[パフォーマンス](performance.md)にあります。カバレッジの主張をうのみにせず確かめたい場合は、生成された行をそのまま `analyzeCoverage` に通してください。後述のカバレッジの節がそれを行っています。

### Python

```python
import coverwise

result = coverwise.generate(
    parameters={
        "os": ["Windows", "macOS", "Linux"],
        "browser": ["Chrome", "Firefox", "Safari"],
        "theme": ["light", "dark"],
    },
)

len(result["tests"])              # 10
result["stats"]["totalTuples"]    # 21
result["coverage"]                # 1.0

for test in result["tests"]:
    print(test)
# {'os': 'macOS', 'browser': 'Firefox', 'theme': 'dark'}
# {'os': 'Linux', 'browser': 'Firefox', 'theme': 'light'}
# {'os': 'Linux', 'browser': 'Safari', 'theme': 'dark'}
# {'os': 'Linux', 'browser': 'Chrome', 'theme': 'dark'}
# {'os': 'macOS', 'browser': 'Chrome', 'theme': 'light'}
# {'os': 'Windows', 'browser': 'Firefox', 'theme': 'light'}
# {'os': 'Windows', 'browser': 'Safari', 'theme': 'dark'}
# {'os': 'Linux', 'browser': 'Safari', 'theme': 'light'}
# {'os': 'macOS', 'browser': 'Safari', 'theme': 'light'}
# {'os': 'Windows', 'browser': 'Chrome', 'theme': 'dark'}
```

同じ 10 行が同じ順序で返ります。どのサーフェスも同じアルゴリズムで動くため、同じモデルとシードからはどこでも同じスイートが得られます。この保証が及ぶ範囲は[決定性](determinism.md)に書いてあります。

### C++

```cpp
#include <coverwise.h>

#include <iostream>

int main() {
  using namespace coverwise;

  model::GenerateOptions opts;
  opts.parameters = {
      {"os", {"Windows", "macOS", "Linux"}},
      {"browser", {"Chrome", "Firefox", "Safari"}},
      {"theme", {"light", "dark"}},
  };
  opts.strength = 2;

  auto result = core::Generate(opts);
  if (!result.error.ok()) {
    std::cerr << result.error.message << "\n";
    return 1;
  }

  std::cout << result.tests.size() << " tests, coverage " << result.coverage << "\n";
  // 10 tests, coverage 1
  return 0;
}
```

C++ の行は値の名前ではなく値のインデックスを保持するため、値を読み出すには `result.parameters[i].values` を引きます。その書き方と、ビルドがインストール済みパッケージに対してコンパイル・実行する長めのプログラムは[C++ API](cpp-api.md)にあります。

### CLI

モデルを `input.json` に書きます。

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] },
    { "name": "theme", "values": ["light", "dark"] }
  ],
  "strength": 2
}
```

そして実行します。

```bash
coverwise generate input.json > tests.json
```

`tests.json` には同じ 10 行が `tests` 配列を持つ JSON オブジェクトとして入ります。完全に網羅できたときの終了コードは 0、届かなかったときは 2 なので、そのまま CI のゲートとして使えます。

## WebAssembly ビルドを読み込めない場合

既定のエントリポイントは WebAssembly モジュールを読み込みます。JavaScript サーフェスのなかで、モデルとは無関係な理由で失敗しうる唯一の段階がこの読み込みです。バンドラが `.wasm` を出力しない場合や、そもそも WebAssembly が無い実行環境である場合が該当します。`/pure` サブパスは同じエンジンを TypeScript に移植したもので、API は同じ、WebAssembly は使いません。

```typescript
import { Coverwise } from '@libraz/coverwise/pure';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'theme',   values: ['light', 'dark'] },
  ],
});

result.tests.length;   // 10, the same suite in the same order
```

変わるのは import 指定子だけです。`Coverwise.create()` と `init()` はピュア TypeScript 側にも存在し、即座に返ります。既定のエントリポイント向けに書いたコードはそのまま動きます。代償は大きなモデルでの速度です。どちらを選ぶべきかは[サーフェスの選び方](choosing-a-surface.md)にまとめてあります。

## 制約の追加

ここから先のブロックは、最初の JavaScript ブロックで束縛した `cw` を使います。実際のモデルには成立しない組み合わせがあり（Safari は Windows でも Linux でも動きません）、制約はそれを規則として表します。

```typescript
const constrained = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'theme',   values: ['light', 'dark'] },
  ],
  constraints: ['IF browser = Safari THEN os = macOS'],
});

constrained.tests.length;         // 9
constrained.stats.totalTuples;    // 19 (21, less the 2 pairs no valid row can hold)
constrained.coverage;             // 1
```

制約が 1 本で足りるのは、それが結果ではなく規則そのものを述べているからです。`IF browser = Safari THEN os = macOS` は Windows 上の Safari と Linux 上の Safari を同時に排除し、モデルに 4 つ目の OS が増えても正しいままです。結果のほうを書く書き方、つまり Safari と組ませてはいけない OS ごとに 1 本ずつ書く書き方では、モデルが増えるたびに行を足す必要があり、書き忘れは何にも検出されない無効なテストケースになります。

必要タプル集合がどう変わったかにも注目してください。21 から 19 に減っています。`os=Windows, browser=Safari` と `os=Linux, browser=Safari` はどの正当なテストケースにも現れえないため、どのスイートにも要求されないからです。これは未網羅であることとは違います。その違いは[制約と必要タプル集合](primer/constraints-and-the-universe.md)で扱います。制約言語そのもの、およびこれらの文字列を生成する TypeScript ビルダーは[制約構文](constraints.md)にあります。

## 既存スイートの測定

`analyzeCoverage` は任意のスイートをモデルに照らして測定します。coverwise が生成したものでも、人が手で書いたものでも構いません。この測定はジェネレータから独立しており、だからこそ手書きのスイートも判定できます。両者を切り離している理由は[FAQ と制限](faq.md)にあります。

```typescript
const parameters = [
  { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
  { name: 'theme',   values: ['light', 'dark'] },
];

const existingTests = [
  { os: 'Windows', browser: 'Chrome',  theme: 'light' },
  { os: 'macOS',   browser: 'Firefox', theme: 'dark' },
  { os: 'Linux',   browser: 'Safari',  theme: 'light' },
];

const report = cw.analyzeCoverage(parameters, existingTests);

report.totalTuples;     // 21
report.coveredTuples;   // 9  (3 rows, 3 parameter pairs each, all distinct)
report.coverageRatio;   // 0.42857142857142855  (9 of 21)
report.uncoveredCount;  // 12

report.uncovered[0].display;   // 'os=Windows, browser=Firefox'
```

手書きの 3 行は 21 ペアのうち 9 ペアを網羅しています。これは 3 行で到達できる最大値です。1 行が os-browser、os-theme、browser-theme を 1 つずつ寄与し、この 3 行はそのどれも重複させていないからです。残る 12 ペアは `report.uncovered` に 1 つずつ返り、それぞれが組み合わせを名指しする `display` を持ちます。レポートは不足量に加えて、何が不足しているかまで述べます。

`analyzeCoverage` に渡すモデルは、テストが書かれた当時のモデルでなければなりません。モデルに無い値を名指す行や、パラメータが欠けている行はカバレッジの計算に入らず、読める理由つきで `report.invalidTests` に入ります。既存スイートのスコアが想定より低いときは、まずここを見てください。

## スイートの拡張

`extendTests` は渡した行をそのまま残し、追加する行を最小限に抑えます。このブロックは前節の続きで、そこで束縛した `parameters` と `existingTests` を使います。

```typescript
const extended = cw.extendTests(existingTests, { parameters });

extended.tests.length;                          // 10 (the 3 rows above, plus 7)
extended.tests.slice(0, existingTests.length);  // the 3 given rows, unchanged and in order
extended.coverage;                              // 1
```

ここでも 10 行で、ゼロから生成した場合と同じ行数です。手書きの 3 行は無駄にならず、書き換えられてもいません。既存の行がモデルの持たない何かを担っているときは拡張を選びます。ゼロから生成し直す場合との判断は[不足を少しずつ埋める](use-cases/close-the-gaps-incrementally.md)で扱います。

## 再現可能な出力

シードを渡すとスイートが固定されます。同じモデルとシードからは、どのサーフェスでも何度実行しても同じ行が同じ順序で返ります。このブロックは 2 つ前のブロックで束縛した `parameters` を使います。

```typescript
const first  = cw.generate({ parameters, seed: 42 });
const second = cw.generate({ parameters, seed: 42 });

first.tests.length;    // 9
second.tests.length;   // 9
// first.tests and second.tests are deeply equal, row for row
```

シードなしの実行が 10 行だったのに対し、ここでは 9 行です。シードが変わるとモデルを探索する順序が変わり、貪欲法が行き着くスイートの行数も変わりえます。シードを渡さない場合の既定値は 0 で、これもまた固定値です。上の例はシードを指定しなくても再現します。モデルを少しでも変えるとスイートは全面的に変わりうるもので、保証されるのは同一の入力に対してであって、似た入力に対してではありません。どのエンジンどうしが何によって結びつけられているかは[決定性](determinism.md)に書いてあります。

## パラメータが 1 つのモデル

パラメータが 1 つだけのモデルは、既定の強度では拒否されます。ペアを作るには 2 つのパラメータが必要だからです。

```typescript
const oneParameter = [{ name: 'os', values: ['Windows', 'macOS', 'Linux'] }];

cw.generate({ parameters: oneParameter });
// CoverwiseError: Strength must be between 1 and parameter count
// error.detail === 'strength=2, parameters=1'

const result = cw.generate({ parameters: oneParameter, strength: 1 });

result.tests.length;   // 3
result.coverage;       // 1
```

強度 1 は、各パラメータの各値が少なくとも 1 回は現れることを要求します。パラメータが 1 つなら、値ごとに 1 行という意味になります。同じ範囲の制限は `analyzeCoverage` にも、サブモデルごとの強度にも適用されます。t は 1 以上、その t が対象とするパラメータ数以下でなければなりません。

## 次に読むもの

- [入門](primer/index.md) — 上記すべての背後にある概念を、前提順に
- [実例集](examples.md) — 機能ごとのレシピ。異常系テスト、混合強度、境界値、重み
- [既存スイートを監査する](use-cases/audit-an-existing-suite.md) — 実際の手書きスイートから始める analyze の道筋
- [JavaScript API](js-api.md) — 公開されている 4 つのエントリポイントの完全なリファレンス
- [制約構文](constraints.md) — 演算子、ビルダー、パーサが拒否するもの
- [用語集](glossary.md) — 1 項目 1 概念の語彙集
