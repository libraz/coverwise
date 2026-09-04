# Introduction

coverwise is a combinatorial test generation engine. Give it a model — named parameters, each with a list of discrete values, plus optional rules about which combinations are legal — and it returns a small suite of test cases that between them exercise every legal combination of any two parameters, or of any three, or of whatever number the model asks for. It also measures a suite that already exists, and adds rows to one that falls short. It does not run those test cases, and it has no opinion about what the parameters mean.

## The problem

A suite with one row per combination stops being affordable long before the model stops being realistic: 3 operating systems, 4 browsers and 3 screen sizes is 3 × 4 × 3 = 36 test cases, and adding a locale and a network condition of three values each takes the same arithmetic to 324. [What combinatorial testing is](primer/what-is-combinatorial-testing.md) works that growth through.

A random subset of those combinations is affordable, but it supports no statement about what it covers. Picking "representative" cases by hand is a judgement call that has to be made again every time a parameter gains a value.

## What a covering array is

A **covering array** is a set of test cases chosen so that every combination of values from any two parameters appears in at least one of them. Nothing is left out at the level of pairs; what is left out is the redundancy between pairs.

| Model | One test per combination | Pairwise suite |
|---|---|---|
| 3 operating systems, 4 browsers, 3 screen sizes | 36 tests | 12 tests |
| the same, plus 2 more three-value parameters | 324 tests | 19 tests |
| 10 parameters, 4 values each | 1,048,576 tests | 38 tests |
| 20 parameters, 4 values each | 1,099,511,627,776 tests | 44 tests |

The right-hand column is what the bundled generator produces at the default seed. A pairwise suite is bounded below by the largest single pair of parameters — 4 × 4 = 16 combinations in the last two rows — so adding parameters past that point costs a few rows rather than multiplying the total.

Pairwise is the default because studies of fault reports across several domains find that most failures are triggered by one parameter alone or by two acting together, with a diminishing tail beyond that; [What combinatorial testing is](primer/what-is-combinatorial-testing.md) sets out that evidence and what it does not establish. It is an observation rather than a theorem: a fault that appears only when three specific values coincide will not be found by a pairwise suite, and raising the strength is the lever for that case. [Strength](primer/strength.md) covers what t = 2 misses and what raising it costs.

## The three operations

coverwise offers three operations, and they compose into one loop:

- **generate** builds a suite from a model.
- **analyze** measures any suite against a model and names the combinations that are missing, rather than returning a percentage alone.
- **extend** takes a suite worth keeping — a hand-written one, or a previous generation — and adds the fewest rows it can to close the gaps, leaving the given rows untouched and in order.

Most projects do not start from nothing. A suite already exists, analysis measures it, extension adds to it, and analysis measures it again. [Close the gaps incrementally](use-cases/close-the-gaps-incrementally.md) walks that loop end to end.

## The vocabulary

### Parameters and values

