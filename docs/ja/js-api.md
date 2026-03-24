# JavaScript API

coverwise は ESM のみです。2つの API スタイルがあります：

**クラスベース（推奨）:**

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();
const result = cw.generate({ parameters: [...] });
```

**関数ベース:**

```typescript
import { init, generate, analyzeCoverage, extendTests, estimateModel } from '@libraz/coverwise';

await init();
const result = generate({ parameters: [...] });
```

どちらも同じ WASM シングルトンを共有し、相互に使用可能です。

## `init()`

WASM モジュールを初期化します。他の関数を呼ぶ前に必ず呼んでください。複数回呼んでも安全です。モジュールは一度だけロードされます。

```typescript
async function init(): Promise<void>
```

## `generate(input)`

パラメータとオプションから最小カバリング配列を生成します。

```typescript
function generate(input: GenerateInput): GenerateResult
```

### GenerateInput

```typescript
interface GenerateInput {
  parameters: Parameter[];       // 必須。1つ以上のパラメータ。
  constraints?: string[];        // 制約式。
  strength?: number;             // 相互作用の強度。デフォルト: 2（ペアワイズ）。
  seed?: number;                 // 決定性のためのRNGシード。デフォルト: 0。
  maxTests?: number;             // 最大テスト数。0 = 無制限（デフォルト）。
  weights?: WeightConfig;        // 値の重み付けヒント。
  seeds?: TestCase[];            // 既存テストケース。
  subModels?: SubModel[];        // 混合強度サブモデル。
}
```

### Parameter

```typescript
interface Parameter {
  name: string;
  values: (string | number | boolean | ParameterValue)[];
}

interface ParameterValue {
  value: string | number | boolean;
  invalid?: boolean;     // ネガティブテスト用の無効値マーク。
  aliases?: string[];    // この値の別名。
  class?: string;        // 同値クラス名。
}
```

**シンプルな値:**

```typescript
{ name: 'os', values: ['Windows', 'macOS', 'Linux'] }
```

**リッチな値:**

```typescript
{
  name: 'browser',
  values: [
    'Chrome',
    { value: 'IE', invalid: true },
    { value: 'Chromium', aliases: ['Chrome', 'Edge'] },
    { value: 'Firefox', class: 'gecko' },
  ],
}
```

**数値・真偽値:**

```typescript
{ name: 'version', values: [1, 2, 3] }
{ name: 'debug', values: [true, false] }
{ name: 'setting', values: ['auto', 0, true] }  // 混合型
```

数値と真偽値は内部で自動的に文字列に変換されます。

### WeightConfig

カバレッジが同等の場合に特定の値を優先するヒント。重みが大きいほど出現しやすくなります。

```typescript
interface WeightConfig {
  [parameterName: string]: {
    [value: string]: number;
  };
}
```

```typescript
generate({
  parameters: [/* ... */],
  weights: {
    os: { Windows: 2.0, macOS: 1.0, Linux: 1.0 },
  },
});
```

### SubModel

特定のパラメータグループに異なる強度を設定：

```typescript
interface SubModel {
  parameters: string[];  // パラメータ名。
  strength: number;      // このグループの強度。
}
```

```typescript
generate({
  parameters: [/* ... */],
  strength: 2,  // デフォルトのペアワイズ。
  subModels: [
    { parameters: ['os', 'browser', 'arch'], strength: 3 },  // 重要グループは3-wise。
  ],
});
```

### GenerateResult

```typescript
interface GenerateResult {
  tests: TestCase[];                // 正常テストケース（無効値なし）。
  negativeTests?: TestCase[];       // ネガティブテスト（無効値が正確に1つ）。
  coverage: number;                 // カバレッジ比率（0.0 – 1.0）。
  uncovered: UncoveredTuple[];      // 未カバータプルと理由。
  stats: GenerateStats;
  suggestions: Suggestion[];        // 改善の提案。
  warnings: string[];               // パフォーマンス/設定の警告。
  strength: number;                 // 使用された強度。
  classCoverage?: ClassCoverage;    // 同値クラス定義時に存在。
}

