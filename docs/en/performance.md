# Performance

This page publishes what generation produces: for a set of model shapes, the size of the required tuple universe and the size of the suite that covers all of it. It does not publish run times, and the section on that below says why. Every configuration here reaches 100% t-wise coverage, measured by the validator rather than by the generator that produced the suite.

The counts are what the bundled generator produces at `seed: 42` for the shapes named below, and every row is regenerable from the model it names: build the uniform model of that shape, generate at that seed, and read `stats.totalTuples` and the row count.

## How to read the tables

A configuration is written as a parameter count times a value count. `10 × 3 uniform` is ten parameters with three values each; `5 × 20 high-card` is five parameters with twenty values each, which is the shape where a small model still has a large universe.

| Column | What it holds |
|---|---|
| Configuration | The model shape, as parameter count times values per parameter |
| Strength | The t of the coverage requirement, where the table is not pairwise |
| Tuples | The size of the required universe — every t-tuple that must appear at least once |
| Tests | The size of the suite the generator produced, which covers all of them |
| Theoretical Min | A lower bound on any suite covering that universe, from combinatorial theory |

Tuples grows with both dimensions and with t. Tests grows far more slowly: a suite of 33 rows covers all 11,025 pairs of a 50-parameter model. See [Strength](primer/strength.md) for why raising t moves both columns and by how much.

None of these models carries constraints. A constraint removes tuples that have no valid completion from the required universe, so a constrained version of the same shape has a smaller Tuples figure and usually a smaller suite. See [Constraints and the required universe](primer/constraints-and-the-universe.md).

## Pairwise (2-wise)

| Configuration | Tuples | Tests | Theoretical Min |
|---------------|--------|-------|-----------------|
| 5 × 3 uniform | 90 | 15 | 9 (OA) |
| 10 × 3 uniform | 405 | 21 | 9 (OA) |
| 13 × 3 uniform | 702 | 22 | 9 (OA) |
| 10 × 5 uniform | 1,125 | 52 | 25 |
| 15 × 4 uniform | 1,680 | 41 | 16 |
| 20 × 2 uniform | 760 | 13 | 4 |
| 20 × 5 uniform | 4,750 | 65 | 25 |
| 30 × 5 uniform | 10,875 | 74 | 25 |
| 50 × 3 uniform | 11,025 | 33 | 9 (OA) |
| 5 × 20 high-card | 4,000 | 514 | 400 |

## Higher strength

| Configuration | Strength | Tuples | Tests |
|---------------|----------|--------|-------|
| 15 × 3 | 3-wise | 12,285 | 98 |
| 8 × 3 | 4-wise | 5,670 | 225 |

## High strength (stress test)

| Configuration | Strength | Tuples | Tests |
|---------------|----------|--------|-------|
| 10 × 3 | 5-wise | 61,236 | 880 |
| 8 × 4 | 5-wise | 57,344 | 2,764 |
| 12 × 3 | 6-wise | 673,596 | 3,349 |
| 15 × 3 | 5-wise | 729,729 | 1,297 |
| 20 × 3 | 5-wise | 3,767,472 | 1,592 |

## What `Theoretical Min` is

`Theoretical Min` is a lower bound on the number of rows any suite covering that universe could have. For most shapes, no suite of that size is known to exist.

Two sources produce the figures. The pairwise bound is the largest product of two parameter domains: those two parameters contribute that many pairs between them, a single row covers exactly one of those pairs, so no suite can be shorter. Every model in the pairwise table is uniform, so the largest product there is the square of the value count — twenty values on each of five parameters give the 400 for `5 × 20 high-card`. Where the figure comes from orthogonal-array theory instead, the column marks it `(OA)`. Both are bounds on what is possible rather than measurements of what was built.

Greedy construction lands at roughly 1.5 to 2.5 times the bound. That is a property of the method rather than of this implementation: each row is chosen for how many still-uncovered tuples it adds, which is the best local choice and not the choice a global optimum would make, and the gap between the two is where the factor comes from. Coverage completeness is the guarantee; suite size is an approximation, and closing the last of that gap costs search time out of proportion to the rows it saves.

A bound can also be far looser than the factor suggests. 5 × 20 high-card produces 514 rows against a bound of 400, a ratio of 1.29. 50 × 3 uniform produces 33 rows against a bound of 9, a ratio well past 2.5 — a lower bound that does not grow with the parameter count says little about a 50-parameter model, so read a large ratio as a loose bound before reading it as a large suite.

## Run times

Run times are deliberately not published per configuration. They depend on the host, the runtime and the build, and nothing in the repository can re-derive them, so a table of seconds would be a number no reader could check and no test could keep honest.

As a shape rather than a figure: the two JavaScript engines are close on pairwise models, and the WebAssembly engine pulls ahead as strength and tuple count grow. Benchmark the configuration you care about on your own hardware, and see [Choosing a surface](choosing-a-surface.md) for what else separates the surfaces.

Cost is driven by the tuple count rather than by the parameter count, which is why the 6-wise row above is the expensive one on this page despite its model being small. The engine's working set is bounded by the [Input limits](limits.md), so a model inside them has a bounded universe to enumerate.

## Where to go next

- [Strength](primer/strength.md) — why raising t multiplies the required universe, and when it is worth it
- [Constraints and the required universe](primer/constraints-and-the-universe.md) — how a constraint shrinks the tuple count these tables show
- [Input limits](limits.md) — the bounds on what a model may declare
- [Choosing a surface](choosing-a-surface.md) — which of the five surfaces to run a model on
- [Determinism](determinism.md) — why the same seed reproduces these suites exactly
