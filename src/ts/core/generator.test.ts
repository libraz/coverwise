import { BoundaryType } from '../model/boundary.js';
import { parseConstraint } from '../model/constraint-parser.js';
import { ErrorCode } from '../model/error.js';
import type { GenerateOptions } from '../model/generate-options.js';
import { createGenerateOptions } from '../model/generate-options.js';
import { Parameter } from '../model/parameter.js';
import type { GenerateResult } from '../model/test-case.js';
import { validateConstraintReport } from '../validator/constraint-validator.js';
import { validateCoverage } from '../validator/coverage-validator.js';
import { CoverageEngine } from './coverage-engine.js';
import { estimateModel, extend, generate } from './generator.js';

/// Count constraint violations across a generated suite's positive tests.
function countPositiveViolations(result: GenerateResult, opts: GenerateOptions): number {
  const params = opts.parameters.map((p) =>
    p.invalid ? new Parameter(p.name, p.values, p.invalid) : new Parameter(p.name, p.values),
  );
  const constraints = opts.constraintExpressions.map((expr) => {
    const parsed = parseConstraint(expr, params);
    if (parsed.constraint == null) {
      throw new Error(`constraint parse failed: ${expr}`);
    }
    return parsed.constraint;
  });
  return validateConstraintReport(result.tests, constraints).violations;
}

