# サーフェスの選び方

coverwise は 1 つのエンジンを 5 つのサーフェスから提供します。JavaScript 向けの WebAssembly ビルド、同じエンジンのピュア TypeScript 移植版、ネイティブの C++ ライブラリ、コマンドラインバイナリ、そして Python パッケージです。同じモデルからは同じスイートが得られるので、このページが扱うのは、どれを採用するといくらの手間がかかり、実行環境に何を要求するかです。出力の良し悪しの話ではありません。速度については[パフォーマンス](performance.md)を参照してください。

## すべてのサーフェスに共通するもの

モデルはどこでも同じです。離散値または境界値を持つパラメータ、任意の制約、強度、そしてシードです。4 つの操作、すなわちスイートの生成、既存スイートの分析、拡張、モデル規模の見積もりも同じで、返されるフィールドの意味も同じです。制約を書くための式の言語は[制約構文](constraints.md)に、強度が何を選ぶのかは[強度](primer/strength.md)にまとめてあります。

決定性はこの範囲全体で成り立ちます。同じ正当なモデルと同じシードからは、どのサーフェスでも同じスイートが得られます。WASM ビルドは C++ コアをコンパイルしたものなので、この 2 つは構成上一致します。TypeScript 移植版はパリティスイートによって WASM の面に合わせられています。保証の正確な内容は[決定性](determinism.md)が述べます。

異なるのは境界です。C++ ライブラリは構造体を受け取り構造体を返します。JavaScript のサーフェスはプレーンなオブジェクトを受け渡しし、JSON のパースは自分では行いません。CLI と Python パッケージは JSON 文書でやり取りします。

## 5 つのサーフェス

| サーフェス | 入手方法 | 必要なもの | 選ぶ場面 |
|---|---|---|---|
| WASM（npm、既定の import） | `npm install @libraz/coverwise` | Node.js 18 以上またはブラウザ、ESM、WASM をロードできるランタイム | JavaScript や TypeScript で書き、WASM を妨げるものが何もない場合 |
| ピュア TypeScript（npm のサブパス） | 同じパッケージの `@libraz/coverwise/pure` | Node.js 18 以上またはブラウザ、ESM | WASM が使えない、禁じられている、あるいは処理量に見合わない手間がかかる場合 |
| ネイティブ C++ | ソースからビルドしてインストールし `find_package` | 浮動小数点 `std::to_chars` を備えた C++17 ツールチェイン | エンジンを C++ のプログラムやテストハーネスへ組み込む場合 |
| コマンドライン | PyPI パッケージに同梱、またはソースからビルド | バイナリ以外に必要なものはなし | スクリプトや CI で使う場合、バインディングのない言語から呼ぶ場合 |
| Python | `pip install coverwise` | Python 3.10 以上 | pytest を書く場合、外部プロセス呼び出しで済ませていた Python コードの場合 |

## WASM、npm の既定

