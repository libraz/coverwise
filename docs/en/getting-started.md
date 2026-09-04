# Getting started

This page installs coverwise on each surface it ships for, generates one complete suite, and explains what came back. The vocabulary it uses — parameter, value, strength, tuple, coverage — is introduced in the [Introduction](introduction.md) and worked through in the [Primer](primer/index.md).

## Installation

### JavaScript and TypeScript

```bash
npm install @libraz/coverwise
# or
yarn add @libraz/coverwise
```

### Python

```bash
pip install coverwise
```

The API, the pytest helpers and the error types are in the [Python API](python-api.md).

### C++

```bash
git clone https://github.com/libraz/coverwise.git
cd coverwise
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel
cmake --install build --prefix ./install
```

Headers, linking and the full API are in the [C++ API](cpp-api.md).

### CLI

The same PyPI package ships the native binary:

```bash
pip install coverwise
```

Linux x64 archives are also published on [GitHub Releases](https://github.com/libraz/coverwise/releases). The npm package does not install the native CLI. To build it from source:

```bash
make build
# Binary at build/bin/coverwise
cmake --install build --prefix ./install
# Installed binary at install/bin/coverwise
```

Commands, JSON schemas and exit codes are in the [CLI reference](cli.md).

## Runtime prerequisites

- **JavaScript surfaces**: Node.js 18 or newer, and ESM only. The package sets `"type": "module"` and every example below uses top-level `await`, which a CommonJS file cannot parse. In a CommonJS project, use a `.mjs` file or set `"type": "module"` in your own `package.json`.
- **Python**: Python 3.10 or newer. Linux wheels are built for manylinux_2_28, so they need glibc 2.28 or newer; macOS wheels are for macOS 14 and later on Apple Silicon.
- **C++**: a C++17 compiler with floating-point `std::to_chars` — GCC 11+, Clang 10+, or AppleClang 14+.

## A first suite

The same model on every surface: three operating systems, three browsers, two themes, at the default strength of 2.

### JavaScript

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'theme',   values: ['light', 'dark'] },
  ],
});

result.tests.length;         // 10
result.stats.totalTuples;    // 21
result.coverage;             // 1

for (const test of result.tests) {
  console.log(test);
}
// { os: 'macOS', browser: 'Firefox', theme: 'dark' }
// { os: 'Linux', browser: 'Firefox', theme: 'light' }
// { os: 'Linux', browser: 'Safari', theme: 'dark' }
// { os: 'Linux', browser: 'Chrome', theme: 'dark' }
// { os: 'macOS', browser: 'Chrome', theme: 'light' }
// { os: 'Windows', browser: 'Firefox', theme: 'light' }
// { os: 'Windows', browser: 'Safari', theme: 'dark' }
// { os: 'Linux', browser: 'Safari', theme: 'light' }
// { os: 'macOS', browser: 'Safari', theme: 'light' }
// { os: 'Windows', browser: 'Chrome', theme: 'dark' }
```

Ten rows, where running every combination would be 3 × 3 × 2 = 18. The reduction comes from the fact that each row covers three pairs at once — one for each pair of parameters — so ten rows carry thirty pair slots for the 21 distinct pairs that have to be covered. That 21 is `3 × 3` os-browser pairs plus `3 × 2` os-theme pairs plus `3 × 2` browser-theme pairs, and `result.stats.totalTuples` reports it.

Ten is also close to the floor. The os-browser pair alone is nine combinations, so no suite of this model is shorter than nine rows and coverwise spends one more than that; [Performance](performance.md) states the rule behind that floor and publishes measured counts beside it. To check the coverage claim rather than take it on faith, feed the rows straight back through `analyzeCoverage`, as the coverage section below does.

### Python

```python
import coverwise

result = coverwise.generate(
    parameters={
        "os": ["Windows", "macOS", "Linux"],
        "browser": ["Chrome", "Firefox", "Safari"],
        "theme": ["light", "dark"],
    },
)

len(result["tests"])              # 10
result["stats"]["totalTuples"]    # 21
result["coverage"]                # 1.0

for test in result["tests"]:
    print(test)
