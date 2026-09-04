# 既存スイートを監査する

手書きのスイートは、書いた人がたまたま書き留めた組み合わせだけを網羅しています。`analyzeCoverage` は、そのスイートがパラメータモデルに対してどの t-タプルに到達しているかを測り、到達していないものを報告します。スイートを書き換えることはありません。モデルが妥当かどうかも判断しません。モデルは対象システムが何を変化させるかについての主張であり、それを与えるのは読み手です。

このページが前提とする語彙、すなわちタプル、カバレッジ単位、カバレッジ率、必要タプル集合は[タプルとカバレッジ](../primer/tuples-and-coverage.md)で説明しています。

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const parameters = [
  { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
  { name: 'device',  values: ['phone', 'desktop'] },
];

const existingTests = [
  { os: 'Windows', browser: 'Chrome',  device: 'desktop' },
  { os: 'macOS',   browser: 'Safari',  device: 'phone' },
  { os: 'Linux',   browser: 'Firefox', device: 'desktop' },
  { os: 'Windows', browser: 'Edge',    device: 'phone' },
  { os: 'macOS',   browser: 'Chrome' },
];

const report = cw.analyzeCoverage(parameters, existingTests);

report.totalTuples;   // 21
report.coveredTuples; // 9
report.coverageRatio; // 0.42857142857142855 (9 of 21 pairs)
```

## カバレッジ率の読み方

分母は、このモデルが強度 2 で持つ 21 個の値ペアです。この 21 の導出は[ユースケース](index.md)にあります。分母はモデルだけで決まり、スイートには依存しません。

分子が 9 なのは、5 行のうち 3 行しか数えられないからです。その 3 行はそれぞれパラメータの組ごとに 1 個ずつ、計 3 個のペアに寄与し、9 個のあいだに重複はありません。したがってスイートは 21 個中 9 個に到達しています。残りの 2 行は数える前に除外されており、その理由は `invalidTests` にレポートされます。

カバレッジ率はモデルに対する割合であって、成績ではありません。システムの半分を書き落としたモデルに対する 0.43 は、システムを記述しきったモデルに対する 0.43 より価値が低いということです。

## `uncovered` の読み方

同じモジュールの続きです。`uncovered` は、スイートが一度も到達していないペアを 1 つずつ挙げます。

```typescript
report.uncoveredCount;       // 12
report.omittedUncovered;     // 0
report.uncovered.length;     // 12
report.uncovered[0].params;  // ['os', 'browser']
report.uncovered[0].tuple;   // ['os=Windows', 'browser=Firefox']
report.uncovered[0].reason;  // 'never covered'
report.uncovered[0].display; // 'os=Windows, browser=Firefox'
```

`params` はそのタプルを構成するパラメータ名、`tuple` は各パラメータの値で、どちらも配列です。文字列を分割しなくてもプログラムから読めます。`display` は同じタプルを 1 行のテキストにしたもので、人が読むレポート向けです。`reason` は未網羅である理由を記録します。全項目で `'never covered'` になります。制約によって不可能になったタプルは、ここに報告されるのではなく必要タプル集合から取り除かれるためです。

モデルが診断として扱いきれないほど多くの欠落を生む場合、`uncovered` は途中で打ち切られます。`uncoveredCount` が真の総数、`omittedUncovered` が省略された件数なので、`uncovered.length` は `uncoveredCount - omittedUncovered` と等しくなります。数を報告するときは件数を、欠落を列挙するときは配列を読んでください。

各項目は判断を求めます。重要なペアは、書くべきテストです。実システムでは起こり得ないペアは、モデルに欠けている制約です。制約として書き留めれば、そのペアは欠落一覧に居座り続けるのではなく分母から外れます。

## モデルが認識できない行

```typescript
report.invalidTests.length;       // 2
report.invalidTests[0].testIndex; // 3
report.invalidTests[0].reason;    // "value 'Edge' is not declared by parameter browser"
report.invalidTests[1].testIndex; // 4
report.invalidTests[1].reason;    // 'missing value for parameter device'
```

`invalidTests` に入るのは、モデルが宣言していない値を挙げている行、モデルが宣言しているパラメータの値を挙げていない行、そして解析に渡した制約に反する行です。`testIndex` は渡した配列内での位置、`reason` は人が読むための文面です。これらの行は `coveredTuples` に一切寄与しません。上のカバレッジ率が 5 行ではなく 3 行を測っているのはそのためです。

モデルが宣言していない項目を持っていることは理由になりません。`locale` パラメータを持たないモデルに対して `locale` キーを含む行は、モデルが宣言しているパラメータについて読まれ、通常どおり数えられます。行に付随情報を持たせているスイートも、解析の前に削ぎ落とす必要はありません。

2 つの理由はどちらも同じ問いを指しています。古いのはモデルと行のどちらか、という問いです。3 番目の行はモデルが宣言していないブラウザを動かしています。モデルに値が足りず `Edge` を加えるべきか、あるいはその行がモデルの記述範囲の外を試しているかのどちらかです。4 番目の行は `device` を欠いています。スイートがそのパラメータより古く値を補うべきか、あるいはそのパラメータがこのモデルに属さないかのどちらかです。

カバレッジ率を読む前に `invalidTests` を解消してください。すべての行が認識されるか意図的に外されるかするまで、カバレッジの数値はディスク上のスイートより小さいスイートを表しています。

## 制約のもとでの解析

制約を持つモデルは制約込みで解析され、分母が変わります。

```typescript
const constraints = ['IF os = Windows THEN browser != Safari'];

const guarded = cw.analyzeCoverage(parameters, existingTests, 2, constraints);

guarded.totalTuples;    // 20
guarded.coveredTuples;  // 9
guarded.coverageRatio;  // 0.45 (9 of 20 pairs)
guarded.uncoveredCount; // 11
```

制約は `os=Windows, browser=Safari` を不可能にするので、このペアは必要タプル集合から外れます。21 が 20 になり、欠落一覧は 12 件から 11 件に減ります。スイートも、スイートが網羅している範囲も変わっていません。変わったのは分母だけです。

制約なしで解析すると、システムが決して生み出せないペアの分までスイートが負担させられ、カバレッジ率は 1 に到達できなくなります。

## 次に読むもの

- [不足を少しずつ埋める](close-the-gaps-incrementally.md) — このレポートを受け取り、既存の行に触れずに要求された行を追加する流れ
- [タプルとカバレッジ](../primer/tuples-and-coverage.md) — 21 という数がどこから来るかを、最初からたどったページ
- [制約構文](../constraints.md) — 式の文法と、制約が必要タプル集合から何を取り除くか
- [JavaScript API](../js-api.md) — `CoverageReport` の全体像とその他のサーフェス
- [CLI リファレンス](../cli.md) — JSON に置いたスイートに対する `coverwise analyze` での同じ解析
