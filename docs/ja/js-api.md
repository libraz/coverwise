# JavaScript API

npm パッケージ `@libraz/coverwise` のリファレンスです。公開されている 4 つのエントリポイント、そこから export される関数と型、そして入力が受理される条件を扱います。JavaScript や TypeScript から coverwise を動かす読者に向けたページで、語彙は[タプルとカバレッジ](primer/tuples-and-coverage.md)と[強度](primer/strength.md)のものを前提とし、ここでは再説明しません。パッケージの導入と最初のスイート生成は[はじめかた](getting-started.md)にあります。

## エントリポイント

このパッケージは 4 つのエントリポイントを公開しています。いずれも ESM で、CommonJS ビルドはありません。Node.js は 18 以上が必要です。

| エントリポイント | 内容 | 必要なもの |
|---|---|---|
| `@libraz/coverwise` | WebAssembly にコンパイルしたエンジンと制約ビルダー。既定の選択肢です。 | WASM をロードできるランタイムと、最初のエンジン呼び出し前の初期化 1 回 |
| `@libraz/coverwise/pure` | 同じ API を TypeScript で実装したもの。名前も結果も同一です。 | なし |
| `@libraz/coverwise/constraint` | 制約ビルダー単体。背後にエンジンを持ちません。 | なし |
| `@libraz/coverwise/wasm` | `.wasm` バイナリそのもの。バンドラにアセットとして扱わせるためのものです。 | バイナリアセットを扱えるバンドラ |

どれを選ぶかは別の問いです。[サーフェスの選び方](choosing-a-surface.md)が、CLI と Python パッケージも含めて答えます。

### ルートのエントリポイント

API のスタイルは 2 つあり、相互に置き換えられます。どちらも同じ WASM インスタンスを動かします。

クラスベース。

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();
const result = cw.generate({ parameters: [/* ... */] });
```

関数ベース。

```typescript
import { init, generate, analyzeCoverage, extendTests, estimateModel } from '@libraz/coverwise';

await init();
const result = generate({ parameters: [/* ... */] });
```

エンジンが公開する操作は `generate`、`analyzeCoverage`、`extendTests`、`estimateModel` の 4 つだけで、これに `init` と制約ビルダーが加わります。`Coverwise` のコンストラクタは private で、各メソッドは同名のフリー関数へ委譲します。2 つのスタイルが食い違うことはありません。

### `@libraz/coverwise/pure`

WASM をロードできない環境、あるいはロードしたくない環境のための TypeScript 移植版です。ルートのエントリポイントと同じ名前を、同じ宣言から export します。関数もクラスも型もすべて同一で、`js/export-parity.test.ts` が両方のエントリポイントをコンパイルして名前の集合が一致することを検証しています。プログラムは import 指定子を差し替えるだけで、他に何も変えずに移行できます。

```typescript
import { Coverwise } from '@libraz/coverwise/pure';

const cw = await Coverwise.create();
const result = cw.generate({ parameters: [/* ... */] });
```

名前ではなく挙動が異なる点が 3 つあります。`init()` は存在しますが何もしません。WASM 版向けに書かれたコードがそのままコンパイルできるようにするためのものです。`Coverwise.create()` は形の上では `async` で、解決済みの Promise を即座に返します。ロードするものはありません。それ以降はすべて同期的で、初期化は一切不要です。

一致するのは型の形だけではありません。`js/compat.test.ts` が `generate`、`analyzeCoverage`、`extendTests`、`estimateModel` の結果全体と、エラーの `code`・`message`・`detail` を突き合わせ、`js/acceptance-parity.test.ts` が受理・拒否する入力を突き合わせています。したがって同じシードからは、どちらのエントリポイントでも同じスイートが得られます。WASM は C++ コアをコンパイルしたものなので、WASM とネイティブの結果はテストではなく構成上一致します。パリティスイートが押さえているのは、TypeScript 移植版をその面に合わせる部分です。何が保証され何が保証されないかは[決定性](determinism.md)にまとめてあります。

### `@libraz/coverwise/constraint`

エンジンを伴わない制約ビルダーです。`when`、`not`、`allOf`、`anyOf` と、`Condition`、`ConditionStart`、`Constraint`、`IfConstraint` の各型を export します。ここから import すると WASM もジェネレータも読み込まれません。式を組み立てるだけのコード、たとえばルール集、テストモデルのエディタ、CLI へ渡す JSON を書き出すスクリプトで使えるのはこのためです。

```typescript
import { when } from '@libraz/coverwise/constraint';

