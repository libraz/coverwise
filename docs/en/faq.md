# Questions and limitations

What coverwise refuses to decide, what it rejects and why, and the behaviour behind the fields the reference pages list. The vocabulary used here is defined in [Glossary](glossary.md) and taught in [Primer](primer/index.md).

## Scope

**Does coverwise run my tests?**
No. It designs a set of test cases and measures the coverage of one. Executing a case, asserting on it and reporting the result belong to the test runner; coverwise hands over rows and reads rows back.

**Does it know what my parameters mean?**
No. A parameter is a name and a list of opaque values. coverwise never infers that `os` names an operating system, that `'0'` is a special number, or that two values are related — an equivalence class or a constraint is how a relationship gets declared, and it acts only on what the model declares.

**Can it tell me whether pairwise is strong enough for my system?**
No, and it will not guess. Strength is a risk decision about the software under test; [Strength](primer/strength.md) sets out what t=2 covers and what raising t buys.

## Modelling

**Why was my one-parameter model rejected?**
Because the default strength is 2 and a coverage unit cannot draw two parameters from a model that has one. The error names both numbers — `Strength must be between 1 and parameter count: strength=2, parameters=1`. Set `strength: 1`, which asks for every value of every parameter to appear at least once.

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [{ name: 'os', values: ['Windows', 'macOS', 'Linux'] }],
  strength: 1,
});

result.tests.length;  // 3, one row per value
```

**Why can two values that differ only in ASCII case not coexist?**
Because value resolution folds ASCII case, so `os = WINDOWS` would have no single answer. A model that declares both `Windows` and `windows` on one parameter is rejected before generation starts rather than resolved by an arbitrary rule.

```text
error: Ambiguous value or alias 'windows' in parameter 'os'
```

The same rule covers aliases, and parameter names. Non-ASCII characters are not folded, so it constrains only the ASCII range.

**Do boundary parameters still need a `values` list?**
Yes. A parameter carrying `type` and `range` is still a parameter, and `values` is required by its shape; write `values: []` when the expansion is the whole value set. Anything listed there is merged into the expanded set rather than replaced.

**Why does `when('status').lt('ok')` compare against a parameter rather than the value `ok`?**
Because a string operand of `gt`, `gte`, `lt` or `lte` is a parameter name, emitted bare. The relational operators compare numbers, so the useful thing to put on the right is either a number or another parameter. A string that cannot be written as one bare token is refused outright rather than emitted as a literal comparison that would read as a different rule.

## Coverage

**Does 100% pairwise coverage mean my system is tested?**
No. It means every pair of values from two different parameters appears in at least one row. A failure that needs three specific values together can still hide in a fully covered pairwise suite, and coverage says nothing at all about whether the assertions are right or whether the model describes the system. It is a property of the test data, not a verdict on the testing.

**Why is my suite bigger than the theoretical minimum?**
Because coverwise builds greedily rather than searching for the smallest covering array, which is expensive to find and rarely worth the wait. The floor for a pairwise suite is the largest product of two parameter domains, a rule [Performance](performance.md) states in full: for a model of 3 operating systems, 4 browsers, 3 languages and 2 themes, that floor is 4 × 3 = 12, and coverwise produces 15 rows against a cross-product of 72. Correctness and speed come first, and minimality is approximated.

**Why is a tuple reported as uncovered when a constraint makes it impossible?**
It is not. A tuple no valid row can hold leaves the required universe instead of being reported as a gap, so it never appears in `uncovered`. With this constraint on a model of 3 operating systems and 3 browsers:

```
IF os = Windows THEN browser != Safari
```

`stats.totalTuples` reports 8 rather than 9, the pair `os=Windows, browser=Safari` is gone from the accounting, and the run still reaches `coverage` 1. [Constraints and the required universe](primer/constraints-and-the-universe.md) works the distinction through on a grid.

**What happens when `maxTests` cuts the run short?**
Generation stops at the ceiling and reports exactly where it stopped, rather than failing or silently claiming success.

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'arch',    values: ['x64', 'arm64'] },
    { name: 'channel', values: ['stable', 'preview'] },
  ],
  maxTests: 4,
});

result.tests.length;    // 4
result.coverage;        // 0.75 — 12 of the 16 required pairs
result.uncoveredCount;  // 4
result.warnings;        // ['Generation stopped at maxTests (4) before reaching 100% coverage']
```

The rows produced are still valid rows, `uncovered` names what was left, and the CLI exits 2. Two related cases behave differently from each other: a ceiling below the number of `seeds` drops the surplus seed rows and says so in `warnings`, while a ceiling below the number of rows `extendTests` was handed is rejected outright, because that call promises to preserve them.

**Why did a row of my hand-written suite not count?**
Because the parameter model does not describe it. `analyzeCoverage` excludes a row that is missing a parameter, or that names a value the model never declared, and lists it in `report.invalidTests` with a readable reason such as `value 'Linux' is not declared by parameter os` or `missing value for parameter browser`. The report is still produced; the CLI writes it to standard output and exits 3, because a suite it could not fully read is an input problem rather than a coverage result.

## Determinism

**Can I get the same suite twice?**
Yes. The same valid model and the same seed produce the same rows in the same order, on every surface. `seed` defaults to 0, so this holds even for a model that never mentions it. [Determinism](determinism.md) sets out the exact scope.

**Will the same seed give the same suite after I upgrade coverwise?**
Not necessarily. Determinism says the algorithm is a function of its input, not that the algorithm is frozen; a change to construction can produce a different, equally valid suite. Pin the version, or commit the generated suite and regenerate deliberately.

## Design decisions

**Why is coverage analysis independent of the generator?**
So that it can be trusted as ground truth. The validator is a layer of its own: `analyzeCoverage` enumerates the required universe from the model, counts what the rows hold, and consults nothing the generator recorded. That is what lets it judge a suite coverwise did not write as readily as one it did, and what makes a coverage claim about a generated suite worth something — a generator marking its own work would only report its own bookkeeping.

**Why is there no `--version` flag?**
There is not one, and `coverwise --version` is treated as an unknown command: usage on standard error, exit 3. `coverwise --help` is the only self-description the binary offers, and it writes to standard output and exits 0.

**Why does piping into `head` end in exit code 3?**
Because a reader that closes the pipe makes the write to standard output fail, and a run that could not deliver its output did not succeed. The CLI reports that as invalid input, which is the code it uses for every condition outside a coverage result.

```bash
coverwise generate input.json | head -c 200
# error: cannot write to standard output
# exit status 3
```

A line count large enough to reach the end of the document does not close the pipe, because the CLI writes the whole document as one line. Redirect to a file and read the file when a partial read is the goal.

**Why do the docs publish no per-configuration run times?**
Because nothing in this repository can re-derive them. The tuple and row counts in [Performance](performance.md) are regenerable: the model is on the page, the seed is `42`, and a run reproduces them. A wall-clock number is a property of the machine that produced it, so no reader could check one against a run of their own. Measure the configuration you care about on your own hardware.

## Where to go next

- [Primer](primer/index.md) — the concepts these answers assume, in prerequisite order.
- [Determinism](determinism.md) — what a seed guarantees and across which surfaces.
- [Examples](examples.md) — one recipe per feature, each with the numbers the engine returns.
- [Input limits](limits.md) — the sizes a model may declare, in one place.
- [Glossary](glossary.md) — every term above, each naming the API it belongs to.
