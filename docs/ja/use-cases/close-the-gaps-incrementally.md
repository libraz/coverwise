# 不足を少しずつ埋める

`extendTests` は、スイートにすでにある行を受け取り、そのすべてを元のまま残し、カバレッジに足りない行だけを追加します。先に解析し、拡張し、もう一度解析します。3 つの手順すべてに同じモデルが答え、3 番目の手順が 2 番目の結果を測ります。

このページが前提とする語彙、すなわちタプル、カバレッジ率、必要タプル集合は[タプルとカバレッジ](../primer/tuples-and-coverage.md)で説明しています。カバレッジレポートを項目ごとに読むには[既存スイートを監査する](audit-an-existing-suite.md)から始めてください。

## 解析する

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const parameters = [
  { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
  { name: 'device',  values: ['phone', 'desktop'] },
];

const existing = [
  { os: 'Windows', browser: 'Chrome',  device: 'desktop' },
  { os: 'macOS',   browser: 'Safari',  device: 'phone' },
  { os: 'Linux',   browser: 'Firefox', device: 'desktop' },
];

const before = cw.analyzeCoverage(parameters, existing);

before.totalTuples;    // 21
before.coveredTuples;  // 9
before.coverageRatio;  // 0.42857142857142855 (9 of 21 pairs)
before.uncoveredCount; // 12
```

この 21 は、[ユースケース](index.md)がこのモデルについて強度 2 で導いているペア数です。3 行はそれぞれパラメータの組ごとに 1 個ずつペアに寄与し、行のあいだで重複しないため、21 個中 9 個に到達します。残る 12 個のペアが欠けています。

## 拡張する

同じモジュールの続きです。`extendTests` は既存の行とモデルを受け取り、スイート全体を返します。

```typescript
const result = cw.extendTests(existing, { parameters, mode: 'strict' });

result.tests.length; // 10
result.coverage;     // 1

const added = result.tests.slice(existing.length);
added.length;        // 7
```

既存の行は与えた順のまま `result.tests` の先頭に一字一句そのまま残ります。したがって `result.tests.slice(existing.length)` がそのまま差分であり、差分を求めるために比較する必要はありません。ファイルに加えるのは次の 7 行です。

```text
os=Windows, browser=Firefox, device=phone
os=macOS, browser=Firefox, device=desktop
os=macOS, browser=Chrome, device=phone
os=macOS, browser=Safari, device=desktop
os=Linux, browser=Chrome, device=phone
os=Windows, browser=Safari, device=phone
os=Linux, browser=Safari, device=phone
```

12 個の欠落が 7 行で埋まるのは、1 行がパラメータの組ごとに 1 個、ここでは 3 個のペアを運び、複数の欠落を同時に埋める行が選ばれるためです。

## 再解析する

```typescript
const after = cw.analyzeCoverage(parameters, result.tests);

after.totalTuples;    // 21
after.coveredTuples;  // 21
after.coverageRatio;  // 1
after.uncoveredCount; // 0
```

`analyzeCoverage` はジェネレータが記録した情報を読まず、必要タプル集合を自分で列挙します。したがって結合後のスイートに対するレポートは独立した確認になります。7 行を書き写したあと、実際にテストが書かれているファイルに対して走らせてください。書き写しの誤りもそこで見つかります。

## 拡張か、生成し直しか

拡張が行を取り除くことはありません。既存の行に冗長さがあれば、その冗長さは結果にそのまま持ち込まれ、ゼロから生成したスイートのほうが小さくなることがあります。

```typescript
const redundant = [
  { os: 'Windows', browser: 'Chrome',  device: 'desktop' },
  { os: 'Windows', browser: 'Chrome',  device: 'desktop' },
  { os: 'Windows', browser: 'Chrome',  device: 'phone' },
  { os: 'Windows', browser: 'Firefox', device: 'desktop' },
  { os: 'Windows', browser: 'Safari',  device: 'desktop' },
  { os: 'Windows', browser: 'Safari',  device: 'phone' },
];

cw.extendTests(redundant, { parameters }).tests.length; // 12
cw.generate({ parameters }).tests.length;               // 10
```

既存の行がモデルにない情報を持つときは拡張します。既知のリグレッション、手書きのアサーションを伴うシナリオ、サポートに寄せられた事例などです。こうした行はモデルからは再現できません。増える 2 行が、それらを残すための費用です。

既存の行が値以外に何も持たないときは生成し直します。もともと生成されたスイートや、全組み合わせで書いたまま見直されていないスイートがこれにあたります。モデルの形が変わったあとも生成し直すのが妥当です。古いモデルに対して書かれた行は、カバレッジではなく `invalidTests` として現れがちだからです。

## `strict` モードが守るもの

`'strict'` は唯一サポートされているモードであり、既定値でもあります。既存の行をすべて元のまま残し、追加しかしません。それ以外の値は解釈されずに不正入力として拒否されるので、モード指定の打ち間違いでスイートが黙って書き換えられることはありません。

拡張の要求として明確に拒否される形が 1 つあります。与えたスイートより小さい上限です。

```typescript
cw.extendTests(existing, { parameters, maxTests: 2 });
// CoverwiseError, code INVALID_INPUT:
// maxTests cannot be smaller than the existing test count
```

`maxTests` が制限するのは結果全体であって、追加分ではありません。3 行はどれかを捨てないかぎり上限 2 に収まりませんが、`'strict'` は行を捨てません。

## 制約のもとでの拡張

`extendTests` は `generate` と同じモデルの項目を受け取ります。制約も含まれ、追加される行はそれを守ります。

```typescript
const constraints = ['IF os = Windows THEN browser != Safari'];

const guarded = cw.extendTests(existing, { parameters, constraints, mode: 'strict' });

guarded.tests.length;      // 9
guarded.coverage;          // 1
guarded.stats.totalTuples; // 20
```

制約は `os=Windows, browser=Safari` を必要タプル集合から取り除くので、総数は 21 ではなく 20 になります。追加される行は 7 行ではなく 6 行で、9 行のどれも Windows と Safari を組み合わせません。上の制約なしの実行が追加していた `os=Windows, browser=Safari, device=phone` の行も含めてです。制約のもとで完全網羅に達するスイートは、制約なしで完全網羅に達するスイートより小さくなります。網羅すべき対象が少ないからです。

制約は、渡された行に対しては検査されません。制約に反する既存の行は結果に残ります。`'strict'` は与えられたものを残すからです。そうした行が姿を現すのは解析の手順です。

## 次に読むもの

- [既存スイートを監査する](audit-an-existing-suite.md) — このページが読むレポート項目の、`invalidTests` を含めた 1 つずつの説明
- [制約構文](../constraints.md) — 式の文法と、制約が必要タプル集合から何を取り除くか
- [決定性](../determinism.md) — 同じモデルとシードがどのサーフェスでも同じ 7 行を生む理由
- [JavaScript API](../js-api.md) — `ExtendInput` と `GenerateResult` の全体像
- [CLI リファレンス](../cli.md) — JSON で管理するスイート向けの `coverwise analyze` と `coverwise extend` による同じループ
