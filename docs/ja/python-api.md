# Python API

PyPI の `coverwise` パッケージのリファレンスです。素の `dict` を扱う 4 つの関数、pytest 用のデコレータ、そしてそれらが駆動するネイティブの実行ファイルを扱います。Python から coverwise を動かす読者に向けたページで、語彙は[タプルとカバレッジ](primer/tuples-and-coverage.md)と[強度](primer/strength.md)のものを前提とし、ここでは再説明しません。パッケージの導入と最初のスイート生成は[はじめかた](getting-started.md)にあります。

パッケージにはネイティブのコマンドラインツールと、それを駆動する薄い Python 層が入っています。ジェネレータの別実装は持たないため、JSON の形も、生成されるスイートも、エラーの分類も CLI のものそのままです。

JavaScript API は同じフィールドを同じ名前・同じ値で返しますが、1 つだけ例外があります。CLI がすべての文書を包むバージョンエンベロープ `schemaVersion` は CLI のフィールドで、JavaScript の型には存在しません。そのため JavaScript の結果を文字列化して、スイートを受け取る CLI コマンドへ渡すと、入力不正として拒否されます。

## インストール

```bash
pip install coverwise
```

このパッケージは Python 3.10 以上を必要とし、その下限を `requires-python` として宣言しています。そのため古いインタプリタでは import 時ではなく pip の段階で拒否されます。実行時に必要な Python の依存パッケージはありません。

パッケージは移植可能な Python コードではなくビルド済みの実行ファイルを同梱するため、wheel はプラットフォームごとに分かれています。

| プラットフォーム | wheel | 必要条件 |
|---|---|---|
| Linux x86_64 | `manylinux_2_28_x86_64` | glibc 2.28 以上 |
| Linux aarch64 | `manylinux_2_28_aarch64` | glibc 2.28 以上 |
| macOS Apple Silicon | `macosx_14_0_arm64` | macOS 14 以上 |

glibc の下限により、その線より古いディストリビューション、たとえば CentOS 7・Ubuntu 18.04・Debian 9 は x86_64 であっても対象外です。wheel のないプラットフォームでは、CLI をソースからビルドして `COVERWISE_BINARY` で指すか、[C++ API](cpp-api.md)を直接使ってください。

追加インストール（extras）は 2 つあります。`pip install coverwise[pytest]` は pytest を追加します。`parametrize` はテストを実行する環境で pytest を必要とします。`pip install coverwise[dev]` はこのパッケージの開発に使うツール一式、build・mypy・pytest・ruff・wheel を追加します。利用するだけであれば不要です。

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
    print(test)  # {"os": "macOS", "browser": "Firefox"}, ...
```

どの関数も、素の `dict` と `list` をやり取りします。中身は[CLI リファレンス](cli.md)に載っている JSON と同じで、自前で直列化したりパースしたりする必要はありません。数値は `json` モジュールがデコードしたそのままなので、カバレッジ 100% のような整数になる比率は `1.0` ではなく `1` として返ります。`1.0` との比較はそのまま成立します。

## モデルの入力

`parameters` は JSON と同じ `list` 形式と、名前から値へのマッピング形式のどちらでも受け取ります。

```python
import coverwise

# Mapping form: concise, and preserves declaration order.
coverwise.generate(parameters={"os": ["win", "mac"]})

# List form: required for per-value options such as invalid values or aliases.
coverwise.generate(
    parameters=[
        {"name": "os", "values": ["win", "mac"]},
        {"name": "browser", "values": ["Chrome", {"value": "IE", "invalid": True}]},
    ]
)
```

マッピングの値は、1 個であっても必ず `list` で書きます。`{"env": "prod"}` は `p`・`r`・`o`・`d` の 4 値としてではなく `TypeError` として拒否されるので、`{"env": ["prod"]}` と書いてください。

値のコンテナが来るべき場所に `set` や `frozenset` を渡すと、書き直し方を示す `TypeError` で拒否されます。`set` の反復順は `PYTHONHASHSEED` と挿入履歴に依存するため、同じ呼び出しが実行のたびに違うモデルを表すことになり、この API の他の部分が保証している決定性が崩れるからです。

```python
import coverwise

coverwise.generate(parameters={"os": {"win", "mac"}})            # TypeError
coverwise.generate(parameters={"os": sorted({"win", "mac"})})    # fine
coverwise.generate(parameters={"os": list(some_set)})            # fine
```

挿入順で反復するコンテナは再現可能なので受け付けます。`dict.keys()` や `dict.items()` はそのまま渡せます。失格の理由はコンテナが満たすプロトコルではなく、順序が定まらないことです。この規則は `parameters` の 2 形式の両方に適用され、`constraints`・`seeds`・テスト行など、コンテナがモデルに入り込む他のすべての場所でも同じです。`generate`・`extend_tests`・`estimate_model`・`analyze_coverage`・`parametrize` のいずれでも、同じ規則が同じ文面で適用されます。

モデルのフィールドはキーワード引数でも、1 つのマッピングでも、両方の併用でも渡せます。キーワード引数がマッピングを上書きするため、保存済みモデルの再利用が簡単になります。

```python
import coverwise

