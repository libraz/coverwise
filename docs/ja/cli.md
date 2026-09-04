# CLI リファレンス

`coverwise` 実行ファイルのリファレンスです。4 つのコマンド、それぞれが読み書きする JSON、そしてそれぞれが返す終了コードを扱います。シェルスクリプト、CI ジョブ、coverwise のバインディングがない言語から coverwise を動かす読者に向けたページで、語彙は[タプルとカバレッジ](primer/tuples-and-coverage.md)と[強度](primer/strength.md)のものを前提とし、ここでは再説明しません。実行ファイルの導入と最初のスイート生成は[はじめかた](getting-started.md)にあります。

入力パスにはいずれも `-` を指定でき、その JSON を標準入力から読み込みます。標準入力は一度しか読めないため、入力を 2 つ取るコマンドではそのうち 1 つにだけ `-` を指定できます。同じ呼び出しで 2 つ目の `-` を渡した場合は、空として読むのではなく拒否されます。

## 実行ファイルのインストール

PyPI パッケージにはネイティブの実行ファイルと、それを駆動する薄い Python ラッパーが入っています。Python 3.10 以上が必要で、Linux では wheel が `manylinux_2_28` としてビルドされているため glibc 2.28 以上が必要です。

```bash
pip install coverwise
```

wheel があるのは Linux x86_64、Linux aarch64、macOS 14 以降の Apple Silicon です。npm パッケージには実行ファイルは含まれません。