ルートの import は、WebAssembly にコンパイルしたエンジンです。JavaScript で最も速く、数値がネイティブのエンジンに追随する選択肢なので、これが既定になっています。

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();
const result = cw.generate({
  parameters: [
    { name: 'os', values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
  ],
  constraints: ['IF os = Windows THEN browser != Safari'],
});
```

代償はロード手順です。`Coverwise.create()`（関数ベースなら `init()`）が最初の呼び出しより前にモジュールを取得して実体化します。つまりこのサーフェスには、ピュア TypeScript 版にはない失敗の仕方があります。

そのうち 1 つは、パッケージの不具合のように見えるため名指ししておく価値があります。ブラウザ向けに配信するコードへ Node 互換レイヤをかぶせる CDN があります。すると WASM のローダは Node に見えるものを見て Node 経路を取り、その経路が成立しないブラウザ上で `Coverwise.create()` が初期化に失敗します。公開されたファイルを書き換えずにそのまま配信する CDN を使えば避けられます。ローダを一切持たないピュア TypeScript のエントリポイントでも避けられます。

## ピュア TypeScript

同じエンジンを TypeScript へ移植し、同じパッケージのサブパスとして公開したものです。名前も型も結果も同一で、プログラムは import 指定子を差し替えるだけで移行できます。

```typescript
import { Coverwise } from '@libraz/coverwise/pure';

const cw = await Coverwise.create();
```

| | WASM（既定） | ピュア TypeScript |
|---|---|---|
| import | `@libraz/coverwise` | `@libraz/coverwise/pure` |
| 起動 | WASM モジュールをロードして実体化します | 即座に返ります。ロードするものはありません |
| 性能 | ペアワイズのモデルではピュア TypeScript と近く、強度とタプル数が増えるほど引き離します | ペアワイズのモデルでは WASM と近く、強度とタプル数が増えるほど引き離されます |
| 必要なもの | WASM をロードできるランタイム | JavaScript ランタイム以外に必要なものはなし |
| API | 同一 | 同一 |

選ぶのは、WASM が使えないか、手間に見合わない場面です。WASM の実体化を拒むランタイムやコンテンツセキュリティポリシー、バイナリを出力させるために設定が必要なバンドラやテストランナー、あるいはロード手順のほうが支配的になるほど小さい処理量のときです。強度が 2 を超えるとき、モデルの幅が広いとき、同じプロセスで多数のスイートを生成するときは既定のほうを選んでください。

## ネイティブ C++

エンジンそのものです。インストールして公開されたターゲットをリンクすれば、傘ヘッダが残りを取り込みます。

```cmake
find_package(coverwise CONFIG REQUIRED)
target_link_libraries(my_tests PRIVATE coverwise::coverwise)
```

必要なのは、標準ライブラリが浮動小数点の `std::to_chars` を実装している C++17 ツールチェインです。GCC 11 以上、Clang 10 以上、AppleClang 14 以上が該当します。このライブラリはファイル入出力を行わず、JSON もパースしません。構造体を受け取り構造体を返すだけで、それが WASM へそのままコンパイルできる理由でもあります。境界で JSON を扱いたい場合、CLI が既にその境界です。[C++ API](cpp-api.md)を参照してください。

## コマンドライン

JSON のモデルを読み、JSON のスイートを書く単一のバイナリです。採用すべきツールチェインが存在しないサーフェスなので、CI のステップ、シェルのパイプライン、そして coverwise のバインディングがない言語に向いています。プロセスを起動して JSON を読めるものなら何でも使えます。

```bash
coverwise generate model.json > suite.json
```

結果は終了コードでも伝わるため、スクリプトは出力をパースせずに分岐できます。コマンド、フラグ、終了コードの表は[CLI リファレンス](cli.md)にあります。

## Python

PyPI パッケージはネイティブバイナリを同梱してラップしているため、`pip install coverwise` だけでインストールは完了します。コンパイル手順も、CLI を別途 PATH に置く作業もありません。

```python
import coverwise

result = coverwise.generate(
    parameters=[
        {"name": "os", "values": ["Windows", "macOS", "Linux"]},
        {"name": "browser", "values": ["Chrome", "Firefox", "Safari"]},
    ],
)
```

必要なのは Python 3.10 以上です。wheel は Linux 向けが manylinux_2_28、つまり glibc 2.28 以上、macOS 向けが 14 以上の Apple Silicon で公開されています。ラッパーが JSON でバイナリを駆動する都合上、呼び出しは毎回プロセス境界をまたぎます。フィクスチャで 1 度だけスイートを生成する使い方では気になりませんが、密なループの中では知っておく価値があります。パッケージにはモデルをそのまま `parametrize` マーカーへ変える pytest ヘルパーも含まれています。[Python API](python-api.md)を参照してください。

## 次に読むもの

- [はじめかた](getting-started.md) — 各サーフェスでのインストールと、最初の完全なスイート、その出力
- [パフォーマンス](performance.md) — ベンチマークの表と、その読み方
- [JavaScript API](js-api.md) — npm の 4 つのエントリポイントのリファレンス
- [Python API](python-api.md) — PyPI パッケージと pytest ヘルパーのリファレンス
- [CLI リファレンス](cli.md) — コマンド、フラグ、終了コード
- [決定性](determinism.md) — シードがこれらのサーフェス間で何を保証し、何を保証しないか