MODEL = {"parameters": {"os": ["win", "mac"], "browser": ["Chrome", "Firefox"]}}

pairwise = coverwise.generate(MODEL)
three_wise = coverwise.generate(MODEL, strength=3)
```

以降の関数の例が使い回すモデルは、上の `MODEL` です。各ブロックは import を毎回書いており、`MODEL` 以外は単体で完結しています。同じモジュールで `MODEL` を 1 度束縛し、どの例もその後ろに置いて実行してください。

## 関数

### `generate(model=None, **fields)`

カバリング配列となるテストスイートを生成します。戻り値は `generate` の結果、つまり `tests`・`coverage`・`uncovered`・`stats` と、CLI が定める残りのフィールドです。

`maxTests` などで 100% に到達できない場合も、例外ではなく `coverage` が 1.0 未満の結果として返るため、部分的な結果をそのまま調べられます。

```python
import coverwise

result = coverwise.generate(MODEL, maxTests=3)
if result["coverage"] < 1.0:
    for missing in result["uncovered"]:
        print(missing["display"])  # "os=mac, browser=Firefox"
```

### `analyze_coverage(parameters, tests, strength=2, constraints=None)`

既にあるテストスイートの t-wise カバレッジを測ります。手書きのテスト、別ツールが出したスイート、以前の `generate` 結果のいずれにも使えます。`tests` はテストケースのリストと `generate` 結果そのものの両方を受け取ります。

```python
import coverwise

report = coverwise.analyze_coverage(
    {"os": ["win", "mac"], "browser": ["Chrome", "Safari"]},
    [{"os": "win", "browser": "Chrome"}],
)

report["coverageRatio"]  # 0.25
[item["display"] for item in report["uncovered"]]  # ["os=win, browser=Safari", ...]
```

CLI の `analyze` にできて、この関数からは到達できないことが 2 つあります。`parameters` は名前から値へのマッピングかパラメータの `list` 形式であり、モデルオブジェクトそのものではありません。`{"parameters": [...], "strength": 3}` を渡すと `strength` をパラメータ名として読み、`TypeError` になります。もう 1 つは強度で、この関数は `strength` を常に CLI へ明示的に渡します。CLI が説明する「`--params` のモデルオブジェクトから強度を受け継ぐ」挙動はここでは起こりません。強度は引数として渡してください。

### `extend_tests(existing, model=None, **fields)`

モデルを網羅するまで既存スイートを拡張します。既存テストはそのまま維持され、戻り値の `tests` の先頭に並ぶため、記録済みの実行結果が無効になりません。

```python
import coverwise

previous = coverwise.generate(MODEL, maxTests=2)
result = coverwise.extend_tests(previous["tests"], MODEL)
added = result["tests"][len(previous["tests"]) :]
```

### `estimate_model(model=None, **fields)`

生成せずにモデルの統計だけを返します。パラメータ数と値の数、制約適用前のタプル数、推定スイートサイズが得られます。制約の構文とパラメータ参照も検証されるので、安価な事前チェックとして使えます。

`estimatedTests` は、最大値数・強度・パラメータ数から求めて `totalTuples` で頭打ちにした、大まかな見積もりです。上限でも下限でもなく、生成されるスイートはこれを下回ることも、上回ることもあります。

```python
import coverwise

stats = coverwise.estimate_model(MODEL, strength=3)
stats["totalTuples"], stats["estimatedTests"]
```

### `parametrize(parameters=None, *, include_negative=False, **fields)`

手書きの全組み合わせを、生成済みの t-wise スイートに置き換えます。戻り値は `pytest.mark.parametrize` のマーカーで、各パラメータは同名のテスト引数になります。

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

上の全組み合わせは 3 × 3 × 2 = 18 ケースですが、制約付きのペアワイズスイートは実現可能なペアすべてを 9 ケースで網羅します。各ケースには読みやすい id（`os=macOS-browser=Chrome-locale=en`）が付きます。

`parameters` は `generate` と同じ 2 つの形式を受け取ります。既定が `None` なのは、省略したときの失敗をはっきりさせるためだけです。省略すると `TypeError: a model requires 'parameters'` になります。`include_negative` はシグネチャの `*` によりキーワード専用なので、2 つ目の位置引数のモデルと取り違えることはありません。`include_negative=True` を渡すと、`"invalid": true` を付けた値を使う異常系テストも実行します。既定では除外されます。

これ以外のキーワード引数はすべてモデルのフィールドとして `generate` へそのまま渡されます。対象は `strength`・`seed`・`maxTests`・`weights`・`subModels`、そして `constraints` と `seeds` です。`parametrize` が組み立てるのは `generate` が受け取るのと同じモデル文書であり、そこに何かを足すことはありません。

パラメータ名はテスト引数になるため、Python の識別子として妥当で、かつ Python のキーワードでない名前である必要があります。引数にできない名前は、pytest が収集を始める前に `ValueError` として拒否されます。

パラメータが 1 個のモデルは特別に扱われます。pytest 側とエンジン側の両方がそれを必要とするからです。

```python
import coverwise

