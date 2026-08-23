# CLI リファレンス

Linux（x86_64 / aarch64）または macOS 14 以降の Apple Silicon では、PyPI から `coverwise` native CLI をインストールできます。

```bash
pip install coverwise
```

native CLIはnpm packageには含まれません。Linux x64版は
[GitHub Releases](https://github.com/libraz/coverwise/releases) からも取得できます。ソースbuild時の
pathは`build/bin/coverwise`で、CMake install後は指定prefix配下の`bin/coverwise`です。

`coverwise` コマンドラインツールは JSON 入力を読み込み、JSON 出力を書き出します。

入力 path にはいずれも `-` を指定でき、その JSON を標準入力から読み込みます。標準入力は一度しか読めないため、入力を 2 つ取るコマンドでは、そのうち 1 つに `-` を指定できます。

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
  "uncoveredCount": 0,
  "omittedUncovered": 0,
  "negativeTests": [],
  "coverage": 1,
  "uncovered": [],
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

これは、同梱ジェネレータで上記の入力を実行した正確な結果を、読みやすさのために
改行したものです。CLI は出力を 1 行で書き出します。制約により
`os=Windows, browser=Safari` は実行不能となるため、要求強度のペアは8個残ります。
`coverage` は往復可能な最短形式の JSON 数値なので、完全カバレッジは `1.0` ではなく
`1` になります。固定した入力とシードに対する出力順は決定的ですが、バージョンをまたぐ
順序の契約として利用しないでください。

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
- `--strength` — 相互作用の強度（デフォルト: `--params` のモデルが持つ `strength`、なければ 2）
- `--constraints` — 制約文字列の JSON ファイル（任意）。`--params` のモデルが宣言した制約を置き換えます。タプルがカバレッジの対象から除外されるのは、すべての制約を満たす有効値の完全な割り当てへ補完できない場合だけです。部分的な制約評価だけを違反とみなすことなく、制約付きで完全にカバーされたスイートは 100% と報告されます。

`--tests` と `--existing` は、テスト配列そのものに加え `generate` が出力する schema-v1 エンベロープも受け付けます。したがって `coverwise generate input.json > tests.json` の出力をそのまま後続コマンドへ渡せます。

**強度をどこから取るか。** `--params` がパラメータ配列そのものではなくモデルオブジェクトの場合、その `strength` フィールドがカバレッジの対象範囲を定めるため、測定にもその値を使います。同じモデルを `generate` に通してから `analyze` に渡す際に、強度を書き直す必要はありません。`--strength` を明示した場合はモデルの値より優先されます。`--strength` はモデルの性質ではなく、その実行のために選ぶ分析用のつまみだからです。どちらも指定がなければペアワイズです。

`subModels` を宣言したモデルは終了コード `3` で拒否します。サブモデルはモデルの一部に独自の強度を与える仕組みですが、カバレッジレポートは 1 つの対象範囲を 1 つの強度で測るため、これを表現できません。グループごとに `--strength` で強度を指定して個別に分析してください。

**`--constraints` はモデルの制約をどう扱うか。** `--constraints` を明示した場合、そのファイルは `--params` が宣言した制約を置き換えます。両者がマージされることはないため、ファイルの内容が測定に使う制約のすべてになります。ファイルの形式は、式の配列そのものか、`constraints` 配列を持つオブジェクトのどちらかです。それ以外のトップレベル文書は終了コード `3` で拒否します。制約を持たないモデルに対して `jq '.constraints'` が書き出す `null` 単体も同様です。こうした文書を「制約なし」と読んでしまうと、制約のない対象範囲を測ってカバレッジ不足を報告し、その理由を説明するエラー出力が何も残らないためです。

**`--params` ファイル:**

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ]
}
```

**`--tests` ファイル:**

```json
[
  { "os": "Windows", "browser": "Chrome" },
  { "os": "Windows", "browser": "Firefox" },
  { "os": "macOS", "browser": "Firefox" },
  { "os": "macOS", "browser": "Safari" },
  { "os": "Linux", "browser": "Chrome" },
  { "os": "Linux", "browser": "Firefox" },
  { "os": "Linux", "browser": "Safari" }
]
```

**出力:**

```json
{
  "schemaVersion": 1,
  "totalTuples": 9,
  "coveredTuples": 7,
  "coverageRatio": 0.7777777777777778,
  "uncovered": [
    {
      "tuple": ["os=Windows", "browser=Safari"],
      "params": ["os", "browser"],
      "indices": [[0, 0], [1, 2]],
      "reason": "never covered",
      "display": "os=Windows, browser=Safari"
    },
    {
      "tuple": ["os=macOS", "browser=Chrome"],
      "params": ["os", "browser"],
      "indices": [[0, 1], [1, 0]],
      "reason": "never covered",
      "display": "os=macOS, browser=Chrome"
    }
  ],
  "uncoveredCount": 2,
  "omittedUncovered": 0,
  "invalidTests": []
}
```

9 ペアのうち 7 ペアしかスイートに現れないため、`analyze` は終了コード `2` を返します。
`uncoveredCount` は未カバータプルの総数です。`uncovered` 配列は診断用の上限で打ち切られ、
そこから漏れた件数が `omittedUncovered` に入るため、配列の要素数は常に
`uncoveredCount - omittedUncovered` と一致します。`coverageRatio` は表示用に丸めず、
往復可能な最短形式で書き出されます。

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

**入力:**

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] },
    { "name": "theme", "values": ["light", "dark"] }
  ],
  "strength": 2,
  "constraints": [
    "IF os = Windows THEN browser != Safari"
  ]
}
```