const rule = when('os').eq('Windows').then(when('browser').ne('Safari'));
rule.toString(); // 'IF os = "Windows" THEN browser != "Safari"'
```

このエントリポイントは `CoverwiseError` をスローします。有限でない数値、1 つの裸のトークンとして書けない関係演算子のオペランド、空の `in()`・`allOf()`・`anyOf()` が対象です。ただしクラス自体は再 export していません。ビルダーがスローしたものを `instanceof` で判定するコードは、`@libraz/coverwise` または `@libraz/coverwise/pure` から `CoverwiseError` も import してください。

### `@libraz/coverwise/wasm`

コンパイル済みバイナリそのもので、JavaScript も型も含みません。バンドラが `.wasm` ファイルをアセットとして解決し、その URL を返せるようにするためのものです。ローダが実行時に探す必要がなくなります。

```typescript
// Vite and other bundlers that support the `?url` suffix.
import wasmUrl from '@libraz/coverwise/wasm?url';

// Webpack asset modules and anything else that understands `import.meta.url`.
const wasmUrlFromMeta = new URL('@libraz/coverwise/wasm', import.meta.url);
```

## `init()`

WASM モジュールをロードします。ルートのエントリポイントでは、他のどの関数よりも先に呼ぶ必要があります。何度呼んでも安全で、モジュールのロードは 1 回だけです。

```typescript
async function init(): Promise<void>
```

ロードの失敗はキャッシュされません。失敗した Promise は破棄され、次の `init()` が再試行します。ロードの失敗も、呼び忘れも、どちらも `CoverwiseError` のコード `INVALID_INPUT` として報告されます。[エラーハンドリング](#エラーハンドリング)を参照してください。

## `generate(input)`

モデルからカバリングスイートを構築します。

```typescript
function generate(input: GenerateInput): GenerateResult
```

### GenerateInput

```typescript
interface GenerateInput {
  parameters: Parameter[];       // Required.
  constraints?: string[];        // Constraint expressions.
  strength?: number;             // Interaction strength. Default: 2 (pairwise).
  seed?: number;                 // RNG seed: uint32 integer [0, 4294967295]. Default: 0.
  maxTests?: number;             // uint32 integer [0, 4294967295]. 0 = no limit (default).
  weights?: WeightConfig;        // Value weight hints.
  seeds?: TestCase[];            // Test cases the suite must contain.
  subModels?: SubModel[];        // Mixed-strength sub-models.
}
```

`strength` は 1 以上、パラメータ数以下の整数である必要があります。したがってパラメータが 1 個のモデルは、既定の強度 2 では拒否されます。対処は `strength: 1` であって、必要のない 2 個目のパラメータを足すことではありません。同じ上限は `analyzeCoverage` の `strength` 引数にも適用されます。

`seed` は、そのモデルが取りうる決定的なスイートのうち 1 つを選びます。同じ入力と同じシードからは同じスイートが得られます。両方のエントリポイントでも、他のどのサーフェスでも同じです。`maxTests` はスイートの件数を打ち切ります。打ち切られた実行はスローせず、`coverage < 1` と警告を伴って返ります。

### Parameter

```typescript
interface ParameterBase {
  name: string;
  values: (string | number | boolean | ParameterValue)[];
}

