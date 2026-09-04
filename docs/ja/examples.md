# 実例集

機能ごとに 1 つずつ、エンジンが実際に返す数値を添えたレシピを並べます。アプリケーションの流れを紹介するページではありません。手元のスイートを起点に最後まで通す例は[ユースケース](use-cases/index.md)にあり、ここで使う語彙（タプル、カバレッジ単位、強度）は[入門](primer/index.md)で扱っています。

以下の TypeScript のレシピはすべて断片で、1 つの共通の準備コードを再利用します。同じモジュールに次のコードを置き、その後で各レシピを実行してください。Python のレシピはそれぞれ単体で完結しており、import を毎回書いています。

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();
```

## 基本的なペアワイズ生成

最も一般的なケースです。異なる 2 つのパラメータから取った値のペアが、すべて少なくとも 1 行に現れます。

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',       values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser',  values: ['Chrome', 'Firefox', 'Safari', 'Edge'] },
    { name: 'language', values: ['en', 'ja', 'de'] },
    { name: 'theme',    values: ['light', 'dark'] },
  ],
});

result.stats.totalTuples;  // 53 pairs: 12 + 9 + 6 + 12 + 8 + 6 over the six parameter pairs
result.tests.length;       // 15 rows, against a cross-product of 3 * 4 * 3 * 2 = 72
result.coverage;           // 1
```

## 無効な組み合わせの除外

制約は、あり得ない行をスイートから締め出します。同時に、網羅すべき対象そのものも縮めます。有効な行が 1 つも持てないペアは、不足ではないからです。

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'iOS', 'Android'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari', 'Edge'] },
    { name: 'device',  values: ['desktop', 'tablet', 'phone'] },
  ],
  constraints: [
    'IF os = iOS THEN browser = Safari',
    'IF device = phone THEN os IN {iOS, Android}',
  ],
});

result.stats.totalTuples;  // 35 required pairs, down from 40 without the constraints
result.tests.length;       // 17 rows
result.coverage;           // 1
```

必要タプル集合から外れたのは 5 ペアです。`os=iOS` と Chrome・Firefox・Edge の組、および `device=phone` と Windows・macOS の組です。式の書き方は[制約構文](constraints.md)を、この 5 つが未網羅として報告されるのではなく除外される理由は[制約と必要タプル集合](primer/constraints-and-the-universe.md)を参照してください。

## 異常系テスト

値に `invalid` を付けると、coverwise はそこから 2 つ目のスイートを組み立てます。異常系の各行は無効値をちょうど 1 つだけ含むため、拒否された原因をその値 1 つに帰せます。

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

result.tests.length;                   // 3 positive rows
result.stats.totalTuples;              // 7 positive pairs: 1 + 3 + 3 over the valid values only
result.negativeTests.length;           // 12 negative rows
result.negativeCoverage?.totalTuples;  // 16 negative tuples, each one invalid value beside one valid one
```

無効値は正常系のカバレッジには数えません。ここでの正常系の対象が、値リスト全体から出る 27 ペアではなく 7 ペアなのはそのためです。

## 重要なグループだけ強度を上げる

`subModels` は、指定したグループの強度だけを上げます。モデル全体でその強度を払う必要はありません。

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
  subModels: [
    { parameters: ['protocol', 'auth', 'cache'], strength: 3 },
  ],
});

result.stats.totalTuples;  // 138: the 120 pairs of the whole model plus 18 triples over the group
result.tests.length;       // 18 rows, where the same model at plain pairwise needs 14
```

18 個の 3 つ組はグループの全組み合わせ（プロトコル 3 種、認証 3 種、キャッシュ 2 種）そのものなので、これだけでスイートの行数に 18 の下限が生まれます。残りはその 18 行の中で網羅されます。

## 等価クラス

複数の値が同じコードパスを通ると分かっていて、関心のあるペアが値と値の間ではなくクラスとクラスの間にあるときに、クラス単位のカバレッジを使います。値単位のカバレッジのほうが厳しい目標であり、実際の挙動の違いに見合わないコストになることがあります。

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

result.stats.totalTuples;                  // 20 value pairs, from 5 ages and 4 plans
result.tests.length;                       // 20 rows, because value-level pairwise is the cross-product here
result.classCoverage?.totalClassTuples;    // 6 class pairs, from 3 age classes and 2 plan classes
result.classCoverage?.classCoverageRatio;  // 1
```