# {'os': 'macOS', 'browser': 'Firefox', 'theme': 'dark'}
# {'os': 'Linux', 'browser': 'Firefox', 'theme': 'light'}
# {'os': 'Linux', 'browser': 'Safari', 'theme': 'dark'}
# {'os': 'Linux', 'browser': 'Chrome', 'theme': 'dark'}
# {'os': 'macOS', 'browser': 'Chrome', 'theme': 'light'}
# {'os': 'Windows', 'browser': 'Firefox', 'theme': 'light'}
# {'os': 'Windows', 'browser': 'Safari', 'theme': 'dark'}
# {'os': 'Linux', 'browser': 'Safari', 'theme': 'light'}
# {'os': 'macOS', 'browser': 'Safari', 'theme': 'light'}
# {'os': 'Windows', 'browser': 'Chrome', 'theme': 'dark'}
```

The same ten rows in the same order. Every surface runs the same algorithm, so the same model and seed produce the same suite everywhere; [Determinism](determinism.md) states the exact scope of that guarantee.

### C++

```cpp
#include <coverwise.h>

#include <iostream>

int main() {
  using namespace coverwise;

  model::GenerateOptions opts;
  opts.parameters = {
      {"os", {"Windows", "macOS", "Linux"}},
      {"browser", {"Chrome", "Firefox", "Safari"}},
      {"theme", {"light", "dark"}},
  };
  opts.strength = 2;

  auto result = core::Generate(opts);
  if (!result.error.ok()) {
    std::cerr << result.error.message << "\n";
    return 1;
  }

  std::cout << result.tests.size() << " tests, coverage " << result.coverage << "\n";
  // 10 tests, coverage 1
  return 0;
}
```

A C++ row holds value indices rather than value names, so reading one back means indexing `result.parameters[i].values`. The [C++ API](cpp-api.md) shows that, and carries the longer program that the build compiles and runs against the installed package.

### CLI

Write the model to `input.json`:

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] },
    { "name": "theme", "values": ["light", "dark"] }
  ],
  "strength": 2
}
```

Then run:

```bash
coverwise generate input.json > tests.json
```

`tests.json` holds the same ten rows as a JSON object with a `tests` array. The command exits 0 on full coverage and 2 when it falls short, which makes it usable as a CI gate.

## When the WebAssembly build cannot load

The default entry point loads a WebAssembly module, and that load is the one step in the JavaScript surface that can fail for reasons the model has no bearing on — a bundler that does not emit the `.wasm` asset, or a runtime with no WebAssembly at all. The `/pure` subpath is a complete TypeScript port of the same engine with the same API and no WebAssembly:

```typescript
import { Coverwise } from '@libraz/coverwise/pure';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'theme',   values: ['light', 'dark'] },
  ],
});

result.tests.length;   // 10, the same suite in the same order
```

Only the import specifier changes. `Coverwise.create()` and `init()` exist on the pure surface too and return immediately, so code written against the default entry point runs unmodified. The trade-off is speed on large models: [Choosing a surface](choosing-a-surface.md) sets out when each one is the right call.

## Adding constraints

Every block from here on reuses the `cw` bound in the first JavaScript block above. Real models have combinations that cannot occur — Safari does not run on Windows or Linux — and a constraint states that as a rule:

```typescript
const constrained = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'theme',   values: ['light', 'dark'] },
  ],
  constraints: ['IF browser = Safari THEN os = macOS'],
});

constrained.tests.length;         // 9
constrained.stats.totalTuples;    // 19 (21, less the 2 pairs no valid row can hold)
constrained.coverage;             // 1
```

One constraint is enough because it states the rule rather than its consequences. `IF browser = Safari THEN os = macOS` rules out Safari on Windows and Safari on Linux at once, and it stays correct when a fourth operating system is added to the model. Writing the consequences instead — one rule per operating system that must not pair with Safari — needs a new line every time the model grows, and a forgotten line is an invalid test case that nothing flags.

Note what happened to the count of required tuples: it fell from 21 to 19, because `os=Windows, browser=Safari` and `os=Linux, browser=Safari` cannot appear in any valid test case and so are not required of any suite. That is different from being uncovered, and the difference is the subject of [Constraints and the required universe](primer/constraints-and-the-universe.md). The constraint language itself, including the TypeScript builder that produces these strings, is in [Constraint syntax](constraints.md).

## Measuring an existing suite