type Parameter =
  | PlainParameter          // ParameterBase & { type?: never; range?: never; step?: never }
  | IntegerBoundaryParameter // ParameterBase & { type: 'integer'; range: [number, number]; step?: 1 }
  | FloatBoundaryParameter;  // ParameterBase & { type: 'float';   range: [number, number]; step?: number }

interface ParameterValue {
  value: string | number | boolean;
  invalid?: boolean;     // Mark as invalid for negative testing.
  aliases?: string[];    // Alternate names for this value.
  class?: string;        // Equivalence class name.
}
```

`Parameter` は、export されている 3 つのインターフェースからなる判別可能な共用体です。離散パラメータには `type`、`range`、`step` のいずれも指定できません。境界値パラメータでは `type` と `range` の両方が必要です。`range` は両端を含み、float の `step` の既定値は `1.0`、integer の `step` は `1` のみです。境界値フィールドの一部だけの指定や不整合な指定は拒否されます。

単純な値。

```typescript
{ name: 'os', values: ['Windows', 'macOS', 'Linux'] }
```

リッチな値。

```typescript
{
  name: 'browser',
  values: [
    'Chrome',
    { value: 'IE', invalid: true },
    { value: 'Chromium', aliases: ['chromium-browser', 'cr'] },
    { value: 'Firefox', class: 'gecko' },
  ],
}
```

1 つのパラメータの中では、すべての値とすべてのエイリアスが、ASCII の大小文字を畳み込んだ後も互いに異なる名前になっている必要があります。そのため `Chrome` を値と `Chromium` のエイリアスの両方に使うことはできず、`Chrome` と `chrome` を並べることもできません。

数値と真偽値。

```typescript
{ name: 'version', values: [1, 2, 3] }
{ name: 'debug', values: [true, false] }
{ name: 'setting', values: ['auto', 0, true] }  // mixed types
```

数値と真偽値は内部で文字列に変換されます。

`values` はすべてのパラメータで必須で、境界値パラメータも例外ではありません。境界値パラメータは値を `range` から導くため、キーは `values: []` と書きます。この空配列は書き忘れではなく意図した綴りで、`Parameter` の共用体を満たすのはこの形です。離散パラメータの `values` が空の場合は拒否されます。

### WeightConfig

カバレッジの上で差がないときに、特定の値を優先させるヒントです。重みが大きいほど出現しやすくなります。

```typescript
interface WeightConfig {
  [parameterName: string]: {
    [value: string]: number;
  };
}
```

各重みは `0` より大きい有限数である必要があります。`0`、負数、`Infinity`、`NaN` は拒否されます。重みはグループ内の最大値に対する比として使われ、正の尺度の上でしか意味を持たないためです。ある値をスイートから完全に外したい場合は、重み `0` を与えるのではなく `values` から外してください。

```typescript
generate({
  parameters: [/* ... */],
  weights: {
    os: { Windows: 2.0, macOS: 1.0, Linux: 1.0 },
  },
});
```

### SubModel

モデル全体の強度を上げずに、特定のパラメータグループの強度だけを上げます。

```typescript
interface SubModel {
  parameters: string[];  // Parameter names.
  strength: number;      // Strength for this group.
}
```

サブモデルの `parameters` は空であってはならず、モデルが宣言しているパラメータを指す必要があり、同じ名前を 2 回挙げることもできません。`strength` を縛るのはモデルではなくそのグループ自身で、1 以上、その `parameters` 配列の長さ以下の整数です。名前が 3 つのグループは `strength: 3` を受理し、`strength: 4` を拒否します。モデル全体のパラメータ数がいくつであっても変わりません。

```typescript
generate({
  parameters: [/* ... */],
  strength: 2,  // Default pairwise.
  subModels: [
    { parameters: ['os', 'browser', 'arch'], strength: 3 },  // 3-wise for a critical group.
  ],
});
```

### GenerateResult

```typescript
interface GenerateResult {
  tests: TestCase[];                // Positive test cases (no invalid values).
  negativeTests: TestCase[];        // Negative tests (exactly 1 invalid value each). Empty array if none.
  negativeCoverage?: NegativeCoverage; // Feasible single-fault negative-tuple coverage.
  coverage: number;                 // Coverage ratio (0.0 – 1.0).
  uncovered: UncoveredTuple[];      // Uncovered tuples with reasons, capped for diagnostics.
  uncoveredCount: number;           // Total uncovered tuples before that cap.
  omittedUncovered: number;         // How many the cap left out of `uncovered`.
  stats: GenerateStats;
  suggestions: Array<{ description: string; testCase: Record<string, string> }>;
  warnings: string[];               // e.g. coverage below 100%, seeds dropped at maxTests.
  strength: number;                 // Actual strength used.
  classCoverage?: {                 // Present when equivalence classes are defined.
    totalClassTuples: number;
    coveredClassTuples: number;
    classCoverageRatio: number;
  };
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
  tuple: string[];    // e.g. ["os=Windows", "browser=Safari"]
  params: string[];   // e.g. ["os", "browser"]
  indices: Array<[number, number]>; // Exact [parameter index, value index] pairs.
  reason: string;
  display: string;    // Human-readable: "os=Windows, browser=Safari"
}