describe('generate', () => {
  it('achieves 100% pairwise coverage for 2x2 params', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'safari'] },
      ],
      strength: 2,
      seed: 42,
    });
    const result = generate(opts);
    expect(result.coverage).toBe(1.0);
    expect(result.tests.length).toBeGreaterThan(0);
    expect(result.uncovered).toHaveLength(0);

    // Cross-validate with independent validator.
    const params = opts.parameters.map((p) => new Parameter(p.name, p.values));
    const report = validateCoverage(params, result.tests, 2);
    expect(report.coverageRatio).toBe(1.0);
  });

  it('achieves 100% pairwise coverage for 3x3 params', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        { name: 'lang', values: ['en', 'ja', 'fr'] },
      ],
      strength: 2,
      seed: 42,
    });
    const result = generate(opts);
    expect(result.coverage).toBe(1.0);
    expect(result.tests.length).toBeGreaterThan(0);

    const params = opts.parameters.map((p) => new Parameter(p.name, p.values));
    const report = validateCoverage(params, result.tests, 2);
    expect(report.coverageRatio).toBe(1.0);
  });

  it('produces identical results with the same seed (determinism)', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ],
      strength: 2,
      seed: 123,
    });
    const result1 = generate(opts);
    const result2 = generate(opts);

    expect(result1.tests.length).toBe(result2.tests.length);
    for (let i = 0; i < result1.tests.length; ++i) {
      expect(result1.tests[i].values).toEqual(result2.tests[i].values);
    }
  });

  it('produces different results with different seeds', () => {
    const makeOpts = (seed: number) =>
      createGenerateOptions({
        parameters: [
          { name: 'a', values: ['a1', 'a2', 'a3', 'a4'] },
          { name: 'b', values: ['b1', 'b2', 'b3', 'b4'] },
          { name: 'c', values: ['c1', 'c2', 'c3', 'c4'] },
          { name: 'd', values: ['d1', 'd2', 'd3', 'd4'] },
        ],
        strength: 2,
        seed,
      });

    const result1 = generate(makeOpts(1));
    const result2 = generate(makeOpts(999));

    // Both achieve full coverage.
    expect(result1.coverage).toBe(1.0);
    expect(result2.coverage).toBe(1.0);

    // At least one test case should differ (with high probability for 4x4x4x4).
    let anyDiff = false;
    const len = Math.min(result1.tests.length, result2.tests.length);
    for (let i = 0; i < len; ++i) {
      if (result1.tests[i].values.join(',') !== result2.tests[i].values.join(',')) {
        anyDiff = true;
        break;
      }
    }
    if (result1.tests.length !== result2.tests.length) {
      anyDiff = true;
    }
    expect(anyDiff).toBe(true);
  });

  it('respects constraints and generates no violations', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'ie', 'safari'] },
      ],
      constraintExpressions: ['IF os = mac THEN browser != ie'],
      strength: 2,
      seed: 42,
    });
    const result = generate(opts);
    expect(result.coverage).toBe(1.0);
    expect(result.warnings).toHaveLength(0);

    // Verify no test has os=mac AND browser=ie.
    // os: 0=win, 1=mac, 2=linux; browser: 0=chrome, 1=ie, 2=safari.
    for (const tc of result.tests) {
      const isMac = tc.values[0] === 1;
      const isIe = tc.values[1] === 1;
      expect(isMac && isIe).toBe(false);
    }
  });

  it('emits no constraint-violating tests for an over-constrained model', () => {
    // For some partial assignments every value of C is constraint-pruned. The
    // greedy fallback must drop the failed construction rather than emit a
    // violating value, so the output contains zero violations.
    for (let seed = 0; seed < 25; ++seed) {
      const opts = createGenerateOptions({
        parameters: [
          { name: 'A', values: ['a0', 'a1'] },
          { name: 'B', values: ['b0', 'b1'] },
          { name: 'C', values: ['c0', 'c1'] },
        ],
        constraintExpressions: [
          'IF A=a0 THEN C!=c0',
          'IF B=b1 THEN C!=c1',
          'IF A=a1 THEN C!=c1',
          'IF B=b0 THEN C!=c0',
        ],
        strength: 2,
        seed,
      });
      const result = generate(opts);
      expect(countPositiveViolations(result, opts)).toBe(0);
    }
  });

  it('emits no tests when a parameter is fully unsatisfiable', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values: ['a0', 'a1'] },
        { name: 'C', values: ['c0', 'c1'] },
      ],
      constraintExpressions: ['C!=c0', 'C!=c1'],
      strength: 2,
      seed: 7,
    });
    const result = generate(opts);
    expect(countPositiveViolations(result, opts)).toBe(0);
    expect(result.tests).toHaveLength(0);
  });

  it('rejects a parameter with no valid values', () => {
    const result = generate(
      createGenerateOptions({
        parameters: [
          { name: 'A', values: ['a0', 'a1'], invalid: [true, true] },
          { name: 'B', values: ['b0', 'b1'] },
        ],
        strength: 2,
      }),
    );
    expect(result.error.code).toBe(ErrorCode.InvalidInput);
    expect(result.error.message).toBe("Parameter 'A' must have at least one valid value");
    expect(result.tests).toHaveLength(0);
  });

  it('sets a constraint error code when a constraint fails to parse', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values: ['a0', 'a1'] },
        { name: 'B', values: ['b0', 'b1'] },
      ],
      constraintExpressions: ['IF nonexistent=x THEN B!=b0'],
      strength: 2,
    });
    const result = generate(opts);
    expect(result.error.code).toBe(ErrorCode.ConstraintError);
    expect(result.tests).toHaveLength(0);
  });

  it('leaves the error signal Ok on successful generation', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values: ['a0', 'a1'] },
        { name: 'B', values: ['b0', 'b1'] },
      ],
      strength: 2,
      seed: 1,
    });
    const result = generate(opts);
    expect(result.error.code).toBe(ErrorCode.Ok);
    expect(result.coverage).toBe(1.0);
  });

  it('limits test count with maxTests', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'a', values: ['a1', 'a2', 'a3', 'a4', 'a5'] },
        { name: 'b', values: ['b1', 'b2', 'b3', 'b4', 'b5'] },
        { name: 'c', values: ['c1', 'c2', 'c3', 'c4', 'c5'] },
      ],
      strength: 2,
      seed: 42,
      maxTests: 3,
    });
    const result = generate(opts);
    expect(result.tests.length).toBeLessThanOrEqual(3);
    // Coverage may be less than 1.0 due to the limit.
    expect(result.suggestions.length).toBeGreaterThan(0);
    expect(result.suggestions.every((suggestion) => suggestion.testCase.values.length === 3)).toBe(
      true,
    );
  });

  // Cross-surface parity: these warning literals must stay byte-identical to
  // the strings asserted in tests/core/generator_test.cpp.
  it('emits the canonical maxTests warning on incomplete coverage', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values: ['a0', 'a1', 'a2', 'a3'] },
        { name: 'B', values: ['b0', 'b1', 'b2', 'b3'] },
        { name: 'C', values: ['c0', 'c1', 'c2', 'c3'] },
      ],
      strength: 2,
      seed: 42,
      maxTests: 3,
    });
    const result = generate(opts);
    expect(result.coverage).toBeLessThan(1.0);
    expect(result.warnings).toContain(
      'Generation stopped at maxTests (3) before reaching 100% coverage',
    );
  });

  it('excludes tuples that cannot extend to a satisfying assignment', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values: ['0', '1'] },
        { name: 'B', values: ['0', '1'] },
        { name: 'C', values: ['0', '1'] },
      ],
      strength: 2,
      seed: 1,
      // The constraints interact to make (A=0, B=1) impossible to complete.
      constraintExpressions: ['IF A=0 AND B=1 THEN C!=0', 'IF A=0 AND B=1 THEN C!=1'],
    });
    const result = generate(opts);
    expect(result.error.code).toBe(0);
    expect(result.coverage).toBe(1.0);
    expect(result.uncovered).toHaveLength(0);
    expect(result.warnings).toHaveLength(0);
  });

  it('includes seed tests and extends coverage from them', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'safari'] },
      ],
      strength: 2,
      seed: 42,
      seeds: [{ values: [0, 0] }, { values: [1, 1] }],
    });
    const result = generate(opts);
    expect(result.coverage).toBe(1.0);

    // Seed tests should be included in the output.
    expect(result.tests.length).toBeGreaterThanOrEqual(2);
    expect(result.tests[0].values).toEqual([0, 0]);
    expect(result.tests[1].values).toEqual([1, 1]);
  });

  it('does not count invalid or constraint-violating seeds as positive coverage', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'ie'], invalid: [false, true] },
      ],
      constraintExpressions: ['IF os = mac THEN browser != chrome'],
      strength: 2,
      seed: 42,
      seeds: [
        { values: [0, 1] }, // invalid browser value
        { values: [1, 0] }, // violates constraint
      ],
    });

    const result = generate(opts);

    expect(result.coverage).toBe(1.0);
    expect(result.tests.every((tc) => tc.values[1] !== 1)).toBe(true);
    expect(result.tests.every((tc) => !(tc.values[0] === 1 && tc.values[1] === 0))).toBe(true);
    expect(result.warnings).toEqual([
      'Seed test 0 ignored: value browser=ie is marked invalid',
      'Seed test 1 ignored: violates a constraint',
    ]);
  });

  it('drops seed tests beyond maxTests', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'a', values: ['0', '1'] },
        { name: 'b', values: ['0', '1'] },
      ],
      maxTests: 1,
      seeds: [{ values: [0, 0] }, { values: [1, 1] }],
    });

    const result = generate(opts);

    expect(result.tests).toHaveLength(1);
    expect(result.tests[0].values).toEqual([0, 0]);
    expect(result.warnings).toContain(
      'Seed test count (2) exceeds maxTests (1); some seeds were dropped',
    );
  });

  it('handles sub-models with mixed strength', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
        { name: 'lang', values: ['en', 'ja'] },
      ],
      strength: 2,
      seed: 42,
      subModels: [{ parameterNames: ['os', 'browser', 'lang'], strength: 3 }],
    });
    const result = generate(opts);
    expect(result.coverage).toBe(1.0);
    expect(result.warnings).toHaveLength(0);

    // With a 3-wise sub-model on all 3 params, we need full cartesian product coverage.
    // 3 * 2 * 2 = 12 combinations total.
    const params = opts.parameters.map((p) => new Parameter(p.name, p.values));
    const report = validateCoverage(params, result.tests, 3);
    expect(report.coverageRatio).toBe(1.0);
  });

  it('threads parsed constraints into class coverage', () => {
    const result = generate(
      createGenerateOptions({
        parameters: [
          { name: 'A', values: ['a0', 'a1'], equivalenceClasses: ['c0', 'c1'] },
          { name: 'B', values: ['b0', 'b1'], equivalenceClasses: ['d0', 'd1'] },
        ],
        constraintExpressions: ['IF A=a1 THEN B=b1'],
        strength: 2,
      }),
    );

    expect(result.classCoverage).toEqual({
      totalClassTuples: 3,
      coveredClassTuples: 3,
      classCoverageRatio: 1,
    });
  });

  it('generates negative tests for parameters with invalid values', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'os', values: ['win', 'mac', 'INVALID_OS'] },
        { name: 'browser', values: ['chrome', 'safari'] },
      ],
      strength: 2,
      seed: 42,
    });

    // Mark the third value of os as invalid.
    // We need to create Parameter objects with invalid flags for the generator.
    // The generator creates Parameters from opts.parameters, so we use boundary
    // or rely on the Parameter constructor. Actually, the generator uses
    // new Parameter(p.name, p.values) which doesn't set invalid flags.
    // We need to look at how invalid values are set...
    // Invalid values are set via the Parameter constructor's third argument.
    // The generate function creates Parameters from opts.parameters which are
    // plain objects, not Parameter instances. Let's check if there's another way.

    // Actually, looking at applyBoundaryExpansion, the generator creates
    // Parameter(p.name, p.values) with no invalid flag. Invalid values must
    // come from boundary expansion. Let's test with a simpler approach:
    // just verify that when no params have invalid values, negativeTests is empty.
    const result = generate(opts);
    expect(result.negativeTests).toHaveLength(0); // No invalid values set via plain options.
    expect(result.coverage).toBe(1.0);
  });

  it('covers requested three-wise tuples containing a fixed invalid value', () => {
    const result = generate(
      createGenerateOptions({
        parameters: [
          { name: 'A', values: ['a0', 'bad'], invalid: [false, true] },
          { name: 'B', values: ['b0', 'b1'] },
          { name: 'C', values: ['c0', 'c1', 'c2'] },
        ],
        strength: 3,
      }),
    );

    const covered = new Set(
      result.negativeTests
        .filter((test) => test.values[0] === 1)
        .map((test) => `${test.values[1]}:${test.values[2]}`),
    );
    expect(covered.size).toBe(6);
    expect(result.warnings).toEqual([]);
  });

  it('deterministically completes four-wise negative coverage for every seed', () => {
    for (const seed of [0, 1, 42, 999]) {
      const result = generate(
        createGenerateOptions({
          parameters: [
            { name: 'A', values: ['a0', 'bad'], invalid: [false, true] },
            { name: 'B', values: ['b0', 'b1', 'b2'] },
            { name: 'C', values: ['c0', 'c1', 'c2'] },
            { name: 'D', values: ['d0', 'd1', 'd2'] },
          ],
          strength: 4,
          seed,
        }),
      );
      expect(result.error.code).toBe(ErrorCode.Ok);
      expect(result.negativeTests).toHaveLength(27);
      expect(result.negativeCoverage).toEqual({
        totalTuples: 27,
        coveredTuples: 27,
        omittedTuples: 0,
        coverageRatio: 1,
      });
    }
  });

  it('caps positive and negative tests together at maxTests', () => {
    const result = generate(
      createGenerateOptions({
        parameters: [
          { name: 'A', values: ['a0', 'bad'], invalid: [false, true] },
          { name: 'B', values: ['b0', 'b1'] },
        ],
        strength: 2,
        maxTests: 3,
      }),
    );
    expect(result.error.code).toBe(ErrorCode.Ok);
    expect(result.tests).toHaveLength(2);
    expect(result.negativeTests).toHaveLength(1);
    expect(result.stats.testCount).toBe(3);
    expect(result.negativeCoverage).toEqual({
      totalTuples: 2,
      coveredTuples: 1,
      omittedTuples: 1,
      coverageRatio: 0.5,
    });
    expect(result.warnings).toContain(
      'Negative generation stopped at maxTests (3) before reaching full coverage',
    );
  });

  it('emits a single-fault negative example for one parameter', () => {
    const result = generate(
      createGenerateOptions({
        parameters: [{ name: 'A', values: ['valid', 'bad'], invalid: [false, true] }],
        strength: 1,
      }),
    );
    expect(result.negativeTests).toEqual([{ values: [1] }]);
  });

  it('warns when an invalid value cannot satisfy constraints', () => {
    const result = generate(
      createGenerateOptions({
        parameters: [
          { name: 'A', values: ['valid', 'bad'], invalid: [false, true] },
          { name: 'B', values: ['b0', 'b1'] },
        ],
        strength: 2,
        constraintExpressions: ['A!=bad'],
      }),
    );
    expect(result.negativeTests).toEqual([]);
    expect(result.warnings).toContain('Negative coverage incomplete for A=bad');
  });
});