値のペア 20 に対してクラスのペアは 6 で、これが判断の大きさです。`classCoverage` は両方を報告するので、システムが実際に区別している水準に合わせてスイートを評価できます。

## 重み付けヒント

重みは同点を破るためのものです。複数の値が同じ数の不足を埋められるとき、重みが大きい値が選ばれやすくなります。スイートに必要な行数は変わりません。

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari', 'Edge'] },
    { name: 'theme',   values: ['light', 'dark'] },
  ],
  weights: {
    os: { Windows: 3.0, macOS: 1.0, Linux: 1.0 },
    browser: { Chrome: 2.0 },
  },
});

result.stats.totalTuples;  // 26 pairs: 12 + 6 + 8
result.tests.length;       // 12 rows, the same count the model produces with no weights at all
```

## シードテスト

`seeds` は、出力に必ず含める行です。ジェネレータはその順序のまま保持し、残りの不足をその周りで埋めます。

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'env',     values: ['staging', 'production'] },
  ],
  seeds: [
    { os: 'Windows', browser: 'Chrome', env: 'production' },
    { os: 'macOS',   browser: 'Safari', env: 'production' },
  ],
});

result.tests.slice(0, 2);  // the two seed rows, in the order they were given
result.stats.totalTuples;  // 21 pairs: 9 + 6 + 6
result.tests.length;       // 9 rows, where the same model without seeds produces 10
```

シードは、これから生成するモデルに属するものです。既存のスイートをそのまま残し、不足を埋める行だけを足したい場合は `extendTests` を使います。その流れは[不足を少しずつ埋める](use-cases/close-the-gaps-incrementally.md)で最後まで通しています。

## 境界値展開

数値範囲は、各端で試す価値のある値へ展開されます。端の 1 つ外側、端そのもの、端の 1 つ内側の 3 つです。

```typescript
const result = cw.generate({
  parameters: [
    { name: 'port',    values: [], type: 'integer', range: [1, 65535], step: 1 },
    { name: 'timeout', values: [], type: 'float',   range: [0.1, 30.0], step: 0.1 },
  ],
});

result.stats.totalTuples;  // 36 pairs: each range expands to 6 values, so 6 * 6
result.tests.length;       // 36 rows
```

`port` は `0`、`1`、`2`、`65534`、`65535`、`65536` に、`timeout` は `0`、`0.1`、`0.2`、`29.9`、`30`、`30.1` に展開されます。境界値パラメータでも `values` は宣言します。そこに書いた値は置き換えられるのではなく、展開後の集合にマージされます。整数の範囲では `step: 1` しか受け付けません。同じ 4 つのフィールドは CLI のモデル文書でもパラメータの下に置きます。[CLI リファレンス](cli.md)を参照してください。

## モデル推定

`estimateModel` は、スイートを作らずにモデルの規模を見積もります。

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

stats.parameterCount;  // 4
stats.totalTuples;     // 500: 4 parameter triples, each with 5 * 5 * 5 = 125 value combinations
stats.estimatedTests;  // 250
```

このモデルを実際に生成すると 165 行になります。`estimatedTests` は規模の目安であり、上限でも下限でもありません。ここでは過大に、別のモデルでは過小に出ます。コストの判断には `stats.totalTuples` を、結果の判断には生成後のカバレッジを使ってください。

## Python

上のモデルフィールドは Python でも同じで、`coverwise.generate` にキーワード引数として渡すか、1 つのマッピングとして渡します。ここに挙げる 2 つのレシピは、モデルではなくテストスイートの形に沿ったものです。完全なリファレンスは[Python API](python-api.md)を、パラメータ化済みスイートの移行は[pytest の全組み合わせを置き換える](use-cases/replace-a-cross-product-in-pytest.md)を参照してください。

### 正常系と異常系を別のテストに分ける

異常系の行は無効値を 1 つずつ含みます。期待結果が逆になるため、2 つのテストに分けるのが自然です。

```python
import coverwise
import pytest