interface NegativeCoverage {
  totalTuples: number;
  coveredTuples: number;
  omittedTuples: number;
  coverageRatio: number;
}
```

`uncovered` は診断用のリストであって、網羅的な一覧ではありません。一定件数を超えるとそれ以上は伸びず、残りは件数として数えられます。`uncoveredCount` が本当の総数、`omittedUncovered` がリストから漏れた数で、`uncovered.length + omittedUncovered === uncoveredCount` は常に成り立ちます。この上限はレポートの構築コストを抑えるためのチューニング値であって、coverwise が受理する入力の一部ではありません。そのため数値としてはここに載せていません。`omittedUncovered > 0` を「まだある」と読み、`uncovered.length` を「ちょうどこれだけある」と読まないでください。

カバレッジ 100% に届かなかったスイートは、スローではなく返り値で伝わります。合図は `coverage < 1` で、理由は `warnings` が文章で述べます。上限が実行を縛った場合は `Generation stopped at maxTests (…) before reaching 100% coverage`、そうでない場合は `Generation stopped before reaching 100% coverage`、シードテストだけで上限を超えた場合は `Seed test count (…) exceeds maxTests (…); some seeds were dropped` です。

## `analyzeCoverage(parameters, tests, strength?, constraints?)`

既に存在するスイートの t-wise カバレッジを測ります。ジェネレータからは独立していて、タプル集合を自前で列挙します。そのため手書きのスイートも生成済みのスイートも同じように判定できます。

```typescript
function analyzeCoverage(
  parameters: Parameter[],
  tests: TestCase[],
  strength?: number,        // Default: 2. Between 1 and the parameter count.
  constraints?: string[],   // Optional constraint DSL strings.
): CoverageReport
```

`constraints` を渡すと、カバレッジの対象には、すべての制約を満たす完全なテストケースへ補完できるタプルだけが含まれます。補完できないタプルは `totalTuples`、`coveredTuples`、`uncovered` から除外されます。これは生成時と同じ意味です。生成済みスイートを解析して `coverageRatio === 1.0` になるのは、生成が完全カバレッジで完了した場合だけです。`maxTests` が実行を打ち切った場合や、生成が不完全カバレッジを報告した場合は、これより低くなります。

### CoverageReport

```typescript
interface CoverageReport {
  totalTuples: number;
  coveredTuples: number;
  coverageRatio: number;          // 0.0 – 1.0
  uncovered: UncoveredTuple[];    // What is missing, capped for diagnostics.
  uncoveredCount: number;         // Total uncovered tuples before that cap.
  omittedUncovered: number;       // How many the cap left out of `uncovered`.
  invalidTests: Array<{ testIndex: number; reason: string }>; // Rows excluded from coverage accounting.
}
```

失敗は返り値ではなくスローで伝わります。手元にある `CoverageReport` は常に完了した測定結果なので、`coverageRatio === 0` は「測っていない」ではなく「何も網羅されていない」を意味します。C++ ライブラリを組み込む場合は同じレポートを返り値として受け取るため、その 2 つを自分で区別する必要があります。どのフィールドがどのエラー終了経路で有効かは[C++ API](cpp-api.md)を参照してください。

### `invalidTests` に入る行

`analyzeCoverage` は手元にあるままのスイートを受け取ります。つまり、採点できない行も受け取るということです。そうした行はエラーではなく、測定を止めもしません。集計から外され、届いた位置のインデックスと、読まれることを前提に書かれた理由とともに `invalidTests` に記録されます。

- `missing value for parameter <name>` — モデルが宣言しているパラメータに、その行が触れていません。
- `value '<text>' is not declared by parameter <name>` — モデルに存在しない値を指しています。多くはモデル側で改名された後の綴りです。
- `value <name>=<value> is marked invalid` — `invalid: true` として宣言された値を使っています。この行はカバレッジ測定ではなく `negativeTests` の領分です。
- `violates constraint #<n> (constraint evaluation is false or indeterminate)` — 一緒に渡された制約に反しています。

