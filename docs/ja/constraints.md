# 制約構文

制約は、coverwise が生成時に除外する無効なパラメータの組み合わせを定義します。同じ制約文字列は JavaScript、C++ API、CLI の JSON 入力で使えます。下記の fluent builder は JavaScript 専用です。

以下の TypeScript サンプルはすべて断片です。実行可能なモジュールでは、まず次の準備コードを置いてください。式だけを示すサンプルは、`constraints` 配列に入れます。

```typescript
import { Coverwise, when, not, allOf, anyOf } from '@libraz/coverwise';

const cw = await Coverwise.create();
```

## 基本構文

### 等値

```
IF os = Windows THEN browser != Safari
```

パラメータ名と値のマッチングは**大文字小文字を区別しません**。解決時に ASCII の大小文字を畳み込むため、大小文字だけが異なるパラメータ名を 2 つ持つモデルや、1 つのパラメータ内で大小文字だけが異なる値・エイリアスを持つモデルは使えません。`os = WINDOWS` の指す先が一意に定まらないためで、こうしたモデルは生成を始める前に入力不正として拒否されます。

### クォートされた値

クォートなしのトークンに書けるのは、ASCII の英数字、`_`、`-`、`.`、および非 ASCII 文字だけです。それ以外の文字（空白、`+`、`%`、`@`、`/`、`(`、`)` など）を含む値は、二重引用符または単一引用符で囲んだ文字列として書きます：

```
IF os = macOS THEN filesystem = "HFS+"
IF language = "C++" THEN build_system != 'make (BSD)'
IF release = "1.0 (beta)" THEN channel = preview
```

クォート内では `\"` が引用符そのものを、`\\` がバックスラッシュをエスケープします。クォートされたトークンは常にリテラル値として扱われ、キーワードともパラメータ名とも解釈されません。そのため `AND` という綴りの値や、パラメータ名と衝突する値も、クォートすれば曖昧さがなくなります。

JavaScript のビルダーは自動でクォートします。`when('filesystem').eq('HFS+')` は `filesystem = "HFS+"` を出力するため、クォートを意識する必要があるのは手書きの制約文字列だけです。

### IF / THEN / ELSE

```
IF os = macOS THEN browser = Safari OR browser = Chrome
IF os = macOS THEN browser = Safari ELSE browser != Safari
```

`ELSE` はオプションです。

### IMPLIES

`A IMPLIES B` は `IF A THEN B` と同じ規則を表します：

```
os = Linux IMPLIES arch != arm32
```

`IMPLIES` の結合はどの演算子よりも弱く、1 つの式に高々 1 つしか書けません。これより深い入れ子が必要な場合は、`IF` / `THEN` と括弧を使ってください。

### 論理演算子

```
IF os = Windows AND device = phone THEN browser = Edge
IF os = macOS OR os = iOS THEN browser = Safari
IF NOT os = Linux THEN arch = x64 OR arch = arm64
```

優先順位: `NOT` > `AND` > `OR` > `IMPLIES`。括弧で上書き可能です：

```
IF (os = Windows OR os = Linux) AND device = desktop THEN browser != Safari
```

### 関係演算子

数値に対して使用：

```
IF age >= 18 THEN plan != child
IF price < 0 THEN status = error
IF count > 100 THEN mode = batch
IF priority <= 3 THEN queue = high
```

サポートする演算子: `=`、`!=`、`<`、`<=`、`>`、`>=`。

### IN 演算子

値の集合に対するマッチ：

```
IF os IN {Windows, macOS} THEN arch != arm32
IF browser IN {Chrome, Edge, Chromium} THEN engine = blink
```

### LIKE 演算子

ワイルドカードによるパターンマッチング：

```
IF browser LIKE Chrome* THEN engine = blink
IF version LIKE *.0.0 THEN is_major = true
IF code LIKE v?.0 THEN generation = first
```

`*` は空文字列を含む任意の文字列に、`?` はちょうど 1 文字にマッチします。どちらもバイトではなく Unicode コードポイント単位で数えます。また、クォートしたパターンの中でもワイルドカードの意味は失われません。`*` や `?` そのものにマッチさせるエスケープはないため、値が本当にこれらの文字を含む場合は `LIKE` ではなく `=` で比較してください。ワイルドカードを含まないパターンは、通常の等値比較になります。

### パラメータ比較

2つのパラメータを直接比較：

```
IF source = target THEN mode = copy
IF input_format != output_format THEN convert = true
```

## キーワードとワイルドカードの一覧

| キーワード | 役割 |
|-----------|------|
| `IF` | 条件付き制約を開始します。 |
| `THEN` | `IF` の帰結を導きます。 |
| `ELSE` | `IF` の代替分岐（省略可）です。 |
| `IMPLIES` | `IF ... THEN ...` の中置形です。 |
| `AND` | 論理積。 |
| `OR` | 論理和。 |
| `NOT` | 否定。 |
| `IN` | 集合リテラルへの所属。 |
| `LIKE` | glob パターンマッチ。 |

キーワードは大文字小文字を区別しません。同じ綴りの値を書くときはクォートしてください。

| ワイルドカード | マッチする対象 |
|---------------|---------------|
| `*` | 空文字列を含む任意の文字列。 |
| `?` | ちょうど 1 文字。 |