LOGIN_MODEL = {
    "parameters": [
        {"name": "email", "values": [
            "user@example.com",
            {"value": "", "invalid": True},
            {"value": "not-an-email", "invalid": True},
        ]},
        {"name": "password", "values": [
            "Str0ng!Pass",
            {"value": "", "invalid": True},
            {"value": "short", "invalid": True},
        ]},
        {"name": "role", "values": ["admin", "user", "guest"]},
    ]
}
_suite = coverwise.generate(LOGIN_MODEL)
# _suite["tests"] holds 3 rows; _suite["negativeTests"] holds 12.


@pytest.mark.parametrize("case", _suite["tests"])
def test_login_accepts_valid_input(case):
    assert login(**case).ok


@pytest.mark.parametrize("case", _suite["negativeTests"])
def test_login_rejects_invalid_input(case):
    with pytest.raises(ValidationError):
        login(**case)
```

行全体を 1 つの `case` 引数として渡しておくと、モデルにパラメータが増えても両方のテストがそのまま動きます。`coverwise.parametrize(..., include_negative=True)` を使えば、2 つのスイートを 1 つのテストで実行できます。こちらは、受け取った値から期待結果を自分で判断するテストに向きます。

### デコレータ経由のサブモデル

`subModels` を含め、モデルのフィールドはそのままデコレータに渡せます。

```python
import coverwise


@coverwise.parametrize(
    {
        "protocol": ["HTTP/1.1", "HTTP/2", "HTTP/3"],
        "auth": ["none", "basic", "oauth"],
        "cache": ["enabled", "disabled"],
        "region": ["us", "eu", "ap"],
    },
    subModels=[{"parameters": ["protocol", "auth", "cache"], "strength": 3}],
)
def test_request_path(protocol, auth, cache, region):
    assert request(protocol, auth, cache, region).ok
```

対象は 63 タプルです。4 パラメータにまたがる 45 ペアと、ネットワーク周りのグループの 18 個の 3 つ組です。実行は 18 ケースで、全組み合わせの 54 ケースに対する数字です。グループの 18 通りが下限を決め、`region` はその中で網羅されます。

## CI でのカバレッジゲート

CLI はカバレッジを終了コードで報告します。追加のスクリプトを書かずに、不足でジョブを失敗させられます。

```bash
# 0 when the hand-written suite covers every pair, 2 when it does not.
coverwise analyze --params params.json --tests tests.json

# 0 at full coverage, 2 when a maxTests ceiling stopped generation short.
coverwise generate input.json > tests.json
```

終了コード 3 は入力が拒否されたことを表します。`analyze` では、パラメータモデルに書かれていないテスト行があった場合もこれに含まれます。全コードは[CLI リファレンス](cli.md)にあります。ここで出るレポートを読んで次に何をするかを決める流れは[既存スイートを監査する](use-cases/audit-an-existing-suite.md)にあります。

```yaml
- uses: actions/setup-python@v7
  with:
    python-version: '3.12'
- name: Install coverwise
  run: pip install coverwise
- name: Check pairwise coverage
  run: coverwise analyze --params params.json --tests tests.json
```

## 次に読むもの

- [ユースケース](use-cases/index.md) — 手元のスイートや全組み合わせを起点に、最後まで通す流れ
- [制約構文](constraints.md) — `constraints` フィールドの背後にある式の言語
- [決定性](determinism.md) — `seed` が保証すること、それがどのサーフェスに及ぶか
- [JavaScript API](js-api.md) — これらのレシピが触れるフィールドと結果の型
- [Python API](python-api.md) — 同じモデルを Python から扱う方法と `parametrize`
- [FAQ と制限](faq.md) — 理論的な最小値よりスイートが大きい理由と、`maxTests` が効いたときの挙動