集計は残った行だけで行われます。したがって `invalidTests` が多く `coverageRatio` が低いレポートは、スイートの穴ではなくスイートとモデルのずれを表しています。カバレッジ率を読む前に `invalidTests` を読んでください。

例です。

```typescript
const report = analyzeCoverage(
  [
    { name: 'os', values: ['Windows', 'macOS'] },
    { name: 'browser', values: ['Chrome', 'Firefox'] },
  ],
  [{ os: 'Windows', browser: 'Chrome' }],
);
// report.coverageRatio === 0.25  (1 of 4 pairs covered)
// report.uncovered.length === 3
```

## `extendTests(existing, input)`

既存のスイートに、穴を埋めるために必要なテストだけを足します。既存のテストはそのまま保持されます。

```typescript
function extendTests(
  existing: TestCase[],
  input: ExtendInput,
): GenerateResult
```

```typescript
interface ExtendInput extends GenerateInput {
  mode?: 'strict'; // Default and only supported mode.
}
```

`strict` は既存テストをすべてそのまま保持し、新しいテストだけを追加します。これ以外の `mode` 値は拒否されます。

返される `result.tests` には既存テストに続いて新規テストが入るため、差分はスライスで取れます。

```typescript
const result = extendTests(existing, input);
const newTests = result.tests.slice(existing.length);
```

`maxTests` は既存の行も含めた結果全体を数えます。そのため `existing.length` より小さい `maxTests` は、自分の入力すら収まらないスイートを求めることになり、その場で拒否されます。コードは `INVALID_INPUT`、メッセージは `maxTests cannot be smaller than the existing test count` です。`existing.length` 以上の `maxTests` は有効で、追加できる行数の上限として働きます。

## `estimateModel(input)`

生成を実行せずにモデルの規模を見積もります。

`totalTuples` は制約による除外を行う前の生のタプル上限ですが、不正な制約構文や未知のパラメータ参照は `generate` と同じように拒否されます。

`estimatedTests` は、最大値数・強度・パラメータ数から求めて `totalTuples` で頭打ちにした、粗い見積もりです。上限でも下限でもなく、生成されるスイートはこれを下回ることも上回ることもあります。

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

`when`、`not`、`allOf`、`anyOf` は、制約式を文字列として書き下す代わりに組み立てます。JavaScript の 3 つのエントリポイントすべてから export されており、チェーンを安全に書けるのはこれらがやり取りする型のおかげです。