Python を導入していない場合は、[GitHub Releases](https://github.com/libraz/coverwise/releases)の Linux x64 アーカイブを使ってください。これは実行ファイル単体ではなく、完全なインストールツリーです。`bin/coverwise` に加えて、ライブラリ、ヘッダ、[C++ API](cpp-api.md)が説明する CMake パッケージが含まれます。展開してその場の `bin/coverwise` を実行するか、ツリーごと prefix 配下に配置してください。

それ以外の環境ではソースからビルドします。実行ファイルは `build/bin/coverwise` に置かれ、`cmake --install` を実行すると指定した prefix 配下の `bin/coverwise` に配置されます。

```bash
make release
```

## usage テキストを読む

`--help` と `-h` は usage テキストを標準出力へ書き、終了コード `0` で終わります。そのままリダイレクトやパイプに渡せます。

```bash
coverwise --help
```

```text
Usage:
  coverwise generate <input.json>
  coverwise analyze --params <params.json> --tests <tests.json> [--strength <n>] [--constraints <file.json>]
  coverwise extend --existing <tests.json> <input.json>
  coverwise stats <input.json>

Any input path may be '-' to read that JSON from standard input.

Exit codes:
  0 = OK (coverage 100%)
  1 = Constraint error
  2 = Insufficient coverage
  3 = Invalid input
```

呼び出し方が誤っている場合は、同じテキストを標準エラーへ書き、終了コード `3` で終わります。

`--version` はありません。`coverwise --version` は未知のコマンドとして扱われ、`Unknown command: --version` に続けて usage テキストを標準エラーへ書き、終了コード `3` で終わります。

## コマンド

### `generate`

JSON のモデルからカバリング配列となるテストスイートを生成します。

```bash
coverwise generate <input.json> [> output.json]
```

モデルは唯一の位置引数です。`generate` にフラグはありません。

**入力フォーマット**

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

必須なのは `parameters` だけで、パラメータを 1 つ以上持つ必要があります。残りのフィールドは任意で、[JavaScript API](js-api.md)が `GenerateInput` として文書化しているものと同じです。

| フィールド | 定義域 |
|-----------|--------|
| `strength` | 4294967295 以下の正の整数。パラメータ数を超えることはできません。デフォルトは `2`（ペアワイズ）。 |
| `seed` | `[0, 4294967295]` の uint32 整数。デフォルトは `0`。 |
| `maxTests` | `[0, 4294967295]` の uint32 整数。`0` は上限なしで、これがデフォルト。ポジティブとネガティブを合わせたスイートに上限をかけます。 |
| `constraints` | 制約式の配列。[制約構文](constraints.md) を参照。 |
| `weights` | パラメータ名から「値 → 重み」オブジェクトへの写像。各重みは `0` より大きい有限数。 |
| `seeds` | 出発点とするテストケースの配列。形式は `tests` 出力と同じオブジェクト形式。 |
| `subModels` | `{ "parameters": [...], "strength": n }` の配列。指定したグループに独自の強度を与えます。 |

パラメータオブジェクトは `name` と `values` を持ち、境界パラメータではさらに `type`・`range`・`step` を持ちます。パラメータは離散パラメータか境界パラメータのどちらかです。境界パラメータは `"type": "integer"` または `"type": "float"` と、両端を含む `"range": [min, max]` を併せて持ち、coverwise がその範囲を端と端付近の値に展開します。`"values"` は依然として必須です。値は範囲から得られるため通常は空配列にしますが、そこに列挙した値は展開結果と併せて保持されます。`"step"` のデフォルトは `1` で、`"type": "integer"` では `1` のみ指定できます。`"type"` だけ、あるいは `"range"` だけを宣言した場合は終了コード `3` で拒否します。

個々の値は文字列ではなくオブジェクトでも記述できます。[パラメータ値のフォーマット](#パラメータ値のフォーマット)を参照してください。

**出力フォーマット**

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

これは同梱ジェネレータで上記の入力を実行した正確な結果を、読みやすさのために改行したものです。CLI は出力を 1 行で書き出します。制約により `os=Windows, browser=Safari` は実行不能となるため、要求強度のペアは 8 個残ります。`coverage` は往復可能な最短形式の JSON 数値なので、完全カバレッジは `1.0` ではなく `1` になります。固定した入力とシードに対する出力順は決定的ですが、バージョンをまたぐ順序の契約ではありません。

`"invalid": true` とした値はポジティブカバレッジから除外され、別の異常系テストとして処理されます。無効値がある場合、出力には `negativeCoverage`（`totalTuples`・`coveredTuples`・`omittedTuples`・`coverageRatio`）も含まれます。`maxTests` はポジティブとネガティブを合わせたスイートに制限をかけるため、ネガティブの生成が未完了になることがあります。すべてのネガティブタプルが出力されたと決めつけず、`negativeCoverage` と `warnings` を確認してください。

### `analyze`

既存のテストスイートの t-wise カバレッジを分析します。

```bash
coverwise analyze --params <params.json> --tests <tests.json> [--strength <n>] [--constraints <file.json>]
```

| フラグ | 引数 | デフォルト |
|--------|------|-----------|
| `--params` | パスまたは標準入力。パラメータ配列そのもの、あるいは `parameters` と、任意で `constraints`・`strength` を持つオブジェクト。必須。 | — |
| `--tests` | パスまたは標準入力。テスト配列そのもの、あるいは `generate` が書き出す schema-v1 エンベロープ。必須。 | — |
| `--strength` | 4294967295 以下の正の整数。 | `--params` のオブジェクトが宣言する `strength`、なければ `2` |
| `--constraints` | パスまたは標準入力。式の配列そのもの、あるいは `constraints` 配列を持つオブジェクト。 | `--params` のオブジェクトが宣言する制約 |

それ以外の引数は `unknown argument` と終了コード `3` で拒否されます。タプルがカバレッジの対象から除外されるのは、すべての制約を満たす有効値の完全な割り当てへ補完できない場合だけです。そのため、制約付きで完全にカバーされたスイートは、部分的な制約評価だけを違反とみなされることなく 100% と報告されます。

`--tests` と `--existing` は、テスト配列そのものに加え `generate` が出力する schema-v1 エンベロープも受け付けます。したがって `coverwise generate input.json > tests.json` の出力をそのまま後続コマンドへ渡せます。

**強度をどこから取るか**。`--params` がパラメータ配列そのものではなくモデルオブジェクトの場合、その `strength` フィールドが必要タプル集合を定めるため、測定にもその値を使います。同じモデルを `generate` に通してから `analyze` に渡す際に、強度を書き直す必要はありません。`--strength` を明示した場合はモデルの値より優先されます。`--strength` はモデルの性質ではなく、その実行のために選ぶ分析用のつまみだからです。どちらも指定がなければペアワイズです。

`subModels` を宣言したモデルは終了コード `3` で拒否します。サブモデルはモデルの一部に独自の強度を与える仕組みですが、カバレッジレポートは 1 つの必要タプル集合を 1 つの強度で測るため、これを表現できません。グループごとに `--strength` で強度を指定して個別に分析してください。

**`--constraints` はモデルの制約をどう扱うか**。`--constraints` を明示した場合、そのファイルは `--params` が宣言した制約を置き換えます。両者がマージされることはないため、ファイルの内容が測定に使う制約のすべてになります。配列そのものでも `constraints` 配列を持つオブジェクトでもないトップレベル文書は、終了コード `3` で拒否します。制約を持たないモデルに対して `jq '.constraints'` が書き出す `null` 単体も同様です。こうした文書を「制約なし」と読んでしまうと、制約のない集合を測ってカバレッジ不足を報告し、その理由を説明するエラー出力が何も残らないためです。

**`--params` ファイル**

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ]
}
```

**`--tests` ファイル**

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

**出力**

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

9 ペアのうち 7 ペアしかスイートに現れないため、`analyze` は終了コード `2` を返します。`uncoveredCount` は未網羅タプルの総数です。`uncovered` 配列は診断用の上限で打ち切られ、そこから漏れた件数が `omittedUncovered` に入るため、配列の要素数は常に `uncoveredCount - omittedUncovered` と一致します。`coverageRatio` は表示用に丸めず、往復可能な最短形式で書き出されます。

**モデルが記述していない行**。`--params` のモデルが宣言していないパラメータや値を含むテストケースは分析対象になりません。除外の理由とともに `invalidTests` に報告され、カバレッジには一切寄与しないため、比率は残りの行だけで測られます。レポート全体はそれでも標準出力に書き出され、そのうえで `analyze` は終了コード `3` を返します。無効な行はカバレッジ不足より優先されます。モデルと合っていないスイートは、それについてのカバレッジ率が意味を持つ前に直す必要があるからです。カバレッジ率を読む前に `invalidTests` を読んでください。

### `extend`

既存のテストスイートを拡張してカバレッジを改善します。

```bash
coverwise extend --existing <tests.json> <input.json> [> output.json]
```

| 引数 | 値 | デフォルト |
|------|-----|-----------|
| `--existing` | パスまたは標準入力。テスト配列そのもの、あるいは `generate` が書き出す schema-v1 エンベロープ。必須。 | — |
| `<input.json>` | パスまたは標準入力。`generate` が読むものと同じモデル文書。必須。 | — |

**`--existing` ファイル**

```json
[
  { "os": "Windows", "browser": "Chrome" },
  { "os": "macOS", "browser": "Firefox" }
]
```

**入力フォーマット**

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

**出力フォーマット**

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

`extend` は `generate` と同じエンベロープを返すため、後続の処理では両者を差し替えられます。既存テストが与えられた順のまま先頭に並び、その後ろに新規テストが続きます。`--existing` の件数より後ろの行が、その実行で追加された分です。`stats` と `coverage` は追加分ではなく結合後のスイートを表します。

`extend` は `--existing` とモデルの `seeds` という 2 か所から行を読み、そのどちらも、その実行が持つ 1 つの合計バイト数の予算へ計上されます。片方だけなら収まっても、両方を合わせると収まらないことがあります。[入力上限](limits.md)を参照してください。

### `stats`

生成を実行せずにモデルの統計をプレビューします。

```bash
coverwise stats <input.json>
```

モデルは唯一の位置引数です。`stats` にフラグはありません。制約の構文とパラメータ参照を検証したうえで、制約による除外前の生のタプル数の見積もりを報告します。

**入力**

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

**出力**

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

`totalTuples` は制約で除外する前のペア数（3·3 + 3·2 + 3·2）です。`estimatedTests` は最大値数・強度・パラメータ数から求めて `totalTuples` で頭打ちにした、大まかな見積もりです。上限でも下限でもなく、`generate` が返すテストケース数は、このモデルのようにこれを下回ることも、上回ることもあります。

## パラメータ値のフォーマット

値は単純な文字列でも、オブジェクトでも指定できます。

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

値オブジェクトは `value`（文字列・数値・真偽値）を持ち、任意で `invalid`・`aliases`・`class` を持ちます。1 つのパラメータの中では、すべての値とすべてのエイリアスが、ASCII の大小文字を畳み込んだ後も互いに異なる名前になっている必要があります。`Chrome` を値と `Chromium` のエイリアスの両方に挙げる場合や、`Chrome` と `chrome` を並べる場合は、大小文字を区別しない解決の結果が一意に定まらないため、終了コード `3` で拒否されます。パラメータ名どうしにも同じ規則が適用されます。

書き込んだ値名はすべてこの大小文字を区別しない解決で扱われます。`seeds`・`tests`・`existing` の行に書いた値、`weights` のキー、制約式のオペランドは、ASCII の大小文字がどの表記であっても、また値そのものでもエイリアスでも、同じ値に解決されます。1 つのパラメータ内で 2 つの `weights` キーが同じ値を指すことはできません。適用できる重みは一方だけだからです。ただし、いずれかがモデルの宣言どおりの綴りであれば、そちらが優先されることで一意に決まります。`{"Windows": 5, "wINdows": 9}` は受理され `Windows` に `5` が付きますが、`{"wINdows": 5, "WINDOWS": 9}` は終了コード `3` になります。

## 出力フィールド

ここまでの出力はいずれも `"schemaVersion": 1` から始まります。これは CLI 自身の出力スキーマのバージョンです。v1 の形では空の配列フィールドも常に出力し、提案を `{ description, testCase }` として表し、`stats` のフィールド名を `subModelCount`・`constraintCount`・`parameters` に統一しています。

`generate` と `extend` は、この順で `schemaVersion`、`tests`、`uncoveredCount`、`omittedUncovered`、`negativeTests`、無効値を宣言したモデルでは `negativeCoverage`、`coverage`、`uncovered`、`stats`、クラスを宣言したモデルでは `classCoverage`、`suggestions`、`warnings`、`strength`、実行が失敗したときは `error` を書き出します。`analyze` は `schemaVersion`、`totalTuples`、`coveredTuples`、`coverageRatio`、`uncovered`、`uncoveredCount`、`omittedUncovered`、`invalidTests` を書き出します。`stats` は `schemaVersion`、`parameterCount`、`totalValues`、`strength`、`totalTuples`、`estimatedTests`、`subModelCount`、`constraintCount`、`parameters` を書き出します。

### クラスカバレッジ

値には `class` を宣言でき、同じパラメータの中で同じ `class` を宣言した値どうしが 1 つの等価クラスになります。いずれかの値がクラスを宣言していると、`generate` と `extend` の出力に `classCoverage` が加わります。

```json
{
  "parameters": [
    {
      "name": "browser",
      "values": [
        { "value": "Chrome", "class": "blink" },
        { "value": "Edge", "class": "blink" },
        { "value": "Firefox", "class": "gecko" }
      ]
    },
    {
      "name": "filesystem",
      "values": [
        { "value": "NTFS", "class": "journaling" },
        { "value": "FAT32", "class": "flat" }
      ]
    }
  ]
}
```

```json
{
  "schemaVersion": 1,
  "tests": [
    { "browser": "Edge", "filesystem": "NTFS" },
    { "browser": "Edge", "filesystem": "FAT32" },
    { "browser": "Firefox", "filesystem": "NTFS" },
    { "browser": "Chrome", "filesystem": "FAT32" },
    { "browser": "Chrome", "filesystem": "NTFS" },
    { "browser": "Firefox", "filesystem": "FAT32" }
  ],
  "uncoveredCount": 0,
  "omittedUncovered": 0,
  "negativeTests": [],
  "coverage": 1,
  "uncovered": [],
  "stats": {
    "totalTuples": 6,
    "coveredTuples": 6,
    "testCount": 6
  },
  "classCoverage": {
    "totalClassTuples": 4,
    "coveredClassTuples": 4,
    "classCoverageRatio": 1
  },
  "suggestions": [],
  "warnings": [],
  "strength": 2
}
```

クラスカバレッジは、クラスを宣言したパラメータだけを対象に、モデルの強度をその個数で頭打ちにして測ります。上のモデルはどちらのパラメータも 2 クラスなので、クラス側の必要タプル集合は 2·2 = 4 組、`stats` が数える値のペアは 3·2 = 6 組になります。

### 異常系テストとテスト件数

`stats.testCount` はポジティブと異常系のケースを合算した件数です。一方 `stats.totalTuples` と `stats.coveredTuples` はポジティブなスイートだけを表します。無効値を 1 つ持つモデルで違いが見えます。

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", { "value": "IE", "invalid": true }] }
  ]
}
```

