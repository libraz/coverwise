# CLI リファレンス

`coverwise` コマンドラインツールは JSON 入力を読み込み、JSON 出力を書き出します。

## コマンド

### `generate`

JSON 仕様から最小カバリング配列を生成します。

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
  "maxTests": 0,
  "constraints": [
    "IF os = Windows THEN browser != Safari"
  ],
  "weights": {
    "os": { "Windows": 2.0 }
  },
  "seeds": [
    { "os": "Windows", "browser": "Chrome" }
  ],
  "subModels": [
    { "parameters": ["os", "browser"], "strength": 3 }
  ]
}
```

`parameters` のみ必須。他のフィールドはすべてオプションです。

**出力フォーマット:**

```json
{
  "tests": [
    { "os": "Windows", "browser": "Chrome" },
    { "os": "macOS", "browser": "Firefox" }
  ],
  "negativeTests": [],
  "coverage": 1.0,
  "uncovered": [],
  "stats": {
    "totalTuples": 9,
    "coveredTuples": 9,
    "testCount": 6
  },
  "suggestions": [],
  "warnings": [],
  "strength": 2
}
```

### `analyze`

既存テストスイートの t-wise カバレッジを分析します。

```bash
coverwise analyze --params <params.json> --tests <tests.json> [--strength <n>]
```

- `--params` — パラメータ定義の JSON ファイル
- `--tests` — テストケースの JSON ファイル
- `--strength` — 相互作用の強度（デフォルト: 2）

**出力:**

```json
{
  "totalTuples": 9,
  "coveredTuples": 7,
  "coverageRatio": 0.778,
  "uncovered": [
    {
      "tuple": ["os=Windows", "browser=Safari"],
      "params": ["os", "browser"],
      "display": "os=Windows, browser=Safari"
    }
  ]
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

```bash
coverwise stats <input.json>
```

**出力:**

```json
{
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
