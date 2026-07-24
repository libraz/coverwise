# Python API

## インストール

PyPI の `coverwise` パッケージは native command-line tool をインストールします。ジェネレータの別の Python 実装は持たないため、JSON の振る舞い、出力、終了コードは C++ CLI と完全に一致します。

```bash
pip install coverwise
coverwise --help
```

対応 wheel は Linux x86_64 と macOS Apple Silicon です。runtime の Python dependency はありません。`python -m coverwise` も同じ command を実行します。

## Command-line interface

shell script や pipeline ではインストール済み command を使います。

```bash
coverwise generate input.json > tests.json
coverwise analyze --params params.json --tests tests.json
coverwise extend --existing tests.json input.json > updated-tests.json
coverwise stats input.json
```

入力 schema、出力 schema、終了コードは [CLI リファレンス](cli.md) を参照してください。

## 自動化用ヘルパー

Python process から実行ファイルを起動する場合、`coverwise.run()` は標準の `subprocess.CompletedProcess` を返します。`coverwise` command の後ろに指定する引数を同じように渡してください。

```python
import coverwise

result = coverwise.run(
    ["generate", "input.json"],
    text=True,
    capture_output=True,
    check=True,
)
print(result.stdout)
```

プロセス生成を自分で管理する integration では、`coverwise.native_binary()` で同梱 executable の path を取得できます。
