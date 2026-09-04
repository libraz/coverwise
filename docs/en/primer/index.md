# Primer

The primer teaches combinatorial testing to a working programmer who has never used it. Each page explains one idea in plain terms, names the decision that idea forces in the coverwise API, shows a short runnable example, and hands off to the reference page that takes it further.

It teaches enough to use coverwise and stops there. The combinatorial-design literature behind covering arrays — orthogonal arrays, construction bounds, the proofs that a suite of a given size exists — is out of scope, and no page here claims that a generated suite is the smallest one possible. Where coverwise picks a default rather than following a law, the page says which it is.

## How the four pages fit together

Each page uses the terms the pages before it defined.

1. [What combinatorial testing is](what-is-combinatorial-testing.md) — why testing every combination is not an option, and why a much smaller set is not an arbitrary sample.
2. [Tuples and coverage](tuples-and-coverage.md) — the unit a covering array covers, counted on a model small enough to check by hand.
3. [Strength](strength.md) — the number t that sets how large a coverage unit is, what t=2 leaves untested, and what raising it costs.
4. [Constraints and the required universe](constraints-and-the-universe.md) — how forbidding a combination changes both what gets built and what is required.

[Tuples and coverage](tuples-and-coverage.md) is the prerequisite for the last two, which are independent of each other and can be read in either order. Every term is glossed where a page first uses it, so a page can be opened on its own.

## What each concept decides

The middle column is the reason a caller needs the concept at all — the place where not knowing it produces a wrong call rather than a gap in vocabulary.

| Concept | What it decides in the API | Page |
|---|---|---|
| Cross-product, covering array | Why a generated suite is far smaller than the input space, and what its size may and may not be read as | [What combinatorial testing is](what-is-combinatorial-testing.md) |
| Tuple, coverage unit | What `totalTuples` counts, and why that number is not the number of test cases | [Tuples and coverage](tuples-and-coverage.md) |
| Coverage ratio | What `coverage` and `coverageRatio` are a fraction of, and what a value below 1 reports | [Tuples and coverage](tuples-and-coverage.md) |
| Strength (t) | What `strength` sets, and why raising it by one is not a small change | [Strength](strength.md) |
| Constraint | Which combinations are never built, and how `constraints` changes the denominator of the coverage ratio | [Constraints and the required universe](constraints-and-the-universe.md) |
| Required universe | Why a tuple can be missing from a suite without ever appearing in `uncovered` | [Constraints and the required universe](constraints-and-the-universe.md) |

## The vocabulary in one model

Everything the primer teaches is a field of one object. A model names the parameters and their values, the constraints that rule combinations out, and the strength the coverage is measured at.

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ],
  "constraints": ["IF os = Windows THEN browser != Safari"],
  "strength": 2
}
```

`parameters` and `values` describe the input space. `strength` fixes the size of a coverage unit. `constraints` decides which combinations are never generated and which are never required. Each surface spells this object in its own idiom — a JSON file for the CLI, an object literal for JavaScript, keyword arguments for Python — and means the same thing by it.

## Where to go next

- [Getting started](../getting-started.md) — install one surface and generate a first suite before reading any theory.
- [Introduction](../introduction.md) — what coverwise is, its three operations, and what it does not do.
- [Use cases](../use-cases/index.md) — complete flows that start from data an application already holds.
- [Glossary](../glossary.md) — every term defined on its own, each naming the API that implements it.