```typescript
interface ConditionStart {
  eq(value: string | number | boolean): Condition;
  ne(value: string | number | boolean): Condition;
  gt(value: number | string): Condition;
  gte(value: number | string): Condition;
  lt(value: number | string): Condition;
  lte(value: number | string): Condition;
  in(...values: (string | number | boolean)[]): Condition;
  like(pattern: string): Condition;
}

interface Condition {
  and(other: Condition): Condition;
  or(other: Condition): Condition;
  then(consequence: Condition): IfConstraint;
  implies(consequence: Condition): Constraint;
  toString(): string;
}

interface Constraint {
  toString(): string;
}

interface IfConstraint extends Constraint {
  else(alternative: Condition): Constraint;
}
```

`when()` は `ConditionStart` を返し、各演算子は `Condition` を返します。そして `then()` は `IfConstraint` を、`implies()` は素の `Constraint` を返します。`else()` が `then()` の後でしか呼べないのはこのためです。文法上 2 つ目の `ELSE` には解釈がないので、`else()` を持つ型は他のどこからも生まれません。誤ったチェーンは、パーサが拒否する式を吐き出す前にコンパイルで止まります。

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

演算子、引用符の規則、ビルダーの全リファレンスは[制約構文](constraints.md)にあります。

## export される型

以下の型はすべて `@libraz/coverwise` と `@libraz/coverwise/pure` の双方から export されており、名前で import して自分のヘルパーに注釈を付けられます。

| 型 | 指すもの |
|---|---|
| `GenerateInput` | モデルとそのオプション。`generate` の引数であり `ExtendInput` の基底 |
| `ExtendInput` | `GenerateInput` に `mode` を加えたもの |
| `Parameter` | パラメータの 3 形状からなる判別可能な共用体 |
| `PlainParameter` | 離散パラメータ。`type`・`range`・`step` はいずれも `never` |
| `IntegerBoundaryParameter` | 整数の境界値パラメータ |
| `FloatBoundaryParameter` | 浮動小数点数の境界値パラメータ |
| `BoundaryParameter` | 非推奨。2 つの境界値形状の共用体。個別の型を名指ししてください |
| `ParameterValue` | `invalid`・`aliases`・`class` を持つリッチな値の形 |
| `SubModel` | グループ単位の強度上書き 1 件 |
| `WeightConfig` | パラメータ、次いで値をキーとする優先度の重み |
| `TestCase` | パラメータと値の対応として読める 1 テストケース |
| `GenerateResult` | `generate` と `extendTests` の返り値 |
| `GenerateStats` | `GenerateResult` に含まれるタプル数とテスト数 |
| `CoverageReport` | `analyzeCoverage` の返り値 |
| `UncoveredTuple` | 未網羅タプル 1 件。名前、インデックス、表示用文字列を持つ |
| `NegativeCoverage` | 単一障害の異常系タプルの指標 |
| `ClassCoverage` | 等価クラスの指標。`GenerateResult.classCoverage` の形 |
| `Suggestion` | `GenerateResult.suggestions` の要素 1 つの形 |
| `ModelStats` | `estimateModel` の返り値 |
| `ParamStats` | `ModelStats.parameters` の要素 1 つの形 |
| `CoverwiseErrorCode` | エラーコード 4 種からなる共用体 |
| `ConditionStart` | 演算子を選ぶ前の、`when()` の返り値 |
| `Condition` | 合成できる条件 |
| `Constraint` | 完成した式 |
| `IfConstraint` | `IF … THEN` の式。`else()` を受け付ける唯一の型 |

`ClassCoverage`、`Suggestion`、`ParamStats` が指すのは、`GenerateResult` と `ModelStats` がインラインで宣言している形です。形は同一なので値はどちらの向きにも代入できます。名前が用意されているのは、自分のコードに注釈を付けるためです。

`CoverwiseError` は型ではなくクラスで、コンストラクタは公開されています。`new CoverwiseError(code, message, detail?)` と書けるので、coverwise をラップする側は、呼び出し元が既に扱っているのと同じエラー型を自分でも投げられます。