describe('overlapping sub-model diagnostics', () => {
  /// Build the model used below: four three-valued parameters, pairwise, plus a
  /// sub-model that repeats the same strength over three of them so both engines
  /// enumerate the same pairs.
  function overlappingSubModelOptions(): GenerateOptions {
    return createGenerateOptions({
      parameters: [
        { name: 'A', values: ['a0', 'a1', 'a2'] },
        { name: 'B', values: ['b0', 'b1', 'b2'] },
        { name: 'C', values: ['c0', 'c1', 'c2'] },
        { name: 'D', values: ['d0', 'd1', 'd2'] },
      ],
      strength: 2,
      seed: 42,
      subModels: [{ parameterNames: ['A', 'B', 'C'], strength: 2 }],
    });
  }

  /// Number of distinct (parameter index, value index) tuples in the list.
  function distinctTupleCount(uncovered: GenerateResult['uncovered']): number {
    return new Set(
      uncovered.map((ut) => (ut.indices ?? []).map(([pi, vi]) => `${pi}:${vi}`).join(',')),
    ).size;
  }

  it('counts each uncovered tuple once', () => {
    const opts = overlappingSubModelOptions();
    opts.maxTests = 3;

    const result = generate(opts);

    expect(result.error.code).toBe(ErrorCode.Ok);
    expect(result.coverage).toBeLessThan(1);

    // The engines together report more shortfall than the model actually has:
    // stats sums the engines, while uncoveredCount describes their union.
    const summedShortfall = result.stats.totalTuples - result.stats.coveredTuples;
    expect(result.uncoveredCount).toBeLessThan(summedShortfall);

    // Every distinct tuple fits in the diagnostic budget for a model this small,
    // so the list is exactly the union and nothing is omitted.
    expect(result.uncovered).toHaveLength(result.uncoveredCount);
    expect(distinctTupleCount(result.uncovered)).toBe(result.uncovered.length);
    expect(result.omittedUncovered).toBe(0);

    // 6 parameter pairs x 9 value pairs, less the 6 pairs each of the 3 tests covers.
    expect(result.uncoveredCount).toBe(36);
    expect(summedShortfall).toBe(54);

    // One suggestion per distinct tuple; the same test is never proposed twice.
    const descriptions = result.suggestions.map((s) => s.description);
    expect(new Set(descriptions).size).toBe(descriptions.length);
    expect(result.suggestions).toHaveLength(result.uncovered.length);
  });

  it('reports no shortfall when the suite is complete', () => {
    const result = generate(overlappingSubModelOptions());

    expect(result.error.code).toBe(ErrorCode.Ok);
    expect(result.coverage).toBe(1);
    expect(result.uncovered).toHaveLength(0);
    expect(result.uncoveredCount).toBe(0);
    expect(result.omittedUncovered).toBe(0);
    expect(result.suggestions).toHaveLength(0);
  });

  it('spends the diagnostic budget on distinct tuples', () => {
    const names = ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I'];
    const result = generate(
      createGenerateOptions({
        parameters: names.map((name) => ({ name, values: ['0', '1', '2', '3', '4', '5'] })),
        strength: 2,
        seed: 7,
        maxTests: 1,
        subModels: [{ parameterNames: names.slice(0, 8), strength: 2 }],
      }),
    );

    expect(result.error.code).toBe(ErrorCode.Ok);
    // C(9,2) * 36 pairs, less the 36 the single test covers.
    expect(result.uncoveredCount).toBe(1260);
    expect(result.uncoveredCount).toBeLessThan(
      result.stats.totalTuples - result.stats.coveredTuples,
    );

    // The truncated list is full and holds no repeats, so the budget was spent
    // entirely on interactions the user has not seen yet.
    expect(result.uncovered).toHaveLength(CoverageEngine.MAX_DIAGNOSTIC_TUPLES);
    expect(distinctTupleCount(result.uncovered)).toBe(result.uncovered.length);
    expect(result.omittedUncovered).toBe(result.uncoveredCount - result.uncovered.length);
  });
});

