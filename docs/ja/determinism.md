# 決定性

同じ妥当なモデルと同じシードからは、同じスイートが同じ順序で 1 行ずつそのまま得られます。これは coverwise の 1 つのビルドについての性質です。マシン・OS・サーフェスをまたいでも成り立ちますが、バージョンをまたいでは成り立ちません。

## シードが固定するもの

生成では乱択が起きます。同じ数の不足を埋められる値が複数あるとき、どれを置くかという選択です。`seed` はそのすべてを固定します。

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'arch',    values: ['x64', 'arm64'] },
    { name: 'channel', values: ['stable', 'preview'] },
  ],
  seed: 0,
});

// result.tests, in order:
// { os: 'macOS', arch: 'arm64', channel: 'preview' }
// { os: 'macOS', arch: 'x64', channel: 'stable' }
// { os: 'Linux', arch: 'x64', channel: 'preview' }
// { os: 'Linux', arch: 'arm64', channel: 'stable' }
// { os: 'Windows', arch: 'arm64', channel: 'stable' }
// { os: 'Windows', arch: 'x64', channel: 'preview' }
```

このモデルの 16 ペアは 6 行で網羅されます。何度実行しても同じ 6 行が得られます。

## シードごとに変わる、妥当なスイート

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'arch',    values: ['x64', 'arm64'] },
    { name: 'channel', values: ['stable', 'preview'] },
  ],
  seed: 42,
});

// result.tests, in order:
// { os: 'Windows', arch: 'x64', channel: 'stable' }
// { os: 'macOS', arch: 'x64', channel: 'preview' }
// { os: 'macOS', arch: 'arm64', channel: 'stable' }
// { os: 'Windows', arch: 'arm64', channel: 'preview' }
// { os: 'Linux', arch: 'x64', channel: 'preview' }
// { os: 'Linux', arch: 'arm64', channel: 'stable' }
```

行の中身は違いますが、6 行という大きさは同じで、網羅される 16 ペアも同じです。どちらか一方が正解ではありません。どちらも要件を満たしており、シードはその実行がどちらを出すかを決めるだけです。

## seed フィールド

`seed` の既定値は 0 です。`seed` を一度も書かないモデルでも再現可能です。正規の定義域は 0 から 4294967295、つまり 2^32 - 1 までです。

この範囲外のシードは、C++ API を含むすべてのサーフェスが入力不正として拒否します。`GenerateOptions::seed` が `uint64_t` で宣言されているのは、JSON からシードを読むサーフェスが範囲外の数値をそのまま受理ゲートまで運べるようにするためです。ゲートは定義域を示すメッセージとともにそこで拒否します。下位 32 ビットへ切り詰めることはありません。

カバレッジ解析は乱択を一切行いません。`analyzeCoverage` は必要タプル集合を列挙して数えるだけなので、シードが何であっても同じレポートを返します。`extendTests` がシードを使うのは追加する行に対してだけで、既存の行は結果の先頭にそのまま保持されます。

## サーフェス間で一致すること

ネイティブライブラリ、CLI、WASM ビルドは同じ C++ のコードです。WASM ビルドはそのコアを Emscripten でコンパイルしたものなので、テストによってではなく構成上一致します。

`@libraz/coverwise/pure` のピュア TypeScript 版は、同じエンジンの 2 つ目の実装です。これは `js/compat.test.ts` のパリティスイートによって WASM のサーフェスに縛られています。このスイートは `generate`・`analyzeCoverage`・`extendTests`・`estimateModel` の結果全体と、両者が投げるエラーの code・message・detail を比較します。比較しているのは WASM とピュア TypeScript だけなので、これは JavaScript 側 2 つについての根拠であって、ネイティブ C++ については何も示しません。

すべての土台にあるジェネレータは 1 つです。SplitMix32 でシードした xoshiro128\*\* を C++ で書き、TypeScript にそのまま写してあるため、同じシードが両方で同じ乱数列を駆動します。

## 決定性が保証しないこと

**coverwise のバージョンをまたいだ安定性。** 保証しているのはアルゴリズムが入力の関数であることであって、アルゴリズムが変わらないことではありません。構築処理の修正や改善によって、同じモデルと同じシードから別の（同じく妥当な）スイートが出ることがあります。バイト単位で同じスイートが必要ならバージョンを固定するか、生成したスイートをコミットして意図的に再生成してください。

**モデルが変わったときの安定性。** モデルは入力そのものであり、そこには値を宣言する順序も含まれます。上の `os` のリストを逆順にすると、別の 6 行になります。値を 1 つ足しただけで全行が動くこともあります。

**最良のスイートであること。** シードは妥当なカバリング配列を 1 つ選ぶだけで、別のシードならもっと小さくなるかどうかについては何も言いません。

**実行時間について。** 決定性は出力についての性質であり、実行にかかる時間についての性質ではありません。

CI で得られるのは、実行のたびに揺れないスイートです。生成物をコミットしておけば、その差分は「モデルが変わった」か「coverwise が変わった」のどちらかを意味します。実行のたまたまの揺らぎではありません。

## 次に読むもの

- [実例集](examples.md) — `seeds` や `weights` など、どのスイートが出るかを左右するフィールド
- [サーフェスの選び方](choosing-a-surface.md) — 上に挙げたサーフェスのどれを使うか
- [JavaScript API](js-api.md) — モデルの中での `seed` と、範囲外の値が投げるエラー
- [CLI リファレンス](cli.md) — JSON のモデル文書での同じフィールド
- [FAQ と制限](faq.md) — 同じスイートが 2 度得られるか、そして実行時間を載せていない理由