## 入力バリデーション

エンジンを伴う 2 つのエントリポイントは、エンジンに到達する前に同一のバリデーションを実行します。したがって `@libraz/coverwise` と `@libraz/coverwise/pure` は、まったく同じ入力を受理し、まったく同じ入力を同じメッセージで拒否します。ここで走る検査は次のとおりです。

- `strength` — 正の整数である必要があります。非整数、負、ゼロは拒否されます。上限、すなわちパラメータ数を超えないという条件は、1 つ後ろの層でエンジンが検査します。
- `seed` — `[0, 4294967295]` の uint32 整数である必要があります。
- `maxTests` — `[0, 4294967295]` の uint32 整数である必要があります。`0` は無制限です。
- `parameters` — 配列である必要があります。空であってはならないという条件はこの層ではなくエンジンの規則で、`At least one parameter is required` というメッセージで届きます。
- パラメータ名 — 一意である必要があり、ASCII の大小文字を畳み込んだ後も一意でなければなりません。`os` と `OS` は共存できません。畳み込みに関する部分は、制約式が参照するパラメータ名を大文字小文字を区別せずに解決するためのものです。大小文字だけが異なると `OS = Windows` の指す先が 2 つのパラメータになってしまいます。
- 値とエイリアス — 1 つのパラメータの中で、値とそのすべてのエイリアスが、ASCII の大小文字を畳み込んだ後も互いに異なっている必要があります。値の解決は大文字小文字を区別しないため、大小文字だけが異なると `os = WINDOWS` の指す先が一意に定まりません。
- `weights` — 各重みは `0` より大きい有限数である必要があります。重みのキーは行と同じ規則で値を指すため、値やそのエイリアスのどの ASCII 大小文字表記でも解決されます。1 つのパラメータ内で 2 つのキーが同じ値を指すことはできません。適用できる重みは一方だけだからです。ただし、いずれかがモデルの宣言どおりの綴りであれば、そちらが優先されることで一意に決まります。エイリアスをキーにした重みと値そのものをキーにした重みを併記できるのは、この規則によるものです。
- リソース上限 — 呼び出しを縛る個数とバイト数の予算、すなわちパラメータ数、パラメータあたりの値数、テスト行数、制約数、文字列 1 つあたりのバイト数、文字列全体のバイト数は、[入力上限](limits.md)にまとめてあります。どちらのエントリポイントにも同じように適用されます。

エンジンがこれに加えて課す上限は、各オプションの説明で述べたとおりです。`strength` はパラメータ数に対して、サブモデルの強度は自分のグループに対して、`extendTests` の `maxTests` は `existing.length` に対して縛られます。

## エラーハンドリング

失敗はすべて `CoverwiseError` のスローとして届きます。返り値で報告されるものはありません。

```typescript
class CoverwiseError extends Error {
  readonly code: 'CONSTRAINT_ERROR' | 'INSUFFICIENT_COVERAGE' | 'INVALID_INPUT' | 'TUPLE_EXPLOSION';
  readonly detail?: string;
}
```

`CoverwiseError` はネイティブの `Error` を継承し、コンストラクタでプロトタイプチェーンを復元しています。そのためトランスパイル後も `instanceof` が機能し、エンジンを伴う両方のエントリポイントで同じように使えます。

```typescript
import { CoverwiseError, generate } from '@libraz/coverwise';

try {
  const result = generate({ parameters: [] });
} catch (e) {
  if (e instanceof CoverwiseError) {
    console.error(e.code, e.message, e.detail);
    // INVALID_INPUT "At least one parameter is required"
  }
}
```

`detail` には補足の文脈がある場合にそれが入ります。問題のある断片や、衝突した 2 つの数値などです。補足がない場合はどのサーフェスでも `undefined` になります。

### JavaScript から実際に分岐できるコード

