# はじめに

## インストール

### JavaScript / TypeScript

```bash
npm install @libraz/coverwise
# or
yarn add @libraz/coverwise
```

### C++ (ネイティブ)

```bash
git clone https://github.com/libraz/coverwise.git
cd coverwise
make build
```

### CLI

ソースからビルド：

```bash
make build
# バイナリは build/coverwise に生成
```

## 最初のテストスイート

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

console.log(`${result.tests.length} テスト生成`);
console.log(`カバレッジ: ${result.coverage * 100}%`);

for (const test of result.tests) {
  console.log(test);
}
// { os: 'Windows', browser: 'Chrome', theme: 'light' }
// { os: 'macOS', browser: 'Firefox', theme: 'dark' }
// ...
```

### C++

```cpp
#include "coverwise.h"
#include <iostream>

int main() {
  coverwise::model::GenerateOptions opts;
  opts.parameters = {
    {"os",      {"Windows", "macOS", "Linux"}},
    {"browser", {"Chrome", "Firefox", "Safari"}},
    {"theme",   {"light", "dark"}},
  };
  opts.strength = 2;

  auto result = coverwise::core::Generate(opts);

  std::cout << "テスト数: " << result.tests.size() << std::endl;
  std::cout << "カバレッジ: " << result.coverage << std::endl;

  return 0;
}
```

### CLI

`input.json` を作成：

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

実行：

```bash
coverwise generate input.json > tests.json
```

## 制約の追加

実際のシステムには無効な組み合わせがあります。制約でそれを除外できます：

```typescript
import { when } from '@libraz/coverwise';

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari', 'IE'] },
  ],
  constraints: [
    when('os').eq('Windows').then(when('browser').ne('Safari')).toString(),
    when('os').eq('Linux').then(when('browser').ne('Safari')).toString(),
    when('os').eq('macOS').then(when('browser').ne('IE')).toString(),
    when('os').eq('Linux').then(when('browser').ne('IE')).toString(),
  ],
});
```

制約に違反するテストケースは生成されません。

## 既存テストのカバレッジ確認

すでにテストがある場合、ペアワイズカバレッジを確認できます：

```typescript
const parameters = [
  { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
];

const existingTests = [
  { os: 'Windows', browser: 'Chrome' },
  { os: 'macOS',   browser: 'Firefox' },
  { os: 'Linux',   browser: 'Safari' },
];

const report = cw.analyzeCoverage(parameters, existingTests);

console.log(`カバレッジ: ${report.coverageRatio * 100}%`);

if (report.uncovered.length > 0) {
  console.log('不足している組み合わせ:');
  for (const u of report.uncovered) {
    console.log(`  ${u.display}`);
  }
}
```

## テストの拡張

ゼロから再生成する代わりに、既存テストを拡張できます：

```typescript
const result = cw.extendTests(existingTests, {
  parameters: [/* ... */],
  constraints: [/* ... */],
});

// result.tests には既存テスト＋新規テストが含まれる
const newTests = result.tests.slice(existingTests.length);
console.log(`100%カバレッジ達成のため ${newTests.length} テスト追加`);
```

## 決定的出力

シードを設定して再現可能な結果を得られます：

```typescript
const result = cw.generate({
  parameters: [/* ... */],
  seed: 42,
});
// 同じパラメータ＋シード＝毎回同じテストスイート
```

## 次のステップ

- [実例集](examples.md) — ネガティブテスト、混合強度、境界値など
- [JavaScript API](js-api.md) — API リファレンス
- [制約構文](constraints.md) — 制約言語の完全なリファレンス
