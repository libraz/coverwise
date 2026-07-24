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

WASM モジュールを初期化します。他の関数を呼ぶ前に必ず呼んでください。複数回呼んでも安全です。モジュールは一度だけロードされます。初期化に失敗した場合（例：WASMファイルが見つからない）、以降の呼び出しは失敗をキャッシュせずリトライします。

```typescript
async function init(): Promise<void>
```

## `generate(input)`

パラメータとオプションからカバリングテストスイートを生成します。

```typescript
function generate(input: GenerateInput): GenerateResult
```

### GenerateInput

```typescript
interface GenerateInput {
  parameters: Parameter[];       // 必須。1つ以上のパラメータ。
  constraints?: string[];        // 制約式。
  strength?: number;             // 相互作用の強度（正の整数）。デフォルト: 2（ペアワイズ）。
  seed?: number;                 // 決定性のための RNG シード。uint32 整数 [0, 4294967295]。デフォルト: 0。
  maxTests?: number;             // uint32 整数 [0, 4294967295]。0 = 無制限（デフォルト）。
  weights?: WeightConfig;        // 値の重み付けヒント。
  seeds?: TestCase[];            // 既存テストケース。
  subModels?: SubModel[];        // 混合強度サブモデル。
}
```

### Parameter

```typescript
interface ParameterBase {
  name: string;
  values: (string | number | boolean | ParameterValue)[];
}

type Parameter =
  | (ParameterBase & { type?: never; range?: never; step?: never })
  | (ParameterBase & { type: 'integer'; range: [number, number]; step?: 1 })
  | (ParameterBase & { type: 'float'; range: [number, number]; step?: number });

interface ParameterValue {
  value: string | number | boolean;
  invalid?: boolean;     // ネガティブテスト用の無効値マーク。
  aliases?: string[];    // この値の別名。
  class?: string;        // 同値クラス名。
}
```

`Parameter` は判別可能な共用体です。離散パラメータには `type`、`range`、`step` のいずれも指定できません。境界値パラメータでは `type` と `range` の両方が必要です。`range` は両端を含み、float の `step` のデフォルトは `1.0`、integer の `step` は `1` のみです。境界値フィールドの一部だけの指定や不整合な指定は拒否されます。

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
  negativeTests: TestCase[];        // ネガティブテスト（無効値が正確に1つ）。該当なしの場合は空配列。
  negativeCoverage?: NegativeCoverage; // 実行可能な単一障害ネガティブタプルのカバレッジ。
  coverage: number;                 // カバレッジ比率（0.0 – 1.0）。
  uncovered: UncoveredTuple[];      // 未カバータプルと理由。
  uncoveredCount: number;           // 診断情報の件数上限前の未カバータプル総数。
  omittedUncovered: number;         // 件数上限により `uncovered` から省かれた数。
  stats: GenerateStats;
  suggestions: Suggestion[];        // 改善の提案。
  warnings: string[];               // 警告（例：カバレッジ100%未達、シード数がmaxTestsを超過）。
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
  indices: Array<[number, number]>; // 正確な [パラメータインデックス, 値インデックス]。
  reason: string;
  display: string;    // 人間可読: "os=Windows, browser=Safari"
}

interface NegativeCoverage {
  totalTuples: number;
  coveredTuples: number;
  omittedTuples: number;
  coverageRatio: number;
}

interface Suggestion {
  description: string;
  testCase: Record<string, string>;
}

interface ClassCoverage {
  totalClassTuples: number;
  coveredClassTuples: number;
  classCoverageRatio: number;
}
```

## `analyzeCoverage(parameters, tests, strength?, constraints?)`

既存テストスイートの t-wise カバレッジを分析します。ジェネレータとは独立しており、任意のテストセットを検証できます。

```typescript
function analyzeCoverage(
  parameters: Parameter[],
  tests: TestCase[],
  strength?: number,        // デフォルト: 2
  constraints?: string[],   // 制約 DSL 文字列(省略可)
): CoverageReport
```

`constraints` を渡すと、カバレッジの対象には、すべての制約を満たす完全なテストケースへ補完できるタプルだけが含まれます。完全なテストケースへ補完できないタプルは `totalTuples`、`coveredTuples`、`uncovered` から除外されます。これは生成時と同じ意味です。生成済みスイートを解析したときに `coverageRatio === 1.0` となるのは、生成が完全カバレッジで正常に完了した場合だけです。`maxTests` により生成が打ち切られた場合（または生成が不完全カバレッジを報告した場合）は、これより低くなることがあります。

### CoverageReport

```typescript
interface CoverageReport {
  totalTuples: number;
  coveredTuples: number;
  coverageRatio: number;          // 0.0 – 1.0
  uncovered: UncoveredTuple[];    // 不足している組み合わせ。
  uncoveredCount: number;         // 診断情報の件数上限前の未カバータプル総数。
  omittedUncovered: number;       // 件数上限により `uncovered` から省かれた数。
  invalidTests: Array<{ testIndex: number; reason: string }>; // カバレッジ集計から除外された行。
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
  input: ExtendInput,
): GenerateResult
```

```typescript
interface ExtendInput extends GenerateInput {
  mode?: 'strict'; // デフォルトかつ唯一サポートされるモード。
}
```

`strict` は既存テストをすべてそのまま保持し、新しいテストだけを追加します。これ以外の `mode` 値は拒否されます。

返される `result.tests` には既存テスト＋新規テストが含まれます。差分の取得：

```typescript
const result = extendTests(existing, input);
const newTests = result.tests.slice(existing.length);
```

## `estimateModel(input)`

生成を実行せずにモデル統計をプレビューします。生成前の複雑さ推定に便利です。

推定値は制約除外前の raw タプル上限ですが、不正な制約構文や未知のパラメータ参照は `generate` と同様に拒否されます。

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
import { init, generate, when, not, allOf } from '@libraz/coverwise';

await init();

const result = generate({
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

## 入力バリデーション

Pure TypeScript API は入力を検証し、不正な値に対して説明的なエラーをスローします：

- `strength`: 正の整数である必要があります。非整数、負、ゼロは拒否されます。
- `seed`: `[0, 4294967295]` の uint32 整数である必要があります。
- `maxTests`: `[0, 4294967295]` の uint32 整数である必要があります。`0` は無制限です。
- `parameters`: 空でない配列である必要があります。
- 公開入力にはリソース上限があります。パラメータは最大 1,024、各値配列は最大 16,384、テスト行は最大 100,000、制約は最大 256、文字列は各 64 KiB、文字列全体では 1 MiB です。

WASM API は C++ 側の境界で同等のバリデーションを行います。

## エラーハンドリング

無効な入力の場合、関数は `CoverwiseError` をスローします：

```typescript
class CoverwiseError extends Error {
  readonly code: 'CONSTRAINT_ERROR' | 'INSUFFICIENT_COVERAGE' | 'INVALID_INPUT' | 'TUPLE_EXPLOSION';
  readonly detail?: string;
}
```

`CoverwiseError` はネイティブの `Error` を継承するため、WASM 版・純 TS 版のどちらでも `instanceof` が機能します：

```typescript
import { CoverwiseError } from '@libraz/coverwise';

try {
  const result = generate({ parameters: [] });
} catch (e) {
  if (e instanceof CoverwiseError) {
    console.error(e.code, e.message, e.detail);
    // INVALID_INPUT "At least one parameter is required"
  }
}
```