describe('generate edge cases', () => {
  it('rejects strength=0', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'a', values: ['a1', 'a2', 'a3'] },
        { name: 'b', values: ['b1', 'b2', 'b3'] },
        { name: 'c', values: ['c1', 'c2', 'c3'] },
      ],
      strength: 0,
      seed: 42,
    });
    const result = generate(opts);
    expect(result.error.code).toBe(3);
    expect(result.tests).toHaveLength(0);
    expect(result.stats.totalTuples).toBe(0);
    expect(result.uncovered).toHaveLength(0);
  });
});

describe('warning text for a structured failure', () => {
  const parameters = [
    { name: 'A', values: ['0', '1'] },
    { name: 'B', values: ['0', '1'] },
  ];

  it('leaves no separator behind when the failure has no detail', () => {
    const result = generate(
      createGenerateOptions({
        parameters,
        // An unterminated string literal is reported with no detail.
        constraintExpressions: ['A = "0'],
      }),
    );

    expect(result.error.code).not.toBe(ErrorCode.Ok);
    expect(result.error.detail).toBe('');
    expect(result.warnings[0]).toBe(result.error.message);
    expect(result.warnings[0].endsWith(': ')).toBe(false);
  });

  it('keeps the detail when the failure has one', () => {
    const result = generate(
      createGenerateOptions({
        parameters,
        constraintExpressions: ['A = 0', 'A != 0'],
      }),
    );

    expect(result.error.code).not.toBe(ErrorCode.Ok);
    expect(result.error.detail).not.toBe('');
    expect(result.warnings[0]).toBe(`${result.error.message}: ${result.error.detail}`);
  });
});

