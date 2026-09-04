# 制約構文

制約が何をするのか、つまり構築を枝刈りすると同時に coverwise が網羅すべきタプルの集合そのものを縮めることについては、[制約と必要タプル集合](primer/constraints-and-the-universe.md)で扱っています。このページは制約を書くためのリファレンスです。

制約は、生成される各行が満たすべき、パラメータ値についてのブール式です。行を組み立てる途中で適用されるため、制約に反する行はそもそも作られません。同じ制約文字列は JavaScript、Python、C++ API、CLI の JSON 入力で使えます。下記の fluent builder は JavaScript 専用です。

以下の TypeScript サンプルはすべて断片です。実行可能なモジュールでは、まず次の準備コードを置いてください。式だけを示すサンプルは、`constraints` 配列に入れます。

```typescript
import { Coverwise, when, not, allOf, anyOf } from '@libraz/coverwise';

const cw = await Coverwise.create();
```

## 式の書き方

### パラメータと値を比較する

```
IF os = Windows THEN browser != Safari
```

パラメータ名と値のマッチングは**大文字小文字を区別しません**。解決時に ASCII の大小文字を畳み込むため、大小文字だけが異なるパラメータ名を 2 つ持つモデルや、1 つのパラメータ内で大小文字だけが異なる値・エイリアスを持つモデルは使えません。`os = WINDOWS` の指す先が一意に定まらないためで、こうしたモデルは生成を始める前に入力不正として拒否されます。

### 値にクォートが必要になる場合

クォートなしのトークンに書けるのは、ASCII の英数字、`_`、`-`、`.`、および非 ASCII 文字だけです。それ以外の文字（空白、`+`、`%`、`@`、`/`、`(`、`)` など）を含む値は、二重引用符または単一引用符で囲んだ文字列として書きます。

```
IF os = macOS THEN filesystem = "HFS+"
IF language = "C++" THEN build_system != 'make (BSD)'
IF release = "1.0 (beta)" THEN channel = preview
```

クォート内では `\"` が引用符そのものを、`\\` がバックスラッシュをエスケープします。クォートされたトークンは常にリテラル値として扱われ、キーワードともパラメータ名とも解釈されません。そのため `AND` という綴りの値や、パラメータ名と衝突する値も、クォートすれば曖昧さがなくなります。

JavaScript のビルダーは自動でクォートします。`when('filesystem').eq('HFS+')` は `filesystem = "HFS+"` を出力するため、クォートを意識する必要があるのは手書きの制約文字列だけです。

### IF / THEN / ELSE で条件付きにする

```
IF os = macOS THEN browser = Safari OR browser = Chrome
IF os = macOS THEN browser = Safari ELSE browser != Safari
```

`ELSE` は省略できます。

### 中置形の IMPLIES

`A IMPLIES B` は `IF A THEN B` と同じ規則を表します。

```
os = Linux IMPLIES arch != arm32
```

`IMPLIES` の結合はどの演算子よりも弱く、1 つの式に高々 1 つしか書けません。これより深い入れ子が必要な場合は、`IF` / `THEN` と括弧を使ってください。

### AND・OR・NOT で条件を組み合わせる

```
IF os = Windows AND device = phone THEN browser = Edge
IF os = macOS OR os = iOS THEN browser = Safari
IF NOT os = Linux THEN arch = x64 OR arch = arm64
```

優先順位は `NOT` > `AND` > `OR` > `IMPLIES` です。括弧で上書きできます。

```
IF (os = Windows OR os = Linux) AND device = desktop THEN browser != Safari
```

### 数値を比較する

関係演算子は両辺を数値として読みます。

```
IF age >= 18 THEN plan != child
IF price < 0 THEN status = error
IF count > 100 THEN mode = batch
IF priority <= 3 THEN queue = high
```

サポートする演算子は `=`、`!=`、`<`、`<=`、`>`、`>=` です。

### IN で値の集合にマッチさせる

`IN` は、波括弧で書いた集合リテラルへの所属を判定します。

```
IF os IN {Windows, macOS} THEN arch != arm32
IF browser IN {Chrome, Edge, Chromium} THEN engine = blink
```

### LIKE でパターンにマッチさせる

`LIKE` は値を glob パターンに照らします。

```
IF browser LIKE Chrome* THEN engine = blink
IF version LIKE *.0.0 THEN is_major = true
IF code LIKE v?.0 THEN generation = first
```

`*` は空文字列を含む任意の文字列に、`?` はちょうど 1 文字にマッチします。どちらもバイトではなく Unicode コードポイント単位で数えます。また、クォートしたパターンの中でもワイルドカードの意味は失われません。`*` や `?` そのものにマッチさせるエスケープはないため、値が本当にこれらの文字を含む場合は `LIKE` ではなく `=` で比較してください。ワイルドカードを含まないパターンは、通常の等値比較になります。

### 2 つのパラメータを比較する

比較のどちら側にも、値の代わりにパラメータ名を書けます。

```
IF source = target THEN mode = copy
IF input_format != output_format THEN convert = true
```

## パーサが受け付けるキーワードとワイルドカード

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

## 制約を複数渡す

複数の制約を配列で渡します。すべての制約が同時に満たされる必要があります。

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

