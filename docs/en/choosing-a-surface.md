# Choosing a surface

coverwise ships one engine behind five surfaces: a WebAssembly build for JavaScript, a pure TypeScript port of the same engine, the native C++ library, a command-line binary, and a Python package. They generate the same suites from the same models, so this page is about what each one costs to adopt and what it needs from its host — not about which produces better output. For how fast each one is, see [Performance](performance.md).

## What every surface shares

The model is the same everywhere: parameters with discrete or boundary values, optional constraints, a strength, and a seed. So are the four operations — generate a suite, analyze an existing one, extend one, estimate a model's size — and the meaning of every field they return. [Constraint syntax](constraints.md) is the expression language a constraint is written in, and [Strength](primer/strength.md) is what a strength selects.

Determinism holds across the set: the same valid model and the same seed produce the same suite on every surface. The WASM build is the C++ core compiled, so those two agree by construction; the TypeScript port is held to the WASM surface by a parity suite. [Determinism](determinism.md) states the guarantee precisely.

What differs is the boundary. The C++ library takes structures and returns structures. The JavaScript surfaces take and return plain objects, and parse no JSON of their own. The CLI and the Python package speak JSON documents.

## The five surfaces

| Surface | How to get it | What it needs | Reach for it when |
|---|---|---|---|
| WASM (npm, default import) | `npm install @libraz/coverwise` | Node.js 18 or newer, or a browser; ESM; a runtime that can load WASM | Writing JavaScript or TypeScript, with nothing blocking WASM |
| Pure TypeScript (npm subpath) | Same package, `@libraz/coverwise/pure` | Node.js 18 or newer, or a browser; ESM | WASM is unavailable, blocked, or more setup than the workload is worth |
| Native C++ | Build from source, install, `find_package` | A C++17 toolchain with floating-point `std::to_chars` | Embedding the engine in a C++ program or a test harness |
| Command line | Ships inside the PyPI package, or build from source | Nothing beyond the binary itself | Scripting, working in CI, or driving coverwise from a language with no binding |
| Python | `pip install coverwise` | Python 3.10 or newer | Writing pytest, or any Python that would otherwise shell out |

## WASM, the npm default

The root import is the engine compiled to WebAssembly. It is the default because it is the fastest JavaScript option and the one whose numbers track the native engine.

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

The cost is a load step. `Coverwise.create()` (or `init()`, in the function-based style) fetches and instantiates the module before the first call, which means the surface has a way to fail that the pure one does not.

One failure mode is worth naming because it looks like a bug in the package. Some CDNs overlay a Node compatibility layer on code they serve to browsers. The WASM loader then sees what looks like Node, takes its Node path, and `Coverwise.create()` fails to initialize — in a browser, where that path cannot work. Serving the published file verbatim from a CDN that rewrites nothing avoids it; so does the pure entry point, which has no loader at all.

## Pure TypeScript

The same engine, ported to TypeScript and published as a subpath of the same package. Names, types and results are identical, and a program moves between the two by swapping the import specifier.

```typescript
import { Coverwise } from '@libraz/coverwise/pure';

const cw = await Coverwise.create();
```

| | WASM (default) | Pure TypeScript |
|---|---|---|
| Import | `@libraz/coverwise` | `@libraz/coverwise/pure` |
| Startup | Loads and instantiates a WASM module | Returns immediately; nothing to load |
| Performance | Close to pure TypeScript on pairwise models, and pulls ahead as strength and tuple count grow | Close to WASM on pairwise models, and falls behind as strength and tuple count grow |
| Requires | A runtime that can load WASM | Nothing beyond the JavaScript runtime |
| API | Identical | Identical |

Reach for it when WASM is not available or not worth the setup: a runtime or content-security policy that refuses to instantiate WASM, a bundler or test runner that would have to be configured to emit the binary, or a workload small enough that the load step dominates. Reach for the default when strength is above 2, when the model is wide, or when the same process generates many suites.

## Native C++

The engine itself. Install it and link the exported target; the umbrella header pulls in the rest.

```cmake
find_package(coverwise CONFIG REQUIRED)
target_link_libraries(my_tests PRIVATE coverwise::coverwise)
```

It needs a C++17 toolchain whose standard library implements floating-point `std::to_chars` — GCC 11 or newer, Clang 10 or newer, AppleClang 14 or newer. The library does no file I/O and parses no JSON: it takes structures and returns structures, which is what lets it compile to WASM unchanged. Where JSON at the boundary is what is wanted, the CLI already is that boundary. See the [C++ API](cpp-api.md).

## Command line

A single binary that reads a JSON model and writes a JSON suite. It is the surface with no toolchain to adopt, which makes it the right one for CI steps, shell pipelines, and languages that have no coverwise binding — anything that can run a process and read JSON can use it.

```bash
coverwise generate model.json > suite.json
```

It reports outcomes as exit codes rather than only as text, so a script can branch on the result without parsing it. The [CLI reference](cli.md) has the commands, their flags and the exit-code table.

## Python

The PyPI package ships the native binary and wraps it, so `pip install coverwise` is the whole installation — there is no compiler step and no separate CLI to place on the path.

```python
import coverwise

result = coverwise.generate(
    parameters=[
        {"name": "os", "values": ["Windows", "macOS", "Linux"]},
        {"name": "browser", "values": ["Chrome", "Firefox", "Safari"]},
    ],
)
```

It needs Python 3.10 or newer. Wheels are published for Linux against manylinux_2_28, so glibc 2.28 or newer, and for macOS 14 and newer on Apple Silicon. Because the wrapper drives the binary over JSON, every call crosses a process boundary; that is unnoticeable for a suite generated once in a fixture and worth knowing about in a tight loop. The package also carries a pytest helper that turns a model into a `parametrize` marker directly. See the [Python API](python-api.md).

## Where to go next

- [Getting started](getting-started.md) — install and a first complete suite on each surface, with its output.
- [Performance](performance.md) — the benchmark tables and how to read them.
- [JavaScript API](js-api.md) — the reference for all four npm entry points.
- [Python API](python-api.md) — the reference for the PyPI package and its pytest helper.
- [CLI reference](cli.md) — the commands, their flags, and the exit codes.
- [Determinism](determinism.md) — what the seed guarantees across these surfaces, and what it does not.
