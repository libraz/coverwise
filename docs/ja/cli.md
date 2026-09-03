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

`parameters` のみ必須。他のフィールドはすべてオプションで、[JavaScript API](js-api.md) が
`GenerateInput` として文書化しているものと同じフィールドです。

| フィールド | 定義域 |
|-----------|--------|
| `strength` | 正の整数。デフォルトは `2`（ペアワイズ）。 |
| `seed` | `[0, 4294967295]` の uint32 整数。デフォルトは `0`。 |
| `maxTests` | `[0, 4294967295]` の uint32 整数。`0` は上限なしで、これがデフォルト。ポジティブとネガティブを合わせたスイートに上限をかけます。 |
| `constraints` | 制約式の配列。[制約構文](constraints.md) を参照。 |
| `weights` | パラメータ名から「値 → 重み」オブジェクトへの写像。各重みは `0` より大きい有限数。 |
| `seeds` | 出発点とするテストケースの配列。形式は `tests` 出力と同じオブジェクト形式。 |
| `subModels` | `{ "parameters": [...], "strength": n }` の配列。指定したグループに独自の強度を与えます。 |

パラメータは離散パラメータか境界パラメータのどちらかです。境界パラメータは
`"type": "integer"` または `"type": "float"` と、両端を含む `"range": [min, max]` を
併せて持ち、coverwise がその範囲を端と端付近の値に展開します。`"values"` は依然として
必須です。値は範囲から得られるため通常は空配列にしますが、そこに列挙した値は展開結果と
併せて保持されます。`"step"` のデフォルトは `1` で、`"type": "integer"` では `1` のみ
指定できます。`"type"` だけ、あるいは `"range"` だけを宣言した場合は終了コード `3` で
拒否します。

