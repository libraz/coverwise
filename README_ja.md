# coverwise

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/coverwise/ci.yml?branch=main&label=CI)](https://github.com/libraz/coverwise/actions)
[![codecov](https://codecov.io/gh/libraz/coverwise/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/coverwise)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue)](https://github.com/libraz/coverwise/blob/main/LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20WebAssembly-lightgrey)](https://github.com/libraz/coverwise)

高品質なテストスイートを設計するためのモダンな組み合わせテストエンジン。完全なカバレッジを保証。

テストケースの生成・分析・拡張を、ブラウザ、Node.js、ネイティブC++で実行。

## なぜ Coverwise？

バグの多くはコンポーネント間の予期しない相互作用から生まれます。Coverwiseはその相互作用を体系的に網羅します。

- **テスト品質を証明** — 正確なカバレッジ指標と、漏れている組み合わせをすべて特定
- **テストを設計する** — 生成するだけでなく、既存スイートの分析と増分拡張が可能
- **どこでも動作** — ブラウザ、CI、バックエンド — WASMによりネイティブ依存ゼロ

QAエンジニア、SDET、そして組み合わせ爆発なしに体系的なカバレッジを必要とする開発者のために。

## 仕組み

```mermaid
graph LR
    A["パラメータ\n+ 制約条件\n+ 強度"] --> B["Coverwise\nエンジン"]
    B --> C["generate\n最小テスト"]
    B --> D["analyze\nカバレッジギャップ"]
    D --> E["extend\n差分テスト"]
```

## クイックスタート

### JavaScript / TypeScript

```bash
npm install @libraz/coverwise
```

```typescript
import { Coverwise, when } from '@libraz/coverwise';

const cw = await Coverwise.create();

// ペアワイズカバレッジ100%の最小テストスイートを生成
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
console.log(result.tests);    // 10テスト、100%カバレッジ
console.log(result.uncovered); // [] — 漏れなし

// 既存テストの実際のカバレッジを計測
const report = cw.analyzeCoverage(parameters, myExistingTests);
console.log(report.coverageRatio); // 0.72
console.log(report.uncovered);     // ["os=Linux, browser=Safari", ...]

// ギャップだけを埋める — ゼロから再生成は不要
const extended = cw.extendTests(myExistingTests, { parameters, constraints });
console.log(extended.tests.length - myExistingTests.length); // 3テスト追加
```

## できること

| 機能 | 説明 |
|------|------|
| **ペアワイズ & t-wise** | 2-wise から任意の強度のカバリング配列を生成 |
| **制約** | `IF/THEN/ELSE`、`AND/OR/NOT`、関係演算（`<`、`>=`）、`IN`、`LIKE` |
| **ネガティブテスト** | 値を `invalid` 指定して単一障害のネガティブテストを自動生成 |
| **混合強度** | サブモデルで重要パラメータ群に高い網羅度を設定 |
| **境界値** | 整数・浮動小数点の範囲を自動的に境界値クラスに展開 |
| **同値クラス** | 値をクラスにグループ化し、クラスレベルのカバレッジを追跡 |
| **シードテスト** | 既存テストを活用して差分だけを追加生成 |
| **重み付け** | カバレッジが同等の場合に特定の値を優先 |
| **カバレッジ分析** | 任意のテストスイートのt-wiseカバレッジを独立検証 |
| **決定的出力** | 同じ入力＋シード＝毎回同じ出力 |

## CLI

```bash
# JSON仕様からテストを生成
coverwise generate input.json > tests.json

# 既存テストのカバレッジを分析
coverwise analyze --params params.json --tests tests.json

# 既存テストを拡張
coverwise extend --existing tests.json input.json

# モデル統計をプレビュー
coverwise stats input.json
```

終了コード: `0` 成功、`1` 制約エラー、`2` カバレッジ不足、`3` 入力不正。

## パフォーマンス

生成時間とテストスイートサイズは、さまざまな構成で良好にスケール：

| シナリオ | パラメータ数 | 値の数 | 強度 | テスト数 | 時間 |
|---------|------------|--------|------|---------|------|
| 小規模 | 10 | 3 | 2-wise | 23 | 18 ms |
| 中規模 | 20 | 5 | 2-wise | 71 | 23 ms |
| 大規模 | 30 | 5 | 2-wise | 76 | 28 ms |
| 3-wise | 15 | 3 | 3-wise | 101 | 31 ms |
| 4-wise | 8 | 3 | 4-wise | 228 | 26 ms |
| 高カーディナリティ | 5 | 20 | 2-wise | 515 | 29 ms |
| 多パラメータ | 50 | 3 | 2-wise | 35 | 27 ms |

Apple Mシリーズで計測。すべてのシナリオが35ms以内に完了し、100%カバレッジを保証。

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
