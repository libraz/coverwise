# 制約構文

制約は、生成テストに含めてはならない無効なパラメータの組み合わせを定義します。coverwise は生成中に制約を評価し、違反する候補を剪定します。

## 基本構文

### 等値

```
IF os = Windows THEN browser != Safari
```

パラメータ名と値のマッチングは**大文字小文字を区別しません**。

### IF / THEN / ELSE

```
IF os = macOS THEN browser = Safari OR browser = Chrome
IF os = macOS THEN browser = Safari ELSE browser != Safari
```

`ELSE` はオプションです。

### 論理演算子

```
IF os = Windows AND device = phone THEN browser = Edge
IF os = macOS OR os = iOS THEN browser = Safari
IF NOT os = Linux THEN arch = x64 OR arch = arm64
```

優先順位: `NOT` > `AND` > `OR`。括弧で上書き可能：

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
```

`*` は任意の文字列にマッチします。

### パラメータ比較

2つのパラメータを直接比較：

```
IF source = target THEN mode = copy
IF input_format != output_format THEN convert = true
```

## 制約の組み合わせ

複数の制約を配列で渡します。すべての制約が同時に満たされる必要があります：

```typescript
generate({
  parameters: [/* ... */],
  constraints: [
    'IF os = Windows THEN browser != Safari',
    'IF os = macOS THEN browser != IE',
    'IF device = phone THEN os IN {iOS, Android}',
  ],
});
```

## 無条件制約

常に適用される制約は `IF` を省略できます：

```
browser != IE
os = Windows OR os = macOS
```

これは以下と同等です：

```
IF true THEN browser != IE
```

## 複雑な例

### 相互排他

```
IF os = iOS THEN browser = Safari
IF browser = Safari THEN os = macOS OR os = iOS
```

### プラットフォーム固有の機能

```
IF os = Windows THEN filesystem IN {NTFS, FAT32}
IF os = macOS THEN filesystem IN {APFS, HFS+}
IF os = Linux THEN filesystem IN {ext4, btrfs, xfs}
```

### 複合条件

```
IF os = Windows AND browser = Chrome AND arch = arm64 THEN mode = compatibility
IF (os = iOS OR os = Android) AND screen_size < 7 THEN device = phone
```

## 制約エラー

制約によって特定のタプルが不可能になった場合、coverwise は適切に処理します：

- 制約違反の組み合わせはカバレッジ対象から除外
- ジェネレータは制約違反テストケースを生成しない
- 未カバータプルは制約による除外かどうかを報告

制約が矛盾する場合（有効な組み合わせが存在しない場合）、生成はエラーコード `CONSTRAINT_ERROR` を返します。

## 制約ビルダー（JavaScript）

fluent API でプログラマティックに制約を構築します。ビルダーオブジェクトは `toString()` で有効な制約文字列を生成します。

```typescript
import { when, not, allOf, anyOf } from '@libraz/coverwise';
```

### 基本比較

```typescript
when('os').eq('Windows')           // os = Windows
when('browser').ne('Safari')       // browser != Safari
when('version').gt(3)              // version > 3
when('version').gte(10)            // version >= 10
when('priority').lt(5)             // priority < 5
when('priority').lte(1)            // priority <= 1
```

### IN と LIKE

```typescript
when('env').in('staging', 'prod')  // env IN {staging, prod}
when('browser').like('chrome*')    // browser LIKE chrome*
```

### パラメータ間比較

```typescript
when('start_date').lt('end_date')  // start_date < end_date
```

### IF / THEN / ELSE

```typescript
when('os').eq('Windows')
  .then(when('browser').ne('Safari'))
// IF os = Windows THEN browser != Safari

when('os').eq('mac')
  .then(when('browser').ne('ie'))
  .else(when('arch').ne('arm'))
// IF os = mac THEN browser != ie ELSE arch != arm
```

### 論理合成

```typescript
// AND
allOf(when('os').eq('win'), when('arch').eq('x64'))
// os = win AND arch = x64

// OR
anyOf(when('os').eq('win'), when('os').eq('linux'))
// os = win OR os = linux

// NOT
not(allOf(when('os').eq('win'), when('browser').eq('safari')))
// NOT (os = win AND browser = safari)
```

### generate() での使用

ビルダーオブジェクトは `.toString()` で文字列に変換して使用します：

```typescript
const cw = await Coverwise.create();

cw.generate({
  parameters: [/* ... */],
  constraints: [
    when('os').eq('Windows').then(when('browser').ne('Safari')).toString(),
    when('device').eq('phone').then(when('os').in('iOS', 'Android')).toString(),
  ],
});
```

文字列制約とビルダー制約は自由に混在できます。
