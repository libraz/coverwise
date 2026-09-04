# pytest の全組み合わせを置き換える

入力の全組み合わせをパラメータ化する pytest モジュールは、モジュールが育つにつれてケース数が掛け算で増えます。`coverwise.parametrize` はその状況での `pytest.mark.parametrize` の差し替え先です。同じリストからカバリング配列を生成し、各ケースを同名の引数としてテストに渡すので、テスト本体は変わりません。

このページが前提とする語彙、すなわちカバリング配列、強度、ペアは[入門](../primer/index.md)で説明しています。Python サーフェスの残りについては[Python API](../python-api.md)を参照してください。

## インストール

pytest を必要とするのはパッケージのうち `parametrize` だけで、しかも必要なのはインストール先ではなくテストを走らせる場所です。そのため pytest は依存ではなく extra になっています。

```bash
pip install coverwise[pytest]
```

Python 3.10 以上が必要です。pytest を import できない状態で `coverwise.parametrize` を呼ぶと、pytest が不足していることを示す `RuntimeError` が送出されます。パッケージの他の部分は pytest なしで動作します。

## いまの全組み合わせ

```python
import itertools

import pytest

OS = ["Windows", "macOS", "Linux"]
BROWSER = ["Chrome", "Firefox", "Safari"]
LOCALE = ["en", "ja", "de"]


@pytest.mark.parametrize("os,browser,locale", list(itertools.product(OS, BROWSER, LOCALE)))
def test_page_renders(os, browser, locale):
    assert render(os, browser, locale).ok
```

3 × 3 × 3 = 27 ケースです。ケース数はリストの長さの積なので、値が 4 つある入力を 1 つ足せば 108 になり、既存のリストに値を 1 つ足すとケース数は 1 ずつではなく倍率で動きます。

## 網羅に足りるスイート

```python
import coverwise


@coverwise.parametrize(
    {
        "os": ["Windows", "macOS", "Linux"],
        "browser": ["Chrome", "Firefox", "Safari"],
        "locale": ["en", "ja", "de"],
    },
    constraints=["IF os = Windows THEN browser != Safari"],
)
def test_page_renders(os, browser, locale):
    assert render(os, browser, locale).ok
```

13 ケースです。入力が 3 つなので入力の組は 3 通りあり、値のペアは 3 × 3 + 3 × 3 + 3 × 3 = 27 個です。制約が `os=Windows, browser=Safari` を不可能にするため、スイートのどこかに現れる必要があるペアは 26 個になります。そのすべてが、27 ケースではなく 13 ケースの中に現れます。制約はケース単位でも守られるので、生成されたどのケースも Windows と Safari を組み合わせません。全組み合わせもここでは 27 になりますが、これは 3 × 3 × 3 という偶然です。ペア数は入力の個数に対して 2 乗で増え、全組み合わせは指数で増えます。

ここで求められる判断は、このスイートで捕まえたい欠陥はどれか、です。13 ケースは入力 2 つの組み合わせで起きる欠陥をすべて捕まえます。3 つすべてに特定の値が揃って初めて起きる欠陥は捕まえません。それを狙うなら強度を上げ、その分をケース数で払います。全組み合わせは 3 つ同時の欠陥も捕まえますが、そのために 27 ケースを要します。

各ケースには値から組み立てた id が付きます。`os=macOS-browser=Firefox-locale=ja` のような形なので、`pytest -k` で選択でき、失敗レポートも組み合わせを調べ直さずに示します。

## モデルの残りを渡す

モデルの項目はそのまま `coverwise.generate` に渡されます。`strength`、`constraints`、`seed`、`maxTests`、`weights`、`subModels` が使えます。`strength` の既定値は 2 です。`seed` はどのカバリング配列が生成されるかを固定するので、固定しておけばケース id が実行間でもマシン間でも変わりません。

`include_negative` はキーワード専用で、モデルではなくデコレータ側の引数です。invalid と印を付けた値は異常系テストのケースを生み、各ケースは不正な値をちょうど 1 つ持ちます。明示的に指定しないかぎり含まれません。

```python
import coverwise


@coverwise.parametrize(
    {
        "email": ["user@example.com", {"value": "", "invalid": True}],
        "plan": ["child", "adult"],
    },
    include_negative=True,
    seed=7,
)
def test_signup_is_answered(email, plan):
    assert signup(email, plan) is not None
```

`include_negative` なしでは 2 ケース、付ければ 4 ケースです。増えた 2 つの異常系ケースは、空のメールアドレスを各 plan と組み合わせたものです。有効にするのは、テスト本体が受け取った値から拒否と受理を見分けられる場合だけにしてください。期待する結果が 2 つのスイートで異なるなら、`generate` の `tests` と `negativeTests` に対して期待の異なる 2 つのテストを書くほうが明快です。

## パラメータが 1 個の場合

パラメータが 1 個のモデルにはペアが存在せず、既定の強度 2 はパラメータ数を超えるためモデルは拒否されます。`parametrize` はこの場合に強度を 1 に既定し、pytest に渡す引数名も複数ではなく 1 つになるので、テストは要素 1 個のタプルではなく値そのものを受け取ります。

```python
import coverwise


@coverwise.parametrize({"browser": ["Chrome", "Firefox", "Safari"]})
def test_page_renders_per_browser(browser):
    assert render(browser).ok
```

3 ケース、id は `browser=Chrome`、`browser=Firefox`、`browser=Safari` で、`browser` は文字列です。強度 1 での必要タプル集合は各パラメータの各値そのものなので、スイートの大きさは最も長い値リストと同じになります。

パラメータ名はそのまま引数名になるため、各名前は Python の識別子として妥当で、かつ予約語であってはなりません。`parametrize` は問題のある名前を挙げて `ValueError` を送出します。pytest の収集時に、より曖昧な形で失敗させることはしません。

## 次に読むもの

- [Python API](../python-api.md) — デコレータが扱わない範囲のための `generate`、`analyze`、`extend`、およびエラー型
- [強度](../primer/strength.md) — t = 2 が捕まえるもの、逃すもの、上げたときの代償
- [制約構文](../constraints.md) — `constraints` のリストを書くための式の文法
- [既存スイートを監査する](audit-an-existing-suite.md) — 手書きのまま残すスイートを測る方法
- [実例集](../examples.md) — 正常系と異常系を 2 つのテストに分ける例を含む、より短いレシピ
