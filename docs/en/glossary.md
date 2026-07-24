# Glossary

Key terms used in combinatorial testing and coverwise.

## Combinatorial Testing

### Covering Array

A set of test cases where every t-wise combination of parameter values appears in at least one test. The primary output of coverwise.

### Pairwise Testing

Combinatorial testing with **strength 2**. Ensures every pair of parameter values from different parameters appears in at least one test. Catches the majority of interaction bugs with a fraction of exhaustive tests.

### t-wise Testing

Generalization of pairwise testing. **t** (the strength) is the number of parameters considered simultaneously. t=2 is pairwise, t=3 is 3-wise, and so on. Higher strength catches deeper interactions but requires more tests.

### Strength

The number of parameters in each coverage unit. For strength t, every possible combination of values from any t parameters must appear in the test suite.

| Strength | Coverage | Test-count implication |
|----------|----------|------------------------|
| 2 | All pairs | Usually much smaller than exhaustive testing |
| 3 | All triples | Usually requires more rows than pairwise coverage |
| t | All t-tuples | Depends on parameter domains, constraints, and the generator |

There is no general test-count formula for a concrete constrained model. Treat
`stats` as an estimate and generated coverage as the result to verify.

### Parameter

A dimension of variation in the system under test. Each parameter has a finite set of values.

### Value

One of the discrete options for a parameter. Values are strings internally but can represent numbers, booleans, or any discrete choice.

### Test Case

A complete assignment of one value to each parameter. A single row in the covering array.

### Tuple

A specific combination of values from t different parameters. For example, with strength 2, `(os=Windows, browser=Chrome)` is a tuple.

## Coverage

### Coverage Ratio

The fraction of required t-wise tuples that appear in the test suite. Ranges from 0.0 to 1.0. coverwise targets 1.0 (100%).

### Uncovered Tuple

A required t-wise combination that does not appear in any test case. coverwise reports these in human-readable format.

### Total Tuples

The number of required t-wise combinations after exclusions. Tuples containing a
value marked invalid are excluded from positive coverage. With constraints, a
tuple is excluded only if no full assignment of valid values can complete it
while satisfying every constraint.

## Constraints

### Constraint

A boolean expression that defines invalid parameter combinations. Positive test
cases violating constraints are never generated. For coverage accounting, a
partial tuple is removed only when no valid full completion can satisfy all
constraints.

### Constraint Pruning

During test case construction, the generator evaluates constraints on partial assignments and prunes branches that would lead to violations. This is more efficient than generate-and-filter.

## Advanced Features

### Sub-Model

A group of parameters with a different strength than the default. Allows mixed-strength coverage — e.g., pairwise overall, but 3-wise for critical parameters.

### Seed Test

An existing test case provided as input. The generator preserves seed tests and generates additional tests to fill coverage gaps.

### Negative Test

A test case containing exactly one invalid value. Used to verify that the system
correctly rejects bad input. coverwise generates these separately from values
marked as `invalid`; invalid values never count toward positive coverage.

Negative generation targets feasible requested-strength tuples containing one
invalid value. A `maxTests` limit applies to both positive and negative rows, so
negative generation can be incomplete. Check `negativeCoverage` and `warnings`
for the covered and omitted negative tuples.

### Equivalence Class

A grouping of parameter values that are expected to behave similarly. coverwise tracks coverage at the class level in addition to value level.

### Boundary Value

A value at or near the edge of a valid range (minimum, maximum, and adjacent values). coverwise can auto-expand numeric ranges into boundary values.

### Weight

A hint to the generator about value preference. When multiple values offer equal coverage improvement, one is chosen by weighted random selection — the probability of picking a value is proportional to its weight, so a higher weight makes a value more likely (not guaranteed) to be chosen. Does not affect coverage completeness.

## Algorithm

### Greedy Construction

The algorithm used by coverwise. For each test case, it evaluates candidates and selects the one that covers the most uncovered tuples. Produces near-optimal covering arrays in practice.

### Determinism

Given the same input parameters, constraints, strength, and seed, coverwise produces identical output. This makes test generation reproducible across environments.