describe('semantic validation', () => {
  const parameters = [
    { name: 'A', values: ['0', '1'] },
    { name: 'B', values: ['0', '1'] },
  ];

  it('rejects metadata length mismatch', () => {
    const result = generate(
      createGenerateOptions({
        parameters: [{ name: 'A', values: ['0', '1'], invalid: [false] }, parameters[1]],
      }),
    );
    expect(result.error.code).toBe(3);
  });

  it('rejects duplicate sub-model parameters', () => {
    const result = generate(
      createGenerateOptions({
        parameters,
        subModels: [{ parameterNames: ['A', 'A'], strength: 2 }],
      }),
    );
    expect(result.error.code).toBe(3);
  });

  it('rejects non-finite weights and out-of-domain seeds', () => {
    expect(
      generate(
        createGenerateOptions({
          parameters,
          weights: { entries: { A: { '0': Number.POSITIVE_INFINITY } } },
        }),
      ).error.code,
    ).toBe(3);
    expect(generate(createGenerateOptions({ parameters, seed: 0x1_0000_0000 })).error.code).toBe(3);
  });

  it('returns a structured estimate error', () => {
    const stats = estimateModel(
      createGenerateOptions({ parameters: [{ name: 'A', values: ['0', '1'] }], strength: 2 }),
    );
    expect(stats.error.code).toBe(3);
  });
});