@coverwise.parametrize({"os": ["Windows", "macOS", "Linux"]})
def test_boot(os):
    assert boot(os).ok
```

パラメータが 1 個のとき `strength` の既定は 2 ではなく 1 になります。既定のままではパラメータ数を超えてしまい、モデルが拒否されるためです。またテストが受け取るのは素の値で、`os` は要素 1 個のタプルではなく文字列 `"Windows"` です。pytest がケースごとにタプルを展開するのは、引数名が複数与えられたときだけだからです。`**fields` で `strength` を明示した場合はそちらが優先されます。

pytest はこのパッケージの依存パッケージではありません。デコレータが pytest を必要とするのは、テストを実行する環境だけです。pytest を入れずに `parametrize` を呼ぶと `RuntimeError: coverwise.parametrize requires pytest; install it to use this decorator` になります。同じ環境にまとめて入れる場合は `pip install coverwise[pytest]` が使えます。

## エラー

不正なモデルや解析できない制約は `coverwise.CoverwiseError` を送出します。

```python
import coverwise

try:
    coverwise.generate(MODEL, constraints=["IF os = = THEN"])
except coverwise.CoverwiseError as error:
    error.code       # "CONSTRAINT_ERROR"
    error.exit_code  # 1, matching the CLI exit-code contract
    error.stderr     # the CLI's full diagnostic output, "error: " prefix included
    error.report     # the JSON report the CLI wrote before failing, or None
```

`code` は `CONSTRAINT_ERROR` か `INVALID_INPUT` のどちらかで、`exit_code` は必ず[CLI リファレンス](cli.md)が定める終了コードのいずれかになります。`stderr` には CLI が書いた診断がそのまま入ります。例外自身のメッセージは、そこから `error: ` の接頭辞を取り除いたものです。JavaScript API にはもう 1 つ `TUPLE_EXPLOSION` という分類がありますが、CLI はそれを `INVALID_INPUT` にまとめており、このパッケージは CLI が報告したとおりを報告します。カバレッジ不足はエラーではありません。上の `generate` を参照してください。

サブプロセスを起動する前に捕まる失敗が 1 種類あります。モデルに含まれる非有限の数値、つまり `inf`・`-inf`・`nan` は JSON へ直列化できないため、直列化の段階で拒否され、`code == "INVALID_INPUT"`、`exit_code == 3` の `CoverwiseError` として送出されます。実行ファイルは動いていないので、`stderr` は空文字列、`report` は `None` です。この経路では例外のメッセージを読んでください。`stderr` には何も入りません。

失敗の内容がメッセージではなくレポートに書かれている場合もあります。`analyze` はスイートを測定してから、不正な行を含むことを理由に拒否するため、その行を指し示すレポートは例外に残ります。

```python
import coverwise

PARAMS = {"os": ["Windows", "macOS"], "browser": ["Chrome", "Safari"]}
CONSTRAINTS = ["IF os = Windows THEN browser != Safari"]
tests = [{"os": "macOS", "browser": "Chrome"}, {"os": "Windows", "browser": "Safari"}]

try:
    coverwise.analyze_coverage(PARAMS, tests, constraints=CONSTRAINTS)
except coverwise.CoverwiseError as error:
    for invalid in error.report["invalidTests"]:
        print(invalid["testIndex"], invalid["reason"])  # 1 violates constraint #1 ...