A **parameter** is one dimension of variation in the system under test. Each parameter carries a finite list of **values**.

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ]
}
```

### Strength

**Strength**, written t, is how many parameters are considered at a time. At t = 2 the suite must contain every combination of values drawn from every pair of parameters; at t = 3, from every triple. Strength describes the size of the combination being covered, not the size of a test case — a test case always names every parameter, whatever t is. Raising t raises the number of combinations to cover, and with it the number of rows.

### Constraints

Real systems reject some combinations outright. A **constraint** is a boolean rule saying which ones, written against parameter names and values:

```
IF os = Windows THEN browser != Safari
```

A constraint is enforced during construction, not filtered afterwards, so a violating test case is never built in the first place. [Constraint syntax](constraints.md) is the full language reference.

### Required tuples and coverage

A **tuple** is one specific combination of values, one from each of t parameters — `os=Windows, browser=Chrome` is a tuple at t = 2. The **required tuples** are all the tuples the suite has to contain, after the constraints have removed those that no legal test case could hold. **Coverage** is the fraction of required tuples that a suite actually contains. coverwise aims at all of them, and when it stops short — because `maxTests` capped it, for instance — it reports which ones are still missing rather than reporting a number alone. [Tuples and coverage](primer/tuples-and-coverage.md) works the whole idea through on a model small enough to check by hand.

## What coverwise does not do

- **It does not run your tests.** The output is a list of value assignments. Turning each row into an executed test case is your test framework's job, and coverwise never reads or writes your framework's files.
- **It does not know what the parameters mean.** `os` and `browser` are opaque names to it. It cannot report a browser missing from the list, two values that name the same thing, or a combination that is nonsense in the domain — only a written constraint states that.
- **It cannot decide whether pairwise is adequate.** Choosing t is a risk decision about the system under test, and it stays with the caller.
- **It does not promise the minimum possible suite.** The generator is a greedy construction; it aims at complete coverage first and at a small suite second, and typically lands somewhat above the theoretical lower bound. [Performance](performance.md) publishes the measured counts beside those bounds.
- **It keeps no state.** Each call takes a model and returns a result. There is no session, no project file and no cache.

## Where coverwise runs

| Surface | Install | Notes |
|---|---|---|
| Node.js | `npm install @libraz/coverwise` | Node.js 18 or newer, ESM only |
| Browser | ESM import | The WebAssembly module is loaded by the entry point |
| Pure TypeScript | `@libraz/coverwise/pure` subpath | Same API with no WebAssembly, for runtimes that cannot load it |
| Python | `pip install coverwise` | Python 3.10 or newer; Linux wheels need glibc 2.28 or newer, macOS wheels are 14+ Apple Silicon |
| Native C++ | CMake, static library | C++17 |
| Command line | `pip install coverwise`, a release archive, or a source build | JSON in, JSON out |

Every surface runs the same algorithm and returns the same suite for the same valid input and seed. [Choosing a surface](choosing-a-surface.md) says which one to reach for; [Determinism](determinism.md) states precisely what that guarantee covers.

## Where to go next

**Primer** — the concepts, for a reader new to combinatorial testing.

- [Primer](primer/index.md) — how the four content pages fit together, in prerequisite order
- [What combinatorial testing is](primer/what-is-combinatorial-testing.md) — why the cross-product is untestable and why a covering subset is not arbitrary
- [Tuples and coverage](primer/tuples-and-coverage.md) — the whole vocabulary worked on a three-parameter model
- [Strength](primer/strength.md) — what t means, what t = 2 misses, what raising it costs
- [Constraints and the required universe](primer/constraints-and-the-universe.md) — why an excluded tuple is not an uncovered one

**Getting started**

- [Getting started](getting-started.md) — install and generate a first suite on every surface

**Use cases** — each starts from data a project already has.

- [Use cases](use-cases/index.md)
- [Audit an existing suite](use-cases/audit-an-existing-suite.md) — measure hand-written tests and read the gaps
- [Close the gaps incrementally](use-cases/close-the-gaps-incrementally.md) — analyze, extend, re-analyze
- [Replace a cross-product in pytest](use-cases/replace-a-cross-product-in-pytest.md) — a parametrized suite, shrunk

**Guides**

- [Examples](examples.md) — copy-pasteable recipes per feature
- [Constraint syntax](constraints.md) — the constraint language reference
- [Choosing a surface](choosing-a-surface.md) — WebAssembly, pure TypeScript, native C++, CLI or Python
- [Determinism](determinism.md) — what the seed guarantees, and across which engines
- [Performance](performance.md) — the benchmark tables and how to read them
- [Input limits](limits.md) — the input limits and what happens at each

**Reference**

- [JavaScript API](js-api.md) — all four published entry points
- [Python API](python-api.md) — the package and its pytest helpers
- [C++ API](cpp-api.md) — the native library
- [CLI reference](cli.md) — commands, JSON schemas and exit codes
- [Glossary](glossary.md) — one entry per concept, each naming the API that implements it
- [Questions and limitations](faq.md)