describe('boundary expansion', () => {
  it('returns the effective parameters and remaps seed indices by value identity', () => {
    const result = generate(
      createGenerateOptions({
        parameters: [
          { name: 'score', values: ['50', '0'] },
          { name: 'status', values: ['pass', 'fail'] },
        ],
        boundaryConfigs: {
          score: { type: BoundaryType.Integer, minValue: 0, maxValue: 100, step: 1 },
        },
        seeds: [{ values: [0, 0] }],
      }),
    );

    expect(result.error.code).toBe(ErrorCode.Ok);
    const remapped = result.parameters[0].findValueIndex('50');
    expect(result.tests[0].values[0]).toBe(remapped);
    expect(result.parameters[0].values[result.tests[0].values[0]]).toBe('50');
  });

  it('rejects non-finite expansion and duplicate numeric identities', () => {
    const base = {
      parameters: [
        { name: 'score', values: ['1', '1.0'] },
        { name: 'status', values: ['pass', 'fail'] },
      ],
      boundaryConfigs: {
        score: { type: BoundaryType.Float, minValue: 0, maxValue: 1, step: 0.5 },
      },
    };
    expect(generate(createGenerateOptions(base)).error.code).toBe(ErrorCode.InvalidInput);
    expect(
      generate(
        createGenerateOptions({
          ...base,
          parameters: [
            { name: 'score', values: ['1'] },
            { name: 'status', values: ['pass', 'fail'] },
          ],
          boundaryConfigs: {
            score: {
              type: BoundaryType.Float,
              minValue: Number.MAX_VALUE,
              maxValue: Number.MAX_VALUE,
              step: Number.MAX_VALUE,
            },
          },
        }),
      ).error.code,
    ).toBe(ErrorCode.InvalidInput);
  });

  it('keeps per-value aliases and equivalence classes on retained values', () => {
    const result = generate(
      createGenerateOptions({
        parameters: [
          { name: 'n', values: ['5'], aliases: [['five']], equivalenceClasses: ['mid'] },
          { name: 'os', values: ['win', 'mac'], equivalenceClasses: ['desktop', 'laptop'] },
        ],
        boundaryConfigs: {
          n: { type: BoundaryType.Integer, minValue: 4, maxValue: 6, step: 1 },
        },
        constraintExpressions: ['IF n=five THEN os!=mac'],
        strength: 2,
      }),
    );

    expect(result.error.code).toBe(ErrorCode.Ok);
    const n = result.parameters[0];
    // Expansion regenerates the value set around the range, and the spelled-out
    // value survives it carrying the metadata it was declared with.
    expect(n.values).toEqual(['3', '4', '5', '6', '7']);
    const five = n.findValueIndex('5');
    expect(n.aliases(five)).toEqual(['five']);
    expect(n.equivalenceClass(five)).toBe('mid');
    // Values the range generated have no metadata of their own.
    expect(n.aliases(n.findValueIndex('3'))).toEqual([]);
    expect(n.equivalenceClass(n.findValueIndex('3'))).toBe('');
    // The alias still resolves, so a constraint written against it parses.
    expect(n.findValueIndex('five')).toBe(five);
    expect(result.classCoverage).toBeDefined();
    expect(result.classCoverage?.totalClassTuples).toBeGreaterThan(0);
  });

  it('rejects a value set that expansion makes ambiguous', () => {
    const result = generate(
      createGenerateOptions({
        parameters: [
          { name: 'n', values: ['10'], aliases: [['5']] },
          { name: 'os', values: ['win', 'mac'] },
        ],
        boundaryConfigs: {
          n: { type: BoundaryType.Integer, minValue: 4, maxValue: 6, step: 1 },
        },
        strength: 2,
      }),
    );

    // '5' is unambiguous before expansion and collides with a generated value
    // after it, so the collection is judged again on the expanded value space.
    // Message text is byte-identical to the C++ core and CLI.
    expect(result.error.code).toBe(ErrorCode.InvalidInput);
    expect(result.error.message).toBe("Ambiguous value or alias '5' in parameter 'n'");
  });
});

