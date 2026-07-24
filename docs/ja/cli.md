# CLI リファレンス

Linux x64 または macOS Apple Silicon では、PyPI から `coverwise` native CLI をインストールできます。

```bash
pip install coverwise
```

native CLIはnpm packageには含まれません。Linux x64版は
[GitHub Releases](https://github.com/libraz/coverwise/releases) からも取得できます。ソースbuild時の
pathは`build/bin/coverwise`で、CMake install後は指定prefix配下の`bin/coverwise`です。

`coverwise` コマンドラインツールは JSON 入力を読み込み、JSON 出力を書き出します。

すべてのコマンド出力は CLI schema version `1` を使用し、`"schemaVersion": 1` を含みます。
v1 では空の配列フィールドも常に出力し、suggestionを`{ description, testCase }`形式へ変更し、
statsの名称を`subModelCount`、`constraintCount`、`parameters`へ統一しています。

## コマンド

### `generate`

JSON 仕様からカバリングテストスイートを生成します。

```bash
coverwise generate <input.json> [> output.json]
```

**入力フォーマット:**

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ],
  "strength": 2,
  "seed": 42,
  "constraints": [
    "IF os = Windows THEN browser != Safari"
  ]
}
```

`parameters` のみ必須。他のフィールドはすべてオプションです。

**出力フォーマット:**

```json
{
  "schemaVersion": 1,
  "tests": [
    { "os": "Linux", "browser": "Firefox" },
    { "os": "Windows", "browser": "Chrome" },
    { "os": "Windows", "browser": "Firefox" },
    { "os": "macOS", "browser": "Chrome" },
    { "os": "macOS", "browser": "Safari" },
    { "os": "Linux", "browser": "Safari" },
    { "os": "Linux", "browser": "Chrome" },
    { "os": "macOS", "browser": "Firefox" }
  ],
  "negativeTests": [],
  "coverage": 1.0,
  "uncovered": [],
  "uncoveredCount": 0,
  "omittedUncovered": 0,
  "stats": {
    "totalTuples": 8,
    "coveredTuples": 8,
    "testCount": 8
  },
  "suggestions": [],
  "warnings": [],
  "strength": 2
}
```

これは、同梱ジェネレータで上記の入力を実行した正確な結果です。制約により
`os=Windows, browser=Safari` は実行不能となるため、要求強度のペアは8個残ります。
固定した入力とシードに対する出力順は決定的ですが、バージョンをまたぐ順序の契約として
利用しないでください。

`"invalid": true` とした値はポジティブカバレッジから除外され、別のネガティブテストとして
処理されます。無効値がある場合、出力には `negativeCoverage`（`totalTuples`、
`coveredTuples`、`omittedTuples`、`coverageRatio`）も含まれます。`maxTests` はポジティブと
ネガティブを合わせたスイートに制限をかけるため、ネガティブ生成は未完了になることがあり
ます。すべてのネガティブタプルが出力されたと決めつけず、`negativeCoverage` と `warnings`
を確認してください。

### `analyze`

既存テストスイートの t-wise カバレッジを分析します。

```bash
coverwise analyze --params <params.json> --tests <tests.json> [--strength <n>] [--constraints <file.json>]
```

- `--params` — パラメータ定義の JSON ファイル
- `--tests` — テストケースの JSON ファイル
- `--strength` — 相互作用の強度（デフォルト: 2）
- `--constraints` — 制約文字列の JSON ファイル（任意）。タプルがカバレッジの対象から除外されるのは、すべての制約を満たす有効値の完全な割り当てへ補完できない場合だけです。部分的な制約評価だけを違反とみなすことなく、制約付きで完全にカバーされたスイートは 100% と報告されます。

`--tests` と `--existing` は、テスト配列そのものに加え `generate` が出力する schema-v1 エンベロープも受け付けます。したがって `coverwise generate input.json > tests.json` の出力をそのまま後続コマンドへ渡せます。

**出力:**

```json
{
  "schemaVersion": 1,
  "totalTuples": 9,
  "coveredTuples": 7,
  "coverageRatio": 0.778,
  "uncovered": [
    {
      "tuple": ["os=Windows", "browser=Safari"],
      "params": ["os", "browser"],
      "indices": [[0, 0], [1, 2]],
      "reason": "never covered",
      "display": "os=Windows, browser=Safari"
    }
  ],
  "uncoveredCount": 2,
  "omittedUncovered": 0,
  "invalidTests": []
}
```

### `extend`

既存テストスイートを拡張してカバレッジを改善します。

```bash
coverwise extend --existing <tests.json> <input.json> [> output.json]
```

- `--existing` — 現在のテストケースの JSON ファイル
- 出力には元のテスト＋新規テストが含まれます

### `stats`

生成を実行せずにモデル統計をプレビューします。

`stats` は、制約で除外する前の raw タプル数を推定しますが、報告前に制約構文とパラメータ参照を検証します。

```bash
coverwise stats <input.json>
```

**出力:**

```json
{
  "schemaVersion": 1,
  "parameterCount": 3,
  "totalValues": 8,
  "strength": 2,
  "totalTuples": 29,
  "estimatedTests": 10,
  "subModelCount": 0,
  "constraintCount": 1,
  "parameters": [
    { "name": "os", "valueCount": 3, "invalidCount": 0 },
    { "name": "browser", "valueCount": 3, "invalidCount": 0 },
    { "name": "theme", "valueCount": 2, "invalidCount": 0 }
  ]
}
```

## 終了コード

| コード | 意味 |
|-------|------|
| `0` | 成功。100%カバレッジ達成。 |
| `1` | 制約エラー。 |
| `2` | カバレッジ不足（例: `maxTests` 制限到達）。 |
| `3` | 入力不正。 |

## パラメータ値のフォーマット

値はシンプルな文字列またはオブジェクトで指定できます：

```json
{
  "parameters": [
    {
      "name": "browser",
      "values": [
        "Chrome",
        { "value": "IE", "invalid": true },
        { "value": "Chromium", "aliases": ["Chrome", "Edge"] },
        { "value": "Firefox", "class": "gecko" }
      ]
    }
  ]
}
```

## パイプ

標準的な Unix パイプが使えます：

```bash
# 生成して件数を確認
coverwise generate input.json | jq '.tests | length'

# 他のツールに連携
coverwise generate input.json | my-test-runner --from-stdin
```

## 使用例

```bash
# 基本的なペアワイズ生成
coverwise generate input.json

# 3-wise カバレッジ分析
coverwise analyze --params params.json --tests tests.json --strength 3

# 制約付きで既存テストを拡張
coverwise extend --existing current.json input.json > updated.json

# モデルサイズの簡易チェック
coverwise stats input.json | jq '.totalTuples'
```
