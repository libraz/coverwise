# Python API

## インストール

PyPI の `coverwise` パッケージは、native command-line tool と、それを駆動する薄い Python API を同梱します。ジェネレータの別の Python 実装は持たないため、結果・JSON の形・エラー分類は C++ CLI および JavaScript API と完全に一致します。

```bash
pip install coverwise
coverwise --help
```

対応 wheel は Linux x86_64、Linux aarch64、macOS 14 以降の Apple Silicon です。パッケージは移植可能な Python コードではなくビルド済み実行ファイルを同梱するため、それ以外の環境ではソースから CLI をビルドしてください。runtime の Python dependency はありません。`python -m coverwise` も同じ command を実行します。

## クイックスタート

```python
import coverwise

result = coverwise.generate(
    parameters={
        "os": ["Windows", "macOS", "Linux"],
        "browser": ["Chrome", "Firefox", "Safari"],
    },
    constraints=["IF os = Windows THEN browser != Safari"],
    strength=2,
)

print(result["coverage"])  # 1
for test in result["tests"]:
    print(test)  # {"os": "Linux", "browser": "Firefox"}, ...
```

どの関数も、素の dict と list をやり取りします。中身は [CLI リファレンス](cli.md) に載っている JSON と同じで、自前の serialize や parse は要りません。数値は `json` の decode 結果そのままなので、カバレッジ 100% のような整数になる比率は `1.0` ではなく `1` として返ります。`1.0` との比較はそのまま成立します。

## モデルの入力

`parameters` は JSON の list 形式と、名前から値への mapping 形式のどちらでも受け取ります。

```python
# mapping 形式: 簡潔で、宣言順もそのまま保たれる
coverwise.generate(parameters={"os": ["win", "mac"]})

# list 形式: invalid 値や alias など値ごとのオプションを指定する場合に使う
coverwise.generate(
    parameters=[
        {"name": "os", "values": ["win", "mac"]},
        {"name": "browser", "values": ["Chrome", {"value": "IE", "invalid": True}]},
    ]
)
```

モデルのフィールドは keyword 引数でも、1 つの mapping でも、両方の併用でも渡せます。keyword 引数が mapping を上書きするため、保存済みモデルの再利用が簡単になります。

```python
MODEL = {"parameters": {"os": ["win", "mac"], "browser": ["Chrome", "Firefox"]}}

pairwise = coverwise.generate(MODEL)
three_wise = coverwise.generate(MODEL, strength=3)
```

## 関数

### `generate(model=None, **fields)`

covering test suite を生成します。戻り値は `generate` の結果、つまり `tests`、`coverage`、`uncovered`、`stats` と、CLI が定める残りのフィールドです。

`maxTests` などで 100% に到達できない場合も、例外ではなく `coverage` が 1.0 未満の結果として返るため、部分的な結果をそのまま調べられます。

```python
result = coverwise.generate(MODEL, maxTests=3)
if result["coverage"] < 1.0:
    for missing in result["uncovered"]:
        print(missing["display"])  # "os=mac, browser=Firefox"
```

### `analyze_coverage(parameters, tests, strength=2, constraints=None)`

既にあるテストスイートの t-wise カバレッジを測ります。手書きのテスト、別ツールが出したスイート、以前の `generate` 結果のいずれにも使えます。テストケースの list と `generate` 結果そのものの両方を受け取ります。

```python
report = coverwise.analyze_coverage(
    {"os": ["win", "mac"], "browser": ["Chrome", "Safari"]},
    [{"os": "win", "browser": "Chrome"}],
)

report["coverageRatio"]  # 0.25
[item["display"] for item in report["uncovered"]]  # ["os=win, browser=Safari", ...]
```

### `extend_tests(existing, model=None, **fields)`

モデルを網羅するまで既存スイートを拡張します。既存テストはそのまま維持され、戻り値の `tests` の先頭に並ぶため、記録済みの実行結果が無効になりません。

```python
result = coverwise.extend_tests(previous["tests"], MODEL)
added = result["tests"][len(previous["tests"]) :]
```

### `estimate_model(model=None, **fields)`

生成せずにモデルの統計だけを返します。パラメータ数と値の数、制約適用前の tuple 数、推定スイートサイズが得られます。制約の構文とパラメータ参照も検証されるので、安価な事前チェックとして使えます。

```python
stats = coverwise.estimate_model(MODEL, strength=3)
stats["totalTuples"], stats["estimatedTests"]
```

## pytest との連携

`coverwise.parametrize` は、手書きの全組み合わせを生成済みの t-wise スイートに置き換えます。各パラメータは同名のテスト引数になります。

```python
import coverwise

@coverwise.parametrize(
    {
        "os": ["Windows", "macOS", "Linux"],
        "browser": ["Chrome", "Firefox", "Safari"],
        "locale": ["en", "ja"],
    },
    constraints=["IF os = Windows THEN browser != Safari"],
)
def test_login(os, browser, locale):
    assert login(os, browser, locale).ok
```

上の全組み合わせは 18 ケースですが、pairwise スイートはすべてのペアをはるかに少ないケースで網羅します。各ケースには読みやすい id (`os=macOS-browser=Chrome-locale=ja`) が付きます。

パラメータ名はテスト引数になるため、Python の識別子として妥当な名前である必要があります。`strength`、`seed`、`maxTests`、`weights`、`subModels` といったモデルのフィールドはそのまま渡されます。`"invalid": true` を付けた値は既定で除外され、`include_negative=True` を渡すと生成された negative test も実行されます。

pytest はこのパッケージの dependency ではありません。decorator が pytest を必要とするのは、テストを実行する環境だけです。その環境にまとめて入れる場合は `pip install coverwise[pytest]` が使えます。

## エラー

不正なモデルや解析できない制約は `coverwise.CoverwiseError` を送出します。

```python
try:
    coverwise.generate(MODEL, constraints=["IF os = = THEN"])
except coverwise.CoverwiseError as error:
    error.code       # "CONSTRAINT_ERROR"
    error.exit_code  # 1 (CLI の終了コード契約と同じ)
    error.stderr     # CLI の診断出力そのまま
```

`code` は JavaScript API と同じ語彙 (`CONSTRAINT_ERROR`、`INVALID_INPUT`) です。カバレッジ不足はエラーではありません。上の `generate` を参照してください。

## Command-line interface

インストールされる command は、API が駆動しているものと同じ実行ファイルです。

```bash
coverwise generate input.json > tests.json
coverwise analyze --params params.json --tests tests.json
coverwise extend --existing tests.json input.json > updated-tests.json
coverwise stats input.json
```

入力 schema、出力 schema、終了コードは [CLI リファレンス](cli.md) を参照してください。

## 実行ファイルを直接呼ぶ

`coverwise.run()` は CLI へそのまま渡す引数を受け取り、標準の `subprocess.CompletedProcess` を返します。プロセス生成を自分で管理する integration では、`coverwise.native_binary()` で同梱 executable の path を取得できます。

```python
result = coverwise.run(["generate", "input.json"], text=True, capture_output=True, check=True)
print(result.stdout)
```

`COVERWISE_BINARY` を設定すると、どちらも別の実行ファイルを指します。ローカルビルドした CLI で開発するための仕組みで、インストール済み wheel では不要です。