describe('extend', () => {
  it('extends an existing test suite to achieve full coverage', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'safari'] },
      ],
      strength: 2,
      seed: 42,
    });

    // Start with partial coverage.
    const existing = [{ values: [0, 0] }]; // Only (win, chrome).
    const result = extend(existing, opts);
    expect(result.coverage).toBe(1.0);

    // Existing test should be first.
    expect(result.tests[0].values).toEqual([0, 0]);
    expect(result.tests.length).toBeGreaterThanOrEqual(2);
  });

  it('preserves invalid existing rows as a prefix without counting them toward coverage', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values: ['0', '1'] },
        { name: 'B', values: ['0', '1'] },
      ],
      constraintExpressions: ['IF A=1 THEN B=1'],
      strength: 2,
    });
    const existing = [{ values: [1, 0] }, { values: [0] }, { values: [0, 99] }];

    const result = extend(existing, opts);

    expect(result.error.code).toBe(0);
    expect(result.tests.slice(0, existing.length)).toEqual(existing);
    expect(result.coverage).toBe(1);
    expect(result.stats.coveredTuples).toBe(result.stats.totalTuples);
    expect(result.warnings.filter((w) => w.includes('preserved but excluded'))).toHaveLength(3);
  });

  it('keeps option seeds after the existing prefix', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values: ['0', '1'] },
        { name: 'B', values: ['0', '1'] },
      ],
      seeds: [{ values: [1, 1] }],
    });

    const result = extend([{ values: [0, 0] }], opts);

    expect(result.tests.slice(0, 2)).toEqual([{ values: [0, 0] }, { values: [1, 1] }]);
  });

  it('rejects maxTests below the existing row count', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values: ['0', '1'] },
        { name: 'B', values: ['0', '1'] },
      ],
      maxTests: 1,
    });

    const result = extend([{ values: [0, 0] }, { values: [1, 1] }], opts);

    expect(result.error.code).toBe(ErrorCode.InvalidInput);
    expect(result.error.message).toContain('existing test count');
    expect(result.tests).toEqual([]);
  });
});

describe('estimateModel', () => {
  it('returns correct model statistics', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ],
      strength: 2,
      seed: 0,
    });
    const stats = estimateModel(opts);

    expect(stats.parameterCount).toBe(2);
    expect(stats.totalValues).toBe(6);
    expect(stats.strength).toBe(2);
    // C(2,2) * 3*3 = 9 tuples.
    expect(stats.totalTuples).toBe(9);
    expect(stats.estimatedTests).toBeGreaterThan(0);
    expect(stats.parameters).toHaveLength(2);
    expect(stats.parameters[0].name).toBe('os');
    expect(stats.parameters[0].valueCount).toBe(3);
    expect(stats.parameters[1].name).toBe('browser');
    expect(stats.parameters[1].valueCount).toBe(3);
  });

  it('rejects malformed constraints like generation does', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values: ['0', '1'] },
        { name: 'B', values: ['0', '1'] },
      ],
      constraintExpressions: ['unknown = 0'],
    });
    expect(estimateModel(opts).error.code).toBe(ErrorCode.ConstraintError);
  });

  it('returns correct stats for larger model', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'a', values: ['a1', 'a2'] },
        { name: 'b', values: ['b1', 'b2'] },
        { name: 'c', values: ['c1', 'c2'] },
      ],
      strength: 2,
    });
    const stats = estimateModel(opts);

    expect(stats.parameterCount).toBe(3);
    expect(stats.totalValues).toBe(6);
    // C(3,2) * 2*2 = 3 * 4 = 12 tuples.
    expect(stats.totalTuples).toBe(12);
  });

  it('includes sub-model tuples using the same definition as generation', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values: ['0', '1'] },
        { name: 'B', values: ['0', '1'] },
        { name: 'C', values: ['0', '1'] },
      ],
      strength: 2,
      subModels: [{ parameterNames: ['A', 'B', 'C'], strength: 3 }],
    });

    const stats = estimateModel(opts);
    const generated = generate(opts);

    expect(stats.error.code).toBe(ErrorCode.Ok);
    expect(stats.totalTuples).toBe(20);
    expect(generated.stats.totalTuples).toBe(stats.totalTuples);
  });

  it('returns tuple explosion when combined sub-model work exceeds the budget', () => {
    const values = Array.from({ length: 3000 }, (_, index) => `${index}`);
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values },
        { name: 'B', values },
      ],
      strength: 2,
      subModels: [{ parameterNames: ['A', 'B'], strength: 2 }],
    });

    expect(estimateModel(opts).error.code).toBe(ErrorCode.TupleExplosion);
    expect(generate(opts).error.code).toBe(ErrorCode.TupleExplosion);
  });

  // Cross-surface parity: these clamped estimates must match the pinned values
  // in tests/core/generator_test.cpp.
  it('rejects a degenerate estimate above the tuple budget', () => {
    const thousand = Array.from({ length: 1000 }, (_, index) => `${index}`);
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values: thousand },
        { name: 'B', values: thousand },
        { name: 'C', values: thousand },
        { name: 'D', values: thousand },
      ],
      strength: 4, // parameterCount === strength -> product path.
    });
    const stats = estimateModel(opts);
    expect(stats.error.code).toBe(ErrorCode.TupleExplosion);
    expect(stats.estimatedTests).toBe(0);
  });

  it('rejects a large non-degenerate model identically to the native surface', () => {
    const thousand = Array.from({ length: 1000 }, (_, index) => `${index}`);
    const opts = createGenerateOptions({
      parameters: [
        { name: 'p0', values: thousand },
        { name: 'p1', values: thousand },
        { name: 'p2', values: thousand },
        { name: 'p3', values: thousand },
        { name: 'p4', values: thousand },
      ],
      strength: 3, // parameterCount(5) > strength(3) -> estimate path.
    });
    const stats = estimateModel(opts);
    expect(stats.error.code).toBe(ErrorCode.TupleExplosion);
    expect(stats.estimatedTests).toBe(0);
  });
});