```json
{
  "schemaVersion": 1,
  "tests": [
    { "os": "Linux", "browser": "Chrome" },
    { "os": "Windows", "browser": "Firefox" },
    { "os": "Windows", "browser": "Chrome" },
    { "os": "Linux", "browser": "Firefox" }
  ],
  "uncoveredCount": 0,
  "omittedUncovered": 0,
  "negativeTests": [
    { "os": "Windows", "browser": "IE" },
    { "os": "Linux", "browser": "IE" }
  ],
  "negativeCoverage": {
    "totalTuples": 2,
    "coveredTuples": 2,
    "omittedTuples": 0,
    "coverageRatio": 1
  },
  "coverage": 1,
  "uncovered": [],
  "stats": {
    "totalTuples": 4,
    "coveredTuples": 4,
    "testCount": 6
  },
  "suggestions": [],
  "warnings": [],
  "strength": 2
}
```

`testCount` は 6 で、`tests` の 4 行と `negativeTests` の 2 行の合計です。`totalTuples` は有効値だけで数えた 2·2 = 4 組です。実行規模を `testCount` から見積もる場合は両方のスイートを数えており、カバレッジを比べる場合はポジティブなスイートだけを読んでいることになります。

### error オブジェクト

`generate` や `extend` の実行が失敗した場合もレポートは書き出され、末尾に `error` オブジェクトが付きます。同時には成り立たない 2 つの制約がその例です。

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ],
  "constraints": [
    "os = Windows",
    "os != Windows"
  ]
}
```

```json
{
  "schemaVersion": 1,
  "tests": [],
  "uncoveredCount": 0,
  "omittedUncovered": 0,
  "negativeTests": [],
  "coverage": 0,
  "uncovered": [],
  "stats": {
    "totalTuples": 0,
    "coveredTuples": 0,
    "testCount": 0
  },
  "suggestions": [],
  "warnings": [
    "Constraints are unsatisfiable: No complete assignment using valid values satisfies all constraints"
  ],
  "strength": 2,
  "error": {
    "code": 1,
    "message": "Constraints are unsatisfiable: No complete assignment using valid values satisfies all constraints"
  }
}
```

同じテキストは `error: ` を前置して標準エラーにも出力され、実行は終了コード `1` で終わります。

`error.code` は JavaScript や Python のサーフェスが使う文字列の語彙ではなく**数値**です。`1` が制約エラー、`2` がカバレッジ不足、`3` が入力不正、`4` がタプル数の爆発です。プロセスの終了コードと同じ数値になりますが、タプル数の爆発だけは例外で、CLI は終了コード `3` を返します。

実行が失敗したときは `error.message` を、成功したときは `warnings` を読んでください。失敗時は両方に同じテキストが入るため、`warnings` だけを読む利用側でもどちらの場合も診断を得られます。成功か失敗かで分岐する利用側は `error` を読み、`warnings` は参考情報として扱ってください。

## 終了コード

| コード | 意味 |
|-------|------|
| `0` | コマンドが完了しました。`generate`・`analyze`・`extend` ではカバレッジが 100% に達したことも意味します。`stats` と `--help` ではカバレッジの意味を持ちません。 |
| `1` | 制約エラー。式が解析できないか、有効値のどの割り当てでも制約を満たせません。 |
| `2` | カバレッジ不足。理由は問いません。`maxTests` の上限、実現不能なタプル、ペアを取りこぼしたスイートのいずれでもこの値になります。 |
| `3` | 入力不正、呼び出し方の誤り、または標準出力への書き込み失敗。 |

終了コード `3` はモデルの不正だけではありません。呼び出し方が誤っている場合も `3` です。`analyze` はモデルが記述していない行がスイートに含まれるとき `3` を返します。標準出力への書き込みが失敗した場合、つまりパイプを閉じた読み手、ディスクの空き容量切れ、閉じられたディスクリプタなどでは、`error: cannot write to standard output` を出して `3` で終わります。

## 入力の上限

どのコマンドも、読み込む入力に同じ上限を適用します。いずれかを超えた場合は終了コード `3` です。上限の一覧、どれが先に効くかを決める計算、それぞれのメッセージは[入力上限](limits.md)にまとめてあります。

## パイプ

標準的な Unix パイプが入力側・出力側とも使えます。入力パスの代わりに `-` を渡すと、その JSON を標準入力から読み込みます。

```bash
# Generate and count the suite
coverwise generate input.json | jq '.tests | length'

