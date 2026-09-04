# Glossary

Terms as coverwise uses them, each naming the field or function where the term shows up in an API. Where a word has more than one common meaning in combinatorial testing, the entry says which one coverwise implements.

## The model

**Parameter** — A dimension of variation in the system under test, holding a finite list of values. Declared in `parameters`; `estimateModel` reports one entry per parameter in `stats.parameters`.

**High-cardinality parameter** — A parameter holding many values. The benchmark tables shorten a model built from a few of them to `high-card`, as in the `5 × 20 high-card` row: five parameters of twenty values each, the shape where a small model still has a large required universe.

**Value** — One discrete option of a parameter. Written as a bare string, number or boolean, or as an object that also carries `invalid`, `aliases` or `class`. Values are compared with ASCII case folded, so one parameter may not hold two values that differ only by case.

**Alias** — An alternative spelling the resolver accepts for a value. Declared in `aliases` on a value object. Output always names the declared spelling, never the alias.

**Invalid value** — A value marked `invalid: true`. It never appears in a positive row and never counts toward positive coverage; it is the raw material of negative testing.

**Equivalence class** — A label grouping values expected to take the same path through the system. Declared in `class` on a value object.

**Boundary value** — A value at or immediately beside the edge of a numeric range. A parameter carrying `type` and `range` expands into six of them: one outside each edge, each edge, and one inside each edge.

**Sub-model** — A named group of parameters given a strength of its own, so one group can be covered more deeply than the rest of the model. Declared in `subModels`.

**Weight** — A preference between values that would close the same number of gaps. Declared in `weights`. A higher weight makes a value likelier to be picked, never certain, and never changes whether the suite reaches full coverage.

**Seed test** — A row required to appear in the output of a generation run. Declared in `seeds`. The generator keeps seed rows in order and fills the remaining gaps around them.

**Constraint** — A boolean expression over parameter values that every generated row must satisfy. Declared in `constraints`; the syntax is [Constraint syntax](constraints.md).

**Test ceiling** — The largest number of rows a run may produce, declared in `maxTests`. Zero means no limit.

## Coverage

**Test case** — A complete assignment of one value to every parameter. One row of `result.tests`.

**Covering array** — A set of test cases in which every required tuple appears at least once. It is what `generate` returns.

**Tuple** — A combination of values drawn from t different parameters, such as `(os=Windows, browser=Chrome)`. Coverage is counted in tuples rather than in rows.

**Coverage unit** — The same object named from the coverage side: one tuple the suite is required to contain. `result.stats.totalTuples` counts them.

**Strength (t)** — How many parameters a coverage unit draws from. Declared in `strength`; 2 is pairwise and the default, and `subModels[].strength` overrides it for one group. Strength is a property of the coverage unit, not of the test case, which always assigns every parameter.

**Required universe** — The set of coverage units a suite has to contain: every t-tuple of the model, minus those holding an invalid value and those no constraint-satisfying row could ever hold. `result.stats.totalTuples` is its size, and [Constraints and the required universe](primer/constraints-and-the-universe.md) works through how a constraint shrinks it.

**Coverage ratio** — Covered units divided by required units, from 0 to 1. `result.coverage` after generation, `report.coverageRatio` after analysis. An empty universe is reported as fully covered.

**Uncovered tuple** — A required unit no row in the suite holds. `result.uncovered` and `report.uncovered` list them, each with a readable `display` string; `uncoveredCount` is the total and `omittedUncovered` says how many the list left out.

**Invalid test row** — A row in an analyzed suite that the parameter model does not describe, because it is missing a parameter or names a value the model never declared. `report.invalidTests` names each one with a readable reason, and such rows are left out of the coverage accounting.

**Negative test** — A row carrying exactly one invalid value, so a rejection can be attributed to that value alone. `result.negativeTests` holds them, separately from `result.tests`.

**Negative coverage** — The same accounting applied to tuples that hold one invalid value. `result.negativeCoverage`.

**Class coverage** — The same accounting applied at equivalence-class level rather than value level. `result.classCoverage`, present when any value declares a `class`.

## Generation

**Generate** — Build a covering array from a model. `generate`.

**Analyze** — Measure an existing suite against a model, by enumerating the required universe independently of the generator. `analyzeCoverage`.

**Extend** — Keep an existing suite verbatim as the prefix of the result and append only the rows that close its gaps. `extendTests`.

**Model estimate** — Sizing a model without building a suite for it. `estimateModel` reports `totalTuples` and `estimatedTests`, the second of which bounds nothing in either direction.

**Greedy construction** — The algorithm coverwise uses. Each new row is chosen to close as many still-open coverage units as it can, which yields near-optimal suites without searching for the true minimum.

**Constraint pruning** — Evaluating constraints against a partial assignment while a row is being built, so a violating row is never constructed and then discarded.

**RNG seed** — The number that fixes every random choice the generator makes. Declared in `seed`, default 0.

**Determinism** — The property that the same valid model and the same seed produce the same suite. What that covers, and what it does not, is [Determinism](determinism.md).

## Where to go next

- [Primer](primer/index.md) — the concepts above taught in prerequisite order, worked on small models.
- [Constraint syntax](constraints.md) — the expression language behind the constraint entry.
- [JavaScript API](js-api.md) — the declarations every field named here belongs to.
- [Determinism](determinism.md) — what a seed guarantees and across which surfaces.
- [Questions and limitations](faq.md) — the behaviour behind the terms, including what happens when the test ceiling binds.