```

CLI が何も書き出す前に失敗した場合、`report` は `None` になります。生成を始める前に受理ゲートが拒否したモデルがこれにあたります。制約の失敗はこれに含まれません。`generate` と `extend` は、失敗が表に出る前にエンベロープ全体を、その中の `error` オブジェクトごと書き出すからです。

`analyze_coverage` と `extend_tests` は入力を 2 つ取りますが、標準入力が運べるのはそのうち 1 つだけです。もう一方は一時ファイルに書き出され、呼び出しが返る前に削除されます。その入力についての診断がこのパスを名指しすると、呼び出し側が書いた覚えもなく、もう見ることもできないパスを指すことになります。そのためパスは引数の名前に書き換えられます。`analyze_coverage` のもう一方の入力の読み込みに失敗した場合は `the 'tests' argument` として、`extend_tests` の場合は `the 'existing' argument` として報告されます。

ネイティブ実行ファイルのクラッシュはモデルのエラーではないため、`CoverwiseError` にはなりません。シグナル名または終了ステータスを示す素の `RuntimeError` が送出されるので、segfault が入力エラーとして提示されることはありません。

## 入力の上限

この API はモデルを JSON へ直列化してネイティブの実行ファイルへ渡すため、上限は CLI のものと同じです。いずれかを超えた場合は `code == "INVALID_INPUT"`、`exit_code == 3` の `CoverwiseError` を送出します。上限そのもの、どれが先に効くか、それぞれのメッセージは[入力上限](limits.md)にまとめてあります。

## 実行ファイルを直接呼ぶ

`coverwise.run()` は CLI へそのまま渡す引数を受け取り、標準の `subprocess.CompletedProcess` を返します。プロセス生成を自分で管理する組み込みでは、`coverwise.native_binary()` で同梱した実行ファイルのパスを取得できます。

```python
import coverwise

result = coverwise.run(["generate", "input.json"], text=True, capture_output=True, check=True)
print(result.stdout)
print(coverwise.native_binary())
```

`coverwise.run()` はキーワード引数をそのまま `subprocess.run` に渡すため、キャプチャした出力が文字列とバイト列のどちらになるかを決めるのは `subprocess.run` です。`stdout` が `str` になるのは `text=True` を渡した形だけです。

実行ファイルを使えないとき、どちらの関数も `OSError` ではなく `RuntimeError` を送出し、4 つの原因のどれであるかをメッセージに含めます。ファイルが存在しない、パスがファイルではない、実行権限がない、OS が起動を拒否した、の 4 つです。前の 3 つは起動前に検査されるため、どの経路で実行ファイルに到達しても同じ形で報告されます。パスが wheel 由来の場合、メッセージの末尾には、展開したソースツリーではなく対応プラットフォームの wheel を PyPI からインストールするよう促す一文が付きます。

`COVERWISE_BINARY` を設定すると、どちらの関数も別の実行ファイルを指します。この経路での失敗はその環境変数を名指しして `COVERWISE_BINARY points at a missing file: ...` のように報告されます。ローカルビルドした CLI で開発するための仕組みで、インストール済み wheel では不要です。

## コンソールスクリプト

パッケージをインストールすると `coverwise` コマンドがパスに置かれます。`python -m coverwise` も同じものを実行します。

```bash
coverwise generate input.json > tests.json
coverwise analyze --params params.json --tests tests.json
coverwise extend --existing tests.json input.json > updated-tests.json
coverwise stats input.json
```

このコンソールスクリプトはサブプロセスを起動しません。`os.execv` で自分のプロセスをネイティブの実行ファイルに置き換えます。したがってシグナル・終了コード・3 つの標準ストリームは実行ファイル自身のものになり、途中でそれらを解釈し直す Python プロセスは残りません。`coverwise generate big.json | head -c 200` が wheel からでもソースビルドからでも同じ挙動になるのはこのためです。出力をプログラムで受け取りたいときは `coverwise.run()` を、シェルや CI ジョブから駆動するときはコマンドを使ってください。

入力と出力のスキーマ、そして終了コードは[CLI リファレンス](cli.md)を参照してください。

## バージョンと型検査

```python
import coverwise

coverwise.__version__  # the version of the package and of the bundled executable
```

`__version__` は `__all__` のメンバーではなくモジュール属性で、Python 層と同梱の実行ファイルがどちらもビルドされた 1 つのバージョンを示します。

パッケージは注釈をインラインで同梱し、PEP 561 のマーカーも持ちます。そのため mypy や pyright は、スタブパッケージを追加せずに実際のシグネチャで呼び出しを検査します。

## 次に読むもの

- [はじめかた](getting-started.md) — 各サーフェスでのインストールと、最初のスイート生成
- [pytest の全組み合わせを置き換える](use-cases/replace-a-cross-product-in-pytest.md) — 既存のスイートに `parametrize` を適用する流れ
- [CLI リファレンス](cli.md) — このパッケージが駆動する実行ファイルと、それが書く JSON
- [制約構文](constraints.md) — `constraints` のリストが受け付ける式の言語
- [入力上限](limits.md) — モデルに書ける範囲と、上限に達したときの報告
- [用語集](glossary.md) — このページが使う語彙