# Feed output to another tool
coverwise generate input.json | my-test-runner --from-stdin

# Build a model on the fly and generate from it
jq '{parameters: .matrix}' config.json | coverwise generate -

# Measure a generated suite without an intermediate file
coverwise generate input.json | coverwise analyze --params input.json --tests -
```

標準入力は一度しか読めないため、1 つのコマンドの両方の入力に `-` を渡した場合は、空として読むのではなく拒否されます。

読み手が途中で止まると、実行は終了コード `3` で終わります。`coverwise generate big.json | head -c 200`、最初の一致で終了する `jq -e`、`q` で抜ける `less` は、いずれもレポートを書いている最中にパイプを閉じます。CLI はこれを `error: cannot write to standard output` と終了コード `3` として報告します。行単位で読む `head` そのものは該当しません。CLI は文書を 1 行で書き出すため、読み手が最初に見る改行がそのまま出力の終わりだからです。パイプラインで `3` を受け取ったときは、入力の誤りより先に、出力を書き切れなかった可能性を疑ってください。

## 使用例

```bash
# Basic pairwise generation
coverwise generate input.json

# 3-wise coverage analysis
coverwise analyze --params params.json --tests tests.json --strength 3

# Extend an existing suite against a model
coverwise extend --existing tests.json input.json > updated.json

# Quick model size check
coverwise stats input.json | jq '.totalTuples'
```

## 次に読むもの

- [はじめかた](getting-started.md) — 各サーフェスでのインストールと、最初のスイート生成
- [制約構文](constraints.md) — `constraints` 配列が受け付ける式の言語
- [入力上限](limits.md) — CLI が受け付ける範囲と、上限に達したときの報告
- [Python API](python-api.md) — 同じ実行ファイルを Python から駆動する方法
- [C++ API](cpp-api.md) — 実行ファイルを起動する代わりにエンジンをリンクする方法
- [用語集](glossary.md) — このページが使う語彙