`analyzeCoverage` measures any suite against a model, whether coverwise generated it or a person wrote it by hand. It is independent of the generator, which is what lets it judge a hand-written suite; [Questions and limitations](faq.md) says why the two are kept apart.

```typescript
const parameters = [
  { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
  { name: 'theme',   values: ['light', 'dark'] },
];

const existingTests = [
  { os: 'Windows', browser: 'Chrome',  theme: 'light' },
  { os: 'macOS',   browser: 'Firefox', theme: 'dark' },
  { os: 'Linux',   browser: 'Safari',  theme: 'light' },
];

const report = cw.analyzeCoverage(parameters, existingTests);

report.totalTuples;     // 21
report.coveredTuples;   // 9  (3 rows, 3 parameter pairs each, all distinct)
report.coverageRatio;   // 0.42857142857142855  (9 of 21)
report.uncoveredCount;  // 12

report.uncovered[0].display;   // 'os=Windows, browser=Firefox'
```

Three hand-written rows cover nine of the 21 pairs, which is the best three rows can do here: each row contributes one os-browser, one os-theme and one browser-theme pair, and these three rows happen to repeat none of them. The remaining twelve come back individually in `report.uncovered`, each with a `display` string naming the exact combination, so the report says what is missing rather than only how much.

The model passed to `analyzeCoverage` has to be the model the tests were written against. A row that names a value the model does not list, or omits a parameter, is not counted against coverage — it lands in `report.invalidTests` with a readable reason, which is worth checking first when a legacy suite scores lower than expected.

## Extending a suite

`extendTests` keeps the rows you pass in and adds the fewest it can. This block continues from the one above and reuses its `parameters` and `existingTests`:

```typescript
const extended = cw.extendTests(existingTests, { parameters });

extended.tests.length;                          // 10 (the 3 rows above, plus 7)
extended.tests.slice(0, existingTests.length);  // the 3 given rows, unchanged and in order
extended.coverage;                              // 1
```

Ten rows again, and the same total as generating from scratch — the three hand-written rows were not wasted, and they were not rewritten either. Extend when the existing rows carry something the model does not; [Close the gaps incrementally](use-cases/close-the-gaps-incrementally.md) sets out that decision against regenerating from scratch.

## Reproducible output

Passing a seed fixes the suite. The same model and seed produce the same rows in the same order, on every surface and every run. This block reuses the `parameters` bound two blocks above:

```typescript
const first  = cw.generate({ parameters, seed: 42 });
const second = cw.generate({ parameters, seed: 42 });

first.tests.length;    // 9
second.tests.length;   // 9
// first.tests and second.tests are deeply equal, row for row
```

Nine rows here rather than the ten of the unseeded run: a seed changes the order in which the model is explored, and the greedy construction can land on a different suite size. With no seed the default is 0, which is itself a fixed value — the suites above are reproducible without asking for it. Change the model in any way and the suite may change entirely; the guarantee is over identical input, not similar input. [Determinism](determinism.md) says which engines are held to each other and by what.

## A model with one parameter

A single-parameter model at the default strength is rejected, because a pair needs two parameters to draw from:

```typescript
const oneParameter = [{ name: 'os', values: ['Windows', 'macOS', 'Linux'] }];

cw.generate({ parameters: oneParameter });
// CoverwiseError: Strength must be between 1 and parameter count
// error.detail === 'strength=2, parameters=1'

const result = cw.generate({ parameters: oneParameter, strength: 1 });

result.tests.length;   // 3
result.coverage;       // 1
```

Strength 1 asks for every value of every parameter to appear at least once, which for one parameter is one row per value. The same bound applies to `analyzeCoverage` and to each sub-model's own strength: t must be between 1 and the number of parameters it is drawn from.

## Where to go next

- [Primer](primer/index.md) — the concepts behind everything above, in prerequisite order
- [Examples](examples.md) — recipes per feature: negative tests, mixed strength, boundary values, weights
- [Audit an existing suite](use-cases/audit-an-existing-suite.md) — the analyze path, from a real hand-written suite
- [JavaScript API](js-api.md) — the full reference for all four published entry points
- [Constraint syntax](constraints.md) — operators, the builder, and what the parser rejects
- [Glossary](glossary.md) — the vocabulary, one entry per concept