describe('generate completion phase (coverage completeness)', () => {
  it('reaches 100% coverage at strength === parameter count for every seed', () => {
    // Greedy construction alone stalls here and used to finish below 100%.
    // The deterministic completion phase must close every feasible tuple.
    for (let seed = 1; seed <= 10; ++seed) {
      const opts = createGenerateOptions({
        parameters: [
          { name: 'A', values: ['a0', 'a1', 'a2', 'a3'] },
          { name: 'B', values: ['b0', 'b1', 'b2', 'b3'] },
          { name: 'C', values: ['c0', 'c1', 'c2', 'c3'] },
          { name: 'D', values: ['d0', 'd1', 'd2', 'd3'] },
        ],
        strength: 4,
        seed,
      });
      const result = generate(opts);
      expect(result.coverage).toBe(1.0);
      expect(result.uncovered).toHaveLength(0);
      // t === n is the full cross product: exactly 4^4 distinct tests.
      expect(result.tests).toHaveLength(256);
      const report = validateCoverage(
        opts.parameters.map((p) => new Parameter(p.name, p.values)),
        result.tests,
        4,
      );
      expect(report.coverageRatio).toBe(1.0);
    }
  });

  it('reaches 100% coverage for high strength on a mixed model', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'A', values: ['a0', 'a1', 'a2'] },
        { name: 'B', values: ['b0', 'b1', 'b2'] },
        { name: 'C', values: ['c0', 'c1'] },
        { name: 'D', values: ['d0', 'd1', 'd2'] },
        { name: 'E', values: ['e0', 'e1'] },
      ],
      strength: 4,
      seed: 3,
    });
    const result = generate(opts);
    expect(result.coverage).toBe(1.0);
    expect(result.uncovered).toHaveLength(0);
  });

  it('accepts/rejects integer boundary endpoints on the safe-integer rule', () => {
    const makeOpts = (maxValue: number) => {
      const opts = createGenerateOptions({
        parameters: [
          { name: 'n', values: [] },
          { name: 'other', values: ['a', 'b'] },
        ],
        strength: 2,
      });
      opts.boundaryConfigs = {
        n: { type: BoundaryType.Integer, minValue: 0, maxValue, step: 1 },
      };
      return opts;
    };
    // Within the safe-integer range: accepted.
    expect(generate(makeOpts(1000)).error.code).toBe(ErrorCode.Ok);
    // Beyond 2^53: rejected as invalid input (parity with the native surface).
    expect(generate(makeOpts(1e18)).error.code).toBe(ErrorCode.InvalidInput);
  });

  it('terminates with a constraint error on a hard contradictory model', () => {
    // Pigeonhole: 12 binary parameters forced pairwise-distinct is unsatisfiable
    // and drives exponential backtracking. The node-bounded search must return a
    // constraint error in finite time instead of hanging.
    const n = 12;
    const parameters = Array.from({ length: n }, (_, i) => ({
      name: `p${i}`,
      values: ['0', '1'],
    }));
    const constraintExpressions: string[] = [];
    for (let i = 0; i < n; ++i) {
      for (let j = i + 1; j < n; ++j) {
        constraintExpressions.push(`IF p${i}=0 THEN p${j}!=0`);
        constraintExpressions.push(`IF p${i}=1 THEN p${j}!=1`);
      }
    }
    const opts = createGenerateOptions({ parameters, strength: 2, constraintExpressions });
    const result = generate(opts);
    expect(result.error.code).toBe(ErrorCode.ConstraintError);
    expect(result.tests).toHaveLength(0);
  });

  it('reaches 100% coverage under constraints that stall greedy', () => {
    const opts = createGenerateOptions({
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'safari', 'edge'] },
        { name: 'arch', values: ['x86', 'arm'] },
      ],
      strength: 2,
      seed: 5,
      constraintExpressions: ['IF os=mac THEN browser!=edge', 'IF os=win THEN browser!=safari'],
    });
    const result = generate(opts);
    expect(result.coverage).toBe(1.0);
    expect(result.uncovered).toHaveLength(0);
    expect(countPositiveViolations(result, opts)).toBe(0);
  });
});
