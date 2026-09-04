# Determinism

The same valid model and the same seed produce the same suite, row for row and in the same order. That is a property of one build of coverwise: it holds across machines, operating systems and surfaces, and it does not hold across versions.

## What a seed fixes

Generation makes random choices — which value to place when several would close the same number of gaps. `seed` fixes every one of them.

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

Six rows cover all 16 pairs of that model, and rerunning it produces those six rows again.

## A different seed gives a different valid suite

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

Different rows, same six-row size, the same 16 pairs covered. Neither suite is the correct one; both satisfy the requirement, and the seed decides which one this run produces.

## The seed field

`seed` defaults to 0, so a model that never mentions it is still reproducible. Its canonical domain is 0 through 4294967295, which is 2^32 - 1.

Every surface rejects a seed outside that range as invalid input, the C++ API included. `GenerateOptions::seed` is declared `uint64_t` so that a surface reading a seed from JSON can carry an out-of-range number as far as the acceptance gate; the gate rejects it there with a message naming the domain, rather than truncating it to its low 32 bits.

Coverage analysis makes no random choices at all: `analyzeCoverage` enumerates the required universe and counts, so it returns the same report whatever the seed. `extendTests` uses the seed only for the rows it appends; the existing rows are preserved verbatim as the result's prefix.

## What agrees across surfaces

The native library, the CLI and the WASM build are the same C++ code — the WASM build is that core compiled through Emscripten — so they agree by construction rather than by testing.

The pure TypeScript port under `@libraz/coverwise/pure` is a second implementation of the same engine. It is held to the WASM surface by the parity suite in `js/compat.test.ts`, which compares whole results for `generate`, `analyzeCoverage`, `extendTests` and `estimateModel`, along with the code, message and detail of every error the two raise. That suite compares WASM against pure TypeScript and nothing else, so it is evidence about the JavaScript pair and not about the native C++ leg.

Underneath all of them is one generator: xoshiro128\*\* seeded through SplitMix32, written once in C++ and mirrored in TypeScript so that a seed drives the same sequence on both.

## What determinism does not promise

**Stability across versions of coverwise.** The guarantee is that the algorithm is a function of its input, not that the algorithm never changes. A correction or an improvement to construction may produce a different — equally valid — suite for the same model and seed. Pin the version when a byte-stable suite matters, or commit the generated suite and regenerate deliberately.

**Stability across a changed model.** The model is the input, and that includes the order values are declared in: reversing the `os` list above produces a different six rows. Adding one value can move every row.

**A best suite.** A seed selects one valid covering array; it says nothing about whether a different seed would produce a smaller one.

**Anything about run time.** Determinism is about output, not about how long a run takes.

In CI, what this buys is a suite that does not churn. A generated file can be committed, and a diff on it then means the model changed or coverwise changed — never that the run happened to go differently.

## Where to go next

- [Examples](examples.md) — `seeds`, `weights` and the other fields that shape which suite a run produces.
- [Choosing a surface](choosing-a-surface.md) — which of the surfaces above to build against.
- [JavaScript API](js-api.md) — `seed` alongside the rest of the model, and the errors an out-of-range value raises.
- [CLI reference](cli.md) — the same field in a JSON model document.
- [Questions and limitations](faq.md) — whether the same suite comes back twice, and why the docs publish no run times.