## 制約の組み合わせ

複数の制約を配列で渡します。すべての制約が同時に満たされる必要があります：

```typescript
cw.generate({
  parameters: [/* ... */],
  constraints: [
    'IF os = Windows THEN browser != Safari',
    'IF os = macOS THEN browser != IE',
    'IF device = phone THEN os IN {iOS, Android}',
  ],
});
```

## 無条件制約

常に適用される制約は `IF` を省略できます。式そのものが規則となり、生成されるすべてのテストケースがそれを満たします：

```
browser != IE
os = Windows OR os = macOS
```

この文法には真偽値リテラルがないため、常に真となる前件を書き下すことはできません。`IF` 節を書く代わりに省略してください。

## 複雑な例

### 相互排他

```
IF os = iOS THEN browser = Safari
IF browser = Safari THEN os = macOS OR os = iOS
```

### プラットフォーム固有の機能

```
IF os = Windows THEN filesystem IN {NTFS, FAT32}
IF os = macOS THEN filesystem IN {APFS, "HFS+"}
IF os = Linux THEN filesystem IN {ext4, btrfs, xfs}
```

### 複合条件

```
IF os = Windows AND browser = Chrome AND arch = arm64 THEN mode = compatibility
IF (os = iOS OR os = Android) AND screen_size < 7 THEN device = phone
```

## 制約エラー

制約によって特定のタプルが不可能になった場合、coverwise は適切に処理します：

- 有効な完全テストケースに含められないタプルは、カバレッジの対象集合から除外されます
- 制約違反のタプルは、そのため `uncovered` には現れません
- `maxTests` の制限で完全に網羅できない場合、`uncovered` には残った必要タプルだけが入ります

制約が矛盾する場合（有効な組み合わせが存在しない場合）、生成はエラーコード `CONSTRAINT_ERROR` を返します。

## 制約ビルダー（JavaScript）

fluent API を使ってプログラムから制約を組み立てます。ビルダーオブジェクトは `toString()` で有効な制約文字列を生成します。

文字列のオペランドは常にクォートして出力されるため、どんな値でも自分でエスケープすることなくビルダーに渡せます。

### 基本比較

```typescript
when('os').eq('Windows')           // os = "Windows"
when('browser').ne('Safari')       // browser != "Safari"
when('version').gt(3)              // version > 3
when('version').gte(10)            // version >= 10
when('priority').lt(5)             // priority < 5
when('priority').lte(1)            // priority <= 1
```

### IN と LIKE

```typescript
when('env').in('staging', 'prod')  // env IN {staging, prod}
when('browser').like('chrome*')    // browser LIKE chrome*
when('code').like('v?.0')          // code LIKE v?.0
```

### パラメータ間比較

```typescript
when('start_date').lt('end_date')  // start_date < end_date
```

### IF / THEN / ELSE と IMPLIES

```typescript
when('os').eq('Windows')
  .then(when('browser').ne('Safari'))
// IF os = "Windows" THEN browser != "Safari"

when('os').eq('mac')
  .then(when('browser').ne('ie'))
  .else(when('arch').ne('arm'))
// IF os = "mac" THEN browser != "ie" ELSE arch != "arm"

when('os').eq('linux')
  .implies(when('arch').ne('arm'))
// os = "linux" IMPLIES arch != "arm"
```

`else()` は `then()` の戻り値だけが持ちます。2 つ目の `ELSE` はこの文法で解釈できないためです。

### 論理合成

```typescript
// AND
allOf(when('os').eq('win'), when('arch').eq('x64'))
when('os').eq('win').and(when('arch').eq('x64'))
// os = "win" AND arch = "x64"

// OR
anyOf(when('os').eq('win'), when('os').eq('linux'))
when('os').eq('win').or(when('os').eq('linux'))
// os = "win" OR os = "linux"

// NOT
not(allOf(when('os').eq('win'), when('browser').eq('safari')))
// NOT (os = "win" AND browser = "safari")
```

`allOf()` と `anyOf()` は、それぞれ `and()` と `or()` で任意個の条件をまとめます。引数が空の場合は拒否されます。

### メソッド一覧

| メソッド | 出力 |
|---------|------|
| `eq(value)` | `param = "value"` |
| `ne(value)` | `param != "value"` |
| `gt(n)` | `param > n` |
| `gte(n)` | `param >= n` |
| `lt(n)` | `param < n` |
| `lte(n)` | `param <= n` |
| `in(...values)` | `param IN {…}` |
| `like(pattern)` | `param LIKE pattern` |
| `and(other)` | `… AND …` |
| `or(other)` | `… OR …` |
| `then(consequence)` | `IF … THEN …` |
| `implies(consequence)` | `… IMPLIES …` |
| `else(alternative)` | `… ELSE …`。`then()` の後でのみ使えます。 |
| `toString()` | `generate()` に渡す制約文字列。 |

### generate() での使用

ビルダーオブジェクトは `.toString()` で文字列に変換して使用します：

```typescript
cw.generate({
  parameters: [/* ... */],
  constraints: [
    when('os').eq('Windows').then(when('browser').ne('Safari')).toString(),
    when('device').eq('phone').then(when('os').in('iOS', 'Android')).toString(),
  ],
});
```

文字列制約とビルダー制約は自由に混在できます。
