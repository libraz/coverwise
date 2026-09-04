# 制約と必要タプル集合

**制約**とは、モデルのパラメータと値についての真偽式で、どの組み合わせが現実には存在しないかを述べるものです。Safari は Windows では動きませんし、ARM32 のビルドに macOS のホストはありません。制約は 2 つのことを同時に行います。この 2 つを分けて理解することがこのページの目的です。制約は、どのテストケースが作られるかを決め、同時に、どのタプルをスイートが網羅すべきかを決めます。このページは[タプルとカバレッジ](tuples-and-coverage.md)の数え方を前提とします。

## 制約による構成の枝刈り

![1 つの値の格子に 3 種類の印を付けた図。禁止領域、構成中に枝刈りされる部分割り当て、そして必要タプル集合から外れるタプル](../../images/constraint-pruning-and-exclusion-ja.svg)

制約が効くのは構成の最中であって、構成のあとではありません。ジェネレータは組み立て途中の部分割り当てに対して式を評価し、もう満たせなくなった枝を打ち切ります。違反する行はそもそも最後まで作られません。生成してから絞り込む工程はなく、不正な行が出力に届いて捨てられることもありません。

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
  ],
  constraints: ['IF os = Windows THEN browser != Safari'],
});

result.tests.length; // 8
result.coverage; // 1

for (const test of result.tests) {
  console.log(test);
}
// { os: 'macOS', browser: 'Firefox' }
// { os: 'macOS', browser: 'Safari' }
// { os: 'Linux', browser: 'Firefox' }
// { os: 'macOS', browser: 'Chrome' }
// { os: 'Linux', browser: 'Chrome' }
// { os: 'Windows', browser: 'Firefox' }
// { os: 'Windows', browser: 'Chrome' }
// { os: 'Linux', browser: 'Safari' }
```

Windows と Safari を組にした行はなく、それでいてカバレッジは 1 と報告されます。どちらも同じ判断から出てくる結果ですが、2 つめのほうには説明が要ります。モデルの値ペアは 9 通り、スイートが持つのは 8 通り、それでレポートは完全だと言っているからです。

## 同じ制約による、要求されるものの縮小

`(os=Windows, browser=Safari)` というペアは、妥当などのテストケースにも現れません。これを要求するということは、どのスイートも決して含めないものを要求するということです。100% のカバレッジは定義上到達不能になり、どのレポートにも、行をいくら足しても埋まらない不足が恒久的に残ります。

そこでこのペアは**必要タプル集合**から外れます。あるタプルが除外されるのは、残りのパラメータに妥当な値をどう割り当てても、すべての制約を満たすテストケースに完成させられないとき、ちょうどそのときです。これはスイートについてではなくタプルについての判断であり、行が 1 つも存在しない段階で下されます。

手で書いた 1 つのスイートを、同じモデルに対して制約なしと制約ありの 2 回測ると、集合の変化だけが取り出せます。まず制約なしです。

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const parameters = [
  { name: 'os', values: ['Windows', 'macOS', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
];

const tests = [
  { os: 'Windows', browser: 'Chrome' },
  { os: 'Windows', browser: 'Firefox' },
  { os: 'macOS', browser: 'Firefox' },
  { os: 'macOS', browser: 'Safari' },
  { os: 'Linux', browser: 'Chrome' },
  { os: 'Linux', browser: 'Firefox' },
  { os: 'Linux', browser: 'Safari' },
];

const report = cw.analyzeCoverage(parameters, tests);

report.totalTuples; // 9
report.coverageRatio; // 0.7777777777777778 (7 of 9 pairs covered)
report.uncoveredCount; // 2 — os=Windows/browser=Safari and os=macOS/browser=Chrome
```

次に、同じ 7 行に対して制約ありです。

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const parameters = [
  { name: 'os', values: ['Windows', 'macOS', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
];

const tests = [
  { os: 'Windows', browser: 'Chrome' },
  { os: 'Windows', browser: 'Firefox' },
  { os: 'macOS', browser: 'Firefox' },
  { os: 'macOS', browser: 'Safari' },
  { os: 'Linux', browser: 'Chrome' },
  { os: 'Linux', browser: 'Firefox' },
  { os: 'Linux', browser: 'Safari' },
];

const constraints = ['IF os = Windows THEN browser != Safari'];

const report = cw.analyzeCoverage(parameters, tests, 2, constraints);

report.totalTuples; // 8
report.coverageRatio; // 0.875 (7 of 8 pairs covered)
report.uncoveredCount; // 1 — os=macOS/browser=Chrome
```

スイートは変わっていません。変わったのは分母で、9 から 8 になりました。そして欠けていた 2 つのペアのうち 1 つは、未網羅ではなくなりました。要求されなくなったからです。

## 除外と未網羅の違い

どちらもスイートが含んでいないペアですが、意味は正反対です。除外されたタプルは、存在しえないから無いのです。**未網羅タプル**は、スイートに穴があるから無いのです。

| レポートの読み方 | 除外されたタプル | 未網羅タプル |
|---|---|---|
| 無い理由 | 妥当なテストケースが持ちえない | 持ちうる行がすべて漏れている |
| `totalTuples` に数えるか | 数えません | 数えます |
| `uncovered` に載るか | 載りません | 載ります |
| `coverageRatio` への影響 | ありません。分母に入っていません | 下げます |
| 対処 | 制約自体が誤っていない限り不要です | 行を足すか、`extend` で追加します |

実務上の帰結は、`uncovered` が本物の穴だけの一覧になるということです。フィルタせずに作業リストとして読めます。一覧に載っていないペアは、制約が除外したペアです。それが想定外なら、読み直すべきはスイートではなく制約のほうです。

これは生成が途中で止まった場合も変わりません。`maxTests` の上限は単位を未網羅のまま残しますが、そこに報告されるのは実行が到達しなかった必要な単位だけで、制約が取り除いた単位が混ざることはありません。

同じ区別はあと 2 か所に現れます。`invalid` と印を付けた値を含むタプルも、正常系のカバレッジからは同じように除外されます。正常系の行はそうした値を持たないからです。もう 1 つ、生成前に取るモデル規模の見積もり（`estimateModel` や `coverwise stats`）は、制約を差し引かない素の集合を報告します。見積もりはモデルを解かないと決めているからです。上の制約付きモデルでは、生成結果が 8 と答えるところを見積もりは 9 と答えます。必要タプル集合は、実際に実行した操作の結果に入っている数のほうです。

## 制約がすべてを除外する場合

妥当な完全割り当てが 1 つも残らない制約は、空のスイートではなくエラーになります。生成は `Constraints are unsatisfiable` を報告し、CLI は終了コード 1 で終わります。これは終了コード 2 のカバレッジ不足とは別ものです。充足不能なモデルには網羅すべきものが無く、カバレッジ不足のモデルには誰も到達しなかった単位があります。

原因はたいてい、個々には妥当なのに同時には成り立たない 2 つの制約です。対処はたいてい、実システムを正しく写している制約を弱めることではなく、パラメータの値を広げることです。式の言語そのもの、演算子や `IN` と `LIKE` の書き方、JavaScript のビルダーについては[制約構文](../constraints.md)がリファレンスです。

## 次に読むもの

- [制約構文](../constraints.md) — 式の完全な構文、ビルダー API、クォートの規則
- [タプルとカバレッジ](tuples-and-coverage.md) — このページが分母を変えた、その数え方
- [ユースケース](../use-cases/index.md) — すでにあるスイートに対して `uncovered` を作業リストとして読む流れ
- [用語集](../glossary.md) — 制約、制約枝刈り、総タプル数、未網羅タプルの、それ単体での定義
