# 実例集

よくあるテストシナリオの実践的なレシピ集です。

## 基本的なペアワイズ生成

最も一般的なケース — すべてのパラメータペアを網羅する最小テストセットを生成：

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',       values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser',  values: ['Chrome', 'Firefox', 'Safari', 'Edge'] },
    { name: 'language', values: ['en', 'ja', 'de'] },
    { name: 'theme',    values: ['light', 'dark'] },
  ],
});

console.log(`${result.tests.length} テストで全 ${result.stats.totalTuples} ペアを網羅`);
```

## 制約：無効な組み合わせの除外

テストに不可能な組み合わせが含まれないようにします：

```typescript
import { when, allOf } from '@libraz/coverwise';

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'iOS', 'Android'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari', 'Edge'] },
    { name: 'device',  values: ['desktop', 'tablet', 'phone'] },
  ],
  constraints: [
    when('os').eq('iOS').then(when('browser').eq('Safari')).toString(),
    when('os').eq('iOS').then(when('device').ne('desktop')).toString(),
    when('os').eq('Android').then(
      allOf(when('browser').ne('Safari'), when('browser').ne('Edge'))
    ).toString(),
    when('device').eq('desktop').then(
      allOf(when('os').ne('iOS'), when('os').ne('Android'))
    ).toString(),
  ],
});
```

制約式の詳細は[制約構文](constraints.md)リファレンスを参照してください。

## ネガティブテスト

値を `invalid` としてマークすると、ネガティブテストケースが自動生成されます。各ネガティブテストは正確に1つの無効値を含み、単一障害の分離を保証します：

```typescript
const result = cw.generate({
  parameters: [
    { name: 'email', values: [
      'user@example.com',
      { value: '', invalid: true },
      { value: 'not-an-email', invalid: true },
    ]},
    { name: 'password', values: [
      'Str0ng!Pass',
      { value: '', invalid: true },
      { value: 'short', invalid: true },
    ]},
    { name: 'role', values: ['admin', 'user', 'guest'] },
  ],
});

console.log('正常テスト:', result.tests.length);
console.log('ネガティブテスト:', result.negativeTests.length);

// 正常テストは有効な組み合わせのみ。
// ネガティブテストはそれぞれ無効値が正確に1つ。
```

## 混合強度（サブモデル）

重要なパラメータグループには高いカバレッジを、それ以外はペアワイズを適用：

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',       values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser',  values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'protocol', values: ['HTTP/1.1', 'HTTP/2', 'HTTP/3'] },
    { name: 'auth',     values: ['none', 'basic', 'oauth'] },
    { name: 'cache',    values: ['enabled', 'disabled'] },
    { name: 'compress', values: ['gzip', 'br', 'none'] },
  ],
  strength: 2,  // デフォルト：ペアワイズ。
  subModels: [
    // 重要なネットワーキング3パラメータは 3-wise。
    { parameters: ['protocol', 'auth', 'cache'], strength: 3 },
  ],
});
```

## 同値クラス

値をクラスにグループ化し、クラスレベルのカバレッジを追跡：

```typescript
const result = cw.generate({
  parameters: [
    { name: 'age', values: [
      { value: '5',  class: 'child' },
      { value: '10', class: 'child' },
      { value: '25', class: 'adult' },
      { value: '40', class: 'adult' },
      { value: '70', class: 'senior' },
    ]},
    { name: 'plan', values: [
      { value: 'free',    class: 'unpaid' },
      { value: 'trial',   class: 'unpaid' },
      { value: 'monthly', class: 'paid' },
      { value: 'annual',  class: 'paid' },
    ]},
  ],
});

if (result.classCoverage) {
  console.log(`クラスカバレッジ: ${result.classCoverage.classCoverageRatio * 100}%`);
  // クラスレベルのカバレッジを追跡（child×unpaid, child×paid 等）
}
```

## 重み付けヒント

複数の候補が同等のカバレッジを提供する場合に値の選択を制御：

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari', 'Edge'] },
  ],
  weights: {
    os: { Windows: 3.0, macOS: 1.0, Linux: 1.0 },
    browser: { Chrome: 2.0 },
  },
});
// Windows と Chrome が出力により頻繁に出現します。
```

## シードテスト：既存テストの活用

必須テストから始めて、不足分を埋めます：

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'env',     values: ['staging', 'production'] },
  ],
  seeds: [
    // これらのテストは出力に必ず含まれます。
    { os: 'Windows', browser: 'Chrome', env: 'production' },
    { os: 'macOS',   browser: 'Safari', env: 'production' },
  ],
});
// シードが最初に含まれ、追加テストがカバレッジの隙間を埋めます。
```

## 境界値展開

数値範囲を境界値クラスに自動展開：

```json
{
  "parameters": [
    {
      "name": "port",
      "type": "integer",
      "range": [1, 65535],
      "step": 1
    },
    {
      "name": "timeout",
      "type": "float",
      "range": [0.1, 30.0],
      "step": 0.1
    }
  ]
}
```

`port` の場合、`1`、`2`、`65534`、`65535`（境界値）のような値が自動生成されます。

## モデル推定

生成前に複雑さを確認：

```typescript
const stats = cw.estimateModel({
  parameters: [
    { name: 'a', values: ['1', '2', '3', '4', '5'] },
    { name: 'b', values: ['1', '2', '3', '4', '5'] },
    { name: 'c', values: ['1', '2', '3', '4', '5'] },
    { name: 'd', values: ['1', '2', '3', '4', '5'] },
  ],
  strength: 3,
});

console.log(`パラメータ数: ${stats.parameterCount}`);
console.log(`3-wise タプル数: ${stats.totalTuples}`);
console.log(`推定テスト数: ${stats.estimatedTests}`);
```

## CI 統合

CI パイプラインで CLI を使用：

```bash
# テスト生成。カバレッジ不足の場合 CI を失敗させる。
coverwise generate input.json > tests.json
# 終了コード 2 = カバレッジ不足（maxTests 設定時）。

# 手書きテストのペアワイズカバレッジを検証。
coverwise analyze --params params.json --tests tests.json
# 終了コード 0 = 100%カバレッジ、2 = 不足あり。
```

```yaml
# GitHub Actions の例
- name: テストカバレッジ検証
  run: |
    coverwise analyze --params params.json --tests tests.json
```