`CoverwiseErrorCode` の要素が 4 つあるのは、C++ のエラー列挙型と 1 対 1 に対応しており、CLI が 4 つすべてを必要とするからです。JavaScript のエンジンがスローするのは、そのうち 3 つだけです。

| コード | JavaScript から届くか | 発生源 |
|---|---|---|
| `INVALID_INPUT` | 届く | 上記のバリデーションが拒否するもの全般、および後述の初期化まわりの 2 つの失敗 |
| `CONSTRAINT_ERROR` | 届く | パースできない制約式、未知のパラメータを指す制約式、モデルを充足不能にする制約式 |
| `TUPLE_EXPLOSION` | 届く | タプル集合の大きさ、またはパラメータの組み合わせ数がエンジン内部の作業量予算を超えるモデル |
| `INSUFFICIENT_COVERAGE` | 届かない | JavaScript のどちらのエンジンからも発生しません。C++ の語彙と CLI の終了コードに対応させるために存在します |

`INSUFFICIENT_COVERAGE` で分岐するコードは決して実行されません。届かなかったスイートはスローではなく返り値です。`coverage < 1` を調べ、`warnings` を読んでください。

`TUPLE_EXPLOSION` は入力の不正さではなく規模によって起こります。引き金になる 2 つはどちらも内部の作業量予算で、公表された上限ではありません。モデルが実体化する t-wise タプルの数と、`C(n, t)` が生むパラメータ組み合わせの数です。幅の広いモデルで `strength` を上げるのが最も一般的な到達経路です。どちらも t に対して組み合わせ的に増えるためです。受理の上限が何であり、これらとどう違うかは[入力上限](limits.md)が扱います。どちらの予算を超えたかはメッセージと `detail` が名指しします。

ルートのエントリポイントでは、2 つの失敗が独自のコードではなく `INVALID_INPUT` として届きます。`init()` より前に関数を呼ぶと `coverwise WASM module not initialized. Call await init() first.`、WASM モジュールのロードに失敗すると `coverwise WASM module failed to initialize: <cause>` です。後者はキャッシュされず次の `init()` が再試行するため、一時的な取得失敗であればリロードなしに復帰できます。

### プロパティ読み取りから届く外来の例外

渡されたオブジェクトのフィールドを読む行為は、呼び出し側のコードを走らせる行為でもあります。ゲッターや、リアクティブなストアやコンポーネントの状態オブジェクトが持つ Proxy のトラップがそれにあたります。バリデーションもエンジン呼び出しも、外来の例外を `CoverwiseError`（コードは `INVALID_INPUT`、メッセージはプロパティ読み取りが何を投げたかを名指しするもの）へ変換するフレームの中で走るため、そうした例外も他の失敗と同じ `CoverwiseError` として届きます。モジュールのハンドルは他のどこからも届かないため、後から追加されるエントリポイントもこの変換を受け継ぎます。

投げられた値の説明自体も防御的です。`instanceof`・`message`・`String` のいずれでも説明できない値は、2 つ目の外来例外に置き換えられるのではなく「説明できない値」として報告されます。したがって `catch (e) { if (e instanceof CoverwiseError) … }` の 1 か所で、Proxy 越しに読んだモデルが投げたものも含め、その呼び出しが起こしうる失敗をすべて受けられます。

## 次に読むもの

- [サーフェスの選び方](choosing-a-surface.md) — WASM、ピュア TypeScript、ネイティブ C++、CLI、Python のどれを選ぶか、それぞれの代償は何か
- [制約構文](constraints.md) — 式の言語、引用符の規則、ビルダーの全リファレンス
- [入力上限](limits.md) — 呼び出しが受理される個数とバイト数の予算を 1 か所にまとめたもの
- [実例集](examples.md) — 機能ごとのコピーして使えるレシピ
- [決定性](determinism.md) — シードが何を、どのエンジン間で保証し、何を保証しないか
- [用語集](glossary.md) — カバリング配列、タプル集合、強度、カバレッジ単位