1 つのモデルが持てる制約の上限は[入力上限](limits.md)に載せています。

## 常に適用される制約

`IF` を書かずに置いた式は、それ自体が規則になります。生成されるすべてのテストケースがそれを満たします。

```
browser != IE
os = Windows OR os = macOS
```

この文法には真偽値リテラルがないため、常に真となる前件を書き下すことはできません。`IF` 節を書く代わりに省略してください。

## 複数の式で組み立てる規則

1 つの式では書けない規則は、複数の式に分けて `constraints` の別々の要素として書きます。それらは同時に成り立つため、以下の形はどれも追加の構文なしに組み合わせられます。

### 互いを排除する 2 つの値

方向ごとに 1 つの式を書きます。1 つ目はそのプラットフォームが使うブラウザを固定し、2 つ目はそのブラウザが現れてよいプラットフォームを限定します。

```
IF os = iOS THEN browser = Safari
IF browser = Safari THEN os = macOS OR os = iOS
```

### プラットフォームによって変わる選択

判断のもとになるパラメータの値ごとに式を 1 つ書き、もう一方のパラメータをその値が対応する集合に絞ります。

```
IF os = Windows THEN filesystem IN {NTFS, FAT32}
IF os = macOS THEN filesystem IN {APFS, "HFS+"}
IF os = Linux THEN filesystem IN {ext4, btrfs, xfs}
```

### 複数の条件が必要な帰結

前件の中の `AND` は、すべての条件が同時に成り立つ場合にだけ帰結を適用させます。括弧で囲んだ `OR` は、そのうち 1 つの条件だけを広げます。

```
IF os = Windows AND browser = Chrome AND arch = arm64 THEN mode = compatibility
IF (os = iOS OR os = Android) AND screen_size < 7 THEN device = phone
```

## 制約エラー

制約によって不可能になったタプルは、未網羅として残るのではなく必要タプル集合そのものから外れます。したがって `uncovered` には現れません。この違いは[制約と必要タプル集合](primer/constraints-and-the-universe.md)で小さなモデルを使って追っています。

制約が矛盾していて有効な組み合わせが 1 つも存在しない場合、生成はエラーコード `CONSTRAINT_ERROR` を返します。

## JavaScript で制約を組み立てる

fluent API を使ってプログラムから制約を組み立てます。ビルダーオブジェクトは `toString()` で有効な制約文字列を生成します。

クォートするかどうかはメソッドごとに決まります。オペランドが置かれる位置によって、パーサがそれをどう読むかが変わるためです。手作業でエスケープする必要はありません。メソッドが決めるのは、そのオペランドがそもそも値として読まれるかどうかです。

- `eq()` と `ne()` は文字列オペランドを常にクォートします。ここでベアのトークンを置くと、同名のパラメータが存在する場合にパラメータ参照として解決され、値の比較が黙ってパラメータ同士の比較にすり替わってしまいます。
- `in()` と `like()` は、1 つのベアのトークンとして書けない値だけをクォートします。`in('staging', 'prod')` は `env IN {staging, prod}` を、`like('chrome*')` は `browser LIKE chrome*` を出力します。ワイルドカードの `*` と `?` はどちらの形でも意味を保ちます。
- `gt()`・`gte()`・`lt()`・`lte()` は、比較する相手として数値を取ります。ここでの**文字列**オペランドはパラメータ名であり、ベアのまま出力されます。1 つのベアのトークンとして書けない場合はその場で拒否されます。つまり `when('status').lt('ok')` は、値 `ok` ではなく `ok` という名前のパラメータと `status` を比較します。

### 値や数値と比較する

```typescript
when('os').eq('Windows')           // os = "Windows"
when('browser').ne('Safari')       // browser != "Safari"
when('version').gt(3)              // version > 3
when('version').gte(10)            // version >= 10
when('priority').lt(5)             // priority < 5
when('priority').lte(1)            // priority <= 1
```

### IN や LIKE の条件を作る

```typescript
when('env').in('staging', 'prod')  // env IN {staging, prod}
when('browser').like('chrome*')    // browser LIKE chrome*
when('code').like('v?.0')          // code LIKE v?.0
```

### パラメータどうしを比較する

```typescript
when('start_date').lt('end_date')  // start_date < end_date
```

### 条件付き制約を組み立てる

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

### and・or・not で条件を合成する

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

### どのメソッドがどの式を出力するか

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

### ビルダーを generate() に渡す

ビルダーオブジェクトは `.toString()` で文字列に変換して使います。

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

## 次に読むもの

- [制約と必要タプル集合](primer/constraints-and-the-universe.md) — 制約が、coverwise の網羅対象となるタプルの集合に何をするか
- [実例集](examples.md) — 制約付きモデルを実行できるレシピとして示し、生成される行数まで添えたページ
- [JavaScript API](js-api.md) — モデルの中で `constraints` が置かれる位置と、ビルダーが返す型
- [CLI リファレンス](cli.md) — 同じ式を JSON のモデル文書に書く形
- [FAQ と制限](faq.md) — 除外されたタプルが不足ではない理由と、綴りが 2 通りある値が拒否される理由
- [用語集](glossary.md) — 制約、制約枝刈り、必要タプル集合