個々の値は文字列ではなくオブジェクトでも記述できます。[パラメータ値のフォーマット](#パラメータ値のフォーマット)
を参照してください。

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

**モデルが記述していない行。** `--params` のモデルが宣言していないパラメータや値を
含むテストケースは分析対象になりません。除外の理由とともに `invalidTests` に報告され、
カバレッジには一切寄与しないため、比率は残りの行だけで測られます。レポート全体は
それでも標準出力に書き出され、そのうえで `analyze` は終了コード `3` を返します。
無効な行はカバレッジ不足より優先されます。モデルと合っていないスイートは、それについての
カバレッジ値が意味を持つ前に直す必要があるからです。`coverageRatio` より先に
`invalidTests` を読んでください。

### `extend`

既存テストスイートを拡張してカバレッジを改善します。

```bash
coverwise extend --existing <tests.json> <input.json> [> output.json]
```

- `--existing` — 現在のテストケースの JSON ファイル
- `<input.json>` — `generate` が読むものと同じモデル

**`--existing` ファイル:**

```json
[
  { "os": "Windows", "browser": "Chrome" },
  { "os": "macOS", "browser": "Firefox" }
]
```

**入力フォーマット:**

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ],
  "strength": 2,
  "seed": 42
}
```

**出力フォーマット:**

```json
{
  "schemaVersion": 1,
  "tests": [
    { "os": "Windows", "browser": "Chrome" },
    { "os": "macOS", "browser": "Firefox" },
    { "os": "Windows", "browser": "Firefox" },
    { "os": "macOS", "browser": "Chrome" },
    { "os": "Linux", "browser": "Firefox" },
    { "os": "Windows", "browser": "Safari" },
    { "os": "macOS", "browser": "Safari" },
    { "os": "Linux", "browser": "Safari" },
    { "os": "Linux", "browser": "Chrome" }
  ],
  "uncoveredCount": 0,
  "omittedUncovered": 0,
  "negativeTests": [],
  "coverage": 1,
  "uncovered": [],
  "stats": {
    "totalTuples": 9,
    "coveredTuples": 9,
    "testCount": 9
  },
  "suggestions": [],
  "warnings": [],
  "strength": 2
}
```

`extend` は `generate` と同じエンベロープを返すため、後続の処理では両者を差し替えられます。
既存テストが与えられた順のまま先頭に並び、その後ろに新規テストが続きます。`--existing` の
件数より後ろの行が、その実行で追加された分です。`stats` と `coverage` は追加分ではなく
結合後のスイートを表します。

`extend` は `--existing` とモデルの `seeds` という 2 か所から行を読み、そのどちらも、
その実行が持つ 1 つの合計バイト数の予算へ計上されます。片方だけなら収まっても、両方を
合わせると収まらないことがあります。[入力の上限](#入力の上限) を参照してください。

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
最大値数・強度・パラメータ数から求めて `totalTuples` で頭打ちにした、大まかな
見積もりです。上限でも下限でもなく、`generate` が返すテストケース数は、このモデルの
ようにこれを下回ることも、上回ることもあります。

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

合計バイト数の予算はコマンド 1 回につき 1 つで、コマンドが読むすべての文字列が、読まれた場所で
そこへ 1 度ずつ計上されます。パラメータ名、値、エイリアス、クラス名、制約式、サブモデルの
パラメータ名、`weights` オブジェクトが書き出す名前、そして `tests`・`seeds`・`existing` 配列の
各行の値が対象です。

行は**値**が計上され、キーは計上されません。キーはパラメータ名であり、モデルの文字列として既に
1 度計上されているためです。行ごとに計上すると同じテキストを行数ぶん数えることになります。
計上されるのは**文字列**の値だけで、数値や真偽値の行の値は予算を消費しません。

**多くのモデルでは、行数の上限より先にバイト数の予算が効きます。** 100,000 行は上限であって、
その規模のスイートが受理されるという約束ではありません。1 MiB を 100,000 行に配分すると 1 行
あたり約 10.5 バイトしか残らず、これに収まるのは極端に幅の狭いモデルだけです。値が 5 バイトの
文字列で、1 行につき各パラメータの値が 1 つの場合、2 つの上限は次の位置で交わります。

| 1 行あたりのパラメータ数 | 受理される行数 |
|--------------------------|----------------|
| 2 | 100,000（行数の上限が先に効く） |
| 3 | 69,902 |
| 10 | 20,969 |
| 100 | 2,094 |

上限は 2 つの次元の関数なので、この数値はモデル自身の文字列が行に比べて十分小さいことを前提と
しています。名前が長いモデルや値の多いモデルは同じ予算の一部を使うため、この数値は下がります。
予算超過は終了コード `3` と `Input strings exceed 1048576 UTF-8 bytes` になります。メッセージは
行数ではなく予算を名指しするため、100,000 行にはるかに満たない位置での拒否はこの上限によるもので
あって不具合ではありません。値の名前を短くすればそのぶん行数を確保でき、予算に収まらないスイートは
分割して分析する必要があります。

**文書のバイト数は 3 つ目の上限で、通常は他の 2 つよりはるかに外側にあります。** これはファイルや
標準入力を読み込む時点、つまり内容をパースする前に適用されるため、波括弧・引用符・コロン・行ごとに
繰り返されるキーといった JSON の構文も数えます。一方バイト数の予算が数えるのは呼び出し側が与えた
テキストだけです。幅の広いモデルでは構文が支配的になります。100 パラメータ × 100,002 行は約
133 MiB の JSON になりますが、そこに含まれる行のテキストは 1 MiB にはるかに届きません。この文書は
上記のどちらの上限でもなく `file '<path>' exceeds the maximum of 67108864 bytes` で拒否されます。
これが、呼び出し側が文書のバイト数に先に到達する唯一の形です。メッセージは行ではなく文書を名指し
するので、スイートについての言明ではなく「このファイルは読み込むには大きすぎる」と読んでください。

パラメータ数の上限は、制約の充足可能性探索を有限に保つためのものです。探索は 1 階層につき 1 パラメータを進むため、探索の深さを抑えるものはパラメータ数のほかにありません。

文書のバイト数は、ファイルの読み込みや標準入力の読み切りに対するメモリ保護であって、CLI が受け付ける入力の条件ではありません。暴走した入力や途切れない入力を際限なくメモリへ読み込むことを防ぎます。通常の幅のモデルであれば他の上限よりはるかに外側にあるため、実際の入力は先にそのいずれかへ到達し、超えた上限そのものを理由に拒否されます。例外は上で述べた形で、幅の広いモデルでは行のテキストがバイト数の予算に十分収まっていても、JSON の構文が文書のバイト数へ先に到達します。

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
