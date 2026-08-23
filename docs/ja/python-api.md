# Python API

## インストール

PyPI の `coverwise` パッケージは、native command-line tool と、それを駆動する薄い Python API を同梱します。ジェネレータの別の Python 実装は持たないため、結果と JSON の形は C++ CLI および JavaScript API と完全に一致します。エラー分類は CLI の終了コード契約に従うため、JavaScript 側より少しだけ粗くなります。[エラー](#エラー) を参照してください。

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

mapping の値は、1 個であっても必ず list で書きます。`{"env": "prod"}` は `p`・`r`・`o`・`d` の 4 値としてではなく `TypeError` として拒否されるので、`{"env": ["prod"]}` と書いてください。

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

パラメータ名はテスト引数になるため、Python の識別子として妥当で、かつ Python のキーワードでない名前である必要があります。引数にできない名前は、pytest が収集を始める前に `ValueError` として拒否されます。`strength`、`seed`、`maxTests`、`weights`、`subModels` といったモデルのフィールドはそのまま渡されます。`"invalid": true` を付けた値は既定で除外され、`include_negative=True` を渡すと生成された negative test も実行されます。

pytest はこのパッケージの dependency ではありません。decorator が pytest を必要とするのは、テストを実行する環境だけです。その環境にまとめて入れる場合は `pip install coverwise[pytest]` が使えます。

## 入力の上限

この API はモデルを JSON へ直列化して native の実行ファイルへ渡すため、上限は CLI のものと同じです。いずれかを超えた場合は `code == "INVALID_INPUT"`、`exit_code == 3` の `CoverwiseError` を送出します。

| 項目 | 上限 |
|------|------|
| 1 モデルのパラメータ数 | 1,024 |
| 1 パラメータの値の数 | 16,384 |
| `tests`・`seeds`・`existing` の行数 | 100,000 |
| 制約式の数 | 256 |
| 1 つの文字列の UTF-8 バイト数 | 65,536（64 KiB） |
| モデル中の文字列の合計 UTF-8 バイト数 | 1,048,576（1 MiB） |
| 直列化した 1 つの JSON 文書のバイト数 | 67,108,864（64 MiB） |

合計バイト数の対象は、モデルを記述する文字列です。パラメータ名、値、エイリアス、クラス名、制約式、サブモデルのパラメータ名が含まれます。パラメータ数の上限は、制約の充足可能性探索を有限に保つためのものです。探索は 1 階層につき 1 パラメータを進むため、探索の深さを抑えるものはパラメータ数のほかにありません。

最後の行は、実行ファイルへ渡る各文書に掛かります。標準入力へ書き込む文書と、入力を 2 つ取る呼び出しがもう一方のために書き出す一時ファイルの両方です。これは API が受け付ける入力の条件ではなくメモリ保護であり、上記の上限を満たすモデルに必要な大きさより十分に大きく取ってあります。したがって実際のモデルは先に上記のいずれかへ到達し、超えた上限そのものを理由に拒否されます。

## エラー

不正なモデルや解析できない制約は `coverwise.CoverwiseError` を送出します。

```python
try:
    coverwise.generate(MODEL, constraints=["IF os = = THEN"])
except coverwise.CoverwiseError as error:
    error.code       # "CONSTRAINT_ERROR"
    error.exit_code  # 1 (CLI の終了コード契約と同じ)
    error.stderr     # CLI の診断出力そのまま
    error.report     # 失敗前に CLI が書き出した JSON レポート、なければ None
```

`code` は `CONSTRAINT_ERROR` か `INVALID_INPUT` のどちらかで、`exit_code` は必ず [CLI リファレンス](cli.md) が定める終了コードのいずれかになります。JavaScript API にはもう 1 つ `TUPLE_EXPLOSION` という分類がありますが、CLI はそれを `INVALID_INPUT` にまとめており、このパッケージは CLI が報告したとおりを報告します。カバレッジ不足はエラーではありません。上の `generate` を参照してください。

失敗の内容がメッセージではなくレポートに書かれている場合もあります。`analyze` はスイートを測定してから、不正な行を含むことを理由に拒否するため、その行を指し示すレポートは例外に残ります。

```python
try:
    coverwise.analyze_coverage(PARAMS, tests, constraints=CONSTRAINTS)
except coverwise.CoverwiseError as error:
    for invalid in error.report["invalidTests"]:
        print(invalid["testIndex"], invalid["reason"])  # 1 violates constraint #1 ...
```

CLI が何も書き出す前に失敗した場合、`report` は `None` になります。解析できないモデルや制約がこれにあたります。

native 実行ファイルのクラッシュはモデルのエラーではないため、`CoverwiseError` にはなりません。シグナル名または終了ステータスを示す素の `RuntimeError` が送出されるので、segfault が入力エラーとして提示されることはありません。

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

`coverwise.run()` は keyword 引数をそのまま `subprocess.run` に渡すため、キャプチャした出力が text と bytes のどちらになるかを決めるのは `subprocess.run` です。`stdout` が `str` になるのは `text=True` を渡した形だけです。

`COVERWISE_BINARY` を設定すると、どちらも別の実行ファイルを指します。ローカルビルドした CLI で開発するための仕組みで、インストール済み wheel では不要です。

## 型検査

パッケージは注釈をインラインで同梱し、PEP 561 のマーカーも持ちます。そのため mypy や pyright は、stub パッケージを追加せずに実際のシグネチャで呼び出しを検査します。