**出力:**

```json
{
  "schemaVersion": 1,
  "parameterCount": 3,
  "totalValues": 8,
  "strength": 2,
  "totalTuples": 21,
  "estimatedTests": 18,
  "subModelCount": 0,
  "constraintCount": 1,
  "parameters": [
    { "name": "os", "valueCount": 3, "invalidCount": 0 },
    { "name": "browser", "valueCount": 3, "invalidCount": 0 },
    { "name": "theme", "valueCount": 2, "invalidCount": 0 }
  ]
}
```

`totalTuples` は制約で除外する前のペア数（3·3 + 3·2 + 3·2）です。`estimatedTests` は
最大値数と強度から求めた保守的な上限であって予測値ではなく、このモデルで実際に
`generate` を実行するとテストケース数はこれより少なくなります。

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
        { "value": "Chromium", "aliases": ["chromium-browser", "cr"] },
        { "value": "Firefox", "class": "gecko" }
      ]
    }
  ]
}
```

1 つのパラメータの中では、すべての値とすべてのエイリアスが、ASCII の大小文字を畳み込んだ後も互いに異なる名前になっている必要があります。`Chrome` を値と `Chromium` のエイリアスの両方に挙げる場合や、`Chrome` と `chrome` を並べる場合は、大小文字を区別しない解決の結果が一意に定まらないため、終了コード `3` で拒否されます。パラメータ名どうしにも同じ規則が適用されます。

## 入力の上限

どのコマンドも、読み込む入力に同じ上限を適用します。いずれかを超えた場合は終了コード `3` です。

| 項目 | 上限 |
|------|------|
| 1 モデルのパラメータ数 | 1,024 |
| 1 パラメータの値の数 | 16,384 |
| `tests`・`seeds`・`existing` 配列の行数 | 100,000 |
| 制約式の数 | 256 |
| 1 つの文字列の UTF-8 バイト数 | 65,536（64 KiB） |
| モデル中の文字列の合計 UTF-8 バイト数 | 1,048,576（1 MiB） |
| ファイルまたは標準入力から読み込む 1 つの JSON 文書のバイト数 | 67,108,864（64 MiB） |

合計バイト数の対象は、モデルを記述する文字列です。パラメータ名、値、エイリアス、クラス名、制約式、サブモデルのパラメータ名が含まれます。

パラメータ数の上限は、制約の充足可能性探索を有限に保つためのものです。探索は 1 階層につき 1 パラメータを進むため、探索の深さを抑えるものはパラメータ数のほかにありません。

文書のバイト数は、ファイルの読み込みや標準入力の読み切りに対するメモリ保護であって、CLI が受け付ける入力の条件ではありません。上記の上限を満たす文書に必要な大きさより十分に大きく取ってあるため、実際の入力は先に上記のいずれかへ到達し、超えた上限そのものを理由に拒否されます。文書のバイト数は、暴走した入力や途切れない入力を際限なくメモリへ読み込むことだけを防ぎます。

## パイプ

標準的な Unix パイプが入力側・出力側とも使えます。入力 path の代わりに `-` を渡すと、その JSON を標準入力から読み込みます。

```bash
# 生成して件数を確認
coverwise generate input.json | jq '.tests | length'

# 他のツールに連携
coverwise generate input.json | my-test-runner --from-stdin

# モデルをその場で組み立てて生成
jq '{parameters: .matrix}' config.json | coverwise generate -

# 中間ファイルなしで生成結果を測定
coverwise generate input.json | coverwise analyze --params input.json --tests -
```

標準入力は一度しか読めないため、1 つのコマンドの両方の入力に `-` を渡した場合は、空として読むのではなくエラーになります。

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