interface TestCase {
  [parameterName: string]: string | number | boolean;
}

interface GenerateStats {
  totalTuples: number;
  coveredTuples: number;
  testCount: number;
}

interface UncoveredTuple {
  tuple: string[];    // 例: ["os=Windows", "browser=Safari"]
  params: string[];   // 例: ["os", "browser"]
  reason: string;
  display: string;    // 人間可読: "os=Windows, browser=Safari"
}

interface Suggestion {
  description: string;
  testCase?: Record<string, string>;
}

interface ClassCoverage {
  totalClassTuples: number;
  coveredClassTuples: number;
  classCoverageRatio: number;
}
```

## `analyzeCoverage(parameters, tests, strength?)`

既存テストスイートの t-wise カバレッジを分析します。ジェネレータとは独立しており、任意のテストセットを検証できます。

```typescript
function analyzeCoverage(
  parameters: Parameter[],
  tests: TestCase[],
  strength?: number,        // デフォルト: 2
): CoverageReport
```

### CoverageReport

```typescript
interface CoverageReport {
  totalTuples: number;
  coveredTuples: number;
  coverageRatio: number;          // 0.0 – 1.0
  uncovered: UncoveredTuple[];    // 不足している組み合わせ。
}
```

**例:**

```typescript
const report = analyzeCoverage(
  [
    { name: 'os', values: ['Windows', 'macOS'] },
    { name: 'browser', values: ['Chrome', 'Firefox'] },
  ],
  [{ os: 'Windows', browser: 'Chrome' }],
);
// report.coverageRatio === 0.25（4ペア中1つカバー）
// report.uncovered.length === 3
```

## `extendTests(existing, input)`

既存テストスイートを拡張してカバレッジを改善します。既存テストはそのまま保持されます。

```typescript
function extendTests(
  existing: TestCase[],
  input: GenerateInput,
): GenerateResult
```

返される `result.tests` には既存テスト＋新規テストが含まれます。差分の取得：

```typescript
const result = extendTests(existing, input);
const newTests = result.tests.slice(existing.length);
```

## `estimateModel(input)`

生成を実行せずにモデル統計をプレビューします。生成前の複雑さ推定に便利です。

```typescript
function estimateModel(input: GenerateInput): ModelStats
```

### ModelStats

```typescript
interface ModelStats {
  parameterCount: number;
  totalValues: number;
  strength: number;
  totalTuples: number;
  estimatedTests: number;
  subModelCount: number;
  constraintCount: number;
  parameters: Array<{
    name: string;
    valueCount: number;
    invalidCount: number;
  }>;
}
```

## 制約ビルダー

文字列の代わりにプログラマティックに制約を構築：

```typescript
import { when, not, allOf, anyOf } from '@libraz/coverwise';

const result = cw.generate({
  parameters: [/* ... */],
  constraints: [
    when('os').eq('Windows').then(when('browser').ne('Safari')).toString(),
    not(allOf(when('os').eq('win'), when('browser').eq('safari'))).toString(),
    when('env').in('staging', 'prod').toString(),
    when('version').gt(3).toString(),
    when('browser').like('chrome*').toString(),
  ],
});
```

詳細は[制約構文](constraints.md)を参照してください。

## エラーハンドリング

無効な入力の場合、関数は `CoverwiseError` をスローします：

```typescript
interface CoverwiseError {
  code: 'CONSTRAINT_ERROR' | 'INSUFFICIENT_COVERAGE' | 'INVALID_INPUT' | 'TUPLE_EXPLOSION';
  message: string;
  detail?: string;
}
```

```typescript
try {
  const result = generate({ parameters: [] });
} catch (e) {
  console.error(e.code, e.message);
  // INVALID_INPUT "At least one parameter is required"
}
```
