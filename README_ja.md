# coverwise

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/coverwise/ci.yml?branch=main&label=CI)](https://github.com/libraz/coverwise/actions)
[![codecov](https://codecov.io/gh/libraz/coverwise/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/coverwise)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue)](https://github.com/libraz/coverwise/blob/main/LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20WebAssembly-lightgrey)](https://github.com/libraz/coverwise)

組み合わせテストカバレッジエンジン。既存テストのカバレッジギャップを分析し、最小テストスイートを生成し、差分だけを追加拡張します。ブラウザ、Node.js、ネイティブ C++ で動作。

## 概要

coverwise はテスト設計ループを構成する3つの操作を提供します：

- **`analyze`** — 既存テストスイートの t-wise カバレッジを計測し、未カバーの組み合わせを列挙
- **`extend`** — カバレッジギャップを埋めるテストだけを生成
- **`generate`** — ゼロから完全カバレッジの最小テストスイートを作成

```mermaid
graph LR
    A["既存テスト"] --> B["analyze"]
    B --> C{"100%?"}
    C -- No --> D["extend"]
    D --> A
    C -- Yes --> E["完了"]
    F["テストなし"] --> G["generate"]
    G --> A
```

多くの組み合わせテストツールは `generate` のみをサポートしています。coverwise は `analyze` と `extend` を同等の操作として扱います。

## クイックスタート

```bash
npm install @libraz/coverwise
```

### 既存テストを分析する

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const report = cw.analyzeCoverage({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'env',     values: ['staging', 'production'] },
  ],
  tests: myExistingTests,
});

report.coverageRatio;  // 0.72
report.uncovered;      // ["os=Linux, browser=Safari", "os=Linux, env=production", ...]
```

### 不足分を拡張する

```typescript
const result = cw.extendTests({
  parameters,
  existing: myExistingTests,
});

result.tests.length - myExistingTests.length;  // 3テスト追加
result.coverage;   // 1.0
result.uncovered;  // []
```

### ゼロから生成する

```typescript
import { when } from '@libraz/coverwise';

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
```

## CLI

```bash
# 既存テストのカバレッジを分析
coverwise analyze --params params.json --tests tests.json

# 不足カバレッジを補うテストを追加
coverwise extend --existing tests.json input.json

# テストスイートをゼロから生成
coverwise generate input.json > tests.json

# モデルの複雑さを確認
coverwise stats input.json
```

終了コード: `0` 成功、`1` 制約エラー、`2` カバレッジ不足、`3` 入力不正。

## 機能一覧

| 機能 | 説明 |
|------|------|
| **カバレッジ分析** | 任意のテストスイートの t-wise カバレッジを計測。未カバーの組み合わせをすべて列挙。 |
| **増分拡張** | カバレッジギャップを埋めるテストだけを追加。既存テストはそのまま保持。 |
| **ペアワイズ & t-wise** | 2-wise から任意の強度のカバリング配列を生成。 |
| **制約** | `IF/THEN/ELSE`、`AND/OR/NOT`、関係演算（`<`、`>=`）、`IN`、`LIKE`。 |
| **ネガティブテスト** | 値を `invalid` 指定して単一障害のネガティブテストを自動生成。 |
| **混合強度** | サブモデルで重要パラメータ群に高い網羅度を設定。 |
| **境界値** | 整数・浮動小数点の範囲を自動的に境界値クラスに展開。 |
| **同値クラス** | 値をクラスにグループ化し、クラスレベルのカバレッジを追跡。 |
| **シードテスト** | 必須テストを起点に、不足分だけを追加生成。 |
| **決定的出力** | 同じ入力＋シード＝毎回同じ出力。 |

## パフォーマンス

すべての構成で 100% t-wise カバレッジを達成。独立したカバレッジバリデータで検証済みです。生成テスト数はカバリングアレイ研究の既知の理論的境界内に収まっています。

### ペアワイズ (2-wise)

| 構成 | パラメータ数 | 値の数 | タプル数 | テスト数 | 理論下限 | 時間 |
|------|------------|--------|---------|---------|---------|------|
| 5 × 3 均一 | 5 | 3 | 90 | 16 | 9 (OA) | < 1 ms |
| 10 × 3 均一 | 10 | 3 | 405 | 20 | 9 (OA) | < 1 ms |
| 13 × 3 均一 | 13 | 3 | 702 | 21 | 9 (OA) | < 1 ms |
| 10 × 5 均一 | 10 | 5 | 1,125 | 52 | 25 | 1 ms |
| 15 × 4 均一 | 15 | 4 | 1,680 | 40 | 16 | 1 ms |
| 20 × 2 均一 | 20 | 2 | 760 | 12 | 4 | < 1 ms |
| 20 × 5 均一 | 20 | 5 | 4,750 | 66 | 25 | 4 ms |
| 30 × 5 均一 | 30 | 5 | 10,875 | 76 | 25 | 9 ms |
| 50 × 3 均一 | 50 | 3 | 11,025 | 33 | 9 (OA) | 6 ms |
| 5 × 20 高カーディナリティ | 5 | 20 | 4,000 | 514 | 400 | 9 ms |
| 3⁴ × 2³ 混合 | 7 | 2–3 | 138 | 14 | 9 | < 1 ms |
| 5¹ × 3³ × 2⁴ 混合 | 8 | 2–5 | 208 | 19 | 15 | < 1 ms |

### 高強度

| 構成 | パラメータ数 | 値の数 | 強度 | タプル数 | テスト数 | 時間 |
|------|------------|--------|------|---------|---------|------|
| 15 × 3 | 15 | 3 | 3-wise | 12,285 | 100 | 11 ms |
| 8 × 3 | 8 | 3 | 4-wise | 5,670 | 236 | 8 ms |

Apple M シリーズで計測（seed=42）。理論下限は直交配列 (OA) 理論または v² 下界に基づく既知の下限値。貪欲法アルゴリズムは一般に理論最小値の 1.5〜2.5 倍のテスト数を生成します。

## ビルド

```bash
# ネイティブ (C++)
make build            # デバッグビルド
make test             # テスト実行
make release          # 最適化ビルド

# WebAssembly
make wasm             # EmscriptenでWASMビルド

# JavaScript
yarn build            # WASM + TypeScriptビルド
yarn test             # JS/WASMテスト実行
```

## ライセンス

[Apache License 2.0](LICENSE)
